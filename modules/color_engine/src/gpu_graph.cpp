#include "color/gpu_graph.hpp"
#include "color/extended_tools.hpp"
#include <array>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_map>

namespace color {
namespace {
using Plane=std::vector<float>;            // straight RGBA float32, 4 floats per pixel
using PlanePtr=std::shared_ptr<const Plane>;
void check(bool ok,const char* message) { if(!ok) throw std::invalid_argument(message); }
// Picks the smallest table size whose linear lookup error stays within 1e-4 (2e-4 contract minus margin).
// The error is measured against exact evaluate() on a dense probe grid. Steep curves need more samples;
// if even the largest table misses the target, throw rather than silently degrade.
std::uint32_t curve_samples_for(const Curves& c) {
    constexpr std::array<std::uint32_t,3> sizes{4096U,16384U,65536U};
    const std::array<const Curve*,4> curves{c.master.get(),c.red.get(),c.green.get(),c.blue.get()};
    for(auto n:sizes) {
        double worst=0;
        for(const Curve* curve:curves) {
            if(!curve) continue;
            std::vector<float> table(n);for(std::uint32_t i=0;i<n;++i) table[i]=curve->evaluate(static_cast<float>(i)/static_cast<float>(n-1));
            for(std::size_t k=0;k<=100000;++k) {
                const float x=static_cast<float>(k)/100000.F,t=x*static_cast<float>(n-1);
                const auto i=std::min<std::size_t>(static_cast<std::size_t>(t),n-2U);const float f=t-static_cast<float>(i);
                worst=std::max(worst,static_cast<double>(std::abs(table[i]*(1-f)+table[i+1]*f-curve->evaluate(x))));
            }
        }
        if(worst<=1e-4) return n;
    }
    throw std::invalid_argument("Curve needs more than 65536 GPU samples to meet the 2e-4 contract");
}
void unsupported(const char* what) { throw std::invalid_argument(std::string("GPU backend does not implement: ")+what); }

struct GradeCall {
    std::uint32_t op{};std::vector<float> params;std::vector<float> table;std::uint32_t edge{},curve_n{};
};
struct DispatchMeta {
    std::uint32_t count,width,op,frame,edge,curve_n,input_count,reserved;
    float key_input,key_output,node_mix;std::uint32_t invert;
};
static_assert(sizeof(DispatchMeta)==48);
struct SpatialMeta { std::uint32_t width,height,kind,radius; };
static_assert(sizeof(SpatialMeta)==16);

std::vector<float> matrix9(const Mat3& m) { std::vector<float> out;out.reserve(9);for(auto x:m.m) out.push_back(static_cast<float>(x));return out; }
void append(std::vector<float>& dst,Vec3 v) { dst.push_back(v.r);dst.push_back(v.g);dst.push_back(v.b); }
void append(std::vector<float>& dst,const std::vector<float>& src) { dst.insert(dst.end(),src.begin(),src.end()); }

GradeCall pack_grade(const Operator& operation,std::uint32_t frame_unused) {
    (void)frame_unused;
    return std::visit([](const auto& p)->GradeCall {
        using T=std::decay_t<decltype(p)>;GradeCall c;
        if constexpr(std::is_same_v<T,CST>) {
            c.op=0;const PreparedCST prepared(p);
            c.params={static_cast<float>(static_cast<unsigned>(p.input_transfer)),static_cast<float>(static_cast<unsigned>(p.output_transfer)),p.exposure_stops,p.tone_knee,p.gamut_compression};
            append(c.params,matrix9(prepared.matrix()));
        } else if constexpr(std::is_same_v<T,Primaries>) {
            c.op=1;append(c.params,p.lift);append(c.params,p.gamma);append(c.params,p.gain);append(c.params,p.offset);
            for(float x:{p.exposure,p.contrast,p.pivot,p.saturation,p.hue_radians,p.vibrance,p.temperature,p.tint,p.luminance_mix}) c.params.push_back(x);
        } else if constexpr(std::is_same_v<T,LogWheels>) {
            c.op=2;append(c.params,p.shadows);append(c.params,p.midtones);append(c.params,p.highlights);
            c.params.push_back(p.low);c.params.push_back(p.high);c.params.push_back(p.softness);
        } else if constexpr(std::is_same_v<T,HDRWheels>) {
            c.op=3;for(auto x:p.stops) c.params.push_back(x);for(auto x:p.centers) c.params.push_back(x);c.params.push_back(p.falloff);c.params.push_back(p.soft_clip);
        } else if constexpr(std::is_same_v<T,ColorSlice>) {
            c.op=4;for(auto v:p.hue_sat_density) append(c.params,v);c.params.push_back(p.width);append(c.params,p.shadow_tint);append(c.params,p.highlight_tint);
        } else if constexpr(std::is_same_v<T,RGBMixer>) {
            c.op=5;append(c.params,matrix9(p.matrix));c.params.push_back(p.monochrome?1.F:0.F);
        } else if constexpr(std::is_same_v<T,HSLQualifier>) {
            c.op=6;append(c.params,p.center);append(c.params,p.width);append(c.params,p.softness);c.params.push_back(p.invert?1.F:0.F);
        } else if constexpr(std::is_same_v<T,PowerWindow>) {
            c.op=7;c.params={static_cast<float>(static_cast<unsigned>(p.shape)),p.cx,p.cy,p.sx,p.sy,p.rotation,p.feather,p.invert?1.F:0.F,p.corner_radius};
        } else if constexpr(std::is_same_v<T,Curves>) {
            c.op=8;c.curve_n=curve_samples_for(p);
            const auto sample=[](const std::shared_ptr<const Curve>& curve,float t) { return curve?curve->evaluate(t):t; };
            c.params={p.master?1.F:0.F,p.red?1.F:0.F,p.green?1.F:0.F,p.blue?1.F:0.F};
            c.table.reserve(c.curve_n*4);
            for(std::uint32_t i=0;i<c.curve_n;++i) {
                const float t=static_cast<float>(i)/static_cast<float>(c.curve_n-1);
                c.table.push_back(sample(p.master,t));c.table.push_back(sample(p.red,t));c.table.push_back(sample(p.green,t));c.table.push_back(sample(p.blue,t));
            }
        } else if constexpr(std::is_same_v<T,std::shared_ptr<const Lut3D>>) {
            check(static_cast<bool>(p),"Missing 3D LUT");c.op=9;c.edge=static_cast<std::uint32_t>(p->edge());
            append(c.params,p->domain_min());append(c.params,p->domain_max());
            for(auto v:p->values()) { c.table.push_back(v.r);c.table.push_back(v.g);c.table.push_back(v.b);c.table.push_back(0); }
        } else if constexpr(std::is_same_v<T,Grain>) {
            c.op=10;c.params={p.amount,p.colored,std::bit_cast<float>(p.seed)};
        } else if constexpr(std::is_same_v<T,RawDecodeParams>) {
            c.op=16;append(c.params,p.black_level);append(c.params,p.white_level);append(c.params,p.white_balance);c.params.push_back(p.exposure_stops);append(c.params,matrix9(p.camera_to_working));
        } else if constexpr(std::is_same_v<T,PolygonWindow>) {
            c.op=17;c.params.assign(67,0.F);c.params[0]=static_cast<float>(p.count);c.params[1]=p.feather;c.params[2]=p.invert?1.F:0.F;
            for(std::size_t i=0;i<p.count;++i) { c.params[3+2*i]=p.vertices[i].x;c.params[4+2*i]=p.vertices[i].y; }
        } else if constexpr(std::is_same_v<T,std::shared_ptr<const Lut1D>>) {
            check(static_cast<bool>(p),"Missing 1D LUT");c.op=22;c.edge=static_cast<std::uint32_t>(p->size());
            append(c.params,p->domain_min());append(c.params,p->domain_max());
            for(auto v:p->values()) { c.table.push_back(v.r);c.table.push_back(v.g);c.table.push_back(v.b);c.table.push_back(0); }
        } else {
            unsupported("operator without a bundled GPU kernel (spatial/external handled separately)");
        }
        return c;
    },operation);
}

class Dispatcher {
public:
    Dispatcher(IVulkanCompute& gpu,const GpuKernels& kernels,std::uint32_t count,std::uint32_t width,std::uint32_t frame)
        :gpu_(gpu),kernels_(kernels),count_(count),width_(width),frame_(frame) {}
    // Grade-style kernel: inputs are contiguous planes; output has the same extent.
    Plane grade(const GradeCall& call,const std::vector<PlanePtr>& inputs,std::uint32_t input_count,float key_in,float key_out,float mix,bool invert) const {
        check(!inputs.empty()&&inputs.size()<=16,"Invalid input plane count");
        Plane gathered;gathered.reserve(inputs.size()*count_*4);
        for(std::size_t j=0;j<inputs.size();++j) { check(inputs[j]&&inputs[j]->size()==static_cast<std::size_t>(count_)*4,"Plane extent mismatch"); gathered.insert(gathered.end(),inputs[j]->begin(),inputs[j]->end()); }
        Plane output(static_cast<std::size_t>(count_)*4,0.F);
        Plane params=call.params;if(params.empty()) params.push_back(0.F);
        Plane table=call.table;if(table.size()<4) table.assign(4,0.F);
        DispatchMeta meta{count_,width_,call.op,frame_,call.edge,call.curve_n,input_count,0,key_in,key_out,mix,invert?1U:0U};
        std::array<ComputeBuffer,4> buffers{{{std::as_writable_bytes(std::span(gathered)),BufferAccess::Input},
                                             {std::as_writable_bytes(std::span(output)),BufferAccess::Output},
                                             {std::as_writable_bytes(std::span(params)),BufferAccess::Input},
                                             {std::as_writable_bytes(std::span(table)),BufferAccess::Input}}};
        gpu_.dispatch({kernels_.grade_spirv,buffers,std::as_bytes(std::span(&meta,1)),count_});
        return output;
    }
    Plane spatial(const SpatialFilter& f,const Plane& source,const Plane* history) const {
        Plane zero;if(!history) zero.assign(static_cast<std::size_t>(count_)*4,0.F);
        const Plane& h=history?*history:zero;
        Plane output(static_cast<std::size_t>(count_)*4,0.F);
        Plane params{f.spatial_sigma,f.range_sigma,f.amount,f.threshold,f.airlight.r,f.airlight.g,f.airlight.b,f.transmission_floor};
        Plane sourcecopy=source;Plane historycopy=h;
        SpatialMeta meta{width_,count_/width_,static_cast<std::uint32_t>(f.kind),static_cast<std::uint32_t>(f.radius)};
        std::array<ComputeBuffer,4> buffers{{{std::as_writable_bytes(std::span(sourcecopy)),BufferAccess::Input},
                                             {std::as_writable_bytes(std::span(historycopy)),BufferAccess::Input},
                                             {std::as_writable_bytes(std::span(output)),BufferAccess::Output},
                                             {std::as_writable_bytes(std::span(params)),BufferAccess::Input}}};
        gpu_.dispatch({kernels_.spatial_spirv,buffers,std::as_bytes(std::span(&meta,1)),count_});
        return output;
    }
private:
    IVulkanCompute& gpu_;const GpuKernels& kernels_;std::uint32_t count_,width_,frame_;
};

std::uint32_t grade_kind_count(const Node& n) { return static_cast<std::uint32_t>(n.inputs.size()); }

} // namespace

GpuGradeEngine::GpuGradeEngine(IVulkanCompute& gpu,GpuKernels kernels,std::size_t budget)
    :gpu_(gpu),kernels_(std::move(kernels)),budget_(budget) {
    check(!kernels_.grade_spirv.empty()&&!kernels_.spatial_spirv.empty()&&budget>=4*sizeof(float),"Invalid GPU kernels/budget");
}

void GpuGradeEngine::RenderGradeFrame(const GradeProgram& program,ColorFrameBuffer32F input,ColorFrameBuffer32F output,const GradeContext& context) const {
    check(input.data&&output.data&&input.width&&input.height&&input.width==output.width&&input.height==output.height,"GPU render frame extent mismatch");
    check(input.row_stride_floats>=input.width*4&&output.row_stride_floats>=output.width*4,"GPU frame stride");
    check(std::isfinite(context.timeline_time_sec)&&std::abs(context.timeline_time_sec)<=1e9,"Invalid timeline time");
    const std::size_t count=input.width*input.height,bytes=count*4*sizeof(float);
    check(count<=std::numeric_limits<std::uint32_t>::max()&&input.width<=std::numeric_limits<std::uint32_t>::max(),"GPU dispatch exceeds 32-bit index range");
    check(bytes<=budget_/2,"GPU frame exceeds memory budget");
    Plane source(count*4);
    for(std::size_t y=0;y<input.height;++y) std::memcpy(source.data()+y*input.width*4,reinterpret_cast<const std::byte*>(input.data)+y*input.row_stride_floats*sizeof(float),input.width*4*sizeof(float));
    for(std::size_t i=0;i<count;++i) { check(std::isfinite(source[4*i])&&std::isfinite(source[4*i+1])&&std::isfinite(source[4*i+2])&&std::isfinite(source[4*i+3])&&source[4*i+3]>=0&&source[4*i+3]<=1,"Nonfinite frame or alpha outside 0..1"); }
    const auto frame=static_cast<std::uint32_t>(context.frame_index);
    const Dispatcher dispatch(gpu_,kernels_,static_cast<std::uint32_t>(count),static_cast<std::uint32_t>(input.width),frame);
    PlanePtr history_plane;
    if(context.temporal_history) {
        const auto& h=*context.temporal_history;check(h.width()==input.width&&h.height()==input.height,"Temporal history dimensions mismatch");
        Plane copy(count*4);for(std::size_t k=0;k<count;++k) { const auto q=h.pixels()[k];check(std::isfinite(q.r)&&std::isfinite(q.g)&&std::isfinite(q.b)&&std::isfinite(q.a),"Invalid temporal history");copy[4*k]=q.r;copy[4*k+1]=q.g;copy[4*k+2]=q.b;copy[4*k+3]=q.a; }
        history_plane=std::make_shared<const Plane>(std::move(copy));
    }
    PlanePtr stage_input=std::make_shared<const Plane>(std::move(source));
    const std::array<const NodeGraph*,4> graphs{&program.group_pre,&program.clip,&program.group_post,&program.timeline};
    std::size_t live_planes=1;
    for(const auto* graph:graphs) {
        const auto sorted=graph->compile();
        if(sorted.empty()) continue;
        std::unordered_map<std::uint64_t,std::size_t> index;for(std::size_t i=0;i<sorted.size();++i) index.emplace(sorted[i].id,i);
        std::vector<std::size_t> uses(sorted.size(),0);
        for(const auto& n:sorted) for(auto id:n.inputs) ++uses[index.at(id)];
        const auto out_index=index.at(graph->output());++uses[out_index];
        std::vector<PlanePtr> planes(sorted.size());
        for(std::size_t i=0;i<sorted.size();++i) {
            const Node& n=sorted[i];
            std::vector<PlanePtr> in;
            if(n.inputs.empty()) in.push_back(stage_input);
            else for(auto id:n.inputs) in.push_back(planes[index.at(id)]);
            const PlanePtr base=in[0];
            PlanePtr result=base;
            if(!n.bypass) {
                const float key_in=n.key_input_gain,key_out=n.key_output_gain,mix=n.mix;const bool invert=n.key_invert;
                auto blend_needed=[&] { return !(key_in*key_out==1.F&&mix==1.F&&!invert); };
                Plane raw;bool has_raw=false;
                switch(n.kind) {
                case NodeKind::MatteUnion:case NodeKind::MatteIntersect:case NodeKind::MatteSubtract: {
                    check(in.size()==2,"Matte algebra requires two inputs");
                    const std::uint32_t op=n.kind==NodeKind::MatteUnion?18U:(n.kind==NodeKind::MatteIntersect?19U:20U);
                    GradeCall call;call.op=op;raw=dispatch.grade(call,in,2,1,1,1,false);has_raw=true;break; }
                case NodeKind::Parallel: { GradeCall call;call.op=11;raw=dispatch.grade(call,in,grade_kind_count(n),1,1,1,false);has_raw=true;break; }
                case NodeKind::LayerMixer: { GradeCall call;call.op=12;raw=dispatch.grade(call,in,grade_kind_count(n),1,1,1,false);has_raw=true;break; }
                case NodeKind::Channel: { GradeCall call;call.op=14;call.params={static_cast<float>(n.channel)};raw=dispatch.grade(call,in,1,1,1,1,false);has_raw=true;break; }
                case NodeKind::Combiner: { GradeCall call;call.op=15;raw=dispatch.grade(call,in,3,1,1,1,false);has_raw=true;break; }
                case NodeKind::Outside: {
                    check(in.size()==2,"Outside requires base and matte");
                    PlanePtr graded=base;
                    if(!std::holds_alternative<std::monostate>(n.operation)) {
                        if(std::holds_alternative<SpatialFilter>(n.operation)||std::holds_alternative<ExternalPlane>(n.operation)) unsupported("spatial/external operator inside Outside on GPU");
                        const auto call=pack_grade(n.operation,frame);graded=std::make_shared<const Plane>(dispatch.grade(call,{base},1,1,1,1,false));
                    }
                    GradeCall call;call.op=13;raw=dispatch.grade(call,{base,in[1],graded},3,1,1,1,false);has_raw=true;break; }
                default: {
                    if(auto spatial=std::get_if<SpatialFilter>(&n.operation)) {
                        check(n.kind==NodeKind::Serial,"Spatial filters require serial nodes");
                        validate_spatial(*spatial);check(spatial->kind!=SpatialKind::TemporalDenoise||context.temporal_history!=nullptr,"Temporal filter requires history");
                        raw=dispatch.spatial(*spatial,*base,history_plane.get());has_raw=true;
                    } else if(auto external=std::get_if<ExternalPlane>(&n.operation)) {
                        check(external->image&&external->image->width()==input.width&&external->image->height()==input.height,"External plane dimensions mismatch");
                        raw.resize(count*4);
                        for(std::size_t k=0;k<count;++k) { const auto q=external->image->pixels()[k];check(std::isfinite(q.r)&&std::isfinite(q.g)&&std::isfinite(q.b)&&std::isfinite(q.a)&&q.a>=0&&q.a<=1,"Invalid external plane pixel");raw[4*k]=q.r;raw[4*k+1]=q.g;raw[4*k+2]=q.b;raw[4*k+3]=q.a; }
                        has_raw=true;
                    } else if(std::holds_alternative<std::monostate>(n.operation)) {
                        has_raw=false;
                    } else {
                        const auto call=pack_grade(n.operation,frame);raw=dispatch.grade(call,{base},1,1,1,1,false);has_raw=true;
                    }
                    break; }
                }
                if(has_raw) {
                    auto raw_ptr=std::make_shared<const Plane>(std::move(raw));
                    if(blend_needed()) {
                        GradeCall call;call.op=21;
                        raw_ptr=std::make_shared<const Plane>(dispatch.grade(call,{base,raw_ptr},2,key_in,key_out,mix,invert));
                    }
                    result=raw_ptr;
                }
            }
            planes[i]=result;
            ++live_planes;
            check((live_planes)*bytes<=budget_,"GPU live planes exceed memory budget");
            for(auto id:n.inputs) if(--uses[index.at(id)]==0) planes[index.at(id)].reset();
            if(uses[i]==0) planes[i].reset();
        }
        stage_input=planes[out_index];
        live_planes=1;
    }
    check(stage_input&&stage_input->size()==count*4,"GPU output missing");
    for(std::size_t y=0;y<output.height;++y) std::memcpy(reinterpret_cast<std::byte*>(output.data)+y*output.row_stride_floats*sizeof(float),stage_input->data()+y*output.width*4,output.width*4*sizeof(float));
}
} // namespace color

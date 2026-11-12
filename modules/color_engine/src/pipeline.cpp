#include "color/color_pipeline_api.hpp"
#include "color/extended_tools.hpp"
#include <algorithm>
#include <atomic>
#include <optional>
#include <utility>
#include <mutex>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <new>
#include <stdexcept>
#include <unordered_map>
#include <type_traits>

namespace color {
namespace {
std::atomic<std::uint64_t> next_revision{1};
void check(bool b,const char* s) { if(!b) throw std::invalid_argument(s); }
std::size_t checked_mul(std::size_t a,std::size_t b) { if(b&&a>std::numeric_limits<std::size_t>::max()/b) throw std::overflow_error("Image extent overflow");return a*b; }
void validate(ColorFrameBuffer32F f) {
    check(f.data&&f.width&&f.height&&reinterpret_cast<std::uintptr_t>(f.data)%alignof(float)==0,"Invalid float frame pointer/extent");
    const auto row=checked_mul(f.width,4);check(f.row_stride_floats>=row,"Invalid float row stride");
    const auto head=checked_mul(f.height-1,f.row_stride_floats);
    check(head<=std::numeric_limits<std::size_t>::max()-row,"Frame span overflow");
    const auto span=head+row;check(span<=f.capacity_floats,"Frame exceeds supplied capacity");
    const auto bytes=checked_mul(span,sizeof(float));check(reinterpret_cast<std::uintptr_t>(f.data)<=std::numeric_limits<std::uintptr_t>::max()-bytes,"Frame address overflow");
}
Vec3 rgb(Pixel p) { return {p.r,p.g,p.b}; }
Pixel rgba(Vec3 v,float a) { return {v.r,v.g,v.b,a}; }
Pixel mix(Pixel a,Pixel b,float t) { if(t==0) return a;if(t==1) return b;const float s=1-t;return {a.r*s+b.r*t,a.g*s+b.g*t,a.b*s+b.b*t,a.a*s+b.a*t}; }
Pixel over(Pixel a,Pixel b) { // b is foreground; straight-alpha buffers
    const float ba=std::clamp(b.a,0.F,1.F),aa=std::clamp(a.a,0.F,1.F),out=ba+aa*(1-ba);
    if(out==0) return {0,0,0,0};
    return {(b.r*ba+a.r*aa*(1-ba))/out,(b.g*ba+a.g*aa*(1-ba))/out,(b.b*ba+a.b*aa*(1-ba))/out,out};
}
Pixel operate(Pixel p,const Operator& op,float x,float y,std::uint32_t px,std::uint32_t py,std::uint32_t frame,const PreparedCST* prepared=nullptr) {
    return std::visit([&](const auto& v)->Pixel {
        using T=std::decay_t<decltype(v)>;
        if constexpr(std::is_same_v<T,std::monostate>) return p;
        else if constexpr(std::is_same_v<T,CST>) return rgba(prepared?prepared->apply(rgb(p)):transform(rgb(p),v),p.a);
        else if constexpr(std::is_same_v<T,HSLQualifier>) return rgba(rgb(p),p.a*qualify(rgb(p),v));
        else if constexpr(std::is_same_v<T,SpatialFilter>) throw std::invalid_argument("Spatial operation requires materialized execution");
        else if constexpr(std::is_same_v<T,std::shared_ptr<const Lut1D>>) { check(static_cast<bool>(v),"Missing 1D LUT asset");return rgba(v->apply(rgb(p)),p.a); }
        else if constexpr(std::is_same_v<T,ExternalPlane>) throw std::invalid_argument("External planes require materialized execution");
        else if constexpr(std::is_same_v<T,PolygonWindow>) return rgba(rgb(p),p.a*polygon_mask(x,y,v));
        else if constexpr(std::is_same_v<T,PowerWindow>) return rgba(rgb(p),p.a*window_mask(x,y,v));
        else if constexpr(std::is_same_v<T,std::shared_ptr<const Lut3D>>) { check(static_cast<bool>(v),"Missing LUT asset");return rgba(v->tetrahedral(rgb(p)),p.a); }
        else if constexpr(std::is_same_v<T,Grain>) return rgba(grain(rgb(p),px,py,frame,v),p.a);
        else return rgba(apply(rgb(p),v),p.a);
    },op);
}
}
void Image32F::Deleter::operator()(Pixel* p) const noexcept { ::operator delete[](p,std::align_val_t{64}); }
Image32F::Image32F(std::size_t w,std::size_t h,std::size_t budget):width_(w),height_(h) {
    check(w&&h,"Empty image");const auto n=checked_mul(w,h),bytes=checked_mul(n,sizeof(Pixel));check(bytes<=budget,"Image exceeds memory budget");
    data_.reset(static_cast<Pixel*>(::operator new[](bytes,std::align_val_t{64})));std::uninitialized_value_construct_n(data_.get(),n);
}
Image32F::~Image32F()=default;Image32F::Image32F(Image32F&& other) noexcept:data_(std::move(other.data_)),width_(std::exchange(other.width_,0)),height_(std::exchange(other.height_,0)) {}
Image32F& Image32F::operator=(Image32F&& other) noexcept { if(this!=&other) { data_=std::move(other.data_);width_=std::exchange(other.width_,0);height_=std::exchange(other.height_,0); }return *this; }
NodeGraph& NodeGraph::operator=(const NodeGraph& other) { if(this!=&other) { NodeGraph copy(other);*this=std::move(copy); }return *this; }
NodeGraph::NodeGraph(NodeGraph&& other) noexcept:nodes_(std::move(other.nodes_)),revision_(std::exchange(other.revision_,0)),output_id_(std::exchange(other.output_id_,0)) { other.nodes_.clear(); }
NodeGraph& NodeGraph::operator=(NodeGraph&& other) noexcept { if(this!=&other) { nodes_=std::move(other.nodes_);revision_=std::exchange(other.revision_,0);output_id_=std::exchange(other.output_id_,0);other.nodes_.clear(); }return *this; }
std::size_t Image32F::width() const noexcept { return width_; } std::size_t Image32F::height() const noexcept { return height_; }
std::span<Pixel> Image32F::pixels() { return {data_.get(),width_*height_}; } std::span<const Pixel> Image32F::pixels() const { return {data_.get(),width_*height_}; }
ColorFrameBuffer32F Image32F::view() { return {reinterpret_cast<float*>(data_.get()),width_,height_,width_*4,width_*height_*4}; }
void NodeGraph::add(Node n) {
    check(nodes_.size()<256,"Graph node budget256");check(n.id!=0,"Node id0 is reserved");
    check(std::none_of(nodes_.begin(),nodes_.end(),[&](const Node& a){return a.id==n.id;}),"Duplicate node id");nodes_.push_back(std::move(n));revision_=next_revision.fetch_add(1,std::memory_order_relaxed);
}
void NodeGraph::replace(Node n) {
    const auto it=std::find_if(nodes_.begin(),nodes_.end(),[&](const Node& a){return a.id==n.id;});check(it!=nodes_.end()&&!it->locked,"Missing/locked node");*it=std::move(n);revision_=next_revision.fetch_add(1,std::memory_order_relaxed);
}
void NodeGraph::set_locked(std::uint64_t id,bool value) { const auto it=std::find_if(nodes_.begin(),nodes_.end(),[&](const Node& n){return n.id==id;});check(it!=nodes_.end(),"Missing node");it->locked=value;revision_=next_revision.fetch_add(1,std::memory_order_relaxed); }
std::uint64_t NodeGraph::revision() const noexcept { return revision_; }
void NodeGraph::set_output(std::uint64_t id) { check(id==0||std::any_of(nodes_.begin(),nodes_.end(),[&](const Node& n){return n.id==id;}),"Missing selected output");output_id_=id;revision_=next_revision.fetch_add(1,std::memory_order_relaxed); }
std::uint64_t NodeGraph::output() const noexcept { return output_id_?output_id_:(nodes_.empty()?0:nodes_.back().id); }
std::vector<Node> NodeGraph::snapshot_nodes() const { return nodes_; }
bool NodeGraph::has_explicit_output() const noexcept { return output_id_!=0; }
std::vector<Node> NodeGraph::compile() const {
    std::unordered_map<std::uint64_t,std::size_t> by_id;
    for(std::size_t i=0;i<nodes_.size();++i) {
        const auto& n=nodes_[i];by_id.emplace(n.id,i);
        check(n.inputs.size()<=16&&n.label.size()<=256&&n.mix>=0&&n.mix<=1&&n.key_input_gain>=0&&n.key_input_gain<=1&&n.key_output_gain>=0&&n.key_output_gain<=1,"Invalid node metadata/key budget");
        if(n.kind==NodeKind::Parallel||n.kind==NodeKind::LayerMixer||n.kind==NodeKind::Channel||n.kind==NodeKind::Combiner||n.kind==NodeKind::MatteUnion||n.kind==NodeKind::MatteIntersect||n.kind==NodeKind::MatteSubtract)
            check(std::holds_alternative<std::monostate>(n.operation),"Composition/split nodes do not accept an ignored grading operator");
        switch(n.kind) {
        case NodeKind::Parallel:case NodeKind::LayerMixer:check(n.inputs.size()>=2,"Mixer requires2 inputs");break;
        case NodeKind::MatteUnion:case NodeKind::MatteIntersect:case NodeKind::MatteSubtract:check(n.inputs.size()==2,"Matte algebra requires2 inputs");break;
        case NodeKind::Outside:check(n.inputs.size()==2,"Outside requires base and alpha matte");break;
        case NodeKind::Combiner:check(n.inputs.size()==3,"RGB combiner requires3 inputs");break;
        case NodeKind::Channel:check(n.channel<3&&n.inputs.size()<=1,"Invalid channel splitter");break;
        case NodeKind::Serial:case NodeKind::SharedNode:case NodeKind::GroupPreClip:case NodeKind::GroupPostClip:case NodeKind::TimelineNode:check(n.inputs.size()<=1,"Serial operation accepts at most1 input");break;
        default:throw std::invalid_argument("Unknown node kind");
        }
    }
    std::vector<unsigned char> state(nodes_.size());std::vector<Node> out;out.reserve(nodes_.size());
    std::function<void(std::size_t)> visit=[&](std::size_t i) {
        check(state[i]!=1,"Color graph cycle");if(state[i]==2) return;state[i]=1;
        for(auto id:nodes_[i].inputs) { const auto it=by_id.find(id);check(it!=by_id.end(),"Missing graph edge");visit(it->second); }
        state[i]=2;out.push_back(nodes_[i]);
    };
    for(std::size_t i=0;i<nodes_.size();++i) visit(i);
    return out;
}
std::vector<LayerListView> NodeGraph::layers() const { const auto ordered=compile();std::vector<LayerListView> out;for(std::size_t i=0;i<ordered.size();++i) out.push_back({ordered[i].id,ordered[i].label,i,ordered[i].locked,ordered[i].bypass,ordered[i].mix});return out; }
namespace {
struct CompiledNode { Node node;std::vector<std::size_t> inputs;std::optional<PreparedCST> cst; };
struct Compiled {
    std::vector<CompiledNode> nodes;std::size_t output_index{};
    std::size_t size() const { return nodes.size(); }bool empty() const { return nodes.empty(); }
    const CompiledNode& operator[](std::size_t i) const { return nodes[i]; }
};
Compiled compile(const NodeGraph& graph) {
    const auto sorted=graph.compile();Compiled out;out.nodes.reserve(sorted.size());std::unordered_map<std::uint64_t,std::size_t> indices;
    for(const auto& n:sorted) { CompiledNode c{n,{},{}};if(auto params=std::get_if<CST>(&n.operation)) c.cst.emplace(*params);for(auto id:n.inputs) c.inputs.push_back(indices.at(id));indices.emplace(n.id,out.size());out.nodes.push_back(std::move(c)); }
    if(!sorted.empty()) out.output_index=indices.at(graph.output());
    return out;
}
Pixel evaluate_one(Pixel input,const CompiledNode& c,const std::vector<Pixel>& values,float x,float y,std::uint32_t px,std::uint32_t py,std::uint32_t frame,const Image32F* spatial_source=nullptr,const Image32F* history=nullptr) {
const auto& n=c.node;const Pixel base=c.inputs.empty()?input:values[c.inputs[0]];Pixel result=base;
        if(!n.bypass) {
            switch(n.kind) {
            case NodeKind::MatteUnion:result.a=std::max(base.a,values[c.inputs[1]].a);break;
            case NodeKind::MatteIntersect:result.a=std::min(base.a,values[c.inputs[1]].a);break;
            case NodeKind::MatteSubtract:result.a=std::max(0.F,base.a-values[c.inputs[1]].a);break;
            case NodeKind::Parallel:{ result={0,0,0,0};for(auto k:c.inputs) { const auto p=values[k];result.r+=p.r;result.g+=p.g;result.b+=p.b;result.a+=p.a; }const float scale=1/static_cast<float>(c.inputs.size());result={result.r*scale,result.g*scale,result.b*scale,result.a*scale};break; }
            case NodeKind::LayerMixer:for(std::size_t j=1;j<c.inputs.size();++j) result=over(result,values[c.inputs[j]]);break;
            case NodeKind::Channel:{ const std::array<float,3> channels{base.r,base.g,base.b};result={channels[n.channel],channels[n.channel],channels[n.channel],base.a};break; }
            case NodeKind::Combiner:result={values[c.inputs[0]].r,values[c.inputs[1]].g,values[c.inputs[2]].b,base.a};break;
            case NodeKind::Outside:result=mix(base,operate(base,n.operation,x,y,px,py,frame,c.cst?&*c.cst:nullptr),1-std::clamp(values[c.inputs[1]].a,0.F,1.F));break;
            default:if(auto spatial=std::get_if<SpatialFilter>(&n.operation)) {
                check(spatial_source!=nullptr,"Missing spatial source");result=spatial_pixel(*spatial_source,px,py,*spatial,history);
            } else result=operate(base,n.operation,x,y,px,py,frame,c.cst?&*c.cst:nullptr);break;
            }
            float key=n.key_input_gain*n.key_output_gain;if(n.key_invert) key=1-key;
            result=mix(base,result,n.mix*key);
        }
        check(finite(result),"Nonfinite grade output; destination unchanged");return result;
}
Pixel evaluate(Pixel input,const Compiled& graph,std::vector<Pixel>& values,float x,float y,std::uint32_t px,std::uint32_t py,std::uint32_t frame) {
    for(std::size_t i=0;i<graph.size();++i) values[i]=evaluate_one(input,graph[i],values,x,y,px,py,frame);
    return graph.empty()?input:values[graph.output_index];
}
class FramePool {
public:
    FramePool(std::size_t w,std::size_t h,std::size_t slots):w_(w),h_(h),limit_(slots) {}
    std::unique_ptr<Image32F> acquire() {
        if(!free_.empty()) { auto result=std::move(free_.back());free_.pop_back();return result; }
        check(allocated_<limit_,"Spatial DAG live frames exceed memory budget");auto result=std::make_unique<Image32F>(w_,h_,w_*h_*sizeof(Pixel));++allocated_;return result;
    }
    void recycle(std::unique_ptr<Image32F> frame) { if(frame) free_.push_back(std::move(frame)); }
private: std::size_t w_,h_,limit_,allocated_{};std::vector<std::unique_ptr<Image32F>> free_;
};
void materialized(const Image32F& source,Image32F& destination,const std::array<std::shared_ptr<const Compiled>,4>& stages,std::size_t budget,const GradeContext& context) {
    const auto count=source.pixels().size(),bytes=count*sizeof(Pixel);FramePool pool(source.width(),source.height(),(budget-2*bytes)/bytes);
    std::unique_ptr<Image32F> stage_result;const Image32F* input=&source;
    for(const auto& stage:stages) {
        const auto& g=*stage;if(g.empty()) continue;
        std::vector<std::unique_ptr<Image32F>> images(g.size());std::vector<std::size_t> uses(g.size());std::vector<Pixel> values(g.size());
        for(const auto& c:g.nodes) { for(auto edge:c.inputs) ++uses[edge]; }
        ++uses[g.output_index];
        for(std::size_t i=0;i<g.size();++i) {
            const auto& c=g[i];if(auto p=std::get_if<SpatialFilter>(&c.node.operation)) { validate_spatial(*p);check(c.node.kind==NodeKind::Serial,"Spatial filters require serial nodes"); }
            images[i]=pool.acquire();const Image32F* spatial_source=c.inputs.empty()?input:images[c.inputs[0]].get();
            if(auto external=std::get_if<ExternalPlane>(&c.node.operation)) {
                check(external->image&&external->image->width()==source.width()&&external->image->height()==source.height(),"External plane dimensions mismatch");
                for(std::size_t k=0;k<source.pixels().size();++k) { const auto q=external->image->pixels()[k];check(finite(q)&&q.a>=0&&q.a<=1,"Invalid external plane pixel");images[i]->pixels()[k]=q; }
                for(auto edge:c.inputs) if(--uses[edge]==0) pool.recycle(std::move(images[edge]));
                if(uses[i]==0) pool.recycle(std::move(images[i]));
                continue;
            }
            for(std::size_t y=0;y<source.height();++y) for(std::size_t x=0;x<source.width();++x) {
                const auto index=y*source.width()+x;for(auto edge:c.inputs) values[edge]=images[edge]->pixels()[index];
                images[i]->pixels()[index]=evaluate_one(input->pixels()[index],c,values,(static_cast<float>(x)+.5F)/static_cast<float>(source.width()),(static_cast<float>(y)+.5F)/static_cast<float>(source.height()),static_cast<std::uint32_t>(x),static_cast<std::uint32_t>(y),static_cast<std::uint32_t>(context.frame_index),spatial_source,context.temporal_history);
            }
            for(auto edge:c.inputs) if(--uses[edge]==0) pool.recycle(std::move(images[edge]));
            if(uses[i]==0) pool.recycle(std::move(images[i]));
        }
        pool.recycle(std::move(stage_result));stage_result=std::move(images[g.output_index]);input=stage_result.get();
    }
    std::copy(input->pixels().begin(),input->pixels().end(),destination.pixels().begin());
}
class Engine final:public IColorEngine {
public:
    explicit Engine(EngineOptions options):options_(options) { check(options.tile_edge>=1&&options.tile_edge<=1024&&options.memory_budget_bytes>=sizeof(Pixel)*2,"Invalid engine budget/tile"); }
    std::string_view backend_name() const noexcept override { return "CPUReference32F"; }
    void RenderGradeFrame(const GradeProgram& program,ColorFrameBuffer32F input,ColorFrameBuffer32F output,const GradeContext& context) const override {
        validate(input);validate(output);check(input.width==output.width&&input.height==output.height,"Color rendering does not silently resize");check(std::isfinite(context.timeline_time_sec)&&std::abs(context.timeline_time_sec)<=1e9,"Invalid timeline time");
        const auto count=checked_mul(input.width,input.height),bytes=checked_mul(count,sizeof(Pixel));check(bytes<=options_.memory_budget_bytes/2,"Frame scratch exceeds memory budget");
        const std::array<std::shared_ptr<const Compiled>,4> stages{cached(program.group_pre),cached(program.clip),cached(program.group_post),cached(program.timeline)};
        std::size_t max_nodes=0;for(const auto& g:stages) max_nodes=std::max(max_nodes,g->size());std::vector<Pixel> values(max_nodes);
        Image32F source(input.width,input.height,bytes),destination(input.width,input.height,bytes);
        for(std::size_t y=0;y<input.height;++y) std::memcpy(source.pixels().data()+y*input.width,reinterpret_cast<const std::byte*>(input.data)+y*input.row_stride_floats*sizeof(float),input.width*sizeof(Pixel));
        for(auto p:source.pixels()) check(finite(p)&&p.a>=0&&p.a<=1,"Nonfinite frame/alpha out of0..1");
        bool needs_spatial=false;
        for(const auto& stage:stages) for(const auto& c:stage->nodes) if(std::holds_alternative<SpatialFilter>(c.node.operation)||std::holds_alternative<ExternalPlane>(c.node.operation)) needs_spatial=true;
        if(context.temporal_history) {
            check(context.temporal_history->width()==input.width&&context.temporal_history->height()==input.height,"Temporal history dimensions mismatch");
            for(auto p:context.temporal_history->pixels()) check(finite(p)&&p.a>=0&&p.a<=1,"Invalid temporal history");
        }
        if(needs_spatial) materialized(source,destination,stages,options_.memory_budget_bytes,context);
        else {
        // No full-frame allocation per DAG node. Intermediate values are reused per pixel.
        const auto frame=static_cast<std::uint32_t>(context.frame_index);
        for(std::size_t ty=0;ty<input.height;ty+=options_.tile_edge) for(std::size_t tx=0;tx<input.width;tx+=options_.tile_edge)
            for(std::size_t y=ty;y<std::min(input.height,ty+options_.tile_edge);++y) for(std::size_t x=tx;x<std::min(input.width,tx+options_.tile_edge);++x) {
                Pixel p=source.pixels()[y*input.width+x];for(const auto& g:stages) p=evaluate(p,*g,values,(static_cast<float>(x)+.5F)/static_cast<float>(input.width),(static_cast<float>(y)+.5F)/static_cast<float>(input.height),static_cast<std::uint32_t>(x),static_cast<std::uint32_t>(y),frame);
                destination.pixels()[y*input.width+x]=p;
            }
        }
        // Failure atomicity also makes overlapping/in-place input/output safe. Padding untouched.
        for(std::size_t y=0;y<output.height;++y) std::memcpy(reinterpret_cast<std::byte*>(output.data)+y*output.row_stride_floats*sizeof(float),destination.pixels().data()+y*output.width,output.width*sizeof(Pixel));
    }
    VideoScopes ComputeVideoScopes(ColorFrameBuffer32F input,const ScopeConfig& c) const override {
        validate(input);check(c.bins>=2&&c.bins<=4096&&c.waveform_columns>=1&&c.waveform_columns<=4096&&std::isfinite(c.low)&&std::isfinite(c.high)&&c.high>c.low,"Invalid scope dimensions/range");
        const auto grid=checked_mul(c.bins,c.bins),wave=checked_mul(c.bins,c.waveform_columns);
        const auto entries=4*c.bins+4*wave+2*grid;check(checked_mul(entries,sizeof(std::uint64_t))<=options_.memory_budget_bytes,"Scope exceeds memory budget");
        VideoScopes s;for(auto& a:s.histogram) a.resize(c.bins);for(auto& a:s.parade) a.resize(wave);s.waveform.resize(wave);s.vectorscope.resize(grid);s.cie_xy.resize(grid);
        const auto bin=[&](float v) { check(!std::isnan(v),"Invalid scope intermediate");const double t=std::clamp((static_cast<double>(v)-c.low)/(static_cast<double>(c.high)-c.low),0.,1.);return std::min(static_cast<std::size_t>(t*static_cast<double>(c.bins)),c.bins-1); };
        const auto matrix=rgb_to_xyz(c.gamut);
        for(std::size_t y=0;y<input.height;++y) for(std::size_t x=0;x<input.width;++x) {
            Pixel p;std::memcpy(&p,reinterpret_cast<const std::byte*>(input.data)+(y*input.row_stride_floats+4*x)*sizeof(float),sizeof(Pixel));check(finite(p),"Nonfinite scope input");const Vec3 v=rgb(p);const float l=luminance(v);const std::array<float,4> channels{p.r,p.g,p.b,l};
            const auto column=std::min(checked_mul(x,c.waveform_columns)/input.width,c.waveform_columns-1);
            for(std::size_t k=0;k<4;++k) { ++s.histogram[k][bin(channels[k])];if(k<3) ++s.parade[k][column*c.bins+bin(channels[k])]; }
            ++s.waveform[column*c.bins+bin(l)];
            const float cb=(p.b-l)/1.8556F+.5F,cr=(p.r-l)/1.5748F+.5F;
            ++s.vectorscope[bin(cr)*c.bins+bin(cb)];
            const Vec3 linear{decode(v.r,c.transfer),decode(v.g,c.transfer),decode(v.b,c.transfer)};
            const float scale=std::max({std::abs(linear.r),std::abs(linear.g),std::abs(linear.b),1e-30F});
            const auto xyz=multiply(matrix,Vec3{linear.r/scale,linear.g/scale,linear.b/scale});const float total=xyz.r+xyz.g+xyz.b;
            if(total>0&&xyz.r>=0&&xyz.g>=0&&xyz.b>=0) ++s.cie_xy[bin(xyz.g/total)*c.bins+bin(xyz.r/total)];else ++s.undefined_chromaticity;
            ++s.pixels;
        }
        return s;
    }
private:
    std::shared_ptr<const Compiled> cached(const NodeGraph& graph) const {
        std::lock_guard<std::mutex> guard(cache_mutex_);
        const auto found=cache_.find(graph.revision());if(found!=cache_.end()) return found->second;
        auto result=std::make_shared<const Compiled>(compile(graph));
        if(cache_.size()>=8) cache_.clear(); // bounded metadata cache, never cache caller pixels
        cache_.emplace(graph.revision(),result);return result;
    }
    EngineOptions options_;mutable std::mutex cache_mutex_;
    mutable std::unordered_map<std::uint64_t,std::shared_ptr<const Compiled>> cache_;
};
}
std::unique_ptr<IColorEngine> create_engine(EngineOptions options) { return std::make_unique<Engine>(options); }
} // namespace color

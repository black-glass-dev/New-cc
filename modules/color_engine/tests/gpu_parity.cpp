#include "color/gpu_graph.hpp"
#include "color/vulkan_compute.hpp"
#include "color/extended_tools.hpp"
#include "color/tracking.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <vector>
using namespace color;
namespace {
std::size_t assertions{};
void check(bool b,const char* s) {++assertions;if(!b) throw std::runtime_error(s);}
void near(float a,float b,float epsilon=1e-5F) {if(!(std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<epsilon)) {std::cerr<<"actual "<<a<<" expected "<<b<<" error "<<std::abs(a-b)<<'\n';throw std::runtime_error("Executed SPIR-V parity mismatch");}++assertions;}
void pixels(std::span<const Pixel> a,std::span<const Pixel> b) {check(a.size()==b.size(),"Pixel parity extent");for(std::size_t i=0;i<a.size();++i) {near(a[i].r,b[i].r);near(a[i].g,b[i].g);near(a[i].b,b[i].b);near(a[i].a,b[i].a);}}
std::vector<std::uint32_t> shader(const char* name) {std::ifstream f(std::string(COLOR_SPIRV_DIRECTORY)+"/"+name+".comp.spv",std::ios::binary|std::ios::ate);check(static_cast<bool>(f),"Missing shader file");const auto n=f.tellg();check(n>0&&n%4==0,"SPIR-V file size");std::vector<std::uint32_t> words(static_cast<std::size_t>(n)/4);f.seekg(0);f.read(reinterpret_cast<char*>(words.data()),static_cast<std::streamsize>(n));return words;}
template<class T> ComputeBuffer buffer(std::vector<T>& x,BufferAccess access=BufferAccess::Input) {return {std::as_writable_bytes(std::span(x)),access};}
template<class T,std::size_t N> ComputeBuffer buffer(std::span<T,N> x,BufferAccess access=BufferAccess::Input) {return {std::as_writable_bytes(x),access};}
template<class T> std::span<const std::byte> meta(const T& x) {return std::as_bytes(std::span(&x,1));}
std::vector<Pixel> fixture() {std::vector<Pixel> x(16);for(std::size_t i=0;i<x.size();++i) x[i]={.05F+.043F*static_cast<float>(i),.17F+.017F*static_cast<float>(i),.39F+.019F*static_cast<float>(i),.75F};return x;}
struct GradeMeta {std::uint32_t count{16},width{4},op{},frame{7},edge{2},curve_n{4096},inputs{1},reserved{};float key_in{1},key_out{1},mix{1};std::uint32_t invert{};};
static_assert(sizeof(GradeMeta)==48);
void grades(IVulkanCompute& gpu) {
    auto code=shader("grade");auto source=fixture();const auto original=source;std::vector<Pixel> target(16),samples(1);std::vector<float> p(80);
    auto run=[&](std::uint32_t op,const std::function<Pixel(Pixel,std::size_t)>& ref,std::uint32_t edge=2) {auto input=source;GradeMeta m;m.op=op;m.edge=edge;std::array<ComputeBuffer,4> buffers{buffer(input),buffer(target,BufferAccess::Output),buffer(p),buffer(samples)};gpu.dispatch({code,buffers,meta(m),16});for(std::size_t i=0;i<16;++i) {const auto v=ref(source[i],i);pixels(std::span(target).subspan(i,1),std::span(&v,1));}};
    auto vec=[&](std::size_t start,Vec3 v){p[start]=v.r;p[start+1]=v.g;p[start+2]=v.b;};
    auto rgb=[](Pixel v){return Vec3{v.r,v.g,v.b};};auto rgba=[](Vec3 v,float a){return Pixel{v.r,v.g,v.b,a};};
    CST c;c.input_transfer=Transfer::SRGB;c.output_transfer=Transfer::DaVinciIntermediate;c.output_gamut=Gamut::DaVinciWide;c.exposure_stops=.3F;c.tone_knee=.5F;c.gamut_compression=.2F;
    p[0]=1;p[1]=4;p[2]=c.exposure_stops;p[3]=c.tone_knee;p[4]=c.gamut_compression;const auto prepared=PreparedCST(c);for(std::size_t i=0;i<9;++i) p[5+i]=static_cast<float>(prepared.matrix().m[i]);run(0,[&](Pixel x,std::size_t){return rgba(transform(rgb(x),c),x.a);});
    for(unsigned t=0;t<12;++t) {
        for(unsigned direction=0;direction<2;++direction) {CST transfer;transfer.input_transfer=direction==0?static_cast<Transfer>(t):Transfer::Linear;transfer.output_transfer=direction==0?Transfer::Linear:static_cast<Transfer>(t);source=original;if(direction==0) for(auto& pixel:source) {pixel.r=encode(pixel.r,static_cast<Transfer>(t));pixel.g=encode(pixel.g,static_cast<Transfer>(t));pixel.b=encode(pixel.b,static_cast<Transfer>(t));}p[0]=static_cast<float>(transfer.input_transfer);p[1]=static_cast<float>(transfer.output_transfer);p[2]=p[3]=p[4]=0;const auto matrix=PreparedCST(transfer).matrix();for(std::size_t i=0;i<9;++i) p[5+i]=static_cast<float>(matrix.m[i]);run(0,[&](Pixel x,std::size_t){return rgba(transform(rgb(x),transfer),x.a);});}
    }
    source=original;
    Primaries primary;primary.lift={.01F,-.01F,.005F};primary.gamma={1.1F,.9F,1.05F};primary.gain={1.1F,.9F,1.02F};primary.offset={.01F,.02F,-.01F};primary.exposure=.2F;primary.contrast=1.1F;primary.saturation=1.15F;primary.hue_radians=.1F;primary.vibrance=.2F;primary.temperature=.03F;primary.tint=-.02F;primary.luminance_mix=.1F;
    vec(0,primary.lift);vec(3,primary.gamma);vec(6,primary.gain);vec(9,primary.offset);p[12]=primary.exposure;p[13]=primary.contrast;p[14]=primary.pivot;p[15]=primary.saturation;p[16]=primary.hue_radians;p[17]=primary.vibrance;p[18]=primary.temperature;p[19]=primary.tint;p[20]=primary.luminance_mix;run(1,[&](Pixel x,std::size_t){return rgba(apply(rgb(x),primary),x.a);});
    LogWheels log;log.shadows={.01F,.02F,-.01F};log.midtones={-.01F,0,.005F};log.highlights={.02F,-.01F,.01F};vec(0,log.shadows);vec(3,log.midtones);vec(6,log.highlights);p[9]=log.low;p[10]=log.high;p[11]=log.softness;run(2,[&](Pixel x,std::size_t){return rgba(apply(rgb(x),log),x.a);});
    HDRWheels hdr;hdr.stops={-.2F,.1F,.3F,-.1F,.2F,.1F};hdr.soft_clip=.6F;for(std::size_t i=0;i<6;++i) {p[i]=hdr.stops[i];p[6+i]=hdr.centers[i];}p[12]=hdr.falloff;p[13]=hdr.soft_clip;run(3,[&](Pixel x,std::size_t){return rgba(apply(rgb(x),hdr),x.a);});
    ColorSlice slice;slice.hue_sat_density[3]={.1F,.2F,.2F};slice.shadow_tint={.01F,.02F,.005F};for(std::size_t i=0;i<7;++i) vec(i*3,slice.hue_sat_density[i]);p[21]=slice.width;vec(22,slice.shadow_tint);vec(25,slice.highlight_tint);run(4,[&](Pixel x,std::size_t){return rgba(apply(rgb(x),slice),x.a);});
    RGBMixer mixer;mixer.matrix.m={.9,.1,0,0,.8,.2,.1,0,.9};for(std::size_t i=0;i<9;++i) p[i]=static_cast<float>(mixer.matrix.m[i]);p[9]=0;run(5,[&](Pixel x,std::size_t){return rgba(apply(rgb(x),mixer),x.a);});
    HSLQualifier key;key.center={.6F,.5F,.5F};key.width={.4F,.5F,.7F};vec(0,key.center);vec(3,key.width);vec(6,key.softness);p[9]=0;run(6,[&](Pixel x,std::size_t){x.a*=qualify(rgb(x),key);return x;});
    PowerWindow window;window.shape=PowerWindow::Shape::Rectangle;window.rotation=.2F;p[0]=1;p[1]=window.cx;p[2]=window.cy;p[3]=window.sx;p[4]=window.sy;p[5]=window.rotation;p[6]=window.feather;p[7]=0;run(7,[&](Pixel x,std::size_t i){x.a*=window_mask((static_cast<float>(i%4)+.5F)/4,(static_cast<float>(i/4)+.5F)/4,window);return x;});
    auto curve=std::make_shared<Curve>(std::array<CurvePoint,3>{{{0,0},{.5F,.6F},{1,1}}});Curves curves{curve,curve,{},{}};samples.resize(4096);for(std::size_t i=0;i<4096;++i){float t=static_cast<float>(i)/4095;samples[i]={curve->evaluate(t),curve->evaluate(t),t,t};}p[0]=1;p[1]=1;p[2]=0;p[3]=0;run(8,[&](Pixel x,std::size_t){return rgba(apply(rgb(x),curves),x.a);});
    // Rounded-rectangle SDF (shape 3, corner radius in P8) and 1D LUT (op 22, domain P0-P5, edge = size).
    PowerWindow rounded=window;rounded.shape=PowerWindow::Shape::RoundedRectangle;rounded.corner_radius=.15F;p[0]=3;p[1]=rounded.cx;p[2]=rounded.cy;p[3]=rounded.sx;p[4]=rounded.sy;p[5]=rounded.rotation;p[6]=rounded.feather;p[7]=0;p[8]=rounded.corner_radius;run(7,[&](Pixel x,std::size_t i){x.a*=window_mask((static_cast<float>(i%4)+.5F)/4,(static_cast<float>(i/4)+.5F)/4,rounded);return x;});
    samples={{0,0,0,0},{.5F,.5F,.5F,0},{1,1,1,0}};const Lut1D lut1d({{0,0,0},{.5F,.5F,.5F},{1,1,1}});vec(0,{0,0,0});vec(3,{1,1,1});run(22,[&](Pixel x,std::size_t){return rgba(lut1d.apply(rgb(x)),x.a);},3);
    samples.assign(8,{0,0,0,0});samples[7]={1,1,1,1};std::vector<Vec3> data(8);data[7]={1,1,1};Lut3D lut(2,data);vec(0,{0,0,0});vec(3,{1,1,1});run(9,[&](Pixel x,std::size_t){return rgba(lut.tetrahedral(rgb(x)),x.a);});
    Grain grain_params{.1F,.7F,0xffffffffU};p[0]=grain_params.amount;p[1]=grain_params.colored;p[2]=std::bit_cast<float>(grain_params.seed);run(10,[&](Pixel x,std::size_t i){return rgba(grain(rgb(x),static_cast<std::uint32_t>(i%4),static_cast<std::uint32_t>(i/4),7,grain_params),x.a);});
    RawDecodeParams raw;raw.white_level={1024,1024,1024};raw.white_balance={1.1F,.9F,1.2F};vec(0,raw.black_level);vec(3,raw.white_level);vec(6,raw.white_balance);p[9]=raw.exposure_stops;for(std::size_t i=0;i<9;++i) p[10+i]=static_cast<float>(raw.camera_to_working.m[i]);run(16,[&](Pixel x,std::size_t){return rgba(apply(rgb(x),raw),x.a);});
    PolygonWindow polygon;polygon.count=4;polygon.feather=.1F;polygon.vertices[0]={.2F,.1F};polygon.vertices[1]={.9F,.2F};polygon.vertices[2]={.8F,.9F};polygon.vertices[3]={.1F,.8F};p[0]=4;p[1]=polygon.feather;p[2]=0;for(std::size_t i=0;i<4;++i) {p[3+2*i]=polygon.vertices[i].x;p[4+2*i]=polygon.vertices[i].y;}run(17,[&](Pixel x,std::size_t i){x.a*=polygon_mask((static_cast<float>(i%4)+.5F)/4,(static_cast<float>(i/4)+.5F)/4,polygon);return x;});
    std::cout<<"GPU grade:CST/primaries/log/HDR/slices/mixer/HSL/window/curves/LUT/grain/RAW/polygon PASS\n";
}
// Full DAG on the GPU scheduler against the CPU reference. Tolerance 2e-4 is the documented contract.
void graph_engine(IVulkanCompute& gpu) {
    GpuGradeEngine engine(gpu,GpuKernels{shader("grade"),shader("spatial")});const auto cpu=create_engine();
    Image32F input(12,12),gpu_out(12,12),cpu_out(12,12);for(std::size_t i=0;i<144;++i) input.pixels()[i]={.1F+.6F*static_cast<float>(i%12)/11,.2F,.3F,1};input.pixels()[6*12+6]={2,2,2,1};
    GradeProgram program;Node first;first.id=1;Primaries primaries;primaries.exposure=.3F;primaries.saturation=1.1F;first.operation=primaries;program.clip.add(first);
    Node second;second.id=2;second.inputs={1};SpatialFilter halation;halation.kind=SpatialKind::Halation;halation.radius=1;halation.threshold=.5F;second.operation=halation;program.clip.add(second);
    cpu->RenderGradeFrame(program,input.view(),cpu_out.view(),{});engine.RenderGradeFrame(program,input.view(),gpu_out.view(),{});
    for(std::size_t i=0;i<144;++i) { near(gpu_out.pixels()[i].r,cpu_out.pixels()[i].r,2e-4F);near(gpu_out.pixels()[i].g,cpu_out.pixels()[i].g,2e-4F);near(gpu_out.pixels()[i].b,cpu_out.pixels()[i].b,2e-4F);near(gpu_out.pixels()[i].a,cpu_out.pixels()[i].a,2e-4F); }
    // Steep curve: the engine must pick a larger table and still match the CPU within 2e-4.
    auto steep=std::make_shared<Curve>(std::array<CurvePoint,4>{{{0,0},{.499F,.001F},{.501F,.999F},{1,1}}});
    GradeProgram curved;Node curve_node;curve_node.id=1;curve_node.operation=Curves{steep,steep,nullptr,nullptr};curved.clip.add(curve_node);
    for(std::size_t i=0;i<144;++i) input.pixels()[i].r=static_cast<float>(i)/143.F;
    cpu->RenderGradeFrame(curved,input.view(),cpu_out.view(),{});engine.RenderGradeFrame(curved,input.view(),gpu_out.view(),{});
    for(std::size_t i=0;i<144;++i) near(gpu_out.pixels()[i].r,cpu_out.pixels()[i].r,2e-4F);
    // External planes are host-uploaded matte sources on both paths: must agree, and size mismatches must throw.
    auto plane=std::make_shared<Image32F>(12,12);for(std::size_t i=0;i<144;++i) plane->pixels()[i]={0,0,0,static_cast<float>(i%12)/11};
    GradeProgram external;Node ext;ext.id=1;ext.operation=ExternalPlane{plane};external.clip.add(ext);
    cpu->RenderGradeFrame(external,input.view(),cpu_out.view(),{});engine.RenderGradeFrame(external,input.view(),gpu_out.view(),{});
    for(std::size_t i=0;i<144;++i) { near(gpu_out.pixels()[i].a,cpu_out.pixels()[i].a,2e-4F);near(gpu_out.pixels()[i].r,cpu_out.pixels()[i].r,2e-4F); }
    auto wrong=std::make_shared<Image32F>(4,4);GradeProgram mismatch;Node bad;bad.id=1;bad.operation=ExternalPlane{wrong};mismatch.clip.add(bad);
    bool threw=false;try{engine.RenderGradeFrame(mismatch,input.view(),gpu_out.view(),{});}catch(const std::invalid_argument&){threw=true;}check(threw,"GPU engine must reject mismatched external planes");
    std::cout<<"GPU graph scheduler: Primaries+Halation DAG within 2e-4 of CPU; external planes match CPU and mismatches rejected PASS\n";
}
void spatial(IVulkanCompute& gpu) {
    auto code=shader("spatial");Image32F in(4,4),history(4,4),reference(4,4);auto source=fixture();for(std::size_t i=0;i<16;++i) {source[i].a=i==5?1.F:0.F;in.pixels()[i]=source[i];history.pixels()[i]={source[i].r*.9F,source[i].g*.9F,source[i].b*.9F,source[i].a};}
    std::vector<Pixel> old(history.pixels().begin(),history.pixels().end()),out(16);std::array<float,8> params{};
    for(unsigned kind=0;kind<8;++kind) {SpatialFilter filter;filter.kind=static_cast<SpatialKind>(kind);filter.radius=1;filter.range_sigma=.2F;filter.amount=.7F;filter.threshold=.2F;params={filter.spatial_sigma,filter.range_sigma,filter.amount,filter.threshold,1,1,1,filter.transmission_floor};process_spatial(in,reference,filter,&history);
        const std::array<std::uint32_t,4> m{4,4,kind,1};std::array<ComputeBuffer,4> buffers{buffer(source),buffer(old),buffer(out,BufferAccess::Output),buffer(std::span(params))};gpu.dispatch({code,buffers,meta(m),16});pixels(out,reference.pixels());}
    std::cout<<"GPU spatial:8 filters incl temporal/morphology/bloom PASS\n";
}
void raw_resize(IVulkanCompute& gpu) {
    auto code=shader("demosaic");constexpr unsigned colors[4][4]={{0,1,1,2},{2,1,1,0},{1,0,2,1},{1,2,0,1}};std::vector<float> raw(24),params(19);std::vector<std::uint16_t> u16(24);std::vector<Pixel> out(16);
    for(std::size_t i=0;i<3;++i) params[3+i]=1024;params[6]=params[7]=params[8]=params[10]=params[14]=params[18]=1;
    for(unsigned pattern=0;pattern<4;++pattern) {for(std::size_t y=0;y<4;++y) for(std::size_t x=0;x<4;++x) {const auto v=static_cast<std::uint16_t>(100+100*colors[pattern][(y%2)*2+x%2]+x*7+y*3);u16[y*6+x]=v;raw[y*6+x]=v;}
        RawDecodeParams p;p.white_level={1024,1024,1024};auto reference=demosaic_bayer16(u16,4,4,6,static_cast<BayerPattern>(pattern),p);const std::array<std::uint32_t,4> m{4,4,6,pattern};std::array<ComputeBuffer,3> buffers{buffer(raw),buffer(out,BufferAccess::Output),buffer(params)};gpu.dispatch({code,buffers,meta(m),16});pixels(out,reference.pixels());}
    code=shader("resize");auto source=fixture();Image32F image(4,4),reference(4,4);std::copy(source.begin(),source.end(),image.pixels().begin());Affine2D transform;transform.m={1,0,.25F,0,1,.25F};resize_affine(image,reference,transform);const std::array<std::uint32_t,4> m{4,4,4,4};std::array<ComputeBuffer,3> buffers{buffer(source),buffer(out,BufferAccess::Output),buffer(std::span(transform.m))};gpu.dispatch({code,buffers,meta(m),16});pixels(out,reference.pixels());std::cout<<"GPU Bayer4 patterns/borders/strides + affine bilinear sizing PASS\n";
}
void composition_scopes(IVulkanCompute& gpu) {
    auto code=shader("grade");const auto src=fixture();std::vector<Pixel> planes(48),out(16),expected(16),dummy(1);std::vector<float> params(1,1);
    for(std::size_t i=0;i<16;++i) {planes[i]=src[i];planes[i+16]={src[i].g,src[i].b,src[i].r,.2F+.02F*static_cast<float>(i)};planes[i+32]={src[i].r*1.1F,src[i].g*1.1F,src[i].b*1.1F,src[i].a};}
    for(auto op:{11U,12U,13U,14U,15U,18U,19U,20U}) {
        GradeMeta m;m.op=op;m.inputs=op==13U||op==15U?3:2;
        for(std::size_t i=0;i<16;++i) {const auto a=planes[i],b=planes[i+16],c=planes[i+32];auto v=a;
            if(op==11U) v={(a.r+b.r)*.5F,(a.g+b.g)*.5F,(a.b+b.b)*.5F,(a.a+b.a)*.5F};
            else if(op==12U) {float alpha=b.a+a.a*(1-b.a);v={(b.r*b.a+a.r*a.a*(1-b.a))/alpha,(b.g*b.a+a.g*a.a*(1-b.a))/alpha,(b.b*b.a+a.b*a.a*(1-b.a))/alpha,alpha};}
            else if(op==13U) v={a.r*b.a+c.r*(1-b.a),a.g*b.a+c.g*(1-b.a),a.b*b.a+c.b*(1-b.a),a.a};
            else if(op==14U) v={a.g,a.g,a.g,a.a};else if(op==15U) v={a.r,b.g,c.b,a.a};else if(op==18U) v.a=std::max(a.a,b.a);else if(op==19U) v.a=std::min(a.a,b.a);else if(op==20U) v.a=std::max(0.F,a.a-b.a);expected[i]=v;}
        std::array<ComputeBuffer,4> buffers{buffer(planes),buffer(out,BufferAccess::Output),buffer(params),buffer(dummy)};gpu.dispatch({code,buffers,meta(m),16});pixels(out,expected);
    }
    code=shader("scopes");Image32F image(4,4);std::copy(src.begin(),src.end(),image.pixels().begin());const auto scope=create_engine()->ComputeVideoScopes(image.view(),{16,4,0,1,Gamut::Rec709,Transfer::SRGB});std::vector<std::uint32_t> counters(833);std::vector<float> matrix(9);const auto xyz=rgb_to_xyz(Gamut::Rec709);for(std::size_t i=0;i<9;++i) matrix[i]=static_cast<float>(xyz.m[i]);auto source=src;
    struct ScopeMeta {std::uint32_t count{16},width{4},bins{16},columns{4};float low{},high{1};std::uint32_t transfer{1},reserved{};} sm;
    std::array<ComputeBuffer,3> buffers{buffer(source),buffer(counters,BufferAccess::Output),buffer(matrix)};gpu.dispatch({code,buffers,meta(sm),16});std::vector<std::uint64_t> reference;for(const auto& h:scope.histogram) reference.insert(reference.end(),h.begin(),h.end());for(const auto& h:scope.parade) reference.insert(reference.end(),h.begin(),h.end());reference.insert(reference.end(),scope.waveform.begin(),scope.waveform.end());reference.insert(reference.end(),scope.vectorscope.begin(),scope.vectorscope.end());reference.insert(reference.end(),scope.cie_xy.begin(),scope.cie_xy.end());reference.push_back(scope.undefined_chromaticity);check(reference.size()==counters.size(),"Scope ABI extent");for(std::size_t i=0;i<reference.size();++i) check(reference[i]==counters[i],"Executed GPU scope count mismatch");
    std::cout<<"GPU composition/split/combine/matte algebra + all scope counters PASS\n";
}
void separable_halation(IVulkanCompute& gpu) {
    auto code=shader("halation");auto source=fixture();std::vector<float> horizontal(16),intermediate(16);std::vector<Pixel> out(16);Image32F image(4,4),reference(4,4);std::copy(source.begin(),source.end(),image.pixels().begin());halation(image,reference,.2F,.3F,2);
    struct Meta {std::uint32_t width{4},height{4},radius{2},phase{};float threshold{.2F},amount{.3F};} m;
    std::array<ComputeBuffer,4> first{buffer(source),buffer(horizontal),buffer(out,BufferAccess::Output),buffer(intermediate,BufferAccess::Output)};gpu.dispatch({code,first,meta(m),16});horizontal=intermediate;m.phase=1;std::array<ComputeBuffer,4> second{buffer(source),buffer(horizontal),buffer(out,BufferAccess::Output),buffer(intermediate,BufferAccess::Output)};gpu.dispatch({code,second,meta(m),16});pixels(out,reference.pixels());std::cout<<"GPU separable two-pass halation PASS\n";
}
void tracking(IVulkanCompute& gpu) {
    auto code=shader("tracking");Image32F previous(64,64),current(64,64);auto scene=[](float x,float y){return .5F+.1F*std::sin(.18F*x)+.1F*std::cos(.23F*y)+.12F*std::sin(.11F*x+.2F*y);};
    for(std::size_t y=0;y<64;++y) for(std::size_t x=0;x<64;++x) {const float a=scene(static_cast<float>(x),static_cast<float>(y)),b=scene(static_cast<float>(x)-1,static_cast<float>(y)+.5F);previous.pixels()[y*64+x]={a,a,a,1};current.pixels()[y*64+x]={b,b,b,1};}
    KLTOptions options;options.levels=1;const auto reference=track_klt(previous,current,std::array<FeaturePoint,1>{{{1,30,30}}},options);std::array<float,4> feature{30,30,30,30},result{};
    struct Meta {std::uint32_t w{64},h{64},count{1},radius{3},iterations{20};float max_error{.15F},convergence{.001F};std::uint32_t reserved{};} m;
    std::array<ComputeBuffer,4> buffers{buffer(previous.pixels()),buffer(current.pixels()),buffer(std::span(feature)),buffer(std::span(result),BufferAccess::Output)};gpu.dispatch({code,buffers,meta(m),1});check(reference[0].status==TrackStatus::Good&&result[3]==0,"GPU KLT status");near(result[0],reference[0].target.x,.001F);near(result[1],reference[0].target.y,.001F);near(result[2],reference[0].rms_error);std::cout<<"GPU single-level Lucas-Kanade vs CPU PASS\n";
}
}
int main() {try{auto gpu=create_vulkan_compute();std::cout<<gpu->device_name()<<'\n';grades(*gpu);graph_engine(*gpu);spatial(*gpu);raw_resize(*gpu);composition_scopes(*gpu);separable_halation(*gpu);tracking(*gpu);std::cout<<assertions<<" executed GPU parity assertions PASS\n";return 0;}catch(const GpuUnavailable& e){std::cout<<"UNRUN: "<<e.what()<<'\n';return 77;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}

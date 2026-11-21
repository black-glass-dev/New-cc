#include "color/color_pipeline_api.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>
#include <thread>
#include <utility>
#include <atomic>
#include <stdexcept>

using namespace color;
namespace {
std::size_t assertions{};
void check(bool v,const char* text) { ++assertions;if(!v) throw std::runtime_error(text); }
void near(float a,float b,float eps=1e-5F) { if(std::abs(a-b)>=eps) std::cerr<<"actual="<<a<<" expected="<<b<<" tolerance="<<eps<<"\n";check(std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<eps,"Float reference mismatch"); }
void near(Vec3 a,Vec3 b,float eps=1e-5F) { near(a.r,b.r,eps);near(a.g,b.g,eps);near(a.b,b.b,eps); }
template<class F> void rejects(F f) { bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"Expected refusal"); }
std::uint64_t sum(const std::vector<std::uint64_t>& v) { return std::accumulate(v.begin(),v.end(),std::uint64_t{0}); }
std::shared_ptr<Lut3D> identity_lut() {
    std::ostringstream text;text<<"TITLE \"identity\"\nLUT_3D_SIZE 2\nDOMAIN_MIN 0 0 0\nDOMAIN_MAX 1 1 1\n";
    for(int b=0;b<2;++b) for(int g=0;g<2;++g) for(int r=0;r<2;++r) text<<r<<' '<<g<<' '<<b<<'\n';
    return std::make_shared<Lut3D>(Lut3D::parse_cube(text.str()));
}
void science() {
    for(auto t:{Transfer::Linear,Transfer::SRGB,Transfer::Gamma24,Transfer::Rec709,Transfer::DaVinciIntermediate,Transfer::ACEScct,Transfer::PQ,Transfer::HLG,Transfer::LogC3EI800,Transfer::LogC4,Transfer::SLog3,Transfer::VLog}) {
        for(int i=0;i<=1000;++i) { const float x=static_cast<float>(i)/1000;near(decode(encode(x,t),t),x,1e-5F); }
    }
    near(encode(.18F,Transfer::DaVinciIntermediate),.336043F,1e-6F);
    near(encode(.18F,Transfer::ACEScct),.4135884F,1e-6F);
    near(encode(.01F,Transfer::PQ),.5080784F,1e-6F); //100nit/10000nit
    near(decode(.5F,Transfer::HLG),1.F/12,1e-6F);
    for(auto g:{Gamut::Rec709,Gamut::Rec2020,Gamut::P3D65,Gamut::ACESAP0,Gamut::ACESAP1,Gamut::DaVinciWide,Gamut::ARRIWide3,Gamut::ARRIWide4,Gamut::SonySGamut3,Gamut::SonySGamut3Cine,Gamut::PanasonicVGamut}) {
        CST forward;forward.input_gamut=g;forward.output_gamut=Gamut::Rec709;
        CST reverse;reverse.input_gamut=Gamut::Rec709;reverse.output_gamut=g;
        for(Vec3 p:std::array<Vec3,4>{{{.01F,.02F,.03F},{.5F,.2F,.9F},{-.1F,.3F,1.2F},{1,1,1}}}) near(transform(transform(p,forward),reverse),p,1e-5F);
    }
    for(auto g:{Gamut::DaVinciWide,Gamut::ACESAP1,Gamut::Rec709}) for(auto t:{Transfer::DaVinciIntermediate,Transfer::ACEScct,Transfer::Gamma24}) {
        CST a;a.input_gamut=g;a.input_transfer=t;a.output_gamut=Gamut::Rec2020;
        CST b;b.input_gamut=a.output_gamut;b.output_gamut=g;b.output_transfer=t;
        near(transform(transform({.3F,.4F,.5F},a),b),{.3F,.4F,.5F});
    }
    rejects([]{(void)decode(std::numeric_limits<float>::quiet_NaN(),Transfer::SRGB);});
    rejects([]{(void)encode(-1,Transfer::PQ);});
    rejects([]{(void)inverse(Mat3{});});
    std::cout<<"science:12 transfer round-trips/11 gamuts/Bradford/official vectors PASS\n";
}
void tools() {
    const Vec3 x{.12F,.2F,.4F};near(apply(x,Primaries{}),x);
    Primaries p;p.exposure=1;near(apply(x,p),x*2);
    near(apply(x,LogWheels{}),x);
    HDRWheels h;h.stops.fill(1);near(apply(x,h),x*2);
    ColorSlice slice;slice.hue_sat_density[0].b=1;check(luminance(apply({1,0,0},slice))<luminance({1,0,0}),"Subtractive red density");
    near(apply(x,RGBMixer{}),x);
    PowerWindow w;near(window_mask(.5F,.5F,w),1);near(window_mask(0,0,w),0);w.invert=true;near(window_mask(.5F,.5F,w),0);
    HSLQualifier q;q.center={0,1,.5F};q.width={.1F,.1F,.1F};near(qualify({1,0,0},q),1);near(qualify({0,1,0},q),0);
    Grain grain_params{.1F,1,17};near(grain(x,4,7,9,grain_params),grain(x,4,7,9,grain_params),1e-7F);
    check(grain(x,4,7,8,grain_params).r!=grain(x,4,7,9,grain_params).r,"Animated grain");
    const std::array<CurvePoint,5> points{{{0,0},{.2F,.1F},{.5F,.7F},{.8F,.9F},{1,1}}};Curve curve(points);
    float previous=-1;for(int i=0;i<=10000;++i) { const float y=curve.evaluate(static_cast<float>(i)/10000);check(y>=previous-1e-6F&&y>=-1e-6F&&y<=1+1e-6F,"Curve monotonic/no overshoot");previous=y; }
    const std::array<CurvePoint,3> nonmonotone{{{0,1},{.5F,0},{1,1}}};Curve bowl(nonmonotone);
    for(int i=0;i<=1000;++i) { const float y=bowl.evaluate(static_cast<float>(i)/1000);check(y>=-1e-6F&&y<=1+1e-6F,"Nonmonotone curve segment bounds"); }
    rejects([]{const std::array<CurvePoint,2> bad{{{0,0},{0,1}}};Curve c(bad);});
    check(curve.bake(4096).size()==4096,"Baked curve extent");
    auto lut=identity_lut();std::mt19937 random(123);std::uniform_real_distribution<float> unit(0,1);
    for(int i=0;i<10000;++i) { const Vec3 v{unit(random),unit(random),unit(random)};near(lut->tetrahedral(v),v,1e-6F);near(lut->trilinear(v),v,1e-6F); }
    std::vector<Vec3> nonlinear(8);for(std::size_t i=0;i<8;++i) nonlinear[i]={0,0,0};nonlinear[7]={1,1,1};Lut3D n(2,nonlinear);
    check(std::abs(n.tetrahedral({.7F,.5F,.2F}).r-n.trilinear({.7F,.5F,.2F}).r)>.05F,"Tetrahedral must not be trilinear alias");
    for(int i=0;i<1000;++i) { const Vec3 v{unit(random),unit(random),unit(random)};near(n.tetrahedral(v),Vec3{std::min({v.r,v.g,v.b}),std::min({v.r,v.g,v.b}),std::min({v.r,v.g,v.b})},1e-6F);near(n.trilinear(v).r,v.r*v.g*v.b,1e-6F); }
    near(lut->domain_min(),{0,0,0});near(lut->domain_max(),{1,1,1});
    rejects([]{(void)Lut3D::parse_cube("TITLE \"unterminated\nLUT_3D_SIZE 2\n");});
    rejects([]{(void)Lut3D::parse_cube("LUT_3D_SIZE 999999\n");});rejects([]{(void)Lut3D::parse_cube("LUT_3D_SIZE 2\n0 0 0 extra\n");});rejects([]{(void)Lut3D::parse_cube("LUT_3D_SIZE 2\n");});
    Image32F impulse(9,9),glow(9,9);for(auto& a:impulse.pixels()) a={0,0,0,1};impulse.pixels()[40]={5,5,5,1};halation(impulse,glow,1,.1F,2);check(glow.pixels()[39].r>0,"Halation spatial neighbor");near(glow.pixels()[39].a,1);
    std::cout<<"operators:primaries/log/HDR/slice/key/SDF/curves/LUT/grain/halation PASS\n";
}
void graph_and_frames() {
    NodeGraph cycle;Node a;a.id=1;a.inputs={2};cycle.add(a);a.id=2;a.inputs={1};cycle.add(a);rejects([&]{(void)cycle.compile();});
    NodeGraph missing;Node n;n.id=1;n.inputs={77};missing.add(n);rejects([&]{(void)missing.compile();});
    GradeProgram p;Primaries exposure;exposure.exposure=1;Node first;first.id=1;first.operation=exposure;first.locked=true;p.clip.add(first);rejects([&]{p.clip.replace(first);});p.clip.set_locked(1,false);
    Node second;second.id=2;second.operation=Primaries{};p.clip.add(second);Node parallel;parallel.id=3;parallel.kind=NodeKind::Parallel;parallel.inputs={1,2};p.clip.add(parallel);
    auto engine=create_engine();Image32F in(3,2),out(3,2);for(auto& x:in.pixels()) x={.2F,.4F,.1F,1};engine->RenderGradeFrame(p,in.view(),out.view(),{1,2,3,0});near(out.pixels()[0].r,.3F);check(p.clip.layers().size()==3,"Layer projection");
    GradeProgram split;for(unsigned c=0;c<3;++c) { Node node;node.id=c+1;node.kind=NodeKind::Channel;node.channel=c;split.clip.add(node); }Node combine;combine.id=4;combine.kind=NodeKind::Combiner;combine.inputs={1,2,3};split.clip.add(combine);
    engine->RenderGradeFrame(split,in.view(),out.view(),{});near({out.pixels()[0].r,out.pixels()[0].g,out.pixels()[0].b},{.2F,.4F,.1F});
    GradeProgram layered;Node base;base.id=1;layered.clip.add(base);Node foreground;foreground.id=2;PowerWindow off;off.cx=5;foreground.operation=off;layered.clip.add(foreground);Node layer;layer.id=3;layer.kind=NodeKind::LayerMixer;layer.inputs={1,2};layered.clip.add(layer);engine->RenderGradeFrame(layered,in.view(),out.view(),{});near(out.pixels()[0].r,.2F);
    GradeProgram outside=layered;outside.clip.set_locked(3,false);layer.kind=NodeKind::Outside;layer.operation=exposure;outside.clip.replace(layer);engine->RenderGradeFrame(outside,in.view(),out.view(),{});near(out.pixels()[0].r,.4F);
    std::vector<float> strided(32,-99);for(std::size_t y=0;y<2;++y) for(std::size_t x=0;x<3;++x) { strided[y*16+x*4]=.2F;strided[y*16+x*4+1]=.4F;strided[y*16+x*4+2]=.1F;strided[y*16+x*4+3]=.5F; }
    ColorFrameBuffer32F frame{strided.data(),3,2,16,strided.size()};engine->RenderGradeFrame(GradeProgram{},frame,frame,{});near(strided[3],.5F);near(strided[12],-99);
    const auto before=strided;GradeProgram invalid;invalid.clip=cycle;rejects([&]{engine->RenderGradeFrame(invalid,frame,frame,{});});check(before==strided,"Failure atomic output");
    auto bad=frame;bad.capacity_floats=3;rejects([&]{engine->RenderGradeFrame({},bad,frame,{});});
    const auto s=engine->ComputeVideoScopes(in.view(),{32,7,0,1,Gamut::Rec709});check(s.pixels==6,"Scope pixels");for(const auto& v:s.histogram) check(sum(v)==6,"Histogram conservation");for(const auto& v:s.parade) check(sum(v)==6,"Parade conservation");check(sum(s.waveform)==6&&sum(s.vectorscope)==6&&sum(s.cie_xy)+s.undefined_chromaticity==6,"Scope conservation");
    rejects([&]{auto tiny=create_engine({32,1});tiny->RenderGradeFrame({},in.view(),out.view(),{});});
    std::cout<<"DAG/frame/scopes:cycle/locks/mixers/outside/RGB/stride/alpha/atomicity/budgets PASS\n";
}
void robustness() {
    RawDecodeParams raw;raw.black_level={64,64,64};raw.white_level={1024,1024,1024};raw.white_balance={2,1,1};
    near(apply({544,544,544},raw),{1,.5F,.5F});rejects([&]{(void)apply({-1,0,0},raw);});
    Node touch;touch.id=1;touch.operation=Primaries{};const auto adjusted=touch_wheel(touch,TouchWheel::Gain,.4F,-.2F,.1F);
    check(std::get<Primaries>(adjusted.operation).gain.r>1,"Wheel must produce real grade");near(std::get<Primaries>(touch.operation).gain.r,1);
    touch.locked=true;rejects([&]{(void)touch_wheel(touch,TouchWheel::Lift,0,0,0);});
    rejects([]{(void)bradford(.3,std::numeric_limits<double>::infinity(),.3,.3);});
    for(auto t:{Transfer::SRGB,Transfer::Gamma24,Transfer::Rec709,Transfer::DaVinciIntermediate,Transfer::ACEScct,Transfer::LogC3EI800,Transfer::LogC4,Transfer::SLog3,Transfer::VLog}) {
        for(float x:{-.1F,-.001F,0.F,.00262409F,.0078125F,.01F,.010591F,.01125F,.018F,.18F,4.F}) near(decode(encode(x,t),t),x,1e-5F);
    }
    auto engine=create_engine();Image32F in(8,8),out(8,8);for(auto& v:in.pixels()) v={.1F,.2F,.3F,1};
    GradeProgram original;Node n;n.id=1;original.clip.add(n);Node dependency;dependency.id=2;dependency.inputs={1};Primaries up;up.exposure=1;dependency.operation=up;original.clip.add(dependency);
    original.clip.set_output(1);engine->RenderGradeFrame(original,in.view(),out.view(),{});near(out.pixels()[0].r,.1F);
    original.clip.set_output(2);engine->RenderGradeFrame(original,in.view(),out.view(),{});near(out.pixels()[0].r,.2F);
    auto a=original,b=original;up.exposure=2;dependency.operation=up;a.clip.replace(dependency);up.exposure=3;dependency.operation=up;b.clip.replace(dependency);
    engine->RenderGradeFrame(a,in.view(),out.view(),{});near(out.pixels()[0].r,.4F);engine->RenderGradeFrame(b,in.view(),out.view(),{});near(out.pixels()[0].r,.8F);
    GradeProgram moved=std::move(a);engine->RenderGradeFrame(a,in.view(),out.view(),{});near(out.pixels()[0].r,.1F);engine->RenderGradeFrame(moved,in.view(),out.view(),{});near(out.pixels()[0].r,.4F);
    for(auto& v:out.pixels()) { v={-9,-9,-9,1}; }
    in.pixels().back().r=std::numeric_limits<float>::quiet_NaN();rejects([&]{engine->RenderGradeFrame(original,in.view(),out.view(),{});});for(const auto v:out.pixels()) near(v.r,-9);in.pixels().back().r=.1F;
    rejects([]{Image32F overflow(std::numeric_limits<std::size_t>::max(),2);});
    const auto before=out.pixels()[0];auto small=in.view();small.row_stride_floats=1;rejects([&]{engine->RenderGradeFrame(original,small,out.view(),{});});near(out.pixels()[0].r,before.r);
    std::atomic<bool> success{true};std::array<std::thread,4> workers;
    for(auto& worker:workers) worker=std::thread([&]{try {Image32F target(8,8);for(int i=0;i<20;++i) {engine->RenderGradeFrame(original,in.view(),target.view(),{});if(std::abs(target.pixels()[0].r-.2F)>1e-6F) success=false;}}catch(...){success=false;}});
    for(auto& worker:workers) { worker.join(); }
    check(success,"Concurrent immutable renders/cache");
    Image32F extremes(1,1);const float fmax=std::numeric_limits<float>::max();extremes.pixels()[0]={fmax,fmax,fmax,1};const auto extreme_scopes=engine->ComputeVideoScopes(extremes.view(),{16,1,-fmax,fmax,Gamut::Rec709});check(sum(extreme_scopes.histogram[3])==1&&sum(extreme_scopes.cie_xy)==1,"Finite extreme scope range/chromaticity");
    HSLQualifier tiny;tiny.width={1,1,1};check(std::isfinite(qualify({1e-30F,0,0},tiny)),"Stable near-black HSL");
    std::cout<<"robustness:RAW color-stage/touch/negative+HDR log domain/output selection/cache fork+move/concurrency PASS\n";
}
void end_to_end() {
    Image32F source(32,24),result(32,24);for(std::size_t i=0;i<source.pixels().size();++i) { const float v=static_cast<float>(i%32)/31;source.pixels()[i]={v*.6F,.18F,v*.3F,1}; }
    GradeProgram program;std::uint64_t previous=0;
    auto add=[&](Operator op) { Node n;n.id=previous+1;if(previous) n.inputs={previous};n.operation=std::move(op);previous=n.id;program.clip.add(std::move(n)); };
    CST c;c.input_transfer=Transfer::SRGB;c.output_gamut=Gamut::DaVinciWide;add(c);
    Primaries p;p.exposure=.2F;p.contrast=1.1F;add(p);add(LogWheels{});add(HDRWheels{});add(ColorSlice{});
    auto curve=std::make_shared<Curve>(std::array<CurvePoint,3>{{{0,0},{.5F,.55F},{1,1}}});add(Curves{curve,{},{},{}});add(identity_lut());
    c.input_gamut=Gamut::DaVinciWide;c.input_transfer=Transfer::Linear;c.output_gamut=Gamut::Rec709;c.output_transfer=Transfer::SRGB;add(c);
    auto engine=create_engine();engine->RenderGradeFrame(program,source.view(),result.view(),{1,2,1,1.25});
    for(const auto v:result.pixels()) check(finite(v)&&v.a==1,"8-node finite alpha-preserving render");
    check(std::abs(result.pixels()[123].r-source.pixels()[123].r)>.001F,"E2E must change pixels");
    const auto scopes=engine->ComputeVideoScopes(result.view(),{64,32,0,1,Gamut::Rec709});check(sum(scopes.histogram[3])==768,"E2E scopes");
    // Reuse the same 8-node chain in AP1/ACEScct -> DWG -> Rec2020/PQ.
    for(std::size_t i=0;i<source.pixels().size();++i) { const float x=.002F+.05F*static_cast<float>(i%32)/31;source.pixels()[i]={encode(x*.8F,Transfer::ACEScct),encode(x*.9F,Transfer::ACEScct),encode(x,Transfer::ACEScct),1}; }
    Node input;input.id=1;c={};c.input_gamut=Gamut::ACESAP1;c.input_transfer=Transfer::ACEScct;c.output_gamut=Gamut::DaVinciWide;input.operation=c;program.clip.replace(input);
    Node output;output.id=8;output.inputs={7};c={};c.input_gamut=Gamut::DaVinciWide;c.output_gamut=Gamut::Rec2020;c.output_transfer=Transfer::PQ;output.operation=c;program.clip.replace(output);
    engine->RenderGradeFrame(program,source.view(),result.view(),{1,2,2,1.25});for(const auto v:result.pixels()) check(finite(v)&&v.a==1&&v.r>=0&&v.r<=1,"8-node PQ output domain");
    std::cout<<"E2E:8-node SDR and HDR/PQ CPU grades + scopes PASS (not tracker/Metal/Vulkan execution)\n";
}
}
int main() { try { science();tools();graph_and_frames();robustness();end_to_end();std::cout<<assertions<<" assertions PASS\n";return 0; } catch(const std::exception& e) { std::cerr<<"FAIL after "<<assertions<<": "<<e.what()<<'\n';return 1; } }

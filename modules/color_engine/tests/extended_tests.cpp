#include "color/extended_tools.hpp"
#include "color/tracking.hpp"
#include "color/grade_archive.hpp"
#include "color/mastering.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace color;
namespace {
std::size_t assertions{};
void check(bool ok,const char* message) { ++assertions;if(!ok) throw std::runtime_error(message); }
void near(float a,float b,float epsilon=1e-5F) { check(std::isfinite(a)&&std::abs(a-b)<epsilon,"Extended numerical reference mismatch"); }
template<class F> void reject(F f) { bool caught=false;try{f();}catch(const std::exception&){caught=true;}check(caught,"Expected validation failure"); }
void statistics() {
    Image32F source(16,16),target(16,16),graded(16,16);
    for(std::size_t i=0;i<256;++i) { const float x=.05F+.5F*static_cast<float>(i)/255;source.pixels()[i]={x,x*.8F,x*.6F,1};target.pixels()[i]={2*x+.1F,1.5F*x*.8F+.03F,.7F*x*.6F+.2F,1}; }
    const auto a=analyze_image(source),b=analyze_image(target);const auto match=estimate_shot_match(a,b);
    for(std::size_t i=0;i<256;++i) { const auto p=source.pixels()[i];const auto q=apply({p.r,p.g,p.b},match);const auto r=target.pixels()[i];near(q.r,r.r);near(q.g,r.g);near(q.b,r.b); }
    const auto auto_grade=estimate_auto_balance(a);const auto mean=apply(a.mean,auto_grade);near(mean.r,mean.g);near(mean.g,mean.b);
    const auto exposure=estimate_auto_balance(a,{.18F,8,0});near(a.median_positive_luma*std::exp2(exposure.exposure),.18F);
    Image32F hidden(2,2);reject([&]{(void)analyze_image(hidden);});for(auto& p:hidden.pixels()) p={0,0,0,1};reject([&]{(void)estimate_auto_balance(analyze_image(hidden));});
    const auto flat=analyze_image(hidden);reject([&]{(void)estimate_shot_match(flat,a);});
    std::cout<<"analysis:alpha-weighted moments/auto exposure+WB/statistical shot-match PASS\n";
}
void raw() {
    constexpr unsigned colors[4][4]={{0,1,1,2},{2,1,1,0},{1,0,2,1},{1,2,0,1}};
    for(unsigned pattern=0;pattern<4;++pattern) {
        std::vector<std::uint16_t> data(8*10,65535);constexpr std::uint16_t values[3]={264,464,664};
        for(std::size_t y=0;y<8;++y) for(std::size_t x=0;x<8;++x) data[y*10+x]=values[colors[pattern][(y%2)*2+x%2]];
        RawDecodeParams p;p.black_level={64,64,64};p.white_level={1064,1064,1064};auto image=demosaic_bayer16(data,8,8,10,static_cast<BayerPattern>(pattern),p);
        for(auto v:image.pixels()) { near(v.r,.2F);near(v.g,.4F);near(v.b,.6F);near(v.a,1); }
        reject([&]{(void)demosaic_bayer16(std::span<const std::uint16_t>(data.data(),3),8,8,10,static_cast<BayerPattern>(pattern),p);});
    }
    std::cout<<"raw:4 Bayer patterns/strided16bit/borders/black+white decode/truncation PASS\n";
}
void spatial() {
    Image32F noisy(16,16),clean(16,16),impulse(16,16),out(16,16);
    for(std::size_t i=0;i<256;++i) { const float x=(i%2)? .6F:.4F;noisy.pixels()[i]={x,x,x,1};impulse.pixels()[i]={0,0,0,0}; }
    SpatialFilter p;p.radius=1;p.range_sigma=.3F;process_spatial(noisy,clean,p);check(analyze_image(clean).deviation.r<analyze_image(noisy).deviation.r,"Bilateral must suppress noise");
    impulse.pixels()[8*16+8]={1,1,1,1};p.kind=SpatialKind::MatteDilate;process_spatial(impulse,out,p);std::size_t on=0;for(auto v:out.pixels()) on+=v.a>.5F?1U:0U;check(on==9,"Square morphological dilation");
    p.kind=SpatialKind::MatteErode;process_spatial(out,out,p);on=0;for(auto v:out.pixels()) on+=v.a>.5F?1U:0U;check(on==1,"In-place morphological erosion");
    p.kind=SpatialKind::MatteBlur;process_spatial(impulse,out,p);check(out.pixels()[8*16+8].a<1&&out.pixels()[8*16+7].a>0,"Matte feather neighbors");
    p.kind=SpatialKind::Unsharp;process_spatial(impulse,out,p);check(out.pixels()[8*16+8].r>1,"Real high-frequency enhancement");
    p.kind=SpatialKind::Halation;p.threshold=.1F;process_spatial(impulse,out,p);check(out.pixels()[8*16+7].r>0&&out.pixels()[8*16+7].r>out.pixels()[8*16+7].g,"Spatial halation warm glow");
    p.kind=SpatialKind::TemporalDenoise;p.range_sigma=1;process_spatial(noisy,out,p,&clean);check(analyze_image(out).deviation.r<analyze_image(noisy).deviation.r,"Temporal blend noise reduction");reject([&]{process_spatial(noisy,out,p);});
    p.kind=SpatialKind::Dehaze;p.amount=.8F;for(auto& v:clean.pixels()) v={.44F,.58F,.72F,1};process_spatial(clean,out,p);check(out.pixels()[0].b-out.pixels()[0].r>.28F,"Dark-channel contrast recovery");
    const auto before=out.pixels()[0];noisy.pixels().back().r=std::numeric_limits<float>::quiet_NaN();reject([&]{process_spatial(noisy,out,p);});near(out.pixels()[0].r,before.r);
    std::cout<<"spatial:bilateral/unsharp/erode/dilate/feather/temporal/dehaze/halation/atomicity PASS\n";
}
void tracking() {
    Image32F previous(64,64),current(64,64),resized(64,64);
    auto image=[](float x,float y){return .5F+.1F*std::sin(.18F*x)+.1F*std::cos(.23F*y)+.12F*std::sin(.11F*x+.2F*y);};
    for(std::size_t y=0;y<64;++y) for(std::size_t x=0;x<64;++x) {float a=image(static_cast<float>(x),static_cast<float>(y)),b=image(static_cast<float>(x)-2,static_cast<float>(y)+1);previous.pixels()[y*64+x]={a,a,a,1};current.pixels()[y*64+x]={b,b,b,1};}
    const std::array<FeaturePoint,2> features{{{1,30,30},{2,35,35}}};auto tracked=track_klt(previous,current,features);
    for(const auto& point:tracked) {check(point.status==TrackStatus::Good,"KLT valid translation");near(point.target.x,point.source.x+2,.08F);near(point.target.y,point.source.y-1,.08F);}
    check(!detect_features(previous,16,5,.00001F).empty(),"Shi-Tomasi features");
    Image32F flat(32,32);for(auto& p:flat.pixels()) p={.5F,.5F,.5F,1};auto failed=track_klt(flat,flat,std::array<FeaturePoint,1>{{{1,16,16}}});check(failed[0].status==TrackStatus::Singular,"Flat patch must not pretend to track");
    std::vector<TrackedPoint> correspondences;Affine2D expected;expected.m={1.02F,.03F,2,-.02F,.98F,-1};
    for(std::size_t i=0;i<25;++i) {FeaturePoint p{i+1,static_cast<float>((i%5)*10),static_cast<float>((i/5)*10)};auto q=expected.apply({p.x,p.y});if(i%5==0) {q.x+=20;q.y-=20;}correspondences.push_back({p,{p.id,q.x,q.y},TrackStatus::Good,0});}
    auto plane=fit_planar_affine(correspondences,.05F,256);check(plane.inlier_ids.size()==20,"RANSAC affine rejects outliers");for(std::size_t i=0;i<6;++i) near(plane.transform.m[i],expected.m[i],.0001F);
    resize_affine(previous,resized,Affine2D{});for(std::size_t i=0;i<previous.pixels().size();++i) near(previous.pixels()[i].r,resized.pixels()[i].r);
    Affine2D moved;moved.m[2]=-1;resize_affine(previous,resized,moved);near(resized.pixels()[0].a,0);near(resized.pixels()[1].r,previous.pixels()[0].r);
    std::cout<<"tracking:Shi-Tomasi/pyramidal KLT/flat rejection/robust affine RANSAC/identity+translated sizing PASS\n";
}
void archive() {
    const auto curve=std::make_shared<Curve>(std::array<CurvePoint,3>{{{0,0},{.5F,.6F},{1,1}}});std::vector<Vec3> table;
    for(int b=0;b<2;++b) for(int g=0;g<2;++g) for(int r=0;r<2;++r) table.push_back({static_cast<float>(r),static_cast<float>(g),static_cast<float>(b)});
    PolygonWindow window;window.count=3;window.vertices[0]={0,0};window.vertices[1]={1,0};window.vertices[2]={.5F,1};
    std::vector<Operator> operators{std::monostate{},CST{},RawDecodeParams{},Primaries{},LogWheels{},HDRWheels{},ColorSlice{},RGBMixer{},HSLQualifier{},PowerWindow{},Curves{curve,curve,{},{}},std::make_shared<Lut3D>(2,table),Grain{.1F,.5F,0xffffffffU},SpatialFilter{},window};
    for(auto op:operators) {GradeProgram p;Node n;n.id=7;n.operation=op;n.label="সংরক্ষিত grade";n.locked=true;p.clip.add(n);const auto bytes=serialize_grade(p);auto q=deserialize_grade(bytes);check(bytes==serialize_grade(q),"All operator archives canonical round-trip");check(!q.clip.has_explicit_output(),"Preserve default output semantics");reject([&]{(void)deserialize_grade(std::span<const std::byte>(bytes.data(),bytes.size()-1));});auto broken=bytes;broken.back()^=std::byte{1};reject([&]{(void)deserialize_grade(broken);});}
    GradeProgram p;Node n;n.id=1;Primaries grade;grade.exposure=.7F;n.operation=grade;p.clip.add(n);
    LookGallery gallery(std::filesystem::path(COLOR_TEST_OUTPUT_DIR)/"gallery");gallery.store("look_1",p);auto loaded=gallery.recall("look_1");check(serialize_grade(p)==serialize_grade(loaded),"Disk gallery recall");grade.exposure=1.2F;n.operation=grade;p.clip.replace(n);gallery.store("look_1",p);check(serialize_grade(p)==serialize_grade(gallery.recall("look_1")),"Atomic overwrite");check(gallery.list().size()==1,"Gallery listing");reject([&]{gallery.store("../escape",p);});gallery.erase("look_1");check(gallery.list().empty(),"Gallery erase");
    std::cout<<"archive:all15 operator types/curves+LUT/UTF8/version+integrity/truncation/atomic disk gallery PASS\n";
}
void mastering() {
    auto engine=create_engine();Image32F input(4,4);for(auto& v:input.pixels()) v={.01F,.02F,.03F,.8F};
    std::array<MasterSpec,2> specs;specs[0].name="SDR";specs[0].output_transform.output_transfer=Transfer::SRGB;specs[1].name="HDR";specs[1].trim.exposure=1;specs[1].output_transform.output_transfer=Transfer::PQ;
    auto frames=render_masters(*engine,{},input.view(),{1,2,3,10.0,240},specs);check(frames.size()==2&&frames[1].name=="HDR","Distinct master outputs");near(frames[0].image.pixels()[0].r,encode(.01F,Transfer::SRGB));near(frames[1].image.pixels()[0].r,encode(.02F,Transfer::PQ));near(input.pixels()[0].r,.01F);near(frames[1].image.pixels()[0].a,.8F);
    reject([&]{(void)render_masters(*engine,{},input.view(),{},specs,16*sizeof(Pixel)*3);});specs[1].name="SDR";reject([&]{(void)render_masters(*engine,{},input.view(),{},specs);});
    std::cout<<"mastering:independent SDR/PQ trims/names/budget/source+alpha preservation PASS\n";
}
void graph() {
    auto engine=create_engine();Image32F input(12,12),out(12,12),reference(12,12);for(auto& v:input.pixels()) v={.1F,.1F,.1F,1};input.pixels()[6*12+6]={2,2,2,1};
    SpatialFilter filter;filter.kind=SpatialKind::Halation;filter.radius=1;filter.threshold=.5F;process_spatial(input,reference,filter);
    GradeProgram program;Node node;node.id=1;node.operation=filter;program.clip.add(node);engine->RenderGradeFrame(program,input.view(),out.view(),{});
    for(std::size_t i=0;i<144;++i) { near(out.pixels()[i].r,reference.pixels()[i].r);near(out.pixels()[i].a,reference.pixels()[i].a); }
    Node next;next.id=2;next.inputs={1};Primaries e;e.exposure=1;next.operation=e;program.clip.add(next);engine->RenderGradeFrame(program,input.view(),out.view(),{});near(out.pixels()[78].r,reference.pixels()[78].r*2);
    program.group_post.add(node);program.timeline.add(node);engine->RenderGradeFrame(program,input.view(),out.view(),{});check(finite(out.pixels()[78]),"Pooled spatial pass order");
    const float before=out.pixels()[0].r;auto tiny=create_engine({144*sizeof(Pixel)*2,2});reject([&]{tiny->RenderGradeFrame(program,input.view(),out.view(),{});});near(out.pixels()[0].r,before);
    PolygonWindow polygon;polygon.count=4;polygon.vertices[0]={.2F,.2F};polygon.vertices[1]={.8F,.2F};polygon.vertices[2]={.8F,.8F};polygon.vertices[3]={.2F,.8F};near(polygon_mask(.5F,.5F,polygon),1);near(polygon_mask(.1F,.5F,polygon),0);polygon.feather=.1F;near(polygon_mask(.2F,.5F,polygon),.5F);
    GradeProgram matte;node={};node.id=1;node.operation=polygon;matte.clip.add(node);next={};next.id=2;PowerWindow window;window.cx=.8F;next.operation=window;matte.clip.add(next);Node combine;combine.id=3;combine.kind=NodeKind::MatteUnion;combine.inputs={1,2};matte.clip.add(combine);engine->RenderGradeFrame(matte,input.view(),out.view(),{});near(out.pixels()[6*12+6].a,1);
    std::cout<<"DAG:spatial nodes/liveness pool/mixed chains/budget atomicity/polygon/matte algebra PASS\n";
}
}
void features() {
    const auto lut=Lut1D::parse_cube("LUT_1D_SIZE 3\n0 0 0\n0.5 0.5 0.5\n1 1 1\n");
    check(lut.size()==3,"Lut1D size");near(lut.apply({.25F,.25F,.25F}).r,.25F,1e-4F);near(lut.apply({.75F,.75F,.75F}).g,.75F,1e-4F);
    reject([]{(void)Lut1D::parse_cube("LUT_1D_SIZE 1\n0 0 0\n");});
    reject([]{(void)Lut1D::parse_cube("LUT_1D_SIZE 3\n0 0 0\n");});
    reject([]{(void)Lut1D::parse_cube("LUT_1D_SIZE 2\n0 0 nan\n1 1 1\n");});
    // Rounded rectangle: sharp corner is inside the plain rectangle but outside the rounded one.
    PowerWindow sharp;sharp.shape=PowerWindow::Shape::Rectangle;sharp.sx=sharp.sy=.3F;sharp.feather=0;
    PowerWindow round=sharp;round.shape=PowerWindow::Shape::RoundedRectangle;round.corner_radius=.2F;
    near(window_mask(.5F+.29F,.5F+.29F,sharp),1);near(window_mask(.5F+.29F,.5F+.29F,round),0);near(window_mask(.5F,.5F,round),1);
    PowerWindow bad=round;bad.corner_radius=.5F;reject([&]{(void)window_mask(.5F,.5F,bad);});
    // Depth hook: band mask around depth 0.5.
    const std::vector<float> depth{0,.5F,1,.5F};const auto matte=depth_matte(depth,4,1,DepthRange{.5F,.2F,0,false});
    near(matte->pixels()[0].a,0);near(matte->pixels()[1].a,1);near(matte->pixels()[2].a,0);
    const auto inverted=depth_matte(depth,4,1,DepthRange{.5F,.2F,0,true});near(inverted->pixels()[1].a,0);
    const std::vector<float> nan_depth{0,std::numeric_limits<float>::quiet_NaN(),1,1};reject([&]{(void)depth_matte(nan_depth,2,2,DepthRange{});});
    // Reframe: Fit letterboxes 200x100 into 100x100; Fill crops; Stretch distorts.
    const auto fit=reframe_transform(200,100,100,100,ReframeMode::Fit);const auto p0=fit.apply({0,25});near(p0.x,0);near(p0.y,0);
    const auto fill=reframe_transform(200,100,100,100,ReframeMode::Fill);near(fill.apply({0,0}).x,50);
    const auto stretch=reframe_transform(200,100,100,100,ReframeMode::Stretch);near(stretch.apply({10,10}).x,20);near(stretch.apply({10,10}).y,10);
    // ExternalPlane: dimension-checked matte source inside the materialized path.
    auto engine=create_engine();auto plane=std::make_shared<Image32F>(8,8);for(auto& v:plane->pixels()) v={0,0,0,1};
    GradeProgram program;Node node;node.id=1;node.operation=ExternalPlane{plane};program.clip.add(node);
    Image32F input(8,8),out(8,8);for(auto& v:input.pixels()) v={.2F,.2F,.2F,1};
    engine->RenderGradeFrame(program,input.view(),out.view(),{});
    auto wrong=std::make_shared<Image32F>(4,4);for(auto& v:wrong->pixels()) v={0,0,0,1};
    GradeProgram mismatch;Node bad_node;bad_node.id=1;bad_node.operation=ExternalPlane{wrong};mismatch.clip.add(bad_node);
    reject([&]{engine->RenderGradeFrame(mismatch,input.view(),out.view(),{});});
    std::cout<<"Features: Lut1D/rounded window/depth matte/reframe/ExternalPlane PASS\n";
}
// Lookup error of baked curves (same linear lookup as baked_curve in grade.inc). The GPU engine picks the
// smallest table size meeting 1e-4; this reference test documents how much each size is off.
void curve_bake_error() {
    const std::array<std::array<CurvePoint,4>,3> shapes{{
        {{{0,0},{.3F,.2F},{.7F,.8F},{1,1}}},
        {{{0,0},{.499F,.001F},{.501F,.999F},{1,1}}},
        {{{0,0},{.5F,.5F},{.9F,.97F},{1,1}}}}};
    for(std::size_t n:{std::size_t{4096},std::size_t{16384},std::size_t{65536}}) {
        double worst=0;
        for(const auto& points:shapes) {
            const Curve curve(points);const auto table=curve.bake(n);check(table.size()==n,"Baked curve size");
            for(std::size_t k=0;k<=100000;++k) {
                const float x=static_cast<float>(k)/100000.F;const float t=x*static_cast<float>(n-1);const auto i=std::min<std::size_t>(static_cast<std::size_t>(t),n-2);
                const float f=t-static_cast<float>(i);const float lookup=table[i]*(1-f)+table[i+1]*f;
                worst=std::max(worst,static_cast<double>(std::abs(lookup-curve.evaluate(x))));
            }
        }
        std::cout<<"Curve bake n="<<n<<": max abs lookup error "<<worst<<"\n";
        if(n==65536) check(worst<2e-4,"Baked 65536-sample curve exceeds 2e-4 contract");
    }
}

int main() {try{curve_bake_error();statistics();raw();spatial();tracking();archive();mastering();graph();features();std::cout<<assertions<<" extended assertions PASS\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL after "<<assertions<<": "<<e.what()<<'\n';return 1;}}

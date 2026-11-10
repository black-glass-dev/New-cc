#include "editor/color.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <source_location>
#include <sstream>
#include <vector>

using namespace editor::color;
namespace {
void check(bool ok, std::source_location at = std::source_location::current()) {
    if (!ok) throw std::runtime_error("color assertion at line "+std::to_string(at.line()));
}
void near(double actual,double expected,double tolerance=1e-12) { check(std::abs(actual-expected) <= tolerance); }
template<class F> void fails(F f) { bool caught=false; try { f(); } catch(const std::exception&) { caught=true; } check(caught); }
void wheel_and_hdr_metadata() {
    check(!hdr_static_info_present({}));
    std::array<std::uint8_t,25> empty{};
    check(!hdr_static_info_present(empty));
    for(std::size_t i=0;i<empty.size();++i) {
        auto changed=empty;changed[i]=1;check(hdr_static_info_present(changed));
    }
    for(std::size_t size=1;size<100;++size) {
        std::vector<std::uint8_t> bytes(size,0);
        check(hdr_static_info_present(bytes)==(size!=25));
    }
    for(int group=0;group<4;++group) {
        const Grade identity{};
        check(wheel_adjust(identity,group,0,0)==identity);
        const auto neutral=wheel_position(identity,group);near(neutral[0],0);near(neutral[1],0);
        const auto red=wheel_adjust(identity,group,0.1,0);
        const auto point=wheel_position(red,group);near(point[0],0.1);near(point[1],0);
        for(int x=-10;x<=10;++x) for(int y=-10;y<=10;++y) {
            auto grade=wheel_adjust(identity,group,x/10.0,y/10.0);validate(grade);
            const auto position=wheel_position(grade,group);
            check(std::hypot(position[0],position[1])<=1.0000000001);
            check(grade.enabled==identity.enabled && grade.exposure_stops==identity.exposure_stops);
        }
    }
    // Wheel screen hue directions must bias neutral pixels toward the same RGB hue.
    for(int group=0;group<4;++group) for(int sector=0;sector<6;++sector) {
        const auto angle=static_cast<double>(sector)*std::acos(-1.0)/3;
        const auto grade=wheel_adjust({},group,0.1*std::cos(angle),0.1*std::sin(angle));
        const auto pixel=Pipeline(grade).linear({0.18,0.18,0.18});
        const std::array<double,3> values{pixel.r,pixel.g,pixel.b};
        const std::array<std::array<int,3>,6> order{{{0,1,2},{0,2,1},{2,0,1},{1,2,0},{1,0,2},{0,1,2}}};
        const auto selected=order[static_cast<std::size_t>(sector)];
        if(sector%2==0) { check(values[selected[0]]>values[selected[1]]);near(values[selected[1]],values[selected[2]]); }
        else { near(values[selected[0]],values[selected[1]]);check(values[selected[1]]>values[selected[2]]); }
    }
    Grade custom;custom.enabled=false;custom.exposure_stops=2;
    check(!wheel_adjust(custom,0,0.5,0.5).enabled);
    check(wheel_adjust(custom,1,0.5,0.5).exposure_stops==2);
    fails([]{(void)wheel_adjust({},4,0,0);});
    fails([]{(void)wheel_adjust({},0,std::numeric_limits<double>::quiet_NaN(),0);});
    fails([]{(void)wheel_adjust({},0,2,0);});
}
void comparisons() {
    const std::vector<std::uint8_t> expected{10,20,30,40,100,110,120,130,77,77,77,77};
    const std::vector<std::uint8_t> actual{15,18,30,41,90,110,125,130,99,99,99,99};
    const auto result=compare_rgba8({expected,2,1,12},{actual,2,1,12});
    check(result.pixels==2 && result.alpha_mismatches==1);
    check(result.maximum_rgb_error==std::array<std::uint8_t,3>{10,2,5});
    near(result.mean_absolute_rgb_error,22.0/6);near(result.mean_signed_rgb_error,-2.0/6);
    const auto alias=compare_rgba8({expected,2,1,12},{expected,2,1,8});
    near(alias.mean_absolute_rgb_error,0);check(alias.alpha_mismatches==0);
    fails([&]{(void)compare_rgba8({expected,2,1,8},{actual,1,2,4});});
    fails([&]{(void)compare_rgba8({expected,2,2,12},{actual,2,2,12});});
    fails([&]{(void)compare_rgba8({expected,2,1,7},{actual,2,1,8});});
    fails([&]{(void)compare_rgba8({expected,0,1,8},{actual,0,1,8});});
    std::vector<std::uint8_t> padded(24,77),packed(16,99);
    for(std::size_t y=0;y<2;++y) for(std::size_t c=0;c<8;++c) padded[y*12+c]=packed[y*8+c]=static_cast<std::uint8_t>(c+y);
    near(compare_rgba8({padded,2,2,12},{packed,2,2,8}).mean_absolute_rgb_error,0);
}
void transfer_and_controls() {
    near(decode_srgb(0.5),0.21404114048223255);
    near(encode_srgb(0.21404114048223255),0.5);
    near(decode_srgb(0.04045),0.04045/12.92);
    near(encode_srgb(0.0031308),0.0031308*12.92);
    for (double x : {-2.0,-0.5,0.0,0.002,0.5,1.0,2.0}) near(encode_srgb(decode_srgb(x)),x,1e-7);
    const Rgb input{0.1,0.25,0.75};
    check(Pipeline({}).linear(input) == input);
    Grade g; g.exposure_stops=1;
    auto result=Pipeline(g).linear(input); near(result.r,0.2); near(result.g,0.5); near(result.b,1.5);
    g={}; g.balance_stops={1,0,-1}; result=Pipeline(g).linear(input);
    near(result.r,0.2); near(result.g,0.25); near(result.b,0.375);
    g={}; g.contrast=2; result=Pipeline(g).linear({0.18,0.18,0.18}); near(result.r,0.18);
    near(Pipeline(g).linear({0.1,0.1,0.1}).r,0.02);
    g={}; g.saturation=0; result=Pipeline(g).linear({1,0,0});
    near(result.r,0.2126); near(result.g,0.2126); near(result.b,0.2126);
    g={}; g.lift={0.1,0,0}; near(Pipeline(g).linear({0,0,0}).r,0.1);
    g={}; g.gain={2,1,1}; near(Pipeline(g).linear(input).r,0.2);
    g={}; g.gamma={2,2,2}; result=Pipeline(g).linear({0.25,-0.25,0});
    near(result.r,0.5); near(result.g,-0.5); near(result.b,0);
    g={}; g.exposure_stops=1; g.strength=0.5; near(Pipeline(g).linear(input).r,0.15);
    g.strength=0; check(Pipeline(g).linear(input) == input && Pipeline(g).identity());
    g.strength=1; g.enabled=false; check(Pipeline(g).linear(input) == input);
    fails([]{ (void)decode_srgb(std::numeric_limits<double>::infinity()); });
    fails([]{ (void)encode_srgb(std::numeric_limits<double>::quiet_NaN()); });
    fails([]{ (void)Pipeline({}).linear({65505,0,0}); });
    for (auto value : {-9.0,9.0,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
        g={}; g.exposure_stops=value; fails([&]{ (void)Pipeline(g); });
    }
    g={}; g.gamma[1]=0; fails([&]{ (void)Pipeline(g); });
    g={}; g.balance_stops[2]=3; fails([&]{ (void)Pipeline(g); });
    g={}; g.pivot=0; fails([&]{ (void)Pipeline(g); });
    g={}; g.strength=1.1; fails([&]{ (void)Pipeline(g); });
    g={}; g.saturation=-1; fails([&]{ (void)Pipeline(g); });
}
void parameter_boundaries() {
    const ParameterBlock low{-8,0,0,0.001,0,-2,-2,-2,-1,-1,-1,0.1,0.1,0.1,0,0,0};
    const ParameterBlock high{8,4,4,1,1,2,2,2,1,1,1,4,4,4,4,4,4};
    for (std::size_t i=0;i<parameter_count;++i) {
        auto values=parameters({});
        values[i]=low[i]; (void)Pipeline(from_parameters(values));
        values[i]=high[i]; (void)Pipeline(from_parameters(values));
        for (const auto bad : {low[i]-1,high[i]+1,std::numeric_limits<double>::quiet_NaN(),
                               std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity()}) {
            values[i]=bad; fails([&]{ (void)from_parameters(values); });
        }
    }
    // Stateless point processing gives the same result when a frame is tiled/cropped.
    std::vector<std::uint8_t> source(32*32*4), full(source.size()), tiled(source.size());
    for(std::size_t i=0;i<source.size();++i) source[i]=static_cast<std::uint8_t>(i*17);
    Grade g; g.exposure_stops=0.7; g.saturation=0.6; g.gamma={0.8,1.2,1.5}; g.strength=0.8;
    check(apply_rgba8({source,32,32,128},{full,32,32,128},g));
    for(std::size_t row=0;row<32;++row)
        check(apply_rgba8({std::span(source).subspan(row*128,128),32,1,128},
                         {std::span(tiled).subspan(row*128,128),32,1,128},g));
    check(full==tiled);
}
void image_layout_and_pixels() {
    // Includes transparent hidden color, row padding and all 256 possible byte values.
    std::vector<std::uint8_t> source(2*1032,0xAD), output(2*1040,0xCD);
    for (std::size_t y=0;y<2;++y) for(std::size_t x=0;x<256;++x) {
        const auto p=y*1032+x*4;
        source[p]=static_cast<std::uint8_t>(x); source[p+1]=static_cast<std::uint8_t>(255-x);
        source[p+2]=127; source[p+3]=static_cast<std::uint8_t>(x);
    }
    const auto original=source;
    check(apply_rgba8({source,256,2,1032},{output,256,2,1040},{}));
    for(std::size_t y=0;y<2;++y) {
        check(std::equal(source.begin()+static_cast<std::ptrdiff_t>(y*1032),source.begin()+static_cast<std::ptrdiff_t>(y*1032+1024),output.begin()+static_cast<std::ptrdiff_t>(y*1040)));
        check(std::all_of(output.begin()+static_cast<std::ptrdiff_t>(y*1040+1024),output.begin()+static_cast<std::ptrdiff_t>((y+1)*1040),[](auto x){ return x==0xCD; }));
    }
    check(source==original);
    std::array<std::uint8_t,8> pixels{128,128,128,37,255,0,0,0}, rendered{};
    Grade g; g.exposure_stops=1;
    check(apply_rgba8({pixels,2,1,8},{rendered,2,1,8},g));
    check(rendered == std::array<std::uint8_t,8>{176,176,176,37,255,0,0,0});
    g={}; g.balance_stops={1,0,0};
    check(apply_rgba8({pixels,2,1,8},{rendered,2,1,8},g));
    check(rendered[0]==176 && rendered[1]==128 && rendered[2]==128 && rendered[3]==37);
    const auto before=rendered;
    fails([&]{ (void)apply_rgba8({pixels,2,1,8},{pixels,2,1,8},{}); });
    fails([&]{ (void)apply_rgba8({pixels,2,1,7},{rendered,2,1,8},{}); });
    fails([&]{ (void)apply_rgba8({pixels,2,2,8},{rendered,2,1,8},{}); });
    fails([&]{ (void)apply_rgba8({pixels,0,1,8},{rendered,2,1,8},{}); });
    fails([&]{ (void)apply_rgba8({pixels,std::numeric_limits<std::size_t>::max(),2,8},{rendered,2,1,8},{}); });
    fails([&]{ (void)apply_rgba8({pixels,2,2,std::numeric_limits<std::size_t>::max()},{rendered,2,1,8},{}); });
    fails([&]{ (void)apply_rgba8({pixels,2,1,8},{rendered,1,1,8},{}); });
    g={}; g.gamma[0]=0;
    fails([&]{ (void)apply_rgba8({pixels,2,1,8},{rendered,2,1,8},g); });
    check(rendered==before); // all structural/parameter failures before output writes
    std::atomic_bool cancelled{true};
    check(!apply_rgba8({pixels,2,1,8},{rendered,2,1,8},{},&cancelled)); check(rendered==before);
    // Adjacent non-overlapping views in the same allocation are legal.
    std::array<std::uint8_t,16> adjacent{128,128,128,255};
    check(apply_rgba8({std::span(adjacent).first(8),2,1,8},{std::span(adjacent).subspan(8),2,1,8},{}));
    check(adjacent[8]==128 && adjacent[11]==255);
    fails([&]{ (void)apply_rgba8({std::span(adjacent).first(8),2,1,8},{std::span(adjacent).subspan(4,8),2,1,8},{}); });
}
void histograms() {
    const std::array<std::uint8_t,16> pixels{0,0,0,255,255,255,255,255,255,0,0,255,128,128,128,0};
    const auto h=histogram({pixels,4,1,16});
    check(h.pixels==3 && h.red[255]==2 && h.green[0]==2 && h.luminance[0]==1 && h.luminance[255]==1);
    check(std::accumulate(h.luminance.begin(),h.luminance.end(),std::uint64_t{0})==3);
    const auto all=histogram({pixels,4,1,16},false);
    check(all.pixels==4 && all.luminance[128]==1);
    fails([&]{ (void)histogram({pixels,4,2,16}); });
}
std::string encode(const Grade& g) { std::ostringstream out(std::ios::binary); PresetCodec::write(g,out); return out.str(); }
Grade decode(const std::string& data) { std::istringstream in(data,std::ios::binary); return PresetCodec::read(in); }
void presets_and_random_images() {
    Grade g; g.enabled=false; g.exposure_stops=2; g.balance_stops={0.5,-0.2,1.0};
    g.gamma={0.5,1.4,2}; g.lift={-0.1,0.1,0.5}; g.gain={0,2,4}; g.strength=0.7;
    auto data=encode(g); check(data.size()==PresetCodec::encoded_size && data.substr(0,8)=="OWNCUTCG");
    check(decode(data)==g);
    check(from_parameters(parameters(g),g.enabled)==g);
    fails([]{ (void)from_parameters({}); });
    for (std::size_t i=0;i<data.size();++i) fails([&]{ (void)decode(data.substr(0,i)); });
    fails([&]{ (void)decode(data+"x"); });
    auto future=data; future[8]=2;
    bool unsupported=false; try { (void)decode(future); } catch(const UnsupportedGradeVersion&) { unsupported=true; }
    check(unsupported);
    auto bad=data; bad[12]=2; fails([&]{ (void)decode(bad); });
    bad=data; bad[0]='X'; fails([&]{ (void)decode(bad); });
    // Replace exposure binary64 with +Inf (little endian).
    bad=data; std::fill(bad.begin()+13,bad.begin()+21,0); bad[19]=static_cast<char>(0xF0); bad[20]=0x7F;
    fails([&]{ (void)decode(bad); });
    g.gamma[0]=0; std::ostringstream untouched; fails([&]{ PresetCodec::write(g,untouched); }); check(untouched.str().empty());
    std::ostringstream failed; failed.setstate(std::ios::badbit); fails([&]{ PresetCodec::write({},failed); });
    std::istringstream read_failed(data); read_failed.setstate(std::ios::badbit); fails([&]{ (void)PresetCodec::read(read_failed); });
    std::mt19937 generator(641);
    std::vector<std::uint8_t> source(32*32*4), output(source.size());
    for(auto& value:source) value=static_cast<std::uint8_t>(generator());
    const auto original=source;
    for(unsigned i=0;i<48;++i) {
        Grade settings; settings.exposure_stops=static_cast<double>(i%17)-8;
        settings.gamma.fill(i%2 ? 0.1 : 4); settings.gain.fill(i%3 ? 4 : 0);
        settings.lift.fill(i%2 ? -1 : 1); settings.saturation=i%2 ? 0 : 4; settings.contrast=i%2 ? 4 : 0;
        check(apply_rgba8({source,32,32,128},{output,32,32,128},settings));
        check(source==original);
        for(std::size_t p=3;p<source.size();p+=4) check(output[p]==source[p]);
        check(decode(encode(settings))==settings);
    }
}
} // namespace
int main() {
    wheel_and_hdr_metadata(); comparisons(); transfer_and_controls(); parameter_boundaries(); image_layout_and_pixels(); histograms(); presets_and_random_images();
    std::cout << "C++ grading: transfer, controls, RGBA pixels/alpha/stride, cancellation, histogram, presets and randomized extremes passed\n";
}

#include "editor/color_gpu.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <locale>
#include <source_location>
using namespace editor::color;
namespace {
void check(bool b,std::source_location at=std::source_location::current()) {if(!b)throw std::runtime_error("GPU color assertion line "+std::to_string(at.line()));}
template<class F> void fails(F f){bool b=false;try{f();}catch(const std::exception&){b=true;}check(b);}
struct Comma : std::numpunct<char> { char do_decimal_point() const override{return ',';} };
void tests() {
    for (int p=-1;p<=3;++p) for (int t=-1;t<=6;++t) for (int r=-1;r<=3;++r) {
        const VideoColorMetadata metadata{static_cast<VideoPrimaries>(p),static_cast<VideoTransfer>(t),static_cast<VideoRange>(r)};
        if(p==1 && t==1 && (r==1 || r==2)) validate_sdr_video_source(metadata);
        else fails([&]{validate_sdr_video_source(metadata);});
    }

    fails([]{validate_sdr_video_source({VideoPrimaries::rec709,VideoTransfer::sdr_video,VideoRange::full,true});});
    const DecodedVideoFormat expected{1920,1080,{VideoPrimaries::rec709,VideoTransfer::sdr_video,VideoRange::limited}};
    validate_decoded_video_format(expected,expected);
    auto actual=expected;actual.width=1280;fails([&]{validate_decoded_video_format(expected,actual);});
    actual=expected;actual.height=0;fails([&]{validate_decoded_video_format(expected,actual);});
    actual=expected;actual.color.range=VideoRange::full;fails([&]{validate_decoded_video_format(expected,actual);});
    actual=expected;actual.color.transfer=VideoTransfer::unknown;fails([&]{validate_decoded_video_format(expected,actual);});
    actual=expected;actual.color.has_hdr_static_metadata=true;fails([&]{validate_decoded_video_format(expected,actual);});
    actual=expected;actual.width=0;fails([&]{validate_decoded_video_format(actual,actual);});
    // HEVC padding is not a display-size change when a complete valid crop proves it.
    check(visible_video_geometry(2880,2176,15,{0,0,2879,2159})==std::array<int,2>{2880,2160});
    check(visible_video_geometry(2944,2176,15,{64,16,2943,2175})==std::array<int,2>{2880,2160});
    check(visible_video_geometry(2880,2160,0,{})==std::array<int,2>{2880,2160});
    check(visible_video_geometry(1,1,15,{0,0,0,0})==std::array<int,2>{1,1});
    check(visible_video_geometry(std::numeric_limits<int>::max(),std::numeric_limits<int>::max(),15,
        {0,0,std::numeric_limits<int>::max()-1,std::numeric_limits<int>::max()-1})==
        std::array<int,2>{std::numeric_limits<int>::max(),std::numeric_limits<int>::max()});
    for(int mask=-1;mask<=16;++mask) if(mask!=0 && mask!=15)
        fails([&]{(void)visible_video_geometry(2880,2176,mask,{0,0,2879,2159});});
    for(const auto crop : {std::array<int,4>{-1,0,2879,2159},{0,-1,2879,2159},{0,0,2880,2159},
        {0,0,2879,2176},{5,0,4,2159},{0,5,2879,4},{0,0,std::numeric_limits<int>::max(),2159}})
        fails([&]{(void)visible_video_geometry(2880,2176,15,crop);});
    fails([]{(void)visible_video_geometry(0,2160,0,{});});
    const DecodedVideoFormat hevc{2880,2160,expected.color};
    auto padded=hevc;const auto visible=visible_video_geometry(2880,2176,15,{0,0,2879,2159});
    padded.width=visible[0];padded.height=visible[1];validate_decoded_video_format(hevc,padded);
    padded.height=2176;fails([&]{validate_decoded_video_format(hevc,padded);});
    padded.width=2160;padded.height=2880;fails([&]{validate_decoded_video_format(hevc,padded);});
    check(grade_vertex_shader().find("#version 100") == 0);
    const Rgb input{0.5,0.25,0.75};
    for(auto e : {TextureEncoding::srgb,TextureEncoding::linear709,TextureEncoding::media3_sdr709}) {
        check(texture_reference(input,{},e)==input);
        check(grade_fragment_shader({},e).find("uniform highp sampler2D uSource;")!=std::string::npos);
        check(grade_fragment_shader({},e).find("toLight")==std::string::npos);
        Grade g;g.exposure_stops=1;
        const auto ref=texture_reference(input,g,e);
        check(ref.r>input.r);
        if(e==TextureEncoding::linear709) check(ref==Rgb{1.0,0.5,1.5});
        check(grade_fragment_shader(g,e).find("sampleColor.a")!=std::string::npos);
        check(grade_fragment_shader(g,e).find("uniform highp sampler2D uSource;")!=std::string::npos);
    }
    Grade g;g.exposure_stops=0.75;
    const auto before=grade_fragment_shader(g,TextureEncoding::srgb);
    auto old=std::locale();std::locale::global(std::locale(old,new Comma));
    const auto after=grade_fragment_shader(g,TextureEncoding::srgb);std::locale::global(old);
    check(before==after); // process locale must never corrupt GLSL constants
    auto excessive=Grade{};excessive.exposure_stops=8;excessive.balance_stops.fill(2);
    excessive.lift.fill(-1);excessive.gain.fill(4);excessive.gamma.fill(0.1);
    fails([&]{(void)grade_fragment_shader(excessive,TextureEncoding::srgb);});
    excessive.enabled=false;check(grade_fragment_shader(excessive,TextureEncoding::srgb).find("toLight")==std::string::npos);
    excessive.enabled=true;excessive.strength=0;check(grade_fragment_shader(excessive,TextureEncoding::srgb).find("toLight")==std::string::npos);
    fails([]{(void)grade_fragment_shader({},static_cast<TextureEncoding>(99));});
    fails([]{(void)texture_reference({-0.1,0,0},{},TextureEncoding::srgb);});
    fails([]{(void)texture_reference({std::numeric_limits<double>::quiet_NaN(),0,0},{},TextureEncoding::srgb);});
    auto invalid=Grade{};invalid.gamma[0]=0;
    fails([&]{(void)grade_fragment_shader(invalid,TextureEncoding::srgb);});
    // Explicit SDR transfer is intentionally not the sRGB reference used by phase 1A.
    auto exposure=Grade{};exposure.exposure_stops=1;
    check(std::abs(texture_reference(input,exposure,TextureEncoding::media3_sdr709).r-
                   texture_reference(input,exposure,TextureEncoding::srgb).r)>0.01);
}
}
int main(){tests();std::cout<<"Native GPU color contract, transfer profiles, safety budget, identity and locale tests passed\n";}

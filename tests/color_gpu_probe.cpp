// Host probe for the production shader/reference. Generated pixels, no media assets.
#include "editor/color_gpu.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
using namespace editor::color;
Grade sample_grade(int n) {
    Grade g;
    switch(n) {
    case 0: break;
    case 1: g.exposure_stops=1; break;
    case 2: g.contrast=1.4;g.saturation=0.6;g.balance_stops={0.2,-0.1,0.3};break;
    case 3: g.lift={0.02,-0.02,0.01};g.gamma={0.8,1.2,1.5};g.gain={1.1,0.8,1.3};g.strength=0.7;break;
    case 4: g.exposure_stops=3;g.enabled=false;break;
    case 5: g.exposure_stops=-2;g.saturation=0;break;
    case 6: g.exposure_stops=-8;g.gamma={4,4,4};break;
    case 7: g.lift={-0.2,-0.1,0.1};g.gamma={0.5,2,1};g.saturation=2;break;
    case 8: g.exposure_stops=8;g.balance_stops.fill(2);g.lift.fill(-1);g.gain.fill(4);g.gamma.fill(0.1);g.strength=0;break;
    default:
        if(n<9 || n>=32) throw std::invalid_argument("unknown probe grade");
        auto seed=static_cast<std::uint32_t>(n);
        auto unit=[&] {seed=seed*1664525U+1013904223U;return static_cast<double>(seed)/4294967295.0;};
        g.exposure_stops=unit()*6-3;g.contrast=unit()*3;g.saturation=unit()*4;
        g.pivot=0.05+unit()*0.45;g.strength=unit();
        for(std::size_t c=0;c<3;++c) {
            g.balance_stops[c]=unit()*2-1;g.lift[c]=unit()*0.4-0.2;
            g.gamma[c]=0.7+unit()*1.5;g.gain[c]=0.5+unit();
        }
        break;
    }
    return g;
}
int main(int argc,char** argv) {
    try {
        if(argc<2) return 2;
        const std::string mode=argv[1];
        if(mode=="vertex") {std::cout<<grade_vertex_shader();return 0;}
        if(argc!=4)return 2;
        const auto g=sample_grade(std::stoi(argv[2]));
        const auto encoding=static_cast<TextureEncoding>(std::stoi(argv[3])); validate(encoding);
        if(mode=="fragment") {std::cout<<grade_fragment_shader(g,encoding);return 0;}
        for(int i=0;i<512;++i) {
            const unsigned r=static_cast<unsigned>(i<256 ? i : (i*17)%256);
            const unsigned green=static_cast<unsigned>(i<256 ? i : (i*71)%256);
            const unsigned b=static_cast<unsigned>(i<256 ? i : (i*131)%256);
            auto rgb=Rgb{r/255.0,green/255.0,b/255.0};
            if(mode=="reference") rgb=texture_reference(rgb,g,encoding);
            else if(mode!="input")return 2;
            for(auto x : {rgb.r,rgb.g,rgb.b}) std::cout.put(static_cast<char>(static_cast<std::uint8_t>(std::floor(std::clamp(x,0.0,1.0)*255+0.5))));
            std::cout.put(static_cast<char>(i%256));
        }
    } catch(const std::exception& error) { std::cerr<<error.what();return 1; }
}

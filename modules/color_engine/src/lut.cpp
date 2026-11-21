#include "color/color_pipeline_api.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace color {
namespace { void check(bool b,const char* s) { if(!b) throw std::invalid_argument(s); } }
Lut3D::Lut3D(std::size_t edge,std::vector<Vec3> values,Vec3 low,Vec3 high):edge_(edge),values_(std::move(values)),low_(low),high_(high) {
    check(edge>=2&&edge<=65&&values_.size()==edge*edge*edge,"3D LUT edge/data budget2..65");
    check(finite(low)&&finite(high)&&high.r>low.r&&high.g>low.g&&high.b>low.b,"Invalid LUT domain");
    for(auto v:values_) check(finite(v),"Nonfinite LUT sample");
}
Lut3D Lut3D::parse_cube(std::string_view text,std::size_t max_edge) {
    check(text.size()<=32U*1024U*1024U&&max_edge>=2&&max_edge<=65,"LUT text/edge budget");
    std::istringstream stream{std::string(text)};stream.imbue(std::locale::classic());std::string line;std::size_t edge=0;
    Vec3 lo{},hi{1,1,1};bool seen_lo=false,seen_hi=false,seen_title=false;std::vector<Vec3> data;
    while(std::getline(stream,line)) {
        check(line.size()<=8192,"LUT line budget");line=line.substr(0,line.find('#'));
        std::istringstream row(line);row.imbue(std::locale::classic());std::string key;if(!(row>>key)) continue;
        if(key=="TITLE") { check(data.empty()&&!seen_title,"Duplicate/late LUT title");seen_title=true;std::string title;row>>std::ws;check(row.peek()=='"'&&static_cast<bool>(row>>std::quoted(title)),"Malformed LUT title");std::string extra;check(!(row>>extra),"Trailing title token");continue; }
        if(key=="LUT_3D_SIZE") { check(edge==0&&data.empty(),"Duplicate/late LUT size");long long n{};check(static_cast<bool>(row>>n)&&n>=2&&n<=static_cast<long long>(max_edge),"Invalid LUT edge");edge=static_cast<std::size_t>(n);data.reserve(edge*edge*edge); }
        else if(key=="DOMAIN_MIN"||key=="DOMAIN_MAX") {
            check(data.empty(),"Late LUT domain");Vec3 v;check(static_cast<bool>(row>>v.r>>v.g>>v.b)&&finite(v),"Invalid LUT domain");
            if(key=="DOMAIN_MIN") { check(!seen_lo,"Duplicate domain min");lo=v;seen_lo=true; } else { check(!seen_hi,"Duplicate domain max");hi=v;seen_hi=true; }
        } else {
            check(edge!=0&&data.size()<edge*edge*edge,"LUT data before size/too many samples");
            std::istringstream value(key);value.imbue(std::locale::classic());Vec3 v;char extra{};
            check(static_cast<bool>(value>>v.r)&&!(value>>extra)&&static_cast<bool>(row>>v.g>>v.b)&&finite(v),"Invalid LUT sample/unsupported directive");data.push_back(v);
        }
        std::string trailing;check(!(row>>trailing),"Trailing LUT token");
    }
    return Lut3D(edge,std::move(data),lo,hi);
}
Vec3 Lut3D::domain_min() const noexcept { return low_; }
Vec3 Lut3D::domain_max() const noexcept { return high_; }
std::size_t Lut3D::edge() const noexcept { return edge_; }
std::span<const Vec3> Lut3D::values() const { return values_; }
namespace {
struct Cell { std::size_t r,g,b;Vec3 f; };
Cell cell(Vec3 v,Vec3 lo,Vec3 hi,std::size_t edge) {
    check(finite(v),"Nonfinite LUT coordinate");const float n=static_cast<float>(edge-1);
    v={std::clamp((v.r-lo.r)/(hi.r-lo.r),0.F,1.F)*n,std::clamp((v.g-lo.g)/(hi.g-lo.g),0.F,1.F)*n,std::clamp((v.b-lo.b)/(hi.b-lo.b),0.F,1.F)*n};
    const auto r=std::min(static_cast<std::size_t>(v.r),edge-2),g=std::min(static_cast<std::size_t>(v.g),edge-2),b=std::min(static_cast<std::size_t>(v.b),edge-2);
    return {r,g,b,{v.r-static_cast<float>(r),v.g-static_cast<float>(g),v.b-static_cast<float>(b)}};
}
}
Vec3 Lut3D::tetrahedral(Vec3 v) const {
    const auto c=cell(v,low_,high_,edge_);const auto f=c.f;
    const auto at=[&](std::size_t r,std::size_t g,std::size_t b) { return values_[((c.b+b)*edge_+c.g+g)*edge_+c.r+r]; };
    const auto a=at(0,0,0),z=at(1,1,1);
    if(f.r>=f.g) {
        if(f.g>=f.b) { const auto b=at(1,0,0),d=at(1,1,0);return a+(b-a)*f.r+(d-b)*f.g+(z-d)*f.b; }
        if(f.r>=f.b) { const auto b=at(1,0,0),d=at(1,0,1);return a+(b-a)*f.r+(d-b)*f.b+(z-d)*f.g; }
        const auto b=at(0,0,1),d=at(1,0,1);return a+(b-a)*f.b+(d-b)*f.r+(z-d)*f.g;
    }
    if(f.r>=f.b) { const auto b=at(0,1,0),d=at(1,1,0);return a+(b-a)*f.g+(d-b)*f.r+(z-d)*f.b; }
    if(f.g>=f.b) { const auto b=at(0,1,0),d=at(0,1,1);return a+(b-a)*f.g+(d-b)*f.b+(z-d)*f.r; }
    const auto b=at(0,0,1),d=at(0,1,1);return a+(b-a)*f.b+(d-b)*f.g+(z-d)*f.r;
}
Vec3 Lut3D::trilinear(Vec3 v) const {
    const auto c=cell(v,low_,high_,edge_);Vec3 sum{};
    for(std::size_t b=0;b<2;++b) for(std::size_t g=0;g<2;++g) for(std::size_t r=0;r<2;++r) {
        const float w=(r?c.f.r:1-c.f.r)*(g?c.f.g:1-c.f.g)*(b?c.f.b:1-c.f.b);
        sum=sum+values_[((c.b+b)*edge_+c.g+g)*edge_+c.r+r]*w;
    }
    return sum;
}
} // namespace color

#include "color/color_pipeline_api.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace color {
namespace {
void require(bool v,const char* message) { if(!v) throw std::invalid_argument(message); }
double signed_pow(double x,double p) { return std::copysign(std::pow(std::abs(x),p),x); }
}
Vec3 operator+(Vec3 a,Vec3 b) { return {a.r+b.r,a.g+b.g,a.b+b.b}; }
Vec3 operator-(Vec3 a,Vec3 b) { return {a.r-b.r,a.g-b.g,a.b-b.b}; }
Vec3 operator*(Vec3 a,float b) { return {a.r*b,a.g*b,a.b*b}; }
bool finite(Vec3 a) { return std::isfinite(a.r)&&std::isfinite(a.g)&&std::isfinite(a.b); }
bool finite(Pixel a) { return finite(Vec3{a.r,a.g,a.b})&&std::isfinite(a.a); }
float luminance(Vec3 a) { return static_cast<float>(std::clamp(.2126*static_cast<double>(a.r)+.7152*static_cast<double>(a.g)+.0722*static_cast<double>(a.b),-static_cast<double>(std::numeric_limits<float>::max()),static_cast<double>(std::numeric_limits<float>::max()))); }
Mat3 Mat3::identity() { return {{1,0,0,0,1,0,0,0,1}}; }
Vec3 multiply(const Mat3& a,Vec3 x) {
    return {static_cast<float>(a.m[0]*x.r+a.m[1]*x.g+a.m[2]*x.b),static_cast<float>(a.m[3]*x.r+a.m[4]*x.g+a.m[5]*x.b),static_cast<float>(a.m[6]*x.r+a.m[7]*x.g+a.m[8]*x.b)};
}
Mat3 multiply(const Mat3& a,const Mat3& b) {
    Mat3 c;
    for(std::size_t r=0;r<3;++r) for(std::size_t k=0;k<3;++k) for(std::size_t j=0;j<3;++j) c.m[r*3+j]+=a.m[r*3+k]*b.m[k*3+j];
    return c;
}
Mat3 inverse(const Mat3& a) {
    const auto& m=a.m;
    const double d=m[0]*(m[4]*m[8]-m[5]*m[7])-m[1]*(m[3]*m[8]-m[5]*m[6])+m[2]*(m[3]*m[7]-m[4]*m[6]);
    require(std::isfinite(d)&&std::abs(d)>1e-14,"Singular/nonfinite color matrix");
    Mat3 r{{m[4]*m[8]-m[5]*m[7],m[2]*m[7]-m[1]*m[8],m[1]*m[5]-m[2]*m[4],m[5]*m[6]-m[3]*m[8],m[0]*m[8]-m[2]*m[6],m[2]*m[3]-m[0]*m[5],m[3]*m[7]-m[4]*m[6],m[1]*m[6]-m[0]*m[7],m[0]*m[4]-m[1]*m[3]}};
    for(auto& x:r.m) x/=d;
    return r;
}
Chromaticities chromaticities(Gamut g) {
    switch(g) {
    case Gamut::Rec709:return {.64,.33,.30,.60,.15,.06,.3127,.3290};
    case Gamut::Rec2020:return {.708,.292,.170,.797,.131,.046,.3127,.3290};
    case Gamut::P3D65:return {.680,.320,.265,.690,.150,.060,.3127,.3290};
    case Gamut::ACESAP0:return {.7347,.2653,0,1,.0001,-.077,.32168,.33767};
    case Gamut::ACESAP1:return {.713,.293,.165,.830,.128,.044,.32168,.33767};
    case Gamut::DaVinciWide:return {.8,.313,.1682,.9877,.079,-.1155,.3127,.3290};
    case Gamut::ARRIWide3:return {.684,.313,.221,.848,.0861,-.102,.3127,.3290};
    case Gamut::ARRIWide4:return {.7347,.2653,.1424,.8576,.0991,-.0308,.3127,.3290};
    case Gamut::SonySGamut3:return {.73,.28,.14,.855,.1,-.05,.3127,.3290};
    case Gamut::SonySGamut3Cine:return {.766,.275,.225,.8,.089,-.087,.3127,.3290};
    case Gamut::PanasonicVGamut:return {.73,.28,.165,.840,.1,-.03,.3127,.3290};
    }
    throw std::invalid_argument("Unknown gamut");
}
Mat3 rgb_to_xyz(Gamut g) {
    const auto p=chromaticities(g);
    Mat3 base{{p.rx/p.ry,p.gx/p.gy,p.bx/p.by,1,1,1,(1-p.rx-p.ry)/p.ry,(1-p.gx-p.gy)/p.gy,(1-p.bx-p.by)/p.by}};
    const auto s=multiply(inverse(base),Vec3{static_cast<float>(p.wx/p.wy),1,static_cast<float>((1-p.wx-p.wy)/p.wy)});
    const std::array<double,3> scale{s.r,s.g,s.b};
    for(std::size_t r=0;r<3;++r) for(std::size_t c=0;c<3;++c) base.m[r*3+c]*=scale[c];
    return base;
}
Mat3 bradford(double sx,double sy,double dx,double dy) {
    require(std::isfinite(sx)&&std::isfinite(sy)&&std::isfinite(dx)&&std::isfinite(dy)&&sx>0&&sy>0&&dx>0&&dy>0&&sx+sy<1&&dx+dy<1,"Invalid white point");
    if(sx==dx&&sy==dy) return Mat3::identity();
    const Mat3 b{{.8951,.2664,-.1614,-.7502,1.7135,.0367,.0389,-.0685,1.0296}};
    const auto s=multiply(b,Vec3{static_cast<float>(sx/sy),1,static_cast<float>((1-sx-sy)/sy)});
    const auto d=multiply(b,Vec3{static_cast<float>(dx/dy),1,static_cast<float>((1-dx-dy)/dy)});
    require(s.r!=0&&s.g!=0&&s.b!=0,"Invalid Bradford cone response");
    return multiply(inverse(b),multiply(Mat3{{d.r/s.r,0,0,0,d.g/s.g,0,0,0,d.b/s.b}},b));
}
float encode(float input,Transfer t) {
    require(std::isfinite(input),"Nonfinite transfer input");
    const double x=input; double y{};
    switch(t) {
    case Transfer::Linear:y=x;break;
    case Transfer::SRGB:y=x<=.0031308?12.92*x:1.055*std::pow(x,1/2.4)-.055;break;
    case Transfer::Gamma24:y=signed_pow(x,1/2.4);break;
    case Transfer::Rec709:y=x<static_cast<double>(.018F)?4.5*x:1.099*std::pow(x,.45)-.099;break;
    case Transfer::DaVinciIntermediate:y=x<=.00262409?x*10.44426855:(std::log2(x+.0075)+7)*.07329248;break;
    case Transfer::ACEScct:y=x<=.0078125?10.5402377416545*x+.0729055341958355:(std::log2(x)+9.72)/17.52;break;
    case Transfer::PQ: { require(x>=0&&x<=1,"PQ linear domain is0..1 (1=10000nit)"); const double p=std::pow(x,2610./16384);y=std::pow((3424./4096+2413./128*p)/(1+2392./128*p),2523./32);break; }
    case Transfer::HLG:require(x>=0&&x<=1,"HLG scene-linear domain is0..1");y=x<=1./12?std::sqrt(3*x):.17883277*std::log(12*x-.28466892)+.55991073;break;
    case Transfer::LogC3EI800:y=x>.010591?.247190*std::log10(5.555556*x+.052272)+.385537:5.367655*x+.092809;break;
    case Transfer::LogC4: { const double a=(std::pow(2.,18)-16)/117.45,b=(1023.-95)/1023,c=95./1023,s=7*std::log(2.)*std::pow(2.,7-14*c/b)/(a*b),cut=(std::pow(2.,6-14*c/b)-64)/a; y=x>=cut?(std::log2(a*x+64)-6)/14*b+c:(x-cut)/s;break; }
    case Transfer::SLog3:y=(x>=.01125?420+std::log10((x+.01)/.19)*261.5:x*(171.2102946929-95)/.01125+95)/1023;break;
    case Transfer::VLog:y=x<.01?5.6*x+.125:.241514*std::log10(x+.00873)+.598206;break;
    default:throw std::invalid_argument("Unknown transfer");
    }
    require(std::isfinite(y)&&std::abs(y)<=static_cast<double>(std::numeric_limits<float>::max()),"Transfer result overflow");return static_cast<float>(y);
}
float decode(float input,Transfer t) {
    require(std::isfinite(input),"Nonfinite transfer input");
    const double x=input; double y{};
    switch(t) {
    case Transfer::Linear:y=x;break;
    case Transfer::SRGB:y=x<=.04045?x/12.92:std::pow((x+.055)/1.055,2.4);break;
    case Transfer::Gamma24:y=signed_pow(x,2.4);break;
    case Transfer::Rec709:y=x<static_cast<double>(.081F)?x/4.5:std::pow((x+.099)/1.099,1/.45);break;
    case Transfer::DaVinciIntermediate:y=x<=.02740668?x/10.44426855:std::exp2(x/.07329248-7)-.0075;break;
    case Transfer::ACEScct:y=x<=.155251141552511?(x-.0729055341958355)/10.5402377416545:std::exp2(x*17.52-9.72);break;
    case Transfer::PQ: { require(x>=0&&x<=1,"PQ signal domain0..1");const double p=std::pow(x,32./2523);y=std::pow(std::max(p-3424./4096,0.)/(2413./128-2392./128*p),16384./2610);break; }
    case Transfer::HLG:require(x>=0&&x<=1,"HLG signal domain0..1");y=x<=.5?x*x/3:(std::exp((x-.55991073)/.17883277)+.28466892)/12;break;
    case Transfer::LogC3EI800:y=x>5.367655*.010591+.092809?(std::pow(10.,(x-.385537)/.247190)-.052272)/5.555556:(x-.092809)/5.367655;break;
    case Transfer::LogC4: { const double a=(std::pow(2.,18)-16)/117.45,b=928./1023,c=95./1023,s=7*std::log(2.)*std::pow(2.,7-14*c/b)/(a*b),cut=(std::pow(2.,6-14*c/b)-64)/a;y=x>=0?(std::exp2(14*(x-c)/b+6)-64)/a:x*s+cut;break; }
    case Transfer::SLog3:y=x>=171.2102946929/1023?std::pow(10.,(x*1023-420)/261.5)*.19-.01:(x*1023-95)*.01125/(171.2102946929-95);break;
    case Transfer::VLog:y=x<.181?(x-.125)/5.6:std::pow(10.,(x-.598206)/.241514)-.00873;break;
    default:throw std::invalid_argument("Unknown transfer");
    }
    require(std::isfinite(y)&&std::abs(y)<=static_cast<double>(std::numeric_limits<float>::max()),"Transfer result overflow");return static_cast<float>(y);
}
PreparedCST::PreparedCST(const CST& c):params_(c) {
    require(std::isfinite(c.exposure_stops)&&std::abs(c.exposure_stops)<=32&&c.tone_knee>=0&&std::isfinite(c.tone_knee)&&c.gamut_compression>=0&&c.gamut_compression<=1,"Invalid CST");
    const auto a=chromaticities(c.input_gamut),b=chromaticities(c.output_gamut);
    (void)decode(.18F,c.input_transfer);(void)encode(.18F,c.output_transfer);
    matrix_=multiply(inverse(rgb_to_xyz(c.output_gamut)),multiply(bradford(a.wx,a.wy,b.wx,b.wy),rgb_to_xyz(c.input_gamut)));
    exposure_=std::exp2(c.exposure_stops);
}
const Mat3& PreparedCST::matrix() const noexcept { return matrix_; }
Vec3 PreparedCST::apply(Vec3 v) const {
    require(finite(v),"Nonfinite CST input");const auto& c=params_;
    v={decode(v.r,c.input_transfer),decode(v.g,c.input_transfer),decode(v.b,c.input_transfer)};
    v=multiply(matrix_,v)*exposure_;
    if(c.tone_knee>0) { const float y=luminance(v); if(y>c.tone_knee) v=v*((c.tone_knee+(y-c.tone_knee)/(1+(y-c.tone_knee)/c.tone_knee))/y); }
    if(c.gamut_compression>0) {
        const float y=luminance(v),lo=std::min({v.r,v.g,v.b}),hi=std::max({v.r,v.g,v.b});
        const float excess=std::max({0.F,-lo,hi-1});
        v=Vec3{y,y,y}+(v-Vec3{y,y,y})*(1/(1+c.gamut_compression*excess));
    }
    return {encode(v.r,c.output_transfer),encode(v.g,c.output_transfer),encode(v.b,c.output_transfer)};
}
Vec3 transform(Vec3 v,const CST& c) { return PreparedCST(c).apply(v); }
} // namespace color

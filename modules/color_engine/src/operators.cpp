#include "color/color_pipeline_api.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <iomanip>
#include <locale>
#include <sstream>
#include <numbers>
#include <stdexcept>

namespace color {
namespace {
void check(bool b,const char* s) { if(!b) throw std::invalid_argument(s); }
float sat(float x) { return std::clamp(x,0.F,1.F); }
float smooth(float a,float b,float x) { const float t=sat((x-a)/(b-a));return t*t*(3-2*t); }
float spow(float x,float y) { return std::copysign(std::pow(std::abs(x),y),x); }
Vec3 hsl(Vec3 v) {
    v={sat(v.r),sat(v.g),sat(v.b)};
    const float hi=std::max({v.r,v.g,v.b}),lo=std::min({v.r,v.g,v.b}),d=hi-lo,l=(hi+lo)*.5F;
    if(d==0) return {0,0,l};
    float h=hi==v.r?(v.g-v.b)/d:(hi==v.g?2+(v.b-v.r)/d:4+(v.r-v.g)/d);
    h=h/6-std::floor(h/6);
    return {h,d/(l<=.5F?hi+lo:2-hi-lo),l};
}
float band(float d,float width,float softness) { return softness==0?(d<=width?1.F:0.F):1-smooth(width,width+softness,d); }
std::uint32_t hash(std::uint32_t x) { x^=x>>16;x*=0x7feb352dU;x^=x>>15;x*=0x846ca68bU;x^=x>>16;return x; }
float noise(std::uint32_t x) { return static_cast<float>(hash(x)>>8)*(1.F/16777216.F)-.5F; }
}
Vec3 apply(Vec3 v,const RawDecodeParams& p) {
    check(finite(v)&&v.r>=0&&v.g>=0&&v.b>=0&&v.r<=65535&&v.g<=65535&&v.b<=65535&&finite(p.black_level)&&finite(p.white_level)&&finite(p.white_balance)&&p.black_level.r>=0&&p.black_level.g>=0&&p.black_level.b>=0&&p.white_level.r>p.black_level.r&&p.white_level.g>p.black_level.g&&p.white_level.b>p.black_level.b&&p.white_level.r<=65535&&p.white_level.g<=65535&&p.white_level.b<=65535&&p.white_balance.r>0&&p.white_balance.g>0&&p.white_balance.b>0&&p.white_balance.r<=16&&p.white_balance.g<=16&&p.white_balance.b<=16&&std::isfinite(p.exposure_stops)&&std::abs(p.exposure_stops)<=32,"Invalid sensor RGB parameters");
    for(auto x:p.camera_to_working.m) check(std::isfinite(x),"Nonfinite camera matrix");
    v={(v.r-p.black_level.r)/(p.white_level.r-p.black_level.r)*p.white_balance.r,(v.g-p.black_level.g)/(p.white_level.g-p.black_level.g)*p.white_balance.g,(v.b-p.black_level.b)/(p.white_level.b-p.black_level.b)*p.white_balance.b};
    return multiply(p.camera_to_working,v)*std::exp2(p.exposure_stops);
}
Node touch_wheel(const Node& original,TouchWheel wheel,float x,float y,float lum) {
    check(!original.locked&&std::isfinite(x)&&std::isfinite(y)&&std::isfinite(lum)&&std::abs(lum)<=1,"Locked/nonfinite touch edit");
    const float radius=std::hypot(x,y);if(radius>1) { x/=radius;y/=radius; }
    check(std::isfinite(radius),"Touch coordinate overflow");
    Node result=original;auto* grade=std::get_if<Primaries>(&result.operation);check(grade!=nullptr,"Touch wheel requires primaries node");
    const Vec3 opponent{x,(-.2126F*x-.0722F*y)/.7152F,y};const Vec3 delta=(opponent+Vec3{lum,lum,lum})*.25F;
    switch(wheel) {
    case TouchWheel::Lift:grade->lift=delta;break;
    case TouchWheel::Offset:grade->offset=delta;break;
    case TouchWheel::Gamma:grade->gamma={std::exp2(delta.r*2),std::exp2(delta.g*2),std::exp2(delta.b*2)};break;
    case TouchWheel::Gain:grade->gain={std::exp2(delta.r*2),std::exp2(delta.g*2),std::exp2(delta.b*2)};break;
    default:throw std::invalid_argument("Unknown touch wheel");
    }
    return result;
}
Vec3 apply(Vec3 v,const Primaries& p) {
    check(finite(v)&&finite(p.lift)&&finite(p.gamma)&&finite(p.gain)&&finite(p.offset)&&p.gamma.r>0&&p.gamma.g>0&&p.gamma.b>0&&p.contrast>=0&&std::isfinite(p.contrast)&&p.pivot>0&&std::isfinite(p.pivot)&&std::isfinite(p.exposure)&&std::abs(p.exposure)<=32&&std::isfinite(p.saturation)&&p.saturation>=0&&std::isfinite(p.hue_radians)&&std::abs(p.hue_radians)<=6.2831855F&&std::isfinite(p.vibrance)&&std::abs(p.vibrance)<=1&&std::isfinite(p.temperature)&&std::abs(p.temperature)<=8&&std::isfinite(p.tint)&&std::abs(p.tint)<=8&&p.luminance_mix>=0&&p.luminance_mix<=1,"Invalid primaries");
    const float old_y=luminance(v);
    v=v*std::exp2(p.exposure);
    v={v.r*std::exp2(p.temperature),v.g*std::exp2(p.tint),v.b*std::exp2(-p.temperature)};
    auto f=[&](float x,float lift,float gamma,float gain,float offset) { const float t=x*gain+lift*(1-x)+offset; const float g=gamma==1?t:spow(t,1/gamma);return p.contrast==1?g:p.pivot*spow(g/p.pivot,p.contrast); };
    v={f(v.r,p.lift.r,p.gamma.r,p.gain.r,p.offset.r),f(v.g,p.lift.g,p.gamma.g,p.gain.g,p.offset.g),f(v.b,p.lift.b,p.gamma.b,p.gain.b,p.offset.b)};
    const float y=luminance(v),u=v.b-y,w=v.r-y;
    const float cs=std::cos(p.hue_radians),sn=std::sin(p.hue_radians);
    const float r=y+w*cs-u*sn,b=y+w*sn+u*cs;
    v={r,(y-.2126F*r-.0722F*b)/.7152F,b};
    const float chroma=std::max({v.r,v.g,v.b})-std::min({v.r,v.g,v.b});
    const float saturation=p.saturation*(1+p.vibrance*(1-chroma/(std::abs(y)+chroma+1e-6F)));
    v=Vec3{y,y,y}+(v-Vec3{y,y,y})*saturation;
    const float correction=(old_y-luminance(v))*p.luminance_mix;
    return v+Vec3{correction,correction,correction};
}
Vec3 apply(Vec3 v,const LogWheels& p) {
    check(finite(v)&&finite(p.shadows)&&finite(p.midtones)&&finite(p.highlights)&&p.low>=0&&p.high<=1&&p.low<p.high&&p.softness>0&&std::isfinite(p.softness),"Invalid log wheels");
    const float y=encode(std::max(luminance(v),0.F),Transfer::DaVinciIntermediate);
    const float a=1-smooth(p.low-p.softness,p.low+p.softness,y),b=smooth(p.high-p.softness,p.high+p.softness,y);
    const float mid=std::max(0.F,1-a-b);
    const Vec3 delta=p.shadows*a+p.midtones*mid+p.highlights*b;
    if(delta.r==0&&delta.g==0&&delta.b==0) return v;
    return {decode(encode(v.r,Transfer::DaVinciIntermediate)+delta.r,Transfer::DaVinciIntermediate),decode(encode(v.g,Transfer::DaVinciIntermediate)+delta.g,Transfer::DaVinciIntermediate),decode(encode(v.b,Transfer::DaVinciIntermediate)+delta.b,Transfer::DaVinciIntermediate)};
}
Vec3 apply(Vec3 v,const HDRWheels& p) {
    check(finite(v)&&p.falloff>0&&std::isfinite(p.falloff)&&p.soft_clip>=0&&std::isfinite(p.soft_clip),"Invalid HDR wheels");
    const float z=std::log2(std::max(luminance(v),1e-12F)/.18F);float sum=0,exposure=0;
    for(std::size_t i=0;i<6;++i) {
        check(std::isfinite(p.stops[i])&&std::abs(p.stops[i])<=32&&std::isfinite(p.centers[i])&&(i==0||p.centers[i]>p.centers[i-1]),"Invalid HDR zones");
        const float q=(z-p.centers[i])/p.falloff,w=std::exp(-.5F*q*q);sum+=w;exposure+=w*p.stops[i];
    }
    if(sum>0) v=v*std::exp2(exposure/sum);
    auto knee=[&](float x) { return p.soft_clip>0&&x>p.soft_clip?p.soft_clip+(x-p.soft_clip)/(1+(x-p.soft_clip)/p.soft_clip):x; };
    return {knee(v.r),knee(v.g),knee(v.b)};
}
Vec3 apply(Vec3 v,const ColorSlice& p) {
    check(finite(v)&&p.width>0&&p.width<=1&&finite(p.shadow_tint)&&finite(p.highlight_tint),"Invalid color slices");
    const auto h=hsl(v);float hue=0,sat_delta=0,density=0;
    constexpr std::array<float,7> centers{0,1.F/6,2.F/6,3.F/6,4.F/6,5.F/6,.08F};
    for(std::size_t i=0;i<7;++i) {
        check(finite(p.hue_sat_density[i]),"Nonfinite slice");
        const float distance=std::abs(std::remainder(h.r-centers[i],1.F));
        const float w=std::pow(std::max(0.F,1-distance/(p.width*.5F)),2.F);
        hue+=p.hue_sat_density[i].r*w;sat_delta+=p.hue_sat_density[i].g*w;density+=p.hue_sat_density[i].b*w;
    }
    check(std::abs(density)<=32,"Slice density exceeds exposure budget");
    Primaries grade;grade.hue_radians=hue;grade.saturation=std::max(0.F,1+sat_delta);
    v=apply(v,grade)*std::exp2(-density);
    return v+p.shadow_tint*(1-h.b)+p.highlight_tint*h.b;
}
Vec3 apply(Vec3 v,const RGBMixer& p) { check(finite(v),"Nonfinite mixer input");for(auto x:p.matrix.m) check(std::isfinite(x),"Nonfinite mixer");v=multiply(p.matrix,v);if(p.monochrome) { const float y=luminance(v);v={y,y,y}; }return v; }
float qualify(Vec3 v,const HSLQualifier& p) {
    check(finite(v)&&finite(p.center)&&finite(p.width)&&finite(p.softness)&&p.width.r>=0&&p.width.g>=0&&p.width.b>=0&&p.softness.r>=0&&p.softness.g>=0&&p.softness.b>=0,"Invalid HSL qualifier");
    const auto h=hsl(v);
    float m=band(std::abs(std::remainder(h.r-p.center.r,1.F)),p.width.r*.5F,p.softness.r)*band(std::abs(h.g-p.center.g),p.width.g*.5F,p.softness.g)*band(std::abs(h.b-p.center.b),p.width.b*.5F,p.softness.b);
    if(p.invert) m=1-m;
    return m;
}
float window_mask(float x,float y,const PowerWindow& p) {
    check(std::isfinite(x)&&std::isfinite(y)&&std::isfinite(p.cx)&&std::isfinite(p.cy)&&p.sx>0&&p.sy>0&&std::isfinite(p.sx)&&std::isfinite(p.sy)&&std::isfinite(p.rotation)&&std::abs(p.rotation)<=6.2831855F&&p.feather>=0&&std::isfinite(p.feather)&&std::isfinite(p.corner_radius)&&p.corner_radius>=0&&p.corner_radius<=std::min(p.sx,p.sy),"Invalid power window");
    const float u=x-p.cx,v=y-p.cy,qx=std::cos(p.rotation)*u+std::sin(p.rotation)*v,qy=-std::sin(p.rotation)*u+std::cos(p.rotation)*v;
    float d{},m{};
    switch(p.shape) {
    case PowerWindow::Shape::Circle:d=(std::hypot(qx/p.sx,qy/p.sy)-1)*std::min(p.sx,p.sy);break;
    case PowerWindow::Shape::Rectangle:{ const float a=std::abs(qx)-p.sx,b=std::abs(qy)-p.sy;d=std::hypot(std::max(a,0.F),std::max(b,0.F))+std::min(std::max(a,b),0.F);break; }
    case PowerWindow::Shape::LinearGradient:m=1-smooth(-p.sx-p.feather,p.sx+p.feather,qx);return p.invert?1-m:m;
    case PowerWindow::Shape::RoundedRectangle:{ const float a=std::abs(qx)-p.sx+p.corner_radius,b=std::abs(qy)-p.sy+p.corner_radius;d=std::hypot(std::max(a,0.F),std::max(b,0.F))+std::min(std::max(a,b),0.F)-p.corner_radius;break; }
    default:throw std::invalid_argument("Unknown SDF shape");
    }
    m=p.feather==0?(d<=0?1.F:0.F):1-smooth(-p.feather,p.feather,d);
    return p.invert?1-m:m;
}
Curve::Curve(std::span<const CurvePoint> points) {
    check(points.size()>=2&&points.size()<=64,"Curve point budget2..64");
    for(std::size_t i=0;i<points.size();++i) check(std::isfinite(points[i].x)&&std::isfinite(points[i].y)&&points[i].x>=0&&points[i].x<=1&&points[i].y>=0&&points[i].y<=1&&(i==0||points[i].x>points[i-1].x),"Invalid curve coordinates");
    check(points.front().x==0&&points.back().x==1,"Curve endpoints must cover0..1");
    points_.assign(points.begin(),points.end());tangents_.resize(points.size());std::vector<float> d(points.size()-1);
    for(std::size_t i=0;i<d.size();++i) { check(points[i+1].x-points[i].x>=1e-6F,"Curve x spacing below float32 precision budget");d[i]=(points[i+1].y-points[i].y)/(points[i+1].x-points[i].x); }
    tangents_.front()=d.front();tangents_.back()=d.back();
    for(std::size_t i=1;i+1<points.size();++i) tangents_[i]=d[i-1]*d[i]<=0?0:2*d[i-1]*d[i]/(d[i-1]+d[i]);
    for(std::size_t i=0;i<d.size();++i) {
        if(d[i]==0) { tangents_[i]=0;tangents_[i+1]=0;continue; }
        const float a=tangents_[i]/d[i],b=tangents_[i+1]/d[i],norm=std::hypot(a,b);
        if(norm>3) { tangents_[i]=3*a*d[i]/norm;tangents_[i+1]=3*b*d[i]/norm; }
    }
}
std::span<const CurvePoint> Curve::points() const noexcept { return points_; }
float Curve::evaluate(float x) const {
    check(std::isfinite(x),"Nonfinite curve query");x=sat(x);
    const auto it=std::upper_bound(points_.begin(),points_.end(),x,[](float a,const CurvePoint& b) { return a<b.x; });
    const std::size_t i=it==points_.end()?points_.size()-2:static_cast<std::size_t>(it-points_.begin()-1);
    const float dx=points_[i+1].x-points_[i].x,t=(x-points_[i].x)/dx,t2=t*t,t3=t2*t;
    return (2*t3-3*t2+1)*points_[i].y+(t3-2*t2+t)*dx*tangents_[i]+(-2*t3+3*t2)*points_[i+1].y+(t3-t2)*dx*tangents_[i+1];
}
std::vector<float> Curve::bake(std::size_t n) const { check(n>=2&&n<=65536,"Curve LUT budget");std::vector<float> out(n);for(std::size_t i=0;i<n;++i) out[i]=evaluate(static_cast<float>(i)/static_cast<float>(n-1));return out; }
Vec3 apply(Vec3 v,const Curves& c) {
    check(finite(v),"Nonfinite curves input");
    if(c.master) v={c.master->evaluate(v.r),c.master->evaluate(v.g),c.master->evaluate(v.b)};
    return {c.red?c.red->evaluate(v.r):v.r,c.green?c.green->evaluate(v.g):v.g,c.blue?c.blue->evaluate(v.b):v.b};
}
Vec3 grain(Vec3 v,std::uint32_t x,std::uint32_t y,std::uint32_t frame,const Grain& g) {
    check(finite(v)&&g.amount>=0&&std::isfinite(g.amount)&&g.colored>=0&&g.colored<=1,"Invalid grain");
    const std::uint32_t seed=hash(x)^hash(y+0x9e3779b9U)^hash(frame)^g.seed;
    const float n=noise(seed);return v+Vec3{n*(1-g.colored)+noise(seed+1)*g.colored,n*(1-g.colored)+noise(seed+2)*g.colored,n*(1-g.colored)+noise(seed+3)*g.colored}*g.amount;
}
void halation(const Image32F& in,Image32F& out,float threshold,float amount,std::size_t radius) {
    check(in.width()==out.width()&&in.height()==out.height()&&&in!=&out&&std::isfinite(threshold)&&threshold>=0&&std::isfinite(amount)&&amount>=0&&radius>=1&&radius<=32,"Invalid halation");
    const auto w=in.width(),h=in.height();std::vector<float> tmp(w*h),blur(w*h),weights(2*radius+1);
    const float sigma=std::max(1.F,static_cast<float>(radius)/2);float sum=0;
    for(std::size_t i=0;i<weights.size();++i) { const float x=static_cast<float>(i)-static_cast<float>(radius);weights[i]=std::exp(-x*x/(2*sigma*sigma));sum+=weights[i]; }
    for(auto& a:weights) a/=sum;
    const auto input=in.pixels();auto output=out.pixels();
    for(std::size_t y=0;y<h;++y) for(std::size_t x=0;x<w;++x) for(std::size_t k=0;k<weights.size();++k) {
        const auto xx=static_cast<std::size_t>(std::clamp(static_cast<std::ptrdiff_t>(x)+static_cast<std::ptrdiff_t>(k)-static_cast<std::ptrdiff_t>(radius),std::ptrdiff_t{0},static_cast<std::ptrdiff_t>(w-1)));
        const auto p=input[y*w+xx];tmp[y*w+x]+=std::max(luminance({p.r,p.g,p.b})-threshold,0.F)*weights[k];
    }
    for(std::size_t y=0;y<h;++y) for(std::size_t x=0;x<w;++x) {
        for(std::size_t k=0;k<weights.size();++k) { const auto yy=static_cast<std::size_t>(std::clamp(static_cast<std::ptrdiff_t>(y)+static_cast<std::ptrdiff_t>(k)-static_cast<std::ptrdiff_t>(radius),std::ptrdiff_t{0},static_cast<std::ptrdiff_t>(h-1)));blur[y*w+x]+=tmp[yy*w+x]*weights[k]; }
        const auto p=input[y*w+x];const float glow=blur[y*w+x]*amount;output[y*w+x]={p.r+glow,p.g+.25F*glow,p.b,p.a};
    }
}
} // namespace color

namespace color {
Lut1D::Lut1D(std::vector<Vec3> values,Vec3 low,Vec3 high):values_(std::move(values)),low_(low),high_(high) {
    check(values_.size()>=2&&values_.size()<=65536&&finite(low_)&&finite(high_),"Invalid 1D LUT size/domain");
    check(high_.r>low_.r&&high_.g>low_.g&&high_.b>low_.b,"1D LUT domain must increase");
    for(auto v:values_) check(finite(v),"Nonfinite 1D LUT sample");
}
Lut1D Lut1D::parse_cube(std::string_view text,std::size_t max_size) {
    check(text.size()<=32U*1024U*1024U&&max_size>=2&&max_size<=65536,"1D LUT text/size budget");
    std::istringstream stream{std::string(text)};stream.imbue(std::locale::classic());std::string line;std::size_t size=0;
    Vec3 lo{},hi{1,1,1};bool seen_lo=false,seen_hi=false,seen_title=false;std::vector<Vec3> data;
    while(std::getline(stream,line)) {
        check(line.size()<=8192,"1D LUT line budget");line=line.substr(0,line.find('#'));
        std::istringstream row(line);row.imbue(std::locale::classic());std::string key;if(!(row>>key)) continue;
        if(key=="TITLE") { check(data.empty()&&!seen_title,"Duplicate/late 1D LUT title");seen_title=true;std::string title;row>>std::ws;check(row.peek()=='"'&&static_cast<bool>(row>>std::quoted(title)),"Malformed 1D LUT title");std::string extra;check(!(row>>extra),"Trailing 1D title token");continue; }
        if(key=="LUT_1D_SIZE") { check(size==0&&data.empty(),"Duplicate/late 1D LUT size");long long n{};check(static_cast<bool>(row>>n)&&n>=2&&n<=static_cast<long long>(max_size),"Invalid 1D LUT size");size=static_cast<std::size_t>(n);data.reserve(size); }
        else if(key=="DOMAIN_MIN"||key=="DOMAIN_MAX") { check(data.empty(),"Late 1D LUT domain");Vec3 v;check(static_cast<bool>(row>>v.r>>v.g>>v.b)&&finite(v),"Invalid 1D LUT domain");if(key=="DOMAIN_MIN") { check(!seen_lo,"Duplicate 1D domain min");lo=v;seen_lo=true; } else { check(!seen_hi,"Duplicate 1D domain max");hi=v;seen_hi=true; } }
        else if(key=="LUT_3D_SIZE") check(false,"3D LUT directive inside 1D LUT file");
        else {
            check(size!=0&&data.size()<size,"1D LUT data before size/too many samples");
            std::istringstream value(key);value.imbue(std::locale::classic());Vec3 v;char extra{};
            check(static_cast<bool>(value>>v.r)&&!(value>>extra)&&static_cast<bool>(row>>v.g>>v.b)&&finite(v),"Invalid 1D LUT sample");data.push_back(v);
        }
        std::string trailing;check(!(row>>trailing),"Trailing 1D LUT token");
    }
    check(size!=0&&data.size()==size,"1D LUT sample count mismatch");
    return Lut1D(std::move(data),lo,hi);
}
Vec3 Lut1D::apply(Vec3 v) const {
    check(finite(v),"Nonfinite 1D LUT input");const float n=static_cast<float>(values_.size()-1);
    auto channel=[&](float x,float lo,float hi,auto select) {
        const float t=std::clamp((x-lo)/(hi-lo),0.F,1.F)*n;const auto i=std::min(static_cast<std::size_t>(t),values_.size()-2);const float f=t-static_cast<float>(i);
        return select(values_[i])*(1-f)+select(values_[i+1])*f;
    };
    return {channel(v.r,low_.r,high_.r,[](Vec3 s){return s.r;}),channel(v.g,low_.g,high_.g,[](Vec3 s){return s.g;}),channel(v.b,low_.b,high_.b,[](Vec3 s){return s.b;})};
}
std::size_t Lut1D::size() const noexcept { return values_.size(); }
std::span<const Vec3> Lut1D::values() const { return values_; }
Vec3 Lut1D::domain_min() const noexcept { return low_; }
Vec3 Lut1D::domain_max() const noexcept { return high_; }
}

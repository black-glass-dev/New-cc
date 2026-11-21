#include "color/extended_tools.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace color {
namespace {
void require(bool b,const char* s) { if(!b) throw std::invalid_argument(s); }
Vec3 rgb(Pixel p) { return {p.r,p.g,p.b}; }
std::size_t extent(std::size_t w,std::size_t h,std::size_t budget) {
    require(w>=2&&h>=2&&w<=std::numeric_limits<std::size_t>::max()/h&&w*h<=budget/sizeof(Pixel),"Invalid image extent/budget");return w*h;
}
unsigned bayer_color(std::size_t x,std::size_t y,BayerPattern pattern) {
    constexpr unsigned colors[4][4]={{0,1,1,2},{2,1,1,0},{1,0,2,1},{1,2,0,1}};
    return colors[static_cast<unsigned>(pattern)][(y%2)*2+x%2];
}
}
Image32F demosaic_bayer16(std::span<const std::uint16_t> data,std::size_t w,std::size_t h,std::size_t stride,BayerPattern pattern,const RawDecodeParams& p,std::size_t budget) {
    (void)extent(w,h,budget);require(static_cast<unsigned>(pattern)<4&&stride>=w&&(h-1)<=std::numeric_limits<std::size_t>::max()/stride,"Invalid Bayer layout");
    const auto head=(h-1)*stride;require(head<=std::numeric_limits<std::size_t>::max()-w&&head+w<=data.size(),"Truncated Bayer source");(void)apply(Vec3{},p);
    Image32F result(w,h,budget);
    for(std::size_t y=0;y<h;++y) for(std::size_t x=0;x<w;++x) {
        std::array<float,3> value{};const auto own=bayer_color(x,y,pattern);
        for(unsigned c=0;c<3;++c) {
            if(c==own) { value[c]=data[y*stride+x];continue; }
            float total=0;unsigned count=0;
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                const auto xx=static_cast<std::ptrdiff_t>(x)+dx,yy=static_cast<std::ptrdiff_t>(y)+dy;
                if(xx<0||yy<0||xx>=static_cast<std::ptrdiff_t>(w)||yy>=static_cast<std::ptrdiff_t>(h)) continue;
                const auto ux=static_cast<std::size_t>(xx),uy=static_cast<std::size_t>(yy);
                if(bayer_color(ux,uy,pattern)==c) { total+=data[uy*stride+ux];++count; }
            }
            require(count>0,"Unobservable Bayer neighborhood");value[c]=total/static_cast<float>(count);
        }
        const auto v=apply({value[0],value[1],value[2]},p);require(finite(v),"Demosaic color overflow");result.pixels()[y*w+x]={v.r,v.g,v.b,1};
    }
    return result;
}
void validate_spatial(const SpatialFilter& p) {
    require(static_cast<unsigned>(p.kind)<=static_cast<unsigned>(SpatialKind::Halation)&&p.radius>=1&&p.radius<=8&&std::isfinite(p.spatial_sigma)&&p.spatial_sigma>=.01F&&p.spatial_sigma<=32&&std::isfinite(p.range_sigma)&&p.range_sigma>=1e-6F&&p.range_sigma<=1e6F&&std::isfinite(p.amount)&&p.amount>=0&&p.amount<=4&&std::isfinite(p.threshold)&&p.threshold>=0&&finite(p.airlight)&&p.airlight.r>0&&p.airlight.g>0&&p.airlight.b>0&&p.transmission_floor>0&&p.transmission_floor<=1,"Invalid spatial filter");
    if(p.kind==SpatialKind::Bilateral||p.kind==SpatialKind::TemporalDenoise||p.kind==SpatialKind::Dehaze) require(p.amount<=1,"Filter amount exceeds1");
}
Pixel spatial_pixel(const Image32F& source,std::size_t x,std::size_t y,const SpatialFilter& p,const Image32F* previous) {
    validate_spatial(p);
    require(x<source.width()&&y<source.height(),"Spatial coordinate outside source");
    const auto w=source.width(),h=source.height();const Pixel center=source.pixels()[y*w+x];const auto c=rgb(center);
    if(p.kind==SpatialKind::TemporalDenoise) {
        require(previous&&previous->width()==w&&previous->height()==h,"Temporal filter requires aligned previous frame");const auto old=rgb(previous->pixels()[y*w+x]),d=old-c;
        const float weight=p.amount*std::exp(-(d.r*d.r+d.g*d.g+d.b*d.b)/(2*p.range_sigma*p.range_sigma));const auto v=c+(old-c)*weight;return {v.r,v.g,v.b,center.a};
    }
    Vec3 total{};float norm=0,alpha_sum=0,alpha_min=1,alpha_max=0,dark=std::numeric_limits<float>::infinity(),bloom=0;
    const int radius=static_cast<int>(p.radius);
    for(int dy=-radius;dy<=radius;++dy) for(int dx=-radius;dx<=radius;++dx) {
        const auto xx=static_cast<std::size_t>(std::clamp(static_cast<std::ptrdiff_t>(x)+dx,std::ptrdiff_t{0},static_cast<std::ptrdiff_t>(w-1)));
        const auto yy=static_cast<std::size_t>(std::clamp(static_cast<std::ptrdiff_t>(y)+dy,std::ptrdiff_t{0},static_cast<std::ptrdiff_t>(h-1)));
        const auto q=source.pixels()[yy*w+xx];const auto v=rgb(q),d=v-c;
        float weight=std::exp(-static_cast<float>(dx*dx+dy*dy)/(2*p.spatial_sigma*p.spatial_sigma));
        if(p.kind==SpatialKind::Bilateral) weight*=std::exp(-(d.r*d.r+d.g*d.g+d.b*d.b)/(2*p.range_sigma*p.range_sigma));
        total=total+v*weight;alpha_sum+=q.a*weight;norm+=weight;alpha_min=std::min(alpha_min,q.a);alpha_max=std::max(alpha_max,q.a);
        dark=std::min({dark,v.r/p.airlight.r,v.g/p.airlight.g,v.b/p.airlight.b});bloom+=std::max(luminance(v)-p.threshold,0.F)*weight;
    }
    const auto blur=total*(1/norm);Vec3 result=c;float alpha=center.a;
    switch(p.kind) {
    case SpatialKind::Bilateral:result=c+(blur-c)*p.amount;break;
    case SpatialKind::Unsharp:result=c+(c-blur)*p.amount;break;
    case SpatialKind::MatteErode:alpha=alpha_min;break;
    case SpatialKind::MatteDilate:alpha=alpha_max;break;
    case SpatialKind::MatteBlur:alpha=alpha_sum/norm;break;
    case SpatialKind::Dehaze:{ const float t=std::clamp(1-p.amount*dark,p.transmission_floor,1.F);result=(c-p.airlight)*(1/t)+p.airlight;break; }
    case SpatialKind::Halation:result=c+Vec3{1,.25F,0}*(p.amount*bloom/norm);break;
    default:throw std::invalid_argument("Unsupported spatial filter");
    }
    return {result.r,result.g,result.b,alpha};
}
void process_spatial(const Image32F& in,Image32F& out,const SpatialFilter& p,const Image32F* previous,std::size_t budget) {
    validate_spatial(p);require(in.width()&&in.height()&&in.width()==out.width()&&in.height()==out.height(),"Spatial extent mismatch");
    require(in.pixels().size()<=budget/sizeof(Pixel),"Spatial scratch exceeds budget");
    if(p.kind==SpatialKind::TemporalDenoise) require(previous&&previous->width()==in.width()&&previous->height()==in.height(),"Missing temporal history");
    for(const auto v:in.pixels()) require(finite(v)&&v.a>=0&&v.a<=1,"Invalid spatial source pixel");
    if(previous) for(const auto v:previous->pixels()) require(finite(v)&&v.a>=0&&v.a<=1,"Invalid temporal history pixel");
    Image32F scratch(in.width(),in.height(),budget);
    for(std::size_t y=0;y<in.height();++y) for(std::size_t x=0;x<in.width();++x) { auto v=spatial_pixel(in,x,y,p,previous);require(finite(v),"Spatial result overflow");scratch.pixels()[y*in.width()+x]=v; }
    std::copy(scratch.pixels().begin(),scratch.pixels().end(),out.pixels().begin());
}
float polygon_mask(float x,float y,const PolygonWindow& p) {
    require(std::isfinite(x)&&std::isfinite(y)&&p.count>=3&&p.count<=32&&std::isfinite(p.feather)&&p.feather>=0,"Invalid polygon mask");
    bool inside=false;double closest=std::numeric_limits<double>::infinity();double area=0;
    for(std::size_t i=0;i<p.count;++i) {
        const auto a=p.vertices[i],b=p.vertices[(i+1)%p.count];require(std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(b.x)&&std::isfinite(b.y)&&std::abs(a.x)<=16&&std::abs(a.y)<=16,"Invalid polygon vertex");
        const double dx=static_cast<double>(b.x)-a.x,dy=static_cast<double>(b.y)-a.y,len=dx*dx+dy*dy;require(len>1e-14,"Degenerate polygon edge");
        const double u=std::clamp(((static_cast<double>(x)-a.x)*dx+(static_cast<double>(y)-a.y)*dy)/len,0.,1.);
        closest=std::min(closest,std::hypot(x-a.x-u*dx,y-a.y-u*dy));area+=static_cast<double>(a.x)*b.y-static_cast<double>(b.x)*a.y;
        if((a.y>y)!=(b.y>y)) { const double hit=(static_cast<double>(b.x)-a.x)*(y-a.y)/(b.y-a.y)+a.x;if(x<hit) inside=!inside; }
    }
    require(std::abs(area)>1e-10,"Zero-area polygon");const double distance=inside?-closest:closest;float m{};
    if(p.feather==0) m=distance<=0?1.F:0.F;else { const float t=static_cast<float>(std::clamp((distance+p.feather)/(2*p.feather),0.,1.));m=1-t*t*(3-2*t); }
    return p.invert?1-m:m;
}
} // namespace color

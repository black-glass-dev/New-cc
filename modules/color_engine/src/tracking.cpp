#include "color/tracking.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>

namespace color {
namespace {
void require(bool b,const char* s) { if(!b) throw std::invalid_argument(s); }
void validate(const Image32F& image) {
    require(image.width()>=2&&image.height()>=2,"Tracking/sizing source too small");for(const auto p:image.pixels()) require(finite(p)&&p.a>=0&&p.a<=1,"Invalid tracking/sizing source");
}
Pixel sample(const Image32F& image,float x,float y) {
    const auto ix=std::min(static_cast<std::size_t>(x),image.width()-1),iy=std::min(static_cast<std::size_t>(y),image.height()-1),jx=std::min(ix+1,image.width()-1),jy=std::min(iy+1,image.height()-1);
    const float tx=x-static_cast<float>(ix),ty=y-static_cast<float>(iy);const auto a=image.pixels()[iy*image.width()+ix],b=image.pixels()[iy*image.width()+jx],c=image.pixels()[jy*image.width()+ix],d=image.pixels()[jy*image.width()+jx];
    auto lerp=[&](float p,float q,float r,float s) { return (p*(1-tx)+q*tx)*(1-ty)+(r*(1-tx)+s*tx)*ty; };
    return {lerp(a.r,b.r,c.r,d.r),lerp(a.g,b.g,c.g,d.g),lerp(a.b,b.b,c.b,d.b),lerp(a.a,b.a,c.a,d.a)};
}
float intensity(const Image32F& image,float x,float y) { auto p=sample(image,x,y);return luminance({p.r,p.g,p.b}); }
bool inside(const Image32F& image,Point2 p,std::size_t radius) {
    const float r=static_cast<float>(radius+1);return std::isfinite(p.x)&&std::isfinite(p.y)&&p.x>=r&&p.y>=r&&p.x<static_cast<float>(image.width())-r-1&&p.y<static_cast<float>(image.height())-r-1;
}
Image32F half(const Image32F& image,std::size_t budget) {
    Image32F out(image.width()/2,image.height()/2,budget);for(std::size_t y=0;y<out.height();++y) for(std::size_t x=0;x<out.width();++x) out.pixels()[y*out.width()+x]=sample(image,static_cast<float>(2*x)+.5F,static_cast<float>(2*y)+.5F);return out;
}
TrackedPoint level(const Image32F& previous,const Image32F& current,FeaturePoint source,Point2 guess,const KLTOptions& options) {
    TrackedPoint out;out.source=source;out.target={source.id,guess.x,guess.y};const Point2 origin{source.x,source.y};if(!inside(previous,origin,options.radius)) return out;
    const int radius=static_cast<int>(options.radius);Point2 q=guess;
    for(std::size_t step=0;step<options.iterations;++step) {
        if(!inside(current,q,options.radius)) { out.status=TrackStatus::Outside;return out; }
        double a=0,b=0,c=0,u=0,v=0;
        for(int dy=-radius;dy<=radius;++dy) for(int dx=-radius;dx<=radius;++dx) {
            const float x=q.x+static_cast<float>(dx),y=q.y+static_cast<float>(dy);
            const float gx=(intensity(current,x+1,y)-intensity(current,x-1,y))*.5F,gy=(intensity(current,x,y+1)-intensity(current,x,y-1))*.5F;
            const float difference=intensity(current,x,y)-intensity(previous,source.x+static_cast<float>(dx),source.y+static_cast<float>(dy));
            a+=gx*gx;b+=gx*gy;c+=gy*gy;u+=gx*difference;v+=gy*difference;
        }
        const double determinant=a*c-b*b;if(!(determinant>1e-6*(a+c)*(a+c)&&determinant>1e-12)) { out.status=TrackStatus::Singular;return out; }
        double delta_x=(c*u-b*v)/determinant,delta_y=(a*v-b*u)/determinant;const double length=std::hypot(delta_x,delta_y);if(length>4) {delta_x*=4/length;delta_y*=4/length;}
        q.x-=static_cast<float>(delta_x);q.y-=static_cast<float>(delta_y);if(length<options.convergence) break;
    }
    if(!inside(current,q,options.radius)) return out;
    double square_error=0;std::size_t samples=0;
    for(int dy=-radius;dy<=radius;++dy) for(int dx=-radius;dx<=radius;++dx) {const double e=intensity(current,q.x+static_cast<float>(dx),q.y+static_cast<float>(dy))-intensity(previous,source.x+static_cast<float>(dx),source.y+static_cast<float>(dy));square_error+=e*e;++samples;}
    out.target={source.id,q.x,q.y};out.rms_error=static_cast<float>(std::sqrt(square_error/static_cast<double>(samples)));out.status=out.rms_error<=options.max_error?TrackStatus::Good:TrackStatus::ResidualTooHigh;return out;
}
Affine2D fit(std::span<const TrackedPoint> points,std::span<const std::size_t> indices) {
    double sx=0,sy=0,tx=0,ty=0;for(auto i:indices) {sx+=points[i].source.x;sy+=points[i].source.y;tx+=points[i].target.x;ty+=points[i].target.y;}
    const double n=static_cast<double>(indices.size());sx/=n;sy/=n;tx/=n;ty/=n;double xx=0,xy=0,yy=0,xu=0,yu=0,xv=0,yv=0;
    for(auto i:indices) {const double x=points[i].source.x-sx,y=points[i].source.y-sy,u=points[i].target.x-tx,v=points[i].target.y-ty;xx+=x*x;xy+=x*y;yy+=y*y;xu+=x*u;yu+=y*u;xv+=x*v;yv+=y*v;}
    const double det=xx*yy-xy*xy;require(det>1e-10*(xx+yy)*(xx+yy)&&det>1e-12,"Degenerate planar correspondences");
    const double a=(xu*yy-yu*xy)/det,b=(yu*xx-xu*xy)/det,c=(xv*yy-yv*xy)/det,d=(yv*xx-xv*xy)/det;
    Affine2D result;result.m={static_cast<float>(a),static_cast<float>(b),static_cast<float>(tx-a*sx-b*sy),static_cast<float>(c),static_cast<float>(d),static_cast<float>(ty-c*sx-d*sy)};for(auto v:result.m) require(std::isfinite(v),"Affine fit overflow");return result;
}
}
std::vector<FeaturePoint> detect_features(const Image32F& image,std::size_t max_points,float distance,float threshold,std::size_t budget) {
    validate(image);require(max_points>=1&&max_points<=1024&&std::isfinite(distance)&&distance>=1&&std::isfinite(threshold)&&threshold>0,"Invalid feature detector");
    struct Candidate {float score;FeaturePoint p;};require(image.pixels().size()<=budget/sizeof(Candidate),"Feature candidate budget");std::vector<Candidate> candidates;candidates.reserve(image.pixels().size());
    if(image.width()<9||image.height()<9) return {};
    for(std::size_t y=3;y+3<image.height();++y) for(std::size_t x=3;x+3<image.width();++x) {
        double a=0,b=0,c=0;for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
            const float px=static_cast<float>(x)+static_cast<float>(dx),py=static_cast<float>(y)+static_cast<float>(dy),gx=(intensity(image,px+1,py)-intensity(image,px-1,py))*.5F,gy=(intensity(image,px,py+1)-intensity(image,px,py-1))*.5F;a+=gx*gx;b+=gx*gy;c+=gy*gy;
        }
        const float score=static_cast<float>((a+c-std::hypot(a-c,2*b))*.5);if(score>=threshold) candidates.push_back({score,{0,static_cast<float>(x),static_cast<float>(y)}});
    }
    std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b){if(a.score!=b.score) return a.score>b.score;if(a.p.y!=b.p.y) return a.p.y<b.p.y;return a.p.x<b.p.x;});std::vector<FeaturePoint> result;
    for(const auto& candidate:candidates) {bool separated=true;for(const auto& p:result) if(std::hypot(p.x-candidate.p.x,p.y-candidate.p.y)<distance) {separated=false;break;}if(separated) {auto p=candidate.p;p.id=result.size()+1;result.push_back(p);if(result.size()==max_points) break;}}
    return result;
}
std::vector<TrackedPoint> track_klt(const Image32F& previous,const Image32F& current,std::span<const FeaturePoint> points,KLTOptions options,std::size_t budget) {
    validate(previous);validate(current);require(previous.width()==current.width()&&previous.height()==current.height()&&points.size()<=1024&&options.radius>=2&&options.radius<=8&&options.levels>=1&&options.levels<=5&&options.iterations>=1&&options.iterations<=50&&options.max_error>0&&std::isfinite(options.max_error)&&options.convergence>0&&std::isfinite(options.convergence),"Invalid KLT inputs");
    for(auto p:points) require(std::isfinite(p.x)&&std::isfinite(p.y)&&std::abs(p.x)<=1e6F&&std::abs(p.y)<=1e6F,"Invalid feature coordinates");
    std::vector<Image32F> pp,cp;pp.reserve(options.levels);cp.reserve(options.levels);std::size_t used=0;
    for(std::size_t l=1;l<options.levels;++l) {const auto& a=pp.empty()?previous:pp.back();const auto& b=cp.empty()?current:cp.back();if(a.width()/2<2*(options.radius+2)+1||a.height()/2<2*(options.radius+2)+1) break;const auto bytes=(a.width()/2)*(a.height()/2)*sizeof(Pixel);require(bytes<=(budget-used)/2,"KLT pyramid budget");pp.push_back(half(a,bytes));cp.push_back(half(b,bytes));used+=2*bytes;}
    std::vector<TrackedPoint> result;result.reserve(points.size());
    for(auto point:points) {
        Point2 q{point.x,point.y};const auto levels=pp.size()+1;const float scale=std::exp2(static_cast<float>(levels-1));q={(q.x+.5F)/scale-.5F,(q.y+.5F)/scale-.5F};TrackedPoint tracked;
        for(std::size_t reverse=levels;reverse>0;--reverse) {const auto l=reverse-1;const float factor=std::exp2(static_cast<float>(l));FeaturePoint p{point.id,(point.x+.5F)/factor-.5F,(point.y+.5F)/factor-.5F};tracked=level(l==0?previous:pp[l-1],l==0?current:cp[l-1],p,q,options);if(tracked.status!=TrackStatus::Good) break;q={(tracked.target.x+.5F)*2-.5F,(tracked.target.y+.5F)*2-.5F};}
        tracked.source=point;tracked.target.id=point.id;result.push_back(tracked);
    }
    return result;
}
Point2 Affine2D::apply(Point2 p) const {return {m[0]*p.x+m[1]*p.y+m[2],m[3]*p.x+m[4]*p.y+m[5]};}
PlanarTrack fit_planar_affine(std::span<const TrackedPoint> points,float threshold,std::size_t trials) {
    require(points.size()>=3&&points.size()<=1024&&std::isfinite(threshold)&&threshold>0&&trials>=1&&trials<=4096,"Invalid affine RANSAC");std::vector<std::size_t> good;
    for(std::size_t i=0;i<points.size();++i) if(points[i].status==TrackStatus::Good) {const auto& p=points[i];require(std::isfinite(p.source.x)&&std::isfinite(p.source.y)&&std::isfinite(p.target.x)&&std::isfinite(p.target.y)&&std::abs(p.source.x)<=1e6F&&std::abs(p.source.y)<=1e6F&&std::abs(p.target.x)<=1e6F&&std::abs(p.target.y)<=1e6F,"Invalid planar feature");good.push_back(i);}
    require(good.size()>=3,"Insufficient good tracks");std::mt19937 random(0x434f4c52);std::uniform_int_distribution<std::size_t> pick(0,good.size()-1);std::vector<std::size_t> best;
    for(std::size_t t=0;t<trials;++t) {const std::array<std::size_t,3> selected{good[pick(random)],good[pick(random)],good[pick(random)]};if(selected[0]==selected[1]||selected[1]==selected[2]||selected[0]==selected[2]) continue;Affine2D transform;try{transform=fit(points,selected);}catch(const std::invalid_argument&){continue;}std::vector<std::size_t> inliers;for(auto i:good) {const auto p=transform.apply({points[i].source.x,points[i].source.y});if(std::hypot(p.x-points[i].target.x,p.y-points[i].target.y)<=threshold) inliers.push_back(i);}if(inliers.size()>best.size()) best=std::move(inliers);}
    require(best.size()>=3,"No nondegenerate planar consensus");PlanarTrack result;result.transform=fit(points,best);double error=0;for(auto i:best) {result.inlier_ids.push_back(points[i].source.id);const auto p=result.transform.apply({points[i].source.x,points[i].source.y});error+=std::pow(p.x-points[i].target.x,2)+std::pow(p.y-points[i].target.y,2);}result.rms_error=static_cast<float>(std::sqrt(error/static_cast<double>(best.size())));return result;
}
PolygonWindow tracked_window(const PolygonWindow& window,const Affine2D& transform,std::size_t width,std::size_t height) {
    require(width>0&&height>0,"Empty tracked window extent");(void)polygon_mask(0,0,window);for(auto v:transform.m) require(std::isfinite(v),"Invalid tracked transform");PolygonWindow out=window;
    for(std::size_t i=0;i<window.count;++i) {const auto p=transform.apply({window.vertices[i].x*static_cast<float>(width),window.vertices[i].y*static_cast<float>(height)});out.vertices[i]={p.x/static_cast<float>(width),p.y/static_cast<float>(height)};}(void)polygon_mask(0,0,out);return out;
}
void resize_affine(const Image32F& source,Image32F& destination,const Affine2D& transform,std::size_t budget) {
    validate(source);require(destination.width()&&destination.height()&&destination.pixels().size()<=budget/sizeof(Pixel),"Invalid sizing destination/budget");for(float v:transform.m) require(std::isfinite(v),"Invalid sizing transform");Image32F scratch(destination.width(),destination.height(),budget);
    for(std::size_t y=0;y<scratch.height();++y) for(std::size_t x=0;x<scratch.width();++x) {const auto p=transform.apply({static_cast<float>(x),static_cast<float>(y)});require(std::isfinite(p.x)&&std::isfinite(p.y),"Sizing coordinate overflow");Pixel q{0,0,0,0};if(p.x>=0&&p.y>=0&&p.x<=static_cast<float>(source.width()-1)&&p.y<=static_cast<float>(source.height()-1)) q=sample(source,p.x,p.y);require(finite(q),"Sizing pixel overflow");scratch.pixels()[y*scratch.width()+x]=q;}
    std::copy(scratch.pixels().begin(),scratch.pixels().end(),destination.pixels().begin());
}
Affine2D reframe_transform(std::size_t sw,std::size_t sh,std::size_t dw,std::size_t dh,ReframeMode mode) {
    require(sw&&sh&&dw&&dh&&sw<=1u<<20&&sh<=1u<<20&&dw<=1u<<20&&dh<=1u<<20,"Invalid reframe extent");
    const double fx=static_cast<double>(dw)/static_cast<double>(sw),fy=static_cast<double>(dh)/static_cast<double>(sh);
    Affine2D t;
    if(mode==ReframeMode::Stretch) { t.m={static_cast<float>(1/fx),0,0,0,static_cast<float>(1/fy),0}; return t; }
    const double k=mode==ReframeMode::Fit?std::min(fx,fy):std::max(fx,fy);
    const double ox=(static_cast<double>(dw)-static_cast<double>(sw)*k)/2,oy=(static_cast<double>(dh)-static_cast<double>(sh)*k)/2;
    t.m={static_cast<float>(1/k),0,static_cast<float>(-ox/k),0,static_cast<float>(1/k),static_cast<float>(-oy/k)};
    for(auto v:t.m) require(std::isfinite(v),"Reframe overflow");
    return t;
}
} // namespace color

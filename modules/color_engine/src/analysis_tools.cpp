#include "color/extended_tools.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <stdexcept>

namespace color {
namespace {
void require(bool b,const char* s) { if(!b) throw std::invalid_argument(s); }
void valid(const ImageStatistics& s) {
    require(s.samples>0&&s.alpha_weight>0&&std::isfinite(s.alpha_weight)&&finite(s.mean)&&finite(s.deviation)&&s.deviation.r>=0&&s.deviation.g>=0&&s.deviation.b>=0&&std::isfinite(s.median_positive_luma)&&s.median_positive_luma>=0,"Invalid image statistics");
}
}
ImageStatistics analyze_image(const Image32F& image) {
    require(image.width()&&image.height(),"Empty statistics source");
    ImageStatistics s;std::array<double,3> mean{},m2{};std::array<double,2048> histogram{};double positive_weight=0;
    for(const auto p:image.pixels()) {
        require(finite(p)&&p.a>=0&&p.a<=1,"Invalid statistics pixel");if(p.a==0) continue;
        const double w=p.a,next=s.alpha_weight+w;const std::array<double,3> value{p.r,p.g,p.b};
        for(std::size_t k=0;k<3;++k) { const double delta=value[k]-mean[k];mean[k]+=w/next*delta;m2[k]+=w*delta*(value[k]-mean[k]); }
        s.alpha_weight=next;++s.samples;
        const float y=luminance({p.r,p.g,p.b});
        if(y>0) { const double coordinate=std::clamp((std::log2(static_cast<double>(y))+24)/48,0.,1.);const auto bin=std::min(static_cast<std::size_t>(coordinate*2048),std::size_t{2047});histogram[bin]+=w;positive_weight+=w; }
    }
    require(s.samples>0,"No nontransparent statistics samples");
    s.mean={static_cast<float>(mean[0]),static_cast<float>(mean[1]),static_cast<float>(mean[2])};
    s.deviation={static_cast<float>(std::sqrt(std::max(m2[0]/s.alpha_weight,0.))),static_cast<float>(std::sqrt(std::max(m2[1]/s.alpha_weight,0.))),static_cast<float>(std::sqrt(std::max(m2[2]/s.alpha_weight,0.)))};
    if(positive_weight>0) { double cumulative=0;for(std::size_t i=0;i<histogram.size();++i) { cumulative+=histogram[i];if(cumulative>=positive_weight*.5) { s.median_positive_luma=static_cast<float>(std::exp2((static_cast<double>(i)+.5)/2048*48-24));break; } } }
    valid(s);return s;
}
Primaries estimate_auto_balance(const ImageStatistics& s,AutoBalanceOptions options) {
    valid(s);require(std::isfinite(options.target_luma)&&options.target_luma>0&&std::isfinite(options.max_exposure)&&options.max_exposure>=0&&options.max_exposure<=16&&options.white_balance_strength>=0&&options.white_balance_strength<=1&&s.median_positive_luma>0,"Unobservable/invalid auto balance");
    Primaries p;
    if(options.white_balance_strength>0) {
        require(s.mean.r>1e-12F&&s.mean.g>1e-12F&&s.mean.b>1e-12F,"Grey-world balance needs positive channel means");
        const double grey=std::exp((std::log(static_cast<double>(s.mean.r))+std::log(static_cast<double>(s.mean.g))+std::log(static_cast<double>(s.mean.b)))/3);
        auto gain=[&](float channel) { return static_cast<float>(std::pow(std::clamp(grey/channel,.25,4.),options.white_balance_strength)); };
        p.gain={gain(s.mean.r),gain(s.mean.g),gain(s.mean.b)};
    }
    const float old_y=luminance(s.mean),new_y=luminance({s.mean.r*p.gain.r,s.mean.g*p.gain.g,s.mean.b*p.gain.b});
    require(old_y>0&&new_y>0,"Invalid auto-balance luminance");
    const double exposure=std::log2(static_cast<double>(options.target_luma)/s.median_positive_luma*old_y/new_y);
    p.exposure=static_cast<float>(std::clamp(exposure,-static_cast<double>(options.max_exposure),static_cast<double>(options.max_exposure)));return p;
}
Primaries estimate_shot_match(const ImageStatistics& source,const ImageStatistics& reference,float max_gain) {
    valid(source);valid(reference);require(std::isfinite(max_gain)&&max_gain>=1&&max_gain<=64,"Invalid shot-match gain limit");
    auto gain=[&](float src,float dst) { if(src<=1e-12F) { require(dst<=1e-12F,"Flat source cannot synthesize reference variance");return 1.F; }return std::min(dst/src,max_gain); };
    Primaries p;p.gain={gain(source.deviation.r,reference.deviation.r),gain(source.deviation.g,reference.deviation.g),gain(source.deviation.b,reference.deviation.b)};
    p.offset={reference.mean.r-p.gain.r*source.mean.r,reference.mean.g-p.gain.g*source.mean.g,reference.mean.b-p.gain.b*source.mean.b};
    require(finite(p.gain)&&finite(p.offset),"Shot-match coefficient overflow");return p;
}
std::shared_ptr<const Image32F> depth_matte(std::span<const float> depth,std::size_t width,std::size_t height,DepthRange r,std::size_t budget) {
    require(width&&height&&depth.size()==width*height&&width<=std::numeric_limits<std::size_t>::max()/height,"Depth map extent mismatch");
    require(std::isfinite(r.center)&&std::isfinite(r.width)&&r.width>=0&&std::isfinite(r.softness)&&r.softness>=0,"Invalid depth range");
    for(float d:depth) require(std::isfinite(d),"Nonfinite depth sample");
    auto image=std::make_shared<Image32F>(width,height,budget);
    for(std::size_t i=0;i<depth.size();++i) {
        const float distance=std::abs(depth[i]-r.center)-r.width*.5F;float m{};
        if(r.softness==0) m=distance<=0?1.F:0.F;
        else { const float t=std::clamp(distance/r.softness,0.F,1.F);m=1-t*t*(3-2*t); }
        if(r.invert) m=1-m;
        image->pixels()[i]={0,0,0,m};
    }
    return image;
}
} // namespace color


#pragma once
#include "color_pipeline_api.hpp"
#include <memory>
#include <span>

namespace color {
struct ImageStatistics {
    Vec3 mean{},deviation{};float median_positive_luma{};
    double alpha_weight{};std::uint64_t samples{};
};
// Depth hook: caller supplies per-pixel depth in any consistent unit. Output alpha is a smooth band
// around center/width/softness; RGB is zero. Feed as ExternalPlane into a matte or window path.
struct DepthRange { float center{.5F},width{.2F},softness{.05F}; bool invert{}; };
std::shared_ptr<const Image32F> depth_matte(std::span<const float> depth,std::size_t width,std::size_t height,DepthRange,std::size_t budget_bytes=256U*1024U*1024U);
// Linear working RGB. Alpha-weighted population moments; deterministic log histogram median.
ImageStatistics analyze_image(const Image32F&);
struct AutoBalanceOptions { float target_luma{.18F},max_exposure{4},white_balance_strength{1}; };
Primaries estimate_auto_balance(const ImageStatistics&,AutoBalanceOptions={});
Primaries estimate_shot_match(const ImageStatistics& source,const ImageStatistics& reference,float max_gain=16);

enum class BayerPattern { RGGB,BGGR,GRBG,GBRG };
// Original bilinear demosaic, not a compressed RAW/container codec or vendor SDK.
Image32F demosaic_bayer16(std::span<const std::uint16_t>,std::size_t width,std::size_t height,
                        std::size_t row_stride_samples,BayerPattern,const RawDecodeParams&,
                        std::size_t budget_bytes=256U*1024U*1024U);

// Pixel reference shared with frame utility and spatial DAG nodes. Source must remain immutable.
Pixel spatial_pixel(const Image32F&,std::size_t x,std::size_t y,const SpatialFilter&,const Image32F* previous=nullptr);
void validate_spatial(const SpatialFilter&);
// Transactional, padding-independent image utility; in/out aliasing supported.
void process_spatial(const Image32F&,Image32F&,const SpatialFilter&,const Image32F* previous=nullptr,
                     std::size_t budget_bytes=256U*1024U*1024U);

float polygon_mask(float x,float y,const PolygonWindow&);
} // namespace color

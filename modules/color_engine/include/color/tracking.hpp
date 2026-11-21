#pragma once
#include "extended_tools.hpp"
namespace color {
struct FeaturePoint { std::uint64_t id{};float x{},y{}; };
enum class TrackStatus { Good,Outside,Singular,ResidualTooHigh };
struct TrackedPoint { FeaturePoint source,target;TrackStatus status{TrackStatus::Outside};float rms_error{}; };
struct KLTOptions { std::size_t radius{3},levels{3},iterations{20};float max_error{.15F},convergence{.001F}; };
std::vector<FeaturePoint> detect_features(const Image32F&,std::size_t max_points=128,float min_distance=6,float minimum_eigenvalue=.0001F,std::size_t budget_bytes=256U*1024U*1024U);
std::vector<TrackedPoint> track_klt(const Image32F& previous,const Image32F& current,std::span<const FeaturePoint>,KLTOptions={},std::size_t budget_bytes=256U*1024U*1024U);
struct Affine2D {
    std::array<float,6> m{1,0,0,0,1,0};
    Point2 apply(Point2) const;
};
struct PlanarTrack { Affine2D transform;std::vector<std::uint64_t> inlier_ids;float rms_error{}; };
// Affine planar model; explicitly not a projective homography or AI object tracker.
PlanarTrack fit_planar_affine(std::span<const TrackedPoint>,float inlier_threshold=1,std::size_t trials=128);
PolygonWindow tracked_window(const PolygonWindow&,const Affine2D& pixel_transform,std::size_t width,std::size_t height);
enum class ReframeMode { Fit,Fill,Stretch };
// Destination->source affine for sizing/reframing. Fit letterboxes, Fill crops, Stretch distorts.
Affine2D reframe_transform(std::size_t source_width,std::size_t source_height,std::size_t destination_width,std::size_t destination_height,ReframeMode);
// Inverse-map sizing, bilinear straight-alpha sampling; outside frame becomes transparent.
void resize_affine(const Image32F&,Image32F&,const Affine2D& destination_to_source,std::size_t budget_bytes=256U*1024U*1024U);
} // namespace color

#pragma once

#include "timeline/media_pool.hpp"
#include "timeline/rational_time.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace owncut::timeline {

enum class RetimeInterpolationMode : uint8_t {
    Nearest = 0,
    FrameBlend = 1,
    OpticalFlow = 2,
    SpeedWarp = 3
};

struct CubicBezierCurve {
    double p1x = 0.25;
    double p1y = 0.1;
    double p2x = 0.25;
    double p2y = 1.0;

    [[nodiscard]] double evaluate(double x) const noexcept;
    [[nodiscard]] double derivative(double x) const noexcept;
};

struct SpeedPoint {
    RationalTime timeline_offset{};
    double speed = 1.0; // >0 forward, 0 freeze, <0 reverse
    CubicBezierCurve ease_out{};
};

struct OpticalFlowField {
    int32_t width = 0;
    int32_t height = 0;
    std::vector<float> flow_xy; // Interleaved (dx, dy) per pixel in pixels from frame A -> frame B

    [[nodiscard]] bool valid() const noexcept {
        return width > 0 && height > 0 &&
               flow_xy.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 2;
    }
    [[nodiscard]] float mean_magnitude() const noexcept;
};

class ISpeedWarpProvider {
public:
    virtual ~ISpeedWarpProvider() = default;
    [[nodiscard]] virtual bool available() const noexcept = 0;
    virtual bool interpolate_frame(const VideoFrame& frame_a,
                                   const VideoFrame& frame_b,
                                   float alpha,
                                   VideoFrame& out_frame,
                                   FrameMemoryPool* pool) = 0;
};

class RetimeEngine {
public:
    static void set_speed_warp_provider(std::shared_ptr<ISpeedWarpProvider> provider);

    // Piecewise integration over SpeedPoints + CubicBezier easing -> exact source time
    static RationalTime map_time_with_speed_points(const RationalTime& local_timeline_t,
                                                   const std::vector<SpeedPoint>& points,
                                                   const RationalTime& clip_source_in,
                                                   bool reversed = false,
                                                   const RationalTime& clip_source_out = RationalTime::zero());

    // Freeze frame helper: clamps time at freeze anchor for freeze_duration
    static RationalTime map_freeze_frame(const RationalTime& local_t,
                                         const RationalTime& freeze_at,
                                         const RationalTime& freeze_duration);

    // Dense Pyramidal Lucas-Kanade Optical Flow estimator (uses ARM NEON on ARM/AArch64, C++20 scalar fallback on x86_64)
    static OpticalFlowField estimate_optical_flow(const VideoFrame& frame_a,
                                                  const VideoFrame& frame_b,
                                                  int32_t pyramid_levels = 3,
                                                  int32_t window_radius = 2);

    // Bidirectional flow-guided frame interpolation
    static bool interpolate_frame(const VideoFrame& frame_a,
                                  const VideoFrame& frame_b,
                                  float alpha,
                                  RetimeInterpolationMode mode,
                                  VideoFrame& out_frame,
                                  FrameMemoryPool* pool = nullptr);

    // SmoothCut jump-cut morph across an edit cut
    static bool smooth_cut_morph(const VideoFrame& outgoing_frame,
                                 const VideoFrame& incoming_frame,
                                 float progress,
                                 VideoFrame& out_frame,
                                 FrameMemoryPool* pool = nullptr);
};

} // namespace owncut::timeline

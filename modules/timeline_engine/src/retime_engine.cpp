#include "timeline/retime_engine.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>

#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace owncut::timeline {
namespace {

std::mutex g_speedwarp_mutex;
std::shared_ptr<ISpeedWarpProvider> g_speedwarp_provider;

double bezier_coord(double t, double p1, double p2) noexcept {
    const double u = 1.0 - t;
    return 3.0 * u * u * t * p1 + 3.0 * u * t * t * p2 + t * t * t;
}

double bezier_slope(double t, double p1, double p2) noexcept {
    const double u = 1.0 - t;
    return 3.0 * u * u * p1 + 6.0 * u * t * (p2 - p1) + 3.0 * t * t * (1.0 - p2);
}

double solve_bezier_t(double x, double p1x, double p2x) noexcept {
    double t = std::clamp(x, 0.0, 1.0);
    for (int iter = 0; iter < 8; ++iter) {
        const double err = bezier_coord(t, p1x, p2x) - x;
        if (std::abs(err) < 1e-7) break;
        const double dx = bezier_slope(t, p1x, p2x);
        if (std::abs(dx) < 1e-7) break;
        t = std::clamp(t - err / dx, 0.0, 1.0);
    }
    return t;
}

std::vector<float> extract_luma(const VideoFrame& f) {
    std::vector<float> luma(static_cast<std::size_t>(f.width) * static_cast<std::size_t>(f.height));
    const float* p = f.data();
    for (int32_t y = 0; y < f.height; ++y) {
        const float* row = p + y * f.stride_floats;
        float* dst = luma.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(f.width);
        for (int32_t x = 0; x < f.width; ++x) {
            dst[x] = 0.2126f * row[x * 4 + 0] + 0.7152f * row[x * 4 + 1] + 0.0722f * row[x * 4 + 2];
        }
    }
    return luma;
}

struct GrayLevel {
    int32_t w = 0;
    int32_t h = 0;
    std::vector<float> data;
};

float sample_gray_bilinear(const GrayLevel& img, float x, float y) noexcept {
    x = std::clamp(x, 0.0f, static_cast<float>(img.w - 1));
    y = std::clamp(y, 0.0f, static_cast<float>(img.h - 1));
    const int32_t x0 = static_cast<int32_t>(std::floor(x));
    const int32_t y0 = static_cast<int32_t>(std::floor(y));
    const int32_t x1 = std::min(img.w - 1, x0 + 1);
    const int32_t y1 = std::min(img.h - 1, y0 + 1);
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);

    const float v00 = img.data[static_cast<std::size_t>(y0) * img.w + x0];
    const float v10 = img.data[static_cast<std::size_t>(y0) * img.w + x1];
    const float v01 = img.data[static_cast<std::size_t>(y1) * img.w + x0];
    const float v11 = img.data[static_cast<std::size_t>(y1) * img.w + x1];
    return (v00 * (1.0f - fx) + v10 * fx) * (1.0f - fy) +
           (v01 * (1.0f - fx) + v11 * fx) * fy;
}

void sample_rgba_bilinear(const VideoFrame& img, float x, float y, float out_rgba[4]) noexcept {
    x = std::clamp(x, 0.0f, static_cast<float>(img.width - 1));
    y = std::clamp(y, 0.0f, static_cast<float>(img.height - 1));
    const int32_t x0 = static_cast<int32_t>(std::floor(x));
    const int32_t y0 = static_cast<int32_t>(std::floor(y));
    const int32_t x1 = std::min(img.width - 1, x0 + 1);
    const int32_t y1 = std::min(img.height - 1, y0 + 1);
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);
    const float w00 = (1.0f - fx) * (1.0f - fy);
    const float w10 = fx * (1.0f - fy);
    const float w01 = (1.0f - fx) * fy;
    const float w11 = fx * fy;

    const float* p00 = img.data() + y0 * img.stride_floats + x0 * 4;
    const float* p10 = img.data() + y0 * img.stride_floats + x1 * 4;
    const float* p01 = img.data() + y1 * img.stride_floats + x0 * 4;
    const float* p11 = img.data() + y1 * img.stride_floats + x1 * 4;

    for (int c = 0; c < 4; ++c) {
        out_rgba[c] = p00[c] * w00 + p10[c] * w10 + p01[c] * w01 + p11[c] * w11;
    }
}

GrayLevel downsample_2x(const GrayLevel& src) {
    GrayLevel dst;
    dst.w = std::max(4, src.w / 2);
    dst.h = std::max(4, src.h / 2);
    dst.data.resize(static_cast<std::size_t>(dst.w) * static_cast<std::size_t>(dst.h));
    for (int32_t y = 0; y < dst.h; ++y) {
        for (int32_t x = 0; x < dst.w; ++x) {
            const int32_t sx = std::min(src.w - 1, x * 2);
            const int32_t sy = std::min(src.h - 1, y * 2);
            float sum = 0.0f;
            float wt = 0.0f;
            for (int dy = -1; dy <= 1; ++dy) {
                const int32_t yy = std::clamp(sy + dy, 0, src.h - 1);
                for (int dx = -1; dx <= 1; ++dx) {
                    const int32_t xx = std::clamp(sx + dx, 0, src.w - 1);
                    const float w = (dx == 0 && dy == 0) ? 4.0f : ((dx == 0 || dy == 0) ? 2.0f : 1.0f);
                    sum += src.data[static_cast<std::size_t>(yy) * src.w + xx] * w;
                    wt += w;
                }
            }
            dst.data[static_cast<std::size_t>(y) * dst.w + x] = sum / wt;
        }
    }
    return dst;
}

void compute_row_tensor_products(const float* ix_row, const float* iy_row, const float* it_row,
                                 int32_t x0, int32_t x1,
                                 float& sxx, float& sxy, float& syy, float& sxt, float& syt) noexcept {
#if defined(__aarch64__)
    float32x4_t vxx = vdupq_n_f32(0.0f);
    float32x4_t vxy = vdupq_n_f32(0.0f);
    float32x4_t vyy = vdupq_n_f32(0.0f);
    float32x4_t vxt = vdupq_n_f32(0.0f);
    float32x4_t vyt = vdupq_n_f32(0.0f);
    int32_t x = x0;
    for (; x + 4 <= x1 + 1; x += 4) {
        const float32x4_t vx = vld1q_f32(ix_row + x);
        const float32x4_t vy = vld1q_f32(iy_row + x);
        const float32x4_t vt = vld1q_f32(it_row + x);
        vxx = vmlaq_f32(vxx, vx, vx);
        vxy = vmlaq_f32(vxy, vx, vy);
        vyy = vmlaq_f32(vyy, vy, vy);
        vxt = vmlaq_f32(vxt, vx, vt);
        vyt = vmlaq_f32(vyt, vy, vt);
    }
    sxx += vaddvq_f32(vxx);
    sxy += vaddvq_f32(vxy);
    syy += vaddvq_f32(vyy);
    sxt += vaddvq_f32(vxt);
    syt += vaddvq_f32(vyt);
    for (; x <= x1; ++x) {
        const float gx = ix_row[x], gy = iy_row[x], gt = it_row[x];
        sxx += gx * gx; sxy += gx * gy; syy += gy * gy;
        sxt += gx * gt; syt += gy * gt;
    }
#else
    for (int32_t x = x0; x <= x1; ++x) {
        const float gx = ix_row[x];
        const float gy = iy_row[x];
        const float gt = it_row[x];
        sxx += gx * gx;
        sxy += gx * gy;
        syy += gy * gy;
        sxt += gx * gt;
        syt += gy * gt;
    }
#endif
}

} // namespace

double CubicBezierCurve::evaluate(double x) const noexcept {
    x = std::clamp(x, 0.0, 1.0);
    const double t = solve_bezier_t(x, p1x, p2x);
    return bezier_coord(t, p1y, p2y);
}

double CubicBezierCurve::derivative(double x) const noexcept {
    constexpr double h = 1e-4;
    const double x0 = std::clamp(x - h, 0.0, 1.0);
    const double x1 = std::clamp(x + h, 0.0, 1.0);
    if (x1 <= x0) return 1.0;
    return (evaluate(x1) - evaluate(x0)) / (x1 - x0);
}

float OpticalFlowField::mean_magnitude() const noexcept {
    if (!valid()) return 0.0f;
    double sum = 0.0;
    const std::size_t n = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (std::size_t i = 0; i < n; ++i) {
        const float dx = flow_xy[i * 2 + 0];
        const float dy = flow_xy[i * 2 + 1];
        sum += std::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy);
    }
    return static_cast<float>(sum / static_cast<double>(n));
}

void RetimeEngine::set_speed_warp_provider(std::shared_ptr<ISpeedWarpProvider> provider) {
    std::lock_guard<std::mutex> lock(g_speedwarp_mutex);
    g_speedwarp_provider = std::move(provider);
}

RationalTime RetimeEngine::map_time_with_speed_points(const RationalTime& local_timeline_t,
                                                       const std::vector<SpeedPoint>& points,
                                                       const RationalTime& clip_source_in,
                                                       bool reversed,
                                                       const RationalTime& clip_source_out) {
    if (points.empty()) {
        return reversed ? (clip_source_out - local_timeline_t) : (clip_source_in + local_timeline_t);
    }

    std::vector<SpeedPoint> sorted = points;
    std::sort(sorted.begin(), sorted.end(), [](const SpeedPoint& a, const SpeedPoint& b) {
        return a.timeline_offset < b.timeline_offset;
    });

    const double target_sec = std::max(0.0, local_timeline_t.to_seconds());
    double integrated_sec = 0.0;

    // Before first speed point, assume first point's speed
    const double first_t = std::max(0.0, sorted.front().timeline_offset.to_seconds());
    if (target_sec <= first_t) {
        integrated_sec = target_sec * sorted.front().speed;
    } else {
        integrated_sec += first_t * sorted.front().speed;
        for (std::size_t i = 0; i < sorted.size(); ++i) {
            const double t0 = sorted[i].timeline_offset.to_seconds();
            const double s0 = sorted[i].speed;
            if (i + 1 >= sorted.size()) {
                integrated_sec += (target_sec - t0) * s0;
                break;
            }
            const double t1 = sorted[i + 1].timeline_offset.to_seconds();
            const double s1 = sorted[i + 1].speed;
            const double span = std::max(1e-9, t1 - t0);
            if (target_sec <= t1) {
                // Simpson/16-step numeric quadrature using Bezier ease curve across active interval
                const int steps = 16;
                const double local_dt = (target_sec - t0) / steps;
                for (int k = 0; k < steps; ++k) {
                    const double tm = ((t0 + (k + 0.5) * local_dt) - t0) / span;
                    const double ease = sorted[i].ease_out.evaluate(tm);
                    const double spd = s0 + (s1 - s0) * ease;
                    integrated_sec += spd * local_dt;
                }
                break;
            } else {
                const int steps = 16;
                const double full_dt = span / steps;
                for (int k = 0; k < steps; ++k) {
                    const double tm = (k + 0.5) / steps;
                    const double ease = sorted[i].ease_out.evaluate(tm);
                    integrated_sec += (s0 + (s1 - s0) * ease) * full_dt;
                }
            }
        }
    }

    const RationalTime delta = RationalTime::from_seconds_approx(integrated_sec, 100000);
    if (reversed) {
        const RationalTime out_t = clip_source_out - delta;
        return out_t < RationalTime::zero() ? RationalTime::zero() : out_t;
    }
    const RationalTime out_t = clip_source_in + delta;
    return out_t < RationalTime::zero() ? RationalTime::zero() : out_t;
}

RationalTime RetimeEngine::map_freeze_frame(const RationalTime& local_t,
                                            const RationalTime& freeze_at,
                                            const RationalTime& freeze_duration) {
    if (local_t <= freeze_at) return local_t;
    const RationalTime freeze_end = freeze_at + freeze_duration;
    if (local_t <= freeze_end) return freeze_at;
    return local_t - freeze_duration;
}

OpticalFlowField RetimeEngine::estimate_optical_flow(const VideoFrame& frame_a,
                                                     const VideoFrame& frame_b,
                                                     int32_t pyramid_levels,
                                                     int32_t window_radius) {
    OpticalFlowField result;
    if (!frame_a.valid() || !frame_b.valid() ||
        frame_a.width != frame_b.width || frame_a.height != frame_b.height) {
        return result;
    }

    const int32_t levels = std::clamp(pyramid_levels, 1, 5);
    const int32_t win = std::clamp(window_radius, 1, 5);

    std::vector<GrayLevel> pyr_a(static_cast<std::size_t>(levels));
    std::vector<GrayLevel> pyr_b(static_cast<std::size_t>(levels));
    pyr_a[0] = GrayLevel{frame_a.width, frame_a.height, extract_luma(frame_a)};
    pyr_b[0] = GrayLevel{frame_b.width, frame_b.height, extract_luma(frame_b)};
    for (int32_t l = 1; l < levels; ++l) {
        pyr_a[l] = downsample_2x(pyr_a[l - 1]);
        pyr_b[l] = downsample_2x(pyr_b[l - 1]);
    }

    std::vector<float> flow_u, flow_v;
    for (int32_t l = levels - 1; l >= 0; --l) {
        const GrayLevel& A = pyr_a[l];
        const GrayLevel& B = pyr_b[l];
        const std::size_t px_count = static_cast<std::size_t>(A.w) * static_cast<std::size_t>(A.h);
        std::vector<float> next_u(px_count, 0.0f);
        std::vector<float> next_v(px_count, 0.0f);

        if (l < levels - 1) {
            const GrayLevel& prev_lvl = pyr_a[l + 1];
            const float sx = static_cast<float>(prev_lvl.w) / static_cast<float>(A.w);
            const float sy = static_cast<float>(prev_lvl.h) / static_cast<float>(A.h);
            for (int32_t y = 0; y < A.h; ++y) {
                const int32_t py = std::clamp(static_cast<int32_t>(y * sy), 0, prev_lvl.h - 1);
                for (int32_t x = 0; x < A.w; ++x) {
                    const int32_t px = std::clamp(static_cast<int32_t>(x * sx), 0, prev_lvl.w - 1);
                    const std::size_t idx = static_cast<std::size_t>(y) * A.w + x;
                    const std::size_t pidx = static_cast<std::size_t>(py) * prev_lvl.w + px;
                    next_u[idx] = flow_u[pidx] * 2.0f;
                    next_v[idx] = flow_v[pidx] * 2.0f;
                }
            }
        }

        // Spatial gradients on A
        std::vector<float> ix(px_count, 0.0f);
        std::vector<float> iy(px_count, 0.0f);
        for (int32_t y = 0; y < A.h; ++y) {
            const int32_t ym = std::max(0, y - 1);
            const int32_t yp = std::min(A.h - 1, y + 1);
            for (int32_t x = 0; x < A.w; ++x) {
                const int32_t xm = std::max(0, x - 1);
                const int32_t xp = std::min(A.w - 1, x + 1);
                const std::size_t idx = static_cast<std::size_t>(y) * A.w + x;
                ix[idx] = 0.5f * (A.data[static_cast<std::size_t>(y) * A.w + xp] -
                                  A.data[static_cast<std::size_t>(y) * A.w + xm]);
                iy[idx] = 0.5f * (A.data[static_cast<std::size_t>(yp) * A.w + x] -
                                  A.data[static_cast<std::size_t>(ym) * A.w + x]);
            }
        }

        // 2 Gauss-Newton LK refinement iterations per level
        for (int iter = 0; iter < 2; ++iter) {
            std::vector<float> it(px_count, 0.0f);
            for (int32_t y = 0; y < A.h; ++y) {
                for (int32_t x = 0; x < A.w; ++x) {
                    const std::size_t idx = static_cast<std::size_t>(y) * A.w + x;
                    const float warped_b = sample_gray_bilinear(B, static_cast<float>(x) + next_u[idx],
                                                                static_cast<float>(y) + next_v[idx]);
                    it[idx] = warped_b - A.data[idx];
                }
            }

            std::vector<float> du(px_count, 0.0f);
            std::vector<float> dv(px_count, 0.0f);
            constexpr float kReg = 1e-3f;
            for (int32_t y = 0; y < A.h; ++y) {
                const int32_t y0 = std::max(0, y - win);
                const int32_t y1 = std::min(A.h - 1, y + win);
                for (int32_t x = 0; x < A.w; ++x) {
                    const int32_t x0 = std::max(0, x - win);
                    const int32_t x1 = std::min(A.w - 1, x + win);
                    float sxx = 0.0f, sxy = 0.0f, syy = 0.0f, sxt = 0.0f, syt = 0.0f;
                    for (int32_t wy = y0; wy <= y1; ++wy) {
                        const std::size_t row_off = static_cast<std::size_t>(wy) * A.w;
                        compute_row_tensor_products(ix.data() + row_off, iy.data() + row_off,
                                                    it.data() + row_off, x0, x1,
                                                    sxx, sxy, syy, sxt, syt);
                    }
                    const float a11 = sxx + kReg;
                    const float a22 = syy + kReg;
                    const float det = a11 * a22 - sxy * sxy;
                    if (det > 1e-6f) {
                        const float inv_det = 1.0f / det;
                        const float step_u = (-a22 * sxt + sxy * syt) * inv_det;
                        const float step_v = (sxy * sxt - a11 * syt) * inv_det;
                        const std::size_t idx = static_cast<std::size_t>(y) * A.w + x;
                        du[idx] = std::clamp(step_u, -4.0f, 4.0f);
                        dv[idx] = std::clamp(step_v, -4.0f, 4.0f);
                    }
                }
            }
            for (std::size_t i = 0; i < px_count; ++i) {
                next_u[i] += du[i];
                next_v[i] += dv[i];
            }
        }
        flow_u = std::move(next_u);
        flow_v = std::move(next_v);
    }

    result.width = frame_a.width;
    result.height = frame_a.height;
    result.flow_xy.resize(static_cast<std::size_t>(result.width) * static_cast<std::size_t>(result.height) * 2);
    for (int32_t y = 0; y < result.height; ++y) {
        for (int32_t x = 0; x < result.width; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) * result.width + x;
            result.flow_xy[idx * 2 + 0] = flow_u[idx];
            result.flow_xy[idx * 2 + 1] = flow_v[idx];
        }
    }
    return result;
}

bool RetimeEngine::interpolate_frame(const VideoFrame& frame_a,
                                     const VideoFrame& frame_b,
                                     float alpha,
                                     RetimeInterpolationMode mode,
                                     VideoFrame& out_frame,
                                     FrameMemoryPool* pool) {
    if (!frame_a.valid() || !frame_b.valid() ||
        frame_a.width != frame_b.width || frame_a.height != frame_b.height) {
        return false;
    }
    alpha = std::clamp(alpha, 0.0f, 1.0f);

    if (mode == RetimeInterpolationMode::SpeedWarp) {
        std::shared_ptr<ISpeedWarpProvider> sw;
        {
            std::lock_guard<std::mutex> lock(g_speedwarp_mutex);
            sw = g_speedwarp_provider;
        }
        if (sw && sw->available()) {
            if (sw->interpolate_frame(frame_a, frame_b, alpha, out_frame, pool)) {
                return true;
            }
        }
        mode = RetimeInterpolationMode::OpticalFlow;
    }

    out_frame = VideoFrame::allocate(frame_a.width, frame_a.height, pool);
    if (!out_frame.valid()) return false;

    const int32_t w = frame_a.width;
    const int32_t h = frame_a.height;
    float* dst = out_frame.data();

    if (mode == RetimeInterpolationMode::Nearest) {
        const VideoFrame& chosen = (alpha < 0.5f) ? frame_a : frame_b;
        for (int32_t y = 0; y < h; ++y) {
            std::copy_n(chosen.data() + y * chosen.stride_floats, w * 4, dst + y * out_frame.stride_floats);
        }
        return true;
    }

    if (mode == RetimeInterpolationMode::FrameBlend) {
        const float inv = 1.0f - alpha;
        for (int32_t y = 0; y < h; ++y) {
            const float* ra = frame_a.data() + y * frame_a.stride_floats;
            const float* rb = frame_b.data() + y * frame_b.stride_floats;
            float* rd = dst + y * out_frame.stride_floats;
            for (int32_t i = 0; i < w * 4; ++i) {
                rd[i] = ra[i] * inv + rb[i] * alpha;
            }
        }
        return true;
    }

    // OpticalFlow bidirectional motion-compensated interpolation
    const OpticalFlowField flow = estimate_optical_flow(frame_a, frame_b, 3, 2);
    if (!flow.valid()) return false;

    const float inv = 1.0f - alpha;
    for (int32_t y = 0; y < h; ++y) {
        float* rd = dst + y * out_frame.stride_floats;
        for (int32_t x = 0; x < w; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) * w + x;
            const float u = flow.flow_xy[idx * 2 + 0];
            const float v = flow.flow_xy[idx * 2 + 1];
            float pa[4]{}, pb[4]{};
            // At intermediate time alpha, pixel at (x,y) came from A at (x - alpha*u, y - alpha*v)
            // and goes to B at (x + (1-alpha)*u, y + (1-alpha)*v).
            sample_rgba_bilinear(frame_a, static_cast<float>(x) - alpha * u, static_cast<float>(y) - alpha * v, pa);
            sample_rgba_bilinear(frame_b, static_cast<float>(x) + inv * u, static_cast<float>(y) + inv * v, pb);
            for (int c = 0; c < 4; ++c) {
                rd[x * 4 + c] = pa[c] * inv + pb[c] * alpha;
            }
        }
    }
    return true;
}

bool RetimeEngine::smooth_cut_morph(const VideoFrame& outgoing_frame,
                                    const VideoFrame& incoming_frame,
                                    float progress,
                                    VideoFrame& out_frame,
                                    FrameMemoryPool* pool) {
    return interpolate_frame(outgoing_frame, incoming_frame, progress,
                             RetimeInterpolationMode::OpticalFlow, out_frame, pool);
}

} // namespace owncut::timeline

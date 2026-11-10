#include "timeline/vision_engine.hpp"
#include "timeline/retime_engine.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numeric>

namespace owncut::timeline {
namespace {

float frame_luma_mad(const VideoFrame& a, const VideoFrame& b) {
    if (!a.valid() || !b.valid() || a.width != b.width || a.height != b.height) return 1.0f;
    double sum = 0.0;
    const std::size_t n = static_cast<std::size_t>(a.width) * static_cast<std::size_t>(a.height);
    for (int32_t y = 0; y < a.height; ++y) {
        const float* ra = a.data() + y * a.stride_floats;
        const float* rb = b.data() + y * b.stride_floats;
        for (int32_t x = 0; x < a.width; ++x) {
            const float la = 0.2126f * ra[x * 4 + 0] + 0.7152f * ra[x * 4 + 1] + 0.0722f * ra[x * 4 + 2];
            const float lb = 0.2126f * rb[x * 4 + 0] + 0.7152f * rb[x * 4 + 1] + 0.0722f * rb[x * 4 + 2];
            sum += std::abs(la - lb);
        }
    }
    return static_cast<float>(sum / static_cast<double>(std::max<std::size_t>(1, n)));
}

float luma_histogram_diff(const VideoFrame& a, const VideoFrame& b) {
    if (!a.valid() || !b.valid() || a.width != b.width || a.height != b.height) return 1.0f;
    constexpr int kBins = 16;
    float ha[kBins]{}, hb[kBins]{};
    const std::size_t n = static_cast<std::size_t>(a.width) * static_cast<std::size_t>(a.height);
    for (int32_t y = 0; y < a.height; ++y) {
        const float* ra = a.data() + y * a.stride_floats;
        const float* rb = b.data() + y * b.stride_floats;
        for (int32_t x = 0; x < a.width; ++x) {
            const float la = std::clamp(0.2126f * ra[x * 4] + 0.7152f * ra[x * 4 + 1] + 0.0722f * ra[x * 4 + 2], 0.0f, 0.9999f);
            const float lb = std::clamp(0.2126f * rb[x * 4] + 0.7152f * rb[x * 4 + 1] + 0.0722f * rb[x * 4 + 2], 0.0f, 0.9999f);
            ha[static_cast<int>(la * kBins)] += 1.0f;
            hb[static_cast<int>(lb * kBins)] += 1.0f;
        }
    }
    double l1 = 0.0;
    const float inv_n = 1.0f / static_cast<float>(std::max<std::size_t>(1, n));
    for (int i = 0; i < kBins; ++i) {
        l1 += std::abs(ha[i] * inv_n - hb[i] * inv_n);
    }
    return static_cast<float>(0.5 * l1);
}

std::vector<float> kalman_1d_smooth(const std::vector<float>& z, float smoothness) {
    std::vector<float> out(z.size(), 0.0f);
    if (z.empty()) return out;
    const float q = std::max(1e-4f, (1.0f - smoothness) * 0.25f);
    const float r = std::max(0.05f, smoothness * 4.0f);
    float x = z[0];
    float p = 1.0f;
    out[0] = x;
    for (std::size_t i = 1; i < z.size(); ++i) {
        p += q;
        const float k = p / (p + r);
        x = x + k * (z[i] - x);
        p = (1.0f - k) * p;
        out[i] = x;
    }
    return out;
}

std::vector<float> gaussian_1d_smooth(const std::vector<float>& z, float smoothness) {
    std::vector<float> out(z.size(), 0.0f);
    if (z.empty()) return out;
    const int radius = std::max(1, static_cast<int>(std::round(smoothness * 8.0f)));
    const float sigma = std::max(0.8f, static_cast<float>(radius) * 0.5f);
    for (std::size_t i = 0; i < z.size(); ++i) {
        double sum = 0.0;
        double wsum = 0.0;
        for (int k = -radius; k <= radius; ++k) {
            const int64_t idx = std::clamp<int64_t>(static_cast<int64_t>(i) + k, 0, static_cast<int64_t>(z.size()) - 1);
            const double w = std::exp(-(k * k) / (2.0 * sigma * sigma));
            sum += z[static_cast<std::size_t>(idx)] * w;
            wsum += w;
        }
        out[i] = static_cast<float>(sum / wsum);
    }
    return out;
}

void sample_bilinear_rgba(const VideoFrame& src, float x, float y, float out[4]) noexcept {
    x = std::clamp(x, 0.0f, static_cast<float>(src.width - 1));
    y = std::clamp(y, 0.0f, static_cast<float>(src.height - 1));
    const int32_t x0 = static_cast<int32_t>(std::floor(x));
    const int32_t y0 = static_cast<int32_t>(std::floor(y));
    const int32_t x1 = std::min(src.width - 1, x0 + 1);
    const int32_t y1 = std::min(src.height - 1, y0 + 1);
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);
    const float w00 = (1.0f - fx) * (1.0f - fy);
    const float w10 = fx * (1.0f - fy);
    const float w01 = (1.0f - fx) * fy;
    const float w11 = fx * fy;
    const float* p00 = src.data() + y0 * src.stride_floats + x0 * 4;
    const float* p10 = src.data() + y0 * src.stride_floats + x1 * 4;
    const float* p01 = src.data() + y1 * src.stride_floats + x0 * 4;
    const float* p11 = src.data() + y1 * src.stride_floats + x1 * 4;
    for (int c = 0; c < 4; ++c) out[c] = p00[c] * w00 + p10[c] * w10 + p01[c] * w01 + p11[c] * w11;
}

std::string to_lower_ascii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

StabilizationAnalysis Stabilizer::analyze_sequence(const std::vector<VideoFrame>& frames,
                                                   const StabilizationSettings& settings) {
    StabilizationAnalysis analysis;
    if (frames.empty()) return analysis;

    const std::size_t n = frames.size();
    std::vector<float> cum_x(n, 0.0f);
    std::vector<float> cum_y(n, 0.0f);
    std::vector<float> cum_theta(n, 0.0f);
    std::vector<float> raw_dx(n, 0.0f);
    std::vector<float> raw_dy(n, 0.0f);
    std::vector<float> raw_dtheta(n, 0.0f);

    for (std::size_t i = 1; i < n; ++i) {
        const OpticalFlowField flow = RetimeEngine::estimate_optical_flow(frames[i - 1], frames[i], 2, 2);
        if (flow.valid()) {
            double sx = 0.0, sy = 0.0, srot = 0.0, srad = 0.0;
            const int32_t w = flow.width;
            const int32_t h = flow.height;
            const double cx = w * 0.5;
            const double cy = h * 0.5;
            const std::size_t count = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
            for (int32_t y = 0; y < h; ++y) {
                for (int32_t x = 0; x < w; ++x) {
                    const std::size_t idx = static_cast<std::size_t>(y) * w + x;
                    const float u = flow.flow_xy[idx * 2 + 0];
                    const float v = flow.flow_xy[idx * 2 + 1];
                    sx += u;
                    sy += v;
                    if (settings.mode != StabilizationMode::Translation) {
                        const double rx = x - cx;
                        const double ry = y - cy;
                        srot += rx * v - ry * u;
                        srad += rx * rx + ry * ry;
                    }
                }
            }
            raw_dx[i] = static_cast<float>(sx / static_cast<double>(count));
            raw_dy[i] = static_cast<float>(sy / static_cast<double>(count));
            raw_dtheta[i] = (srad > 1.0 && settings.mode != StabilizationMode::Translation)
                                ? static_cast<float>(srot / srad)
                                : 0.0f;
        }
        cum_x[i] = cum_x[i - 1] + raw_dx[i];
        cum_y[i] = cum_y[i - 1] + raw_dy[i];
        cum_theta[i] = cum_theta[i - 1] + raw_dtheta[i];
    }

    std::vector<float> smooth_x(n, 0.0f), smooth_y(n, 0.0f), smooth_theta(n, 0.0f);
    if (!settings.camera_lock) {
        if (settings.use_kalman_filter) {
            smooth_x = kalman_1d_smooth(cum_x, settings.smoothness);
            smooth_y = kalman_1d_smooth(cum_y, settings.smoothness);
            smooth_theta = kalman_1d_smooth(cum_theta, settings.smoothness);
        } else {
            smooth_x = gaussian_1d_smooth(cum_x, settings.smoothness);
            smooth_y = gaussian_1d_smooth(cum_y, settings.smoothness);
            smooth_theta = gaussian_1d_smooth(cum_theta, settings.smoothness);
        }
    }

    const float w = static_cast<float>(std::max(1, frames[0].width));
    const float h = static_cast<float>(std::max(1, frames[0].height));
    float max_req_zoom = 1.0f;

    analysis.samples.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        FrameMotionSample s;
        s.frame_index = static_cast<int64_t>(i);
        s.raw_dx = raw_dx[i];
        s.raw_dy = raw_dy[i];
        s.raw_dtheta = raw_dtheta[i];
        // Note: optical flow measures how pixels moved from frame i-1 to i;
        // cum_x[i] is pixel drift; correction shifts pixels back by (smooth_x[i] - cum_x[i]).
        s.corr_dx = smooth_x[i] - cum_x[i];
        s.corr_dy = smooth_y[i] - cum_y[i];
        s.corr_dtheta = smooth_theta[i] - cum_theta[i];

        const float req = 1.0f + 2.0f * (std::abs(s.corr_dx) / w + std::abs(s.corr_dy) / h) +
                          std::abs(std::sin(s.corr_dtheta)) * 0.5f;
        s.required_zoom = std::clamp(req, 1.0f, settings.max_zoom);
        max_req_zoom = std::max(max_req_zoom, s.required_zoom);
        analysis.samples[i] = s;
    }
    analysis.adaptive_zoom = settings.zoom_adaptive ? max_req_zoom : 1.0f;
    return analysis;
}

bool Stabilizer::stabilize_frame(const VideoFrame& src, const FrameMotionSample& sample,
                                 float zoom_factor, VideoFrame& dst, FrameMemoryPool* pool) {
    if (!src.valid()) return false;
    dst = VideoFrame::allocate(src.width, src.height, pool);
    if (!dst.valid()) return false;
    dst.pts = src.pts;

    const float cx = src.width * 0.5f;
    const float cy = src.height * 0.5f;
    const float cs = std::cos(-sample.corr_dtheta);
    const float sn = std::sin(-sample.corr_dtheta);
    const float inv_zoom = 1.0f / std::max(0.5f, zoom_factor);

    for (int32_t y = 0; y < dst.height; ++y) {
        float* row = dst.data() + y * dst.stride_floats;
        for (int32_t x = 0; x < dst.width; ++x) {
            const float dx = (static_cast<float>(x) - cx) * inv_zoom;
            const float dy = (static_cast<float>(y) - cy) * inv_zoom;
            // Source coordinate that shifts stabilized frame by +corr_dx, +corr_dy
            const float sx = dx * cs - dy * sn + cx - sample.corr_dx;
            const float sy = dx * sn + dy * cs + cy - sample.corr_dy;
            float rgba[4]{};
            sample_bilinear_rgba(src, sx, sy, rgba);
            for (int c = 0; c < 4; ++c) row[x * 4 + c] = rgba[c];
        }
    }
    return true;
}

std::vector<ReframeCropBox> SmartReframe::compute_reframe_trajectory(const std::vector<VideoFrame>& frames,
                                                                     int32_t target_aspect_num,
                                                                     int32_t target_aspect_den,
                                                                     float smoothing) {
    std::vector<ReframeCropBox> boxes;
    if (frames.empty() || target_aspect_num <= 0 || target_aspect_den <= 0) return boxes;

    const double target_ar = static_cast<double>(target_aspect_num) / static_cast<double>(target_aspect_den);
    std::vector<float> raw_cx(frames.size(), 0.5f);
    std::vector<float> raw_cy(frames.size(), 0.5f);

    for (std::size_t i = 0; i < frames.size(); ++i) {
        const VideoFrame& f = frames[i];
        if (!f.valid()) continue;
        // Saliency centroid combining edge gradient energy + bright foreground subject contrast
        double wsum = 0.0, sx = 0.0, sy = 0.0;
        for (int32_t y = 1; y + 1 < f.height; ++y) {
            const float* r = f.data() + y * f.stride_floats;
            const float* r_up = f.data() + (y - 1) * f.stride_floats;
            const float* r_dn = f.data() + (y + 1) * f.stride_floats;
            for (int32_t x = 1; x + 1 < f.width; ++x) {
                const float gx = std::abs(r[(x + 1) * 4] - r[(x - 1) * 4]);
                const float gy = std::abs(r_dn[x * 4] - r_up[x * 4]);
                const float luma = 0.2126f * r[x * 4] + 0.7152f * r[x * 4 + 1] + 0.0722f * r[x * 4 + 2];
                const double weight = static_cast<double>(gx + gy) + 0.25 * static_cast<double>(luma);
                sx += weight * static_cast<double>(x) / static_cast<double>(f.width);
                sy += weight * static_cast<double>(y) / static_cast<double>(f.height);
                wsum += weight;
            }
        }
        if (wsum > 1e-6) {
            raw_cx[i] = static_cast<float>(sx / wsum);
            raw_cy[i] = static_cast<float>(sy / wsum);
        }
    }

    const auto sm_cx = gaussian_1d_smooth(raw_cx, smoothing);
    const auto sm_cy = gaussian_1d_smooth(raw_cy, smoothing);

    boxes.resize(frames.size());
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const VideoFrame& f = frames[i];
        const double src_ar = static_cast<double>(std::max(1, f.width)) / static_cast<double>(std::max(1, f.height));
        double cw = 1.0, ch = 1.0;
        if (target_ar < src_ar) {
            cw = target_ar / src_ar;
            ch = 1.0;
        } else {
            cw = 1.0;
            ch = src_ar / target_ar;
        }
        const float half_w = static_cast<float>(cw * 0.5);
        const float half_h = static_cast<float>(ch * 0.5);
        ReframeCropBox box;
        box.frame_index = static_cast<int64_t>(i);
        box.crop_width_norm = static_cast<float>(cw);
        box.crop_height_norm = static_cast<float>(ch);
        box.center_x_norm = std::clamp(sm_cx[i], half_w, 1.0f - half_w);
        box.center_y_norm = std::clamp(sm_cy[i], half_h, 1.0f - half_h);
        boxes[i] = box;
    }
    return boxes;
}

bool SmartReframe::apply_reframe(const VideoFrame& src, const ReframeCropBox& box,
                                 int32_t out_width, int32_t out_height,
                                 VideoFrame& dst, FrameMemoryPool* pool) {
    if (!src.valid() || out_width <= 0 || out_height <= 0) return false;
    dst = VideoFrame::allocate(out_width, out_height, pool);
    if (!dst.valid()) return false;
    dst.pts = src.pts;

    const float x0 = (box.center_x_norm - box.crop_width_norm * 0.5f) * src.width;
    const float y0 = (box.center_y_norm - box.crop_height_norm * 0.5f) * src.height;
    const float cw = box.crop_width_norm * src.width;
    const float ch = box.crop_height_norm * src.height;

    for (int32_t y = 0; y < out_height; ++y) {
        float* row = dst.data() + y * dst.stride_floats;
        const float sy = y0 + (static_cast<float>(y) + 0.5f) / static_cast<float>(out_height) * ch;
        for (int32_t x = 0; x < out_width; ++x) {
            const float sx = x0 + (static_cast<float>(x) + 0.5f) / static_cast<float>(out_width) * cw;
            float rgba[4]{};
            sample_bilinear_rgba(src, sx, sy, rgba);
            for (int c = 0; c < 4; ++c) row[x * 4 + c] = rgba[c];
        }
    }
    return true;
}

std::vector<DetectedCut> SceneAndCutDetector::detect_scene_cuts(const std::vector<VideoFrame>& frames,
                                                                FrameRate fps,
                                                                float threshold) {
    std::vector<DetectedCut> cuts;
    for (std::size_t i = 1; i < frames.size(); ++i) {
        const float hist = luma_histogram_diff(frames[i - 1], frames[i]);
        const float mad = frame_luma_mad(frames[i - 1], frames[i]);
        const float score = 0.6f * hist + 0.4f * mad;
        if (score >= threshold) {
            cuts.push_back(DetectedCut{
                static_cast<int64_t>(i),
                RationalTime::from_frames(static_cast<int64_t>(i), fps),
                score
            });
        }
    }
    return cuts;
}

std::vector<CandidateTrimSegment> SceneAndCutDetector::detect_boring_and_jump_cuts(
    const std::vector<VideoFrame>& frames,
    FrameRate fps,
    float low_motion_thresh,
    int64_t min_boring_frames) {
    std::vector<CandidateTrimSegment> out;
    if (frames.size() < 2) return out;

    int64_t run_start = -1;
    for (std::size_t i = 1; i < frames.size(); ++i) {
        const float mad = frame_luma_mad(frames[i - 1], frames[i]);
        if (mad < low_motion_thresh) {
            if (run_start < 0) run_start = static_cast<int64_t>(i - 1);
        } else {
            if (run_start >= 0) {
                const int64_t run_len = static_cast<int64_t>(i) - run_start;
                if (run_len >= min_boring_frames) {
                    const RationalTime st = RationalTime::from_frames(run_start, fps);
                    const RationalTime dur = RationalTime::from_frames(run_len, fps);
                    out.push_back(CandidateTrimSegment{TimeRange{st, dur},
                                                       CandidateTrimSegment::Reason::StaticBoring, 1.0f});
                }
                run_start = -1;
            }
            if (mad > 0.25f) {
                const RationalTime st = RationalTime::from_frames(static_cast<int64_t>(i), fps);
                const RationalTime dur = RationalTime::from_frames(1, fps);
                out.push_back(CandidateTrimSegment{TimeRange{st, dur},
                                                   CandidateTrimSegment::Reason::JumpCut, mad});
            }
        }
    }
    if (run_start >= 0) {
        const int64_t run_len = static_cast<int64_t>(frames.size()) - run_start;
        if (run_len >= min_boring_frames) {
            const RationalTime st = RationalTime::from_frames(run_start, fps);
            const RationalTime dur = RationalTime::from_frames(run_len, fps);
            out.push_back(CandidateTrimSegment{TimeRange{st, dur},
                                               CandidateTrimSegment::Reason::StaticBoring, 1.0f});
        }
    }
    return out;
}

bool SceneAndCutDetector::remove_candidate_segments(Timeline& tl, CommandStack& stack,
                                                    const std::vector<CandidateTrimSegment>& segments) {
    if (segments.empty()) return false;
    std::vector<CandidateTrimSegment> sorted = segments;
    std::sort(sorted.begin(), sorted.end(), [](const CandidateTrimSegment& a, const CandidateTrimSegment& b) {
        return a.range.start > b.range.start; // Back-to-front so earlier ranges don't shift
    });
    bool ok = false;
    for (const auto& seg : sorted) {
        if (EditOperations::extract_range(tl, stack, seg.range)) ok = true;
    }
    return ok;
}

bool TextBasedEditor::mark_word_deleted(uint64_t word_id, bool deleted) {
    for (auto& w : words_) {
        if (w.id == word_id) {
            w.deleted = deleted;
            return true;
        }
    }
    return false;
}

bool TextBasedEditor::reorder_word_block(std::size_t from_idx, std::size_t count, std::size_t insert_before_idx) {
    if (from_idx + count > words_.size() || insert_before_idx > words_.size()) return false;
    if (insert_before_idx >= from_idx && insert_before_idx <= from_idx + count) return false;
    std::vector<TranscriptWord> block(words_.begin() + static_cast<std::ptrdiff_t>(from_idx),
                                      words_.begin() + static_cast<std::ptrdiff_t>(from_idx + count));
    words_.erase(words_.begin() + static_cast<std::ptrdiff_t>(from_idx),
                 words_.begin() + static_cast<std::ptrdiff_t>(from_idx + count));
    if (insert_before_idx > from_idx) insert_before_idx -= count;
    words_.insert(words_.begin() + static_cast<std::ptrdiff_t>(insert_before_idx), block.begin(), block.end());
    return true;
}

bool TextBasedEditor::apply_edits_to_timeline(Timeline& tl, CommandStack& stack, uint64_t media_id) const {
    // Coalesce contiguous surviving word segments and assemble cleanly
    struct Span { RationalTime start; RationalTime end; uint64_t media; };
    std::vector<Span> spans;
    for (const auto& w : words_) {
        if (w.deleted || w.source_end <= w.source_start) continue;
        const uint64_t mid = w.media_id ? w.media_id : media_id;
        if (!spans.empty() && spans.back().media == mid && spans.back().end == w.source_start) {
            spans.back().end = w.source_end;
        } else {
            spans.push_back(Span{w.source_start, w.source_end, mid});
        }
    }
    if (spans.empty()) return false;

    auto cmd = std::make_unique<SnapshotTimelineCommand>(
        "TextBasedEditRebuild",
        [spans](Timeline& target) -> bool {
            Track* vt = target.primary_track(TrackType::Video);
            Track* at = target.primary_track(TrackType::Audio);
            if (!vt) return false;
            vt->clips.clear();
            if (at) at->clips.clear();

            RationalTime cursor = RationalTime::zero();
            for (const auto& sp : spans) {
                const RationalTime dur = sp.end - sp.start;
                const uint64_t lg = at ? target.allocate_link_group_id() : 0;
                ClipItem vc;
                vc.id = target.allocate_clip_id();
                vc.media_id = sp.media;
                vc.track_id = vt->id;
                vc.link_group_id = lg;
                vc.media_role = TrackType::Video;
                vc.name = "Dialogue Cut";
                vc.timeline_start = cursor;
                vc.timeline_duration = dur;
                vc.source_in = sp.start;
                vc.source_out = sp.end;
                vc.source_total_duration = sp.end;
                vt->clips.push_back(std::move(vc));

                if (at) {
                    ClipItem ac;
                    ac.id = target.allocate_clip_id();
                    ac.media_id = sp.media;
                    ac.track_id = at->id;
                    ac.link_group_id = lg;
                    ac.media_role = TrackType::Audio;
                    ac.name = "Dialogue Audio";
                    ac.timeline_start = cursor;
                    ac.timeline_duration = dur;
                    ac.source_in = sp.start;
                    ac.source_out = sp.end;
                    ac.source_total_duration = sp.end;
                    at->clips.push_back(std::move(ac));
                }
                cursor += dur;
            }
            return true;
        });
    return stack.execute(std::move(cmd), tl);
}

bool IntelliScriptMatcher::assemble_timeline_from_script(const std::vector<std::string>& script_lines,
                                                         const std::vector<ScriptSegment>& transcript_pool,
                                                         Timeline& out_timeline,
                                                         CommandStack& stack) {
    if (script_lines.empty() || transcript_pool.empty()) return false;
    std::vector<ScriptSegment> chosen;
    for (const auto& line : script_lines) {
        const std::string q = to_lower_ascii(line);
        const ScriptSegment* best = nullptr;
        int best_score = 0;
        for (const auto& cand : transcript_pool) {
            const std::string t = to_lower_ascii(cand.text);
            int score = 0;
            if (t.find(q) != std::string::npos || q.find(t) != std::string::npos) {
                score = 100;
            } else {
                // Token overlap score
                std::size_t pos = 0;
                while (pos < q.size()) {
                    while (pos < q.size() && std::isspace(static_cast<unsigned char>(q[pos]))) ++pos;
                    std::size_t end = pos;
                    while (end < q.size() && !std::isspace(static_cast<unsigned char>(q[end]))) ++end;
                    if (end > pos) {
                        const std::string tok = q.substr(pos, end - pos);
                        if (t.find(tok) != std::string::npos) ++score;
                    }
                    pos = end;
                }
            }
            if (score > best_score) {
                best_score = score;
                best = &cand;
            }
        }
        if (best != nullptr) chosen.push_back(*best);
    }
    if (chosen.empty()) return false;

    bool ok = false;
    for (const auto& seg : chosen) {
        SourceInsertSpec spec;
        spec.media_id = seg.media_id;
        spec.name = seg.text;
        spec.source_in = seg.source_start;
        spec.source_out = seg.source_end;
        spec.media_duration = seg.source_end;
        if (EditOperations::perform_edit(out_timeline, stack, EditMode::AppendAtEnd, spec)) {
            ok = true;
        }
    }
    return ok;
}

} // namespace owncut::timeline

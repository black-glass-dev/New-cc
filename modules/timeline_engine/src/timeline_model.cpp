#include "timeline/timeline_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>

namespace owncut::timeline {
namespace {
float clamp01(float v) noexcept {
    return std::clamp(v, 0.0f, 1.0f);
}

void rgb_to_hsl(float r, float g, float b, float& h, float& s, float& l) noexcept {
    const float mx = std::max({r, g, b});
    const float mn = std::min({r, g, b});
    l = 0.5f * (mx + mn);
    const float d = mx - mn;
    if (d < 1e-6f) {
        h = 0.0f;
        s = 0.0f;
        return;
    }
    s = (l > 0.5f) ? d / std::max(1e-6f, 2.0f - mx - mn) : d / std::max(1e-6f, mx + mn);
    if (mx == r) {
        h = std::fmod((g - b) / d + (g < b ? 6.0f : 0.0f), 6.0f) / 6.0f;
    } else if (mx == g) {
        h = ((b - r) / d + 2.0f) / 6.0f;
    } else {
        h = ((r - g) / d + 4.0f) / 6.0f;
    }
}

float hue2rgb(float p, float q, float t) noexcept {
    if (t < 0.0f) t += 1.0f;
    if (t > 1.0f) t -= 1.0f;
    if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
    if (t < 0.5f) return q;
    if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    return p;
}

void hsl_to_rgb(float h, float s, float l, float& r, float& g, float& b) noexcept {
    if (s <= 1e-6f) {
        r = g = b = l;
        return;
    }
    const float q = (l < 0.5f) ? l * (1.0f + s) : l + s - l * s;
    const float p = 2.0f * l - q;
    r = hue2rgb(p, q, h + 1.0f / 3.0f);
    g = hue2rgb(p, q, h);
    b = hue2rgb(p, q, h - 1.0f / 3.0f);
}

float blend_channel(float d, float s, BlendMode mode) noexcept {
    d = clamp01(d);
    s = clamp01(s);
    switch (mode) {
        case BlendMode::Normal: return s;
        case BlendMode::Add: return std::min(1.0f, d + s);
        case BlendMode::Multiply: return d * s;
        case BlendMode::Screen: return 1.0f - (1.0f - d) * (1.0f - s);
        case BlendMode::Overlay: return (d < 0.5f) ? (2.0f * d * s) : (1.0f - 2.0f * (1.0f - d) * (1.0f - s));
        case BlendMode::Darken: return std::min(d, s);
        case BlendMode::Lighten: return std::max(d, s);
        case BlendMode::ColorDodge: return (s >= 0.9999f) ? 1.0f : std::min(1.0f, d / (1.0f - s));
        case BlendMode::ColorBurn: return (s <= 0.0001f) ? 0.0f : std::max(0.0f, 1.0f - (1.0f - d) / s);
        case BlendMode::HardLight: return (s < 0.5f) ? (2.0f * d * s) : (1.0f - 2.0f * (1.0f - d) * (1.0f - s));
        case BlendMode::SoftLight:
            return (s < 0.5f)
                ? (d - (1.0f - 2.0f * s) * d * (1.0f - d))
                : (d + (2.0f * s - 1.0f) * (((d <= 0.25f) ? ((16.0f * d - 12.0f) * d + 4.0f) * d : std::sqrt(d)) - d));
        case BlendMode::Difference: return std::abs(d - s);
        case BlendMode::Exclusion: return d + s - 2.0f * d * s;
        case BlendMode::Subtract: return std::max(0.0f, d - s);
        default: return s;
    }
}

void render_generator(const GeneratorSpec& spec, VideoFrame& dst, const RationalTime& t) {
    float* p = dst.data();
    const int32_t w = dst.width;
    const int32_t h = dst.height;
    const int32_t stride = dst.stride_floats;
    for (int32_t y = 0; y < h; ++y) {
        float* row = p + y * stride;
        const float fy = static_cast<float>(y) / static_cast<float>(std::max(1, h - 1));
        for (int32_t x = 0; x < w; ++x) {
            const float fx = static_cast<float>(x) / static_cast<float>(std::max(1, w - 1));
            float rgba[4]{spec.color_rgba[0], spec.color_rgba[1], spec.color_rgba[2], spec.color_rgba[3]};
            if (spec.kind == "gradient") {
                for (int c = 0; c < 4; ++c) {
                    rgba[c] = spec.color_rgba[c] * (1.0f - fx) + spec.secondary_rgba[c] * fx;
                }
            } else if (spec.kind == "bars") {
                const int bar = (x * 6) / std::max(1, w);
                rgba[0] = (bar & 1) ? 0.9f : 0.1f;
                rgba[1] = (bar & 2) ? 0.9f : 0.1f;
                rgba[2] = (bar & 4) ? 0.9f : 0.1f;
                rgba[3] = 1.0f;
            } else if (spec.kind == "grid") {
                const bool line = (x % 16 == 0) || (y % 16 == 0);
                rgba[0] = line ? 0.9f : 0.05f + 0.05f * fy;
                rgba[1] = line ? 0.9f : 0.05f + 0.05f * static_cast<float>(std::fmod(t.to_seconds(), 1.0));
                rgba[2] = line ? 0.9f : 0.1f;
                rgba[3] = 1.0f;
            }
            row[x * 4 + 0] = rgba[0];
            row[x * 4 + 1] = rgba[1];
            row[x * 4 + 2] = rgba[2];
            row[x * 4 + 3] = rgba[3];
        }
    }
}
} // namespace

RationalTime ClipItem::map_timeline_to_source(const RationalTime& timeline_t) const {
    const RationalTime local = std::clamp(timeline_t - timeline_start, RationalTime::zero(), timeline_duration);
    const int64_t speed_scaled = std::llround(std::abs(speed) * 1000000.0);
    const RationalTime delta = scale_rational(local, speed_scaled, 1000000);
    if (!reversed) {
        return source_in + delta;
    }
    const RationalTime anchor = (source_out > source_in) ? source_out : (source_in + timeline_duration);
    const RationalTime mapped = anchor - delta;
    return mapped < RationalTime::zero() ? RationalTime::zero() : mapped;
}

void Track::sort_clips() {
    std::stable_sort(clips.begin(), clips.end(),
                     [](const ClipItem& a, const ClipItem& b) {
                         if (a.timeline_start != b.timeline_start) return a.timeline_start < b.timeline_start;
                         return a.id < b.id;
                     });
}

ClipItem* Track::find_clip(uint64_t clip_id) {
    for (auto& c : clips) {
        if (c.id == clip_id) return &c;
    }
    return nullptr;
}

const ClipItem* Track::find_clip(uint64_t clip_id) const {
    for (const auto& c : clips) {
        if (c.id == clip_id) return &c;
    }
    return nullptr;
}

const ClipItem* Track::clip_at(const RationalTime& t) const {
    for (const auto& c : clips) {
        if (c.contains_time(t)) return &c;
    }
    return nullptr;
}

ClipItem* Track::clip_at(const RationalTime& t) {
    for (auto& c : clips) {
        if (c.contains_time(t)) return &c;
    }
    return nullptr;
}

std::vector<TimeRange> Track::find_gaps(const RationalTime& up_to) const {
    std::vector<ClipItem> sorted = clips;
    std::sort(sorted.begin(), sorted.end(), [](const ClipItem& a, const ClipItem& b) {
        return a.timeline_start < b.timeline_start;
    });
    std::vector<TimeRange> gaps;
    RationalTime cursor = RationalTime::zero();
    for (const auto& c : sorted) {
        if (c.timeline_start > cursor) {
            gaps.push_back(TimeRange{cursor, c.timeline_start - cursor});
        }
        if (c.timeline_end() > cursor) {
            cursor = c.timeline_end();
        }
    }
    if (up_to > cursor) {
        gaps.push_back(TimeRange{cursor, up_to - cursor});
    }
    return gaps;
}

RationalTime Track::duration() const {
    RationalTime end{};
    for (const auto& c : clips) {
        end = std::max(end, c.timeline_end());
    }
    return end;
}

Timeline::Timeline(uint64_t id, std::string name, FrameRate fps, int32_t width, int32_t height)
    : id_(id), name_(std::move(name)), fps_(fps), width_(width), height_(height) {
    add_track(TrackType::Video, "V1");
    add_track(TrackType::Audio, "A1");
}

Track& Timeline::add_track(TrackType type, std::string name) {
    int32_t count_same = 0;
    for (const auto& t : tracks_) {
        if (t.type == type) ++count_same;
    }
    if (name.empty()) {
        const char* prefix = (type == TrackType::Video) ? "V"
                           : (type == TrackType::Audio) ? "A"
                           : (type == TrackType::Subtitle) ? "S"
                           : "ADJ";
        name = std::string(prefix) + std::to_string(count_same + 1);
    }
    Track tr;
    tr.id = next_track_id_++;
    tr.type = type;
    tr.index = count_same;
    tr.name = std::move(name);
    tracks_.push_back(std::move(tr));
    return tracks_.back();
}

bool Timeline::remove_track(uint64_t track_id) {
    const auto it = std::find_if(tracks_.begin(), tracks_.end(),
                                 [track_id](const Track& t) { return t.id == track_id; });
    if (it == tracks_.end()) return false;
    tracks_.erase(it);
    return true;
}

Track* Timeline::find_track(uint64_t track_id) {
    for (auto& t : tracks_) {
        if (t.id == track_id) return &t;
    }
    return nullptr;
}

const Track* Timeline::find_track(uint64_t track_id) const {
    for (const auto& t : tracks_) {
        if (t.id == track_id) return &t;
    }
    return nullptr;
}

Track* Timeline::primary_track(TrackType type) {
    for (auto& t : tracks_) {
        if (t.type == type && t.auto_track_selector) return &t;
    }
    for (auto& t : tracks_) {
        if (t.type == type) return &t;
    }
    return nullptr;
}

const Track* Timeline::primary_track(TrackType type) const {
    for (const auto& t : tracks_) {
        if (t.type == type && t.auto_track_selector) return &t;
    }
    for (const auto& t : tracks_) {
        if (t.type == type) return &t;
    }
    return nullptr;
}

std::vector<Track*> Timeline::tracks_of_type(TrackType type) {
    std::vector<Track*> out;
    for (auto& t : tracks_) {
        if (t.type == type) out.push_back(&t);
    }
    std::sort(out.begin(), out.end(), [](const Track* a, const Track* b) { return a->index < b->index; });
    return out;
}

std::vector<const Track*> Timeline::tracks_of_type(TrackType type) const {
    std::vector<const Track*> out;
    for (const auto& t : tracks_) {
        if (t.type == type) out.push_back(&t);
    }
    std::sort(out.begin(), out.end(), [](const Track* a, const Track* b) { return a->index < b->index; });
    return out;
}

ClipItem* Timeline::find_clip(uint64_t clip_id, uint64_t* out_track_id) {
    for (auto& t : tracks_) {
        if (ClipItem* c = t.find_clip(clip_id)) {
            if (out_track_id) *out_track_id = t.id;
            return c;
        }
    }
    return nullptr;
}

const ClipItem* Timeline::find_clip(uint64_t clip_id, uint64_t* out_track_id) const {
    for (const auto& t : tracks_) {
        if (const ClipItem* c = t.find_clip(clip_id)) {
            if (out_track_id) *out_track_id = t.id;
            return c;
        }
    }
    return nullptr;
}

std::vector<ClipItem*> Timeline::find_linked_clips(uint64_t link_group_id) {
    std::vector<ClipItem*> out;
    if (link_group_id == 0) return out;
    for (auto& t : tracks_) {
        for (auto& c : t.clips) {
            if (c.link_group_id == link_group_id) out.push_back(&c);
        }
    }
    return out;
}

uint64_t Timeline::add_marker(Marker m) {
    if (m.id == 0) m.id = next_marker_id_++;
    else next_marker_id_ = std::max(next_marker_id_, m.id + 1);
    markers_.push_back(std::move(m));
    return markers_.back().id;
}

bool Timeline::remove_marker(uint64_t marker_id) {
    const auto it = std::find_if(markers_.begin(), markers_.end(),
                                 [marker_id](const Marker& m) { return m.id == marker_id; });
    if (it == markers_.end()) return false;
    markers_.erase(it);
    return true;
}

uint64_t Timeline::add_annotation(PresentationAnnotation a) {
    if (a.id == 0) a.id = next_annotation_id_++;
    else next_annotation_id_ = std::max(next_annotation_id_, a.id + 1);
    annotations_.push_back(std::move(a));
    return annotations_.back().id;
}

bool Timeline::resolve_annotation(uint64_t annotation_id, bool resolved) {
    for (auto& a : annotations_) {
        if (a.id == annotation_id) {
            a.resolved = resolved;
            return true;
        }
    }
    return false;
}

RationalTime Timeline::duration() const {
    RationalTime end{};
    for (const auto& t : tracks_) {
        end = std::max(end, t.duration());
    }
    return end;
}

FrameRenderPlan Timeline::build_render_plan(const RationalTime& t) const {
    FrameRenderPlan plan;
    plan.timeline_time = t;
    std::vector<const Track*> v_tracks = tracks_of_type(TrackType::Video);
    bool any_solo = false;
    for (const Track* tr : v_tracks) {
        if (tr->soloed) { any_solo = true; break; }
    }
    for (const Track* tr : v_tracks) {
        if (!tr->enabled || tr->muted) continue;
        if (any_solo && !tr->soloed) continue;
        const ClipItem* c = tr->clip_at(t);
        if (!c || !c->enabled) continue;
        if (c->kind == ClipKind::AdjustmentClip) {
            plan.active_adjustment_clip_ids.push_back(c->id);
            continue;
        }
        FrameLayerDescriptor desc;
        desc.track_id = tr->id;
        desc.track_index = tr->index;
        desc.clip_id = c->id;
        desc.color_group_id = c->color_group_id;
        desc.kind = c->kind;
        desc.blend_mode = c->blend_mode;
        desc.opacity = c->opacity;
        if (c->kind == ClipKind::MulticamClip && !c->multicam_angles.empty()) {
            const auto idx = std::min(c->multicam_active_angle, c->multicam_angles.size() - 1);
            desc.media_id = c->multicam_angles[idx].media_id;
            desc.source_time = c->map_timeline_to_source(t) + c->multicam_angles[idx].sync_offset;
        } else {
            desc.media_id = c->media_id;
            desc.source_time = c->map_timeline_to_source(t);
        }
        plan.video_layers.push_back(std::move(desc));
    }

    for (const Track* tr : tracks_of_type(TrackType::Adjustment)) {
        if (!tr->enabled || tr->muted) continue;
        if (const ClipItem* c = tr->clip_at(t)) {
            if (c->enabled) plan.active_adjustment_clip_ids.push_back(c->id);
        }
    }
    return plan;
}

void composite_rgba32f_layer(VideoFrame& dst, const VideoFrame& src, BlendMode mode, float opacity) {
    if (!dst.valid() || !src.valid()) return;
    const int32_t w = std::min(dst.width, src.width);
    const int32_t h = std::min(dst.height, src.height);
    const float op = clamp01(opacity);
    float* dp = dst.data();
    const float* sp = src.data();

    for (int32_t y = 0; y < h; ++y) {
        float* drow = dp + y * dst.stride_floats;
        const float* srow = sp + y * src.stride_floats;
        for (int32_t x = 0; x < w; ++x) {
            const float sr = srow[x * 4 + 0];
            const float sg = srow[x * 4 + 1];
            const float sb = srow[x * 4 + 2];
            const float sa = clamp01(srow[x * 4 + 3]) * op;

            const float dr = drow[x * 4 + 0];
            const float dg = drow[x * 4 + 1];
            const float db = drow[x * 4 + 2];
            const float da = clamp01(drow[x * 4 + 3]);

            float br = sr, bg = sg, bb = sb;
            if (mode == BlendMode::Hue || mode == BlendMode::Saturation ||
                mode == BlendMode::Color || mode == BlendMode::Luminosity) {
                float dh, ds, dl, sh, ss, sl;
                rgb_to_hsl(clamp01(dr), clamp01(dg), clamp01(db), dh, ds, dl);
                rgb_to_hsl(clamp01(sr), clamp01(sg), clamp01(sb), sh, ss, sl);
                if (mode == BlendMode::Hue) hsl_to_rgb(sh, ds, dl, br, bg, bb);
                else if (mode == BlendMode::Saturation) hsl_to_rgb(dh, ss, dl, br, bg, bb);
                else if (mode == BlendMode::Color) hsl_to_rgb(sh, ss, dl, br, bg, bb);
                else hsl_to_rgb(dh, ds, sl, br, bg, bb);
            } else {
                br = blend_channel(dr, sr, mode);
                bg = blend_channel(dg, sg, mode);
                bb = blend_channel(db, sb, mode);
            }

            const float inv_sa = 1.0f - sa;
            drow[x * 4 + 0] = br * sa + dr * inv_sa;
            drow[x * 4 + 1] = bg * sa + dg * inv_sa;
            drow[x * 4 + 2] = bb * sa + db * inv_sa;
            drow[x * 4 + 3] = sa + da * inv_sa;
        }
    }
}

bool Timeline::render_frame_with_color_hook(const RationalTime& t,
                                            const MediaPool& media_pool,
                                            VideoFrame& out_frame,
                                            IColorPipelineHook* color_hook,
                                            FrameMemoryPool* memory_pool) const {
    const FrameRenderPlan plan = build_render_plan(t);
    out_frame = VideoFrame::allocate(width_, height_, memory_pool);
    if (!out_frame.valid()) return false;
    out_frame.pts = t;

    // Clear canvas to opaque black background
    float* canvas = out_frame.data();
    const std::size_t px_count = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
    for (std::size_t i = 0; i < px_count; ++i) {
        canvas[i * 4 + 0] = 0.0f;
        canvas[i * 4 + 1] = 0.0f;
        canvas[i * 4 + 2] = 0.0f;
        canvas[i * 4 + 3] = 1.0f;
    }

    for (const auto& layer : plan.video_layers) {
        VideoFrame layer_frame;
        if (layer.kind == ClipKind::GeneratorClip) {
            if (const ClipItem* c = find_clip(layer.clip_id)) {
                layer_frame = VideoFrame::allocate(width_, height_, memory_pool);
                render_generator(c->generator_spec, layer_frame, layer.source_time);
            }
        } else if (layer.kind == ClipKind::CompoundClip) {
            if (const ClipItem* c = find_clip(layer.clip_id)) {
                if (const Timeline* sub = find_compound_timeline(c->compound_timeline_id)) {
                    sub->render_frame_with_color_hook(layer.source_time, media_pool, layer_frame,
                                                      color_hook, memory_pool);
                }
            }
        } else {
            const MediaItem* item = media_pool.find(layer.media_id);
            if (!item || !item->decoder) continue;
            if (!item->decoder->decode_video_frame(layer.source_time, layer_frame, memory_pool)) {
                continue;
            }
        }

        if (!layer_frame.valid()) continue;

        if (color_hook != nullptr && !color_hook->is_bypassed(layer.clip_id, layer.color_group_id)) {
            FrameBufferView32F view = layer_frame.view(layer.clip_id, layer.track_id, layer.color_group_id);
            view.time = RationalTime64{t.num(), t.den()};
            color_hook->process_frame(view, ColorPassScope::Clip);
        }

        // Apply clip spatial transform / dynamic zoom & transition/fade envelope from metadata
        float layer_opacity = layer.opacity;
        if (const ClipItem* clip_obj = find_clip(layer.clip_id)) {
            auto meta_double = [&](const char* key, double fallback) -> double {
                const auto it = clip_obj->metadata.find(key);
                if (it == clip_obj->metadata.end() || it->second.empty()) return fallback;
                char* endp = nullptr;
                const double v = std::strtod(it->second.c_str(), &endp);
                return (endp != it->second.c_str() && std::isfinite(v)) ? v : fallback;
            };
            auto meta_str = [&](const char* key) -> std::string {
                const auto it = clip_obj->metadata.find(key);
                return (it != clip_obj->metadata.end()) ? it->second : std::string();
            };

            const double clip_dur_ms = std::max(1.0, clip_obj->timeline_duration.to_seconds() * 1000.0);
            const double local_ms = std::clamp((t - clip_obj->timeline_start).to_seconds() * 1000.0, 0.0, clip_dur_ms);
            const double u_prog = local_ms / clip_dur_ms;

            const double dz_start = meta_double("dynamic_zoom_start", 1.0);
            const double dz_end = meta_double("dynamic_zoom_end", 1.0);
            const double dz_scale = dz_start + (dz_end - dz_start) * u_prog;

            const double base_scale = std::clamp(meta_double("scale", 1.0) * dz_scale, 0.1, 8.0);
            const double pan_x = meta_double("pan_x", 0.0);
            const double pan_y = meta_double("pan_y", 0.0);
            const double rot_deg = meta_double("rotation_deg", 0.0);

            const double fade_in_ms = std::max(0.0, meta_double("fade_in_ms", 0.0));
            const double fade_out_ms = std::max(0.0, meta_double("fade_out_ms", 0.0));
            if (fade_in_ms > 0.0 && local_ms < fade_in_ms) {
                layer_opacity *= static_cast<float>(std::clamp(local_ms / fade_in_ms, 0.0, 1.0));
            }
            if (fade_out_ms > 0.0 && (clip_dur_ms - local_ms) < fade_out_ms) {
                layer_opacity *= static_cast<float>(std::clamp((clip_dur_ms - local_ms) / fade_out_ms, 0.0, 1.0));
            }

            const std::string tr_kind = meta_str("transition_kind");
            const double tr_ms = std::max(0.0, meta_double("transition_ms", 0.0));
            double tr_u = 1.0;
            if (!tr_kind.empty() && tr_kind != "none" && tr_ms > 0.0 && local_ms < tr_ms) {
                tr_u = std::clamp(local_ms / tr_ms, 0.0, 1.0);
            }

            // If a non-dip transition overlaps a preceding adjacent clip on this track, render the outgoing frame underneath
            if (tr_u < 1.0 && tr_kind != "dip_black" && tr_kind != "dip_white") {
                if (const Track* tr = find_track(layer.track_id)) {
                    const ClipItem* prev_clip = nullptr;
                    for (const auto& cand : tr->clips) {
                        if (cand.id != clip_obj->id && cand.enabled &&
                            cand.timeline_end() <= clip_obj->timeline_start) {
                            if (!prev_clip || cand.timeline_end() > prev_clip->timeline_end()) {
                                prev_clip = &cand;
                            }
                        }
                    }
                    if (prev_clip != nullptr &&
                        (clip_obj->timeline_start - prev_clip->timeline_end()).to_seconds() <= 0.05) {
                        if (const MediaItem* prev_item = media_pool.find(prev_clip->media_id);
                            prev_item != nullptr && prev_item->decoder != nullptr) {
                            VideoFrame prev_frame;
                            const RationalTime prev_src_t = prev_clip->source_out;
                            if (prev_item->decoder->decode_video_frame(prev_src_t, prev_frame, memory_pool) &&
                                prev_frame.valid()) {
                                if (color_hook != nullptr &&
                                    !color_hook->is_bypassed(prev_clip->id, prev_clip->color_group_id)) {
                                    FrameBufferView32F pv = prev_frame.view(
                                        prev_clip->id, tr->id, prev_clip->color_group_id);
                                    pv.time = RationalTime64{t.num(), t.den()};
                                    color_hook->process_frame(pv, ColorPassScope::Clip);
                                }
                                composite_rgba32f_layer(out_frame, prev_frame, prev_clip->blend_mode, prev_clip->opacity);
                            }
                        }
                    }
                }
            }

            double eff_scale = base_scale;
            double eff_pan_x = pan_x;
            if (tr_u < 1.0) {
                if (tr_kind == "dissolve") {
                    layer_opacity *= static_cast<float>(tr_u);
                } else if (tr_kind == "slide_left") {
                    eff_pan_x += (1.0 - tr_u);
                } else if (tr_kind == "zoom_in") {
                    eff_scale *= (0.75 + 0.25 * tr_u);
                    layer_opacity *= static_cast<float>(tr_u);
                }
            }

            if (std::abs(eff_scale - 1.0) > 1e-4 || std::abs(eff_pan_x) > 1e-4 ||
                std::abs(pan_y) > 1e-4 || std::abs(rot_deg) > 1e-4) {
                VideoFrame xformed = VideoFrame::allocate(layer_frame.width, layer_frame.height, memory_pool);
                if (xformed.valid()) {
                    const double rad = -rot_deg * 3.14159265358979323846 / 180.0;
                    const double cs = std::cos(rad);
                    const double sn = std::sin(rad);
                    const double inv_s = 1.0 / eff_scale;
                    const float cx = static_cast<float>(layer_frame.width) * 0.5f;
                    const float cy = static_cast<float>(layer_frame.height) * 0.5f;
                    const float* sp = layer_frame.data();
                    float* dp = xformed.data();
                    for (int32_t y = 0; y < xformed.height; ++y) {
                        float* drow = dp + y * xformed.stride_floats;
                        const double dy = static_cast<double>(y) - cy - pan_y * layer_frame.height;
                        for (int32_t x = 0; x < xformed.width; ++x) {
                            const double dx = static_cast<double>(x) - cx - eff_pan_x * layer_frame.width;
                            const double sx = (dx * cs - dy * sn) * inv_s + cx;
                            const double sy = (dx * sn + dy * cs) * inv_s + cy;
                            const int32_t ix = static_cast<int32_t>(std::llround(sx));
                            const int32_t iy = static_cast<int32_t>(std::llround(sy));
                            if (ix >= 0 && ix < layer_frame.width && iy >= 0 && iy < layer_frame.height) {
                                const float* srow = sp + iy * layer_frame.stride_floats;
                                drow[x * 4 + 0] = srow[ix * 4 + 0];
                                drow[x * 4 + 1] = srow[ix * 4 + 1];
                                drow[x * 4 + 2] = srow[ix * 4 + 2];
                                drow[x * 4 + 3] = srow[ix * 4 + 3];
                            } else {
                                drow[x * 4 + 0] = drow[x * 4 + 1] = drow[x * 4 + 2] = drow[x * 4 + 3] = 0.0f;
                            }
                        }
                    }
                    layer_frame = std::move(xformed);
                }
            }

            if (tr_u < 1.0) {
                if (tr_kind == "dip_black" || tr_kind == "dip_white") {
                    const float dip_v = (tr_kind == "dip_white") ? 1.0f : 0.0f;
                    const float k = static_cast<float>(tr_u);
                    float* lp = layer_frame.data();
                    for (int32_t y = 0; y < layer_frame.height; ++y) {
                        float* lrow = lp + y * layer_frame.stride_floats;
                        for (int32_t x = 0; x < layer_frame.width; ++x) {
                            lrow[x * 4 + 0] = dip_v * (1.0f - k) + lrow[x * 4 + 0] * k;
                            lrow[x * 4 + 1] = dip_v * (1.0f - k) + lrow[x * 4 + 1] * k;
                            lrow[x * 4 + 2] = dip_v * (1.0f - k) + lrow[x * 4 + 2] * k;
                        }
                    }
                } else if (tr_kind == "wipe_left") {
                    const int32_t cutoff_x = static_cast<int32_t>(std::llround(tr_u * layer_frame.width));
                    float* lp = layer_frame.data();
                    for (int32_t y = 0; y < layer_frame.height; ++y) {
                        float* lrow = lp + y * layer_frame.stride_floats;
                        for (int32_t x = cutoff_x; x < layer_frame.width; ++x) {
                            lrow[x * 4 + 3] = 0.0f;
                        }
                    }
                }
            }
        }

        composite_rgba32f_layer(out_frame, layer_frame, layer.blend_mode, layer_opacity);
    }

    if (color_hook != nullptr) {
        const uint64_t adj_id = plan.active_adjustment_clip_ids.empty()
                                    ? 0 : plan.active_adjustment_clip_ids.front();
        if (!color_hook->is_bypassed(adj_id, 0)) {
            FrameBufferView32F tl_view = out_frame.view(adj_id, 0, 0);
            tl_view.time = RationalTime64{t.num(), t.den()};
            color_hook->process_frame(tl_view, ColorPassScope::Timeline);
        }
    }

    return true;
}

void Timeline::set_compound_timeline(uint64_t compound_id, Timeline sub) {
    compound_timelines_.insert_or_assign(compound_id, std::move(sub));
}

const Timeline* Timeline::find_compound_timeline(uint64_t compound_id) const {
    auto it = compound_timelines_.find(compound_id);
    return it == compound_timelines_.end() ? nullptr : &it->second;
}

Project::Project(std::string name) : name_(std::move(name)) {}

Timeline& Project::create_timeline(const std::string& name, FrameRate fps, int32_t width, int32_t height) {
    timelines_.emplace_back(next_timeline_id_++, name, fps, width, height);
    return timelines_.back();
}

Timeline* Project::find_timeline(uint64_t id) {
    for (auto& tl : timelines_) {
        if (tl.id() == id) return &tl;
    }
    return nullptr;
}

const Timeline* Project::find_timeline(uint64_t id) const {
    for (const auto& tl : timelines_) {
        if (tl.id() == id) return &tl;
    }
    return nullptr;
}

void TabbedStackedTimelineManager::open_tab(uint64_t timeline_id) {
    if (std::find(open_tabs_.begin(), open_tabs_.end(), timeline_id) == open_tabs_.end()) {
        open_tabs_.push_back(timeline_id);
    }
    active_tab_ = timeline_id;
}

void TabbedStackedTimelineManager::close_tab(uint64_t timeline_id) {
    open_tabs_.erase(std::remove(open_tabs_.begin(), open_tabs_.end(), timeline_id), open_tabs_.end());
    if (active_tab_ == timeline_id) {
        active_tab_ = open_tabs_.empty() ? 0 : open_tabs_.back();
    }
    if (secondary_tab_ == timeline_id) {
        secondary_tab_.reset();
    }
}

bool TabbedStackedTimelineManager::set_active_tab(uint64_t timeline_id) {
    if (std::find(open_tabs_.begin(), open_tabs_.end(), timeline_id) == open_tabs_.end()) return false;
    active_tab_ = timeline_id;
    return true;
}

void TabbedStackedTimelineManager::enable_stacked_view(bool enabled, std::optional<uint64_t> secondary_timeline_id) {
    stacked_enabled_ = enabled;
    secondary_tab_ = secondary_timeline_id;
}

std::vector<TimelineDiffEntry> compare_timelines(const Timeline& baseline, const Timeline& current) {
    struct Rec { const ClipItem* clip; int32_t track_idx; TrackType type; };
    std::map<uint64_t, Rec> base_map;
    std::map<uint64_t, Rec> curr_map;

    for (const auto& tr : baseline.tracks()) {
        for (const auto& c : tr.clips) {
            base_map[c.id] = Rec{&c, tr.index, tr.type};
        }
    }
    for (const auto& tr : current.tracks()) {
        for (const auto& c : tr.clips) {
            curr_map[c.id] = Rec{&c, tr.index, tr.type};
        }
    }

    std::vector<TimelineDiffEntry> diffs;
    for (const auto& [id, brec] : base_map) {
        auto it = curr_map.find(id);
        if (it == curr_map.end()) {
            diffs.push_back(TimelineDiffEntry{
                DiffKind::Removed, id, brec.clip->name, brec.track_idx, brec.type,
                brec.clip->timeline_start, brec.clip->timeline_duration, {}, {}
            });
        } else {
            const auto& crec = it->second;
            if (brec.clip->timeline_duration != crec.clip->timeline_duration ||
                brec.clip->source_in != crec.clip->source_in) {
                diffs.push_back(TimelineDiffEntry{
                    DiffKind::Trimmed, id, crec.clip->name, crec.track_idx, crec.type,
                    brec.clip->timeline_start, brec.clip->timeline_duration,
                    crec.clip->timeline_start, crec.clip->timeline_duration
                });
            } else if (brec.clip->timeline_start != crec.clip->timeline_start ||
                       brec.track_idx != crec.track_idx) {
                diffs.push_back(TimelineDiffEntry{
                    DiffKind::Moved, id, crec.clip->name, crec.track_idx, crec.type,
                    brec.clip->timeline_start, brec.clip->timeline_duration,
                    crec.clip->timeline_start, crec.clip->timeline_duration
                });
            }
        }
    }
    for (const auto& [id, crec] : curr_map) {
        if (base_map.find(id) == base_map.end()) {
            diffs.push_back(TimelineDiffEntry{
                DiffKind::Added, id, crec.clip->name, crec.track_idx, crec.type,
                {}, {}, crec.clip->timeline_start, crec.clip->timeline_duration
            });
        }
    }
    return diffs;
}

void DualTimelineController::set_viewport_duration(const RationalTime& detail_span) {
    if (detail_span > RationalTime::zero()) detail_span_ = detail_span;
}

void DualTimelineController::sync_to_playhead(const Timeline& tl) {
    const RationalTime half = detail_span_ / 2;
    const RationalTime p = tl.playhead();
    detail_start_ = (p > half) ? (p - half) : RationalTime::zero();
}

void DualTimelineController::scrub_overview(Timeline& tl, double normalized_pos) {
    const double u = std::clamp(normalized_pos, 0.0, 1.0);
    const RationalTime total = tl.duration();
    const RationalTime target = RationalTime::from_seconds_approx(total.to_seconds() * u, 10000);
    tl.set_playhead(target.quantized_to_frame(tl.fps()));
    sync_to_playhead(tl);
}

void DualTimelineController::scrub_detail(Timeline& tl, double normalized_pos) {
    const double u = std::clamp(normalized_pos, 0.0, 1.0);
    const RationalTime offset = RationalTime::from_seconds_approx(detail_span_.to_seconds() * u, 10000);
    tl.set_playhead((detail_start_ + offset).quantized_to_frame(tl.fps()));
}

TimeRange DualTimelineController::overview_range(const Timeline& tl) const {
    return TimeRange{RationalTime::zero(), tl.duration()};
}

TimeRange DualTimelineController::detail_range(const Timeline& tl) const {
    (void)tl;
    return TimeRange{detail_start_, detail_span_};
}

void SourceTapeController::rebuild(const std::vector<const MediaItem*>& items) {
    segments_.clear();
    RationalTime cursor = RationalTime::zero();
    for (const MediaItem* item : items) {
        if (!item) continue;
        const RationalTime dur = item->metadata.duration > RationalTime::zero()
                                     ? item->metadata.duration
                                     : RationalTime(5, 1);
        segments_.push_back(SourceTapeSegment{item->id, cursor, dur});
        cursor += dur;
    }
}

RationalTime SourceTapeController::total_duration() const {
    if (segments_.empty()) return RationalTime::zero();
    const auto& last = segments_.back();
    return last.tape_start + last.media_duration;
}

std::optional<std::pair<uint64_t, RationalTime>> SourceTapeController::map_tape_time(const RationalTime& tape_t) const {
    for (const auto& seg : segments_) {
        if (tape_t >= seg.tape_start && tape_t < seg.tape_start + seg.media_duration) {
            return std::make_pair(seg.media_id, tape_t - seg.tape_start);
        }
    }
    return std::nullopt;
}

SmartIndicatorResult compute_smart_indicator(const Timeline& tl, TrackType role) {
    SmartIndicatorResult res;
    const Track* target = tl.primary_track(role);
    if (!target) return res;
    res.target_track_id = target->id;
    const RationalTime ph = tl.playhead();
    const RationalTime snap_tol = RationalTime::from_frames(6, tl.fps());

    RationalTime best_point = ph;
    RationalTime best_dist = snap_tol;
    bool snapped = false;

    for (const auto& c : target->clips) {
        const RationalTime d_start = (c.timeline_start - ph).abs();
        if (d_start <= best_dist) {
            best_dist = d_start;
            best_point = c.timeline_start;
            snapped = true;
        }
        const RationalTime end = c.timeline_end();
        const RationalTime d_end = (end - ph).abs();
        if (d_end <= best_dist) {
            best_dist = d_end;
            best_point = end;
            snapped = true;
        }
    }
    res.insert_time = best_point;
    res.snapped_to_edit_point = snapped;
    res.will_ripple = true;
    return res;
}

} // namespace owncut::timeline

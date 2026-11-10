#include "timeline/owncut_adapter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <optional>
#include <sstream>

namespace owncut::timeline {
namespace {

TrackType infer_track_type(const std::string& track_id) {
    if (!track_id.empty() && (track_id[0] == 'a' || track_id[0] == 'A')) {
        return TrackType::Audio;
    }
    if (!track_id.empty() && (track_id[0] == 's' || track_id[0] == 'S')) {
        return TrackType::Subtitle;
    }
    return TrackType::Video;
}

bool ranges_overlap(int64_t a_start, int64_t a_dur, int64_t b_start, int64_t b_dur) {
    return a_start < (b_start + b_dur) && b_start < (a_start + a_dur);
}

BlendMode parse_blend_mode(const std::string& s) {
    if (s == "multiply") return BlendMode::Multiply;
    if (s == "screen") return BlendMode::Screen;
    if (s == "overlay") return BlendMode::Overlay;
    if (s == "add") return BlendMode::Add;
    if (s == "darken") return BlendMode::Darken;
    if (s == "lighten") return BlendMode::Lighten;
    if (s == "difference") return BlendMode::Difference;
    return BlendMode::Normal;
}

void parse_hex_rgba(const std::string& hex_in, float out_rgba[4], const float fallback[4]) {
    for (int i = 0; i < 4; ++i) out_rgba[i] = fallback[i];
    if (hex_in.empty()) return;
    std::string h = hex_in[0] == '#' ? hex_in.substr(1) : hex_in;
    unsigned long val = 0;
    if (h.size() == 6) {
        if (std::sscanf(h.c_str(), "%lx", &val) == 1) {
            out_rgba[0] = static_cast<float>((val >> 16) & 0xFFu) / 255.0f;
            out_rgba[1] = static_cast<float>((val >> 8) & 0xFFu) / 255.0f;
            out_rgba[2] = static_cast<float>(val & 0xFFu) / 255.0f;
            out_rgba[3] = 1.0f;
        }
    } else if (h.size() == 8) {
        if (std::sscanf(h.c_str(), "%lx", &val) == 1) {
            out_rgba[3] = static_cast<float>((val >> 24) & 0xFFu) / 255.0f;
            out_rgba[0] = static_cast<float>((val >> 16) & 0xFFu) / 255.0f;
            out_rgba[1] = static_cast<float>((val >> 8) & 0xFFu) / 255.0f;
            out_rgba[2] = static_cast<float>(val & 0xFFu) / 255.0f;
        }
    }
}

void apply_fx_to_clip(ClipItem& item, const RichClipFx& fx) {
    item.speed = std::clamp(fx.speed, 0.1, 8.0);
    item.reversed = fx.reversed;
    item.opacity = static_cast<float>(std::clamp(fx.opacity, 0.0, 1.0));
    item.blend_mode = parse_blend_mode(fx.blend_mode);
    item.muted = fx.muted;
    item.gain_db = (fx.volume <= 1e-4) ? -96.0f : static_cast<float>(20.0 * std::log10(fx.volume));

    auto put_d = [&](const char* k, double v) {
        item.metadata[k] = std::to_string(v);
    };
    auto put_i = [&](const char* k, int64_t v) {
        item.metadata[k] = std::to_string(v);
    };
    put_d("scale", fx.scale);
    put_d("pan_x", fx.pan_x);
    put_d("pan_y", fx.pan_y);
    put_d("rotation_deg", fx.rotation_deg);
    put_d("dynamic_zoom_start", fx.dynamic_zoom_start);
    put_d("dynamic_zoom_end", fx.dynamic_zoom_end);
    put_i("fade_in_ms", fx.fade_in_ms);
    put_i("fade_out_ms", fx.fade_out_ms);
    put_i("freeze_ms", fx.freeze_ms);
    item.metadata["transition_kind"] = fx.transition_kind;
    put_i("transition_ms", fx.transition_ms);
    item.metadata["chroma_key_hex"] = fx.chroma_key_hex;
    put_d("chroma_tolerance", fx.chroma_tolerance);
}

} // namespace

OwncutTimelineAdapter::OwncutTimelineAdapter(FrameRate fps, int32_t width, int32_t height)
    : project_("Owncut Session")
    , timeline_(1, "Main", fps, width, height) {
    if (auto* v = timeline_.primary_track(TrackType::Video)) {
        v->name = "v1";
        track_ids_["v1"] = v->id;
    } else {
        track_ids_["v1"] = timeline_.add_track(TrackType::Video, "v1").id;
    }
    if (auto* a = timeline_.primary_track(TrackType::Audio)) {
        a->name = "a1";
        track_ids_["a1"] = a->id;
    } else {
        track_ids_["a1"] = timeline_.add_track(TrackType::Audio, "a1").id;
    }
    snap_engine_.set_enabled(true);
    snap_engine_.set_threshold(RationalTime::from_frames(4, fps));
    dual_timeline_.set_viewport_duration(RationalTime(10, 1));
}

uint64_t OwncutTimelineAdapter::ensure_track_unlocked(const std::string& owncut_track_id) {
    const std::string key = owncut_track_id.empty() ? "v1" : owncut_track_id;
    const auto it = track_ids_.find(key);
    if (it != track_ids_.end() && timeline_.find_track(it->second) != nullptr) {
        return it->second;
    }
    Track& tr = timeline_.add_track(infer_track_type(key), key);
    track_ids_[key] = tr.id;
    return tr.id;
}

uint64_t OwncutTimelineAdapter::ensure_media_unlocked(const std::string& media_uri, int64_t min_duration_ms) {
    const std::string key = media_uri.empty() ? "synthetic://empty" : media_uri;
    const int64_t safe_ms = std::max<int64_t>(1, min_duration_ms);
    const auto it = media_ids_.find(key);
    if (it != media_ids_.end()) {
        if (MediaItem* existing = project_.media_pool().find(it->second)) {
            const RationalTime candidate_dur(safe_ms, 1000);
            if (candidate_dur > existing->metadata.duration) {
                existing->metadata.duration = candidate_dur;
            }
        }
        return it->second;
    }
    MediaItem item;
    item.metadata.name = key;
    item.metadata.uri = key;
    item.metadata.width = timeline_.width();
    item.metadata.height = timeline_.height();
    item.metadata.fps = timeline_.fps();
    item.metadata.duration = RationalTime(safe_ms, 1000);
    std::shared_ptr<IMediaDecoder> decoder =
        PlatformHardwareDecoderDescriptor::create_best_decoder(key, SyntheticPattern::MovingBlock);
    if (!decoder) {
        decoder = std::make_shared<SyntheticMediaDecoder>(SyntheticPattern::MovingBlock, item.metadata);
    }
    item.decoder = std::move(decoder);
    const uint64_t new_id = project_.media_pool().add_item(std::move(item));
    media_ids_[key] = new_id;
    return new_id;
}

uint64_t OwncutTimelineAdapter::ensure_clip_id_unlocked(const std::string& owncut_clip_id) {
    const auto it = clip_ids_.find(owncut_clip_id);
    if (it != clip_ids_.end()) return it->second;
    const uint64_t id = next_stable_clip_id_++;
    clip_ids_[owncut_clip_id] = id;
    return id;
}

void OwncutTimelineAdapter::apply_rich_state_to_timeline_unlocked() {
    snap_engine_.set_enabled(rich_state_.snap_enabled);

    // Sync markers onto core timeline model so MagneticSnapEngine uses them
    std::vector<uint64_t> old_ids;
    for (const auto& m : timeline_.markers()) old_ids.push_back(m.id);
    for (uint64_t mid : old_ids) timeline_.remove_marker(mid);
    for (const auto& rm : rich_state_.markers) {
        Marker m;
        m.time = RationalTime::from_milliseconds(std::max<int64_t>(0, rm.position_ms));
        m.name = rm.label;
        m.note = rm.id;
        timeline_.add_marker(std::move(m));
    }

    // Sync subtitle cues
    subtitle_engine_.clear();
    for (const auto& sc : rich_state_.subtitles) {
        SubtitleCue cue;
        cue.start = RationalTime::from_milliseconds(std::max<int64_t>(0, sc.start_ms));
        cue.end = RationalTime::from_milliseconds(std::max<int64_t>(sc.start_ms + 1, sc.end_ms));
        cue.text = sc.text;
        subtitle_engine_.add_cue(std::move(cue));
    }

    // Sync clip FX onto any matching clips currently on the timeline
    for (auto& tr : timeline_.tracks()) {
        for (auto& c : tr.clips) {
            const auto it_id = c.metadata.find("owncut_id");
            if (it_id == c.metadata.end()) continue;
            const auto it_fx = fx_by_clip_id_.find(it_id->second);
            if (it_fx != fx_by_clip_id_.end()) {
                apply_fx_to_clip(c, it_fx->second);
            }
        }
    }
}

void OwncutTimelineAdapter::sync_clips(const std::vector<OwncutClipSeed>& clips) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& tr : timeline_.tracks()) {
        tr.clips.clear();
    }

    const FrameRate fps = timeline_.fps();
    for (const auto& seed : clips) {
        if (seed.id.empty()) continue;
        const uint64_t tl_track_id = ensure_track_unlocked(seed.track_id);
        Track* track = timeline_.find_track(tl_track_id);
        if (!track) continue;

        const int64_t safe_dur_frames = std::max<int64_t>(1, seed.duration_frames);
        const int64_t safe_start_frame = std::max<int64_t>(0, seed.start_frame);
        const int64_t safe_src_in = std::max<int64_t>(0, seed.source_in_ms);
        const int64_t safe_src_out = std::max<int64_t>(safe_src_in + 1, seed.source_out_ms);

        const uint64_t tl_media_id = ensure_media_unlocked(seed.media_id, safe_src_out);
        const uint64_t tl_clip_id = ensure_clip_id_unlocked(seed.id);

        ClipItem item;
        item.id = tl_clip_id;
        item.media_id = tl_media_id;
        item.track_id = tl_track_id;
        item.color_group_id = seed.grade_active ? tl_clip_id : 0;
        item.kind = ClipKind::SourceClip;
        item.media_role = track->type;
        item.name = seed.id;
        item.timeline_start = RationalTime::from_frames(safe_start_frame, fps);
        item.timeline_duration = RationalTime::from_frames(safe_dur_frames, fps);
        item.source_in = RationalTime(safe_src_in, 1000);
        item.source_out = RationalTime(safe_src_out, 1000);
        item.source_total_duration = RationalTime(safe_src_out, 1000);
        item.metadata["owncut_id"] = seed.id;
        item.metadata["owncut_track"] = seed.track_id;

        const auto it_fx = fx_by_clip_id_.find(seed.id);
        if (it_fx != fx_by_clip_id_.end()) {
            apply_fx_to_clip(item, it_fx->second);
        }
        track->clips.push_back(std::move(item));
    }

    for (auto& tr : timeline_.tracks()) {
        tr.sort_clips();
    }
    dual_timeline_.sync_to_playhead(timeline_);
}

OwncutSnapEvaluation OwncutTimelineAdapter::evaluate_snap(const std::string& track_id,
                                                          const std::string& moving_clip_id,
                                                          int64_t proposed_start_frame,
                                                          int64_t duration_frames,
                                                          int64_t playhead_frame,
                                                          double pixels_per_frame,
                                                          bool snap_enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    OwncutSnapEvaluation out{};
    const FrameRate fps = timeline_.fps();
    const int64_t safe_proposed = std::max<int64_t>(0, proposed_start_frame);
    const int64_t safe_dur = std::max<int64_t>(1, duration_frames);
    const int64_t safe_playhead = std::max<int64_t>(0, playhead_frame);
    timeline_.set_playhead(RationalTime::from_frames(safe_playhead, fps));

    std::optional<uint64_t> exclude_id;
    if (!moving_clip_id.empty()) {
        const auto it = clip_ids_.find(moving_clip_id);
        if (it != clip_ids_.end()) exclude_id = it->second;
    }

    int64_t candidate_start = safe_proposed;
    if (snap_enabled && rich_state_.snap_enabled) {
        int64_t threshold_frames = 3;
        if (pixels_per_frame > 0.0 && std::isfinite(pixels_per_frame)) {
            threshold_frames = std::clamp(static_cast<int64_t>(std::llround(10.0 / pixels_per_frame)),
                                          int64_t{1}, int64_t{15});
        }
        snap_engine_.set_enabled(true);
        snap_engine_.set_threshold(RationalTime::from_frames(threshold_frames, fps));

        const RationalTime head_cand = RationalTime::from_frames(safe_proposed, fps);
        const RationalTime tail_cand = RationalTime::from_frames(safe_proposed + safe_dur, fps);

        const SnapResult head_snap = snap_engine_.snap(timeline_, head_cand, exclude_id, false);
        const SnapResult tail_snap = snap_engine_.snap(timeline_, tail_cand, exclude_id, false);

        int64_t best_dist = threshold_frames + 1;
        int64_t chosen_start = safe_proposed;
        int64_t chosen_guide = -1;
        bool did_snap = false;

        if (safe_proposed <= threshold_frames) {
            best_dist = safe_proposed;
            chosen_start = 0;
            chosen_guide = 0;
            did_snap = true;
        }
        if (head_snap.snapped) {
            const int64_t snapped_f = std::max<int64_t>(0, head_snap.snapped_time.to_frames(fps));
            const int64_t dist = std::abs(snapped_f - safe_proposed);
            if (dist < best_dist) {
                best_dist = dist;
                chosen_start = snapped_f;
                chosen_guide = snapped_f;
                did_snap = true;
            }
        }
        if (tail_snap.snapped) {
            const int64_t snapped_end_f = std::max<int64_t>(0, tail_snap.snapped_time.to_frames(fps));
            const int64_t tail_start_f = snapped_end_f - safe_dur;
            if (tail_start_f >= 0) {
                const int64_t dist = std::abs(snapped_end_f - (safe_proposed + safe_dur));
                if (dist < best_dist) {
                    best_dist = dist;
                    chosen_start = tail_start_f;
                    chosen_guide = snapped_end_f;
                    did_snap = true;
                }
            }
        }

        // Beat-grid magnetic snapping (if BPM active)
        if (rich_state_.beat_bpm > 1.0 && std::isfinite(rich_state_.beat_bpm)) {
            const double beat_period_frames = (60.0 / rich_state_.beat_bpm) * fps.to_double();
            if (beat_period_frames >= 1.0) {
                const double offset_frames =
                    (static_cast<double>(rich_state_.beat_offset_ms) / 1000.0) * fps.to_double();
                const int64_t nearest_beat_idx = static_cast<int64_t>(
                    std::llround((static_cast<double>(safe_proposed) - offset_frames) / beat_period_frames));
                if (nearest_beat_idx >= 0) {
                    const int64_t beat_f = std::max<int64_t>(
                        0, static_cast<int64_t>(std::llround(offset_frames + nearest_beat_idx * beat_period_frames)));
                    const int64_t dist = std::abs(beat_f - safe_proposed);
                    if (dist < best_dist) {
                        best_dist = dist;
                        chosen_start = beat_f;
                        chosen_guide = beat_f;
                        did_snap = true;
                    }
                }
            }
        }

        candidate_start = chosen_start;
        out.snapped = did_snap;
        out.snap_guide_frame = chosen_guide;
    }

    const uint64_t tl_track_id = ensure_track_unlocked(track_id);
    const Track* track = timeline_.find_track(tl_track_id);

    struct Span { int64_t start; int64_t dur; };
    std::vector<Span> others;
    if (track) {
        for (const auto& c : track->clips) {
            if (exclude_id.has_value() && c.id == *exclude_id) continue;
            others.push_back(Span{
                std::max<int64_t>(0, c.timeline_start.to_frames(fps)),
                std::max<int64_t>(1, c.timeline_duration.to_frames(fps))
            });
        }
    }

    auto is_free = [&](int64_t start_f) -> bool {
        if (start_f < 0) return false;
        for (const auto& o : others) {
            if (ranges_overlap(start_f, safe_dur, o.start, o.dur)) return false;
        }
        return true;
    };

    if (is_free(candidate_start)) {
        out.final_frame = candidate_start;
        out.slot_adjusted = false;
        out.haptic_pulse = out.snapped;
        return out;
    }

    std::vector<int64_t> free_slots;
    if (is_free(0)) free_slots.push_back(0);
    for (const auto& o : others) {
        if (o.start >= safe_dur && is_free(o.start - safe_dur)) {
            free_slots.push_back(o.start - safe_dur);
        }
        if (is_free(o.start + o.dur)) {
            free_slots.push_back(o.start + o.dur);
        }
    }

    if (free_slots.empty()) {
        out.final_frame = safe_proposed;
        out.slot_adjusted = false;
        out.haptic_pulse = false;
        return out;
    }

    int64_t best_slot = free_slots.front();
    int64_t best_gap = std::abs(best_slot - safe_proposed);
    for (const int64_t slot : free_slots) {
        const int64_t gap = std::abs(slot - safe_proposed);
        if (gap < best_gap || (gap == best_gap && slot < best_slot)) {
            best_gap = gap;
            best_slot = slot;
        }
    }

    out.final_frame = best_slot;
    out.slot_adjusted = true;
    out.snapped = true;
    out.snap_guide_frame = best_slot;
    out.haptic_pulse = true;
    return out;
}

void OwncutTimelineAdapter::set_snap_enabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    rich_state_.snap_enabled = enabled;
    snap_engine_.set_enabled(enabled);
}

bool OwncutTimelineAdapter::snap_enabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rich_state_.snap_enabled;
}

void OwncutTimelineAdapter::set_beat_grid(double bpm, int64_t offset_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    rich_state_.beat_bpm = (std::isfinite(bpm) && bpm > 0.0) ? bpm : 0.0;
    rich_state_.beat_offset_ms = std::max<int64_t>(0, offset_ms);
}

void OwncutTimelineAdapter::set_clip_fx(const RichClipFx& fx) {
    if (fx.clip_id.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    fx_by_clip_id_[fx.clip_id] = fx;

    bool found = false;
    for (auto& existing : rich_state_.clip_fx) {
        if (existing.clip_id == fx.clip_id) {
            existing = fx;
            found = true;
            break;
        }
    }
    if (!found) {
        rich_state_.clip_fx.push_back(fx);
    }
    apply_rich_state_to_timeline_unlocked();
}

bool OwncutTimelineAdapter::remove_clip_fx(const std::string& clip_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool erased = fx_by_clip_id_.erase(clip_id) > 0;
    rich_state_.clip_fx.erase(
        std::remove_if(rich_state_.clip_fx.begin(), rich_state_.clip_fx.end(),
                       [&](const RichClipFx& f) { return f.clip_id == clip_id; }),
        rich_state_.clip_fx.end());
    return erased;
}

OwncutRichState OwncutTimelineAdapter::rich_state_copy() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rich_state_;
}

bool OwncutTimelineAdapter::import_rich_state_json(const std::string& json) {
    OwncutRichState parsed;
    if (!TimelineSerializer::state_from_json(json, parsed)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    rich_state_ = std::move(parsed);
    fx_by_clip_id_.clear();
    for (const auto& fx : rich_state_.clip_fx) {
        if (!fx.clip_id.empty()) fx_by_clip_id_[fx.clip_id] = fx;
    }
    apply_rich_state_to_timeline_unlocked();
    return true;
}

std::string OwncutTimelineAdapter::export_rich_state_json() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return TimelineSerializer::state_to_json(rich_state_);
}

void OwncutTimelineAdapter::clear_rich_state() {
    std::lock_guard<std::mutex> lock(mutex_);
    rich_state_ = OwncutRichState{};
    fx_by_clip_id_.clear();
    apply_rich_state_to_timeline_unlocked();
}

void OwncutTimelineAdapter::upsert_marker(const RichMarker& marker) {
    if (marker.id.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    bool replaced = false;
    for (auto& m : rich_state_.markers) {
        if (m.id == marker.id) {
            m = marker;
            replaced = true;
            break;
        }
    }
    if (!replaced) rich_state_.markers.push_back(marker);
    std::sort(rich_state_.markers.begin(), rich_state_.markers.end(),
              [](const RichMarker& a, const RichMarker& b) { return a.position_ms < b.position_ms; });
    apply_rich_state_to_timeline_unlocked();
}

bool OwncutTimelineAdapter::remove_marker(const std::string& marker_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t before = rich_state_.markers.size();
    rich_state_.markers.erase(
        std::remove_if(rich_state_.markers.begin(), rich_state_.markers.end(),
                       [&](const RichMarker& m) { return m.id == marker_id; }),
        rich_state_.markers.end());
    if (rich_state_.markers.size() != before) {
        apply_rich_state_to_timeline_unlocked();
        return true;
    }
    return false;
}

void OwncutTimelineAdapter::upsert_overlay(const RichTextOverlay& overlay) {
    if (overlay.id.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& o : rich_state_.overlays) {
        if (o.id == overlay.id) {
            o = overlay;
            return;
        }
    }
    rich_state_.overlays.push_back(overlay);
}

bool OwncutTimelineAdapter::remove_overlay(const std::string& overlay_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t before = rich_state_.overlays.size();
    rich_state_.overlays.erase(
        std::remove_if(rich_state_.overlays.begin(), rich_state_.overlays.end(),
                       [&](const RichTextOverlay& o) { return o.id == overlay_id; }),
        rich_state_.overlays.end());
    return rich_state_.overlays.size() != before;
}

bool OwncutTimelineAdapter::import_srt(const std::string& srt_text) {
    SubtitleEngine tmp;
    if (!tmp.import_srt(srt_text)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    rich_state_.subtitles.clear();
    std::size_t idx = 1;
    for (const auto& cue : tmp.cues()) {
        RichSubtitleCue sc;
        sc.id = "sub-" + std::to_string(idx++);
        sc.start_ms = cue.start.to_milliseconds();
        sc.end_ms = cue.end.to_milliseconds();
        sc.text = cue.text;
        rich_state_.subtitles.push_back(std::move(sc));
    }
    apply_rich_state_to_timeline_unlocked();
    return true;
}

std::string OwncutTimelineAdapter::export_srt() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return subtitle_engine_.export_srt();
}

bool OwncutTimelineAdapter::render_composited_frame(int64_t frame_index,
                                                    VideoFrame& out_frame,
                                                    IColorPipelineHook* color_hook,
                                                    FrameMemoryPool* memory_pool) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const RationalTime t = RationalTime::from_frames(std::max<int64_t>(0, frame_index), timeline_.fps());
    if (!timeline_.render_frame_with_color_hook(t, project_.media_pool(), out_frame, color_hook, memory_pool)) {
        return false;
    }

    const int64_t now_ms = t.to_milliseconds();
    MultiTextEngine mte;
    for (const auto& ov : rich_state_.overlays) {
        if (ov.text.empty()) continue;
        if (now_ms < ov.start_ms || now_ms >= ov.start_ms + std::max<int64_t>(1, ov.duration_ms)) continue;
        TextLayer tl_layer;
        tl_layer.utf8_text = ov.text;
        tl_layer.visible = true;
        tl_layer.style.font_size_pt = 42.0f;
        tl_layer.style.bold = ov.bold;
        const float def_fg[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        const float def_bg[4] = {0.0f, 0.0f, 0.0f, 0.72f};
        parse_hex_rgba(ov.fg_hex, tl_layer.style.color_rgba, def_fg);
        parse_hex_rgba(ov.bg_hex, tl_layer.style.background_rgba, def_bg);
        if (ov.style == "caption") {
            tl_layer.style.font_size_pt = 28.0f;
        } else if (ov.style == "sticker") {
            tl_layer.style.font_size_pt = 50.0f;
            tl_layer.style.outline_width = 2.0f;
        }
        tl_layer.transform.pos_x.set_default(ov.x - 0.5);
        tl_layer.transform.pos_y.set_default(ov.y - 0.5);
        tl_layer.transform.scale_x.set_default(ov.scale);
        tl_layer.transform.scale_y.set_default(ov.scale);
        tl_layer.transform.rotation_deg.set_default(ov.rotation_deg);
        mte.add_layer(std::move(tl_layer));
    }
    if (!mte.layers().empty()) {
        mte.composite_layers(out_frame, RationalTime::zero(), RationalTime::from_milliseconds(1000));
    }
    subtitle_engine_.composite_cues(out_frame, t);
    return true;
}

OwncutAudioAnalysis OwncutTimelineAdapter::analyze_pcm16_audio(const int16_t* pcm_interleaved,
                                                               int32_t sample_frames,
                                                               int32_t channels,
                                                               int32_t sample_rate,
                                                               int32_t num_peaks) {
    OwncutAudioAnalysis out;
    if (!pcm_interleaved || sample_frames <= 0 || channels <= 0 || sample_rate <= 0) {
        return out;
    }

    AudioBuffer buf;
    if (!convert_pcm16_to_audio_buffer(pcm_interleaved,
                                       static_cast<int64_t>(sample_frames),
                                       sample_rate,
                                       channels,
                                       RationalTime::zero(),
                                       buf)) {
        return out;
    }

    const LoudnessReport lufs = LoudnessMeter::measure(buf);
    out.integrated_lufs = lufs.integrated_lufs;
    out.true_peak_dbtp = lufs.true_peak_dbtp;

    const BeatDetectionResult beats = BeatDetector::detect_beats(buf);
    out.detected_bpm = beats.estimated_bpm;

    const int32_t safe_bins = std::clamp(num_peaks, 1, 4096);
    out.waveform_peaks.assign(static_cast<std::size_t>(safe_bins), 0.0f);
    for (int32_t b = 0; b < safe_bins; ++b) {
        const int64_t f0 = (static_cast<int64_t>(b) * sample_frames) / safe_bins;
        const int64_t f1 = std::max(f0 + 1, (static_cast<int64_t>(b + 1) * sample_frames) / safe_bins);
        float peak = 0.0f;
        for (int64_t f = f0; f < f1 && f < sample_frames; ++f) {
            for (int32_t c = 0; c < channels; ++c) {
                peak = std::max(peak, std::abs(buf.sample(f, c)));
            }
        }
        out.waveform_peaks[static_cast<std::size_t>(b)] = std::clamp(peak, 0.0f, 1.0f);
    }
    return out;
}

std::vector<int32_t> OwncutTimelineAdapter::detect_scene_cuts_rgba(const uint8_t* rgba_frames,
                                                                   int32_t num_frames,
                                                                   int32_t width,
                                                                   int32_t height,
                                                                   double threshold) {
    std::vector<int32_t> out_indices;
    if (!rgba_frames || num_frames < 2 || width <= 0 || height <= 0) return out_indices;

    const std::size_t frame_bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    std::vector<VideoFrame> frames;
    frames.reserve(static_cast<std::size_t>(num_frames));
    for (int32_t i = 0; i < num_frames; ++i) {
        VideoFrame vf = VideoFrame::allocate(width, height, nullptr);
        if (!vf.valid()) return out_indices;
        const uint8_t* src = rgba_frames + static_cast<std::size_t>(i) * frame_bytes;
        float* dst = vf.data();
        for (int32_t y = 0; y < height; ++y) {
            const uint8_t* srow = src + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4;
            float* drow = dst + y * vf.stride_floats;
            for (int32_t x = 0; x < width * 4; ++x) {
                drow[x] = static_cast<float>(srow[x]) / 255.0f;
            }
        }
        frames.push_back(std::move(vf));
    }

    const std::vector<DetectedCut> cuts = SceneAndCutDetector::detect_scene_cuts(
        frames, FrameRate::fps_30(), static_cast<float>(std::clamp(threshold, 0.05, 1.0)));
    out_indices.reserve(cuts.size());
    for (const auto& dc : cuts) {
        out_indices.push_back(static_cast<int32_t>(dc.frame_index));
    }
    return out_indices;
}

double OwncutTimelineAdapter::apply_pinch_zoom(double scale_factor, double focus_x_px) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (scale_factor > 0.0 && std::isfinite(scale_factor)) {
        gesture_controller_.on_pinch_zoom(scale_factor, focus_x_px);
    }
    return gesture_controller_.pixels_per_second() / 80.0;
}

void OwncutTimelineAdapter::reset_zoom() {
    std::lock_guard<std::mutex> lock(mutex_);
    gesture_controller_ = MobileGestureController{};
}

double OwncutTimelineAdapter::zoom_scale() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return gesture_controller_.pixels_per_second() / 80.0;
}

int64_t OwncutTimelineAdapter::duration_frames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return timeline_.duration().to_frames(timeline_.fps());
}

std::size_t OwncutTimelineAdapter::clip_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t count = 0;
    for (const auto& tr : timeline_.tracks()) count += tr.clips.size();
    return count;
}

std::size_t OwncutTimelineAdapter::track_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return timeline_.tracks().size();
}

uint64_t OwncutTimelineAdapter::resolve_clip_id(const std::string& owncut_clip_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = clip_ids_.find(owncut_clip_id);
    return (it != clip_ids_.end()) ? it->second : 0;
}

uint64_t OwncutTimelineAdapter::resolve_track_id(const std::string& owncut_track_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = track_ids_.find(owncut_track_id);
    return (it != track_ids_.end()) ? it->second : 0;
}

std::string OwncutTimelineAdapter::capabilities_json() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t clips = 0;
    for (const auto& tr : timeline_.tracks()) clips += tr.clips.size();
    const double zoom = gesture_controller_.pixels_per_second() / 80.0;
    const int64_t dur = timeline_.duration().to_frames(timeline_.fps());

    std::ostringstream oss;
    oss << "{"
        << "\"engine\":\"owncut_timeline_engine\","
        << "\"version\":\"1.1.0\","
        << "\"fpsNum\":" << timeline_.fps().num << ","
        << "\"fpsDen\":" << timeline_.fps().den << ","
        << "\"tracks\":" << timeline_.tracks().size() << ","
        << "\"clips\":" << clips << ","
        << "\"durationFrames\":" << dur << ","
        << "\"zoomScale\":" << zoom << ","
        << "\"snapEnabled\":" << (rich_state_.snap_enabled ? "true" : "false") << ","
        << "\"beatBpm\":" << rich_state_.beat_bpm << ","
        << "\"markers\":" << rich_state_.markers.size() << ","
        << "\"overlays\":" << rich_state_.overlays.size() << ","
        << "\"subtitles\":" << rich_state_.subtitles.size() << ","
        << "\"mediacodecAvailable\":" << (PlatformHardwareDecoderDescriptor::is_mediacodec_available() ? "true" : "false") << ","
        << "\"modules\":["
        << "\"rational_time\","
        << "\"memory_pool\","
        << "\"task_scheduler\","
        << "\"media_pool\","
        << "\"multitrack_model\","
        << "\"command_stack\","
        << "\"edit_7_modes\","
        << "\"magnetic_snap\","
        << "\"trim_engine\","
        << "\"retime_optical_flow\","
        << "\"keyframe_animation\","
        << "\"subtitles_titles\","
        << "\"vision_stabilizer_reframe\","
        << "\"audio_fft_dsp_lufs\","
        << "\"mobile_gestures\","
        << "\"color_pipeline_hook\","
        << "\"json_persistence\","
        << "\"android_mediacodec_decoder\""
        << "]"
        << "}";
    return oss.str();
}

} // namespace owncut::timeline

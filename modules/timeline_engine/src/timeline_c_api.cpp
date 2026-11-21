#include "timeline_c_api.h"

#include "timeline/command_stack.hpp"
#include "timeline/edit_operations.hpp"
#include "timeline/media_pool.hpp"
#include "timeline/memory_pool.hpp"
#include "timeline/mobile_ux.hpp"
#include "timeline/owncut_adapter.hpp"
#include "timeline/timeline_model.hpp"
#include "timeline/trim_engine.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

using namespace owncut::timeline;

struct OwnCutTimelineEngine {
    Project project;
    uint64_t active_timeline_id = 1;
    CommandStack command_stack;
    MobileGestureController gestures;
    std::unique_ptr<FrameMemoryPool> frame_pool;
    std::unique_ptr<CAbiColorPipelineHook> color_hook;
    std::unique_ptr<OwncutTimelineAdapter> owncut_adapter;

    Timeline& active_timeline() {
        Timeline* tl = project.find_timeline(active_timeline_id);
        return tl ? *tl : project.timelines().front();
    }
    const Timeline& active_timeline() const {
        const Timeline* tl = project.find_timeline(active_timeline_id);
        return tl ? *tl : project.timelines().front();
    }
};

extern "C" {

OwnCutTimelineEngine* owncut_tl_create(int32_t fps_num, int32_t fps_den, int32_t width, int32_t height) {
    if (fps_num <= 0 || fps_den <= 0 || width <= 0 || height <= 0) return nullptr;
    auto* eng = new OwnCutTimelineEngine();
    const FrameRate fps(fps_num, fps_den);
    Timeline& tl = eng->project.create_timeline("Main", fps, width, height);
    eng->active_timeline_id = tl.id();
    const std::size_t slab_bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4 * sizeof(float);
    eng->frame_pool = std::make_unique<FrameMemoryPool>(std::max<std::size_t>(64, slab_bytes), 6);
    eng->owncut_adapter = std::make_unique<OwncutTimelineAdapter>(fps, width, height);
    return eng;
}

void owncut_tl_destroy(OwnCutTimelineEngine* engine) {
    delete engine;
}

void owncut_tl_register_color_hook(OwnCutTimelineEngine* engine, ColorPipelineHookC hook) {
    if (!engine) return;
    if (!hook.process_frame) {
        engine->color_hook.reset();
    } else {
        engine->color_hook = std::make_unique<CAbiColorPipelineHook>(hook);
    }
}

uint64_t owncut_tl_add_synthetic_media(OwnCutTimelineEngine* engine,
                                       const char* name,
                                       int32_t width,
                                       int32_t height,
                                       int64_t duration_frames) {
    if (!engine || width <= 0 || height <= 0 || duration_frames <= 0) return 0;
    const FrameRate fps = engine->active_timeline().fps();
    MediaItem item;
    item.metadata.name = name ? std::string(name) : "Synthetic Clip";
    item.metadata.uri = "synthetic://" + item.metadata.name;
    item.metadata.width = width;
    item.metadata.height = height;
    item.metadata.fps = fps;
    item.metadata.duration = RationalTime::from_frames(duration_frames, fps);
    item.decoder = std::make_shared<SyntheticMediaDecoder>(SyntheticPattern::MovingBlock, item.metadata);
    return engine->project.media_pool().add_item(std::move(item));
}

int32_t owncut_tl_perform_edit(OwnCutTimelineEngine* engine,
                               int32_t edit_mode,
                               uint64_t media_id,
                               int64_t source_in_frames,
                               int64_t source_out_frames) {
    if (!engine || edit_mode < 0 || edit_mode > 6) return 0;
    const MediaItem* m = engine->project.media_pool().find(media_id);
    if (!m) return 0;
    Timeline& tl = engine->active_timeline();
    SourceInsertSpec spec;
    spec.media_id = media_id;
    spec.name = m->metadata.name;
    spec.source_in = RationalTime::from_frames(source_in_frames, tl.fps());
    spec.source_out = RationalTime::from_frames(source_out_frames, tl.fps());
    spec.media_duration = m->metadata.duration;
    return EditOperations::perform_edit(tl, engine->command_stack, static_cast<EditMode>(edit_mode), spec) ? 1 : 0;
}

int32_t owncut_tl_blade_split(OwnCutTimelineEngine* engine, int64_t frame_index) {
    if (!engine) return 0;
    Timeline& tl = engine->active_timeline();
    return EditOperations::blade_split_at(tl, engine->command_stack,
                                            RationalTime::from_frames(frame_index, tl.fps()))
               ? 1
               : 0;
}

int32_t owncut_tl_trim_clip(OwnCutTimelineEngine* engine,
                            uint64_t clip_id,
                            int32_t trim_mode,
                            int64_t delta_frames,
                            int32_t ripple,
                            int32_t safe_trimming) {
    if (!engine || trim_mode < 0 || trim_mode > 4) return 0;
    Timeline& tl = engine->active_timeline();
    const auto rep = TrimEngine::trim_clip(
        tl, engine->command_stack, clip_id, static_cast<TrimMode>(trim_mode),
        RationalTime::from_frames(delta_frames, tl.fps()),
        ripple != 0, safe_trimming != 0);
    return rep.applied_delta.num() != 0 ? 1 : 0;
}

int32_t owncut_tl_ripple_delete_clip(OwnCutTimelineEngine* engine, uint64_t clip_id) {
    if (!engine) return 0;
    return EditOperations::ripple_delete_clip(engine->active_timeline(), engine->command_stack, clip_id) ? 1 : 0;
}

int32_t owncut_tl_undo(OwnCutTimelineEngine* engine) {
    if (!engine) return 0;
    return engine->command_stack.undo(engine->active_timeline()) ? 1 : 0;
}

int32_t owncut_tl_redo(OwnCutTimelineEngine* engine) {
    if (!engine) return 0;
    return engine->command_stack.redo(engine->active_timeline()) ? 1 : 0;
}

void owncut_tl_set_playhead_frames(OwnCutTimelineEngine* engine, int64_t frame_index) {
    if (!engine) return;
    Timeline& tl = engine->active_timeline();
    tl.set_playhead(RationalTime::from_frames(std::max<int64_t>(0, frame_index), tl.fps()));
}

int64_t owncut_tl_get_playhead_frames(const OwnCutTimelineEngine* engine) {
    if (!engine) return 0;
    const Timeline& tl = engine->active_timeline();
    return tl.playhead().to_frames(tl.fps());
}

int64_t owncut_tl_get_duration_frames(const OwnCutTimelineEngine* engine) {
    if (!engine) return 0;
    const Timeline& tl = engine->active_timeline();
    return tl.duration().to_frames(tl.fps());
}

int32_t owncut_tl_render_frame_rgba32f(OwnCutTimelineEngine* engine,
                                       int64_t frame_index,
                                       float* out_rgba32f,
                                       int32_t out_buffer_floats) {
    if (!engine || !out_rgba32f) return 0;
    const Timeline& tl = engine->active_timeline();
    const int32_t needed = tl.width() * tl.height() * 4;
    if (out_buffer_floats < needed) return 0;

    VideoFrame rendered;
    const RationalTime t = RationalTime::from_frames(std::max<int64_t>(0, frame_index), tl.fps());
    if (!tl.render_frame_with_color_hook(t, engine->project.media_pool(), rendered,
                                         engine->color_hook.get(), engine->frame_pool.get())) {
        return 0;
    }
    const int32_t w = tl.width();
    const int32_t h = tl.height();
    for (int32_t y = 0; y < h; ++y) {
        std::copy_n(rendered.data() + y * rendered.stride_floats, w * 4, out_rgba32f + y * w * 4);
    }
    return 1;
}

void owncut_tl_gesture_pinch(OwnCutTimelineEngine* engine, double scale, double focus_x_px) {
    if (!engine) return;
    engine->gestures.on_pinch_zoom(scale, focus_x_px);
    if (engine->owncut_adapter) {
        (void)engine->owncut_adapter->apply_pinch_zoom(scale, focus_x_px);
    }
}

double owncut_tl_gesture_pinch_scale(OwnCutTimelineEngine* engine, double scale, double focus_x_px) {
    if (!engine) return 1.0;
    engine->gestures.on_pinch_zoom(scale, focus_x_px);
    if (engine->owncut_adapter) {
        return engine->owncut_adapter->apply_pinch_zoom(scale, focus_x_px);
    }
    return engine->gestures.pixels_per_second() / 80.0;
}

void owncut_tl_reset_zoom(OwnCutTimelineEngine* engine) {
    if (!engine) return;
    engine->gestures = MobileGestureController{};
    if (engine->owncut_adapter) engine->owncut_adapter->reset_zoom();
}

double owncut_tl_get_zoom_scale(const OwnCutTimelineEngine* engine) {
    if (!engine) return 1.0;
    if (engine->owncut_adapter) return engine->owncut_adapter->zoom_scale();
    return engine->gestures.pixels_per_second() / 80.0;
}

int32_t owncut_tl_gesture_two_finger_undo(OwnCutTimelineEngine* engine) {
    if (!engine) return 0;
    return engine->gestures.on_two_finger_tap(engine->active_timeline(), engine->command_stack) ? 1 : 0;
}

int32_t owncut_tl_gesture_three_finger_redo(OwnCutTimelineEngine* engine) {
    if (!engine) return 0;
    return engine->gestures.on_three_finger_tap(engine->active_timeline(), engine->command_stack) ? 1 : 0;
}

void owncut_tl_gesture_jog_rotate(OwnCutTimelineEngine* engine, double delta_rad) {
    if (!engine) return;
    engine->gestures.on_jog_rotate(engine->active_timeline(), delta_rad);
}

int32_t owncut_tl_sync_clips(OwnCutTimelineEngine* engine,
                             const OwnCutBridgeClipSpec* clips,
                             int32_t clip_count) {
    if (!engine || !engine->owncut_adapter || clip_count < 0) return 0;
    if (clip_count > 0 && !clips) return 0;

    std::vector<OwncutClipSeed> seeds;
    seeds.reserve(static_cast<std::size_t>(clip_count));
    for (int32_t i = 0; i < clip_count; ++i) {
        const auto& c = clips[i];
        OwncutClipSeed s;
        s.id = c.id ? std::string(c.id) : "";
        s.track_id = c.track_id ? std::string(c.track_id) : "v1";
        s.media_id = c.media_id ? std::string(c.media_id) : "";
        s.start_frame = c.start_frame;
        s.duration_frames = c.duration_frames;
        s.source_in_ms = c.source_in_ms;
        s.source_out_ms = c.source_out_ms;
        s.grade_active = (c.grade_active != 0);
        seeds.push_back(std::move(s));
    }
    engine->owncut_adapter->sync_clips(seeds);
    return 1;
}

int32_t owncut_tl_evaluate_snap(OwnCutTimelineEngine* engine,
                                const char* track_id,
                                const char* moving_clip_id,
                                int64_t proposed_start_frame,
                                int64_t duration_frames,
                                int64_t playhead_frame,
                                double pixels_per_frame,
                                int32_t snap_enabled,
                                OwnCutSnapOutcome* out_outcome) {
    if (!engine || !engine->owncut_adapter || !out_outcome) return 0;
    const OwncutSnapEvaluation eval = engine->owncut_adapter->evaluate_snap(
        track_id ? std::string(track_id) : "v1",
        moving_clip_id ? std::string(moving_clip_id) : "",
        proposed_start_frame,
        duration_frames,
        playhead_frame,
        pixels_per_frame,
        snap_enabled != 0);

    out_outcome->final_frame = eval.final_frame;
    out_outcome->snapped = eval.snapped ? 1 : 0;
    out_outcome->snap_guide_frame = eval.snap_guide_frame;
    out_outcome->slot_adjusted = eval.slot_adjusted ? 1 : 0;
    out_outcome->haptic_pulse = eval.haptic_pulse ? 1 : 0;
    return 1;
}

int32_t owncut_tl_get_capabilities_json(const OwnCutTimelineEngine* engine,
                                        char* out_buffer,
                                        int32_t out_capacity) {
    if (!engine || !engine->owncut_adapter) return 0;
    const std::string json = engine->owncut_adapter->capabilities_json();
    const int32_t needed = static_cast<int32_t>(json.size()) + 1;
    if (out_buffer && out_capacity > 0) {
        const std::size_t copy_len = std::min<std::size_t>(json.size(), static_cast<std::size_t>(out_capacity - 1));
        std::memcpy(out_buffer, json.data(), copy_len);
        out_buffer[copy_len] = '\0';
    }
    return needed;
}

int32_t owncut_tl_export_state_json(const OwnCutTimelineEngine* engine,
                                    char* out_buffer,
                                    int32_t out_capacity) {
    if (!engine || !engine->owncut_adapter) return 0;
    const std::string json = engine->owncut_adapter->export_rich_state_json();
    const int32_t needed = static_cast<int32_t>(json.size()) + 1;
    if (out_buffer && out_capacity > 0) {
        const std::size_t copy_len = std::min<std::size_t>(json.size(), static_cast<std::size_t>(out_capacity - 1));
        std::memcpy(out_buffer, json.data(), copy_len);
        out_buffer[copy_len] = '\0';
    }
    return needed;
}

int32_t owncut_tl_import_state_json(OwnCutTimelineEngine* engine, const char* json) {
    if (!engine || !engine->owncut_adapter || !json) return 0;
    return engine->owncut_adapter->import_rich_state_json(std::string(json)) ? 1 : 0;
}

void owncut_tl_set_snap_enabled(OwnCutTimelineEngine* engine, int32_t enabled) {
    if (!engine || !engine->owncut_adapter) return;
    engine->owncut_adapter->set_snap_enabled(enabled != 0);
}

int32_t owncut_tl_get_snap_enabled(const OwnCutTimelineEngine* engine) {
    if (!engine || !engine->owncut_adapter) return 1;
    return engine->owncut_adapter->snap_enabled() ? 1 : 0;
}

void owncut_tl_set_beat_grid(OwnCutTimelineEngine* engine, double bpm, int64_t offset_ms) {
    if (!engine || !engine->owncut_adapter) return;
    engine->owncut_adapter->set_beat_grid(bpm, offset_ms);
}

void owncut_tl_upsert_marker(OwnCutTimelineEngine* engine,
                             const char* id,
                             int64_t position_ms,
                             const char* label,
                             const char* color_hex) {
    if (!engine || !engine->owncut_adapter || !id) return;
    RichMarker m;
    m.id = id;
    m.position_ms = position_ms;
    m.label = label ? std::string(label) : "";
    m.color_hex = color_hex ? std::string(color_hex) : "#4F9CF9";
    engine->owncut_adapter->upsert_marker(m);
}

int32_t owncut_tl_remove_marker(OwnCutTimelineEngine* engine, const char* id) {
    if (!engine || !engine->owncut_adapter || !id) return 0;
    return engine->owncut_adapter->remove_marker(std::string(id)) ? 1 : 0;
}

int32_t owncut_tl_import_srt(OwnCutTimelineEngine* engine, const char* srt_text) {
    if (!engine || !engine->owncut_adapter || !srt_text) return 0;
    return engine->owncut_adapter->import_srt(std::string(srt_text)) ? 1 : 0;
}

int32_t owncut_tl_export_srt(const OwnCutTimelineEngine* engine,
                             char* out_buffer,
                             int32_t out_capacity) {
    if (!engine || !engine->owncut_adapter) return 0;
    const std::string srt = engine->owncut_adapter->export_srt();
    const int32_t needed = static_cast<int32_t>(srt.size()) + 1;
    if (out_buffer && out_capacity > 0) {
        const std::size_t copy_len = std::min<std::size_t>(srt.size(), static_cast<std::size_t>(out_capacity - 1));
        std::memcpy(out_buffer, srt.data(), copy_len);
        out_buffer[copy_len] = '\0';
    }
    return needed;
}

int32_t owncut_tl_render_adapter_frame_rgba32f(OwnCutTimelineEngine* engine,
                                               int64_t frame_index,
                                               float* out_rgba32f,
                                               int32_t out_buffer_floats) {
    if (!engine || !engine->owncut_adapter || !out_rgba32f) return 0;
    const Timeline& tl = engine->owncut_adapter->timeline();
    const int32_t needed = tl.width() * tl.height() * 4;
    if (out_buffer_floats < needed) return 0;

    VideoFrame rendered;
    if (!engine->owncut_adapter->render_composited_frame(
            frame_index, rendered, engine->color_hook.get(), engine->frame_pool.get())) {
        return 0;
    }
    const int32_t w = tl.width();
    const int32_t h = tl.height();
    for (int32_t y = 0; y < h; ++y) {
        std::copy_n(rendered.data() + y * rendered.stride_floats, w * 4, out_rgba32f + y * w * 4);
    }
    return 1;
}

int32_t owncut_tl_analyze_pcm16(const int16_t* pcm_interleaved,
                                int32_t sample_frames,
                                int32_t channels,
                                int32_t sample_rate,
                                int32_t num_peaks,
                                float* out_peaks,
                                double* out_bpm,
                                double* out_lufs) {
    if (!pcm_interleaved || sample_frames <= 0 || channels <= 0 || sample_rate <= 0) return 0;
    const OwncutAudioAnalysis res = OwncutTimelineAdapter::analyze_pcm16_audio(
        pcm_interleaved, sample_frames, channels, sample_rate, num_peaks);
    if (out_bpm) *out_bpm = res.detected_bpm;
    if (out_lufs) *out_lufs = res.integrated_lufs;
    if (out_peaks && num_peaks > 0) {
        const std::size_t n = std::min<std::size_t>(res.waveform_peaks.size(), static_cast<std::size_t>(num_peaks));
        for (std::size_t i = 0; i < n; ++i) {
            out_peaks[i] = res.waveform_peaks[i];
        }
    }
    return static_cast<int32_t>(res.waveform_peaks.size());
}

int32_t owncut_tl_detect_scene_cuts(const uint8_t* rgba_frames,
                                    int32_t num_frames,
                                    int32_t width,
                                    int32_t height,
                                    double threshold,
                                    int32_t* out_indices,
                                    int32_t max_indices) {
    if (!rgba_frames || num_frames < 2 || width <= 0 || height <= 0) return 0;
    const std::vector<int32_t> cuts = OwncutTimelineAdapter::detect_scene_cuts_rgba(
        rgba_frames, num_frames, width, height, threshold);
    if (out_indices && max_indices > 0) {
        const std::size_t n = std::min<std::size_t>(cuts.size(), static_cast<std::size_t>(max_indices));
        for (std::size_t i = 0; i < n; ++i) {
            out_indices[i] = cuts[i];
        }
    }
    return static_cast<int32_t>(cuts.size());
}

} // extern "C"

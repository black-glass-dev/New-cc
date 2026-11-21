#ifndef OWNCUT_TIMELINE_C_API_H
#define OWNCUT_TIMELINE_C_API_H

#include "timeline/color_hook.hpp"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OwnCutTimelineEngine OwnCutTimelineEngine;

typedef struct OwnCutBridgeClipSpec {
    const char* id;
    const char* track_id;
    const char* media_id;
    int64_t start_frame;
    int64_t duration_frames;
    int64_t source_in_ms;
    int64_t source_out_ms;
    int32_t grade_active;
} OwnCutBridgeClipSpec;

typedef struct OwnCutSnapOutcome {
    int64_t final_frame;
    int32_t snapped;
    int64_t snap_guide_frame;
    int32_t slot_adjusted;
    int32_t haptic_pulse;
} OwnCutSnapOutcome;

OwnCutTimelineEngine* owncut_tl_create(int32_t fps_num, int32_t fps_den, int32_t width, int32_t height);
void owncut_tl_destroy(OwnCutTimelineEngine* engine);

#ifdef __cplusplus
void owncut_tl_register_color_hook(OwnCutTimelineEngine* engine,
                                   ::owncut::timeline::ColorPipelineHookC hook);
#endif

uint64_t owncut_tl_add_synthetic_media(OwnCutTimelineEngine* engine,
                                       const char* name,
                                       int32_t width,
                                       int32_t height,
                                       int64_t duration_frames);

int32_t owncut_tl_perform_edit(OwnCutTimelineEngine* engine,
                               int32_t edit_mode,
                               uint64_t media_id,
                               int64_t source_in_frames,
                               int64_t source_out_frames);

int32_t owncut_tl_blade_split(OwnCutTimelineEngine* engine, int64_t frame_index);
int32_t owncut_tl_trim_clip(OwnCutTimelineEngine* engine,
                            uint64_t clip_id,
                            int32_t trim_mode,
                            int64_t delta_frames,
                            int32_t ripple,
                            int32_t safe_trimming);
int32_t owncut_tl_ripple_delete_clip(OwnCutTimelineEngine* engine, uint64_t clip_id);

int32_t owncut_tl_undo(OwnCutTimelineEngine* engine);
int32_t owncut_tl_redo(OwnCutTimelineEngine* engine);

void owncut_tl_set_playhead_frames(OwnCutTimelineEngine* engine, int64_t frame_index);
int64_t owncut_tl_get_playhead_frames(const OwnCutTimelineEngine* engine);
int64_t owncut_tl_get_duration_frames(const OwnCutTimelineEngine* engine);

int32_t owncut_tl_render_frame_rgba32f(OwnCutTimelineEngine* engine,
                                       int64_t frame_index,
                                       float* out_rgba32f,
                                       int32_t out_buffer_floats);

void owncut_tl_gesture_pinch(OwnCutTimelineEngine* engine, double scale, double focus_x_px);
double owncut_tl_gesture_pinch_scale(OwnCutTimelineEngine* engine, double scale, double focus_x_px);
void owncut_tl_reset_zoom(OwnCutTimelineEngine* engine);
double owncut_tl_get_zoom_scale(const OwnCutTimelineEngine* engine);
int32_t owncut_tl_gesture_two_finger_undo(OwnCutTimelineEngine* engine);
int32_t owncut_tl_gesture_three_finger_redo(OwnCutTimelineEngine* engine);
void owncut_tl_gesture_jog_rotate(OwnCutTimelineEngine* engine, double delta_rad);

int32_t owncut_tl_sync_clips(OwnCutTimelineEngine* engine,
                             const OwnCutBridgeClipSpec* clips,
                             int32_t clip_count);

int32_t owncut_tl_evaluate_snap(OwnCutTimelineEngine* engine,
                                const char* track_id,
                                const char* moving_clip_id,
                                int64_t proposed_start_frame,
                                int64_t duration_frames,
                                int64_t playhead_frame,
                                double pixels_per_frame,
                                int32_t snap_enabled,
                                OwnCutSnapOutcome* out_outcome);

int32_t owncut_tl_get_capabilities_json(const OwnCutTimelineEngine* engine,
                                        char* out_buffer,
                                        int32_t out_capacity);

int32_t owncut_tl_export_state_json(const OwnCutTimelineEngine* engine,
                                    char* out_buffer,
                                    int32_t out_capacity);
int32_t owncut_tl_import_state_json(OwnCutTimelineEngine* engine, const char* json);

void owncut_tl_set_snap_enabled(OwnCutTimelineEngine* engine, int32_t enabled);
int32_t owncut_tl_get_snap_enabled(const OwnCutTimelineEngine* engine);
void owncut_tl_set_beat_grid(OwnCutTimelineEngine* engine, double bpm, int64_t offset_ms);
void owncut_tl_upsert_marker(OwnCutTimelineEngine* engine,
                             const char* id,
                             int64_t position_ms,
                             const char* label,
                             const char* color_hex);
int32_t owncut_tl_remove_marker(OwnCutTimelineEngine* engine, const char* id);

int32_t owncut_tl_import_srt(OwnCutTimelineEngine* engine, const char* srt_text);
int32_t owncut_tl_export_srt(const OwnCutTimelineEngine* engine,
                             char* out_buffer,
                             int32_t out_capacity);

int32_t owncut_tl_render_adapter_frame_rgba32f(OwnCutTimelineEngine* engine,
                                               int64_t frame_index,
                                               float* out_rgba32f,
                                               int32_t out_buffer_floats);

int32_t owncut_tl_analyze_pcm16(const int16_t* pcm_interleaved,
                                int32_t sample_frames,
                                int32_t channels,
                                int32_t sample_rate,
                                int32_t num_peaks,
                                float* out_peaks,
                                double* out_bpm,
                                double* out_lufs);

int32_t owncut_tl_detect_scene_cuts(const uint8_t* rgba_frames,
                                    int32_t num_frames,
                                    int32_t width,
                                    int32_t height,
                                    double threshold,
                                    int32_t* out_indices,
                                    int32_t max_indices);

#ifdef __cplusplus
}
#endif

#endif // OWNCUT_TIMELINE_C_API_H

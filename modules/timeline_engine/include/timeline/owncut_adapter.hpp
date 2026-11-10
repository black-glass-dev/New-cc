#pragma once

#include "timeline/audio_engine.hpp"
#include "timeline/command_stack.hpp"
#include "timeline/edit_operations.hpp"
#include "timeline/mobile_ux.hpp"
#include "timeline/persistence.hpp"
#include "timeline/text_subtitle_engine.hpp"
#include "timeline/timeline_model.hpp"
#include "timeline/vision_engine.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace owncut::timeline {

struct OwncutClipSeed {
    std::string id;
    std::string track_id;
    std::string media_id;
    int64_t start_frame = 0;
    int64_t duration_frames = 0;
    int64_t source_in_ms = 0;
    int64_t source_out_ms = 0;
    bool grade_active = false;
};

struct OwncutSnapEvaluation {
    int64_t final_frame = 0;
    bool snapped = false;
    int64_t snap_guide_frame = -1;
    bool slot_adjusted = false;
    bool haptic_pulse = false;
};

struct OwncutAudioAnalysis {
    double detected_bpm = 0.0;
    double integrated_lufs = -70.0;
    double true_peak_dbtp = -120.0;
    std::vector<float> waveform_peaks;
};

class OwncutTimelineAdapter {
public:
    explicit OwncutTimelineAdapter(FrameRate fps = FrameRate::fps_30(),
                                   int32_t width = 1920,
                                   int32_t height = 1080);

    void sync_clips(const std::vector<OwncutClipSeed>& clips);

    [[nodiscard]] OwncutSnapEvaluation evaluate_snap(const std::string& track_id,
                                                     const std::string& moving_clip_id,
                                                     int64_t proposed_start_frame,
                                                     int64_t duration_frames,
                                                     int64_t playhead_frame,
                                                     double pixels_per_frame,
                                                     bool snap_enabled);

    // Rich FX, overlays, subtitles, markers, beat grid, and state persistence
    void set_snap_enabled(bool enabled);
    [[nodiscard]] bool snap_enabled() const;

    void set_beat_grid(double bpm, int64_t offset_ms = 0);
    void set_clip_fx(const RichClipFx& fx);
    bool remove_clip_fx(const std::string& clip_id);
    [[nodiscard]] OwncutRichState rich_state_copy() const;
    bool import_rich_state_json(const std::string& json);
    [[nodiscard]] std::string export_rich_state_json() const;
    void clear_rich_state();

    void upsert_marker(const RichMarker& marker);
    bool remove_marker(const std::string& marker_id);

    void upsert_overlay(const RichTextOverlay& overlay);
    bool remove_overlay(const std::string& overlay_id);

    bool import_srt(const std::string& srt_text);
    [[nodiscard]] std::string export_srt() const;

    bool render_composited_frame(int64_t frame_index,
                                 VideoFrame& out_frame,
                                 IColorPipelineHook* color_hook = nullptr,
                                 FrameMemoryPool* memory_pool = nullptr) const;

    static OwncutAudioAnalysis analyze_pcm16_audio(const int16_t* pcm_interleaved,
                                                   int32_t sample_frames,
                                                   int32_t channels,
                                                   int32_t sample_rate,
                                                   int32_t num_peaks);

    static std::vector<int32_t> detect_scene_cuts_rgba(const uint8_t* rgba_frames,
                                                       int32_t num_frames,
                                                       int32_t width,
                                                       int32_t height,
                                                       double threshold);

    double apply_pinch_zoom(double scale_factor, double focus_x_px);
    void reset_zoom();
    [[nodiscard]] double zoom_scale() const;

    [[nodiscard]] std::string capabilities_json() const;
    [[nodiscard]] int64_t duration_frames() const;
    [[nodiscard]] std::size_t clip_count() const;
    [[nodiscard]] std::size_t track_count() const;

    [[nodiscard]] uint64_t resolve_clip_id(const std::string& owncut_clip_id) const;
    [[nodiscard]] uint64_t resolve_track_id(const std::string& owncut_track_id) const;

    // Access underlying modular timeline components under external care / testing
    [[nodiscard]] const Timeline& timeline() const noexcept { return timeline_; }
    [[nodiscard]] Timeline& timeline() noexcept { return timeline_; }
    [[nodiscard]] const MediaPool& media_pool() const noexcept { return project_.media_pool(); }

private:
    uint64_t ensure_track_unlocked(const std::string& owncut_track_id);
    uint64_t ensure_media_unlocked(const std::string& media_uri, int64_t min_duration_ms);
    uint64_t ensure_clip_id_unlocked(const std::string& owncut_clip_id);
    void apply_rich_state_to_timeline_unlocked();

    mutable std::mutex mutex_;
    Project project_;
    uint64_t timeline_id_ = 1;
    Timeline timeline_;
    MagneticSnapEngine snap_engine_;
    MobileGestureController gesture_controller_;
    DualTimelineController dual_timeline_;
    OwncutRichState rich_state_;
    SubtitleEngine subtitle_engine_;

    std::unordered_map<std::string, uint64_t> track_ids_;
    std::unordered_map<std::string, uint64_t> media_ids_;
    std::unordered_map<std::string, uint64_t> clip_ids_;
    std::unordered_map<std::string, RichClipFx> fx_by_clip_id_;
    uint64_t next_stable_clip_id_ = 1;
};

} // namespace owncut::timeline

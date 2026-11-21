#pragma once

#include "timeline/command_stack.hpp"
#include "timeline/edit_operations.hpp"
#include "timeline/media_pool.hpp"
#include "timeline/timeline_model.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace owncut::timeline {

enum class StabilizationMode : uint8_t {
    Perspective = 0,
    Similarity = 1,
    Translation = 2
};

struct StabilizationSettings {
    StabilizationMode mode = StabilizationMode::Similarity;
    float smoothness = 0.65f; // 0.0 (none) .. 1.0 (very smooth)
    bool use_kalman_filter = true;
    bool camera_lock = false; // Locks frame toward initial reference pose
    bool zoom_adaptive = true;
    float max_zoom = 1.35f;
};

struct FrameMotionSample {
    int64_t frame_index = 0;
    float raw_dx = 0.0f;
    float raw_dy = 0.0f;
    float raw_dtheta = 0.0f;
    float corr_dx = 0.0f;
    float corr_dy = 0.0f;
    float corr_dtheta = 0.0f;
    float required_zoom = 1.0f;
};

struct StabilizationAnalysis {
    std::vector<FrameMotionSample> samples;
    float adaptive_zoom = 1.0f;
};

class Stabilizer {
public:
    static StabilizationAnalysis analyze_sequence(const std::vector<VideoFrame>& frames,
                                                  const StabilizationSettings& settings);
    static bool stabilize_frame(const VideoFrame& src, const FrameMotionSample& sample,
                                float zoom_factor, VideoFrame& dst,
                                FrameMemoryPool* pool = nullptr);
};

struct ReframeCropBox {
    int64_t frame_index = 0;
    float center_x_norm = 0.5f; // 0..1
    float center_y_norm = 0.5f;
    float crop_width_norm = 0.5625f;
    float crop_height_norm = 1.0f;
};

class SmartReframe {
public:
    // Computes temporally smoothed crop trajectory targeting aspect_num:aspect_den (e.g. 9:16, 1:1, 4:5)
    static std::vector<ReframeCropBox> compute_reframe_trajectory(const std::vector<VideoFrame>& frames,
                                                                  int32_t target_aspect_num,
                                                                  int32_t target_aspect_den,
                                                                  float smoothing = 0.8f);
    static bool apply_reframe(const VideoFrame& src, const ReframeCropBox& box,
                              int32_t out_width, int32_t out_height,
                              VideoFrame& dst, FrameMemoryPool* pool = nullptr);
};

struct DetectedCut {
    int64_t frame_index = 0;
    RationalTime timestamp{};
    float confidence = 0.0f;
};

struct CandidateTrimSegment {
    TimeRange range{};
    enum class Reason : uint8_t {
        StaticBoring = 0,
        JumpCut = 1,
        Silence = 2
    } reason = Reason::StaticBoring;
    float score = 0.0f;
};

class SceneAndCutDetector {
public:
    // Histogram + MAD shot-boundary detection
    static std::vector<DetectedCut> detect_scene_cuts(const std::vector<VideoFrame>& frames,
                                                      FrameRate fps,
                                                      float threshold = 0.35f);

    // Detects stagnant low-motion "boring" spans and brief jump-cut anomalies
    static std::vector<CandidateTrimSegment> detect_boring_and_jump_cuts(const std::vector<VideoFrame>& frames,
                                                                         FrameRate fps,
                                                                         float low_motion_thresh = 0.008f,
                                                                         int64_t min_boring_frames = 10);

    static bool remove_candidate_segments(Timeline& tl, CommandStack& stack,
                                          const std::vector<CandidateTrimSegment>& segments);
};

struct TranscriptWord {
    uint64_t id = 0;
    uint64_t media_id = 0;
    std::string text;
    std::string speaker = "Speaker 1";
    RationalTime source_start{};
    RationalTime source_end{};
    bool deleted = false;
};

class TextBasedEditor {
public:
    void set_words(std::vector<TranscriptWord> words) { words_ = std::move(words); }
    [[nodiscard]] const std::vector<TranscriptWord>& words() const noexcept { return words_; }
    [[nodiscard]] std::vector<TranscriptWord>& words() noexcept { return words_; }

    bool mark_word_deleted(uint64_t word_id, bool deleted = true);
    bool reorder_word_block(std::size_t from_idx, std::size_t count, std::size_t insert_before_idx);

    // Compiles surviving transcript words into a clean cut list and rebuilds/updates target track
    bool apply_edits_to_timeline(Timeline& tl, CommandStack& stack, uint64_t media_id) const;

private:
    std::vector<TranscriptWord> words_;
};

struct ScriptSegment {
    uint64_t media_id = 0;
    std::string text;
    RationalTime source_start{};
    RationalTime source_end{};
};

class IntelliScriptMatcher {
public:
    // Matches desired script lines against available transcript clips and constructs timeline
    static bool assemble_timeline_from_script(const std::vector<std::string>& script_lines,
                                              const std::vector<ScriptSegment>& transcript_pool,
                                              Timeline& out_timeline,
                                              CommandStack& stack);
};

} // namespace owncut::timeline

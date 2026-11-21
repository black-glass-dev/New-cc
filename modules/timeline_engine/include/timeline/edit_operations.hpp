#pragma once

#include "timeline/command_stack.hpp"
#include "timeline/timeline_model.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace owncut::timeline {

enum class EditMode : uint8_t {
    SmartInsert = 0,
    AppendAtEnd = 1,
    PlaceOnTop = 2,
    Overwrite = 3,
    RippleOverwrite = 4,
    Replace = 5,
    FitToFill = 6
};

struct SourceInsertSpec {
    uint64_t media_id = 0;
    std::string name;
    ClipKind kind = ClipKind::SourceClip;
    RationalTime source_in{};
    RationalTime source_out{};
    RationalTime media_duration{};
    bool include_audio = true;
    // J-cut / L-cut split offsets relative to video boundary
    RationalTime audio_in_offset{};
    RationalTime audio_out_offset{};
    std::optional<uint64_t> target_video_track_id;
    std::optional<uint64_t> target_audio_track_id;
};

struct SnapTarget {
    enum class Kind : uint8_t {
        Playhead = 0,
        ClipBoundary = 1,
        Marker = 2,
        InOutPoint = 3,
        BeatGrid = 4
    };
    Kind kind = Kind::ClipBoundary;
    RationalTime time{};
    uint64_t source_id = 0;
};

struct SnapResult {
    bool snapped = false;
    RationalTime snapped_time{};
    SnapTarget::Kind target_kind = SnapTarget::Kind::ClipBoundary;
};

class MagneticSnapEngine {
public:
    using HapticCallback = std::function<void(const SnapResult&)>;

    void set_enabled(bool e) noexcept { enabled_ = e; }
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    void set_threshold(const RationalTime& threshold) { threshold_ = threshold; }
    void set_beat_grid(std::vector<RationalTime> beats) { beat_grid_ = std::move(beats); }
    void set_haptic_callback(HapticCallback cb) { haptic_cb_ = std::move(cb); }

    [[nodiscard]] std::vector<SnapTarget> collect_targets(const Timeline& tl,
                                                          std::optional<uint64_t> exclude_clip_id = std::nullopt) const;
    [[nodiscard]] SnapResult snap(const Timeline& tl,
                                  const RationalTime& candidate,
                                  std::optional<uint64_t> exclude_clip_id = std::nullopt,
                                  bool trigger_haptic = true) const;

private:
    bool enabled_ = true;
    RationalTime threshold_ = RationalTime(100, 1000); // 100 ms snap magnet window
    std::vector<RationalTime> beat_grid_;
    HapticCallback haptic_cb_;
};

struct ClipboardPayload {
    bool has_data = false;
    std::vector<ClipItem> clips;
    RationalTime anchor_time{};
};

class EditOperations {
public:
    // Executes one of the 7 Cut/Edit modes as a reversible CommandStack command.
    static bool perform_edit(Timeline& tl, CommandStack& stack, EditMode mode, const SourceInsertSpec& spec);

    // Split/Blade
    static bool blade_split_at(Timeline& tl, CommandStack& stack, const RationalTime& time,
                               std::optional<uint64_t> clip_id = std::nullopt);

    // Ripple Delete clip or gap
    static bool ripple_delete_clip(Timeline& tl, CommandStack& stack, uint64_t clip_id);
    static bool delete_gap(Timeline& tl, CommandStack& stack, uint64_t track_id, const TimeRange& gap_range);
    static bool close_all_gaps(Timeline& tl, CommandStack& stack, std::optional<uint64_t> track_id = std::nullopt);

    // Lift (leaves gap) vs Extract (ripples closed)
    static bool lift_range(Timeline& tl, CommandStack& stack, const TimeRange& range,
                           std::optional<uint64_t> track_id = std::nullopt);
    static bool extract_range(Timeline& tl, CommandStack& stack, const TimeRange& range,
                              std::optional<uint64_t> track_id = std::nullopt);

    // Swap & Shuffle
    static bool swap_clips(Timeline& tl, CommandStack& stack, uint64_t clip_a_id, uint64_t clip_b_id);
    static bool shuffle_track_clips(Timeline& tl, CommandStack& stack, uint64_t track_id,
                                    const std::vector<uint64_t>& new_order_clip_ids);

    // Separate A/V In/Out offsets for J-cut / L-cut while maintaining link_group_id
    static bool set_av_split_offsets(Timeline& tl, CommandStack& stack, uint64_t video_clip_id,
                                     const RationalTime& audio_start_delta,
                                     const RationalTime& audio_end_delta);

    // Clipboard Cut/Copy/Paste (works within or across timelines)
    static ClipboardPayload copy_clips(const Timeline& tl, const std::vector<uint64_t>& clip_ids);
    static ClipboardPayload cut_clips(Timeline& tl, CommandStack& stack, const std::vector<uint64_t>& clip_ids,
                                      bool ripple = false);
    static bool paste_clips(Timeline& tl, CommandStack& stack, const ClipboardPayload& payload,
                            const RationalTime& target_time, bool ripple_insert = false);

    // Compound & Adjustment & Multicam creation
    static uint64_t create_compound_clip(Timeline& tl, CommandStack& stack,
                                         const std::vector<uint64_t>& clip_ids,
                                         const std::string& compound_name);
    static uint64_t switch_multicam_angle(Timeline& tl, CommandStack& stack, uint64_t clip_id,
                                          std::size_t angle_index);
};

} // namespace owncut::timeline

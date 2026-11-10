#pragma once

#include "timeline/command_stack.hpp"
#include "timeline/timeline_model.hpp"

#include <cstdint>
#include <vector>

namespace owncut::timeline {

enum class TrimMode : uint8_t {
    TrimIn = 0,
    TrimOut = 1,
    RollEdit = 2,
    SlipEdit = 3,
    SlideEdit = 4
};

struct TrimClampReport {
    RationalTime requested_delta{};
    RationalTime applied_delta{};
    bool clamped_by_media_start = false;
    bool clamped_by_media_end = false;
    bool clamped_by_neighbor = false;
    bool clamped_by_min_duration = false;
};

struct TrackTrimAction {
    uint64_t clip_id = 0;
    TrimMode mode = TrimMode::TrimOut;
    RationalTime delta{};
};

struct ABTrimHandles {
    uint64_t outgoing_clip_id = 0;
    uint64_t incoming_clip_id = 0;
    RationalTime outgoing_head_available{};
    RationalTime outgoing_tail_available{};
    RationalTime incoming_head_available{};
    RationalTime incoming_tail_available{};
    RationalTime edit_point{};
    RationalTime outgoing_frame_time{};
    RationalTime incoming_frame_time{};
};

class TrimEngine {
public:
    static constexpr int64_t kDefaultMinFrames = 1;

    static TrimClampReport trim_clip(Timeline& tl, CommandStack& stack,
                                     uint64_t clip_id, TrimMode mode,
                                     const RationalTime& delta,
                                     bool ripple = false,
                                     bool safe_trimming = true,
                                     uint64_t coalesce_key = 0);

    // Asymmetric Multi-Track Trim (supports simultaneous J/L-cut & multi-track linked adjustments)
    static bool asymmetric_multi_track_trim(Timeline& tl, CommandStack& stack,
                                            const std::vector<TrackTrimAction>& actions,
                                            bool ripple = false,
                                            bool safe_trimming = true);

    // Inspect available A/B handles for mobile Loupe & Dual-View A/B Graphical Trimmer
    static std::optional<ABTrimHandles> inspect_ab_handles(const Timeline& tl,
                                                           uint64_t outgoing_clip_id,
                                                           uint64_t incoming_clip_id);
};

// Dynamic JKL Shuttle & Loop Trimmer state machine
class DynamicJKLLoopTrim {
public:
    void set_shuttle_j(); // Reverse speed ramp (-1x -> -2x -> -4x -> -8x)
    void set_shuttle_k(); // Pause (0x)
    void set_shuttle_l(); // Forward speed ramp (+1x -> +2x -> +4x -> +8x)
    void step_k_plus_j(Timeline& tl); // 1 frame backward step
    void step_k_plus_l(Timeline& tl); // 1 frame forward step

    void configure_loop(const RationalTime& edit_point,
                        const RationalTime& pre_roll,
                        const RationalTime& post_roll);
    void set_loop_enabled(bool enabled) noexcept { loop_enabled_ = enabled; }
    void tick(Timeline& tl, const RationalTime& host_dt);

    [[nodiscard]] double current_speed() const noexcept { return speed_; }
    [[nodiscard]] bool loop_enabled() const noexcept { return loop_enabled_; }

private:
    double speed_ = 0.0;
    bool loop_enabled_ = false;
    RationalTime loop_center_{};
    RationalTime pre_roll_ = RationalTime(2, 1);
    RationalTime post_roll_ = RationalTime(2, 1);
};

} // namespace owncut::timeline

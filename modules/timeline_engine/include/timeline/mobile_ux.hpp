#pragma once

#include "timeline/command_stack.hpp"
#include "timeline/edit_operations.hpp"
#include "timeline/timeline_model.hpp"
#include "timeline/trim_engine.hpp"

#include <cstdint>
#include <functional>

namespace owncut::timeline {

enum class OrientationMode : uint8_t {
    Portrait = 0,
    Landscape = 1
};

enum class HapticPulseType : uint8_t {
    SnapTick = 0,
    JogDetent = 1,
    UndoConfirm = 2,
    RedoConfirm = 3,
    LoupeLock = 4,
    TrimLimitHit = 5
};

struct LayoutRect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

struct MobileLayoutGeometry {
    OrientationMode orientation = OrientationMode::Portrait;
    LayoutRect viewer_rect{};
    LayoutRect overview_bar_rect{};
    LayoutRect timeline_tracks_rect{};
    LayoutRect toolbar_rect{};
    LayoutRect keyframe_tray_rect{};
    bool side_by_side_inspector = false;
};

enum class LoupeTrimEdge : uint8_t {
    InEdge = 0,
    OutEdge = 1
};

class MobileGestureController {
public:
    using HapticCallback = std::function<void(HapticPulseType)>;

    void set_haptic_callback(HapticCallback cb) { haptic_cb_ = std::move(cb); }

    // Responsive Portrait vs Landscape mobile layout solver
    static MobileLayoutGeometry compute_layout(float screen_w, float screen_h,
                                               bool keyframe_tray_open = false,
                                               bool inspector_open = false);

    // Pinch-to-Zoom timeline scale (clamped [10 px/s, 1200 px/s])
    void on_pinch_zoom(double scale_factor, double focus_x_px);
    void on_pan_scroll(double delta_x_px);

    // Two-Finger Tap -> Undo, Three-Finger Tap -> Redo
    bool on_two_finger_tap(Timeline& tl, CommandStack& stack);
    bool on_three_finger_tap(Timeline& tl, CommandStack& stack);

    // Virtual Jog Dial rotation with haptic detents
    void on_jog_rotate(Timeline& tl, double delta_radians, double detent_radians = 0.15);

    // Long-Press Loupe Trim (high-precision zoomed scrubbing/trimming with Safe Trimming Mode)
    bool begin_long_press_loupe(const Timeline& tl, uint64_t clip_id, LoupeTrimEdge edge);
    TrimClampReport drag_loupe_trim(Timeline& tl, CommandStack& stack,
                                    double delta_px, bool ripple = false, bool safe_trimming = true);
    void end_long_press_loupe();

    [[nodiscard]] double pixels_per_second() const noexcept { return pixels_per_second_; }
    [[nodiscard]] double scroll_offset_sec() const noexcept { return scroll_offset_sec_; }
    [[nodiscard]] bool loupe_active() const noexcept { return loupe_active_; }
    [[nodiscard]] double loupe_magnification() const noexcept { return loupe_active_ ? 3.0 : 1.0; }

private:
    void emit_haptic(HapticPulseType pulse) const;

    HapticCallback haptic_cb_;
    double pixels_per_second_ = 80.0;
    double scroll_offset_sec_ = 0.0;
    double jog_accum_rad_ = 0.0;

    bool loupe_active_ = false;
    uint64_t loupe_clip_id_ = 0;
    LoupeTrimEdge loupe_edge_ = LoupeTrimEdge::OutEdge;
    uint64_t loupe_coalesce_key_ = 9001;
};

} // namespace owncut::timeline

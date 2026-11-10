#include "timeline/mobile_ux.hpp"

#include <algorithm>
#include <cmath>

namespace owncut::timeline {

MobileLayoutGeometry MobileGestureController::compute_layout(float screen_w, float screen_h,
                                                             bool keyframe_tray_open,
                                                             bool inspector_open) {
    MobileLayoutGeometry geom;
    const bool landscape = screen_w >= screen_h;
    geom.orientation = landscape ? OrientationMode::Landscape : OrientationMode::Portrait;
    geom.side_by_side_inspector = landscape && inspector_open;

    if (!landscape) {
        const float viewer_h = screen_h * 0.34f;
        const float toolbar_h = 52.0f;
        const float overview_h = 34.0f;
        const float tray_h = keyframe_tray_open ? screen_h * 0.16f : 0.0f;
        const float tracks_h = std::max(80.0f, screen_h - viewer_h - toolbar_h - overview_h - tray_h);

        geom.viewer_rect = LayoutRect{0.0f, 0.0f, screen_w, viewer_h};
        geom.overview_bar_rect = LayoutRect{0.0f, viewer_h, screen_w, overview_h};
        geom.timeline_tracks_rect = LayoutRect{0.0f, viewer_h + overview_h, screen_w, tracks_h};
        geom.keyframe_tray_rect = LayoutRect{0.0f, viewer_h + overview_h + tracks_h, screen_w, tray_h};
        geom.toolbar_rect = LayoutRect{0.0f, screen_h - toolbar_h, screen_w, toolbar_h};
    } else {
        const float viewer_w = inspector_open ? screen_w * 0.50f : screen_w * 0.56f;
        const float viewer_h = screen_h * 0.44f;
        const float toolbar_h = 48.0f;
        const float overview_h = 28.0f;
        const float tray_h = keyframe_tray_open ? screen_h * 0.14f : 0.0f;
        const float tracks_y = viewer_h + overview_h;
        const float tracks_h = std::max(80.0f, screen_h - tracks_y - toolbar_h - tray_h);

        geom.viewer_rect = LayoutRect{0.0f, 0.0f, viewer_w, viewer_h};
        geom.overview_bar_rect = LayoutRect{0.0f, viewer_h, screen_w, overview_h};
        geom.timeline_tracks_rect = LayoutRect{0.0f, tracks_y, screen_w, tracks_h};
        geom.keyframe_tray_rect = LayoutRect{0.0f, tracks_y + tracks_h, screen_w, tray_h};
        geom.toolbar_rect = LayoutRect{0.0f, screen_h - toolbar_h, screen_w, toolbar_h};
    }
    return geom;
}

void MobileGestureController::emit_haptic(HapticPulseType pulse) const {
    if (haptic_cb_) haptic_cb_(pulse);
}

void MobileGestureController::on_pinch_zoom(double scale_factor, double focus_x_px) {
    if (scale_factor <= 0.0) return;
    const double old_pps = pixels_per_second_;
    const double focus_time = scroll_offset_sec_ + std::max(0.0, focus_x_px) / old_pps;
    pixels_per_second_ = std::clamp(old_pps * scale_factor, 10.0, 1200.0);
    scroll_offset_sec_ = std::max(0.0, focus_time - std::max(0.0, focus_x_px) / pixels_per_second_);
}

void MobileGestureController::on_pan_scroll(double delta_x_px) {
    scroll_offset_sec_ = std::max(0.0, scroll_offset_sec_ - delta_x_px / pixels_per_second_);
}

bool MobileGestureController::on_two_finger_tap(Timeline& tl, CommandStack& stack) {
    const bool ok = stack.undo(tl);
    if (ok) emit_haptic(HapticPulseType::UndoConfirm);
    return ok;
}

bool MobileGestureController::on_three_finger_tap(Timeline& tl, CommandStack& stack) {
    const bool ok = stack.redo(tl);
    if (ok) emit_haptic(HapticPulseType::RedoConfirm);
    return ok;
}

void MobileGestureController::on_jog_rotate(Timeline& tl, double delta_radians, double detent_radians) {
    if (detent_radians <= 0.0) detent_radians = 0.15;
    jog_accum_rad_ += delta_radians;
    const int steps = static_cast<int>(std::trunc(jog_accum_rad_ / detent_radians));
    if (steps != 0) {
        jog_accum_rad_ -= static_cast<double>(steps) * detent_radians;
        const RationalTime frame_step = RationalTime::from_frames(std::abs(steps), tl.fps());
        if (steps > 0) {
            tl.set_playhead(tl.playhead() + frame_step);
        } else {
            tl.set_playhead(tl.playhead() > frame_step ? (tl.playhead() - frame_step) : RationalTime::zero());
        }
        emit_haptic(HapticPulseType::JogDetent);
    }
}

bool MobileGestureController::begin_long_press_loupe(const Timeline& tl, uint64_t clip_id, LoupeTrimEdge edge) {
    if (!tl.find_clip(clip_id)) return false;
    loupe_active_ = true;
    loupe_clip_id_ = clip_id;
    loupe_edge_ = edge;
    ++loupe_coalesce_key_;
    emit_haptic(HapticPulseType::LoupeLock);
    return true;
}

TrimClampReport MobileGestureController::drag_loupe_trim(Timeline& tl, CommandStack& stack,
                                                         double delta_px, bool ripple, bool safe_trimming) {
    if (!loupe_active_) return TrimClampReport{};
    // Loupe precision multiplier slows pointer sensitivity by 3x
    const double dt_sec = delta_px / (pixels_per_second_ * 3.0);
    const RationalTime delta = RationalTime::from_seconds_approx(dt_sec, 1000).quantized_to_frame(tl.fps());
    if (delta.num() == 0) return TrimClampReport{};

    const TrimMode mode = (loupe_edge_ == LoupeTrimEdge::InEdge) ? TrimMode::TrimIn : TrimMode::TrimOut;
    const TrimClampReport rep = TrimEngine::trim_clip(tl, stack, loupe_clip_id_, mode, delta,
                                                      ripple, safe_trimming, loupe_coalesce_key_);
    if (rep.clamped_by_media_start || rep.clamped_by_media_end ||
        rep.clamped_by_neighbor || rep.clamped_by_min_duration) {
        emit_haptic(HapticPulseType::TrimLimitHit);
    }
    return rep;
}

void MobileGestureController::end_long_press_loupe() {
    loupe_active_ = false;
    loupe_clip_id_ = 0;
}

} // namespace owncut::timeline

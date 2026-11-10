#include "test_harness.hpp"
#include "timeline/mobile_ux.hpp"
#include "../bridge/timeline_c_api.h"

#include <atomic>
#include <cmath>
#include <vector>

using namespace owncut::timeline;

namespace {
struct HookCallStats {
    int clip_calls = 0;
    int timeline_calls = 0;
    uint64_t last_clip_id = 0;
    uint64_t last_track_id = 0;
    uint64_t last_group_id = 0;
};

bool mock_color_process_fn(FrameBufferView32F* frame, ColorPassScopeC scope, void* user_data) {
    auto* st = static_cast<HookCallStats*>(user_data);
    if (scope == OWNCUT_COLOR_PASS_CLIP) {
        ++st->clip_calls;
        st->last_clip_id = frame->clip_id;
        st->last_track_id = frame->track_id;
        st->last_group_id = frame->group_id;
        // Scale RGB by 0.5x to simulate ColorEngine clip-grade node
        for (int32_t y = 0; y < frame->height; ++y) {
            float* row = frame->rgba32f + y * frame->stride;
            for (int32_t x = 0; x < frame->width; ++x) {
                row[x * 4 + 0] *= 0.5f;
                row[x * 4 + 1] *= 0.5f;
                row[x * 4 + 2] *= 0.5f;
            }
        }
    } else if (scope == OWNCUT_COLOR_PASS_TIMELINE) {
        ++st->timeline_calls;
        // Offset RGB by +0.1 to simulate Timeline/Adjustment grade
        for (int32_t y = 0; y < frame->height; ++y) {
            float* row = frame->rgba32f + y * frame->stride;
            for (int32_t x = 0; x < frame->width; ++x) {
                row[x * 4 + 0] += 0.1f;
                row[x * 4 + 1] += 0.1f;
                row[x * 4 + 2] += 0.1f;
            }
        }
    }
    return true;
}

bool mock_color_bypass_fn(uint64_t, uint64_t, void*) {
    return false;
}
} // namespace

OCU_TEST(MobileGestureLayoutPinchJogLoupeAndHaptics) {
    const auto portrait = MobileGestureController::compute_layout(1080.0f, 2400.0f, true, true);
    const auto landscape = MobileGestureController::compute_layout(2400.0f, 1080.0f, true, true);
    OCU_ASSERT_EQ(portrait.orientation, OrientationMode::Portrait);
    OCU_ASSERT_EQ(landscape.orientation, OrientationMode::Landscape);
    OCU_ASSERT_TRUE(!portrait.side_by_side_inspector);
    OCU_ASSERT_TRUE(landscape.side_by_side_inspector);

    Timeline tl(1, "Mobile", FrameRate::fps_30(), 64, 36);
    CommandStack stack;
    SourceInsertSpec sp;
    sp.media_id = 1;
    sp.name = "M1";
    sp.source_in = RationalTime(1, 1);
    sp.source_out = RationalTime(5, 1);
    sp.media_duration = RationalTime(8, 1);
    OCU_ASSERT_TRUE(EditOperations::perform_edit(tl, stack, EditMode::AppendAtEnd, sp));
    const uint64_t cid = tl.primary_track(TrackType::Video)->clips[0].id;

    MobileGestureController gestures;
    int jog_haptics = 0;
    int undo_haptics = 0;
    int loupe_haptics = 0;
    gestures.set_haptic_callback([&](HapticPulseType p) {
        if (p == HapticPulseType::JogDetent) ++jog_haptics;
        if (p == HapticPulseType::UndoConfirm) ++undo_haptics;
        if (p == HapticPulseType::LoupeLock) ++loupe_haptics;
    });

    const double old_pps = gestures.pixels_per_second();
    gestures.on_pinch_zoom(2.0, 200.0);
    OCU_ASSERT_NEAR(gestures.pixels_per_second(), old_pps * 2.0, 1e-6);

    tl.set_playhead(RationalTime::zero());
    gestures.on_jog_rotate(tl, 0.45, 0.15); // +3 frames
    OCU_ASSERT_EQ(tl.playhead().to_frames(tl.fps()), 3);
    OCU_ASSERT_EQ(jog_haptics, 1);

    OCU_ASSERT_TRUE(gestures.begin_long_press_loupe(tl, cid, LoupeTrimEdge::OutEdge));
    OCU_ASSERT_EQ(loupe_haptics, 1);
    OCU_ASSERT_NEAR(gestures.loupe_magnification(), 3.0, 1e-6);
    auto rep = gestures.drag_loupe_trim(tl, stack, 160.0, true, true);
    OCU_ASSERT_TRUE(rep.applied_delta.num() > 0);
    gestures.end_long_press_loupe();

    // Two-finger undo / Three-finger redo
    const RationalTime trimmed_dur = tl.duration();
    OCU_ASSERT_TRUE(gestures.on_two_finger_tap(tl, stack));
    OCU_ASSERT_EQ(undo_haptics, 1);
    OCU_ASSERT_TRUE(tl.duration() < trimmed_dur);
    OCU_ASSERT_TRUE(gestures.on_three_finger_tap(tl, stack));
    OCU_ASSERT_EQ(tl.duration(), trimmed_dur);
}

OCU_TEST(EndToEndCApiAndColorHookIntegration) {
    OwnCutTimelineEngine* eng = owncut_tl_create(30, 1, 32, 18);
    OCU_ASSERT_TRUE(eng != nullptr);

    HookCallStats stats{};
    ColorPipelineHookC hook{};
    hook.process_frame = &mock_color_process_fn;
    hook.is_bypassed = &mock_color_bypass_fn;
    hook.hook_name = "MockColorEngine";
    hook.user_data = &stats;
    owncut_tl_register_color_hook(eng, hook);

    const uint64_t mid = owncut_tl_add_synthetic_media(eng, "HeroShot", 32, 18, 120);
    OCU_ASSERT_TRUE(mid != 0);

    // AppendAtEnd (edit_mode = 1) 0..60 frames (2 sec)
    OCU_ASSERT_EQ(owncut_tl_perform_edit(eng, 1, mid, 0, 60), 1);
    OCU_ASSERT_EQ(owncut_tl_get_duration_frames(eng), 60);

    // Blade split at frame 30 + Undo + Redo via gesture C API
    OCU_ASSERT_EQ(owncut_tl_blade_split(eng, 30), 1);
    OCU_ASSERT_EQ(owncut_tl_gesture_two_finger_undo(eng), 1);
    OCU_ASSERT_EQ(owncut_tl_gesture_three_finger_redo(eng), 1);

    // Render frame 15 and confirm ColorPipelineHookC processed clip + timeline passes!
    std::vector<float> rgba(32 * 18 * 4, -1.0f);
    OCU_ASSERT_EQ(owncut_tl_render_frame_rgba32f(eng, 15, rgba.data(), static_cast<int32_t>(rgba.size())), 1);
    OCU_ASSERT_EQ(stats.clip_calls, 1);
    OCU_ASSERT_EQ(stats.timeline_calls, 1);
    OCU_ASSERT_TRUE(stats.last_clip_id != 0);
    OCU_ASSERT_TRUE(stats.last_track_id != 0);
    // Original channels are in [0,1]; after *0.5 + 0.1, all RGB channels must sit cleanly in [0.1, 0.65].
    OCU_ASSERT_TRUE(rgba[0] >= 0.09f && rgba[0] <= 0.65f);
    OCU_ASSERT_NEAR(rgba[3], 1.0f, 1e-4);

    owncut_tl_destroy(eng);
}

int main() {
    return ::owncut::test::run_all("Phase4_E2E_Bridges");
}

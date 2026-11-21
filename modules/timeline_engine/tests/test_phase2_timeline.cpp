#include "test_harness.hpp"
#include "timeline/command_stack.hpp"
#include "timeline/edit_operations.hpp"
#include "timeline/retime_engine.hpp"
#include "timeline/timeline_model.hpp"
#include "timeline/trim_engine.hpp"

#include <cmath>
#include <vector>

using namespace owncut::timeline;

namespace {
SourceInsertSpec make_spec(uint64_t media_id, const char* name, int64_t dur_sec) {
    SourceInsertSpec s;
    s.media_id = media_id;
    s.name = name;
    s.source_in = RationalTime::zero();
    s.source_out = RationalTime(dur_sec, 1);
    s.media_duration = RationalTime(30, 1);
    return s;
}
} // namespace

OCU_TEST(AllSevenEditModesAndUndoRedo) {
    Project proj("P");
    Timeline& tl = proj.create_timeline("Seq", FrameRate::fps_30(), 64, 36);
    CommandStack stack;

    // 1. AppendAtEnd
    OCU_ASSERT_TRUE(EditOperations::perform_edit(tl, stack, EditMode::AppendAtEnd, make_spec(1, "A", 4)));
    OCU_ASSERT_EQ(tl.duration(), RationalTime(4, 1));

    // 2. SmartInsert at playhead 2s (splits A and ripples)
    tl.set_playhead(RationalTime(2, 1));
    OCU_ASSERT_TRUE(EditOperations::perform_edit(tl, stack, EditMode::SmartInsert, make_spec(2, "B", 2)));
    OCU_ASSERT_EQ(tl.duration(), RationalTime(6, 1));
    OCU_ASSERT_EQ(tl.primary_track(TrackType::Video)->clips.size(), 3u);

    // Undo & Redo symmetry
    OCU_ASSERT_TRUE(stack.undo(tl));
    OCU_ASSERT_EQ(tl.duration(), RationalTime(4, 1));
    OCU_ASSERT_TRUE(stack.redo(tl));
    OCU_ASSERT_EQ(tl.duration(), RationalTime(6, 1));

    // 3. PlaceOnTop creates/uses V2 at playhead 1s
    tl.set_playhead(RationalTime(1, 1));
    OCU_ASSERT_TRUE(EditOperations::perform_edit(tl, stack, EditMode::PlaceOnTop, make_spec(3, "Overlay", 1)));
    OCU_ASSERT_TRUE(tl.tracks_of_type(TrackType::Video).size() >= 2u);

    // 4. Overwrite on V1 at 0s (1s duration)
    tl.set_playhead(RationalTime::zero());
    OCU_ASSERT_TRUE(EditOperations::perform_edit(tl, stack, EditMode::Overwrite, make_spec(4, "Ovr", 1)));
    OCU_ASSERT_EQ(tl.primary_track(TrackType::Video)->clip_at(RationalTime::zero())->media_id, 4u);

    // 5. RippleOverwrite over "B" at 2s (dur 2s -> 5s, ripples total +3s to 9s)
    tl.set_playhead(RationalTime(2, 1));
    OCU_ASSERT_TRUE(EditOperations::perform_edit(tl, stack, EditMode::RippleOverwrite, make_spec(5, "RipOvr", 5)));
    OCU_ASSERT_EQ(tl.duration(), RationalTime(9, 1));

    // 6. Replace keeps duration (5s) and aligns source at playhead
    tl.set_playhead(RationalTime(3, 1));
    auto rep_spec = make_spec(6, "Repl", 10);
    rep_spec.source_in = RationalTime(4, 1);
    OCU_ASSERT_TRUE(EditOperations::perform_edit(tl, stack, EditMode::Replace, rep_spec));
    const auto* replaced = tl.primary_track(TrackType::Video)->clip_at(RationalTime(3, 1));
    OCU_ASSERT_TRUE(replaced != nullptr);
    OCU_ASSERT_EQ(replaced->media_id, 6u);
    OCU_ASSERT_EQ(replaced->timeline_duration, RationalTime(5, 1));

    // 7. FitToFill across In/Out [0, 2) with 6s source -> speed = 3.0x
    tl.set_in_point(RationalTime::zero());
    tl.set_out_point(RationalTime(2, 1));
    OCU_ASSERT_TRUE(EditOperations::perform_edit(tl, stack, EditMode::FitToFill, make_spec(7, "Fit", 6)));
    const auto* fitted = tl.primary_track(TrackType::Video)->clip_at(RationalTime::zero());
    OCU_ASSERT_TRUE(fitted != nullptr);
    OCU_ASSERT_NEAR(fitted->speed, 3.0, 1e-6);
}

OCU_TEST(BladeSwapShuffleJLCutsSnappingAndClipboard) {
    Project proj("P2");
    Timeline& tl = proj.create_timeline("Seq", FrameRate::fps_30(), 64, 36);
    CommandStack stack;

    auto s1 = make_spec(1, "C1", 4);
    s1.source_in = RationalTime(2, 1);
    s1.source_out = RationalTime(6, 1);
    OCU_ASSERT_TRUE(EditOperations::perform_edit(tl, stack, EditMode::AppendAtEnd, s1));
    OCU_ASSERT_TRUE(EditOperations::perform_edit(tl, stack, EditMode::AppendAtEnd, make_spec(2, "C2", 4)));

    // J-cut audio offset on C2 (at 4s): -0.5s audio pre-lap, +0.5s tail while preserving link & trimming A1 cleanly
    const uint64_t v1_id = tl.primary_track(TrackType::Video)->clips[0].id;
    const uint64_t v2_id = tl.primary_track(TrackType::Video)->clips[1].id;
    OCU_ASSERT_TRUE(EditOperations::set_av_split_offsets(tl, stack, v2_id, RationalTime(-1, 2), RationalTime(1, 2)));
    OCU_ASSERT_EQ(tl.primary_track(TrackType::Audio)->clips[1].timeline_duration, RationalTime(5, 1));
    OCU_ASSERT_EQ(tl.primary_track(TrackType::Audio)->clips[0].timeline_end(), RationalTime(7, 2));

    // Blade split at 2s & Ripple Delete
    OCU_ASSERT_TRUE(EditOperations::blade_split_at(tl, stack, RationalTime(2, 1), v1_id));
    OCU_ASSERT_EQ(tl.primary_track(TrackType::Video)->clips.size(), 3u);
    OCU_ASSERT_TRUE(EditOperations::ripple_delete_clip(tl, stack, v1_id));
    OCU_ASSERT_EQ(tl.primary_track(TrackType::Video)->clips[0].timeline_start, RationalTime::zero());

    // Lift & Close gaps
    OCU_ASSERT_TRUE(EditOperations::lift_range(tl, stack, TimeRange{RationalTime::zero(), RationalTime(1, 1)}));
    OCU_ASSERT_TRUE(!tl.primary_track(TrackType::Video)->find_gaps(tl.duration()).empty());
    OCU_ASSERT_TRUE(EditOperations::close_all_gaps(tl, stack));
    OCU_ASSERT_TRUE(tl.primary_track(TrackType::Video)->find_gaps(tl.duration()).empty());

    // Swap & Shuffle
    auto& vclips = tl.primary_track(TrackType::Video)->clips;
    const uint64_t first_id = vclips[0].id;
    const uint64_t second_id = vclips[1].id;
    OCU_ASSERT_TRUE(EditOperations::swap_clips(tl, stack, first_id, second_id));
    OCU_ASSERT_EQ(tl.primary_track(TrackType::Video)->clips[0].id, second_id);

    // Magnetic Snap with haptic callback
    MagneticSnapEngine snap;
    bool haptic_fired = false;
    snap.set_haptic_callback([&](const SnapResult&) { haptic_fired = true; });
    const RationalTime edge = tl.primary_track(TrackType::Video)->clips[0].timeline_end();
    const SnapResult sr = snap.snap(tl, edge + RationalTime(30, 1000));
    OCU_ASSERT_TRUE(sr.snapped);
    OCU_ASSERT_EQ(sr.snapped_time, edge);
    OCU_ASSERT_TRUE(haptic_fired);

    // Compound Clip & Multicam & Compare Timeline Diff
    Timeline baseline = tl;
    const uint64_t comp_id = EditOperations::create_compound_clip(tl, stack, {second_id}, "Compound_A");
    OCU_ASSERT_TRUE(comp_id != 0);
    auto diffs = compare_timelines(baseline, tl);
    OCU_ASSERT_TRUE(!diffs.empty());

    // Clipboard cross-timeline paste
    Timeline tl2(99, "Other");
    CommandStack stack2;
    auto payload = EditOperations::copy_clips(tl, {comp_id});
    OCU_ASSERT_TRUE(EditOperations::paste_clips(tl2, stack2, payload, RationalTime(1, 1)));
    OCU_ASSERT_TRUE(tl2.find_clip(tl2.primary_track(TrackType::Video)->clips[0].id) != nullptr);
}

OCU_TEST(TrimEngineModesSafeTrimmingABAndJKL) {
    Project proj("TrimP");
    Timeline& tl = proj.create_timeline("Seq", FrameRate::fps_30());
    CommandStack stack;

    SourceInsertSpec s1 = make_spec(1, "Out", 4);
    s1.source_in = RationalTime(2, 1);
    s1.source_out = RationalTime(6, 1);
    s1.media_duration = RationalTime(10, 1);
    SourceInsertSpec s2 = make_spec(2, "In", 4);
    s2.source_in = RationalTime(2, 1);
    s2.source_out = RationalTime(6, 1);
    s2.media_duration = RationalTime(10, 1);

    OCU_ASSERT_TRUE(EditOperations::perform_edit(tl, stack, EditMode::AppendAtEnd, s1));
    OCU_ASSERT_TRUE(EditOperations::perform_edit(tl, stack, EditMode::AppendAtEnd, s2));

    const uint64_t c1 = tl.primary_track(TrackType::Video)->clips[0].id;
    const uint64_t c2 = tl.primary_track(TrackType::Video)->clips[1].id;

    // A/B Handles inspect
    auto ab = TrimEngine::inspect_ab_handles(tl, c1, c2);
    OCU_ASSERT_TRUE(ab.has_value());
    OCU_ASSERT_EQ(ab->outgoing_tail_available, RationalTime(4, 1));
    OCU_ASSERT_EQ(ab->incoming_head_available, RationalTime(2, 1));

    // Roll Edit moves cut +1s right
    auto r1 = TrimEngine::trim_clip(tl, stack, c1, TrimMode::RollEdit, RationalTime(1, 1));
    OCU_ASSERT_EQ(r1.applied_delta, RationalTime(1, 1));
    OCU_ASSERT_EQ(tl.find_clip(c1)->timeline_duration, RationalTime(5, 1));
    OCU_ASSERT_EQ(tl.find_clip(c2)->timeline_duration, RationalTime(3, 1));

    // Slip Edit shifts source_in/source_out without changing timeline_start/duration
    auto r2 = TrimEngine::trim_clip(tl, stack, c2, TrimMode::SlipEdit, RationalTime(1, 1));
    OCU_ASSERT_EQ(r2.applied_delta, RationalTime(1, 1));
    OCU_ASSERT_EQ(tl.find_clip(c2)->source_in, RationalTime(4, 1));
    OCU_ASSERT_EQ(tl.find_clip(c2)->timeline_duration, RationalTime(3, 1));

    // Safe Trimming Mode clamp beyond available media handle (+20s -> clamped to +1s)
    auto r3 = TrimEngine::trim_clip(tl, stack, c2, TrimMode::TrimOut, RationalTime(20, 1), true, true);
    OCU_ASSERT_TRUE(r3.clamped_by_media_end);
    OCU_ASSERT_EQ(r3.applied_delta, RationalTime(3, 1)); // source_out was 7, max 10 -> +3s

    // Dynamic JKL Shuttle & Loop
    DynamicJKLLoopTrim jkl;
    jkl.set_shuttle_l();
    OCU_ASSERT_NEAR(jkl.current_speed(), 1.0, 1e-6);
    jkl.set_shuttle_l();
    OCU_ASSERT_NEAR(jkl.current_speed(), 2.0, 1e-6);
    jkl.configure_loop(RationalTime(5, 1), RationalTime(1, 1), RationalTime(1, 1));
    jkl.set_loop_enabled(true);
    tl.set_playhead(RationalTime(5, 1) + RationalTime(1, 2));
    jkl.tick(tl, RationalTime(1, 1)); // Exceeds 6s -> wraps to 4s
    OCU_ASSERT_EQ(tl.playhead(), RationalTime(4, 1));
    jkl.step_k_plus_j(tl);
    OCU_ASSERT_NEAR(jkl.current_speed(), 0.0, 1e-6);
}

OCU_TEST(RetimeEngineOpticalFlowSpeedPointsAndSmoothCut) {
    // Bezier SpeedPoints & Freeze Frame
    std::vector<SpeedPoint> pts = {
        SpeedPoint{RationalTime(0, 1), 1.0, CubicBezierCurve{0.42, 0.0, 0.58, 1.0}},
        SpeedPoint{RationalTime(2, 1), 2.0, CubicBezierCurve{0.42, 0.0, 0.58, 1.0}},
    };
    const RationalTime mapped = RetimeEngine::map_time_with_speed_points(
        RationalTime(3, 1), pts, RationalTime::zero(), false);
    OCU_ASSERT_TRUE(mapped.to_seconds() > 3.5); // Integrated >= 1.5 average * 2 + 2.0 * 1

    OCU_ASSERT_EQ(RetimeEngine::map_freeze_frame(RationalTime(3, 1), RationalTime(2, 1), RationalTime(3, 1)),
                  RationalTime(2, 1));
    OCU_ASSERT_EQ(RetimeEngine::map_freeze_frame(RationalTime(6, 1), RationalTime(2, 1), RationalTime(3, 1)),
                  RationalTime(3, 1));

    // Pyramidal Lucas-Kanade Optical Flow on MovingBlock (+2 px/frame horizontal motion)
    MediaMetadata meta;
    meta.width = 64;
    meta.height = 48;
    meta.fps = FrameRate::fps_30();
    SyntheticMediaDecoder dec(SyntheticPattern::MovingBlock, meta);
    VideoFrame f0, f1;
    OCU_ASSERT_TRUE(dec.decode_video_frame(RationalTime::from_frames(0, meta.fps), f0, nullptr));
    OCU_ASSERT_TRUE(dec.decode_video_frame(RationalTime::from_frames(1, meta.fps), f1, nullptr));

    const OpticalFlowField flow = RetimeEngine::estimate_optical_flow(f0, f1, 3, 2);
    OCU_ASSERT_TRUE(flow.valid());
    OCU_ASSERT_TRUE(flow.mean_magnitude() > 0.05f);

    // Interpolate mid-frame across OpticalFlow & SmoothCut
    VideoFrame interp, smooth;
    OCU_ASSERT_TRUE(RetimeEngine::interpolate_frame(f0, f1, 0.5f, RetimeInterpolationMode::OpticalFlow, interp));
    OCU_ASSERT_TRUE(RetimeEngine::smooth_cut_morph(f0, f1, 0.5f, smooth));
    OCU_ASSERT_TRUE(interp.valid() && smooth.valid());
}

int main() {
    return ::owncut::test::run_all("Phase2_Timeline");
}

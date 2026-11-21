#include "editor/session_coordinator.hpp"
#include "editor/command_history.hpp"
#include "timeline/owncut_adapter.hpp"
#include "timeline_c_api.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

std::vector<owncut::timeline::OwncutClipSeed> project_to_seeds(const editor::SessionCoordinator& session) {
    std::vector<owncut::timeline::OwncutClipSeed> seeds;
    std::size_t offset = 0;
    std::optional<std::uint64_t> revision;
    for (;;) {
        const auto page = session.clip_page(offset, editor::SessionCoordinator::max_page_size, revision);
        revision = page.revision;
        for (const auto& loc : page.clips) {
            const auto start_frame = loc.clip.timeline_start.rescaled({1, 30}).ticks;
            const auto dur_frames = (loc.clip.source_out - loc.clip.source_in).rescaled({1, 30}).ticks;
            const auto in_ms = loc.clip.source_in.rescaled({1, 1000}).ticks;
            const auto out_ms = loc.clip.source_out.rescaled({1, 1000}).ticks;
            const bool active = !editor::color::Pipeline(loc.clip.grade).identity();
            seeds.push_back(owncut::timeline::OwncutClipSeed{
                loc.clip.id,
                loc.track_id,
                loc.clip.media_id,
                start_frame,
                dur_frames,
                in_ms,
                out_ms,
                active,
            });
        }
        offset = page.next_offset;
        if (offset == page.total) break;
    }
    return seeds;
}

} // namespace

int main() {
    editor::SessionCoordinator session("integration-proj", {1, 30});
    const auto clip1 = session.add_media("content://media/first.mp4", 3000);  // 0..90 frames
    const auto clip2 = session.add_media("content://media/second.mp4", 2000); // 90..150 frames
    session.execute(std::make_unique<editor::MoveClipCommand>(clip2, "v1", editor::Timestamp{120, {1, 30}}));

    owncut::timeline::OwncutTimelineAdapter adapter(
        owncut::timeline::FrameRate::fps_30(), 1920, 1080);
    adapter.sync_clips(project_to_seeds(session));

    assert(adapter.clip_count() == 2);
    assert(adapter.duration_frames() == 180);

    // Drag clip2 near clip1 end (90 frames) -> magnetic snap recommends frame 90
    const auto snap = adapter.evaluate_snap("v1", clip2, 92, 60, 0, 1.6, true);
    assert(snap.snapped);
    assert(snap.final_frame == 90);
    assert(snap.snap_guide_frame == 90);

    // Apply snapped move into SessionCoordinator & undo cleanly
    session.execute(std::make_unique<editor::MoveClipCommand>(
        clip2, "v1", editor::Timestamp{snap.final_frame, {1, 30}}));
    adapter.sync_clips(project_to_seeds(session));
    assert(adapter.duration_frames() == 150);

    assert(session.undo());
    adapter.sync_clips(project_to_seeds(session));
    assert(adapter.duration_frames() == 180);

    // Multi-track V2/A2 creation, clip placement, track state & undo
    session.ensure_track("v2", editor::TrackKind::video);
    session.ensure_track("a2", editor::TrackKind::audio);
    const auto ov_clip = session.add_media_to_track("v2", "content://media/overlay.mp4", 1500, editor::Timestamp{30, {1, 30}});
    const auto bgm_clip = session.add_media_to_track("a2", "content://audio/bgm.m4a", 4000);
    assert(!ov_clip.empty() && !bgm_clip.empty());
    adapter.sync_clips(project_to_seeds(session));
    assert(adapter.track_count() >= 4);
    assert(adapter.clip_count() == 4);
    session.set_track_state("a2", true, false);
    assert(session.snapshot().project.timeline.tracks().back().muted);
    assert(session.undo()); // undoes bgm_clip add

    // Verify C API handle lifecycle + zoom state + rich state roundtrip
    OwnCutTimelineEngine* eng = owncut_tl_create(30, 1, 1920, 1080);
    assert(eng != nullptr);
    const double z = owncut_tl_gesture_pinch_scale(eng, 1.5, 100.0);
    assert(std::abs(z - 1.5) < 1e-6);
    owncut_tl_set_beat_grid(eng, 120.0, 0);
    owncut_tl_upsert_marker(eng, "mk-1", 1000, "Chorus", "#4F9CF9");
    const int32_t jsz = owncut_tl_export_state_json(eng, nullptr, 0);
    assert(jsz > 20);
    owncut_tl_destroy(eng);

    std::cout << "SessionCoordinator + timeline_engine adapter integration tests passed\n";
    return 0;
}

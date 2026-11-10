#include "test_harness.hpp"
#include "timeline/owncut_adapter.hpp"
#include "timeline_c_api.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace owncut::timeline;

OCU_TEST(adapter_sync_and_magnetic_snap) {
    OwncutTimelineAdapter adapter(FrameRate::fps_30(), 1920, 1080);
    std::vector<OwncutClipSeed> clips{
        {"clip-1", "v1", "content://video/a", 0, 90, 0, 3000, false},
        {"clip-2", "v1", "content://video/b", 120, 60, 1000, 3000, true},
        {"audio-1", "a1", "content://audio/bgm", 0, 180, 0, 6000, false},
    };
    adapter.sync_clips(clips);

    OCU_ASSERT_EQ(adapter.track_count(), std::size_t{2});
    OCU_ASSERT_EQ(adapter.clip_count(), std::size_t{3});
    OCU_ASSERT_EQ(adapter.duration_frames(), int64_t{180});

    // Dragging clip-2 close to clip-1's end (frame 90) should snap magnetically to 90.
    const auto snap_to_end = adapter.evaluate_snap("v1", "clip-2", 92, 60, 0, 1.5, true);
    OCU_ASSERT_TRUE(snap_to_end.snapped);
    OCU_ASSERT_EQ(snap_to_end.final_frame, int64_t{90});
    OCU_ASSERT_EQ(snap_to_end.snap_guide_frame, int64_t{90});
    OCU_ASSERT_TRUE(!snap_to_end.slot_adjusted);
    OCU_ASSERT_TRUE(snap_to_end.haptic_pulse);

    // Dragging clip-2 tail (start + 60) near playhead at 250 (proposed start = 188 -> end = 248)
    const auto snap_to_playhead = adapter.evaluate_snap("v1", "clip-2", 188, 60, 250, 1.5, true);
    OCU_ASSERT_TRUE(snap_to_playhead.snapped);
    OCU_ASSERT_EQ(snap_to_playhead.final_frame, int64_t{190});
    OCU_ASSERT_EQ(snap_to_playhead.snap_guide_frame, int64_t{250});

    // Overlapping clip-1 directly at frame 40 must adjust cleanly to nearest non-overlapping slot (90).
    const auto overlap_resolved = adapter.evaluate_snap("v1", "clip-2", 40, 60, 0, 1.5, true);
    OCU_ASSERT_TRUE(overlap_resolved.slot_adjusted);
    OCU_ASSERT_EQ(overlap_resolved.final_frame, int64_t{90});

    // Cross-track boundary snap: audio-1 on a1 ends at 180, so dragging near 180 snaps cleanly.
    const auto cross_track = adapter.evaluate_snap("v1", "clip-2", 181, 60, 0, 1.5, true);
    OCU_ASSERT_TRUE(cross_track.snapped);
    OCU_ASSERT_EQ(cross_track.final_frame, int64_t{180});

    // Far free position (240) must NOT falsely snap or shift.
    const auto free_spot = adapter.evaluate_snap("v1", "clip-2", 240, 60, 0, 1.5, true);
    OCU_ASSERT_TRUE(!free_spot.snapped);
    OCU_ASSERT_TRUE(!free_spot.slot_adjusted);
    OCU_ASSERT_EQ(free_spot.final_frame, int64_t{240});
}

OCU_TEST(c_api_owncut_bridge) {
    OwnCutTimelineEngine* eng = owncut_tl_create(30, 1, 1920, 1080);
    OCU_ASSERT_TRUE(eng != nullptr);

    const OwnCutBridgeClipSpec specs[2] = {
        {"c1", "v1", "content://test/1", 0, 90, 0, 3000, 0},
        {"c2", "v1", "content://test/2", 150, 90, 0, 3000, 1},
    };
    OCU_ASSERT_EQ(owncut_tl_sync_clips(eng, specs, 2), int32_t{1});

    OwnCutSnapOutcome out{};
    OCU_ASSERT_EQ(owncut_tl_evaluate_snap(eng, "v1", "c2", 91, 90, 0, 1.6, 1, &out), int32_t{1});
    OCU_ASSERT_EQ(out.snapped, int32_t{1});
    OCU_ASSERT_EQ(out.final_frame, int64_t{90});

    const double initial_zoom = owncut_tl_get_zoom_scale(eng);
    OCU_ASSERT_NEAR(initial_zoom, 1.0, 1e-6);
    const double zoomed = owncut_tl_gesture_pinch_scale(eng, 2.0, 200.0);
    OCU_ASSERT_TRUE(zoomed > 1.9);
    owncut_tl_reset_zoom(eng);
    OCU_ASSERT_NEAR(owncut_tl_get_zoom_scale(eng), 1.0, 1e-6);

    const int32_t needed = owncut_tl_get_capabilities_json(eng, nullptr, 0);
    OCU_ASSERT_TRUE(needed > 20);
    std::string json(static_cast<std::size_t>(needed), '\0');
    owncut_tl_get_capabilities_json(eng, json.data(), needed);
    OCU_ASSERT_TRUE(json.find("\"engine\":\"owncut_timeline_engine\"") != std::string::npos);
    OCU_ASSERT_TRUE(json.find("\"clips\":2") != std::string::npos);

    owncut_tl_destroy(eng);
}

OCU_TEST(yuv_and_pcm_converters) {
    // 4x4 limited-range white YUV (Y=235, U=128, V=128) across I420, NV12, NV21
    const int32_t w = 4, h = 4;
    std::vector<uint8_t> i420(static_cast<std::size_t>(w * h + 2 * (w / 2) * (h / 2)), 128);
    std::fill_n(i420.begin(), w * h, static_cast<uint8_t>(235));
    VideoFrame f_i420;
    OCU_ASSERT_TRUE(convert_yuv420_to_rgba32f(
        i420.data(), i420.size(), w, h, w, h, Yuv420Layout::I420, true, f_i420, nullptr));
    OCU_ASSERT_TRUE(f_i420.valid());
    OCU_ASSERT_NEAR(f_i420.data()[0], 1.0f, 0.02f);
    OCU_ASSERT_NEAR(f_i420.data()[1], 1.0f, 0.02f);
    OCU_ASSERT_NEAR(f_i420.data()[2], 1.0f, 0.02f);

    std::vector<uint8_t> nv12(static_cast<std::size_t>(w * h * 3 / 2), 128);
    std::fill_n(nv12.begin(), w * h, static_cast<uint8_t>(16)); // black
    VideoFrame f_nv12;
    OCU_ASSERT_TRUE(convert_yuv420_to_rgba32f(
        nv12.data(), nv12.size(), w, h, w, h, Yuv420Layout::NV12, false, f_nv12, nullptr));
    OCU_ASSERT_NEAR(f_nv12.data()[0], 0.0f, 0.02f);

    const std::vector<int16_t> pcm = {16384, -16384, 32767, 0};
    AudioBuffer ab;
    OCU_ASSERT_TRUE(convert_pcm16_to_audio_buffer(pcm.data(), 2, 48000, 2, RationalTime::zero(), ab));
    OCU_ASSERT_EQ(ab.channels, 2);
    OCU_ASSERT_EQ(ab.frame_count, int64_t{2});
    OCU_ASSERT_NEAR(ab.sample(0, 0), 0.5f, 0.01f);
    OCU_ASSERT_NEAR(ab.sample(0, 1), -0.5f, 0.01f);
}

OCU_TEST(unicode_text_and_bengali_subtitle_raster) {
    VideoFrame canvas = VideoFrame::allocate(320, 180, nullptr);
    OCU_ASSERT_TRUE(canvas.valid());
    std::fill_n(canvas.data(), static_cast<std::size_t>(canvas.stride_floats) * canvas.height, 0.0f);

    MultiTextEngine mte;
    TextLayer layer;
    layer.utf8_text = "OwnCut বাংলা";
    layer.style.font_size_pt = 28.0f;
    layer.style.bold = true;
    layer.style.outline_width = 1.0f;
    layer.style.alignment = TextAlignment::Center;
    mte.add_layer(layer);
    mte.composite_layers(canvas, RationalTime::zero(), RationalTime::from_milliseconds(1000));

    int non_black_pixels = 0;
    for (int32_t y = 0; y < canvas.height; ++y) {
        const float* row = canvas.data() + y * canvas.stride_floats;
        for (int32_t x = 0; x < canvas.width; ++x) {
            if (row[x * 4 + 0] > 0.25f) ++non_black_pixels;
        }
    }
    OCU_ASSERT_TRUE(non_black_pixels > 50);

    SubtitleEngine subs;
    OCU_ASSERT_TRUE(subs.import_srt("1\n00:00:00,000 --> 00:00:02,000\nস্বাগতম Hello\n\n"));
    subs.composite_cues(canvas, RationalTime::from_milliseconds(500));
    OCU_ASSERT_EQ(subs.active_cues_at(RationalTime::from_milliseconds(500)).size(), std::size_t{1});
}

OCU_TEST(persistence_json_and_atomic_io) {
    OwncutRichState st;
    st.snap_enabled = false;
    st.beat_bpm = 128.5;
    st.beat_offset_ms = 250;

    RichClipFx fx;
    fx.track_id = "v2";
    fx.clip_id = "clip-\"quoted\"\\1";
    fx.speed = 1.5;
    fx.reversed = true;
    fx.freeze_ms = 400;
    fx.volume = 0.85;
    fx.fade_in_ms = 120;
    fx.fade_out_ms = 180;
    fx.opacity = 0.75;
    fx.blend_mode = "screen";
    fx.scale = 1.25;
    fx.pan_x = -0.1;
    fx.pan_y = 0.05;
    fx.rotation_deg = 12.0;
    fx.dynamic_zoom_start = 1.0;
    fx.dynamic_zoom_end = 1.2;
    fx.transition_kind = "wipe_left";
    fx.transition_ms = 400;
    fx.audio_peaks = {0.1, 0.55, 0.9};
    st.clip_fx.push_back(fx);

    st.overlays.push_back(RichTextOverlay{"ov-1", "Title বাংলা\nLine2", 100, 1500, 0.5, 0.8, 1.1, 0.0, "#FFFFFF", "#B8000000", "title", true});
    st.subtitles.push_back(RichSubtitleCue{"s-1", 0, 1000, "Cue One"});
    st.markers.push_back(RichMarker{"m-1", 500, "Beat 1", "#4F9CF9"});

    const std::string json = TimelineSerializer::state_to_json(st);
    OwncutRichState decoded;
    OCU_ASSERT_TRUE(TimelineSerializer::state_from_json(json, decoded));
    OCU_ASSERT_EQ(decoded.snap_enabled, false);
    OCU_ASSERT_NEAR(decoded.beat_bpm, 128.5, 1e-4);
    OCU_ASSERT_EQ(decoded.beat_offset_ms, int64_t{250});
    OCU_ASSERT_EQ(decoded.clip_fx.size(), std::size_t{1});
    OCU_ASSERT_EQ(decoded.clip_fx[0].clip_id, fx.clip_id);
    OCU_ASSERT_EQ(decoded.clip_fx[0].transition_kind, std::string("wipe_left"));
    OCU_ASSERT_EQ(decoded.overlays.size(), std::size_t{1});
    OCU_ASSERT_EQ(decoded.overlays[0].text, std::string("Title বাংলা\nLine2"));
    OCU_ASSERT_EQ(decoded.subtitles.size(), std::size_t{1});
    OCU_ASSERT_EQ(decoded.markers.size(), std::size_t{1});

    const std::string path = "/tmp/owncut_tl_state_test.tlstate";
    OCU_ASSERT_TRUE(TimelineSerializer::write_atomic_utf8(path, json));
    std::string loaded;
    OCU_ASSERT_TRUE(TimelineSerializer::read_utf8_file(path, loaded));
    OCU_ASSERT_EQ(loaded, json);
    std::remove(path.c_str());

    OwncutRichState bad;
    OCU_ASSERT_TRUE(!TimelineSerializer::state_from_json("{invalid", bad));
}

OCU_TEST(adapter_rich_fx_beat_snap_audio_vision_c_api) {
    OwnCutTimelineEngine* eng = owncut_tl_create(30, 1, 160, 90);
    OCU_ASSERT_TRUE(eng != nullptr);

    const OwnCutBridgeClipSpec specs[2] = {
        {"c1", "v1", "synthetic://a", 0, 60, 0, 2000, 0},
        {"c2", "v1", "synthetic://b", 60, 60, 0, 2000, 0},
    };
    OCU_ASSERT_EQ(owncut_tl_sync_clips(eng, specs, 2), int32_t{1});

    // Enable 120 BPM beat grid (beat every 15 frames at 30fps)
    owncut_tl_set_beat_grid(eng, 120.0, 0);
    owncut_tl_upsert_marker(eng, "m1", 2000, "Drop", "#FFB454");

    // Drag c2 near beat at frame 75 (5 beats * 15 frames) -> should snap to 75
    OwnCutSnapOutcome beat_snap{};
    OCU_ASSERT_EQ(owncut_tl_evaluate_snap(eng, "v1", "c2", 74, 60, 0, 1.5, 1, &beat_snap), int32_t{1});
    OCU_ASSERT_EQ(beat_snap.snapped, int32_t{1});
    OCU_ASSERT_EQ(beat_snap.final_frame, int64_t{75});

    // Import SRT + export SRT round-trip + render adapter frame
    OCU_ASSERT_EQ(owncut_tl_import_srt(eng, "1\n00:00:00,000 --> 00:00:03,000\nBeat Sync\n\n"), int32_t{1});
    std::vector<float> rgba(static_cast<std::size_t>(160 * 90 * 4), 0.0f);
    OCU_ASSERT_EQ(owncut_tl_render_adapter_frame_rgba32f(eng, 15, rgba.data(), static_cast<int32_t>(rgba.size())), int32_t{1});

    // Rich state export -> fresh engine import
    const int32_t st_len = owncut_tl_export_state_json(eng, nullptr, 0);
    OCU_ASSERT_TRUE(st_len > 20);
    std::string st_json(static_cast<std::size_t>(st_len), '\0');
    owncut_tl_export_state_json(eng, st_json.data(), st_len);

    OwnCutTimelineEngine* eng2 = owncut_tl_create(30, 1, 160, 90);
    OCU_ASSERT_EQ(owncut_tl_import_state_json(eng2, st_json.c_str()), int32_t{1});
    owncut_tl_destroy(eng2);

    // PCM16 analyzer on a pulsed 120 BPM audio signal
    std::vector<int16_t> pcm(48000, 0);
    for (int i = 0; i < 48000; ++i) {
        if ((i % 24000) < 1200) pcm[static_cast<std::size_t>(i)] = 20000;
    }
    float peaks[16]{};
    double bpm = 0.0, lufs = -70.0;
    OCU_ASSERT_EQ(owncut_tl_analyze_pcm16(pcm.data(), 48000, 1, 48000, 16, peaks, &bpm, &lufs), int32_t{16});
    OCU_ASSERT_TRUE(peaks[0] > 0.5f);
    OCU_ASSERT_TRUE(lufs > -70.0);

    // Scene cut detector: 3 black frames -> 3 bright white frames -> cut at index 3
    const int32_t fw = 16, fh = 16;
    std::vector<uint8_t> frames(static_cast<std::size_t>(6 * fw * fh * 4), 0);
    std::fill(frames.begin() + 3 * fw * fh * 4, frames.end(), static_cast<uint8_t>(255));
    int32_t cuts[6]{};
    const int32_t num_cuts = owncut_tl_detect_scene_cuts(frames.data(), 6, fw, fh, 0.35, cuts, 6);
    OCU_ASSERT_TRUE(num_cuts >= 1);
    OCU_ASSERT_EQ(cuts[0], int32_t{3});

    owncut_tl_destroy(eng);
}

OCU_TEST(clip_transform_fades_and_transitions) {
    OwncutTimelineAdapter adapter(FrameRate::fps_30(), 128, 72);
    adapter.sync_clips({
        {"c1", "v1", "synthetic://a", 0, 30, 0, 1000, false},
        {"c2", "v1", "synthetic://b", 30, 30, 0, 1000, false},
    });

    RichClipFx fx2;
    fx2.track_id = "v1";
    fx2.clip_id = "c2";
    fx2.scale = 0.8;
    fx2.pan_x = 0.1;
    fx2.rotation_deg = 15.0;
    fx2.dynamic_zoom_start = 1.0;
    fx2.dynamic_zoom_end = 1.25;
    fx2.fade_in_ms = 200;
    fx2.fade_out_ms = 200;
    fx2.transition_kind = "dip_white";
    fx2.transition_ms = 300;
    adapter.set_clip_fx(fx2);

    VideoFrame vf0;
    OCU_ASSERT_TRUE(adapter.render_composited_frame(30, vf0));
    OCU_ASSERT_TRUE(vf0.valid());
    // At frame 30 (start of dip_white + fade_in=0ms), output frame exists cleanly without NaNs
    OCU_ASSERT_TRUE(vf0.data()[0] >= 0.0f && vf0.data()[0] <= 1.0f);

    // Mid-clip wipe_left transition against predecessor clip c1
    fx2.transition_kind = "wipe_left";
    fx2.fade_in_ms = 0;
    adapter.set_clip_fx(fx2);
    VideoFrame vf_wipe;
    OCU_ASSERT_TRUE(adapter.render_composited_frame(34, vf_wipe));
    OCU_ASSERT_TRUE(vf_wipe.valid());
}

int main() {
    return ::owncut::test::run_all("OwncutAdapter");
}

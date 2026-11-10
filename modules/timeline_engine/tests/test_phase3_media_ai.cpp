#include "test_harness.hpp"
#include "timeline/animation_engine.hpp"
#include "timeline/audio_engine.hpp"
#include "timeline/text_subtitle_engine.hpp"
#include "timeline/vision_engine.hpp"

#include <cmath>
#include <vector>

using namespace owncut::timeline;

OCU_TEST(AnimationKeyframesDynamicZoomJSONAndTransitions) {
    AnimatedTransformRig rig;
    rig.pos_x.add_or_update(RationalTime(0, 1), 0.0, KeyframeInterpolation::EaseInOut);
    rig.pos_x.add_or_update(RationalTime(2, 1), 0.5, KeyframeInterpolation::Linear);
    rig.scale_x.set_loop_mode(KeyframeLoopMode::PingPong);
    rig.scale_x.add_or_update(RationalTime(0, 1), 1.0);
    rig.scale_x.add_or_update(RationalTime(1, 1), 1.5);
    rig.dynamic_zoom = DynamicZoomSpec{true, true, 1.0, 1.4, 0.0, 0.0, 0.0, 0.0};

    const auto mid = rig.evaluate(RationalTime(1, 1), RationalTime(2, 1));
    OCU_ASSERT_NEAR(mid.pos_x, 0.25, 0.05);
    OCU_ASSERT_NEAR(mid.scale_x, 1.5 * 1.2, 1e-4);

    // PingPong at t=1.5 should bounce back from 1.5 -> 1.25
    rig.dynamic_zoom.enabled = false;
    OCU_ASSERT_NEAR(rig.scale_x.evaluate(RationalTime(3, 2)), 1.25, 1e-4);

    // Inspector Preset JSON roundtrip
    InspectorPreset p;
    p.name = "ZoomPunch";
    p.blend_mode = BlendMode::Overlay;
    p.static_transform.rotation_deg = 12.5;
    p.dynamic_zoom.enabled = true;
    const std::string json = p.to_json();
    auto parsed = InspectorPreset::from_json(json);
    OCU_ASSERT_TRUE(parsed.has_value());
    OCU_ASSERT_EQ(parsed->name, std::string("ZoomPunch"));
    OCU_ASSERT_EQ(parsed->blend_mode, BlendMode::Overlay);
    OCU_ASSERT_NEAR(parsed->static_transform.rotation_deg, 12.5, 1e-6);

    // Frame transform + Transition
    VideoFrame a = VideoFrame::allocate(32, 32);
    VideoFrame b = VideoFrame::allocate(32, 32);
    std::fill(a.owned_rgba32f.begin(), a.owned_rgba32f.end(), 1.0f);
    std::fill(b.owned_rgba32f.begin(), b.owned_rgba32f.end(), 0.2f);
    VideoFrame out_xf, out_tr;
    AnimatedTransformRig::apply_transform(a, mid, out_xf);
    OCU_ASSERT_TRUE(out_xf.valid());
    OCU_ASSERT_TRUE(TransitionEngine::render_transition(a, b, 0.5f, TransitionType::CrossDissolve, out_tr));
    OCU_ASSERT_NEAR(out_tr.data()[0], 0.6f, 1e-4);
}

OCU_TEST(TextBanglaSubtitlesSRTVTTTTMLAndMotionTemplates) {
    VideoFrame canvas = VideoFrame::allocate(160, 90);
    MultiTextEngine mte;
    TextLayer l1;
    l1.utf8_text = "স্বাগতম OwnCut"; // Bangla + English UTF-8
    l1.style.font_size_pt = 24.0f;
    l1.style.background_rgba[3] = 0.5f;
    mte.add_layer(std::move(l1));
    mte.composite_layers(canvas, RationalTime::zero(), RationalTime(2, 1));
    // Verify some center canvas pixel got drawn above 0
    const int cx = 80, cy = 45;
    OCU_ASSERT_TRUE(canvas.data()[(cy * canvas.width + cx) * 4] > 0.1f);

    SubtitleEngine sub;
    sub.add_cue(SubtitleCue{0, RationalTime::from_milliseconds(1500), RationalTime::from_milliseconds(4000),
                            "হ্যালো বিশ্ব", "Host", ""});
    const std::string srt = sub.export_srt();
    const std::string vtt = sub.export_vtt();
    const std::string ttml = sub.export_ttml();

    SubtitleEngine s2, s3, s4;
    OCU_ASSERT_TRUE(s2.import_srt(srt));
    OCU_ASSERT_TRUE(s3.import_vtt(vtt));
    OCU_ASSERT_TRUE(s4.import_ttml(ttml));
    OCU_ASSERT_EQ(s2.cues()[0].text, std::string("হ্যালো বিশ্ব"));
    OCU_ASSERT_EQ(s3.cues()[0].start, RationalTime::from_milliseconds(1500));
    OCU_ASSERT_EQ(s4.active_cues_at(RationalTime(2, 1)).size(), 1u);

    BuiltinMotionGraphicsRenderer lottie;
    MotionTemplateHandle mh;
    OCU_ASSERT_TRUE(lottie.load_template(MotionTemplateKind::Lottie, "{\"nm\":\"LowerThird\"}", mh));
    mh.width = 64; mh.height = 36;
    VideoFrame mf;
    OCU_ASSERT_TRUE(lottie.render_template_frame(mh, RationalTime(1, 1), mf, nullptr));
    OCU_ASSERT_TRUE(mf.valid());
}

OCU_TEST(VisionStabilizerSmartReframeSceneCutAndTextBasedEditor) {
    MediaMetadata meta;
    meta.width = 64;
    meta.height = 36;
    meta.fps = FrameRate::fps_30();
    meta.duration = RationalTime::from_frames(20, meta.fps);

    // 1. Stabilizer with jitter
    SyntheticMediaDecoder jitter_dec(SyntheticPattern::MovingBlock, meta);
    jitter_dec.set_camera_jitter(3.0, 2.0);
    std::vector<VideoFrame> frames(12);
    for (int i = 0; i < 12; ++i) {
        OCU_ASSERT_TRUE(jitter_dec.decode_video_frame(RationalTime::from_frames(i, meta.fps), frames[i], nullptr));
    }
    StabilizationSettings st_cfg;
    st_cfg.use_kalman_filter = true;
    st_cfg.zoom_adaptive = true;
    auto analysis = Stabilizer::analyze_sequence(frames, st_cfg);
    OCU_ASSERT_EQ(analysis.samples.size(), 12u);
    OCU_ASSERT_TRUE(analysis.adaptive_zoom >= 1.0f);
    VideoFrame stab_out;
    OCU_ASSERT_TRUE(Stabilizer::stabilize_frame(frames[5], analysis.samples[5], analysis.adaptive_zoom, stab_out));

    // 2. Smart Reframe 16:9 -> 9:16
    auto boxes = SmartReframe::compute_reframe_trajectory(frames, 9, 16);
    OCU_ASSERT_EQ(boxes.size(), 12u);
    OCU_ASSERT_TRUE(boxes[0].crop_width_norm < 0.6f);
    VideoFrame reframed;
    OCU_ASSERT_TRUE(SmartReframe::apply_reframe(frames[0], boxes[0], 36, 64, reframed));

    // 3. Scene Cut detection on ShotChangeAtHalf
    SyntheticMediaDecoder cut_dec(SyntheticPattern::ShotChangeAtHalf, meta);
    std::vector<VideoFrame> cut_frames(20);
    for (int i = 0; i < 20; ++i) {
        OCU_ASSERT_TRUE(cut_dec.decode_video_frame(RationalTime::from_frames(i, meta.fps), cut_frames[i], nullptr));
    }
    auto cuts = SceneAndCutDetector::detect_scene_cuts(cut_frames, meta.fps, 0.30f);
    OCU_ASSERT_EQ(cuts.size(), 1u);
    OCU_ASSERT_EQ(cuts[0].frame_index, 10);

    // 4. Boring segment detection & Text-based editor
    auto boring = SceneAndCutDetector::detect_boring_and_jump_cuts(cut_frames, meta.fps, 0.01f, 5);
    OCU_ASSERT_TRUE(!boring.empty());

    TextBasedEditor tbe;
    tbe.set_words({
        TranscriptWord{1, 10, "Hello", "A", RationalTime(0, 1), RationalTime(1, 1), false},
        TranscriptWord{2, 10, "um", "A", RationalTime(1, 1), RationalTime(2, 1), false},
        TranscriptWord{3, 10, "world", "A", RationalTime(2, 1), RationalTime(3, 1), false}
    });
    OCU_ASSERT_TRUE(tbe.mark_word_deleted(2, true));
    Timeline tl(1, "TBE");
    CommandStack stack;
    OCU_ASSERT_TRUE(tbe.apply_edits_to_timeline(tl, stack, 10));
    OCU_ASSERT_EQ(tl.duration(), RationalTime(2, 1));
}

OCU_TEST(AudioSyncBinFFTMixerEQLoudnessVoiceoverAndBeatDetector) {
    // 1. FFT Cross-Correlation SyncBin (300 sample delay on pulsing audio)
    MediaPool pool;
    MediaMetadata mm;
    mm.fps = FrameRate::fps_30();
    mm.duration = RationalTime(4, 1);

    auto dec_ref = std::make_shared<SyntheticMediaDecoder>(SyntheticPattern::MovingBlock, mm);
    dec_ref->set_audio_tone_hz(440.0);
    dec_ref->set_audio_delay_samples(0);
    dec_ref->set_audio_pulse_period_samples(12000); // Impulse every 250ms (240 BPM)
    auto dec_lag = std::make_shared<SyntheticMediaDecoder>(SyntheticPattern::MovingBlock, mm);
    dec_lag->set_audio_tone_hz(440.0);
    dec_lag->set_audio_delay_samples(320);
    dec_lag->set_audio_pulse_period_samples(12000); // Delayed by +320 samples

    MediaItem m1; m1.metadata = mm; m1.metadata.name = "Cam1"; m1.decoder = dec_ref;
    MediaItem m2; m2.metadata = mm; m2.metadata.name = "Cam2"; m2.decoder = dec_lag;
    const uint64_t id1 = pool.add_item(m1);
    const uint64_t id2 = pool.add_item(m2);

    SyncBinController sync_bin;
    OCU_ASSERT_TRUE(sync_bin.sync_clips_by_audio({pool.find(id1), pool.find(id2)}, 16384, 48000));
    OCU_ASSERT_EQ(sync_bin.synced_angles().size(), 2u);
    OCU_ASSERT_NEAR(sync_bin.synced_angles()[1].sync_offset.to_samples(48000), -320.0, 4.0);

    // 2. BeatDetector finds pulses cleanly
    AudioBuffer pulse_buf;
    OCU_ASSERT_TRUE(dec_ref->decode_audio_samples(RationalTime::zero(), 48000 * 2, 48000, 1, pulse_buf));
    const auto beats = BeatDetector::detect_beats(pulse_buf, 1024, 512, 1.35f);
    OCU_ASSERT_TRUE(beats.beat_times.size() >= 4u);

    // 3. 6-band EQ (HPF at 500Hz attenuates 60Hz sine significantly)
    AudioBuffer low_sine;
    low_sine.sample_rate = 48000;
    low_sine.channels = 1;
    low_sine.frame_count = 4800;
    low_sine.interleaved.resize(4800);
    for (int i = 0; i < 4800; ++i) {
        low_sine.interleaved[i] = 0.8f * std::sin(2.0f * 3.1415926f * 60.0f * i / 48000.0f);
    }
    const auto before_lufs = LoudnessMeter::measure(low_sine);
    ParametricEQ6Band eq;
    eq.set_band(0, EqBandConfig{true, BiquadBandType::HighPass, 500.0, 0.0, 0.707}, 48000.0);
    eq.process_inplace(low_sine);
    const auto after_lufs = LoudnessMeter::measure(low_sine);
    OCU_ASSERT_TRUE(after_lufs.integrated_lufs < before_lufs.integrated_lufs - 15.0);

    // 4. Dynamics Sidechain Ducker + Limiter
    DynamicsProcessor dyn;
    DynamicsConfig dc;
    dc.limiter_enabled = true;
    dc.limiter_ceiling_db = -3.0f;
    dc.sidechain_ducker_enabled = true;
    dc.duck_threshold_db = -20.0f;
    dc.duck_amount_db = -12.0f;
    dyn.set_config(dc);
    AudioBuffer music = pulse_buf;
    std::fill(music.interleaved.begin(), music.interleaved.end(), 0.9f);
    AudioBuffer sc = music;
    dyn.process_inplace(music, &sc);
    OCU_ASSERT_TRUE(std::abs(music.interleaved.back()) < 0.75f);

    // 5. Live Voiceover Recorder (Countdown + Lock-Free RingBuffer + Teleprompter)
    LiveVoiceoverRecorder rec;
    rec.set_teleprompter_script("Welcome to the OwnCut timeline editing engine test script", 120.0);
    rec.start_countdown(2.0);
    OCU_ASSERT_TRUE(!rec.tick_countdown(1.0));
    OCU_ASSERT_TRUE(rec.tick_countdown(1.1));
    OCU_ASSERT_TRUE(rec.is_recording());
    float dummy_vo[256];
    std::fill_n(dummy_vo, 256, 0.25f);
    OCU_ASSERT_EQ(rec.push_captured_samples(dummy_vo, 256), 256u);
    AudioBuffer vo_out;
    rec.drain_to_buffer(vo_out, 48000, 1);
    OCU_ASSERT_EQ(vo_out.frame_count, 256);
    OCU_ASSERT_TRUE(!rec.current_teleprompter_hint(0.5).empty());
}

int main() {
    return ::owncut::test::run_all("Phase3_MediaAI");
}

#pragma once

#include "timeline/animation_engine.hpp"
#include "timeline/command_stack.hpp"
#include "timeline/media_pool.hpp"
#include "timeline/memory_pool.hpp"
#include "timeline/timeline_model.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace owncut::timeline {

// Real Cooley-Tukey Radix-2 FFT + Cross-Correlation utilities
class AudioDSP {
public:
    static std::pair<int64_t, float> cross_correlate_offset(const AudioBuffer& reference,
                                                            const AudioBuffer& target,
                                                            int64_t max_lag_samples);
    static std::vector<float> compute_magnitude_spectrum(const float* samples, std::size_t fft_size);
};

// Cut-Page SyncBin (Timecode + FFT Audio Cross-Correlation + 4/9/16/25 Grid + Live Overwrite)
enum class SyncMethod : uint8_t {
    Timecode = 0,
    AudioWaveformFFT = 1
};

enum class SyncBinGridSize : int32_t {
    Grid4 = 4,   // 2x2
    Grid9 = 9,   // 3x3
    Grid16 = 16, // 4x4
    Grid25 = 25  // 5x5
};

struct SyncedAngleClip {
    uint64_t media_id = 0;
    std::string label;
    RationalTime sync_offset{}; // Shift to align with timeline anchor
    float correlation_confidence = 1.0f;
};

class SyncBinController {
public:
    bool sync_clips_by_timecode(const std::vector<const MediaItem*>& items,
                                const std::vector<RationalTime>& start_timecodes);

    bool sync_clips_by_audio(const std::vector<const MediaItem*>& items,
                             int64_t analyze_samples = 16384,
                             int32_t sample_rate = 48000);

    void set_grid_size(SyncBinGridSize g) noexcept { grid_size_ = g; }
    [[nodiscard]] SyncBinGridSize grid_size() const noexcept { return grid_size_; }
    [[nodiscard]] const std::vector<SyncedAngleClip>& synced_angles() const noexcept { return angles_; }

    // Live Overwrite selected angle at playhead into timeline
    bool live_overwrite_angle(Timeline& tl, CommandStack& stack, std::size_t angle_idx,
                              const RationalTime& duration);

private:
    SyncBinGridSize grid_size_ = SyncBinGridSize::Grid4;
    std::vector<SyncedAngleClip> angles_;
};

// Fairlight-style 6-Band Parametric Biquad EQ
enum class BiquadBandType : uint8_t {
    Bypass = 0,
    HighPass = 1,
    LowShelf = 2,
    Peaking = 3,
    HighShelf = 4,
    LowPass = 5
};

struct EqBandConfig {
    bool enabled = true;
    BiquadBandType type = BiquadBandType::Peaking;
    double freq_hz = 1000.0;
    double gain_db = 0.0;
    double q = 0.707;
};

class BiquadFilter {
public:
    void configure(const EqBandConfig& cfg, double sample_rate);
    void reset() noexcept;
    float process_sample(float x, int32_t channel) noexcept;

private:
    double b0_ = 1.0, b1_ = 0.0, b2_ = 0.0, a1_ = 0.0, a2_ = 0.0;
    static constexpr int32_t kMaxCh = 8;
    double x1_[kMaxCh]{}, x2_[kMaxCh]{}, y1_[kMaxCh]{}, y2_[kMaxCh]{};
};

class ParametricEQ6Band {
public:
    static constexpr int32_t kNumBands = 6;
    ParametricEQ6Band();
    void set_band(int32_t idx, const EqBandConfig& cfg, double sample_rate);
    [[nodiscard]] const EqBandConfig& band(int32_t idx) const { return configs_[idx]; }
    void process_inplace(AudioBuffer& buffer);

private:
    EqBandConfig configs_[kNumBands];
    BiquadFilter filters_[kNumBands];
};

// Dynamics: Compressor, Limiter, Noise Gate, Sidechain Ducker
struct DynamicsConfig {
    bool compressor_enabled = false;
    float comp_threshold_db = -18.0f;
    float comp_ratio = 4.0f;
    float comp_attack_ms = 5.0f;
    float comp_release_ms = 80.0f;
    float comp_makeup_db = 0.0f;

    bool limiter_enabled = false;
    float limiter_ceiling_db = -1.0f;

    bool gate_enabled = false;
    float gate_threshold_db = -45.0f;

    bool sidechain_ducker_enabled = false;
    float duck_threshold_db = -24.0f;
    float duck_amount_db = -10.0f;
    float duck_release_ms = 180.0f;
};

class DynamicsProcessor {
public:
    void set_config(const DynamicsConfig& cfg) { cfg_ = cfg; }
    [[nodiscard]] const DynamicsConfig& config() const noexcept { return cfg_; }
    void reset() noexcept;
    void process_inplace(AudioBuffer& main_audio, const AudioBuffer* sidechain_audio = nullptr);

private:
    DynamicsConfig cfg_{};
    float comp_env_ = 0.0f;
    float gate_env_ = 1.0f;
    float duck_env_ = 0.0f;
};

// ITU-R BS.1770 / EBU R128 Loudness & True-Peak Meter
struct LoudnessReport {
    double integrated_lufs = -70.0;
    double momentary_max_lufs = -70.0;
    double short_term_max_lufs = -70.0;
    double true_peak_dbtp = -120.0;
};

class LoudnessMeter {
public:
    static LoudnessReport measure(const AudioBuffer& buffer);
};

// Fairlight Track Strip & Lock-free Audio Mixer
struct AudioTrackStrip {
    uint64_t track_id = 0;
    std::atomic<float> fader_db{0.0f};
    std::atomic<float> pan{0.0f};
    std::atomic<bool> mute{false};
    std::atomic<bool> solo{false};
    KeyframedProperty gain_automation_db{0.0};
    KeyframedProperty pan_automation{0.0};
    ParametricEQ6Band eq;
    DynamicsProcessor dynamics;
};

class AudioMixer {
public:
    AudioTrackStrip& ensure_strip(uint64_t track_id);
    AudioTrackStrip* find_strip(uint64_t track_id);

    // Mixes active timeline audio tracks with EQ, Dynamics, Automation, Constant-Power Pan, and Sidechain Ducking
    bool mix_timeline_range(const Timeline& tl,
                            const MediaPool& media_pool,
                            const RationalTime& start_time,
                            int64_t frame_count,
                            int32_t sample_rate,
                            AudioBuffer& out_stereo,
                            std::optional<uint64_t> sidechain_dialogue_track_id = std::nullopt);

private:
    std::vector<std::unique_ptr<AudioTrackStrip>> strips_;
};

// Live Voiceover Recorder with SPSC ring-buffer + countdown + teleprompter
class LiveVoiceoverRecorder {
public:
    static constexpr std::size_t kRingCapacitySamples = 262144;
    LiveVoiceoverRecorder() = default;

    void set_teleprompter_script(std::string script, double words_per_minute = 140.0);
    [[nodiscard]] std::string current_teleprompter_hint(double elapsed_sec) const;

    void start_countdown(double seconds = 3.0);
    bool tick_countdown(double dt_sec); // Returns true when countdown completes and recording starts
    [[nodiscard]] bool is_recording() const noexcept { return recording_.load(std::memory_order_acquire); }
    [[nodiscard]] double countdown_remaining() const noexcept { return countdown_rem_; }

    // Called from audio capture callback (wait-free)
    std::size_t push_captured_samples(const float* interleaved_mono_or_stereo, std::size_t count);
    // Called from engine thread
    void drain_to_buffer(AudioBuffer& target, int32_t sample_rate = 48000, int32_t channels = 1);
    void stop_recording();

private:
    LockFreeRingBuffer<float, kRingCapacitySamples> ring_;
    std::atomic<bool> recording_{false};
    double countdown_rem_ = 0.0;
    std::string script_;
    double wpm_ = 140.0;
};

// Spectral-Flux Beat Detector -> snap markers
struct BeatDetectionResult {
    double estimated_bpm = 120.0;
    std::vector<RationalTime> beat_times;
};

class BeatDetector {
public:
    static BeatDetectionResult detect_beats(const AudioBuffer& audio,
                                            std::size_t window_size = 1024,
                                            std::size_t hop_size = 512,
                                            float sensitivity = 1.35f);
};

// AI Voice Isolation & Music Remixer extensible hook + built-in DSP fallback
class IAudioAIEnhancer {
public:
    virtual ~IAudioAIEnhancer() = default;
    virtual bool isolate_voice(const AudioBuffer& in, AudioBuffer& out, float strength) = 0;
    virtual bool remix_music_stems(const AudioBuffer& in, AudioBuffer& out,
                                   float vocals_gain, float drums_gain, float bass_gain, float other_gain) = 0;
};

class BuiltinAudioEnhancer final : public IAudioAIEnhancer {
public:
    bool isolate_voice(const AudioBuffer& in, AudioBuffer& out, float strength) override;
    bool remix_music_stems(const AudioBuffer& in, AudioBuffer& out,
                           float vocals_gain, float drums_gain, float bass_gain, float other_gain) override;
};

} // namespace owncut::timeline

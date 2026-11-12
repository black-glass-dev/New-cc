#include "timeline/audio_engine.hpp"
#include "timeline/edit_operations.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>
#include <sstream>

namespace owncut::timeline {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 6.28318530717958647692;

using Complex = std::complex<double>;

std::size_t next_pow2(std::size_t v) {
    std::size_t n = 1;
    while (n < v) n <<= 1;
    return n;
}

void fft_inplace(std::vector<Complex>& a, bool inverse) {
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double ang = kTwoPi / static_cast<double>(len) * (inverse ? 1.0 : -1.0);
        const Complex wlen(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len) {
            Complex w(1.0, 0.0);
            for (std::size_t j = 0; j < len / 2; ++j) {
                const Complex u = a[i + j];
                const Complex v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
    if (inverse) {
        const double inv_n = 1.0 / static_cast<double>(n);
        for (auto& x : a) x *= inv_n;
    }
}

float db_to_linear(float db) noexcept {
    return std::pow(10.0f, db / 20.0f);
}

float linear_to_db(float lin) noexcept {
    return 20.0f * std::log10(std::max(1e-7f, lin));
}

std::vector<float> extract_mono(const AudioBuffer& buf) {
    std::vector<float> out(static_cast<std::size_t>(std::max<int64_t>(0, buf.frame_count)), 0.0f);
    if (buf.frame_count <= 0 || buf.channels <= 0) return out;
    const float inv_ch = 1.0f / static_cast<float>(buf.channels);
    for (int64_t i = 0; i < buf.frame_count; ++i) {
        float s = 0.0f;
        for (int32_t c = 0; c < buf.channels; ++c) s += buf.sample(i, c);
        out[static_cast<std::size_t>(i)] = s * inv_ch;
    }
    return out;
}

} // namespace

std::pair<int64_t, float> AudioDSP::cross_correlate_offset(const AudioBuffer& reference,
                                                           const AudioBuffer& target,
                                                           int64_t max_lag_samples) {
    const auto a = extract_mono(reference);
    const auto b = extract_mono(target);
    if (a.empty() || b.empty()) return {0, 0.0f};

    const std::size_t n = next_pow2(a.size() + b.size());
    std::vector<Complex> fa(n, 0.0), fb(n, 0.0);
    double ea = 0.0, eb = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        fa[i] = Complex(a[i], 0.0);
        ea += static_cast<double>(a[i]) * a[i];
    }
    for (std::size_t i = 0; i < b.size(); ++i) {
        fb[i] = Complex(b[i], 0.0);
        eb += static_cast<double>(b[i]) * b[i];
    }
    const double norm = std::sqrt(std::max(1e-12, ea * eb));

    fft_inplace(fa, false);
    fft_inplace(fb, false);
    // C[k] = sum_n b[n] * a[n - lag] -> positive lag means target `b` is delayed relative to `a`.
    for (std::size_t i = 0; i < n; ++i) {
        fa[i] = fb[i] * std::conj(fa[i]);
    }
    fft_inplace(fa, true);

    const int64_t limit = std::min<int64_t>(max_lag_samples, static_cast<int64_t>(n / 2) - 1);
    int64_t best_lag = 0;
    double best_corr = -1e18;
    for (int64_t lag = -limit; lag <= limit; ++lag) {
        const std::size_t idx = (lag >= 0) ? static_cast<std::size_t>(lag)
                                           : (n - static_cast<std::size_t>(-lag));
        const double val = fa[idx].real() / norm;
        if (val > best_corr) {
            best_corr = val;
            best_lag = lag;
        }
    }
    return {best_lag, static_cast<float>(std::clamp(best_corr, 0.0, 1.0))};
}

std::vector<float> AudioDSP::compute_magnitude_spectrum(const float* samples, std::size_t fft_size) {
    const std::size_t n = next_pow2(fft_size);
    std::vector<Complex> buf(n, 0.0);
    for (std::size_t i = 0; i < fft_size; ++i) {
        const double w = 0.5 * (1.0 - std::cos(kTwoPi * static_cast<double>(i) / static_cast<double>(fft_size - 1)));
        buf[i] = Complex(static_cast<double>(samples[i]) * w, 0.0);
    }
    fft_inplace(buf, false);
    std::vector<float> mag(n / 2 + 1, 0.0f);
    for (std::size_t i = 0; i <= n / 2; ++i) {
        mag[i] = static_cast<float>(std::abs(buf[i]));
    }
    return mag;
}

bool SyncBinController::sync_clips_by_timecode(const std::vector<const MediaItem*>& items,
                                               const std::vector<RationalTime>& start_timecodes) {
    if (items.empty() || items.size() != start_timecodes.size()) return false;
    angles_.clear();
    const RationalTime anchor = *std::min_element(start_timecodes.begin(), start_timecodes.end());
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (!items[i]) continue;
        angles_.push_back(SyncedAngleClip{
            items[i]->id,
            items[i]->metadata.name,
            start_timecodes[i] - anchor,
            1.0f
        });
    }
    return !angles_.empty();
}

bool SyncBinController::sync_clips_by_audio(const std::vector<const MediaItem*>& items,
                                            int64_t analyze_samples,
                                            int32_t sample_rate) {
    if (items.empty() || !items[0] || !items[0]->decoder) return false;
    angles_.clear();
    AudioBuffer ref_buf;
    if (!items[0]->decoder->decode_audio_samples(RationalTime::zero(), analyze_samples, sample_rate, 1, ref_buf)) {
        return false;
    }
    angles_.push_back(SyncedAngleClip{items[0]->id, items[0]->metadata.name, RationalTime::zero(), 1.0f});

    for (std::size_t i = 1; i < items.size(); ++i) {
        if (!items[i] || !items[i]->decoder) continue;
        AudioBuffer tgt_buf;
        if (!items[i]->decoder->decode_audio_samples(RationalTime::zero(), analyze_samples, sample_rate, 1, tgt_buf)) {
            continue;
        }
        const auto [lag_samples, conf] = AudioDSP::cross_correlate_offset(ref_buf, tgt_buf, analyze_samples / 2);
        // If target waveform appears at +lag_samples relative to reference, shifting target earlier by -lag aligns them.
        const RationalTime offset = RationalTime::from_samples(-lag_samples, sample_rate);
        angles_.push_back(SyncedAngleClip{items[i]->id, items[i]->metadata.name, offset, conf});
    }
    return !angles_.empty();
}

bool SyncBinController::live_overwrite_angle(Timeline& tl, CommandStack& stack, std::size_t angle_idx,
                                             const RationalTime& duration) {
    if (angle_idx >= angles_.size() || duration.num() <= 0) return false;
    const auto& ang = angles_[angle_idx];
    SourceInsertSpec spec;
    spec.media_id = ang.media_id;
    spec.name = ang.label.empty() ? ("Cam " + std::to_string(angle_idx + 1)) : ang.label;
    spec.source_in = std::max(RationalTime::zero(), tl.playhead() - ang.sync_offset);
    spec.source_out = spec.source_in + duration;
    spec.media_duration = spec.source_out + RationalTime(60, 1);
    return EditOperations::perform_edit(tl, stack, EditMode::Overwrite, spec);
}

void BiquadFilter::configure(const EqBandConfig& cfg, double sample_rate) {
    if (!cfg.enabled || cfg.type == BiquadBandType::Bypass || sample_rate <= 0.0) {
        b0_ = 1.0; b1_ = 0.0; b2_ = 0.0; a1_ = 0.0; a2_ = 0.0;
        return;
    }
    const double w0 = kTwoPi * std::clamp(cfg.freq_hz, 10.0, sample_rate * 0.48) / sample_rate;
    const double cs = std::cos(w0);
    const double sn = std::sin(w0);
    const double q = std::max(0.1, cfg.q);
    const double alpha = sn / (2.0 * q);
    const double A = std::pow(10.0, cfg.gain_db / 40.0);

    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;
    switch (cfg.type) {
        case BiquadBandType::HighPass:
            b0 = (1.0 + cs) * 0.5;
            b1 = -(1.0 + cs);
            b2 = (1.0 + cs) * 0.5;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cs;
            a2 = 1.0 - alpha;
            break;
        case BiquadBandType::LowPass:
            b0 = (1.0 - cs) * 0.5;
            b1 = 1.0 - cs;
            b2 = (1.0 - cs) * 0.5;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cs;
            a2 = 1.0 - alpha;
            break;
        case BiquadBandType::Peaking:
            b0 = 1.0 + alpha * A;
            b1 = -2.0 * cs;
            b2 = 1.0 - alpha * A;
            a0 = 1.0 + alpha / A;
            a1 = -2.0 * cs;
            a2 = 1.0 - alpha / A;
            break;
        case BiquadBandType::LowShelf: {
            const double sq = 2.0 * std::sqrt(A) * alpha;
            b0 = A * ((A + 1.0) - (A - 1.0) * cs + sq);
            b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cs);
            b2 = A * ((A + 1.0) - (A - 1.0) * cs - sq);
            a0 = (A + 1.0) + (A - 1.0) * cs + sq;
            a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cs);
            a2 = (A + 1.0) + (A - 1.0) * cs - sq;
            break;
        }
        case BiquadBandType::HighShelf: {
            const double sq = 2.0 * std::sqrt(A) * alpha;
            b0 = A * ((A + 1.0) + (A - 1.0) * cs + sq);
            b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cs);
            b2 = A * ((A + 1.0) + (A - 1.0) * cs - sq);
            a0 = (A + 1.0) - (A - 1.0) * cs + sq;
            a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cs);
            a2 = (A + 1.0) - (A - 1.0) * cs - sq;
            break;
        }
        case BiquadBandType::Bypass:
            break;
    }
    const double inv_a0 = 1.0 / a0;
    b0_ = b0 * inv_a0;
    b1_ = b1 * inv_a0;
    b2_ = b2 * inv_a0;
    a1_ = a1 * inv_a0;
    a2_ = a2 * inv_a0;
}

void BiquadFilter::reset() noexcept {
    for (int32_t c = 0; c < kMaxCh; ++c) {
        x1_[c] = x2_[c] = y1_[c] = y2_[c] = 0.0;
    }
}

float BiquadFilter::process_sample(float x, int32_t channel) noexcept {
    const int32_t ch = std::clamp(channel, 0, kMaxCh - 1);
    const double dx = static_cast<double>(x);
    const double y = b0_ * dx + b1_ * x1_[ch] + b2_ * x2_[ch] - a1_ * y1_[ch] - a2_ * y2_[ch];
    x2_[ch] = x1_[ch];
    x1_[ch] = dx;
    y2_[ch] = y1_[ch];
    y1_[ch] = y;
    return static_cast<float>(y);
}

ParametricEQ6Band::ParametricEQ6Band() {
    configs_[0] = EqBandConfig{false, BiquadBandType::HighPass, 80.0, 0.0, 0.707};
    configs_[1] = EqBandConfig{true, BiquadBandType::LowShelf, 120.0, 0.0, 0.707};
    configs_[2] = EqBandConfig{true, BiquadBandType::Peaking, 500.0, 0.0, 1.0};
    configs_[3] = EqBandConfig{true, BiquadBandType::Peaking, 1500.0, 0.0, 1.0};
    configs_[4] = EqBandConfig{true, BiquadBandType::Peaking, 4000.0, 0.0, 1.0};
    configs_[5] = EqBandConfig{true, BiquadBandType::HighShelf, 10000.0, 0.0, 0.707};
    for (int32_t i = 0; i < kNumBands; ++i) {
        filters_[i].configure(configs_[i], 48000.0);
    }
}

void ParametricEQ6Band::set_band(int32_t idx, const EqBandConfig& cfg, double sample_rate) {
    if (idx < 0 || idx >= kNumBands) return;
    configs_[idx] = cfg;
    filters_[idx].configure(cfg, sample_rate);
}

void ParametricEQ6Band::process_inplace(AudioBuffer& buffer) {
    if (buffer.frame_count <= 0 || buffer.channels <= 0) return;
    for (int32_t b = 0; b < kNumBands; ++b) {
        if (!configs_[b].enabled || configs_[b].type == BiquadBandType::Bypass) continue;
        if (configs_[b].type == BiquadBandType::Peaking && std::abs(configs_[b].gain_db) < 1e-4) continue;
        filters_[b].configure(configs_[b], static_cast<double>(buffer.sample_rate));
        for (int64_t f = 0; f < buffer.frame_count; ++f) {
            for (int32_t ch = 0; ch < buffer.channels; ++ch) {
                const float s = buffer.sample(f, ch);
                buffer.set_sample(f, ch, filters_[b].process_sample(s, ch));
            }
        }
    }
}

void DynamicsProcessor::reset() noexcept {
    comp_env_ = 0.0f;
    gate_env_ = 1.0f;
    duck_env_ = 0.0f;
}

void DynamicsProcessor::process_inplace(AudioBuffer& main_audio, const AudioBuffer* sidechain_audio) {
    if (main_audio.frame_count <= 0 || main_audio.channels <= 0) return;
    const float sr = static_cast<float>(std::max(8000, main_audio.sample_rate));
    const float att_c = std::exp(-1.0f / (std::max(0.1f, cfg_.comp_attack_ms) * 0.001f * sr));
    const float rel_c = std::exp(-1.0f / (std::max(1.0f, cfg_.comp_release_ms) * 0.001f * sr));
    const float duck_rel = std::exp(-1.0f / (std::max(5.0f, cfg_.duck_release_ms) * 0.001f * sr));
    const float makeup = db_to_linear(cfg_.comp_makeup_db);
    const float ceiling = db_to_linear(cfg_.limiter_ceiling_db);

    for (int64_t f = 0; f < main_audio.frame_count; ++f) {
        float peak = 0.0f;
        for (int32_t c = 0; c < main_audio.channels; ++c) {
            peak = std::max(peak, std::abs(main_audio.sample(f, c)));
        }

        float total_gain = 1.0f;
        if (cfg_.gate_enabled) {
            const float in_db = linear_to_db(peak);
            const float target_g = (in_db < cfg_.gate_threshold_db) ? 0.02f : 1.0f;
            gate_env_ = 0.99f * gate_env_ + 0.01f * target_g;
            total_gain *= gate_env_;
        }

        if (cfg_.compressor_enabled) {
            const float coeff = (peak > comp_env_) ? att_c : rel_c;
            comp_env_ = coeff * comp_env_ + (1.0f - coeff) * peak;
            const float env_db = linear_to_db(comp_env_);
            float gr_db = 0.0f;
            if (env_db > cfg_.comp_threshold_db) {
                const float over = env_db - cfg_.comp_threshold_db;
                gr_db = -over * (1.0f - 1.0f / std::max(1.0f, cfg_.comp_ratio));
            }
            total_gain *= db_to_linear(gr_db) * makeup;
        }

        if (cfg_.sidechain_ducker_enabled && sidechain_audio != nullptr && f < sidechain_audio->frame_count) {
            float sc_peak = 0.0f;
            for (int32_t c = 0; c < sidechain_audio->channels; ++c) {
                sc_peak = std::max(sc_peak, std::abs(sidechain_audio->sample(f, c)));
            }
            const float sc_db = linear_to_db(sc_peak);
            const float trig = (sc_db > cfg_.duck_threshold_db) ? 1.0f : 0.0f;
            const float dc = (trig > duck_env_) ? 0.90f : duck_rel;
            duck_env_ = dc * duck_env_ + (1.0f - dc) * trig;
            total_gain *= db_to_linear(cfg_.duck_amount_db * duck_env_);
        }

        for (int32_t c = 0; c < main_audio.channels; ++c) {
            float out = main_audio.sample(f, c) * total_gain;
            if (cfg_.limiter_enabled) {
                out = std::clamp(out, -ceiling, ceiling);
            }
            main_audio.set_sample(f, c, out);
        }
    }
}

LoudnessReport LoudnessMeter::measure(const AudioBuffer& buffer) {
    LoudnessReport rep;
    if (buffer.frame_count <= 0 || buffer.channels <= 0) return rep;

    // ITU-R BS.1770 K-weighting: Stage 1 (+4 dB high-shelf ~1681 Hz) + Stage 2 (high-pass ~38 Hz)
    EqBandConfig k_shelf{true, BiquadBandType::HighShelf, 1681.97, 4.0, 0.707};
    EqBandConfig k_hp{true, BiquadBandType::HighPass, 38.13, 0.0, 0.5};
    BiquadFilter f1, f2;
    f1.configure(k_shelf, static_cast<double>(buffer.sample_rate));
    f2.configure(k_hp, static_cast<double>(buffer.sample_rate));

    std::vector<double> frame_energy(static_cast<std::size_t>(buffer.frame_count), 0.0);
    double true_peak = 0.0;

    for (int64_t f = 0; f < buffer.frame_count; ++f) {
        double ch_sq = 0.0;
        for (int32_t c = 0; c < buffer.channels; ++c) {
            const float x = buffer.sample(f, c);
            true_peak = std::max(true_peak, static_cast<double>(std::abs(x)));
            // 4x oversampled cubic Catmull-Rom inter-sample true peak check
            if (f >= 1 && f + 2 < buffer.frame_count) {
                const double p0 = buffer.sample(f - 1, c);
                const double p1 = x;
                const double p2 = buffer.sample(f + 1, c);
                const double p3 = buffer.sample(f + 2, c);
                for (int sub = 1; sub <= 3; ++sub) {
                    const double t = sub * 0.25;
                    const double t2 = t * t;
                    const double t3 = t2 * t;
                    const double interp = 0.5 * ((2.0 * p1) + (-p0 + p2) * t +
                                                 (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
                                                 (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
                    true_peak = std::max(true_peak, std::abs(interp));
                }
            }
            const float kw = f2.process_sample(f1.process_sample(x, c), c);
            ch_sq += static_cast<double>(kw) * static_cast<double>(kw);
        }
        frame_energy[static_cast<std::size_t>(f)] = ch_sq;
    }

    const double mean_e = std::accumulate(frame_energy.begin(), frame_energy.end(), 0.0) /
                          static_cast<double>(frame_energy.size());
    rep.integrated_lufs = (mean_e > 1e-12) ? (-0.691 + 10.0 * std::log10(mean_e)) : -70.0;
    rep.true_peak_dbtp = (true_peak > 1e-12) ? (20.0 * std::log10(true_peak)) : -120.0;

    auto max_window_lufs = [&](int64_t win_samples) -> double {
        if (win_samples <= 0) return rep.integrated_lufs;
        const int64_t w = std::min(win_samples, buffer.frame_count);
        double cur = 0.0;
        for (int64_t i = 0; i < w; ++i) cur += frame_energy[static_cast<std::size_t>(i)];
        double best = cur / static_cast<double>(w);
        for (int64_t i = w; i < buffer.frame_count; ++i) {
            cur += frame_energy[static_cast<std::size_t>(i)] - frame_energy[static_cast<std::size_t>(i - w)];
            best = std::max(best, cur / static_cast<double>(w));
        }
        return (best > 1e-12) ? (-0.691 + 10.0 * std::log10(best)) : -70.0;
    };

    rep.momentary_max_lufs = max_window_lufs((buffer.sample_rate * 400) / 1000);
    rep.short_term_max_lufs = max_window_lufs(buffer.sample_rate * 3);
    return rep;
}

AudioTrackStrip& AudioMixer::ensure_strip(uint64_t track_id) {
    if (auto* p = find_strip(track_id)) return *p;
    auto s = std::make_unique<AudioTrackStrip>();
    s->track_id = track_id;
    strips_.push_back(std::move(s));
    return *strips_.back();
}

AudioTrackStrip* AudioMixer::find_strip(uint64_t track_id) {
    for (auto& s : strips_) {
        if (s->track_id == track_id) return s.get();
    }
    return nullptr;
}

bool AudioMixer::mix_timeline_range(const Timeline& tl,
                                    const MediaPool& media_pool,
                                    const RationalTime& start_time,
                                    int64_t frame_count,
                                    int32_t sample_rate,
                                    AudioBuffer& out_stereo,
                                    std::optional<uint64_t> sidechain_dialogue_track_id) {
    if (frame_count <= 0 || sample_rate <= 0) return false;
    out_stereo.sample_rate = sample_rate;
    out_stereo.channels = 2;
    out_stereo.frame_count = frame_count;
    out_stereo.pts = start_time;
    out_stereo.interleaved.assign(static_cast<std::size_t>(frame_count * 2), 0.0f);

    const auto a_tracks = tl.tracks_of_type(TrackType::Audio);
    bool any_solo = false;
    for (const Track* tr : a_tracks) {
        if (tr->soloed) any_solo = true;
        if (const auto* st = find_strip(tr->id)) {
            if (st->solo.load(std::memory_order_relaxed)) any_solo = true;
        }
    }

    const RationalTime dur = RationalTime::from_samples(frame_count, sample_rate);
    const TimeRange win_range{start_time, dur};

    // Helper to render one track buffer into stereo
    auto render_track_buffer = [&](const Track& tr, AudioBuffer& tr_buf) {
        tr_buf.sample_rate = sample_rate;
        tr_buf.channels = 2;
        tr_buf.frame_count = frame_count;
        tr_buf.pts = start_time;
        tr_buf.interleaved.assign(static_cast<std::size_t>(frame_count * 2), 0.0f);

        for (const auto& clip : tr.clips) {
            if (!clip.enabled || clip.muted) continue;
            auto overlap = clip.timeline_range().intersection(win_range);
            if (!overlap.has_value()) continue;

            const MediaItem* item = media_pool.find(clip.media_id);
            if (!item || !item->decoder) continue;

            const int64_t out_offset = (overlap->start - start_time).to_samples(sample_rate);
            const int64_t count = overlap->duration.to_samples(sample_rate);
            if (count <= 0 || out_offset < 0 || out_offset + count > frame_count) continue;

            const RationalTime src_t = clip.map_timeline_to_source(overlap->start);
            AudioBuffer clip_buf;
            if (!item->decoder->decode_audio_samples(src_t, count, sample_rate, 2, clip_buf)) continue;

            const float clip_gain = db_to_linear(clip.gain_db);
            for (int64_t i = 0; i < count; ++i) {
                for (int32_t c = 0; c < 2; ++c) {
                    const float cur = tr_buf.sample(out_offset + i, c);
                    tr_buf.set_sample(out_offset + i, c, cur + clip_buf.sample(i, c) * clip_gain);
                }
            }
        }
    };

    AudioBuffer sidechain_buf;
    const AudioBuffer* sc_ptr = nullptr;
    if (sidechain_dialogue_track_id.has_value()) {
        if (const Track* sc_tr = tl.find_track(*sidechain_dialogue_track_id)) {
            render_track_buffer(*sc_tr, sidechain_buf);
            sc_ptr = &sidechain_buf;
        }
    }

    for (const Track* tr : a_tracks) {
        if (!tr->enabled || tr->muted) continue;
        AudioTrackStrip& strip = ensure_strip(tr->id);
        if (strip.mute.load(std::memory_order_relaxed)) continue;
        if (any_solo && !tr->soloed && !strip.solo.load(std::memory_order_relaxed)) continue;

        AudioBuffer tr_buf;
        render_track_buffer(*tr, tr_buf);
        strip.eq.process_inplace(tr_buf);
        strip.dynamics.process_inplace(tr_buf, (sc_ptr && tr->id != *sidechain_dialogue_track_id) ? sc_ptr : nullptr);

        const float base_gain_db = strip.fader_db.load(std::memory_order_relaxed);
        const float base_pan = strip.pan.load(std::memory_order_relaxed);
        const bool has_gain_auto = !strip.gain_automation_db.keyframes().empty();
        const bool has_pan_auto = !strip.pan_automation.keyframes().empty();

        for (int64_t i = 0; i < frame_count; ++i) {
            float g_db = base_gain_db;
            float p = base_pan;
            if (has_gain_auto || has_pan_auto) {
                const RationalTime qt = start_time + RationalTime::from_samples(i, sample_rate);
                if (has_gain_auto) g_db += static_cast<float>(strip.gain_automation_db.evaluate(qt));
                if (has_pan_auto) p = static_cast<float>(strip.pan_automation.evaluate(qt));
            }
            const float gain = db_to_linear(g_db);
            const float pan_norm = (std::clamp(p, -1.0f, 1.0f) + 1.0f) * 0.5f;
            const float lg = std::cos(pan_norm * static_cast<float>(kPi * 0.5)) * gain * 1.41421356f;
            const float rg = std::sin(pan_norm * static_cast<float>(kPi * 0.5)) * gain * 1.41421356f;

            out_stereo.set_sample(i, 0, out_stereo.sample(i, 0) + tr_buf.sample(i, 0) * lg);
            out_stereo.set_sample(i, 1, out_stereo.sample(i, 1) + tr_buf.sample(i, 1) * rg);
        }
    }
    return true;
}

void LiveVoiceoverRecorder::set_teleprompter_script(std::string script, double words_per_minute) {
    script_ = std::move(script);
    wpm_ = std::max(40.0, words_per_minute);
}

std::string LiveVoiceoverRecorder::current_teleprompter_hint(double elapsed_sec) const {
    if (script_.empty()) return "";
    std::istringstream iss(script_);
    std::vector<std::string> words;
    std::string w;
    while (iss >> w) words.push_back(std::move(w));
    if (words.empty()) return "";
    const std::size_t idx = std::min(words.size() - 1,
                                     static_cast<std::size_t>(std::max(0.0, elapsed_sec) * (wpm_ / 60.0)));
    std::string out;
    for (std::size_t i = idx; i < std::min(words.size(), idx + 8); ++i) {
        if (!out.empty()) out += ' ';
        out += words[i];
    }
    return out;
}

void LiveVoiceoverRecorder::start_countdown(double seconds) {
    countdown_rem_ = std::max(0.0, seconds);
    if (countdown_rem_ <= 0.0) {
        recording_.store(true, std::memory_order_release);
    } else {
        recording_.store(false, std::memory_order_release);
    }
}

bool LiveVoiceoverRecorder::tick_countdown(double dt_sec) {
    if (recording_.load(std::memory_order_acquire)) return true;
    countdown_rem_ = std::max(0.0, countdown_rem_ - std::max(0.0, dt_sec));
    if (countdown_rem_ <= 1e-6) {
        recording_.store(true, std::memory_order_release);
        return true;
    }
    return false;
}

std::size_t LiveVoiceoverRecorder::push_captured_samples(const float* samples, std::size_t count) {
    if (!recording_.load(std::memory_order_acquire) || samples == nullptr) return 0;
    std::size_t pushed = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (!ring_.push(samples[i])) break;
        ++pushed;
    }
    return pushed;
}

void LiveVoiceoverRecorder::drain_to_buffer(AudioBuffer& target, int32_t sample_rate, int32_t channels) {
    target.sample_rate = sample_rate;
    target.channels = channels;
    float s = 0.0f;
    while (ring_.pop(s)) {
        target.interleaved.push_back(s);
    }
    target.frame_count = static_cast<int64_t>(target.interleaved.size() / static_cast<std::size_t>(std::max(1, channels)));
}

void LiveVoiceoverRecorder::stop_recording() {
    recording_.store(false, std::memory_order_release);
}

BeatDetectionResult BeatDetector::detect_beats(const AudioBuffer& audio,
                                               std::size_t window_size,
                                               std::size_t hop_size,
                                               float sensitivity) {
    BeatDetectionResult res;
    const auto mono = extract_mono(audio);
    if (mono.size() < window_size || hop_size == 0 || audio.sample_rate <= 0) return res;

    std::vector<float> flux;
    std::vector<float> prev_mag(window_size / 2 + 1, 0.0f);
    for (std::size_t pos = 0; pos + window_size <= mono.size(); pos += hop_size) {
        auto mag = AudioDSP::compute_magnitude_spectrum(mono.data() + pos, window_size);
        double sf = 0.0;
        for (std::size_t k = 0; k < std::min(mag.size(), prev_mag.size()); ++k) {
            const float diff = mag[k] - prev_mag[k];
            if (diff > 0.0f) sf += diff;
        }
        flux.push_back(static_cast<float>(sf));
        prev_mag = std::move(mag);
    }

    const int radius = 4;
    const std::size_t min_gap_hops = static_cast<std::size_t>(
        std::max<int64_t>(1, (audio.sample_rate / 5) / static_cast<int64_t>(hop_size))); // >=200 ms between beats
    std::size_t last_beat_hop = 0;
    bool have_beat = false;

    for (std::size_t i = 1; i + 1 < flux.size(); ++i) {
        const std::size_t lo = (i > static_cast<std::size_t>(radius)) ? (i - radius) : 0;
        const std::size_t hi = std::min(flux.size() - 1, i + radius);
        double local_avg = 0.0;
        for (std::size_t j = lo; j <= hi; ++j) local_avg += flux[j];
        local_avg /= static_cast<double>(hi - lo + 1);

        if (flux[i] > flux[i - 1] && flux[i] >= flux[i + 1] &&
            flux[i] > static_cast<float>(local_avg * sensitivity) && flux[i] > 1.0f) {
            if (!have_beat || (i - last_beat_hop) >= min_gap_hops) {
                const int64_t sample_idx = static_cast<int64_t>(i * hop_size);
                res.beat_times.push_back(RationalTime::from_samples(sample_idx, audio.sample_rate));
                last_beat_hop = i;
                have_beat = true;
            }
        }
    }

    if (res.beat_times.size() >= 2) {
        std::vector<double> ibis;
        for (std::size_t i = 1; i < res.beat_times.size(); ++i) {
            const double dt = (res.beat_times[i] - res.beat_times[i - 1]).to_seconds();
            if (dt > 0.2 && dt < 2.0) ibis.push_back(dt);
        }
        if (!ibis.empty()) {
            std::sort(ibis.begin(), ibis.end());
            const double median_ibi = ibis[ibis.size() / 2];
            res.estimated_bpm = 60.0 / median_ibi;
        }
    }
    return res;
}

bool BuiltinAudioEnhancer::isolate_voice(const AudioBuffer& in, AudioBuffer& out, float strength) {
    out = in;
    if (out.frame_count <= 0) return false;
    ParametricEQ6Band eq;
    eq.set_band(0, EqBandConfig{true, BiquadBandType::HighPass, 120.0, 0.0, 0.707}, out.sample_rate);
    eq.set_band(2, EqBandConfig{true, BiquadBandType::Peaking, 2200.0, 3.5 * strength, 1.1}, out.sample_rate);
    eq.set_band(5, EqBandConfig{true, BiquadBandType::LowPass, 7500.0, 0.0, 0.707}, out.sample_rate);
    eq.process_inplace(out);
    return true;
}

bool BuiltinAudioEnhancer::remix_music_stems(const AudioBuffer& in, AudioBuffer& out,
                                             float vocals_gain, float drums_gain,
                                             float bass_gain, float other_gain) {
    out = in;
    if (out.frame_count <= 0) return false;
    // Multi-band spectral stem approximation (bass < 220 Hz, vocal presence 300-4k, air/drums > 4k)
    const float bass_db = 20.0f * std::log10(std::max(0.05f, bass_gain));
    const float vox_db = 20.0f * std::log10(std::max(0.05f, (vocals_gain + other_gain) * 0.5f));
    const float high_db = 20.0f * std::log10(std::max(0.05f, drums_gain));
    ParametricEQ6Band eq;
    eq.set_band(1, EqBandConfig{true, BiquadBandType::LowShelf, 180.0, bass_db, 0.707}, out.sample_rate);
    eq.set_band(3, EqBandConfig{true, BiquadBandType::Peaking, 1600.0, vox_db, 0.8}, out.sample_rate);
    eq.set_band(5, EqBandConfig{true, BiquadBandType::HighShelf, 5500.0, high_db, 0.707}, out.sample_rate);
    eq.process_inplace(out);
    return true;
}

} // namespace owncut::timeline

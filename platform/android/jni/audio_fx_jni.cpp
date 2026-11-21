#include <jni.h>

#include "timeline/audio_engine.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace {
using namespace owncut::timeline;

struct AudioFxState final {
    std::mutex mutex;
    int32_t sample_rate = 48000;
    int32_t channels = 2;
    std::array<float, ParametricEQ6Band::kNumBands> gains{};
    DynamicsConfig dynamics_config{};
    ParametricEQ6Band eq;
    DynamicsProcessor dynamics;

    void configure() {
        static constexpr std::array<double, ParametricEQ6Band::kNumBands> frequencies{
            60.0, 150.0, 400.0, 1000.0, 4000.0, 10000.0
        };
        for (int32_t index = 0; index < ParametricEQ6Band::kNumBands; ++index) {
            EqBandConfig band;
            band.enabled = std::abs(gains[static_cast<std::size_t>(index)]) > 0.01f;
            band.type = BiquadBandType::Peaking;
            band.freq_hz = frequencies[static_cast<std::size_t>(index)];
            band.gain_db = std::clamp(static_cast<double>(gains[static_cast<std::size_t>(index)]), -18.0, 18.0);
            band.q = 0.707;
            eq.set_band(index, band, static_cast<double>(sample_rate));
        }
        dynamics.set_config(dynamics_config);
    }
};

std::mutex registry_mutex;
std::unordered_map<uint64_t, std::shared_ptr<AudioFxState>> registry;
std::atomic<uint64_t> next_token{1};

uint64_t allocate_token() {
    auto candidate = next_token.load(std::memory_order_relaxed);
    for (;;) {
        if (candidate == 0 || candidate > static_cast<uint64_t>(INT64_MAX))
            throw std::overflow_error("audio effect token space exhausted");
        if (next_token.compare_exchange_weak(candidate, candidate + 1,
                std::memory_order_relaxed, std::memory_order_relaxed)) return candidate;
    }
}

std::shared_ptr<AudioFxState> resolve(jlong handle) {
    if (handle <= 0) return {};
    std::lock_guard lock(registry_mutex);
    const auto it = registry.find(static_cast<uint64_t>(handle));
    return it == registry.end() ? std::shared_ptr<AudioFxState>{} : it->second;
}

void throw_argument(JNIEnv* env, const char* message) {
    if (env->ExceptionCheck()) return;
    const auto type = env->FindClass("java/lang/IllegalArgumentException");
    if (type) env->ThrowNew(type, message);
}
} // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_owncut_app_NativeAudioFx_nativeCreate(JNIEnv* env, jobject,
    jfloatArray gains, jboolean compressor, jfloat threshold, jfloat ratio,
    jboolean limiter, jfloat ceiling, jboolean gate, jfloat gate_threshold,
    jint sample_rate, jint channels) {
    try {
        if (gains == nullptr || env->GetArrayLength(gains) != ParametricEQ6Band::kNumBands ||
            sample_rate < 8000 || sample_rate > 192000 || channels < 1 || channels > 8)
            throw std::invalid_argument("invalid audio effect configuration");
        std::array<jfloat, ParametricEQ6Band::kNumBands> values{};
        env->GetFloatArrayRegion(gains, 0, static_cast<jsize>(values.size()), values.data());
        if (env->ExceptionCheck()) return 0;
        auto state = std::make_shared<AudioFxState>();
        state->sample_rate = sample_rate;
        state->channels = channels;
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (!std::isfinite(values[i])) throw std::invalid_argument("non-finite EQ band");
            state->gains[i] = std::clamp(values[i], -18.0f, 18.0f);
        }
        if (!std::isfinite(threshold) || !std::isfinite(ratio) || !std::isfinite(ceiling) || !std::isfinite(gate_threshold))
            throw std::invalid_argument("non-finite dynamics setting");
        state->dynamics_config.compressor_enabled = compressor == JNI_TRUE;
        state->dynamics_config.comp_threshold_db = std::clamp(threshold, -60.0f, 0.0f);
        state->dynamics_config.comp_ratio = std::clamp(ratio, 1.0f, 20.0f);
        state->dynamics_config.limiter_enabled = limiter == JNI_TRUE;
        state->dynamics_config.limiter_ceiling_db = std::clamp(ceiling, -12.0f, 0.0f);
        state->dynamics_config.gate_enabled = gate == JNI_TRUE;
        state->dynamics_config.gate_threshold_db = std::clamp(gate_threshold, -80.0f, -6.0f);
        state->configure();
        const auto token = allocate_token();
        {
            std::lock_guard lock(registry_mutex);
            registry.emplace(token, std::move(state));
        }
        return static_cast<jlong>(token);
    } catch (const std::exception& error) {
        throw_argument(env, error.what());
        return 0;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_owncut_app_NativeAudioFx_nativeProcess(JNIEnv* env, jobject, jlong handle, jshortArray samples) {
    try {
        auto state = resolve(handle);
        if (!state || samples == nullptr) throw std::invalid_argument("invalid audio effect handle or samples");
        const jsize length = env->GetArrayLength(samples);
        if (length < 0 || length % state->channels != 0) throw std::invalid_argument("PCM buffer is not channel aligned");
        jboolean copied = JNI_FALSE;
        jshort* data = env->GetShortArrayElements(samples, &copied);
        if (!data) return;
        try {
            std::lock_guard lock(state->mutex);
            AudioBuffer buffer;
            buffer.sample_rate = state->sample_rate;
            buffer.channels = state->channels;
            buffer.frame_count = length / state->channels;
            buffer.interleaved.resize(static_cast<std::size_t>(length));
            for (jsize index = 0; index < length; ++index)
                buffer.interleaved[static_cast<std::size_t>(index)] = static_cast<float>(data[index]) / 32768.0f;
            state->eq.process_inplace(buffer);
            state->dynamics.process_inplace(buffer);
            for (jsize index = 0; index < length; ++index) {
                const auto value = std::clamp(buffer.interleaved[static_cast<std::size_t>(index)], -1.0f, 0.9999695f);
                data[index] = static_cast<jshort>(std::lrint(value * 32768.0f));
            }
        } catch (...) {
            env->ReleaseShortArrayElements(samples, data, JNI_ABORT);
            throw;
        }
        env->ReleaseShortArrayElements(samples, data, 0);
    } catch (const std::exception& error) { throw_argument(env, error.what()); }
}

extern "C" JNIEXPORT void JNICALL
Java_com_owncut_app_NativeAudioFx_nativeReset(JNIEnv* env, jobject, jlong handle) {
    try {
        auto state = resolve(handle);
        if (!state) throw std::invalid_argument("invalid audio effect handle");
        std::lock_guard lock(state->mutex);
        state->eq = ParametricEQ6Band{};
        state->dynamics = DynamicsProcessor{};
        state->configure();
    } catch (const std::exception& error) { throw_argument(env, error.what()); }
}

extern "C" JNIEXPORT void JNICALL
Java_com_owncut_app_NativeAudioFx_nativeDestroy(JNIEnv*, jobject, jlong handle) {
    if (handle <= 0) return;
    std::lock_guard lock(registry_mutex);
    registry.erase(static_cast<uint64_t>(handle));
}

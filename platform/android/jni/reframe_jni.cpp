#include <jni.h>

#include "timeline/vision_engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using owncut::timeline::FrameMotionSample;
using owncut::timeline::ReframeCropBox;
using owncut::timeline::SmartReframe;
using owncut::timeline::StabilizationMode;
using owncut::timeline::StabilizationSettings;
using owncut::timeline::Stabilizer;
using owncut::timeline::VideoFrame;

void raise_illegal_argument(JNIEnv* env, const char* message) {
    if (env->ExceptionCheck()) return;
    jclass type = env->FindClass("java/lang/IllegalArgumentException");
    if (type != nullptr) env->ThrowNew(type, message);
}

bool validate_frame_array(JNIEnv* env, jbyteArray data, jint width, jint height, jint count,
                          int64_t& expected_bytes) {
    if (data == nullptr || width < 16 || height < 16 || width > 160 || height > 160 ||
        static_cast<int64_t>(width) * height > 160 * 90 || count < 1 || count > 120) return false;
    expected_bytes = static_cast<int64_t>(width) * height * count * 4;
    return expected_bytes <= std::numeric_limits<jsize>::max() &&
        static_cast<int64_t>(env->GetArrayLength(data)) == expected_bytes;
}

std::vector<VideoFrame> frames_from_rgba(JNIEnv* env, jbyteArray data, jint width, jint height,
                                          jint count, int64_t expected_bytes) {
    std::vector<jbyte> bytes(static_cast<std::size_t>(expected_bytes));
    env->GetByteArrayRegion(data, 0, static_cast<jsize>(expected_bytes), bytes.data());
    if (env->ExceptionCheck()) return {};
    const std::size_t frame_values = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    std::vector<VideoFrame> frames(static_cast<std::size_t>(count));
    for (jint index = 0; index < count; ++index) {
        VideoFrame& frame = frames[static_cast<std::size_t>(index)];
        frame.width = width;
        frame.height = height;
        frame.stride_floats = width * 4;
        frame.bit_depth = 8;
        frame.owned_rgba32f.resize(frame_values);
        const std::size_t base = static_cast<std::size_t>(index) * frame_values;
        for (std::size_t pixel = 0; pixel < frame_values; pixel += 4) {
            for (std::size_t channel = 0; channel < 4; ++channel) {
                const auto value = static_cast<uint8_t>(bytes[base + pixel + channel]);
                frame.owned_rgba32f[pixel + channel] = static_cast<float>(value) / 255.0f;
            }
        }
    }
    return frames;
}

jfloatArray make_result(JNIEnv* env, const std::vector<jfloat>& packed) {
    jfloatArray result = env->NewFloatArray(static_cast<jsize>(packed.size()));
    if (result == nullptr) return nullptr;
    env->SetFloatArrayRegion(result, 0, static_cast<jsize>(packed.size()), packed.data());
    return result;
}
} // namespace

extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_owncut_app_NativeReframe_nativeAnalyzeRgba(JNIEnv* env, jobject,
    jbyteArray rgba_frames, jint width, jint height, jint frame_count,
    jint aspect_num, jint aspect_den, jfloat smoothing) {
    try {
        int64_t expected_bytes = 0;
        const double aspect = static_cast<double>(aspect_num) / static_cast<double>(std::max(1, aspect_den));
        if (!validate_frame_array(env, rgba_frames, width, height, frame_count, expected_bytes) ||
            aspect_num < 1 || aspect_num > 10000 || aspect_den < 1 || aspect_den > 10000 ||
            aspect < 0.3 || aspect > 3.2 || !std::isfinite(smoothing)) {
            throw std::invalid_argument("Invalid offline reframe input bounds");
        }
        auto frames = frames_from_rgba(env, rgba_frames, width, height, frame_count, expected_bytes);
        if (env->ExceptionCheck()) return nullptr;
        const auto boxes = SmartReframe::compute_reframe_trajectory(
            frames, aspect_num, aspect_den, std::clamp(smoothing, 0.0f, 1.0f));
        if (boxes.size() != frames.size()) throw std::runtime_error("Native tracker returned incomplete trajectory");
        std::vector<jfloat> packed;
        packed.reserve(boxes.size() * 4);
        for (const ReframeCropBox& box : boxes) {
            packed.push_back(box.center_x_norm);
            packed.push_back(box.center_y_norm);
            packed.push_back(box.crop_width_norm);
            packed.push_back(box.crop_height_norm);
        }
        return make_result(env, packed);
    } catch (const std::exception& error) {
        raise_illegal_argument(env, error.what());
        return nullptr;
    }
}

extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_owncut_app_NativeReframe_nativeAnalyzeStabilization(JNIEnv* env, jobject,
    jbyteArray rgba_frames, jint width, jint height, jint frame_count,
    jfloat smoothness, jboolean camera_lock, jfloat max_zoom) {
    try {
        int64_t expected_bytes = 0;
        if (!validate_frame_array(env, rgba_frames, width, height, frame_count, expected_bytes) ||
            !std::isfinite(smoothness) || !std::isfinite(max_zoom)) {
            throw std::invalid_argument("Invalid offline stabilization input bounds");
        }
        auto frames = frames_from_rgba(env, rgba_frames, width, height, frame_count, expected_bytes);
        if (env->ExceptionCheck()) return nullptr;
        StabilizationSettings settings;
        settings.mode = StabilizationMode::Similarity;
        settings.smoothness = std::clamp(smoothness, 0.0f, 1.0f);
        settings.use_kalman_filter = true;
        settings.camera_lock = camera_lock == JNI_TRUE;
        settings.zoom_adaptive = true;
        settings.max_zoom = std::clamp(max_zoom, 1.0f, 1.5f);
        const auto analysis = Stabilizer::analyze_sequence(frames, settings);
        if (analysis.samples.size() != frames.size()) throw std::runtime_error("Native stabilizer returned incomplete trajectory");
        std::vector<jfloat> packed;
        packed.reserve(analysis.samples.size() * 4);
        const float inv_width = 1.0f / static_cast<float>(width);
        const float inv_height = 1.0f / static_cast<float>(height);
        for (const FrameMotionSample& sample : analysis.samples) {
            packed.push_back(-sample.corr_dx * inv_width);
            packed.push_back(sample.corr_dy * inv_height);
            packed.push_back(sample.corr_dtheta);
            packed.push_back(analysis.adaptive_zoom);
        }
        return make_result(env, packed);
    } catch (const std::exception& error) {
        raise_illegal_argument(env, error.what());
        return nullptr;
    }
}

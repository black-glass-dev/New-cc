// JNI bridge for the FP32 CPU grade (editor::color::fp32::Rec709Grader). Used by the 16/32-bit export loop.
// Opaque handle per clip grade. Buffers are direct: RGBA8 input (pixels*4 bytes), RGBA32F output (pixels*4 floats).
// Errors throw java.lang.IllegalStateException or IllegalArgumentException. Nothing is silently skipped.

#include <jni.h>

#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "editor/color.hpp"
#include "editor/color_fp32.hpp"

namespace {

void throw_java(JNIEnv* env, const char* cls_name, const std::string& message) {
    jclass cls = env->FindClass(cls_name);
    if (cls != nullptr) env->ThrowNew(cls, message.c_str());
}

editor::color::fp32::Rec709Grader* as_grader(jlong handle) {
    return reinterpret_cast<editor::color::fp32::Rec709Grader*>(handle);
}

}  // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_owncut_app_GradeEngine_nativeOpen(JNIEnv* env, jclass, jdoubleArray parameters, jboolean enabled) {
    if (parameters == nullptr || env->GetArrayLength(parameters) != static_cast<jsize>(editor::color::parameter_count)) {
        throw_java(env, "java/lang/IllegalArgumentException", "grade needs 17 parameters");
        return 0;
    }
    std::vector<double> values(editor::color::parameter_count);
    env->GetDoubleArrayRegion(parameters, 0, static_cast<jsize>(values.size()), values.data());
    try {
        const auto grade = editor::color::from_parameters(values, enabled == JNI_TRUE);
        return reinterpret_cast<jlong>(new editor::color::fp32::Rec709Grader(grade));
    } catch (const std::exception& error) {
        throw_java(env, "java/lang/IllegalArgumentException", std::string("grade rejected: ") + error.what());
        return 0;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_owncut_app_GradeEngine_nativeApply(JNIEnv* env, jclass, jlong handle, jobject rgba8, jobject out, jint pixels) {
    auto* grader = as_grader(handle);
    const auto* in = static_cast<const std::uint8_t*>(env->GetDirectBufferAddress(rgba8));
    auto* dst = static_cast<float*>(env->GetDirectBufferAddress(out));
    if (grader == nullptr || in == nullptr || dst == nullptr || pixels <= 0) {
        throw_java(env, "java/lang/IllegalStateException", "grade apply needs an open grade and direct buffers");
        return;
    }
    // Buffer sizes are checked by GradeEngine.apply (Kotlin), where element counts are unambiguous.
    const auto count = static_cast<std::size_t>(pixels);
    try {
        static_assert(sizeof(editor::color::fp32::RgbaF32) == 4 * sizeof(float));
        auto* pixel_out = reinterpret_cast<editor::color::fp32::RgbaF32*>(dst);
        grader->apply_rgba8(std::span<const std::uint8_t>(in, count * 4), std::span<editor::color::fp32::RgbaF32>(pixel_out, count));
    } catch (const std::exception& error) {
        throw_java(env, "java/lang/IllegalStateException", std::string("grade failed: ") + error.what());
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_owncut_app_GradeEngine_nativeClose(JNIEnv*, jclass, jlong handle) {
    delete as_grader(handle);
}

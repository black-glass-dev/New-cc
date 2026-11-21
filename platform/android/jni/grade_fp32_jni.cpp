// JNI bridge for the FP32 grade used by the 16/32-bit export loop (GradeEngine.kt).
// Two backends, chosen once at open time:
//  * Vulkan compute (editor::color::vulkan::ExposureCompute::grade) when a device with the required formats exists.
//  * CPU reference (editor::color::fp32::Rec709Grader) otherwise.
// Both implement the same Rec.709 grade arithmetic in binary32. The backend name is reported by nativeBackend, so the
// caller can log which one ran. A device that fails during apply throws; the export never switches backend mid-clip.
// Opaque handle per clip grade. Buffers are direct: RGBA8 input (w*h*4 bytes), RGBA32F output (w*h*4 floats).
// Errors throw java.lang.IllegalStateException or IllegalArgumentException. Nothing is silently skipped.

#include <jni.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "editor/color.hpp"
#include "editor/color_fp32.hpp"
#include "editor/color_vulkan.hpp"

namespace {

namespace fp32 = editor::color::fp32;
namespace vk = editor::color::vulkan;

struct GradeHandle {
    editor::color::Grade grade{};
    std::unique_ptr<fp32::Rec709Grader> cpu;     // set when the CPU reference is used
    std::unique_ptr<vk::ExposureCompute> gpu;    // set when the Vulkan device is used
    std::string backend;
};

void throw_java(JNIEnv* env, const char* cls_name, const std::string& message) {
    jclass cls = env->FindClass(cls_name);
    if (cls != nullptr) env->ThrowNew(cls, message.c_str());
}

GradeHandle* as_handle(jlong handle) { return reinterpret_cast<GradeHandle*>(handle); }

// Shared parameter decoding. Returns false (exception pending) when the array is malformed or rejected.
bool read_grade(JNIEnv* env, jdoubleArray parameters, jboolean enabled, editor::color::Grade& out) {
    if (parameters == nullptr || env->GetArrayLength(parameters) != static_cast<jsize>(editor::color::parameter_count)) {
        throw_java(env, "java/lang/IllegalArgumentException", "grade needs 17 parameters");
        return false;
    }
    std::vector<double> values(editor::color::parameter_count);
    env->GetDoubleArrayRegion(parameters, 0, static_cast<jsize>(values.size()), values.data());
    try {
        out = editor::color::from_parameters(values, enabled == JNI_TRUE);
        return true;
    } catch (const std::exception& error) {
        throw_java(env, "java/lang/IllegalArgumentException", std::string("grade rejected: ") + error.what());
        return false;
    }
}

}  // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_owncut_app_GradeEngine_nativeOpenBackend(JNIEnv* env, jclass, jdoubleArray parameters, jboolean enabled,
                                                  jboolean allow_vulkan) {
    auto handle = std::make_unique<GradeHandle>();
    if (!read_grade(env, parameters, enabled, handle->grade)) return 0;
    try {
        // Validates the grade up front, before either backend is opened.
        handle->cpu = std::make_unique<fp32::Rec709Grader>(handle->grade);
    } catch (const std::exception& error) {
        throw_java(env, "java/lang/IllegalArgumentException", std::string("grade rejected: ") + error.what());
        return 0;
    }
    handle->backend = "cpu-fp32";
    if (allow_vulkan == JNI_TRUE) {
        try {
            handle->gpu = std::make_unique<vk::ExposureCompute>();
            handle->cpu.reset();  // the device path owns the grade from here on
            handle->backend = "vulkan:" + handle->gpu->device_name();
        } catch (const vk::NoVulkanDevice&) {
            // No device: the CPU reference is the explicit fallback. Reported through nativeBackend.
            handle->gpu.reset();
        } catch (const std::exception& error) {
            throw_java(env, "java/lang/IllegalStateException", std::string("Vulkan setup failed: ") + error.what());
            return 0;
        }
    }
    return reinterpret_cast<jlong>(handle.release());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_owncut_app_GradeEngine_nativeBackend(JNIEnv* env, jclass, jlong handle) {
    GradeHandle* h = as_handle(handle);
    if (h == nullptr) {
        throw_java(env, "java/lang/IllegalStateException", "grade is closed");
        return nullptr;
    }
    return env->NewStringUTF(h->backend.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_owncut_app_GradeEngine_nativeApply(JNIEnv* env, jclass, jlong handle, jobject rgba8, jobject out, jint width,
                                            jint height) {
    GradeHandle* h = as_handle(handle);
    const auto* in = static_cast<const std::uint8_t*>(env->GetDirectBufferAddress(rgba8));
    auto* dst = static_cast<float*>(env->GetDirectBufferAddress(out));
    if (h == nullptr || (h->cpu == nullptr && h->gpu == nullptr) || in == nullptr || dst == nullptr || width <= 0 ||
        height <= 0) {
        throw_java(env, "java/lang/IllegalStateException", "grade apply needs an open grade and direct buffers");
        return;
    }
    // Buffer element counts are checked by GradeEngine.grade (Kotlin), where they are unambiguous.
    const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    try {
        if (h->gpu) {
            const auto pixels = h->gpu->grade(std::span<const std::uint8_t>(in, count * 4),
                                              static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
                                              h->grade);
            static_assert(sizeof(fp32::RgbaF32) == 4 * sizeof(float));
            std::memcpy(dst, pixels.data(), pixels.size() * sizeof(fp32::RgbaF32));
            return;
        }
        static_assert(sizeof(fp32::RgbaF32) == 4 * sizeof(float));
        auto* pixel_out = reinterpret_cast<fp32::RgbaF32*>(dst);
        h->cpu->apply_rgba8(std::span<const std::uint8_t>(in, count * 4), std::span<fp32::RgbaF32>(pixel_out, count));
    } catch (const std::exception& error) {
        throw_java(env, "java/lang/IllegalStateException", std::string("grade failed: ") + error.what());
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_owncut_app_GradeEngine_nativeClose(JNIEnv*, jclass, jlong handle) {
    delete as_handle(handle);
}

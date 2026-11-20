// FP32 exposure renderer for the 16/32-bit export loop.
// Uses the Vulkan compute path when a device exists, otherwise the CPU FP32 reference
// (editor::color::fp32::rec709_exposure). Both produce the same float RGBA layout, so every phone can export;
// only the time taken depends on the hardware. Errors throw java.lang.IllegalStateException.

#include <jni.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "editor/color_fp32.hpp"
#include "editor/color_vulkan.hpp"

namespace {

namespace fp32 = editor::color::fp32;
namespace vk = editor::color::vulkan;

struct Renderer {
    std::unique_ptr<vk::ExposureCompute> gpu;  // null when the CPU reference is used
    std::string backend;
};

void throw_java(JNIEnv* env, const std::string& message) {
    jclass cls = env->FindClass("java/lang/IllegalStateException");
    if (cls != nullptr) env->ThrowNew(cls, message.c_str());
}

Renderer* as_renderer(jlong handle) { return reinterpret_cast<Renderer*>(handle); }

}  // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_owncut_app_ExposureRenderer_nativeCreate(JNIEnv* env, jclass, jboolean allow_vulkan) {
    auto renderer = std::make_unique<Renderer>();
    if (allow_vulkan) {
        try {
            renderer->gpu = std::make_unique<vk::ExposureCompute>();
            renderer->backend = "vulkan:" + renderer->gpu->device_name();
        } catch (const vk::NoVulkanDevice&) {
            renderer->gpu.reset();  // no device on this phone: use the CPU reference, same maths
        } catch (const std::exception& error) {
            throw_java(env, std::string("Vulkan setup failed: ") + error.what());
            return 0;
        }
    }
    if (!renderer->gpu) renderer->backend = "cpu-fp32";
    return reinterpret_cast<jlong>(renderer.release());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_owncut_app_ExposureRenderer_nativeBackend(JNIEnv* env, jclass, jlong handle) {
    Renderer* renderer = as_renderer(handle);
    if (renderer == nullptr) return env->NewStringUTF("closed");
    return env->NewStringUTF(renderer->backend.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_owncut_app_ExposureRenderer_nativeRender(JNIEnv* env, jclass, jlong handle, jobject rgba8, jint width,
                                                  jint height, jfloat stops, jobject out) {
    Renderer* renderer = as_renderer(handle);
    const auto* input = static_cast<const std::uint8_t*>(env->GetDirectBufferAddress(rgba8));
    auto* output = static_cast<float*>(env->GetDirectBufferAddress(out));
    if (renderer == nullptr || input == nullptr || output == nullptr || width <= 0 || height <= 0) {
        throw_java(env, "exposure render needs an open renderer and direct buffers");
        return;
    }
    const std::size_t pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const jlong input_capacity = env->GetDirectBufferCapacity(rgba8);
    const jlong output_capacity = env->GetDirectBufferCapacity(out);
    // GetDirectBufferCapacity counts elements of the buffer type: bytes for input, floats for the output view.
    if (input_capacity < static_cast<jlong>(pixels * 4) || output_capacity < static_cast<jlong>(pixels * 4)) {
        throw_java(env, "exposure render buffers are smaller than width*height");
        return;
    }
    try {
        std::span<const std::uint8_t> bytes(input, pixels * 4);
        if (renderer->gpu) {
            const std::vector<fp32::RgbaF32> result = renderer->gpu->render(
                bytes, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), stops);
            std::memcpy(output, result.data(), result.size() * sizeof(fp32::RgbaF32));
            return;
        }
        std::vector<fp32::RgbaF32> source(pixels);
        for (std::size_t i = 0; i < pixels; ++i) {
            source[i] = {input[i * 4] / 255.0f, input[i * 4 + 1] / 255.0f, input[i * 4 + 2] / 255.0f,
                         input[i * 4 + 3] / 255.0f};
        }
        std::vector<fp32::RgbaF32> graded(pixels);
        fp32::rec709_exposure(source, graded, stops);
        std::memcpy(output, graded.data(), graded.size() * sizeof(fp32::RgbaF32));
    } catch (const std::exception& error) {
        throw_java(env, std::string("exposure render failed: ") + error.what());
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_owncut_app_ExposureRenderer_nativeClose(JNIEnv*, jclass, jlong handle) {
    delete as_renderer(handle);
}

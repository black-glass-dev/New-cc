// JNI bridge for the standalone modules/color_engine reference (CPU path only).
// Diagnostic self-test for the Android APK: renders a synthetic gradient through the module's
// grade DAG (Primaries -> Lut1D -> rounded PowerWindow) and returns ASCII numbers for inspection.
// This is not a phone performance, GPU, or codec measurement.
#include <jni.h>
#include "color/color_pipeline_api.hpp"
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <vector>

namespace {
std::string run_self_test(int width, int height) {
    if (width < 8 || height < 8 || width > 512 || height > 512) throw std::invalid_argument("size must be 8..512");
    const auto w = static_cast<std::size_t>(width), h = static_cast<std::size_t>(height);
    color::Image32F input(w, h), output(w, h);
    for (std::size_t y = 0; y < h; ++y)
        for (std::size_t x = 0; x < w; ++x)
            input.pixels()[y * w + x] = {static_cast<float>(x) / static_cast<float>(w - 1),
                                         static_cast<float>(y) / static_cast<float>(h - 1), 0.25F, 1.F};

    auto lut = std::make_shared<const color::Lut1D>(std::vector<color::Vec3>{{0, 0, 0}, {.5F, .5F, .5F}, {1, 1, 1}});
    color::GradeProgram program;
    color::Node primaries; primaries.id = 1;
    color::Primaries p; p.exposure = .2F; p.saturation = 1.1F; primaries.operation = p;
    program.clip.add(primaries);
    color::Node curve; curve.id = 2; curve.inputs = {1}; curve.operation = lut;
    program.clip.add(curve);
    color::Node window; window.id = 3; window.inputs = {2};
    color::PowerWindow pw; pw.shape = color::PowerWindow::Shape::RoundedRectangle;
    pw.cx = .5F; pw.cy = .5F; pw.sx = pw.sy = .3F; pw.corner_radius = .1F; pw.feather = 0;
    window.operation = pw;
    program.clip.add(window);

    auto engine = color::create_engine();
    engine->RenderGradeFrame(program, input.view(), output.view(), {});

    const auto at = [&](std::size_t x, std::size_t y) { return output.pixels()[y * w + x]; };
    const auto center = at(w / 2, h / 2), corner = at(0, 0);
    char line[512];
    std::snprintf(line, sizeof line,
                  "Color module CPU self-test %dx%d\n"
                  "center rgba=(%.4f, %.4f, %.4f, %.4f)\n"
                  "corner rgba=(%.4f, %.4f, %.4f, %.4f)\n"
                  "Nodes: Primaries -> Lut1D -> RoundedRectangle window\n"
                  "Host CPU only; not device, GPU, thermal or codec measurement.",
                  width, height, center.r, center.g, center.b, center.a, corner.r, corner.g, corner.b, corner.a);
    return line;
}
} // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_owncut_app_ColorModuleDiagnostics_nativeRunSelfTest(JNIEnv* env, jobject, jint width, jint height) {
    try {
        const auto text = run_self_test(width, height);
        return env->NewStringUTF(text.c_str());
    } catch (const std::exception& e) {
        if (!env->ExceptionCheck()) {
            const auto type = env->FindClass("java/lang/IllegalStateException");
            if (type) { env->ThrowNew(type, e.what()); env->DeleteLocalRef(type); }
        }
        return nullptr;
    }
}

// Vulkan compute exposure stage vs the FP32 CPU reference.
// Exit codes: 0 = all checks passed on a real device; 77 = no Vulkan device (CTest SKIP_RETURN_CODE);
// 1 = a check failed. A 77 is NOT a GPU result: it means this host could not run the device path.
#include "editor/color_fp32.hpp"
#include "editor/color_vulkan.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <limits>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <string>
#include <vector>

using namespace editor::color;

namespace {

constexpr int kSkipCode = 77;
// Tolerance for pow() on the device vs the CPU reference (binary32 on both sides).
constexpr float kTolerance = 1e-4f;

void check(bool condition, std::source_location at = std::source_location::current()) {
    if (!condition) throw std::runtime_error("Vulkan colour assertion line " + std::to_string(at.line()));
}

template <class F>
void fails(F f) {
    bool threw = false;
    try { f(); } catch (const std::exception&) { threw = true; }
    check(threw);
}

// Deterministic RGBA8 test pattern covering the full byte range, with varying alpha.
std::vector<std::uint8_t> pattern(std::uint32_t width, std::uint32_t height) {
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * height * 4);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4;
            bytes[i + 0] = static_cast<std::uint8_t>((x * 255u) / (width - 1));
            bytes[i + 1] = static_cast<std::uint8_t>((y * 255u) / (height - 1));
            bytes[i + 2] = static_cast<std::uint8_t>(((x + y) * 7u) & 255u);
            bytes[i + 3] = static_cast<std::uint8_t>(255u - ((x * 3u + y) & 127u));
        }
    }
    return bytes;
}

std::vector<fp32::RgbaF32> cpu_reference(const std::vector<std::uint8_t>& bytes, float stops) {
    std::vector<fp32::RgbaF32> in(bytes.size() / 4), out(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        in[i] = {bytes[i * 4] / 255.0f, bytes[i * 4 + 1] / 255.0f, bytes[i * 4 + 2] / 255.0f, bytes[i * 4 + 3] / 255.0f};
    }
    fp32::rec709_exposure(in, out, stops);
    return out;
}

// Returns the worst absolute channel error; alpha must match exactly.
float compare(const std::vector<fp32::RgbaF32>& gpu, const std::vector<fp32::RgbaF32>& cpu) {
    check(gpu.size() == cpu.size());
    float worst = 0.0f;
    for (std::size_t i = 0; i < gpu.size(); ++i) {
        worst = std::max({worst, std::fabs(gpu[i].r - cpu[i].r), std::fabs(gpu[i].g - cpu[i].g),
                          std::fabs(gpu[i].b - cpu[i].b)});
        check(gpu[i].a == cpu[i].a);
    }
    return worst;
}

int run() {
    std::unique_ptr<vulkan::ExposureCompute> compute;
    try {
        compute = std::make_unique<vulkan::ExposureCompute>();
    } catch (const vulkan::NoVulkanDevice& error) {
        std::cout << "SKIP: " << error.what() << " (device path not executed on this host)\n";
        return kSkipCode;
    }
    std::cout << "Vulkan device: " << compute->device_name() << '\n';

    const std::uint32_t width = 64, height = 48;
    const auto bytes = pattern(width, height);
    float worst_overall = 0.0f;
    for (float stops : {-1.0f, 0.0f, 0.5f, 1.0f, 2.0f}) {
        const auto gpu = compute->render(bytes, width, height, stops);
        const float worst = compare(gpu, cpu_reference(bytes, stops));
        worst_overall = std::max(worst_overall, worst);
        std::printf("stops=%+.1f worst channel error vs CPU reference: %.3g\n", stops, worst);
        check(worst <= kTolerance);
    }

    // FP32 output survives above display white: full-white input at +2 stops must exceed 1.0 on the device.
    const std::vector<std::uint8_t> white{255, 255, 255, 255};
    const auto boosted = compute->render(white, 1, 1, 2.0f);
    check(boosted[0].r > 1.0f && boosted[0].g > 1.0f && boosted[0].b > 1.0f);
    const float expected_boost = fp32::rec709_from_linear(fp32::rec709_to_linear(1.0f) * 4.0f);
    check(std::fabs(boosted[0].r - expected_boost) < kTolerance);

    // Zero exposure is identity to within transfer-function rounding.
    const auto identity = compute->render(bytes, width, height, 0.0f);
    for (std::size_t i = 0; i < identity.size(); ++i) {
        check(std::fabs(identity[i].r - bytes[i * 4] / 255.0f) < kTolerance);
    }

    // Fail-closed input validation happens before any device work.
    fails([&] { compute->render(bytes, width, height + 1, 0.0f); });
    fails([&] { compute->render(bytes, 0, height, 0.0f); });
    fails([&] { compute->render(bytes, width, height, std::numeric_limits<float>::infinity()); });
    fails([&] { compute->render(bytes, width, height, 9.0f); });

    // FP32 grade: the device must match fp32::Rec709Grader (color::Pipeline::linear in double, stored as float).
    // Mixed grade touches every parameter: exposure, balance, lift, gamma, gain, contrast, saturation, strength.
    Grade mixed;
    mixed.exposure_stops = 0.5;
    mixed.contrast = 1.2;
    mixed.saturation = 0.8;
    mixed.pivot = 0.18;
    mixed.strength = 0.9;
    mixed.balance_stops = {0.1, -0.1, 0.0};
    mixed.lift = {0.02, 0.0, -0.02};
    mixed.gamma = {1.1, 0.95, 1.0};
    mixed.gain = {1.05, 1.0, 0.97};
    Grade extreme_gamma;
    extreme_gamma.gamma = {2.5, 0.4, 1.0};
    extreme_gamma.saturation = 0.0;
    const std::vector<Grade> grades{Grade{}, mixed, extreme_gamma};
    float worst_grade = 0.0f;
    for (const Grade& grade : grades) {
        const fp32::Rec709Grader grader(grade);
        std::vector<fp32::RgbaF32> cpu(static_cast<std::size_t>(width) * height);
        grader.apply_rgba8(bytes, cpu);
        const auto gpu = compute->grade(bytes, width, height, grade);
        check(gpu.size() == cpu.size());
        float worst = 0.0f;
        for (std::size_t i = 0; i < gpu.size(); ++i) {
            worst = std::max({worst, std::fabs(gpu[i].r - cpu[i].r), std::fabs(gpu[i].g - cpu[i].g),
                              std::fabs(gpu[i].b - cpu[i].b)});
            check(gpu[i].a == cpu[i].a);
        }
        worst_grade = std::max(worst_grade, worst);
        std::printf("grade worst channel error vs CPU reference: %.3g\n", worst);
        check(worst <= kTolerance);
    }
    // Neutral grade is a byte-for-byte copy of the decoded RGBA8 values (identity flag), not a transfer round trip.
    {
        const auto neutral = compute->grade(bytes, width, height, Grade{});
        for (std::size_t i = 0; i < neutral.size(); ++i) {
            check(neutral[i].r == bytes[i * 4] / 255.0f);
            check(neutral[i].b == bytes[i * 4 + 2] / 255.0f);
        }
    }
    // Grade keeps values above display white and below zero on the device (no clamp on the GPU).
    {
        Grade boost;
        boost.gain = {4.0, 4.0, 4.0};
        const auto out = compute->grade(white, 1, 1, boost);
        check(out[0].r > 1.0f);
    }
    // Invalid grades and buffers fail before any device work.
    fails([&] { Grade bad; bad.exposure_stops = 9.0; compute->grade(bytes, width, height, bad); });
    fails([&] { compute->grade(bytes, width, height + 1, Grade{}); });
    fails([&] { compute->grade(bytes, 0, height, Grade{}); });

    std::printf("Vulkan grade compute vs FP32 reference passed (worst error %.3g, tolerance %.3g)\n", worst_grade,
                kTolerance);
    std::printf("Vulkan exposure compute vs FP32 reference passed (worst error %.3g, tolerance %.3g)\n",
                worst_overall, kTolerance);
    return 0;
}

} // namespace

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}

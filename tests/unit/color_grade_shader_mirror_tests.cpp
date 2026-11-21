// Host check for engine/color/shaders/grade_fp32.comp without a GPU: replays the shader's binary32 math, operation
// for operation, and compares it with fp32::Rec709Grader (the oracle the device path is tested against).
// If the shader's formula changes and this mirror does not, the device test and this test will disagree.
#include "editor/color.hpp"
#include "editor/color_fp32.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <stdexcept>
#include <vector>

using namespace editor::color;

namespace {

struct V3 { float x, y, z; };
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, V3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }

float sgn(float v) { return v > 0 ? 1.0f : (v < 0 ? -1.0f : 0.0f); }
float to_lin(float e) {
    const float a = std::fabs(e);
    const float l = a < 0.081f ? a / 4.5f : std::pow((a + 0.099f) / 1.099f, 1.0f / 0.45f);
    return e < 0 ? -l : l;
}
float from_lin(float l) {
    const float a = std::fabs(l);
    const float e = a < 0.018f ? a * 4.5f : 1.099f * std::pow(a, 0.45f) - 0.099f;
    return l < 0 ? -e : e;
}

// Push-constant block as the host computes it (ExposureCompute::grade).
struct Push {
    V3 scale, lift, gain, inverse_gamma;
    float pivot, contrast, saturation, strength;
    bool identity;
};

Push make_push(const Grade& g) {
    Push p{};
    p.scale = V3{static_cast<float>(std::exp2(g.exposure_stops + g.balance_stops[0])),
                 static_cast<float>(std::exp2(g.exposure_stops + g.balance_stops[1])),
                 static_cast<float>(std::exp2(g.exposure_stops + g.balance_stops[2]))};
    p.lift = V3{static_cast<float>(g.lift[0]), static_cast<float>(g.lift[1]), static_cast<float>(g.lift[2])};
    p.gain = V3{static_cast<float>(g.gain[0]), static_cast<float>(g.gain[1]), static_cast<float>(g.gain[2])};
    p.inverse_gamma = V3{static_cast<float>(1.0 / g.gamma[0]), static_cast<float>(1.0 / g.gamma[1]),
                         static_cast<float>(1.0 / g.gamma[2])};
    p.pivot = static_cast<float>(g.pivot);
    p.contrast = static_cast<float>(g.contrast);
    p.saturation = static_cast<float>(g.saturation);
    p.strength = static_cast<float>(g.strength);
    p.identity = Pipeline(g).identity();
    return p;
}

// Mirrors main() of grade_fp32.comp for one pixel.
fp32::RgbaF32 shader_pixel(const Push& p, unsigned r8, unsigned g8, unsigned b8, unsigned a8) {
    const float k = 1.0f / 255.0f;
    const float r = static_cast<float>(r8) * k, g = static_cast<float>(g8) * k;
    const float b = static_cast<float>(b8) * k, a = static_cast<float>(a8) * k;
    if (p.identity) return {r, g, b, a};
    const V3 original{to_lin(r), to_lin(g), to_lin(b)};
    V3 x = original * p.scale;
    x = (x + p.lift * (V3{1, 1, 1} - x)) * p.gain;
    x = V3{sgn(x.x) * std::pow(std::fabs(x.x), p.inverse_gamma.x), sgn(x.y) * std::pow(std::fabs(x.y), p.inverse_gamma.y),
           sgn(x.z) * std::pow(std::fabs(x.z), p.inverse_gamma.z)};
    const float pv = p.pivot;
    x = V3{pv, pv, pv} + (x - V3{pv, pv, pv}) * p.contrast;
    const float y = x.x * 0.2126f + x.y * 0.7152f + x.z * 0.0722f;
    x = V3{y, y, y} + (x - V3{y, y, y}) * p.saturation;
    x = original + (x - original) * p.strength;
    return {from_lin(x.x), from_lin(x.y), from_lin(x.z), a};
}

void check(bool condition, int line) {
    if (!condition) throw std::runtime_error("grade shader mirror check failed at line " + std::to_string(line));
}

int run() {
    constexpr float kTolerance = 1e-4f;  // same contract as the device test
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    const auto between = [&](double lo, double hi) { return lo + (hi - lo) * unit(rng); };

    float worst = 0.0f;
    std::size_t pixels = 0;
    for (int trial = 0; trial < 120; ++trial) {
        Grade g;
        if (trial > 0) {
            g.exposure_stops = between(-2, 2);
            g.contrast = between(0, 2.5);
            g.saturation = between(0, 2.5);
            g.pivot = between(0.05, 0.5);
            g.strength = between(0, 1);
            for (std::size_t c = 0; c < 3; ++c) {
                g.balance_stops[c] = between(-1, 1);
                g.lift[c] = between(-0.3, 0.3);
                g.gamma[c] = between(0.3, 3);
                g.gain[c] = between(0, 2);
            }
        }
        const fp32::Rec709Grader grader(g);
        const Push push = make_push(g);
        for (unsigned v = 0; v < 256; v += 3) {
            for (unsigned w = 0; w < 256; w += 17) {
                const unsigned r8 = v, g8 = w, b8 = (v * 7 + w) & 255u, a8 = (v + w) & 255u;
                const std::vector<std::uint8_t> px{static_cast<std::uint8_t>(r8), static_cast<std::uint8_t>(g8),
                                                   static_cast<std::uint8_t>(b8), static_cast<std::uint8_t>(a8)};
                fp32::RgbaF32 cpu{};
                grader.apply_rgba8(px, std::span<fp32::RgbaF32>(&cpu, 1));
                const fp32::RgbaF32 mirror = shader_pixel(push, r8, g8, b8, a8);
                check(mirror.a == cpu.a, __LINE__);
                worst = std::max({worst, std::fabs(mirror.r - cpu.r), std::fabs(mirror.g - cpu.g),
                                  std::fabs(mirror.b - cpu.b)});
                ++pixels;
            }
        }
    }
    check(worst <= kTolerance, __LINE__);
    std::printf("grade shader mirror vs Rec709Grader passed: %zu pixels, worst channel error %.3g (tolerance %.3g)\n",
                pixels, worst, kTolerance);
    return 0;
}

} // namespace

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
}

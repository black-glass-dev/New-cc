#pragma once

// FP32 reference colour path (step 2). Rec.709 SDR, scene-linear working space.
//
// Contract:
//  * Every stage reads and writes RgbaF32 (four IEEE-754 binary32 channels, 128 bits per pixel).
//    There is no 8-bit, 10-bit, half or 16-bit storage between stages in this module.
//  * Transfer functions and grade arithmetic are float (binary32). No clamp happens inside the
//    pipeline; highlights above 1.0 stay above 1.0 until the explicit display conversion.
//  * This is the CPU reference and the oracle for the GPU path. It is NOT a GPU, Vulkan or device
//    result, and it does not replace the Media3 path that the app uses today.

#include "editor/color.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace editor::color::fp32 {

static_assert(sizeof(float) == 4, "FP32 path requires 32-bit float");

struct RgbaF32 final {
    float r, g, b, a;
    bool operator==(const RgbaF32&) const = default;
};
static_assert(sizeof(RgbaF32) == 16, "RGBA32F pixel must be 128 bits");

// Rec.709 OETF inverse / forward, evaluated in binary32.
float rec709_to_linear(float encoded);
float rec709_from_linear(float linear);

// Exposure in stops: linear * 2^stops, evaluated in binary32.
float exposure_scale(float stops);

// One FP32 exposure stage over an RGBA32F buffer. Input must be finite; alpha is passed through.
// Decode -> linear exposure -> encode happens in one pass, so the intermediate stays FP32.
void rec709_exposure(std::span<const RgbaF32> source, std::span<RgbaF32> destination, float exposure_stops);

// FP32 grade between Rec.709 transfers. Decode Rec.709 -> scene-linear float, apply the grade arithmetic of
// editor::color::Pipeline::linear (evaluated in double, stored as FP32), encode back to Rec.709. Alpha passes
// through. An identity grade copies values exactly. No clamp anywhere. Instances are immutable and thread-safe
// for disjoint destination spans.
class Rec709Grader final {
public:
    explicit Rec709Grader(const color::Grade& grade);
    [[nodiscard]] bool identity() const noexcept { return pipeline_.identity(); }
    // source and destination are RGBA32F with the same pixel count.
    void apply(std::span<const RgbaF32> source, std::span<RgbaF32> destination) const;
    // source: packed RGBA8 bytes (4 per pixel, value/255). destination: one RgbaF32 per pixel.
    void apply_rgba8(std::span<const std::uint8_t> source, std::span<RgbaF32> destination) const;
private:
    color::Pipeline pipeline_;
};

// No-op stage: copies FP32 values unchanged (used to test FP32 survival between stages).
void copy_stage(std::span<const RgbaF32> source, std::span<RgbaF32> destination);

// Explicit final conversion to an 8-bit display/encoder value. Clamps to [0,1] here only.
std::vector<unsigned char> to_rgba8_display(std::span<const RgbaF32> source);

// Test helper: round a binary32 value to the nearest IEEE-754 binary16 value (ties to even).
// Used as a negative control to show that a half-precision intermediate would lose the detail
// that the FP32 path keeps. Handles normal binary16 range only.
float round_to_binary16(float value);

} // namespace editor::color::fp32

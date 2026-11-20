#include "editor/color_fp32.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace editor::color::fp32 {
namespace {

// Rec.709 (BT.709-6) OETF constants. Linear segment 4.5*L below L=0.018 (encoded 0.081).
// Note: the piecewise standard has a small gap. The power branch maps L=0.018 to encoded
// 0.0812479, above 0.081, so encoded values in (0.081, 0.0812479) have no exact preimage.
// The exact round trip is therefore on the linear side: L -> encoded -> L.
constexpr float kLinearSlope = 4.5f;
constexpr float kEncodeThreshold = 0.018f;
constexpr float kDecodeThreshold = 0.081f;
constexpr float kOffset = 0.099f;
constexpr float kScale = 1.099f;
constexpr float kPowerEncode = 0.45f;

void require_finite(float value, const char* what) {
    if (!std::isfinite(value)) throw std::invalid_argument(what);
}

} // namespace

float rec709_to_linear(float encoded) {
    require_finite(encoded, "rec709_to_linear: input must be finite");
    const float a = std::fabs(encoded);
    const float linear = a < kDecodeThreshold
        ? a / kLinearSlope
        : std::pow((a + kOffset) / kScale, 1.0f / kPowerEncode);
    return std::copysign(linear, encoded);
}

float rec709_from_linear(float linear) {
    require_finite(linear, "rec709_from_linear: input must be finite");
    const float a = std::fabs(linear);
    const float encoded = a < kEncodeThreshold
        ? a * kLinearSlope
        : kScale * std::pow(a, kPowerEncode) - kOffset;
    return std::copysign(encoded, linear);
}

float exposure_scale(float stops) {
    if (!std::isfinite(stops) || stops < -8.0f || stops > 8.0f)
        throw std::invalid_argument("exposure_scale: stops must be finite and within [-8,8]");
    return std::exp2(stops);
}

void rec709_exposure(std::span<const RgbaF32> source, std::span<RgbaF32> destination, float exposure_stops) {
    if (source.size() != destination.size()) throw std::invalid_argument("rec709_exposure: size mismatch");
    const float scale = exposure_scale(exposure_stops);
    for (std::size_t i = 0; i < source.size(); ++i) {
        const RgbaF32& in = source[i];
        require_finite(in.r, "rec709_exposure: non-finite red");
        require_finite(in.g, "rec709_exposure: non-finite green");
        require_finite(in.b, "rec709_exposure: non-finite blue");
        require_finite(in.a, "rec709_exposure: non-finite alpha");
        destination[i] = RgbaF32{
            rec709_from_linear(rec709_to_linear(in.r) * scale),
            rec709_from_linear(rec709_to_linear(in.g) * scale),
            rec709_from_linear(rec709_to_linear(in.b) * scale),
            in.a,
        };
    }
}

Rec709Grader::Rec709Grader(const color::Grade& grade) : pipeline_(grade) {}

namespace {
RgbaF32 grade_pixel(const color::Pipeline& pipeline, float r, float g, float b, float a) {
    if (pipeline.identity()) return RgbaF32{r, g, b, a};
    require_finite(r, "Rec709Grader: non-finite red");
    require_finite(g, "Rec709Grader: non-finite green");
    require_finite(b, "Rec709Grader: non-finite blue");
    require_finite(a, "Rec709Grader: non-finite alpha");
    const color::Rgb linear = pipeline.linear({static_cast<double>(rec709_to_linear(r)),
                                              static_cast<double>(rec709_to_linear(g)),
                                              static_cast<double>(rec709_to_linear(b))});
    return RgbaF32{rec709_from_linear(static_cast<float>(linear.r)),
                   rec709_from_linear(static_cast<float>(linear.g)),
                   rec709_from_linear(static_cast<float>(linear.b)), a};
}
} // namespace

void Rec709Grader::apply(std::span<const RgbaF32> source, std::span<RgbaF32> destination) const {
    if (source.size() != destination.size()) throw std::invalid_argument("Rec709Grader: size mismatch");
    for (std::size_t i = 0; i < source.size(); ++i) {
        const RgbaF32& in = source[i];
        destination[i] = grade_pixel(pipeline_, in.r, in.g, in.b, in.a);
    }
}

void Rec709Grader::apply_rgba8(std::span<const std::uint8_t> source, std::span<RgbaF32> destination) const {
    if (source.size() != destination.size() * 4) throw std::invalid_argument("Rec709Grader: RGBA8 size mismatch");
    constexpr float kScale = 1.0f / 255.0f;
    for (std::size_t i = 0; i < destination.size(); ++i) {
        destination[i] = grade_pixel(pipeline_, source[i * 4] * kScale, source[i * 4 + 1] * kScale,
                                     source[i * 4 + 2] * kScale, source[i * 4 + 3] * kScale);
    }
}

void copy_stage(std::span<const RgbaF32> source, std::span<RgbaF32> destination) {
    if (source.size() != destination.size()) throw std::invalid_argument("copy_stage: size mismatch");
    std::copy(source.begin(), source.end(), destination.begin());
}

std::vector<unsigned char> to_rgba8_display(std::span<const RgbaF32> source) {
    std::vector<unsigned char> out;
    out.reserve(source.size() * 4);
    const auto byte = [](float value) {
        require_finite(value, "to_rgba8_display: non-finite value");
        const float clamped = std::clamp(value, 0.0f, 1.0f);
        return static_cast<unsigned char>(std::floor(clamped * 255.0f + 0.5f));
    };
    for (const RgbaF32& p : source) {
        out.push_back(byte(p.r));
        out.push_back(byte(p.g));
        out.push_back(byte(p.b));
        out.push_back(byte(p.a));
    }
    return out;
}

float round_to_binary16(float value) {
    require_finite(value, "round_to_binary16: input must be finite");
    if (value == 0.0f) return value;
    constexpr float kHalfMax = 65504.0f;
    if (std::fabs(value) > kHalfMax) throw std::out_of_range("round_to_binary16: beyond binary16 range");
    // binary16 keeps 11 significant bits, so the quantum at this exponent is 2^(e-11).
    int exponent = 0;
    std::frexp(value, &exponent); // value = m * 2^exponent, m in [0.5, 1)
    const float quantum = std::ldexp(1.0f, exponent - 11);
    return std::nearbyint(value / quantum) * quantum; // default rounding: ties to even
}

} // namespace editor::color::fp32

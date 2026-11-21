#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <span>
#include <stdexcept>

namespace editor::color {
struct Rgb final {
    double r, g, b;
    bool operator==(const Rgb&) const = default;
};

// Original SDR reference pipeline. Rec.709/sRGB primaries, D65; straight alpha.
// RGB balance is channel stops, NOT a calibrated Kelvin temperature control.
struct Grade final {
    bool enabled{true};
    double exposure_stops{0}; // [-8,8]
    double contrast{1};       // [0,4], affine around linear pivot
    double saturation{1};     // [0,4], linear Rec.709 luminance
    double pivot{0.18};       // [0.001,1], scene-linear
    double strength{1};       // [0,1], linear-light original/graded mix
    std::array<double,3> balance_stops{0,0,0}; // [-2,2]
    std::array<double,3> lift{0,0,0};           // [-1,1]
    std::array<double,3> gamma{1,1,1};          // [0.1,4]
    std::array<double,3> gain{1,1,1};           // [0,4]
    bool operator==(const Grade&) const = default;
};
void validate(const Grade& grade);
// Original wheel input mapping (zero-mean RGB plane), not a proprietary grading model.
// Groups: balance=0, lift=1, gamma=2, gain=3. Existing scalar/master is retained.
[[nodiscard]] Grade wheel_adjust(const Grade& base, int group, double x, double y);
[[nodiscard]] std::array<double,2> wheel_position(const Grade& grade, int group);
// Only absent or exact all-zero CTA type-1 (25-byte) padding is treated as unset.
// Any nonzero/unknown/malformed payload remains present and fails SDR admission.
[[nodiscard]] bool hdr_static_info_present(std::span<const std::uint8_t> bytes) noexcept;
inline constexpr std::size_t parameter_count = 17;
using ParameterBlock = std::array<double,parameter_count>;
// v1 narrow bridge/preset field order: five scalars, balance, lift, gamma, gain.
[[nodiscard]] ParameterBlock parameters(const Grade& grade);
[[nodiscard]] Grade from_parameters(std::span<const double> values, bool enabled = true);
// Finite signed transfer extensions; no clipping here. These are sRGB transfer
// functions, not a claim to decode every video's transfer/primaries automatically.
double decode_srgb(double encoded);
double encode_srgb(double linear);

class Pipeline final {
public:
    explicit Pipeline(Grade grade);
    [[nodiscard]] bool identity() const noexcept { return identity_; }
    // No gamut clamp: intermediate negative/highlight values remain available.
    // Reference input is bounded to +/-65504/channel; rejects NaN/Inf/out of range.
    [[nodiscard]] Rgb linear(Rgb input) const;
    [[nodiscard]] Rgb srgb(Rgb input) const;
private:
    Grade grade_;
    std::array<double,3> multipliers_;
    bool identity_;
};

struct Rgba8View final {
    std::span<const std::uint8_t> bytes;
    std::size_t width, height, stride;
};
struct Rgba8Buffer final {
    std::span<std::uint8_t> bytes;
    std::size_t width, height, stride;
};
inline constexpr std::size_t max_pixels = 64 * 1024 * 1024;
// Explicitly sRGB RGBA8, NOT YUV, HDR or premultiplied Android bitmap bytes.
// Validates parameters, dimensions, spans and non-overlap before any output write.
// Source/padding are unchanged; alpha is copied exactly. SDR clamps only at packing.
// false means cancellation: destination may be partial and MUST be discarded.
// No in-place/overlapping processing; no source media is ever modified.
[[nodiscard]] bool apply_rgba8(Rgba8View source, Rgba8Buffer output, const Grade& grade,
                               const std::atomic_bool* cancelled = nullptr);

struct FrameComparison final {
    std::uint64_t pixels{}, alpha_mismatches{};
    std::array<std::uint8_t,3> maximum_rgb_error{};
    double mean_absolute_rgb_error{}, mean_signed_rgb_error{}; // actual minus expected
};
// Byte/code-value diagnostics only: no transfer, alignment, timing or color conversion.
// Same size required; strides/padding may differ. Alias is safe; input never changes.
[[nodiscard]] FrameComparison compare_rgba8(Rgba8View expected, Rgba8View actual);

struct Histogram final {
    std::array<std::uint64_t,256> red{}, green{}, blue{}, luminance{};
    std::uint64_t pixels{0};
};
// RGB bins are encoded sRGB; luminance bins encode linear Rec.709 luminance to
// sRGB. Not a broadcast-code-value waveform, vectorscope or HDR measurement.
[[nodiscard]] Histogram histogram(Rgba8View source, bool skip_transparent = true);

class UnsupportedGradeVersion final : public std::runtime_error {
public: using std::runtime_error::runtime_error;
};
// Standalone bounded preset, NOT automatic .owncut project persistence/history.
// OWNCUTCG v1, little endian IEEE binary64; no trailing data; validates all fields.
class PresetCodec final {
public:
    static constexpr unsigned version = 1;
    static constexpr std::size_t encoded_size = 149;
    static void write(const Grade& grade, std::ostream& output);
    [[nodiscard]] static Grade read(std::istream& input);
};
} // namespace editor::color

// FP32 reference colour path: precision of intermediates, Rec.709 transfer, exposure, display conversion.
// Host CPU only. Not a GPU, Vulkan, device or codec result.
#include "editor/color_fp32.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <source_location>
#include <stdexcept>
#include <string>
#include <vector>

using namespace editor::color::fp32;

namespace {

void check(bool condition, std::source_location at = std::source_location::current()) {
    if (!condition) throw std::runtime_error("FP32 colour assertion line " + std::to_string(at.line()));
}

template <class F>
void fails(F f) {
    bool threw = false;
    try { f(); } catch (const std::exception&) { threw = true; }
    check(threw);
}

// Bit-exact comparison: the intermediate must return the same binary32 bits it was given.
bool same_bits(float a, float b) {
    std::uint32_t x = 0, y = 0;
    std::memcpy(&x, &a, sizeof x);
    std::memcpy(&y, &b, sizeof y);
    return x == y;
}

// Double-precision reference for the same Rec.709 exposure, used to bound FP32 error.
double ref_to_linear(double e) {
    const double a = std::fabs(e);
    const double l = a < 0.081 ? a / 4.5 : std::pow((a + 0.099) / 1.099, 1.0 / 0.45);
    return std::copysign(l, e);
}
double ref_from_linear(double l) {
    const double a = std::fabs(l);
    const double e = a < 0.018 ? a * 4.5 : 1.099 * std::pow(a, 0.45) - 0.099;
    return std::copysign(e, l);
}

void format_and_types() {
    static_assert(std::numeric_limits<float>::is_iec559, "binary32 must be IEEE-754");
    static_assert(sizeof(RgbaF32) == 16);
    check(std::numeric_limits<float>::digits == 24);
}

void patch_survives_fp32_intermediate() {
    // Two patches that differ only below binary16 resolution at 1.0.
    const float one = 1.0f;
    const float just_above = 1.0f + std::ldexp(1.0f, -14);
    check(one != just_above);

    const std::vector<RgbaF32> source{{one, one, one, 1.0f}, {just_above, just_above, just_above, 1.0f}};
    std::vector<RgbaF32> intermediate(source.size());
    copy_stage(source, intermediate);          // no-op stage, result written to FP32 storage
    std::vector<RgbaF32> read_back(source.size());
    copy_stage(intermediate, read_back);       // next stage reads the FP32 intermediate

    check(read_back == source);
    check(same_bits(read_back[0].r, one));
    check(same_bits(read_back[1].r, just_above));
    check(read_back[0].r != read_back[1].r);
}

void half_precision_control_collapses_patch() {
    // Negative control: the same patch through binary16 becomes identical, so a hidden FP16
    // intermediate would be visible to the test above.
    const float just_above = 1.0f + std::ldexp(1.0f, -14);
    check(round_to_binary16(1.0f) == 1.0f);
    check(round_to_binary16(just_above) == 1.0f);
    check(round_to_binary16(just_above) != just_above);
    check(round_to_binary16(1.0f + std::ldexp(1.0f, -10)) == 1.0f + std::ldexp(1.0f, -10));
    fails([] { round_to_binary16(std::numeric_limits<float>::infinity()); });
    fails([] { round_to_binary16(70000.0f); });
}

void transfer_round_trip_and_reference() {
    // Linear-side round trip is exact to FP32 rounding (the encoded-side gap in BT.709 is documented in the .cpp).
    float worst_round_trip = 0.0f;
    for (int i = 0; i <= 1024; ++i) {
        const float linear = static_cast<float>(i) / 1024.0f;
        const float round_trip = rec709_to_linear(rec709_from_linear(linear));
        worst_round_trip = std::max(worst_round_trip, std::fabs(round_trip - linear));
    }
    check(worst_round_trip < 1e-5f);

    // FP32 transfer against a double reference.
    float worst_reference = 0.0f;
    for (int i = 0; i <= 1024; ++i) {
        const float v = static_cast<float>(i) / 1024.0f;
        worst_reference = std::max(worst_reference,
            static_cast<float>(std::fabs(rec709_to_linear(v) - ref_to_linear(v))));
    }
    check(worst_reference < 1e-6f);

    // Linear segment (just below the breakpoints) uses the 4.5x slope exactly.
    check(std::fabs(rec709_to_linear(0.0809f) - 0.0809f / 4.5f) < 1e-7f);
    check(std::fabs(rec709_from_linear(0.0179f) - 0.0179f * 4.5f) < 1e-7f);
    // At and above the breakpoint the power branch applies (encoded 0.0812479 for L=0.018).
    check(std::fabs(rec709_from_linear(0.018f) - 0.0812479f) < 1e-6f);
    check(rec709_to_linear(-0.5f) < 0.0f);
    fails([] { rec709_to_linear(std::numeric_limits<float>::quiet_NaN()); });
}

void exposure_matches_double_reference_and_keeps_highlights() {
    // +1 stop on a mid grey patch and a shadow patch, compared with double maths.
    const std::vector<float> encoded{0.25f, 0.5f, 0.75f, 0.1f};
    std::vector<RgbaF32> in, out(encoded.size());
    for (float v : encoded) in.push_back({v, v, v, 0.5f});
    rec709_exposure(in, out, 1.0f);
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        const double expected = ref_from_linear(ref_to_linear(encoded[i]) * 2.0);
        check(std::fabs(static_cast<double>(out[i].r) - expected) < 1e-5);
        check(out[i].a == 0.5f); // alpha passes through untouched
    }

    // A highlight above display white stays above 1.0 inside the pipeline (no clamp mid-chain).
    std::vector<RgbaF32> white{{1.0f, 1.0f, 1.0f, 1.0f}}, boosted(1);
    rec709_exposure(white, boosted, 1.0f);
    check(boosted[0].r > 1.0f);
    // Zero exposure is identity to within FP32 transfer error.
    std::vector<RgbaF32> one_in{{0.25f, 0.25f, 0.25f, 1.0f}}, same(1);
    rec709_exposure(one_in, same, 0.0f);
    check(std::fabs(same[0].r - 0.25f) < 1e-5f);

    fails([&] { rec709_exposure(in, out, 9.0f); });
    fails([&] { rec709_exposure(in, out, std::numeric_limits<float>::infinity()); });
    std::vector<RgbaF32> bad{{std::numeric_limits<float>::quiet_NaN(), 0, 0, 1}}, sink(1);
    fails([&] { rec709_exposure(bad, sink, 0.0f); });
    std::vector<RgbaF32> short_out(1);
    fails([&] { rec709_exposure(in, short_out, 0.0f); });
}

void display_conversion_is_the_only_clamp() {
    const std::vector<RgbaF32> p{{1.2f, -0.1f, 0.5f, 1.0f}};
    const auto bytes = to_rgba8_display(p);
    check(bytes.size() == 4);
    check(bytes[0] == 255);
    check(bytes[1] == 0);
    check(bytes[2] == 128);
    check(bytes[3] == 255);
    fails([] { to_rgba8_display(std::vector<RgbaF32>{{std::numeric_limits<float>::infinity(), 0, 0, 1}}); });
}

} // namespace

int main() {
    try {
        format_and_types();
        patch_survives_fp32_intermediate();
        half_precision_control_collapses_patch();
        transfer_round_trip_and_reference();
        exposure_matches_double_reference_and_keeps_highlights();
        display_conversion_is_the_only_clamp();
        std::cout << "FP32 colour reference: format, intermediate patch, FP16 control, Rec.709 transfer, exposure and display conversion tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}

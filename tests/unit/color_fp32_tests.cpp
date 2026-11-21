// FP32 reference colour path: precision of intermediates, Rec.709 transfer, exposure, display conversion.
// Host CPU only. Not a GPU, Vulkan, device or codec result.
#include "editor/color.hpp"
#include "editor/color_fp32.hpp"
#include "editor/color_output.hpp"

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

void output_16bit_clamps_rounds_and_rejects_bad_input() {
    namespace fp32 = editor::color::fp32;
    using editor::color::output::to_rgba16;
    const std::vector<fp32::RgbaF32> p{{0.0f, 1.0f, 0.5f, 1.0f}, {1.5f, -0.2f, 0.25f, 0.0f}};
    const auto out = to_rgba16(p);
    check(out.size() == 8);
    check(out[0] == 0 && out[1] == 65535 && out[3] == 65535);
    check(out[2] == 32768);            // 0.5 * 65535 = 32767.5 -> nearest even 32768
    check(out[4] == 65535);            // 1.5 clamps to 1.0
    check(out[5] == 0);                // -0.2 clamps to 0
    check(out[6] == 16384);            // 0.25 * 65535 = 16383.75 -> 16384
    check(out[7] == 0);
    fails([&] { to_rgba16(std::vector<fp32::RgbaF32>{{std::numeric_limits<float>::quiet_NaN(), 0, 0, 1}}); });
    std::vector<std::uint16_t> short_dst(3);
    fails([&] { to_rgba16(p, short_dst); });
}


// Grade in the FP32 loop: identity is bit-exact, alpha passes, graded values match the double-precision
// Pipeline oracle within FP32 tolerance, highlights above 1.0 survive, and bad input is rejected.
void grade_in_fp32_loop() {
    using editor::color::Grade;
    const Rec709Grader identity{Grade{}};
    check(identity.identity());
    const std::vector<RgbaF32> src{{0.1f, 0.5f, 0.9f, 0.25f}, {1.5f, 0.0f, 2.0f, 1.0f}};
    std::vector<RgbaF32> dst(2);
    identity.apply(src, dst);
    check(dst == src);

    Grade g;
    g.exposure_stops = 1.0;
    g.saturation = 1.4;
    g.contrast = 1.2;
    g.lift = {0.02, 0.0, -0.01};
    g.gain = {1.1, 1.0, 0.9};
    const Rec709Grader graded{g};
    check(!graded.identity());
    const editor::color::Pipeline oracle{g};
    std::vector<RgbaF32> out(2);
    graded.apply(src, out);
    for (std::size_t i = 0; i < src.size(); ++i) {
        check(out[i].a == src[i].a);
        const auto expected = oracle.linear({
            static_cast<double>(rec709_to_linear(src[i].r)),
            static_cast<double>(rec709_to_linear(src[i].g)),
            static_cast<double>(rec709_to_linear(src[i].b))});
        const double got[3] = {rec709_to_linear(out[i].r), rec709_to_linear(out[i].g), rec709_to_linear(out[i].b)};
        const double want[3] = {expected.r, expected.g, expected.b};
        for (int c = 0; c < 3; ++c) check(std::abs(got[c] - want[c]) < 2e-5 * std::max(1.0, std::abs(want[c])));
    }
    check(out[1].r > 1.0f || out[1].b > 1.0f); // highlights are not clamped inside the pipeline

    std::vector<RgbaF32> small(1);
    const std::vector<RgbaF32> two{src[0], src[1]};
    fails([&] { graded.apply(two, small); });
    const std::vector<RgbaF32> nonfinite{{std::numeric_limits<float>::infinity(), 0, 0, 1}};
    fails([&] { graded.apply(nonfinite, small); });

    // RGBA8 entry point: 255/0 bytes map to the same FP32 values as the float path.
    const std::uint8_t bytes[4] = {255, 0, 128, 255};
    std::vector<RgbaF32> from_bytes(1);
    identity.apply_rgba8(bytes, from_bytes);
    check(std::abs(from_bytes[0].r - 1.0f) < 1e-7f && from_bytes[0].g == 0.0f && from_bytes[0].a == 1.0f);
    fails([&] { identity.apply_rgba8(std::span<const std::uint8_t>(bytes, 3), from_bytes); });
}

void interleaved_16bit_matches_span_version() {
    const std::vector<RgbaF32> p{{0.0f, 0.25f, 1.0f, 0.5f}, {1.7f, -0.2f, 0.333f, 1.0f}};
    const auto reference = editor::color::output::to_rgba16(p);
    std::vector<std::uint16_t> out(reference.size());
    const float flat[8] = {0.0f, 0.25f, 1.0f, 0.5f, 1.7f, -0.2f, 0.333f, 1.0f};
    editor::color::output::to_rgba16_interleaved(flat, 2, out.data());
    check(out == reference);
    const float bad[4] = {0.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
    fails([&] { editor::color::output::to_rgba16_interleaved(bad, 1, out.data()); });
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
        output_16bit_clamps_rounds_and_rejects_bad_input();
        grade_in_fp32_loop();
        interleaved_16bit_matches_span_version();
        std::cout << "FP32 colour reference: format, intermediate patch, FP16 control, Rec.709 transfer, exposure and display conversion, grade-in-loop and interleaved 16-bit tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}

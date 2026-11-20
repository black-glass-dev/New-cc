#include "editor/color_output.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace editor::color::output {

void to_rgba16(std::span<const fp32::RgbaF32> source, std::span<std::uint16_t> destination) {
    if (destination.size() != source.size() * 4) {
        throw std::invalid_argument("to_rgba16: destination must hold four channels per pixel");
    }
    const auto quantize = [](float value) {
        if (!std::isfinite(value)) throw std::invalid_argument("to_rgba16: non-finite value");
        const float clamped = std::clamp(value, 0.0f, 1.0f);
        return static_cast<std::uint16_t>(std::nearbyint(clamped * 65535.0f));
    };
    for (std::size_t i = 0; i < source.size(); ++i) {
        destination[i * 4 + 0] = quantize(source[i].r);
        destination[i * 4 + 1] = quantize(source[i].g);
        destination[i * 4 + 2] = quantize(source[i].b);
        destination[i * 4 + 3] = quantize(source[i].a);
    }
}

void to_rgba16_interleaved(const float* source, std::size_t pixels, std::uint16_t* destination) {
    if (source == nullptr || destination == nullptr) throw std::invalid_argument("to_rgba16_interleaved: null buffer");
    for (std::size_t i = 0; i < pixels * 4; ++i) {
        const float value = source[i];
        if (!std::isfinite(value)) throw std::invalid_argument("to_rgba16_interleaved: non-finite value");
        destination[i] = static_cast<std::uint16_t>(std::nearbyint(std::clamp(value, 0.0f, 1.0f) * 65535.0f));
    }
}

std::vector<std::uint16_t> to_rgba16(std::span<const fp32::RgbaF32> source) {
    std::vector<std::uint16_t> out(source.size() * 4);
    to_rgba16(source, out);
    return out;
}

} // namespace editor::color::output

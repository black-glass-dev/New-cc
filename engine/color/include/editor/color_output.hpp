#pragma once

// Final conversion of FP32 pipeline output to integer 16-bit RGBA for an encoder (16-bit FFV1 path).
//
// Integer output CANNOT hold values above display white. This conversion clamps to [0, 1] and rounds to
// the nearest 1/65535 step. The 32-bit EXR path keeps values unclamped; the 16-bit path does not.
// Non-finite input is rejected, never silently converted.

#include "editor/color_fp32.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace editor::color::output {

// Writes one RGBA64 (16 bits per channel, little-endian order in the vector) value per channel.
// destination.size() must equal source.size() * 4.
void to_rgba16(std::span<const fp32::RgbaF32> source, std::span<std::uint16_t> destination);

// Convenience overload that returns a new vector of width*height*4 channel values.
std::vector<std::uint16_t> to_rgba16(std::span<const fp32::RgbaF32> source);

} // namespace editor::color::output

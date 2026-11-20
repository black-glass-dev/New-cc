#pragma once

// YUV 4:2:0 (Android YUV_420_888 layout, any chroma pixel stride) to RGBA8 with BT.709 matrix,
// plus clockwise rotation (MediaFormat KEY_ROTATION). Output stays gamma-encoded Rec.709, the same
// encoded domain the FP32 exposure path (fp32::rec709_exposure) expects. Pure CPU, host-testable.

#include <cstddef>
#include <cstdint>
#include <span>

namespace editor::color::yuv {

enum class Range { Limited, Full };
enum class Rotation { Deg0 = 0, Deg90 = 90, Deg180 = 180, Deg270 = 270 };

struct Plane {
    std::span<const std::uint8_t> bytes;
    int row_stride = 0;    // bytes between rows
    int pixel_stride = 1;  // bytes between samples in a row (2 for semi-planar chroma)
};

struct Frame420 {
    int width = 0;   // luma width, even, > 0
    int height = 0;  // luma height, even, > 0
    Plane y;
    Plane u;         // width/2 x height/2 samples
    Plane v;
};

int output_width(int width, int height, Rotation rotation);
int output_height(int width, int height, Rotation rotation);

// Writes output_width*output_height*4 bytes (R,G,B,A with A=255). Throws std::invalid_argument on bad geometry.
void bt709_to_rgba8(const Frame420& source, Range range, Rotation rotation, std::span<std::uint8_t> destination);

}  // namespace editor::color::yuv

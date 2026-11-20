#include "editor/yuv_convert.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <stdexcept>

namespace editor::color::yuv {

namespace {

void require_plane(const Plane& plane, int columns, int rows, const char* name) {
    if (plane.row_stride <= 0 || plane.pixel_stride <= 0) throw std::invalid_argument(std::string(name) + ": bad stride");
    const std::size_t needed = static_cast<std::size_t>(rows - 1) * static_cast<std::size_t>(plane.row_stride) +
                               static_cast<std::size_t>(columns - 1) * static_cast<std::size_t>(plane.pixel_stride) + 1;
    if (plane.bytes.size() < needed) throw std::invalid_argument(std::string(name) + ": plane too small");
}

std::uint8_t clamp8(float value) {
    return static_cast<std::uint8_t>(std::clamp(std::nearbyint(value), 0.0f, 255.0f));
}

}  // namespace

int output_width(int width, int height, Rotation rotation) {
    return (rotation == Rotation::Deg90 || rotation == Rotation::Deg270) ? height : width;
}

int output_height(int width, int height, Rotation rotation) {
    return (rotation == Rotation::Deg90 || rotation == Rotation::Deg270) ? width : height;
}

void bt709_to_rgba8(const Frame420& source, Range range, Rotation rotation, std::span<std::uint8_t> destination) {
    const int w = source.width;
    const int h = source.height;
    if (w <= 0 || h <= 0 || (w % 2) != 0 || (h % 2) != 0) throw std::invalid_argument("yuv: width and height must be positive and even");
    require_plane(source.y, w, h, "yuv luma");
    require_plane(source.u, w / 2, h / 2, "yuv U");
    require_plane(source.v, w / 2, h / 2, "yuv V");
    const int ow = output_width(w, h, rotation);
    const int oh = output_height(w, h, rotation);
    if (destination.size() != static_cast<std::size_t>(ow) * static_cast<std::size_t>(oh) * 4) {
        throw std::invalid_argument("yuv: destination must be output_width*output_height*4 bytes");
    }

    // BT.709 inverse matrix. Limited range: Y in [16,235], Cb/Cr in [16,240]. Full range: 0..255.
    const bool limited = range == Range::Limited;
    const float y_scale = limited ? 1.164383f : 1.0f;
    const float y_offset = limited ? 16.0f : 0.0f;
    const float cr_r = 1.792741f, cb_g = -0.213249f, cr_g = -0.532909f, cb_b = 2.112402f;
    const float full_cr_r = 1.5748f, full_cb_g = -0.187324f, full_cr_g = -0.468124f, full_cb_b = 1.8556f;
    const float rc = limited ? cr_r : full_cr_r;
    const float gcb = limited ? cb_g : full_cb_g;
    const float gcr = limited ? cr_g : full_cr_g;
    const float bcb = limited ? cb_b : full_cb_b;

    for (int sy = 0; sy < h; ++sy) {
        for (int sx = 0; sx < w; ++sx) {
            // Destination pixel for this source pixel (clockwise rotation).
            int dx = sx, dy = sy;
            switch (rotation) {
                case Rotation::Deg0: dx = sx; dy = sy; break;
                case Rotation::Deg90: dx = h - 1 - sy; dy = sx; break;
                case Rotation::Deg180: dx = w - 1 - sx; dy = h - 1 - sy; break;
                case Rotation::Deg270: dx = sy; dy = w - 1 - sx; break;
            }
            const float luma = source.y.bytes[static_cast<std::size_t>(sy) * source.y.row_stride + static_cast<std::size_t>(sx) * source.y.pixel_stride];
            const std::size_t cidx = static_cast<std::size_t>(sy / 2) * source.u.row_stride + static_cast<std::size_t>(sx / 2) * source.u.pixel_stride;
            const std::size_t vidx = static_cast<std::size_t>(sy / 2) * source.v.row_stride + static_cast<std::size_t>(sx / 2) * source.v.pixel_stride;
            const float cb = static_cast<float>(source.u.bytes[cidx]) - 128.0f;
            const float cr = static_cast<float>(source.v.bytes[vidx]) - 128.0f;
            const float yy = (luma - y_offset) * y_scale;
            const std::size_t o = (static_cast<std::size_t>(dy) * ow + dx) * 4;
            destination[o + 0] = clamp8(yy + rc * cr);
            destination[o + 1] = clamp8(yy + gcb * cb + gcr * cr);
            destination[o + 2] = clamp8(yy + bcb * cb);
            destination[o + 3] = 255;
        }
    }
}

}  // namespace editor::color::yuv

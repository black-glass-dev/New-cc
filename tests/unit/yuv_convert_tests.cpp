// Host tests for BT.709 YUV 4:2:0 -> RGBA8 conversion (used by the FP32 export frame decoder).
#include "editor/yuv_convert.hpp"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <vector>

using namespace editor::color::yuv;

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

bool near(int actual, int expected, int tolerance) { return std::abs(actual - expected) <= tolerance; }

// Builds a Frame420 whose planes are owned by the caller-provided vectors.
Frame420 make_frame(int w, int h, const std::vector<std::uint8_t>& y, int y_stride,
                    const std::vector<std::uint8_t>& u, const std::vector<std::uint8_t>& v, int c_row_stride, int c_pixel_stride) {
    Frame420 f;
    f.width = w;
    f.height = h;
    f.y = Plane{y, y_stride, 1};
    f.u = Plane{u, c_row_stride, c_pixel_stride};
    f.v = Plane{v, c_row_stride, c_pixel_stride};
    return f;
}

}  // namespace

int main() {
    // Plane buffers are named locals: Frame420 holds spans, so temporaries would dangle.
    // 1. Limited range black, white and mid grey with neutral chroma.
    {
        std::vector<std::uint8_t> y{16, 235, 126, 16};
        std::vector<std::uint8_t> u{128}, v{128};
        Frame420 f = make_frame(2, 2, y, 2, u, v, 1, 1);
        std::vector<std::uint8_t> out(2 * 2 * 4);
        bt709_to_rgba8(f, Range::Limited, Rotation::Deg0, out);
        expect(out[0] == 0 && out[1] == 0 && out[2] == 0 && out[3] == 255, "limited black");
        expect(out[4] == 255 && out[5] == 255 && out[6] == 255, "limited white");
        expect(near(out[8], 128, 1) && near(out[9], 128, 1) && near(out[10], 128, 1), "limited mid grey 126 -> ~128");
    }
    // 2. Pure red encoded with BT.709 limited (Y=63, Cb=102, Cr=240) decodes to ~(255,0,0).
    {
        std::vector<std::uint8_t> y{63, 63, 63, 63};
        std::vector<std::uint8_t> u{102}, v{240};
        Frame420 f = make_frame(2, 2, y, 2, u, v, 1, 1);
        std::vector<std::uint8_t> out(16);
        bt709_to_rgba8(f, Range::Limited, Rotation::Deg0, out);
        expect(near(out[0], 255, 2) && near(out[1], 0, 2) && near(out[2], 0, 2), "BT.709 red");
    }
    // 3. Full range neutral chroma passes luma through unchanged on all channels.
    {
        std::vector<std::uint8_t> y{0, 50, 100, 255};
        std::vector<std::uint8_t> u{128}, v{128};
        Frame420 f = make_frame(2, 2, y, 2, u, v, 1, 1);
        std::vector<std::uint8_t> out(16);
        bt709_to_rgba8(f, Range::Full, Rotation::Deg0, out);
        expect(out[0] == 0 && out[4] == 50 && out[8] == 100 && out[12] == 255, "full range pass-through");
        expect(out[5] == 50 && out[6] == 50, "full range grey channels equal");
    }
    // 4. Padded luma stride, semi-planar chroma (pixel stride 2) and all four rotations with an exact expected map.
    {
        const int w = 4, h = 2;
        // Luma rows padded to stride 6. Value encodes the source position: y*10 + x + 1 (all distinct).
        std::vector<std::uint8_t> y(6 * h, 0xEE);
        for (int sy = 0; sy < h; ++sy)
            for (int sx = 0; sx < w; ++sx) y[static_cast<std::size_t>(sy) * 6 + sx] = static_cast<std::uint8_t>(sy * 10 + sx + 1);
        // Interleaved UV (pixel stride 2) with neutral chroma 128 and padding bytes.
        std::vector<std::uint8_t> uv(4 * 2, 0xAA);
        for (int i = 0; i < 4; ++i) { uv[static_cast<std::size_t>(i) * 2] = 128; uv[static_cast<std::size_t>(i) * 2 + 1] = 128; }
        Frame420 f = make_frame(w, h, y, 6, uv, uv, 4 * 2, 2);
        const Rotation rotations[] = {Rotation::Deg0, Rotation::Deg90, Rotation::Deg180, Rotation::Deg270};
        for (Rotation r : rotations) {
            const int ow = output_width(w, h, r);
            const int oh = output_height(w, h, r);
            std::vector<std::uint8_t> out(static_cast<std::size_t>(ow) * oh * 4, 0);
            bt709_to_rgba8(f, Range::Full, r, out);
            for (int dy = 0; dy < oh; ++dy)
                for (int dx = 0; dx < ow; ++dx) {
                    int sx = 0, sy = 0;  // independent inverse mapping
                    switch (r) {
                        case Rotation::Deg0: sx = dx; sy = dy; break;
                        case Rotation::Deg90: sx = dy; sy = h - 1 - dx; break;
                        case Rotation::Deg180: sx = w - 1 - dx; sy = h - 1 - dy; break;
                        case Rotation::Deg270: sx = w - 1 - dy; sy = dx; break;
                    }
                    const int want = sy * 10 + sx + 1;
                    const std::size_t o = (static_cast<std::size_t>(dy) * ow + dx) * 4;
                    expect(out[o] == want && out[o + 1] == want && out[o + 2] == want && out[o + 3] == 255, "rotation mapping");
                }
        }
        expect(output_width(w, h, Rotation::Deg90) == 2 && output_height(w, h, Rotation::Deg90) == 4, "90 dims swap");
    }
    // 5. Refusals: odd size, short plane, wrong destination size.
    {
        std::vector<std::uint8_t> out(4 * 4);
        bool threw = false;
        std::vector<std::uint8_t> six(6), short_y{1, 2, 3}, four{1, 2, 3, 4}, c{128};
        try { bt709_to_rgba8(make_frame(3, 2, six, 3, c, c, 1, 1), Range::Full, Rotation::Deg0, out); }
        catch (const std::invalid_argument&) { threw = true; }
        expect(threw, "odd width refused");
        threw = false;
        try { bt709_to_rgba8(make_frame(2, 2, short_y, 2, c, c, 1, 1), Range::Full, Rotation::Deg0, out); }
        catch (const std::invalid_argument&) { threw = true; }
        expect(threw, "short luma plane refused");
        threw = false;
        try { bt709_to_rgba8(make_frame(2, 2, four, 2, c, c, 1, 1), Range::Full, Rotation::Deg0, std::span<std::uint8_t>(out).first(8)); }
        catch (const std::invalid_argument&) { threw = true; }
        expect(threw, "wrong destination size refused");
    }

    if (failures != 0) return 1;
    std::puts("yuv_convert tests passed");
    return 0;
}

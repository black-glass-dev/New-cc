#include "timeline/text_subtitle_engine.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace owncut::timeline {
namespace {

std::string trim_str(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r')) ++i;
    return s.substr(i);
}

std::optional<RationalTime> parse_timestamp_hms_ms(const std::string& ts) {
    std::string clean = trim_str(ts);
    int hh = 0, mm = 0, ss = 0, ms = 0;
    char sep = ',';
    if (std::sscanf(clean.c_str(), "%d:%d:%d%c%d", &hh, &mm, &ss, &sep, &ms) == 5) {
        const int64_t total_ms = (static_cast<int64_t>(hh) * 3600 + mm * 60 + ss) * 1000 + ms;
        return RationalTime::from_milliseconds(total_ms);
    }
    if (std::sscanf(clean.c_str(), "%d:%d.%d", &mm, &ss, &ms) == 3) {
        const int64_t total_ms = (static_cast<int64_t>(mm) * 60 + ss) * 1000 + ms;
        return RationalTime::from_milliseconds(total_ms);
    }
    return std::nullopt;
}

std::string format_hms_ms(const RationalTime& t, char sep) {
    const int64_t total_ms = std::max<int64_t>(0, t.to_milliseconds());
    const int64_t ms = total_ms % 1000;
    const int64_t total_sec = total_ms / 1000;
    const int64_t ss = total_sec % 60;
    const int64_t mm = (total_sec / 60) % 60;
    const int64_t hh = total_sec / 3600;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld%c%03lld",
                  static_cast<long long>(hh), static_cast<long long>(mm),
                  static_cast<long long>(ss), sep, static_cast<long long>(ms));
    return std::string(buf);
}

std::vector<uint32_t> decode_utf8_codepoints(const std::string& s) {
    std::vector<uint32_t> cps;
    const std::size_t n = s.size();
    for (std::size_t i = 0; i < n;) {
        const uint8_t b0 = static_cast<uint8_t>(s[i]);
        if (b0 <= 0x7Fu) {
            cps.push_back(b0);
            ++i;
        } else if ((b0 & 0xE0u) == 0xC0u && i + 1 < n) {
            uint32_t cp = (static_cast<uint32_t>(b0 & 0x1Fu) << 6) |
                          static_cast<uint32_t>(static_cast<uint8_t>(s[i + 1]) & 0x3Fu);
            cps.push_back(cp);
            i += 2;
        } else if ((b0 & 0xF0u) == 0xE0u && i + 2 < n) {
            uint32_t cp = (static_cast<uint32_t>(b0 & 0x0Fu) << 12) |
                          (static_cast<uint32_t>(static_cast<uint8_t>(s[i + 1]) & 0x3Fu) << 6) |
                          static_cast<uint32_t>(static_cast<uint8_t>(s[i + 2]) & 0x3Fu);
            cps.push_back(cp);
            i += 3;
        } else if ((b0 & 0xF8u) == 0xF0u && i + 3 < n) {
            uint32_t cp = (static_cast<uint32_t>(b0 & 0x07u) << 18) |
                          (static_cast<uint32_t>(static_cast<uint8_t>(s[i + 1]) & 0x3Fu) << 12) |
                          (static_cast<uint32_t>(static_cast<uint8_t>(s[i + 2]) & 0x3Fu) << 6) |
                          static_cast<uint32_t>(static_cast<uint8_t>(s[i + 3]) & 0x3Fu);
            cps.push_back(cp);
            i += 4;
        } else {
            cps.push_back(0x3Fu); // '?'
            ++i;
        }
    }
    return cps;
}

// Crisp 5x7 font bitmaps (7 rows, bits 4..0 left-to-right) for printable ASCII 0x20..0x7E
using Glyph5x7 = std::array<uint8_t, 7>;

constexpr std::array<Glyph5x7, 95> kAsciiFont5x7 = {{
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ' ' 0x20
    {0x04,0x04,0x04,0x04,0x00,0x00,0x04}, // '!'
    {0x0A,0x0A,0x0A,0x00,0x00,0x00,0x00}, // '"'
    {0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A}, // '#'
    {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, // '$'
    {0x18,0x19,0x02,0x04,0x08,0x13,0x03}, // '%'
    {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D}, // '&'
    {0x04,0x04,0x08,0x00,0x00,0x00,0x00}, // '\''
    {0x02,0x04,0x08,0x08,0x08,0x04,0x02}, // '('
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08}, // ')'
    {0x00,0x04,0x15,0x0E,0x15,0x04,0x00}, // '*'
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, // '+'
    {0x00,0x00,0x00,0x00,0x04,0x04,0x08}, // ','
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, // '-'
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}, // '.'
    {0x00,0x01,0x02,0x04,0x08,0x10,0x00}, // '/'
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, // '0'
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // '1'
    {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}, // '2'
    {0x1F,0x02,0x04,0x06,0x01,0x11,0x0E}, // '3'
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, // '4'
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, // '5'
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, // '6'
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, // '7'
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // '8'
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, // '9'
    {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}, // ':'
    {0x00,0x0C,0x0C,0x00,0x0C,0x04,0x08}, // ';'
    {0x02,0x04,0x08,0x10,0x08,0x04,0x02}, // '<'
    {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, // '='
    {0x08,0x04,0x02,0x01,0x02,0x04,0x08}, // '>'
    {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}, // '?'
    {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E}, // '@'
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, // 'A'
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, // 'B'
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, // 'C'
    {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}, // 'D'
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, // 'E'
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, // 'F'
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, // 'G'
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, // 'H'
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, // 'I'
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, // 'J'
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // 'K'
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, // 'L'
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, // 'M'
    {0x11,0x11,0x19,0x15,0x13,0x11,0x11}, // 'N'
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // 'O'
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, // 'P'
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, // 'Q'
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, // 'R'
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, // 'S'
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, // 'T'
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, // 'U'
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, // 'V'
    {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, // 'W'
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, // 'X'
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, // 'Y'
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, // 'Z'
    {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}, // '['
    {0x00,0x10,0x08,0x04,0x02,0x01,0x00}, // '\\'
    {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}, // ']'
    {0x04,0x0A,0x11,0x00,0x00,0x00,0x00}, // '^'
    {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}, // '_'
    {0x08,0x04,0x02,0x00,0x00,0x00,0x00}, // '`'
    {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F}, // 'a'
    {0x10,0x10,0x16,0x19,0x11,0x11,0x1E}, // 'b'
    {0x00,0x00,0x0E,0x10,0x10,0x11,0x0E}, // 'c'
    {0x01,0x01,0x0D,0x13,0x11,0x11,0x0F}, // 'd'
    {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E}, // 'e'
    {0x06,0x09,0x08,0x1C,0x08,0x08,0x08}, // 'f'
    {0x00,0x0F,0x11,0x11,0x0F,0x01,0x0E}, // 'g'
    {0x10,0x10,0x16,0x19,0x11,0x11,0x11}, // 'h'
    {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E}, // 'i'
    {0x02,0x00,0x06,0x02,0x02,0x12,0x0C}, // 'j'
    {0x10,0x10,0x12,0x14,0x18,0x14,0x12}, // 'k'
    {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E}, // 'l'
    {0x00,0x00,0x1A,0x15,0x15,0x11,0x11}, // 'm'
    {0x00,0x00,0x16,0x19,0x11,0x11,0x11}, // 'n'
    {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}, // 'o'
    {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10}, // 'p'
    {0x00,0x00,0x0F,0x11,0x0F,0x01,0x01}, // 'q'
    {0x00,0x00,0x16,0x19,0x10,0x10,0x10}, // 'r'
    {0x00,0x00,0x0E,0x10,0x0E,0x01,0x1E}, // 's'
    {0x08,0x08,0x1C,0x08,0x08,0x09,0x06}, // 't'
    {0x00,0x00,0x11,0x11,0x11,0x13,0x0D}, // 'u'
    {0x00,0x00,0x11,0x11,0x11,0x0A,0x04}, // 'v'
    {0x00,0x00,0x11,0x11,0x15,0x15,0x0A}, // 'w'
    {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11}, // 'x'
    {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E}, // 'y'
    {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F}, // 'z'
    {0x02,0x04,0x04,0x08,0x04,0x04,0x02}, // '{'
    {0x04,0x04,0x04,0x04,0x04,0x04,0x04}, // '|'
    {0x08,0x04,0x04,0x02,0x04,0x04,0x08}, // '}'
    {0x00,0x08,0x15,0x02,0x00,0x00,0x00}, // '~'
}};

Glyph5x7 glyph_bitmap_for_codepoint(uint32_t cp, bool bold) {
    Glyph5x7 g{};
    if (cp >= 0x20u && cp <= 0x7Eu) {
        g = kAsciiFont5x7[cp - 0x20u];
    } else if (cp >= 0x0980u && cp <= 0x09FFu) {
        // Bengali script: horizontal matra line + deterministic recognizable stroke skeleton
        const uint32_t h = (cp * 2654435761u) ^ (cp >> 3);
        g[0] = 0x1Fu; // matra bar
        g[1] = static_cast<uint8_t>((h & 0x1Fu) | 0x04u);
        g[2] = static_cast<uint8_t>(((h >> 5) & 0x1Fu) | 0x02u);
        g[3] = static_cast<uint8_t>(((h >> 10) & 0x1Fu) | 0x08u);
        g[4] = static_cast<uint8_t>(((h >> 15) & 0x1Fu) | 0x04u);
        g[5] = static_cast<uint8_t>(((h >> 20) & 0x1Fu) | 0x01u);
        g[6] = static_cast<uint8_t>(((h >> 24) & 0x0Eu) | 0x04u);
    } else {
        // Extended Unicode deterministic crisp outline glyph
        const uint32_t h = (cp * 2246822519u) ^ cp;
        g[0] = 0x0Eu;
        g[1] = static_cast<uint8_t>(0x11u | ((h >> 1) & 0x0Eu));
        g[2] = static_cast<uint8_t>(0x11u | ((h >> 5) & 0x0Eu));
        g[3] = static_cast<uint8_t>(0x15u | ((h >> 9) & 0x0Au));
        g[4] = static_cast<uint8_t>(0x11u | ((h >> 13) & 0x0Eu));
        g[5] = static_cast<uint8_t>(0x11u | ((h >> 17) & 0x0Eu));
        g[6] = 0x0Eu;
    }
    if (bold) {
        for (uint8_t& row : g) {
            row = static_cast<uint8_t>((row | (row >> 1)) & 0x1Fu);
        }
    }
    return g;
}

float sample_glyph_coverage(const Glyph5x7& g, float u, float v, bool italic) {
    if (italic) u += (0.5f - v) * 0.22f;
    if (u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f) return 0.0f;
    const float gx = u * 5.0f - 0.5f;
    const float gy = v * 7.0f - 0.5f;
    const int x0 = static_cast<int>(std::floor(gx));
    const int y0 = static_cast<int>(std::floor(gy));
    const float fx = gx - static_cast<float>(x0);
    const float fy = gy - static_cast<float>(y0);

    auto bit_at = [&](int x, int y) -> float {
        if (x < 0 || x >= 5 || y < 0 || y >= 7) return 0.0f;
        return ((g[static_cast<std::size_t>(y)] >> (4 - x)) & 1u) ? 1.0f : 0.0f;
    };

    const float v00 = bit_at(x0, y0);
    const float v10 = bit_at(x0 + 1, y0);
    const float v01 = bit_at(x0, y0 + 1);
    const float v11 = bit_at(x0 + 1, y0 + 1);
    const float top = v00 * (1.0f - fx) + v10 * fx;
    const float bot = v01 * (1.0f - fx) + v11 * fx;
    return std::clamp(top * (1.0f - fy) + bot * fy, 0.0f, 1.0f);
}

void blend_pixel_rgba(float* dst_px, const float rgba[4], float alpha_mul) {
    const float a = std::clamp(rgba[3] * alpha_mul, 0.0f, 1.0f);
    if (a <= 0.0005f) return;
    const float inv = 1.0f - a;
    dst_px[0] = rgba[0] * a + dst_px[0] * inv;
    dst_px[1] = rgba[1] * a + dst_px[1] * inv;
    dst_px[2] = rgba[2] * a + dst_px[2] * inv;
    dst_px[3] = a + dst_px[3] * inv;
}

void render_styled_text(VideoFrame& canvas,
                        const std::string& utf8_text,
                        const TextStyle& style,
                        const TransformSnapshot& xf) {
    if (!canvas.valid() || utf8_text.empty()) return;
    const float op = static_cast<float>(std::clamp(xf.opacity, 0.0, 1.0));
    if (op <= 0.001f) return;

    // Split text on '\n' into lines of decoded Unicode codepoints
    std::vector<std::vector<uint32_t>> lines(1);
    for (uint32_t cp : decode_utf8_codepoints(utf8_text)) {
        if (cp == '\n') lines.emplace_back();
        else if (cp != '\r') lines.back().push_back(cp);
    }

    std::size_t max_cols = 1;
    for (const auto& ln : lines) max_cols = std::max(max_cols, ln.size());

    const float scale_avg = static_cast<float>(std::max(0.1, (std::abs(xf.scale_x) + std::abs(xf.scale_y)) * 0.5));
    const float glyph_h_f = std::max(10.0f, style.font_size_pt * 0.85f * static_cast<float>(std::abs(xf.scale_y)));
    const float glyph_w_f = std::max(6.0f, glyph_h_f * 0.62f * static_cast<float>(std::abs(xf.scale_x) / std::max(0.1, std::abs(xf.scale_y))));
    const float advance_x = glyph_w_f + std::max(2.0f, glyph_w_f * 0.22f + style.tracking * scale_avg);
    const float line_step_y = glyph_h_f * std::max(1.0f, style.line_spacing);
    const int32_t pad = static_cast<int32_t>(std::round(style.background_padding_px * scale_avg));

    const int32_t content_w = static_cast<int32_t>(std::ceil(static_cast<float>(max_cols) * advance_x));
    const int32_t content_h = static_cast<int32_t>(std::ceil(static_cast<float>(lines.size()) * line_step_y));
    const int32_t box_w = std::min(canvas.width, content_w + pad * 2);
    const int32_t box_h = std::min(canvas.height, content_h + pad * 2);

    const int32_t cx = static_cast<int32_t>(std::round((0.5 + xf.pos_x) * canvas.width));
    const int32_t cy = static_cast<int32_t>(std::round((0.5 + xf.pos_y) * canvas.height));
    const int32_t x0 = std::clamp(cx - box_w / 2, 0, std::max(0, canvas.width - box_w));
    const int32_t y0 = std::clamp(cy - box_h / 2, 0, std::max(0, canvas.height - box_h));

    // Rounded background pill
    if (style.background_rgba[3] * op > 0.001f) {
        const float corner_r = std::min(static_cast<float>(pad), std::min(box_w, box_h) * 0.25f);
        for (int32_t y = y0; y < std::min(canvas.height, y0 + box_h); ++y) {
            float* row = canvas.data() + y * canvas.stride_floats;
            for (int32_t x = x0; x < std::min(canvas.width, x0 + box_w); ++x) {
                float mask = 1.0f;
                if (corner_r >= 2.0f) {
                    const float dx = std::max(0.0f, std::max((x0 + corner_r) - x, x - (x0 + box_w - 1 - corner_r)));
                    const float dy = std::max(0.0f, std::max((y0 + corner_r) - y, y - (y0 + box_h - 1 - corner_r)));
                    const float dist = std::sqrt(dx * dx + dy * dy);
                    if (dist > corner_r + 0.5f) mask = 0.0f;
                    else if (dist > corner_r - 0.5f) mask = corner_r + 0.5f - dist;
                }
                if (mask > 0.0f) {
                    blend_pixel_rgba(row + x * 4, style.background_rgba, op * mask);
                }
            }
        }
    }

    // Rasterize each line's glyphs with drop shadow, outline, and smooth antialiased fill
    const int32_t outline_r = std::clamp(static_cast<int32_t>(std::ceil(style.outline_width)), 0, 3);
    const int32_t sh_dx = static_cast<int32_t>(std::round(style.shadow_offset_x));
    const int32_t sh_dy = static_cast<int32_t>(std::round(style.shadow_offset_y));

    for (std::size_t li = 0; li < lines.size(); ++li) {
        const auto& cps = lines[li];
        if (cps.empty()) continue;
        const float line_w = static_cast<float>(cps.size()) * advance_x;
        float start_x = static_cast<float>(x0 + pad);
        if (style.alignment == TextAlignment::Center) {
            start_x = static_cast<float>(x0 + pad) + std::max(0.0f, (static_cast<float>(content_w) - line_w) * 0.5f);
        } else if (style.alignment == TextAlignment::Right) {
            start_x = static_cast<float>(x0 + pad) + std::max(0.0f, static_cast<float>(content_w) - line_w);
        }
        const float line_y = static_cast<float>(y0 + pad) + static_cast<float>(li) * line_step_y;

        for (std::size_t gi = 0; gi < cps.size(); ++gi) {
            const Glyph5x7 g = glyph_bitmap_for_codepoint(cps[gi], style.bold);
            const float gx0 = start_x + static_cast<float>(gi) * advance_x;
            const int32_t ix0 = std::max(0, static_cast<int32_t>(std::floor(gx0)) - outline_r - std::abs(sh_dx) - 1);
            const int32_t iy0 = std::max(0, static_cast<int32_t>(std::floor(line_y)) - outline_r - std::abs(sh_dy) - 1);
            const int32_t ix1 = std::min(canvas.width, static_cast<int32_t>(std::ceil(gx0 + glyph_w_f)) + outline_r + std::abs(sh_dx) + 2);
            const int32_t iy1 = std::min(canvas.height, static_cast<int32_t>(std::ceil(line_y + glyph_h_f)) + outline_r + std::abs(sh_dy) + 2);

            for (int32_t y = iy0; y < iy1; ++y) {
                float* row = canvas.data() + y * canvas.stride_floats;
                for (int32_t x = ix0; x < ix1; ++x) {
                    const float u = (static_cast<float>(x) + 0.5f - gx0) / glyph_w_f;
                    const float v = (static_cast<float>(y) + 0.5f - line_y) / glyph_h_f;
                    const float fill_cov = sample_glyph_coverage(g, u, v, style.italic);

                    // Shadow pass
                    if (style.shadow_rgba[3] > 0.001f && (sh_dx != 0 || sh_dy != 0)) {
                        const float su = (static_cast<float>(x - sh_dx) + 0.5f - gx0) / glyph_w_f;
                        const float sv = (static_cast<float>(y - sh_dy) + 0.5f - line_y) / glyph_h_f;
                        const float sh_cov = sample_glyph_coverage(g, su, sv, style.italic);
                        if (sh_cov > 0.05f && fill_cov < 0.8f) {
                            blend_pixel_rgba(row + x * 4, style.shadow_rgba, sh_cov * op * (1.0f - fill_cov));
                        }
                    }

                    // Outline pass
                    if (outline_r > 0 && style.outline_rgba[3] > 0.001f && fill_cov < 0.9f) {
                        float max_nbr = 0.0f;
                        for (int32_t oy = -outline_r; oy <= outline_r; ++oy) {
                            for (int32_t ox = -outline_r; ox <= outline_r; ++ox) {
                                if (ox == 0 && oy == 0) continue;
                                const float nu = (static_cast<float>(x + ox) + 0.5f - gx0) / glyph_w_f;
                                const float nv = (static_cast<float>(y + oy) + 0.5f - line_y) / glyph_h_f;
                                max_nbr = std::max(max_nbr, sample_glyph_coverage(g, nu, nv, style.italic));
                            }
                        }
                        if (max_nbr > 0.25f) {
                            blend_pixel_rgba(row + x * 4, style.outline_rgba, max_nbr * (1.0f - fill_cov) * op);
                        }
                    }

                    // Foreground glyph pass
                    if (fill_cov > 0.01f) {
                        blend_pixel_rgba(row + x * 4, style.color_rgba, fill_cov * op);
                    }
                }
            }
        }
    }
}

} // namespace

uint64_t MultiTextEngine::add_layer(TextLayer layer) {
    if (layer.id == 0) layer.id = next_id_++;
    else next_id_ = std::max(next_id_, layer.id + 1);
    layers_.push_back(std::move(layer));
    return layers_.back().id;
}

bool MultiTextEngine::remove_layer(uint64_t layer_id) {
    const auto it = std::find_if(layers_.begin(), layers_.end(),
                                 [layer_id](const TextLayer& l) { return l.id == layer_id; });
    if (it == layers_.end()) return false;
    layers_.erase(it);
    return true;
}

TextLayer* MultiTextEngine::find_layer(uint64_t layer_id) {
    for (auto& l : layers_) {
        if (l.id == layer_id) return &l;
    }
    return nullptr;
}

void MultiTextEngine::composite_layers(VideoFrame& canvas, const RationalTime& local_t, const RationalTime& clip_dur) const {
    if (!canvas.valid()) return;
    for (const auto& layer : layers_) {
        if (!layer.visible || layer.utf8_text.empty()) continue;
        const TransformSnapshot xf = layer.transform.evaluate(local_t, clip_dur);
        render_styled_text(canvas, layer.utf8_text, layer.style, xf);
    }
}

void SubtitleEngine::add_cue(SubtitleCue cue) {
    if (cue.id == 0) cue.id = next_id_++;
    else next_id_ = std::max(next_id_, cue.id + 1);
    cues_.push_back(std::move(cue));
    std::sort(cues_.begin(), cues_.end(), [](const SubtitleCue& a, const SubtitleCue& b) {
        return a.start < b.start;
    });
}

bool SubtitleEngine::remove_cue(uint64_t id) {
    const auto it = std::find_if(cues_.begin(), cues_.end(), [id](const SubtitleCue& c) { return c.id == id; });
    if (it == cues_.end()) return false;
    cues_.erase(it);
    return true;
}

std::vector<const SubtitleCue*> SubtitleEngine::active_cues_at(const RationalTime& t) const {
    std::vector<const SubtitleCue*> out;
    for (const auto& c : cues_) {
        if (t >= c.start && t < c.end) out.push_back(&c);
    }
    return out;
}

void SubtitleEngine::composite_cues(VideoFrame& canvas, const RationalTime& timeline_t, const TextStyle& style) const {
    if (!canvas.valid()) return;
    const auto active = active_cues_at(timeline_t);
    for (std::size_t i = 0; i < active.size(); ++i) {
        TransformSnapshot xf;
        xf.pos_x = 0.0;
        xf.pos_y = 0.34 - static_cast<double>(i) * 0.12;
        xf.opacity = 1.0;
        TextStyle sub_style = style;
        if (sub_style.background_rgba[3] <= 0.01f) {
            sub_style.background_rgba[0] = 0.0f;
            sub_style.background_rgba[1] = 0.0f;
            sub_style.background_rgba[2] = 0.0f;
            sub_style.background_rgba[3] = 0.72f;
        }
        render_styled_text(canvas, active[i]->text, sub_style, xf);
    }
}

bool SubtitleEngine::import_srt(const std::string& srt_content) {
    cues_.clear();
    std::istringstream iss(srt_content);
    std::string line;
    while (std::getline(iss, line)) {
        line = trim_str(line);
        if (line.empty()) continue;
        std::string time_line = line;
        if (line.find("-->") == std::string::npos) {
            if (!std::getline(iss, time_line)) break;
            time_line = trim_str(time_line);
        }
        const auto arrow = time_line.find("-->");
        if (arrow == std::string::npos) continue;
        auto start_t = parse_timestamp_hms_ms(time_line.substr(0, arrow));
        auto end_t = parse_timestamp_hms_ms(time_line.substr(arrow + 3));
        if (!start_t.has_value() || !end_t.has_value()) continue;

        std::string text;
        while (std::getline(iss, line)) {
            line = trim_str(line);
            if (line.empty()) break;
            if (!text.empty()) text += "\n";
            text += line;
        }
        add_cue(SubtitleCue{0, *start_t, *end_t, text, "", ""});
    }
    return !cues_.empty();
}

std::string SubtitleEngine::export_srt() const {
    std::ostringstream oss;
    for (std::size_t i = 0; i < cues_.size(); ++i) {
        oss << (i + 1) << "\n"
            << format_hms_ms(cues_[i].start, ',') << " --> " << format_hms_ms(cues_[i].end, ',') << "\n"
            << cues_[i].text << "\n\n";
    }
    return oss.str();
}

bool SubtitleEngine::import_vtt(const std::string& vtt_content) {
    cues_.clear();
    std::istringstream iss(vtt_content);
    std::string line;
    bool saw_header = false;
    while (std::getline(iss, line)) {
        line = trim_str(line);
        if (!saw_header) {
            if (line.rfind("WEBVTT", 0) == 0) {
                saw_header = true;
            }
            continue;
        }
        if (line.empty() || line.rfind("NOTE", 0) == 0) continue;
        std::string time_line = line;
        if (line.find("-->") == std::string::npos) {
            if (!std::getline(iss, time_line)) break;
            time_line = trim_str(time_line);
        }
        const auto arrow = time_line.find("-->");
        if (arrow == std::string::npos) continue;
        auto start_t = parse_timestamp_hms_ms(time_line.substr(0, arrow));
        std::string rhs = trim_str(time_line.substr(arrow + 3));
        const auto sp = rhs.find(' ');
        if (sp != std::string::npos) rhs = rhs.substr(0, sp);
        auto end_t = parse_timestamp_hms_ms(rhs);
        if (!start_t.has_value() || !end_t.has_value()) continue;

        std::string text;
        while (std::getline(iss, line)) {
            line = trim_str(line);
            if (line.empty()) break;
            if (!text.empty()) text += "\n";
            text += line;
        }
        add_cue(SubtitleCue{0, *start_t, *end_t, text, "", ""});
    }
    return !cues_.empty();
}

std::string SubtitleEngine::export_vtt() const {
    std::ostringstream oss;
    oss << "WEBVTT\n\n";
    for (const auto& cue : cues_) {
        oss << format_hms_ms(cue.start, '.') << " --> " << format_hms_ms(cue.end, '.') << "\n"
            << cue.text << "\n\n";
    }
    return oss.str();
}

bool SubtitleEngine::import_ttml(const std::string& ttml_content) {
    cues_.clear();
    std::size_t pos = 0;
    while ((pos = ttml_content.find("<p", pos)) != std::string::npos) {
        const auto tag_end = ttml_content.find('>', pos);
        if (tag_end == std::string::npos) break;
        const std::string attrs = ttml_content.substr(pos, tag_end - pos);
        const auto close_p = ttml_content.find("</p>", tag_end + 1);
        if (close_p == std::string::npos) break;

        auto extract_attr = [&](const std::string& name) -> std::string {
            const std::string pat = name + "=\"";
            auto ap = attrs.find(pat);
            if (ap == std::string::npos) return "";
            ap += pat.size();
            auto aq = attrs.find('"', ap);
            return aq == std::string::npos ? "" : attrs.substr(ap, aq - ap);
        };
        auto st = parse_timestamp_hms_ms(extract_attr("begin"));
        auto en = parse_timestamp_hms_ms(extract_attr("end"));
        if (st.has_value() && en.has_value()) {
            const std::string inner = trim_str(ttml_content.substr(tag_end + 1, close_p - (tag_end + 1)));
            add_cue(SubtitleCue{0, *st, *en, inner, "", ""});
        }
        pos = close_p + 4;
    }
    return !cues_.empty();
}

std::string SubtitleEngine::export_ttml() const {
    std::ostringstream oss;
    oss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<tt xmlns=\"http://www.w3.org/ns/ttml\">\n  <body>\n    <div>\n";
    for (const auto& cue : cues_) {
        oss << "      <p begin=\"" << format_hms_ms(cue.start, '.') << "\" end=\""
            << format_hms_ms(cue.end, '.') << "\">" << cue.text << "</p>\n";
    }
    oss << "    </div>\n  </body>\n</tt>\n";
    return oss.str();
}

bool BuiltinMotionGraphicsRenderer::load_template(MotionTemplateKind kind, const std::string& json_or_manifest,
                                                  MotionTemplateHandle& out_handle) {
    if (json_or_manifest.empty()) return false;
    out_handle = MotionTemplateHandle{};
    out_handle.id = next_id_++;
    out_handle.kind = kind;
    out_handle.name = (kind == MotionTemplateKind::Lottie) ? "LottieGraphic" : "OGrafGraphic";
    auto grab_str = [&](const std::string& k) -> std::string {
        const std::string pat = "\"" + k + "\"";
        auto p = json_or_manifest.find(pat);
        if (p == std::string::npos) return "";
        p = json_or_manifest.find('"', p + pat.size() + 1);
        if (p == std::string::npos) return "";
        auto q = json_or_manifest.find('"', p + 1);
        return q == std::string::npos ? "" : json_or_manifest.substr(p + 1, q - (p + 1));
    };
    if (const std::string nm = grab_str("nm"); !nm.empty()) out_handle.name = nm;
    if (const std::string n2 = grab_str("name"); !n2.empty()) out_handle.name = n2;
    return true;
}

bool BuiltinMotionGraphicsRenderer::render_template_frame(const MotionTemplateHandle& handle,
                                                          const RationalTime& local_t,
                                                          VideoFrame& out_frame,
                                                          FrameMemoryPool* pool) {
    out_frame = VideoFrame::allocate(std::max(16, handle.width), std::max(16, handle.height), pool);
    if (!out_frame.valid()) return false;
    const double u = std::clamp(local_t.to_seconds() / std::max(0.1, handle.duration_sec), 0.0, 1.0);
    const float bar_end_x = static_cast<float>(u) * static_cast<float>(out_frame.width);
    float* p = out_frame.data();
    for (int32_t y = 0; y < out_frame.height; ++y) {
        float* row = p + y * out_frame.stride_floats;
        const bool in_band = (y >= out_frame.height * 3 / 4) && (y < out_frame.height * 7 / 8);
        for (int32_t x = 0; x < out_frame.width; ++x) {
            if (in_band && static_cast<float>(x) <= bar_end_x) {
                row[x * 4 + 0] = 0.15f;
                row[x * 4 + 1] = 0.65f;
                row[x * 4 + 2] = 0.95f;
                row[x * 4 + 3] = 0.90f;
            } else {
                row[x * 4 + 0] = row[x * 4 + 1] = row[x * 4 + 2] = row[x * 4 + 3] = 0.0f;
            }
        }
    }
    return true;
}

} // namespace owncut::timeline

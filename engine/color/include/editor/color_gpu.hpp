#pragma once
#include "editor/color.hpp"
#include <string>

namespace editor::color {
// Explicit normalized straight-alpha RGB709 texture encodings. NOT raw video/YUV,
// PQ/HLG, Rec2020, premultiplied input, or an auto-detected media color description.
enum class TextureEncoding { srgb = 0, linear709 = 1, media3_sdr709 = 2 };
void validate(TextureEncoding encoding);
// Canonical metadata values, independent of Android/Media3 numeric constants.
enum class VideoPrimaries { unknown=0, rec709=1, other=2 };
enum class VideoTransfer { unknown=0, sdr_video=1, srgb=2, pq=3, hlg=4, other=5 };
enum class VideoRange { unknown=0, full=1, limited=2 };
struct VideoColorMetadata {
    VideoPrimaries primaries{VideoPrimaries::unknown};
    VideoTransfer transfer{VideoTransfer::unknown};
    VideoRange range{VideoRange::unknown};
    bool has_hdr_static_metadata{false};
};
// Admission policy only; decoder/platform owns YUV/range normalization to the
// explicit Media3 DEFAULT RGB texture. Never infer missing tags from resolution.
void validate_sdr_video_source(const VideoColorMetadata& source);
struct DecodedVideoFormat {
    int width{};
    int height{};
    VideoColorMetadata color{};
};
// Android crop right/bottom are inclusive. Mask0=no crop,15=all four keys.
// Returns pre-rotation visible pixels; never infers padding, swaps dimensions or uses stride.
[[nodiscard]] std::array<int,2> visible_video_geometry(int width,int height,int crop_mask,
    const std::array<int,4>& crop);
// Strict subset: unknown tags, changed normalized visible geometry/range/transfer fail.
// Does not verify URI byte identity, rotation/crop transforms, or texture pixels.
void validate_decoded_video_format(const DecodedVideoFormat& expected, const DecodedVideoFormat& actual);
[[nodiscard]] Rgb texture_reference(Rgb normalized, const Grade& grade, TextureEncoding encoding);
// Original GLES 2 shader, immutable parameters baked by C++ (no Kotlin grade math).
// Require fragment highp; reject grades whose intermediates cannot safely fit floats.
// Caller owns GL/context/lifetime. Output alpha unchanged, no timing/geometry transform.
[[nodiscard]] std::string grade_vertex_shader();
[[nodiscard]] std::string grade_fragment_shader(const Grade& grade, TextureEncoding encoding);
} // namespace editor::color

#include "timeline/media_pool.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

#if defined(__ANDROID__)
#include <fcntl.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace owncut::timeline {
namespace {
constexpr double kTwoPi = 6.28318530717958647692;

std::string to_lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

#if defined(__ANDROID__)
Yuv420Layout infer_yuv_layout(int32_t color_format) noexcept {
    // 19 = YUV420Planar (I420); 21 = YUV420SemiPlanar (NV12); 39/0x7FA30C04/2135033992 handled safely
    if (color_format == 19) return Yuv420Layout::I420;
    if (color_format == 39) return Yuv420Layout::NV21;
    return Yuv420Layout::NV12;
}

class AndroidMediaCodecDecoder final : public IMediaDecoder {
public:
    AndroidMediaCodecDecoder() = default;
    AndroidMediaCodecDecoder(int fd, int64_t offset_bytes, int64_t length_bytes)
        : owned_fd_(fd >= 0 ? ::dup(fd) : -1)
        , fd_offset_(std::max<int64_t>(0, offset_bytes))
        , fd_length_(length_bytes) {}

    ~AndroidMediaCodecDecoder() override { close(); }

    [[nodiscard]] DecoderBackendKind backend_kind() const noexcept override {
        return DecoderBackendKind::AndroidMediaCodec;
    }
    [[nodiscard]] bool supports_hardware() const noexcept override { return true; }

    bool open(const std::string& uri, MediaMetadata& out_meta) override {
        close();
        if (uri.empty() && owned_fd_ < 0) return false;

        int tmp_fd = -1;
        int64_t tmp_off = 0;
        int64_t tmp_len = 0;
        std::string local_path;

        if (owned_fd_ >= 0) {
            tmp_fd = ::dup(owned_fd_);
            tmp_off = fd_offset_;
            tmp_len = fd_length_;
        } else if (uri.rfind("fd://", 0) == 0) {
            const long parsed_fd = std::strtol(uri.c_str() + 5, nullptr, 10);
            if (parsed_fd >= 0) tmp_fd = ::dup(static_cast<int>(parsed_fd));
        } else if (uri.rfind("file://", 0) == 0) {
            local_path = uri.substr(7);
        } else if (!uri.empty() && uri[0] == '/') {
            local_path = uri;
        }

        if (tmp_fd < 0 && local_path.empty()) return false;

        AMediaExtractor* ex = AMediaExtractor_new();
        if (!ex) {
            if (tmp_fd >= 0) ::close(tmp_fd);
            return false;
        }

        media_status_t st = AMEDIA_ERROR_UNKNOWN;
        if (tmp_fd >= 0) {
            if (tmp_len <= 0) {
                struct stat sb {};
                if (::fstat(tmp_fd, &sb) == 0 && sb.st_size > tmp_off) {
                    tmp_len = static_cast<int64_t>(sb.st_size) - tmp_off;
                }
            }
            if (tmp_len > 0) {
                st = AMediaExtractor_setDataSourceFd(ex, tmp_fd, static_cast<off64_t>(tmp_off), static_cast<off64_t>(tmp_len));
            }
            ::close(tmp_fd);
        } else {
            st = AMediaExtractor_setDataSource(ex, local_path.c_str());
        }

        if (st != AMEDIA_OK) {
            AMediaExtractor_delete(ex);
            return false;
        }

        const size_t track_count = AMediaExtractor_getTrackCount(ex);
        int32_t v_track = -1;
        int32_t a_track = -1;
        MediaMetadata meta;
        meta.uri = uri;
        const auto slash = uri.find_last_of('/');
        meta.name = (slash == std::string::npos) ? uri : uri.substr(slash + 1);
        int64_t max_dur_us = 0;

        for (size_t i = 0; i < track_count; ++i) {
            AMediaFormat* fmt = AMediaExtractor_getTrackFormat(ex, i);
            if (!fmt) continue;
            const char* mime = nullptr;
            if (AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &mime) && mime) {
                int64_t dur_us = 0;
                AMediaFormat_getInt64(fmt, AMEDIAFORMAT_KEY_DURATION, &dur_us);
                max_dur_us = std::max(max_dur_us, dur_us);

                if (v_track < 0 && std::strncmp(mime, "video/", 6) == 0) {
                    v_track = static_cast<int32_t>(i);
                    int32_t w = 0, h = 0, fps_int = 30;
                    AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_WIDTH, &w);
                    AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, &h);
                    AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_FRAME_RATE, &fps_int);
                    if (w > 0) meta.width = w;
                    if (h > 0) meta.height = h;
                    if (fps_int > 0) meta.fps = FrameRate(fps_int, 1);
                    meta.codec_name = mime;
                    video_mime_ = mime;
                } else if (a_track < 0 && std::strncmp(mime, "audio/", 6) == 0) {
                    a_track = static_cast<int32_t>(i);
                    int32_t sr = 48000, ch = 2;
                    AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, &sr);
                    AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &ch);
                    if (sr > 0) meta.audio_sample_rate = sr;
                    if (ch > 0) meta.audio_channels = ch;
                    audio_mime_ = mime;
                }
            }
            AMediaFormat_delete(fmt);
        }

        AMediaExtractor_delete(ex);
        if (v_track < 0 && a_track < 0) return false;

        if (max_dur_us > 0) {
            meta.duration = RationalTime::from_milliseconds(max_dur_us / 1000);
        }
        uri_ = uri;
        local_path_ = local_path;
        video_track_idx_ = v_track;
        audio_track_idx_ = a_track;
        meta_ = meta;
        out_meta = meta;
        opened_ = true;
        return true;
    }

    bool decode_video_frame(const RationalTime& source_time,
                            VideoFrame& out_frame,
                            FrameMemoryPool* pool = nullptr) override {
        if (!opened_ || video_track_idx_ < 0 || video_mime_.empty()) return false;
        AMediaExtractor* ex = create_extractor_locked();
        if (!ex) return false;

        AMediaExtractor_selectTrack(ex, static_cast<size_t>(video_track_idx_));
        AMediaFormat* fmt = AMediaExtractor_getTrackFormat(ex, static_cast<size_t>(video_track_idx_));
        if (!fmt) {
            AMediaExtractor_delete(ex);
            return false;
        }

        AMediaCodec* codec = AMediaCodec_createDecoderByType(video_mime_.c_str());
        if (!codec || AMediaCodec_configure(codec, fmt, nullptr, nullptr, 0) != AMEDIA_OK ||
            AMediaCodec_start(codec) != AMEDIA_OK) {
            if (codec) AMediaCodec_delete(codec);
            AMediaFormat_delete(fmt);
            AMediaExtractor_delete(ex);
            return false;
        }
        AMediaFormat_delete(fmt);

        const int64_t target_us = std::max<int64_t>(0, source_time.to_milliseconds() * 1000);
        AMediaExtractor_seekTo(ex, target_us, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);

        bool saw_eos = false;
        bool decoded = false;
        int32_t out_w = meta_.width;
        int32_t out_h = meta_.height;
        int32_t stride = meta_.width;
        int32_t slice_h = meta_.height;
        int32_t color_fmt = 21;

        for (int iter = 0; iter < 180 && !decoded; ++iter) {
            if (!saw_eos) {
                const ssize_t in_idx = AMediaCodec_dequeueInputBuffer(codec, 5000);
                if (in_idx >= 0) {
                    size_t buf_sz = 0;
                    uint8_t* in_buf = AMediaCodec_getInputBuffer(codec, static_cast<size_t>(in_idx), &buf_sz);
                    const ssize_t sample_sz = in_buf ? AMediaExtractor_readSampleData(ex, in_buf, buf_sz) : -1;
                    if (sample_sz < 0) {
                        AMediaCodec_queueInputBuffer(codec, static_cast<size_t>(in_idx), 0, 0, 0,
                                                     AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                        saw_eos = true;
                    } else {
                        const int64_t pts_us = AMediaExtractor_getSampleTime(ex);
                        AMediaCodec_queueInputBuffer(codec, static_cast<size_t>(in_idx), 0,
                                                     static_cast<size_t>(sample_sz),
                                                     static_cast<uint64_t>(std::max<int64_t>(0, pts_us)), 0);
                        AMediaExtractor_advance(ex);
                    }
                }
            }

            AMediaCodecBufferInfo info{};
            const ssize_t out_idx = AMediaCodec_dequeueOutputBuffer(codec, &info, 5000);
            if (out_idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                if (AMediaFormat* ofmt = AMediaCodec_getOutputFormat(codec)) {
                    AMediaFormat_getInt32(ofmt, AMEDIAFORMAT_KEY_WIDTH, &out_w);
                    AMediaFormat_getInt32(ofmt, AMEDIAFORMAT_KEY_HEIGHT, &out_h);
                    if (!AMediaFormat_getInt32(ofmt, "stride", &stride) || stride <= 0) stride = out_w;
                    if (!AMediaFormat_getInt32(ofmt, "slice-height", &slice_h) || slice_h <= 0) slice_h = out_h;
                    AMediaFormat_getInt32(ofmt, AMEDIAFORMAT_KEY_COLOR_FORMAT, &color_fmt);
                    AMediaFormat_delete(ofmt);
                }
            } else if (out_idx >= 0) {
                const bool reached_target = (info.presentationTimeUs >= target_us) ||
                                            ((info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0);
                if (reached_target && info.offset >= 0 && info.size > 0) {
                    size_t out_buf_sz = 0;
                    const uint8_t* out_buf = AMediaCodec_getOutputBuffer(codec, static_cast<size_t>(out_idx), &out_buf_sz);
                    if (out_buf && out_buf_sz >= static_cast<size_t>(info.offset) + static_cast<size_t>(info.size)) {
                        decoded = convert_yuv420_to_rgba32f(
                            out_buf + info.offset, static_cast<size_t>(info.size),
                            out_w, out_h, stride, slice_h,
                            infer_yuv_layout(color_fmt), true, out_frame, pool);
                        if (decoded) {
                            out_frame.pts = RationalTime::from_milliseconds(info.presentationTimeUs / 1000);
                        }
                    }
                }
                AMediaCodec_releaseOutputBuffer(codec, static_cast<size_t>(out_idx), false);
                if ((info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0) break;
            }
        }

        AMediaCodec_stop(codec);
        AMediaCodec_delete(codec);
        AMediaExtractor_delete(ex);
        return decoded;
    }

    bool decode_audio_samples(const RationalTime& source_start,
                              int64_t frame_count,
                              int32_t sample_rate,
                              int32_t channels,
                              AudioBuffer& out_audio) override {
        if (!opened_ || audio_track_idx_ < 0 || audio_mime_.empty() ||
            frame_count <= 0 || sample_rate <= 0 || channels <= 0) {
            return false;
        }
        AMediaExtractor* ex = create_extractor_locked();
        if (!ex) return false;

        AMediaExtractor_selectTrack(ex, static_cast<size_t>(audio_track_idx_));
        AMediaFormat* fmt = AMediaExtractor_getTrackFormat(ex, static_cast<size_t>(audio_track_idx_));
        if (!fmt) {
            AMediaExtractor_delete(ex);
            return false;
        }

        AMediaCodec* codec = AMediaCodec_createDecoderByType(audio_mime_.c_str());
        if (!codec || AMediaCodec_configure(codec, fmt, nullptr, nullptr, 0) != AMEDIA_OK ||
            AMediaCodec_start(codec) != AMEDIA_OK) {
            if (codec) AMediaCodec_delete(codec);
            AMediaFormat_delete(fmt);
            AMediaExtractor_delete(ex);
            return false;
        }
        AMediaFormat_delete(fmt);

        const int64_t start_us = std::max<int64_t>(0, source_start.to_milliseconds() * 1000);
        AMediaExtractor_seekTo(ex, start_us, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);

        int32_t dec_sr = meta_.audio_sample_rate;
        int32_t dec_ch = meta_.audio_channels;
        std::vector<float> collected;
        collected.reserve(static_cast<size_t>(frame_count * std::max(1, dec_ch)));

        bool saw_eos = false;
        const int64_t needed_frames = std::max<int64_t>(1, (frame_count * dec_sr) / std::max(1, sample_rate));
        for (int iter = 0; iter < 512 && static_cast<int64_t>(collected.size()) < needed_frames * dec_ch; ++iter) {
            if (!saw_eos) {
                const ssize_t in_idx = AMediaCodec_dequeueInputBuffer(codec, 4000);
                if (in_idx >= 0) {
                    size_t buf_sz = 0;
                    uint8_t* in_buf = AMediaCodec_getInputBuffer(codec, static_cast<size_t>(in_idx), &buf_sz);
                    const ssize_t sample_sz = in_buf ? AMediaExtractor_readSampleData(ex, in_buf, buf_sz) : -1;
                    if (sample_sz < 0) {
                        AMediaCodec_queueInputBuffer(codec, static_cast<size_t>(in_idx), 0, 0, 0,
                                                     AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                        saw_eos = true;
                    } else {
                        const int64_t pts_us = AMediaExtractor_getSampleTime(ex);
                        AMediaCodec_queueInputBuffer(codec, static_cast<size_t>(in_idx), 0,
                                                     static_cast<size_t>(sample_sz),
                                                     static_cast<uint64_t>(std::max<int64_t>(0, pts_us)), 0);
                        AMediaExtractor_advance(ex);
                    }
                }
            }
            AMediaCodecBufferInfo info{};
            const ssize_t out_idx = AMediaCodec_dequeueOutputBuffer(codec, &info, 4000);
            if (out_idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                if (AMediaFormat* ofmt = AMediaCodec_getOutputFormat(codec)) {
                    AMediaFormat_getInt32(ofmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, &dec_sr);
                    AMediaFormat_getInt32(ofmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &dec_ch);
                    AMediaFormat_delete(ofmt);
                }
            } else if (out_idx >= 0) {
                if (info.offset >= 0 && info.size > 0 && info.presentationTimeUs >= start_us) {
                    size_t out_buf_sz = 0;
                    const uint8_t* out_buf = AMediaCodec_getOutputBuffer(codec, static_cast<size_t>(out_idx), &out_buf_sz);
                    if (out_buf && out_buf_sz >= static_cast<size_t>(info.offset) + static_cast<size_t>(info.size)) {
                        const int16_t* pcm = reinterpret_cast<const int16_t*>(out_buf + info.offset);
                        const int64_t n_samples = static_cast<int64_t>(info.size) / static_cast<int64_t>(sizeof(int16_t));
                        for (int64_t s = 0; s < n_samples; ++s) {
                            collected.push_back(static_cast<float>(pcm[s]) / 32768.0f);
                        }
                    }
                }
                AMediaCodec_releaseOutputBuffer(codec, static_cast<size_t>(out_idx), false);
                if ((info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0) break;
            }
        }

        AMediaCodec_stop(codec);
        AMediaCodec_delete(codec);
        AMediaExtractor_delete(ex);

        if (collected.empty() || dec_ch <= 0 || dec_sr <= 0) return false;
        const int64_t dec_frames = static_cast<int64_t>(collected.size()) / dec_ch;
        out_audio.sample_rate = sample_rate;
        out_audio.channels = channels;
        out_audio.frame_count = frame_count;
        out_audio.pts = source_start;
        out_audio.interleaved.assign(static_cast<size_t>(frame_count * channels), 0.0f);

        for (int64_t i = 0; i < frame_count; ++i) {
            const int64_t src_idx = std::min<int64_t>(dec_frames - 1, (i * dec_sr) / sample_rate);
            for (int32_t ch = 0; ch < channels; ++ch) {
                const int32_t src_ch = (ch < dec_ch) ? ch : 0;
                out_audio.set_sample(i, ch, collected[static_cast<size_t>(src_idx * dec_ch + src_ch)]);
            }
        }
        return true;
    }

    void close() noexcept override {
        opened_ = false;
        if (owned_fd_ >= 0) {
            ::close(owned_fd_);
            owned_fd_ = -1;
        }
    }

private:
    AMediaExtractor* create_extractor_locked() const {
        AMediaExtractor* ex = AMediaExtractor_new();
        if (!ex) return nullptr;
        media_status_t st = AMEDIA_ERROR_UNKNOWN;
        if (owned_fd_ >= 0) {
            int dup_fd = ::dup(owned_fd_);
            if (dup_fd >= 0) {
                int64_t len = fd_length_;
                if (len <= 0) {
                    struct stat sb {};
                    if (::fstat(dup_fd, &sb) == 0 && sb.st_size > fd_offset_) {
                        len = static_cast<int64_t>(sb.st_size) - fd_offset_;
                    }
                }
                if (len > 0) {
                    st = AMediaExtractor_setDataSourceFd(ex, dup_fd, static_cast<off64_t>(fd_offset_), static_cast<off64_t>(len));
                }
                ::close(dup_fd);
            }
        } else if (!local_path_.empty()) {
            st = AMediaExtractor_setDataSource(ex, local_path_.c_str());
        }
        if (st != AMEDIA_OK) {
            AMediaExtractor_delete(ex);
            return nullptr;
        }
        return ex;
    }

    int owned_fd_ = -1;
    int64_t fd_offset_ = 0;
    int64_t fd_length_ = 0;
    std::string uri_;
    std::string local_path_;
    std::string video_mime_;
    std::string audio_mime_;
    int32_t video_track_idx_ = -1;
    int32_t audio_track_idx_ = -1;
    MediaMetadata meta_{};
    bool opened_ = false;
};
#endif
} // namespace

bool convert_yuv420_to_rgba32f(const uint8_t* yuv_data,
                                 std::size_t yuv_bytes,
                                 int32_t width,
                                 int32_t height,
                                 int32_t stride,
                                 int32_t slice_height,
                                 Yuv420Layout layout,
                                 bool bt709,
                                 VideoFrame& out_frame,
                                 FrameMemoryPool* pool) {
    if (!yuv_data || width <= 0 || height <= 0) return false;
    const int32_t safe_stride = std::max(width, stride);
    const int32_t safe_slice = std::max(height, slice_height);
    const std::size_t y_plane_bytes = static_cast<std::size_t>(safe_stride) * static_cast<std::size_t>(safe_slice);
    const int32_t uv_h = (height + 1) / 2;
    const int32_t uv_w = (width + 1) / 2;

    if (yuv_bytes < static_cast<std::size_t>(safe_stride) * static_cast<std::size_t>(height)) return false;

    std::size_t min_needed = 0;
    if (layout == Yuv420Layout::I420) {
        const std::size_t uv_stride = static_cast<std::size_t>((safe_stride + 1) / 2);
        const std::size_t uv_plane_bytes = uv_stride * static_cast<std::size_t>((safe_slice + 1) / 2);
        min_needed = y_plane_bytes + uv_plane_bytes + uv_stride * static_cast<std::size_t>(uv_h);
    } else {
        min_needed = y_plane_bytes + static_cast<std::size_t>(safe_stride) * static_cast<std::size_t>(uv_h);
    }
    if (yuv_bytes < min_needed) return false;

    out_frame = VideoFrame::allocate(width, height, pool);
    if (!out_frame.valid()) return false;

    float* dst = out_frame.data();
    const uint8_t* y_plane = yuv_data;
    const float kr = bt709 ? 1.5748f : 1.4020f;
    const float kg_u = bt709 ? -0.1873f : -0.344136f;
    const float kg_v = bt709 ? -0.4681f : -0.714136f;
    const float kb = bt709 ? 1.8556f : 1.7720f;

    const std::size_t uv_stride_i420 = static_cast<std::size_t>((safe_stride + 1) / 2);
    const std::size_t uv_slice_i420 = static_cast<std::size_t>((safe_slice + 1) / 2);
    const uint8_t* u_plane = yuv_data + y_plane_bytes;
    const uint8_t* v_plane = (layout == Yuv420Layout::I420)
                                 ? (u_plane + uv_stride_i420 * uv_slice_i420)
                                 : u_plane;

    for (int32_t row = 0; row < height; ++row) {
        const uint8_t* y_row = y_plane + static_cast<std::size_t>(row) * static_cast<std::size_t>(safe_stride);
        const int32_t chroma_y = std::min(uv_h - 1, row / 2);
        float* out_row = dst + static_cast<std::size_t>(row) * static_cast<std::size_t>(out_frame.stride_floats);

        for (int32_t col = 0; col < width; ++col) {
            const float yf = std::clamp((static_cast<float>(y_row[col]) - 16.0f) / 219.0f, 0.0f, 1.0f);
            const int32_t chroma_x = std::min(uv_w - 1, col / 2);
            uint8_t u8 = 128;
            uint8_t v8 = 128;
            if (layout == Yuv420Layout::I420) {
                const std::size_t off = static_cast<std::size_t>(chroma_y) * uv_stride_i420 + static_cast<std::size_t>(chroma_x);
                u8 = u_plane[off];
                v8 = v_plane[off];
            } else if (layout == Yuv420Layout::NV12) {
                const std::size_t off = static_cast<std::size_t>(chroma_y) * static_cast<std::size_t>(safe_stride) + static_cast<std::size_t>(chroma_x * 2);
                u8 = u_plane[off + 0];
                v8 = u_plane[off + 1];
            } else {
                const std::size_t off = static_cast<std::size_t>(chroma_y) * static_cast<std::size_t>(safe_stride) + static_cast<std::size_t>(chroma_x * 2);
                v8 = u_plane[off + 0];
                u8 = u_plane[off + 1];
            }

            const float uf = (static_cast<float>(u8) - 128.0f) / 224.0f;
            const float vf = (static_cast<float>(v8) - 128.0f) / 224.0f;

            out_row[col * 4 + 0] = std::clamp(yf + kr * vf, 0.0f, 1.0f);
            out_row[col * 4 + 1] = std::clamp(yf + kg_u * uf + kg_v * vf, 0.0f, 1.0f);
            out_row[col * 4 + 2] = std::clamp(yf + kb * uf, 0.0f, 1.0f);
            out_row[col * 4 + 3] = 1.0f;
        }
    }
    return true;
}

bool convert_pcm16_to_audio_buffer(const int16_t* pcm_interleaved,
                                   int64_t frame_count,
                                   int32_t sample_rate,
                                   int32_t channels,
                                   const RationalTime& pts,
                                   AudioBuffer& out_audio) {
    if (!pcm_interleaved || frame_count <= 0 || sample_rate <= 0 || channels <= 0) return false;
    out_audio.sample_rate = sample_rate;
    out_audio.channels = channels;
    out_audio.frame_count = frame_count;
    out_audio.pts = pts;
    const std::size_t total = static_cast<std::size_t>(frame_count) * static_cast<std::size_t>(channels);
    out_audio.interleaved.resize(total);
    for (std::size_t i = 0; i < total; ++i) {
        out_audio.interleaved[i] = std::clamp(static_cast<float>(pcm_interleaved[i]) / 32768.0f, -1.0f, 1.0f);
    }
    return true;
}

VideoFrame VideoFrame::allocate(int32_t w, int32_t h, FrameMemoryPool* pool) {
    VideoFrame frame;
    if (w <= 0 || h <= 0) return frame;
    frame.width = w;
    frame.height = h;
    frame.stride_floats = w * 4;
    frame.format = PixelFormat::RGBA32F;
    const std::size_t needed_bytes = static_cast<std::size_t>(frame.stride_floats) * static_cast<std::size_t>(h) * sizeof(float);
    if (pool != nullptr && pool->slab_bytes() >= needed_bytes) {
        frame.pooled = pool->acquire();
    }
    if (!frame.pooled.valid()) {
        frame.owned_rgba32f.assign(static_cast<std::size_t>(frame.stride_floats) * static_cast<std::size_t>(h), 0.0f);
    }
    return frame;
}

bool SyntheticMediaDecoder::open(const std::string& uri, MediaMetadata& out_meta) {
    if (uri.empty()) return false;
    meta_.uri = uri;
    if (meta_.name.empty()) {
        const auto slash = uri.find_last_of('/');
        meta_.name = (slash == std::string::npos) ? uri : uri.substr(slash + 1);
    }
    out_meta = meta_;
    opened_ = true;
    return true;
}

bool SyntheticMediaDecoder::decode_video_frame(const RationalTime& source_time,
                                                 VideoFrame& out_frame,
                                                 FrameMemoryPool* pool) {
    if (!opened_ && !meta_.uri.empty()) opened_ = true;
    if (meta_.width <= 0 || meta_.height <= 0) return false;

    out_frame = VideoFrame::allocate(meta_.width, meta_.height, pool);
    if (!out_frame.valid()) return false;
    out_frame.pts = source_time;
    out_frame.bit_depth = meta_.source_bit_depth;

    float* dst = out_frame.data();
    const int32_t w = out_frame.width;
    const int32_t h = out_frame.height;
    const int32_t stride = out_frame.stride_floats;
    const double t_sec = source_time.to_seconds();
    const int64_t frame_idx = source_time.to_frames(meta_.fps);

    // Optional simulated camera jitter for Stabilization tests.
    const double jx = jitter_x_ * std::sin(t_sec * 17.0);
    const double jy = jitter_y_ * std::cos(t_sec * 13.0);

    if (pattern_ == SyntheticPattern::ColorBarsAndSweep) {
        for (int32_t y = 0; y < h; ++y) {
            float* row = dst + y * stride;
            const float fy = static_cast<float>(y) / static_cast<float>(std::max(1, h - 1));
            for (int32_t x = 0; x < w; ++x) {
                const int bar = (x * 7) / std::max(1, w);
                float r = ((bar & 1) ? 0.85f : 0.15f);
                float g = ((bar & 2) ? 0.85f : 0.15f);
                float b = ((bar & 4) ? 0.85f : 0.15f);
                const float sweep = std::fmod(static_cast<float>(x) / static_cast<float>(w) + static_cast<float>(t_sec * 0.25), 1.0f);
                r = 0.7f * r + 0.3f * sweep;
                g = 0.7f * g + 0.3f * fy;
                row[x * 4 + 0] = r;
                row[x * 4 + 1] = g;
                row[x * 4 + 2] = b;
                row[x * 4 + 3] = 1.0f;
            }
        }
    } else if (pattern_ == SyntheticPattern::ShotChangeAtHalf) {
        const bool second_half = source_time >= (meta_.duration / 2);
        const float r = second_half ? 0.90f : 0.10f;
        const float g = second_half ? 0.25f : 0.80f;
        const float b = second_half ? 0.15f : 0.90f;
        for (int32_t y = 0; y < h; ++y) {
            float* row = dst + y * stride;
            for (int32_t x = 0; x < w; ++x) {
                row[x * 4 + 0] = r;
                row[x * 4 + 1] = g;
                row[x * 4 + 2] = b;
                row[x * 4 + 3] = 1.0f;
            }
        }
    } else if (pattern_ == SyntheticPattern::CheckerMotion) {
        const double ox = t_sec * 20.0 + jx;
        const double oy = t_sec * 10.0 + jy;
        for (int32_t y = 0; y < h; ++y) {
            float* row = dst + y * stride;
            for (int32_t x = 0; x < w; ++x) {
                const double sx = static_cast<double>(x) - ox;
                const double sy = static_cast<double>(y) - oy;
                const float v = static_cast<float>(0.5 + 0.4 * std::sin(sx * 0.25) * std::cos(sy * 0.25));
                row[x * 4 + 0] = v;
                row[x * 4 + 1] = v * 0.9f;
                row[x * 4 + 2] = 1.0f - v * 0.5f;
                row[x * 4 + 3] = 1.0f;
            }
        }
    } else { // MovingBlock (rich smooth Gaussian blob + textured grid for Optical Flow / Stabilizer)
        const double cx = static_cast<double>(w) * 0.25 + static_cast<double>(frame_idx) * 2.0 + jx;
        const double cy = static_cast<double>(h) * 0.5 + std::sin(t_sec * 2.0) * (h * 0.1) + jy;
        const double inv_r2 = 1.0 / std::max(16.0, static_cast<double>(w * w) * 0.01);
        for (int32_t y = 0; y < h; ++y) {
            float* row = dst + y * stride;
            const double dy = static_cast<double>(y) - cy;
            for (int32_t x = 0; x < w; ++x) {
                const double dx = static_cast<double>(x) - cx;
                const double d2 = dx * dx + dy * dy;
                const float bg = 0.12f + 0.08f * static_cast<float>(std::sin((x - jx) * 0.15) * std::cos((y - jy) * 0.15));
                const float blob = static_cast<float>(std::exp(-d2 * inv_r2));
                row[x * 4 + 0] = std::clamp(bg + 0.85f * blob, 0.0f, 1.0f);
                row[x * 4 + 1] = std::clamp(bg + 0.55f * blob, 0.0f, 1.0f);
                row[x * 4 + 2] = std::clamp(bg + 0.25f * blob, 0.0f, 1.0f);
                row[x * 4 + 3] = 1.0f;
            }
        }
    }
    return true;
}

bool SyntheticMediaDecoder::decode_audio_samples(const RationalTime& source_start,
                                                   int64_t frame_count,
                                                   int32_t sample_rate,
                                                   int32_t channels,
                                                   AudioBuffer& out_audio) {
    if (frame_count <= 0 || sample_rate <= 0 || channels <= 0) return false;
    out_audio.sample_rate = sample_rate;
    out_audio.channels = channels;
    out_audio.frame_count = frame_count;
    out_audio.pts = source_start;
    out_audio.interleaved.assign(static_cast<std::size_t>(frame_count * channels), 0.0f);

    const int64_t start_sample = source_start.to_samples(sample_rate);
    for (int64_t i = 0; i < frame_count; ++i) {
        const int64_t s = start_sample + i - audio_delay_samples_;
        float v = 0.0f;
        if (s >= 0) {
            const double t = static_cast<double>(s) / static_cast<double>(sample_rate);
            if (pulse_period_samples_ > 0) {
                const int64_t phase = s % pulse_period_samples_;
                if (phase < 128) {
                    const double env = 1.0 - static_cast<double>(phase) / 128.0;
                    v = static_cast<float>(0.9 * env * std::sin(kTwoPi * 120.0 * t));
                } else {
                    v = static_cast<float>(0.05 * std::sin(kTwoPi * tone_hz_ * t));
                }
            } else {
                v = static_cast<float>(0.45 * std::sin(kTwoPi * tone_hz_ * t));
            }
        }
        for (int32_t c = 0; c < channels; ++c) {
            out_audio.set_sample(i, c, v);
        }
    }
    return true;
}

bool PlatformHardwareDecoderDescriptor::is_ffmpeg_available() noexcept {
#if defined(OWNCUT_HAS_FFMPEG)
    return true;
#else
    return false;
#endif
}

bool PlatformHardwareDecoderDescriptor::is_mediacodec_available() noexcept {
#if defined(__ANDROID__)
    return true;
#else
    return false;
#endif
}

bool PlatformHardwareDecoderDescriptor::is_videotoolbox_available() noexcept {
#if defined(__APPLE__)
    return true;
#else
    return false;
#endif
}

std::unique_ptr<IMediaDecoder> PlatformHardwareDecoderDescriptor::create_best_decoder(
    const std::string& uri,
    SyntheticPattern fallback_pattern) {
#if defined(__ANDROID__)
    if (!uri.empty() && (uri[0] == '/' || uri.rfind("file://", 0) == 0 || uri.rfind("fd://", 0) == 0)) {
        auto hw = std::make_unique<AndroidMediaCodecDecoder>();
        MediaMetadata meta;
        if (hw->open(uri, meta)) {
            return hw;
        }
    }
#else
    (void)uri;
#endif
    return std::make_unique<SyntheticMediaDecoder>(fallback_pattern);
}

std::unique_ptr<IMediaDecoder> PlatformHardwareDecoderDescriptor::create_fd_decoder(
    int fd,
    int64_t offset_bytes,
    int64_t length_bytes,
    const std::string& label_uri) {
#if defined(__ANDROID__)
    if (fd >= 0) {
        auto hw = std::make_unique<AndroidMediaCodecDecoder>(fd, offset_bytes, length_bytes);
        MediaMetadata meta;
        if (hw->open(label_uri.empty() ? ("fd://" + std::to_string(fd)) : label_uri, meta)) {
            return hw;
        }
    }
#else
    (void)fd;
    (void)offset_bytes;
    (void)length_bytes;
    (void)label_uri;
#endif
    return std::make_unique<SyntheticMediaDecoder>(SyntheticPattern::MovingBlock);
}

ThumbnailMipmapPyramid ThumbnailMipmapPyramid::build(const VideoFrame& source,
                                                     int32_t base_width,
                                                     int32_t base_height,
                                                     int32_t num_levels) {
    ThumbnailMipmapPyramid pyr;
    pyr.source_time = source.pts;
    if (!source.valid() || base_width <= 0 || base_height <= 0 || num_levels <= 0) return pyr;

    int32_t lw = base_width;
    int32_t lh = base_height;
    for (int32_t lvl = 0; lvl < num_levels; ++lvl) {
        ThumbnailLevel level;
        level.width = lw;
        level.height = lh;
        level.rgba32f.resize(static_cast<std::size_t>(lw * lh * 4));
        const float* src = (lvl == 0) ? source.data() : pyr.levels.back().rgba32f.data();
        const int32_t sw = (lvl == 0) ? source.width : pyr.levels.back().width;
        const int32_t sh = (lvl == 0) ? source.height : pyr.levels.back().height;
        const int32_t sstride = (lvl == 0) ? source.stride_floats : sw * 4;

        for (int32_t y = 0; y < lh; ++y) {
            const int32_t sy = std::min(sh - 1, (y * sh) / lh);
            for (int32_t x = 0; x < lw; ++x) {
                const int32_t sx = std::min(sw - 1, (x * sw) / lw);
                for (int c = 0; c < 4; ++c) {
                    level.rgba32f[static_cast<std::size_t>((y * lw + x) * 4 + c)] =
                        src[static_cast<std::size_t>(sy * sstride + sx * 4 + c)];
                }
            }
        }
        pyr.levels.push_back(std::move(level));
        lw = std::max(1, lw / 2);
        lh = std::max(1, lh / 2);
    }
    return pyr;
}

WaveformMipmapPyramid WaveformMipmapPyramid::build(const AudioBuffer& audio,
                                                   const std::vector<int64_t>& bucket_sizes) {
    WaveformMipmapPyramid pyr;
    pyr.sample_rate = audio.sample_rate;
    if (audio.frame_count <= 0 || audio.channels <= 0) return pyr;

    for (int64_t bsize : bucket_sizes) {
        if (bsize <= 0) continue;
        WaveformMipLevel level;
        level.samples_per_bucket = bsize;
        const int64_t num_buckets = (audio.frame_count + bsize - 1) / bsize;
        level.buckets.reserve(static_cast<std::size_t>(num_buckets));
        for (int64_t b = 0; b < num_buckets; ++b) {
            const int64_t start = b * bsize;
            const int64_t end = std::min(audio.frame_count, start + bsize);
            float mn = 1e9f;
            float mx = -1e9f;
            double sq = 0.0;
            int64_t count = 0;
            for (int64_t f = start; f < end; ++f) {
                float mono = 0.0f;
                for (int32_t ch = 0; ch < audio.channels; ++ch) {
                    mono += audio.sample(f, ch);
                }
                mono /= static_cast<float>(audio.channels);
                mn = std::min(mn, mono);
                mx = std::max(mx, mono);
                sq += static_cast<double>(mono) * static_cast<double>(mono);
                ++count;
            }
            WaveformBucket bucket;
            if (count > 0) {
                bucket.min_sample = mn;
                bucket.max_sample = mx;
                bucket.rms = static_cast<float>(std::sqrt(sq / static_cast<double>(count)));
            }
            level.buckets.push_back(bucket);
        }
        pyr.levels.push_back(std::move(level));
    }
    return pyr;
}

const WaveformMipLevel* WaveformMipmapPyramid::best_level_for_zoom(double samples_per_pixel) const noexcept {
    if (levels.empty()) return nullptr;
    const WaveformMipLevel* best = &levels.front();
    for (const auto& lvl : levels) {
        if (static_cast<double>(lvl.samples_per_bucket) <= samples_per_pixel) {
            best = &lvl;
        }
    }
    return best;
}

bool SmartBinRule::matches(const MediaItem& item) const {
    if (item.star_rating < min_rating) return false;
    if (required_flag.has_value()) {
        if (std::find(item.flags.begin(), item.flags.end(), *required_flag) == item.flags.end()) {
            return false;
        }
    }
    if (required_color_tag.has_value() && item.color_tag != *required_color_tag) return false;
    const double fps = item.metadata.fps.to_double();
    if (min_fps.has_value() && fps < *min_fps) return false;
    if (max_fps.has_value() && fps > *max_fps) return false;
    if (required_proxy_state.has_value() && item.proxy_state != *required_proxy_state) return false;
    if (!name_contains.empty()) {
        if (to_lower(item.metadata.name).find(to_lower(name_contains)) == std::string::npos) {
            return false;
        }
    }
    return true;
}

uint64_t MediaPool::add_item(MediaItem item) {
    if (item.id == 0) {
        item.id = next_id_++;
    } else {
        next_id_ = std::max(next_id_, item.id + 1);
    }
    if (!item.decoder) {
        item.decoder = std::make_shared<SyntheticMediaDecoder>(SyntheticPattern::MovingBlock, item.metadata);
    }
    items_.push_back(std::move(item));
    return items_.back().id;
}

bool MediaPool::remove_item(uint64_t media_id) {
    const auto it = std::find_if(items_.begin(), items_.end(),
                                 [media_id](const MediaItem& m) { return m.id == media_id; });
    if (it == items_.end()) return false;
    items_.erase(it);
    return true;
}

MediaItem* MediaPool::find(uint64_t media_id) {
    const auto it = std::find_if(items_.begin(), items_.end(),
                                 [media_id](const MediaItem& m) { return m.id == media_id; });
    return it == items_.end() ? nullptr : &(*it);
}

const MediaItem* MediaPool::find(uint64_t media_id) const {
    const auto it = std::find_if(items_.begin(), items_.end(),
                                 [media_id](const MediaItem& m) { return m.id == media_id; });
    return it == items_.end() ? nullptr : &(*it);
}

bool MediaPool::set_flag(uint64_t media_id, ClipFlagColor flag) {
    MediaItem* item = find(media_id);
    if (!item) return false;
    if (flag == ClipFlagColor::None) {
        item->flags.clear();
    } else if (std::find(item->flags.begin(), item->flags.end(), flag) == item->flags.end()) {
        item->flags.push_back(flag);
    }
    return true;
}

bool MediaPool::set_color_tag(uint64_t media_id, std::string tag) {
    MediaItem* item = find(media_id);
    if (!item) return false;
    item->color_tag = std::move(tag);
    return true;
}

bool MediaPool::set_star_rating(uint64_t media_id, int32_t rating) {
    MediaItem* item = find(media_id);
    if (!item) return false;
    item->star_rating = std::clamp(rating, 0, 5);
    return true;
}

bool MediaPool::set_custom_tag(uint64_t media_id, std::string key, std::string value) {
    MediaItem* item = find(media_id);
    if (!item) return false;
    item->custom_tags[std::move(key)] = std::move(value);
    return true;
}

std::vector<const MediaItem*> MediaPool::all_items() const {
    std::vector<const MediaItem*> out;
    out.reserve(items_.size());
    for (const auto& item : items_) out.push_back(&item);
    return out;
}

std::vector<const MediaItem*> MediaPool::query_smart_bin(const SmartBinRule& rule) const {
    std::vector<const MediaItem*> out;
    for (const auto& item : items_) {
        if (rule.matches(item)) out.push_back(&item);
    }
    return out;
}

std::vector<const MediaItem*> MediaPool::search_metadata(const std::string& query) const {
    const std::string q = to_lower(query);
    std::vector<const MediaItem*> out;
    for (const auto& item : items_) {
        if (q.empty() ||
            to_lower(item.metadata.name).find(q) != std::string::npos ||
            to_lower(item.color_tag).find(q) != std::string::npos ||
            to_lower(item.metadata.codec_name).find(q) != std::string::npos) {
            out.push_back(&item);
            continue;
        }
        for (const auto& [k, v] : item.custom_tags) {
            if (to_lower(k).find(q) != std::string::npos || to_lower(v).find(q) != std::string::npos) {
                out.push_back(&item);
                break;
            }
        }
    }
    return out;
}

bool ProxyCacheManager::generate_proxy_for_item(MediaItem& item, int32_t target_width, int32_t target_height) {
    if (target_width <= 0 || target_height <= 0) {
        item.proxy_state = ProxyState::Failed;
        return false;
    }
    item.proxy_state = ProxyState::Generating;
    item.proxy_uri = item.metadata.uri + ".proxy_" + std::to_string(target_width) + "x" + std::to_string(target_height);
    item.proxy_state = ProxyState::Ready;
    return true;
}

void ProxyCacheManager::put_smart_render_frame(uint64_t timeline_id, int64_t frame_index, VideoFrame frame) {
    for (auto& e : cache_) {
        if (e.timeline_id == timeline_id && e.frame_index == frame_index) {
            e.frame = std::move(frame);
            e.lru_stamp = ++lru_counter_;
            return;
        }
    }
    if (max_cached_frames_ == 0) return;
    if (cache_.size() >= max_cached_frames_) {
        auto victim = std::min_element(cache_.begin(), cache_.end(),
                                       [](const CacheEntry& a, const CacheEntry& b) {
                                           return a.lru_stamp < b.lru_stamp;
                                       });
        cache_.erase(victim);
    }
    cache_.push_back(CacheEntry{timeline_id, frame_index, ++lru_counter_, std::move(frame)});
}

std::optional<const VideoFrame*> ProxyCacheManager::get_smart_render_frame(uint64_t timeline_id, int64_t frame_index) {
    for (auto& e : cache_) {
        if (e.timeline_id == timeline_id && e.frame_index == frame_index) {
            e.lru_stamp = ++lru_counter_;
            return &e.frame;
        }
    }
    return std::nullopt;
}

void ProxyCacheManager::invalidate_range(uint64_t timeline_id, int64_t start_frame, int64_t end_frame) {
    cache_.erase(
        std::remove_if(cache_.begin(), cache_.end(),
                       [timeline_id, start_frame, end_frame](const CacheEntry& e) {
                           return e.timeline_id == timeline_id &&
                                  e.frame_index >= start_frame &&
                                  e.frame_index <= end_frame;
                       }),
        cache_.end());
}

void ProxyCacheManager::mark_render_in_place(MediaItem& item, RenderInPlaceState state) {
    item.render_in_place_state = state;
}

} // namespace owncut::timeline

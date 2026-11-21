#pragma once

#include "timeline/memory_pool.hpp"
#include "timeline/rational_time.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace owncut::timeline {

enum class ClipFlagColor : uint8_t {
    None = 0,
    Blue,
    Cyan,
    Green,
    Yellow,
    Red,
    Pink,
    Purple
};

enum class ProxyState : uint8_t {
    None = 0,
    Generating,
    Ready,
    Failed
};

enum class SmartRenderState : uint8_t {
    Uncached = 0,
    Queued,
    Rendering,
    Cached
};

enum class RenderInPlaceState : uint8_t {
    Original = 0,
    Rendering,
    Rendered
};

enum class DecoderBackendKind : uint8_t {
    Synthetic10Bit = 0,
    AndroidMediaCodec = 1,
    AppleVideoToolbox = 2,
    FFmpegLibav = 3
};

// Owned float32 RGBA frame (backed either by FrameMemoryPool or standalone aligned storage).
struct VideoFrame final {
    int32_t width{0};
    int32_t height{0};
    int32_t stride_floats{0};
    int32_t bit_depth{10};
    RationalTime pts;
    PixelFormat format{PixelFormat::RGBA32F};
    PooledBuffer pooled;
    std::vector<float> owned_rgba32f;

    [[nodiscard]] bool valid() const noexcept {
        return width > 0 && height > 0 && stride_floats >= width * 4 && data() != nullptr;
    }

    [[nodiscard]] float* data() noexcept {
        return pooled.valid() ? pooled.as_float()
                              : (owned_rgba32f.empty() ? nullptr : owned_rgba32f.data());
    }

    [[nodiscard]] const float* data() const noexcept {
        return pooled.valid() ? pooled.as_float()
                              : (owned_rgba32f.empty() ? nullptr : owned_rgba32f.data());
    }

    [[nodiscard]] FrameBufferView32F view(uint64_t clip_id = 0,
                                          uint64_t track_id = 0,
                                          uint64_t group_id = 0) noexcept {
        return FrameBufferView32F{
            data(), width, height, stride_floats, pts.to_c64(), clip_id, track_id, group_id
        };
    }

    static VideoFrame allocate(int32_t w, int32_t h, FrameMemoryPool* pool = nullptr);
};

struct AudioBuffer final {
    int32_t sample_rate{48000};
    int32_t channels{2};
    int64_t frame_count{0};
    RationalTime pts;
    std::vector<float> interleaved; // frame_count * channels

    [[nodiscard]] float sample(int64_t frame, int32_t ch) const noexcept {
        if (frame < 0 || frame >= frame_count || ch < 0 || ch >= channels) return 0.0f;
        return interleaved[static_cast<std::size_t>(frame * channels + ch)];
    }

    void set_sample(int64_t frame, int32_t ch, float v) noexcept {
        if (frame < 0 || frame >= frame_count || ch < 0 || ch >= channels) return;
        interleaved[static_cast<std::size_t>(frame * channels + ch)] = v;
    }
};

struct MediaMetadata final {
    std::string uri;
    std::string name;
    int32_t width{1920};
    int32_t height{1080};
    FrameRate fps{FrameRate::fps_30()};
    RationalTime duration{RationalTime::from_frames(300, FrameRate::fps_30())};
    RationalTime timecode_start{RationalTime::zero()};
    int64_t creation_epoch_sec{0};
    int32_t audio_sample_rate{48000};
    int32_t audio_channels{2};
    int32_t source_bit_depth{10};
    std::string codec_name{"synthetic-10bit"};
};

// Hardware & software decoder abstraction.
class IMediaDecoder {
public:
    virtual ~IMediaDecoder() = default;
    [[nodiscard]] virtual DecoderBackendKind backend_kind() const noexcept = 0;
    [[nodiscard]] virtual bool supports_hardware() const noexcept = 0;
    [[nodiscard]] virtual bool open(const std::string& uri, MediaMetadata& out_meta) = 0;
    [[nodiscard]] virtual bool decode_video_frame(const RationalTime& source_time,
                                                  VideoFrame& out_frame,
                                                  FrameMemoryPool* pool = nullptr) = 0;
    [[nodiscard]] virtual bool decode_audio_samples(const RationalTime& source_start,
                                                    int64_t frame_count,
                                                    int32_t sample_rate,
                                                    int32_t channels,
                                                    AudioBuffer& out_audio) = 0;
    virtual void close() noexcept = 0;
};

enum class SyntheticPattern : uint8_t {
    ColorBarsAndSweep = 0,
    MovingBlock = 1,
    CheckerMotion = 2,
    ShotChangeAtHalf = 3
};

// Deterministic 10-bit/float32 frame & audio generator for standalone engine execution and tests.
class SyntheticMediaDecoder final : public IMediaDecoder {
public:
    explicit SyntheticMediaDecoder(SyntheticPattern pattern = SyntheticPattern::MovingBlock,
                                   MediaMetadata template_meta = {})
        : pattern_(pattern), meta_(std::move(template_meta)) {}

    [[nodiscard]] DecoderBackendKind backend_kind() const noexcept override {
        return DecoderBackendKind::Synthetic10Bit;
    }
    [[nodiscard]] bool supports_hardware() const noexcept override { return false; }
    [[nodiscard]] bool open(const std::string& uri, MediaMetadata& out_meta) override;
    [[nodiscard]] bool decode_video_frame(const RationalTime& source_time,
                                          VideoFrame& out_frame,
                                          FrameMemoryPool* pool = nullptr) override;
    [[nodiscard]] bool decode_audio_samples(const RationalTime& source_start,
                                            int64_t frame_count,
                                            int32_t sample_rate,
                                            int32_t channels,
                                            AudioBuffer& out_audio) override;
    void close() noexcept override { opened_ = false; }

    void set_audio_tone_hz(double hz) noexcept { tone_hz_ = hz; }
    void set_audio_pulse_period_samples(int64_t period) noexcept { pulse_period_samples_ = period; }
    void set_audio_delay_samples(int64_t delay) noexcept { audio_delay_samples_ = delay; }
    void set_camera_jitter(double amp_x, double amp_y) noexcept { jitter_x_ = amp_x; jitter_y_ = amp_y; }

private:
    SyntheticPattern pattern_;
    MediaMetadata meta_;
    bool opened_{false};
    double tone_hz_{440.0};
    int64_t pulse_period_samples_{0};
    int64_t audio_delay_samples_{0};
    double jitter_x_{0.0};
    double jitter_y_{0.0};
};

// Pure C++20 YUV420 & PCM16 decode helpers (cross-platform + used by Android NDK MediaCodec)
enum class Yuv420Layout : uint8_t {
    I420 = 0, // Y plane, then U plane (w/2 * h/2), then V plane (w/2 * h/2)
    NV12 = 1, // Y plane, then interleaved UV plane
    NV21 = 2  // Y plane, then interleaved VU plane
};

[[nodiscard]] bool convert_yuv420_to_rgba32f(const uint8_t* yuv_data,
                                             std::size_t yuv_bytes,
                                             int32_t width,
                                             int32_t height,
                                             int32_t stride,
                                             int32_t slice_height,
                                             Yuv420Layout layout,
                                             bool bt709,
                                             VideoFrame& out_frame,
                                             FrameMemoryPool* pool = nullptr);

[[nodiscard]] bool convert_pcm16_to_audio_buffer(const int16_t* pcm_interleaved,
                                                 int64_t frame_count,
                                                 int32_t sample_rate,
                                                 int32_t channels,
                                                 const RationalTime& pts,
                                                 AudioBuffer& out_audio);

// Platform hardware decoder interfaces (Android MediaCodec / iOS VideoToolbox) + optional FFmpeg detection.
class PlatformHardwareDecoderDescriptor final {
public:
    [[nodiscard]] static bool is_ffmpeg_available() noexcept;
    [[nodiscard]] static bool is_mediacodec_available() noexcept;
    [[nodiscard]] static bool is_videotoolbox_available() noexcept;
    [[nodiscard]] static std::unique_ptr<IMediaDecoder> create_best_decoder(
        const std::string& uri,
        SyntheticPattern fallback_pattern = SyntheticPattern::MovingBlock);
    [[nodiscard]] static std::unique_ptr<IMediaDecoder> create_fd_decoder(
        int fd,
        int64_t offset_bytes,
        int64_t length_bytes,
        const std::string& label_uri = "");
};

struct ThumbnailLevel final {
    int32_t width{0};
    int32_t height{0};
    std::vector<float> rgba32f;
};

struct ThumbnailMipmapPyramid final {
    RationalTime source_time;
    std::vector<ThumbnailLevel> levels; // level 0 = highest thumb res, increasing downsampled levels

    [[nodiscard]] static ThumbnailMipmapPyramid build(const VideoFrame& source,
                                                      int32_t base_width = 160,
                                                      int32_t base_height = 90,
                                                      int32_t num_levels = 3);
};

struct WaveformBucket final {
    float min_sample{0.0f};
    float max_sample{0.0f};
    float rms{0.0f};
};

struct WaveformMipLevel final {
    int64_t samples_per_bucket{64};
    std::vector<WaveformBucket> buckets;
};

struct WaveformMipmapPyramid final {
    int32_t sample_rate{48000};
    std::vector<WaveformMipLevel> levels;

    [[nodiscard]] static WaveformMipmapPyramid build(const AudioBuffer& audio,
                                                     const std::vector<int64_t>& bucket_sizes = {64, 256, 1024, 4096});
    [[nodiscard]] const WaveformMipLevel* best_level_for_zoom(double samples_per_pixel) const noexcept;
};

struct MediaItem final {
    uint64_t id{0};
    MediaMetadata metadata;
    std::vector<ClipFlagColor> flags;
    std::string color_tag;
    int32_t star_rating{0}; // 0..5
    std::unordered_map<std::string, std::string> custom_tags;
    ProxyState proxy_state{ProxyState::None};
    std::string proxy_uri;
    SmartRenderState smart_render_state{SmartRenderState::Uncached};
    RenderInPlaceState render_in_place_state{RenderInPlaceState::Original};
    std::shared_ptr<IMediaDecoder> decoder;
};

struct SmartBinRule final {
    std::string name;
    int32_t min_rating{0};
    std::optional<ClipFlagColor> required_flag;
    std::optional<std::string> required_color_tag;
    std::optional<double> min_fps;
    std::optional<double> max_fps;
    std::optional<ProxyState> required_proxy_state;
    std::string name_contains;

    [[nodiscard]] bool matches(const MediaItem& item) const;
};

class MediaPool final {
public:
    uint64_t add_item(MediaItem item);
    bool remove_item(uint64_t media_id);
    [[nodiscard]] MediaItem* find(uint64_t media_id);
    [[nodiscard]] const MediaItem* find(uint64_t media_id) const;

    bool set_flag(uint64_t media_id, ClipFlagColor flag);
    bool set_color_tag(uint64_t media_id, std::string tag);
    bool set_star_rating(uint64_t media_id, int32_t rating);
    bool set_custom_tag(uint64_t media_id, std::string key, std::string value);

    [[nodiscard]] std::vector<const MediaItem*> all_items() const;
    [[nodiscard]] std::vector<const MediaItem*> query_smart_bin(const SmartBinRule& rule) const;
    [[nodiscard]] std::vector<const MediaItem*> search_metadata(const std::string& query) const;

private:
    uint64_t next_id_{1};
    std::vector<MediaItem> items_;
};

class ProxyCacheManager final {
public:
    explicit ProxyCacheManager(std::size_t max_cached_frames = 128)
        : max_cached_frames_(max_cached_frames) {}

    bool generate_proxy_for_item(MediaItem& item, int32_t target_width = 640, int32_t target_height = 360);

    void put_smart_render_frame(uint64_t timeline_id, int64_t frame_index, VideoFrame frame);
    [[nodiscard]] std::optional<const VideoFrame*> get_smart_render_frame(uint64_t timeline_id, int64_t frame_index);
    void invalidate_range(uint64_t timeline_id, int64_t start_frame, int64_t end_frame);
    void mark_render_in_place(MediaItem& item, RenderInPlaceState state);

    [[nodiscard]] std::size_t cached_frame_count() const noexcept { return cache_.size(); }

private:
    struct CacheEntry {
        uint64_t timeline_id{0};
        int64_t frame_index{0};
        uint64_t lru_stamp{0};
        VideoFrame frame;
    };

    std::size_t max_cached_frames_;
    uint64_t lru_counter_{0};
    std::vector<CacheEntry> cache_;
};

} // namespace owncut::timeline

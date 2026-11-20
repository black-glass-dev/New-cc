// 16-bit lossless FFV1 writer (MKV) with an optional AAC stream. FP32 RGBA frames come from Kotlin as a direct
// FloatBuffer and are quantised straight into the encoder frame by editor::color::output::to_rgba16_interleaved
// (clamped to [0,1]). Audio arrives as compressed AAC packets copied from the source (no decode, no re-encode),
// stamped in microseconds on the output timeline. Encoded by FFmpeg's native ffv1 encoder.
// Built only in the FFmpeg block of CMakeLists.txt. Any failure throws java.lang.IllegalStateException with the
// FFmpeg reason. Nothing is silently skipped.

#include <jni.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "editor/color_output.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
}

namespace {

std::string ff_error(int code) {
    char text[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(code, text, sizeof text);
    return std::string(text);
}

[[noreturn]] void fail(const std::string& what, int code = 0) {
    throw std::runtime_error(code == 0 ? what : what + ": " + ff_error(code));
}

// Audio stream description. sample_rate == 0 means no audio stream.
struct AudioConfig {
    int sample_rate = 0;
    int channels = 0;
    std::vector<std::uint8_t> extradata;  // AudioSpecificConfig from the source track
};

class Ffv1Writer {
public:
    Ffv1Writer(const char* path, int width, int height, int fps_num, int fps_den, const AudioConfig& audio = {})
        : width_(width), height_(height), audio_(audio) {
        try {
            open(path, fps_num, fps_den);
        } catch (...) {
            release();  // a half-built object gets no destructor call, so free what was allocated
            throw;
        }
    }

    ~Ffv1Writer() { release(); }
    Ffv1Writer(const Ffv1Writer&) = delete;
    Ffv1Writer& operator=(const Ffv1Writer&) = delete;

    bool has_audio() const { return audio_stream_ != nullptr; }

    // rgba: width*height*4 floats, row-major, interleaved RGBA. Quantised directly into the encoder frame.
    void write(const float* rgba, std::size_t count) {
        const std::size_t pixels = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
        if (rgba == nullptr || count != pixels * 4) fail("frame float count does not match width*height*4");

        int code = av_frame_make_writable(frame_);
        if (code < 0) fail("frame not writable", code);
        const std::size_t row_values = static_cast<std::size_t>(width_) * 4;
        for (int y = 0; y < height_; ++y) {
            std::uint16_t* dst = reinterpret_cast<std::uint16_t*>(
                frame_->data[0] + static_cast<std::size_t>(y) * static_cast<std::size_t>(frame_->linesize[0]));
            editor::color::output::to_rgba16_interleaved(rgba + static_cast<std::size_t>(y) * row_values,
                                                         static_cast<std::size_t>(width_), dst);
        }
        frame_->pts = next_pts_++;
        code = avcodec_send_frame(encoder_, frame_);
        if (code < 0) fail("ffv1 send frame failed", code);
        drain();
    }

    // One compressed AAC access unit. pts_us is on the output timeline (microseconds, starts at 0).
    void write_audio(const std::uint8_t* data, std::size_t size, std::int64_t pts_us) {
        if (audio_stream_ == nullptr) fail("this export has no audio stream");
        if (data == nullptr || size == 0) fail("empty audio packet");
        if (pts_us < 0) fail("negative audio timestamp");
        if (size > static_cast<std::size_t>(INT32_MAX)) fail("audio packet too large");
        AVPacket* packet = av_packet_alloc();
        if (packet == nullptr) fail("audio packet allocation failed");
        int code = av_new_packet(packet, static_cast<int>(size));
        if (code < 0) {
            av_packet_free(&packet);
            fail("audio packet buffer failed", code);
        }
        std::memcpy(packet->data, data, size);
        packet->pts = pts_us;
        packet->dts = pts_us;
        packet->flags |= AV_PKT_FLAG_KEY;
        packet->stream_index = audio_stream_->index;
        av_packet_rescale_ts(packet, kAudioTimeBase, audio_stream_->time_base);
        code = av_interleaved_write_frame(format_, packet);  // takes ownership of the buffer
        av_packet_free(&packet);
        if (code < 0) fail("write audio packet failed", code);
    }

    void finish() {
        if (finished_) return;
        int code = avcodec_send_frame(encoder_, nullptr);
        if (code < 0 && code != AVERROR_EOF) fail("ffv1 flush failed", code);
        drain();
        code = av_write_trailer(format_);
        if (code < 0) fail("write trailer failed", code);
        finished_ = true;
    }

private:
    static constexpr AVRational kAudioTimeBase{1, 1000000};

    void open(const char* path, int fps_num, int fps_den) {
        if (width_ <= 0 || height_ <= 0 || fps_num <= 0 || fps_den <= 0) fail("invalid frame size or frame rate");
        if (audio_.sample_rate < 0 || audio_.channels < 0) fail("invalid audio parameters");
        int code = avformat_alloc_output_context2(&format_, nullptr, "matroska", path);
        if (code < 0 || format_ == nullptr) fail("matroska muxer unavailable", code);

        const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_FFV1);
        if (codec == nullptr) fail("ffv1 encoder missing from this FFmpeg build");
        encoder_ = avcodec_alloc_context3(codec);
        if (encoder_ == nullptr) fail("ffv1 context allocation failed");
        encoder_->width = width_;
        encoder_->height = height_;
        encoder_->pix_fmt = AV_PIX_FMT_RGBA64LE;
        encoder_->time_base = AVRational{fps_den, fps_num};
        encoder_->framerate = AVRational{fps_num, fps_den};
        if (format_->oformat->flags & AVFMT_GLOBALHEADER) encoder_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        code = avcodec_open2(encoder_, codec, nullptr);
        if (code < 0) fail("ffv1 open failed", code);

        stream_ = avformat_new_stream(format_, nullptr);
        if (stream_ == nullptr) fail("new video stream failed");
        code = avcodec_parameters_from_context(stream_->codecpar, encoder_);
        if (code < 0) fail("video stream parameters failed", code);
        stream_->time_base = encoder_->time_base;

        if (audio_.sample_rate > 0) {
            if (audio_.channels <= 0 || audio_.extradata.empty()) fail("AAC audio needs channels and AudioSpecificConfig");
            audio_stream_ = avformat_new_stream(format_, nullptr);
            if (audio_stream_ == nullptr) fail("new audio stream failed");
            AVCodecParameters* par = audio_stream_->codecpar;
            par->codec_type = AVMEDIA_TYPE_AUDIO;
            par->codec_id = AV_CODEC_ID_AAC;
            par->codec_tag = 0;
            par->sample_rate = audio_.sample_rate;
            av_channel_layout_default(&par->ch_layout, audio_.channels);
            par->extradata = static_cast<std::uint8_t*>(av_mallocz(audio_.extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE));
            if (par->extradata == nullptr) fail("audio extradata allocation failed");
            std::memcpy(par->extradata, audio_.extradata.data(), audio_.extradata.size());
            par->extradata_size = static_cast<int>(audio_.extradata.size());
            audio_stream_->time_base = kAudioTimeBase;
        }

        code = avio_open(&format_->pb, path, AVIO_FLAG_WRITE);
        if (code < 0) fail("output file open failed", code);
        code = avformat_write_header(format_, nullptr);
        if (code < 0) fail("write header failed", code);
        if (audio_stream_ != nullptr) audio_stream_ = format_->streams[audio_stream_->index];

        packet_ = av_packet_alloc();
        frame_ = av_frame_alloc();
        if (packet_ == nullptr || frame_ == nullptr) fail("packet or frame allocation failed");
        frame_->format = AV_PIX_FMT_RGBA64LE;
        frame_->width = width_;
        frame_->height = height_;
        code = av_frame_get_buffer(frame_, 0);
        if (code < 0) fail("frame buffer allocation failed", code);
    }

    void drain() {
        for (;;) {
            int code = avcodec_receive_packet(encoder_, packet_);
            if (code == AVERROR(EAGAIN) || code == AVERROR_EOF) break;
            if (code < 0) fail("ffv1 receive packet failed", code);
            av_packet_rescale_ts(packet_, encoder_->time_base, stream_->time_base);
            packet_->stream_index = stream_->index;
            code = av_interleaved_write_frame(format_, packet_);
            if (code < 0) fail("write packet failed", code);
        }
    }

    void release() noexcept {
        if (frame_) av_frame_free(&frame_);
        if (packet_) av_packet_free(&packet_);
        if (encoder_) avcodec_free_context(&encoder_);
        if (format_) {
            if (format_->pb) avio_closep(&format_->pb);
            avformat_free_context(format_);
            format_ = nullptr;
        }
    }

    int width_;
    int height_;
    AudioConfig audio_;
    AVFormatContext* format_ = nullptr;
    AVCodecContext* encoder_ = nullptr;
    AVStream* stream_ = nullptr;
    AVStream* audio_stream_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVPacket* packet_ = nullptr;
    std::int64_t next_pts_ = 0;
    bool finished_ = false;
};

void throw_java(JNIEnv* env, const char* message) {
    jclass cls = env->FindClass("java/lang/IllegalStateException");
    if (cls != nullptr) env->ThrowNew(cls, message);
}

Ffv1Writer* as_writer(jlong handle) { return reinterpret_cast<Ffv1Writer*>(handle); }

}  // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_owncut_app_Ffv1Writer_nativeOpen(JNIEnv* env, jclass, jstring path, jint width, jint height,
                                          jint fps_num, jint fps_den, jint sample_rate, jint channels,
                                          jbyteArray extradata) {
    const char* path_chars = env->GetStringUTFChars(path, nullptr);
    AudioConfig audio;
    audio.sample_rate = sample_rate;
    audio.channels = channels;
    if (extradata != nullptr) {
        const jsize length = env->GetArrayLength(extradata);
        audio.extradata.resize(static_cast<std::size_t>(length));
        if (length > 0) env->GetByteArrayRegion(extradata, 0, length, reinterpret_cast<jbyte*>(audio.extradata.data()));
    }
    std::unique_ptr<Ffv1Writer> writer;
    try {
        writer = std::make_unique<Ffv1Writer>(path_chars, width, height, fps_num, fps_den, audio);
    } catch (const std::exception& error) {
        env->ReleaseStringUTFChars(path, path_chars);
        throw_java(env, (std::string("16-bit FFV1 open failed: ") + error.what()).c_str());
        return 0;
    }
    env->ReleaseStringUTFChars(path, path_chars);
    return reinterpret_cast<jlong>(writer.release());
}

extern "C" JNIEXPORT void JNICALL
Java_com_owncut_app_Ffv1Writer_nativeWrite(JNIEnv* env, jclass, jlong handle, jobject pixels, jint count) {
    auto* writer = as_writer(handle);
    const float* data = static_cast<const float*>(env->GetDirectBufferAddress(pixels));
    if (writer == nullptr || data == nullptr || count < 0) {
        throw_java(env, "16-bit FFV1 write needs an open writer and a direct float buffer");
        return;
    }
    try {
        writer->write(data, static_cast<std::size_t>(count));
    } catch (const std::exception& error) {
        throw_java(env, (std::string("16-bit FFV1 write failed: ") + error.what()).c_str());
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_owncut_app_Ffv1Writer_nativeWriteAudio(JNIEnv* env, jclass, jlong handle, jobject packet, jint size,
                                                jlong pts_us) {
    auto* writer = as_writer(handle);
    const auto* data = static_cast<const std::uint8_t*>(env->GetDirectBufferAddress(packet));
    if (writer == nullptr || data == nullptr || size <= 0) {
        throw_java(env, "16-bit FFV1 audio needs an open writer and a direct packet buffer");
        return;
    }
    try {
        writer->write_audio(data, static_cast<std::size_t>(size), static_cast<std::int64_t>(pts_us));
    } catch (const std::exception& error) {
        throw_java(env, (std::string("16-bit FFV1 audio write failed: ") + error.what()).c_str());
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_owncut_app_Ffv1Writer_nativeClose(JNIEnv* env, jclass, jlong handle) {
    std::unique_ptr<Ffv1Writer> writer(as_writer(handle));
    if (!writer) return;
    try {
        writer->finish();
    } catch (const std::exception& error) {
        throw_java(env, (std::string("16-bit FFV1 finish failed: ") + error.what()).c_str());
    }
}

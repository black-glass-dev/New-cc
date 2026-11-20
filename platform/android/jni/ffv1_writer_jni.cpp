// 16-bit lossless FFV1 writer (MKV). FP32 RGBA frames come from Kotlin as a direct FloatBuffer,
// are converted to RGBA64LE by editor::color::output::to_rgba16 (clamped to [0,1]), and are encoded
// by FFmpeg's native ffv1 encoder. Built only in the FFmpeg block of CMakeLists.txt.
// Any failure throws java.lang.IllegalStateException with the FFmpeg reason. Nothing is silently skipped.

#include <jni.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "editor/color_fp32.hpp"
#include "editor/color_output.hpp"

namespace fp32 = editor::color::fp32;

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
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

class Ffv1Writer {
public:
    Ffv1Writer(const char* path, int width, int height, int fps_num, int fps_den)
        : width_(width), height_(height) {
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

    // rgba: width*height*4 floats, row-major, interleaved RGBA.
    void write(const float* rgba, std::size_t count) {
        const std::size_t pixels = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
        if (count != pixels * 4) fail("frame float count does not match width*height*4");
        std::vector<fp32::RgbaF32> pixel_values(pixels);
        for (std::size_t i = 0; i < pixels; ++i) {
            pixel_values[i] = {rgba[i * 4], rgba[i * 4 + 1], rgba[i * 4 + 2], rgba[i * 4 + 3]};
        }
        const std::vector<std::uint16_t> packed = editor::color::output::to_rgba16(pixel_values);

        int code = av_frame_make_writable(frame_);
        if (code < 0) fail("frame not writable", code);
        const std::size_t row_bytes = static_cast<std::size_t>(width_) * 8;
        for (int y = 0; y < height_; ++y) {
            std::uint8_t* dst = frame_->data[0] + static_cast<std::size_t>(y) * static_cast<std::size_t>(frame_->linesize[0]);
            const std::uint8_t* src = reinterpret_cast<const std::uint8_t*>(packed.data()) + static_cast<std::size_t>(y) * row_bytes;
            std::copy(src, src + row_bytes, dst);
        }
        frame_->pts = next_pts_++;
        code = avcodec_send_frame(encoder_, frame_);
        if (code < 0) fail("ffv1 send frame failed", code);
        drain();
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
    void open(const char* path, int fps_num, int fps_den) {
        if (width_ <= 0 || height_ <= 0 || fps_num <= 0 || fps_den <= 0) fail("invalid frame size or frame rate");
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

        code = avio_open(&format_->pb, path, AVIO_FLAG_WRITE);
        if (code < 0) fail("output file open failed", code);
        code = avformat_write_header(format_, nullptr);
        if (code < 0) fail("write header failed", code);

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
    AVFormatContext* format_ = nullptr;
    AVCodecContext* encoder_ = nullptr;
    AVStream* stream_ = nullptr;
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
                                          jint fps_num, jint fps_den) {
    const char* path_chars = env->GetStringUTFChars(path, nullptr);
    std::unique_ptr<Ffv1Writer> writer;
    try {
        writer = std::make_unique<Ffv1Writer>(path_chars, width, height, fps_num, fps_den);
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
Java_com_owncut_app_Ffv1Writer_nativeClose(JNIEnv* env, jclass, jlong handle) {
    std::unique_ptr<Ffv1Writer> writer(as_writer(handle));
    if (!writer) return;
    try {
        writer->finish();
    } catch (const std::exception& error) {
        throw_java(env, (std::string("16-bit FFV1 finish failed: ") + error.what()).c_str());
    }
}

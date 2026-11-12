// JNI bridge: stream-copy remux (no decode, no re-encode) of a finished export into MOV or Matroska.
// Requires the LGPL FFmpeg build from scripts/build_ffmpeg_android.sh (OWNCUT_FFMPEG_ROOT).
#include <jni.h>

#include <exception>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}

namespace {

std::string ff_error(int code) {
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(code, buffer, sizeof(buffer));
    return std::string(buffer);
}

struct InputDeleter {
    void operator()(AVFormatContext* context) const { avformat_close_input(&context); }
};

struct OutputDeleter {
    void operator()(AVFormatContext* context) const {
        if (context == nullptr) return;
        if (context->pb != nullptr && !(context->oformat->flags & AVFMT_NOFILE)) avio_closep(&context->pb);
        avformat_free_context(context);
    }
};

// Returns an empty string on success, otherwise a short English diagnostic.
std::string remux(const std::string& input_path, const std::string& output_path, const std::string& container) {
    const char* format_name = container == "mov" ? "mov" : (container == "mkv" ? "matroska" : nullptr);
    if (format_name == nullptr) return "unsupported container: " + container;

    AVFormatContext* raw_input = nullptr;
    int code = avformat_open_input(&raw_input, input_path.c_str(), nullptr, nullptr);
    if (code < 0) return "open input failed: " + ff_error(code);
    std::unique_ptr<AVFormatContext, InputDeleter> input(raw_input);

    code = avformat_find_stream_info(input.get(), nullptr);
    if (code < 0) return "read stream info failed: " + ff_error(code);

    AVFormatContext* raw_output = nullptr;
    code = avformat_alloc_output_context2(&raw_output, nullptr, format_name, output_path.c_str());
    if (code < 0 || raw_output == nullptr) return "output context failed: " + ff_error(code);
    std::unique_ptr<AVFormatContext, OutputDeleter> output(raw_output);

    // Map only video, audio and subtitle streams; everything else is dropped.
    std::vector<int> stream_map(input->nb_streams, -1);
    for (unsigned index = 0; index < input->nb_streams; ++index) {
        AVStream* in_stream = input->streams[index];
        const AVMediaType type = in_stream->codecpar->codec_type;
        if (type != AVMEDIA_TYPE_VIDEO && type != AVMEDIA_TYPE_AUDIO && type != AVMEDIA_TYPE_SUBTITLE) continue;
        AVStream* out_stream = avformat_new_stream(output.get(), nullptr);
        if (out_stream == nullptr) return "new output stream failed";
        code = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
        if (code < 0) return "copy stream parameters failed: " + ff_error(code);
        out_stream->codecpar->codec_tag = 0;
        out_stream->time_base = in_stream->time_base;
        stream_map[index] = static_cast<int>(output->nb_streams) - 1;
    }
    if (output->nb_streams == 0) return "no remuxable streams in input";

    if (!(output->oformat->flags & AVFMT_NOFILE)) {
        code = avio_open(&output->pb, output_path.c_str(), AVIO_FLAG_WRITE);
        if (code < 0) return "open output failed: " + ff_error(code);
    }

    code = avformat_write_header(output.get(), nullptr);
    if (code < 0) return "write header failed: " + ff_error(code);

    AVPacket* packet = av_packet_alloc();
    if (packet == nullptr) return "packet allocation failed";
    std::string error;
    while (true) {
        code = av_read_frame(input.get(), packet);
        if (code == AVERROR_EOF) break;
        if (code < 0) {
            error = "read frame failed: " + ff_error(code);
            break;
        }
        const int mapped = packet->stream_index >= 0 && static_cast<size_t>(packet->stream_index) < stream_map.size()
            ? stream_map[packet->stream_index] : -1;
        if (mapped < 0) {
            av_packet_unref(packet);
            continue;
        }
        AVStream* in_stream = input->streams[packet->stream_index];
        AVStream* out_stream = output->streams[mapped];
        av_packet_rescale_ts(packet, in_stream->time_base, out_stream->time_base);
        packet->stream_index = mapped;
        code = av_interleaved_write_frame(output.get(), packet);
        av_packet_unref(packet);
        if (code < 0) {
            error = "write frame failed: " + ff_error(code);
            break;
        }
    }
    av_packet_free(&packet);
    if (!error.empty()) return error;

    code = av_write_trailer(output.get());
    if (code < 0) return "write trailer failed: " + ff_error(code);
    return std::string();
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_owncut_app_FfmpegRemux_nativeRemux(JNIEnv* env, jobject, jstring input, jstring output, jstring container) {
    const char* input_chars = env->GetStringUTFChars(input, nullptr);
    const char* output_chars = env->GetStringUTFChars(output, nullptr);
    const char* container_chars = env->GetStringUTFChars(container, nullptr);
    std::string error;
    try {
        error = remux(input_chars, output_chars, container_chars);
    } catch (const std::exception& exception) {
        error = std::string("remux exception: ") + exception.what();
    }
    env->ReleaseStringUTFChars(input, input_chars);
    env->ReleaseStringUTFChars(output, output_chars);
    env->ReleaseStringUTFChars(container, container_chars);
    if (error.empty()) return nullptr;
    return env->NewStringUTF(error.c_str());
}

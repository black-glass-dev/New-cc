// JNI bridge: remux a finished MP4 export into MOV, Matroska or MXF.
//   mov / mkv: stream copy of every audio, video and subtitle stream. No decode, no re-encode.
//   mxf:       H.264 video is stream copied (Annex B via the muxer's own bitstream filter).
//              AAC audio is decoded and written as 16-bit little-endian PCM, the only audio
//              essence the MXF muxer accepts. Other audio or video codecs fail closed.
// Requires the LGPL FFmpeg build from scripts/build_ffmpeg_android.sh (OWNCUT_FFMPEG_ROOT).
#include <jni.h>

#include <cmath>
#include <cstdint>
#include <exception>
#include <map>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
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

// MXF in this build carries 48 kHz PCM only (the muxer rejects other rates).
constexpr int kMxfSampleRate = 48000;

// One AAC input stream decoded and resampled to 48 kHz 16-bit PCM for the MXF output.
struct PcmTranscode {
    AVCodecContext* decoder = nullptr;
    SwrContext* swr = nullptr;
    AVFrame* frame = nullptr;
    int input_index = -1;
    int output_index = -1;
    int channels = 0;
    AVRational input_tb{1, 1};
    bool timeline_started = false;
    int64_t next_sample_pts = 0;  // in 1/kMxfSampleRate units

    ~PcmTranscode() {
        swr_free(&swr);
        avcodec_free_context(&decoder);
        av_frame_free(&frame);
    }
};

// Converts one decoded frame (or a null frame to drain the resampler) and writes the PCM packet.
std::string convert_and_write(AVFormatContext* output, PcmTranscode& audio, AVFrame* frame) {
    const int in_samples = frame != nullptr ? frame->nb_samples : 0;
    if (frame != nullptr) {
        if (frame->ch_layout.nb_channels != audio.channels) return "audio channel count changed mid-stream";
        if (!audio.timeline_started && frame->best_effort_timestamp != AV_NOPTS_VALUE) {
            audio.next_sample_pts = av_rescale_q(frame->best_effort_timestamp, audio.input_tb,
                                                 AVRational{1, kMxfSampleRate});
            audio.timeline_started = true;
        }
    }
    // swr_get_delay(.., base) is already in output-rate units; only the new input needs rescaling.
    const int64_t pending = swr_get_delay(audio.swr, kMxfSampleRate);
    const int64_t incoming = av_rescale_rnd(in_samples, kMxfSampleRate, audio.decoder->sample_rate, AV_ROUND_UP);
    const int max_out = static_cast<int>(pending + incoming) + 16;
    AVPacket* packet = av_packet_alloc();
    if (packet == nullptr) return "packet allocation failed";
    int code = av_new_packet(packet, max_out * audio.channels * 2);
    if (code < 0) {
        av_packet_free(&packet);
        return "packet buffer failed: " + ff_error(code);
    }
    uint8_t* out_planes[1] = {packet->data};
    const uint8_t* const* in_planes = frame != nullptr ? (const uint8_t* const*)frame->extended_data : nullptr;
    const int got = swr_convert(audio.swr, out_planes, max_out, in_planes, in_samples);
    if (got < 0) {
        av_packet_free(&packet);
        return "resample failed: " + ff_error(got);
    }
    if (got == 0) {
        av_packet_free(&packet);
        return std::string();
    }
    av_shrink_packet(packet, got * audio.channels * 2);

    AVStream* stream = output->streams[audio.output_index];
    const AVRational sample_tb{1, kMxfSampleRate};
    packet->stream_index = audio.output_index;
    packet->pts = av_rescale_q(audio.next_sample_pts, sample_tb, stream->time_base);
    packet->dts = packet->pts;
    packet->duration = av_rescale_q(got, sample_tb, stream->time_base);
    packet->flags |= AV_PKT_FLAG_KEY;
    audio.next_sample_pts += got;

    code = av_interleaved_write_frame(output, packet);  // takes ownership of the packet buffer
    av_packet_free(&packet);
    if (code < 0) return "write audio failed: " + ff_error(code);
    return std::string();
}

std::string drain_decoder(AVFormatContext* output, PcmTranscode& audio) {
    while (true) {
        const int code = avcodec_receive_frame(audio.decoder, audio.frame);
        if (code == AVERROR(EAGAIN) || code == AVERROR_EOF) return std::string();
        if (code < 0) return "audio decode failed: " + ff_error(code);
        const std::string error = convert_and_write(output, audio, audio.frame);
        av_frame_unref(audio.frame);
        if (!error.empty()) return error;
    }
}

// Opens an AAC decoder and a resampler for one input stream and reserves the PCM output stream.
std::string open_pcm_transcode(AVFormatContext* input, AVFormatContext* output, int input_index,
                               PcmTranscode& audio) {
    AVStream* in_stream = input->streams[input_index];
    if (in_stream->codecpar->codec_id != AV_CODEC_ID_AAC) {
        return std::string("MXF audio must be AAC in this build, found: ") +
               avcodec_get_name(in_stream->codecpar->codec_id);
    }
    const AVCodec* decoder_codec = avcodec_find_decoder(AV_CODEC_ID_AAC);
    if (decoder_codec == nullptr) return "AAC decoder missing from this FFmpeg build";
    audio.decoder = avcodec_alloc_context3(decoder_codec);
    if (audio.decoder == nullptr) return "audio decoder allocation failed";
    int code = avcodec_parameters_to_context(audio.decoder, in_stream->codecpar);
    if (code < 0) return "audio decoder parameters failed: " + ff_error(code);
    audio.decoder->pkt_timebase = in_stream->time_base;
    code = avcodec_open2(audio.decoder, decoder_codec, nullptr);
    if (code < 0) return "audio decoder open failed: " + ff_error(code);
    audio.frame = av_frame_alloc();
    if (audio.frame == nullptr) return "audio frame allocation failed";

    audio.input_index = input_index;
    audio.input_tb = in_stream->time_base;
    audio.channels = in_stream->codecpar->ch_layout.nb_channels;
    const int input_rate = in_stream->codecpar->sample_rate;
    if (audio.channels <= 0 || input_rate <= 0) return "audio channel or sample rate missing";

    code = swr_alloc_set_opts2(&audio.swr, &audio.decoder->ch_layout, AV_SAMPLE_FMT_S16, kMxfSampleRate,
                               &audio.decoder->ch_layout, AV_SAMPLE_FMT_FLTP, input_rate, 0, nullptr);
    if (code < 0 || audio.swr == nullptr) return "resampler allocation failed: " + ff_error(code);
    code = swr_init(audio.swr);
    if (code < 0) return "resampler init failed: " + ff_error(code);

    AVStream* out_stream = avformat_new_stream(output, nullptr);
    if (out_stream == nullptr) return "new audio output stream failed";
    code = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
    if (code < 0) return "copy audio parameters failed: " + ff_error(code);
    AVCodecParameters* par = out_stream->codecpar;
    par->codec_id = AV_CODEC_ID_PCM_S16LE;
    par->codec_tag = 0;
    par->format = AV_SAMPLE_FMT_S16;
    par->sample_rate = kMxfSampleRate;
    par->bits_per_coded_sample = 16;
    par->bits_per_raw_sample = 16;
    par->block_align = audio.channels * 2;
    par->bit_rate = static_cast<int64_t>(kMxfSampleRate) * audio.channels * 16;
    par->frame_size = 0;
    par->profile = AV_PROFILE_UNKNOWN;
    av_freep(&par->extradata);
    par->extradata_size = 0;
    out_stream->time_base = AVRational{1, kMxfSampleRate};
    audio.output_index = static_cast<int>(output->nb_streams) - 1;
    return std::string();
}

// Returns an empty string on success, otherwise a short English diagnostic.
std::string remux(const std::string& input_path, const std::string& output_path, const std::string& container) {
    const bool is_mxf = container == "mxf";
    const char* format_name = nullptr;
    if (container == "mov") format_name = "mov";
    else if (container == "mkv") format_name = "matroska";
    else if (is_mxf) format_name = "mxf";
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

    std::vector<int> stream_map(input->nb_streams, -1);
    std::map<int, std::unique_ptr<PcmTranscode>> pcm_by_input;
    for (unsigned index = 0; index < input->nb_streams; ++index) {
        AVStream* in_stream = input->streams[index];
        const AVMediaType type = in_stream->codecpar->codec_type;
        if (is_mxf) {
            if (type == AVMEDIA_TYPE_VIDEO) {
                if (in_stream->codecpar->codec_id != AV_CODEC_ID_H264) {
                    return std::string("MXF video must be H.264 in this build, found: ") +
                           avcodec_get_name(in_stream->codecpar->codec_id);
                }
                // With B-frames, the FFmpeg 8.1 MXF muxer and demuxer lose the final frame, so the
                // export is refused instead of silently shortened.
                if (in_stream->codecpar->video_delay > 0) {
                    return "MXF cannot carry B-frame H.264 in this build (the last frame would be lost)";
                }
            } else if (type == AVMEDIA_TYPE_AUDIO) {
                auto transcode = std::make_unique<PcmTranscode>();
                const std::string error = open_pcm_transcode(input.get(), output.get(),
                                                            static_cast<int>(index), *transcode);
                if (!error.empty()) return error;
                pcm_by_input[static_cast<int>(index)] = std::move(transcode);
                continue;
            } else {
                continue;  // data and subtitle streams are not carried into MXF here
            }
        } else if (type != AVMEDIA_TYPE_VIDEO && type != AVMEDIA_TYPE_AUDIO && type != AVMEDIA_TYPE_SUBTITLE) {
            continue;
        }
        AVStream* out_stream = avformat_new_stream(output.get(), nullptr);
        if (out_stream == nullptr) return "new output stream failed";
        code = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
        if (code < 0) return "copy stream parameters failed: " + ff_error(code);
        out_stream->codecpar->codec_tag = 0;
        if (is_mxf && type == AVMEDIA_TYPE_VIDEO && out_stream->codecpar->format == AV_PIX_FMT_NONE) {
            // The MP4 demuxer leaves the pixel format unset, but the MXF muxer requires it. The export
            // encoder produces 8-bit 4:2:0 H.264, so that is the format declared here.
            out_stream->codecpar->format = AV_PIX_FMT_YUV420P;
        }
        out_stream->time_base = in_stream->time_base;
        if (type == AVMEDIA_TYPE_VIDEO) {
            out_stream->avg_frame_rate = in_stream->avg_frame_rate;
            out_stream->r_frame_rate = in_stream->r_frame_rate;
        }
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
    while (error.empty()) {
        code = av_read_frame(input.get(), packet);
        if (code == AVERROR_EOF) break;
        if (code < 0) {
            error = "read frame failed: " + ff_error(code);
            break;
        }
        const int source = packet->stream_index;
        auto pcm = pcm_by_input.find(source);
        if (pcm != pcm_by_input.end()) {
            code = avcodec_send_packet(pcm->second->decoder, packet);
            if (code < 0) {
                error = "audio decode failed: " + ff_error(code);
            } else {
                error = drain_decoder(output.get(), *pcm->second);
            }
            av_packet_unref(packet);
            continue;
        }
        const int mapped = source >= 0 && static_cast<size_t>(source) < stream_map.size()
            ? stream_map[source] : -1;
        if (mapped < 0) {
            av_packet_unref(packet);
            continue;
        }
        AVStream* in_stream = input->streams[source];
        AVStream* out_stream = output->streams[mapped];
        av_packet_rescale_ts(packet, in_stream->time_base, out_stream->time_base);
        packet->stream_index = mapped;
        code = av_interleaved_write_frame(output.get(), packet);
        av_packet_unref(packet);
        if (code < 0) error = "write frame failed: " + ff_error(code);
    }
    // Flush the audio decoders so the last frames are not lost.
    for (auto& entry : pcm_by_input) {
        if (!error.empty()) break;
        if (avcodec_send_packet(entry.second->decoder, nullptr) < 0) {
            error = "audio flush failed";
            break;
        }
        error = drain_decoder(output.get(), *entry.second);
        // Drain whatever the resampler still holds.
        for (int guard = 0; error.empty() && guard < 64 && swr_get_delay(entry.second->swr, kMxfSampleRate) > 0; ++guard) {
            error = convert_and_write(output.get(), *entry.second, nullptr);
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

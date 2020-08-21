//
// Created by PingZi on 2020/8/21.
//

#include <corecrt_wstdio.h>
#include <stdio.h>
#include <stdarg.h>

#define out &

extern "C" {
#include "libavformat/avformat.h"
}

static void logging(FILE *file, const char *prefix, const char *suffix, const char *format, va_list args) {

    if (prefix != nullptr) {
        fprintf(file, "%s", prefix);
    }

    vfprintf(file, format, args);

    if (suffix != nullptr) {
        fprintf(file, "%s", suffix);
    }

    fprintf(file, "\n");
}

static void info(const char *format, ...) {
    va_list args;
            va_start(args, format);
    logging(stdout, "INFO: ", nullptr, format, args);
            va_end(args);
}

static void error(const char *format, ...) {
    va_list args;
            va_start(args, format);
    logging(stderr, "ERROR: ", nullptr, format, args);
            va_end(args);
}

/**
 * 打印输出视音频文件的头信息
 */
static void log_format(AVFormatContext *format_context) {
    info("+-Format Context-+");
    if (format_context == nullptr) {
        info("NULL");
        goto end;
    }

    info("format name: %s", format_context->iformat->long_name);
    info("duration: %" PRId64, format_context->duration);
    info("bit rate: %" PRId64, format_context->bit_rate);

    end:
    info("+-END-+");
}

static void log_stream(AVStream *stream) {
    info("+-Stream-+");
    if (stream == nullptr) {
        info("NULL");
        goto end;
    }

    info("index: %d", stream->index);
    info("start time: %" PRId64, stream->start_time);
    info("duration: %" PRId64, stream->duration);
    info("time base: %d/%d", stream->time_base.num, stream->time_base.den);
    info("frame rate: %d/%d", stream->r_frame_rate.num, stream->r_frame_rate.den);

    end:
    info("+-END-+");
}

static void log_codec_parameters(AVCodecParameters *parameters) {
    info("+-Codec Parameters-+");
    if (parameters == nullptr) {
        info("NULL");
        goto end;
    }

    switch (parameters->codec_type) {

        case AVMEDIA_TYPE_VIDEO:
            info("TYPE: VIDEO");
            info("Video Width: %d", parameters->width);
            info("Video Height: %d", parameters->height);
            break;

        case AVMEDIA_TYPE_AUDIO:
            info("TYPE: AUDIO");
            info("sample rate: %d", parameters->sample_rate);
            info("channels: %d", parameters->channels); // TODO 忘了这个 channels
            break;

        default:
            info("Ignore type other than video and audio");
            break;
    }

    end:
    info("+-END-+");
}

int write_picture_head(FILE *file, AVFrame *frame) {
    fprintf(file, "P5\n %d\n%d\n", frame->width, frame->height, 255);
    return 0;
}

int write_gray_picture(const char *filename, AVFrame *frame) {
    FILE *file = fopen(filename, "wb+");

    write_picture_head(file, frame);

    int line_number = frame->height;
    uint8_t* picture_data = frame->data[0];
    int line_size = frame->linesize[0];

    for (int line = 0; line < line_number; line++) {
        fwrite(picture_data + line * line_size, line_size, 1, file);
    }

    fclose(file);
    return 0;
}

int decode_packet(AVPacket *packet, AVFrame *frame, AVCodecContext *decoder) {
    int response = avcodec_send_packet(decoder, packet);
    if (response == AVERROR(EAGAIN)) {
        info("input is not accepted in the current state.\n "
             "once all output is read, the packet should be resent, "
             "and the call will not fail with EAGAIN");
        response = 0; // 这种情况不是错误, 不需要理会
    }

    if (response < 0) {
        error("Cannot send packet to decoder: %d", response);
        return -1;
    }

    while (response >= 0) {
        response = avcodec_receive_frame(decoder, frame);
        if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
            break;
        }

        info("received frame, writing to picture");
        char filename[1024];
        snprintf(filename, sizeof(filename),
                 "Frame-%d", decoder->frame_number); // 忘记了是 decoder 里面的 frame number

        write_gray_picture(filename, frame);
    }

    return 0;
}

int main0820(int argc, char *argv[]) {
    if (argc < 2) {
        error("should input filename");
        return -1;
    }

    const char *filename = argv[1];
    AVFormatContext *format_context = nullptr;

    int response = avformat_open_input(out format_context, filename, nullptr, nullptr);
    if (response < 0) {
        error("Failed to open file: %s. avformat_open_input returned: %d", filename, response);
        return -1;
    }

    info("file open success!");
    log_format(format_context);

    response = avformat_find_stream_info(format_context, nullptr);
    if (response < 0) {
        error("Failed to find stream info: %d", response);
        goto end;
    }

    int video_stream_index = -1;
    AVCodecParameters *video_codec_parameters = nullptr;
    AVCodec *video_codec = nullptr;

    for (int i = 0; i < format_context->nb_streams; i++) {
        AVStream *stream = format_context->streams[i];
        AVCodecParameters *parameters = stream->codecpar;

        log_stream(stream);
        log_codec_parameters(parameters);

        if (parameters->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (video_stream_index == -1) {

                AVCodec *codec = avcodec_find_decoder(parameters->codec_id);
                if (codec == nullptr) {
                    error("Failed to find decoder for codec_id: %d", parameters->codec_id);
                    goto end;
                }

                video_stream_index = stream->index;
                video_codec_parameters = parameters;
                video_codec = codec;
            }
        }
    }

    // open codec, 这次没有忘
    AVCodecContext *video_decoder = avcodec_alloc_context3(video_codec);
    if (video_decoder == nullptr) {
        error("Cannot alloc memory for av codec context");
        goto end;
    }

    avcodec_parameters_to_context(video_decoder, video_codec_parameters);
    response = avcodec_open2(video_decoder, video_codec, nullptr);
    if (response < 0) {
        error("Cannot open decoder for video.");
        goto end;
    }

    AVFrame *frame = av_frame_alloc();
    AVPacket *packet = av_packet_alloc();
    if (frame == nullptr || packet == nullptr) {
        error("Frame or packet is null");
        goto end;
    }

    while (av_read_frame(format_context, packet) >= 0) {
        if (packet->stream_index != video_stream_index) {
            av_packet_unref(packet);
            continue;
        }

        // video stream
        response = decode_packet(packet, frame, video_decoder);
        av_packet_unref(packet);
    }

    end:
    avformat_close_input(out format_context);
    format_context = nullptr;
    if (video_decoder != nullptr) {
        avcodec_free_context(out video_decoder);
        video_decoder = nullptr;
    }

    return 0;
}
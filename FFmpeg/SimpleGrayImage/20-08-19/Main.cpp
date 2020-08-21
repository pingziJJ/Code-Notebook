//
// Created by PingZi on 2020/8/19.
// 练习, 尝试默写一遍.
//

#define out &

extern "C" {
#include "libavformat/avformat.h"
}

static void info(const char *fmt, ...);

static void error(const char *fmt, ...);

static void logging(FILE *file, const char *fmt, va_list args);

static int decode_video(AVCodecContext *decoder, AVPacket *packet);

static void write_gray_image(int width, int height, uint8_t *data, int line_size, const char *filename);

static void write_pgm_header(int width, int height, FILE *file);
int main(int argc, char *argv[]) {

    if (argc != 2) {
        error("You should specify a file.");
        return -1;
    }

    const char *input_file = argv[1];

    AVFormatContext *format_context = nullptr;
    int response = avformat_open_input(out format_context, input_file, nullptr, nullptr);
    if (response < 0) {
        error("Failed to open file(%s)", input_file);
        return -1;
    }

    // open input 之后可以打印一些文件的头信息出来了
    info("format: %s", format_context->iformat->long_name);
    info("duration: %" PRId64, format_context->duration);
    info("bit_rate: %" PRId64, format_context->bit_rate);

    // 打开视音频的流
    response = avformat_find_stream_info(format_context, nullptr);
    if (response < 0) {
        error("cannot find stream info for file");
        return -1;
    }


    // video stream info
    int video_stream_index = -1;
    AVStream *video_stream; // TODO 这里背错了, 没有 video streams
    AVCodecParameters *video_parameters;
    AVCodec *video_codec;

    for (int i = 0; i < format_context->nb_streams; i++) {
        AVStream *current_stream = format_context->streams[i];
        AVCodecParameters *parameters = current_stream->codecpar;

        // 输出流信息
        info("Information of stream: %d", i);
        info("timebase: %d/%d", current_stream->time_base.num, current_stream->time_base.den);
        info("frame rate: %d/%d", current_stream->r_frame_rate.num, current_stream->r_frame_rate.den);
        info("start time: %d", current_stream->start_time);
        info("duration: %d", current_stream->duration);

        // 输出流的 codec parameters 信息
        info("Information of stream %d codec", i);
        if (parameters->codec_type == AVMEDIA_TYPE_VIDEO) {
            // video codec
            info("stream is video");
            info("width: %d, height: %d", parameters->width, parameters->height);

            if (video_stream_index == -1) {

                AVCodec *decoder = avcodec_find_decoder(parameters->codec_id);
                if (decoder == nullptr) {
                    error("Cannot find decoder for: %s", parameters->codec_id);
                    return -1;
                }

                video_stream_index = i;
                video_stream = current_stream;
                video_parameters = parameters;
                video_codec = decoder;
            }
            continue;
        }

        if (parameters->codec_type == AVMEDIA_TYPE_AUDIO) {
            // audio codec
            info("stream is audio");
            info("sample rate: %d/%d", parameters->sample_rate);
            info("channels: ", parameters->channels);
            continue;
        }
    }

    // TODO 这里忘了 open codec!!!
    AVCodecContext *decoder_context = avcodec_alloc_context3(video_codec);
    if (decoder_context == nullptr) {
        error("Cannot alloc context for codec: %s", video_codec->name);
        return -1;
    }

    // TODO 这里忘了 copy parameters to context
    response = avcodec_parameters_to_context(decoder_context, video_parameters);
    if (response < 0) {
        error("Failed to copy parameters to context");
        return -1;
    }

    response = avcodec_open2(decoder_context, video_codec, nullptr);
    if (response < 0) {
        error("Cannot open decoder for: %s", video_codec->name);
        return -1;
    }

    // 这个时候已经有了 video stream 解码 video stream 并存储到文件中
    AVPacket *packet = av_packet_alloc();
    if (packet == nullptr) {
        error("Filed to alloc memory for packet");
        return -1;
    }

    while (av_read_frame(format_context, packet) > 0) {
        if (packet->stream_index == video_stream_index) {
            response = decode_video();
            if (response < 0) {
                av_packet_unref(packet);
                error("Cannot decode video");
                return -1;
            }
        }

        av_packet_unref(packet);
    }

    // release
    avformat_close_input(&format_context);

    avcodec_close(decoder_context);
    avcodec_free_context(&decoder_context);
    av_packet_free(&packet);
}

static int decode_video(AVCodecContext *decoder, AVPacket *packet) {
    int response = avcodec_send_packet(decoder, packet);
    if (response < 0 && response != AVERROR(EAGAIN)) {
        error("error while send packet");
        return response;
    }

    if (response == AVERROR(EAGAIN)) {
        response = 0;
    }

    AVFrame *frame = av_frame_alloc();
    if (frame == nullptr) {
        error("failed to alloc memory for frame");
        return -1;
    }

    while (response > 0) {
        response = avcodec_receive_frame(decoder, frame);
        if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
            // 文件正常被读写的两个错误码
            break;
        }

        if (response < 0) {
            error("Failed to receive frame: " + response);
            return response;
        }

        info("Success receive uncompressed frame");
        info("Write to pgm file...");

        char filename[1024];
        snprintf(filename, sizeof(filename), "Frame-%d.pgm", decoder->frame_number);

        response = write_gray_image(frame->width, frame->height, frame->data[0], frame->linesize[0], filename);

        if (response < 0) {
            error("Cannot write file.");
            return response;
        }
    }

    av_frame_free(&frame);
}

static void write_gray_image(int width, int height, uint8_t *data, int line_size, const char *filename) {
    FILE *file = fopen(filename, "wb+");
    write_gray_image(width, height, file);

    for (int i = 0; i < height; i++) {
        fwrite(data + i * line_size, line_size, 1, file);
    }

    // TODO 忘了 fclose TAT
    fclose(file);
}

static void write_pgm_header(int width, int height, FILE *file){
    fprintf(file, "P5\n%d %d\n%d\n", width, height, 255);
}

static void info(const char *fmt, ...) {
    FILE* out = stdout;
    va_arg args;
            va_start(args, fmt);
    logging(out, fmt, args);
            va_end(args);
}

static void error(const char *fmt, ...) {
    FILE* out = stderr;
    va_arg args;
            va_start(args, fmt);
    logging(out, fmt, args);
            va_end(args);
}

static void logging(FILE *file, const char *fmt, va_arg args) {
    vfprintf(file, fmt, args);
    fprintf(file, "\n");
}
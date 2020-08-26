//
// Created by PingZi on 2020/8/26.
//

#include "GrayImage0826.h"
#include "Logger.h"

extern "C" {
#include "libavformat/avformat.h"
}

// 找到第一个 type 是 codec_type 的 stream index
int stream_index_first(AVMediaType codec_type, AVStream *streams[], uint32_t number_of_streams) {
    for (int i = 0; i < number_of_streams; i++) {
        AVMediaType current_codec_type = streams[i]->codecpar->codec_type;
        if (current_codec_type == codec_type) {
            return streams[i]->index;
        }
    }

    return -1;
}

int stream_index_first(AVMediaType codec_type, AVFormatContext *format_context) {
    return stream_index_first(codec_type, format_context->streams, format_context->nb_streams);
}

void save_image_header(FILE *file, int image_width, int image_height) {
    // TODO 这里少记了 \n
    fprintf(file, "P5\n%d %d\n%d\n", image_width, image_height, 255);
}

void save_gray_image(FILE *file, AVFrame *frame) {
    int image_width = frame->width;
    int image_height = frame->height;
    save_image_header(file, image_width, image_height);

    for (int line = 0; line < frame->height; line++) {
        uint8_t *gray_image_data = frame->data[0];
        int linesize = frame->linesize[0];

        fwrite(gray_image_data + linesize * line, linesize, 1, file);
    }
}

void save_gray_image(const char *filename, AVFrame *frame) {
    FILE *file = fopen(filename, "wb+");
    save_gray_image(file, frame);
    fclose(file);
}

int decode_packet(AVCodecContext *decoder, AVPacket *packet, AVFrame *frame) {
    int response = avcodec_send_packet(decoder, packet);
    if (response < 0 && response != AVERROR(EAGAIN) && response != AVERROR_EOF) {
        error("cannot send packet to decoder.");
        return response;
    }

    if (response == AVERROR_EOF) {
        info("send: EOF");
        return response;
    }

    if (response == AVERROR(EAGAIN)) {
        info("full buffer size, packet will auto send when handler receive frame from buffer.");
        response = 0;
    }

    info("packet succeed send to decoder.");

    while (response >= 0) {
        response = avcodec_receive_frame(decoder, frame);
        if (response == AVERROR_EOF) {
            info("receive: EOF");
            break;
        }

        if (response == AVERROR(EAGAIN)) {
            info("empty buffer size.");
            continue;
        }

        info("--------------------");
        info("Frame: %d", decoder->frame_number);
        info("type: %c, size: %d bytes", av_get_picture_type_char(frame->pict_type), frame->pkt_size);
        info("pts: %" PRId64 ", dts: %" PRId64, frame->pts, frame->pkt_dts);
        info("key frame: %d", frame->key_frame);
        info("--------------------");

        // 正常接收到了图片
        char filename[1024];
        // TODO 这个 frame number 又记错了, 是 decoder 上面的 frame_number
        sprintf(filename, "frame-%d.pgm", decoder->frame_number);
        save_gray_image(filename, frame);
    }

    return 0;
}

int run0826(int argc, char **argv) {

    if (argc < 2) {
        error("filename request.");
        return -1;
    }

    int ret = 0;

    const char *filename = argv[1];
    int packet_count = 4; // 输出的灰度图的数量


    info("opening input file: %s", filename);

    AVFormatContext *format_context = nullptr;
    int response = avformat_open_input(&format_context, filename, nullptr, nullptr);
    if (response < 0) {
        ret = response;
        error("cannot open input file: %s.", filename);
        goto end;
    }

    info("success open input. finding stream info...");

    response = avformat_find_stream_info(format_context, nullptr);
    if (response < 0) {
        ret = response;
        error("cannot find stream info for input file.");
        goto end;
    }

    // find video stream index
    int video_stream_index = stream_index_first(AVMEDIA_TYPE_VIDEO, format_context);
    if (video_stream_index < 0) {
        ret = -1;
        error("Cannot find video stream for input file.");
        goto end;
    }

    info("stream initialized, video stream index: %d.", video_stream_index);
    info("open video decoder...");

    // open codec
    AVStream *video_stream = format_context->streams[video_stream_index];
    AVCodecParameters *video_codec_par = video_stream->codecpar;
    AVCodec *video_decoder = avcodec_find_decoder(video_codec_par->codec_id);
    if (video_decoder == nullptr) {
        ret = -1;
        error("cannot find decoder for video stream [index:%d].", video_stream_index);
        goto end;
    }
    AVCodecContext *video_decode_context = avcodec_alloc_context3(video_decoder);
    if (video_decode_context == nullptr) {
        ret = AVERROR(ENOMEM);
        error("cannot alloc memory for decode context.");
        goto end;
    }
    response = avcodec_parameters_to_context(video_decode_context, video_codec_par);
    if (response < 0) {
        error("error while copy parameters to context.");
        ret = -1;
        goto end;
    }
    response = avcodec_open2(video_decode_context, video_decoder, nullptr);
    if (response < 0) {
        error("cannot open decoder.");
        ret = response;
        goto end;
    }

    info("codec is succeed open!");

    // read frame
    AVFrame *frame = av_frame_alloc();
    AVPacket *packet = av_packet_alloc();
    if (frame == nullptr || packet == nullptr) {
        ret = AVERROR(ENOMEM);
        error("cannot alloc memory for frame or packet.");
        goto end;
    }

    // TODO 这里的结果应该是 >= 0 写成了 > 0 没有出图片
    while (av_read_frame(format_context, packet) >= 0) {
        if (packet->stream_index != video_stream_index) {
            av_packet_unref(packet);
            continue;
        }

        // video packet
        response = decode_packet(video_decode_context, packet, frame);
        av_packet_unref(packet);
        if (response < 0) {
            error("failed to decode packet.");
            ret = response;
            goto end;
        }

        packet_count--;
        if (packet_count <= 0) {
            break;
        }
    }

    end:
    if (format_context != nullptr) {
        avformat_close_input(&format_context);
        format_context = nullptr;
    }

    if (packet != nullptr) {
        av_packet_free(&packet);
        packet = nullptr;
    }

    if (frame != nullptr) {
        av_frame_free(&frame);
        frame = nullptr;
    }

    return ret;
}

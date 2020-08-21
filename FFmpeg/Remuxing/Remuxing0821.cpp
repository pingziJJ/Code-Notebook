//
// Created by PingZi on 2020/8/21.
//

#define out &

#include "Remuxing0821.h"
#include "logger.h"

extern "C"{
#include "libavformat/avformat.h"
}

int run(int argc, char **argv) {

    if (argc < 3) {
        error("You must pass at least two parameters.");
        return -1;
    }

    const char *input = argv[1];
    const char *output = argv[2];

    info("Remuxing %s to %s.", input, output);

    int ret = 0;
    AVFormatContext *input_format = nullptr;
    AVFormatContext *output_format = nullptr;

    // 初始化 input format
    int response = avformat_open_input(out input_format, input, nullptr, nullptr);
    if (response < 0 || input_format == nullptr) {
        ret = AVERROR_UNKNOWN;
        error("Cannot open input file: %s.", input);
        goto end;
    }

    response = avformat_find_stream_info(input_format, nullptr);
    if (response < 0) {
        ret = AVERROR_UNKNOWN;
        error("Cannot find stream info for input file.");
        goto end;
    }

    // 初始化 output format
    response = avformat_alloc_output_context2(out output_format, nullptr, nullptr, output);
    if (response < 0 || output_format == nullptr) {
        error("Failed to alloc memory for output format.");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    int *output_stream_list = static_cast<int *>(av_mallocz_array(input_format->nb_streams, sizeof(*output_stream_list)));
    if (output_stream_list == nullptr) {
        ret = AVERROR(ENOMEM);
        error("Failed to alloc memory for stream list");
        goto end;
    }

    int output_stream_index = 0;

    for (int i = 0; i < input_format->nb_streams; i++) {
        AVStream *stream = input_format->streams[i];
        AVCodecParameters *parameters = stream->codecpar;

        if (parameters->codec_type != AVMEDIA_TYPE_AUDIO &&
            parameters->codec_type != AVMEDIA_TYPE_VIDEO &&
            parameters->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            info("Ignore type not in (AUDIO, VIDEO, SUBTITLE).");
            output_stream_list[i] = -1;
            continue;
        }

        AVStream *out_stream = avformat_new_stream(output_format, nullptr);
        if (out_stream == nullptr) {
            error("Cannot alloc memory for output stream.");
            ret = AVERROR(ENOMEM);
            goto end;
        }

        output_stream_list[i] = output_stream_index++;
        response = avcodec_parameters_copy(out_stream->codecpar, parameters);
        if (response < 0) {
            error("Cannot copy src stream's codecpar to dest");
            return response;
        }
    }

    info("output stream init success");
    av_dump_format(output_format, 0, output, 1);

    int no_file = output_format->oformat->flags & AVFMT_NOFILE;
    if (!no_file) {
        info("Has File...");
        response = avio_open(out output_format->pb, output, AVIO_FLAG_WRITE);
        if (response < 0) {
            error("Failed to open output file.");
            ret = response;
            goto end;
        }
    } else {
        info("No File.");
    }

    AVDictionary *options = nullptr;

    // fragment mp4 options
    av_dict_set(out options, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);

    response = avformat_write_header(output_format, &options);
    if (response < 0) {
        error("Failed to write header for output.");
        ret = response;
        goto end;
    }

    AVPacket packet; // 为什么不使用之前的 av_packet_alloc了
    while (true) {
        AVStream *src = nullptr;
        AVStream *dest = nullptr;

        response = av_read_frame(input_format, &packet);
        if (response < 0) {
            break;
        }

        if (packet.stream_index > input_format->nb_streams) {
            av_packet_unref(&packet);
            continue;
        }

        if (output_stream_list[packet.stream_index] < 0) {
            av_packet_unref(&packet);
            continue;
        }

        src = input_format->streams[packet.stream_index];

        packet.stream_index = output_stream_list[packet.stream_index];
        dest = output_format->streams[packet.stream_index]; // 上一个循环中使用 avformat_new_stream 设置过了

        // copy packet
        packet.pts = av_rescale_q_rnd(packet.pts, src->time_base, dest->time_base,
                                      static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        packet.dts = av_rescale_q_rnd(packet.dts, src->time_base, dest->time_base,
                                      static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        packet.duration = av_rescale_q(packet.duration, src->time_base, dest->time_base);
        packet.pos = -1;

        response = av_interleaved_write_frame(output_format, &packet);
        if (response < 0) {
            ret = response;
            error("Failed to write frame to stream.");
            av_packet_unref(&packet);
            goto end;
        }

        av_packet_unref(&packet);
    }

    av_write_trailer(output_format);

    end:
    if (input_format != nullptr) {
        avformat_close_input(out input_format);
        input_format = nullptr;
    }

    if (output_format != nullptr) {
        if (output_format->oformat->flags & AVFMT_NOFILE == 0) {
            avio_closep(&output_format->pb);
        }

        avformat_free_context(output_format);
        av_free(output_stream_list);

        if (ret < 0 && ret != AVERROR_EOF) {
            error("error occurred: %d.", ret);
            return -1;
        }
    }
    return ret;
}

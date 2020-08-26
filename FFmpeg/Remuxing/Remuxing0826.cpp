//
// Created by PingZi on 2020/8/26.
//

#define out &

#include "Remuxing0826.h"
#include "logger.h"

extern "C" {
#include "libavformat/avformat.h"
}

int run_0826(int argc, char *argv[]) {
    if (argc < 3) {
        error("must pass at least 2 parameters.");
        return -1;
    }

    int ret = 0;

    const char *input = argv[1];
    const char *output = argv[2];

    AVFormatContext *input_context = nullptr;
    int response = avformat_open_input(out input_context, input, nullptr, nullptr);
    if (response < 0) {
        error("cannot open input file(%s).", input);
        ret = response;
        goto end;
    }

    response = avformat_find_stream_info(input_context, nullptr);
    if (response < 0) {
        error("cannot find stream info for input file.");
        ret = response;
        goto end;
    }

    // initialize output
    AVFormatContext *output_context = nullptr;
    response = avformat_alloc_output_context2(out output_context, nullptr, nullptr, output);
    if (response < 0) {
        error("cannot alloc memory for output context.");
        ret = response;
        goto end;
    }

    // TODO 这里记错了, 使用 oformat->flags 而不是 avio_flag
    //  avio_flag 应该和 AVIO_FLAG_XXX 的值有关系
    int no_file = output_context->oformat->flags & AVFMT_NOFILE;
    if (!no_file) {
        response = avio_open(out output_context->pb, output, AVIO_FLAG_WRITE);
        if (response < 0) {
            error("cannot open output file(%s) to write.", output);
            ret = response;
            goto end;
        }
    }

    int *stream_list = static_cast<int *>(av_mallocz_array(input_context->nb_streams, sizeof(*stream_list)));
    int output_index = 0;
    // 将 stream list 中需要转换的流的index按照顺序记录下来
    for (int i = 0; i < input_context->nb_streams; i++) {
        AVStream *input_stream = input_context->streams[i];
        AVCodecParameters *input_parameters = input_stream->codecpar;

        if (input_parameters->codec_type == AVMEDIA_TYPE_AUDIO ||
            input_parameters->codec_type == AVMEDIA_TYPE_VIDEO ||
            input_parameters->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            AVStream *output_stream = avformat_new_stream(output_context,
                                                          avcodec_find_decoder(input_parameters->codec_id));
            // TODO 这里忘记了拷贝 codec_parameters
            //  虽然 codec_parameters 听起来是一个参数的概念, 我还以为要转换成 codec context 才行.
            //  看来不用
            response = avcodec_parameters_copy(output_stream->codecpar, input_parameters);
            if (response < 0) {
                ret = response;
                error("error when copying parameters");
                goto end;
            }
            stream_list[i] = output_stream->index;
            continue;
        }

        stream_list[i] = -1;
        info("Ignore codec type other than (audio, video, subtitle).");
    }

    // TODO 这里有点忘了, 是需要将 input 的 packet 读取出来, 然后写入输出文件. 不能直接将 stream 强塞给输出文件
    AVPacket *packet = av_packet_alloc();
    if (packet == nullptr) {
        error("cannot alloc memory for av packet.");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    response = avformat_write_header(output_context, nullptr);
    if (response < 0) {
        error("cannot write header for output context.");
        ret = response;
        goto end;
    }
    while (av_read_frame(input_context, packet) >= 0) {
        int stream_index = packet->stream_index;
        if (stream_list[stream_index] < 0) {
            // ignore types other than ...
            av_packet_unref(packet);
            continue;
        }

        AVStream *input_stream = input_context->streams[stream_index];
        AVStream *output_stream = output_context->streams[stream_list[stream_index]];

        // packet to output
        packet->stream_index = stream_list[stream_index];
        packet->pts = av_rescale_q_rnd(packet->pts, input_stream->time_base, output_stream->time_base,
                                       static_cast<AVRounding>(AV_ROUND_PASS_MINMAX | AV_ROUND_NEAR_INF));
        packet->dts = av_rescale_q_rnd(packet->dts, input_stream->time_base, output_stream->time_base,
                                       static_cast<AVRounding>(AV_ROUND_PASS_MINMAX | AV_ROUND_NEAR_INF));
        packet->duration = av_rescale_q(packet->duration, input_stream->time_base,
                                        output_stream->time_base); // TODO 为什么不用缩放
        packet->pos = /*UNKNOW*/-1;

        av_interleaved_write_frame(output_context, packet);
        av_packet_unref(packet);
    }
    response = av_write_trailer(output_context);
    if (response < 0) {
        error("cannot write trailer for output context.");
        ret = response;
        goto end;
    }
    end:
    if (input_context != nullptr) {
        avformat_close_input(out input_context);
        input_context = nullptr;
    }

    if (output_context != nullptr) {
        if (!(output_context->oformat->flags & AVFMT_NOFILE)) {
            avio_close(output_context->pb);
        }
        avformat_free_context(output_context);
        output_context = nullptr;
        av_free(stream_list);
    }

    return ret;
}
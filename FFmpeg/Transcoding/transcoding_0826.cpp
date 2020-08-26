//
// Created by PingZi on 2020/8/26.
//

#include "transcoding_0826.h"
#include "Logger.h"

extern "C" {
#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
}

typedef struct StreamingParams {
    bool copy_video;
    bool copy_audio;
    char *output_extension;
    char *muxer_opt_key;
    char *muxer_opt_value;
    char *video_codec;
    char *audio_codec;
    char *codec_priv_key;
    char *codec_priv_value;
} StreamingParams;

typedef struct StreamingContext {
    AVFormatContext *format_context;
    AVCodec *video_codec;
    AVCodec *audio_codec;
    AVStream *video_stream;
    AVStream *audio_stream;
    AVCodecContext *video_codec_context;
    AVCodecContext *audio_codec_context;
    int video_index;
    int audio_index;
    char *filename;
} StreamingContext;

/**
 * 使用读取方式打开 streaming context
 */
int open_media(StreamingContext *streaming_context) {
    if (streaming_context->filename == nullptr) {
        error("failed to open file NULL");
        return -1;
    }

    int response = avformat_open_input(
            &streaming_context->format_context,
            streaming_context->filename,
            nullptr, nullptr
    );
    if (response < 0) {
        error("failed to open file: %s", streaming_context->filename);
        return response;
    }

    response = avformat_find_stream_info(
            streaming_context->format_context,
            nullptr
    );
    if (response < 0) {
        error("failed to find stream info.");
        return response;
    }

    return 0;
}

int fill_stream_info(AVStream *stream, AVCodec **codec, AVCodecContext **codec_context) {
    AVCodecParameters *parameters = stream->codecpar;
    *codec = avcodec_find_decoder(parameters->codec_id);
    if (*codec == nullptr) {
        error("cannot find decoder for codec id: %d", parameters->codec_id);
        return -1;
    }

    *codec_context = avcodec_alloc_context3(*codec);
    if (*codec_context == nullptr) {
        error("cannot alloc memory for codec context.");
        return -1;
    }

    int response = avcodec_parameters_to_context(*codec_context, parameters);
    if (response < 0) {
        error("Error while copying parameters to context.");
        return response;
    }

    response = avcodec_open2(*codec_context, *codec, nullptr);
    if (response < 0) {
        error("failed to open codec.");
        return response;
    }
    return 0;
}

int prepare_decoder(StreamingContext *streaming_context) {
    AVFormatContext *format_context = streaming_context->format_context;
    uint8_t number = format_context->nb_streams;
    AVStream **streams = format_context->streams;

    for (int i = 0; i < number; i++) {
        AVStream *current_stream = streams[i];
        AVCodecParameters *parameters = current_stream->codecpar;

        if (parameters->codec_type == AVMEDIA_TYPE_VIDEO) {
            info("fill video stream index: %d.", i);
            streaming_context->video_stream = current_stream;
            streaming_context->video_index = i;
            int response = fill_stream_info(
                    streaming_context->video_stream,
                    &streaming_context->video_codec,
                    &streaming_context->video_codec_context
            );
            if (response < 0) {
                error("cannot fill stream info for index: %d, type: video.", i);
                return response;
            }
            continue;
        }

        if (parameters->codec_type == AVMEDIA_TYPE_AUDIO) {
            info("fill audio stream index: %d.", i);
            streaming_context->audio_index = i;
            streaming_context->audio_stream = current_stream;
            int response = fill_stream_info(
                    current_stream,
                    &streaming_context->audio_codec,
                    &streaming_context->audio_codec_context
            );
            if (response < 0) {
                error("cannot fill stream info for index: %d, type: audio.", i);
                return response;
            }
            continue;
        }

        info("skipping streams other than video and audio.");
    }

    return 0;
}

int prepare_video_encoder(StreamingContext *output_context, AVCodecContext *decoder, AVRational input_framerate,
                          StreamingParams params) {
    info("prepare video encoder");
    output_context->video_stream = avformat_new_stream(output_context->format_context, nullptr);
    if (output_context->video_stream == nullptr) {
        error("cannot add new video stream to output context");
        return -1;
    }

    output_context->video_codec = avcodec_find_encoder_by_name(params.video_codec);
    if (output_context->video_codec == nullptr) {
        error("cannot find encoder for name: %s", params.video_codec);
        return -1;
    }

    output_context->video_codec_context = avcodec_alloc_context3(output_context->video_codec);
    if (output_context->video_codec_context == nullptr) {
        error("failed to alloc memory for output video codec context.");
        return -1;
    }

    // TODO 这是什么
    av_opt_set(output_context->video_codec_context->priv_data, "preset", "fast", 0);
    if (params.codec_priv_key != nullptr && params.codec_priv_value != nullptr) {
        av_opt_set(output_context->video_codec_context->priv_data, params.codec_priv_key, params.codec_priv_value, 0);
    }

    output_context->video_codec_context->height = decoder->height;
    output_context->video_codec_context->width = decoder->width;

    output_context->video_codec_context->sample_aspect_ratio = decoder->sample_aspect_ratio;
    // TODO 这是在做什么
    if (output_context->video_codec->pix_fmts != nullptr) {
        output_context->video_codec_context->pix_fmt = output_context->video_codec->pix_fmts[0];
    } else {
        output_context->video_codec_context->pix_fmt = decoder->pix_fmt;
    }

    // TODO 这是个什么数啊, 凭什么
    output_context->video_codec_context->bit_rate = 2 * 1000 * 1000;
    output_context->video_codec_context->rc_buffer_size = 4 * 1000 * 1000;
    output_context->video_codec_context->rc_max_rate = 2 * 1000 * 1000;
    output_context->video_codec_context->rc_min_rate = 2.5 * 1000 * 1000;

    output_context->video_codec_context->time_base = av_inv_q(input_framerate); // TODO 新函数
    output_context->video_stream->time_base = output_context->video_codec_context->time_base;

    int response = avcodec_open2(output_context->video_codec_context, output_context->video_codec, nullptr);
    if (response < 0) {
        error("cannot open codec for output context");
        return response;
    }
    avcodec_parameters_from_context(output_context->video_stream->codecpar, output_context->video_codec_context);

    return 0;
}

int prepare_copy(AVFormatContext *dest_format_context, AVStream **dest_stream, AVCodecParameters *src_parameters) {
    *dest_stream = avformat_new_stream(dest_format_context, nullptr);
    if (*dest_stream == nullptr) {
        error("cannot add stream for format_context.");
        return -1;
    }

    return avcodec_parameters_copy((*dest_stream)->codecpar, src_parameters);
}

int prepare_audio_encoder(StreamingContext *streaming_context, int src_sample_rate, StreamingParams params) {
    info("prepare audio encoder.");
    streaming_context->audio_stream = avformat_new_stream(streaming_context->format_context, nullptr);
    if (streaming_context->audio_stream == nullptr) {
        error("cannot to add new audio stream for format context");
        return -1;
    }

    streaming_context->audio_codec = avcodec_find_encoder_by_name(params.audio_codec);
    if (streaming_context->audio_codec == nullptr) {
        error("cannot find encoder by name: %s", params.audio_codec);
        return -1;
    }

    streaming_context->audio_codec_context = avcodec_alloc_context3(streaming_context->audio_codec);
    if (streaming_context->audio_codec_context == nullptr) {
        error("failed to alloc memory for audio codec context");
        return -1;
    }

    // TODO 又来了 什么数儿啊这是
    int OUTPUT_CHANNELS = 2;
    int OUTPUT_BIT_RATE = 196000;

    streaming_context->audio_codec_context->channels = OUTPUT_CHANNELS;
    streaming_context->audio_codec_context->channel_layout = av_get_default_channel_layout(OUTPUT_CHANNELS);
    streaming_context->audio_codec_context->sample_rate = src_sample_rate;
    streaming_context->audio_codec_context->sample_fmt = streaming_context->audio_codec->sample_fmts[0];
    streaming_context->audio_codec_context->bit_rate = OUTPUT_BIT_RATE;
    streaming_context->audio_codec_context->time_base = AVRational {1, src_sample_rate};


    streaming_context->audio_codec_context->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
    streaming_context->audio_stream->time_base = streaming_context->audio_codec_context->time_base;

    int response = avcodec_open2(streaming_context->audio_codec_context, streaming_context->audio_codec, nullptr);
    if (response < 0) {
        error("cannot open audio codec for output");
        return response;
    }
    avcodec_parameters_from_context(streaming_context->audio_stream->codecpar, streaming_context->audio_codec_context);

    return 0;
}

int64_t av_rational_division(AVRational left, AVRational right) {
    return ((float)left.num / left.den) / ((float)right.num / right.den);
}

int encode_video(StreamingContext *input_context, StreamingContext *output_context, AVFrame *frame) {
    info("run video encoding.");
    if (frame != nullptr) { // frame 有可能为空, 在最后一部分 flush 的时候
        frame->pict_type = AV_PICTURE_TYPE_NONE; // TODO 是什么\
    }
    AVPacket *packet = av_packet_alloc();
    if (packet == nullptr) {
        error("cannot alloc memory for video output packet.");
        return -1;
    }

    AVCodecContext *encoder = output_context->video_codec_context;
    AVCodecContext *decoder = input_context->video_codec_context;

    int response = avcodec_send_frame(encoder, frame);
    if (response < 0) {
        error("error while sending frame to video encoder.");
        av_packet_unref(packet);
        av_frame_unref(frame);
        av_packet_free(&packet);
        return response;
    }

    while ((response = avcodec_receive_packet(encoder, packet)) >= 0) {
        packet->stream_index = output_context->video_index;
        packet->duration = av_rational_division(input_context->video_stream->avg_frame_rate,
                                                output_context->video_stream->time_base);
        av_packet_rescale_ts(packet, input_context->video_stream->time_base, output_context->video_stream->time_base);
        response = av_interleaved_write_frame(output_context->format_context, packet);
        if (response < 0) {
            error("cannot write frame for output video.");
            av_packet_unref(packet);
            av_frame_unref(frame);
            av_packet_free(&packet);
            return response;
        }
    }
    av_packet_unref(packet);
    av_frame_unref(frame);
    av_packet_free(&packet);

    return 0;
}

int encode_audio(StreamingContext *input_context, StreamingContext *output_context,
                 AVFrame *frame) {
    info("run audio encoding...");
    AVCodecContext *encoder = output_context->audio_codec_context;
    AVCodecContext *decoder = input_context->audio_codec_context;

    AVPacket *packet = av_packet_alloc();
    if (packet == nullptr) {
        error("Cannot alloc packet for audio encoder.");
        return -1;
    }

    int response = avcodec_send_frame(encoder, frame);
    if (response < 0) {
        if (response == AVERROR_EOF) {
            info("send frame to encoder catch eof.");
            return 0;
        }

        if (response == AVERROR(EAGAIN)) {
            response = 0;
        } else {
            error("Failed to send frame to audio encoder.");
            return -1;
        }
    }

    while ((response = avcodec_receive_packet(encoder, packet)) >= 0) {
        packet->stream_index = output_context->audio_index;
        response = av_interleaved_write_frame(output_context->format_context, packet);
        if (response < 0) {
            error("Failed to write frame to output audio stream");
            av_packet_unref(packet);
            av_frame_unref(frame);
            av_packet_free(&packet);
            return response;
        }
    }
    av_packet_free(&packet);

    if (response < 0 && response != AVERROR_EOF && response != AVERROR(EAGAIN)) {
        error("Failed to receive packet from audio encoder.");
        return response;
    }

    if (response == AVERROR_EOF) {
        info("receiving frame from audio encoder catch EOF.");
    }

    return 0;
}

int transcode_video(StreamingContext *input_context, StreamingContext *output_context,
                    AVPacket *packet, AVFrame *frame) {

    AVCodecContext *encoder = output_context->video_codec_context;
    AVCodecContext *decoder = input_context->video_codec_context;

    int response = avcodec_send_packet(decoder, packet);
    if (response < 0 && response != AVERROR_EOF && response != AVERROR(EAGAIN)) {
        error("failed to send packet to decoder");
        return response;
    }

    if (response == AVERROR_EOF) {
        info("decoder EOF");
        return 0;
    }

    if (response == AVERROR(EAGAIN)) {
        response = 0;
    }

    while ((response = avcodec_receive_frame(decoder, frame)) >= 0) {
        response = encode_video(input_context, output_context, frame);
        if (response < 0) {
            error("Failed to encode video");
            return response;
        }
    }

    if (response == AVERROR_EOF) {
        info("receive EOF.");
        return 0;
    }

    if (response == AVERROR(EAGAIN)) {
        return 0;
    }

    return 0;
}

int transcode_audio(StreamingContext *input_context, StreamingContext *output_context,
                    AVPacket *packet, AVFrame *frame) {
    AVCodecContext *encoder = output_context->audio_codec_context;
    AVCodecContext *decoder = input_context->audio_codec_context;

    int response = avcodec_send_packet(decoder, packet);
    if (response == AVERROR_EOF) {
        info("send audio packet catch eof.");
        return 0;
    }

    if (response == AVERROR(EAGAIN)) {
        response = 0;
    }

    if (response < 0) {
        error("Failed to send audio stream to decoder.");
        return response;
    }

    while ((response = avcodec_receive_frame(decoder, frame)) >= 0) {
        response = encode_audio(input_context, output_context, frame);
        if (response < 0) {
            error("Failed to decoder audio.");
            return response;
        }
    }
    if (response == AVERROR_EOF) {
        info("receiving audio packet catch eof,");
        return 0;
    }

    if (response == AVERROR(EAGAIN)) {
        return 0;
    }

    if (response < 0) {
        error("Failed to receive frame from audio decoder.");
        return response;
    }

    return 0;
}

int remux(AVPacket **packet, AVFormatContext **output_context, AVRational input_timebase, AVRational output_timebase) {
    info("remux...");
    // TODO ssm
    av_packet_rescale_ts(*packet, input_timebase, output_timebase);
    int response = av_interleaved_write_frame(*output_context, *packet);
    if (response < 0) {
        error("error while copying stream packet.");
        return response;
    }

    return 0;
}

int run_0826(int argc, char **argv) {

    if (argc < 3) {
        error("filename request");
        return -1;
    }

    StreamingParams params = {0};
    params.copy_audio = true;
    params.copy_video = false;
    params.video_codec = "libx265";
    params.codec_priv_key = "x265-params";
    params.codec_priv_value = "keyint=60:min-keyint=60:scenecut=0";

    int ret = 0;
    int response = 0;

    // 为什么不是 malloc
    StreamingContext *input_context = static_cast<StreamingContext *>(calloc(1, sizeof(StreamingContext)));
    input_context->filename = argv[1];

    StreamingContext *output_context = static_cast<StreamingContext *>(calloc(1, sizeof(StreamingContext)));
    output_context->filename = argv[2];

    if (params.output_extension != nullptr) {
        strcat(output_context->filename, params.output_extension);
    }

    response = open_media(input_context);
    if (response < 0) {
        error("cannot open media to read.");
        ret = response;
        goto end;
    }
    response = prepare_decoder(input_context);
    if (response < 0) {
        error("Failed to prepare decoder.");
        ret = response;
        goto end;
    }

    info("input file prepare success.");

    response = avformat_alloc_output_context2(&output_context->format_context, nullptr, nullptr,
                                              output_context->filename);
    if (response < 0 || output_context->format_context == nullptr) {
        error("Failed to alloc memory for output context");
        ret = response;
        goto end;
    }

    if (!params.copy_video) {
        AVRational input_framerate = av_guess_frame_rate(
                input_context->format_context,
                input_context->video_stream,
                nullptr
        );
        int response = prepare_video_encoder(output_context, input_context->video_codec_context, input_framerate,
                                             params);
        if (response < 0) {
            error("failed to prepare video encoder.");
            ret = response;
            goto end;
        }
    } else {
        // prepare copy 没有给 output_context 塞那么多参数诶
        info("prepare video copy");
        int response = prepare_copy(output_context->format_context, &output_context->video_stream,
                                    input_context->video_stream->codecpar);
        if (response < 0) {
            error("failed to prepare copy video stream.");
            ret = response;
            goto end;
        }
    }

    if (!params.copy_audio) {
        int response = prepare_audio_encoder(output_context, input_context->audio_codec_context->sample_rate, params);
        if (response < 0) {
            error("failed to prepare audio encoder.");
            ret = response;
            goto end;
        }
    } else {
        info("prepare audio copy");
        int response = prepare_copy(output_context->format_context, &output_context->audio_stream,
                                    input_context->audio_stream->codecpar);
        if (response < 0) {
            error("failed to prepare copy audio stream.");
            ret = response;
            goto end;
        }
    }

    info("io open output file");
    // TODO 咩啊?
    if ((output_context->format_context->oformat->flags & AVFMT_GLOBALHEADER) != 0) {
        output_context->format_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    if ((output_context->format_context->oformat->flags & AVFMT_NOFILE) == 0) {
        response = avio_open(&output_context->format_context->pb, output_context->filename, AVIO_FLAG_WRITE);
        if (response < 0) {
            error("cannot open output file");
            ret = response;
            goto end;
        }
    }

    info("output file prepare success");

    // TODO 咋个意思
    AVDictionary *muxer_opts = nullptr;
    if (params.muxer_opt_key != nullptr && params.muxer_opt_value != nullptr) {
        av_dict_set(&muxer_opts, params.muxer_opt_key, params.muxer_opt_value, 0);
    }

    AVFrame *frame = av_frame_alloc();
    AVPacket *packet = av_packet_alloc();

    if (frame == nullptr || packet == nullptr) {
        error("cannot alloc memory for frame or packet");
        ret = -1;
        goto end;
    }

    response = avformat_write_header(output_context->format_context, &muxer_opts);
    AVStream **input_streams = input_context->format_context->streams;
    while (av_read_frame(input_context->format_context, packet) >= 0) {
        int stream_index = packet->stream_index;
        AVMediaType current_type = input_streams[stream_index]->codecpar->codec_type;

        if (current_type == AVMEDIA_TYPE_AUDIO) {
            info("precessing encode/copy audio stream.");
            if (!params.copy_audio) {
                response = transcode_audio(input_context, output_context, packet, frame);
                av_packet_unref(packet);
                if (response < 0) {
                    error("Error while transcoding audio.");
                    ret = response;
                    goto end;
                }
            } else {
                response = remux(&packet, &output_context->format_context,
                                 input_context->audio_stream->time_base,
                                 output_context->audio_stream->time_base);
                av_packet_unref(packet);
                if (response < 0) {
                    error("Error while copying audio");
                    ret = response;
                    goto end;
                }
            }
            continue;
        }

        if (current_type == AVMEDIA_TYPE_VIDEO) {
            info("precessing encode/copy video stream.");
            if (!params.copy_video) {
                response = transcode_video(input_context, output_context, packet, frame);
                if (response < 0) {
                    error("Error while transcoding video.");
                    ret = response;
                    goto end;
                }
            } else {
                response = remux(&packet, &output_context->format_context,
                                 input_context->video_stream->time_base,
                                 output_context->video_stream->time_base);
            }
            continue;
        }

        av_packet_unref(packet);
    }

    response = encode_video(input_context, output_context, nullptr);
    if (response < 0) {
        ret = response;
        goto end;
    }
    av_write_trailer(output_context->format_context);

    end:
    if (muxer_opts != nullptr) {
        av_dict_free(&muxer_opts);
        muxer_opts = nullptr;
    }

    if (frame != nullptr) {
        av_frame_free(&frame);
        frame = nullptr;
    }

    if (packet != nullptr) {
        av_packet_free(&packet);
        packet = nullptr;
    }

    avformat_close_input(&input_context->format_context);

    avformat_free_context(input_context->format_context);
    avformat_free_context(output_context->format_context);
    input_context->format_context = nullptr;
    output_context->format_context = nullptr;

    free(input_context);
    free(output_context);
    input_context = nullptr;
    output_context = nullptr;

    if (ret == 0) {
        info("success!");
    } else {
        error("something happened!");
    }

    return ret;
}

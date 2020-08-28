//
// Created by PingZi on 2020/8/28.
//

#include "transcoding0828.h"
#include "Logger.h"

extern "C" {
#include "libavformat/avformat.h"
}

typedef struct TranscodingParameters {
    bool copy_audio;
    bool copy_video;
    char *video_codec;
    char *audio_codec;
    // 参数什么的不记得了
} TranscodingParameters;

typedef struct StreamContext {
    int stream_index;
    AVStream *stream;
    AVCodecContext *codec_context;
    AVCodec *codec;
} StreamContext;

// 对应示例项目的 StreamingContext
typedef struct MediaFormat {
    char *filename;
    AVFormatContext *format_context;
    StreamContext video_stream;
    StreamContext audio_stream;

} MediaFormat;

int open_decoder(AVCodecParameters *parameters, AVCodec **codec, AVCodecContext **codec_context) {
    *codec = avcodec_find_decoder(parameters->codec_id);
    if ((*codec) == nullptr) {
        error("cannot find decoder for codec_id: %d.", parameters->codec_id);
        return -1;
    }

    *codec_context = avcodec_alloc_context3(*codec);
    if ((*codec_context) == nullptr) {
        error("cannot alloc memory for codec_context.");
        return -1;
    }

    int response = avcodec_parameters_to_context(*codec_context, parameters);
    if (response < 0) {
        error("cannot copy parameters to context.");
        return response;
    }


    response = avcodec_open2(*codec_context, *codec, nullptr);
    if (response < 0) {
        error("cannot open codec for decoder.");
        return response;
    }

    return 0;
}

int open_input(MediaFormat *media) {
    if (media->filename == nullptr) {
        error("cannot open file, file name is null");
        return -1;
    }

    int response = avformat_open_input(&media->format_context, media->filename, nullptr, nullptr);
    if (response < 0) {
        error("cannot open file for input, error code: %d", response);
        return response;
    }

    response = avformat_find_stream_info(media->format_context, nullptr);
    if (response < 0) {
        error("failed to find stream info for input media.");
        return response;
    }

    for (int i = 0; i < media->format_context->nb_streams; i++) {
        AVStream *current_stream = media->format_context->streams[i];
        AVCodecParameters *parameters = current_stream->codecpar;

        if (parameters->codec_type == AVMEDIA_TYPE_VIDEO) {
            info("find video stream index: %d in input file.");
            AVCodec *codec = nullptr;
            AVCodecContext *decoder = nullptr;

            // open codec
            response = open_decoder(parameters, &codec, &decoder);
            if (response < 0) {
                error("cannot open video decoder for input file: %d.", response);
                return response;
            }

            media->video_stream.stream = current_stream;
            media->video_stream.stream_index = current_stream->index;
            media->video_stream.codec = codec;
            media->video_stream.codec_context = decoder;
            continue;
        }

        if (parameters->codec_type == AVMEDIA_TYPE_AUDIO) {
            info("find audio stream index: %d in input file.", current_stream->index);
            AVCodec *codec = nullptr;
            AVCodecContext *decoder = nullptr;

            response = open_decoder(parameters, &codec, &decoder);
            if (response < 0) {
                error("cannot open audio decoder for input file: %d", response);
                return response;
            }

            media->audio_stream.stream = current_stream;
            media->audio_stream.stream_index = current_stream->index;
            media->audio_stream.codec = codec;
            media->audio_stream.codec_context = decoder;
            continue;
        }

        info("ignore stream type other than video and audio.");
    }

    info("open input file success.");
    return 0;
}

int find_encoder_by_name(const char *name, AVCodec **codec, AVCodecContext **context) {
    info("finding encoder by name: %s.", name);

    *codec = avcodec_find_encoder_by_name(name);
    if ((*codec) == nullptr) {
        error("cannot find encoder by name: %s.", name);
        return -1;
    }
    info("codec find: %s.", (*codec)->long_name);

    *context = avcodec_alloc_context3(*codec);
    if ((*context) == nullptr) {
        error("cannot alloc memory for encoder context.");
        return -1;
    }

    return 0;
}

int
new_output_video_stream(bool copy_video, const char *video_codec, StreamContext input_video,
                        StreamContext *output_video,
                        AVFormatContext *output_format, AVRational framerate) {

    // Add new stream to output.
    output_video->stream = avformat_new_stream(output_format, nullptr);
    if (output_video->stream == nullptr) {
        error("cannot add new stream for output format context.");
        return -1;
    }
    output_video->stream_index = output_video->stream->index;

    // init output stream codec
    int response = 0;
    if (copy_video) {
        info("copy input video codec.");
        response = avcodec_parameters_copy(output_video->stream->codecpar, input_video.stream->codecpar);
        return response;
    } else {

        response = find_encoder_by_name(video_codec, &output_video->codec, &output_video->codec_context);
        if (response < 0) {
            error("Failed to find encoder by name: %s", video_codec);
            return response;
        }
        info("use encoder by name: %s", video_codec);

        AVCodecContext *encoder = output_video->codec_context;
        AVCodecContext *decoder = input_video.codec_context;

        encoder->width = decoder->width;
        encoder->height = decoder->height;
        encoder->time_base = av_inv_q(framerate); // 这个属性必须设置, 不然使用 avcodec_open2 打不开文件

        if (output_video->codec->pix_fmts != nullptr) {
            encoder->pix_fmt = output_video->codec->pix_fmts[0];
        } else {
            encoder->pix_fmt = decoder->pix_fmt;
        }

        encoder->bit_rate = 2 * 1000 * 1000;
        encoder->rc_max_rate = 4 * 1000 * 1000;
        encoder->rc_min_rate = 2 * 1000 * 1000;
        // TODO 还有啥饶了我吧, 这咋背的过.
        //  不过我严重怀疑可能除了宽高和比例别的不设置也行.

        info("open video encoder...");
        response = avcodec_open2(output_video->codec_context, output_video->codec, nullptr);
        if (response < 0) {
            error("cannot open video encoder for output video.");
            return response;
        }

        avcodec_parameters_from_context(output_video->stream->codecpar, output_video->codec_context);
        info("encoder context success opened.");

        return 0;
    }
}

int new_output_audio_stream(bool copy_audio, const char *audio_codec,
                            StreamContext input_audio, StreamContext *output_audio,
                            AVFormatContext *output_format) {

    int response = 0;

    output_audio->stream = avformat_new_stream(output_format, nullptr);
    output_audio->stream_index = output_audio->stream->index;

    if (copy_audio) {

        response = avcodec_parameters_copy(output_audio->stream->codecpar, input_audio.stream->codecpar);
        return response;
    } else {

        response = find_encoder_by_name(audio_codec, &output_audio->codec, &output_audio->codec_context);
        if (response < 0) {
            error("Failed to find encoder by name: %s", audio_codec);
            return response;
        }

        AVCodecContext *encoder = output_audio->codec_context;
        AVCodecContext *decoder = input_audio.codec_context;

        encoder->channels = 2;
        encoder->channel_layout = av_get_default_channel_layout(encoder->channels);
        encoder->sample_rate = decoder->sample_rate;
        encoder->bit_rate = 196000;
        // TODO 这个真的要背吗

        response = avcodec_open2(output_audio->codec_context, output_audio->codec, nullptr);
        if (response < 0) {
            error("cannot open audio encoder for output audio stream.");
            return response;
        }
        avcodec_parameters_from_context(output_audio->stream->codecpar, output_audio->codec_context);
        return 0;
    }
}

int remuxing(AVFormatContext *output, AVPacket *packet, AVRational src_ts, AVRational dest_ts) {

    av_packet_rescale_ts(packet, src_ts, dest_ts);
    return av_interleaved_write_frame(output, packet);
}

int write_audio_stream(MediaFormat input, MediaFormat output, AVPacket *packet, AVFrame *frame, bool copy) {
    if (copy) {
        int response = remuxing(output.format_context, packet,
                                input.audio_stream.stream->time_base,
                                output.audio_stream.stream->time_base);
        if (response < 0) {
            error("error while copying audio stream to output.");
            return response;
        }
        return 0;
    } else {
        AVCodecContext *encoder = output.audio_stream.codec_context;
        AVCodecContext *decoder = input.audio_stream.codec_context;

        int response = avcodec_send_packet(decoder, packet);
        if (response < 0 && response != AVERROR(EAGAIN)) {
            error("cannot send packet to decoder.");
            return response;
        }

        AVPacket *encoder_packet = av_packet_alloc();
        if (encoder_packet == nullptr) {
            error("cannot alloc memory for encoder packet.");
            return -1;
        }
        while ((response = avcodec_receive_frame(decoder, frame)) >= 0) {
            // uncompress frame
            response = avcodec_send_frame(encoder, frame);
            if (response < 0 && response != AVERROR(EAGAIN)) {
                error("cannot send frame to encoder.");
                av_frame_unref(frame);
                break;
            }

            while ((response = avcodec_receive_packet(encoder, encoder_packet))) {
                encoder_packet->stream_index = output.audio_stream.stream_index;
                response = av_interleaved_write_frame(output.format_context, encoder_packet);
                if (response < 0) {
                    av_packet_unref(packet);
                    break;
                }
                av_packet_unref(encoder_packet);
            }

            av_frame_unref(frame);
        }

        if (response < 0 && response != AVERROR(EAGAIN)) {
            error("error while receive frame from decoder.");
            return response;
        }

        return 0;
    }
}

int write_video_stream(MediaFormat input, MediaFormat output, AVPacket *packet, AVFrame *frame, bool copy) {
    if (copy) {
        int response = remuxing(output.format_context, packet,
                                input.video_stream.stream->time_base,
                                output.video_stream.stream->time_base);

        if (response < 0) {
            error("error while copying video stream to output file.");
            return response;
        }

        return 0;
    } else {
        AVCodecContext *encoder = output.video_stream.codec_context;
        AVCodecContext *decoder = input.video_stream.codec_context;

        int response = avcodec_send_packet(decoder, packet);
        if (response < 0 && response != AVERROR(EAGAIN)) {
            error("error while send packet to decoder.");
            return response;
        }

        AVPacket *encoder_packet = av_packet_alloc();
        if (encoder_packet == nullptr) {
            error("cannot alloc memory for encoder.");
            return -1;
        }
        while ((response = avcodec_receive_frame(decoder, frame)) >= 0) {

            response = avcodec_send_frame(encoder, frame);
            if (response < 0 && response != AVERROR(EAGAIN)) {
                error("cannot send frame to video encoder.");
                av_frame_unref(frame);
                break;
            }

            while ((response = avcodec_receive_packet(encoder, encoder_packet)) >= 0) {
                response = av_interleaved_write_frame(output.format_context, encoder_packet);
                if (response < 0) {
                    av_packet_unref(encoder_packet);
                    break;
                }
                av_packet_unref(encoder_packet);
            }

            av_frame_unref(frame);
        }

        if (response < 0 && response != AVERROR(EAGAIN)) {
            error("error while write packet to output file.");
            return response;
        }

        return 0;
    }
}

int run0828(int argc, char **argv) {

    int ret = 0;

    TranscodingParameters parameters = {};
    parameters.copy_video = false;
    parameters.copy_audio = true;
    parameters.video_codec = "libx265";

    MediaFormat input_media = {};
    MediaFormat output_media = {};

    input_media.filename = argv[1];
    output_media.filename = argv[2];

    int response = open_input(&input_media);
    if (response < 0) {
        error("cannot open input file: %d.", response);
        ret = response;
        goto end;
    }

    info("alloc memory for output format context.");
    response = avformat_alloc_output_context2(&output_media.format_context, nullptr, nullptr, output_media.filename);
    if (response < 0) {
        error("cannot alloc memory for output context.");
        ret = response;
        goto end;
    }

    info("fill output video stream.");
    AVRational framerate = av_guess_frame_rate(input_media.format_context, input_media.video_stream.stream, nullptr);
    response = new_output_video_stream(
            parameters.copy_video, parameters.video_codec,
            input_media.video_stream, &output_media.video_stream,
            output_media.format_context, framerate);
    if (response < 0) {
        error("cannot fill output video stream");
        ret = response;
        goto end;
    }
    info("output video stream init success: [videoIndex: %d].", output_media.video_stream.stream_index);

    info("fill output audio stream.");
    response = new_output_audio_stream(
            parameters.copy_audio, parameters.audio_codec,
            input_media.audio_stream, &output_media.audio_stream,
            output_media.format_context);
    if (response < 0) {
        error("cannot fill output audio stream.");
        ret = response;
        goto end;
    }
    info("output audio stream init success: [audioIndex: %d].", output_media.audio_stream.stream_index);

    info("open output media file.");
    // open output and copy file
    response = avio_open(&output_media.format_context->pb, output_media.filename, AVIO_FLAG_WRITE);
    if (response < 0) {
        error("cannot open media file named: %s", output_media.filename);
        ret = response;
        goto end;
    }

    // 我记得这里原来是给了个什么参数, 忘了

    info("writing output media...");
    response = avformat_write_header(output_media.format_context, nullptr);
    if (response < 0) {
        error("cannot write header to output file");
        ret = response;
        goto end;
    }

    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    if (packet == nullptr || frame == nullptr) {
        error("Failed to alloc memory for packet or frame.");
        ret = -1;
        goto end;
    }
    info("write packet or frame to output file.");
    AVStream **input_streams = input_media.format_context->streams;
    while ((av_read_frame(input_media.format_context, packet)) >= 0) {

        AVStream *current_stream = input_streams[packet->stream_index];
        AVCodecParameters *codec_params = current_stream->codecpar;

        if (codec_params->codec_type == AVMEDIA_TYPE_AUDIO) {
            response = write_audio_stream(input_media, output_media, packet, frame, parameters.copy_audio);
            if (response < 0) {
                error("Error while write stream to audio.");
                ret = response;
                av_packet_unref(packet);
                goto end;
            }
            continue;
        }

        if (codec_params->codec_type == AVMEDIA_TYPE_VIDEO) {
            response = write_video_stream(input_media, output_media, packet, frame, parameters.copy_video);
            if (response < 0) {
                error("Error while write stream to video.");
                ret = response;
                av_packet_unref(packet);
                goto end;
            }
            continue;
        }

        av_packet_unref(packet);
        info("Ignore types other than audio and video.");
    }

    response = av_write_trailer(output_media.format_context);
    if (response < 0) {
        error("Failed to write trailer for output file.");
        ret = response;
        goto end;
    }
    info("success!");
    end:

    return 0;
}
#include <iostream>

#define out &

extern "C" {
#include "libavformat/avformat.h"
}

static void logging(const char *fmt, ...) {
    va_list arg;
            va_start(arg, fmt);
    vfprintf(stdout, fmt, arg);
    fprintf(stdout, "\n");
            va_end (arg);

}

static void save_gray_image(uint8_t *buf, int wrap, int x_size, int y_size, const char *filename) {
    FILE *file;
    int i;

    file = fopen(filename, "w");
    fprintf(file, "P5\n%d %d\n%d\n", x_size, y_size, 255);

    for (i = 0; i < y_size; i++) {
        fwrite(buf + i * wrap, 1, x_size, file);
    }

    fclose(file);
}

static int decode_packet(AVPacket *packet, AVCodecContext *codec_context, AVFrame *frame) {
    int response = avcodec_send_packet(codec_context, packet);
    if (response < 0) {
        logging("ERROR while sending a packet to the decoder: %s", av_err2str(response));
        return response;
    }

    while (response > 0) {
        response = avcodec_receive_frame(codec_context, frame);
        if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
            break;
        }

        if (response < 0) {
            logging("ERROR while receiving a frame from the decoder: %s", av_err2str(response));
            return response;
        }

        if (response > 0) {
            logging("Frame %d (type=%c, size=%d bytes) pts %d key_frame %d [DTS %d]",
                    codec_context->frame_number,
                    av_get_picture_type_char(frame->pict_type),
                    frame->pkt_size,
                    frame->pts,
                    frame->key_frame,
                    frame->coded_picture_number);
        }

        char frame_filename[1024];
        snprintf(frame_filename, sizeof(frame_filename), "%s-%d.pgm", "frame", codec_context->frame_number);
        save_gray_image(frame->data[0], frame->linesize[0], frame->width, frame->height, frame_filename);
    }
    return 0;
}

int main(int argc, char* argv[]) {

    if (argc < 2) {
        logging("You need to specify a media file.\n");
        return -1;
    }

    const char* input = argv[1];

    logging("Initializing all the containers, codecs and protocols.");

    AVFormatContext *format_context = avformat_alloc_context();
    if (format_context == nullptr) {
        logging("Error could not allocate memory for Format Context.");
        return -1;
    }

    logging("opening the input file (%s), and loading format(container) header.", input);
    int ret = avformat_open_input(out format_context, input, nullptr, nullptr);
    if (ret < 0) {
        logging("ERROR could not open file(%s)", input);
        return -1;
    }

    logging("input file information:");
    logging("format: %s", format_context->iformat->name);
    logging("duration: %lld us", format_context->duration);
    logging("bitrate: %lld", format_context->bit_rate);
    logging("...");

    logging("finding stream info from format.");
    ret = avformat_find_stream_info(format_context, nullptr);
    if (ret < 0) {
        logging("ERROR: could not find stream info.");
        return -1;
    }

    AVCodec *video_codec = nullptr;
    AVCodecParameters *video_codec_parameters = nullptr;
    int video_stream_index = -1;
    unsigned int number_of_streams = format_context->nb_streams;

    logging("video streams info:");
    for (int i = 0; i < number_of_streams; i++) {
        AVStream *stream = format_context->streams[i];
        AVCodecParameters* parameters = stream->codecpar;
        logging("--------------stream index: %d.---------------", i);
        logging("timebase before open codec: %d/%d", stream->time_base.num, stream->time_base.den);
        logging("r frame rate before open codec: %d/%d", stream->r_frame_rate.num, stream->r_frame_rate.den);
        logging("start time%" PRId64, stream->start_time);
        logging("duration: %" PRId64, stream->duration);

        logging("finding the proper decoder(CODEC)");
        AVCodec *codec = nullptr;

        codec = avcodec_find_decoder(parameters->codec_id);
        if (codec == nullptr) {
            logging("ERROR: unsupported codec!");
            return -1;
        }

        if (parameters->codec_type == AVMEDIA_TYPE_VIDEO) {
            logging("video codec:");
            logging("resolution %d x %d", parameters->width, parameters->height);

            if (video_stream_index == -1) {
                video_stream_index = i;
                video_codec = codec;
                video_codec_parameters = parameters;
            }
        } else if (parameters->codec_type == AVMEDIA_TYPE_AUDIO) {
            logging("audio codec:");
            logging("channels: %d", parameters->channels);
            logging("sample rate: %d", parameters->sample_rate);
        }

        logging("\t codec %s ID %d bitrate %lld", codec->name, codec->id, parameters->bit_rate);
        logging("----------------------end---------------------");
    }

    AVCodecContext *codec_context = avcodec_alloc_context3(video_codec);
    if (codec_context == nullptr) {
        logging("ERROR failed to allocate memory for avcodec context");
        return -1;
    }

    ret = avcodec_parameters_to_context(codec_context, video_codec_parameters);
    if (ret < 0) {
        logging("ERROR failed to copy codec params to codec context");
        return -1;
    }

    ret = avcodec_open2(codec_context, video_codec, nullptr);
    if (ret < 0) {
        logging("ERROR failed to open codec(%s) through avcodec_open2", video_codec->name);
        return -1;
    }

    AVFrame *frame = av_frame_alloc();
    if (frame == nullptr) {
        logging("ERROR failed to allocate memory for av frame");
        return -1;
    }

    AVPacket *packet = av_packet_alloc();
    if (packet == nullptr) {
        logging("ERROR failed to allocate memory for av pakcet");
        return -1;
    }

    int response = 0;
    int how_many_packet_to_process = 8;

    while (av_read_frame(format_context, packet) > 0) {
        if (packet->stream_index != video_stream_index) {
            av_packet_unref(packet);
            continue;
        }

        logging("pts: %" PRId64, packet->pts);
        response = decode_packet(packet, codec_context, frame);
        if (response < 0) {
            break;
        }

        how_many_packet_to_process--;
        if (how_many_packet_to_process < 0) {
            break;
        }

        av_packet_unref(packet);
    }

    logging("releasing all the resources");
    avformat_close_input(&format_context);
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&codec_context);
    return 0;
}



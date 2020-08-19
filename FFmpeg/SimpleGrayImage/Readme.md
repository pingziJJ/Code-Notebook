# 使用 FFmpeg 完成解析视频, 输出黑白图片

> 项目学习自 https://github.com/leandromoreira/ffmpeg-libav-tutorial/blob/master/0_hello_world.c

代码会输出一些视频信息给控制台, 解析一些 frame 并且将解析到的 frame 存储成一个图片文件.
使用 [GitHub项目 [ffmpeg-libav-tutorial](https://github.com/leandromoreira/ffmpeg-libav-tutorial#chapter-0---the-infamous-hello-world)]上的图片来解释:
![decoding](.\images\decoding.png)<br>

**VIDEO.MP4** 是输入的 video 文件. 在图中是用 mpeg4 封装的, 使用 h264 压缩视频, 使用 aac 压缩音频.<br>
**AVFormatContext** 包含着视频的一些头文件信息. 在使用 av_format_open_input 打开 AVFormatContext 之后, 要注意此时并没有将整个视频读入内存. 使用 av_format_find_stream_info 之后才会将流数据加载进内存<br>
**AVStream** 是一个流信息, 可能是视频流也可能是音频流/字幕流等, 有时候一个视频有多个视频流或多个音频流.

接下来的两个结构体是比较重要的.<br>
**AVPacket** 代表读取出来的一个数据包. 从 packet 通过 AVCodec 解码才可以得到无压缩的图片/音频数据<br>
**AVFrame** 代表解码完成之后的无压缩数据, 可以直接用来播放<br>
...
<br>

### Code

#### 1. Open Input

首先需要打开文件, 读取文件的头信息到 AVFormatContext
```c++
AVFormatContext* format_context = nullptr;
int response = avformat_open_input(out format_context, filename, 
                        /*AVInputFormat*/nullptr, /*AVDictionary*/nullptr);
```
`av_format_open_input` 除了接受 `AVFormatContext` 和 `String filename` 作为参数之外, 还有两个经常被置为空的参数:<br>
`AVInputFormat`: 如果非空, 在 open_input 的时候会强制指定 format_context 的 iformat 值为这个. 如果为空, ffmpeg 会自己判断(猜测) iformat 的值.
`AVDictionary` : 设置一些解码的时候的参数.
<br>
当文件被 open_input 打开之后, 就可以输出一些文件的封装信息了. 比如:<br>
封装格式: `format_context->iformat->long_name`<br>
时长: `format_context->duration`<br>
比特率: `format_context->bit_rate`

#### 2. Find Stream Info

需要注意的是, open input 只会加载一些基本的头信息到 AVFormatContext 上, 这个时候并没有 stream 的信息. 

看上面的图, 我们需要拿到 stream 才能接着解析 AVPacket 最终得到 AVFrame. 有了无压缩的 AVFrame 才可以将 AVFrame 的信息输出给图片.

```c++
response = avformat_find_stream_info(format_context, nullptr);
```

一旦这个操作执行成功, format_context 中的两个值会被设置好: `nb_streams` `streams`

```c++
int number_of_streams = format_context->nb_streams; // nb 就是 number 的意思

for (int i = 0; i < number_of_streams; i++){
    AVStream* stream = format_context->streams[i];
}
```

当前的 Stream 可能是 VIDEO 或者 AUDIO 类型(也可能是字幕等类型, 这里不予考虑). 想要知道类型就需要一个结构体 `AVCodecParameters` 或者 `AVCodecContext`. 使用 AVCodecContext 的时候会被警告 stream->codec 已经被遗弃了. `AVCodecParameters` 的功能更单一, 只用来获取当前 Stream 的参数(Parameters).

```c++
for(...){
    ...;
    AVCodecParaneters parameters = stream->codecpar;
}
    
```

到这里已经可以获取 stream 的大部分信息了

1. stream->time_base

   > 对于一个视频播放器来讲, 我们需要在正确的时间上播放正确的帧. 不然的话视频播放的就会过快或者过慢.
   >
   > 为了控制视频播放的速度,  我们需要一个显示时间戳 (presentation timestamp|PTS). 

   

   > 显示时间戳PTS是一个自增的值, 自增的量为 timescale/fps. 
   >
   > fps 指代每秒的帧数 frame pre second, 理解为 frame_count/second
   >
   > 那么 pts 就是 (timescale * second) / frame_count, 将 timescale 按照字面意思理解成时间缩放的话, 是指每一帧对应的**缩放后**的每段时间的量(有点绕)

   按照我学习的项目中给出的例子:

   假设 FPS=60, timebase=1/60000. 那么 pts = timescale/fps = 1000 (可以看到 timescale 和 timebase 互为倒数). 再假设 pts 从0 开始计数.

   | FRAME(帧) | PTS (时间缩放后每一帧的时间) | PTS_TIME (正常时间下每一帧的时间) |
   | --------- | ---------------------------- | --------------------------------- |
   | 0         | 0                            | 0                                 |
   | 1         | 1000                         | PTS * timebase = 0.016            |
   | 2         | 2000                         | PTS * timebase = 0.033            |

   

   如果假设 timebase = 1/60, 那么 pts = 1

   | FRAME (帧) | PTS (时间缩放后每一帧的时间) | PTS_TIME (正常时间下每一帧的时间) |
   | ---------- | ---------------------------- | --------------------------------- |
   | 0          | 0                            | 0                                 |
   | 1          | 1                            | PTS * timebase = 0.016            |
   | 2          | 2                            | PTS * timebase = 0.033            |

   \* 正常时间下每一帧的时间只和 fps 有关系.

   

   如果假设 fps=25. timebase=1/75. PTS计算得出值为3

   | FRAME(帧) | PTS(时间缩放后每一帧的时间) | PTS_TIME(正常时间下每一帧的时间) |
   | --------- | --------------------------- | -------------------------------- |
   | 0         | 0                           | 0                                |
   | 1         | 3                           | 0.04                             |
   | 2         | 6                           | 0.08                             |
   | ...       |                             |                                  |
   | 24        | 72                          | 0.96                             |
   | ...       |                             |                                  |
   | 4064      | 12192                       | 162.56                           |

   通过这种方式就可以使用 pts 完成音视频同步,

   **为什么有 timebase? **

   根据上面的结果来看, 似乎 pts_time 只和 fps 有关系, pts 并不在音视频同步上起到作用, 那么为什么还需要中间插一脚 timebase? 其实是对不同的封装格式来说, timebase 是不一样的. 另外在转码的过程中, 不同的数据状态对应的时间基也不同. 

   拿 mpegts 格式的视频举例: 非压缩状态下 AVFrame 对应 AVCodecContext 的 time_base 是 AVRational{1,25}; 压缩状态下 AVStream 对应的 time_base 是 {1, 90000}.

2. stream->r_frame_rate

   > 流实际的 framerate. 就是 FPS

3. stream->start_time

   > 该流中第一个帧的时间, 基于 timebase.
   >
   > 如果值为 AV_NOPTS_VALUE 的话, 就说明这个值是 undefind

4. stream->duration

   >该流的时间长度, 基于 timebase

5. 视频的宽高 parameters->width, parameters->height

6. 音频的 channel 和 sample_rate. parameters->channel, parameters->sample_rate

   >channel 表示是第几个传输的音频通道??
   >
   >sample_rate 表示音频的 fps

   

#### 3. open codec

本例要输出一张黑白图片, 所以在循环体外部保存好关于视频的 avStream 和 parameters 之后就可以打开解码器进行解码了.

```c++
AVCodecContext *codec_context = avcodec_alloc_context(video_codec);
response = avcodec_parameters_to_context(codec_context, video_codec_parameters);

response = avcodec_open2(codec_context, video_codec, nullptr);
```



#### 4. alloc frame and packet

打开解码器之后, 还需要申请内存给 frame 和 packet, ffmpeg 也提供好了相应的 API

```c++
AVFrame *frame = av_frame_alloc();
AVPacket *packet = av_packet_alloc();
```



#### 5. decode

这个时候开始解码, 并将获取到的 frame 转换成图片文件就好了

```c++
static void save_gray_image(uint8_t* buf, int wrap, int x_size, int y_size, const char* filename);
static void decode_packet(AVPacket* packet, AVCodecContext* decoder_context, AVFrame* frame);
```

```c++
while (av_read_frame(format_context, packet) > 0) {
    if (packet->stream_index != video_stream_index){
        av_packet_unref(packet);
        continue;
    }
    
    // video stream
    logging("pts: %" PRId64, packet->pts);
    // decode_packet 的自定义的函数
    response = decode_packet(packet, codec_context, frame);
    if (response < 0) {
        av_packet_unref(packet);
        break;
    }
    
    av_packet_unref(packet);
}
```

```c++
static void decode_packet(AVPacket* packet, AVCodecCOntext* decoder_context, AVFrame* frame) {
    int response = avcodec_send_packet(codec_context, packet);
    if (reponse < 0 && reponse != AVERROR(EAGAIN)){
        logging("ERROR while sending a packet to the decoder");
        return response;
    }
    
    if (response == AVERROR(EAGAIN)) {
        // 这个值的意思是, 上一次发过了 packet 但是还没有被 receive_frame 接收到.
        // 请求发送的 packet 会在下次 receive_frame 之后发送. 其实是一种正常的现象
        response = 0;
    }
    
    while (reponse > 0) {
        response = avcodec_receive_frame(codec_context, frame);
        if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
            break;
        }
        
        if (response < 0) {
            logging ("ERROR while receiving a frame from the decoder");
            return response;
        }
        
        // 成功
        char frame_filename[1024];
        snprinf(frame_filename, sizeof(frame_filename),
               "%s-%d.pgm", "frame", codec_context->frame_numbere);
        
        save_gray_image(frame->data[0], frame->linesize[0],
                       frame->width, frame->height, frame_filename);
    }
}

static void save_gray_image(uint8_t *buf, int wrap, int x_size, int y_size, const char* filename){
    FILE *file;
    int i;
    
    file = fopen(filename, "w");
    fprintf(file, "P5\n%d %d\n%d\n", x_size, y_size, 255);
    
    for (int i = 0; i < y_size; i ++) {
        fwrite(buf+i*wrap, 1, x_size, file);
    }
    
    fclose(file);
}
```

代码需要一些解释:

while 循环的内容应该不需要解释, 就是读取到一个个的 packet, 然后在不出错的情况下进行 decoed_packet 操作.

decode_packet 现在使用的都是 avcodec_send_packet 和 avcodec_receive_frame 两个函数来完成, 从函数的名称来看, send_packet 是将 packet 发给某个存储区, receive_frame 是从这个存储区拿到 frame.

**AVFrame-data**: 指向解码后的未压缩数据;

​	|- 对于视频来说 data 指向 RGB 数据或者 YUV 数据. 而对于音频来说是指向 PCM 数据.

​	|- 对于 pakcet 的数据 (如 RGB24), data 会将数据统一打包进 data[0]. 对于 planar 的数据, 会将 YUV 分别打包进 data[0] data[1] data[2].

**AVFrame-linesize**: 对应 data 中一行数据的长度.

最后使用 fprintf 写文件的时候, 头文件的信息可以在这里查到 [PGM](http://netpbm.sourceforge.net/doc/pgm.html)


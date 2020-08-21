## 完成文件格式转换的功能

参考: [ffmpeg-libav-tutorial](https://github.com/leandromoreira/ffmpeg-libav-tutorial#chapter-2---remuxing)

相当于使用了

> ffmpeg input.mp4 -c copy output.ts

虽然文件直接右键重命名就可以更改格式, 但是那种更改并不改变文件原有的封装格式. 例如将 input.mp4 右键改名为 output.ts 的话, 在视频播放器里仍然看到封装格式为 mpeg4. 用 ffmpeg 可以将封装格式改到 mpeg-ts.



### 思路

目标: 改变文件的封装格式, 而不需要改变视频音频的编码. 

**- 首先要打开输入文件, 并且查找到输入文件的流信息**

**- 第二步要申请输出文件的 format_context. 如果要写输出文件, FFmpeg 也提供了 avio api 来解决**

**- 需要从输入文件的流中筛选出来需要使用的流, 并且确定好在输出文件中流的 index.**

**- 最后通过 av_read_frame 从输入文件读取数据, 通过 av_..._write 之类的 api 将数据写入输出文件**



### Code 

要完成这样的功能, 首先需要准备两个 `AVFormatContext` 分别代表输入和输出的 format.

```c++
AVFormatContext* input_format = nullptr;
AVFormatContext* output_format = nullptr;
```

对于输入文件, 我们还是照常获取文件的流信息.

```c++
avformat_open_input(out input_format, filename, nullptr, nullptr);
avformat_find_stream_info(input_format, nullptr);
```

此时可以遍历 `input_format` 下面的 `streams` 来获取 stream 信息了. 不过在此之前, 要先准备一下给 `output_format` 以及需要用的数据.

```c++
avformat_alloc_output_context2(out output_context, nullptr, nullptr, output_filename);

int stream_index = 0;
// 使用 stream_index_list 来记录 output 文件中 stream 的 stream_index
int* stream_index_list = av_mallocz_array(number_of_input_streams, sizeof(*stream_index_list));
```

准备好 input 和 output 需要用的数据之后, 就可以像之前一样遍历 streams 了.

```c++
// 循环 streams 
for (int i = 0; i < number_of_input_streams, i++){
    AVStream* src = input_format->streams[i];
    AVCodecParameters* parameters = src->codecpar;
    
    if (parameters->codec_type != AVMEDIA_AUDIO && 
        parameters->codec_type != AVMEDIA_VIDEO && 
        parameters->codec_type != AVMEDIA_SUBTITLE) {
        // 将这个值置为 -1, 这样在拷贝流的时候, 就可以有意忽略 audio video subtitle 之外的流了
        stream_index_list[i] = -1;
        continue;
    }
    
    stream_index_list[i] = stream_index ++;
	AVStream dest = avformat_new_stream(output_format, /*AVCodec*/null);
    avcodec_parameters_copy(dest->codecpar, src->codecpar);
}
```

此时我们的output_format已经按照input_format中的流信息自己预备好了流信息. 

使用AVIO提供的API创建输出文件.

```c++
int no_file = output_format_context->oformat->flags & AVFMT_NOFILE;
if(!no_file){
    // 这里很奇怪, 因为不管我是否删除掉输出文件, 都会走到这个 if 里面
    // 暂时也没有百度到 oformat->flags 在什么情况下会不被设置 NOFILE
    avio_open(out output_format->pb, output_filename, AVIO_FLAG_WRITE);
}
```

使用 `avio_open`打开文件之后, 就可以写文件了.  写文件需要用到三个 api

`avformat_write_header(output_format, /*option*/nullptr)`

`av_interleaved_write_frame(output_format, packet)` interleaved 是/交错的/的意思, 不知道为什么不是 avformat 了.

`avformat_write_trailer(output_format)`

可以猜到, 一组 write header 和 write trailer 之间会有多个 write frame.

```c++
avformat_write_header (format_context, nullptr);

while (true) {
    AVStream *src = nullptr;
    AVStream *desc = nullptr;
    
    av_read_frame(input_format, packet);
    if (packet->stream_index > number_of_input_streams){
        av_packet_unref(packet);
        continue;
    }
    
    if (stream_index_list[packet->stream_index] < 0){
        // 上一个循环我们排除的非 video audio 和 subtitle 的其它 stream
        av_packet_unref(packet);
        continue;
    }
    
    src = input_format[packet->stream_index];
    packet->stream_index = stream_index_list[packet->stream_index];
    desc = outout_format[packet->stream_index];
    
    // pts 在上一个项目中都讲过是显示时间戳, dts 是一个类似的概念, 视频在压缩的过程中, 有的帧信息
    // 是需要前后不同的帧的信息来作为补充的, 所以帧传输的顺序会不一样, dts==>解码时间戳. 在解码的时候
    // 使用这个时间戳来判断每个帧在什么时间段可以被解码出来.
    packet->dts = av_rescale_q_rnd(packet->pts, src->time_base, desc->time_base, 
                                  AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
    packet->pts = av_rescale_q_rnd(packet->dts, src->time_base, desc->time_base,
                                  AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
    packet->duration = av_rescale_q(packet->dts, src->time_base, desc->time_base);
    packet->pos = -1;
    
    av_interleaved_write_frame(output_format, packet);
    av_packet_unref(packet);
}

avformat_write_trailer(format_context);
```

这里有几个不常用的 API, 其中写文件的API在前面提过了. 主要解释一下 `av_rescale_q` 这个 API.

从上节可以知道, 不同的 time_base 会影响 pts dts duration 这些变量的值. 所以当进行 stream 之间的转换的时候要将packet的这些数据转换一下. 使用 `av_rescale_q ` 这个 API 可以将对应的变量从一个时基转换到另一个时基. 其中 `av_rescale_q_rnd` 多接受一个 `enum AVRounding` 参数. AVRounding 控制舍入的方法(比如四舍五入).

```c++
/**
 * Rounding methods.
 * 舍入方法
 */
enum AVRounding {
    AV_ROUND_ZERO     = 0, ///< Round toward zero. 向 0 舍入. 
    AV_ROUND_INF      = 1, ///< Round away from zero.  向 非0 舍入
    AV_ROUND_DOWN     = 2, ///< Round toward -infinity. 向下舍入
    AV_ROUND_UP       = 3, ///< Round toward +infinity. 向上舍入
    AV_ROUND_NEAR_INF = 5, ///< Round to nearest and halfway cases away from zero. 四舍五入
    
    /**
     * Flag telling rescaling functions to pass `INT64_MIN`/`MAX` through
     * unchanged, avoiding special cases for #AV_NOPTS_VALUE.
     * 这个值告诉 rescaling functions 放弃舍入 `INT64_MIN`或者`...MAX`, 直接保持原值不变.
     * 避免出现 AV_NOPTS_VALUE 的特殊情况
     *
     * Unlike other values of the enumeration AVRounding, this value is a
     * bitmask that must be used in conjunction with another value of the
     * enumeration through a bitwise OR, in order to set behavior for normal
     * cases.
     * 不像其它的值, 这个值正常工作的话, 是需要和其它值进行位或运算
     *
     * @code{.c}
     * av_rescale_rnd(3, 1, 2, AV_ROUND_UP | AV_ROUND_PASS_MINMAX);
     * // Rescaling 3:
     * //     Calculating 3 * 1 / 2
     * //     3 / 2 is rounded up to 2
     * //     => 2
     *
     * av_rescale_rnd(AV_NOPTS_VALUE, 1, 2, AV_ROUND_UP | AV_ROUND_PASS_MINMAX);
     * // Rescaling AV_NOPTS_VALUE:
     * //     AV_NOPTS_VALUE == INT64_MIN
     * //     AV_NOPTS_VALUE is passed through
     * //     => AV_NOPTS_VALUE
     * @endcode
     */
    AV_ROUND_PASS_MINMAX = 8192,
};
```

代码中使用的 flag 是 `AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX `, 表示在缩放的时候, 使用向无穷的方向舍入, 并且要避免 AV_NOPTS_VALUE 的情况.



**最后**

在代码中出现了一句代码

```c++
/**
 * 教程文件参考了:
 * https://developer.mozilla.org/en-US/docs/Web/API/Media_Source_Extensions_API/Transcoding_assets_for_MSE
 */
    AVDictionary *options = nullptr;

    // fragment mp4 options
    av_dict_set(out options, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);

```

我参考了 [ffmpeg 的文档](https://ffmpeg.org/ffmpeg-formats.html)

`frag_keyframe`: 在每个关键帧的地方插入一个关键帧

`empty_moov`:  在文件的开头写一个初始的 moov 原子(moov atom), 不描述其中任何的样本. ( 不知道起什么作用的 )

`default_base_moof`:  与 `omit_tfhd_offset` 类似, 这个标志不会在 tfhd 原子中写入绝对的 `base_data_offset`, 而是使用新的 default-base-is-moof 这个 flag 来替换它. 

还需要一段时间来理解.

**2020年8月21日 理解**

 - 视频文件需要切片主要是方便网络传输.

 - 文件被分割成小片段, 不管是在直播还是点播都用的上.

 - 如果直接在网络上传输整个 mp4 文件而不是切片的话, 视频网站需要持续下载一整个视频文件.

 - 切片一个常用的文件是 M3U8 格式的文件.

 - M3U8 是 M3U 格式的文件在 UTF8 编码下的实现.

 - M3U8 使用 TS 文件作为传输的视频流.

 - M3U8 文件只记录了切片的地址, 本身没有视频内容.

 - M3U8 使用 TS 不使用 mp4 文件的原因是 mp4 文件的关键帧过大, 连续播放 mp4 文件的时候会卡.

 - HLS 和 M3U8 有相同的功能, HLS 可以支持 TS 以外的格式.

   <此时仍然没有看懂上面的参数...>



最后释放资源.


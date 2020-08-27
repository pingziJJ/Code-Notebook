## Transcoding

之前的 demo 是将封装格式改变, 不需要做什么转码操作, 读出 packet 然后简单的转一下 timebase 就好了. 

这次的 demo 是要更改文件的编码格式, 是需要将 packet 解码成未压缩的 frame. 最后将 frame 压缩成对应编码的 packet 转换 timebase 之后写入目标文件.



### 代码

> 代码和文件中的代码可能有出入, 是因为我学习的项目中的代码抽取了几个结构体和几个函数. 这里的示意代码并不使用这些结构体

- 首先需要先读取输入文件,  find_stream 并找到视频和音频对应的 stream.

  ```c++
  AVFormatContext* input_format = nullptr;
  avformat_open_input(&input_format, filename, nullptr, nullptr);
  avformat_find_stream_info(input_format, nullptr);
  
  int input_video_index = -1;
  AVStream* input_video_stream = nullptr;
  AVCodec* input_video_codec = nullptr;
  AVCodecContext* input_video_decoder = nullptr;
  
  int input_audio_index = -1;
  AVStream* input_audio_stream = nullptr;
  AVCodec* input_audio_codec = nullptr;
  AVCodecContext* input_audio_decoder = nullptr;
  
  // 准备输入文件的解码器
  for (int i = 0; i < input_format.nb_stream; i++) {
  	AVStream* stream = input_format->streams[i];
      AVCodecParameters* parameters = stream->codecpar;
      
      switch(parameters.codec_type) {
          case AVMEDIA_TYPE_VIDEO:
              input_video_index = stream->index;
              input_video_stream = stream;
              input_video_codec = avcodec_find_decoder(parameters->codec_id);
              input_video_decoder = avcodec_alloc_context3(input_video_codec);
              
              avcodec_parameters_to_context(input_video_decoder, parameters);
              avcodec_open2(input_video_decoder, input_video_codec, nullptr);
  			break;
              
          case AVMEDIA_TYPE_AUDIO:
              input_audio_index = stream->index;
              input_audio_stream = stream;
              input_audio_codec = avcodec_find_decoder(parameters->codec_id);
              input_audio_decoder = avcodec_alloc_context3(input_audio_codec);
              
              avcodec_parameters_to_context(input_audio_decoder, parameters);
              avcodec_open2(input_audio_decoder, input_audio_codec, nullptr);
  			break;
              
          default:
              info("skepping streams other than video and audio.");
              break;
      }
  }
  ```

  

- 在输出文件中使用 `avformat_new_stream` 创建好输出用的 stream. 并且要对 stream 设置好编码的参数.

  又分成两种情况, copy_codec 和不 copy_codec

  ```c++
  if (copy_video){
      output_video_stream = avformat_new_stream(output_format, nullptr);
      avcodec_parameters_copy(output_video_stream.codec_par, input_video_parameters);
  } else {...}
  if(copy_audio){
      output_audio_stream = avformat_new_stream(output_format, nullptr);
      avcodec_parametres_copy(output_audio_stream.codec_par, input_audio_paramters);
  }else{...}
  ```

而对于不 copy 编码的情况, 就是 transcoding 的情况, 又细分为以下几步:

- new stream

  ```c++
  if (copy_video) {
      ...
  } else {
      output_video_stream = avformat_new_stream(output_format, nullptr);
  }
  ```

  

- 使用 `avcodec_find_encoder_by_name` 根据编码的名称查找对应的 codec

  ```c++
  ...;
  output_video_codec = avcodec_find_encoder_by_name(video_codec);
  ```

- 使用 `avcodec_alloc_context3` 来根据 codec 创建 codec_context

  ```c++
  output_video_encoder = avcodec_alloc_context3(output_video_codec);
  ```

- 设置编码的一些基础信息 ※

  **对于视频**

  ```c++
  // 参见: https://trac.ffmpeg.org/wiki/Encode/H.264
  // preset 参数设置的是压缩速度, 速度越慢提供的压缩就越好 (压缩指的是视频质量和体积的比值)
  av_opt_set(output_video_encoder, "preset", "fast", 0);
  
  if(codec_priv_key != nullptr && codec_priv_value != nullptr){
      // priv_data 应该指的是 private data. 并不知道里面要放入什么内容.
      // 根据 example 里面的做法, 似乎将 preset 也可以放入 priv_data
      // example: https://ffmpeg.org/doxygen/3.2/decoding_encoding_8c-example.html#a76
      av_opt_set(output_video_decoder->priv_data, codec_priv_key, codec_priv_data, 0);
  }
  
  output_video_encoder.width = input_video_decoder.width;
  output_video_encoder.height = input_video_decodeer.height;
  
  output_video_encoder.sample_aspect_ratio = input_video_decoder.sample_aspect_ratio; // 长宽比,不知道sample aspect ratio 中的 sample 代表什么...
  
  // pix_fmt 是 YUV420 一类的东西
  if (output_video_codec.pix_fmts != nullptr) {
      // AVCodec 的 pix_fmts 是当前的 codec 支持的 pixel formats 数组
      output_video_encoder->pix_fmt = output_video_codec->pix_fmts[0];
  } else {
      output_video_encoder->pix_fmt = input_video_decoder->pix_fmt;
  }
  
  // 神秘数字
  // bit rate 指平均比特率, 指单位时间内传输或处理的比特数量. bit/s
  output_video_encoder.bit_rate = 2 * 1000 * 1000;
  // 解码器的 byte stream 缓冲区大小? 做什么用啊
  output_video_encoder.rc_buffer_size = 4 * 1000 * 1000;
  // 最大和最小的动态比特率
  output_video_encoder.rc_max_rate = 2 * 1000 * 1000;
  output_video_encoder.rc_min_rate = 2.5 * 1000 * 1000;
  
  output_video_encoder->time_base = av_inv_q(av_guess_frame_rate(input_format, input_video_stream));
  output_video_stream->time_base = output_video_encoder.time_base;
  
  avcodec_open2(output_video_encoder, output_video_codec, nullptr);
  avcodec_parameters_from_context(output_video_stream.codecpar, output_video_encoder).
  ```

  参考了 [what-is-video-bitrate](https://filmora.wondershare.com/video-editing-tips/what-is-video-bitrate.html) 之后, 大概明白 bit_rate 指的是比特率, max_rate 和 min_rate 指的是最大或最小的动态比特率. 比特率越大视频的体积和质量越大, 但是过大的比特率也就只会造成空间浪费而已. 并且网站中给出了一些推荐的 bitrate 的值, 还有一个计算器.

  `av_inv_q` 是给一个值去倒数的函数, 这里给 输出文件的 time_base 设置为帧率的倒数, 那么 pts 应该就为 1?

  最后 open codec 并将最终的 context 输出给 video stream 的 codecpar 结束.

  **对于音频**

  只有设置参数的区别

  ```c++
  // ...
  int OUTPUT_CHANNELS = 2;
  int OUTPUT_BIT_RATE = 196000;
  
  // 双声道?
  output_audio_encoder.channels = OUTPUT_CHANNELS;
  output_audio_encoder.channel_layout = av_get_default_channel_layout(OUTPUT_CHANNELS);
  
  // 音频采样率, 采样率越高音频越自然
  output_audio_encoder.smaple_rate = input_audio_decoder.sample_rate;
  output_audio_encoder.sample_fmt = output_audio_codec->sample_fmts[0];
  
  output_audio_encoder.bit_rate = OUTPUT_BIT_RATE;
  
  output_audio_encoder.time_base = AVRational{1, input_audio_decoder.sample_rate};
  output_audio_stream.time_base = output_audio_encoder = time_base;
  
  output_audio_encoder.strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
  
  // open, parameters from context
  ```

- 此时完成了一大步, 我们准备好了输出文件的编码器, 并且设置好了编码的参数. 接下来将未压缩的 frame 解码出来,交给编码器编码就完成了最后重要的一步.

- ```c++
  // 这个 GLOBALHEADER 看不懂是在做什么. 注释说如果 format 需要 GLOBALHEADER 的时候会有这个 flag
  if ((output_context->format_context->oformat->flags & AVFMT_GLOBALHEADER) != 0) {
          output_context->format_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }
  ```

- `avio_open`

  ```c++
      if ((output_context->format_context->oformat->flags & AVFMT_NOFILE) == 0) {
          response = avio_open(&output_context->format_context->pb, output_context->filename, AVIO_FLAG_WRITE);
          if (response < 0) {
              error("cannot open output file");
              ret = response;
              goto end;
          }
      }
  ```

  `avio_open` 是老朋友了, 不需要过多解释

- 这里设置了一个 muxer_opts 是给 write header 用的

  ```c++
      AVDictionary *muxer_opts = nullptr;
      if (params.muxer_opt_key != nullptr && params.muxer_opt_value != nullptr) {
          av_dict_set(&muxer_opts, params.muxer_opt_key, params.muxer_opt_value, 0);
      }
  ```

- 循环读取 packet.

  ```c++
  avformat_write_header(output_format, &muxer_opts);
  
  while(av_read_frame(input_format, packet) >= 0) {
      int stream_index = packet.stream_index;
      AVMediaType type = input_streams[stream_index].codecpar.codec_type;
      
      if (current_type == AVMEDIA_TYPE_AUDIO) {
          if (copy_audio) {
              av_packet_rescale_ts(packet, input_timebase, output_timebase);
              av_interleaved_write_frame(output_format, packet);
          } else {
              AVCodecContext* decoder = input_audio_decoder;
              avcodec_send_packet(decoder, packet);
              while(avcodec_receive_frame(decoder, frame) >= 0) {
                  AVPacket* new_packet = av_packet_alloc();
                  AVCodecContext* encoder = output_audio_encoder;
                  avcodec_send_frame(encoder, frame);
                  while(avcodec_receive_packet(encoder, new_packet) >= 0) {
                      new_packet.stream_index = output_video_index;
                      new_packet.duration = av_retional_division(
                          input_video_stream.avg_frame_rate, 
                          output_video_stream.time_base);
                      av_packet_rescale_ts(packet, input_video_stream.timebase,
                                          output_video_stream.timebase);
                      av_interleaved_write_frame(output_format, packet);
                      
                      av_packet_unref(new_packet);
                  }
                  av_frame_unref(frame);
                  av_packet_unref(packet);
              }
          }
      }
      
      // audio 和 video 的操作一样
  }
  
  av_write_trailer(output_format);
  ```

  




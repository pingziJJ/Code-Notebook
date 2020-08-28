cmake_minimum_required(VERSION 3.16)
project(Transcoding)

include_directories("includes")
link_directories("libs")

set(CMAKE_CXX_STANDARD 14)

add_executable(Transcoding main.cpp Logger.cpp Logger.h transcoding_0826.cpp transcoding_0826.h transcoding0828.cpp transcoding0828.h)
target_link_libraries(
        Transcoding
        avcodec
        avdevice
        avfilter
        avformat
        avutil
        postproc
        swresample
        swscale
)
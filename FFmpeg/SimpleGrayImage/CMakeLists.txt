cmake_minimum_required(VERSION 3.16)
project(SimpleGrayImage)

include_directories("includes")
link_directories("libs")

set(CMAKE_CXX_STANDARD 20)


add_executable(SimpleGrayImage main.cpp GrayImage0826.cpp GrayImage0826.h Logger.cpp Logger.h)

target_link_libraries(
        SimpleGrayImage
        avformat
        avcodec
        avdevice
        avfilter
        avutil
        postproc
        swresample
        swscale
)
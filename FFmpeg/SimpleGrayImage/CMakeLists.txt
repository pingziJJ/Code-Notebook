cmake_minimum_required(VERSION 3.16)
project(SimpleGrayImage)

set(CMAKE_CXX_STANDARD 20)

include_directories("includes")
link_directories("libs")

add_executable(SimpleGrayImage main.cpp)

link_libraries(
        SimpleGrayImage
        avcodec
        avdevice
        avfilter
        avformat
        avutil
        postproc
        swresample
        swscale
)
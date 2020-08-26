cmake_minimum_required(VERSION 3.16)
project(Remuxing)

include_directories("includes")
link_directories("libs")

set(CMAKE_CXX_STANDARD 14)

add_executable(Remuxing main.cpp Remuxing0821.cpp Remuxing0821.h logger.cpp logger.h Remuxing0826.cpp Remuxing0826.h)

target_link_libraries(
        Remuxing
        avcodec
        avdevice
        avfilter
        avformat
        avutil
        postproc
        swresample
        swscale
)
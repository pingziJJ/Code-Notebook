//
// Created by PingZi on 2020/8/26.
//

#ifndef SIMPLEGRAYIMAGE_LOGGER_H
#define SIMPLEGRAYIMAGE_LOGGER_H

#endif //SIMPLEGRAYIMAGE_LOGGER_H
#include "stdio.h"

void logging(FILE *output, const char *prefix, const char *suffix, const char *format, va_list args);

void info(const char *format, ...);

void error(const char *format, ...);

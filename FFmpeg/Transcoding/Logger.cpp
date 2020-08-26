//
// Created by PingZi on 2020/8/26.
//

#include "Logger.h"
#include "stdio.h"
#include "rpc.h"

void logging(FILE *file, const char *prefix, const char *suffix, const char *format, va_list args) {
    if (prefix != nullptr) {
        fprintf(file, "%s", prefix);
    }

    vfprintf(file, format, args);

    if (suffix != nullptr) {
        fprintf(file, "%s", suffix);
    }

    fprintf(file, "\n");
}

void info(const char *format, ...) {

    va_list args;
            va_start(args, format);
    logging(stdout, "INFO: ", nullptr, format, args);
            va_end(args);
}

void error(const char *format, ...) {
    va_list args;
            va_start(args, format);
    logging(stderr, "ERROR: ", nullptr, format, args);
            va_end(args);
}
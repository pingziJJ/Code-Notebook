//
// Created by PingZi on 2020/8/26.
//

#include <stdarg.h>
#include "Logger.h"


void logging(FILE *output, const char *prefix, const char *suffix, const char *format, va_list args) {
    if (prefix != nullptr) {
        fprintf(output, "%s", prefix);
    }

    vfprintf(output, format, args);

    if (suffix != nullptr) {
        fprintf(output, "%s", suffix);
    }

    fprintf(output, "\n");
}

void info(const char *format, ...) {
    FILE *output = stdout;
    va_list args;
            va_start(args, format);
    logging(output, "INFO: ", nullptr, format, args);
            va_end(args);
}

void error(const char *format, ...) {
    FILE *output = stderr;
    va_list args;
            va_start(args, format);
    logging(output, "ERROR: ", nullptr, format, args);
            va_end(args);
}

//
// Created by PingZi on 2020/8/21.
//

#include <cstdio>
#include <rpc.h>
#include "logger.h"

static void logging(FILE *file, const char *prefix, const char *suffix, const char *format, va_list list) {
    if (prefix != nullptr) {
        fprintf(file, "%s", prefix);
    }

    vfprintf(file, format, list);

    if (suffix != nullptr) {
        fprintf(file, "%s", suffix);
    }

    fprintf(file, "\n");
}

void info(const char *format, ...) {

    va_list list;
            va_start(list, format);

    logging(stdout, "INFO: ", nullptr, format, list);
            va_end(list);
}

void error(const char *format, ...) {
    va_list list;
    va_start(list, format);

    logging (stderr, "ERROR: ", nullptr, format, list);

    va_end(list);
}

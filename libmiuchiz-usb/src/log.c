#include "libmiuchiz-usb.h"
#include "log.h"

#include <stdio.h>
#include <stdarg.h>

// Off by default: the library prints nothing unless the consumer opts in.
static int logging_enabled = 0;

void miuchiz_set_logging(int enabled) {
    logging_enabled = enabled;
}

void miuchiz_log(const char* fmt, ...) {
    if (!logging_enabled) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

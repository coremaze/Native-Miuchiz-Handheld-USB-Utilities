#ifndef MIUCHIZ_LIBMIUCHIZ_LOG_H
#define MIUCHIZ_LIBMIUCHIZ_LOG_H

// Internal diagnostic logging. Output is suppressed unless the consumer turns
// it on with miuchiz_set_logging() (declared in libmiuchiz-usb.h). When logging
// is disabled, miuchiz_log() is a cheap no-op, so call sites need no guard.
void miuchiz_log(const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

#endif

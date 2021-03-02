#ifndef MIUCHIZ_LIBMIUCHIZ_TIMER_H
#define MIUCHIZ_LIBMIUCHIZ_TIMER_H

#include <time.h>
#include <inttypes.h>

#if defined(unix) || defined(__unix__) || defined(__unix)
    #define utimer_time_t struct timespec
#elif defined(_WIN32)
    #define utimer_time_t struct timeval
#endif

struct Utimer {
    utimer_time_t start_time;
    utimer_time_t end_time;
};

void miuchiz_utimer_start(struct Utimer* t);
void miuchiz_utimer_end(struct Utimer* t);
uint64_t miuchiz_utimer_elapsed(struct Utimer* t);

#endif
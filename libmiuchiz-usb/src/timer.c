#include "timer.h"

void miuchiz_utimer_start(struct Utimer* t) {
    #if defined(unix) || defined(__unix__) || defined(__unix)
        clock_gettime(CLOCK_MONOTONIC, &t->start_time);
    #elif defined(_WIN32)
        mingw_gettimeofday(&t->start_time, NULL);
    #endif
}

void miuchiz_utimer_end(struct Utimer* t) {
    #if defined(unix) || defined(__unix__) || defined(__unix)
        clock_gettime(CLOCK_MONOTONIC, &t->end_time);
    #elif defined(_WIN32)
        mingw_gettimeofday(&t->end_time, NULL);
    #endif
}

uint64_t miuchiz_utimer_elapsed(struct Utimer* t) {
    uint64_t start_utime = t->start_time.tv_sec  * 1000000LL /*1000000 usec per sec*/
    #if defined(unix) || defined(__unix__) || defined(__unix)
                         + t->start_time.tv_nsec   / 1000LL;    /*1000 nsec per usec*/
    #elif defined(_WIN32)
                         + t->start_time.tv_usec;
    #endif

    uint64_t end_utime   = t->end_time.tv_sec  * 1000000LL  /*1000000 usec per sec*/
    #if defined(unix) || defined(__unix__) || defined(__unix)
                         + t->end_time.tv_nsec / 1000LL;    /*1000 nsec per usec*/
    #elif defined(_WIN32)
                         + t->end_time.tv_usec;
    #endif

    return end_utime - start_utime;
}
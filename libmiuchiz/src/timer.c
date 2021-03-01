#include "timer.h"

void miuchiz_utimer_start(struct Utimer* t) {
   clock_gettime(CLOCK_MONOTONIC, &t->start_time);
}

void miuchiz_utimer_end(struct Utimer* t) {
    clock_gettime(CLOCK_MONOTONIC, &t->end_time);
}

uint64_t miuchiz_utimer_elapsed(struct Utimer* t) {
    uint64_t start_utime = t->start_time.tv_sec  * 1000000 /*1000000 usec per sec*/
                         + t->start_time.tv_nsec / 1000;   /*1000 nsec per usec*/

    uint64_t end_utime   = t->end_time.tv_sec  * 1000000  /*1000000 usec per sec*/
                         + t->end_time.tv_nsec / 1000;    /*1000 nsec per usec*/

    return end_utime - start_utime;
}
#ifndef MIUCHIZ_LIBMIUCHIZ_TIMER_H
#define MIUCHIZ_LIBMIUCHIZ_TIMER_H

#include <inttypes.h>

struct Utimer {
    uint64_t start_time;
    uint64_t end_time;
};

void miuchiz_utimer_start(struct Utimer* t);
void miuchiz_utimer_end(struct Utimer* t);
uint64_t miuchiz_utimer_elapsed(struct Utimer* t);

#endif

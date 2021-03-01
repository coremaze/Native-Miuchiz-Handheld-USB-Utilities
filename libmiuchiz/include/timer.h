#include <time.h>
#include <inttypes.h>

struct Utimer {
    struct timespec start_time;
    struct timespec end_time;
};

void miuchiz_utimer_start(struct Utimer* t);
void miuchiz_utimer_end(struct Utimer* t);
uint64_t miuchiz_utimer_elapsed(struct Utimer* t);
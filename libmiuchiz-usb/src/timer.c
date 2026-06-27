#include "timer.h"

#if defined(_WIN32)
    #include <windows.h>

    static uint64_t miuchiz_utimer_now(void) {
        LARGE_INTEGER counter;
        QueryPerformanceCounter(&counter);
        return (uint64_t)counter.QuadPart;
    }

    /* The performance-counter frequency is fixed for the system's boot
     * lifetime, so query it once and cache it. Returns 0 if the platform has no
     * high-resolution counter (so callers can avoid dividing by it). */
    static uint64_t miuchiz_utimer_ticks_per_sec(void) {
        static uint64_t cached = 0;
        if (cached == 0) {
            LARGE_INTEGER freq;
            if (QueryPerformanceFrequency(&freq) && freq.QuadPart > 0) {
                cached = (uint64_t)freq.QuadPart;
            }
        }
        return cached;
    }
#else
    #include <time.h>

    static uint64_t miuchiz_utimer_now(void) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    }
#endif

void miuchiz_utimer_start(struct Utimer* t) {
    t->start_time = miuchiz_utimer_now();
}

void miuchiz_utimer_end(struct Utimer* t) {
    t->end_time = miuchiz_utimer_now();
}

uint64_t miuchiz_utimer_elapsed(struct Utimer* t) {
    uint64_t elapsed_ticks = t->end_time - t->start_time;
#if defined(_WIN32)
    // Convert QueryPerformanceCounter ticks to microseconds.
    uint64_t freq = miuchiz_utimer_ticks_per_sec();
    if (freq == 0) {
        return 0;
    }
    uint64_t whole_seconds = elapsed_ticks / freq;
    uint64_t remainder_ticks = elapsed_ticks % freq;
    return whole_seconds * 1000000ULL + (remainder_ticks * 1000000ULL) / freq;
#else
    /* clock_gettime gives nanoseconds; convert to microseconds. */
    return elapsed_ticks / 1000ULL;
#endif
}

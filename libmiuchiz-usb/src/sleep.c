#if defined(_WIN32)
    #include <windows.h>
#else
    #include <time.h>
    #include <errno.h>
#endif

void miuchiz_sleep_ms(unsigned int ms) {
#if defined(_WIN32)
    Sleep(ms);
#else
    struct timespec req;
    req.tv_sec = ms / 1000u;
    req.tv_nsec = (long)(ms % 1000u) * 1000000L;
    /* Resume across signal interruptions so we sleep the full duration. */
    while (nanosleep(&req, &req) == -1 && errno == EINTR) {
    }
#endif
}
#include "libmiuchiz-usb.h"

// Native Linux backend: talks to the handheld as a raw SCSI block device under
// /dev/sd*. Not compiled when the libusb backend is selected.
#if (defined(unix) || defined(__unix__) || defined(__unix)) && !defined(MIUCHIZ_USE_LIBUSB)

#include "backend.h"
#include "timer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <glob.h>

fp_t miuchiz_backend_open(struct Handheld* handheld) {
    handheld->fd = open(handheld->device, O_RDWR | __O_DIRECT | O_NONBLOCK | O_SYNC);
    return handheld->fd;
}

void miuchiz_backend_close(struct Handheld* handheld) {
    if (handheld->fd != -1) {
        close(handheld->fd);
        handheld->fd = -1;
    }
}

ssize_t miuchiz_backend_read(struct Handheld* handheld, void* buf, size_t n) {
    if (handheld->fd == -1) {
        return -1;
    }
    return read(handheld->fd, buf, n);
}

ssize_t miuchiz_backend_write(struct Handheld* handheld, const void* buf, size_t n) {
    /*
    Writing to the device without giving it enough time will make it stop
    working. Even though write() shouldn't return early due to O_DIRECT,
    the issue still happens.
    */
    if (handheld->fd == -1) {
        return -1;
    }

    struct Utimer timer;
    miuchiz_utimer_start(&timer);

    ssize_t result = write(handheld->fd, buf, n);

    miuchiz_utimer_end(&timer);

    uint64_t usecs_to_sleep = miuchiz_utimer_elapsed(&timer) / 3;
    usleep(usecs_to_sleep);

    return result;
}

off_t miuchiz_backend_seek(struct Handheld* handheld, off_t offset) {
    if (handheld->fd == -1) {
        return -1;
    }
    return lseek(handheld->fd, offset, SEEK_SET);
}

int miuchiz_backend_enumerate(struct Handheld*** handhelds) {
    int handhelds_count = 0;
    *handhelds = NULL;

    // Get all SCSI disks on the system
    glob_t globbuf;
    if (!glob("/dev/sd*", 0, NULL, &globbuf)) {
        // 1 more than the possible maximum number of handhelds is allocated
        // here. This is to ensure that there is at least one NULL at the end.
        size_t handhelds_array_size = (globbuf.gl_pathc + 1) * sizeof(struct Handheld*);
        *handhelds = malloc(handhelds_array_size);
        memset(*handhelds, 0, handhelds_array_size);

        // Find all the disks that are handhelds
        for (size_t i = 0; i < globbuf.gl_pathc; i++) {
            struct Handheld* handheld_candidate = miuchiz_handheld_create(globbuf.gl_pathv[i]);
            if (miuchiz_handheld_is_handheld(handheld_candidate)) {
                (*handhelds)[handhelds_count++] = handheld_candidate;
            }
            else {
                miuchiz_handheld_destroy(handheld_candidate);
            }
        }
        globfree(&globbuf);
    }

    return handhelds_count;
}

void* miuchiz_backend_dma_alloc(size_t size) {
    size_t alignment = (size_t)miuchiz_page_alignment();
    // aligned_alloc requires the size to be a multiple of the alignment.
    size_t alloc_size = miuchiz_round_size_up(size, (int)alignment);
    return aligned_alloc(alignment, alloc_size);
}

void miuchiz_backend_dma_free(void* p) {
    free(p);
}

long miuchiz_backend_page_alignment(void) {
    return sysconf(_SC_PAGESIZE);
}

#endif // native Linux backend

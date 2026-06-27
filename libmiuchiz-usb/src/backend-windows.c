#include "libmiuchiz-usb.h"

// Native Windows backend: talks to the handheld through a \\.\X: volume handle.
// Not compiled when the libusb backend is selected.
#if defined(_WIN32) && !defined(MIUCHIZ_USE_LIBUSB)

#include "backend.h"
#include "timer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <malloc.h>

fp_t miuchiz_backend_open(struct Handheld* handheld) {
    handheld->fd = CreateFileA(handheld->device,
                               GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL,
                               OPEN_EXISTING,
                               FILE_FLAG_NO_BUFFERING | FILE_ATTRIBUTE_NORMAL,
                               NULL);
    return handheld->fd;
}

void miuchiz_backend_close(struct Handheld* handheld) {
    if (handheld->fd != INVALID_HANDLE_VALUE) {
        CloseHandle(handheld->fd);
        handheld->fd = INVALID_HANDLE_VALUE;
    }
}

ssize_t miuchiz_backend_read(struct Handheld* handheld, void* buf, size_t n) {
    if (handheld->fd == INVALID_HANDLE_VALUE) {
        return -1;
    }
    DWORD dresult = 0;
    if (ReadFile(handheld->fd, buf, n, &dresult, NULL) == 0) {
        return -1;
    }
    return dresult;
}

ssize_t miuchiz_backend_write(struct Handheld* handheld, const void* buf, size_t n) {
    /*
    Writing to the device without giving it enough time will make it stop
    working.
    */
    if (handheld->fd == INVALID_HANDLE_VALUE) {
        return -1;
    }

    struct Utimer timer;
    miuchiz_utimer_start(&timer);

    ssize_t result;
    DWORD dresult = 0;
    if (WriteFile(handheld->fd, buf, n, &dresult, NULL) == 0) {
        result = -1;
    }
    else {
        result = dresult;
    }

    miuchiz_utimer_end(&timer);

    // For some reason, Windows 10 needs a lot more time
    int msecs_to_sleep = (miuchiz_utimer_elapsed(&timer) * 0.5) / 1000;
    if (msecs_to_sleep < 1) {
        msecs_to_sleep = 1;
    }
    Sleep(msecs_to_sleep);

    return result;
}

off_t miuchiz_backend_seek(struct Handheld* handheld, off_t offset) {
    if (handheld->fd == INVALID_HANDLE_VALUE) {
        return -1;
    }
    return SetFilePointer(handheld->fd, offset, 0, FILE_BEGIN);
}

int miuchiz_backend_enumerate(struct Handheld*** handhelds) {
    int handhelds_count = 0;
    *handhelds = NULL;

    // Get all the drive letters mounted
    unsigned int mask = GetLogicalDrives();
    char letters[256] = { 0 };
    int letters_count = 0;
    for (int i = 0; mask; i++) {
        if (mask & 1) {
            char letter = 'A' + i;
            letters[letters_count++] = letter;
        }
        mask = mask >> 1;
    }

    // Find all the drive letters that are handhelds.
    // 1 more than the possible maximum number of handhelds is allocated here.
    // This is to ensure that there is at least one NULL at the end.
    size_t handhelds_array_size = (letters_count + 1) * sizeof(struct Handheld*);
    *handhelds = malloc(handhelds_array_size);
    memset(*handhelds, 0, handhelds_array_size);

    for (int i = 0; i < letters_count; i++) {
        char drive[16] = { 0 };

        sprintf(drive, "\\\\.\\%c:", letters[i]);
        struct Handheld* handheld_candidate = miuchiz_handheld_create(drive);
        if (miuchiz_handheld_is_handheld(handheld_candidate)) {
            (*handhelds)[handhelds_count++] = handheld_candidate;
        }
        else {
            miuchiz_handheld_destroy(handheld_candidate);
        }
    }

    return handhelds_count;
}

void* miuchiz_backend_dma_alloc(size_t size) {
    size_t alignment = (size_t)miuchiz_page_alignment();
    return _aligned_malloc(size, alignment);
}

void miuchiz_backend_dma_free(void* p) {
    _aligned_free(p);
}

long miuchiz_backend_page_alignment(void) {
    return 4096;
}

#endif // native Windows backend

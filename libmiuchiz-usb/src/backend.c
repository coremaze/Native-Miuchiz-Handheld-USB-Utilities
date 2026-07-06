#include "libmiuchiz-usb.h"
#include "backend.h"
#include "backend-internal.h"

#include <stdlib.h>
#include <string.h>

/*
 * Per-handle transport dispatch. The platform backend (compiled in at
 * configure time, as before) reaches real hardware; the emulator backend
 * reaches running emiu2 instances over a local socket. A handheld's transport
 * is decided by its device string: "emu:..." is an emulator, anything else
 * belongs to the platform backend.
 */

fp_t miuchiz_backend_open(struct Handheld* handheld) {
    if (miuchiz_emu_is(handheld)) {
        miuchiz_emu_open(handheld);
        return handheld->fd; /* untouched; emulator state lives in ->emu */
    }
    return miuchiz_platform_open(handheld);
}

void miuchiz_backend_close(struct Handheld* handheld) {
    if (miuchiz_emu_is(handheld)) {
        miuchiz_emu_close(handheld);
        return;
    }
    miuchiz_platform_close(handheld);
}

ssize_t miuchiz_backend_read(struct Handheld* handheld, void* buf, size_t n) {
    if (miuchiz_emu_is(handheld)) {
        return miuchiz_emu_read(handheld, buf, n);
    }
    return miuchiz_platform_read(handheld, buf, n);
}

ssize_t miuchiz_backend_write(struct Handheld* handheld, const void* buf, size_t n) {
    if (miuchiz_emu_is(handheld)) {
        return miuchiz_emu_write(handheld, buf, n);
    }
    return miuchiz_platform_write(handheld, buf, n);
}

off_t miuchiz_backend_seek(struct Handheld* handheld, off_t offset) {
    if (miuchiz_emu_is(handheld)) {
        return miuchiz_emu_seek(handheld, offset);
    }
    return miuchiz_platform_seek(handheld, offset);
}

/* The DMA helpers are not per-handle; the platform backend's (stricter)
 * alignment rules satisfy the emulator transport too. */

void* miuchiz_backend_dma_alloc(size_t size) {
    return miuchiz_platform_dma_alloc(size);
}

void miuchiz_backend_dma_free(void* p) {
    miuchiz_platform_dma_free(p);
}

long miuchiz_backend_page_alignment(void) {
    return miuchiz_platform_page_alignment();
}

int miuchiz_backend_enumerate(struct Handheld*** handhelds) {
    *handhelds = NULL;

    struct Handheld** platform_list = NULL;
    int platform_count = miuchiz_platform_enumerate(&platform_list);

    struct Handheld** emu_list = NULL;
    int emu_count = miuchiz_emu_enumerate(&emu_list);

    int real = (platform_count > 0) ? platform_count : 0;
    int emulated = (emu_count > 0) ? emu_count : 0;
    int total = real + emulated;

    if (total == 0) {
        free(platform_list);
        free(emu_list);
        /* Preserve the platform backend's "device present but inaccessible"
         * report (MIUCHIZ_ERROR_ACCESS, with no array) when there is nothing
         * else to show. Otherwise callers get an empty NULL-terminated array,
         * the "searched fine, found none" shape they expect. */
        if (platform_count < 0) {
            return platform_count;
        }
        *handhelds = calloc(1, sizeof(struct Handheld*));
        return 0;
    }

    /* One more than needed so the array is always NULL-terminated. */
    struct Handheld** merged = malloc((total + 1) * sizeof(struct Handheld*));
    memset(merged, 0, (total + 1) * sizeof(struct Handheld*));

    int at = 0;
    for (int i = 0; i < real; i++) {
        merged[at++] = platform_list[i];
    }
    for (int i = 0; i < emulated; i++) {
        merged[at++] = emu_list[i];
    }
    free(platform_list);
    free(emu_list);

    *handhelds = merged;
    return total;
}

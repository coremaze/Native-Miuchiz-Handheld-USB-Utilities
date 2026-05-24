#ifndef MIUCHIZ_LIBMIUCHIZ_BACKEND_H
#define MIUCHIZ_LIBMIUCHIZ_BACKEND_H

#include "libmiuchiz-usb.h"

/*
 * Internal platform backend interface.
 *
 * Exactly one backend is compiled into the library, selected at configure time
 * (see MIUCHIZ_USE_LIBUSB in libmiuchiz-usb.h and CMakeLists.txt):
 *
 *   - backend-libusb.c   libusb transport (required on macOS, optional on Linux)
 *   - backend-linux.c    native /dev/sd* SCSI block device
 *   - backend-windows.c  Win32 \\.\X: volume handle
 *
 * The platform-independent core in libmiuchiz-usb.c talks to a device
 * exclusively through these functions, so it contains no per-OS #ifdefs.
 */

/**
 * Opens handheld->device, storing the opened object in handheld->fd.
 * @return handheld->fd. The fd is left in a "closed" state on failure.
 */
fp_t miuchiz_backend_open(struct Handheld* handheld);

/**
 * Closes the device referenced by handheld->fd.
 */
void miuchiz_backend_close(struct Handheld* handheld);

/**
 * Reads from the position set by the most recent miuchiz_backend_seek.
 * @return Number of bytes read, or a negative value on error.
 */
ssize_t miuchiz_backend_read(struct Handheld* handheld, void* buf, size_t n);

/**
 * Writes at the position set by the most recent miuchiz_backend_seek.
 * @return Number of bytes written, or a negative value on error.
 */
ssize_t miuchiz_backend_write(struct Handheld* handheld, const void* buf, size_t n);

/**
 * Positions the device at byte offset for the next read/write.
 * @return >= 0 on success, negative on error.
 */
off_t miuchiz_backend_seek(struct Handheld* handheld, off_t offset);

/**
 * Discovers every connected handheld candidate on the system.
 * @param handhelds Receives a freshly allocated, NULL-terminated array.
 * @return The number of candidates placed in the array.
 */
int miuchiz_backend_enumerate(struct Handheld*** handhelds);

/**
 * Allocates a buffer suitable for the backend's transfers (e.g. page-aligned
 * for O_DIRECT on Linux). Free with miuchiz_backend_dma_free.
 */
void* miuchiz_backend_dma_alloc(size_t size);

/**
 * Frees a buffer obtained from miuchiz_backend_dma_alloc.
 */
void miuchiz_backend_dma_free(void* p);

/**
 * Returns the memory alignment the backend needs for transfers, before the
 * core clamps it to at least MIUCHIZ_SECTOR_SIZE.
 */
long miuchiz_backend_page_alignment(void);

#endif

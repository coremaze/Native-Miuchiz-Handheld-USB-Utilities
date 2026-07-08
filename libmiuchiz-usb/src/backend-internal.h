#ifndef MIUCHIZ_LIBMIUCHIZ_BACKEND_INTERNAL_H
#define MIUCHIZ_LIBMIUCHIZ_BACKEND_INTERNAL_H

#include "libmiuchiz-usb.h"

/*
 * The two transports behind the miuchiz_backend_* dispatchers (backend.c).
 *
 * miuchiz_platform_*: real hardware, exactly one implementation compiled in
 * (backend-linux.c / backend-windows.c / backend-libusb.c, selected at
 * configure time).
 *
 * miuchiz_emu_*: a running emiu2 emulator instance, reached over a local
 * socket (backend-emu.c, compiled on every platform). Emulator handhelds are
 * recognized by their device string: "emu:" followed by the endpoint file
 * path. They keep their connection state in handheld->emu, leaving
 * handheld->fd (whose type belongs to the platform backend) untouched.
 */

/* --- the platform backend (one of the three per-OS files) ---------------- */

fp_t miuchiz_platform_open(struct Handheld* handheld);
void miuchiz_platform_close(struct Handheld* handheld);
ssize_t miuchiz_platform_read(struct Handheld* handheld, void* buf, size_t n);
ssize_t miuchiz_platform_write(struct Handheld* handheld, const void* buf, size_t n);
off_t miuchiz_platform_seek(struct Handheld* handheld, off_t offset);
int miuchiz_platform_enumerate(struct Handheld*** handhelds);
void* miuchiz_platform_dma_alloc(size_t size);
void miuchiz_platform_dma_free(void* p);
long miuchiz_platform_page_alignment(void);

/* --- the emulator backend (backend-emu.c, always compiled) --------------- */

/** Whether this handheld's device string names an emulator endpoint. */
int miuchiz_emu_is(const struct Handheld* handheld);

/**
 * Resolves `app`'s runtime directory per the shared Miuchiz Reborn
 * storage-location policy (mirrors the miuchiz-reborn-paths crate; verified
 * against its vendored test vectors by the paths-conformance test).
 * @return 0 on success, -1 on failure.
 */
int miuchiz_emu_runtime_dir(const char* app, char* buf, size_t bufn);

/**
 * The directory emulators publish USB endpoints in: emiu2's runtime
 * directory, or the EMIU2_USB_DIR override.
 * @return 0 on success, -1 on failure.
 */
int miuchiz_emu_endpoint_dir(char* buf, size_t bufn);

void miuchiz_emu_open(struct Handheld* handheld);
void miuchiz_emu_close(struct Handheld* handheld);
ssize_t miuchiz_emu_read(struct Handheld* handheld, void* buf, size_t n);
ssize_t miuchiz_emu_write(struct Handheld* handheld, const void* buf, size_t n);
off_t miuchiz_emu_seek(struct Handheld* handheld, off_t offset);

/**
 * Discovers running emulator instances (endpoint files in the emiu2 runtime
 * directory), verifying each candidate like the platform enumerators do.
 * @param handhelds Receives a freshly allocated, NULL-terminated array
 *                  (NULL when none were found).
 * @return The number of verified emulator handhelds.
 */
int miuchiz_emu_enumerate(struct Handheld*** handhelds);

#endif

#include "libmiuchiz-usb.h"

#if defined(MIUCHIZ_USE_LIBUSB)

#include "backend.h"
#include "timer.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SITRONIX_VENDOR (0x1403)
#define SITRONIX_PRODUCT (0x0001)

// Endpoint addresses. Bit 7 of a USB endpoint address is the direction:
// set means IN (device -> host), clear means OUT (host -> device).
#define SITRONIX_ENDPOINT_OUT (0x02) // bulk OUT: host -> device
#define SITRONIX_ENDPOINT_IN  (0x81) // bulk IN:  device -> host

// USB Mass Storage Bulk-Only Transport wrappers.
#define CBW_SIZE (31) // Command Block Wrapper
#define CSW_SIZE (13) // Command Status Wrapper

// check_csw() results.
#define CSW_OK     (0) // command passed (bCSWStatus == 0)
#define CSW_FAIL   (1) // valid CSW, but the command failed (bCSWStatus == 1)
#define CSW_RESYNC (2) // CSW invalid or out of sync (bad signature/tag, or phase error)

// libusb's default context is reference counted; initialise it once, lazily,
// so the backend works whether the caller went through miuchiz_backend_enumerate
// or created a handheld directly by device string.
static void miuchiz_libusb_cleanup(void) {
    libusb_exit(NULL);
}

static void ensure_libusb_init(void) {
    static int initialized = 0;
    if (!initialized) {
        libusb_init(NULL);
        atexit(miuchiz_libusb_cleanup);
        initialized = 1;
    }
}

// Debugging helper, used from the commented-out dumps in the transfer paths.
__attribute__((unused))
static void hexdump(void* buf, size_t n) {
    size_t i = 0;
    unsigned char* data = buf;

    while (i < n) {
        printf("%08zX: ", i);
        for (int j = 0; j < 16; j++) {
            size_t new_i = i + j;
            if (new_i < n) {
                printf("%02X ", data[new_i]);
            }
            else {
                printf("   ");
            }
        }

        for (int j = 0; j < 16; j++) {
            size_t new_i = i + j;
            if (new_i < n) {
                char c = data[new_i];
                if (c >= 0x20) {
                    printf("%c", c);
                }
                else {
                    printf(".");
                }
            }
        }

        i += 16;
        printf("\n");
    }
}

static char* bus_and_address_to_str(uint8_t bus, uint8_t addr) {
    static char result[5];
    sprintf(result, "%02X%02X", bus, addr);
    return result;
}

// Validates a Command Status Wrapper:
//   [0-3]  dCSWSignature  = "USBS"
//   [4-7]  dCSWTag        echoes the CBW tag
//   [8-11] dCSWDataResidue
//   [12]   bCSWStatus     0 = passed, 1 = failed, 2 = phase error
// Returns 0 if the command passed, -1 otherwise.
static int check_csw(const unsigned char* csw, int len, uint32_t expected_tag) {
    if (len != CSW_SIZE) {
        miuchiz_log("libmiuchiz: short CSW (%d bytes, expected %d)\n", len, CSW_SIZE);
        return CSW_RESYNC;
    }
    if (memcmp(csw, "USBS", 4) != 0) {
        miuchiz_log("libmiuchiz: bad CSW signature %02X %02X %02X %02X\n",
                    csw[0], csw[1], csw[2], csw[3]);
        return CSW_RESYNC;
    }
    uint32_t tag = (uint32_t)csw[4] | ((uint32_t)csw[5] << 8) |
                   ((uint32_t)csw[6] << 16) | ((uint32_t)csw[7] << 24);
    if (tag != expected_tag) {
        // The status doesn't belong to the command we just sent, so we're out of
        // sync (e.g. a stale CSW left in the pipe). Resync via reset recovery.
        miuchiz_log("libmiuchiz: CSW tag mismatch (got %08X, expected %08X)\n",
                    tag, expected_tag);
        return CSW_RESYNC;
    }
    unsigned char status = csw[12];
    if (status == 0x02) {
        // Phase error: the BOT spec requires the host to do a reset recovery.
        miuchiz_log("libmiuchiz: CSW phase error\n");
        return CSW_RESYNC;
    }
    if (status != 0x00) {
        miuchiz_log("libmiuchiz: CSW reports command failed (status %u)\n", status);
        return CSW_FAIL;
    }
    return CSW_OK;
}

// Monotonically increasing CBW tag. The device echoes each tag back in the CSW,
// letting us match a status to its command and detect a stale/out-of-sync
// response. The values themselves are arbitrary per the BOT spec.
static uint32_t cbw_tag = 0;

static ssize_t scsi_bulk_write(libusb_device_handle *handle, uint32_t sector, const void *buf, size_t n) {
    write_start:;

    uint32_t tag = ++cbw_tag;
    uint16_t transfer_len = (n + (MIUCHIZ_SECTOR_SIZE-1)) / MIUCHIZ_SECTOR_SIZE;
    unsigned char cbw[CBW_SIZE] = {
        'U', 'S', 'B', 'C',                                                                       // Signature
        (tag >> 0) & 0xFF, (tag >> 8) & 0xFF, (tag >> 16) & 0xFF, (tag >> 24) & 0xFF,             // dCBWTag (little endian)
        (n >> 0) & 0xFF, (n >> 8) & 0xFF, (n >> 16) & 0xFF, (n >> 24) & 0xFF,                     // DataTransferLength (little endian 4 bytes)
        0x00,                                                                                     // Flags (data out)
        0x00,                                                                                     // LUN
        0x0a,                                                                                     // CDB Length
        0x2a,                                                                                     // SCSI SBC Opcode (WRITE(10))
        0x00,                                                                                     // SCSI SBC FUA_NV
        (sector >> 24) & 0xFF, (sector >> 16) & 0xFF, (sector >> 8) & 0xFF, (sector >> 0) & 0xFF, // Logical Block Address (Sector) (big endian)
        0x00,                                                                                     // SCSI SBC Group
        (transfer_len >> 8) & 0xFF, (transfer_len >> 0) & 0xFF,                                   // Transfer Length
        0x00,                                                                                     // Obsolete
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    int err = 0;
    int actual_out_len = 0;

    // Command phase: send the CBW.
    err = libusb_bulk_transfer(handle, SITRONIX_ENDPOINT_OUT, cbw, sizeof(cbw), &actual_out_len, 1000);
    if (err < 0) {
        switch (err) {
            case LIBUSB_ERROR_TIMEOUT:
            case LIBUSB_ERROR_PIPE:
                goto otp_race_bug_recover;
            default:
                return err;
        }
    }

    // Data phase: send the payload out to the device.
    int actual_data_len = 0;
    err = libusb_bulk_transfer(handle, SITRONIX_ENDPOINT_OUT, (unsigned char*)buf, n, &actual_data_len, 1000);
    if (err < 0) {
        switch (err) {
            case LIBUSB_ERROR_TIMEOUT:
            case LIBUSB_ERROR_PIPE:
                goto otp_race_bug_recover;
            default:
                return err;
        }
    }

    // Status phase: read and validate the CSW.
    unsigned char csw[CSW_SIZE] = {0};
    int csw_len = 0;
    err = libusb_bulk_transfer(handle, SITRONIX_ENDPOINT_IN, csw, sizeof(csw), &csw_len, 1000);
    if (err < 0) {
        switch (err) {
            case LIBUSB_ERROR_TIMEOUT:
            case LIBUSB_ERROR_PIPE:
                goto otp_race_bug_recover;
            default:
                return err;
        }
    }
    switch (check_csw(csw, csw_len, tag)) {
        case CSW_OK:
            break;
        case CSW_RESYNC:
            goto otp_race_bug_recover;
        default: // CSW_FAIL
            return -1;
    }

    return ((size_t)actual_data_len > n) ? (ssize_t)n : actual_data_len;

    otp_race_bug_recover:
    miuchiz_log("libmiuchiz: recovering write\n");
    usleep(250000);
    // USB Mass Storage reset recovery: Bulk-Only Mass Storage Reset, then clear
    // the halt condition on each bulk endpoint.
    err = libusb_control_transfer(handle, 0x21, 0xFF, 0, 0, NULL, 0, 1000);
    if (err < 0) {
        miuchiz_log("libmiuchiz: write reset failed (%d)\n", err);
        return err;
    }
    libusb_clear_halt(handle, SITRONIX_ENDPOINT_IN);
    libusb_clear_halt(handle, SITRONIX_ENDPOINT_OUT);
    usleep(250000);
    goto write_start;
}

static ssize_t scsi_bulk_read(libusb_device_handle *handle, uint32_t sector, void *buf, size_t n) {
    read_start:;

    uint32_t tag = ++cbw_tag;
    uint16_t transfer_len = (n + (MIUCHIZ_SECTOR_SIZE-1)) / MIUCHIZ_SECTOR_SIZE;
    unsigned char cbw[CBW_SIZE] = {
        'U', 'S', 'B', 'C',                                                                       // Signature
        (tag >> 0) & 0xFF, (tag >> 8) & 0xFF, (tag >> 16) & 0xFF, (tag >> 24) & 0xFF,             // dCBWTag (little endian)
        (n >> 0) & 0xFF, (n >> 8) & 0xFF, (n >> 16) & 0xFF, (n >> 24) & 0xFF,                     // DataTransferLength (little endian 4 bytes)
        0x80,                                                                                     // Flags (data in)
        0x00,                                                                                     // LUN
        0x0a,                                                                                     // CDB Length
        0x28,                                                                                     // SCSI SBC Opcode (READ(10))
        0x00,                                                                                     // SCSI SBC FUA_NV
        (sector >> 24) & 0xFF, (sector >> 16) & 0xFF, (sector >> 8) & 0xFF, (sector >> 0) & 0xFF, // Logical Block Address (Sector) (big endian)
        0x00,                                                                                     // SCSI SBC Group
        (transfer_len >> 8) & 0xFF, (transfer_len >> 0) & 0xFF,                                   // Transfer Length
        0x00,                                                                                     // Obsolete
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    int err = 0;
    int actual_out_len = 0;

    // Command phase: send the CBW.
    err = libusb_bulk_transfer(handle, SITRONIX_ENDPOINT_OUT, cbw, sizeof(cbw), &actual_out_len, 1000);
    if (err < 0) {
        switch (err) {
            case LIBUSB_ERROR_TIMEOUT:
            case LIBUSB_ERROR_PIPE:
                goto otp_race_bug_recover;
            default:
                return err;
        }
    }

    // Data phase: read the payload in from the device.
    int actual_data_len = 0;
    err = libusb_bulk_transfer(handle, SITRONIX_ENDPOINT_IN, buf, n, &actual_data_len, 1000);
    if (err < 0) {
        switch (err) {
            case LIBUSB_ERROR_TIMEOUT:
            case LIBUSB_ERROR_PIPE:
                goto otp_race_bug_recover;
            default:
                return err;
        }
    }

    // Status phase: read and validate the CSW.
    unsigned char csw[CSW_SIZE] = {0};
    int csw_len = 0;
    err = libusb_bulk_transfer(handle, SITRONIX_ENDPOINT_IN, csw, sizeof(csw), &csw_len, 1000);
    if (err < 0) {
        switch (err) {
            case LIBUSB_ERROR_TIMEOUT:
            case LIBUSB_ERROR_PIPE:
                goto otp_race_bug_recover;
            default:
                return err;
        }
    }
    switch (check_csw(csw, csw_len, tag)) {
        case CSW_OK:
            break;
        case CSW_RESYNC:
            goto otp_race_bug_recover;
        default: // CSW_FAIL
            return -1;
    }

    return ((size_t)actual_data_len > n) ? (ssize_t)n : actual_data_len;

    otp_race_bug_recover:
    miuchiz_log("libmiuchiz: recovering read\n");
    usleep(250000);
    // USB Mass Storage reset recovery: Bulk-Only Mass Storage Reset, then clear
    // the halt condition on each bulk endpoint.
    err = libusb_control_transfer(handle, 0x21, 0xFF, 0, 0, NULL, 0, 1000);
    if (err < 0) {
        miuchiz_log("libmiuchiz: read reset failed (%d)\n", err);
        return err;
    }
    libusb_clear_halt(handle, SITRONIX_ENDPOINT_IN);
    libusb_clear_halt(handle, SITRONIX_ENDPOINT_OUT);
    usleep(250000);
    goto read_start;
}

fp_t miuchiz_backend_open(struct Handheld* handheld) {
    ensure_libusb_init();

    handheld->fd.handle = NULL;
    handheld->fd.current_sector = 0;

    libusb_device **list;
    ssize_t count = libusb_get_device_list(NULL, &list);
    if (count < 0) {
        // On error the list is not allocated, so there is nothing to free.
        return handheld->fd;
    }
    for (int i = 0; i < count; i++) {
        libusb_device *device = list[i];
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(device, &desc) < 0) {
            miuchiz_log("libmiuchiz: libusb_get_device_descriptor failed\n");
            continue;
        }
        if (desc.idVendor != SITRONIX_VENDOR || desc.idProduct != SITRONIX_PRODUCT) {
            continue;
        }
        uint8_t bus = libusb_get_bus_number(device);
        uint8_t address = libusb_get_device_address(device);

        if (strcmp(bus_and_address_to_str(bus, address), handheld->device) != 0) {
            // This is not the right device
            continue;
        }

        struct libusb_device_handle *handle = NULL;
        if (libusb_open(device, &handle) < 0) {
            miuchiz_log("libmiuchiz: libusb_open failed\n");
            break;
        }

        if (libusb_kernel_driver_active(handle, 0) == 1) {
            if (libusb_detach_kernel_driver(handle, 0) != 0) {
                miuchiz_log("libmiuchiz: couldn't detach kernel driver\n");
                libusb_close(handle);
                break;
            }
        }
        miuchiz_log("libmiuchiz: claiming interface\n");

        if (libusb_claim_interface(handle, 0) < 0) {
            miuchiz_log("libmiuchiz: cannot claim interface\n");
            libusb_close(handle);
            break;
        }

        miuchiz_log("libmiuchiz: claimed interface\n");

        handheld->fd.current_sector = 0;
        handheld->fd.handle = handle;
        break;
    }
    libusb_free_device_list(list, 1);

    return handheld->fd;
}

void miuchiz_backend_close(struct Handheld* handheld) {
    if (handheld->fd.handle != NULL) {
        libusb_release_interface(handheld->fd.handle, 0);
        // Best effort: hand the device back to the OS. Not supported on every
        // platform (e.g. macOS), so the result is intentionally ignored.
        libusb_attach_kernel_driver(handheld->fd.handle, 0);
        libusb_close(handheld->fd.handle);
        handheld->fd.handle = NULL;
    }
}

ssize_t miuchiz_backend_read(struct Handheld* handheld, void* buf, size_t n) {
    if (handheld->fd.handle == NULL) {
        return -1;
    }
    return scsi_bulk_read(handheld->fd.handle, handheld->fd.current_sector, buf, n);
}

ssize_t miuchiz_backend_write(struct Handheld* handheld, const void* buf, size_t n) {
    /*
    Same workaround as the native backend: the device's firmware misbehaves
    (pipe errors) when operated on too quickly, so we time the write and then
    sleep for a fraction of however long it took, giving the device time to
    recover before the next operation.
    */
    if (handheld->fd.handle == NULL) {
        return -1;
    }

    struct Utimer timer;
    miuchiz_utimer_start(&timer);

    ssize_t result = scsi_bulk_write(handheld->fd.handle, handheld->fd.current_sector, buf, n);

    miuchiz_utimer_end(&timer);

    usleep(miuchiz_utimer_elapsed(&timer) / 3);

    return result;
}

off_t miuchiz_backend_seek(struct Handheld* handheld, off_t offset) {
    handheld->fd.current_sector = offset / MIUCHIZ_SECTOR_SIZE;
    return 0;
}

int miuchiz_backend_enumerate(struct Handheld*** handhelds) {
    int handhelds_count = 0;
    // Whether a device with the Miuchiz vendor/product id is on the bus, even if
    // we cannot open or read it. Reading USB descriptors is never gated by OS
    // privacy controls, so this lets us tell "no handheld connected" apart from
    // "handheld connected but its data could not be accessed" (see the
    // MIUCHIZ_ERROR_ACCESS return below).
    int present = 0;
    *handhelds = NULL;

    ensure_libusb_init();

    libusb_device **list;
    ssize_t count = libusb_get_device_list(NULL, &list);
    if (count < 0) {
        // On error the list is not allocated, so there is nothing to free.
        return 0;
    }

    // 1 more than the possible maximum number of handhelds is allocated here.
    // This is to ensure that there is at least one NULL at the end.
    size_t handhelds_array_size = (count + 1) * sizeof(struct Handheld*);
    *handhelds = malloc(handhelds_array_size);
    memset(*handhelds, 0, handhelds_array_size);

    for (int i = 0; i < count; i++) {
        libusb_device *device = list[i];
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(device, &desc) < 0) {
            miuchiz_log("libmiuchiz: libusb_get_device_descriptor failed\n");
            continue;
        }
        if (desc.idVendor != SITRONIX_VENDOR || desc.idProduct != SITRONIX_PRODUCT) {
            continue;
        }
        present = 1;
        uint8_t bus = libusb_get_bus_number(device);
        uint8_t address = libusb_get_device_address(device);

        struct Handheld* handheld_candidate = miuchiz_handheld_create(bus_and_address_to_str(bus, address));
        if (miuchiz_handheld_is_handheld(handheld_candidate)) {
            (*handhelds)[handhelds_count++] = handheld_candidate;
        }
        else {
            miuchiz_handheld_destroy(handheld_candidate);
        }
    }
    libusb_free_device_list(list, 1);

    // A Miuchiz device is attached but none could be opened/read - report it as
    // an access error rather than "nothing found", so the caller can tell the
    // user their handheld was detected but blocked (e.g. by macOS privacy).
    if (handhelds_count == 0 && present) {
        free(*handhelds);
        *handhelds = NULL;
        return MIUCHIZ_ERROR_ACCESS;
    }

    return handhelds_count;
}

void* miuchiz_backend_dma_alloc(size_t size) {
    void* ptr = malloc(size);
    // memset(ptr, 0xCA, size); // Makes it easier to spot usage of uninitialized memory
    return ptr;
}

void miuchiz_backend_dma_free(void* p) {
    free(p);
}

long miuchiz_backend_page_alignment(void) {
    // The libusb transfers go through plain malloc'd buffers, so any alignment
    // works; report the OS page size to keep behaviour consistent with the
    // native backends.
#if defined(_WIN32)
    return 4096;
#else
    return sysconf(_SC_PAGESIZE);
#endif
}

#endif // MIUCHIZ_USE_LIBUSB

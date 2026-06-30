#ifndef MIUCHIZ_LIBMIUCHIZ_USB_H
#define MIUCHIZ_LIBMIUCHIZ_USB_H
#include <stddef.h>
#include <stdint.h>

/*
 * Portable POSIX-ish types used throughout the backend interface (ssize_t,
 * off_t). POSIX platforms, and MinGW, get them from <unistd.h>. MSVC ships
 * neither in <unistd.h> (it has no such header), so pull the equivalents in
 * from the Win32 SDK instead: SSIZE_T from <BaseTsd.h> and off_t from
 * <sys/types.h> (where MSVC defines it as long, wide enough for the small
 * volume offsets this library uses).
 */
#if defined(_WIN32) && !defined(__MINGW32__)
    #include <BaseTsd.h>
    #include <sys/types.h>
    /* Guard against a redefinition error if another header in the consumer's
     * translation unit already provided ssize_t. _SSIZE_T_DEFINED is the guard
     * macro that Windows toolchains conventionally use for exactly this. */
    #if !defined(_SSIZE_T_DEFINED)
        typedef SSIZE_T ssize_t;
        #define _SSIZE_T_DEFINED
    #endif
#else
    #include <unistd.h>
#endif

/*
 * Backend selection.
 *
 * The library reaches a handheld either through the native OS block device
 * (Linux /dev/sd*, Windows volume handle) or through libusb. macOS has no raw
 * SCSI passthrough we can use, so it always uses libusb; on other platforms
 * the native backend is the default and the libusb backend can be enabled at
 * configure time with -DMIUCHIZ_USE_LIBUSB=ON.
 *
 * The build system defines MIUCHIZ_USE_LIBUSB when the libusb backend is
 * selected. We additionally force it on for Apple here so this header stays
 * correct even when it is included without the build flag.
 */
#if !defined(MIUCHIZ_USE_LIBUSB) && defined(__APPLE__)
    #define MIUCHIZ_USE_LIBUSB 1
#endif

#if defined(MIUCHIZ_USE_LIBUSB)
    #include <libusb.h>
    typedef struct {
        libusb_device_handle* handle;
        uint32_t current_sector;
    } fp_t;
#elif defined(_WIN32)
    #include <windows.h>
    typedef HANDLE fp_t;
#elif defined(unix) || defined(__unix__) || defined(__unix)
    typedef int fp_t;
#endif

#define MIUCHIZ_SECTOR_SIZE (512)
#define MIUCHIZ_SECTOR_SCSI_WRITE (0x31)
#define MIUCHIZ_SECTOR_DATA_READ (0x58)
#define MIUCHIZ_SECTOR_DATA_WRITE (0x33)
#define MIUCHIZ_PAGE_SIZE (0x1000)
#define MIUCHIZ_PAGE_COUNT (0x200)

/* Error codes returned by the sector/page functions below. On failure they
 * return one of these (all negative); on success they return a non-negative
 * byte count. */
#define MIUCHIZ_ERROR_IO        (-1) /* I/O failure; errno is set */
#define MIUCHIZ_ERROR_TOO_SMALL (-2) /* buffer/data was smaller than one sector */
#define MIUCHIZ_ERROR_PAGE_SIZE (-3) /* size argument invalid for a page operation */
#define MIUCHIZ_ERROR_ACCESS    (-4) /* a handheld is attached but could not be opened
                                      * or read - typically the OS denied access (e.g.
                                      * the macOS removable-volume privacy gate). Distinct
                                      * from "no device present", which is not an error. */

struct Handheld {
    char* device;
    fp_t fd;
};

/** 
 *Opens a device as a Miuchiz handheld.
 *@param device The device.
 *@return A Handheld* with .fd set to the opened object.
 *@note Free and close device with miuchiz_handheld_destroy.
 *@warning This does not verify that the device is a valid handheld.
 *         Use miuchiz_handheld_is_handheld for that.
 */
struct Handheld* miuchiz_handheld_create(const char* device);

/** 
 *Closes and frees a Miuchiz handheld.
 *@param handheld Pointer to a Handheld.
 *@note Compliment to miuchiz_handheld_create
 */
void miuchiz_handheld_destroy(struct Handheld* handheld);

/** 
 *Opens device.
 *@param handheld Pointer to a Handheld.
 */
fp_t miuchiz_handheld_open(struct Handheld* handheld);

/** 
 *Closes device.
 *@param handheld Pointer to a Handheld.
 */
void miuchiz_handheld_close(struct Handheld* handheld);

/** 
 *Opens all valid Miuchiz devices on the system.
 *@param handhelds A Handheld***, that is, a pointer to which an array of Handheld pointers will be placed.
 *@return The number of handhelds found and verified; the number of elements in handhelds.
 *@note Free and close all with miuchiz_handheld_destroy_all.
 */
int miuchiz_handheld_create_all(struct Handheld*** handhelds);

/** 
 *Closes and frees an array of handhelds from miuchiz_handheld_create_all.
 *@param handhelds The Handheld** filled by miuchiz_handheld_create_all.
 *@return The number of handhelds found and verified; the number of elements in handhelds.
 *@note Compliment to miuchiz_handheld_create_all.
 */
void miuchiz_handheld_destroy_all(struct Handheld** handhelds);

/** 
 *Verifies that a Handheld* actually refers to a Miuchiz handheld device.
 *@param handheld A Handheld* to be verified.
 *@return 1 if the device is a Miuchiz handheld, 0 otherwise.
 */
int miuchiz_handheld_is_handheld(struct Handheld* handheld);

/** 
 *Writes data to a sector of a Miuchiz handheld.
 *@param handheld A Handheld* to be written to.
 *@param sector The sector to write to.
 *@param data A pointer to data to write.
 *@param ndata The size of data to write.
 *@return MIUCHIZ_ERROR_TOO_SMALL if data was not at least 1 sector large.
 *        MIUCHIZ_ERROR_IO if data failed to be written; errno will be filled.
 *        Otherwise, the number of bytes written.
 */
int miuchiz_handheld_write_sector(struct Handheld* handheld, int sector, const void* data, size_t ndata);

/** 
 *Reads data from a sector of a Miuchiz handheld.
 *@param handheld A Handheld* to be read from.
 *@param sector The sector to read from.
 *@param buf A pointer to a buffer to fill with read data.
 *@param nbuf The size of the buffer to be filled.
 *@return MIUCHIZ_ERROR_TOO_SMALL if the buffer was not at least 1 sector large.
 *        MIUCHIZ_ERROR_IO if data failed to be read; errno will be filled.
 *        Otherwise, the number of bytes read.
 */
int miuchiz_handheld_read_sector(struct Handheld* handheld, int sector, void* buf, size_t nbuf);

/** 
 *Writes a (SCSI) command to the command interface of a Miuchiz handheld.
 *@param handheld A Handheld* to which to send the command.
 *@param data The command to send.
 *@param ndata The size of the command.
 *@return miuchiz_handheld_write_sector error code or bytes written.
 */
int miuchiz_handheld_send_scsi(struct Handheld* handheld, const void* data, size_t ndata);

/** 
 *Reads a page (0x1000 bytes) from a Miuchiz handheld's flash memory.
 *@param handheld A Handheld* to be read from.
 *@param page The page to be read (0x0000 ~ 0x01FF normally).
 *@param buf A pointer to a buffer to fill with read data.
 *@param nbuf The size of the buffer to be filled.
 *@return MIUCHIZ_ERROR_PAGE_SIZE if nbuf is not larger than one sector,
 *        a miuchiz_handheld_read_sector error code, or the number of bytes read.
 */
int miuchiz_handheld_read_page(struct Handheld* handheld, int page, void* buf, size_t nbuf);

/** 
 *Writes a page (0x1000 bytes) to a Miuchiz handheld's flash memory.
 *@param handheld A Handheld* to write to.
 *@param page The page to be read (0x0000 ~ 0x01FF normally).
 *@param buf A pointer to data to write.
 *@param nbuf The size of the data.
 *@return MIUCHIZ_ERROR_PAGE_SIZE if nbuf is not exactly MIUCHIZ_PAGE_SIZE,
 *        a miuchiz_handheld_write_sector error code, or the number of bytes written.
 */
int miuchiz_handheld_write_page(struct Handheld* handheld, int page, const void* buf, size_t nbuf);

/** 
 *Rounds n up to the nearest multiple of alignment.
 *@param n Number to be rounded.
 *@param alignment Number to which n will be rounded to a multiple of.
 *@return Rounded number.
 */
size_t miuchiz_round_size_up(size_t n, int alignment);

/** 
 *Prints a hex dump to the screen.
 *@param buffer Data to be dumped.
 *@param n Size of data.
 */
void miuchiz_hex_dump(const void* buffer, size_t n);

/**
 *Gets the memory alignment needed to write to the device.
 *@return Alignment required.
 */
long miuchiz_page_alignment(void);

/**
 *Enables or disables the library's diagnostic logging. Logging is off by
 *default, so the library prints nothing unless this is turned on. When enabled,
 *messages are written to stderr.
 *@param enabled Non-zero to enable logging, zero to disable.
 */
void miuchiz_set_logging(int enabled);

/**
 *Parses a 32-bit little-endian HCD-encoded integer from 4 bytes.
 *@param bytes Pointer to 4 bytes in little-endian order.
 *@return Host-endian integer value of the HCD word (not decimal expanded).
 */
uint32_t miuchiz_le32_read(const unsigned char* bytes);

/**
 *Writes a 32-bit value as little-endian bytes.
 *@param bytes Destination buffer of at least 4 bytes.
 *@param value Host-endian value to write.
 */
void miuchiz_le32_write(unsigned char* bytes, uint32_t value);

/**
 *Reads a 16-bit value as little-endian bytes.
 *@param bytes Pointer to 2 bytes in little-endian order.
 *@return Host-endian integer value of the 16-bit word (not decimal expanded).
 */
uint16_t miuchiz_le16_read(const unsigned char* bytes);

/**
 *Writes a 16-bit value as little-endian bytes.
 *@param bytes Destination buffer of at least 2 bytes.
 *@param value Host-endian value to write.
 */
void miuchiz_le16_write(unsigned char* bytes, uint16_t value);

/**
 *Converts a host-endian integer to HCD (hex-coded decimal) 32-bit value.
 *@param value Unsigned 32-bit integer (up to 8 decimal digits used).
 *@return 32-bit value where each nibble encodes a decimal digit (little-endian).
 */
uint32_t miuchiz_hcd_encode(uint32_t value);

/**
 *Converts a 32-bit HCD (hex-coded decimal) value to a host-endian unsigned 32-bit integer.
 *@param hcd_le 32-bit value where each nibble encodes a decimal digit (little-endian).
 *@return Parsed unsigned 32-bit integer.
 */
uint32_t miuchiz_hcd_decode(uint32_t hcd_le);

#endif
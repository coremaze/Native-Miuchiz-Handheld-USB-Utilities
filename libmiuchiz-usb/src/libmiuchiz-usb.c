#include "libmiuchiz-usb.h"
#include "backend.h"
#include "commands.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// Exposed functions

struct Handheld* miuchiz_handheld_create(const char* device) {
    struct Handheld* handheld = malloc(sizeof(struct Handheld));

    handheld->device = strdup(device);
    handheld->emu = NULL;
    miuchiz_handheld_open(handheld);

    return handheld;
}

void miuchiz_handheld_destroy(struct Handheld* handheld) {
    miuchiz_handheld_close(handheld);
    free(handheld->device);
    free(handheld);
}

fp_t miuchiz_handheld_open(struct Handheld* handheld) {
    return miuchiz_backend_open(handheld);
}

void miuchiz_handheld_close(struct Handheld* handheld) {
    miuchiz_backend_close(handheld);
}

int miuchiz_handheld_create_all(struct Handheld*** handhelds) {
    return miuchiz_backend_enumerate(handhelds);
}

void miuchiz_handheld_destroy_all(struct Handheld** handhelds) {
    if (handhelds != NULL) {
        for (struct Handheld** handheld = handhelds; *handheld != NULL; handheld++) {
            miuchiz_handheld_destroy(*handheld);
        }
        free(handhelds);
    }
}

int miuchiz_handheld_is_handheld(struct Handheld* handheld) {
    char* data = malloc(MIUCHIZ_SECTOR_SIZE);
    int bytes_read = miuchiz_handheld_read_sector(handheld, 0, data, MIUCHIZ_SECTOR_SIZE);
    int is_handheld;
    if (bytes_read < MIUCHIZ_SECTOR_SIZE) {
        is_handheld = 0;
    }
    else {
        // This is how Miuchiz Sync checks to see if a mass storage device is a handheld
        is_handheld = memcmp(data + 43, "SITRONIXTM", 10) == 0;
    }

    free(data);
    return is_handheld;
}

int miuchiz_handheld_write_sector(struct Handheld* handheld, int sector, const void* data, size_t ndata) {
    if (ndata < MIUCHIZ_SECTOR_SIZE) {
        return MIUCHIZ_ERROR_TOO_SMALL;
    }

    char* aligned_buf = miuchiz_backend_dma_alloc(ndata);
    if (aligned_buf == NULL) {
        miuchiz_log("miuchiz_handheld_write_sector: allocation failed\n");
        return MIUCHIZ_ERROR_IO;
    }

    memcpy(aligned_buf, data, ndata);

    miuchiz_backend_seek(handheld, sector * MIUCHIZ_SECTOR_SIZE);
    int result = miuchiz_backend_write(handheld, aligned_buf, ndata);

    //miuchiz_hex_dump(aligned_buf, 0x20);
    if (result == MIUCHIZ_ERROR_IO) {
        miuchiz_log("miuchiz_handheld_write_sector failed. [%d] %s\n", errno, strerror(errno));
    }

    miuchiz_backend_dma_free(aligned_buf);

    return result;
}

int miuchiz_handheld_read_sector(struct Handheld* handheld, int sector, void* buf, size_t nbuf) {
    if (nbuf < MIUCHIZ_SECTOR_SIZE) {
        return MIUCHIZ_ERROR_TOO_SMALL;
    }

    // Data needs to be a multiple of sector size
    size_t required_size = miuchiz_round_size_up(nbuf, MIUCHIZ_SECTOR_SIZE);

    char* aligned_buf = miuchiz_backend_dma_alloc(required_size);
    if (aligned_buf == NULL) {
        miuchiz_log("miuchiz_handheld_read_sector: allocation failed\n");
        return MIUCHIZ_ERROR_IO;
    }

    miuchiz_backend_seek(handheld, sector * MIUCHIZ_SECTOR_SIZE);
    int result = miuchiz_backend_read(handheld, aligned_buf, required_size);
    if (result >= 0) {
        memcpy(buf, aligned_buf, nbuf);
    }
    else {
        miuchiz_log("miuchiz_handheld_read_sector failed. [%d] %s\n", errno, strerror(errno));
    }

    miuchiz_backend_dma_free(aligned_buf);

    return result;
}

int miuchiz_handheld_send_scsi(struct Handheld* handheld, const void* data, size_t ndata) {
    // Data needs to be a multiple of sector size
    size_t required_size = miuchiz_round_size_up(ndata, MIUCHIZ_SECTOR_SIZE);

    char* padded_data = miuchiz_backend_dma_alloc(required_size);
    if (padded_data == NULL) {
        miuchiz_log("miuchiz_handheld_send_scsi: allocation failed\n");
        return MIUCHIZ_ERROR_IO;
    }
    memset(padded_data, 0, required_size);
    memcpy(padded_data, data, ndata);

    int result = miuchiz_handheld_write_sector(handheld, MIUCHIZ_SECTOR_SCSI_WRITE, padded_data, required_size);

    miuchiz_backend_dma_free(padded_data);

    return result;
}

int miuchiz_handheld_read_page(struct Handheld* handheld, int page, void* buf, size_t nbuf) {
    if (nbuf <= MIUCHIZ_SECTOR_SIZE) {
        return MIUCHIZ_ERROR_PAGE_SIZE;
    }

    int read_result;

    // Write initiator to command interface
    {
        struct SCSIWriteFilemarksCommand cmd = miuchiz_scsi_write_filemarks_command();
        miuchiz_handheld_send_scsi(handheld, &cmd, sizeof(cmd));
    }

    // Tell command interface we want to read from this page
    {
        struct SCSIReadCommand cmd = miuchiz_scsi_read_command(page);
        miuchiz_handheld_send_scsi(handheld, &cmd, sizeof(cmd));
    }

    // Read response data from device's data output interface
    {
        // The response will look like this:
        // 4 bytes length, big endian
        // length of data, but we fill with the size requested

        size_t page_data_size = sizeof(int32_t) + nbuf;
        unsigned char* page_data = malloc(page_data_size);
        if (page_data == NULL) {
            miuchiz_log("miuchiz_handheld_read_page: allocation failed\n");
            read_result = MIUCHIZ_ERROR_IO;
        }
        else {
            read_result = miuchiz_handheld_read_sector(handheld, MIUCHIZ_SECTOR_DATA_READ, page_data, page_data_size);
            if (read_result >= 0) {
                // Skip the length bytes
                memcpy(buf, page_data + sizeof(int32_t), nbuf);
            }
            else {
                miuchiz_log("miuchiz_handheld_read_sector failed in read_page. [%d] %s\n", errno, strerror(errno));
            }
            free(page_data);
        }
    }

    // Send terminator to command interface
    {
        struct SCSIReadReverseCommand cmd = miuchiz_scsi_read_reverse_command();
        miuchiz_handheld_send_scsi(handheld, &cmd, sizeof(cmd));
    }

    return read_result;
}

int miuchiz_handheld_write_page(struct Handheld* handheld, int page, const void* buf, size_t nbuf) {
    if (nbuf != MIUCHIZ_PAGE_SIZE) {
        return MIUCHIZ_ERROR_PAGE_SIZE;
    }

    int write_result;

    // Write initiator to command interface
    {
        struct SCSIWriteFilemarksCommand cmd = miuchiz_scsi_write_filemarks_command();
        miuchiz_handheld_send_scsi(handheld, &cmd, sizeof(cmd));
    }

    // Tell command interface we want to write to this page
    {
        struct SCSIWriteCommand cmd = miuchiz_scsi_write_command(page, nbuf);
        miuchiz_handheld_send_scsi(handheld, &cmd, sizeof(cmd));
    }

    // Put our data into the data input interface
    {
        write_result = miuchiz_handheld_write_sector(handheld, MIUCHIZ_SECTOR_DATA_WRITE, buf, nbuf);
    }

    // Send terminator to command interface
    {
        struct SCSIReadReverseCommand cmd = miuchiz_scsi_read_reverse_command();
        miuchiz_handheld_send_scsi(handheld, &cmd, sizeof(cmd));
    }

    return write_result;
}

size_t miuchiz_round_size_up(size_t n, int alignment) {
    size_t result;
    if (n % alignment == 0) {
        result = n;
    }
    else {
        result = ((n/alignment) + 1) * alignment;
    }
    return result;
}

void miuchiz_hex_dump(const void* buffer, size_t n) {
    const unsigned char* data = buffer;

    for (size_t i = 0; i < n; i++) {
        if (i % 16 == 0) {
            if (i != 0) {
                printf("\n");
            }
            printf("%08zX ", i);
        }

        printf("%02X", data[i]);

        if (i % 16 != 15) {
            printf(" ");
        }
    }
    if (n != 0) {
        printf("\n");
    }
}

long miuchiz_page_alignment(void) {
    static long page_size = 0;
    if (page_size == 0) {
        page_size = miuchiz_backend_page_alignment();
        if (page_size < MIUCHIZ_SECTOR_SIZE) {
            page_size = MIUCHIZ_SECTOR_SIZE;
        }
    }
    return page_size;
}

uint32_t miuchiz_le32_read(const unsigned char* bytes) {
    return ((uint32_t)bytes[0])
         | ((uint32_t)bytes[1] << 8)
         | ((uint32_t)bytes[2] << 16)
         | ((uint32_t)bytes[3] << 24);
}

void miuchiz_le32_write(unsigned char* bytes, uint32_t value) {
    bytes[0] = (unsigned char)(value & 0xFF);
    bytes[1] = (unsigned char)((value >> 8) & 0xFF);
    bytes[2] = (unsigned char)((value >> 16) & 0xFF);
    bytes[3] = (unsigned char)((value >> 24) & 0xFF);
}

uint16_t miuchiz_le16_read(const unsigned char* bytes) {
    return ((uint16_t)bytes[0])
         | ((uint16_t)bytes[1] << 8);
}

void miuchiz_le16_write(unsigned char* bytes, uint16_t value) {
    bytes[0] = (unsigned char)(value & 0xFF);
    bytes[1] = (unsigned char)((value >> 8) & 0xFF);
}

uint32_t miuchiz_hcd_encode(uint32_t value) {
    uint32_t result = 0;
    uint32_t place = 0;
    for (int i = 0; i < 8; i++) {
        int digit = value % 10;
        result |= ((uint32_t)(digit & 0xF)) << place;
        place += 4;
        value /= 10;
    }
    return result;
}

uint32_t miuchiz_hcd_decode(uint32_t hcd_le) {
    int result = 0;
    int place = 1;
    for (int i = 0; i < 8; i++) {
        int digit = (hcd_le >> (i * 4)) & 0xF;
        result += digit * place;
        place *= 10;
    }
    return result;
}

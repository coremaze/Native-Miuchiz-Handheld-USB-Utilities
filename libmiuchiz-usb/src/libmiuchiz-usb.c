#include "libmiuchiz-usb.h"
#include "commands.h"
#include "timer.h"

#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#if defined(unix) || defined(__unix__) || defined(__unix)
    #include <glob.h>
#elif defined(_WIN32)
    #include <windows.h>
    #include <malloc.h>
#endif

#define LOG_ERRORS 0

// Internal functions

static ssize_t _miuchiz_handheld_write(struct Handheld* handheld, const void *buf, size_t n) {
    /*
    Writing to the device without giving it enough time will make it stop
    working. Even though write() shouldn't return early due to O_DIRECT,
    the issue still happens.
    */
    ssize_t result = 0;
    struct Utimer timer;
    miuchiz_utimer_start(&timer);

    #if defined(unix) || defined(__unix__) || defined(__unix)
        if (handheld->fd != -1) {
            result = write(handheld->fd, buf, n);
        }
    #elif defined(_WIN32)
        if (handheld->fd != INVALID_HANDLE_VALUE) {
            DWORD dresult = 0;
            if (WriteFile(handheld->fd, buf, n, &dresult, NULL) == 0) {
                result = -1;
            }
            else {
                result = dresult;
            }
        }
    #endif

    miuchiz_utimer_end(&timer);

    #if defined(unix) || defined(__unix__) || defined(__unix)
        uint64_t usecs_to_sleep = miuchiz_utimer_elapsed(&timer) / 3;
        usleep(usecs_to_sleep);
    #elif defined(_WIN32)
        // For some reason, Windows 10 needs a lot more time
        int msecs_to_sleep = (miuchiz_utimer_elapsed(&timer) * 0.5) / 1000;
        if (msecs_to_sleep < 1) {
            msecs_to_sleep = 1;
        }
        Sleep(msecs_to_sleep);
    #endif
    
    return result;
}

static ssize_t _miuchiz_handheld_read(struct Handheld* handheld, void* buf, size_t nbytes) {
    ssize_t result = -1;
    #if defined(unix) || defined(__unix__) || defined(__unix)
        if (handheld->fd != -1) {
            result = read(handheld->fd, buf, nbytes);
        } 
    #elif defined(_WIN32)
        if (handheld->fd != INVALID_HANDLE_VALUE) {
            DWORD dresult = 0;
            if (ReadFile(handheld->fd, buf, nbytes, &dresult, NULL) == 0) {
                result = -1;
            }
            else {
                result = dresult;
            }
        }
    #endif
    return result;
}

static off_t _miuchiz_handheld_seek(struct Handheld* handheld, off_t offset) {
    off_t result = -1;
    #if defined(unix) || defined(__unix__) || defined(__unix)
        if (handheld->fd != -1) {
            result = lseek(handheld->fd, offset, SEEK_SET);
        }
    #elif defined(_WIN32)
        if (handheld->fd != INVALID_HANDLE_VALUE) {
            result = SetFilePointer(handheld->fd, offset, 0, FILE_BEGIN);
        }
    #endif
    return result;
}

static void* _miuchiz_dma_alloc(size_t size) {
    #if defined(unix) || defined(__unix__) || defined(__unix)
        size_t alignment = (size_t)miuchiz_page_alignment();
        // Ensure that the size is a multiple of the alignment
        size_t alloc_size = miuchiz_round_size_up(size, (int)alignment);
        return aligned_alloc(alignment, alloc_size);
    #elif defined(_WIN32)
        size_t alignment = (size_t)miuchiz_page_alignment();
        return _aligned_malloc(size, alignment);
    #endif
}

static void _miuchiz_dma_free(void* p) {
    #if defined(unix) || defined(__unix__) || defined(__unix)
        free(p);
    #elif defined(_WIN32)
        _aligned_free(p);
    #endif
}

// End internal functions

// Exposed functions

struct Handheld* miuchiz_handheld_create(const char* device) {
    struct Handheld* handheld = malloc(sizeof(struct Handheld));

    handheld->device = strdup(device);
    miuchiz_handheld_open(handheld);

    return handheld;
}

void miuchiz_handheld_destroy(struct Handheld* handheld) {
    miuchiz_handheld_close(handheld);
    free(handheld->device);
    free(handheld);
}

fp_t miuchiz_handheld_open(struct Handheld* handheld) {
    #if defined(unix) || defined(__unix__) || defined(__unix)
        handheld->fd = open(handheld->device, O_RDWR | __O_DIRECT | O_NONBLOCK | O_SYNC );
    #elif defined(_WIN32)
        handheld->fd = CreateFileA(handheld->device,
                                   GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   NULL,
                                   OPEN_EXISTING,
                                   FILE_FLAG_NO_BUFFERING | FILE_ATTRIBUTE_NORMAL,
                                   NULL);
    #endif
    return handheld->fd;
}

void miuchiz_handheld_close(struct Handheld* handheld) {
    #if defined(unix) || defined(__unix__) || defined(__unix)
        if (handheld->fd != -1) {
            close(handheld->fd);
            handheld->fd = -1;
        }
    #elif defined(_WIN32)
        if (handheld->fd != INVALID_HANDLE_VALUE) {
            CloseHandle(handheld->fd);
            handheld->fd = INVALID_HANDLE_VALUE;
        }
    #endif 
}

int miuchiz_handheld_create_all(struct Handheld*** handhelds) {
    int handhelds_count = 0;
    *handhelds = NULL;

    // 1 more than the possible maximum number of handhelds is allocated here.
    // This is to ensure that there is at least one NULL at the end.

    #if defined(unix) || defined(__unix__) || defined(__unix)
        // Get all SCSI disks on the system
        glob_t globbuf;
        if (!glob("/dev/sd*", 0, NULL, &globbuf)) {
            size_t handhelds_array_size = (globbuf.gl_pathc + 1) * sizeof(struct Handheld*);
            *handhelds = malloc(handhelds_array_size);
            memset(*handhelds, 0, handhelds_array_size);

            // Find all the disks that are handhelds
            for (int i = 0;  i < globbuf.gl_pathc; i++) {
                struct Handheld* handheld_candidate = miuchiz_handheld_create(globbuf.gl_pathv[i]);
                if (miuchiz_handheld_is_handheld(handheld_candidate)) {
                    (*handhelds)[handhelds_count++] = handheld_candidate;
                }
                else {
                    miuchiz_handheld_destroy(handheld_candidate);
                }
            }
        }
        globfree(&globbuf);
    #elif defined(_WIN32)
        // Get all the drive letters mounted
        unsigned int mask = GetLogicalDrives();
        char letters[256] = { };
        int letters_count = 0;
        for (int i = 0; mask; i++){
            if (mask & 1){
                char letter = 'A' + i;
                letters[letters_count++] = letter;
            }
            mask = mask >> 1;
        }

        // Find all the drive letters that are handhelds
        size_t handhelds_array_size = (letters_count + 1) * sizeof(struct Handheld*);
        *handhelds = malloc(handhelds_array_size);
        memset(*handhelds, 0, handhelds_array_size);

        for (int i = 0; i<letters_count; i++){
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
    #endif 

    return handhelds_count;
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
        return -2;
    }

    char* aligned_buf = _miuchiz_dma_alloc(ndata);
    if (aligned_buf == NULL) {
        if (LOG_ERRORS) {
            printf("_miuchiz_dma_alloc failed in write_sector.\n");
        }
        return -1;
    }

    memcpy(aligned_buf, data, ndata);

    _miuchiz_handheld_seek(handheld, sector * MIUCHIZ_SECTOR_SIZE);
    int result = _miuchiz_handheld_write(handheld, aligned_buf, ndata);
    
    //miuchiz_hex_dump(aligned_buf, 0x20);
    if (LOG_ERRORS && result == -1) {
        printf("miuchiz_handheld_write_sector failed. [%d] %s\n", errno, strerror(errno));
    }

    _miuchiz_dma_free(aligned_buf);

    return result;
}

int miuchiz_handheld_read_sector(struct Handheld* handheld, int sector, void* buf, size_t nbuf) {
    if (nbuf < MIUCHIZ_SECTOR_SIZE) {
        return -2;
    }

    // Data needs to be a multiple of sector size
    size_t required_size = miuchiz_round_size_up(nbuf, MIUCHIZ_SECTOR_SIZE); 

    char* aligned_buf = _miuchiz_dma_alloc(required_size);
    if (aligned_buf == NULL) {
        if (LOG_ERRORS) {
            printf("_miuchiz_dma_alloc failed in read_sector.\n");
        }
        return -1;
    }

    _miuchiz_handheld_seek(handheld, sector * MIUCHIZ_SECTOR_SIZE);
    int result = _miuchiz_handheld_read(handheld, aligned_buf, required_size);

    if (result >= 0) {
        memcpy(buf, aligned_buf, nbuf);
    }
    else if (LOG_ERRORS) {
        printf("miuchiz_handheld_read_sector failed. [%d] %s\n", errno, strerror(errno));
    }

    _miuchiz_dma_free(aligned_buf);

    return result;
}

int miuchiz_handheld_send_scsi(struct Handheld* handheld, const void* data, size_t ndata) {
    // Data needs to be a multiple of sector size
    size_t required_size = miuchiz_round_size_up(ndata, MIUCHIZ_SECTOR_SIZE); 

    char* padded_data = _miuchiz_dma_alloc(required_size);
    if (padded_data == NULL) {
        if (LOG_ERRORS) {
            printf("_miuchiz_dma_alloc failed in send_scsi.\n");
        }
        return -1;
    }
    memset(padded_data, 0, required_size);
    memcpy(padded_data, data, ndata);

    int result = miuchiz_handheld_write_sector(handheld, MIUCHIZ_SECTOR_SCSI_WRITE, padded_data, required_size);

    _miuchiz_dma_free(padded_data);

    return result;
}

int miuchiz_handheld_read_page(struct Handheld* handheld, int page, void* buf, size_t nbuf) {
    if (nbuf <= MIUCHIZ_SECTOR_SIZE) {
        return -3;
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
            if (LOG_ERRORS) {
                printf("malloc failed in read_page.\n");
            }
            read_result = -1;
        }
        else {
            read_result = miuchiz_handheld_read_sector(handheld, MIUCHIZ_SECTOR_DATA_READ, page_data, page_data_size);
            if (read_result >= 0) {
                // Skip the length bytes
                memcpy(buf, page_data + sizeof(int32_t), nbuf);
            }
            else if (LOG_ERRORS) {
                printf("miuchiz_handheld_read_sector failed in read_page. [%d] %s\n", errno, strerror(errno));
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
        return -3;
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

    for (int i = 0; i < n; i++) {
        if (i % 16 == 0) {
            if (i != 0) {
                printf("\n");
            }
            printf("%08X ", i);
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

long miuchiz_page_alignment() {
    static long page_size = 0;
    if (page_size == 0) {
        #if defined(unix) || defined(__unix__) || defined(__unix)
            page_size = sysconf(_SC_PAGESIZE);
            if (page_size < MIUCHIZ_SECTOR_SIZE) {
                page_size = MIUCHIZ_SECTOR_SIZE;
            }
        #elif defined(_WIN32)
            page_size = 4096;
        #endif
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
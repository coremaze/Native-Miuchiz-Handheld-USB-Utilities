#include "libmiuchiz.h"
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
#endif

#define LOG_ERRORS 1

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
    result = write(handheld->fd, buf, n);
    #elif defined(_WIN32)
    DWORD dresult = 0;
    if (WriteFile(handheld->fd, buf, n, &dresult, NULL) == 0) {
        result = -1;
    }
    else {
        result = dresult;
    }
    #endif

    miuchiz_utimer_end(&timer);

    uint64_t usecs_to_sleep = miuchiz_utimer_elapsed(&timer) / 3;

    #if defined(unix) || defined(__unix__) || defined(__unix)
    usleep(usecs_to_sleep);
    #elif defined(_WIN32)
    int msecs_to_sleep = miuchiz_round_size_up(usecs_to_sleep, 1000) / 1000;
    if (msecs_to_sleep == 0) {
        msecs_to_sleep = 1;
    }
    Sleep(msecs_to_sleep);
    #endif
    
    return result;
}

static ssize_t _miuchiz_handheld_read(struct Handheld* handheld, void* buf, size_t nbytes) {
    ssize_t result = 0;
    #if defined(unix) || defined(__unix__) || defined(__unix)
    result = read(handheld->fd, buf, nbytes);
    #elif defined(_WIN32)
    DWORD dresult = 0;
    if (ReadFile(handheld->fd, buf, nbytes, &dresult, NULL) == 0) {
        result = -1;
    }
    else {
        result = dresult;
    }
    #endif
    return result;
}

static off_t _miuchiz_handheld_seek(struct Handheld* handheld, off_t offset) {
    #if defined(unix) || defined(__unix__) || defined(__unix)
    return lseek(handheld->fd, offset, SEEK_SET);
    #elif defined(_WIN32)
    return SetFilePointer(handheld->fd, offset, 0, FILE_BEGIN);
    #endif
}

static void* _miuchiz_dma_alloc(size_t size) {
    #if defined(unix) || defined(__unix__) || defined(__unix)
    return aligned_alloc(miuchiz_page_alignment(), size);
    #elif defined(_WIN32)
    return malloc(size);
    #endif
}

static void _miuchiz_dma_free(void* p) {
    free(p);
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
    if (handheld->fd > 0) {
        #if defined(unix) || defined(__unix__) || defined(__unix)
        close(handheld->fd);
        #elif defined(_WIN32)
        CloseHandle(handheld->fd);
        #endif 
    } 
}

int miuchiz_handheld_create_all(struct Handheld*** handhelds) {
    int handhelds_count = 0;
    *handhelds = NULL;

    // 1 more than the possible maximum number of handhelds is allocated here.
    // This is to ensure that there is at least one NULL at the end.

    #if defined(unix) || defined(__unix__) || defined(__unix)
    // Get all SCSI disks on the system
    glob_t globbuf;
    if (!glob("/dev/sdb", 0, NULL, &globbuf)) {
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
    int read = miuchiz_handheld_read_sector(handheld, 0, data, MIUCHIZ_SECTOR_SIZE);
    int is_handheld;

    if (read < MIUCHIZ_SECTOR_SIZE) {
        is_handheld = 0;
    }
    else {
        is_handheld = strncmp(&data[43], "SITRONIXTM", 10) == 0;
    }

    free(data);

    return is_handheld;
}

int miuchiz_handheld_write_sector(struct Handheld* handheld, int sector, const void* data, size_t ndata) {
    if (ndata < MIUCHIZ_SECTOR_SIZE) {
        return -2;
    }

    char* aligned_buf = _miuchiz_dma_alloc(ndata);

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
        struct SCSIWriteFilemarksCommand* cmd = miuchiz_scsi_write_filemarks_command_create();
        miuchiz_handheld_send_scsi(handheld, cmd, sizeof(*cmd));
        miuchiz_scsi_write_filemarks_command_destroy(cmd);
    }

    // Tell command interface we want to read from this page
    {
        struct SCSIReadCommand* cmd = miuchiz_scsi_read_command_create(page);
        miuchiz_handheld_send_scsi(handheld, cmd, sizeof(*cmd));
        miuchiz_scsi_read_command_destroy(cmd);
    }

    // Read response data from device's data output interface
    {
        struct __attribute__ ((packed)) {
            int length_be; char data[nbuf];
        } page_data;

        read_result = miuchiz_handheld_read_sector(handheld, MIUCHIZ_SECTOR_DATA_READ, &page_data, sizeof(page_data));

        memcpy(buf, page_data.data, nbuf);
    }

    // Send terminator to command interface
    {
        struct SCSIReadReverseCommand* cmd = miuchiz_scsi_read_reverse_command_create();
        miuchiz_handheld_send_scsi(handheld, cmd, sizeof(*cmd));
        miuchiz_scsi_read_reverse_command_destroy(cmd);
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
        struct SCSIWriteFilemarksCommand* cmd = miuchiz_scsi_write_filemarks_command_create();
        miuchiz_handheld_send_scsi(handheld, cmd, sizeof(*cmd));
        miuchiz_scsi_write_filemarks_command_destroy(cmd);
    }

    // Tell command interface we want to write to this page
    {
        struct SCSIWriteCommand* cmd = miuchiz_scsi_write_command_create(page, nbuf);
        miuchiz_handheld_send_scsi(handheld, cmd, sizeof(*cmd));
        miuchiz_scsi_write_command_destroy(cmd);
    }

    // Put our data into the data input interface
    {
        write_result = miuchiz_handheld_write_sector(handheld, MIUCHIZ_SECTOR_DATA_WRITE, buf, nbuf);
    }

    // Send terminator to command interface
    {
        struct SCSIReadReverseCommand* cmd = miuchiz_scsi_read_reverse_command_create();
        miuchiz_handheld_send_scsi(handheld, cmd, sizeof(*cmd));
        miuchiz_scsi_read_reverse_command_destroy(cmd);
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
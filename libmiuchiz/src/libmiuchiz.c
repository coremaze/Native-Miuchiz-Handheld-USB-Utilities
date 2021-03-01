#include "libmiuchiz.h"

#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <glob.h>
#include <unistd.h>


#define MIUCHIZ_ALIGNMENT_SIZE              (4096)

#define MIUCHIZ_SCSI_OPCODE_READ            (0x28)
#define MIUCHIZ_SCSI_OPCODE_WRITE           (0x2A)
#define MIUCHIZ_SCSI_OPCODE_WRITE_FILEMARKS (0x80)
#define MIUCHIZ_SCSI_OPCODE_READ_REVERSE    (0x81)

const char MIUCHIZ_SCSI_SEQ_WRITE_FILEMARKS[] = {MIUCHIZ_SCSI_OPCODE_WRITE_FILEMARKS, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
const char MIUCHIZ_SCSI_SEQ_READ_REVERSE[] =    {MIUCHIZ_SCSI_OPCODE_READ_REVERSE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

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

int miuchiz_handheld_open(struct Handheld* handheld) {
    handheld->fd = open(handheld->device, O_RDWR | __O_DIRECT | O_NONBLOCK | O_SYNC );
    return handheld->fd;
}

void miuchiz_handheld_close(struct Handheld* handheld) {
    if (handheld->fd >= 0) {
        close(handheld->fd);
    } 
}

int miuchiz_handheld_create_all(struct Handheld*** handhelds) {
    glob_t globbuf;
    int handhelds_count = 0;

    if (!glob("/dev/sd*", 0, NULL, &globbuf)) {
        size_t handhelds_array_size = (globbuf.gl_pathc + 1) * sizeof(struct Handheld*);
        *handhelds = malloc(handhelds_array_size);
        memset(*handhelds, 0, handhelds_array_size);

        
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

int miuchiz_handheld_write_sector(struct Handheld* handheld, int sector, const char* data, size_t ndata) {
    if (ndata < MIUCHIZ_SECTOR_SIZE) {
        return -2;
    }

    char* aligned_buf = aligned_alloc(MIUCHIZ_ALIGNMENT_SIZE, ndata);
    memcpy(aligned_buf, data, ndata);

    lseek(handheld->fd, sector * MIUCHIZ_SECTOR_SIZE, SEEK_SET);
    int result = write(handheld->fd, aligned_buf, ndata);
    usleep(1000);
    
    //miuchiz_hex_dump(aligned_buf, 0x20);
    if (result == -1) {
        printf("miuchiz_handheld_write_sector failed. [%d] %s\n", errno, strerror(errno));
    }

    free(aligned_buf);

    return result;
}

int miuchiz_handheld_read_sector(struct Handheld* handheld, int sector, char* buf, size_t nbuf) {
    if (nbuf < MIUCHIZ_SECTOR_SIZE) {
        return -2;
    }

    // Data needs to be a multiple of sector size
    size_t required_size = miuchiz_round_size_up(nbuf, MIUCHIZ_SECTOR_SIZE); 

    char* aligned_buf = aligned_alloc(MIUCHIZ_ALIGNMENT_SIZE, required_size);

    lseek(handheld->fd, sector * MIUCHIZ_SECTOR_SIZE, SEEK_SET);
    int result = read(handheld->fd, aligned_buf, required_size);

    if (result >= 0) {
        memcpy(buf, aligned_buf, nbuf);
    }
    else {
        // printf("miuchiz_handheld_read_sector failed. [%d] %s\n", errno, strerror(errno));
    }

    free(aligned_buf);

    return result;
}

int miuchiz_handheld_send_scsi(struct Handheld* handheld, const char* data, size_t ndata) {
    // Data needs to be a multiple of sector size
    size_t required_size = miuchiz_round_size_up(ndata, MIUCHIZ_SECTOR_SIZE); 

    char* padded_data = aligned_alloc(MIUCHIZ_ALIGNMENT_SIZE, MIUCHIZ_SECTOR_SIZE);
    memset(padded_data, 0, required_size);
    memcpy(padded_data, data, ndata);

    int result = miuchiz_handheld_write_sector(handheld, MIUCHIZ_SECTOR_SCSI_WRITE, padded_data, required_size);

    free(padded_data);

    return result;
}

int miuchiz_handheld_read_page(struct Handheld* handheld, int page, char* buf, size_t nbuf) {
    if (nbuf != MIUCHIZ_PAGE_SIZE) {
        return -3;
    }

    struct __attribute__ ((packed)) {
        int length; 
        char data[MIUCHIZ_PAGE_SIZE];
    } page_data;

    miuchiz_handheld_send_scsi(handheld, MIUCHIZ_SCSI_SEQ_WRITE_FILEMARKS, sizeof(MIUCHIZ_SCSI_SEQ_WRITE_FILEMARKS));

    const char read_seq[] = {MIUCHIZ_SCSI_OPCODE_READ, 0, 0, page>>8, page&0xFF};
    miuchiz_handheld_send_scsi(handheld, read_seq, sizeof(read_seq));

    int read_result = miuchiz_handheld_read_sector(handheld, MIUCHIZ_SECTOR_DATA_READ, (char*)&page_data, sizeof(page_data));

    memcpy(buf, page_data.data, nbuf);

    miuchiz_handheld_send_scsi(handheld, MIUCHIZ_SCSI_SEQ_READ_REVERSE, sizeof(MIUCHIZ_SCSI_SEQ_READ_REVERSE));

    return read_result;
}

int miuchiz_handheld_write_page(struct Handheld* handheld, int page, const char* buf, size_t nbuf) {
    if (nbuf != MIUCHIZ_PAGE_SIZE) {
        return -1;
    }

    miuchiz_handheld_send_scsi(handheld, MIUCHIZ_SCSI_SEQ_WRITE_FILEMARKS, sizeof(MIUCHIZ_SCSI_SEQ_WRITE_FILEMARKS));

    const char write_seq[] = {MIUCHIZ_SCSI_OPCODE_WRITE, // opcode 
                              0, 0, page>>8, page&0xFF,  // destination, big endian
                              0x00, 0x00, 0x10, 0x00     // size, big endian
                              };
    miuchiz_handheld_send_scsi(handheld, write_seq, sizeof(write_seq));

    char* big_buf = malloc(nbuf*2);
    memcpy(big_buf, buf, nbuf);
    memcpy(big_buf+nbuf, buf, nbuf);


    int write_result = miuchiz_handheld_write_sector(handheld, MIUCHIZ_SECTOR_DATA_WRITE, big_buf, 2*nbuf);

    miuchiz_handheld_send_scsi(handheld, MIUCHIZ_SCSI_SEQ_READ_REVERSE, sizeof(MIUCHIZ_SCSI_SEQ_READ_REVERSE));

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
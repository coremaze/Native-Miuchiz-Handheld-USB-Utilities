#include "libmiuchiz.h"

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

int main(int argc, char** argv) {
    // Validate arguments
    if (argc != 2) {
        printf("Usage: %s <input flash dump>\n", argv[0]);
        return 1;
    }

    if (strlen(argv[1]) == 0) {
        printf("Must provide a file name\n");
        return 1;
    }

    // Get all handhelds
    struct Handheld** handhelds;
    int handheld_count = miuchiz_handheld_create_all(&handhelds);

    if (handhelds == NULL) {
        printf("Failed to search for handhelds.\n");
        return 1;
    }

    if (handheld_count != 1) {
        printf("%d handhelds are connected, but 1 is needed.\n", handheld_count);
        miuchiz_handheld_destroy_all(handhelds);
        return 1;
    }

    char* filename = argv[1];
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        printf("Unable to open %s for writing. [%d] %s\n", filename, errno, strerror(errno));
        miuchiz_handheld_destroy_all(handhelds);
        return 1;
    }

    size_t file_size = lseek(fd, 0, SEEK_END);

    if (file_size != MIUCHIZ_PAGE_SIZE * 0x200) {
        printf("Flash file must be 0x%X bytes.\n", MIUCHIZ_PAGE_SIZE * 0x200);
        miuchiz_handheld_destroy_all(handhelds);
        return 1;
    }

    struct Handheld* handheld = handhelds[0];

    for (int pagenum = 0; pagenum < 0x200; pagenum++) {
        int success = 0;
        char page[MIUCHIZ_PAGE_SIZE] = { 0 };
        printf("Writing page %d\n", pagenum);

        for (int retry = 0; retry < 5; retry++) {
            lseek(fd, pagenum * sizeof(page), SEEK_SET);
            int read_result = read(fd, page, sizeof(page));
            if (read_result == -1) {
                printf("Reading of page %d from file failed. Retrying.\n", pagenum);
                continue;
            }
            
            int write_result = miuchiz_handheld_write_page(handheld, pagenum, page, sizeof(page));
            if (write_result == -1) {
                printf("Writing of page %d to device failed. Retrying.\n", pagenum);
                continue;
            }

            success = 1;
            break;
        }

        if (success == 0) {
            printf("Reading of page %d has failed too many times.\n", pagenum);
            break;
        }
    }

    close(fd);
    miuchiz_handheld_destroy_all(handhelds);
    return 0;
}
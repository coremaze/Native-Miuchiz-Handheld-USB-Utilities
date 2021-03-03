#include "libmiuchiz.h"
#include "timer.h"

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

int main(int argc, char** argv) {
    // Validate arguments
    if (argc != 2) {
        printf("Usage: %s <output>\n", argv[0]);
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
    FILE* fp = fopen(filename, "wb");
    if (fp == NULL) {
        printf("Unable to open %s for writing. [%d] %s\n", filename, errno, strerror(errno));
        miuchiz_handheld_destroy_all(handhelds);
        return 1;
    }

    struct Handheld* handheld = handhelds[0];

    struct Utimer timer;
    miuchiz_utimer_start(&timer);

    int success = 0;
    for (int pagenum = 0; pagenum < MIUCHIZ_PAGE_COUNT; pagenum++) {
        success = 0;
        char page[MIUCHIZ_PAGE_SIZE] = { 0 };

        miuchiz_utimer_end(&timer);
        int seconds = miuchiz_utimer_elapsed(&timer) / 1000000;
        int minutes = seconds / 60;
        seconds = seconds % 60;
        printf("\r[%02d:%02d] Reading page %d/%d (%d%%)", 
               minutes,
               seconds,
               pagenum + 1,
               MIUCHIZ_PAGE_COUNT,
               (100 * (pagenum + 1)) / MIUCHIZ_PAGE_COUNT);
        fflush(stdout);

        for (int retry = 0; retry < 5; retry++) {
            int read_result = miuchiz_handheld_read_page(handheld, pagenum, page, sizeof(page));
            if (read_result == -1) {
                printf("\rReading of page %d failed. Retrying.\n", pagenum);
            }
            else {
                success = 1;
                break;
            }
        }

        if (success == 0) {
            printf("\rReading of page %d has failed too many times.\n", pagenum);
            break;
        }

        size_t write_result = fwrite(page, 1, sizeof(page), fp);
    }

    if (success) {
        printf("\n");
    }

    fclose(fp);
    miuchiz_handheld_destroy_all(handhelds);
    return 0;
}
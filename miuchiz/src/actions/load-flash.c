#include "libmiuchiz-usb.h"
#include "timer.h"

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <getopt.h>
#include <string.h>

struct args {
    char* device;
    char* infile;
    int check_changes;
};

static void usage(char* program_name) {
    fprintf(stderr, "Usage: %s [-d device] infile\n", program_name);
}

static int args_parse(struct args* args, int argc, char** argv) {
    int opt;
    int option_index;
    static struct option long_options[] = {
        {"device",        required_argument, 0, 'd' },
        {"check-changes", no_argument,       0, 'c'},
        {0,               0,                 0,  0 }
    };

    memset(args, 0, sizeof(*args));

    while ((opt = getopt_long(argc, argv, "d:c", (struct option*)&long_options, &option_index)) != -1) {
        switch (opt) {
            case 'd':
                args->device = strdup(optarg);
                break;
            case 'c':
                args->check_changes = 1;
                break;
            default:
                return 1;
                break;
        }
    }

    if (optind < argc) args->infile = strdup(argv[optind++]); else return 1;

    if (optind < argc) {
        return 1;
    }

    return 0;
}

static void args_free(struct args* args) {
    free(args->device);
    free(args->infile);
}

int load_flash_main(int argc, char** argv) {
    int result = 0;

    // Get arguments from the command line
    struct args args;
    if (args_parse(&args, argc, argv)) {
        usage(argv[0]);
        result = 1;
        goto leave_args;
    }

    // Get a list of all the connected handhelds
    struct Handheld** handhelds;
    int handheld_count = miuchiz_handheld_create_all(&handhelds);

    // Handle the case where something went wrong getting handhelds
    if (handhelds == NULL) {
        fprintf(stderr, "Failed to search for handhelds.\n");
        result = 1;
        goto leave_handhelds;
    }

    const char* specified_device = NULL;
    if (handheld_count == 0) {
        fprintf(stderr, "No handhelds are connected.\n");
        result = 1;
        goto leave_handhelds;
    }
    else if (handheld_count == 1 || args.device) {
        specified_device = args.device;
    }
    else {
        fprintf(stderr, "%d handhelds are connected. Specify 1 with -d or --device.\n", handheld_count);
        result = 1;
        goto leave_handhelds;
    }

    // Find the handheld
    struct Handheld* handheld = NULL;
    for (int i = 0; i < handheld_count; i++) {
        if (!specified_device || (specified_device && strcmp(specified_device, handhelds[i]->device) == 0)) {
            handheld = handhelds[i];
            break;
        }
    }

    if (!handheld) {
        if (specified_device) {
            fprintf(stderr, "No handheld was found at %s.\n", specified_device);
        }
        else {
            fprintf(stderr, "Unable to find handheld.\n");
        }
        result = 1;
        goto leave_handhelds;
    }

    FILE* fp = fopen(args.infile, "rb");
    if (fp == NULL) {
        printf("Unable to open %s for reading. [%d] %s\n", args.infile, errno, strerror(errno));
        result = 1;
        goto leave_file;
    }

    fseek(fp, 0, SEEK_END);
    size_t file_size = ftell(fp);

    if (file_size != MIUCHIZ_PAGE_SIZE * MIUCHIZ_PAGE_COUNT) {
        printf("Flash file must be 0x%X bytes.\n", MIUCHIZ_PAGE_SIZE * MIUCHIZ_PAGE_COUNT);
        miuchiz_handheld_destroy_all(handhelds);
        return 1;
    }

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
        printf("\r[%02d:%02d] Writing page %d/%d (%d%%)", 
               minutes,
               seconds,
               pagenum + 1,
               MIUCHIZ_PAGE_COUNT,
               (100 * (pagenum + 1)) / MIUCHIZ_PAGE_COUNT);
        fflush(stdout);

        for (int retry = 0; retry < 5; retry++) {
            fseek(fp, pagenum * sizeof(page), SEEK_SET);
            size_t read_result = fread(page, 1, sizeof(page), fp);
            if (read_result != sizeof(page)) {
                printf("\rReading of page %d from file failed. Retrying.\n", pagenum);
                continue;
            }
            
            /* If check-changes was specified, read the current page from the device.
             * If the page already on the device is already identical, then consider this 
             * page successfully written. The read involved here is much faster than 
             * writing, so this is normally faster if there are even a few identical pages. */

            if (args.check_changes) {
                char device_page[MIUCHIZ_PAGE_SIZE] = { 0 };
                int device_read_result = miuchiz_handheld_read_page(handheld, pagenum, device_page, sizeof(device_page));
                if (device_read_result == -1) {
                    printf("\rReading from page %d of device failed. Retrying.\n", pagenum);
                    continue;
                }
                if (memcmp(device_page, page, MIUCHIZ_PAGE_SIZE) == 0) {
                    success = 1;
                }
            }

            // This page may have already been considered successfully written due to check-changes
            if (!success) {
                int write_result = miuchiz_handheld_write_page(handheld, pagenum, page, sizeof(page));
                if (write_result == -1) {
                    printf("\rWriting of page %d to device failed. Retrying.\n", pagenum);
                    continue;
                }
            }

            success = 1;
            break;
        }

        if (success == 0) {
            printf("\rReading of page %d has failed too many times.\n", pagenum);
            break;
        }
    }

    if (success) {
        printf("\n");
    }
    else {
        result = 1;
    }

leave_file:
    fclose(fp);

leave_handhelds:
    miuchiz_handheld_destroy_all(handhelds);

leave_args:
    args_free(&args);

    return result;
}
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
    char* outfile;
};

static void usage(char* program_name) {
    fprintf(stderr, "Usage: %s [-d device] outfile\n", program_name);
}

static int args_parse(struct args* args, int argc, char** argv) {
    int opt;
    int option_index;
    static struct option long_options[] = {
        {"device", required_argument, 0, 'd' },
        {0,        0,                 0,  0 }
    };

    memset(args, 0, sizeof(*args));

    while ((opt = getopt_long(argc, argv, "d:", (struct option*)&long_options, &option_index)) != -1) {
        switch (opt) {
            case 'd':
                args->device = strdup(optarg);
                break;
            default:
                return 1;
                break;
        }
    }

    if (optind < argc) args->outfile = strdup(argv[optind++]); else return 1;

    if (optind < argc) {
        return 1;
    }

    return 0;
}

static void args_free(struct args* args) {
    free(args->device);
    free(args->outfile);
}

int dump_flash_main(int argc, char** argv) {
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

    FILE* fp = fopen(args.outfile, "wb");
    if (fp == NULL) {
        fprintf(stderr, "Unable to open %s for writing. [%d] %s\n", args.outfile, errno, strerror(errno));
        result = 1;
        goto leave_file;
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
    else {
        result = 1;
    }

leave_file:
    if (fp) {
        fclose(fp);
    }

leave_handhelds:
    miuchiz_handheld_destroy_all(handhelds);

leave_args:
    args_free(&args);

    return result;
}
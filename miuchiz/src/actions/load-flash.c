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
    char* mirrorfile;
    int check_changes;
};

struct setup_info {
    struct args args;
    FILE* infile_fp;
    FILE* mirrorfile_fp;
    struct Handheld** handhelds;
    struct Handheld* target_handheld;
};

static void usage(char* program_name) {
    fprintf(stderr, "Usage: %s [-d device] [-m mirrorfile] infile\n", program_name);
}

static int args_parse(struct args* args, int argc, char** argv) {
    int opt;
    int option_index;
    static struct option long_options[] = {
        {"device",        required_argument, 0, 'd' },
        {"check-changes", no_argument,       0, 'c'},
        {"mirror",        required_argument, 0, 'm' },
        {0,               0,                 0,  0 }
    };

    memset(args, 0, sizeof(*args));

    while ((opt = getopt_long(argc, argv, "d:cm:", (struct option*)&long_options, &option_index)) != -1) {
        switch (opt) {
            case 'd':
                args->device = strdup(optarg);
                break;
            case 'c':
                args->check_changes = 1;
                break;
            case 'm':
                args->mirrorfile = strdup(optarg);
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
    free(args->mirrorfile);
}

int copy_mirror(FILE* target, FILE* source) {
    if (!target || !source) {
        return 1;
    }

    char page[MIUCHIZ_PAGE_SIZE];
    for (int pagenum = 0; pagenum < MIUCHIZ_PAGE_COUNT; pagenum++) {
        fseek(source, MIUCHIZ_PAGE_SIZE * pagenum, SEEK_SET);
        fseek(target, MIUCHIZ_PAGE_SIZE * pagenum, SEEK_SET);
        if (fread(page, 1, sizeof(page), source) != sizeof(page)) {
            return 1;
        }
        if (fwrite(page, 1, sizeof(page), target) != sizeof(page)) {
            return 1;
        }
    }
    
    return 0;
}

int load_flash_setup(int argc, char** argv, struct setup_info* info) {
    info->infile_fp = NULL;
    info->mirrorfile_fp = NULL;
    info->handhelds = NULL;
    info->target_handheld = NULL;

    // Get arguments from the command line
    if (args_parse(&info->args, argc, argv)) {
        usage(argv[0]);
        return 1;
    }

    // Get a list of all the connected handhelds
    int handheld_count = miuchiz_handheld_create_all(&info->handhelds);

    // Handle the case where something went wrong getting handhelds
    if (info->handhelds == NULL) {
        fprintf(stderr, "Failed to search for handhelds.\n");
        return 1;
    }

    // Handle differently based on how many handhelds are connected
    const char* specified_device = NULL;
    if (handheld_count == 0) {
        fprintf(stderr, "No handhelds are connected.\n");
        return 1;
    }
    else if (handheld_count == 1 || info->args.device) {
        specified_device = info->args.device;
    }
    else {
        fprintf(stderr, "%d handhelds are connected. Specify 1 with -d or --device.\n", handheld_count);
    }

    // Find the handheld
    for (int i = 0; i < handheld_count; i++) {
        if (!specified_device || (specified_device && strcmp(specified_device, info->handhelds[i]->device) == 0)) {
            info->target_handheld = info->handhelds[i];
            break;
        }
    }

    // Leave if no target handheld was found
    if (!info->target_handheld) {
        if (specified_device) {
            fprintf(stderr, "No handheld was found at %s.\n", specified_device);
        }
        else {
            fprintf(stderr, "Unable to find handheld.\n");
        }
        return 1;
    }

    // Open the file that will be loaded onto the device
    info->infile_fp = fopen(info->args.infile, "rb");
    if (info->infile_fp == NULL) {
        printf("Unable to open %s for reading. [%d] %s\n", info->args.infile, errno, strerror(errno));
        return 1;
    }

    // Make sure the file to load onto the device is the right size
    fseek(info->infile_fp, 0, SEEK_END);
    size_t infile_size = ftell(info->infile_fp);

    if (infile_size != MIUCHIZ_PAGE_SIZE * MIUCHIZ_PAGE_COUNT) {
        printf("Flash file must be 0x%X bytes.\n", MIUCHIZ_PAGE_SIZE * MIUCHIZ_PAGE_COUNT);
        return 1;
    }

    /* Try to open the mirror file, if it doesn't open, we just won't
     * read from it. */
    if (info->args.mirrorfile) {
        info->mirrorfile_fp = fopen(info->args.mirrorfile, "rb");
        if (info->mirrorfile_fp) {
            fseek(info->mirrorfile_fp, 0, SEEK_END);
            size_t mirrorfile_size = ftell(info->mirrorfile_fp);
            
            if (mirrorfile_size != MIUCHIZ_PAGE_SIZE * MIUCHIZ_PAGE_COUNT) {
                printf("Mirror file must be 0x%X bytes.\n", MIUCHIZ_PAGE_SIZE * MIUCHIZ_PAGE_COUNT);
                return 1;
            }
        }
    }

    return 0;
}

int load_flash_process(struct setup_info* info) {
    struct Utimer timer;
    miuchiz_utimer_start(&timer);

    int page_write_success = 0;
    for (int pagenum = 0; pagenum < MIUCHIZ_PAGE_COUNT; pagenum++) {
        page_write_success = 0;
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
            // Read this page from the infile
            fseek(info->infile_fp, pagenum * sizeof(page), SEEK_SET);
            if (fread(page, 1, sizeof(page), info->infile_fp) != sizeof(page)) {
                printf("\rReading of page %d from file failed. Retrying.\n", pagenum);
                continue;
            }

            /* If a mirror file was opened, check whether the data to write already
             * matches the mirror file. Consider page successfully written if it matches. */
            if (info->mirrorfile_fp) {
                char mirrorfile_page[MIUCHIZ_PAGE_SIZE] = { 0 };
                fseek(info->mirrorfile_fp, pagenum * sizeof(mirrorfile_page), SEEK_SET);
                if (fread(mirrorfile_page, 1, sizeof(mirrorfile_page), info->mirrorfile_fp) != sizeof(mirrorfile_page)) {
                    printf("\rReading of page %d from mirror file failed. Retrying.\n", pagenum);
                    continue;
                }
                if (memcmp(mirrorfile_page, page, MIUCHIZ_PAGE_SIZE) == 0) {
                    page_write_success = 1;
                }
            }
            
            /* If check-changes was specified, read the current page from the device.
             * If the page already on the device is already identical, then consider this 
             * page successfully written. The read involved here is much faster than 
             * writing, so this is normally faster if there are even a few identical pages. */

            if (!page_write_success && info->args.check_changes) {
                char device_page[MIUCHIZ_PAGE_SIZE] = { 0 };
                int device_read_result = miuchiz_handheld_read_page(info->target_handheld, pagenum, device_page, sizeof(device_page));
                if (device_read_result == -1) {
                    printf("\rReading from page %d of device failed. Retrying.\n", pagenum);
                    continue;
                }
                if (memcmp(device_page, page, MIUCHIZ_PAGE_SIZE) == 0) {
                    page_write_success = 1;
                }
            }

            // This page may have already been considered successfully written due to check-changes
            if (!page_write_success) {
                int write_result = miuchiz_handheld_write_page(info->target_handheld, pagenum, page, sizeof(page));
                if (write_result == -1) {
                    printf("\rWriting of page %d to device failed. Retrying.\n", pagenum);
                    continue;
                }
            }

            page_write_success = 1;
            break;
        }

        if (page_write_success == 0) {
            printf("\rWriting of page %d has failed too many times.\n", pagenum);
            break;
        }
    }

    if (!page_write_success) {
        return 1;
    }

    printf("\n");

    if (info->args.mirrorfile) {
        /* If the transfer was successful, the mirror file needs to be updated
         * if one was provided. */
        if (info->mirrorfile_fp) {
            fclose(info->mirrorfile_fp);
        }

        info->mirrorfile_fp = fopen(info->args.mirrorfile, "wb");

        if (copy_mirror(info->mirrorfile_fp, info->infile_fp)) {
            printf("Failed to update mirror file.\n");
            return 1;
        }
    }
    
    return 0;
}

void load_flash_cleanup(struct setup_info* info) {
    if (info->infile_fp) {
        fclose(info->infile_fp);
    }

    if (info->mirrorfile_fp) {
        fclose(info->mirrorfile_fp);
    }

    if (info->handhelds) {
        miuchiz_handheld_destroy_all(info->handhelds);
    }

    args_free(&info->args);
}

int load_flash_main(int argc, char** argv) {
    struct setup_info setup_info;
    int result = 1;

    if (!load_flash_setup(argc, argv, &setup_info) && 
        !load_flash_process(&setup_info)) {
        result = 0;
    }

    load_flash_cleanup(&setup_info);

    return result;
}
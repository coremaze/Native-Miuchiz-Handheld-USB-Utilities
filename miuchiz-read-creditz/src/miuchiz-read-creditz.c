#include "libmiuchiz.h"

#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>

struct args {
    char* device;
};

static void usage(char* program_name) {
    fprintf(stderr, "Usage: %s [-d device]\n", program_name);
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

    if (optind < argc) {
        return 1;
    }

    return 0;
}

static void args_free(struct args* args) {
    free(args->device);
}

static int hcd_to_int(int hcd) {
    int result = 0;
    int place = 1;
    int digit;
    for (int i = 0; i < 4; i++) {
        digit = hcd & 0xF;
        result += digit * place;
        place *= 10;

        digit = (hcd & 0xF0) >> 4;
        result += digit * place;
        place *= 10;

        hcd >>= 8;
    }
    return result;
}

int main(int argc, char** argv) {
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
        if (specified_device || (specified_device && strcmp(specified_device, handhelds[i]->device) == 0)) {
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

    char page[MIUCHIZ_PAGE_SIZE] = { 0 };
    /* 0x9AA on page 0x1FF happens to be where creditz are stored.
     * There's not really a cleaner way to do this without mapping
     * out the entire page as a struct. */ 
    miuchiz_handheld_read_page(handheld, 0x1FF, page, sizeof(page));
    int creditz = hcd_to_int(*(unsigned int*)&page[0x9AA]);
    printf("%d\n", creditz);
    
leave_handhelds:
    miuchiz_handheld_destroy_all(handhelds);

leave_args:
    args_free(&args);

    return result;
}
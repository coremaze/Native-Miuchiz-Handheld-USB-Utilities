#include "libmiuchiz-usb.h"

#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <errno.h>
#include <string.h>

struct args {
    char* device;
    char* outfile;
    int do_checksum;
};

static void usage(char* program_name) {
    fprintf(stderr, "Usage: %s [-d device] [-c] outfile\n", program_name);
}

static int args_parse(struct args* args, int argc, char** argv) {
    int opt;
    int option_index;
    static struct option long_options[] = {
        {"device",   required_argument, 0, 'd' },
        {"checksum", no_argument,       0, 'c' },
        {0,        0,                 0,  0 }
    };

    memset(args, 0, sizeof(*args));

    args->do_checksum = 0;

    while ((opt = getopt_long(argc, argv, "d:c", (struct option*)&long_options, &option_index)) != -1) {
        switch (opt) {
            case 'd':
                args->device = strdup(optarg);
                break;
            case 'c':
                args->do_checksum = 1;
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

static u_int64_t checksum(void* buf, size_t n) {
    u_int8_t* buffer = (u_int8_t*)buf;
    u_int64_t result = 0;
    for (int i = 0; i < n; i++) {
        result += buffer[i];
    }
    return result;
}

int dump_otp_main(int argc, char** argv) {
    int result = 0;
    struct args args;
    int handheld_count = 0;
    struct Handheld** handhelds = NULL;
    struct Handheld* target_handheld = NULL;
    const char* specified_device = NULL;
    FILE* fp = NULL;
    char* otp = NULL;
    char* read_sector = NULL;

    // Get arguments from the command line
    if (args_parse(&args, argc, argv)) {
        usage(argv[0]);
        result = 1;
        goto leave;
    }

    handheld_count = miuchiz_handheld_create_all(&handhelds);

    // Handle the case where something went wrong getting handhelds
    if (handhelds == NULL) {
        fprintf(stderr, "Failed to search for handhelds.\n");
        result = 1;
        goto leave;
    }

    if (handheld_count == 0) {
        fprintf(stderr, "No handhelds are connected.\n");
        result = 1;
        goto leave;
    }
    else if (handheld_count == 1 || args.device) {
        specified_device = args.device;
    }
    else {
        fprintf(stderr, "%d handhelds are connected. Specify 1 with -d or --device.\n", handheld_count);
        result = 1;
        goto leave;
    }

    // Find the handheld
    for (int i = 0; i < handheld_count; i++) {
        if (!specified_device || (specified_device && strcmp(specified_device, handhelds[i]->device) == 0)) {
            target_handheld = handhelds[i];
            break;
        }
    }

    if (!target_handheld) {
        if (specified_device) {
            fprintf(stderr, "No handheld was found at %s.\n", specified_device);
        }
        else {
            fprintf(stderr, "Unable to find handheld.\n");
        }
        result = 1;
        goto leave;
    }

    fp = fopen(args.outfile, "wb");
    if (fp == NULL) {
        fprintf(stderr, "Unable to open %s for writing. [%d] %s\n", args.outfile, errno, strerror(errno));
        result = 1;
        goto leave;
    }

    /* When you read from sector 0, it exposes the OTP, repeating, and
     * beginning at offset 0xBDC in the OTP. */
    const size_t OTP_SIZE = 0x4000; // 16KiB
    const size_t OTP_STARTING_OFFSET = 0xBDC;
    otp = malloc(OTP_SIZE);
    read_sector = malloc(OTP_SIZE);

    if (miuchiz_handheld_read_sector(target_handheld, 0, read_sector, OTP_SIZE) != OTP_SIZE) {
        fprintf(stderr, "Failed to read OTP from device.\n");
        result = 1;
        goto leave;
    }

    // Put the last 0xBDC bytes at the start to correct OTP
    memcpy(&otp[0], &read_sector[OTP_SIZE - OTP_STARTING_OFFSET], OTP_STARTING_OFFSET);
    memcpy(&otp[OTP_STARTING_OFFSET], &read_sector[0], OTP_SIZE - OTP_STARTING_OFFSET);

    fwrite(otp, 1, OTP_SIZE, fp);

    if (args.do_checksum) {
        printf("Checksum: %lX\n", checksum(otp, OTP_SIZE));
    }

leave:
    if (fp) {
        fclose(fp);
    }

    if (handhelds) {
        miuchiz_handheld_destroy_all(handhelds);
    }

    if (otp) {
        free(otp);
    }

    if (read_sector) {
        free(read_sector);
    }

    args_free(&args);

    return result;
}
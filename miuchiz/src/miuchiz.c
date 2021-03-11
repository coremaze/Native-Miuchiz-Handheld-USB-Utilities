#include "actions/dump-flash.h"
#include "actions/eject.h"
#include "actions/load-flash.h"
#include "actions/read-creditz.h"
#include "actions/set-creditz.h"
#include "actions/status.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <getopt.h>

struct action {
    char* phrase;
    int (*function)(int argc, char** argv);
};

static struct action actions[] = {
    {"dump-flash", dump_flash_main},
    {"eject", eject_main},
    {"load-flash", load_flash_main},
    {"read-creditz", read_creditz_main},
    {"set-creditz", set_creditz_main},
    {"status", status_main},
    {NULL, NULL}
};

static void usage(char* program_name) {
    fprintf(stderr, "Usage: %s action [...]\n", program_name);
}

static void help(char* program_name) {
    printf("%s - Miuchiz Handheld USB Utilities\n", program_name);
    printf("Available actions:\n");

    for (struct action* action = (struct action*)&actions; 
         action->phrase != NULL && action->function != NULL;
         action++) { 
        printf("\t%s %s\n", program_name, action->phrase);
    }
}

static void version() {
    printf("%s\n", MIUCHIZ_UTILS_VERSION);
}

static int handle_opt(int argc, char** argv) {
    int result = 0;
    int old_opterr = opterr;
    opterr = 0;
    int opt;
    int option_index;
    static struct option long_options[] = {
        {"help",    no_argument, 0, 'h' },
        {"version", no_argument, 0, 'v' },
        {0,         0,           0,  0  }
    };

    while ((opt = getopt_long(argc, argv, "hv", (struct option*)&long_options, &option_index)) != -1) {
        switch (opt) {
            case 'h':
                help(argv[0]);
                result = 1;
                goto leave;
                break;
            case 'v':
                version();
                result = 1;
                goto leave;
                break;
            default:
                goto leave;
                break;
        }
    }

leave:
    optarg = NULL;
    optind = 0;
    opterr = old_opterr;
    return result;
}

int main(int argc, char** argv) {
    if (handle_opt(argc, argv)) {
        return 1;
    }

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    } 

    char* action_arg = argv[1];

    for (struct action* action = (struct action*)&actions; 
         action->phrase != NULL && action->function != NULL;
         action++) {
        
        if (strcmp(action_arg, action->phrase) == 0) {
            char** new_argv = malloc(sizeof(char*) * argc);
            int new_argc = argc - 1;

            // The command's argv[0] will be like "miuchiz <command>"
            int new_arg0_size = strlen(argv[0]) + sizeof(' ') + strlen(action_arg) + sizeof('\0');
            new_argv[0] = malloc(new_arg0_size);
            sprintf(new_argv[0], "%s %s", argv[0], action_arg);

            // argv[1] has been manually handled, so start at 2
            for (int i = 2; i < argc; i++) {
                new_argv[i - 1] = argv[i];
            }

            return action->function(new_argc, new_argv);
        }
    }

    fprintf(stderr, "Invalid action: %s\n", action_arg);

    return 1;
}
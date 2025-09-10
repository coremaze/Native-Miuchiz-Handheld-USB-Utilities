#include "actions/dump-flash.h"
#include "actions/dump-otp.h"
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
    {"dump-otp", dump_otp_main},
    {"eject", eject_main},
    {"load-flash", load_flash_main},
    {"read-creditz", read_creditz_main},
    {"set-creditz", set_creditz_main},
    {"status", status_main},
    {NULL, NULL}
};

static void usage(const char* program_name) {
    fprintf(stderr, "Usage: %s action [...]\n", program_name);
}

static void help(const char* program_name) {
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

static int handle_opt(int argc, char** argv, const char* program_name) {
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
                help(program_name);
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
    int result = 0;

    /* This exists to make it easier to work with snaps.
     * Running this program with snap will change what argv[0] is,
     * so we are allowing the snap installation to just specify its own name.
     */
    char* program_name;
    const char* env_program_name = getenv("MIUCHIZ_UTILS_NAME");

    if (env_program_name == NULL) {
        program_name = strdup(argv[0]);
    }
    else {
        program_name = strdup(env_program_name);
    }

    if (handle_opt(argc, argv, program_name)) {
        result = 1;
        goto leave;
    }

    if (argc < 2) {
        usage(program_name);
        result = 1;
        goto leave;
    } 

    char* action_arg = argv[1];

    for (struct action* action = (struct action*)&actions; 
         action->phrase != NULL && action->function != NULL;
         action++) {
        
        if (strcmp(action_arg, action->phrase) == 0) {
            char** new_argv = malloc(sizeof(char*) * (argc));
            int new_argc = argc - 1;

            // The command's argv[0] will be like "miuchiz <command>"
            size_t new_arg0_size = strlen(argv[0]) + 1 /* space */ + strlen(action_arg) + 1 /* NUL */;
            new_argv[0] = malloc(new_arg0_size);
            snprintf(new_argv[0], new_arg0_size, "%s %s", argv[0], action_arg);

            // argv[1] has been manually handled, so start at 2
            for (int i = 2; i < argc; i++) {
                new_argv[i - 1] = argv[i];
            }
            new_argv[new_argc] = NULL;

            result = action->function(new_argc, new_argv);
            
            free(new_argv[0]);
            free(new_argv); 

            goto leave;
        }
    }

    fprintf(stderr, "Invalid action: %s\n", action_arg);

leave:
    free(program_name);
    return result;
}
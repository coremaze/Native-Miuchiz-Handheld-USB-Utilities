/*
 * Runs the Miuchiz Reborn storage-location policy's shared conformance suite
 * (test-vectors.txt, vendored from the miuchiz-reborn-paths repository)
 * against this library's C implementation of the policy's runtime category.
 * A policy change lands in the vector file first, and this test fails until
 * the C side follows.
 *
 * Usage: paths-conformance <path/to/test-vectors.txt>
 */

#include "backend-internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
    #define CURRENT_PLATFORM "windows"
    /* MSVC and MinGW spell POSIX strtok_r as strtok_s (same signature). */
    #define strtok_r(s, delim, ctx) strtok_s(s, delim, ctx)
#elif defined(__APPLE__)
    #define CURRENT_PLATFORM "macos"
#else
    #define CURRENT_PLATFORM "linux"
#endif

static void set_env(const char* key, const char* value) {
#if defined(_WIN32)
    _putenv_s(key, value != NULL ? value : "");
#else
    if (value != NULL && value[0] != '\0') {
        setenv(key, value, 1);
    }
    else {
        unsetenv(key);
    }
#endif
}

/* Applies "KEY=VALUE,KEY=,..." (empty VALUE unsets). Modifies its copy. */
static void apply_env(char* env) {
    char* saveptr = NULL;
    for (char* item = strtok_r(env, ",", &saveptr); item != NULL;
         item = strtok_r(NULL, ",", &saveptr)) {
        char* eq = strchr(item, '=');
        if (eq == NULL) {
            continue;
        }
        *eq = '\0';
        set_env(item, eq + 1);
    }
}

static void normalize_slashes(char* s) {
    for (; *s != '\0'; s++) {
        if (*s == '\\') {
            *s = '/';
        }
    }
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <test-vectors.txt>\n", argv[0]);
        return 2;
    }
    FILE* f = fopen(argv[1], "r");
    if (f == NULL) {
        fprintf(stderr, "Cannot open %s\n", argv[1]);
        return 2;
    }

    int ran = 0;
    int failed = 0;
    char line[2048];
    while (fgets(line, sizeof(line), f) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        /* <platforms>|<env>|<category>|<app>|<expected> */
        char* fields[5];
        char* saveptr = NULL;
        int nfields = 0;
        for (char* field = strtok_r(line, "|", &saveptr);
             field != NULL && nfields < 5;
             field = strtok_r(NULL, "|", &saveptr)) {
            fields[nfields++] = field;
        }
        if (nfields != 5) {
            fprintf(stderr, "FAIL malformed vector line\n");
            failed++;
            continue;
        }
        const char* platforms = fields[0];
        char* env = fields[1];
        const char* category = fields[2];
        const char* app = fields[3];
        const char* expected = fields[4];

        if (strcmp(platforms, "all") != 0 && strcmp(platforms, CURRENT_PLATFORM) != 0) {
            continue;
        }
        /* This implementation mirrors only the runtime category. */
        if (strcmp(category, "runtime") != 0) {
            continue;
        }

        apply_env(env);

        char got[1024];
        if (miuchiz_emu_runtime_dir(app, got, sizeof(got)) != 0) {
            fprintf(stderr, "FAIL runtime_dir(%s) errored (expected %s)\n", app, expected);
            failed++;
            continue;
        }
        normalize_slashes(got);
        if (strcmp(got, expected) != 0) {
            fprintf(stderr, "FAIL runtime_dir(%s) = %s, expected %s\n", app, got, expected);
            failed++;
        }
        ran++;
    }
    fclose(f);

    /* The endpoint directory adds the EMIU2_USB_DIR override on top of the
     * policy; check the precedence here since it is not in the shared file. */
    {
        char got[1024];
        set_env("MIUCHIZ_REBORN_HOME", "/mr");
        set_env("EMIU2_USB_DIR", "/custom/usb");
        if (miuchiz_emu_endpoint_dir(got, sizeof(got)) != 0 || strcmp(got, "/custom/usb") != 0) {
            fprintf(stderr, "FAIL EMIU2_USB_DIR should override the policy\n");
            failed++;
        }
        set_env("EMIU2_USB_DIR", NULL);
        if (miuchiz_emu_endpoint_dir(got, sizeof(got)) != 0
            || strcmp(got, "/mr/runtime/emiu2") != 0) {
            fprintf(stderr, "FAIL endpoint dir should fall back to the policy (got %s)\n", got);
            failed++;
        }
        ran += 2;
    }

    if (ran < 3) {
        fprintf(stderr, "FAIL suspiciously few vectors ran (%d)\n", ran);
        failed++;
    }
    printf("paths-conformance: %d checks, %d failures\n", ran, failed);
    return failed == 0 ? 0 : 1;
}

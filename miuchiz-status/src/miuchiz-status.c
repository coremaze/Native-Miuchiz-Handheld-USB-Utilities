#include "libmiuchiz.h"

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>

union miuchiz_u16_t {
    uint16_t val;
    uint8_t b[2];
};

union miuchiz_u32_t {
    uint32_t val;
    uint8_t b[4];
};

char* units[] = {"Cloe", "Yasmin", "Spike", "Dash", "Roc", "Creeper", "Inferno"};

int main(int argc, char** argv) {
    struct Handheld** handhelds;
    int handheld_count = miuchiz_handheld_create_all(&handhelds);

    if (handhelds == NULL) {
        fprintf(stderr, "Failed to search for handhelds.\n");
        return 1;
    }

    // This is put into stderr to make the result easier to process with shell commands
    fprintf(stderr, "%d handhelds are connected.\n", handheld_count);

    for (int i = 0; i < handheld_count; i++) {
        struct Handheld* handheld = handhelds[i];

        char page[MIUCHIZ_PAGE_SIZE] = { 0 };

        /* There's not really a cleaner way to do this without mapping
         * out the entire page as a struct. */ 
        miuchiz_handheld_read_page(handheld, 0x1FF, page, sizeof(page));

        union miuchiz_u16_t major_version = *(union miuchiz_u16_t*)&page[0x9A4];
        uint8_t unit_id = *(uint8_t*)&page[0x9A8];
        char* unit = unit_id < (sizeof(units) / sizeof(*units)) ? units[unit_id] : "Unknown";

        printf("Device: %s; Major version: %d.%02d; Character: %s\n", 
                handheld->device,
                major_version.b[1], major_version.b[0],
                unit);
    }

    miuchiz_handheld_destroy_all(handhelds);
    return 0;
}
#include "libmiuchiz-usb.h"

#include <stdlib.h>
#include <stdio.h>


static char* units[] = {"Cloe", "Yasmin", "Spike", "Dash", "Roc", "Creeper", "Inferno"};

int status_main(int argc, char** argv) {
    struct Handheld** handhelds;
    int handheld_count = miuchiz_handheld_create_all(&handhelds);

    if (handhelds == NULL) {
        fprintf(stderr, "Failed to search for handhelds.\n");
        return 1;
    }

    // This is put into stderr to make the result easier to process with shell commands
    fprintf(stderr, "Handhelds connected: %d\n", handheld_count);

    for (int i = 0; i < handheld_count; i++) {
        struct Handheld* handheld = handhelds[i];

        char page[MIUCHIZ_PAGE_SIZE] = { 0 };

        /* There's not really a cleaner way to do this without mapping
         * out the entire page as a struct. */ 
        miuchiz_handheld_read_page(handheld, 0x1FF, page, sizeof(page));

        uint16_t major_version = miuchiz_le16_read(&page[0x9A4]);
        uint8_t major_version_upper = (major_version >> 8) & 0xFF;
        uint8_t major_version_lower = major_version & 0xFF;
        uint8_t unit_id = page[0x9A8];
        char* unit = unit_id < (sizeof(units) / sizeof(*units)) ? units[unit_id] : "Unknown";

        printf("Device: %s; Major version: %d.%02d; Character: %s\n", 
                handheld->device,
                major_version_upper, major_version_lower,
                unit);
    }

    miuchiz_handheld_destroy_all(handhelds);
    return 0;
}
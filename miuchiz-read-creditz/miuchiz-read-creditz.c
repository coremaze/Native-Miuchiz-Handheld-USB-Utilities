#include "libmiuchiz.h"

#include <stdlib.h>
#include <stdio.h>

int hcd_to_int(int hcd) {
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
    struct Handheld** handhelds;
    int handheld_count = miuchiz_handheld_create_all(&handhelds);

    if (handhelds == NULL) {
        printf("Failed to search for handhelds.\n");
        return 1;
    }

    if (handheld_count != 1) {
        printf("%d handhelds are connected.\n", handheld_count);
    }

    for (int i = 0; i < handheld_count; i++) {
        struct Handheld* handheld = handhelds[i];

        char page[MIUCHIZ_PAGE_SIZE] = { 0 };

        miuchiz_handheld_read_page(handheld, 0x1FF, page, sizeof(page));

        int creditz = hcd_to_int(*(unsigned int*)&page[0x9AA]);

        printf("%d\n", creditz);
    }

    miuchiz_handheld_destroy_all(handhelds);
    return 0;
}
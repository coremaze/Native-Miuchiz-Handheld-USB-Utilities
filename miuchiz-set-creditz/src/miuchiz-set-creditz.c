#include "libmiuchiz.h"

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

int int_to_hcd(int num) {
    int result = 0;
    int place = 1;
    int digit;
    for (int i = 0; i < 8; i++) {
        digit = num % 10;
        result += digit * place;
        place *= 0x10;
        num /= 10;
    }
    return result;
}

int main(int argc, char** argv) {
    // Validate arguments
    if (argc != 2) {
        printf("Usage: %s <creditz>\n", argv[0]);
        return 1;
    }

    // Ensure creditz is a number
    for (int i = 0; i < strlen(argv[1]); i++) {
        if (!isdigit(argv[1][i])) {
            printf("Invalid creditz amount: %s\n", argv[1]);
            return 1;
        }
    }

    int creditz = atoi(argv[1]);

    // Get all handhelds
    struct Handheld** handhelds;
    int handheld_count = miuchiz_handheld_create_all(&handhelds);

    if (handhelds == NULL) {
        printf("Failed to search for handhelds.\n");
        return 1;
    }

    if (handheld_count != 1) {
        printf("%d handhelds are connected.\n", handheld_count);
    }

    // Set creditz for each handheld.
    for (int i = 0; i < handheld_count; i++) {
        struct Handheld* handheld = handhelds[i];

        char page[MIUCHIZ_PAGE_SIZE] = { 0 };

        miuchiz_handheld_read_page(handheld, 0x1FF, page, sizeof(page));

        *(unsigned int*)&page[0x9AA] = int_to_hcd(creditz);

        miuchiz_handheld_write_page(handheld, 0x1FF, page, sizeof(page));
    }

    miuchiz_handheld_destroy_all(handhelds);
    return 0;
}
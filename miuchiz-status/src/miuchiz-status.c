#include "libmiuchiz.h"

#include <stdlib.h>
#include <stdio.h>

int main(int argc, char** argv) {
    struct Handheld** handhelds;
    int handheld_count = miuchiz_handheld_create_all(&handhelds);

    if (handhelds == NULL) {
        printf("Failed to search for handhelds.\n");
        return 1;
    }

    printf("%d handhelds are connected.\n", handheld_count);

    for (int i = 0; i < handheld_count; i++) {
        struct Handheld* handheld = handhelds[i];

        printf("%s\n", handheld->device);
    }

    miuchiz_handheld_destroy_all(handhelds);
    return 0;
}
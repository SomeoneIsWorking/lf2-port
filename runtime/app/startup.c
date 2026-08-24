#include "startup.h"

#include "boot_guest.h"

#include <stdio.h>

int startup_load_data(uint32_t self, uint32_t mode, uint32_t frame_surface)
{
    if (!self || !boot_guest_load_required(mode)) return 0;

    fprintf(stderr, "startup: loading game data synchronously\n");
    boot_guest_load_data(self, frame_surface);
    fprintf(stderr, "startup: data loaded; entering the mode menu\n");
    return 1;
}

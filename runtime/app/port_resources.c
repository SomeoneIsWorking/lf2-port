#include "port_resources.h"

#include <SDL3/SDL.h>

#ifdef __ANDROID__
#include <SDL3/SDL_system.h>
#endif

#include <stdio.h>

int port_resources_stages(char *output, size_t capacity)
{
    if (!output || capacity == 0) return 0;
#ifdef __ANDROID__
    const char *base = SDL_GetAndroidInternalStoragePath();
    const char *separator = "/";
#else
    const char *base = SDL_GetBasePath();
    const char *separator = ""; /* SDL base paths include their trailing separator. */
#endif
    if (!base || !*base) return 0;
    const int written = snprintf(output, capacity, "%s%sstages", base, separator);
    return written >= 0 && (size_t)written < capacity;
}

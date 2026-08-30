#include "user_paths.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int join_config_path(const char *directory, char *output, size_t capacity, char *error, size_t error_capacity)
{
    const size_t length = strlen(directory);
    const char *separator = length && (directory[length - 1] == '/' || directory[length - 1] == '\\') ? "" : "/";
    const int written = snprintf(output, capacity, "%s%slf2.cfg", directory, separator);
    if (written < 0 || (size_t)written >= capacity) {
        snprintf(error, error_capacity, "settings path is longer than %zu bytes", capacity - 1);
        return 0;
    }
    return 1;
}

int user_paths_config_file(char *output, size_t capacity, int create_directory, char *error, size_t error_capacity)
{
    if (!output || capacity == 0 || !error || error_capacity == 0) return 0;
    output[0] = 0;
    error[0] = 0;

#if defined(__linux__) && !defined(__ANDROID__)
    char directory[4096];
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0] == '/') {
        if (snprintf(directory, sizeof directory, "%s/lf2-port", xdg) >= (int)sizeof directory) {
            snprintf(error, error_capacity, "XDG_CONFIG_HOME is too long");
            return 0;
        }
    } else {
        const char *home = getenv("HOME");
        if (!home || home[0] != '/') {
            snprintf(error, error_capacity, "neither an absolute XDG_CONFIG_HOME nor HOME is available");
            return 0;
        }
        if (snprintf(directory, sizeof directory, "%s/.config/lf2-port", home) >= (int)sizeof directory) {
            snprintf(error, error_capacity, "HOME is too long");
            return 0;
        }
    }
    if (create_directory && !SDL_CreateDirectory(directory)) {
        snprintf(error, error_capacity, "cannot create %s: %s", directory, SDL_GetError());
        return 0;
    }
    return join_config_path(directory, output, capacity, error, error_capacity);
#else
    char *directory = SDL_GetPrefPath("SomeoneIsWorking", "LF2 Port");
    if (!directory) {
        snprintf(error, error_capacity, "cannot resolve the application settings directory: %s", SDL_GetError());
        return 0;
    }
    const int result = join_config_path(directory, output, capacity, error, error_capacity);
    SDL_free(directory);
    return result;
#endif
}

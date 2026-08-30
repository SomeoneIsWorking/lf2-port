#include "user_paths.h"

#include <lucent/platform_c.h>

#include <stdio.h>
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

    const char *directory = lucent_platform_user_data_directory("lf2-port");
    if (!directory) {
        snprintf(error, error_capacity, "cannot resolve the OS user-data directory for lf2-port");
        return 0;
    }
    if (create_directory && !lucent_platform_ensure_user_data_directory("lf2-port")) {
        snprintf(error, error_capacity, "cannot create the OS user-data directory: %s", directory);
        return 0;
    }
    return join_config_path(directory, output, capacity, error, error_capacity);
}

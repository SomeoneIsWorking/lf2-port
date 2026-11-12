#include "user_paths.h"

#include <lucent/platform_c.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static int join_path(const char *directory, const char *name, char *output, size_t capacity, char *error,
                     size_t error_capacity)
{
    const size_t length = strlen(directory);
    const char *separator = length && (directory[length - 1] == '/' || directory[length - 1] == '\\') ? "" : "/";
    const int written = snprintf(output, capacity, "%s%s%s", directory, separator, name);
    if (written < 0 || (size_t)written >= capacity) {
        snprintf(error, error_capacity, "%s path is longer than %zu bytes", name, capacity - 1);
        return 0;
    }
    return 1;
}

static int user_path(const char *name, char *output, size_t capacity, int create_directory, char *error,
                     size_t error_capacity)
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
    return join_path(directory, name, output, capacity, error, error_capacity);
}

int user_paths_config_file(char *output, size_t capacity, int create_directory, char *error, size_t error_capacity)
{
    return user_path("lf2.cfg", output, capacity, create_directory, error, error_capacity);
}

int user_paths_zerotier_directory(char *output, size_t capacity, char *error, size_t error_capacity)
{
    return user_path("zerotier", output, capacity, 1, error, error_capacity);
}

int user_paths_scratch_directory(char *output, size_t capacity, char *error, size_t error_capacity)
{
    if (!user_path("scratch", output, capacity, 1, error, error_capacity)) return 0;
    if (mkdir(output, 0700) != 0 && errno != EEXIST) {
        snprintf(error, error_capacity, "cannot create %s", output);
        return 0;
    }
    return 1;
}

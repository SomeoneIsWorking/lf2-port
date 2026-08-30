#include "game_data.h"

#include <errno.h>
#include <libgen.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef LF2_EXPECTED_EXE_SIZE
#define LF2_EXPECTED_EXE_SIZE 3424256u
#endif
#ifndef LF2_EXPECTED_EXE_CRC32
#define LF2_EXPECTED_EXE_CRC32 0x937da83cu
#endif

static int regular_file(const char *path)
{
    struct stat status;
    return stat(path, &status) == 0 && S_ISREG(status.st_mode);
}

static int path_join(char *output, size_t capacity, const char *directory, const char *name)
{
    const size_t length = strlen(directory);
    const char *separator = length && directory[length - 1] == '/' ? "" : "/";
    const int written = snprintf(output, capacity, "%s%s%s", directory, separator, name);
    return written >= 0 && (size_t)written < capacity;
}

static int file_crc32(const char *path, uint32_t *checksum, size_t *size, char *error, size_t error_capacity)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        snprintf(error, error_capacity, "%s: %s", path, strerror(errno));
        return 0;
    }
    unsigned char buffer[64 * 1024];
    uint32_t crc = UINT32_MAX;
    size_t total = 0;
    while (!feof(file)) {
        const size_t count = fread(buffer, 1, sizeof buffer, file);
        if (count) {
            for (size_t index = 0; index < count; ++index) {
                crc ^= buffer[index];
                for (int bit = 0; bit < 8; ++bit) {
                    const uint32_t low_bit_mask = 0u - (crc & 1u);
                    crc = (crc >> 1) ^ (0xedb88320u & low_bit_mask);
                }
            }
            total += count;
        }
        if (ferror(file)) {
            snprintf(error, error_capacity, "%s: read failed", path);
            fclose(file);
            return 0;
        }
    }
    fclose(file);
    *checksum = ~crc;
    *size = total;
    return 1;
}

int game_data_validate_executable(const char *executable, GameData *result)
{
    memset(result, 0, sizeof *result);
    if (!executable || !*executable) {
        snprintf(result->error, sizeof result->error, "no LF2 executable was selected");
        return 0;
    }

    char canonical[GAME_DATA_PATH_CAPACITY];
    if (!realpath(executable, canonical)) {
        snprintf(result->error, sizeof result->error, "%s: %s", executable, strerror(errno));
        return 0;
    }

    char leaf_buffer[GAME_DATA_PATH_CAPACITY];
    char root_buffer[GAME_DATA_PATH_CAPACITY];
    snprintf(leaf_buffer, sizeof leaf_buffer, "%s", canonical);
    snprintf(root_buffer, sizeof root_buffer, "%s", canonical);
    if (strcasecmp(basename(leaf_buffer), "lf2.exe") != 0) {
        snprintf(result->error, sizeof result->error, "%s is not lf2.exe from Little Fighter 2 v2.0a", canonical);
        return 0;
    }

    uint32_t checksum = 0;
    size_t size = 0;
    if (!file_crc32(canonical, &checksum, &size, result->error, sizeof result->error)) return 0;
    if (size != LF2_EXPECTED_EXE_SIZE || checksum != LF2_EXPECTED_EXE_CRC32) {
        snprintf(result->error, sizeof result->error,
                 "%s is not the supported LF2 v2.0a executable "
                 "(size %zu, CRC32 %08x)",
                 canonical, size, checksum);
        return 0;
    }

    const char *root = dirname(root_buffer);
    char data_file[GAME_DATA_PATH_CAPACITY];
    if (!path_join(data_file, sizeof data_file, root, "data/data.txt") || !regular_file(data_file)) {
        snprintf(result->error, sizeof result->error,
                 "%s is the correct executable, but %s/data/data.txt is missing; "
                 "select lf2.exe inside the complete extracted LF2 v2.0a tree",
                 canonical, root);
        return 0;
    }

    snprintf(result->root, sizeof result->root, "%s", root);
    snprintf(result->executable, sizeof result->executable, "%s", canonical);
    return 1;
}

static int validate_root(const char *root, GameData *result)
{
    char executable[GAME_DATA_PATH_CAPACITY];
    if (!path_join(executable, sizeof executable, root, "lf2.exe")) {
        snprintf(result->error, sizeof result->error, "game-data path is too long: %s", root);
        return 0;
    }
    return game_data_validate_executable(executable, result);
}

static int appimage_game_path(const char *appimage, char *output, size_t capacity)
{
    char copy[GAME_DATA_PATH_CAPACITY];
    if (!appimage || !*appimage || snprintf(copy, sizeof copy, "%s", appimage) >= (int)sizeof copy) return 0;
    return path_join(output, capacity, dirname(copy), "game");
}

int game_data_discover(const char *explicit_executable, const char *configured_root, const char *appimage,
                       const char *working_directory, GameData *result)
{
    memset(result, 0, sizeof *result);
    if (explicit_executable && *explicit_executable) return game_data_validate_executable(explicit_executable, result);

    char configured_error[GAME_DATA_ERROR_CAPACITY] = "";
    if (configured_root && *configured_root) {
        if (validate_root(configured_root, result)) return 1;
        snprintf(configured_error, sizeof configured_error, "%s", result->error);
    }

    char sibling[GAME_DATA_PATH_CAPACITY];
    if (appimage_game_path(appimage, sibling, sizeof sibling) && validate_root(sibling, result)) return 1;

    if (working_directory && *working_directory) {
        char direct[GAME_DATA_PATH_CAPACITY];
        if (path_join(direct, sizeof direct, working_directory, "lf2.exe") &&
            game_data_validate_executable(direct, result))
            return 1;
        char nested[GAME_DATA_PATH_CAPACITY];
        if (path_join(nested, sizeof nested, working_directory, "game") && validate_root(nested, result)) return 1;
    }

    if (appimage_game_path(appimage, sibling, sizeof sibling)) {
        snprintf(result->error, sizeof result->error,
                 "Little Fighter 2 v2.0a game files are not configured.\n\n"
                 "Choose lf2.exe inside a complete extracted LF2 v2.0a tree, or place that "
                 "tree here:\n%s\n\nThe AppImage does not contain the original game files.%s%s",
                 sibling, configured_error[0] ? "\n\nThe saved selection was refused:\n" : "", configured_error);
    } else {
        snprintf(result->error, sizeof result->error,
                 "Little Fighter 2 v2.0a game files are not configured.\n\n"
                 "Choose lf2.exe inside a complete extracted LF2 v2.0a tree.%s%s",
                 configured_error[0] ? "\n\nThe saved selection was refused:\n" : "", configured_error);
    }
    return 0;
}

int game_data_activate(GameData *data)
{
    if (chdir(data->root) == 0) return 1;
    snprintf(data->error, sizeof data->error, "cannot enter game-data directory %s: %s", data->root, strerror(errno));
    return 0;
}

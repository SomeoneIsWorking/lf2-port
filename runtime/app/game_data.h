#ifndef LF2_GAME_DATA_H
#define LF2_GAME_DATA_H

#include <stddef.h>

enum { GAME_DATA_PATH_CAPACITY = 4096, GAME_DATA_ERROR_CAPACITY = 1024 };

typedef struct GameData {
    char root[GAME_DATA_PATH_CAPACITY];
    char executable[GAME_DATA_PATH_CAPACITY];
    char error[GAME_DATA_ERROR_CAPACITY];
} GameData;

/* Validate the exact LF2 v2.0a executable and its critical sibling data. */
int game_data_validate_executable(const char *executable, GameData *result);
int game_data_validate_root(const char *root, GameData *result);

/* Shipping discovery order: explicit developer path, persisted player selection,
 * outer-AppImage sibling game/, then the two source-tree development layouts. */
int game_data_discover(const char *explicit_executable, const char *configured_root, const char *appimage,
                       const char *working_directory, GameData *result);

/* Enter the validated game root. Guest files are Windows-relative paths, so this
 * working-directory boundary remains part of the guest ABI. */
int game_data_activate(GameData *data);

#endif

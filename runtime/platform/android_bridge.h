#ifndef LF2_ANDROID_BRIDGE_H
#define LF2_ANDROID_BRIDGE_H

#include "setup_ui.h"

#include <stddef.h>

int android_bridge_initialize(void);
int android_bridge_game_root(char *output, size_t capacity);
SetupUiResult android_bridge_choose_game_tree(const char *message, char *selection, size_t capacity);
int android_bridge_commit_game_tree(const char *staging_root, char *output, size_t capacity, char *error,
                                    size_t error_capacity);

#endif

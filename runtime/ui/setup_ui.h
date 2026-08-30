#ifndef LF2_SETUP_UI_H
#define LF2_SETUP_UI_H

#include <stddef.h>

typedef enum SetupUiResult {
    SETUP_UI_ERROR = -1,
    SETUP_UI_CANCELLED = 0,
    SETUP_UI_SELECTED = 1,
} SetupUiResult;

/* Present the terminal-free game-file setup boundary and return the selected
 * lf2.exe path. Validation and persistence remain with runtime/app. */
SetupUiResult setup_ui_choose_game(const char *message, char *selection, size_t capacity);

#endif

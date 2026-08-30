#include "setup_ui.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <string.h>

#ifdef __ANDROID__
#include "android_bridge.h"
#endif

#ifndef __ANDROID__
enum { BUTTON_QUIT = 0, BUTTON_BROWSE = 1 };

typedef struct FileDialogState {
    SDL_Mutex *mutex;
    int done;
    int failed;
    char selection[4096];
    char error[512];
} FileDialogState;

static void SDLCALL file_selected(void *userdata, const char *const *files, int filter)
{
    (void)filter;
    FileDialogState *state = userdata;
    SDL_LockMutex(state->mutex);
    if (!files) {
        state->failed = 1;
        snprintf(state->error, sizeof state->error, "%s", SDL_GetError());
    } else if (files[0]) {
        const int written = snprintf(state->selection, sizeof state->selection, "%s", files[0]);
        if (written < 0 || (size_t)written >= sizeof state->selection) {
            state->selection[0] = 0;
            state->failed = 1;
            snprintf(state->error, sizeof state->error, "the selected path is longer than %zu bytes",
                     sizeof state->selection - 1);
        }
    }
    state->done = 1;
    SDL_UnlockMutex(state->mutex);
}

static int browse_button(const char *message)
{
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, BUTTON_QUIT, "Quit"},
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, BUTTON_BROWSE, "Browse…"},
    };
    const SDL_MessageBoxData box = {
        SDL_MESSAGEBOX_ERROR,
        NULL,
        "Little Fighter 2 game files required",
        message,
        (int)SDL_arraysize(buttons),
        buttons,
        NULL,
    };
    int button = BUTTON_QUIT;
    if (!SDL_ShowMessageBox(&box, &button)) {
        fprintf(stderr, "setup: cannot show the game-file dialog: %s\n", SDL_GetError());
        return BUTTON_QUIT;
    }
    return button;
}
#endif

SetupUiResult setup_ui_choose_game(const char *message, char *selection, size_t capacity)
{
    if (!selection || capacity == 0) return SETUP_UI_ERROR;
    selection[0] = 0;
    fprintf(stderr, "setup: %s\n", message);

#ifdef __ANDROID__
    return android_bridge_choose_game_tree(message, selection, capacity);
#else

    SDL_SetAppMetadata("LF2 Port", NULL, "io.github.SomeoneIsWorking.lf2-port");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "setup: SDL video initialization failed: %s\n", SDL_GetError());
        return SETUP_UI_ERROR;
    }
    if (browse_button(message) != BUTTON_BROWSE) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return SETUP_UI_CANCELLED;
    }

    FileDialogState state = {0};
    state.mutex = SDL_CreateMutex();
    if (!state.mutex) {
        fprintf(stderr, "setup: cannot create file-dialog state: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return SETUP_UI_ERROR;
    }
    const SDL_DialogFileFilter filters[] = {
        {"LF2 v2.0a executable", "exe"},
        {"ZIP archive", "zip"},
    };
    SDL_ShowOpenFileDialog(file_selected, &state, NULL, filters, 2, SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS), false);

    for (;;) {
        SDL_LockMutex(state.mutex);
        const int done = state.done;
        SDL_UnlockMutex(state.mutex);
        if (done) break;
        SDL_PumpEvents();
        SDL_Delay(10);
    }

    SDL_LockMutex(state.mutex);
    const int failed = state.failed;
    const int selected = state.selection[0] != 0;
    char dialog_error[sizeof state.error];
    snprintf(dialog_error, sizeof dialog_error, "%s", state.error);
    if (selected) {
        const int written = snprintf(selection, capacity, "%s", state.selection);
        if (written < 0 || (size_t)written >= capacity) {
            selection[0] = 0;
            snprintf(dialog_error, sizeof dialog_error, "the selected path is longer than %zu bytes", capacity - 1);
        }
    }
    SDL_UnlockMutex(state.mutex);
    SDL_DestroyMutex(state.mutex);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);

    if (failed || (selected && !selection[0])) {
        fprintf(stderr, "setup: file picker failed: %s\n", dialog_error);
        return SETUP_UI_ERROR;
    }
    return selected ? SETUP_UI_SELECTED : SETUP_UI_CANCELLED;
#endif
}

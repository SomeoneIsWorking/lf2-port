/* game_picker.c -- the host's own chooser for the player's LF2 files.
 *
 * One responsibility: turn "the player wants to point at their game files"
 * into a path. The setup screen (runtime/ui/setup_screen.cpp) draws the UI and
 * judges what comes back; runtime/app/game_selection resolves it. */
#include "game_picker.h"

#include "lf2_log.h"

#include <SDL3/SDL.h>

#include <stdio.h>

#ifdef __ANDROID__
#include "android_bridge.h"
#endif

#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
typedef struct FileDialogState {
    SDL_Mutex *mutex;
    int done;
    int failed;
    char selection[GAME_PICKER_PATH_CAPACITY];
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
#endif

GamePickResult game_picker_choose(const char *message, char *selection, size_t capacity)
{
    if (!selection || capacity == 0) return GAME_PICK_ERROR;
    selection[0] = 0;

#ifdef __ANDROID__
    /* The Activity owns the SAF chooser and stages what it returns into private
     * storage, so the path handed back is already one this process can read. */
    return android_bridge_choose_game_tree(message, selection, capacity);
#elif defined(__EMSCRIPTEN__)
    (void)message;
    lf2_log_writef(LF2_LOG_INFO, "game_picker",
                   "setup: browser setup is owned by the WebAssembly page; native path selection is unavailable\n");
    return GAME_PICK_ERROR;
#else
    (void)message;
    FileDialogState state = {0};
    state.mutex = SDL_CreateMutex();
    if (!state.mutex) {
        lf2_log_writef(LF2_LOG_INFO, "game_picker", "setup: cannot create file-dialog state: %s\n", SDL_GetError());
        return GAME_PICK_ERROR;
    }
    const SDL_DialogFileFilter filters[] = {
        {"LF2 v2.0a installer or executable", "exe"},
        {"ZIP archive", "zip"},
    };
    SDL_ShowOpenFileDialog(file_selected, &state, NULL, filters, SDL_arraysize(filters),
                           SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS), false);

    /* SDL delivers the chooser result on the thread that pumps events, which is
     * this one, so this is a pump loop and not a blocking join. Pumping also
     * keeps the setup screen's own window answering the compositor while the
     * host's chooser is up. */
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

    if (failed || (selected && !selection[0])) {
        lf2_log_writef(LF2_LOG_INFO, "game_picker", "setup: file picker failed: %s\n", dialog_error);
        return GAME_PICK_ERROR;
    }
    return selected ? GAME_PICK_SELECTED : GAME_PICK_CANCELLED;
#endif
}

#include "environment.h"
#include "guest.h"
#include "com.h"
#include "hostwin.h"
#include "config.h"
#include "lf2_log.h"
#include "port_entry.h"
#include "game_data.h"
#include "game_selection.h"
#include "setup_ui.h"
#ifdef __ANDROID__
#include "android_bridge.h"
#endif

void import_stats_report(void);
void scan_prof_report(void);
void load_span_report(void);
void mix_dump_close(void);
void blt_stack_report(void);
void loadprof_report(void);

void ddraw_register(void);
void dsound_register(void);
void dshow_register(void);

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL_filesystem.h>

static int prepare_game_data(int argc, char **argv, GameData *game)
{
    const int force_selection = argc > 1 && strcmp(argv[1], "--select-game") == 0;
    const char *explicit_executable =
        force_selection ? NULL : (argc > 1 ? argv[1] : lf2_environment_get(LF2_ENV_GAME_EXE));
    char *working_directory = SDL_GetCurrentDirectory();
    if (!working_directory) {
        lf2_log_writef(LF2_LOG_INFO, "main", "startup: cannot resolve the working directory: %s\n", SDL_GetError());
        return 0;
    }

    const char *configured_root = config_get("game_dir");
#ifdef __ANDROID__
    char android_root[GAME_DATA_PATH_CAPACITY];
    if (android_bridge_game_root(android_root, sizeof android_root)) configured_root = android_root;
#endif
    if (!force_selection && game_data_discover(explicit_executable, configured_root,
                                               lf2_environment_get(LF2_ENV_APPIMAGE), working_directory, game)) {
        SDL_free(working_directory);
        if (game_data_activate(game)) return 1;
    } else {
        SDL_free(working_directory);
    }

    if (force_selection) {
        snprintf(game->error, sizeof game->error,
                 "Choose the original LF2 v2.0a installer, lf2.exe inside its complete tree, or a ZIP of that "
                 "tree.\n\n"
                 "The selected tree will replace the saved game-file location.");
    }

    for (;;) {
        char selection[GAME_DATA_PATH_CAPACITY];
        char executable[GAME_DATA_PATH_CAPACITY];
        const SetupUiResult choice = setup_ui_choose_game(game->error, selection, sizeof selection);
        if (choice != SETUP_UI_SELECTED) return 0;
#ifdef __ANDROID__
        if (!game_selection_resolve_staged(selection, executable, sizeof executable, game->error, sizeof game->error))
#else
        if (!game_selection_resolve(selection, executable, sizeof executable, game->error, sizeof game->error))
#endif
            continue;
        if (!game_data_validate_executable(executable, game)) continue;
#ifdef __ANDROID__
        char committed_root[GAME_DATA_PATH_CAPACITY];
        if (!android_bridge_commit_game_tree(game->root, committed_root, sizeof committed_root, game->error,
                                             sizeof game->error))
            continue;
        if (!game_data_validate_root(committed_root, game)) continue;
#endif
        if (!game_data_activate(game)) continue;
        if (!config_set("game_dir", game->root)) {
            snprintf(game->error, sizeof game->error, "The selected game-data path is too long to save:\n%s",
                     game->root);
            continue;
        }
        if (!config_save()) {
            snprintf(game->error, sizeof game->error,
                     "The game files are valid, but the selected location could not be saved.\n\n%s", game->root);
            continue;
        }
        return 1;
    }
}

int main(int argc, char **argv)
{
#ifdef __ANDROID__
    if (!android_bridge_initialize()) return 2;
#endif
    config_load();
    GameData game;
    if (!prepare_game_data(argc, argv, &game)) return 2;
    guest_init();
    ddraw_register();
    dsound_register();
    dshow_register();
    com_init();
    guest_load_image("lf2.exe");
    lf2_log_write(LF2_LOG_INFO, "startup", "authenticated image loaded; using native entry and x86port JIT");
    /* The game exits through the CRT's exit(), not by returning from its entry point, so
     * teardown has to be an atexit hook -- calling it after dispatch() would never run.
     * Registered here at startup rather than lazily, so it is armed on every path. */
    atexit(hostwin_shutdown);
    atexit(import_stats_report);
    atexit(scan_prof_report);
    atexit(load_span_report);
    atexit(mix_dump_close);
    atexit(blt_stack_report);
    atexit(loadprof_report);

    return port_entry_run();
}

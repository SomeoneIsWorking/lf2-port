#include "environment.h"
#include "update.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_process.h>

#ifdef __ANDROID__
#include "android_bridge.h"
#endif

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

enum { UPDATE_PATH_CAPACITY = 4096 };

#ifndef __ANDROID__
static int appimage_updater_path(char *output, size_t capacity)
{
    const char *outer = lf2_environment_get(LF2_ENV_APPIMAGE);
    const char *base = SDL_GetBasePath();
    if (!outer || !*outer || !base || !*base) return 0;
    const int written = snprintf(output, capacity, "%s../libexec/lf2/AppImageUpdate-x86_64.AppImage", base);
    if (written < 0 || (size_t)written >= capacity) return 0;
    SDL_PathInfo info;
    return SDL_GetPathInfo(output, &info) && info.type == SDL_PATHTYPE_FILE;
}
#endif

int update_supported(void)
{
#ifdef __ANDROID__
    return 1;
#else
    char path[UPDATE_PATH_CAPACITY];
    return appimage_updater_path(path, sizeof path);
#endif
}

void update_request(void)
{
#ifdef __ANDROID__
    android_bridge_request_update();
#else
    char updater[UPDATE_PATH_CAPACITY];
    const char *outer = lf2_environment_get(LF2_ENV_APPIMAGE);
    if (!appimage_updater_path(updater, sizeof updater) || !outer || !*outer) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "LF2 Port update",
                                 "This build does not include the AppImage updater.", NULL);
        return;
    }
    const char *arguments[] = {updater, outer, NULL};
    SDL_Process *process = SDL_CreateProcess(arguments, false);
    if (!process) {
        char message[512];
        snprintf(message, sizeof message, "Could not start the AppImage updater:\n%s", SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "LF2 Port update", message, NULL);
        return;
    }
    SDL_DestroyProcess(process);
#endif
}

/* Host window state, shared between the win32 and ddraw layers. */
#ifndef HOSTWIN_H
#define HOSTWIN_H

#include <SDL3/SDL.h>
#include <stdint.h>

typedef struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    SDL_Texture  *texture;
    int           width, height;
    uint32_t      hwnd;
    uint32_t      wndproc;
} HostWin;

extern HostWin hw;
void hostwin_pump(void);
void hostwin_present(const uint8_t *indexed, const uint32_t *palette, int w, int h);

#endif

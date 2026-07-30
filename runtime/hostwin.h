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
/* Surfaces are 32-bit XRGB; the palette parameter this used to take was dead from
 * the point the 8-bit assumption was dropped, and every caller passed NULL. */
void hostwin_present(const uint8_t *pixels, int w, int h, int src_pitch);

#endif

void audio_report(void);
int  music_load(const char *path);
void music_start(void);
void music_stop(void);
void music_set_volume(int32_t centibels);
long hostwin_frames(void);
void hostwin_shutdown(void);

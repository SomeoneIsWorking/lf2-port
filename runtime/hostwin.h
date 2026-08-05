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
void hostwin_apply_screen_override(void);   /* LF2_SCREEN=<w>x<h>, see runtime/win32.c */

/* Which post-load screen the game is drawing this frame; see runtime/ddraw.c. */
int  panel_charselect_up(void);
int  panel_overlay_up(void);
int  panel_hud_up(void);
int  screen_offset_x(void);   /* centring offset for fixed-width screens */
void hostwin_shutdown(void);
void hostwin_inject_key(uint32_t vk, int down);
void hostwin_inject_pointer(int x, int y, int down);
int  hostwin_injected_key(uint32_t vk);
int  menu_move(int delta);
void menu_confirm(void);
int  gamepad_player_buttons(int index, unsigned char out[7]);
void input_report(void);
void glyph_hint_set(int ch);
void glyph_hint_clear(void);
int  hostwin_key_held(uint32_t vk);
void controls_hint_enable(int on);

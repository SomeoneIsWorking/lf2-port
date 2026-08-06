/* Host window state, shared between the win32 and ddraw layers. */
#ifndef HOSTWIN_H
#define HOSTWIN_H

#include <SDL3/SDL.h>
#include <stdint.h>

typedef struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    SDL_Texture  *texture;
    int           width, height;    /* the COMPOSE size the game draws into */
    int           win_w, win_h;     /* the actual window, which the above follows */
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
int  lf2_loading_now(void);              /* runtime/imports.c */
enum { GUEST_FRAME_NS = 33333333 };      /* the guest clock's tick, shared with imports.c */
/* The window changed size (or was just created): recompute the composition from its PIXEL
 * width and re-point everything that depends on it. In runtime/ddraw.c, because that is
 * where the surfaces and the presentation live. */
void hostwin_window_geometry(int win_w, int win_h);

/* Which post-load screen the game is drawing this frame; see runtime/ddraw.c. */
int  panel_charselect_up(void);
int  panel_overlay_up(void);
int  panel_hud_up(void);
int  lf2_wide_width(void);    /* the composition's width when it is wider than 794, else 0 */

/* Where the composition sits in the window: 1:1, centred. Both present paths use it, and it
 * is what replaced SDL's logical presentation -- there is no scaling left to do. */
void lf2_compose_placement(int comp_w, int comp_h, float *x, float *y);
int  screen_offset_x(void);   /* centring offset for fixed-width screens */
int  hud_offset_x(int dst_w, int bottom);   /* the in-match HUD's own centring */
void hostwin_shutdown(void);
void hostwin_inject_key(uint32_t vk, int down);
void hostwin_inject_pointer(int x, int y, int down);
int  hostwin_injected_key(uint32_t vk);
int  menu_move(int delta);
void menu_confirm(void);
int  gamepad_player_buttons(int index, unsigned char out[7]);
void input_report(void);
void clock_sites_report(void);   /* LF2_CLOCK_SITES: who reads the clock, and who spins on it */
/* The scripted-input exit report lives in runtime/script.h (script_report). */
void window_resize_report(void); /* any LF2_WINDOW_RESIZE step the run never reached */
void glyph_hint_set(int ch);
void glyph_hint_clear(void);

/* The clip-draw override tells the blit path when a draw is the stage's own shadow ellipse,
 * identified by the object it is drawn on -- learned per stage, see runtime/ddraw.c. */
void shadow_hint_set(int on);

/* The clip-draw override hands over the object each draw is made on; the blit path learns
 * from it which object draws the stage's shadow ellipse, and answers with shadow_object(). */
void     clip_obj_note(uint32_t obj);
uint32_t shadow_object(void);

/* runtime/overrides/assets.c -- the stage's shadow geometry, from the background record. */
void     bg_shadow_size(int *w, int *h);
uint32_t bg_shadow_stage(void);

/* The stage's walkable floor, from bg.dat's `zboundary:` -- which is where the floor IS on
 * the screen, because LF2's depth axis projects straight down it. 0 when no stage is loaded
 * or the record does not give an ordered pair inside 550 rows. */
int      bg_z_bounds(int *zmin, int *zmax);
int  hostwin_key_held(uint32_t vk);
void hostwin_request_quit(void);
int  hostwin_width(void);
int  hostwin_height(void);
int  gamepad_start_held(void);
int  gamepad_start_index(void);   /* which pad, or -1; drop-out is per player */

/* The pause menu's reach into the game. `device_player` is the slot a device is driving,
 * -1 if none; `input_synth_confirm` makes a device's attack read as pressed for a few
 * gathers, so the GAME dispatches a menu item rather than the port simulating what it
 * would have done. `coop_drop_out` is the deliberate half of what unplugging a pad does.
 * `exit_to_menu_begin` drives the game's own way out of a match. */
int  device_player(int dev);
int  any_playing_device(void);    /* a device whose buttons actually reach the game */
void input_synth_confirm(int dev, int frames);
int  coop_owns(int slot);         /* is this slot the port's to release? */
int  coop_drop_out(int slot);
void exit_to_menu_begin(int dev);
void exit_to_menu_tick(void);

/* Ask for the NEXT presented frame to be written out, for a probe whose moment is decided
 * by game state rather than by a frame number. */
void gfx_request_frame_dump(void);

/* Pause menu, runtime/pause.c */
int  pause_active(void);
void pause_tick(void);
void pause_draw(uint32_t pix, int w, int h, int pitch);
void present_frozen_frame(void);
void controls_hint_enable(int on);

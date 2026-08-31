/* Host window state, shared between the win32 and ddraw layers. */
#ifndef HOSTWIN_H
#define HOSTWIN_H

#include <SDL3/SDL.h>
#include <stdint.h>

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    int width, height; /* the COMPOSE size the game draws into */
    int win_w, win_h;  /* the actual window, which the above follows */
    uint32_t hwnd;
    uint32_t wndproc;
} HostWin;

extern HostWin hw;
void hostwin_pump(void);
/* Surfaces are 32-bit XRGB; the palette parameter this used to take was dead from
 * the point the 8-bit assumption was dropped, and every caller passed NULL. */
void hostwin_present(const uint8_t *pixels, int w, int h, int src_pitch);

/* The surface the frame is composed into, discovered from the game's own copy to the
 * primary. 0 until that copy has happened. */
void frame_source_note(uint32_t pixels, int off);
uint32_t frame_source_pixels(void);

#endif

void audio_report(void);
int music_load(const char *path);
void music_start(void);
void music_stop(void);
void music_set_volume(int32_t centibels);
long hostwin_frames(void);
/* Resolve one frame specification through the same screen-anchor owner used by captures. */
int hostwin_frame_selected(const char *spec, long frame);
int lf2_loading_now(void);          /* runtime/win32/imports.c */
enum { GUEST_FRAME_NS = 33333333 }; /* the guest clock's tick, shared with imports.c */
/* The window changed size (or was just created): recompute the composition from its PIXEL
 * width and re-point everything that depends on it. In runtime/video/ddraw.c, because that is
 * where the surfaces and the presentation live. */
void hostwin_window_geometry(int win_w, int win_h);

/* Which post-load screen the game is drawing this frame; see runtime/video/ddraw.c. */
int panel_charselect_up(void);
int panel_overlay_up(void);
int panel_hud_up(void);
int panel_modemenu_up(void); /* the MODE menu, likewise -- not the game-mode word (issue #51) */
int lf2_wide_width(void);    /* the composition's width when it is wider than 794, else 0 */

/* Where the composition is drawn in the window, and how big (issue #41): the height sets the
 * scale, leftover width is field of view. Both present paths use the rectangle; the renderer
 * also needs the scale on its own, to apply per quad. lf2_window_to_compose is the exact
 * inverse, and every hit test goes through it. This is what replaced SDL's logical
 * presentation, which cannot express "scale the geometry, not the finished frame". */
void lf2_compose_rect(int comp_w, int comp_h, SDL_FRect *r);
float lf2_world_scale(void);
void lf2_window_to_compose(float wx, float wy, float *cx, float *cy);
/* The pointer's own path: SDL delivers it in POINTS, the composition is placed in PIXELS, and
 * the density between them is the whole of issue #56 as far as hit tests are concerned. */
void lf2_pointer_to_compose(float px, float py, float density, float *cx, float *cy);
int screen_offset_x(void);               /* centring offset for fixed-width screens */
int hud_offset_x(int dst_w, int bottom); /* the in-match HUD's own centring */
void hostwin_shutdown(void);
void hostwin_inject_key(uint32_t vk, int down);
void hostwin_inject_pointer(int x, int y, int down);
int hostwin_injected_key(uint32_t vk);
void input_report(void);
void audio_pan_report(void);
void bg_camera_report(void);
/* LF2_STAGE_GEOM=1: how much hand-woven geometry actually reached the frame (issue #62). */
void bg_geom_report(void);
void mode_force_report(void); /* LF2_MODE: which mode a scripted run entered, and whether it did */
/* LF2_CAMERA: was the wide view actually re-centred? */ /* LF2_AUDIO_PAN: the audible span vs
                                                            the picture (issue #39) */
void clock_sites_report(void);                           /* LF2_CLOCK_SITES: who reads the clock, and who spins on it */
/* The scripted-input exit report lives in runtime/app/script.h (script_report). */
void window_resize_report(void); /* any LF2_WINDOW_RESIZE step the run never reached */
void glyph_hint_set(int ch);
void glyph_hint_clear(void);

/* The clip-draw override tells the blit path when a draw is the stage's own shadow ellipse,
 * identified by the object it is drawn on -- learned per stage, see runtime/video/ddraw.c. */
void shadow_hint_set(int on);
/* Scope fn_0040de30's world-object draw so every textured piece receives the object's exact
 * ground row and independent character/cast-shadow membership. */
void render_shadow_object_begin(int ground_y, int character, int casts_shadow);
void render_shadow_object_end(void);

/* The background override marks the stage's own colour-fill bands, because the game's fill
 * helper is shared with the front end and the blit cannot tell them apart (issue #42). */
void world_band_hint_set(int on);
/* The background override marks an explicitly authored opaque far plane while it crosses the
 * guest call boundary. It may continue its outer edge in native-size reflected segments;
 * keyed scenery and undeclared stages never receive the hint. */
void world_backdrop_hint_set(int on);
void world_band_report(void);   /* LF2_BAND_DEBUG=1 */
void glyph_scale_report(void);  /* LF2_GLYPH_DEBUG=1 -- was text rasterised at window size? */
void framing_report(void);      /* LF2_FRAMING_DEBUG=1 -- per-screen framing, issue #44 */
int screen_backdrop_left(void); /* this screen's BACKDROP art is anchored at x=0 */

/* The clip-draw override hands over the object each draw is made on; the blit path learns
 * from it which object draws the stage's shadow ellipse, and answers with shadow_object(). */
void clip_obj_note(uint32_t obj);
uint32_t shadow_object(void);

/* runtime/overrides/assets.c -- the stage's shadow geometry, from the background record. */
void bg_shadow_size(int *w, int *h);
uint32_t bg_shadow_stage(void);

/* The stage's walkable floor, from bg.dat's `zboundary:` -- which is where the floor IS on
 * the screen, because LF2's depth axis projects straight down it. 0 when no stage is loaded
 * or the record does not give an ordered pair inside 550 rows. */
int bg_z_bounds(int *zmin, int *zmax);
void hostwin_request_quit(void);
void hostwin_inject_window_pointer(float x, float y, int down);
int hostwin_quit_requested(void);
int hostwin_width(void);
int hostwin_height(void);

/* The pause menu's reach into the game. `device_player` is the slot a device is driving,
 * -1 if none. `coop_drop_out` is the deliberate half of what unplugging a pad does. Leave
 * Match itself is an F4 pulse through hostwin_inject_key, so the guest owns every consequence
 * after the modal closes (issue #22). */
int device_player(int dev);
int device_for_player(int slot); /* the device driving a slot, -1 = none (issue #74) */
int coop_owns(int slot);         /* is this slot the port's to release? */
int coop_drop_out(int slot);

/* Ask for the NEXT presented frame to be written out, for a probe whose moment is decided
 * by game state rather than by a frame number. */
void gfx_request_frame_dump(void);

/* Global RmlUi menu lifecycle and LF2 action bridge, runtime/app/pause.c. */
void pause_tick(void);
int pause_menu_in_match(void);
int pause_menu_can_drop(void);
void pause_menu_close(void);
void pause_menu_drop_out(void);
void pause_menu_leave_match(void);
void pause_report(void);
void controls_hint_enable(int on);

/* The native renderer: the game's draws as GPU geometry instead of a flattened buffer.
 *
 * WHY THIS EXISTS (issue #30). Until now every pixel the game drew was composed on the CPU
 * by runtime/video/ddraw.c's software blitter, and the finished buffer was handed to SDL as ONE
 * streaming texture drawn as a single quad. That is a picture of a frame, not a frame: there
 * is no per-sprite geometry, no depth, no render target and no blend stage, so none of what
 * an HD2D presentation or a real cast shadow needs can be expressed at all.
 *
 * WHAT IS CAPTURED, and why it is enough. The game draws through exactly two routes, both
 * measured rather than assumed (LF2_DRAW_PATHS, instrument I007): 19753 calls to Blt in a
 * run that reaches a match, and zero to BltFast or through Lock -- plus GDI text, which
 * writes into a surface directly and is captured here as a TILE. So a display list built at
 * those two points is the whole frame.
 *
 * HOW A FRAME IS RECOGNISED, without anyone having to name a surface. The game composes into
 * an off-screen surface and copies that to the primary in one blit. So: draws are recorded
 * per DESTINATION surface, and when a copy to the primary arrives, its SOURCE is by
 * definition the composition -- that list is the frame, and it is drawn then. Nothing is
 * hardcoded to a surface address, and the arrangement is discovered from the game's own
 * blits every run.
 *
 * THE SOFTWARE PATH IS NOT REMOVED. It still composes every frame, and LF2_RENDERER=soft
 * presents it exactly as before. That is deliberate: a renderer that cannot be diffed against
 * the thing it replaces is a rewrite, and every other piece of this port was landed by
 * running the two side by side (tools/routes/render_test.sh).
 */
#ifndef LF2_RENDER_H
#define LF2_RENDER_H

#include <stdint.h>

struct SDL_Renderer;

/* Which path presents. Read once; LF2_RENDERER=soft selects the software compositor. */
int  render_gpu_enabled(void);

/* ddraw.c hands the renderer the SDL renderer once the window exists. */
void render_init(struct SDL_Renderer *r);
void render_shutdown(void);

/* ---- building a frame ----
 *
 * Every call names its DESTINATION by the guest address of the surface's pixels, which is
 * the identity every writer in this port already has in hand.
 */

/* One textured quad. `key` is honoured only when `keyed`; a keyed source is uploaded with
 * the key range turned into alpha 0, which is where this port's blend stage comes from. */
void render_blit(uint32_t dst_pixels,
                 int dl, int dt, int dr, int db,
                 uint32_t src_pixels, int sw, int sh, int spitch,
                 int sl, int st, int sr, int sb,
                 int keyed, uint32_t key_lo, uint32_t key_hi);

/* The game's own shadow ellipse, identified by the object it is drawn on. It is NOT added to
 * the list as a picture: its rectangle is where the object stands on the ground, and that is
 * the one thing a cast shadow needs that the sprite itself cannot supply. The next sprite
 * drawn is the object it belongs to -- the game draws the ellipse immediately before it. */
void render_shadow_ground(uint32_t dst_pixels, int dl, int dt, int dr, int db);

/* Whether the cast shadows are in force. With them off -- or with the shaders unavailable,
 * because the mask they draw into would then be consumed by nothing -- the game's own ellipse
 * is recorded as an ordinary picture and the GPU frame stays byte-comparable with the
 * software one. It is also what MARKS AN OBJECT: a sprite with a ground marker in front of
 * it is a fighter standing in the field, which is the only thing the lighting touches. */
int  render_shadows_enabled(void);

/* A solid rectangle, the game's DDBLT_COLORFILL. */
void render_fill(uint32_t dst_pixels, int dl, int dt, int dr, int db, uint32_t argb);

/* ---- tiles: pixels written straight into a surface ----
 *
 * GDI text does not go through Blt; it composites itself into the destination. A tile is a
 * small PREMULTIPLIED RGBA scratch buffer that such a writer fills instead, appended to the
 * display list at the point it was drawn -- so its ordering against the surrounding blits is
 * the game's own, not an approximation of it.
 *
 * Premultiplied because that is what makes the layer exact: compositing `src + dst*(1-a)`
 * over the scene gives the same pixels the CPU path produced by blending against what was
 * already there, without the tile needing to know what that was.
 *
 * render_tile_begin returns NULL when the GPU path is off or the rectangle is unusable, and
 * the caller must then do what it did before. A tile that is begun must be ended.
 */
/* x,y,w,h place the tile in the GAME's coordinates; tw,th are how many pixels the caller
 * rasterised. Pass tw/th larger to draw text at the window's real resolution (issue #45);
 * pass 0 for both to mean "the same as w,h", which is what every non-text caller wants. */
uint32_t *render_tile_begin(uint32_t dst_pixels, int x, int y, int w, int h, int tw, int th);
void      render_tile_end(void);

/* ---- presenting ----
 *
 * Called from the copy-to-primary. `src_pixels` is the composition, `off` the widescreen
 * centring offset, and w/h the primary's size. Returns 1 if it drew and presented the frame,
 * 0 if the caller should fall back to the software present.
 */
int render_present(uint32_t src_pixels, int off, int w, int h);

/* Called when a surface's pixels change outside the renderer's knowledge (a palette write,
 * a fresh load) so its cached texture is rebuilt. */
void render_surface_dirty(uint32_t pixels);

/* Marks the frame spent. Called after every present, by whichever path drew it.
 *
 * The lists are not emptied here but by the first call that RECORDS over them, which on an
 * ordinary frame is the same thing. It differs for a frame during which the game records
 * nothing -- the pause menu freezes the world by declining to call the game's update, so no
 * blit arrives, and a renderer that had thrown its list away at the last present would have
 * nothing to draw the menu on top of (issue #52). */
void render_frame_reset(void);

/* Draw over the frame that is already there. Returns 0 when there is no retained frame -- the
 * GPU path is off, or nothing has been drawn yet -- and the caller must then do without the
 * renderer. It REWINDS the previous held frame's overlay first, so a menu held up for a
 * minute is recorded once per frame rather than appended to itself sixty times a second. */
int render_hold_begin(void);

/* Mark where the port's own UI begins in a LIVE frame's list (the controls hint on a menu the
 * game is still updating). Everything from that point is drawn after the lighting pass and is
 * not part of the scene. render_hold_begin does the same for a frozen frame. */
void render_overlay_mark(uint32_t dst_pixels);

/* The presented frame, read back off the GPU into an ARGB8888 buffer. This is what makes the
 * two paths diffable: without it every LF2_FRAME_DUMP would be the software compositor's
 * buffer whichever renderer actually drew the screen. */
int render_readback(uint32_t *dst, int w, int h, int dst_pitch);

/* The size the last frame was DRAWN at -- the render output, not the game's composition.
 * They differ whenever the window is not the composition's size, which is the point of the
 * renderer: the frame is built at the window's resolution rather than point-scaled up to it. */
void render_output_size(int *w, int *h);

void render_report(void);       /* LF2_RENDER_DEBUG=1: what the frame was made of */

#endif

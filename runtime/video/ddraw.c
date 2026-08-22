/* DirectDraw 1 on SDL3.
 *
 * Surface pixels are allocated inside guest memory, so Lock() hands back a plain guest
 * address and the GDI text path can scribble into the same buffer. Everything is 8-bit
 * indexed, which is what the game's BMPs are. */
#include "com.h"
#include "overrides/geom.h"
#include "guest_map.h"
#include "guest_ops.h"
#include "hostwin.h"
#include "render.h"
#include "engine.h"
#include "backdrop.h"
#include "blt_trace.h"
#include "result_panel.h"
#include "stage_banner.h"
#include "framespec.h"
#include "script.h"
#include "rmlui.h"
#include "startup.h"
#include "device_icons.h"
#include "overlay_panel.h"

void menu_click_report(void);   /* issue #27: the click flag as the front-end menu sees it */

/* runtime/win32/gdi.c -- the picture the game just loaded, so a surface created to hold it can be
 * told from the composition surface (issue #50). */
int  gdi_last_bitmap(int *w, int *h, long *loaded_total);
void gdi_last_bitmap_consume(void);

#include <time.h>
#include "loadprof.h"

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define ARG(n) LD32(R(ESP) + 4 + 4 * (n))

/* The composition the game asks for. Taken from geom.h rather than written again here, so
 * there is one 794x550 in the port and tests/test_geom.c is testing this one. */
enum { NATIVE_W = GEOM_SCREEN_W, NATIVE_H = GEOM_SCREEN_H };

/* DDSURFACEDESC field offsets */
enum { SD_SIZE = 0, SD_FLAGS = 4, SD_HEIGHT = 8, SD_WIDTH = 12, SD_PITCH = 16,
       SD_SURFACE = 36, SD_PIXELFORMAT = 72, SD_CAPS = 104, SD_BYTES = 108 };
enum { DDSD_CAPS = 1, DDSD_HEIGHT = 2, DDSD_WIDTH = 4, DDSD_PITCH = 8,
       DDSD_PIXELFORMAT = 0x1000 };
enum { DDSCAPS_PRIMARYSURFACE = 0x200 };
enum { DDBLT_COLORFILL = 0x400, DDBLT_KEYSRC = 0x8000 };

/* DDBLTFX.dwFillColor sits at offset 80 of a 100-byte DDBLTFX -- it is the last union in
 * the struct, after ten z-buffer and alpha members. It was read from offset 16
 * (dwRotationAngle) here, which the callers never write, so every colour fill painted a
 * leftover stack dword: the ground band of every stage was filled 0x000038 (navy) instead
 * of its real colour, which is exactly the "navy rectangles on the ground" symptom.
 *
 * Confirmed against the game rather than against a header: the fill helper decompiles to a
 * 0x64-byte frame whose only two stores are dwSize=100 at +0 and the colour at +0x50. */
enum { DDBLTFX_FILLCOLOR = 80 };

extern long ck_set, ck_blt_keyed, ck_blt_plain;
void colorkey_report(void);
void draw_paths_report(void);
enum { DDCKEY_SRCBLT = 0x8 };

typedef struct {
    int      w, h, pitch;
    int      rows;          /* rows the buffer was ALLOCATED for; 0 when h is the allocation.
                               A window-following surface is allocated once at the maximum and
                               only moves w/h, because vram_alloc is a bump allocator with no
                               free and reallocating per resize event exhausts the arena
                               during a single drag of a window edge (issue #20). */
    uint32_t pixels;        /* guest address of the pixel buffer */
    int      primary;
    int      has_key;
    uint32_t key_lo, key_hi;
    uint32_t palette;       /* guest object address of the attached palette, 0 if none */
    uint32_t attached;      /* back buffer handed out by GetAttachedSurface */
    uint32_t clipper;   /* IDirectDrawClipper set via SetClipper, 0 if none */
} Surface;

typedef struct { uint32_t entries[256]; } Palette;

static uint32_t primary_surface;
static uint32_t active_palette;

/* Guest surfaces are mutable.  Both native render paths cache their GPU uploads, so every
 * write known to DirectDraw must invalidate both caches.  The content hash is only a backstop:
 * sampling rows cannot reliably notice a one-row colour-key border or a short-lived effect. */
static void surface_changed(const Surface *s)
{
    if (!s) return;
    render_surface_dirty(s->pixels);
    engine_surface_dirty(s->pixels);
}

/* Surfaces live here rather than on the guest heap so a huge sprite sheet cannot
 * collide with malloc'd game data. The range is declared in guest_map.h, which is also
 * what stops it colliding with the sound PCM arena -- it used to, silently. */
enum { VRAM_BASE = GUEST_VRAM_BASE };
static uint32_t vram_next = VRAM_BASE;

long vram_allocs, vram_bytes;

/* LF2_BLT_STACK bookkeeping; see the hook in the Blt path. */
static int blt_stack_wanted, blt_stack_hit;
static int blt_stack_best = 1 << 30, blt_stack_best_l, blt_stack_best_t;

void blt_stack_report(void)
{
    const char *want = getenv("LF2_BLT_STACK");
    if (!want) return;
    if (blt_stack_hit) return;
    if (!blt_stack_wanted) {
        fprintf(stderr, "blt stack: NO BLITS AT ALL this run -- %s was never tested\n", want);
        return;
    }
    fprintf(stderr, "blt stack: nothing landed on %s. The match is on the exact top-left\n"
                    "           corner; the nearest destination seen was (%d,%d).\n",
            want, blt_stack_best_l, blt_stack_best_t);
}

/* ---- the game's own text ----
 *
 * The overrides layer recognises a glyph blit -- a clip drawn from one of the three 8x16
 * font sheets, with the character code as the clip index -- and leaves the code here for
 * the blit that follows. This is where the substitution has to happen, because this is the
 * only place that knows BOTH the character (from the hint) and the destination surface.
 *
 * A hint rather than a lookup: the alternative is deriving the character from the source
 * rectangle, which needs the sheet's cell layout, and that is not something worth guessing
 * at when the caller already knows the answer.
 */
static int glyph_hint = -1;
long glyphs_drawn;

void glyph_hint_set(int ch) { glyph_hint = ch; }

/* Set by the clip-draw override when the draw is the stage's own shadow bitmap, cleared by
 * the next draw that is not. Latched here because this is the only place that also knows the
 * destination rectangle -- which is where the object stands on the ground, and therefore
 * where a real cast shadow belongs. */
static int shadow_hint;
void shadow_hint_set(int on) { shadow_hint = on; }

/* Set by runtime/overrides/background.c around the stage's tinted-layer fills, because the
 * game's colour-fill helper is shared with the front end and the blit cannot tell them apart.
 * See the comment at that call site -- and issue #42, which is what a rectangle-shaped guess
 * cost. Counted so the widening cannot become a rule nobody has ever seen fire. */
static int  world_band_hint;
static int  world_backdrop_hint;
static long world_band_fills, world_band_widened;
void world_band_hint_set(int on) { world_band_hint = on; }
void world_backdrop_hint_set(int on) { world_backdrop_hint = on; }

void glyph_hint_clear(void) { glyph_hint = -1; }

/* ---- learning which object draws the stage's shadow ----
 *
 * The shadow is one bitmap per stage, drawn under every object from a source surface whose
 * size the stage record gives (bg.dat's `shadowsize:`). So the FIRST blit whose source
 * surface is that size names the object the ellipse is drawn on, and every later draw on
 * that object is a shadow -- an identification made once per stage rather than a size test
 * applied to every blit for the rest of the run.
 *
 * WHAT WOULD MAKE THIS WRONG, since it is a size match and not a handle: a sprite sheet that
 * happens to be within two pixels of the shadow's size and is drawn BEFORE the first shadow
 * would be learned instead, and that object's draws would be swallowed as ground markers for
 * the rest of the stage. No stage measured does this -- LF2_SHADOW_DEBUG prints the size that
 * matched and the object, so a wrong learn is visible rather than silent -- but it is a size
 * match, not proof, and the honest fix if it ever bites is the shadow's own handle out of the
 * background record. That handle was looked for and NOT found: the offset that looked right
 * matched 0 of 40000 clip draws (claim C019).
 *
 * If no such blit is ever seen the port simply never replaces the shadow, and says so.
 */
static uint32_t clip_obj, shadow_obj_learned;
static int      shadow_obj_stage = -1;
void clip_obj_note(uint32_t obj) { clip_obj = obj; }
uint32_t shadow_object(void) { return shadow_obj_learned; }

static void shadow_learn(const Surface *s)
{
    const int stage = (int)bg_shadow_stage();
    if (stage != shadow_obj_stage) {          /* a new stage: the old object is stale */
        shadow_obj_stage = stage;
        shadow_obj_learned = 0;
    }
    if (shadow_obj_learned || !clip_obj) return;
    int sw = 0, sh = 0;
    bg_shadow_size(&sw, &sh);
    if (sw <= 0 || sh <= 0) return;
    /* Matched within two pixels: the two numbers in bg.dat are the size the game DRAWS at,
     * which need not be the bitmap's own. */
    if (abs(s->w - sw) > 2 || abs(s->h - sh) > 2) return;
    shadow_obj_learned = clip_obj;
    if (getenv("LF2_SHADOW_DEBUG"))
        fprintf(stderr, "shadow: stage %d draws its ellipse on object %08x "
                        "(source %dx%d, shadowsize %dx%d)\n",
                stage, clip_obj, s->w, s->h, sw, sh);
}

int game_glyph_draw(int ch, int x, int y, uint32_t ink,
                    uint32_t dpix, int dwid, int dhei, int dpitch);
/* The display-list half of the same glyph, for text the port draws over a frame it did not
 * compose -- see controls_hint_draw. */
int game_glyph_tile(int ch, int x, int y, uint32_t ink, uint32_t dst_pixels);

/* The ink colour comes from the sheet's own glyph, so each of the three sheets keeps its
 * colour without the port having to know what they are. Taken as the brightest non-keyed
 * pixel in the cell: a bitmap glyph is one colour plus anti-aliasing towards the key. */
static uint32_t glyph_ink(const Surface *s, int sl, int st, int sr, int sb)
{
    uint32_t best = 0x00ffffffu;
    int best_lum = -1;
    for (int y = st; y < sb && y < s->h; y++) {
        const uint32_t *row = (const uint32_t *)(g_mem + s->pixels
                                                 + (size_t)y * (size_t)s->pitch);
        for (int x = sl; x < sr && x < s->w; x++) {
            const uint32_t px = row[x] & 0x00ffffffu;
            if (s->has_key && px >= s->key_lo && px <= s->key_hi) continue;
            const int lum = (int)((px >> 16) & 0xff) + (int)((px >> 8) & 0xff)
                          + (int)(px & 0xff);
            if (lum > best_lum) { best_lum = lum; best = px; }
        }
    }
    return best;
}

/* Whether the stage's full-width bands were widened, and if not, WHY not -- because "0
 * widened" has two very different causes and a silent zero would read as a broken feature.
 * Printed only when the run actually reached a stage, so the front end cannot report on a
 * mechanism it never had any business exercising. LF2_BAND_DEBUG=1. */
void world_band_report(void)
{
    if (!getenv("LF2_BAND_DEBUG")) return;
    if (!world_band_fills) {
        fprintf(stderr, "bands: the stage's fill path drew NOTHING this run -- either no "
                        "match was reached, or no loaded stage has a tinted layer. This says "
                        "nothing about whether widening works.\n");
        return;
    }
    fprintf(stderr, "bands: %ld stage fill(s), %ld widened to the viewport (view %d, the "
                    "game's own screen %d)%s\n",
            world_band_fills, world_band_widened, hw.width, GEOM_SCREEN_W,
            world_band_widened ? ""
                : " -- none spanned the whole 794 at x 0, so none is a full-width band");
}

void vram_report(void)
{
    /* Reported relative to VRAM_BASE. Printing the raw cursor makes a 316 MB arena look
     * like 1.5 GB, because the base is 0x50000000. */
    fprintf(stderr, "vram: %ld allocations, %ld KB requested, %u KB of arena used\n",
            vram_allocs, vram_bytes / 1024, (vram_next - VRAM_BASE) / 1024);
}

static uint32_t vram_alloc(uint32_t n)
{
    /* Refuse past the reservation instead of walking into the next arena. Running off
     * the end used to be invisible: the allocator handed out addresses belonging to the
     * sound PCM, the surfaces overwrote it, and the only symptom was that the game
     * sounded broken. An arena that cannot overflow loudly will overflow quietly. */
    if (vram_next + n < vram_next || vram_next + n > GUEST_VRAM_END) {
        fprintf(stderr,
                "vram arena exhausted: %u bytes requested at %08x, reservation ends at %08x\n"
                "  (%ld allocations, %ld KB so far). Raise GUEST_VRAM_SIZE in guest_map.h.\n",
                n, vram_next, (unsigned)GUEST_VRAM_END, vram_allocs, vram_bytes / 1024);
        abort();
    }
    vram_allocs++; vram_bytes += n;
    uint32_t p = vram_next;
    vram_next = (vram_next + n + 4095u) & ~4095u;
    return p;
}

/* The game never creates a DirectDraw palette -- it queries GetPixelFormat and adapts.
 * Surfaces are therefore 32-bit XRGB, and the 8-bit bitmaps are converted through their
 * own palette when GDI blits them in, which is what GDI does on Windows. */
static void write_pixelformat(uint32_t pf)
{
    if (!pf) return;
    ST32(pf, 32);                    /* dwSize */
    ST32(pf + 4, 0x40);              /* DDPF_RGB */
    ST32(pf + 12, 32);               /* bit count */
    ST32(pf + 16, 0x00ff0000);       /* R */
    ST32(pf + 20, 0x0000ff00);       /* G */
    ST32(pf + 24, 0x000000ff);       /* B */
    ST32(pf + 28, 0);
}

/* ---- screen-change detection ----
 * Whether a scripted click actually did anything is not answerable from the key array --
 * every screen reads the same keys -- so LF2_SCREEN_HASH watches the framebuffer instead.
 *
 * The comparison is deliberately coarse. Menus animate (cursors blink, banners scroll), so
 * an exact hash changes every frame and reports nothing useful. Instead a subsampled
 * signature is compared byte-for-byte and a change is reported only when a large fraction
 * of it differs, which is what a screen transition looks like and what local animation
 * does not.
 */
enum { SIG_N = 1024, SCREEN_CHANGE_PCT = 25 };

static void screen_change_check(const uint8_t *px, int w, int h, int pitch, long frame)
{
    if (!getenv("LF2_SCREEN_HASH") || !px || w <= 0 || h <= 0) return;

    static uint8_t sig[SIG_N], prev[SIG_N];
    static int have_prev;
    for (int i = 0; i < SIG_N; i++) {
        const int x = (int)((long)i * 7919 % w);
        const int y = (int)((long)i * 6271 % h);
        sig[i] = px[(long)y * pitch + x];
    }
    if (!have_prev) {
        memcpy(prev, sig, SIG_N); have_prev = 1;
        fprintf(stderr, "screen: first frame %ld\n", frame);
        return;
    }
    int diff = 0;
    for (int i = 0; i < SIG_N; i++) if (sig[i] != prev[i]) diff++;
    const int pct = diff * 100 / SIG_N;
    if (pct >= SCREEN_CHANGE_PCT) {
        fprintf(stderr, "screen: CHANGED at frame %ld (%d%% of samples)\n", frame, pct);
        memcpy(prev, sig, SIG_N);
    }
}

/* Diagnostic dumps go to $LF2_DUMP_DIR, default "scratch". Never an absolute path: this
 * is a committed file in a public repository, and a baked-in home directory is both
 * unusable for anyone else and a leak of the author's layout. */
static void dump_path(char *out, size_t n, const char *fmt, ...)
{
    const char *dir = getenv("LF2_DUMP_DIR");
    if (!dir || !*dir) dir = "scratch";
    int k = snprintf(out, n, "%s/", dir);
    if (k < 0 || (size_t)k >= n) { out[0] = 0; return; }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(out + k, n - (size_t)k, fmt, ap);
    va_end(ap);
}

/* Deterministic visual capture: LF2_FRAME_DUMP=1500,1800 writes those presented frames as
 * PPM into $LF2_DUMP_DIR. Screenshotting an X server instead means racing the game's own
 * timing -- two attempts at capturing a match landed on the menu before it -- and cannot
 * run headless at all. Frame numbers are exact, so a capture is reproducible.
 */
/* AN ITEM MAY BE `@screen+N` TOO, and that is not a convenience -- it is the same defect the
 * pad scripts had. A dump frame is a stopwatch aimed at a moving target: render_test asked for
 * frame 2250 to get "a frame with fighters on it", and when the routes stopped waiting 840
 * frames for a front end that was already up, 2250-840 landed somewhere else in the match and
 * the arm failed for a reason that had nothing to do with the renderer. The pad scripts were
 * given screen anchors for exactly this in issue #25; the dumps kept their stopwatches.
 *
 * The grammar lives in runtime/app/framespec.h so tests/test_framespec.c can walk it without
 * booting the game; script_when is the resolver, because it is what knows the screens. */
int hostwin_frame_selected(const char *spec, long frame)
{
    return framespec_matches(spec, frame, script_when);
}
/* A capture aimed at a fixed frame number and a probe that fires off game STATE can
 * disagree, and when they do the picture is of the wrong thing while looking perfectly
 * valid -- an A/B of two spawns produced one arm whose run never reached the match, and the
 * two screenshots would have been compared as if they showed the same experiment. So a
 * probe can ask for the next frame instead, and the capture follows the event. */
static int frame_requested;
void gfx_request_frame_dump(void) { frame_requested = 1; }

static int frame_wanted(long frame)
{
    if (frame_requested) { frame_requested = 0; return 1; }
    return hostwin_frame_selected(getenv("LF2_FRAME_DUMP"), frame);
}

/* LF2_MEM_DUMP=<frame>[,<frame>...] writes the game's whole .data section to
 * data_<frame>.bin in $LF2_DUMP_DIR. Diffing two of them across a single input finds the
 * variable behind an on-screen change when reading the disassembly would mean picking one
 * candidate out of hundreds -- which is how the pre-fight overlay's selection index was
 * located. tools/re/diff_data.py does the comparison.
 *
 * The range is the section's own bounds from the PE header, not a guess: dumping too
 * little would drop the answer and look like "nothing changed". */
enum { DATA_BASE = 0x0044d000, DATA_SIZE = 0xc724 };

/* LF2_HEAP_DUMP=<frame>[,...] snapshots the guest heap in use, for the same
 * before/after diffing as LF2_MEM_DUMP but over the region .data cannot reach.
 * tools/re/diff_data.py --base 0x20000000 reads it. */
uint32_t guest_heap_used(void);          /* imports.c */

static void dump_heap(long frame)
{
    if (!hostwin_frame_selected(getenv("LF2_HEAP_DUMP"), frame)) return;
    const uint32_t used = guest_heap_used();
    char path[256];
    dump_path(path, sizeof path, "heap_%06ld.bin", frame);
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "heap dump: cannot write %s\n", path); return; }
    fwrite(g_mem + GUEST_HEAP_BASE, 1, used, f);
    fclose(f);
    fprintf(stderr, "heap dump: wrote %s (%u bytes from %08x)\n",
            path, used, (unsigned)GUEST_HEAP_BASE);
}

static void dump_data(long frame)
{
    if (!hostwin_frame_selected(getenv("LF2_MEM_DUMP"), frame)) return;
    char path[256];
    dump_path(path, sizeof path, "data_%06ld.bin", frame);
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "data dump: cannot write %s\n", path); return; }
    fwrite(g_mem + DATA_BASE, 1, DATA_SIZE, f);
    fclose(f);
    fprintf(stderr, "data dump: wrote %s (%d bytes from %08x)\n", path, DATA_SIZE, DATA_BASE);
}

static void dump_frame(const uint8_t *px, int w, int h, int pitch, long frame)
{
    if (!frame_wanted(frame)) return;
    char path[256];
    dump_path(path, sizeof path, "frame_%06ld.ppm", frame);
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "frame dump: cannot write %s\n", path); return; }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; y++) {
        const uint32_t *row = (const uint32_t *)(px + (size_t)y * (size_t)pitch);
        for (int x = 0; x < w; x++) {
            const uint8_t rgb[3] = { (uint8_t)(row[x] >> 16), (uint8_t)(row[x] >> 8),
                                     (uint8_t)row[x] };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    fprintf(stderr, "frame dump: wrote %s (%dx%d)\n", path, w, h);
}

/* Release SDL explicitly at exit. Leaving it to process teardown is usually harmless, but
 * it means the audio device and window outlive the game's own shutdown, and a diagnostic
 * that runs at exit cannot tell an orderly stop from a crash. */
void hostwin_shutdown(void)
{
    script_report();
    pause_report();
    menu_click_report();
    clock_sites_report();
    window_resize_report();
    if (getenv("LF2_SHUTDOWN_DEBUG")) fprintf(stderr, "shutdown: releasing SDL\n");
    render_shutdown();
    overlay_panel_shutdown();
    device_icons_shutdown();
    if (hw.texture)  { SDL_DestroyTexture(hw.texture);   hw.texture = NULL; }
    if (hw.renderer) { SDL_DestroyRenderer(hw.renderer); hw.renderer = NULL; }
    if (hw.window)   { SDL_DestroyWindow(hw.window);     hw.window = NULL; }
    SDL_Quit();
}

/* ---- presentation ---- */

/* File scope so the quit hook can read it; the counter was previously function-local. */
static long frames;
long hostwin_frames(void) { return frames; }

static void frame_pace(void);
/* Declared here because both the present and the frame boundary need it, and its definition
 * sits further down with the controls hint it belongs to. */
static int hint_on;
/* The composition the last copy-to-primary came from, and the centring offset it was copied
 * with. Recorded at the copy and consumed at the present, because those are two different
 * calls and only the first knows which surface the frame was built in. */
static uint32_t frame_src_pixels;
static int      frame_src_off;
void frame_source_note(uint32_t pixels, int off) { frame_src_pixels = pixels; frame_src_off = off; }

/* WHICH surface the frame is being composed into, for a caller that wants to put something in
 * the display list at a particular point rather than at the end (issue #62's stage geometry).
 *
 * It is DISCOVERED, from the source of the game's own copy to the primary, not hardcoded -- so
 * it is 0 until the first such copy has happened, which is once per process and not once per
 * frame. A caller that gets 0 must count that rather than skip quietly: "the composition is not
 * known yet" and "there was nothing to draw" produce the same empty frame. */
uint32_t frame_source_pixels(void) { return frame_src_pixels; }

void hostwin_present(const uint8_t *pixels, int w, int h, int src_pitch,
                     uint32_t source_pixels, int source_off)
{
    rwatch_frame();
    if (++frames % 60 == 1) fprintf(stderr, "present #%ld %dx%d renderer=%p\n", frames, w, h, (void *)hw.renderer);
    /* The real loader runs, but its draw list is never part of the port's presentation. Keep
     * pacing/frame boundaries and discard the list until startup admits the mode menu. */
    if (!startup_present_enabled()) {
        render_frame_reset();
        frame_pace();
        return;
    }
    /* DRAW FIRST, then look at what was drawn. The GPU frame has to exist before it can be
     * read back, and getting this order wrong is not a subtle bug with an obvious symptom:
     * reading the target before drawing dumps the PREVIOUS frame, and with the camera
     * scrolling about a pixel a frame that presents as a clean one-pixel horizontal shift of
     * the whole world -- which reads as a sampler or half-texel problem and sends you looking
     * in entirely the wrong place. */
    static uint32_t *shot;
    static int shot_w, shot_h;
    const uint8_t *shown = pixels;
    int shown_pitch = src_pitch;
    /* RmlUi uses the game's ordinary update/draw/present path; it does not substitute a
     * retained or frozen frame beneath the document. */
    const int gpu = hw.renderer && render_gpu_enabled()
                    && render_present(source_pixels, source_off, w, h);
    int shown_w = w, shown_h = h;
    /* READ THE FRAME BACK ONLY IF SOMETHING WILL LOOK AT IT (issue #57). A readback is a
     * full GPU-to-CPU stall: the CPU waits for every queued draw to finish before the pixels
     * can be copied, so doing it per frame throws away the pipelining that makes a GPU
     * renderer worth having. Both consumers are off in an ordinary run -- the screen-change
     * detector needs LF2_SCREEN_HASH and the dump needs this frame to be in LF2_FRAME_DUMP --
     * and the readback was being paid for on every frame regardless.
     *
     * Measured on a 2400-frame headless run: the GPU path was about three times the software
     * one and spent roughly 30% of its wall clock waiting, while the software path ran at 96%
     * CPU. That wait is this call. */
    const int want_pixels = gpu && (frame_wanted(frames) || getenv("LF2_SCREEN_HASH") != NULL);
    if (want_pixels) {
        /* The GPU frame is the size of the OUTPUT, not of the composition -- the renderer
         * draws at the window's resolution. Dumping it at the composition's size would slice
         * the top-left corner out of it and call that the frame. */
        render_output_size(&shown_w, &shown_h);
        if (shown_w <= 0 || shown_h <= 0) { shown_w = w; shown_h = h; }
        if (shot && (shot_w != shown_w || shot_h != shown_h)) { free(shot); shot = NULL; }
        if (!shot) {
            shot = malloc((size_t)shown_w * (size_t)shown_h * 4);
            shot_w = shown_w; shot_h = shown_h;
        }
        if (shot && render_readback(shot, shown_w, shown_h, shown_w * 4)) {
            shown = (const uint8_t *)shot;
            shown_pitch = shown_w * 4;
        } else { shown_w = w; shown_h = h; }
    }
    /* Whatever was presented is what the dumps and the screen-change detector see. Dumping
     * `pixels` regardless would hand every A/B the software compositor's buffer no matter
     * which renderer drew the screen, and the comparison would be of a buffer against
     * itself. */
    screen_change_check(shown, shown_w, shown_h, shown_pitch, frames);
    dump_frame(shown, shown_w, shown_h, shown_pitch, frames);
    dump_data(frames);
    dump_heap(frames);
    /* Periodic, not one-shot: a single report at frame 900 lands before the match has
     * started, so it measures the menus and reads as if nothing ever plays. */
    if (frames % 900 == 0) { colorkey_report(); draw_paths_report(); render_report(); vram_report(); world_band_report(); glyph_scale_report(); framing_report(); com_release_report(); input_report(); audio_pan_report(); bg_camera_report(); bg_geom_report(); mode_force_report(); if (getenv("LF2_AUDIO_DEBUG")) audio_report(); }
    /* The read profile is reported on the same periodic boundary and reset each time, so
     * each block covers one window rather than the whole run: an array swept only during a
     * match would otherwise be averaged with the menus that came before it. */
    if (frames % 300 == 0) {
        char when[32];
        snprintf(when, sizeof when, "frames %ld-%ld", frames - 299, frames);
        rwatch_raw_flush(when);
    }
    render_frame_reset();
    if (!hw.renderer) return;
    if (gpu) { startup_reveal_window(hw.window); frame_pace(); return; }
    /* The texture is the size of the composition, and the composition follows the window, so
     * it is checked against the frame in hand rather than created once. Sizes are kept here
     * because SDL_GetTextureSize is a call per frame to learn what this port already knows. */
    static int tex_w, tex_h;
    if (hw.texture && (tex_w != w || tex_h != h)) {
        SDL_DestroyTexture(hw.texture);
        hw.texture = NULL;
    }
    if (!hw.texture) {
        hw.texture = SDL_CreateTexture(hw.renderer, SDL_PIXELFORMAT_XRGB8888,
                                       SDL_TEXTUREACCESS_STREAMING, w, h);
        SDL_SetTextureScaleMode(hw.texture, SDL_SCALEMODE_NEAREST);
        tex_w = w; tex_h = h;
    }
    void *dst = NULL;
    int pitch = 0;
    if (SDL_LockTexture(hw.texture, NULL, &dst, &pitch)) {
        for (int y = 0; y < h; y++) {
            uint32_t *row = (uint32_t *)((uint8_t *)dst + (size_t)y * (size_t)pitch);
            /* Rows are src_pitch apart, not width apart. */
            const uint32_t *src = (const uint32_t *)(pixels + (size_t)y * (size_t)src_pitch);
            memcpy(row, src, (size_t)w * 4);
        }
        SDL_UnlockTexture(hw.texture);
    }
    /* THE SOFTWARE PATH STRETCHES ONE FINISHED BUFFER, and there is nothing better it can do:
     * by the time a frame reaches here every sprite has been flattened into these pixels, so
     * the only scale available is a scale of the whole picture. That is exactly what issue #41
     * says not to ship as the feature -- and it is not the feature; the native renderer scales
     * per quad at full resolution and this is the fallback for when it is off or unavailable.
     * Presenting it 1:1 in the middle of a large window instead would leave the fallback
     * showing a small picture in a black field, which is worse and no more honest. */
    SDL_FRect place;
    lf2_compose_rect(w, h, &place);
    SDL_SetRenderDrawColor(hw.renderer, 0, 0, 0, 255);
    SDL_RenderClear(hw.renderer);
    SDL_RenderTexture(hw.renderer, hw.texture, NULL, &place);
    if (rmlui_active()) rmlui_render();
    SDL_RenderPresent(hw.renderer);
    startup_reveal_window(hw.window);
    frame_pace();
}

/* ---- real time, and it lives HERE now ----
 *
 * The guest clock is the frame counter (runtime/win32/imports.c), so the game's main loop is
 * always "overdue" by its own reckoning and never sleeps -- it runs frames as fast as it is
 * given them. What makes those frames arrive thirty times a second is this: each present
 * waits until the WALL reaches the frame's due time. The guest counts, the host paces.
 *
 * Waiting until a deadline rather than sleeping a fixed amount is what absorbs nanosleep's
 * overshoot: a frame that ran long leaves the next one with less to wait, so the rate holds
 * instead of drifting. When the machine cannot keep up there is nothing to wait for and the
 * frames simply arrive late -- the guest's timeline does not notice, which is the point.
 *
 * NOT WHILE LOADING. The data load draws a frame per file and has no business being held to
 * thirty a second; pacing it would put ten seconds back into a load this port spent real
 * effort taking out. The anchor is re-taken when the debt passes a quarter second, so a load
 * or a stall cannot leave the game sprinting through the following minute to catch up. */
/* LF2_UNPACED=1 runs frames as fast as the machine will do them.
 *
 * The pacer exists so the game runs at the speed a player experiences: the guest clock IS the
 * frame counter, so the game never sleeps of its own accord and every frame would otherwise
 * arrive as fast as the CPU could make it. That is exactly what a scripted test wants. Route
 * tests are frame-numbered end to end -- LF2_QUIT_AFTER, LF2_FRAME_DUMP and every `@screen+N`
 * key are counts, not clocks -- so removing the sleep changes which frames happen not at all,
 * only how long it takes to get to them. A 3000-frame route spent 100 seconds of its 100
 * seconds asleep.
 *
 * IT IS AN EXPLICIT SWITCH AND NOT DERIVED FROM THE VIDEO DRIVER, which was the first idea:
 * "nobody can watch an offscreen surface, so do not pace it". That would silently break the
 * one test that must stay paced. tools/routes/smoke_test.sh asserts CPU usage under 50% of a core --
 * the busy-wait guard, which separates a run that honours Sleep (~13%) from one that spins
 * (~96%) -- and that check is only meaningful while the pacer is doing its job. Two headless
 * runs need opposite behaviour and nothing about the environment tells them apart, so the
 * caller says which it wants. */
static int frame_unpaced(void)
{
    static int on = -1;
    if (on < 0) {
        on = getenv("LF2_UNPACED") != NULL;
        if (on)
            fprintf(stderr, "pacing: LF2_UNPACED -- frames run as fast as the machine allows. "
                            "Every route key is a frame count, so this changes how long the "
                            "run takes and nothing about what happens in it.\n");
    }
    return on;
}

static void frame_pace(void)
{
    static uint64_t anchor_wall, anchor_frame;

    if (frame_unpaced()) return;

    /* The anchor is DROPPED while loading, not merely unused. Frames keep being counted
     * through the load and the wall does not follow them, so an anchor taken before it
     * would leave every frame after it due far in the future -- measured, the game then
     * slept its way through a whole run at under 12 fps with 1.8 s of CPU used. Pacing
     * re-anchors on the first frame after the load instead. */
    if (lf2_loading_now()) { anchor_wall = 0; return; }
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const uint64_t w = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    const uint64_t f = (uint64_t)hostwin_frames();

    if (!anchor_wall) { anchor_wall = w; anchor_frame = f; return; }
    const uint64_t due = anchor_wall + (f - anchor_frame) * (uint64_t)GUEST_FRAME_NS;
    if (due > w) {
        const uint64_t d = due - w;
        struct timespec req = { (time_t)(d / 1000000000ull), (long)(d % 1000000000ull) };
        nanosleep(&req, NULL);
    } else if (w - due > 250000000ull) {
        anchor_wall = w; anchor_frame = f;
    }
}

/* The one keyboard layout, shown where the advertising used to sit. Drawn onto the
 * primary just before present, so it rides above whatever the game composed; the menu
 * override turns it on outside the game proper and off inside it. Rendered with the same
 * glyph renderer the game's own text uses -- without SDL3_ttf there are no glyphs and
 * the hint simply does not appear, which costs nothing but the hint. */
void controls_hint_enable(int on) { hint_on = on; }

static void controls_hint_draw(const Surface *s)
{
    static const char TEXT[] = "KEYBOARD:  ARROWS MOVE   Z ATTACK   X JUMP   C DEFEND";
    /* Left, because the game's own URL owns the right -- but inside the centred picture,
     * not inside the black band beside it, or the line starts in the margin and crosses
     * the edge halfway through a word. */
    const int x0 = 8 + screen_offset_x();
    /* TWO DESTINATIONS, ONE LINE, and the split is issue #52's fix. The pixels go on the
     * PRIMARY, which is what the software compositor presents. The display-list tiles go on
     * the COMPOSITION -- the surface the native renderer builds its frame from -- because a
     * frame it cannot see the hint in is a frame it is not allowed to present, and that gate
     * is what kept the renderer off every menu screen in the game.
     *
     * The composition's list is still intact here: this runs after the copy-to-primary and
     * before hostwin_present, and render_frame_reset happens after the present. The tiles
     * land at the END of the list, which is exactly where a hint drawn over the frame
     * belongs. Both surfaces are the same size and the centring is already baked into the
     * composition (frame_source_note passes off 0), so one x serves both. */
    if (frame_src_pixels && frame_src_pixels != s->pixels)
        render_overlay_mark(frame_src_pixels);
    for (int i = 0; TEXT[i]; i++) {
        const int x = x0 + i * 8, y = s->h - 16;
        if (frame_src_pixels && frame_src_pixels != s->pixels)
            game_glyph_tile(TEXT[i], x, y, 0xffffffu, frame_src_pixels);
        game_glyph_draw(TEXT[i], x, y, 0xffffffu, s->pixels, s->w, s->h, s->pitch);
    }
}

/* ---- the device icons (issues #74, #77) ----
 *
 * The in-match HUD panels and the character-select portraits are each a human or a computer,
 * and nothing on either says which device drives a human one. The shared keyboard/gamepad SVG
 * does. It is drawn here, at the present, because this is where
 * the port knows both the panel's place on the screen AND which device claimed which slot
 * (input.c's dev_player table, reversed by device_for_player).
 *
 * THE HUD PANELS' OWN GEOMETRY, read off fn_0041ae60's decompilation: slot i sits at
 * ((i & 3) * 0xc6, (i >> 2) * 0x36) -- 198 px wide, 54 tall, two rows of four -- and on a
 * wide view the whole strip takes the HUD's centring offset (hud_offset_x), which is the
 * same number the panels' own blits got. A computer slot carries no device and gets nothing.
 *
 * THE CHARACTER-SELECT SLOTS are the same 2x4 grid of portraits (screens.c's CS_COL/CS_ROW),
 * so the same (col, row) cell and the same icon work for both screens; charselect is
 * centred by screen_offset_x() like the other fixed-794 screens. The overlay keeps that screen
 * word, so labels enter painter order before its panel instead of being appended at present. */
static void device_icon_draw(const Surface *s, int x, int y, int dev)
{
    /* The helper records a premultiplied display-list tile and composites the same decoded
     * pixels into the software primary. Both renderer paths therefore use one rasterisation. */
    const uint32_t record_pixels = s->primary ? frame_src_pixels : s->pixels;
    if (record_pixels) {
        render_overlay_mark(record_pixels);
        device_icon_record(record_pixels, x, y, dev);
    }
    device_icon_paint(s->pixels, s->w, s->h, s->pitch, x, y, dev);
}

static void hud_device_labels(const Surface *s)
{
    if (!device_icon_hud_visible(panel_hud_up(), panel_overlay_up())) return;
    for (int i = 0; i < 8; i++) {
        const int dev = device_for_player(i);
        if (dev < 0) continue;                       /* a computer, or nobody claimed it */
        const int off = hud_offset_x(s->w, (i >> 2) * 54 + 54);
        device_icon_draw(s, (i & 3) * 198 + off + 2, (i >> 2) * 54 + 2, dev);
    }
}

/* Character-select rows and columns are NOT the HUD's grid: the portraits sit at x
 * {147,300,453,606} and y {95,306} (screens.c's CS_COL/CS_ROW), each 153 px wide -- the HUD's
 * 198-wide panels are only the strip along the top. */
static const int CS_XBASE[4] = { 147, 300, 453, 606 };
static const int CS_YBASE[2] = { 95, 306 };

static void charselect_device_labels_draw(const Surface *s)
{
    const int off = screen_offset_x();
    for (int i = 0; i < 8; i++) {
        const int dev = device_for_player(i);
        if (dev < 0) continue;
        device_icon_draw(s, CS_XBASE[i & 3] + off + 2, CS_YBASE[i >> 2] + 2, dev);
    }
}

static void charselect_device_labels_present(const Surface *s)
{
    const int charselect_up = LD32(0x0044d020u) == 1;
    if (device_icon_charselect_phase(charselect_up, panel_overlay_up())
        == DEVICE_ICON_CHARSELECT_PRESENT)
        charselect_device_labels_draw(s);
}

static void present_primary(void);

static void present_primary(void)
{
    if (!primary_surface) return;
    LOADPROF_SCOPE(LP_PRESENT);
    Surface *s = com_host(primary_surface);
    if (hint_on) controls_hint_draw(s);
    hud_device_labels(s);
    charselect_device_labels_present(s);
    hostwin_present(g_mem + s->pixels, s->w, s->h, s->pitch,
                    frame_src_pixels, frame_src_off);
    LOADPROF_END();
}

/* ---- IDirectDrawPalette ---- */

static void pal_SetEntries(uint32_t self)
{
    Palette *p = com_host(self);
    const uint32_t start = ARG(2), count = ARG(3), src = ARG(4);
    for (uint32_t i = 0; i < count && start + i < 256; i++) {
        const uint32_t e = src + i * 4;
        p->entries[start + i] = ((uint32_t)LD8(e) << 16) | ((uint32_t)LD8(e + 1) << 8)
                              | (uint32_t)LD8(e + 2);
    }
    com_ret(5, DD_OK);
}

static void pal_GetEntries(uint32_t self)
{
    Palette *p = com_host(self);
    const uint32_t start = ARG(2), count = ARG(3), dst = ARG(4);
    for (uint32_t i = 0; i < count && start + i < 256; i++) {
        const uint32_t c = p->entries[start + i], e = dst + i * 4;
        ST8(e, (uint8_t)(c >> 16)); ST8(e + 1, (uint8_t)(c >> 8));
        ST8(e + 2, (uint8_t)c); ST8(e + 3, 0);
    }
    com_ret(5, DD_OK);
}

/* ---- IDirectDrawSurface ---- */

/* ---- LF2_DRAW_PATHS=1: which routes actually carry pixels ----
 *
 * A native renderer (issue #30) can only be fed from named draw calls if the draw calls are
 * where the pixels are. Three routes exist in this file -- Blt, BltFast, and the game writing
 * straight into a surface between Lock and Unlock -- and counting the first while assuming the
 * other two are idle is exactly the kind of negative that cannot contradict itself.
 *
 * So all three are counted, and the Lock route is counted by whether the pixels CHANGED, not
 * by whether a Lock happened: a Lock taken to read is not a draw. The hash is over the whole
 * surface, which is why this is behind a switch.
 */
static long path_blt, path_bltfast, path_lock, path_lock_wrote;
static uint32_t lock_hash_at_lock;
static uint32_t lock_hash_surface;

static int draw_paths_on(void)
{
    static int on = -1;
    if (on < 0) on = getenv("LF2_DRAW_PATHS") != NULL;
    return on;
}

static uint32_t surface_hash(const Surface *s)
{
    uint32_t h = 2166136261u;
    for (int y = 0; y < s->h; y++) {
        const uint32_t *row = (const uint32_t *)(g_mem + s->pixels + (size_t)y * (size_t)s->pitch);
        for (int x = 0; x < s->w; x++) { h ^= row[x]; h *= 16777619u; }
    }
    return h;
}

/* Prints every counter including the zeros, and says what it could not see. "BltFast: 0" and
 * "I never instrumented BltFast" must not look alike -- that distinction is the entire reason
 * this exists rather than a grep of the blit log. */
void draw_paths_report(void)
{
    if (!draw_paths_on()) return;
    fprintf(stderr, "draw paths: Blt=%ld BltFast=%ld Lock=%ld (of which changed pixels=%ld)\n",
            path_blt, path_bltfast, path_lock, path_lock_wrote);
    if (!path_blt && !path_bltfast && !path_lock)
        fprintf(stderr, "draw paths: ALL ZERO -- this run drew nothing at all, so it says "
                        "nothing about which route the game uses\n");
    else if (!path_lock_wrote && path_lock)
        fprintf(stderr, "draw paths: %ld locks and none of them changed a pixel, so on this "
                        "route the game reads and does not draw\n", path_lock);
    fprintf(stderr, "draw paths: NOT covered -- GDI text goes straight into the surface "
                    "without Lock (runtime/win32/gdi.c), and a lock whose writes cancel out would "
                    "hash the same. Both would read as no-draw here.\n");
}

static void surf_Lock(uint32_t self)
{
    Surface *s = com_host(self);
    const uint32_t desc = ARG(2);
    ST32(desc + SD_SIZE, SD_BYTES);
    ST32(desc + SD_FLAGS, DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT);
    ST32(desc + SD_HEIGHT, (uint32_t)s->h);
    ST32(desc + SD_WIDTH, (uint32_t)s->w);
    ST32(desc + SD_PITCH, (uint32_t)s->pitch);
    ST32(desc + SD_SURFACE, s->pixels);
    write_pixelformat(desc + SD_PIXELFORMAT);
    if (draw_paths_on()) {
        path_lock++;
        lock_hash_surface = self;
        lock_hash_at_lock = surface_hash(s);
    }
    com_ret(5, DD_OK);
}

static void surf_Unlock(uint32_t self)
{
    Surface *s = com_host(self);
    if (draw_paths_on() && self == lock_hash_surface
        && surface_hash(s) != lock_hash_at_lock) path_lock_wrote++;
    surface_changed(s);
    if (s->primary) present_primary();
    com_ret(2, DD_OK);
}

static void surf_GetSurfaceDesc(uint32_t self)
{
    Surface *s = com_host(self);
    const uint32_t desc = ARG(1);
    ST32(desc + SD_SIZE, SD_BYTES);
    ST32(desc + SD_FLAGS, DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT);
    ST32(desc + SD_HEIGHT, (uint32_t)s->h);
    ST32(desc + SD_WIDTH, (uint32_t)s->w);
    ST32(desc + SD_PITCH, (uint32_t)s->pitch);
    ST32(desc + SD_SURFACE, s->pixels);
    write_pixelformat(desc + SD_PIXELFORMAT);
    ST32(desc + SD_CAPS, s->primary ? DDSCAPS_PRIMARYSURFACE : 0);
    com_ret(2, DD_OK);
}

static void read_rect(uint32_t p, int *l, int *t, int *r, int *b, int dw, int dh)
{
    if (!p) { *l = 0; *t = 0; *r = dw; *b = dh; return; }
    *l = (int)LD32(p); *t = (int)LD32(p + 4);
    *r = (int)LD32(p + 8); *b = (int)LD32(p + 12);
}

/* DirectDraw stretches when the destination rectangle differs in size from the source,
 * and a NULL source rectangle means the whole surface. Copying 1:1 and ignoring the
 * destination extent draws the wrong part of the source wherever the game scales. */
/* The source column for each destination column, computed once per blit.
 *
 * The inner loop used to evaluate `sx + (int64_t)x * sw / dw` PER PIXEL -- a 64-bit
 * multiply and an integer divide for every one of the 437,000 pixels in a full-screen
 * paint. Stack sampling during a load put three of six samples inside this function and
 * StretchBlt, and each full-screen paint measured around 20 ms, which is ~46 ns a pixel.
 *
 * The mapping depends only on x, so it is hoisted to one divide per COLUMN. That is the
 * same arithmetic, not an approximation: a fixed-point stepper would round differently and
 * shift pixels, and this path draws every sprite in the game. */
enum { BLIT_MAXW = 4096 };

static void blit_mapped(Surface *d, int dx, int dy, int dw, int dh,
                        Surface *s, int sx, int sy, int sw, int sh,
                        int keyed, uint32_t klo, uint32_t khi, int mirror_x)
{
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;

    const uint32_t lo = klo & 0x00ffffffu, hi = khi & 0x00ffffffu;

    /* Destination columns that land inside both surfaces, and their source column.
     * Clipping here as well means the inner loop carries no per-pixel branches. */
    static int col_src[BLIT_MAXW], col_dst[BLIT_MAXW];
    int ncol = 0;
    const int wlim = dw < BLIT_MAXW ? dw : BLIT_MAXW;
    for (int x = 0; x < wlim; x++) {
        const int source_offset = (int)((int64_t)x * sw / dw);
        const int sxx = mirror_x ? sx + sw - 1 - source_offset : sx + source_offset;
        const int dxx = dx + x;
        if (sxx < 0 || sxx >= s->w || dxx < 0 || dxx >= d->w) continue;
        col_src[ncol] = sxx; col_dst[ncol] = dxx; ncol++;
    }
    if (!ncol) return;

    /* A run of columns that is contiguous and 1:1 (the unscaled case, which is most of
     * them) can be copied without indirection. */
    const int direct = (ncol > 1) && (col_src[1] - col_src[0] == 1)
                    && (col_src[ncol - 1] - col_src[0] == ncol - 1)
                    && (col_dst[ncol - 1] - col_dst[0] == ncol - 1);

    for (int y = 0; y < dh; y++) {
        const int syy = sy + (int)((int64_t)y * sh / dh), dyy = dy + y;
        if (syy < 0 || syy >= s->h || dyy < 0 || dyy >= d->h) continue;
        const uint32_t *sp = (const uint32_t *)(g_mem + s->pixels + (size_t)syy * (size_t)s->pitch);
        uint32_t *dp = (uint32_t *)(g_mem + d->pixels + (size_t)dyy * (size_t)d->pitch);

        if (direct) {
            const uint32_t *srow = sp + col_src[0];
            uint32_t *drow = dp + col_dst[0];
            if (!keyed) {
                for (int i = 0; i < ncol; i++) drow[i] = srow[i] & 0x00ffffffu;
            } else {
                for (int i = 0; i < ncol; i++) {
                    const uint32_t v = srow[i] & 0x00ffffffu;
                    if (v >= lo && v <= hi) continue;
                    drow[i] = v;
                }
            }
        } else if (!keyed) {
            for (int i = 0; i < ncol; i++) dp[col_dst[i]] = sp[col_src[i]] & 0x00ffffffu;
        } else {
            for (int i = 0; i < ncol; i++) {
                const uint32_t v = sp[col_src[i]] & 0x00ffffffu;
                if (v >= lo && v <= hi) continue;
                dp[col_dst[i]] = v;
            }
        }
    }
    surface_changed(d);
}

static void blit(Surface *d, int dx, int dy, int dw, int dh,
                 Surface *s, int sx, int sy, int sw, int sh,
                 int keyed, uint32_t klo, uint32_t khi)
{
    blit_mapped(d, dx, dy, dw, dh, s, sx, sy, sw, sh, keyed, klo, khi, 0);
}

static void blit_mirror_x(Surface *d, int dx, int dy, int dw, int dh,
                          Surface *s, int sx, int sy, int sw, int sh,
                          int keyed, uint32_t klo, uint32_t khi)
{
    blit_mapped(d, dx, dy, dw, dh, s, sx, sy, sw, sh, keyed, klo, khi, 1);
}

/* ---- THE SPACE BESIDE A SCREEN WHOSE BACKDROP IS A PICTURE (issue #44) ----
 *
 * SAY THIS FIRST: THERE IS NO FAITHFUL ANSWER HERE AND THIS IS A PORT DECISION, NOT THE
 * GAME'S. The loading screen's background is not a colour the port can extend, it is the
 * resource bitmap MENU_WAIT drawn whole at (0,0) -- one blit, no fill of its own -- and the
 * game draws NOTHING beside it, because the game never has a screen wider than its picture.
 * Anything the port puts in those columns is invented. The reporter asked for them to be
 * filled rather than left black; this is the least-inventing fill available, and it is
 * declared as a choice rather than shipped as fidelity.
 *
 * WHAT IT DOES: continues each ROW's edge colour sideways -- clamp-to-edge. It is chosen
 * because it invents no SHAPE and no LAYOUT: every column it writes is a copy of a column the
 * game itself drew, and the picture's own backdrop is a horizontally-uniform vertical gradient
 * (measured: its left and right edge columns agree to within 4/255 on 405 of its 550 rows), so
 * on those rows the extension is what a wider version of that same gradient would be. On the
 * remaining rows the artwork itself reaches an edge and those rows smear their edge colour
 * outwards -- that is the visible cost of the choice, and it is a colour band, never a
 * duplicated shape.
 *
 * WHAT IT IS NOT, and both were rejected on the rules this port already has: STRETCHING the
 * picture across the composition, which invents layout the game does not have (issue #42 for
 * a menu backdrop, issue #23 for a stage's sky), and MIRRORING or TILING it, which invents a
 * second copy of the artwork.
 *
 * The runs are merged so a gradient costs a few dozen display-list entries rather than one per
 * row, and both the software composition and the recorded list get the same rectangles, so the
 * GPU and software frames stay comparable. Stage art does not use this colour clamp: Lion
 * Forest's explicitly authored opaque far plane continues as native-size reflected segments. */
static void band_paint(Surface *d, int x, int w, int top, int bot, uint32_t c)
{
    if (w <= 0 || bot <= top) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > d->w) w = d->w - x;
    if (w <= 0) return;
    render_fill(d->pixels, x, top, x + w, bot, c);
    for (int y = top; y < bot && y < d->h; y++) {
        if (y < 0) continue;
        uint32_t *row = (uint32_t *)(g_mem + d->pixels + (size_t)y * (size_t)d->pitch);
        for (int i = x; i < x + w; i++) row[i] = c;
    }
}

static void backdrop_edge_bands(Surface *d, Surface *s,
                                int sl, int st_, int sr, int sb,
                                int dl, int dt, int dr, int db)
{
    if (db <= dt || sb <= st_ || sr <= sl) return;
    if (sl < 0 || sr > s->w) return;

    BackdropBand left, right;
    const int have_left = backdrop_side_band(1, d->w, -1, dl, dt, dr, db, sl, sr, &left);
    const int have_right = backdrop_side_band(1, d->w, 1, dl, dt, dr, db, sl, sr, &right);
    if (!have_left && !have_right) return;

    int have = 0, run_from = dt;
    uint32_t run_l = 0, run_r = 0;
    for (int y = dt; y <= db; y++) {
        uint32_t cl = 0, cr = 0;
        int valid = 0;
        if (y < db && y >= 0 && y < d->h) {
            const int sy = st_ + (int)((int64_t)(y - dt) * (sb - st_) / (db - dt));
            if (sy >= 0 && sy < s->h) {
                const uint32_t *row =
                    (const uint32_t *)(g_mem + s->pixels + (size_t)sy * (size_t)s->pitch);
                if (have_left) cl = row[left.source_x] & 0x00ffffffu;
                if (have_right) cr = row[right.source_x] & 0x00ffffffu;
                valid = 1;
            }
        }
        if (have && (!valid || cl != run_l || cr != run_r)) {
            if (have_left) band_paint(d, left.dl, left.dr - left.dl, run_from, y, run_l);
            if (have_right) band_paint(d, right.dl, right.dr - right.dl, run_from, y, run_r);
            have = 0;
        }
        if (valid && !have) {
            run_from = y;
            run_l = cl;
            run_r = cr;
            have = 1;
        }
    }
}

/* A declared far-plane edge continues in native-size segments whose X direction alternates.
 * This costs one quad per segment rather than one per column; destination/source extents remain
 * exactly equal, and keyed scenery never reaches this boundary with continuation flags. */
static void backdrop_mirror_segments(Surface *d, Surface *s, int flags,
                                     int sl, int st_, int sr, int sb,
                                     int dl, int dt, int dr, int db,
                                     int keyed, const BltTrace *trace)
{
    for (int segment = 0;; segment++) {
        BackdropBlit piece;
        if (!backdrop_mirror_segment(flags, d->w, segment, dl, dt, dr, db, sl, st_, sr, sb, &piece)) break;
        blt_trace_backdrop(trace, &piece);
        if (!d->primary) {
            if (piece.mirror_x)
                render_blit_mirror_x(d->pixels, piece.dl, piece.dt, piece.dr, piece.db,
                                     s->pixels, s->w, s->h, s->pitch,
                                     piece.sl, piece.st, piece.sr, piece.sb,
                                     keyed, s->key_lo, s->key_hi);
            else
                render_blit(d->pixels, piece.dl, piece.dt, piece.dr, piece.db,
                            s->pixels, s->w, s->h, s->pitch,
                            piece.sl, piece.st, piece.sr, piece.sb,
                            keyed, s->key_lo, s->key_hi);
        }
        if (piece.mirror_x)
            blit_mirror_x(d, piece.dl, piece.dt, piece.dr - piece.dl, piece.db - piece.dt,
                          s, piece.sl, piece.st, piece.sr - piece.sl, piece.sb - piece.st,
                          keyed, s->key_lo, s->key_hi);
        else
            blit(d, piece.dl, piece.dt, piece.dr - piece.dl, piece.db - piece.dt,
                 s, piece.sl, piece.st, piece.sr - piece.sl, piece.sb - piece.st,
                 keyed, s->key_lo, s->key_hi);
    }
}
static void dump_surface(uint32_t obj, const char *tag)
{
    Surface *s = com_host(obj);
    if (!s) return;
    char path[160];
    dump_path(path, sizeof path, "dump_%s.ppm", tag);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", s->w, s->h);
    for (int y = 0; y < s->h; y++) {
        const uint32_t *r = (const uint32_t *)(g_mem + s->pixels + (size_t)y * (size_t)s->pitch);
        for (int x = 0; x < s->w; x++) {
            const uint8_t px[3] = { (uint8_t)(r[x] >> 16), (uint8_t)(r[x] >> 8), (uint8_t)r[x] };
            fwrite(px, 1, 3, f);
        }
    }
    fclose(f);
}

/* LF2_CURSOR_FIND=1 -- which blit is the game's own mouse cursor?
 *
 * "Lands near the pointer" is NOT the test: the full-screen background blits at (0,0) and
 * scores 96% whenever the pointer happens to rest near the origin, which is exactly the
 * false positive the first version of this hook produced.
 *
 * A cursor keeps a CONSTANT offset from the pointer wherever the pointer goes. So the
 * measure is the spread of (dest - pointer) across many different pointer positions: a
 * cursor's spread is a few pixels, a fixed decoration's spread is as large as the
 * pointer's travel. The pointer's own travel is reported alongside, because a spread of
 * zero proves nothing if the pointer never moved.
 *
 * Called from BOTH surf_Blt and surf_BltFast. Hooking only Blt reported "no blit tracks
 * the pointer" while the cursor was plainly in the framebuffer -- a clean-looking negative
 * produced by not looking at the other half of the drawing paths. */
void cursor_find_note(int dl, int dt, const char *via)
{
    static int on = -1;
    if (on < 0) on = getenv("LF2_CURSOR_FIND") != NULL;   /* cached: this is on every blit */
    if (!on) return;
    enum { MAX_SITES = 64 };
    static struct { uint32_t ra; const char *via; long n; int lo_x, hi_x, lo_y, hi_y; }
        site[MAX_SITES];
    static int nsite;
    static long frames;
    static int p_lo_x = 1 << 30, p_hi_x = -(1 << 30), p_lo_y = 1 << 30, p_hi_y = -(1 << 30);

    const int mx = (int)LD32(0x004546f0), my = (int)LD32(0x00453cdc);
    if (mx < p_lo_x) p_lo_x = mx;
    if (mx > p_hi_x) p_hi_x = mx;
    if (my < p_lo_y) p_lo_y = my;
    if (my > p_hi_y) p_hi_y = my;

    const uint32_t ra = LD32(R(ESP));
    const int ox = dl - mx, oy = dt - my;
    int k = 0;
    for (; k < nsite; k++) if (site[k].ra == ra) break;
    if (k == nsite && nsite < MAX_SITES) {
        site[k].ra = ra; site[k].via = via; site[k].n = 0;
        site[k].lo_x = site[k].hi_x = ox;
        site[k].lo_y = site[k].hi_y = oy;
        nsite++;
    }
    if (k < MAX_SITES) {
        site[k].n++;
        if (ox < site[k].lo_x) site[k].lo_x = ox;
        if (ox > site[k].hi_x) site[k].hi_x = ox;
        if (oy < site[k].lo_y) site[k].lo_y = oy;
        if (oy > site[k].hi_y) site[k].hi_y = oy;
    }
    if (++frames % 4000 == 0) {
        fprintf(stderr, "cursor-find: pointer travelled x %d..%d, y %d..%d\n",
                p_lo_x, p_hi_x, p_lo_y, p_hi_y);
        if (p_hi_x - p_lo_x < 40 && p_hi_y - p_lo_y < 40)
            fprintf(stderr, "  POINTER BARELY MOVED -- this cannot identify a cursor\n");
        for (int i = 0; i < nsite; i++) {
            const int sx = site[i].hi_x - site[i].lo_x, sy = site[i].hi_y - site[i].lo_y;
            if (site[i].n < 50) continue;
            fprintf(stderr, "  %-8s ra=%08x n=%-6ld spread x=%-5d y=%-5d offset(%d,%d) %s\n",
                    site[i].via, site[i].ra, site[i].n, sx, sy, site[i].lo_x, site[i].lo_y,
                    (sx <= 8 && sy <= 8) ? "<== TRACKS THE POINTER" : "");
        }
    }
}

/* ---- which post-load screen is up ----
 *
 * The ported menus on character selection and the pre-fight overlay both hit-test the
 * pointer, and the overlay is drawn ON TOP of character selection, so something has to say
 * which one owns the pointer this frame. The obvious place to look was a .data flag, and
 * that is exactly how this went wrong the first time: a word was found that separated the
 * two screens perfectly in stage mode (-100 / 0 / 1) and turned out to be the GAME MODE,
 * reading 1 in VS mode whether the overlay was up or not. It was derived from stage-mode
 * dumps and validated against stage-mode dumps, so it could not have failed.
 *
 * This asks the game instead. Both panels are drawn every frame they are up, at fixed
 * destinations taken from the blit log and confirmed identical in both modes:
 *
 *   character-select panel  (40,33)-(745,520)
 *   pre-fight overlay panel (3,3)-(307,159)
 *
 * A screen is "up" if its panel was drawn in the last couple of frames -- a small window,
 * because the menu override and the blits do not run in a fixed order within a frame, and
 * because a screen that stopped being drawn two frames ago is gone. */
enum { PANEL_FRESH = 2 };
static long panel_charselect_frame = -1000, panel_overlay_frame = -1000;
static long panel_hud_frame = -1000;
static long panel_modemenu_frame = -1000;

static void panel_note(int l, int t, int r, int b)
{
    if (l == 40 && t == 33 && r == 745 && b == 520) panel_charselect_frame = frames;
    else if (l == 3 && t == 3 && r == 307 && b == 159) panel_overlay_frame = frames;
    /* The in-match HUD strip: eight player slots as two rows of four 198x54 panels. It is
     * drawn only while a match is on screen, which makes it the signal for "the world view
     * is up" -- the one screen that should be WIDE rather than centred. */
    else if (r - l == 198 && b - t == 54 && (t == 0 || t == 54)) panel_hud_frame = frames;
}

/* ---- WHICH SCREEN IS UP, FROM THE COLOUR IT PAINTS ITSELF (issue #44) ----
 *
 * The two screens the reporter wants LEFT-aligned are the front end and the mode menu, and
 * they are NOT the same drawn screen: different backdrop colour, different character bitmap,
 * different menu sheet. So per-screen framing needs to identify two screens, and the port has
 * no signal for the second one -- the word screens.c calls MODEMENU_SEL is the GAME MODE, not
 * a screen (issue #51), which is the same trap the pre-fight overlay already fell into once.
 *
 * The identifier used here is the one thing each screen does that nothing else does: it paints
 * its whole 794x550 screen a flat colour of its own. Front end 0x10206c, mode menu 0x122565 --
 * and each of those two literals appears EXACTLY ONCE in the whole of .text (pushed at
 * 004270c9 and 00431d3a, both into the game's single colour-fill helper FUN_00415160). That
 * makes them identifications rather than coincidences, and it is the same rule the port
 * already prefers everywhere else: ask what the game DRAWS, not what a .data word holds.
 *
 * IT IS NOT A FRESHNESS WINDOW, unlike the panels above, and that is not a style choice: the
 * loading screen is on screen for TWO frames, and with the panels' two-frame window it
 * inherited the front end's LEFT alignment for the whole of its life -- measured, on the first
 * build of this. The alignment is a property of the screen whose BACKDROP was drawn most
 * recently, so every whole-screen backdrop sets it: a fill in one of the two menu colours says
 * LEFT, any other whole-screen fill and a whole-screen picture say CENTRED. Every screen paints
 * its own backdrop before its art, so within a frame the art that follows is already placed
 * correctly. */
enum { FILL_FRONT_END = 0x0010206cu, FILL_MODE_MENU = 0x00122565u };
static int screen_align_left;
static long backdrop_art_seen;   /* counted so the framing report cannot claim a draw it never saw */

/* WHAT THE FRAMING ACTUALLY DID, per screen, so a test can assert it from the run's own
 * output instead of from a screenshot somebody once looked at (LF2_FRAMING_DEBUG=1).
 *
 * Every fixed-794 screen reports, not only the two that moved: character selection is the
 * control for issue #44 and a log that only printed the screens that changed could not show
 * that it did not. The tally at the end names each screen it never saw, so a run that reached
 * none of them says so loudly rather than printing nothing and reading as a pass. */
static long framing_n_left, framing_n_centre, framing_n_picture;
static uint32_t framing_last = 0xfffffffful;

static void framing_note(const char *what, uint32_t key, int off, int left)
{
    if (key == framing_last) return;
    framing_last = key;
    if (!getenv("LF2_FRAMING_DEBUG")) return;
    fprintf(stderr, "framing: frame %ld %s -> %s, offset %d in a %d-wide composition "
                    "(the game's own screen is %d)\n",
            frames, what, left ? "CENTRED, backdrop art LEFT at x 0" : "CENTRED",
            off, hw.width, NATIVE_W);
}

void framing_report(void)
{
    if (!getenv("LF2_FRAMING_DEBUG")) return;
    /* The backdrop count is part of the answer and not decoration: "the menu screens were
     * seen" and "their left-anchored picture was actually drawn without the centring" are
     * different facts, and a report giving only the first would read as a pass on a build
     * where the picture never matched and quietly got centred with everything else. */
    fprintf(stderr, "framing: %ld menu screen(s) with a left-anchored backdrop (%ld such "
                    "draw(s) kept at x 0), %ld centred flat-backdrop screen(s), %ld "
                    "picture-backdrop screen(s) so far%s\n",
            framing_n_left, backdrop_art_seen, framing_n_centre, framing_n_picture,
            (framing_n_left || framing_n_centre || framing_n_picture) ? ""
              : " -- NO fixed-794 screen has been framed at all, so this run measured NOTHING "
                "about per-screen framing (not wide, or never left the world view)");
}

static void screen_fill_note(uint32_t colour, int l, int t, int r, int b)
{
    if (l != 0 || t != 0 || r != NATIVE_W || b != NATIVE_H) return;
    const uint32_t c = colour & 0x00ffffffu;
    const int left = (c == FILL_FRONT_END || c == FILL_MODE_MENU);
    screen_align_left = left;
    /* The mode menu's own colour, for the same reason: it is the only honest way to
     * say "the mode menu is on screen". The word screens.c used to gate on is the game
     * MODE, which reads 1/4/5 during a match (issue #51). */
    if (c == FILL_MODE_MENU) panel_modemenu_frame = frames;
    if (!lf2_wide_width() || panel_hud_up()) return;
    if (left) framing_n_left++; else framing_n_centre++;
    char what[64];
    snprintf(what, sizeof what, "screen with backdrop fill %06x", c);
    /* The offset REPORTED must be the offset APPLIED. This said GEOM_ALIGN_LEFT -> 0 for the
     * two menu screens while their content was in fact being centred by 874, because the rule
     * narrowed (only the backdrop ART is left-anchored, not the whole screen) and the report
     * kept the old question. A diagnostic that answers a question the code no longer asks is
     * worse than none: it was printing 0 next to a picture that had visibly moved. */
    framing_note(what, c | 0x40000000u, screen_offset_x(), left);
}

/* Exposed so tools/routes/widescreen_test.sh can assert the framing of each screen from the run's
 * own output rather than from a screenshot someone looked at once. */
/* Does the screen now up anchor its BACKDROP ART to x = 0? True for the front end and the
 * mode menu, whose portrait (MENU_BACK<n>) the game draws at a hard literal x = 0 so that it
 * bleeds off the screen's left edge. Nothing else about those screens is special: their menu
 * content is centred like every other screen's. */
int screen_backdrop_left(void) { return screen_align_left; }

int panel_charselect_up(void) { return frames - panel_charselect_frame <= PANEL_FRESH; }
int panel_overlay_up(void)    { return frames - panel_overlay_frame    <= PANEL_FRESH; }
int panel_hud_up(void)        { return frames - panel_hud_frame        <= PANEL_FRESH; }
/* The front end repaints its backdrop every frame it is up, so the same freshness window that
 * suits the panels suits it. It is deliberately NOT the loading screen or the mode menu: those
 * paint other colours, and a route anchored here means "the first screen, before anything has
 * been chosen". */
int panel_modemenu_up(void)   { return frames - panel_modemenu_frame   <= PANEL_FRESH; }

/* ---- centring what cannot be made wide ----
 *
 * Widescreen gives the MATCH more world, because the world is drawn from a camera and the
 * game's own viewport width drives it. Nothing else in the game works that way: the front
 * end, the mode menu, character selection and the pre-fight overlay are fixed 794-wide
 * compositions, and on a wider viewport they simply sat against the left edge with a black
 * band down the right.
 *
 * So they are centred instead. One offset, applied WHILE COMPOSING to every draw that fits
 * inside the game's own 794-wide screen, and subtracted again from the pointer so the game's
 * own hit tests and the ported menus still line up with what the player sees.
 *
 * A screen's own full-screen colour fill is the exception and is NOT centred: it is the
 * background, so it spans the composition and starts at the left edge, and the screen's art
 * is centred on top of it. That is why the front end fills a wide window instead of sitting
 * in the middle of one (issue #42).
 *
 * The offset used to be applied on the way out, to the single copy from the compose surface
 * to the primary. That copy then hung off the right and never wrote the primary's leftmost
 * `off` columns, which held whatever was there when the geometry last changed -- issue #29,
 * covered by a clear. Composing centred instead makes the copy 1:1, so every column of the
 * primary is written every frame and the ghost has nowhere to live. */

/* The widest composition this port will hand the game, and the width every resizable
 * surface's PITCH is fixed at. It is not a taste: vram_alloc is a bump allocator with no
 * free, so a surface reallocated on every resize event would exhaust the arena during one
 * drag of a window edge (issue #20). Allocating once at the maximum and moving only s->w
 * costs 4096*4*550 = 9 MB of a 1 GiB arena per resizable surface, and keeps the pitch
 * constant so anything the game cached from an earlier Lock stays valid.
 *
 * 4096 is the bound the port already validated widths against when this was an env var,
 * kept so the two do not disagree. A 32:9 monitor at full height asks for 1956; the rest of
 * the range is for a window someone has dragged very short and very wide.
 *
 * HIGH_MAX is the same bargain on the other axis and it is deliberately the game's own 550,
 * because that is every row the composition will ever have -- LF2's vertical screen axis
 * carries z and jump height, both fixed by stage data, so the height does NOT follow the
 * window (hostwin_window_geometry says why at length).
 *
 * It was briefly 2304, left over from trying a full-window composition, and that is worth
 * recording rather than quietly deleting: it made every resizable surface 37.7 MB instead of
 * 9 MB for rows nothing would ever draw into. Four times the committed guest memory per
 * instance, on a machine that then ran out of it. An allocation sized for a feature that was
 * measured and abandoned is exactly the kind of thing that survives a revert. */
enum { WIDE_MAX = 4096, HIGH_MAX = NATIVE_H };
enum { HUD_W = 792, HUD_BAND_H = 118 };   /* the in-match HUD strip and the band it owns */
/* THE TEXT BAND IS TALLER THAN THE BLIT BAND, and the two cannot be one number (issue #54).
 *
 * HUD_BAND_H stops at 118 because the WORLD's layer blits start at y 128 -- measured from a
 * widescreen match frame -- so a blit band any taller would take the stage's own layers with
 * the HUD and shift the world sideways.
 *
 * The game's own status line does not respect that boundary. In stage mode it draws
 * "Man: n  HP: n" and "STAGE 1-1" at y 110 and "Difficult" at y 115, so their bottoms are 126
 * and 131 -- below the blit band, above the world. Text is not a blit, and the same 128 does
 * not apply to it: what matters is whether any WORLD text lands in the strip, and none does.
 *
 * MEASURED rather than assumed, LF2_TEXT_DEBUG over a stage-mode run at 1920x1080: 104
 * distinct (row, text) draws, and between y 115 and y 219 there is NOTHING. 219 is character
 * selection, where panel_hud_up() is false and hud_offset_x declines anyway. So 131 captures
 * the status line exactly and reaches nothing else. */
enum { HUD_TEXT_BAND_H = 131 };

/* The in-match HUD's own centring offset, exposed because the GDI text path draws straight
 * into the surface and never goes through Blt -- so without this the game's text in the HUD
 * band stays at the left edge while the panels under it move. `bottom` is the destination's
 * lowest row, which is what decides whether a draw belongs to the HUD band. */
int hud_offset_x(int dst_w, int bottom)
{
    if (!lf2_wide_width() || !panel_hud_up()) return 0;
    if (dst_w <= NATIVE_W || bottom > HUD_TEXT_BAND_H) return 0;
    return (dst_w - HUD_W) / 2;
}

/* PER SCREEN since issue #44, and it stays ONE function on purpose: it has four consumers --
 * composing (below), the controls hint drawn onto the primary, the GDI text path which never
 * goes through Blt, and mouse_lparam's inverse mapping -- and if any of them got a different
 * answer the pointer would stop matching the picture. That failure is silent: a menu activates
 * the wrong entry, or none, and a screenshot looks perfect (issue #41's second bug). */
int screen_offset_x(void)
{
    const int wide = lf2_wide_width();
    if (!wide || panel_hud_up()) return 0;
    /* CENTRED for every screen, and that includes the two menus. Their BACKDROP ART is
     * left-anchored and is handled where it is drawn (see backdrop_left below) -- the menu
     * itself, its logo and its list, are centred like everything else. An earlier version
     * left-aligned the whole screen, which moved the menu with the picture behind it. */
    return geom_screen_offset_x(hw.width, GEOM_ALIGN_CENTRE);
}

/* Issue #29: after a resize, character selection kept a ghost of itself standing to its left.
 *
 * WHAT IS ACTUALLY WRONG, measured rather than guessed. The composition is copied to the
 * primary in ONE blit whose source is the WHOLE compose surface, and the centring offset is
 * added to that copy's destination:
 *
 *     blt 13  dst=(256,0)-(1562,550)  src=[1306x550]      into a 1306-wide primary
 *
 * So the copy hangs 256 px off the right and never writes the leftmost 256 px at all. The
 * comment above screen_offset_x() claimed "the band either side is the game's own full-screen
 * clear, which already covers the whole viewport" -- and that clear does exist, but it goes
 * to the COMPOSE surface (blt 1, dst 0..1306), and the shift is what moves it off the
 * primary's left band. The band is therefore never written by anything.
 *
 * At a steady size that is invisible: the primary starts black and the unwritten band stays
 * black. It only shows after a RESIZE, when the band still holds pixels drawn at the previous
 * size and offset -- which is exactly a ghost of the old, differently-centred panel.
 *
 * So the fix is not a per-frame clear of the primary; the band is stale only when the
 * geometry MOVES. Clear it when the offset or the surface size changes, which is the moment
 * the previously-written region stops matching the one about to be written. */
/* LF2_PRIMARY_STALE=1 -- a TEST-ONLY DEFECT INJECTOR: leave the leftmost columns of the
 * primary unwritten by the composition copy, so they keep whatever was there before.
 *
 * IT USED TO BE SOMETHING ELSE, and the change is worth recording. The copy to the primary
 * carried the centring offset, so it hung off the right and never wrote the first `off`
 * columns; after a resize those held a ghost of the previous, differently-centred screen
 * (issue #29), and primary_clear_on_move() blacked them out when the geometry moved. This
 * flag disabled that clear.
 *
 * Issue #42 moved the centring into the composition, so the copy is 1:1 and covers every
 * column of the primary. The ghost cannot happen any more -- not because something clears it
 * but because nothing is left unwritten -- and the clear, and the flag that disabled it, were
 * both dead. A flag whose negative arm can no longer fail is worse than no flag: it makes a
 * test that cannot fail look like a test that passes, which is what tools/routes/resize_test.sh said
 * when it went red rather than reporting a pass it could not justify.
 *
 * So the flag now injects the failure directly. The invariant it guards is the new one --
 * every pixel of the primary is written every frame -- and a run with it set must show the
 * previous size's pixels standing in that band. Never set in normal use. */
static int primary_stale_injected(void)
{
    static int on = -1;
    if (on < 0) on = getenv("LF2_PRIMARY_STALE") != NULL;
    return on;
}

static void surf_Blt(uint32_t self)
{
    LOADPROF_SCOPE(LP_BLT);
    Surface *d = com_host(self);
    if (draw_paths_on()) path_blt++;
    if (getenv("LF2_DUMP_SRC")) {
        static int done;
        const uint32_t want = (uint32_t)strtoul(getenv("LF2_DUMP_SRC"), NULL, 16);
        if (!done && ARG(2) == want) { dump_surface(want, "src"); done = 1; }
    }
    const uint32_t drect = ARG(1), srcobj = ARG(2), srect = ARG(3), flags = ARG(4);

    /* Value-level trace: the call sequence already matches the oracle, so the next
     * signal is the arguments. Flags are comparable across runs; pointers are not. */
    if (getenv("LF2_COM_TRACE"))
        fprintf(stderr, "ARG Blt flags=0x%x dst=%s src=%s\n", flags,
                drect ? "rect" : "null", srcobj ? "surf" : "null");

    int dl, dt, dr, db;
    read_rect(drect, &dl, &dt, &dr, &db, d->w, d->h);
    Surface *s = srcobj ? com_host(srcobj) : NULL;
    int sl = -1, st_ = -1, sr = -1, sb = -1;
    if (s) read_rect(srect, &sl, &st_, &sr, &sb, s->w, s->h);
    /* WHERE A FIXED-WIDTH SCREEN IS CENTRED, and it moved (issue #42).
     *
     * It used to be done on the way OUT: the game composes off-screen and copies that surface
     * to the primary in one blit, so shifting only that copy centred the whole composition at
     * a stroke. That is why it was done there -- and it is why the front end's backdrop could
     * not be made to fill a wide window. The backdrop is a fill of the game's whole 794-wide
     * screen; widening it to the composition and then shifting the composition right by the
     * centring offset put the blue at `off..width` and left the first `off` columns of the
     * primary written by nobody. Black down the left, picture against the right.
     *
     * So the centring is applied while COMPOSING, to the draws that belong to the game's own
     * 794-wide screen, and the copy to the primary is left alone. The rule is one line and it
     * says what it means: a draw that fits inside the game's screen is centred; a draw that
     * already spans the composition is the background, and stays where it is.
     *
     * The old comment warned that offsetting during composition "moved everything twice -- a
     * 132 px margin came out at 264". That was true while BOTH were done; the copy no longer
     * shifts, so there is one offset and it is applied once.
     *
     * Not applied here, but AFTER panel_note below: the screen detector recognises screens by
     * their destination rectangles, in the game's own coordinates, and it is what decides
     * screen_offset_x() in the first place. Shifting before it would feed the offset back into
     * the thing that computes it.
     *
     * ASKED FOR at each of the two places it is applied, and NOT computed once at the top of
     * the function, because issue #44 made the answer depend on WHICH SCREEN is up and the
     * thing that says which screen is up is this very draw. The loading screen is the case
     * that proves it: it is two frames long, and a value read before its backdrop was
     * recognised placed it with the alignment of the front end that preceded it. */

    const long trace_frame = hostwin_frames() + 1;
    const BltTrace trace = {
        .frame = trace_frame, .selected = hostwin_frame_selected(getenv("LF2_BLT_FRAME"), trace_frame),
        .destination = self, .destination_w = d->w, .destination_h = d->h,
        .destination_primary = d->primary,
        .dl = dl, .dt = dt, .dr = dr, .db = db,
        .source = srcobj, .source_w = s ? s->w : -1, .source_h = s ? s->h : -1,
        .has_source_rect = srect != 0, .sl = sl, .st = st_, .sr = sr, .sb = sb,
        .flags = flags, .caller = LD32(R(ESP)), .has_fill = flags & DDBLT_COLORFILL,
        .fill = (flags & DDBLT_COLORFILL) && ARG(5) ? LD32(ARG(5) + DDBLTFX_FILLCOLOR) : 0,
    };
    blt_trace_log(&trace);

    if (flags & DDBLT_COLORFILL) {
        const uint32_t fill = ARG(5) ? LD32(ARG(5) + DDBLTFX_FILLCOLOR) : 0;
        /* Widescreen: a STAGE's full-width band -- the sky, the ground, the road -- has to
         * span the whole viewport, because the game gives its width as an immediate rather
         * than from a viewport variable and so it does not follow the layers. Without it the
         * ground stopped at 794 and the rest of the stage floor was black.
         *
         * WHICH FILLS THOSE ARE comes from the background pass saying so (world_band_hint,
         * set in runtime/overrides/background.c), NOT from the rectangle. It used to be
         * `dl == 0 && dr == NATIVE_W`, and that is the front end's backdrop to the pixel --
         * the front end fills its whole 794-wide screen, so a wide window got the menu's blue
         * stretched across the entire composition and the centring shift then moved it right,
         * leaving black down the left (issue #42). The sibling stretch for full-width backdrop
         * BLITS below already knew this trap and gated itself; this one did not. */
        /* The `dl == 0 && dr == NATIVE_W` half is KEPT, and narrowed rather than replaced: it
         * is the game saying this band covers the screen. A tinted layer whose authored span
         * is narrower than the screen covers only what it covers, and stretching that would be
         * inventing layout the stage does not have -- the same answer issue #23 gives for a
         * non-looping layer with no more picture. What the hint adds is that the fill came
         * from the stage at all. */
        if (world_band_hint) world_band_fills++;
        /* Before anything rewrites the rectangle: a screen paints itself in the game's own
         * coordinates, and the widening below turns dr into the composition width. */
        if (!world_band_hint) screen_fill_note(fill, dl, dt, dr, db);
        const int compose_off = (!d->primary && d->w > NATIVE_W) ? screen_offset_x() : 0;
        int spans_screen = (dl == 0 && dr == NATIVE_W && d->w > NATIVE_W && lf2_wide_width());
        if (world_band_hint && spans_screen) {
            dr = d->w;
            world_band_widened++;
        } else if (spans_screen) {
            /* THE SCREEN'S OWN BACKDROP, and this is what the reporter asked for: a fill of
             * the game's whole screen is the BACKGROUND, so it covers the composition and
             * starts at the left edge rather than moving with the content. Nothing is invented
             * -- it is the same flat colour the game chose, over more of the window. The art
             * on top of it (the logo, the character, the menu) is a set of smaller draws and
             * IS placed, below.
             *
             * THE GATE USED TO BE `compose_off`, i.e. the offset being non-zero, and issue #44
             * is what makes that wrong: a left-aligned screen HAS no offset, so widening its
             * backdrop would have stopped the moment the screen moved to the left edge, and
             * the front end would have gone back to 794 columns of blue with black beside it --
             * issue #42's symptom, reintroduced by the fix for #44. What the test means is
             * "this is a fixed-794 screen rather than the world view", so that is what it now
             * says.
             *
             * AND THEN IT HAD `!panel_hud_up()` ADDED, and the stage-mode swipe is why that is
             * gone again (issue #73). A stage-intro transition is the same "full screen, flat
             * colour" shape as a menu backdrop -- FUN_00437860 wipes the screen with 794-wide
             * horizontal bands -- and it runs DURING the match, where the gate above declined.
             * A wide view left the wipe 184 px short of the composition edge at 978, showing
             * the stage it was supposed to cover. On the frames the gate was written for the
             * gate never mattered: compose_off is zero during a match, and the stage's own
             * full-width bands (the ground, the sky) are already widened above by
             * world_band_hint, so the only fills this newly reaches are the swipe's. The full
             * clear arrives already spanning the surface (the game clears by surface width),
             * so it does not even match `dr == NATIVE_W` here. At 794 none of it applies: the
             * rule requires `d->w > NATIVE_W` up front. */
            dr = d->w;
        } else if (compose_off && dl >= 0 && dr <= NATIVE_W) {
            dl += compose_off; dr += compose_off;
        }
        if (!d->primary) render_fill(d->pixels, dl, dt, dr, db, fill);
        for (int y = dt; y < db && y < d->h; y++) {
            if (y < 0) continue;
            uint32_t *row = (uint32_t *)(g_mem + d->pixels + (size_t)y * (size_t)d->pitch);
            for (int x = dl < 0 ? 0 : dl; x < dr && x < d->w; x++) row[x] = fill & 0x00ffffffu;
        }
        surface_changed(d);
        if (d->primary) present_primary();
        _lp_slot = LP_FILL;
        LOADPROF_END();
        com_ret(6, DD_OK);
        return;
    }

    if (getenv("LF2_FIND_BLT")) {
        static int done;
        const char *want = getenv("LF2_FIND_BLT");
        int wx = 0, wy = 0;
        sscanf(want, "%d,%d", &wx, &wy);
        if (!done && dl == wx && dt == wy) {
            fprintf(stderr, "blt (%d,%d)-(%d,%d) issued from guest %08x\n",
                    dl, dt, dr, db, LD32(R(ESP)));
            done = 1;
        }
    }
    if (getenv("LF2_BLT_RECTS")) {
        /* Capped by NOVELTY, not by count. The cap used to be the first 4000 blits, which
         * is a few seconds of the menu -- so a search for something drawn during a match
         * found nothing and read as "that is never drawn". Distinct rectangles are what the
         * hook is for, and there are only a few hundred of them across a whole run. */
        enum { MAX_RECTS = 4096 };
        static struct { int l, t, r, b; } seen[MAX_RECTS];
        static int nseen;
        static long dropped;

        int known = 0;
        for (int i = 0; i < nseen; i++)
            if (seen[i].l == dl && seen[i].t == dt && seen[i].r == dr && seen[i].b == db) {
                known = 1;
                break;
            }
        if (!known) {
            if (nseen < MAX_RECTS) {
                seen[nseen].l = dl; seen[nseen].t = dt;
                seen[nseen].r = dr; seen[nseen].b = db;
                nseen++;
                fprintf(stderr, "RECT %d %d %d %d\n", dl, dt, dr, db);
            } else if (++dropped == 1) {
                fprintf(stderr, "RECT: more than %d distinct rectangles; the rest are NOT "
                                "logged\n", (int)MAX_RECTS);
            }
        }
    }
    /* Widescreen: the in-match HUD is a fixed 792-wide strip -- eight player slots as two
     * rows of four 198x54 panels -- and nothing about it is width-driven, so on a wider
     * viewport it sat against the left edge. It is centred, like every other fixed-width
     * piece of the game. (It was briefly tiled out to the edge instead, which filled the
     * space but invented four more empty slots that the game does not have.)
     *
     * The band is the top 118 px, which during a match is HUD only: the world viewport
     * starts at y 128, measured from the layer blits in a widescreen match frame. */
    if (lf2_wide_width() && panel_hud_up() && d->w > NATIVE_W && db <= HUD_BAND_H) {
        const int off = (d->w - HUD_W) / 2;
        dl += off; dr += off;
    }

    /* Centre the fixed-width STAGE n-n announcement (issues #73, #90). Sheet geometry is
     * insufficient identity: selected Demo shares the 794x600 source and y=339. The helper
     * therefore owns the running-match signal too. */
    {
        const Surface *banner = srcobj ? com_host(srcobj) : NULL;
        if (banner && !d->primary) {
            const int match_up = panel_hud_up() && !panel_overlay_up();
            const int off = stage_banner_offset(match_up, d->w, banner->w, banner->h, dt);
            dl += off; dr += off;
        }
    }

    if (getenv("LF2_BAND_DEBUG") && lf2_wide_width() && panel_hud_up() && srcobj && dl == 0)
        fprintf(stderr, "band: dl %d dr %d dt %d db %d dest %d wide (NATIVE_W %d)\n",
                dl, dr, dt, db, d->w, NATIVE_W);
    const int backdrop_flags = lf2_wide_width() && panel_hud_up() && s && d->w > NATIVE_W
                             ? world_backdrop_hint : 0;
    const int extend_world_backdrop = (backdrop_flags & BACKDROP_EXTEND_BOTTOM) != 0;

    /* The Summary screen is a fixed-width panel drawn over a world that remains wide. Its
     * first bitmap identifies the branch in fn_0041bc90; only draws inside that panel take
     * the fixed-screen offset, while the stage and the HUD keep their world placement. */
    if (s) {
        const int off = result_panel_blit_offset(frames, d->w, dl, dt, dr, db,
                                                 s->w, s->h, sl, st_, sr, sb);
        dl += off;
        dr += off;
    }

    const int overlay_panel_draw = dl == 3 && dt == 3 && dr == 307 && db == 159;
    panel_note(dl, dt, dr, db);

    /* Record the label before the pre-fight panel so ordinary painter order covers it. */
    if (overlay_panel_draw && !d->primary
        && device_icon_charselect_phase(LD32(0x0044d020u) == 1, 1)
               == DEVICE_ICON_CHARSELECT_BEFORE_OVERLAY)
        charselect_device_labels_draw(d);

    /* A PICTURE THAT COVERS THE WHOLE OF THE GAME'S SCREEN IS THAT SCREEN'S BACKGROUND, and
     * the only one in the game is the loading screen's MENU_WAIT (issue #44). It is decided
     * here, on the game's own coordinates, for the same reason panel_note is: after the shift
     * below there is no whole-screen rectangle left to recognise. The bands themselves are
     * painted after the picture lands -- see backdrop_picture_bands, which says at length that
     * what goes in them is a declared choice and not the game's.
     *
     * Gated off the world view: during a match the rule above has already widened a full-width
     * stage layer, and a stage layer is not a screen's backdrop. */
    const int backdrop_picture = srcobj && !d->primary && lf2_wide_width() && !panel_hud_up()
                              && d->w > NATIVE_W
                              && dl == 0 && dt == 0 && dr == NATIVE_W && db == NATIVE_H;
    /* A screen whose backdrop is a picture is one of the CENTRED ones, and it says so here
     * rather than by not saying anything: the alignment is whatever the last screen to draw a
     * backdrop asked for, so a screen that stayed silent would keep the previous screen's. */
    if (backdrop_picture) screen_align_left = 0;

    /* Placed now that the screen detector has seen the game's own coordinates -- see the long
     * comment at the top of Blt. A draw that fits inside the game's 794-wide screen belongs
     * to that screen and moves with it; one that already spans the composition is background
     * and does not. */
    const int compose_off = (!d->primary && d->w > NATIVE_W) ? screen_offset_x() : 0;

    /* THE BACKDROP ART KEEPS THE EDGE IT WAS DRAWN AGAINST (issue #44).
     *
     * On the front end and the mode menu the game draws its character portrait -- the same
     * sprite on both, MENU_BACK<n> -- at a hard literal x = 0, and it is 546 of the screen's
     * 550 rows tall. It is composed against the LEFT EDGE and bleeds off it. Centring it puts
     * that edge in the middle of a wide window, which is what the picture is not supposed to
     * do; so this one draw keeps x = 0 while the menu in front of it is centred.
     *
     * Identified by both halves of what the RE found, not by one: the literal x = 0 AND the
     * near-full height. A menu strip also starts at x 0 sometimes, and it is short -- taking
     * only the x would drag those to the edge as well and pull the list apart. Anything that
     * fails either half is ordinary screen content and is centred. */
    const int backdrop_art = screen_backdrop_left() && srcobj && dl == 0
                          && (db - dt) >= NATIVE_H - 60;
    if (backdrop_art) backdrop_art_seen++;
    if (compose_off && !backdrop_art && dl >= 0 && dr <= NATIVE_W) {
        dl += compose_off; dr += compose_off;
    }

    cursor_find_note(dl, dt, "Blt");
    /* LF2_SMALL_BLT=1 -- a cursor is a SMALL sprite, so list the small destinations
     * distinctly. The correlation hook asks "does this track the pointer" and answers no
     * for every site; this asks the simpler question of what tiny things get drawn at all,
     * which is what a cursor would be hiding among. Distinct rects only, so a per-frame
     * redraw does not bury the list. */
    if (getenv("LF2_SMALL_BLT")) {
        const int w = dr - dl, h = db - dt;
        if (w > 0 && h > 0 && w <= 40 && h <= 40) {
            enum { MAXS = 64 };
            static struct { int l, t, w, h; uint32_t ra; } seen[MAXS];
            static int n;
            int known = 0;
            for (int i = 0; i < n; i++)
                if (seen[i].l == dl && seen[i].t == dt && seen[i].w == w && seen[i].h == h) { known = 1; break; }
            if (!known && n < MAXS) {
                seen[n].l = dl; seen[n].t = dt; seen[n].w = w; seen[n].h = h;
                seen[n].ra = LD32(R(ESP));
                fprintf(stderr, "small blt (%d,%d) %dx%d from guest %08x\n",
                        dl, dt, w, h, seen[n].ra);
                n++;
            }
        }
    }
    if (getenv("LF2_BLT_STACK")) {
        int wx = 0, wy = 0;
        sscanf(getenv("LF2_BLT_STACK"), "%d,%d", &wx, &wy);
        /* The match is on the exact top-left corner, so a coordinate that is one pixel out
         * finds nothing -- and printing nothing is indistinguishable from "that rectangle
         * is never drawn". Track the nearest destination seen so the miss can say what it
         * did see; blt_stack_report() prints it at exit. */
        blt_stack_wanted = 1;
        const int d = abs(dl - wx) + abs(dt - wy);
        if (d < blt_stack_best) {
            blt_stack_best = d;
            blt_stack_best_l = dl; blt_stack_best_t = dt;
        }
        static int shown;
        if (!shown && dl == wx && dt == wy) {
            blt_stack_hit = 1;
            shown = 1;
            /* Poor man's backtrace: scan the guest stack for values that look like code
             * addresses in .text, which are the return addresses of the frames above. */
            fprintf(stderr, "blt (%d,%d) guest call chain:", dl, dt);
            uint32_t sp = R(ESP);
            for (int k = 0; k < 400; k++) {
                const uint32_t v = LD32(sp + (uint32_t)k * 4);
                if (v >= 0x401000u && v < 0x44e000u) fprintf(stderr, " %08x", v);
            }
            fprintf(stderr, "\n");
        }
    }
    if (s) {
        /* LF2_CK_FORCE is a discriminator, not a fix: if honouring the key on every blit
         * that has one makes the sprites transparent, they arrive through Blt and the
         * question is about the flags; if nothing changes, they are composited elsewhere
         * (Lock) and the Blt path is innocent. Run against both classes before believing
         * either. */
        shadow_learn(s);
        const int keyed = ((flags & DDBLT_KEYSRC) || getenv("LF2_CK_FORCE")) && s->has_key;
        if (keyed) ck_blt_keyed++; else ck_blt_plain++;
        if (getenv("LF2_CK_DEBUG")) {
            /* Keyed by (flags, caller): the caller is the part that leads anywhere, since
             * it names the guest code that decides whether to ask for the key. */
            static long seen[16]; static uint32_t fv[16], cv[16]; static int nf;
            const uint32_t caller = LD32(R(ESP));
            int hit = -1;
            for (int i = 0; i < nf; i++) if (fv[i] == flags && cv[i] == caller) hit = i;
            if (hit < 0 && nf < 16) { fv[nf] = flags; cv[nf] = caller; hit = nf++; }
            if (hit >= 0 && seen[hit]++ == 0)
                fprintf(stderr, "Blt flags=%08x has_key=%d from guest %08x\n",
                        flags, s->has_key, caller);
        }
        if (glyph_hint >= 0
            && game_glyph_draw(glyph_hint, dl, dt, glyph_ink(s, sl, st_, sr, sb),
                               d->pixels, d->w, d->h, d->pitch)) {
            glyphs_drawn++;
        } else {
            /* Recorded for the native renderer and ALSO composed in software. Both paths
             * build every frame; which one is presented is decided at the copy-to-primary.
             * Running them together is what makes tools/routes/render_test.sh able to diff them. */
            if (!d->primary) {
                /* The stage's own shadow becomes a GROUND MARKER for the renderer rather
                 * than a picture: it says where the object standing here meets the floor,
                 * which is the one thing a cast shadow needs and the sprite cannot give.
                 * With the effect off it is recorded as the picture it is, so the GPU frame
                 * stays comparable with the software one. */
                if (getenv("LF2_SHADOW_DEBUG")) {
                    static long hits, miss;
                    if (shadow_hint) hits++; else miss++;
                    if ((hits + miss) % 4000 == 0)
                        fprintf(stderr, "shadow: hint set on %ld of %ld sprite blits "
                                        "(learned obj %08x, shadows enabled %d)\n",
                                hits, hits + miss, shadow_object(),
                                render_shadows_enabled());
                }
                if (shadow_hint && render_shadows_enabled())
                    render_shadow_ground(d->pixels, dl, dt, dr, db);
                else
                    render_blit(d->pixels, dl, dt, dr, db,
                                s->pixels, s->w, s->h, s->pitch, sl, st_, sr, sb,
                                keyed, s->key_lo, s->key_hi);
            }
            /* The injector, and it is deliberately applied to the SOFTWARE copy only --
             * see primary_stale_injected(). Advancing the source with the destination keeps
             * the picture aligned, so the band is left holding the previous frame's pixels
             * rather than a shifted copy of this one. */
            if (d->primary && primary_stale_injected()) {
                /* Exactly the old defect, not an approximation of it: skip the columns the
                 * centred copy used to miss, which is the centring offset itself. Skipping an
                 * arbitrary 64 was not enough -- the leftmost columns are black in every frame
                 * at any one size, so the injected run and the clean run agreed and the arm
                 * still could not fail. The ghost only appears where a DIFFERENTLY centred
                 * screen used to have picture, and that is this many columns wide. */
                const int skip = (d->w - NATIVE_W) / 2;
                if (skip > 0 && dr - dl > skip) { dl += skip; sl += skip; }
            }
            blit(d, dl, dt, dr - dl, db - dt, s, sl, st_, sr - sl, sb - st_,
                 keyed, s->key_lo, s->key_hi);
            if (backdrop_flags & (BACKDROP_MIRROR_LEFT | BACKDROP_MIRROR_RIGHT))
                backdrop_mirror_segments(d, s, backdrop_flags, sl, st_, sr, sb,
                                         dl, dt, dr, db, keyed, &trace);
            BackdropBlit ext;
            const int backdrop_bottom = d->h < GEOM_WORLD_BOTTOM ? d->h : GEOM_WORLD_BOTTOM;
            for (int row = db; backdrop_bottom_row(extend_world_backdrop, backdrop_bottom, row,
                                                   dl, dt, dr, db, sl, st_, sr, sb, &ext); row++) {
                blt_trace_backdrop(&trace, &ext);
                if (!d->primary)
                    render_blit(d->pixels, ext.dl, ext.dt, ext.dr, ext.db,
                                s->pixels, s->w, s->h, s->pitch,
                                ext.sl, ext.st, ext.sr, ext.sb,
                                keyed, s->key_lo, s->key_hi);
                blit(d, ext.dl, ext.dt, ext.dr - ext.dl, ext.db - ext.dt,
                     s, ext.sl, ext.st, ext.sr - ext.sl, ext.sb - ext.st,
                     keyed, s->key_lo, s->key_hi);
                backdrop_mirror_segments(d, s, backdrop_flags, ext.sl, ext.st, ext.sr, ext.sb,
                                         ext.dl, ext.dt, ext.dr, ext.db, keyed, &trace);
            }
            if (backdrop_picture) {
                backdrop_edge_bands(d, s, sl, st_, sr, sb, dl, dt, dr, db);
                framing_n_picture++;
                framing_note("screen with a PICTURE backdrop, side bands extended from its "
                             "edge columns (a declared port choice, not the game's)",
                             0x80000000u, dl, 0);
            }
        }
        if (overlay_panel_apply(d->pixels, d->w, d->h, d->pitch, dl, dt, lf2_world_scale())) surface_changed(d);
    }
    /* There used to be a widescreen "finish a tiling series the game stopped at 794" hook
     * here: it recognised a run of edge-to-edge copies by CONTIGUITY and kept repeating the
     * last one to the edge of the surface. It is gone, and the reason is worth keeping.
     *
     * It could not tell a layer that repeats from a layer that does not, because by the time
     * a layer reaches Blt the two are the same picture drawn edge to edge. So on Brokeback
     * Clif -- whose cliffs are three DIFFERENT bitmaps abutting, with no repeat at all -- it
     * repeated the middle one across the widened band with a hard seam at each end, inventing
     * stage the game never draws. It also took its period from the last copy's DESTINATION
     * width, which is short whenever that copy was clipped.
     *
     * The layer itself carries the answer (bg.dat's `loop:`), and the pass that draws it is
     * now runtime/overrides/background.c. Continuing a run is decided there, from the data,
     * before the blit exists. This comment is the whole of what is left. */

    /* THE FRAME BOUNDARY. The game composes off-screen and copies that surface to the
     * primary in one blit, so this call names the composition -- which is how the renderer
     * finds the right display list without anyone hardcoding a surface address. The software
     * copy still happens; which path is PRESENTED is decided in hostwin_present, so the
     * primary always holds the software frame and a frame dump can be taken of either. */
    if (d->primary && srcobj) {
        Surface *cs = com_host(srcobj);
        /* Zero, not screen_offset_x(): the centring now happens while composing (see
             * compose_off above), so the display list the renderer replays already carries it
             * and adding it again at present time would centre twice. */
            if (cs) frame_source_note(cs->pixels, 0);
    }

    if (d->primary) {
        if (getenv("LF2_BLT_DEBUG")) {
            static long n;
            fprintf(stderr, "blt->primary #%ld drect=%08x [%d %d %d %d] src=%08x srect=%08x flags=%08x\n",
                    ++n, drect, dl, dt, dr, db, srcobj, srect, flags);
        }
        present_primary();
    }
    LOADPROF_END();
    com_ret(6, DD_OK);
}

static void surf_BltFast(uint32_t self)
{
    Surface *d = com_host(self);
    if (draw_paths_on()) path_bltfast++;
    const int dx = (int)ARG(1), dy = (int)ARG(2);
    Surface *s = ARG(3) ? com_host(ARG(3)) : NULL;
    const uint32_t srect = ARG(4), flags = ARG(5);
    if (s) {
        int sl, st_, sr, sb;
        read_rect(srect, &sl, &st_, &sr, &sb, s->w, s->h);
        if ((flags & 1) && s->has_key) ck_blt_keyed++; else ck_blt_plain++;
        if (getenv("LF2_CK_DEBUG")) {
            static long seen[8]; static uint32_t fv[8]; static int nf;
            int hit = -1;
            for (int i = 0; i < nf; i++) if (fv[i] == flags) hit = i;
            if (hit < 0 && nf < 8) { fv[nf] = flags; hit = nf++; }
            if (hit >= 0 && seen[hit]++ == 0)
                fprintf(stderr, "BltFast flags=%08x (has_key=%d)\n", flags, s->has_key);
        }
        cursor_find_note(dx, dy, "BltFast");
        blit(d, dx, dy, sr - sl, sb - st_, s, sl, st_, sr - sl, sb - st_,
             (flags & 1) && s->has_key, s->key_lo, s->key_hi);
    }
    if (d->primary) present_primary();
    com_ret(6, DD_OK);
}

long ck_set, ck_blt_keyed, ck_blt_plain;

/* Reports both halves and both zeros: "no keyed blits" and "no colour key was ever set"
 * are different faults with the same symptom, and a counter that only prints when it
 * fires cannot tell them apart. */
void colorkey_report(void)
{
    if (!getenv("LF2_CK_DEBUG")) return;
    fprintf(stderr, "colour-key: SetColorKey=%ld keyed blits=%ld unkeyed blits=%ld\n",
            ck_set, ck_blt_keyed, ck_blt_plain);
}

static void surf_SetColorKey(uint32_t self)
{
    Surface *s = com_host(self);
    const uint32_t key = ARG(2);
    ck_set++;
    if (getenv("LF2_CK_DEBUG") && ck_set <= 4)
        fprintf(stderr, "SetColorKey #%ld flags=%08x key=%08x range=%08x..%08x\n",
                ck_set, ARG(1), key, key ? LD32(key) : 0, key ? LD32(key + 4) : 0);
    if (key) { s->has_key = 1; s->key_lo = LD32(key); s->key_hi = LD32(key + 4); }
    else s->has_key = 0;
    com_ret(3, DD_OK);
}

static void surf_SetPalette(uint32_t self)
{
    Surface *s = com_host(self);
    s->palette = ARG(1);
    if (s->primary) active_palette = s->palette;
    com_ret(2, DD_OK);
}

static void surf_Flip(uint32_t self)
{
    (void)self;
    present_primary();
    com_ret(3, DD_OK);
}

/* GetDC hands back the surface itself as a device context: the GDI layer only ever
 * draws text into it, and it needs the same pixels the game blits to. */
static void surf_GetDC(uint32_t self)
{
    ST32(ARG(1), self);
    com_ret(2, DD_OK);
}
static void surf_ReleaseDC(uint32_t self)
{
    Surface *s = com_host(self);
    if (s->primary) present_primary();
    com_ret(2, DD_OK);
}

/* Methods with out-parameters MUST write them. Returning S_OK and leaving the caller's
 * pointer untouched hands the game uninitialised memory it then calls through -- the
 * failure surfaces much later as a call to a garbage address. */
static void surf_GetAttachedSurface(uint32_t self)
{
    Surface *s = com_host(self);
    if (!s->attached) {
        Surface *b = SDL_calloc(1, sizeof *b);
        b->w = s->w; b->h = s->h; b->pitch = s->pitch;
        b->pixels = vram_alloc((uint32_t)b->pitch * (uint32_t)b->h);
        memset(g_mem + b->pixels, 0, (size_t)b->pitch * (size_t)b->h);
        s->attached = com_create(IF_SURFACE, b);
    }
    if (ARG(2)) ST32(ARG(2), s->attached);
    com_ret(3, DD_OK);
}

static void surf_GetCaps(uint32_t self)
{
    Surface *s = com_host(self);
    if (ARG(1)) ST32(ARG(1), s->primary ? DDSCAPS_PRIMARYSURFACE : 0x40u /* OFFSCREENPLAIN */);
    com_ret(2, DD_OK);
}

/* Hand back whatever SetClipper attached. Reporting DDERR_NOCLIPPERATTACHED
 * unconditionally -- which this did -- means the game can never reach its clipper, so it
 * never calls GetClipList and takes a different drawing path than it does against real
 * DirectDraw. Measured against the Wine oracle: 8.4% of the oracle's DirectDraw calls are
 * GetClipList, and 0% of ours were. */
static void surf_GetClipper(uint32_t self)
{
    Surface *s = com_host(self);
    if (!s || !s->clipper) {
        if (ARG(1)) ST32(ARG(1), 0);
        com_ret(2, E_FAIL);        /* DDERR_NOCLIPPERATTACHED */
        return;
    }
    if (ARG(1)) ST32(ARG(1), s->clipper);
    com_ret(2, DD_OK);
}

static void surf_SetClipper(uint32_t self)
{
    Surface *s = com_host(self);
    if (s) s->clipper = ARG(1);
    com_ret(2, DD_OK);
}

static void surf_GetColorKey(uint32_t self)
{
    Surface *s = com_host(self);
    if (ARG(2)) { ST32(ARG(2), s->key_lo); ST32(ARG(2) + 4, s->key_hi); }
    com_ret(3, DD_OK);
}

static void surf_GetPalette(uint32_t self)
{
    Surface *s = com_host(self);
    if (ARG(1)) ST32(ARG(1), s->palette);
    com_ret(2, s->palette ? DD_OK : E_FAIL);
}

static void surf_ret_ok1(uint32_t self) { (void)self; com_ret(1, DD_OK); }

static void surf_GetPixelFormat(uint32_t self)
{
    (void)self;
    write_pixelformat(ARG(1));
    com_ret(2, DD_OK);
}

/* ---- IDirectDraw ---- */

/* `maxw` is the width the PITCH is fixed at, which is not always the width the surface
 * starts out being: a surface that follows the window is allocated at WIDE_MAX so a resize
 * can move s->w without reallocating. See surfaces_follow_window. */
static uint32_t make_surface(int w, int h, int primary, int maxw, int maxh)
{
    Surface *s = SDL_calloc(1, sizeof *s);
    if (maxw < w) maxw = w;
    if (maxh < h) maxh = h;
    s->w = w; s->h = h;
    s->pitch = maxw * 4;
    s->rows  = maxh;
    s->pixels = vram_alloc((uint32_t)s->pitch * (uint32_t)maxh);
    s->primary = primary;
    memset(g_mem + s->pixels, 0, (size_t)s->pitch * (size_t)maxh);
    return com_create(IF_SURFACE, s);
}

/* The surfaces whose width follows the window: the primary, and every surface the game
 * asked for at exactly the 794x550 it composes the world into. They are held so a resize
 * can move them; the game keeps its own pointers and never learns anything changed except
 * through Lock, which reports w/h/pitch fresh on every call.
 *
 * A fixed array with a loud refusal rather than a growing one: the game creates a handful of
 * these once at startup, so a run that reaches the cap means the assumption is wrong, and
 * that is worth a line on stderr rather than a silent realloc. */
enum { FOLLOW_MAX = 8 };
static uint32_t follow[FOLLOW_MAX];
static int follow_n;

static void follow_add(uint32_t obj)
{
    if (follow_n < FOLLOW_MAX) { follow[follow_n++] = obj; return; }
    fprintf(stderr, "widescreen: more than %d window-following surfaces exist; %08x will "
                    "NOT follow a resize and will keep the width it was created at\n",
            FOLLOW_MAX, obj);
}

static void surfaces_follow_window(int w, int h)
{
    for (int i = 0; i < follow_n; i++) {
        Surface *s = com_host(follow[i]);
        if (!s) continue;
        /* Never past what it was allocated with -- a surface created before the caps were
         * known (there are none today) would otherwise write off the end of its buffer. */
        s->w = w * 4 <= s->pitch ? w : s->pitch / 4;
        s->h = (s->rows && h > s->rows) ? s->rows : h;
    }
}

/* The window changed size. THE COMPOSITION IS HOW MUCH WORLD IS ON SCREEN; how BIG that world
 * is drawn is a separate number, and the two together are what make the picture fill the
 * window (issue #41). geom_world_scale / geom_compose_width hold the rule and state it.
 *
 * THE HEIGHT SETS THE SCALE. LF2's vertical screen axis carries the depth (z, bounded by
 * bg.dat's zboundary) and the jump height, both fixed by the stage's own data, and every
 * background layer's picture is 550 rows tall. Composing 1080 rows was tried and does what
 * that sentence predicts: the game draws its world in the top 550 and the remaining 530 are
 * black, because there is nothing behind them to draw. So the rows that EXIST are scaled to
 * the rows the window HAS.
 *
 * THE WIDTH IS SPENT ON FIELD OF VIEW. Whatever width is left after the scale becomes world:
 * the game's own width words are patched to the composition (menu.c wide_apply), so the
 * camera and the layer loops draw MORE WORLD rather than a stretched picture. At the game's
 * own 794x550 the scale is exactly 1 and the composition is exactly 794, so nothing about the
 * 4:3 game moves -- which is what every byte-identity arm in the suite depends on.
 *
 * THIS IS NOT THE UPSCALE THE PORT REMOVED, and the difference is the whole of issue #41.
 * That one composed 978x550 into a small buffer and let SDL blow the finished frame up by
 * 1.96, so a game pixel became a block two OR three screen pixels wide and text and lighting
 * were quantised to the small grid before being enlarged. The scale here is applied PER QUAD
 * by the native renderer as the display list is drawn into a full-resolution target: the
 * geometry is exact at float precision and only a sprite's own texels are magnified. Between
 * those two the composition width happens to come out the same at 16:9; nothing else does.
 *
 * A window proportionally NARROWER than the game floors the composition at 794 and the width
 * binds the scale instead, leaving black rows: below 794 the HUD strip does not fit, and
 * squashing the world is the one thing worse than scaling it. */
void hostwin_window_geometry(int win_w, int win_h)
{
    if (win_w <= 0 || win_h <= 0) return;
    hw.win_w = win_w;
    hw.win_h = win_h;

    /* The rule itself is geom_compose_width, which tests/test_geom.c walks; what stays here
     * is the part that needs the window -- the diagnostic for a window past what this build
     * allocated pitch for, which the arithmetic has no way to say. */
    const long w = geom_compose_width(win_w, win_h, WIDE_MAX);
    if (w >= WIDE_MAX && win_w > WIDE_MAX) {
        static int said;
        if (!said) {
            said = 1;
            fprintf(stderr, "widescreen: a %dx%d window asks for a %d-wide composition, past "
                            "the %d this build allocates for; clamped, so the picture is "
                            "cropped at the sides from here on\n", win_w, win_h, win_w, WIDE_MAX);
        }
    }

    if ((int)w == hw.width && hw.height == NATIVE_H) return;
    /* Both numbers, and the rectangle they land in, because either alone reads as the whole
     * answer and is not: the composition says how much WORLD is on screen and the scale says
     * how big it is drawn. The rectangle is what says whether the picture fills the window --
     * a claim no width on its own can make, and the one issue #41 is about. */
    {
        SDL_FRect r;
        hw.width = (int)w;                       /* set first: lf2_compose_rect reads the window */
        hw.height = NATIVE_H;
        lf2_compose_rect((int)w, NATIVE_H, &r);
        fprintf(stderr, "widescreen: window %dx%d -> composition %ldx%d at scale %.3f, drawn "
                        "into %.0fx%.0f at (%.0f,%.0f)%s\n",
                win_w, win_h, w, NATIVE_H, (double)lf2_world_scale(),
                (double)r.w, (double)r.h, (double)r.x, (double)r.y,
                (r.h >= (float)win_h - 1.0f && r.w >= (float)win_w - 1.0f)
                    ? " -- fills the window" : " -- with a band");
    }

    surfaces_follow_window(hw.width, hw.height);
    /* Logical presentation is what used to do the scaling, and there is none to do now. The
     * renderer places the composition in the window itself, because it is the only thing that
     * knows both sizes and it has to place the lighting's targets the same way. */
    if (hw.renderer)
        SDL_SetRenderLogicalPresentation(hw.renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    /* The texture is sized to the composition, so it has to go with it. Recreated on the
     * next present rather than here, where the renderer may not exist yet. */
    if (hw.texture) { SDL_DestroyTexture(hw.texture); hw.texture = NULL; }
}

/* WHERE THE COMPOSITION SITS IN THE WINDOW, and how big it is drawn (issue #41).
 *
 * Both present paths need this and neither can work it out alone -- the software compositor
 * knows the composition and the renderer knows the output -- so it is computed once here off
 * the window geometry this file already owns, and the rule itself is geom_compose_rect, which
 * `ctest geometry` walks.
 *
 * The two paths use the same rectangle but do NOT get the same picture out of it, and that is
 * the point of issue #41. The renderer applies the scale PER QUAD as it draws, so the geometry
 * is exact and only a sprite's own texels are magnified. The software compositor has already
 * flattened everything into one buffer by the time it gets here and can only stretch that
 * buffer -- it is the fallback, and it looks like one.
 */
void lf2_compose_rect(int comp_w, int comp_h, SDL_FRect *r)
{
    geom_compose_rect(hw.win_w, hw.win_h, comp_w, comp_h, &r->x, &r->y, &r->w, &r->h);
}

/* How many screen pixels a game pixel becomes. The renderer needs it on its own to scale the
 * quads, and hd2d needs it to put the stage's floor in the rows it is actually drawn in. */
float lf2_world_scale(void)
{
    return geom_world_scale(hw.win_w, hw.win_h);
}

/* A POINT ON THE WINDOW, IN THE GAME'S OWN PIXELS -- the inverse of the rectangle above, and
 * the thing every hit test needs.
 *
 * It has to be the exact inverse or the pointer and the picture disagree, and the failure is
 * silent: a menu simply activates the wrong entry, or nothing, and a screenshot looks fine.
 * The old code subtracted a horizontal centring offset and nothing else, which was right only
 * while the composition was drawn 1:1 at the top of the window. It was already wrong
 * VERTICALLY in a tall window -- the picture was centred with 265 rows above it at 1080 and
 * the pointer's y was never moved to match -- and a scale would have made it wrong in both
 * axes at once (issue #41).
 *
 * SDL_RenderCoordinatesFromWindow cannot do this job: it undoes SDL's own logical
 * presentation, which this port turns OFF precisely because it does its placement itself. */
void lf2_window_to_compose(float wx, float wy, float *cx, float *cy)
{
    geom_window_to_compose(hw.win_w, hw.win_h, hw.width, hw.height, wx, wy, cx, cy);
}

/* The same mapping for a pointer, which SDL delivers in POINTS while hw.win_w/h are PIXELS
 * (issue #56). The density enters the geometry here and nowhere else, and it is in geom.h so
 * tests/test_geom.c can walk it across densities without a scaled display to run on. */
void lf2_pointer_to_compose(float px, float py, float density, float *cx, float *cy)
{
    geom_pointer_to_compose(hw.win_w, hw.win_h, hw.width, hw.height, density, px, py, cx, cy);
}

/* Widescreen is ON whenever the window is wider in aspect than the game's own picture, and
 * OFF when it is not. There is no switch: an env var read once at startup was a developer's
 * escape hatch rather than a feature (issue #20). The guest half -- the game's own viewport
 * width words -- is wide_apply() in runtime/overrides/menu.c, and it reads this, so there is
 * one source of truth and not two that can disagree. */
int lf2_wide_width(void)
{
    return hw.width > NATIVE_W ? hw.width : 0;
}

static void dd_CreateSurface(uint32_t self)
{
    (void)self;
    const uint32_t desc = ARG(1), out = ARG(2);
    const uint32_t flags = LD32(desc + SD_FLAGS);
    const uint32_t caps = LD32(desc + SD_CAPS);
    int w = (flags & DDSD_WIDTH) ? (int)LD32(desc + SD_WIDTH) : hw.width;
    int h = (flags & DDSD_HEIGHT) ? (int)LD32(desc + SD_HEIGHT) : hw.height;

    /* Widescreen: the game composes the world into an off-screen surface it asks for at
     * exactly its own 794x550 and then scales that onto the primary, so enlarging the
     * primary alone only STRETCHES the picture. Widening this surface too is half of what a
     * wider field of view needs; the other half is the game's own width variables, patched
     * in runtime/overrides/menu.c.
     *
     * Both this and the primary FOLLOW THE WINDOW from here on, so they are allocated at
     * WIDE_MAX and start at whatever the window's aspect asks for now. */
    /* ISSUE #50: A SURFACE ASKED FOR AT THE SIZE OF THE PICTURE ABOUT TO BE PUT IN IT IS NOT
     * THE COMPOSITION.
     *
     * "Created at exactly 794x550" was the whole of the test, and it is not a discriminator:
     * the game's picture loader (FUN_0043ed10) creates every sprite surface at its bitmap's
     * own size, and one of those bitmaps -- MENU_WAIT, the loading screen -- happens to be
     * exactly screen-sized. Widening it broke the loader's own invariant, because the loader
     * then reads the surface back with GetSurfaceDesc and StretchBlts the bitmap to whatever
     * width it finds (measured: the ONE non-1:1 StretchBlt in a wide run, 794x550 -> 2542x550
     * from guest 004014a1). The picture came out stretched 3.2x and cropped to its left third,
     * because the draw that puts it on screen takes the authored 794-wide source rect.
     *
     * So the test is now what the game just did: LoadImage handed back a bitmap of exactly
     * this size, which is the loader saying "this surface is a holder for that picture". The
     * composition surface is created at startup from the game's own screen constants, before
     * any bitmap exists, so it is unaffected. The latch is consumed on the first match.
     *
     * NOT fixed by clamping the StretchBlt's destination to 794: that would leave a correct
     * picture inside a surface whose other 1748 columns are undefined, and would hide the same
     * mismatch for any other screen-sized content surface. */
    const int primary = (caps & DDSCAPS_PRIMARYSURFACE) != 0;
    int picture_w = 0, picture_h = 0; long bitmaps_loaded = 0;
    const int picture = !primary && gdi_last_bitmap(&picture_w, &picture_h, &bitmaps_loaded)
                        && picture_w == w && picture_h == h;
    if (picture) gdi_last_bitmap_consume();
    const int follows = primary || (!picture && w == NATIVE_W && h == NATIVE_H);
    if (follows) { w = hw.width; h = hw.height; }
    if (w <= 0) w = 1;
    if (h <= 0) h = 1;

    const uint32_t obj = make_surface(w, h, primary, follows ? WIDE_MAX : w,
                                      follows ? HIGH_MAX : h);
    if (primary) primary_surface = obj;
    if (follows) follow_add(obj);
    /* LF2_SURF_DEBUG=1 -- every CreateSurface, not only the ones that follow, and the guest
     * call site of each. The interesting question is always which of the screen-sized
     * surfaces got widened and which did not, and a log that printed only the widened ones
     * could not answer it. Every line carries the verdict, so "no surface followed" and "no
     * surface was created" are different outputs. */
    if (getenv("LF2_SURF_DEBUG")) {
        static int n;
        fprintf(stderr, "createsurface #%d asked %dx%d caps=%08x from guest %08x -> %dx%d %s\n",
                ++n, (flags & DDSD_WIDTH) ? (int)LD32(desc + SD_WIDTH) : -1,
                (flags & DDSD_HEIGHT) ? (int)LD32(desc + SD_HEIGHT) : -1,
                caps, LD32(R(ESP)), w, h,
                primary ? "PRIMARY (follows)" : follows ? "FOLLOWS THE WINDOW"
                        : picture ? "fixed: holds the picture just loaded (issue #50)"
                        : "fixed");
        if (n == 1)
            fprintf(stderr, "createsurface: %ld bitmaps had been loaded before the first "
                            "surface existed\n", bitmaps_loaded);
    }
    ST32(out, obj);
    com_ret(4, DD_OK);
}

static void dd_CreatePalette(uint32_t self)
{
    (void)self;
    Palette *p = SDL_calloc(1, sizeof *p);
    const uint32_t src = ARG(2), out = ARG(3);
    if (src)
        for (int i = 0; i < 256; i++) {
            const uint32_t e = src + (uint32_t)i * 4;
            p->entries[i] = ((uint32_t)LD8(e) << 16) | ((uint32_t)LD8(e + 1) << 8)
                          | (uint32_t)LD8(e + 2);
        }
    ST32(out, com_create(IF_PALETTE, p));
    com_ret(5, DD_OK);
}

static void dd_CreateClipper(uint32_t self)
{
    (void)self;
    ST32(ARG(2), com_create(IF_CLIPPER, NULL));
    com_ret(4, DD_OK);
}

static void dd_SetCooperativeLevel(uint32_t self) { (void)self; com_ret(3, DD_OK); }
static void dd_SetDisplayMode(uint32_t self)
{
    (void)self;
    /* The game asking for a display mode does NOT resize the window any more. The window is
     * the source of truth for how wide the game is (issue #20), and a request for 794x550
     * that snapped a user's resized window back would undo the resize on the next mode set.
     * The composition it would have produced is whatever the window's aspect already gives,
     * so there is nothing to do but say the mode was set. */
    const int w = (int)ARG(1), h = (int)ARG(2);
    static int said;
    if (!said && w > 0 && h > 0 && (w != hw.width || h != hw.height)) {
        said = 1;
        fprintf(stderr, "ddraw: the game asked for a %dx%d display mode; the window is "
                        "%dx%d and gives a %dx%d composition, which is what it gets\n",
                w, h, hw.win_w, hw.win_h, hw.width, hw.height);
    }
    com_ret(4, DD_OK);
}
static void dd_WaitForVerticalBlank(uint32_t self) { (void)self; com_ret(3, DD_OK); }
static void dd_GetCaps(uint32_t self)
{
    (void)self;
    /* Both structures are optional; zero them apart from the leading dwSize. */
    for (int i = 0; i < 2; i++) {
        const uint32_t p = ARG((unsigned)i + 1);
        if (!p) continue;
        const uint32_t size = LD32(p);
        for (uint32_t o = 4; o < (size ? size : 316u); o += 4) ST32(p + o, 0);
    }
    com_ret(3, DD_OK);
}

static void dd_GetDisplayMode(uint32_t self)
{
    (void)self;
    const uint32_t desc = ARG(1);
    if (desc) {
        ST32(desc + SD_SIZE, SD_BYTES);
        ST32(desc + SD_FLAGS, DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT);
        ST32(desc + SD_HEIGHT, (uint32_t)hw.height);
        ST32(desc + SD_WIDTH, (uint32_t)hw.width);
        ST32(desc + SD_PITCH, (uint32_t)((hw.width + 3) & ~3));
        write_pixelformat(desc + SD_PIXELFORMAT);
    }
    com_ret(2, DD_OK);
}

static void dd_ret_ok1(uint32_t self) { (void)self; com_ret(1, DD_OK); }
static void dd_ret_ok2(uint32_t self) { (void)self; com_ret(2, DD_OK); }
static void dd_ret_ok3(uint32_t self) { (void)self; com_ret(3, DD_OK); }
static void dd_ret_ok4(uint32_t self) { (void)self; com_ret(4, DD_OK); }

static void obj_QueryInterface(uint32_t self)
{
    /* Every ddraw interface version the game asks for maps to the same object. */
    ST32(ARG(2), self);
    com_ret(3, DD_OK);
}
static void obj_AddRef(uint32_t self)  { (void)self; com_ret(1, 1); }
static void obj_Release(uint32_t self) { (void)self; com_ret(1, 0); }

/* ---- clipper ---- */
static void clip_SetHWnd(uint32_t self) { (void)self; com_ret(3, DD_OK); }
static void clip_GetClipList(uint32_t self)
{
    (void)self;
    const uint32_t rgn = ARG(2), size = ARG(3);
    if (rgn && size) {
        /* One clip rect covering the whole window. */
        ST32(rgn, 32 + 16); ST32(rgn + 4, 32); ST32(rgn + 8, 1);
        ST32(rgn + 12, 16); ST32(rgn + 16, 0);
        ST32(rgn + 20, 0); ST32(rgn + 24, 0);
        ST32(rgn + 28, (uint32_t)hw.width); ST32(rgn + 32, (uint32_t)hw.height);
        ST32(rgn + 36, 0); ST32(rgn + 40, 0);
        ST32(rgn + 44, (uint32_t)hw.width); ST32(rgn + 48, (uint32_t)hw.height);
    }
    if (size) ST32(size, 48);
    com_ret(4, DD_OK);
}

/* Lets the GDI layer draw into a surface: DirectDraw's GetDC hands the surface object
 * itself back as a device context, so StretchBlt and TextOut land here. */
int ddraw_surface_info(uint32_t obj, uint32_t *pixels, int *w, int *h, int *pitch)
{
    if (obj < 0x30000000u || obj >= 0x40000000u) return 0;
    Surface *s = com_host(obj);
    if (!s) return 0;
    *pixels = s->pixels; *w = s->w; *h = s->h; *pitch = s->pitch;
    return 1;
}

void ddraw_surface_present(uint32_t obj)
{
    Surface *s = com_host(obj);
    if (s && s->primary) present_primary();
}

/* ---- registration ---- */

/* Method names, so the call trace can be diffed against Wine's ddraw channel. */
static const char *DD_NAMES[23] = {
    "QueryInterface", "AddRef", "Release", "Compact", "CreateClipper", "CreatePalette",
    "CreateSurface", "DuplicateSurface", "EnumDisplayModes", "EnumSurfaces",
    "FlipToGDISurface", "GetCaps", "GetDisplayMode", "GetFourCCCodes", "GetGDISurface",
    "GetMonitorFrequency", "GetScanLine", "GetVerticalBlankStatus", "Initialize",
    "RestoreDisplayMode", "SetCooperativeLevel", "SetDisplayMode", "WaitForVerticalBlank",
};
static const char *SURF_NAMES[36] = {
    "QueryInterface", "AddRef", "Release", "AddAttachedSurface", "AddOverlayDirtyRect",
    "Blt", "BltBatch", "BltFast", "DeleteAttachedSurface", "EnumAttachedSurfaces",
    "EnumOverlayZOrders", "Flip", "GetAttachedSurface", "GetBltStatus", "GetCaps",
    "GetClipper", "GetColorKey", "GetDC", "GetFlipStatus", "GetOverlayPosition",
    "GetPalette", "GetPixelFormat", "GetSurfaceDesc", "Initialize", "IsLost", "Lock",
    "ReleaseDC", "Restore", "SetClipper", "SetColorKey", "SetOverlayPosition",
    "SetPalette", "Unlock", "UpdateOverlay", "UpdateOverlayDisplay", "UpdateOverlayZOrder",
};
static const char *CLIP_NAMES[9] = {
    "QueryInterface", "AddRef", "Release", "GetClipList", "GetHWnd", "Initialize",
    "IsClipListChanged", "SetClipList", "SetHWnd",
};
static const char *PAL_NAMES[7] = {
    "QueryInterface", "AddRef", "Release", "GetCaps", "GetEntries", "Initialize",
    "SetEntries",
};

static void ddraw_name_tables(void);

void ddraw_register(void)
{
    ComClass *c;

    c = &com_class[IF_DDRAW];
    c->name = "IDirectDraw";
    c->nmethods = 23;
    c->method[0] = obj_QueryInterface;
    c->method[1] = obj_AddRef;
    c->method[2] = obj_Release;
    c->method[4] = dd_CreateClipper;
    c->method[5] = dd_CreatePalette;
    c->method[6] = dd_CreateSurface;
    c->method[10] = dd_ret_ok1;              /* FlipToGDISurface */
    c->method[11] = dd_GetCaps;
    c->method[12] = dd_GetDisplayMode;
    c->method[18] = dd_ret_ok2;              /* Initialize */
    c->method[19] = dd_ret_ok1;              /* RestoreDisplayMode */
    c->method[20] = dd_SetCooperativeLevel;
    c->method[21] = dd_SetDisplayMode;
    c->method[22] = dd_WaitForVerticalBlank;

    c = &com_class[IF_SURFACE];
    c->name = "IDirectDrawSurface";
    c->nmethods = 36;
    c->method[0] = obj_QueryInterface;
    c->method[1] = obj_AddRef;
    c->method[2] = obj_Release;
    c->method[5] = surf_Blt;
    c->method[7] = surf_BltFast;
    c->method[11] = surf_Flip;
    c->method[12] = surf_GetAttachedSurface;
    c->method[14] = surf_GetCaps;
    c->method[15] = surf_GetClipper;
    c->method[16] = surf_GetColorKey;
    c->method[20] = surf_GetPalette;
    c->method[17] = surf_GetDC;
    c->method[21] = surf_GetPixelFormat;
    c->method[22] = surf_GetSurfaceDesc;
    c->method[24] = surf_ret_ok1;            /* IsLost */
    c->method[25] = surf_Lock;
    c->method[26] = surf_ReleaseDC;
    c->method[27] = surf_ret_ok1;            /* Restore */
    c->method[28] = surf_SetClipper;
    c->method[29] = surf_SetColorKey;
    c->method[31] = surf_SetPalette;
    c->method[32] = surf_Unlock;

    for (int i = 0; i < 23; i++) com_class[IF_DDRAW].mname[i] = DD_NAMES[i];
    for (int i = 0; i < 36; i++) com_class[IF_SURFACE].mname[i] = SURF_NAMES[i];

    c = &com_class[IF_CLIPPER];
    c->name = "IDirectDrawClipper";
    c->nmethods = 9;
    c->method[0] = obj_QueryInterface;
    c->method[1] = obj_AddRef;
    c->method[2] = obj_Release;
    c->method[3] = clip_GetClipList;
    c->method[4] = dd_ret_ok2;               /* GetHWnd */
    c->method[5] = dd_ret_ok3;               /* Initialize */
    c->method[6] = dd_ret_ok2;               /* IsClipListChanged */
    c->method[7] = dd_ret_ok3;               /* SetClipList */
    c->method[8] = clip_SetHWnd;

    c = &com_class[IF_PALETTE];
    c->name = "IDirectDrawPalette";
    c->nmethods = 7;
    c->method[0] = obj_QueryInterface;
    c->method[1] = obj_AddRef;
    c->method[2] = obj_Release;
    c->method[3] = dd_ret_ok2;               /* GetCaps */
    c->method[4] = pal_GetEntries;
    c->method[5] = dd_ret_ok4;               /* Initialize(dd, flags, table) */
    c->method[6] = pal_SetEntries;

    ddraw_name_tables();
}

static void ddraw_name_tables(void)
{
    for (int i = 0; i < 9; i++) com_class[IF_CLIPPER].mname[i] = CLIP_NAMES[i];
    for (int i = 0; i < 7; i++) com_class[IF_PALETTE].mname[i] = PAL_NAMES[i];
}

/* DirectDrawCreate(guid, ppDD, outer) */
void ddraw_create(void)
{
    ST32(LD32(R(ESP) + 8), com_create(IF_DDRAW, NULL));
    R(EAX) = DD_OK;
    R(ESP) += 4 + 12;
}

typedef void (*Handler)(void);

Handler gfx_lookup(const char *dll, const char *name)
{
    if (strcmp(dll, "DDRAW.dll") == 0 && strcmp(name, "DirectDrawCreate") == 0)
        return ddraw_create;
    return NULL;
}

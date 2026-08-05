/* DirectDraw 1 on SDL3.
 *
 * Surface pixels are allocated inside guest memory, so Lock() hands back a plain guest
 * address and the GDI text path can scribble into the same buffer. Everything is 8-bit
 * indexed, which is what the game's BMPs are. */
#include "com.h"
#include "guest_map.h"
#include "guest_ops.h"
#include "hostwin.h"

#include <time.h>
#include "loadprof.h"


#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define ARG(n) LD32(R(ESP) + 4 + 4 * (n))

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
enum { DDCKEY_SRCBLT = 0x8 };

typedef struct {
    int      w, h, pitch;
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
void glyph_hint_clear(void) { glyph_hint = -1; }

int game_glyph_draw(int ch, int x, int y, uint32_t ink,
                    uint32_t dpix, int dwid, int dhei, int dpitch);

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
static int spec_lists(const char *spec, long frame)
{
    if (!spec) return 0;
    for (const char *c = spec; *c; ) {
        char *end = NULL;
        const long v = strtol(c, &end, 10);
        if (end == c) break;
        if (v == frame) return 1;
        c = end;
        while (*c == ',' || *c == ' ') c++;
    }
    return 0;
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
    return spec_lists(getenv("LF2_FRAME_DUMP"), frame);
}

/* LF2_MEM_DUMP=<frame>[,<frame>...] writes the game's whole .data section to
 * data_<frame>.bin in $LF2_DUMP_DIR. Diffing two of them across a single input finds the
 * variable behind an on-screen change when reading the disassembly would mean picking one
 * candidate out of hundreds -- which is how the pre-fight overlay's selection index was
 * located. tools/diff_data.py does the comparison.
 *
 * The range is the section's own bounds from the PE header, not a guess: dumping too
 * little would drop the answer and look like "nothing changed". */
enum { DATA_BASE = 0x0044d000, DATA_SIZE = 0xc724 };

/* LF2_HEAP_DUMP=<frame>[,...] snapshots the guest heap in use, for the same
 * before/after diffing as LF2_MEM_DUMP but over the region .data cannot reach.
 * tools/diff_data.py --base 0x20000000 reads it. */
uint32_t guest_heap_used(void);          /* imports.c */

static void dump_heap(long frame)
{
    if (!spec_lists(getenv("LF2_HEAP_DUMP"), frame)) return;
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
    if (!spec_lists(getenv("LF2_MEM_DUMP"), frame)) return;
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
    virtual_pad_report();
    clock_sites_report();
    window_resize_report();
    if (getenv("LF2_SHUTDOWN_DEBUG")) fprintf(stderr, "shutdown: releasing SDL\n");
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

void hostwin_present(const uint8_t *pixels, int w, int h, int src_pitch)
{
    rwatch_frame();
    if (++frames % 60 == 1) fprintf(stderr, "present #%ld %dx%d renderer=%p\n", frames, w, h, (void *)hw.renderer);
    screen_change_check(pixels, w, h, src_pitch, frames);
    dump_frame(pixels, w, h, src_pitch, frames);
    dump_data(frames);
    dump_heap(frames);
    /* Periodic, not one-shot: a single report at frame 900 lands before the match has
     * started, so it measures the menus and reads as if nothing ever plays. */
    if (frames % 900 == 0) { colorkey_report(); vram_report(); com_release_report(); input_report(); if (getenv("LF2_AUDIO_DEBUG")) audio_report(); }
    /* The read profile is reported on the same periodic boundary and reset each time, so
     * each block covers one window rather than the whole run: an array swept only during a
     * match would otherwise be averaged with the menus that came before it. */
    if (frames % 300 == 0) {
        char when[32];
        snprintf(when, sizeof when, "frames %ld-%ld", frames - 299, frames);
        rwatch_raw_flush(when);
    }
    if (!hw.renderer) return;
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
    SDL_RenderClear(hw.renderer);
    SDL_RenderTexture(hw.renderer, hw.texture, NULL, NULL);
    SDL_RenderPresent(hw.renderer);
    frame_pace();
}

/* ---- real time, and it lives HERE now ----
 *
 * The guest clock is the frame counter (runtime/imports.c), so the game's main loop is
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
static void frame_pace(void)
{
    static uint64_t anchor_wall, anchor_frame;

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
static int hint_on;
void controls_hint_enable(int on) { hint_on = on; }

static void controls_hint_draw(const Surface *s)
{
    static const char TEXT[] = "KEYBOARD:  ARROWS MOVE   Z ATTACK   X JUMP   C DEFEND";
    /* Left, because the game's own URL owns the right -- but inside the centred picture,
     * not inside the black band beside it, or the line starts in the margin and crosses
     * the edge halfway through a word. */
    const int x0 = 8 + screen_offset_x();
    for (int i = 0; TEXT[i]; i++)
        game_glyph_draw(TEXT[i], x0 + i * 8, s->h - 16,
                        0xffffffu, s->pixels, s->w, s->h, s->pitch);
}

/* The pause menu needs the frame to keep being shown while the game's update is not
 * running -- and the present turned out to live INSIDE that update, so freezing it stopped
 * the picture entirely (frames simply stopped at the pause). This is the same present, on
 * demand. */
static void present_primary(void);
void present_frozen_frame(void) { present_primary(); }

static void present_primary(void)
{
    if (!primary_surface) return;
    LOADPROF_SCOPE(LP_PRESENT);
    Surface *s = com_host(primary_surface);
    if (hint_on) controls_hint_draw(s);
    /* After the frame is assembled and before it is shown: the composition is frozen while
     * paused, so the menu has to go on the primary rather than be composed with it. */
    pause_draw(s->pixels, s->w, s->h, s->pitch);
    hostwin_present(g_mem + s->pixels, s->w, s->h, s->pitch);
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
    com_ret(5, DD_OK);
}

static void surf_Unlock(uint32_t self)
{
    Surface *s = com_host(self);
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

static void blit(Surface *d, int dx, int dy, int dw, int dh,
                 Surface *s, int sx, int sy, int sw, int sh,
                 int keyed, uint32_t klo, uint32_t khi)
{
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;

    const uint32_t lo = klo & 0x00ffffffu, hi = khi & 0x00ffffffu;

    /* Destination columns that land inside both surfaces, and their source column.
     * Clipping here as well means the inner loop carries no per-pixel branches. */
    static int col_src[BLIT_MAXW], col_dst[BLIT_MAXW];
    int ncol = 0;
    const int wlim = dw < BLIT_MAXW ? dw : BLIT_MAXW;
    for (int x = 0; x < wlim; x++) {
        const int sxx = sx + (int)((int64_t)x * sw / dw), dxx = dx + x;
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

static void panel_note(int l, int t, int r, int b)
{
    if (l == 40 && t == 33 && r == 745 && b == 520) panel_charselect_frame = frames;
    else if (l == 3 && t == 3 && r == 307 && b == 159) panel_overlay_frame = frames;
    /* The in-match HUD strip: eight player slots as two rows of four 198x54 panels. It is
     * drawn only while a match is on screen, which makes it the signal for "the world view
     * is up" -- the one screen that should be WIDE rather than centred. */
    else if (r - l == 198 && b - t == 54 && (t == 0 || t == 54)) panel_hud_frame = frames;
}

int panel_charselect_up(void) { return frames - panel_charselect_frame <= PANEL_FRESH; }
int panel_overlay_up(void)    { return frames - panel_overlay_frame    <= PANEL_FRESH; }
int panel_hud_up(void)        { return frames - panel_hud_frame        <= PANEL_FRESH; }

/* ---- centring what cannot be made wide ----
 *
 * Widescreen gives the MATCH more world, because the world is drawn from a camera and the
 * game's own viewport width drives it. Nothing else in the game works that way: the front
 * end, the mode menu, character selection and the pre-fight overlay are fixed 794-wide
 * compositions, and on a wider viewport they simply sat against the left edge with a black
 * band down the right.
 *
 * So they are centred instead. One offset, applied to every blit destination in the frames
 * where the world is not on screen, and subtracted again from the pointer so the game's own
 * hit tests and the ported menus still line up with what the player sees. The band either
 * side is the game's own full-screen clear, which already covers the whole viewport. */
enum { NATIVE_W = 794, NATIVE_H = 550 };   /* the composition the game asks for */

/* The widest composition this port will hand the game, and the width every resizable
 * surface's PITCH is fixed at. It is not a taste: vram_alloc is a bump allocator with no
 * free, so a surface reallocated on every resize event would exhaust the arena during one
 * drag of a window edge (issue #20). Allocating once at the maximum and moving only s->w
 * costs 4096*4*550 = 9 MB of a 1 GiB arena per resizable surface, and keeps the pitch
 * constant so anything the game cached from an earlier Lock stays valid.
 *
 * 4096 is the bound the port already validated widths against when this was an env var,
 * kept so the two do not disagree. A 32:9 monitor at full height asks for 1956; the rest of
 * the range is for a window someone has dragged very short and very wide. */
enum { WIDE_MAX = 4096 };
enum { HUD_W = 792, HUD_BAND_H = 118 };   /* the in-match HUD strip and the band it owns */

/* The in-match HUD's own centring offset, exposed because the GDI text path draws straight
 * into the surface and never goes through Blt -- so without this the game's text in the HUD
 * band stays at the left edge while the panels under it move. `bottom` is the destination's
 * lowest row, which is what decides whether a draw belongs to the HUD band. */
int hud_offset_x(int dst_w, int bottom)
{
    if (!lf2_wide_width() || !panel_hud_up()) return 0;
    if (dst_w <= NATIVE_W || bottom > HUD_BAND_H) return 0;
    return (dst_w - HUD_W) / 2;
}

int screen_offset_x(void)
{
    const int wide = lf2_wide_width();
    if (!wide || panel_hud_up()) return 0;
    const int off = (hw.width - NATIVE_W) / 2;
    return off > 0 ? off : 0;
}

/* LF2_BLT_FRAME=<frame>[,...] -- every blit that composes those presented frames, with both
 * rectangles, the source surface and the caller. This replaces LF2_BLT_ALL, which was capped
 * at the first 24 blits of the whole run: that is the front end, so a question about what a
 * match draws got 24 lines about the menu and no denominator. Scoping to a frame is what
 * makes the list complete AND finite -- a match frame is about 140 blits.
 *
 * It is called BEFORE the colour-fill branch on purpose. That branch returns early, so a
 * hook placed after it cannot see a fill at all -- and "the ground is drawn by 4 layer
 * blits" was a wrong answer produced exactly that way, with the fill underneath them
 * invisible to the instrument.
 *
 * Blits for presented frame N are issued while the counter still reads N-1, so the number
 * printed is the frame the pixels land in, which is the number LF2_FRAME_DUMP uses. */
static void blt_frame_log(int dl, int dt, int dr, int db,
                          uint32_t srcobj, uint32_t srect, uint32_t flags)
{
    const char *spec = getenv("LF2_BLT_FRAME");
    if (!spec) return;

    const long f = hostwin_frames() + 1;
    static long logged_for = -1, n;
    if (!spec_lists(spec, f)) {
        if (logged_for >= 0) {
            fprintf(stderr, "bltframe %ld: %ld blits total\n", logged_for, n);
            logged_for = -1;
        }
        return;
    }
    if (logged_for != f) {
        if (logged_for >= 0) fprintf(stderr, "bltframe %ld: %ld blits total\n", logged_for, n);
        logged_for = f; n = 0;
        fprintf(stderr, "bltframe %ld: begin\n", f);
    }
    n++;

    int sl = -1, st = -1, sr = -1, sb = -1, sw = -1, sh = -1;
    if (srcobj) {
        Surface *s = com_host(srcobj);
        read_rect(srect, &sl, &st, &sr, &sb, s->w, s->h);
        sw = s->w; sh = s->h;
    }
    char fill[48] = "";
    if (flags & DDBLT_COLORFILL)
        snprintf(fill, sizeof fill, " COLORFILL=%08x",
                 ARG(5) ? LD32(ARG(5) + DDBLTFX_FILLCOLOR) : 0);
    fprintf(stderr, "blt %ld dst=(%d,%d)-(%d,%d) src=%08x[%dx%d] srect=%s(%d,%d)-(%d,%d)"
                    " flags=%08x from=%08x%s\n",
            n, dl, dt, dr, db, srcobj, sw, sh, srect ? "" : "NULL",
            sl, st, sr, sb, flags, LD32(R(ESP)), fill);
}

static void surf_Blt(uint32_t self)
{
    LOADPROF_SCOPE(LP_BLT);
    Surface *d = com_host(self);
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
    /* Only on the way OUT, never while composing. The game builds its frame in an
     * off-screen surface and copies that to the primary in one blit, so shifting just that
     * copy centres the whole composition once. Offsetting during composition as well moved
     * everything twice -- a 132 px margin came out at 264. */
    if (d->primary) {
        const int off = screen_offset_x();
        dl += off; dr += off;
    }

    blt_frame_log(dl, dt, dr, db, srcobj, srect, flags);

    if (flags & DDBLT_COLORFILL) {
        const uint32_t fill = ARG(5) ? LD32(ARG(5) + DDBLTFX_FILLCOLOR) : 0;
        /* Widescreen: a fill that spans the whole native width is a full-width band -- the
         * sky, the ground, the road -- so it should span the whole viewport. The width is
         * an immediate in the game rather than one of the viewport variables, which is why
         * these did not follow the layers: without this the ground simply stopped at 794
         * and the rest of the stage floor was black. */
        if (lf2_wide_width() && dl == 0 && dr == NATIVE_W && d->w > NATIVE_W) dr = d->w;
        for (int y = dt; y < db && y < d->h; y++) {
            if (y < 0) continue;
            uint32_t *row = (uint32_t *)(g_mem + d->pixels + (size_t)y * (size_t)d->pitch);
            for (int x = dl < 0 ? 0 : dl; x < dr && x < d->w; x++) row[x] = fill & 0x00ffffffu;
        }
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

    /* Widescreen: a background layer drawn from x 0 across the whole native width is a
     * full-width backdrop -- the sky, a distant panorama -- and the game draws it as ONE
     * blit clipped to 794 rather than by looping it. Those are the only pieces that cannot
     * be made wider by drawing more of them, so they are stretched across the viewport
     * instead. It is a soft gradient over a third more width; the alternative is the black
     * band that was there before.
     *
     * Gated on the world view being up, or it would also stretch the fixed 794-wide menu
     * backdrops that are deliberately being CENTRED. */
    if (lf2_wide_width() && panel_hud_up() && srcobj
        && dl == 0 && dr == NATIVE_W && d->w > NATIVE_W)
        dr = d->w;

    panel_note(dl, dt, dr, db);
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
    Surface *s = srcobj ? com_host(srcobj) : NULL;
    if (s) {
        int sl, st_, sr, sb;
        read_rect(srect, &sl, &st_, &sr, &sb, s->w, s->h);
        /* LF2_CK_FORCE is a discriminator, not a fix: if honouring the key on every blit
         * that has one makes the sprites transparent, they arrive through Blt and the
         * question is about the flags; if nothing changes, they are composited elsewhere
         * (Lock) and the Blt path is innocent. Run against both classes before believing
         * either. */
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
        /* A glyph the port can draw better is drawn instead of copied. Anything it
         * declines -- no font, or a character outside printable ASCII, which is how the
         * sheets' CJK stays correct -- falls through to the original copy. */
        if (glyph_hint >= 0
            && game_glyph_draw(glyph_hint, dl, dt, glyph_ink(s, sl, st_, sr, sb),
                               d->pixels, d->w, d->h, d->pitch)) {
            glyphs_drawn++;
        } else {
            blit(d, dl, dt, dr - dl, db - dt, s, sl, st_, sr - sl, sb - st_,
                 keyed, s->key_lo, s->key_hi);
        }
    }
    /* Widescreen: finish a tiling series the game stopped at 794.
     *
     * Background layers that repeat are drawn as a run of edge-to-edge copies, and the
     * count comes from an immediate 794 inside FUN_0041a250 -- baked into the recompiled
     * code, so unlike the viewport words it cannot be written at runtime. The series
     * therefore stops one copy short: HK Coliseum's arch band ends at 803 with 255 px of
     * black beyond it.
     *
     * A copy is recognised as part of a series by being CONTIGUOUS with the blit before it
     * -- same rows, left edge exactly where the last one ended -- which is what the game's
     * own tiling looks like and what a lone prop (a lamp, a crate) never looks like. Given
     * that, continuing at the same period is the game's layout carried on, not invented.
     *
     * A period of zero or a run that already reaches the edge does nothing, and the copies
     * are bounded by the surface, so a pathological period cannot loop for ever. */
    if (lf2_wide_width() && srcobj && d->w > NATIVE_W && dt >= HUD_BAND_H) {
        /* Tracked for EVERY blit in the world band, not only the short ones: the previous
         * rectangle is the evidence that this one continues a series, so letting it go
         * stale would let an unrelated blit inherit a match. */
        static int prev_l, prev_t, prev_r, prev_b;
        const int period = dr - dl;
        /* Size floor: the game draws its TEXT glyph by glyph, edge to edge, which is
         * contiguous by exactly the same test -- without this the bottom-right "VS mode
         * (Difficult)" caption tiled itself across the whole width. A background layer is
         * never 8x16. */
        enum { TILE_MIN_PERIOD = 48, TILE_MIN_HEIGHT = 30 };
        if (dr < d->w && period >= TILE_MIN_PERIOD && db - dt >= TILE_MIN_HEIGHT
            && prev_r > prev_l && dl == prev_r && dt == prev_t && db == prev_b) {
            Surface *src = com_host(srcobj);
            int sl, st, sr, sb;
            read_rect(srect, &sl, &st, &sr, &sb, src->w, src->h);
            for (int x = dr; x < d->w; x += period)
                blit(d, x, dt, period, db - dt, src, sl, st, sr - sl, sb - st,
                     (flags & DDBLT_KEYSRC) && src->has_key, src->key_lo, src->key_hi);
        }
        prev_l = dl; prev_t = dt; prev_r = dr; prev_b = db;
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
        fprintf(stderr, "SetColorKey #%ld flags=%08x key=%08x\n", ck_set, ARG(1), key);
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
static uint32_t make_surface(int w, int h, int primary, int maxw)
{
    Surface *s = SDL_calloc(1, sizeof *s);
    if (maxw < w) maxw = w;
    s->w = w; s->h = h;
    s->pitch = maxw * 4;
    s->pixels = vram_alloc((uint32_t)s->pitch * (uint32_t)h);
    s->primary = primary;
    memset(g_mem + s->pixels, 0, (size_t)s->pitch * (size_t)h);
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

static void surfaces_follow_window(int w)
{
    for (int i = 0; i < follow_n; i++) {
        Surface *s = com_host(follow[i]);
        if (!s) continue;
        /* Never past the pitch it was allocated with -- a surface created before the cap was
         * known (there are none today) would otherwise write off the end of its buffer. */
        s->w = w * 4 <= s->pitch ? w : s->pitch / 4;
    }
}

/* The window changed size. The compose width follows its ASPECT, not its pixel width: a
 * 1920x1080 window wants 978x550 of world scaled up to fill it, not 1920x550 sitting in a
 * 1080-tall window with the pixels shrunk to a quarter. Anything narrower in aspect than the
 * game's own 794x550 gets 794 and is letterboxed, because the HUD strip is 792 wide and
 * there is nothing sensible below that. */
void hostwin_window_geometry(int win_w, int win_h)
{
    if (win_w <= 0 || win_h <= 0) return;
    hw.win_w = win_w;
    hw.win_h = win_h;

    long w = ((long)NATIVE_H * win_w + win_h / 2) / win_h;
    if (w < NATIVE_W) w = NATIVE_W;
    if (w > WIDE_MAX) {
        static int said;
        if (!said) {
            said = 1;
            fprintf(stderr, "widescreen: a %dx%d window asks for a %ld-wide composition, "
                            "past the %d this build allocates for; clamped, so the picture "
                            "is letterboxed at the sides from here on\n",
                    win_w, win_h, w, WIDE_MAX);
        }
        w = WIDE_MAX;
    }

    if ((int)w == hw.width && hw.height == NATIVE_H) return;   /* the aspect did not move */
    fprintf(stderr, "widescreen: window %dx%d -> composition %ldx%d (was %dx%d)\n",
            win_w, win_h, w, NATIVE_H, hw.width, hw.height);
    hw.width = (int)w;
    hw.height = NATIVE_H;

    surfaces_follow_window(hw.width);
    if (hw.renderer)
        SDL_SetRenderLogicalPresentation(hw.renderer, hw.width, hw.height,
                                         SDL_LOGICAL_PRESENTATION_LETTERBOX);
    /* The texture is sized to the composition, so it has to go with it. Recreated on the
     * next present rather than here, where the renderer may not exist yet. */
    if (hw.texture) { SDL_DestroyTexture(hw.texture); hw.texture = NULL; }
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
    const int primary = (caps & DDSCAPS_PRIMARYSURFACE) != 0;
    const int follows = primary || (w == NATIVE_W && h == NATIVE_H);
    if (follows) { w = hw.width; h = hw.height; }
    if (w <= 0) w = 1;
    if (h <= 0) h = 1;

    const uint32_t obj = make_surface(w, h, primary, follows ? WIDE_MAX : w);
    if (primary) primary_surface = obj;
    if (follows) follow_add(obj);
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

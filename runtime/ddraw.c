/* DirectDraw 1 on SDL3.
 *
 * Surface pixels are allocated inside guest memory, so Lock() hands back a plain guest
 * address and the GDI text path can scribble into the same buffer. Everything is 8-bit
 * indexed, which is what the game's BMPs are. */
#include "com.h"
#include "guest_ops.h"
#include "hostwin.h"

#include <SDL3/SDL.h>
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
enum { DDCKEY_SRCBLT = 0x8 };

typedef struct {
    int      w, h, pitch;
    uint32_t pixels;        /* guest address of the pixel buffer */
    int      primary;
    int      has_key;
    uint32_t key_lo, key_hi;
    uint32_t palette;       /* guest object address of the attached palette, 0 if none */
    uint32_t attached;      /* back buffer handed out by GetAttachedSurface */
} Surface;

typedef struct { uint32_t entries[256]; } Palette;

static uint32_t primary_surface;
static uint32_t active_palette;

/* Surfaces live here rather than on the guest heap so a huge sprite sheet cannot
 * collide with malloc'd game data. */
enum { VRAM_BASE = 0x50000000u };
static uint32_t vram_next = VRAM_BASE;

static uint32_t vram_alloc(uint32_t n)
{
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

/* ---- presentation ---- */

void hostwin_present(const uint8_t *indexed, const uint32_t *palette, int w, int h, int src_pitch)
{
    static long frames;
    rwatch_frame();
    if (++frames % 60 == 1) fprintf(stderr, "present #%ld %dx%d renderer=%p\n", frames, w, h, (void *)hw.renderer);
    screen_change_check(indexed, w, h, src_pitch, frames);
    if (!hw.renderer) return;
    if (!hw.texture) {
        hw.texture = SDL_CreateTexture(hw.renderer, SDL_PIXELFORMAT_XRGB8888,
                                       SDL_TEXTUREACCESS_STREAMING, w, h);
        SDL_SetTextureScaleMode(hw.texture, SDL_SCALEMODE_NEAREST);
    }
    void *dst = NULL;
    int pitch = 0;
    if (SDL_LockTexture(hw.texture, NULL, &dst, &pitch)) {
        for (int y = 0; y < h; y++) {
            uint32_t *row = (uint32_t *)((uint8_t *)dst + (size_t)y * (size_t)pitch);
            /* Rows are src_pitch apart, not width apart. */
            const uint32_t *src = (const uint32_t *)(indexed + (size_t)y * (size_t)src_pitch);
            memcpy(row, src, (size_t)w * 4);
        }
        SDL_UnlockTexture(hw.texture);
    }
    SDL_RenderClear(hw.renderer);
    SDL_RenderTexture(hw.renderer, hw.texture, NULL, NULL);
    SDL_RenderPresent(hw.renderer);
}

static void present_primary(void)
{
    if (!primary_surface) return;
    Surface *s = com_host(primary_surface);
    hostwin_present(g_mem + s->pixels, NULL, s->w, s->h, s->pitch);
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
static void blit(Surface *d, int dx, int dy, int dw, int dh,
                 Surface *s, int sx, int sy, int sw, int sh,
                 int keyed, uint32_t klo, uint32_t khi)
{
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    for (int y = 0; y < dh; y++) {
        const int syy = sy + (int)((int64_t)y * sh / dh), dyy = dy + y;
        if (syy < 0 || syy >= s->h || dyy < 0 || dyy >= d->h) continue;
        const uint32_t *sp = (const uint32_t *)(g_mem + s->pixels + (size_t)syy * (size_t)s->pitch);
        uint32_t *dp = (uint32_t *)(g_mem + d->pixels + (size_t)dyy * (size_t)d->pitch);
        for (int x = 0; x < dw; x++) {
            const int sxx = sx + (int)((int64_t)x * sw / dw), dxx = dx + x;
            if (sxx < 0 || sxx >= s->w || dxx < 0 || dxx >= d->w) continue;
            const uint32_t v = sp[sxx] & 0x00ffffffu;
            if (keyed && v >= (klo & 0x00ffffffu) && v <= (khi & 0x00ffffffu)) continue;
            dp[dxx] = v;
        }
    }
}

static void dump_surface(uint32_t obj, const char *tag)
{
    Surface *s = com_host(obj);
    if (!s) return;
    char path[160];
    snprintf(path, sizeof path, "./scratch/dump_%s.ppm", tag);
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

static void surf_Blt(uint32_t self)
{
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

    if (flags & DDBLT_COLORFILL) {
        const uint32_t fill = ARG(5) ? LD32(ARG(5) + 16) : 0;   /* DDBLTFX.dwFillColor */
        for (int y = dt; y < db && y < d->h; y++) {
            if (y < 0) continue;
            uint32_t *row = (uint32_t *)(g_mem + d->pixels + (size_t)y * (size_t)d->pitch);
            for (int x = dl < 0 ? 0 : dl; x < dr && x < d->w; x++) row[x] = fill & 0x00ffffffu;
        }
        if (d->primary) present_primary();
        com_ret(6, DD_OK);
        return;
    }

    if (getenv("LF2_BLT_ALL")) {
        static long n;
        if (++n <= 24)
            {
                int _sl = -1, _st = -1, _sr = -1, _sb = -1;
                if (srcobj) { Surface *_s = com_host(srcobj);
                              read_rect(srect, &_sl, &_st, &_sr, &_sb, _s->w, _s->h); }
                fprintf(stderr, "blt#%ld dst=(%d,%d)-(%d,%d) src=%08x srect=%s(%d,%d)-(%d,%d)\n",
                        n, dl, dt, dr, db, srcobj, srect ? "" : "NULL", _sl, _st, _sr, _sb);
            }
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
    Surface *s = srcobj ? com_host(srcobj) : NULL;
    if (s) {
        int sl, st_, sr, sb;
        read_rect(srect, &sl, &st_, &sr, &sb, s->w, s->h);
        const int keyed = (flags & DDBLT_KEYSRC) && s->has_key;
        blit(d, dl, dt, dr - dl, db - dt, s, sl, st_, sr - sl, sb - st_,
             keyed, s->key_lo, s->key_hi);
    }
    if (d->primary) {
        if (getenv("LF2_BLT_DEBUG")) {
            static long n;
            fprintf(stderr, "blt->primary #%ld drect=%08x [%d %d %d %d] src=%08x srect=%08x flags=%08x\n",
                    ++n, drect, dl, dt, dr, db, srcobj, srect, flags);
        }
        present_primary();
    }
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
        blit(d, dx, dy, sr - sl, sb - st_, s, sl, st_, sr - sl, sb - st_,
             (flags & 1) && s->has_key, s->key_lo, s->key_hi);
    }
    if (d->primary) present_primary();
    com_ret(6, DD_OK);
}

static void surf_SetColorKey(uint32_t self)
{
    Surface *s = com_host(self);
    const uint32_t key = ARG(2);
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

static void surf_GetClipper(uint32_t self)
{
    (void)self;
    if (ARG(1)) ST32(ARG(1), 0);
    com_ret(2, E_FAIL);            /* DDERR_NOCLIPPERATTACHED */
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
static void surf_ret_ok2(uint32_t self) { (void)self; com_ret(2, DD_OK); }
static void surf_ret_ok3(uint32_t self) { (void)self; com_ret(3, DD_OK); }

static void surf_GetPixelFormat(uint32_t self)
{
    (void)self;
    write_pixelformat(ARG(1));
    com_ret(2, DD_OK);
}

/* ---- IDirectDraw ---- */

static uint32_t make_surface(int w, int h, int primary)
{
    Surface *s = SDL_calloc(1, sizeof *s);
    s->w = w; s->h = h;
    s->pitch = w * 4;
    s->pixels = vram_alloc((uint32_t)s->pitch * (uint32_t)h);
    s->primary = primary;
    memset(g_mem + s->pixels, 0, (size_t)s->pitch * (size_t)h);
    return com_create(IF_SURFACE, s);
}

static void dd_CreateSurface(uint32_t self)
{
    (void)self;
    const uint32_t desc = ARG(1), out = ARG(2);
    const uint32_t flags = LD32(desc + SD_FLAGS);
    const uint32_t caps = LD32(desc + SD_CAPS);
    int w = (flags & DDSD_WIDTH) ? (int)LD32(desc + SD_WIDTH) : hw.width;
    int h = (flags & DDSD_HEIGHT) ? (int)LD32(desc + SD_HEIGHT) : hw.height;
    const int primary = (caps & DDSCAPS_PRIMARYSURFACE) != 0;
    if (primary) { w = hw.width; h = hw.height; }
    if (w <= 0) w = 1;
    if (h <= 0) h = 1;

    const uint32_t obj = make_surface(w, h, primary);
    if (primary) primary_surface = obj;
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
    const int w = (int)ARG(1), h = (int)ARG(2);
    if (w > 0 && h > 0) {
        hw.width = w; hw.height = h;
        if (hw.window) SDL_SetWindowSize(hw.window, w, h);
        if (hw.texture) { SDL_DestroyTexture(hw.texture); hw.texture = NULL; }
        if (hw.renderer)
            SDL_SetRenderLogicalPresentation(hw.renderer, w, h, SDL_LOGICAL_PRESENTATION_LETTERBOX);
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
    c->method[28] = surf_ret_ok2;            /* SetClipper */
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

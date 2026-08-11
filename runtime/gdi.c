/* GDI, enough for how the game loads sprites.
 *
 * The game builds each sprite surface by: LoadImage the BMP, CreateCompatibleDC,
 * SelectObject the bitmap, DirectDraw GetDC on the surface, StretchBlt the bitmap in,
 * ReleaseDC, then SetColorKey. Stubbing StretchBlt left every surface blank and the
 * game took a different path from the real one -- the trace harness showed exactly
 * that. Surfaces and the game's BMPs are both 8-bit indexed, so the copy is a plain
 * index copy with no colour conversion.
 */
#include "render.h"
#include "com.h"
#include "guest_ops.h"
#include "hostwin.h"
#include "loadprof.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef LF2_HAVE_TTF
#include <SDL3_ttf/SDL_ttf.h>
#endif

#define ARG(n) LD32(R(ESP) + 4 + 4 * (n))

static void ret_stdcall(int nargs, uint32_t value)
{
    R(EAX) = value;
    R(ESP) += 4 + 4u * (unsigned)nargs;
}

int  ddraw_surface_info(uint32_t obj, uint32_t *pixels, int *w, int *h, int *pitch);
void ddraw_surface_present(uint32_t obj);
const char *host_path_of(uint32_t guest_str);

typedef struct {
    int      w, h, pitch, bpp;
    uint8_t *pixels;            /* top-down, one byte per pixel for 8-bit */
    uint32_t pal[256];
} Bitmap;

enum { MAX_GDI = 512, H_BITMAP = 0xB0000000u, H_DC = 0xD0000000u };
static Bitmap *bitmaps[MAX_GDI];
static int nbitmaps;
static uint32_t dc_bitmap[MAX_GDI];   /* memory DC -> selected bitmap handle */
static int ndcs = 1;

static Bitmap *bitmap_of(uint32_t h)
{
    const uint32_t i = h - H_BITMAP;
    return i < (uint32_t)nbitmaps ? bitmaps[i] : NULL;
}

/* RLE8, which is what the game's bitmaps actually use -- their headers declare far more
 * pixels than the file contains, and reading them as raw rows yields garbage that then
 * runs out partway down.
 *
 * Pairs of (count, value): a non-zero count repeats value. A zero count is an escape --
 * 0 ends the line, 1 ends the bitmap, 2 is a delta, and 3 or more introduces that many
 * literal bytes padded to a word boundary. Output is written top-down here; the caller
 * has already accounted for the bottom-up flip. */
static void rle8_decode(const uint8_t *src, size_t n, Bitmap *b, int flip)
{
    size_t i = 0;
    int x = 0, y = 0;
    while (i + 1 < n && y < b->h) {
        const uint8_t count = src[i], value = src[i + 1];
        i += 2;
        if (count) {
            for (int k = 0; k < count && x < b->w; k++, x++)
                b->pixels[(size_t)(flip ? b->h - 1 - y : y) * (size_t)b->pitch + (size_t)x] = value;
            continue;
        }
        if (value == 0) { x = 0; y++; continue; }          /* end of line */
        if (value == 1) break;                              /* end of bitmap */
        if (value == 2) {                                   /* delta */
            if (i + 1 >= n) break;
            x += src[i]; y += src[i + 1];
            i += 2;
            continue;
        }
        for (int k = 0; k < value && i < n; k++, i++, x++) { /* absolute run */
            if (x < b->w && y < b->h)
                b->pixels[(size_t)(flip ? b->h - 1 - y : y) * (size_t)b->pitch + (size_t)x] = src[i];
        }
        if (value & 1) i++;                                 /* pad to a word */
    }
}

/* Load a Windows BMP. Rows are stored bottom-up and padded to four bytes. */
static Bitmap *bmp_load(const char *path)
{
    FILE *fh = fopen(path, "rb");
    if (!fh) return NULL;
    uint8_t hdr[54];
    if (fread(hdr, 1, sizeof hdr, fh) != sizeof hdr || hdr[0] != 'B' || hdr[1] != 'M') {
        fclose(fh);
        return NULL;
    }
    const uint32_t data_off = (uint32_t)hdr[10] | ((uint32_t)hdr[11] << 8)
                            | ((uint32_t)hdr[12] << 16) | ((uint32_t)hdr[13] << 24);
    const int32_t w = (int32_t)((uint32_t)hdr[18] | ((uint32_t)hdr[19] << 8)
                     | ((uint32_t)hdr[20] << 16) | ((uint32_t)hdr[21] << 24));
    const int32_t h = (int32_t)((uint32_t)hdr[22] | ((uint32_t)hdr[23] << 8)
                     | ((uint32_t)hdr[24] << 16) | ((uint32_t)hdr[25] << 24));
    const int bpp = (int)((uint32_t)hdr[28] | ((uint32_t)hdr[29] << 8));
    const uint32_t compression = (uint32_t)hdr[30] | ((uint32_t)hdr[31] << 8)
                               | ((uint32_t)hdr[32] << 16) | ((uint32_t)hdr[33] << 24);
    const uint32_t clr_used = (uint32_t)hdr[46] | ((uint32_t)hdr[47] << 8)
                            | ((uint32_t)hdr[48] << 16) | ((uint32_t)hdr[49] << 24);
    if (bpp != 8 || w <= 0) { fclose(fh); return NULL; }

    const int flip = h > 0;                 /* positive height means bottom-up */
    const int rows = flip ? h : -h;

    Bitmap *b = SDL_calloc(1, sizeof *b);
    b->w = w; b->h = rows; b->bpp = bpp;
    b->pitch = w;
    b->pixels = SDL_calloc(1, (size_t)w * (size_t)rows);

    const uint32_t ncolours = clr_used ? clr_used : 256u;
    fseek(fh, 54, SEEK_SET);
    for (uint32_t i = 0; i < ncolours && i < 256; i++) {
        uint8_t e[4];
        if (fread(e, 1, 4, fh) != 4) break;
        b->pal[i] = ((uint32_t)e[2] << 16) | ((uint32_t)e[1] << 8) | e[0];
    }

    fseek(fh, 0, SEEK_END);
    const long file_size = ftell(fh);
    fseek(fh, (long)data_off, SEEK_SET);

    if (compression == 1) {                                 /* BI_RLE8 */
        const size_t n = (size_t)(file_size - (long)data_off);
        uint8_t *raw = SDL_malloc(n);
        const size_t got = fread(raw, 1, n, fh);
        rle8_decode(raw, got, b, flip);
        SDL_free(raw);
        fclose(fh);
        return b;
    }

    const size_t src_pitch = ((size_t)w + 3u) & ~3u;
    uint8_t *row = SDL_malloc(src_pitch);
    for (int y = 0; y < rows; y++) {
        if (fread(row, 1, src_pitch, fh) != src_pitch) break;
        memcpy(b->pixels + (size_t)(flip ? rows - 1 - y : y) * (size_t)w, row, (size_t)w);
    }
    SDL_free(row);
    fclose(fh);
    return b;
}

static uint32_t bitmap_handle(Bitmap *b)
{
    if (!b || nbitmaps >= (int)MAX_GDI) return 0;
    bitmaps[nbitmaps] = b;
    return H_BITMAP + (uint32_t)nbitmaps++;
}

/* ---- PE resources ----
 * The menu bitmaps are not files; they live in the 3 MB .rsrc section and the game asks
 * for them by name. The image is already mapped in guest memory, so the resource tree is
 * walked there. A resource bitmap is a DIB: BITMAPINFOHEADER, palette, pixels, with no
 * file header. */
enum { IMAGE_BASE = 0x400000, RT_BITMAP = 2 };

static uint32_t rsrc_base;

static int name_matches(uint32_t entry_name, const char *want)
{
    if (!(entry_name & 0x80000000u)) return 0;
    const uint32_t p = rsrc_base + (entry_name & 0x7fffffffu);
    const uint32_t len = LD16(p);
    for (uint32_t i = 0; i < len; i++) {
        const uint32_t ch = LD16(p + 2 + i * 2);
        const unsigned char w = (unsigned char)want[i];
        if (!w || ch > 0xff) return 0;
        if (SDL_toupper(ch) != SDL_toupper(w)) return 0;
    }
    return want[len] == 0;
}

/* Returns the data entry RVA, or 0. */
static uint32_t rsrc_find(const char *name)
{
    const uint32_t pe = LD32(IMAGE_BASE + 0x3C) + IMAGE_BASE;
    const uint32_t dir_rva = LD32(pe + 24 + 112);          /* data directory 2 */
    if (!dir_rva) return 0;
    rsrc_base = IMAGE_BASE + dir_rva;

    if (getenv("LF2_RSRC_DEBUG"))
        fprintf(stderr, "rsrc base=%08x named=%d id=%d\n", rsrc_base,
                LD16(rsrc_base + 12), LD16(rsrc_base + 14));

    /* level 1: type */
    const uint32_t n1 = (uint32_t)LD16(rsrc_base + 12) + LD16(rsrc_base + 14);
    for (uint32_t i = 0; i < n1; i++) {
        const uint32_t e = rsrc_base + 16 + i * 8;
        if (getenv("LF2_RSRC_DEBUG"))
            fprintf(stderr, "  type entry %u: name=%08x sub=%08x\n", i, LD32(e), LD32(e + 4));
        if (LD32(e) != RT_BITMAP) continue;
        const uint32_t sub = LD32(e + 4);
        if (!(sub & 0x80000000u)) continue;
        const uint32_t d2 = rsrc_base + (sub & 0x7fffffffu);

        /* level 2: name */
        const uint32_t n2 = (uint32_t)LD16(d2 + 12) + LD16(d2 + 14);
        for (uint32_t j = 0; j < n2; j++) {
            const uint32_t e2 = d2 + 16 + j * 8;
            if (!name_matches(LD32(e2), name)) continue;
            /* Every offset in the resource tree is relative to the section base, not
             * an address. The language level is optional. */
            uint32_t leaf = LD32(e2 + 4);
            if (leaf & 0x80000000u) {                       /* level 3: language */
                const uint32_t d3 = rsrc_base + (leaf & 0x7fffffffu);
                leaf = LD32(d3 + 16 + 4);
            }
            return LD32(rsrc_base + leaf);                  /* data entry -> RVA */
        }
    }
    return 0;
}

/* Build a Bitmap from a DIB already in guest memory. */
static Bitmap *dib_load(uint32_t p)
{
    const uint32_t hdr = LD32(p);                           /* biSize */
    const int32_t w = (int32_t)LD32(p + 4);
    const int32_t h = (int32_t)LD32(p + 8);
    const int bpp = (int)LD16(p + 14);
    const uint32_t clr = LD32(p + 32);
    if (bpp != 8 || w <= 0 || hdr < 40) return NULL;

    const int flip = h > 0;
    const int rows = flip ? h : -h;
    Bitmap *b = SDL_calloc(1, sizeof *b);
    b->w = w; b->h = rows; b->bpp = bpp; b->pitch = w;
    b->pixels = SDL_calloc(1, (size_t)w * (size_t)rows);

    const uint32_t pal = p + hdr;
    const uint32_t ncolours = clr ? clr : 256u;
    for (uint32_t i = 0; i < ncolours && i < 256; i++)
        b->pal[i] = ((uint32_t)LD8(pal + i * 4 + 2) << 16)
                  | ((uint32_t)LD8(pal + i * 4 + 1) << 8) | LD8(pal + i * 4);

    const uint32_t bits = pal + ncolours * 4;
    const uint32_t compression = LD32(p + 16);

    if (compression == 1) {                                 /* BI_RLE8 */
        const uint32_t n = LD32(p + 20) ? LD32(p + 20) : 0x100000u;   /* biSizeImage */
        rle8_decode(g_mem + bits, n, b, flip);
        return b;
    }

    const size_t src_pitch = ((size_t)w + 3u) & ~3u;
    for (int y = 0; y < rows; y++) {
        const uint32_t src = bits + (uint32_t)((size_t)y * src_pitch);
        uint8_t *dst = b->pixels + (size_t)(flip ? rows - 1 - y : y) * (size_t)w;
        for (int x = 0; x < w; x++) dst[x] = LD8(src + (uint32_t)x);
    }
    return b;
}

/* ---- entry points ---- */

/* The size of the last bitmap LoadImage handed back, and how many it has handed back at all.
 * See the comment at the write below (issue #50). */
static int gdi_loaded_w, gdi_loaded_h;
static long gdi_loaded_n;

static void h_LoadImageA(void)
{
    /* LoadImageA(hinst, name, type, cx, cy, fuLoad); LR_LOADFROMFILE = 0x10 */
    const uint32_t name = ARG(1), flags = ARG(5);
    if (!name) { ret_stdcall(6, 0); return; }

    if (getenv("LF2_RSRC_DEBUG"))
        fprintf(stderr, "LoadImage name=%s type=%u flags=%08x\n",
                (const char *)(g_mem + name), ARG(2), flags);

    Bitmap *b = NULL;
    if (flags & 0x10u) {                       /* LR_LOADFROMFILE */
        b = bmp_load(host_path_of(name));
    } else {
        const uint32_t rva = rsrc_find((const char *)(g_mem + name));
        if (rva) b = dib_load(IMAGE_BASE + rva);
    }
    if (!b) {
        /* Expected: the game probes for an override file first so users can replace
         * artwork, then asks for the resource of the same name. Only noisy on request. */
        if (getenv("LF2_RSRC_DEBUG"))
            fprintf(stderr, "LoadImage: cannot load %s\n", (const char *)(g_mem + name));
        ret_stdcall(6, 0);
        return;
    }
    /* Issue #50: the game's picture loader (FUN_0043ed10) goes LoadImage -> GetObject ->
     * CreateSurface(the bitmap's OWN width and height) -> GetDC -> StretchBlt(the surface's
     * width and height, read back with GetSurfaceDesc) -> ReleaseDC. So a surface it creates
     * is a PICTURE HOLDER sized by its content, and the StretchBlt is 1:1 by construction --
     * unless something between the two changes the surface's size, which is exactly what the
     * port's "every surface asked for at 794x550 follows the window" rule did to MENU_WAIT,
     * the 794x550 loading picture. This latch is how dd_CreateSurface tells the two apart:
     * a request whose size is the size of the bitmap that was just loaded came from that
     * loader and must keep it. It is content-derived rather than an address, and it is
     * consumed on the first match so a later coincidence cannot revive it. */
    gdi_loaded_w = b->w; gdi_loaded_h = b->h; gdi_loaded_n++;
    ret_stdcall(6, bitmap_handle(b));
}

/* Reported to ddraw.c. The counter is part of the answer: "no surface matched a loaded
 * bitmap" and "no bitmap was ever loaded" are different failures and a caller that could
 * not distinguish them would be reading a lie. */
int gdi_last_bitmap(int *w, int *h, long *loaded_total)
{
    if (loaded_total) *loaded_total = gdi_loaded_n;
    if (gdi_loaded_w <= 0) return 0;
    if (w) *w = gdi_loaded_w;
    if (h) *h = gdi_loaded_h;
    return 1;
}

void gdi_last_bitmap_consume(void) { gdi_loaded_w = gdi_loaded_h = 0; }

static void h_CreateCompatibleDC(void)
{
    if (ndcs >= (int)MAX_GDI) { ret_stdcall(1, 0); return; }
    dc_bitmap[ndcs] = 0;
    ret_stdcall(1, H_DC + (uint32_t)ndcs++);
}

static void h_SelectObject(void)
{
    const uint32_t hdc = ARG(0), obj = ARG(1);
    const uint32_t i = hdc - H_DC;
    uint32_t prev = 0;
    if (i < (uint32_t)ndcs) {
        prev = dc_bitmap[i];
        if (obj >= H_BITMAP && obj < H_BITMAP + MAX_GDI) dc_bitmap[i] = obj;
    }
    ret_stdcall(2, prev);
}

static void h_GetObjectA(void)
{
    Bitmap *b = bitmap_of(ARG(0));
    const uint32_t out = ARG(2);
    if (b && out && ARG(1) >= 24) {
        ST32(out, 0);                                   /* bmType */
        ST32(out + 4, (uint32_t)b->w);
        ST32(out + 8, (uint32_t)b->h);
        ST32(out + 12, (uint32_t)b->pitch);
        ST32(out + 16, 1);                              /* bmPlanes */
        ST32(out + 20, (uint32_t)b->bpp);
        ret_stdcall(3, 24);
        return;
    }
    ret_stdcall(3, 0);
}

void cursor_find_note(int dl, int dt, const char *via);   /* ddraw.c */

static void h_StretchBlt(void)
{
    LOADPROF_SCOPE(LP_STRETCH);
    /* StretchBlt(hdcDst, x, y, w, h, hdcSrc, sx, sy, sw, sh, rop) */
    const uint32_t hdst = ARG(0), hsrc = ARG(5);
    const int dx = (int)ARG(1), dy = (int)ARG(2), dw = (int)ARG(3), dh = (int)ARG(4);
    const int sx = (int)ARG(6), sy = (int)ARG(7), sw = (int)ARG(8), sh = (int)ARG(9);

    uint32_t dpix; int dwid, dhei, dpitch;
    if (!ddraw_surface_info(hdst, &dpix, &dwid, &dhei, &dpitch) || dw <= 0 || dh <= 0) {
        static long n;
        if (++n % 200 == 1)
            fprintf(stderr, "stretchblt: dest %08x is not a surface (#%ld, %dx%d)\n",
                    hdst, n, dw, dh);
        LOADPROF_END();
        ret_stdcall(11, 0);
        return;
    }
    const uint32_t si = hsrc - H_DC;
    Bitmap *b = (si < (uint32_t)ndcs) ? bitmap_of(dc_bitmap[si]) : NULL;
    if (!b || sw <= 0 || sh <= 0) {
        { static long f; if (++f % 200 == 1) fprintf(stderr, "stretchblt FAILED #%ld src_dc=%08x bitmap=%s\n", f, hsrc, b ? "ok" : "none"); }
        LOADPROF_END(); ret_stdcall(11, 0); return;
    }

    cursor_find_note(dx, dy, "StretchBlt");
    for (int y = 0; y < dh; y++) {
        const int ty = dy + y;
        if (ty < 0 || ty >= dhei) continue;
        const int by = sy + (int)((int64_t)y * sh / dh);
        if (by < 0 || by >= b->h) continue;
        uint32_t *dst = (uint32_t *)(g_mem + dpix + (size_t)ty * (size_t)dpitch);
        const uint8_t *src = b->pixels + (size_t)by * (size_t)b->pitch;
        for (int x = 0; x < dw; x++) {
            const int tx = dx + x;
            if (tx < 0 || tx >= dwid) continue;
            const int bx = sx + (int)((int64_t)x * sw / dw);
            if (bx < 0 || bx >= b->w) continue;
            dst[tx] = b->pal[src[bx]];      /* index -> XRGB via the bitmap's palette */
        }
    }
    if (getenv("LF2_DUMP_SURF")) {
        static int n;
        char path[128];
        /* Relative, and overridable with LF2_DUMP_DIR -- see runtime/ddraw.c. An
         * absolute home path in a committed file is unusable for anyone else. */
        const char *dir = getenv("LF2_DUMP_DIR");
        snprintf(path, sizeof path, "%s/surf_%02d.ppm", (dir && *dir) ? dir : "scratch", n++);
        FILE *f = fopen(path, "wb");
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", dwid, dhei);
            for (int y = 0; y < dhei; y++) {
                const uint32_t *r = (const uint32_t *)(g_mem + dpix + (size_t)y * (size_t)dpitch);
                for (int x = 0; x < dwid; x++) {
                    const uint8_t px[3] = { (uint8_t)(r[x] >> 16), (uint8_t)(r[x] >> 8), (uint8_t)r[x] };
                    fwrite(px, 1, 3, f);
                }
            }
            fclose(f);
        }
    }
    ddraw_surface_present(hdst);
    /* The caller is part of the line because a scaling StretchBlt is a BUG SIGNAL in this port
     * (issue #50) and "which guest call site asked for it" is the first thing wanted next. */
    { static long n; if (getenv("LF2_RSRC_DEBUG"))
        fprintf(stderr, "stretchblt #%ld %dx%d -> %dx%d at (%d,%d) in %dx%d from guest %08x%s\n",
                ++n, sw, sh, dw, dh, dx, dy, dwid, dhei, LD32(R(ESP)),
                (sw == dw && sh == dh) ? "" : "  <== SCALING"); }
    LOADPROF_END();
    ret_stdcall(11, 1);
}

static void h_DeleteObject(void) { ret_stdcall(1, 1); }
static void h_DeleteDC(void)     { ret_stdcall(1, 1); }
static void h_SetBkColor(void)   { ret_stdcall(2, 0); }
static uint32_t text_colour = 0x00ffffffu;   /* COLORREF is 0x00BBGGRR */

static void h_SetTextColor(void)
{
    const uint32_t ref = ARG(1);
    text_colour = ((ref & 0xff) << 16) | (ref & 0xff00) | ((ref >> 16) & 0xff);
    ret_stdcall(2, 0);
}

/* Text, through SDL's built-in debug font.
 *
 * The binary imports no CreateFont, so the game draws with the device context's default
 * font -- there is no font of its own to reproduce here. SDL rasterises an 8x8 face with
 * no font file, so the string is drawn white onto a scratch surface and used as a mask to
 * stamp the current text colour into the DirectDraw surface. That keeps both the colour
 * and the see-through behaviour right without pulling in a font library.
 */
/* ---- text rendering ----
 *
 * The game draws part of every frame through GDI, with the device context's DEFAULT font --
 * it imports no CreateFont, so there is no font of its own to reproduce. On Windows that
 * default is a proportional system face, and the game's layouts were sized against it.
 *
 * THE FACE IS COMPILED INTO THE BINARY (issue #45). assets/fonts/LiberationSans-Regular.ttf,
 * unmodified, SIL OFL 1.1, embedded by CMakeLists.txt as a byte array. There is no font
 * search and no fallback, and the removal of those is the point rather than a simplification:
 * this used to walk nine candidate system paths and, when none matched, quietly drop to SDL's
 * 8x8 debug font. A build on one machine and a build on another then drew different text, and
 * nothing said so -- which makes two screenshots incomparable and a "the text looks wrong"
 * report unanswerable. If the face cannot be opened now, the port says so and draws nothing.
 *
 * Liberation Sans is metrically compatible with Arial, which is what makes it the honest
 * substitute here rather than merely an available one: the advance widths the game's layout
 * was sized against are the ones it gets.
 */
#ifdef LF2_HAVE_TTF
enum { TEXT_PT = 13 };          /* close to the ~11 px Windows default at 96 dpi */

extern const unsigned char lf2_font_sans[];
extern const unsigned int  lf2_font_sans_len;
extern const unsigned char lf2_font_mono[];
extern const unsigned int  lf2_font_mono_len;

/* Opens one of the embedded faces. SDL takes ownership of the stream and closes it with the
 * font; the byte array is static, so nothing is copied and nothing is freed. */
static TTF_Font *font_from_memory(const unsigned char *data, unsigned int len, float pt)
{
    SDL_IOStream *io = SDL_IOFromConstMem(data, len);
    if (!io) return NULL;
    return TTF_OpenFontIO(io, true, pt);
}

/* ONE OPEN FACE PER SIZE, and the size is part of the key. A cache that ignored it would
 * keep handing back the glyphs rasterised for the previous window and the text would stay
 * blurry after a resize while looking as though the feature worked -- the exact failure this
 * cache exists to avoid. Small and fixed: a handful of window sizes in a run, and a face that
 * does not fit is simply not cached rather than evicting anything. */
enum { FONT_SIZES_MAX = 8 };
static struct { int pt; TTF_Font *font; } sized_fonts[FONT_SIZES_MAX];
static int sized_fonts_n;

static TTF_Font *font_at(float pt)
{
    const int key = (int)(pt + 0.5f);
    if (key < 4) return NULL;
    for (int i = 0; i < sized_fonts_n; i++)
        if (sized_fonts[i].pt == key) return sized_fonts[i].font;
    if (sized_fonts_n >= FONT_SIZES_MAX) return NULL;
    TTF_Font *f = font_from_memory(lf2_font_sans, lf2_font_sans_len, (float)key);
    if (!f) return NULL;
    sized_fonts[sized_fonts_n].pt = key;
    sized_fonts[sized_fonts_n].font = f;
    sized_fonts_n++;
    if (getenv("LF2_GLYPH_DEBUG"))
        fprintf(stderr, "text: opened Liberation Sans at %d pt for the window's scale "
                        "(%d size(s) cached)\n", key, sized_fonts_n);
    return f;
}

static TTF_Font *ui_font;
static int ui_font_tried;

static TTF_Font *font_open(void)
{
    if (ui_font_tried) return ui_font;
    ui_font_tried = 1;

    if (!TTF_Init()) {
        fprintf(stderr, "text: TTF_Init failed (%s) -- NO TEXT WILL BE DRAWN. This is not a "
                        "cosmetic fallback; the port has no second way to draw a string.\n",
                SDL_GetError());
        return NULL;
    }
    ui_font = font_from_memory(lf2_font_sans, lf2_font_sans_len, (float)TEXT_PT);
    if (!ui_font)
        fprintf(stderr, "text: the embedded Liberation Sans (%u bytes) would not open (%s) -- "
                        "NO TEXT WILL BE DRAWN.\n", lf2_font_sans_len, SDL_GetError());
    else if (getenv("LF2_GLYPH_DEBUG"))
        fprintf(stderr, "text: embedded Liberation Sans, %u bytes, %d pt\n",
                lf2_font_sans_len, TEXT_PT);
    return ui_font;
}

/* Returns 1 if it drew. Blends by coverage, so the anti-aliased edges sit on whatever the
 * game already drew rather than on a black box. */
static int text_draw_ttf(const char *text, int x, int y,
                         uint32_t dpix, int dwid, int dhei, int dpitch)
{
    TTF_Font *font = font_open();
    if (!font) return 0;

    SDL_Color white = { 255, 255, 255, 255 };
    SDL_Surface *glyphs = TTF_RenderText_Blended(font, text, 0, white);
    if (!glyphs) return 0;
    SDL_Surface *rgba = SDL_ConvertSurface(glyphs, SDL_PIXELFORMAT_ARGB8888);
    SDL_DestroySurface(glyphs);
    if (!rgba) return 0;

    const int tr = (int)((text_colour >> 16) & 0xff);
    const int tg = (int)((text_colour >> 8) & 0xff);
    const int tb = (int)(text_colour & 0xff);

    /* THE TILE IS RASTERISED AT THE WINDOW'S RESOLUTION, NOT THE GAME'S (issue #45).
     *
     * The `rgba` surface above is the string at the game's own 13 pt, and it stays that size
     * because the software compositor below writes into a buffer at the game's resolution --
     * that path cannot use anything finer. But the GPU path draws this tile into a
     * destination that issue #41 scales by the window, so handing it the small raster is what
     * made text the one thing in the frame that did not get sharper as the window grew: an
     * 8-pixel-tall glyph magnified 1.96x is an 8-pixel-tall glyph with bigger pixels.
     *
     * So the string is rasterised a SECOND time at TEXT_PT * scale and handed over as more
     * texels for the SAME destination rectangle. The outline is re-hinted at that size, which
     * is the whole difference between a vector font and a bitmap one -- the curve is
     * re-evaluated rather than the pixels stretched.
     *
     * Its destination stays the game's own w/h, so the game's layout is untouched: nothing
     * about where the string sits or how much room it takes depends on the window. */
    const float wscale = lf2_world_scale();
    SDL_Surface *hi = NULL;
    if (wscale > 1.01f) {
        TTF_Font *big = font_at(TEXT_PT * wscale);
        if (big) {
            SDL_Surface *g2 = TTF_RenderText_Blended(big, text, 0, white);
            if (g2) {
                hi = SDL_ConvertSurface(g2, SDL_PIXELFORMAT_ARGB8888);
                SDL_DestroySurface(g2);
            }
        }
    }
    const SDL_Surface *tsrc = hi ? hi : rgba;
    uint32_t *tile = render_tile_begin(dpix, x, y, rgba->w, rgba->h, tsrc->w, tsrc->h);
    if (tile) {
        for (int ty = 0; ty < tsrc->h; ty++) {
            const uint32_t *src = (const uint32_t *)((const uint8_t *)tsrc->pixels
                                                     + (size_t)ty * (size_t)tsrc->pitch);
            for (int tx = 0; tx < tsrc->w; tx++) {
                const int a = (int)(src[tx] >> 24);
                if (!a) continue;
                tile[ty * tsrc->w + tx] = ((uint32_t)a << 24)
                                        | ((uint32_t)(tr * a / 255) << 16)
                                        | ((uint32_t)(tg * a / 255) << 8)
                                        | (uint32_t)(tb * a / 255);
            }
        }
        render_tile_end();
    }
    if (hi) SDL_DestroySurface(hi);

    for (int ty = 0; ty < rgba->h; ty++) {
        const int dy = y + ty;
        if (dy < 0 || dy >= dhei) continue;
        const uint32_t *src = (const uint32_t *)((const uint8_t *)rgba->pixels
                                                 + (size_t)ty * (size_t)rgba->pitch);
        uint32_t *dst = (uint32_t *)(g_mem + dpix + (size_t)dy * (size_t)dpitch);
        for (int tx = 0; tx < rgba->w; tx++) {
            const int dx = x + tx;
            if (dx < 0 || dx >= dwid) continue;
            const int a = (int)(src[tx] >> 24);
            if (!a) continue;
            const uint32_t bg = dst[dx];
            const int br = (int)((bg >> 16) & 0xff), bgc = (int)((bg >> 8) & 0xff);
            const int bb = (int)(bg & 0xff);
            const int r = (tr * a + br * (255 - a)) / 255;
            const int g = (tg * a + bgc * (255 - a)) / 255;
            const int b = (tb * a + bb * (255 - a)) / 255;
            dst[dx] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
    SDL_DestroySurface(rgba);
    return 1;
}
#endif /* LF2_HAVE_TTF */

/* ---- the game's own 8x16 text ----
 *
 * Every string the game draws itself -- name tags, the status line, screen headings --
 * goes out as one blit per glyph from a fixed-pitch bitmap sheet, with the character code
 * as the clip index. The caller advances the pen 8 pixels regardless of what was drawn,
 * so the glyph SHAPE can be replaced without touching a single layout calculation.
 *
 * That constrains the choice: the replacement has to be MONOSPACE and sized so its advance
 * is at most 8 pixels, or characters overlap. A proportional face is not an option here,
 * which is why this is a separate font from the one the GDI path uses.
 *
 * Glyphs are cached per (character, colour). The game draws 20,000+ of them in a run and
 * rendering each one through TTF every time would be visible.
 */
#ifdef LF2_HAVE_TTF
enum { GLYPH_W = 8, GLYPH_H = 16 };

static TTF_Font *mono_font;
static int mono_tried, mono_baseline;
static int mono_pt;   /* the size measured to fit the cell; the scaled raster multiplies it */

static TTF_Font *mono_open(void)
{
    if (mono_tried) return mono_font;
    mono_tried = 1;
    if (!TTF_Init()) return NULL;

    /* THE EMBEDDED MONOSPACE FACE (issue #45), for the same reason as the proportional one
     * above: no search, no host dependency, no silent difference between two machines. It is
     * a SECOND face and not the same one, because this text is drawn on a fixed 8-pixel cell
     * and a proportional face put in that cell clips.
     *
     * The size is still MEASURED rather than assumed -- the largest whose advance fits the
     * cell -- because that ratio is a property of the face and guessing it wrong makes every
     * glyph overlap its neighbour. It is measured against the one face now, so it is a
     * constant of this build rather than of whatever was installed. */
    for (int pt = 20; pt >= 8 && !mono_font; pt--) {
        TTF_Font *f = font_from_memory(lf2_font_mono, lf2_font_mono_len, (float)pt);
        if (!f) break;
        int adv = 0;
        if (TTF_GetGlyphMetrics(f, 'M', NULL, NULL, NULL, NULL, &adv) && adv <= GLYPH_W) {
            mono_font = f;
            mono_pt = pt;
            mono_baseline = (GLYPH_H - TTF_GetFontHeight(f)) / 2 + TTF_GetFontAscent(f);
            if (getenv("LF2_GLYPH_DEBUG"))
                fprintf(stderr, "glyph font: embedded Liberation Mono, %d pt, advance %d px, "
                                "baseline %d\n", pt, adv, mono_baseline);
            break;
        }
        TTF_CloseFont(f);
    }
    if (!mono_font)
        fprintf(stderr, "glyph font: the embedded Liberation Mono fits no size into an %d px "
                        "cell, so the game's own bitmap font is drawn instead. That should be "
                        "impossible with a committed face and means this build is broken.\n",
                (int)GLYPH_W);
    return mono_font;
}

/* The fixed-cell face at `scale` times its measured size. Same size-keyed shape as the
 * proportional face; a face that will not open simply leaves the caller on the 8x16 mask. */
static struct { int pt; TTF_Font *font; } mono_sized[FONT_SIZES_MAX];
static int mono_sized_n;

static TTF_Font *mono_at(float scale)
{
    if (!mono_open() || mono_pt <= 0) return NULL;
    const int key = (int)((float)mono_pt * scale + 0.5f);
    for (int i = 0; i < mono_sized_n; i++)
        if (mono_sized[i].pt == key) return mono_sized[i].font;
    if (mono_sized_n >= FONT_SIZES_MAX) return NULL;
    TTF_Font *f = font_from_memory(lf2_font_mono, lf2_font_mono_len, (float)key);
    if (!f) return NULL;
    mono_sized[mono_sized_n].pt = key;
    mono_sized[mono_sized_n].font = f;
    mono_sized_n++;
    return f;
}

/* One cached 8x16 coverage mask per character. Colour is applied at blit time, so a glyph
 * drawn in two colours is still rendered once. */
typedef struct { uint8_t cov[GLYPH_W * GLYPH_H]; int valid; } Glyph;
static Glyph glyph_cache[128];

/* ---- THE SAME GLYPH AGAIN, AT THE WINDOW'S RESOLUTION (issue #45) ----
 *
 * The 8x16 mask above is what the software compositor needs, because that path writes into a
 * buffer at the game's own resolution and can use nothing finer. The GPU path draws the same
 * glyph into a destination that issue #41 scales by the window, so it gets its own raster at
 * cell * scale -- the outline re-hinted at that size rather than eight rows of pixels
 * magnified. This is the larger of the port's two text paths: it carries the HUD, the name
 * tags, "Character Selection", the pause menu and the controls hint.
 *
 * KEYED ON THE SCALE, and that is not bookkeeping: without it a resize would keep showing the
 * glyphs rasterised for the old window, which looks exactly like the feature working until
 * someone drags an edge. The whole cache is dropped when the scale changes rather than kept
 * per size -- a run has one window at a time, and 95 glyphs are a millisecond to redo. */
enum { HI_MAX = GLYPH_W * 8 * GLYPH_H * 8 };
typedef struct { uint8_t *cov; int w, h; int valid; } GlyphHi;
static GlyphHi glyph_hi[128];
static int     glyph_hi_scale_x100 = -1;
static long    glyph_hi_rasterised, glyph_hi_dropped;

static void glyph_hi_reset(int scale_x100)
{
    for (int i = 0; i < 128; i++) {
        SDL_free(glyph_hi[i].cov);
        glyph_hi[i].cov = NULL;
        glyph_hi[i].valid = 0;
    }
    if (glyph_hi_scale_x100 >= 0) glyph_hi_dropped++;
    glyph_hi_scale_x100 = scale_x100;
}

/* The coverage for `ch` at the current world scale, or NULL when there is nothing finer to
 * draw (scale 1, no face, or the raster failed). NULL means "use the 8x16 mask", which is
 * always correct -- it is the old behaviour, not a blank. */
static const GlyphHi *glyph_hi_of(int ch, float scale)
{
    /* Not when the GPU path is off: the finer raster exists ONLY for the display-list tile,
     * and render_tile_begin returns NULL under LF2_RENDERER=soft. Rasterising it anyway cost
     * the work for nothing AND made the report claim glyphs had been drawn at 1.96x in a run
     * whose text was, necessarily, the 8x16 cell -- a true sentence that answered a question
     * nobody asked and read as a pass. */
    if (ch < 32 || ch > 126 || scale <= 1.01f || !render_gpu_enabled()) return NULL;
    const int key = (int)(scale * 100.0f + 0.5f);
    if (key != glyph_hi_scale_x100) glyph_hi_reset(key);

    GlyphHi *g = &glyph_hi[ch];
    if (g->valid) return g->cov ? g : NULL;
    g->valid = 1;

    const int cw = (int)((float)GLYPH_W * scale + 0.5f);
    const int chh = (int)((float)GLYPH_H * scale + 0.5f);
    if (cw <= 0 || chh <= 0 || cw * chh > HI_MAX) return NULL;

    TTF_Font *font = mono_at(scale);
    if (!font) return NULL;

    const char text[2] = { (char)ch, 0 };
    SDL_Color white = { 255, 255, 255, 255 };
    SDL_Surface *surf = TTF_RenderText_Blended(font, text, 1, white);
    if (!surf) return NULL;
    SDL_Surface *rgba = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_ARGB8888);
    SDL_DestroySurface(surf);
    if (!rgba) return NULL;

    g->cov = SDL_calloc(1, (size_t)cw * (size_t)chh);
    g->w = cw; g->h = chh;
    if (!g->cov) { SDL_DestroySurface(rgba); return NULL; }

    /* The baseline is the game's own, scaled -- the glyph must sit on the same line the 8x16
     * mask puts it on, or scaled text drifts vertically against the art around it. */
    const int top = (int)((float)mono_baseline * scale + 0.5f) - TTF_GetFontAscent(font);
    for (int y = 0; y < rgba->h; y++) {
        const int cy = y + top;
        if (cy < 0 || cy >= chh) continue;
        const uint32_t *row = (const uint32_t *)((const uint8_t *)rgba->pixels
                                                 + (size_t)y * (size_t)rgba->pitch);
        for (int x = 0; x < rgba->w && x < cw; x++)
            g->cov[cy * cw + x] = (uint8_t)(row[x] >> 24);
    }
    SDL_DestroySurface(rgba);
    glyph_hi_rasterised++;
    return g;
}

/* LF2_GLYPH_DEBUG also answers the question this feature is judged on: was anything actually
 * rasterised at the window's size, or did every glyph fall back to the 8x16 cell? A zero here
 * with a scale above 1 is the feature not working, and it says so rather than printing
 * nothing. */
void glyph_scale_report(void)
{
    if (!getenv("LF2_GLYPH_DEBUG")) return;
    const float s = lf2_world_scale();
    if (!render_gpu_enabled())
        fprintf(stderr, "glyph scale: the software compositor was presenting, so text is the "
                        "game's 8x16 cell by definition -- this run says NOTHING about "
                        "window-resolution text.\n");
    else if (glyph_hi_scale_x100 < 0)
        fprintf(stderr, "glyph scale: NOTHING was rasterised above the game's 8x16 cell "
                        "(world scale %.3f). At a scale of 1 that is correct; above it, the "
                        "text is not getting sharper and the feature is not working.\n",
                (double)s);
    else
        fprintf(stderr, "glyph scale: %ld glyph(s) rasterised at %.2fx the game's cell, "
                        "%ld cache drop(s) from the window changing size\n",
                glyph_hi_rasterised, glyph_hi_scale_x100 / 100.0, glyph_hi_dropped);
}

static const Glyph *glyph_of(int ch)
{
    if (ch < 32 || ch > 126) return NULL;         /* CJK and control codes: not ours */
    Glyph *g = &glyph_cache[ch];
    if (g->valid) return g;

    TTF_Font *font = mono_open();
    if (!font) return NULL;

    const char text[2] = { (char)ch, 0 };
    SDL_Color white = { 255, 255, 255, 255 };
    SDL_Surface *surf = TTF_RenderText_Blended(font, text, 1, white);
    if (!surf) { g->valid = 1; return g; }        /* blank, but do not retry every frame */
    SDL_Surface *rgba = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_ARGB8888);
    SDL_DestroySurface(surf);
    if (!rgba) { g->valid = 1; return g; }

    const int top = mono_baseline - TTF_GetFontAscent(font);
    for (int y = 0; y < rgba->h; y++) {
        const int cy = y + top;
        if (cy < 0 || cy >= GLYPH_H) continue;
        const uint32_t *row = (const uint32_t *)((const uint8_t *)rgba->pixels
                                                 + (size_t)y * (size_t)rgba->pitch);
        for (int x = 0; x < rgba->w && x < GLYPH_W; x++)
            g->cov[cy * GLYPH_W + x] = (uint8_t)(row[x] >> 24);
    }
    SDL_DestroySurface(rgba);
    g->valid = 1;
    return g;
}

/* Returns 1 if it drew the glyph, 0 to let the game's bitmap copy proceed. */
int game_glyph_draw(int ch, int x, int y, uint32_t ink,
                    uint32_t dpix, int dwid, int dhei, int dpitch)
{
    const Glyph *g = glyph_of(ch);
    if (!g) return 0;

    const int ir = (int)((ink >> 16) & 0xff), ig = (int)((ink >> 8) & 0xff);
    const int ib = (int)(ink & 0xff);

    /* The native renderer does not see this draw -- it does not go through Blt -- so the
     * same coverage is written into a display-list TILE as well, PREMULTIPLIED. The CPU
     * blend below still runs: both renderers build every frame, and the software one is the
     * reference the GPU path is diffed against. */
    /* The GPU tile takes the WINDOW-RESOLUTION raster when there is one, into the same 8x16
     * destination -- more texels for the same place on screen, which is what makes the glyph
     * sharper instead of merely bigger (issue #45). At scale 1, or if the finer raster could
     * not be made, this is the 8x16 mask and the behaviour is exactly what it was. */
    const GlyphHi *hi = glyph_hi_of(ch, lf2_world_scale());
    const uint8_t *cov = hi ? hi->cov : g->cov;
    const int cw = hi ? hi->w : GLYPH_W, chh = hi ? hi->h : GLYPH_H;
    uint32_t *tile = render_tile_begin(dpix, x, y, GLYPH_W, GLYPH_H, cw, chh);
    if (tile) {
        for (int gy = 0; gy < chh; gy++)
            for (int gx = 0; gx < cw; gx++) {
                const int a = cov[gy * cw + gx];
                if (!a) continue;
                tile[gy * cw + gx] = ((uint32_t)a << 24)
                                   | ((uint32_t)(ir * a / 255) << 16)
                                   | ((uint32_t)(ig * a / 255) << 8)
                                   | (uint32_t)(ib * a / 255);
            }
        render_tile_end();
    }

    for (int gy = 0; gy < GLYPH_H; gy++) {
        const int dy = y + gy;
        if (dy < 0 || dy >= dhei) continue;
        uint32_t *dst = (uint32_t *)(g_mem + dpix + (size_t)dy * (size_t)dpitch);
        for (int gx = 0; gx < GLYPH_W; gx++) {
            const int a = g->cov[gy * GLYPH_W + gx];
            if (!a) continue;
            const int dx = x + gx;
            if (dx < 0 || dx >= dwid) continue;
            const uint32_t bg = dst[dx];
            const int br = (int)((bg >> 16) & 0xff), bgc = (int)((bg >> 8) & 0xff);
            const int bb = (int)(bg & 0xff);
            dst[dx] = ((uint32_t)((ir * a + br * (255 - a)) / 255) << 16)
                    | ((uint32_t)((ig * a + bgc * (255 - a)) / 255) << 8)
                    | (uint32_t)((ib * a + bb * (255 - a)) / 255);
        }
    }
    return 1;
}
#else
int game_glyph_draw(int ch, int x, int y, uint32_t ink,
                    uint32_t dpix, int dwid, int dhei, int dpitch)
{
    (void)ch; (void)x; (void)y; (void)ink;
    (void)dpix; (void)dwid; (void)dhei; (void)dpitch;
    return 0;
}
#endif

static void h_TextOutA(void)
{
    const uint32_t hdc = ARG(0), str = ARG(3);
    int x = (int)ARG(1);
    const int y = (int)ARG(2);
    int len = (int)ARG(4);

    uint32_t dpix; int dwid, dhei, dpitch;
    if (!ddraw_surface_info(hdc, &dpix, &dwid, &dhei, &dpitch) || len <= 0 || !str) {
        ret_stdcall(5, 1);
        return;
    }
    /* Widescreen: text inside the in-match HUD band moves with the panels it sits on. GDI
     * writes straight into the surface, so it misses the offset the blit path applies --
     * which left the game's own readout stranded at the left edge once the HUD was centred,
     * where it had previously been hidden behind the panel entirely. */
    x += hud_offset_x(dwid, y + 16);
    /* And text on a fixed-width SCREEN moves with that screen, for the same reason and since
     * issue #42 for a second one: the centring used to be applied to the whole composition on
     * its way to the primary, which carried GDI text along with it for free. It is applied
     * while composing now -- so that the front end's backdrop can fill the window while its
     * content stays centred -- and a path that does not go through Blt has to ask for it. */
    if (dwid > 794) x += screen_offset_x();
    if (len > 512) len = 512;

    char text[513];
    for (int i = 0; i < len; i++) text[i] = (char)LD8(str + (uint32_t)i);
    text[len] = 0;

    /* Every string the game draws, with where it drew it. This is how the leftover "Update
     * on" in the menu corner was traced back to the ad updater. */
    if (getenv("LF2_TEXT_DEBUG"))
        fprintf(stderr, "text (%d,%d) %d %s\n", x, y, len, text);

#ifdef LF2_HAVE_TTF
    if (text_draw_ttf(text, x, y, dpix, dwid, dhei, dpitch)) {
        ddraw_surface_present(hdc);
        ret_stdcall(5, 1);
        return;
    }
#endif

    const int w = len * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
    const int h = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
    SDL_Surface *scratch = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_XRGB8888);
    if (!scratch) { ret_stdcall(5, 1); return; }
    SDL_Renderer *r = SDL_CreateSoftwareRenderer(scratch);
    if (!r) { SDL_DestroySurface(scratch); ret_stdcall(5, 1); return; }

    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderDebugText(r, 0, 0, text);
    SDL_RenderPresent(r);

    /* Advance per glyph by its own ink width, not by the font's fixed 8-pixel cell.
     *
     * The game draws with the device context's default font, which on Windows is
     * proportional, and it sizes its own layout accordingly. Blitting SDL's fixed-pitch
     * debug font straight out overruns that layout: the main menu's copyright line came
     * out as "by Marti Wong, Starsky Won", clipped at the surface edge, where the same
     * screen under Wine reads "...Starsky Wong" and two further lines in full.
     *
     * The widths are measured from the rendered glyphs rather than tabulated, so there is
     * nothing to keep in step with the font. */
    const int cell = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
    int pen = x;
    for (int i = 0; i < len; i++) {
        int ink_l = cell, ink_r = -1;
        for (int ty = 0; ty < h; ty++) {
            const uint32_t *row = (const uint32_t *)((const uint8_t *)scratch->pixels
                                                     + (size_t)ty * (size_t)scratch->pitch);
            for (int cx = 0; cx < cell; cx++)
                if (row[i * cell + cx] & 0x00ffffffu) {
                    if (cx < ink_l) ink_l = cx;
                    if (cx > ink_r) ink_r = cx;
                }
        }
        if (ink_r < 0) { pen += cell / 2; continue; }        /* blank: half-cell space */

        for (int ty = 0; ty < h; ty++) {
            const int dy = y + ty;
            if (dy < 0 || dy >= dhei) continue;
            uint32_t *dst = (uint32_t *)(g_mem + dpix + (size_t)dy * (size_t)dpitch);
            const uint32_t *row = (const uint32_t *)((const uint8_t *)scratch->pixels
                                                     + (size_t)ty * (size_t)scratch->pitch);
            for (int cx = ink_l; cx <= ink_r; cx++) {
                const int dx = pen + (cx - ink_l);
                if (dx < 0 || dx >= dwid) continue;
                if (row[i * cell + cx] & 0x00ffffffu) dst[dx] = text_colour;
            }
        }
        pen += (ink_r - ink_l) + 2;                          /* one pixel of side bearing */
    }

    SDL_DestroyRenderer(r);
    SDL_DestroySurface(scratch);
    ddraw_surface_present(hdc);
    ret_stdcall(5, 1);
}

typedef void (*Handler)(void);

Handler gdi_lookup(const char *dll, const char *name)
{
    static const struct { const char *dll, *name; Handler fn; } T[] = {
        { "GDI32.dll", "CreateCompatibleDC", h_CreateCompatibleDC },
        { "GDI32.dll", "DeleteDC",           h_DeleteDC },
        { "GDI32.dll", "DeleteObject",       h_DeleteObject },
        { "GDI32.dll", "SelectObject",       h_SelectObject },
        { "GDI32.dll", "GetObjectA",         h_GetObjectA },
        { "GDI32.dll", "SetBkColor",         h_SetBkColor },
        { "GDI32.dll", "SetTextColor",       h_SetTextColor },
        { "GDI32.dll", "TextOutA",           h_TextOutA },
        { "GDI32.dll", "StretchBlt",         h_StretchBlt },
        { "USER32.dll", "LoadImageA",        h_LoadImageA },
    };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++)
        if (strcmp(T[i].dll, dll) == 0 && strcmp(T[i].name, name) == 0) return T[i].fn;
    return NULL;
}

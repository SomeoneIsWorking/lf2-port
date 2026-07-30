/* GDI, enough for how the game loads sprites.
 *
 * The game builds each sprite surface by: LoadImage the BMP, CreateCompatibleDC,
 * SelectObject the bitmap, DirectDraw GetDC on the surface, StretchBlt the bitmap in,
 * ReleaseDC, then SetColorKey. Stubbing StretchBlt left every surface blank and the
 * game took a different path from the real one -- the trace harness showed exactly
 * that. Surfaces and the game's BMPs are both 8-bit indexed, so the copy is a plain
 * index copy with no colour conversion.
 */
#include "com.h"
#include "guest_ops.h"
#include "hostwin.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

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
    ret_stdcall(6, bitmap_handle(b));
}

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

static void h_StretchBlt(void)
{
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
        ret_stdcall(11, 0);
        return;
    }
    const uint32_t si = hsrc - H_DC;
    Bitmap *b = (si < (uint32_t)ndcs) ? bitmap_of(dc_bitmap[si]) : NULL;
    if (!b || sw <= 0 || sh <= 0) {
        { static long f; if (++f % 200 == 1) fprintf(stderr, "stretchblt FAILED #%ld src_dc=%08x bitmap=%s\n", f, hsrc, b ? "ok" : "none"); }
        ret_stdcall(11, 0); return;
    }

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
        snprintf(path, sizeof path, "./scratch/surf_%02d.ppm", n++);
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
    { static long n; if (getenv("LF2_RSRC_DEBUG")) fprintf(stderr, "stretchblt #%ld %dx%d -> %dx%d\n", ++n, sw, sh, dw, dh); }
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
static void h_TextOutA(void)
{
    const uint32_t hdc = ARG(0), str = ARG(3);
    const int x = (int)ARG(1), y = (int)ARG(2);
    int len = (int)ARG(4);

    uint32_t dpix; int dwid, dhei, dpitch;
    if (!ddraw_surface_info(hdc, &dpix, &dwid, &dhei, &dpitch) || len <= 0 || !str) {
        ret_stdcall(5, 1);
        return;
    }
    if (len > 512) len = 512;

    char text[513];
    for (int i = 0; i < len; i++) text[i] = (char)LD8(str + (uint32_t)i);
    text[len] = 0;

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

    for (int ty = 0; ty < h; ty++) {
        const int dy = y + ty;
        if (dy < 0 || dy >= dhei) continue;
        uint32_t *dst = (uint32_t *)(g_mem + dpix + (size_t)dy * (size_t)dpitch);
        const uint32_t *row = (const uint32_t *)((const uint8_t *)scratch->pixels
                                                 + (size_t)ty * (size_t)scratch->pitch);
        for (int tx = 0; tx < w; tx++) {
            const int dx = x + tx;
            if (dx < 0 || dx >= dwid) continue;
            if (row[tx] & 0x00ffffffu) dst[dx] = text_colour;
        }
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

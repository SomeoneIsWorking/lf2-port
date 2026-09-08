/* Bitmap file/resource decoding shared by GDI image-loading entry points. */
#include "gdi_bitmap.h"
#include "guest.h"
#include "environment.h"
#include "lf2_log.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

/* RLE8, which is what the game's bitmaps actually use -- their headers declare far more
 * pixels than the file contains, and reading them as raw rows yields garbage that then
 * runs out partway down.
 *
 * Pairs of (count, value): a non-zero count repeats value. A zero count is an escape --
 * 0 ends the line, 1 ends the bitmap, 2 is a delta, and 3 or more introduces that many
 * literal bytes padded to a word boundary. Output is written top-down here; the caller
 * has already accounted for the bottom-up flip.
 *
 * Runs move through memset/memcpy with the destination row pointer kept across tokens,
 * rather than through a bounds check and two multiplications per pixel. The token state
 * machine -- including its quirks (a count run stops at the line's end, a literal run
 * consumes and advances x past it, the pad follows an odd literal) -- is untouched. */
static void rle8_decode(const uint8_t *src, size_t n, Bitmap *b, int flip)
{
    /* The decoded row `flip ? b->h - 1 - y : y`, refreshed whenever y moves. */
    uint8_t *row = b->pixels + (size_t)(flip ? b->h - 1 : 0) * (size_t)b->pitch;
    size_t i = 0;
    int x = 0, y = 0;
    while (i + 1 < n && y < b->h) {
        const uint8_t count = src[i], value = src[i + 1];
        i += 2;
        if (count) {
            int room = b->w - x;
            if (room <= 0) continue; /* past the line's end the run is dropped, as before */
            if (room > count) room = count;
            memset(row + x, value, (size_t)room);
            x += room;
            continue;
        }
        if (value == 0) {
            x = 0;
            y++;
            row += flip ? -(ptrdiff_t)b->pitch : (ptrdiff_t)b->pitch;
            continue;
        } /* end of line */
        if (value == 1) break; /* end of bitmap */
        if (value == 2) {      /* delta */
            if (i + 1 >= n) break;
            x += src[i];
            y += src[i + 1];
            row = b->pixels + (size_t)(flip ? b->h - 1 - y : y) * (size_t)b->pitch;
            i += 2;
            continue;
        }
        const size_t have = n - i < (size_t)value ? n - i : (size_t)value;
        int room = b->w - x;
        if (room > (int)have) room = (int)have;
        if (room > 0) memcpy(row + x, src + i, (size_t)room);
        i += have;
        x += (int)have;
        if (value & 1) i++; /* pad to a word */
    }
}

/* Load a Windows BMP. Rows are stored bottom-up and padded to four bytes. */
Bitmap *bitmap_load_file(const char *path)
{
    FILE *fh = fopen(path, "rb");
    if (!fh) return NULL;
    uint8_t hdr[54];
    if (fread(hdr, 1, sizeof hdr, fh) != sizeof hdr || hdr[0] != 'B' || hdr[1] != 'M') {
        fclose(fh);
        return NULL;
    }
    const uint32_t data_off =
        (uint32_t)hdr[10] | ((uint32_t)hdr[11] << 8) | ((uint32_t)hdr[12] << 16) | ((uint32_t)hdr[13] << 24);
    const int32_t w =
        (int32_t)((uint32_t)hdr[18] | ((uint32_t)hdr[19] << 8) | ((uint32_t)hdr[20] << 16) | ((uint32_t)hdr[21] << 24));
    const int32_t h =
        (int32_t)((uint32_t)hdr[22] | ((uint32_t)hdr[23] << 8) | ((uint32_t)hdr[24] << 16) | ((uint32_t)hdr[25] << 24));
    const int bpp = (int)((uint32_t)hdr[28] | ((uint32_t)hdr[29] << 8));
    const uint32_t compression =
        (uint32_t)hdr[30] | ((uint32_t)hdr[31] << 8) | ((uint32_t)hdr[32] << 16) | ((uint32_t)hdr[33] << 24);
    const uint32_t clr_used =
        (uint32_t)hdr[46] | ((uint32_t)hdr[47] << 8) | ((uint32_t)hdr[48] << 16) | ((uint32_t)hdr[49] << 24);
    if (bpp != 8 || w <= 0) {
        fclose(fh);
        return NULL;
    }

    const int flip = h > 0; /* positive height means bottom-up */
    const int rows = flip ? h : -h;

    Bitmap *b = SDL_calloc(1, sizeof *b);
    b->w = w;
    b->h = rows;
    b->bpp = bpp;
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

    if (compression == 1) { /* BI_RLE8 */
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
static uint32_t rsrc_find(const char *name, uint32_t *size)
{
    const uint32_t pe = LD32(IMAGE_BASE + 0x3C) + IMAGE_BASE;
    const uint32_t dir_rva = LD32(pe + 24 + 112); /* data directory 2 */
    if (!dir_rva) return 0;
    rsrc_base = IMAGE_BASE + dir_rva;

    if (lf2_environment_get(LF2_ENV_RSRC_DEBUG))
        lf2_log_writef(LF2_LOG_INFO, "gdi", "rsrc base=%08x named=%d id=%d\n", rsrc_base, LD16(rsrc_base + 12),
                       LD16(rsrc_base + 14));

    /* level 1: type */
    const uint32_t n1 = (uint32_t)LD16(rsrc_base + 12) + LD16(rsrc_base + 14);
    for (uint32_t i = 0; i < n1; i++) {
        const uint32_t e = rsrc_base + 16 + i * 8;
        if (lf2_environment_get(LF2_ENV_RSRC_DEBUG))
            lf2_log_writef(LF2_LOG_INFO, "gdi", "  type entry %u: name=%08x sub=%08x\n", i, LD32(e), LD32(e + 4));
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
            if (leaf & 0x80000000u) { /* level 3: language */
                const uint32_t d3 = rsrc_base + (leaf & 0x7fffffffu);
                leaf = LD32(d3 + 16 + 4);
            }
            *size = LD32(rsrc_base + leaf + 4);
            return LD32(rsrc_base + leaf); /* data entry -> RVA */
        }
    }
    return 0;
}

/* Build a Bitmap from a DIB already in guest memory. */
static Bitmap *dib_load(uint32_t p, uint32_t size)
{
    if (size < 40) return NULL;
    guest_pointer(p, size);
    const uint32_t hdr = LD32(p); /* biSize */
    const int32_t w = (int32_t)LD32(p + 4);
    const int32_t h = (int32_t)LD32(p + 8);
    const int bpp = (int)LD16(p + 14);
    const uint32_t clr = LD32(p + 32);
    if (bpp != 8 || w <= 0 || h == 0 || h == INT32_MIN || hdr < 40 || hdr > size) return NULL;
    const uint32_t ncolours = clr ? clr : 256u;
    if (ncolours > 256 || ncolours > (size - hdr) / 4) return NULL;
    const uint32_t payload = size - hdr - ncolours * 4;
    const uint32_t compression = LD32(p + 16);
    const uint32_t declared_size = LD32(p + 20);
    if (compression == 1 && declared_size > payload) return NULL;

    const int flip = h > 0;
    const int rows = flip ? h : -h;
    const size_t src_pitch = ((size_t)w + 3u) & ~3u;
    if (compression != 1 && (uint64_t)src_pitch * (uint32_t)rows > payload) return NULL;
    Bitmap *b = SDL_calloc(1, sizeof *b);
    b->w = w;
    b->h = rows;
    b->bpp = bpp;
    b->pitch = w;
    b->pixels = SDL_calloc(1, (size_t)w * (size_t)rows);

    const uint32_t pal = p + hdr;
    for (uint32_t i = 0; i < ncolours && i < 256; i++)
        b->pal[i] = ((uint32_t)LD8(pal + i * 4 + 2) << 16) | ((uint32_t)LD8(pal + i * 4 + 1) << 8) | LD8(pal + i * 4);

    const uint32_t bits = pal + ncolours * 4;
    if (compression == 1) { /* BI_RLE8 */
        const uint32_t n = declared_size ? declared_size : payload;
        rle8_decode(guest_pointer(bits, n), n, b, flip);
        return b;
    }

    for (int y = 0; y < rows; y++) {
        const uint32_t src = bits + (uint32_t)((size_t)y * src_pitch);
        uint8_t *dst = b->pixels + (size_t)(flip ? rows - 1 - y : y) * (size_t)w;
        for (int x = 0; x < w; x++) dst[x] = LD8(src + (uint32_t)x);
    }
    return b;
}

Bitmap *bitmap_load_resource(const char *name)
{
    uint32_t size = 0;
    const uint32_t rva = rsrc_find(name, &size);
    return rva ? dib_load(IMAGE_BASE + rva, size) : NULL;
}

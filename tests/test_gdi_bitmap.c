#include "gdi_bitmap.h"
#include "guest.h"

#include <SDL3/SDL.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

uint32_t g_rwatch_lo, g_rwatch_hi;
static unsigned invalidations;
void lf2_jit_invalidate(uint32_t address, uint32_t size)
{
    assert(address >= 0x400000 && size > 0);
    ++invalidations;
}
void rwatch_hit(uint32_t address)
{
    (void)address;
    assert(0 && "read watch is disabled in this fixture");
}

static void release_bitmap(Bitmap *bitmap)
{
    SDL_free(bitmap->pixels);
    SDL_free(bitmap);
}

static void check_bitmap(Bitmap *bitmap)
{
    assert(bitmap);
    assert(bitmap->w == 1 && bitmap->h == 1 && bitmap->bpp == 8);
    assert(bitmap->pixels[0] == 1);
    assert(bitmap->pal[1] == 0x123456);
    release_bitmap(bitmap);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    enum { BASE = 0x400000, RESOURCE = BASE + 0x100, DIB = BASE + 0x1000, SIZE = 52 };
    guest_memory_init();
    guest_memory_map(BASE, 0x2000);
    ST32(BASE + 0x3c, 0x80);
    ST32(BASE + 0x80 + 24 + 112, 0x100);
    ST16(RESOURCE + 14, 1);
    ST32(RESOURCE + 16, 2);
    ST32(RESOURCE + 20, 0x80000020);
    ST16(RESOURCE + 0x20 + 12, 1);
    ST32(RESOURCE + 0x20 + 16, 0x80000080);
    ST32(RESOURCE + 0x20 + 20, 0x60);
    ST16(RESOURCE + 0x80, 1);
    ST16(RESOURCE + 0x82, 'A');
    ST32(RESOURCE + 0x60, 0x1000);
    ST32(RESOURCE + 0x64, SIZE);
    ST32(DIB, 40);
    ST32(DIB + 4, 1);
    ST32(DIB + 8, 1);
    ST16(DIB + 12, 1);
    ST16(DIB + 14, 8);
    ST32(DIB + 16, 1);
    ST32(DIB + 20, 4);
    ST32(DIB + 32, 2);
    ST32(DIB + 44, 0x123456);
    const uint8_t rle[] = {1, 1, 0, 1};
    memcpy(guest_write_pointer(DIB + 48, sizeof rle), rle, sizeof rle);
    check_bitmap(bitmap_load_resource("A"));
    assert(!bitmap_load_resource("missing"));

    /* The resource leaf bounds a zero biSizeImage; no guessed borrow extends
       past the resource into the surrounding PE image. */
    ST32(DIB + 20, 0);
    check_bitmap(bitmap_load_resource("A"));
    ST32(DIB + 20, 5);
    assert(!bitmap_load_resource("A"));
    ST32(DIB + 20, 4);
    ST32(RESOURCE + 0x64, 47);
    assert(!bitmap_load_resource("A"));
    ST32(RESOURCE + 0x64, SIZE);

    uint8_t file_header[14] = {'B', 'M'};
    file_header[2] = sizeof file_header + SIZE;
    file_header[10] = sizeof file_header + 48;
    FILE *file = fopen(argv[1], "wb");
    assert(file);
    assert(fwrite(file_header, 1, sizeof file_header, file) == sizeof file_header);
    assert(guest_memory_dump(file, DIB, SIZE));
    assert(fclose(file) == 0);
    check_bitmap(bitmap_load_file(argv[1]));
    assert(remove(argv[1]) == 0);
    assert(invalidations > 0);
    guest_memory_shutdown();
    puts("GDI bitmap: sparse resource and file RLE parity; missing/oversized/truncated resource refusals");
    return 0;
}

/* fn_004148a0 -- the data-file decrypt the game did one byte at a time.
 *
 * One of the hand-written native replacements for recompiled functions; see
 * runtime/overrides/overrides.h for how the set is divided and why.
 */

#include "overrides.h"
#include "world.h"

#include "../guest_ops.h"
#include "../guest_map.h"
#include "../hostwin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * fn_004148a0 -- decrypt one data file into data\temporary.txt.
 *
 * This is the data load. Every one of the game's ~150 object files goes through it, and
 * the original does it ONE BYTE AT A TIME through the C runtime:
 *
 *     fscanf(in, "%c", &c);  ...  fprintf(out, "%c", c - key[i]);
 *
 * which is fine at native speed and is not fine through a recompiled CPU, where each of
 * those is a guest call into a host import. It came to 2.5 million fscanf calls per load,
 * and it is why every attempt to speed the load up by touching the RENDERING failed: the
 * drawing was measured at 14% of the load (LF2_LOAD_PROF), and this is most of the rest.
 *
 * The cipher, read straight out of the decompiled body rather than guessed:
 *   key    "SiuHungIsAGoodBearBecauseHeIsVeryGood", 37 bytes
 *   header the first 0x7b = 123 bytes are consumed and discarded, and the key index is
 *          advanced once per consumed byte, so the payload starts at key index 123 % 37 = 12
 *   byte   out = (in - key[i]) mod 256, then i = (i + 1) % 37
 *
 * (Index 12 is where "odBearBecauseHeIsVeryGood" starts, which is why the widely circulated
 * 25-character key decrypts the first 25 bytes of a file and then turns to noise -- it is
 * this key seen from its offset, with the wrap missing.)
 *
 * Byte-exactness matters more than speed here, so the two things the CRT does that a naive
 * port would not are both reproduced: the input is opened in TEXT mode, so CRLF collapses
 * to LF before decryption (lf2_open_text), and the output is written raw, which is what the
 * port's own "w" fopen does. Anything that cannot be done -- a missing input, an unwritable
 * output -- falls through to the original body rather than silently producing a short file,
 * because a truncated decrypt would show up as the game quietly missing objects.
 *
 * Calling convention: cdecl. The generated body ends in `R(ESP) += 4`, so the argument is
 * the caller's to pop and only the return address comes off here.
 * ------------------------------------------------------------------------ */
const char *lf2_host_path(const char *guest_style);      /* imports.c */
char       *lf2_read_text(const char *host_path, size_t *len);   /* imports.c */

long decrypt_files, decrypt_bytes;

void fn_004148a0__orig(void);

/* LF2_DECRYPT_DUMP=<dir> copies each decrypted file out as NNN.txt, in order. Run once with
 * LF2_SLOW_DECRYPT=1 and once without, diff the two directories, and the native decrypt is
 * either byte-identical to the game's own on every file or it is not -- which is the only
 * check worth having, since a decrypt that is subtly wrong shows up as the game quietly
 * missing frames rather than as a crash. It has to sit in the override and not in the fast
 * path, or the control run dumps nothing and the diff reads as a pass. */
static void decrypt_dump(void)
{
    const char *dir = getenv("LF2_DECRYPT_DUMP");
    if (!dir || !*dir) return;
    static int n;
    char dst[512];
    snprintf(dst, sizeof dst, "%s/%04d.txt", dir, n++);
    FILE *in = fopen(lf2_host_path("data\\temporary.txt"), "rb");
    if (!in) { fprintf(stderr, "decrypt dump: cannot read the output for %s\n", dst); return; }
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); fprintf(stderr, "decrypt dump: cannot write %s\n", dst); return; }
    char b[65536]; size_t got;
    while ((got = fread(b, 1, sizeof b, in)) > 0) fwrite(b, 1, got, out);
    fclose(in); fclose(out);
}

void fn_004148a0(void)
{
    static int native = -1;
    if (native < 0) native = getenv("LF2_SLOW_DECRYPT") == NULL;
    if (!native) { fn_004148a0__orig(); decrypt_dump(); return; }

    static const char KEY[] = "SiuHungIsAGoodBearBecauseHeIsVeryGood";
    enum { KEYLEN = 37, HEADER = 0x7b };
    _Static_assert(sizeof KEY - 1 == KEYLEN, "the key length is part of the cipher");

    const uint32_t arg = LD32(R(ESP) + 4);
    const char *src = lf2_host_path((const char *)(g_mem + arg));

    size_t n = 0;
    char *buf = lf2_read_text(src, &n);
    if (!buf) { fn_004148a0__orig(); return; }

    FILE *out = fopen(lf2_host_path("data\\temporary.txt"), "w");
    if (!out) { free(buf); fn_004148a0__orig(); return; }

    unsigned ki = HEADER % KEYLEN;
    for (size_t i = HEADER; i < n; i++) {
        const int v = ((int)(unsigned char)buf[i] - (int)(unsigned char)KEY[ki]) & 0xff;
        ki = (ki + 1u) % KEYLEN;
        fputc(v, out);
    }
    decrypt_files++;
    decrypt_bytes += (long)(n > HEADER ? n - HEADER : 0);

    fclose(out);
    free(buf);
    decrypt_dump();
    R(ESP) += 4;                 /* cdecl: the return address only */
}

/* ---- the stage's background layer table ----
 *
 * Read the way the game reads it (world.h carries the derivation). Nothing here writes to
 * the table; this is the port learning the stage's own layout so widescreen can carry a
 * tiling layer past the game's 794 at the period the DATA gives rather than one guessed
 * from the blit stream (issue #23).
 */
uint32_t bg_layer_field(uint32_t field_const, int layer)
{
    if (layer < 0 || layer >= BG_MAX_LAYERS) return 0;
    const uint32_t registry = LD32(BG_REGISTRY);
    if (!registry) return 0;                      /* no stage loaded yet */
    const uint32_t bg = LD32(BG_INDEX);
    return LD32(registry + (bg * BG_STRIDE_DW + (uint32_t)layer) * 4u + field_const);
}

uint32_t bg_stage_field(uint32_t field_const)
{
    const uint32_t registry = LD32(BG_REGISTRY);
    if (!registry) return 0;
    return LD32(registry + LD32(BG_INDEX) * BG_STRIDE_DW * 4u + field_const);
}

/* bg.dat's `shadowsize:`, which is what identifies the stage's shadow bitmap in the blit
 * stream. Read from the background record beside the stage width, and checked against the
 * file: Brokeback Clif's bg.dat says `shadowsize: 37 9` and the record holds 37 and 9. */
void bg_shadow_size(int *w, int *h)
{
    *w = (int)(int32_t)bg_stage_field(BG_SHADOW_W);
    *h = (int)(int32_t)bg_stage_field(BG_SHADOW_H);
}

/* Which stage is loaded, so a shadow object learned on one is discarded on the next. */
uint32_t bg_shadow_stage(void) { return LD32(BG_INDEX); }

/* The game's own count, which is what fn_0041a250 iterates on -- not a scan for the first
 * zero span. The two agree on every stage measured, but a scan would silently stop at a
 * layer the game is perfectly happy to draw, and "the layer list ends here" is exactly the
 * kind of guess this table exists to remove. */
int bg_layer_count(void)
{
    const int32_t n = (int32_t)bg_stage_field(BG_LAYER_COUNT);
    if (n <= 0) return 0;
    return n < BG_MAX_LAYERS ? (int)n : BG_MAX_LAYERS;
}

/* One background's record, printed in the form tools/decrypt_dat.py --layers prints so the
 * two can be diffed line for line. `which` indexes the registry directly; the loaded stage is
 * LD32(BG_INDEX). */
static void bg_record_report(uint32_t which)
{
    const uint32_t registry = LD32(BG_REGISTRY);
    const uint32_t base = registry + which * BG_STRIDE_DW * 4u;
    const int32_t  cnt = (int32_t)LD32(base + BG_LAYER_COUNT);
    const int n = cnt <= 0 ? 0 : (cnt < BG_MAX_LAYERS ? (int)cnt : BG_MAX_LAYERS);
    fprintf(stderr, "bg table: background %u  stage width %u  %d layer(s)%s\n",
            which, LD32(base + BG_STAGE_WIDTH), n,
            which == LD32(BG_INDEX) ? "   <- loaded" : "");
    if (n == 0) {
        fprintf(stderr, "bg table:   NO LAYERS -- this record is empty; that is a fact about "
                        "this index, not about the address computation\n");
        return;
    }
    for (int i = 0; i < n; i++)
        fprintf(stderr, "bg table:   layer %-2d span=%-6u x=%-6d y=%-4d loop=%u\n", i,
                LD32(base + BG_LAYER_SPAN + (uint32_t)i * 4u),
                (int32_t)LD32(base + BG_LAYER_X + (uint32_t)i * 4u),
                (int32_t)LD32(base + BG_LAYER_Y + (uint32_t)i * 4u),
                LD32(base + BG_LAYER_LOOP + (uint32_t)i * 4u));
}

/* LF2_BG_TABLE=1 prints the loaded stage's layers once; LF2_BG_TABLE=all prints EVERY
 * background the registry holds. It exists to be CHECKED against the files:
 * `tools/decrypt_dat.py --layers` over every bg.dat under game/bg prints the same spans,
 * offsets and loops, and the two agreeing is what makes the address computation an
 * identification rather than arithmetic that happened to land somewhere.
 *
 * `all` is the stronger check and the reason it exists: VS mode picked the same stage on six
 * consecutive headless runs, so "run it again and hope for a different background" is not a
 * way to test a second stage. The registry holds all of them at once, and the loaded index
 * only selects which one is drawn -- so one run can be checked against twelve files.
 *
 * A stage with no layers is reported as such rather than printing a header and nothing --
 * "the table was empty" and "the table was never reached" must not look alike. */
/* LF2_BG_RECORD=1 dumps the loaded background's whole record as dwords, with each offset
 * given relative to BG_LAYER_SPAN so it can be read against the field constants in world.h.
 * It exists to LOCATE a field rather than to guess one: the stage's shadow bitmap is drawn
 * from a surface whose handle LF2_BLT_FRAME shows, and finding that handle in this dump is
 * what turns "the shadow is probably that small sprite" into an identification. */
static void bg_record_dump(void)
{
    if (!getenv("LF2_BG_RECORD")) return;
    const uint32_t registry = LD32(BG_REGISTRY);
    if (!registry) return;
    const uint32_t base = registry + LD32(BG_INDEX) * BG_STRIDE_DW * 4u;
    fprintf(stderr, "bg record: background %u, %d dwords from base-1200 to base+1440\n",
            LD32(BG_INDEX), (1440 + 1200) / 4);
    for (int32_t off = -1200; off <= 1440; off += 4) {
        const uint32_t v = LD32((uint32_t)((int32_t)(base + BG_LAYER_SPAN) + off));
        if (!v) continue;                       /* zeros are the bulk and say nothing */
        fprintf(stderr, "bg record:  %+5d = %10u  0x%08x\n", off, v, v);
    }
}

void bg_table_report(void)
{
    static int done;
    const char *want = getenv("LF2_BG_TABLE");
    if (done || !want) return;
    /* Sampled while a MATCH is on screen, which is the moment a stage is certainly loaded.
     * Reporting on the first frame that had a registry pointer instead caught the front end,
     * where the background index is 100 and there are no layers -- a true "nothing here" that
     * says nothing about the address computation, and latching on it threw away the one
     * sample that could have. */
    if (!panel_hud_up()) return;
    const uint32_t registry = LD32(BG_REGISTRY);
    if (!registry) return;
    done = 1;
    fprintf(stderr, "bg table: registry %08x, loaded background %u\n",
            registry, LD32(BG_INDEX));
    bg_record_dump();
    if (strcmp(want, "all") != 0) { bg_record_report(LD32(BG_INDEX)); return; }
    /* The count is not known from the table, so walk until a record has no layers AND no
     * stage width -- and say how far the walk got, so a run that found one background reads
     * differently from a run that found twelve. */
    int found = 0;
    for (uint32_t i = 0; i < 64; i++) {
        const uint32_t base = registry + i * BG_STRIDE_DW * 4u;
        if ((int32_t)LD32(base + BG_LAYER_COUNT) <= 0 && !LD32(base + BG_STAGE_WIDTH)) continue;
        bg_record_report(i);
        found++;
    }
    fprintf(stderr, "bg table: %d non-empty background record(s) in the first 64 slots\n",
            found);
}

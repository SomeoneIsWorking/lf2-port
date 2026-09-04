/* LF2's data-file decryptors, which the game ran one byte at a time.
 *
 * One of the hand-written native replacements for guest functions; see
 * runtime/overrides/overrides.h for how the set is divided and why.
 */

#include "environment.h"
#include "overrides.h"
#include "world.h"

#include "guest.h"
#include "guest_map.h"
#include "hostwin.h"
#include "jit_executor.h"
#include "paths.h"
#include "lf2_log.h"

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
 * which is fine at native speed and is not fine through guest execution, where each of
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
 * to LF before decryption (lf2_read_text), and the output is written raw, which is what the
 * port's own "w" fopen does. Anything that cannot be done -- a missing input, an unwritable
 * output -- falls through to the original body rather than silently producing a short file,
 * because a truncated decrypt would show up as the game quietly missing objects.
 *
 * Calling convention: cdecl. The recovered body returns with a four-byte stack adjustment,
 * so the argument is the caller's to pop and only the return address comes off here.
 * ------------------------------------------------------------------------ */
long decrypt_files, decrypt_bytes;

/* LF2_DECRYPT_DUMP=<dir> copies each decrypted file out as NNN.txt, in order. Run once with
 * LF2_SLOW_DECRYPT=1 and once without, diff the two directories, and the native decrypt is
 * either byte-identical to the game's own on every file or it is not -- which is the only
 * check worth having, since a decrypt that is subtly wrong shows up as the game quietly
 * missing frames rather than as a crash. It has to sit in the override and not in the fast
 * path, or the control run dumps nothing and the diff reads as a pass. */
static void decrypt_dump(void)
{
    const char *dir = lf2_environment_get(LF2_ENV_DECRYPT_DUMP);
    if (!dir || !*dir) return;
    static int n;
    char dst[512];
    snprintf(dst, sizeof dst, "%s/%04d.txt", dir, n++);
    FILE *in = fopen(lf2_host_path("data\\temporary.txt"), "rb");
    if (!in) {
        lf2_log_writef(LF2_LOG_INFO, "assets", "decrypt dump: cannot read the output for %s\n", dst);
        return;
    }
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        lf2_log_writef(LF2_LOG_INFO, "assets", "decrypt dump: cannot write %s\n", dst);
        return;
    }
    char b[65536];
    size_t got;
    while ((got = fread(b, 1, sizeof b, in)) > 0) fwrite(b, 1, got, out);
    fclose(in);
    fclose(out);
}

static int decrypt_file(const char *source, const char *destination)
{
    static const char KEY[] = "SiuHungIsAGoodBearBecauseHeIsVeryGood";
    enum { KEYLEN = 37, HEADER = 0x7b };
    _Static_assert(sizeof KEY - 1 == KEYLEN, "the key length is part of the cipher");

    size_t size = 0;
    char *buffer = lf2_read_text(source, &size);
    if (!buffer) return 0;

    FILE *output = fopen(destination, "w");
    if (!output) {
        free(buffer);
        return 0;
    }

    unsigned key_index = HEADER % KEYLEN;
    /* Decrypt into memory and write once: the loop used to put a byte through fputc and its
     * FILE locking each time, which measured as real seconds across a boot's ~150 files.
     * The bytes are the same; only the buffering changed. */
    char *out = malloc(size > HEADER ? size - HEADER : 0);
    int ok = 1;
    if (!out && size > HEADER) return 0;
    size_t produced = 0;
    for (size_t i = HEADER; i < size; ++i) {
        const int value = ((int)(unsigned char)buffer[i] - (int)(unsigned char)KEY[key_index]) & 0xff;
        key_index = (key_index + 1u) % KEYLEN;
        out[produced++] = (char)value;
    }
    if (produced && fwrite(out, 1, produced, output) != produced) ok = 0;
    free(out);
    if (fclose(output) != 0) ok = 0;
    free(buffer);
    if (!ok) return 0;

    ++decrypt_files;
    decrypt_bytes += (long)(size > HEADER ? size - HEADER : 0);
    return 1;
}

void fn_004148a0(void)
{
    static int native = -1;
    if (native < 0) native = lf2_environment_get(LF2_ENV_SLOW_DECRYPT) == NULL;
    if (!native) {
        lf2_jit_call_original(0x004148a0);
        decrypt_dump();
        return;
    }

    const uint32_t arg = LD32(R(ESP) + 4);
    char source[1024];
    snprintf(source, sizeof source, "%s", lf2_host_path((const char *)(g_mem + arg)));
    const char *destination = lf2_host_path("data\\temporary.txt");
    if (!decrypt_file(source, destination)) {
        lf2_jit_call_original(0x004148a0);
        return;
    }
    decrypt_dump();
    R(ESP) += 4; /* cdecl: the return address only */
}

/* fn_00414a30 is the same cipher with explicit source and destination arguments. LF2 uses
 * this second body only for data/stage.dat, so overriding fn_004148a0 alone left another
 * 91,396 byte-at-a-time fscanf calls in every boot. */
void fn_00414a30(void)
{
    static int native = -1;
    if (native < 0) native = lf2_environment_get(LF2_ENV_SLOW_DECRYPT) == NULL;
    if (!native) {
        lf2_jit_call_original(0x00414a30);
        decrypt_dump();
        return;
    }

    char source[1024];
    snprintf(source, sizeof source, "%s", lf2_host_path((const char *)g_mem + LD32(R(ESP) + 4)));
    const char *destination = lf2_host_path((const char *)g_mem + LD32(R(ESP) + 8));
    if (!decrypt_file(source, destination)) {
        lf2_jit_call_original(0x00414a30);
        return;
    }
    decrypt_dump();
    R(ESP) += 4;
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
    if (!registry) return 0; /* no stage loaded yet */
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
uint32_t bg_shadow_stage(void)
{
    return LD32(BG_INDEX);
}

/* ---- the record's two strings ----
 *
 * fn_0040c160, the bg.dat parser, scans `name:` and each `layer:` path straight into the
 * background record (world.h carries the offsets and how they were read). So the port can name
 * the loaded stage and its layers from the game's own memory -- no second decrypt of bg.dat, no
 * data.txt, and no assumption that the registry index matches the order the files load in.
 * Issue #62 needs both: the stage's name keys its hand-woven geometry file, and a layer's name
 * is how an authored solid takes that layer's parallax depth.
 *
 * A 30-byte field is not guaranteed to be NUL-terminated by anything but the data, so both of
 * these copy out and terminate. Returning a pointer into guest memory would hand the caller a
 * string that is only accidentally a C string.
 */
static const char *bg_string(uint32_t at, char *out, size_t n)
{
    size_t i = 0;
    for (; i + 1 < n && i < BG_NAME_LEN; i++) {
        const char c = (char)LD8(at + (uint32_t)i);
        if (!c) break;
        out[i] = c;
    }
    out[i] = 0;
    return out;
}

const char *bg_stage_name(void)
{
    const uint32_t registry = LD32(BG_REGISTRY);
    if (!registry) return NULL; /* no stage loaded: not the same as "" */
    static char name[BG_NAME_LEN + 1];
    bg_string(registry + LD32(BG_INDEX) * BG_STRIDE_DW * 4u + BG_STAGE_NAME, name, sizeof name);
    /* fn_0040c160 turns every '_' into ' ' as it reads the file, so the record says "The Great
     * Wall" where bg.dat says "The_Great_Wall". Put them back, because the FILE's spelling is
     * what a hand-woven stage is named after and a name with spaces in it is a poor file name. */
    for (char *p = name; *p; p++)
        if (*p == ' ') *p = '_';
    return name;
}

int bg_stage_index(const char *want)
{
    const uint32_t registry = LD32(BG_REGISTRY);
    if (!registry) return -2;
    if (!want || !*want) return -1;

    /* bg_table_report's scan shows the registry's live non-empty records in this bounded range.
     * It is deliberately a scan of the game's parsed records, not a table of shipped names: a
     * diagnostic selector that accepted a stale name would be indistinguishable from a scene
     * that happened to be selected. */
    for (uint32_t i = 0; i < 64; i++) {
        const uint32_t base = registry + i * BG_STRIDE_DW * 4u;
        if ((int32_t)LD32(base + BG_LAYER_COUNT) <= 0 && !LD32(base + BG_STAGE_WIDTH)) continue;
        char name[BG_NAME_LEN + 1];
        bg_string(base + BG_STAGE_NAME, name, sizeof name);
        for (char *p = name; *p; p++)
            if (*p == ' ') *p = '_';
        if (strcmp(name, want) == 0) return (int)i;
    }
    return -1;
}

const char *bg_layer_name(int layer)
{
    if (layer < 0 || layer >= bg_layer_count()) return NULL;
    const uint32_t registry = LD32(BG_REGISTRY);
    if (!registry) return NULL;
    static char path[BG_NAME_LEN + 1];
    bg_string(registry + LD32(BG_INDEX) * BG_STRIDE_DW * 4u + BG_LAYER_NAME + (uint32_t)layer * BG_NAME_LEN, path,
              sizeof path);
    /* The record holds the path as bg.dat writes it -- `bg\sys\gw\hill1.bmp`, with the game's
     * backslashes. The leaf is what identifies the layer to an author, and it is unique within
     * a stage because it is a file in one directory. */
    char *leaf = path;
    for (char *p = path; *p; p++)
        if (*p == '\\' || *p == '/') leaf = p + 1;
    return leaf;
}

/* bg.dat's `zboundary:` -- see world.h for why this is the FLOOR and not just a clamp.
 *
 * Located by dumping the whole background record (LF2_BG_RECORD) and reading the per-stage
 * scalar block. Brokeback Clif gives 1500, 300, 510, ..., 37, 9, 5 at -1124, -1120, -1116,
 * -1104, -1100, -1096, and the first, the fourth/fifth and the sixth of those were ALREADY
 * mapped and verified as the stage width, the shadow size and the layer count. The pair in
 * the middle is 300 and 510, which is exactly the `zboundary: 300 510` that claim C018
 * records for this stage from a completely different direction -- walking a fighter to the
 * back wall and watching object+0x18 stop at 300.
 *
 * Checked against the game's own DRAWING as well as against the file: the shadow ellipses in
 * a match span screen y 302..441, inside [300, 510] and touching the far edge. A band that
 * did not bracket the markers would be the wrong field.
 *
 * The sanity test is not decoration. These are two dwords next to a POINTER (-1128), so a
 * record read at the wrong stride returns a pointer-shaped number that would put the floor
 * at row 600000000. Anything that is not an ordered pair inside the game's 550 rows is
 * refused and the caller lights nothing. */
int bg_z_bounds(int *zmin, int *zmax)
{
    if (!LD32(BG_REGISTRY)) return 0;
    const int lo = (int)(int32_t)bg_stage_field(BG_Z_MIN);
    const int hi = (int)(int32_t)bg_stage_field(BG_Z_MAX);
    if (lo <= 0 || hi <= lo || hi > 550) return 0;
    *zmin = lo;
    *zmax = hi;
    return 1;
}

/* Every background's floor band at once, which is how a field located on ONE stage is shown
 * not to be a coincidence of that stage. A wrong stride would give one plausible row and
 * nonsense everywhere else; a field that were something other than the z boundary would not
 * be an ordered pair inside 550 rows on every stage the game ships.
 *
 * It prints the REFUSALS too, and counts them: "20 of 20 stages have a sane floor band" and
 * "1 of 20, and the walk stopped early" must not look alike. */
static void bg_z_report(void)
{
    const uint32_t registry = LD32(BG_REGISTRY);
    if (!registry) return;
    const uint32_t here = LD32(BG_INDEX);
    int sane = 0, seen = 0;
    lf2_log_writef(LF2_LOG_INFO, "assets",
                   "bg zboundary: the walkable floor of each background, from the record's "
                   "own scalar block\n");
    for (uint32_t bg = 0; bg < 60; bg++) {
        const uint32_t base = registry + bg * BG_STRIDE_DW * 4u;
        const int32_t count = (int32_t)LD32(base + BG_LAYER_COUNT);
        const int32_t width = (int32_t)LD32(base + BG_STAGE_WIDTH);
        if (count <= 0 && width <= 0) continue; /* past the end of the table */
        seen++;
        const int lo = (int)(int32_t)LD32(base + BG_Z_MIN);
        const int hi = (int)(int32_t)LD32(base + BG_Z_MAX);
        const int ok = (lo > 0 && hi > lo && hi <= 550);
        if (ok) sane++;
        lf2_log_writef(LF2_LOG_INFO, "assets", "bg zboundary:  %2u %s  z %d..%d  (stage width %d, %d layer(s))%s\n", bg,
                       bg == here ? "<-loaded" : "        ", lo, hi, width, count,
                       ok ? "" : "   REFUSED: not an ordered pair inside 550 rows");
    }
    lf2_log_writef(LF2_LOG_INFO, "assets", "bg zboundary: %d of %d backgrounds give a sane floor band%s\n", sane, seen,
                   seen == 0 ? " -- the table was EMPTY, so this says nothing" : "");
}

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

/* One background's record, printed in the form tools/re/decrypt_dat.py --layers prints so the
 * two can be diffed line for line. `which` indexes the registry directly; the loaded stage is
 * LD32(BG_INDEX). */
static void bg_record_report(uint32_t which)
{
    const uint32_t registry = LD32(BG_REGISTRY);
    const uint32_t base = registry + which * BG_STRIDE_DW * 4u;
    const int32_t cnt = (int32_t)LD32(base + BG_LAYER_COUNT);
    const int n = cnt <= 0 ? 0 : (cnt < BG_MAX_LAYERS ? (int)cnt : BG_MAX_LAYERS);
    /* The NAME, and it is the check on the whole string half of the record: `decrypt_dat.py`
     * prints bg.dat's own `name:` and its own layer paths, and this prints what the game
     * parsed them into. Two files' worth of strings agreeing to the character is what turns
     * "0x4d4617c looked like a name" into an identification. A record whose name is empty
     * prints as (empty) rather than as nothing, because a blank in this column would read as
     * "the report did not get that far". */
    char nm[BG_NAME_LEN + 1];
    bg_string(base + BG_STAGE_NAME, nm, sizeof nm);
    lf2_log_writef(LF2_LOG_INFO, "assets", "bg table: background %u  \"%s\"  stage width %u  %d layer(s)%s\n", which,
                   nm[0] ? nm : "(empty)", LD32(base + BG_STAGE_WIDTH), n,
                   which == LD32(BG_INDEX) ? "   <- loaded" : "");
    if (n == 0) {
        lf2_log_writef(LF2_LOG_INFO, "assets",
                       "bg table:   NO LAYERS -- this record is empty; that is a fact about "
                       "this index, not about the address computation\n");
        return;
    }
    for (int i = 0; i < n; i++) {
        char lp[BG_NAME_LEN + 1];
        /* The layer name array's stride is 30 BYTES, not 4 -- it is a table of fixed-width
         * strings and the numeric fields around it are dword arrays. Indexing it the way the
         * others are indexed reads a quarter of the way into each name. */
        bg_string(base + BG_LAYER_NAME + (uint32_t)i * BG_NAME_LEN, lp, sizeof lp);
        lf2_log_writef(
            LF2_LOG_INFO, "assets", "bg table:   layer %-2d span=%-6u x=%-6d y=%-4d loop=%-4u %s\n", i,
            LD32(base + BG_LAYER_SPAN + (uint32_t)i * 4u), (int32_t)LD32(base + BG_LAYER_X + (uint32_t)i * 4u),
            (int32_t)LD32(base + BG_LAYER_Y + (uint32_t)i * 4u), LD32(base + BG_LAYER_LOOP + (uint32_t)i * 4u),
            lp[0] ? lp : "(no name -- the record's string field is empty here)");
    }
}

/* LF2_BG_TABLE=1 prints the loaded stage's layers once; LF2_BG_TABLE=all prints EVERY
 * background the registry holds. It exists to be CHECKED against the files:
 * `tools/re/decrypt_dat.py --layers` over every bg.dat under game/bg prints the same spans,
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
    if (!lf2_environment_get(LF2_ENV_BG_RECORD)) return;
    const uint32_t registry = LD32(BG_REGISTRY);
    if (!registry) return;
    const uint32_t base = registry + LD32(BG_INDEX) * BG_STRIDE_DW * 4u;
    lf2_log_writef(LF2_LOG_INFO, "assets",
                   "bg record: background %u, every non-zero dword from base-2600 to "
                   "base+2600, offset relative to BG_LAYER_SPAN\n",
                   LD32(BG_INDEX));
    for (int32_t off = -2600; off <= 2600; off += 4) {
        const uint32_t v = LD32((uint32_t)((int32_t)(base + BG_LAYER_SPAN) + off));
        if (!v) continue; /* zeros are the bulk and say nothing */
        lf2_log_writef(LF2_LOG_INFO, "assets", "bg record:  %+5d = %10u  0x%08x\n", off, v, v);
    }
}

void bg_table_report(void)
{
    static int done;
    const char *want = lf2_environment_get(LF2_ENV_BG_TABLE);
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
    lf2_log_writef(LF2_LOG_INFO, "assets", "bg table: registry %08x, loaded background %u\n", registry, LD32(BG_INDEX));
    bg_z_report();
    bg_record_dump();
    if (strcmp(want, "all") != 0) {
        bg_record_report(LD32(BG_INDEX));
        return;
    }
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
    lf2_log_writef(LF2_LOG_INFO, "assets", "bg table: %d non-empty background record(s) in the first 64 slots\n",
                   found);
}

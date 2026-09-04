/* The instruments over the object world -- every LF2_COOP_* probe.
 *
 * One of the hand-written native replacements for guest routines; see
 * runtime/overrides/overrides.h for how the set is divided and why.
 */

#include "environment.h"
#include "overrides.h"
#include "world.h"

#include "guest.h"
#include "guest_map.h"
#include "hostwin.h"
#include "lf2_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the follow-up watch: which entry the spawn diagnostics are about ----
 *
 * Latched by coop_build, when it is asked to. The watch follows the FIRST spawn of a run:
 * with a list of spawns, a watch that silently re-pointed at the last one would report on a
 * different fighter than the reader expects, so the switch is ANNOUNCED instead of made.
 *
 * A character-selection REBUILD does not latch, and that is the point of the flag: the
 * watch reports ages since a spawn, and restarting it every time a joiner pressed left
 * would make its samples measure the last keypress rather than the fighter's life. */
static uint32_t spawn_dst_obj;
static int spawn_dst_idx = -1;
static long spawn_frame;

void coop_watch_latch(int dst, uint32_t obj)
{
    if (spawn_dst_idx < 0) {
        spawn_dst_obj = obj;
        spawn_dst_idx = dst;
        spawn_frame = hostwin_frames();
    } else if (spawn_dst_idx != dst) {
        lf2_log_writef(LF2_LOG_INFO, "coop-debug",
                       "coop build: the follow-up watch stays on entry %d, the first spawn "
                       "of this run -- entry %d is not being watched\n",
                       spawn_dst_idx, dst);
    }
}

/* ---- LF2_COOP_REFS: who POINTS AT a player record ----
 *
 * The question this answers is the one thing left before drop-in coop is mechanical: a
 * fully initialised player record plus its bit in the joined mask is NOT enough to put a
 * fighter in the world (measured -- see issue #15), so there is a list of active objects a
 * fighter also has to be in. A list of objects is a list of POINTERS to them, so the list
 * is found by scanning memory for the pointer values the game already has.
 *
 * It scans every region a guest pointer can live in and reports each aligned dword that
 * equals one of the eight player-record pointers, with the dwords around it -- an array
 * shows up as several hits one stride apart, and a single field in a struct does not.
 *
 * The negative is designed first, because "no references found" is the result that would
 * be believed without evidence. The report always states the regions and their byte
 * counts, the number of dwords compared, a per-target hit count, and the blind spots the
 * method has (unaligned or tagged pointers; the VRAM and PCM arenas, which are ours and
 * hold no game structures). A scan of a zero-length heap prints REFUSED rather than "0
 * hits", because those are not the same answer.
 *
 * And it carries a positive control that cannot be skipped: the eight pointers are read
 * out of `this+404`, so the scan MUST find each non-null one at exactly that address. If
 * it does not, the scan is not seeing memory it claims to see, and the line says FAILED
 * instead of a hit count. A scan that finds nothing and passes its control is evidence;
 * one that finds nothing and never checked is not. */
uint32_t guest_heap_used(void); /* imports.c */

struct refs_region {
    const char *name;
    uint32_t lo, hi;
};

void coop_refs_scan(uint32_t self)
{
    enum { NTARGET = 12 }; /* eight slots, plus up to four extras */
    uint32_t target[NTARGET];
    char label[NTARGET][16];
    long hits[NTARGET];
    int ntarget = 0, nslot = 0;

    for (int i = 0; i < 8; i++) {
        const uint32_t p = LD32(self + PLAYER_PTRS + 4u * (uint32_t)i);
        if (!p) continue;
        target[ntarget] = p;
        snprintf(label[ntarget], sizeof label[0], "slot%d", i);
        hits[ntarget] = 0;
        ntarget++;
        nslot++;
    }

    /* LF2_COOP_REFS_ADDR=<hex>[,...] -- chase an address the scan itself turned up, such
     * as the 1052-byte object at 0x25f149a0 that the "Fight!" heap diff left unexplained. */
    const char *extra = lf2_environment_get(LF2_ENV_COOP_REFS_ADDR);
    for (const char *s = extra; s && *s && ntarget < NTARGET;) {
        char *end;
        const unsigned long v = strtoul(s, &end, 16);
        if (end == s) break;
        target[ntarget] = (uint32_t)v;
        snprintf(label[ntarget], sizeof label[0], "extra%d", ntarget - nslot);
        hits[ntarget] = 0;
        ntarget++;
        s = (*end == ',') ? end + 1 : end;
    }

    const uint32_t heap_used = guest_heap_used();
    const struct refs_region regions[] = {
        {"image", g_image_lo, g_image_hi},
        {"heap", GUEST_HEAP_BASE, GUEST_HEAP_BASE + heap_used},
        {"stack", GUEST_STACK_BASE, GUEST_STACK_END},
    };
    const int nregion = (int)(sizeof regions / sizeof regions[0]);

    lf2_log_writef(LF2_LOG_INFO, "coop-debug", "coop refs: frame %ld, this=%08x, %d targets\n", hostwin_frames(), self,
                   ntarget);
    for (int t = 0; t < ntarget; t++)
        lf2_log_writef(LF2_LOG_INFO, "coop-debug", "  target %-7s %08x\n", label[t], target[t]);

    if (ntarget == 0) {
        lf2_log_writef(LF2_LOG_INFO, "coop-debug",
                       "coop refs: REFUSED -- every player pointer at this+404 is null, so "
                       "there is nothing to look for. This is not a negative result; the "
                       "scan was pointed at a frame that is not a match.\n");
        return;
    }

    long dwords = 0;
    int printed = 0, total_hits = 0;
    for (int r = 0; r < nregion; r++) {
        const uint32_t lo = regions[r].lo, hi = regions[r].hi;
        if (hi <= lo) {
            lf2_log_writef(LF2_LOG_INFO, "coop-debug",
                           "coop refs: region %-5s REFUSED -- empty range [%08x,%08x). "
                           "Nothing in it was compared.\n",
                           regions[r].name, lo, hi);
            continue;
        }
        lf2_log_writef(LF2_LOG_INFO, "coop-debug", "coop refs: region %-5s [%08x,%08x) %.1f MiB\n", regions[r].name, lo,
                       hi, (double)(hi - lo) / (1024.0 * 1024.0));
        for (uint32_t a = lo & ~3u; a + 4 <= hi; a += 4) {
            const uint32_t v = LD32(a);
            dwords++;
            for (int t = 0; t < ntarget; t++) {
                if (v != target[t]) continue;
                hits[t]++;
                total_hits++;
                if (printed++ < 80) {
                    lf2_log_writef(LF2_LOG_INFO, "coop-debug", "  hit %08x = %s (%s)  ctx:", a, label[t],
                                   regions[r].name);
                    for (int k = -4; k <= 4; k++) {
                        const uint32_t ca = a + 4u * (uint32_t)k;
                        if (ca < lo || ca + 4 > hi) {
                            lf2_log_writef(LF2_LOG_INFO, "coop-debug", " --------");
                            continue;
                        }
                        lf2_log_writef(LF2_LOG_INFO, "coop-debug", "%c%08x", k == 0 ? '[' : ' ', LD32(ca));
                    }
                    lf2_log_writef(LF2_LOG_INFO, "coop-debug", "\n");
                }
            }
        }
    }

    /* The positive control. Every non-null slot pointer is stored at this+404+4i, so the
     * scan has to have found it there -- unless `this` is outside every region above, which
     * is itself something the scan must say out loud rather than pass over. */
    int control_ok = 1, control_checked = 0;
    for (int i = 0; i < 8; i++) {
        const uint32_t at = self + PLAYER_PTRS + 4u * (uint32_t)i;
        if (!LD32(at)) continue;
        int covered = 0;
        for (int r = 0; r < nregion; r++)
            if (regions[r].hi > regions[r].lo && at >= regions[r].lo && at + 4 <= regions[r].hi) covered = 1;
        if (!covered) {
            control_ok = 0;
            continue;
        }
        control_checked++;
    }
    /* Being covered is necessary; having been counted is the actual check. Each covered
     * slot pointer contributes at least the one hit at this+404+4i. */
    for (int t = 0; t < nslot && control_ok; t++)
        if (hits[t] < 1) control_ok = 0;

    lf2_log_writef(LF2_LOG_INFO, "coop-debug", "coop refs: %ld dwords compared across %d regions, %d hits\n", dwords,
                   nregion, total_hits);
    if (printed > 80) lf2_log_writef(LF2_LOG_INFO, "coop-debug", "coop refs: %d hits were not printed\n", printed - 80);
    for (int t = 0; t < ntarget; t++)
        lf2_log_writef(LF2_LOG_INFO, "coop-debug", "  %-7s %08x  %ld reference%s\n", label[t], target[t], hits[t],
                       hits[t] == 1 ? "" : "s");
    if (control_ok && control_checked)
        lf2_log_writef(LF2_LOG_INFO, "coop-debug",
                       "coop refs: control PASSED -- all %d slot pointers were found at "
                       "this+404, so the scan does see the memory it reports on\n",
                       control_checked);
    else
        lf2_log_writef(LF2_LOG_INFO, "coop-debug",
                       "coop refs: control FAILED (%d slot pointers inside a scanned "
                       "region) -- the counts above are NOT evidence of anything\n",
                       control_checked);
    lf2_log_writef(LF2_LOG_INFO, "coop-debug",
                   "coop refs: blind spots -- unaligned or tagged pointers, an object held "
                   "only as base+offset, and the VRAM/PCM arenas (ours, no game structures). "
                   "A zero count means no ALIGNED dword in the regions above holds that "
                   "value.\n");
}

/* ---- LF2_COOP_TABLE: the object table at this+404, past the eight player slots ----
 *
 * What LF2_COOP_REFS established: the eight player records are pointed at from exactly ONE
 * place each -- consecutive dwords at 0x00458c94 in .data -- and from NOWHERE in 101.9 MiB
 * of heap. So there is no separate heap list of active objects; this table is the list, and
 * it does not stop at eight. The dwords after slot 7 continue on the same 0x420 stride, and
 * the object the "Fight!" heap diff could not place, 0x25f149a0, is entry 11 of it.
 *
 * This walks the table and prints, per entry, the pointer, its index on the stride grid,
 * and the fields the playing-vs-idle diff in issue #15 identified: the chosen character at
 * +0x364, HP at +0x2fc, position at +0x10/+0x18, and +0x354 (99 idle / 0 playing).
 *
 * The negative, again first: the walk states how many entries it examined and why it
 * stopped, and every entry is printed -- including null and off-grid ones -- rather than
 * only the ones that look like objects. A table dump that silently skipped what it could
 * not parse would make a short table and a misread one look the same. */
/* An entry that has never been put into the world reads exactly as the game initialised it:
 * no character, full HP, the origin, and 99 at +0x354. That signature is not a guess -- it
 * is what LF2_COOP_DIFF prints for an idle slot beside a playing one, in the same run. Any
 * departure from it is an object that something has touched. */
int coop_entry_live(uint32_t p)
{
    return (int32_t)LD32(p + 0x364) != 0      /* chosen character */
           || (int32_t)LD32(p + 0x2fc) != 500 /* HP */
           || (int32_t)LD32(p + 0x10) != 0    /* x */
           || (int32_t)LD32(p + 0x18) != 0    /* y */
           || (int32_t)LD32(p + 0x354) != 99;
}

/* LF2_COOP_PAIR=<i>,<j> -- the dwords where table entry i differs from entry j. The existing
 * LF2_COOP_DIFF compares slot 0 against slot 4, which mixes two questions: a player record
 * differs from an idle one both by being a fighter in the world AND by being a player. This
 * takes any two indices, so a live NON-player entry (the computer fighter the game itself
 * put in the table) can be compared against an untouched neighbour of the same kind, which
 * isolates "is in the world" from "is a player". */
void coop_pair_diff(uint32_t self, int i, int j)
{
    const uint32_t a = LD32(self + PLAYER_PTRS + 4u * (uint32_t)i);
    const uint32_t b = LD32(self + PLAYER_PTRS + 4u * (uint32_t)j);
    lf2_log_writef(LF2_LOG_INFO, "coop-debug", "coop pair: frame %ld, [%d]=%08x (%s) vs [%d]=%08x (%s)\n",
                   hostwin_frames(), i, a, a && coop_entry_live(a) ? "LIVE" : "idle", j, b,
                   b && coop_entry_live(b) ? "LIVE" : "idle");
    if (!a || !b) {
        lf2_log_writef(LF2_LOG_INFO, "coop-debug", "coop pair: REFUSED -- an entry is null, nothing was compared\n");
        return;
    }
    if (!(a && coop_entry_live(a)) && !(b && coop_entry_live(b)))
        lf2_log_writef(LF2_LOG_INFO, "coop-debug",
                       "coop pair: WARNING -- NEITHER entry is live, so any difference "
                       "below is between two untouched records and says nothing about "
                       "being in the world\n");
    int n = 0;
    for (uint32_t o = 0; o < 0x420u; o += 4) {
        const uint32_t va = LD32(a + o), vb = LD32(b + o);
        if (va == vb) continue;
        n++;
        lf2_log_writef(LF2_LOG_INFO, "coop-debug", "  +%03x  [%d]=%-11d [%d]=%-11d (%08x / %08x)\n", o, i, (int32_t)va,
                       j, (int32_t)vb, va, vb);
    }
    lf2_log_writef(LF2_LOG_INFO, "coop-debug", "coop pair: %d differing dwords of %d compared\n", n, 0x420 / 4);
}

/* Called every gather once a spawn has been attempted. Reports on a schedule AND on any
 * change back towards the idle default, because the reset is the finding. */
void coop_spawn_watch(uint32_t self)
{
    if (spawn_dst_idx < 0) return;
    const long age = hostwin_frames() - spawn_frame;

    /* Which of the seven buttons have EVER reached this fighter's record since it joined.
     * Accumulated every frame rather than sampled, because a press lasting a few frames
     * would fall between samples and read as "the pad never reached it".
     *
     * This exists because DISPLACEMENT is the wrong signal for a fighter that joins into an
     * ongoing fight: it gets knocked about, so an idle joiner drifted 56 and then 69 px
     * across runs while a driven one managed ~120, and no threshold separates those. The
     * claim tested here is that the pad's input reaches THIS record; that the game turns
     * such input into movement is what two_human_match measures, on a fighter standing at
     * its own start position where displacement IS clean. */
    static unsigned char seen[7];
    for (int b = 0; b < 7; b++) seen[b] |= LD8(spawn_dst_obj + BTN_CUR + b);
    static int was_live = 1, said_reset;
    const int live = coop_entry_live(spawn_dst_obj);
    if (was_live && !live && !said_reset) {
        said_reset = 1;
        lf2_log_writef(LF2_LOG_INFO, "coop-debug",
                       "coop spawn: entry %d was RESET to the idle default %ld frames after "
                       "the clone -- the game's own sweep undid it, which is a different "
                       "answer from the spawn having no effect\n",
                       spawn_dst_idx, age);
    }
    was_live = live;
    if (age == 1 || age == 5 || age == 30 || age == 120 || age == 300)
        lf2_log_writef(LF2_LOG_INFO, "coop-debug",
                       "coop spawn: +%3ld frames  entry %d %s  +000=%d char=%d hp=%d "
                       "x=%d y=%d +354=%d +418=%d\n",
                       age, spawn_dst_idx, live ? "LIVE" : "idle", (int32_t)LD32(spawn_dst_obj + 0x000),
                       (int32_t)LD32(spawn_dst_obj + 0x364), (int32_t)LD32(spawn_dst_obj + 0x2fc),
                       (int32_t)LD32(spawn_dst_obj + 0x10), (int32_t)LD32(spawn_dst_obj + 0x18),
                       (int32_t)LD32(spawn_dst_obj + 0x354), (int32_t)LD32(spawn_dst_obj + 0x418));
    if (age == 1 || age == 5 || age == 30 || age == 120 || age == 300)
        lf2_log_writef(LF2_LOG_INFO, "coop-debug",
                       "coop spawn: +%3ld frames  entry %d buttons seen: "
                       "up=%d down=%d left=%d right=%d attack=%d jump=%d defend=%d\n",
                       age, spawn_dst_idx, seen[0], seen[1], seen[2], seen[3], seen[4], seen[5], seen[6]);

    /* The spawned fighter draws and fights, but its HUD PORTRAIT is not its character. So
     * something reads identity from a field the spawn does not set. The shortest way to
     * that field is to diff this record against one the GAME built -- the computer opponent
     * -- once both have been running a while: what differs is what was not set, minus
     * whatever has diverged through being in different places doing different things.
     *
     * Deliberately at +90 rather than +1: at +1 the spawn's own writes dominate and every
     * position and state field differs, which buries the handful that matter. */
    /* LF2_COOP_SHOT=<n>: capture the frame n frames after the spawn, so the picture is of
     * the spawn rather than of whatever the run happened to be showing at a chosen frame. */
    {
        const char *shot = lf2_environment_get(LF2_ENV_COOP_SHOT);
        if (shot && age == atol(shot)) {
            lf2_log_writef(LF2_LOG_INFO, "coop-debug", "coop spawn: requesting a frame capture at +%ld frames\n", age);
            gfx_request_frame_dump();
        }
    }

    if (age == 90) {
        int other = -1;
        for (int k = 0; k < TABLE_N; k++)
            if (k != spawn_dst_idx && LD8(EXISTS + (uint32_t)k)) {
                other = k;
                break;
            }
        if (other < 0)
            lf2_log_writef(LF2_LOG_INFO, "coop-debug",
                           "coop spawn: no other live entry to diff against, so the "
                           "portrait question is unanswered by this run\n");
        else {
            lf2_log_writef(LF2_LOG_INFO, "coop-debug",
                           "coop spawn: spawned entry %d against game-built entry %d --\n"
                           "  the fields the spawn does not set are in here somewhere\n",
                           spawn_dst_idx, other);
            coop_pair_diff(self, spawn_dst_idx, other);
        }
    }
}

/* ---- LF2_COOP_REGISTRY: the object-data registry the game spawns from ----
 *
 * The spawn inlined in fn_0041bc90 reads
 *
 *     reg   = LD32(this + 2004)
 *     count = LD32(reg + 81273728)
 *     for (i = 0; i < count; i++)
 *         if (LD32(LD32(reg + 4i) + 1780) == <wanted>) break;
 *     obj->872 = LD32(reg + 4i);   obj->796 = data->144
 *
 * so the registry is an array of pointers to per-object data blocks, and every field named
 * here is one the game itself indexes by. It is dumped raw because the one thing that must
 * NOT happen is field 1780 being written down as "the character id" on the strength of one
 * comparison: fn_0041bc90 compares it against 999 and fn_004064d0 compares it against 7 and
 * 8, which are not the same kind of value. Printing the blocks side by side against entries
 * whose character is already known from the table is what settles it.
 *
 * The count's offset from the registry base is enormous (81273728 = 0x4d82000), which is a
 * real possibility for a struct this game's size but also exactly what a misread would look
 * like. So the count is sanity-checked and the dump REFUSES rather than walking an array of
 * whatever length a bad read produced. */
void coop_registry_dump(uint32_t self)
{
    const uint32_t reg = LD32(self + REG_PTR);
    lf2_log_writef(LF2_LOG_INFO, "coop-debug", "coop registry: frame %ld, this+%d = %08x\n", hostwin_frames(), REG_PTR,
                   reg);
    if (!reg || reg < GUEST_HEAP_BASE || reg >= GUEST_HEAP_END) {
        lf2_log_writef(LF2_LOG_INFO, "coop-debug",
                       "coop registry: REFUSED -- %08x is not a heap pointer, so this is "
                       "not the registry and nothing was read\n",
                       reg);
        return;
    }
    const uint32_t count_at = reg + (uint32_t)REG_COUNT_OFF;
    const int32_t count = (int32_t)LD32(count_at);
    lf2_log_writef(LF2_LOG_INFO, "coop-debug", "coop registry: count at %08x (reg + 0x%x) reads %d\n", count_at,
                   REG_COUNT_OFF, count);
    if (count <= 0 || count > 512) {
        lf2_log_writef(LF2_LOG_INFO, "coop-debug",
                       "coop registry: REFUSED -- a count of %d is not plausible for an "
                       "object table, so the 0x%x offset is being read wrong. Nothing was "
                       "walked; this is not an empty registry.\n",
                       count, REG_COUNT_OFF);
        return;
    }

    /* Which registry entry each LIVE object is using, so the dump can be read against
     * characters whose identity is already known from the table. */
    lf2_log_writef(LF2_LOG_INFO, "coop-debug", "coop registry: live objects and the data block each points at:\n");
    for (int k = 0; k < TABLE_N; k++) {
        if (!LD8(EXISTS + (uint32_t)k)) continue;
        const uint32_t o = LD32(self + PLAYER_PTRS + 4u * (uint32_t)k);
        if (!o) continue;
        const uint32_t data = LD32(o + 872);
        int which = -1;
        for (int i = 0; i < count; i++)
            if (LD32(reg + 4u * (uint32_t)i) == data) {
                which = i;
                break;
            }
        lf2_log_writef(LF2_LOG_INFO, "coop-debug", "  object [%3d] char(+0x364)=%-4d data(+872)=%08x = registry[%d]\n",
                       k, (int32_t)LD32(o + 0x364), data, which);
    }

    /* LF2_COOP_REGDUMP=<file> -- the head of every data block, for finding a field whose
     * value matches something the game's own data.txt declares. data.txt gives an id AND a
     * type per object (type 0 is a character), and the registry is in data.txt order, so a
     * candidate offset can be required to match ALL 65 entries rather than a sample. That
     * is the difference between locating a field and guessing one. */
    {
        const char *dump = lf2_environment_get(LF2_ENV_COOP_REGDUMP);
        if (dump) {
            FILE *f = fopen(dump, "wb");
            if (!f) {
                lf2_log_writef(LF2_LOG_INFO, "coop-debug", "coop registry: cannot write %s -- nothing dumped\n", dump);
            } else {
                enum { HEAD = 2048 };
                const uint32_t n = (uint32_t)count, head = HEAD;
                fwrite(&n, 4, 1, f);
                fwrite(&head, 4, 1, f);
                int written = 0;
                for (int i = 0; i < count; i++) {
                    const uint32_t d = LD32(reg + 4u * (uint32_t)i);
                    if (d < GUEST_HEAP_BASE || d + HEAD > GUEST_HEAP_END) {
                        static const uint8_t zero[HEAD];
                        fwrite(zero, 1, HEAD, f);
                        continue;
                    }
                    fwrite(g_mem + d, 1, HEAD, f);
                    written++;
                }
                fclose(f);
                lf2_log_writef(LF2_LOG_INFO, "coop-debug",
                               "coop registry: wrote %s -- %d entries of %d bytes, %d of "
                               "them real blocks\n",
                               dump, count, HEAD, written);
            }
        }
    }

    lf2_log_writef(LF2_LOG_INFO, "coop-debug",
                   "coop registry: %d entries -- ptr, +1780, +144, and the first printable "
                   "run in the block:\n",
                   count);
    for (int i = 0; i < count; i++) {
        const uint32_t d = LD32(reg + 4u * (uint32_t)i);
        lf2_log_writef(LF2_LOG_INFO, "coop-debug", "  [%3d] %08x", i, d);
        if (d < GUEST_HEAP_BASE || d >= GUEST_HEAP_END) {
            lf2_log_writef(LF2_LOG_INFO, "coop-debug", "  NOT A HEAP POINTER -- not read\n");
            continue;
        }
        char printable[65];
        for (uint32_t o = 0; o < 64; o++) {
            const uint8_t c = LD8(d + o);
            printable[o] = (char)(c >= 32 && c < 127 ? c : '.');
        }
        printable[64] = '\0';
        lf2_log_writef(LF2_LOG_INFO, "coop-debug", "  id=%-6d type=%-3d +144=%-6d  \"%s\"\n",
                       (int32_t)LD32(d + DATA_ID), (int32_t)LD32(d + DATA_TYPE), (int32_t)LD32(d + 144), printable);
    }
}

void coop_table_dump(uint32_t self)
{
    enum { MAXENT = 512 };
    const uint32_t base = LD32(self + PLAYER_PTRS);

    lf2_log_writef(LF2_LOG_INFO, "coop-debug",
                   "coop table: frame %ld, this=%08x, table at %08x, grid base %08x "
                   "stride 0x420\n",
                   hostwin_frames(), self, self + PLAYER_PTRS, base);
    if (!base) {
        lf2_log_writef(LF2_LOG_INFO, "coop-debug",
                       "coop table: REFUSED -- entry 0 is null, so there is no grid base to "
                       "measure against. Not a short table; a wrong frame.\n");
        return;
    }

    int nonnull = 0, ongrid = 0, offgrid = 0, i = 0, nullrun = 0, live = 0;
    for (; i < MAXENT; i++) {
        const uint32_t p = LD32(self + PLAYER_PTRS + 4u * (uint32_t)i);
        if (!p) {
            nullrun++;
            if (nullrun >= 8) {
                i++;
                break;
            }
            continue;
        }
        nullrun = 0;
        nonnull++;
        const int32_t delta = (int32_t)(p - base);
        const int grid = (delta % 0x420) == 0;
        if (grid) ongrid++;
        else offgrid++;

        const int inheap = p >= GUEST_HEAP_BASE && p + 0x420 <= GUEST_HEAP_END;
        const int is_live = inheap && coop_entry_live(p);
        if (is_live) live++;

        /* The first eight are printed unconditionally because they are the player slots and
         * their being idle is itself the thing being measured; past that only entries that
         * are not untouched defaults are printed, or the dump is 400 identical lines. */
        if (i >= 8 && !is_live && grid && inheap && !LD8(EXISTS + (uint32_t)i)) continue;

        lf2_log_writef(LF2_LOG_INFO, "coop-debug", "  [%3d] %08x gate=%-3d %s%s", i, p, LD8(EXISTS + (uint32_t)i),
                       grid ? "" : "OFF-GRID ", is_live ? "LIVE " : "     ");
        if (grid) lf2_log_writef(LF2_LOG_INFO, "coop-debug", "idx %-4d ", delta / 0x420);
        if (inheap)
            /* +0x338 is printed because the read profile says it is the ONLY dword the
             * per-frame sweep reads on an idle object -- 300 reads in 300 frames and
             * nothing else in the record. Whatever gates an object into the world is
             * decided from it. */
            lf2_log_writef(LF2_LOG_INFO, "coop-debug",
                           "+338=%-10d +000=%-4d char=%-4d hp=%-5d x=%-6d y=%-6d "
                           "+354=%-4d +418=%-4d +368=%08x",
                           (int32_t)LD32(p + 0x338), (int32_t)LD32(p + 0x000), (int32_t)LD32(p + 0x364),
                           (int32_t)LD32(p + 0x2fc), (int32_t)LD32(p + 0x10), (int32_t)LD32(p + 0x18),
                           (int32_t)LD32(p + 0x354), (int32_t)LD32(p + 0x418), LD32(p + 0x368));
        else lf2_log_writef(LF2_LOG_INFO, "coop-debug", "NOT IN THE HEAP -- not an object of this kind");
        lf2_log_writef(LF2_LOG_INFO, "coop-debug", "\n");
    }
    lf2_log_writef(LF2_LOG_INFO, "coop-debug",
                   "coop table: %d entries examined, %d non-null (%d on the 0x420 grid, %d "
                   "off it), %d LIVE; stopped %s\n",
                   i, nonnull, ongrid, offgrid, live,
                   i >= MAXENT ? "at the MAXENT cap, so the table may be longer than this"
                               : "after 8 consecutive nulls");
    /* What follows the 400 pointers. The per-byte read profile of `this` puts a hot dword
     * at +0x7d4 -- immediately past the table -- read about 94 times a frame during a
     * match, which is the profile of a count or a list head rather than a stored setting.
     * Printed raw, with no interpretation, because naming it before seeing it is how a
     * reading gets fixed in place. */
    lf2_log_writef(LF2_LOG_INFO, "coop-debug", "coop table: the 64 dwords after the table (this+0x7d4 onwards):\n");
    for (int k = 0; k < 64; k += 8) {
        lf2_log_writef(LF2_LOG_INFO, "coop-debug", "  +%03x:", 0x7d4 + k * 4);
        for (int j = 0; j < 8; j++)
            lf2_log_writef(LF2_LOG_INFO, "coop-debug", " %11d", (int32_t)LD32(self + 0x7d4u + 4u * (uint32_t)(k + j)));
        lf2_log_writef(LF2_LOG_INFO, "coop-debug", "\n");
    }

    if (live == 0)
        lf2_log_writef(LF2_LOG_INFO, "coop-debug",
                       "coop table: NOT A MATCH -- every entry is still at its initialised "
                       "default (no character, 500 HP, the origin, 99 at +0x354), so no "
                       "fighter exists on this frame. This dump says NOTHING about how "
                       "fighters are registered; the run did not reach a match.\n");
}

/* Counters, not a hit log: the interesting failure is that this never merges anything, and
 * a diagnostic that only printed when it did would be silent in exactly that case. The
 * three together distinguish "no player slot was live", "no controller was bound to one"
 * and "a pad was there and nothing was pressed" -- which are three different bugs. */

/* ---- coop_debug_tick: every LF2_COOP_* probe, once per gather ----
 *
 * Called unconditionally from the input gather, because the spawn watch inside it has to
 * see every frame: the finding it exists for is the game RESETTING a record, which happens
 * on a frame nobody chose in advance. Everything else in here is env-gated and costs a
 * getenv.
 *
 * It lives beside the probes rather than inside fn_00419a60 for the reason the split exists
 * at all -- the gather is a page of routing devices to buttons, and it was previously
 * buried in two hundred and fifty lines of instruments. */
void coop_debug_tick(uint32_t self)
{
    /* LF2_COOP_REFS=<frame> -- scan memory for pointers to the player records. Outside the
     * LF2_COOP_DEBUG block on purpose: it is a one-shot scan and does not want the slot
     * table wall alongside it. */
    {
        const char *rf = lf2_environment_get(LF2_ENV_COOP_REFS);
        if (rf && hostwin_frames() == atol(rf)) coop_refs_scan(self);
        /* LF2_COOP_TABLE=<frame> | live[+<n>]. The frame form is exact but brittle: the
         * data load does not take the same number of frames every run, so a scripted route
         * can be at character selection on the frame it reached the match on last time --
         * which is how the first table dump came back 400 lines of untouched defaults. The
         * `live` form fires <n> frames after the first frame on which any entry is not an
         * untouched default, so it lands in a match or does not fire at all. */
        {
            const char *tf2 = lf2_environment_get(LF2_ENV_COOP_TABLE);
            static long live_at = -1, fired = -1;
            if (tf2 && *tf2) {
                if (strncmp(tf2, "live", 4) == 0) {
                    const long after = tf2[4] == '+' ? atol(tf2 + 5) : 0;
                    if (live_at < 0) {
                        const uint32_t p0 = LD32(self + PLAYER_PTRS);
                        if (p0 && coop_entry_live(p0)) live_at = hostwin_frames();
                    }
                    if (live_at >= 0 && fired < 0 && hostwin_frames() >= live_at + after) {
                        fired = hostwin_frames();
                        lf2_log_writef(LF2_LOG_INFO, "coop-debug",
                                       "coop table: slot 0 first became live at frame %ld\n", live_at);
                        coop_table_dump(self);
                        if (lf2_environment_get(LF2_ENV_COOP_REGISTRY)) coop_registry_dump(self);
                        /* `auto` picks the first LIVE entry past the eight player slots --
                         * the fighter the game put in the table itself -- against its next
                         * neighbour. Which index that is varies between runs, so naming it
                         * by number would silently compare two idle records on a run where
                         * it landed elsewhere. */
                        const char *pr = lf2_environment_get(LF2_ENV_COOP_PAIR);
                        int pi = -1, pj = -1;
                        if (pr && strcmp(pr, "auto") == 0) {
                            for (int k = 8; k < 400; k++) {
                                const uint32_t p = LD32(self + PLAYER_PTRS + 4u * (uint32_t)k);
                                if (p && coop_entry_live(p)) {
                                    pi = k;
                                    pj = k + 1;
                                    break;
                                }
                            }
                            if (pi < 0)
                                lf2_log_writef(LF2_LOG_INFO, "coop-debug",
                                               "coop pair: auto found no live entry past the "
                                               "player slots, so nothing was compared\n");
                        } else if (pr) {
                            if (sscanf(pr, "%d,%d", &pi, &pj) != 2) pi = -1;
                        }
                        if (pi >= 0) coop_pair_diff(self, pi, pj);

                        /* LF2_COOP_SPAWN=<dst>[,<id>[,<+0x364>]][;...] -- a LIST, because
                         * the only way to compare two spawns fairly is to make them in the
                         * SAME run. VS mode randomises the characters, so two runs differ
                         * in the fighters already on the stage; an A/B across runs showed
                         * three portraits changing when one variable had been altered, and
                         * a difference read off that would have been the randomiser.
                         *
                         * The id defaults to 1 (Bandit), which every copy of the game has,
                         * and the spawn position comes from whichever entry is live, found
                         * here rather than assumed to be slot 0. */
                        const char *sp = lf2_environment_get(LF2_ENV_COOP_SPAWN);
                        for (const char *c = sp; c && *c;) {
                            int sd = -1, sid = 1, ssel = -1;
                            const int got = sscanf(c, "%d,%d,%d", &sd, &sid, &ssel);
                            if (got >= 1 && sd >= 0) {
                                int posref = -1;
                                for (int k = 0; k < TABLE_N; k++)
                                    if (LD8(EXISTS + (uint32_t)k)) {
                                        posref = k;
                                        break;
                                    }
                                coop_spawn(self, sd, sid, posref, ssel);
                            } else {
                                lf2_log_writef(LF2_LOG_INFO, "coop-debug",
                                               "coop spawn: REFUSED -- each item must be "
                                               "<index>[,<object id>[,<+0x364>]], got "
                                               "\"%s\"\n",
                                               c);
                            }
                            const char *semi = strchr(c, ';');
                            c = semi ? semi + 1 : NULL;
                        }

                        const char *jn = lf2_environment_get(LF2_ENV_COOP_JOIN);
                        if (jn) {
                            int js = -1, jid = 1;
                            if (sscanf(jn, "%d,%d", &js, &jid) >= 1) coop_join(self, js, jid, NULL, 1);
                            else
                                lf2_log_writef(LF2_LOG_INFO, "coop-debug",
                                               "coop join: REFUSED -- LF2_COOP_JOIN must be "
                                               "<slot>[,<object id>], got \"%s\"\n",
                                               jn);
                        }
                    }
                } else if (hostwin_frames() == atol(tf2)) {
                    coop_table_dump(self);
                }
            }
        }
    }

    coop_spawn_watch(self);

    /* LF2_COOP_TRACK=<index> -- that table entry's position, every 30 frames while it is in
     * the world. The spawn watch only follows a fighter this port created; this follows any
     * of them, which is what a test of "does the pad drive PLAYER TWO's fighter" needs,
     * since that fighter is placed by the game's own character selection.
     *
     * It prints a line saying the entry is NOT in the world too, rather than going quiet:
     * silence would otherwise be indistinguishable from a run that never reached a match,
     * which is the exact confusion a movement assertion must not inherit. */
    {
        const char *tr = lf2_environment_get(LF2_ENV_COOP_TRACK);
        if (tr) {
            const int k = atoi(tr);
            static long last_print;
            const long f = hostwin_frames();
            if (k >= 0 && k < TABLE_N && f - last_print >= 30) {
                last_print = f;
                const uint32_t o = LD32(self + PLAYER_PTRS + 4u * (uint32_t)k);
                /* Only inside a match. The gate byte alone is not enough: it goes up and
                 * down on the character-select screen with the object still at the origin,
                 * so a position sampled there would enter a movement measurement as a jump
                 * from x=0 -- which is exactly the false failure that found this. */
                if (o && LD8(EXISTS + (uint32_t)k) && coop_match_running(self))
                    /* x, y, z -- and +0x18 is Z, not the `y` this line used to call it.
                     * Measured by driving one input at a time: pressing RIGHT moves +0x10
                     * and leaves +0x18 at 334, pressing UP moves +0x18 and not +0x10, and
                     * +0x14 only twitches to -6 mid-jump. Confirmed independently by the
                     * stage data -- up walked +0x18 to exactly 300, which is Brokeback
                     * Clif's `zboundary: 300 510` lower bound.
                     *
                     * The old label mattered: fn_0041a5a0 depth-sorts the world on this
                     * word, and a renderer told it was `y` would sort on the jump height. */
                    lf2_log_writef(LF2_LOG_INFO, "coop-debug",
                                   "coop track: frame %ld entry %d x=%d y=%d z=%d +000=%d\n", f, k,
                                   (int32_t)LD32(o + 0x10), (int32_t)LD32(o + 0x14), (int32_t)LD32(o + 0x18),
                                   (int32_t)LD32(o + 0x000));
                else
                    lf2_log_writef(LF2_LOG_INFO, "coop-debug", "coop track: frame %ld entry %d is NOT in the world\n",
                                   f, k);
            }
        }
    }

    /* LF2_COOP_DEBUG=1 -- the player slot table as the game maintains it, printed whenever
     * it changes: the device selector per slot and the object pointer per slot. This is the
     * ground truth for "can a player join after the stage started" -- a slot going live
     * mid-match would show up here as a selector and a pointer appearing together. Printing
     * on CHANGE only, with the frame, so the log is the transitions rather than a wall. */
    if (lf2_environment_get(LF2_ENV_COOP_DEBUG)) {
        static uint32_t last_sel[8], last_obj[8];
        static int first = 1;
        for (int i = 0; i < 8; i++) {
            const uint32_t sv = LD32(DEVSEL + 4u * (uint32_t)i);
            const uint32_t ov = LD32(self + PLAYER_PTRS + 4u * (uint32_t)i);
            if (!first && sv == last_sel[i] && ov == last_obj[i]) continue;
            last_sel[i] = sv;
            last_obj[i] = ov;
            lf2_log_writef(LF2_LOG_INFO, "coop-debug", "coop f%ld slot %d: devsel=%d obj=%08x\n", hostwin_frames(), i,
                           (int32_t)sv, ov);
        }
        first = 0;

        /* LF2_COOP_DIFF=<frame> -- what actually distinguishes a slot that is PLAYING from
         * one that is not. Every one of the eight player objects already exists from the
         * moment character selection runs, so joining cannot be an object being created; it
         * has to be a field. This prints the dwords where a playing slot and an idle one
         * differ, which is the shortest path to that field. */
        /* LF2_COOP_SNAP=<a>,<b> -- slot 0's object at frame a against the same object at
         * frame b. Diffing one slot against ANOTHER slot cannot show a join, because both
         * records exist the whole time; diffing one slot across the moment it joins can.
         * The window is 0x800, twice the 0x420 stride, so a field past the stride is not
         * silently outside the picture. */
        {
            enum { SNAP_N = 0x800 / 4 };
            static uint32_t snap[SNAP_N];
            static int have;
            const char *spec = lf2_environment_get(LF2_ENV_COOP_SNAP);
            long fa = 0, fb = 0;
            if (spec && sscanf(spec, "%ld,%ld", &fa, &fb) == 2) {
                const uint32_t o = LD32(self + PLAYER_PTRS);
                const long f = hostwin_frames();
                if (o && f == fa && !have) {
                    for (int k = 0; k < SNAP_N; k++) snap[k] = LD32(o + 4u * (uint32_t)k);
                    have = 1;
                    lf2_log_writef(LF2_LOG_INFO, "coop-debug", "coop snap: slot 0 object %08x captured at frame %ld\n",
                                   o, f);
                } else if (o && f == fb) {
                    if (!have) {
                        lf2_log_writef(LF2_LOG_INFO, "coop-debug",
                                       "coop snap: NOTHING was captured at frame %ld, so this "
                                       "diff compares against zeros -- ignore it\n",
                                       fa);
                    } else {
                        int n = 0;
                        for (int k = 0; k < SNAP_N; k++) {
                            const uint32_t v = LD32(o + 4u * (uint32_t)k);
                            if (v == snap[k]) continue;
                            if (++n <= 80)
                                lf2_log_writef(LF2_LOG_INFO, "coop-debug",
                                               "  +%03x  before=%-11d after=%-11d "
                                               "(%08x / %08x)\n",
                                               k * 4, (int32_t)snap[k], (int32_t)v, snap[k], v);
                        }
                        lf2_log_writef(LF2_LOG_INFO, "coop-debug", "coop snap: %d differing dwords of %d\n", n, SNAP_N);
                    }
                }
            }
        }

        /* LF2_COOP_TEST=<frame> -- set the next unset bit of the joined-players mask at that
         * frame, mid-match, and see whether a player appears. 0x00451288 was found by
         * diffing .data across a character-select join and again across a SECOND join: it
         * reads 1 with one player and 3 with two, which is a per-player bitmask and not a
         * count. Whether flipping it mid-match is enough -- whether a fighter follows -- is
         * exactly what this answers, and it is the whole question for drop-in. */
        {
            const char *tf = lf2_environment_get(LF2_ENV_COOP_TEST);
            if (tf && hostwin_frames() == atol(tf)) {
                const uint32_t m = LD32(JOINED_MASK);
                int bit = 0;
                while (bit < 8 && (m & (1u << bit))) bit++;
                if (bit < 8) {
                    /* The mask alone does nothing mid-match -- measured -- because it is
                     * read when the match STARTS. So this also gives the idle slot the state
                     * a playing one has, by copying the playing record over it. Two fields
                     * are kept: +368, which differs per slot and looks like the slot's own
                     * buffer, and the x position, so the new player does not land exactly on
                     * top of the one it was copied from.
                     *
                     * RESULT: not sufficient. The mask is set and the record is complete and
                     * still no third fighter appears or draws. So being a filled-in player
                     * record is not what puts a fighter in the world -- there is a list of
                     * active objects it also has to be in, and finding that is the next
                     * step. Kept as the probe that established it. */
                    const uint32_t src = LD32(self + PLAYER_PTRS);
                    const uint32_t dst = LD32(self + PLAYER_PTRS + 4u * (uint32_t)bit);
                    if (src && dst) {
                        const uint32_t keep368 = LD32(dst + 0x368);
                        for (uint32_t o = 0; o < 0x420u; o += 4) ST32(dst + o, LD32(src + o));
                        ST32(dst + 0x368, keep368);
                        ST32(dst + 0x10, LD32(src + 0x10) + 120u);   /* x, in ints   */
                        ST32(dst + 0x5c, LD32(src + 0x5c) + 0x400u); /* x, the float */
                    }
                    ST32(JOINED_MASK, m | (1u << bit));
                    lf2_log_writef(LF2_LOG_INFO, "coop-debug",
                                   "coop test: joined mask %08x -> %08x (set bit %d), "
                                   "record %08x cloned from %08x\n",
                                   m, m | (1u << bit), bit, dst, src);
                } else {
                    lf2_log_writef(LF2_LOG_INFO, "coop-debug", "coop test: mask %08x is already full, nothing set\n",
                                   m);
                }
            }
        }

        const char *at = lf2_environment_get(LF2_ENV_COOP_DIFF);
        if (at && hostwin_frames() == atol(at)) {
            const uint32_t a = LD32(self + PLAYER_PTRS + 0);
            const uint32_t b = LD32(self + PLAYER_PTRS + 4u * 4u);
            lf2_log_writef(LF2_LOG_INFO, "coop-debug", "coop diff: playing=%08x idle=%08x\n", a, b);
            if (a && b) {
                int n = 0;
                for (uint32_t o = 0; o < 0x420u; o += 4) {
                    const uint32_t va = LD32(a + o), vb = LD32(b + o);
                    if (va == vb) continue;
                    if (++n <= 60)
                        lf2_log_writef(LF2_LOG_INFO, "coop-debug", "  +%03x  playing=%-11d idle=%-11d (%08x / %08x)\n",
                                       o, (int32_t)va, (int32_t)vb, va, vb);
                }
                lf2_log_writef(LF2_LOG_INFO, "coop-debug", "coop diff: %d differing dwords of %d\n", n, 0x420 / 4);
            }
        }
    }
}

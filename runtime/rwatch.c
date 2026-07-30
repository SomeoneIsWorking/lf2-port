/* Guest memory read-watch. Its own translation unit because the load macros in
 * guest.h reference it, so every target that includes them must be able to link it,
 * including the test harnesses that do not pull in guest.c. */
#include "guest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- read-watch ----
 * LF2_READ_WATCH=<lo>:<hi> reports which offsets in a span the game loads.
 *
 * The raw set is useless on its own: the game rebuilds its input bitmask by sweeping the
 * whole key array in order, so every frame reads all 250 offsets no matter what screen it
 * is on. That bulk scan is separable by *shape* rather than by call site -- it is a long
 * strictly-ascending run, whereas a deliberate "is this key down" check is an isolated,
 * out-of-sequence access. Filtering ascending runs leaves the discriminating reads, and
 * costs nothing on the hot path, unlike tracking the reading instruction's address.
 *
 * Sweeps are closed by the frame, which is the natural boundary. Closing them on a
 * repeated offset instead splits the single array scan in two and leaves its tail looking
 * like a deliberate check -- a phantom finding, which is exactly what the first version
 * of this reported.
 */
uint32_t g_rwatch_lo, g_rwatch_hi;
static long rwatch_hits;

/* Shortest ascending run treated as a scan. Genuine checks of adjacent keys do occur (the
 * arrow keys are 0x25..0x28), so this must sit above any plausible cluster. */
enum { SCAN_RUN = 16, RW_SPAN = 4096, RW_SEQ = 16384 };

static uint8_t  rw_prev[RW_SPAN], rw_seen[RW_SPAN];
static uint16_t rw_seq[RW_SEQ];
static int rw_seqn, rw_have_prev, rw_sweeps;

void rwatch_hit(uint32_t a)
{
    const uint32_t off = a - g_rwatch_lo;
    if (off >= RW_SPAN || rw_seqn == RW_SEQ) return;
    rwatch_hits++;
    rw_seen[off] = 1;
    rw_seq[rw_seqn++] = (uint16_t)off;
}

/* Called once per presented frame. */
/* Set once the game is seen polling the key named by LF2_AUTOKEY_AFTER. Scripted input
 * keys off this rather than a stopwatch: the point at which character selection starts
 * asking about the player keys is a fact about the game's state, whereas "32 seconds in"
 * is a guess that drifts with load time. */
static int trigger_seen;

int rwatch_triggered(void) { return trigger_seen; }

void rwatch_frame(void)
{
    if (!g_rwatch_hi || rw_seqn == 0) return;
    rw_sweeps++;

    static uint8_t cur[RW_SPAN];
    memset(cur, 0, sizeof cur);
    for (int i = 0; i < rw_seqn; ) {
        int j = i + 1;
        while (j < rw_seqn && rw_seq[j] == rw_seq[j - 1] + 1) j++;
        if (j - i < SCAN_RUN)
            for (int k = i; k < j; k++) cur[rw_seq[k]] = 1;
        i = j;
    }

    const char *after = getenv("LF2_AUTOKEY_AFTER");
    if (after && !trigger_seen) {
        const uint32_t want = (uint32_t)strtoul(after, NULL, 0);
        if (want < RW_SPAN && cur[want]) {
            trigger_seen = 1;
            fprintf(stderr, "input trigger: game polled key %02x, starting key script\n",
                    want);
        }
    }

    if (!rw_have_prev || memcmp(cur, rw_prev, sizeof cur) != 0) {
        fprintf(stderr, "read set changed (frame %d, +0x%x, scans filtered):",
                rw_sweeps, g_rwatch_lo);
        int any = 0;
        for (int i = 0; i < RW_SPAN; i++) if (cur[i]) { fprintf(stderr, " %02x", i); any = 1; }
        if (!any) fprintf(stderr, " (nothing but sequential scans)");
        fprintf(stderr, "\n");
        memcpy(rw_prev, cur, sizeof cur);
        rw_have_prev = 1;
    }
    memset(rw_seen, 0, sizeof rw_seen);
    rw_seqn = 0;
}

/* Feeds a synthetic frame: a full ascending scan plus four isolated checks. Only the four
 * may be reported. Without this the filter would ship having never been seen to separate
 * the two, and "(nothing but sequential scans)" would be indistinguishable from a filter
 * that discards everything. */
void rwatch_selftest(void)
{
    fprintf(stderr, "LF2_READ_WATCH selftest: expect exactly '68 57 49 26' below\n");
    const uint32_t save_hi = g_rwatch_hi;
    g_rwatch_hi = g_rwatch_lo + RW_SPAN;
    for (unsigned i = 0; i < 250; i++) rwatch_hit(g_rwatch_lo + i);
    const unsigned keys[] = { 0x68, 0x57, 0x49, 0x26 };
    for (unsigned i = 0; i < 4; i++) rwatch_hit(g_rwatch_lo + keys[i]);
    rwatch_frame();
    g_rwatch_hi = save_hi;
    rw_have_prev = 0;
    fprintf(stderr, "LF2_READ_WATCH selftest: done\n");
}

void rwatch_report(void)
{
    if (!g_rwatch_hi) return;
    fprintf(stderr, "LF2_READ_WATCH [%08x,%08x): %ld reads\n",
            g_rwatch_lo, g_rwatch_hi, rwatch_hits);
    if (rwatch_hits == 0)
        fprintf(stderr, "  nothing in that span was read -- the watch saw NOTHING, which\n"
                        "  is not the same as the game not reading its input.\n");
}

void rwatch_init(void)
{
    const char *spec = getenv("LF2_READ_WATCH");
    /* LF2_AUTOKEY_AFTER needs the key array watched to see the trigger, so arm it here
     * rather than making the caller remember to pass both. */
    if (!spec && getenv("LF2_AUTOKEY_AFTER")) spec = "0x455378:0x455478";
    if (!spec) return;
    char *end = NULL;
    const uint32_t lo = (uint32_t)strtoul(spec, &end, 0);
    if (!end || *end != ':') {
        fprintf(stderr, "LF2_READ_WATCH must be <lo>:<hi>, got \"%s\"\n", spec);
        exit(2);                       /* refuse rather than silently watch nothing */
    }
    const uint32_t hi = (uint32_t)strtoul(end + 1, NULL, 0);
    if (hi <= lo) {
        fprintf(stderr, "LF2_READ_WATCH: empty span %08x:%08x\n", lo, hi);
        exit(2);
    }
    g_rwatch_lo = lo; g_rwatch_hi = hi;
    if (getenv("LF2_READ_WATCH_SELFTEST")) rwatch_selftest();
    fprintf(stderr, "LF2_READ_WATCH watching [%08x,%08x)\n", lo, hi);
}


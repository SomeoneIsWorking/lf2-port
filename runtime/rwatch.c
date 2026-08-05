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

/* ---- raw mode ----
 * The run-filter above exists for one question (which key does this screen check) and is
 * wrong for another: when the span is an ARRAY the game sweeps, the sweep IS the finding.
 * Raw mode counts every read per byte and reports the profile, so "does the update loop
 * touch entry 12 at all" is answerable -- a bounded loop over a count leaves the tail of
 * the array at zero, and a full sweep with a per-entry test does not. Those are different
 * mechanisms and the filtered view cannot tell them apart. */
static long rw_count[RW_SPAN];
static int rw_raw = -1;

void rwatch_hit(uint32_t a)
{
    const uint32_t off = a - g_rwatch_lo;
    if (off >= RW_SPAN) return;
    rwatch_hits++;
    if (rw_raw > 0) { rw_count[off]++; return; }
    if (rw_seqn == RW_SEQ) return;
    rw_seen[off] = 1;
    rw_seq[rw_seqn++] = (uint16_t)off;
}

/* Prints the per-BYTE read profile, collapsing equal-count neighbours into a range so a
 * 400-entry sweep is one line rather than 400. Zero counts are named explicitly rather
 * than omitted: "entries 12..399 were read 0 times" is the result, and a report that only
 * listed what WAS read would leave the reader to notice an absence. */
static void rwatch_raw_report(const char *when)
{
    const int n = RW_SPAN;
    long total = 0;
    for (int i = 0; i < n; i++) total += rw_count[i];
    fprintf(stderr, "read profile (%s) [%08x,%08x), %ld reads over %u bytes:\n",
            when, g_rwatch_lo, g_rwatch_hi, total, g_rwatch_hi - g_rwatch_lo);
    if (total == 0) {
        fprintf(stderr, "  NOTHING in the span was read. The watch saw zero loads, which is\n"
                        "  a statement about the watch as much as about the game -- check\n"
                        "  the span is the one you meant before reading anything into it.\n");
        return;
    }
    const int last = (int)(g_rwatch_hi - g_rwatch_lo);
    for (int i = 0; i < last && i < n; ) {
        int j = i + 1;
        while (j < last && j < n && rw_count[j] == rw_count[i]) j++;
        if (rw_count[i] == 0 && j - i > 1)
            fprintf(stderr, "  +%03x..+%03x  0\n", i, j - 1);
        else if (j - i == 1)
            fprintf(stderr, "  +%03x         %ld\n", i, rw_count[i]);
        else
            fprintf(stderr, "  +%03x..+%03x  %ld each\n", i, j - 1, rw_count[i]);
        i = j;
    }
}

void rwatch_raw_flush(const char *when)
{
    if (rw_raw <= 0) return;
    rwatch_raw_report(when);
    memset(rw_count, 0, sizeof rw_count);
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
    if (rw_raw > 0) return;              /* raw mode reports on demand, not per frame */
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
    const uint32_t save_hi = g_rwatch_hi;
    g_rwatch_hi = g_rwatch_lo + RW_SPAN;
    if (rw_raw > 0) {
        /* Raw mode has its own failure to rule out: a counter that never increments, and a
         * range collapse that hides the one entry that differs. Reading dword 3 four times
         * and nothing else must come back as exactly that, with the rest at zero. */
        fprintf(stderr, "LF2_READ_WATCH selftest (raw): expect +00c read 4 times, "
                        "everything else 0\n");
        for (unsigned i = 0; i < 4; i++) rwatch_hit(g_rwatch_lo + 12);
        const uint32_t save_lo_hi = g_rwatch_hi;
        g_rwatch_hi = g_rwatch_lo + 32;             /* report only the first 32 bytes */
        rwatch_raw_flush("selftest");
        g_rwatch_hi = save_lo_hi;
    } else {
        fprintf(stderr, "LF2_READ_WATCH selftest: expect exactly '68 57 49 26' below\n");
        for (unsigned i = 0; i < 250; i++) rwatch_hit(g_rwatch_lo + i);
        const unsigned keys[] = { 0x68, 0x57, 0x49, 0x26 };
        for (unsigned i = 0; i < 4; i++) rwatch_hit(g_rwatch_lo + keys[i]);
        rwatch_frame();
    }
    g_rwatch_hi = save_hi;
    rw_have_prev = 0;
    rwatch_hits = 0;
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
    if (hi - lo > RW_SPAN) {
        fprintf(stderr, "LF2_READ_WATCH: span %08x:%08x is %u bytes, past the %d-byte "
                        "window -- reads past it would be dropped silently\n",
                lo, hi, hi - lo, RW_SPAN);
        exit(2);
    }
    g_rwatch_lo = lo; g_rwatch_hi = hi;
    rw_raw = getenv("LF2_READ_WATCH_RAW") != NULL;
    if (getenv("LF2_READ_WATCH_SELFTEST")) rwatch_selftest();
    fprintf(stderr, "LF2_READ_WATCH watching [%08x,%08x)%s\n", lo, hi,
            rw_raw > 0 ? " in raw per-byte counting mode" : "");
}


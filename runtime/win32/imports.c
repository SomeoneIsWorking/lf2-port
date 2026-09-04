/* Host implementations of the imports lf2.exe calls.
 *
 * Calling convention: guest CALL pushes a return address before dispatch, so on
 * entry [ESP] is the return address and [ESP+4+4n] is argument n. stdcall (Win32) pops
 * its own arguments; cdecl (CRT) leaves that to the caller. */
#include "lf2_log.h"
#include "environment.h"
#include "guest.h"
#include "hostwin.h"
#include "guest_map.h"
#include "paths.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>
#include <strings.h>

#define ARG(n) LD32(R(ESP) + 4 + 4 * (n))

static void ret_stdcall(int nargs, uint32_t value)
{
    R(EAX) = value;
    R(ESP) += 4 + 4u * (unsigned)nargs;
}

static void ret_cdecl(uint32_t value)
{
    R(EAX) = value;
    R(ESP) += 4;
}

/* ---- guest heap ----
 * Bump allocator over a dedicated region. Free is a no-op for now: the game allocates
 * its sprite and stage data once at load, so this holds for a session, but it will need
 * a real free list before anything long-running. */
enum { HEAP_BASE = GUEST_HEAP_BASE, HEAP_SIZE = GUEST_HEAP_SIZE };
static uint32_t heap_next = HEAP_BASE;

/* How far the bump allocator has grown. The character-select slot state lives in a
 * malloc'd structure, so a .data-only snapshot cannot see it -- diffing .data across a
 * cursor move found only free-running counters, and the negative control changed the
 * same ones. Exposed so the dump can cover the heap that is actually in use rather than
 * a fixed guess at its size. */
uint32_t guest_heap_used(void)
{
    return heap_next - HEAP_BASE;
}

static uint32_t guest_alloc(uint32_t size)
{
    size = (size + 15u) & ~15u;
    if (heap_next + size > HEAP_BASE + HEAP_SIZE) {
        lf2_log_writef(LF2_LOG_INFO, "imports", "guest heap exhausted\n");
        abort();
    }
    uint32_t p = heap_next;
    heap_next += size;
    return p;
}

/* ---- handlers ---- */

typedef void (*Handler)(void);

static void h_GetSystemTimeAsFileTime(void)
{
    /* Windows epoch is 1601-01-01, in 100 ns ticks. */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t ft = (uint64_t)ts.tv_sec * 10000000ull + (uint64_t)ts.tv_nsec / 100ull + 116444736000000000ull;
    uint32_t out = ARG(0);
    ST32(out, (uint32_t)ft);
    ST32(out + 4, (uint32_t)(ft >> 32));
    ret_stdcall(1, 0);
}

int lf2_loading_now(void); /* defined with the file handlers below */
long hostwin_frames(void); /* runtime/video/ddraw.c */

/* See "the guest clock" below: the offset the port owes the guest for waits it
 * decided not to take. Declared here because h_Sleep is what pays into it. */
static uint64_t guest_clock_offset_ns;
extern long load_skipped_sleeps;

/* Sleep requested, accumulated so the load span can be split into "sleeping" versus
 * "working". Ranking imports by CALL COUNT hid this completely: Sleep is 0.07% of the
 * calls and the overwhelming majority of the time. */
static double sleep_ns_total;
static long sleep_calls_total;
enum { SLEEP_SITES = 16 };
static uint32_t sleep_site[SLEEP_SITES];
static long sleep_site_n[SLEEP_SITES];
static long sleep_site_at_first[SLEEP_SITES], sleep_site_at_last[SLEEP_SITES];
static int sleep_nsites;

/* ---- LF2_CLOCK_SITES -- who looks at the clock, and who looks at it WITHOUT SLEEPING ----
 *
 * The question this exists to answer is issue #18's: a virtual guest clock, advanced only by
 * the game's own sleeps, hung until a microsecond was credited per READ as well -- so some
 * loop watches the clock and never sleeps, and with a clock that only sleeps advance, it
 * waits for a time that can never arrive. A microsecond a read makes it terminate, but it
 * also distorts the timeline, so the loop wants finding rather than papering over.
 *
 * CALL COUNT ALONE WOULD NOT FIND IT, which is why this measures something else as well. The
 * frame pacer reads the clock constantly and is perfectly well behaved, because it sleeps
 * between reads. What distinguishes a spin is the RUN: how many reads a site made since the
 * last Sleep of any kind. A well-behaved deadline loop's run is a handful; a spin's is
 * enormous. Both are reported, so a site cannot look innocent on one and be caught on the
 * other.
 *
 * The negative is reported too: a run with the variable set that names NO site says so, with
 * the number of reads it did see, rather than printing an empty list that reads like "there
 * is no spin". */
enum { CLOCK_SITES = 24 };
static uint32_t clk_site[CLOCK_SITES];
static long clk_site_n[CLOCK_SITES];          /* total reads from this site */
static long clk_site_max_run[CLOCK_SITES];    /* longest burst with no Sleep in it */
static long clk_site_max_at[CLOCK_SITES];     /* and the presented frame it happened on --
                                               * "during the load" and "during play" are
                                               * different diagnoses and this is what tells
                                               * them apart */
static const char *clk_site_api[CLOCK_SITES]; /* which of the three it came through */
static int clk_nsites;
static long clk_reads_total, clk_run, clk_dropped;

static void clock_read_note(const char *api)
{
    static int on = -1;
    if (on < 0) on = lf2_environment_get(LF2_ENV_CLOCK_SITES) != NULL;
    if (!on) return;

    clk_reads_total++;
    clk_run++;
    const uint32_t ra = LD32(R(ESP));
    int k = 0;
    for (; k < clk_nsites; k++)
        if (clk_site[k] == ra) break;
    if (k == clk_nsites) {
        if (clk_nsites >= CLOCK_SITES) {
            clk_dropped++;
            return;
        }
        clk_site[clk_nsites] = ra;
        clk_site_api[clk_nsites] = api;
        clk_nsites++;
    }
    clk_site_n[k]++;
    if (clk_run > clk_site_max_run[k]) {
        clk_site_max_run[k] = clk_run;
        clk_site_max_at[k] = hostwin_frames();
    }
}

void clock_sites_report(void)
{
    if (!lf2_environment_get(LF2_ENV_CLOCK_SITES)) return;
    if (clk_reads_total == 0) {
        lf2_log_writef(LF2_LOG_INFO, "imports",
                       "clock sites: the guest NEVER read its clock -- no site to name, and "
                       "nothing here is evidence about spinning\n");
        return;
    }
    lf2_log_writef(LF2_LOG_INFO, "imports",
                   "clock sites: %ld reads from %d call site(s)%s. `run` is reads since the "
                   "last Sleep -- a large one is a loop watching the clock without "
                   "sleeping\n",
                   clk_reads_total, clk_nsites, clk_dropped ? " (and more sites than this build can hold; some were DROPPED)" : "");
    for (int k = 0; k < clk_nsites; k++) lf2_log_writef(LF2_LOG_INFO, "imports", "  from=%08x  %-24s reads=%-9ld longest run=%-7ld at frame %ld\n", clk_site[k], clk_site_api[k], clk_site_n[k], clk_site_max_run[k], clk_site_max_at[k]);
    if (clk_dropped)
        lf2_log_writef(LF2_LOG_INFO, "imports",
                       "  ... and %ld reads from call sites past the %d this build records, "
                       "which are NOT in the list above\n",
                       clk_dropped, CLOCK_SITES);
}

/* Sleep was a no-op returning immediately, so the game's frame pacing -- which is a
 * Sleep in a loop -- became a spin, pegging a core at ~96% for a 30 fps 2D fighter.
 * Honouring it is also the faithful behaviour: on Windows this blocks the thread, and the
 * game is written expecting that. Sleep(0) is a yield, not a delay. */
static void h_Sleep(void)
{
    const uint32_t ms = ARG(0);
    clk_run = 0; /* a Sleep ends any run of clock reads */
    /* LF2_NO_SLEEP restores the old no-op, purely so the cost of honouring Sleep can be
     * A/B measured. Not a tuning knob: skipping it burns a whole core. */
    if (lf2_environment_get(LF2_ENV_NO_SLEEP)) {
        ret_stdcall(1, 0);
        return;
    }
    if (lf2_environment_get(LF2_ENV_SLEEP_DEBUG)) {
        static long n, total_ms, hist[6]; /* 0, 1, 2-5, 6-15, 16-50, 50+ */
        n++;
        total_ms += ms;
        hist[ms == 0 ? 0 : ms == 1 ? 1 : ms <= 5 ? 2 : ms <= 15 ? 3 : ms <= 50 ? 4 : 5]++;
        if (n % 2000 == 0)
            lf2_log_writef(LF2_LOG_INFO, "imports",
                           "sleep: %ld calls, %ld ms requested; 0:%ld 1:%ld 2-5:%ld "
                           "6-15:%ld 16-50:%ld 50+:%ld\n",
                           n, total_ms, hist[0], hist[1], hist[2], hist[3], hist[4], hist[5]);
    }
    /* While the data load is running, do not sleep out the frame. The loop advances one
     * data file per tick and then waits ~33 ms for a deadline that exists to pace a 30 fps
     * game, not a loading screen -- so the wait is pure latency, 9.5 s of a 14.8 s load.
     * Frame pacing is restored the moment the load stops; this is not LF2_NO_SLEEP, which
     * disables it everywhere and burns a core during play. */
    static int fast = -1;
    if (fast < 0) fast = lf2_environment_get(LF2_ENV_SLOW_LOAD) == NULL;
    if (fast && lf2_loading_now()) {
        /* Fast-forward rather than no-op. The caller is a deadline loop -- it sleeps, then
         * re-reads the clock, and goes round again if the deadline has not passed -- so a
         * Sleep that returns instantly without moving the clock does not shorten the wait,
         * it converts it into a spin. Crediting the requested time (a 0 ms sleep is a yield,
         * so it counts as the shortest tick the loop can distinguish) makes the deadline
         * arrive on the next check instead of hundreds of checks later. */
        guest_clock_offset_ns += (uint64_t)(ms ? ms : 1u) * 1000000ull;
        load_skipped_sleeps++;
        ret_stdcall(1, 0);
        return;
    }

    /* CREDITED HERE TOO, not only on the load's fast path. The clock is the frame counter,
     * and a wait that produces no frames -- the startup waits before the load is even
     * flagged -- would otherwise wait for a time that can never arrive. Measured: without
     * this the run used 1.8 s of CPU in 200 s of wall and reached no screen at all.
     *
     * It costs nothing during play, and that is a property of the game rather than luck: a
     * frame period of 33.33 ms is above the 33 ms threshold fn_0043cf40 compares against, so
     * once frames are flowing the loop always takes its "overdue" branch and never sleeps.
     * The credit is therefore constant from the end of the load onwards, and the clock is
     * the frame counter plus a fixed startup offset.
     *
     * ONE MILLISECOND MORE THAN ASKED FOR, and that is not a fudge -- it is what a real
     * Sleep does. Sleep(n) returns after AT LEAST n; nanosleep never returns early. Crediting
     * exactly n models a sleep that returns exactly on time, and the game's pacer lands on
     * its own boundary and stops dead there: it sleeps `remaining` when remaining <= 5, so it
     * arrives at elapsed == 33 exactly, where `elapsed <= 33` sends it to the sleep path and
     * `remaining == 0` sends it past the Sleep -- neither working nor waiting. Measured
     * before the +1: 59,331,701 clock reads at frame 0 with no Sleep and no frame ever
     * presented, at 99% of a core. A real clock crosses the boundary through overshoot; an
     * exact one has to be told that a sleep is a floor, not an equality. */
    guest_clock_offset_ns += ((uint64_t)(ms ? ms : 1u) + 1ull) * 1000000ull;

    if (ms == 0) sched_yield();
    else {
        struct timespec req = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000L};
        nanosleep(&req, NULL);
    }
    sleep_ns_total += (double)ms * 1e6;
    sleep_calls_total++;
    /* Which guest loop is sleeping? [ESP] held the return address on entry, and
     * ret_stdcall has not run yet. Distinct sites, not a sample: there are only a
     * handful, and a sample of a rare site would read as absent. */
    {
        const uint32_t ra = LD32(R(ESP));
        int k = 0;
        for (; k < sleep_nsites; k++)
            if (sleep_site[k] == ra) break;
        if (k == sleep_nsites && sleep_nsites < (int)(sizeof sleep_site / sizeof *sleep_site)) sleep_site[sleep_nsites++] = ra;
        if (k < (int)(sizeof sleep_site / sizeof *sleep_site)) sleep_site_n[k]++;
    }
    ret_stdcall(1, 0);
}

/* ---- the guest clock, and it is the FRAME COUNTER ----
 *
 * Guest time is exactly `presented frames * 33.33 ms` and nothing else. It never reads the
 * wall, so how much of the game's timeline has passed by frame N is a property of the game
 * rather than of how fast or how busy the machine is. That is issue #18: every scripted
 * route is a schedule of presented frames, and with a wall-derived clock the game's progress
 * between two of them depended on load -- measured, a route that reaches a match every time
 * on an idle box reported "screens reached -- NONE" under fourteen busy loops.
 *
 * WHY THE FRAME COUNTER AND NOT A CREDIT PER SLEEP, which was the obvious first answer and
 * is wrong. fn_0043cf40, the game's main loop, paces itself like this:
 *
 *     now = timeGetTime(); elapsed = now - last
 *     if (elapsed <= 33)  { remaining = last + 33 - now; if (remaining > 0) Sleep(min(rem,5)); }
 *     else                { if (elapsed > 100) last = now - 100;   // resync, still overdue
 *                           run the frame; last += 33; ... }       // and round again
 *
 * When it is behind it does NOT sleep -- it runs frames back to back until it catches up. A
 * clock that only sleeps advance therefore never lets it catch up, and it runs frames
 * forever: measured at 111 s of user CPU against 20 s. LF2_CLOCK_SITES named the two reads
 * (0x0043d162 and 0x0043d195, 218,937 reads each) and the frames they peak on -- 8 and 9,
 * the data load, and 2141, the match starting. Every one of those iterations produces a
 * frame, so a clock driven by the frame counter is exactly the one that lets the loop
 * converge, and it converges by doing the work the loop exists to do.
 *
 * The other side of it: with a frame period at or above the game's own 33 ms threshold the
 * loop is always "overdue", so it never sleeps and never waits -- which is why REAL-TIME
 * PACING MOVES TO THE HOST, into the present (runtime/video/ddraw.c). The game runs its loop flat
 * out and the host holds each frame until the wall catches up. That is the ordinary shape of
 * an emulator: the guest counts, the host paces.
 *
 * Not to be confused with SCALING time, which was tried and measured and is worse: running
 * the clock 4x/16x/32x faster during the load gave 3.5 s / 4.7 s / 7.0 s against 3.6 s at
 * 1x. A jumping clock makes the game do more catch-up work, not less. */
/* The tick is GUEST_FRAME_NS in hostwin.h, shared with the host pacer that honours it. */

static uint64_t guest_ns(void)
{
    return (uint64_t)hostwin_frames() * (uint64_t)GUEST_FRAME_NS + guest_clock_offset_ns;
}

static void h_GetTickCount(void)
{
    clock_read_note("GetTickCount");
    ret_stdcall(0, (uint32_t)(guest_ns() / 1000000ull));
}

static void h_QueryPerformanceCounter(void)
{
    clock_read_note("QueryPerformanceCounter");
    const uint64_t v = guest_ns();
    ST32(ARG(0), (uint32_t)v);
    ST32(ARG(0) + 4, (uint32_t)(v >> 32));
    ret_stdcall(1, 1);
}

static void h_GetVersionExA(void)
{
    /* Report Windows XP SP3; the game only version-gates on >= 5.1. */
    uint32_t p = ARG(0);
    ST32(p + 4, 5);     /* major */
    ST32(p + 8, 1);     /* minor */
    ST32(p + 12, 2600); /* build */
    ST32(p + 16, 2);    /* VER_PLATFORM_WIN32_NT */
    ret_stdcall(1, 1);
}

static void h_ret0_0(void)
{
    ret_stdcall(0, 0);
}
static void h_ret1_0(void)
{
    ret_stdcall(0, 1);
}
static void h_ret0_1(void)
{
    ret_stdcall(1, 0);
}
static void h_ret1_4(void)
{
    ret_stdcall(4, 1);
}

static void h_GetCurrentProcess(void)
{
    ret_stdcall(0, 0xFFFFFFFFu);
}
static void h_GetCurrentProcessId(void)
{
    ret_stdcall(0, 0x1234);
}
static void h_GetCurrentThreadId(void)
{
    ret_stdcall(0, 0x5678);
}
static void h_GetModuleHandleA(void)
{
    ret_stdcall(1, 0x400000);
}

static void h_GetStartupInfoA(void)
{
    uint32_t p = ARG(0);
    for (int i = 0; i < 68; i += 4) ST32(p + (uint32_t)i, 0);
    ST32(p, 68);
    ret_stdcall(1, 0);
}

static void h_InterlockedExchange(void)
{
    uint32_t addr = ARG(0), val = ARG(1), old = LD32(addr);
    ST32(addr, val);
    ret_stdcall(2, old);
}

static void h_InterlockedCompareExchange(void)
{
    uint32_t addr = ARG(0), val = ARG(1), cmp = ARG(2), old = LD32(addr);
    if (old == cmp) ST32(addr, val);
    ret_stdcall(3, old);
}

static void h_lstrlenA(void)
{
    uint32_t p = ARG(0), n = 0;
    while (LD8(p + n)) n++;
    ret_stdcall(1, n);
}

static void h_OutputDebugStringA(void)
{
    uint32_t p = ARG(0);
    lf2_log_writef(LF2_LOG_INFO, "imports", "[dbg] %s\n", (const char *)(g_mem + p));
    ret_stdcall(1, 0);
}

/* ---- CRT (cdecl: caller pops) ---- */

static void h_malloc(void)
{
    ret_cdecl(guest_alloc(ARG(0)));
}
static void h_calloc(void)
{
    uint32_t n = ARG(0) * ARG(1), p = guest_alloc(n);
    memset(g_mem + p, 0, n);
    ret_cdecl(p);
}
static void h_free(void)
{
    ret_cdecl(0);
}
static void h_memcpy(void)
{
    memmove(g_mem + ARG(0), g_mem + ARG(1), ARG(2));
    ret_cdecl(ARG(0));
}
static void h_memset(void)
{
    memset(g_mem + ARG(0), (int)ARG(1), ARG(2));
    ret_cdecl(ARG(0));
}

static void h_getmainargs(void)
{
    /* argc = 1, argv = { "lf2.exe", NULL }, env = { NULL } */
    static uint32_t argv_block;
    if (!argv_block) {
        uint32_t name = guest_alloc(16);
        memcpy(g_mem + name, "lf2.exe", 8);
        argv_block = guest_alloc(8);
        ST32(argv_block, name);
        ST32(argv_block + 4, 0);
    }
    ST32(ARG(0), 1);
    ST32(ARG(1), argv_block);
    ST32(ARG(2), argv_block + 4);
    ret_cdecl(0);
}

static void h_initterm(void)
{
    /* Walk the function-pointer table and call each non-null entry. */
    uint32_t p = ARG(0), end = ARG(1);
    for (; p < end; p += 4) {
        uint32_t fn = LD32(p);
        if (fn) dispatch(fn);
    }
    ret_cdecl(0);
}

static void h_initterm_e(void)
{
    uint32_t p = ARG(0), end = ARG(1);
    for (; p < end; p += 4) {
        uint32_t fn = LD32(p);
        if (fn) dispatch(fn);
    }
    ret_cdecl(0);
}

static uint32_t commode_slot, fmode_slot;
static void h_p_commode(void)
{
    if (!commode_slot) commode_slot = guest_alloc(4);
    ret_cdecl(commode_slot);
}
static void h_p_fmode(void)
{
    if (!fmode_slot) fmode_slot = guest_alloc(4);
    ret_cdecl(fmode_slot);
}

static void h_cdecl0(void)
{
    ret_cdecl(0);
}
static void h_identity(void)
{
    ret_cdecl(ARG(0));
} /* _encode_pointer / _decode_pointer */

static void h_controlfp_s(void)
{
    if (ARG(0)) ST32(ARG(0), 0x8001f);
    ret_cdecl(0);
}

/* ---- CRT file I/O ----
 * Guest FILE* is an opaque token; the host FILE* lives in a side table so guest code
 * never sees a 64-bit pointer. */
enum { MAX_FILES = 64 };
static FILE *files[MAX_FILES];

static uint32_t file_token(FILE *fh)
{
    for (int i = 1; i < MAX_FILES; i++)
        if (!files[i]) {
            files[i] = fh;
            return 0xFE000000u + (uint32_t)i;
        }
    return 0;
}

static FILE *file_of(uint32_t tok)
{
    uint32_t i = tok - 0xFE000000u;
    return (i > 0 && i < MAX_FILES) ? files[i] : NULL;
}

static const char *gstr(uint32_t p)
{
    return (const char *)(g_mem + p);
}

/* Text-mode translation.
 *
 * MSVC's CRT opens files in TEXT mode unless the mode string says "b", and translates
 * CRLF to LF on the way in. Linux does no such thing, so every line the game read
 * carried a trailing \r it does not expect -- enough to send a parse down a branch the
 * real program never takes. The file is slurped, translated, and handed back as an
 * in-memory stream so fscanf/fgets see what they would see on Windows. */
static char *text_buf[MAX_FILES];

/* ---- "is the game loading right now?" ----
 *
 * The data load runs one file per main-loop tick, and the tick period is 33 ms, so a load
 * is (files x 33 ms) -- 315 files measured, which is the ~10 s. The game is therefore
 * loading exactly while it is opening its data files, and that is a far more reliable
 * signal than the loading screen's presenter: keying off fn_004242e0 engaged for nine
 * frames of an entire run, because that function is the ad grid, not the loader.
 *
 * Time-based rather than a frame counter, so a slow file cannot end the window early. */
enum { LOAD_MAXSITES = 24 };
static uint32_t load_site[LOAD_MAXSITES];
static long load_site_n[LOAD_MAXSITES];
static char load_site_path[LOAD_MAXSITES][80];
static int load_nsites;

static uint32_t load_last_open_ms;
static uint32_t load_first_open_ms;
static long load_files;
static uint32_t load_active_ms;
long load_skipped_sleeps;

/* The DATA LOAD's own span, first data file opened to last. The parse span reported
 * alongside it starts at the first fscanf anywhere, which happens on the menu before the
 * player has even chosen a mode, so it carries menu idle time that no amount of load
 * work affects. This is the number the loading work is actually judged by. */
void load_span_report(void)
{
    if (lf2_environment_get(LF2_ENV_LOAD_SITES)) {
        lf2_log_writef(LF2_LOG_INFO, "imports", "load sites: %d distinct guest callers of fopen on data files\n", load_nsites);
        for (int i = 0; i < load_nsites; i++) lf2_log_writef(LF2_LOG_INFO, "imports", "  %08x  %8ld files  first: %s\n", load_site[i], load_site_n[i], load_site_path[i]);
        if (!load_nsites) lf2_log_writef(LF2_LOG_INFO, "imports", "  none -- no data file was opened in this run at all\n");
    }
    if (!lf2_environment_get(LF2_ENV_SCAN_PROF)) return;
    if (!load_files) {
        lf2_log_writef(LF2_LOG_INFO, "imports", "data load: no data files were opened in this run\n");
        return;
    }
    /* ACTIVE loading time, not first-open-to-last-open. The first data file is opened at
     * boot for the menu, so a first-to-last span silently includes however long the player
     * sat on the menu -- which is why the earlier figure looked barely improved. Only gaps
     * shorter than the loading window are counted, so idle time between bursts is not. */
    lf2_log_writef(LF2_LOG_INFO, "imports",
                   "data load: %ld files, %.3f s actively loading (span %.3f s incl. idle), "
                   "%ld frame-pacing sleeps skipped\n",
                   load_files, (double)load_active_ms / 1000.0, (double)(load_last_open_ms - load_first_open_ms) / 1000.0, load_skipped_sleeps);
}

static uint32_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

long lf2_load_active_ms(void)
{
    return (long)load_active_ms;
}

int lf2_loading_now(void)
{
    return load_last_open_ms && (now_ms() - load_last_open_ms) < 300u;
}

static void note_data_open(const char *path)
{
    if (!path) return;
    const size_t n = strlen(path);
    /* the game's own data: data\*.dat, *.txt indexes, and the sprite sheets it pulls in */
    if (n > 4 && (strcasecmp(path + n - 4, ".dat") == 0 || strcasecmp(path + n - 4, ".txt") == 0 || strcasecmp(path + n - 4, ".bmp") == 0)) {
        const uint32_t t = now_ms();
        if (load_last_open_ms && (t - load_last_open_ms) < 300u) load_active_ms += t - load_last_open_ms;
        load_last_open_ms = t;
        if (!load_first_open_ms) load_first_open_ms = t;
        load_files++;
        /* LF2_LOAD_SITES=1 -- which guest code opens the data. Distinct return addresses,
         * with a count each, because the question is "is there ONE loader step function"
         * and a sample cannot answer that. Printed as it is discovered, so a run that
         * finds none says so by printing nothing under a header that was still emitted. */
        if (lf2_environment_get(LF2_ENV_LOAD_SITES)) {
            const uint32_t ra = LD32(R(ESP));
            int k = 0;
            for (; k < load_nsites; k++)
                if (load_site[k] == ra) break;
            if (k == load_nsites && load_nsites < LOAD_MAXSITES) {
                load_site[load_nsites] = ra;
                snprintf(load_site_path[load_nsites], sizeof load_site_path[0], "%s", path);
                load_nsites++;
            }
            if (k < LOAD_MAXSITES) load_site_n[k]++;
        }
    }
}

static void h_fopen(void)
{
    const char *mode = gstr(ARG(1));
    const int text = !strchr(mode, 'b');
    const int reading = !strchr(mode, 'w') && !strchr(mode, 'a');
    char *backing = NULL;
    FILE *fh = (text && reading) ? lf2_open_translated(host_path_of(ARG(0)), &backing) : fopen(host_path_of(ARG(0)), mode);
    if (!fh) {
        ret_cdecl(0);
        return;
    }
    const uint32_t tok = file_token(fh);
    if (!tok) {
        fclose(fh);
        free(backing);
        ret_cdecl(0);
        return;
    }
    text_buf[tok - 0xFE000000u] = backing;
    note_data_open(host_path_of(ARG(0)));
    if (lf2_environment_get(LF2_ENV_STR_DEBUG)) lf2_log_writef(LF2_LOG_INFO, "imports", "fopen[%08x] %s (%s)\n", tok, host_path_of(ARG(0)), mode);
    ret_cdecl(tok);
}
static void h_fclose(void)
{
    FILE *fh = file_of(ARG(0));
    if (fh) {
        const uint32_t i = ARG(0) - 0xFE000000u;
        fclose(fh);
        files[i] = NULL;
        free(text_buf[i]);
        text_buf[i] = NULL;
    }
    ret_cdecl(0);
}

static void h_fgets(void)
{
    FILE *fh = file_of(ARG(2));
    char *r = fh ? fgets((char *)(g_mem + ARG(0)), (int)ARG(1), fh) : NULL;
    ret_cdecl(r ? ARG(0) : 0);
}

static void h_feof(void)
{
    FILE *fh = file_of(ARG(0));
    ret_cdecl(fh ? (uint32_t)feof(fh) : 1);
}

/* ---- printf family ----
 * Formats against guest varargs (already on the guest stack) into guest memory.
 * Only the conversions this binary actually uses are handled; anything else aborts
 * rather than silently emitting the wrong text. */
static int gformat(char *out, size_t cap, const char *fmt, uint32_t argp)
{
    size_t o = 0;
    for (const char *f = fmt; *f && o + 1 < cap; f++) {
        if (*f != '%') {
            out[o++] = *f;
            continue;
        }
        char spec[32];
        int n = 0;
        spec[n++] = *f++;
        while (*f && !strchr("diouxXcsfgeEp%", *f) && n < 30) spec[n++] = *f++;
        if (!*f) break;
        spec[n++] = *f;
        spec[n] = 0;

        char tmp[512];
        switch (*f) {
        case '%':
            tmp[0] = '%';
            tmp[1] = 0;
            break;
        case 'd':
        case 'i':
        case 'u':
        case 'o':
        case 'x':
        case 'X':
        case 'c':
            snprintf(tmp, sizeof tmp, spec, (int)LD32(argp));
            argp += 4;
            break;
        case 's':
            snprintf(tmp, sizeof tmp, spec, gstr(LD32(argp)));
            argp += 4;
            break;
        case 'f':
        case 'g':
        case 'e':
        case 'E': {
            double d;
            __builtin_memcpy(&d, g_mem + argp, 8);
            snprintf(tmp, sizeof tmp, spec, d);
            argp += 8;
            break;
        }
        case 'p':
            snprintf(tmp, sizeof tmp, "%08x", LD32(argp));
            argp += 4;
            break;
        default: lf2_log_writef(LF2_LOG_INFO, "imports", "unsupported printf conversion '%s'\n", spec); abort();
        }
        for (const char *t = tmp; *t && o + 1 < cap; t++) out[o++] = *t;
    }
    out[o] = 0;
    return (int)o;
}

static void h_sprintf(void)
{
    char buf[4096];
    int n = gformat(buf, sizeof buf, gstr(ARG(1)), R(ESP) + 4 + 8);
    if (lf2_environment_get(LF2_ENV_STR_DEBUG)) lf2_log_writef(LF2_LOG_INFO, "imports", "sprintf -> %08x (%d bytes) fmt=\"%s\" out=\"%.60s\"\n", ARG(0), n, gstr(ARG(1)), buf);
    memcpy(g_mem + ARG(0), buf, (size_t)n + 1);
    ret_cdecl((uint32_t)n);
}

static void h_fprintf(void)
{
    char buf[4096];
    int n = gformat(buf, sizeof buf, gstr(ARG(1)), R(ESP) + 4 + 8);
    FILE *fh = file_of(ARG(0));
    if (fh) fwrite(buf, 1, (size_t)n, fh);
    ret_cdecl((uint32_t)n);
}

/* ---- scanf family ----
 * Splitting the format into directives and calling the host once per directive does NOT
 * have the same semantics as one atomic scanf: matching failures, pushback and the
 * return value all behave differently at directive boundaries. So the whole format is
 * handed to the host in a single call with host-side storage, and the results are copied
 * into guest memory afterwards. The host then defines the semantics exactly.
 */
enum { SCAN_MAX = 12, SCAN_SLOT = 512 };

typedef struct {
    char conv;
    int suppressed;
    int is_long;  /* %lf stores a DOUBLE -- 8 bytes, not 4 */
    uint32_t out; /* guest destination */
} ScanArg;

/* Walk the format, recording each conversion. Returns the count, or -1 if unsupported. */
static int scan_parse(const char *fmt, uint32_t argp, ScanArg *args)
{
    int n = 0;
    for (const char *f = fmt; *f; f++) {
        if (*f != '%') continue;
        f++;
        if (*f == '%') continue;
        int suppressed = 0, is_long = 0;
        if (*f == '*') {
            suppressed = 1;
            f++;
        }
        while (*f && !strchr("diouxXcsfgeEnp[", *f)) {
            if (*f == 'l') is_long = 1;
            f++;
        }
        if (!*f) break;
        if (*f == '[' || *f == 'n' || *f == 'p') return -1; /* not used by this game */
        if (n >= SCAN_MAX) return -1;
        args[n].conv = *f;
        args[n].suppressed = suppressed;
        args[n].is_long = is_long;
        args[n].out = suppressed ? 0 : LD32(argp);
        if (!suppressed) argp += 4;
        n++;
    }
    return n;
}

/* Copy one converted value from host storage into guest memory. */
static void scan_store(const ScanArg *a, const void *slot)
{
    switch (a->conv) {
    case 'd':
    case 'i':
    case 'u':
    case 'o':
    case 'x':
    case 'X': ST32(a->out, (uint32_t)*(const int *)slot); break;
    case 'f':
    case 'g':
    case 'e':
    case 'E': {
        /* %lf is a DOUBLE: the host wrote 8 bytes, and MSVC's scanf stores 8 into the
         * caller's variable. Storing only the low half left the value's entire magnitude
         * in whatever guest memory previously held -- every %lf in the game is a
         * character-header physics field (walking_speed, jump_height, ...), so this was
         * a physics bug wearing a portability costume: right on a heap whose stale
         * contents happened to hold the old default, zero or garbage elsewhere. */
        uint64_t bits = 0;
        __builtin_memcpy(&bits, slot, a->is_long ? 8 : 4);
        ST32(a->out, (uint32_t)bits);
        if (a->is_long) ST32(a->out + 4, (uint32_t)(bits >> 32));
        break;
    }
    case 'c': ST8(a->out, *(const uint8_t *)slot); break;
    case 's': {
        const char *str = slot;
        const size_t len = strlen(str);
        memcpy(g_mem + a->out, str, len + 1);
        break;
    }
    default: break;
    }
}

/* LF2_SCAN_PROF=1 measures the scanf path, which dominates the data load: the game
 * decrypts each .dat into a temp file and parses it back token by token, so this is
 * where a multi-second load is won or lost. It reports unconditionally when enabled --
 * a profiler that prints nothing when the count is zero cannot be told apart from one
 * that never ran, and this path is easy to route around by accident. The two
 * clock_gettime calls cost tens of ns each, which is itself a few percent at this call
 * count, so read the per-call figure as an upper bound. */
static long scan_calls;
static double scan_ns;
static struct timespec scan_first, scan_last; /* the load SPAN, not just time in gscan */
static double sleep_ns_at_first, sleep_ns_at_last;
static long sleep_calls_at_first, sleep_calls_at_last;

void scan_prof_report(void)
{
    if (!lf2_environment_get(LF2_ENV_SCAN_PROF)) return;
    lf2_log_writef(LF2_LOG_INFO, "imports", "gscan: %ld calls, %.3f s inside gscan, %.0f ns/call (timer overhead included)\n", scan_calls, scan_ns / 1e9, scan_calls ? scan_ns / (double)scan_calls : 0.0);
    if (!scan_calls) {
        lf2_log_writef(LF2_LOG_INFO, "imports", "gscan: no parse span -- the data load never ran in this route\n");
        return;
    }
    const double span = (double)(scan_last.tv_sec - scan_first.tv_sec) + (double)(scan_last.tv_nsec - scan_first.tv_nsec) / 1e9;
    const double slept = (sleep_ns_at_last - sleep_ns_at_first) / 1e9;
    lf2_log_writef(LF2_LOG_INFO, "imports", "gscan: parse span %.3f s (first to last call), %.1f%% of it inside gscan\n", span, span > 0 ? 100.0 * (scan_ns / 1e9) / span : 0.0);
    lf2_log_writef(LF2_LOG_INFO, "imports",
                   "load:  %.3f s span = %.3f s slept (%ld Sleep calls) + %.3f s not sleeping"
                   " -- %.1f%% of the load is Sleep\n",
                   span, slept, sleep_calls_at_last - sleep_calls_at_first, span - slept, span > 0 ? 100.0 * slept / span : 0.0);
    lf2_log_writef(LF2_LOG_INFO, "imports", "sleep call sites (guest return address), during the load:\n");
    for (int k = 0; k < sleep_nsites; k++) {
        const long during = sleep_site_at_last[k] - sleep_site_at_first[k];
        lf2_log_writef(LF2_LOG_INFO, "imports", "  ra=%08x  %8ld during load  %8ld total\n", sleep_site[k], during, sleep_site_n[k]);
    }
    if (!sleep_nsites) lf2_log_writef(LF2_LOG_INFO, "imports", "  (none -- Sleep was never called)\n");
}

static int gscan_inner(FILE *fh, const char *input, const char *fmt, uint32_t argp)
{
    ScanArg args[SCAN_MAX];
    const int nargs = scan_parse(fmt, argp, args);
    if (nargs < 0) {
        lf2_log_writef(LF2_LOG_INFO, "imports", "unsupported scanf format \"%s\"\n", fmt);
        abort();
    }

    /* One slot per conversion, each big enough for any of them. Extra pointers beyond
     * what the format consumes are simply ignored by the host. */
    static char slots[SCAN_MAX][SCAN_SLOT];
    memset(slots, 0, sizeof slots);
    void *p[SCAN_MAX];
    for (int i = 0; i < SCAN_MAX; i++) p[i] = slots[i];

    const int got = fh ? fscanf(fh, fmt, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11]) : sscanf(input, fmt, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11]);

    if (got <= 0) return got;

    /* The host filled the first `got` NON-suppressed conversions, in order. */
    int filled = 0;
    for (int i = 0; i < nargs && filled < got; i++) {
        if (args[i].suppressed) continue;
        scan_store(&args[i], slots[i]);
        filled++;
    }
    return got;
}

/* Wrapper rather than a timer at each return, so a later return cannot escape it. */
static int gscan(FILE *fh, const char *input, const char *fmt, uint32_t argp)
{
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    const int got = gscan_inner(fh, input, fmt, argp);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    if (!scan_calls) {
        scan_first = t0;
        sleep_ns_at_first = sleep_ns_total;
        sleep_calls_at_first = sleep_calls_total;
        memcpy(sleep_site_at_first, sleep_site_n, sizeof sleep_site_n);
    }
    scan_last = t1;
    sleep_ns_at_last = sleep_ns_total;
    sleep_calls_at_last = sleep_calls_total;
    /* Snapshot every call, so the final one lands exactly on the end of the load. */
    memcpy(sleep_site_at_last, sleep_site_n, sizeof sleep_site_n);
    scan_calls++;
    scan_ns += (double)(t1.tv_sec - t0.tv_sec) * 1e9 + (double)(t1.tv_nsec - t0.tv_nsec);
    return got;
}

static void h_fscanf(void)
{
    FILE *fh = file_of(ARG(0));
    const int n = fh ? gscan(fh, NULL, gstr(ARG(1)), R(ESP) + 4 + 8) : -1;
    static int dbg = -1;
    if (env_flag(LF2_ENV_STR_DEBUG, &dbg)) {
        char escaped[256];
        size_t out = 0;
        for (const char *c = gstr(ARG(1)); *c && out + 2 < sizeof escaped; ++c) {
            if (*c == '\n' || *c == '\r') escaped[out++] = '\\';
            escaped[out++] = *c == '\n' ? 'n' : *c == '\r' ? 'r' : *c;
        }
        escaped[out] = '\0';
        lf2_log_writef(LF2_LOG_INFO, "imports", "fscanf[%08x] -> %d fmt=\"%s\"", ARG(0), n, escaped);
    }
    ret_cdecl((uint32_t)n);
}

static void h_sscanf(void)
{
    const int n = gscan(NULL, gstr(ARG(0)), gstr(ARG(1)), R(ESP) + 4 + 8);
    ret_cdecl((uint32_t)n);
}

static void h_rand(void)
{
    ret_cdecl((uint32_t)(rand() & 0x7fff));
}
static void h_srand(void)
{
    srand(ARG(0));
    ret_cdecl(0);
}
static void h_time64(void)
{
    int64_t t = (int64_t)time(NULL);
    if (ARG(0)) {
        ST32(ARG(0), (uint32_t)t);
        ST32(ARG(0) + 4, (uint32_t)(t >> 32));
    }
    R(EAX) = (uint32_t)t;
    R(EDX) = (uint32_t)(t >> 32);
    R(ESP) += 4;
}
static void h_exit(void)
{
    exit((int)ARG(0));
}

/* MultiByteToWideChar(CodePage, dwFlags, src, cbSrc, dst, cchDst).
 * Six parameters -- popping the wrong number leaks guest stack on every call, which
 * shows up much later as a POP taking a return address. */
static void h_MultiByteToWideChar(void)
{
    const uint32_t src = ARG(2), dst = ARG(4);
    const int32_t cb = (int32_t)ARG(3);
    const uint32_t cch = ARG(5);

    uint32_t n = 0;
    if (cb < 0) {
        while (LD8(src + n)) n++;
        n++;
    } /* -1: NUL-terminated, NUL included */
    else
        n = (uint32_t)cb;

    if (cch == 0) {
        ret_stdcall(6, n);
        return;
    } /* size query */

    uint32_t written = 0;
    for (; written < n && written < cch; written++) ST16(dst + written * 2, LD8(src + written)); /* the game's text is 8-bit */
    ret_stdcall(6, written);
}

static void h_localtime64(void)
{
    /* MSVC struct tm: nine ints. Returned in a static guest buffer, as the CRT does. */
    static uint32_t buf;
    if (!buf) buf = guest_alloc(36);
    int64_t t = (int64_t)LD32(ARG(0)) | ((int64_t)LD32(ARG(0) + 4) << 32);
    time_t tt = (time_t)t;
    struct tm *g = localtime(&tt);
    const int v[9] = {g->tm_sec, g->tm_min, g->tm_hour, g->tm_mday, g->tm_mon, g->tm_year, g->tm_wday, g->tm_yday, g->tm_isdst};
    for (int i = 0; i < 9; i++) ST32(buf + (uint32_t)i * 4, (uint32_t)v[i]);
    ret_cdecl(buf);
}
static void h_getcwd(void)
{
    if (ARG(0) && getcwd((char *)(g_mem + ARG(0)), ARG(1))) ret_cdecl(ARG(0));
    else ret_cdecl(0);
}
static void h_chdir(void)
{
    ret_cdecl((uint32_t)chdir(gstr(ARG(0))));
}

/* ---- MMIO ----
 * The game reads its WAVs through the RIFF chunk API rather than plain fread.
 * MMCKINFO is { ckid, cksize, fccType, dwDataOffset, dwFlags }. */
enum { MMIO_FINDCHUNK = 0x0010, MMIO_FINDRIFF = 0x0020, MMIO_FINDLIST = 0x0040 };
enum { MMSYSERR_NOERROR = 0, MMIOERR_CHUNKNOTFOUND = 261 };

static void h_mmioOpenA(void)
{
    FILE *fh = fopen(host_path_of(ARG(0)), "rb");
    ret_stdcall(3, fh ? file_token(fh) : 0);
}

static void h_mmioClose(void)
{
    FILE *fh = file_of(ARG(0));
    if (fh) {
        fclose(fh);
        files[ARG(0) - 0xFE000000u] = NULL;
    }
    ret_stdcall(2, 0);
}

static void h_mmioRead(void)
{
    FILE *fh = file_of(ARG(0));
    long n = fh ? (long)fread(g_mem + ARG(1), 1, ARG(2), fh) : -1;
    ret_stdcall(3, (uint32_t)n);
}

static void h_mmioDescend(void)
{
    FILE *fh = file_of(ARG(0));
    const uint32_t ck = ARG(1), flags = ARG(3);
    if (!fh) {
        ret_stdcall(4, MMIOERR_CHUNKNOTFOUND);
        return;
    }

    const uint32_t want = (flags & (MMIO_FINDRIFF | MMIO_FINDLIST | MMIO_FINDCHUNK)) ? LD32(ck + ((flags & MMIO_FINDCHUNK) ? 0 : 8)) : 0;

    for (;;) {
        uint8_t hdr[8];
        if (fread(hdr, 1, 8, fh) != 8) {
            ret_stdcall(4, MMIOERR_CHUNKNOTFOUND);
            return;
        }
        const uint32_t id = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) | ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
        const uint32_t size = (uint32_t)hdr[4] | ((uint32_t)hdr[5] << 8) | ((uint32_t)hdr[6] << 16) | ((uint32_t)hdr[7] << 24);

        uint32_t type = 0;
        const int is_container = (flags & (MMIO_FINDRIFF | MMIO_FINDLIST)) != 0;
        if (is_container) {
            uint8_t t[4];
            if (fread(t, 1, 4, fh) != 4) {
                ret_stdcall(4, MMIOERR_CHUNKNOTFOUND);
                return;
            }
            type = (uint32_t)t[0] | ((uint32_t)t[1] << 8) | ((uint32_t)t[2] << 16) | ((uint32_t)t[3] << 24);
        }

        const uint32_t data_off = (uint32_t)ftell(fh);
        const uint32_t match = is_container ? type : id;
        if (!want || match == want) {
            ST32(ck, id);
            ST32(ck + 4, size);
            ST32(ck + 8, type);
            ST32(ck + 12, data_off);
            ST32(ck + 16, 0);
            ret_stdcall(4, MMSYSERR_NOERROR);
            return;
        }
        /* Not the chunk asked for: skip its body (chunks are word-aligned) and retry. */
        const long skip = (long)size - (is_container ? 4 : 0);
        if (fseek(fh, skip + (skip & 1), SEEK_CUR) != 0) {
            ret_stdcall(4, MMIOERR_CHUNKNOTFOUND);
            return;
        }
    }
}

static void h_mmioAscend(void)
{
    FILE *fh = file_of(ARG(0));
    const uint32_t ck = ARG(1);
    if (fh) {
        const uint32_t end = LD32(ck + 12) + LD32(ck + 4);
        fseek(fh, (long)(end + (end & 1)), SEEK_SET);
    }
    ret_stdcall(3, MMSYSERR_NOERROR);
}

/* ---- Win32 file API ----
 * The import table has CreateFileA/WriteFile/CloseHandle but no ReadFile, so this path
 * is write-only: settings and recorded matches. */
static void h_CreateFileA(void)
{
    const uint32_t access = ARG(1), disp = ARG(4);
    const char *mode = (access & 0x40000000u) ? ((disp == 2 /*CREATE_ALWAYS*/) ? "wb" : "r+b") : "rb";
    FILE *fh = fopen(host_path_of(ARG(0)), mode);
    if (!fh && (access & 0x40000000u)) fh = fopen(host_path_of(ARG(0)), "wb");
    ret_stdcall(7, fh ? file_token(fh) : 0xFFFFFFFFu); /* INVALID_HANDLE_VALUE */
}

static void h_WriteFile(void)
{
    FILE *fh = file_of(ARG(0));
    size_t n = fh ? fwrite(g_mem + ARG(1), 1, ARG(2), fh) : 0;
    if (ARG(3)) ST32(ARG(3), (uint32_t)n);
    ret_stdcall(5, fh ? 1 : 0);
}

static void h_CloseHandle(void)
{
    FILE *fh = file_of(ARG(0));
    if (fh) {
        fclose(fh);
        files[ARG(0) - 0xFE000000u] = NULL;
    }
    ret_stdcall(1, 1);
}

/* Must actually terminate. Returning from this lets a /GS stack-cookie failure fall
 * through __report_gsfailure and keep running on a wrecked stack, which then surfaces
 * far away as a wild pointer. */
static void h_TerminateProcess(void)
{
    lf2_log_writef(LF2_LOG_INFO, "imports", "TerminateProcess called -- the guest is aborting deliberately\n");
    abort();
}

static void h_GetLocalTime(void)
{
    time_t t = time(NULL);
    struct tm *g = localtime(&t);
    const uint32_t p = ARG(0);
    const uint16_t v[8] = {(uint16_t)(g->tm_year + 1900), (uint16_t)(g->tm_mon + 1), (uint16_t)g->tm_wday, (uint16_t)g->tm_mday, (uint16_t)g->tm_hour, (uint16_t)g->tm_min, (uint16_t)g->tm_sec, 0};
    for (int i = 0; i < 8; i++) ST16(p + (uint32_t)i * 2, v[i]);
    ret_stdcall(1, 0);
}

/* Netplay is out of scope, and the thread this creates is the network thread. It is not
 * started: running it inline would block the caller, and the game must not observe a
 * silently-succeeding thread that never runs, so this is logged. */
static void h_CreateThread(void)
{
    static int warned;
    if (!warned) {
        lf2_log_writef(LF2_LOG_INFO, "imports", "note: CreateThread ignored (netplay is not ported)\n");
        warned = 1;
    }
    if (ARG(5)) ST32(ARG(5), 0);
    ret_stdcall(6, 0xFD000001u);
}

static void h_timeGetTime(void)
{
    clock_read_note("timeGetTime");
    ret_stdcall(0, (uint32_t)(guest_ns() / 1000000ull));
}

/* ---- table ---- */

static const struct {
    const char *dll, *name;
    Handler fn;
} TABLE[] = {
    {"KERNEL32.dll", "GetSystemTimeAsFileTime", h_GetSystemTimeAsFileTime},
    {"KERNEL32.dll", "GetTickCount", h_GetTickCount},
    {"KERNEL32.dll", "QueryPerformanceCounter", h_QueryPerformanceCounter},
    {"KERNEL32.dll", "GetVersionExA", h_GetVersionExA},
    {"KERNEL32.dll", "GetCurrentProcess", h_GetCurrentProcess},
    {"KERNEL32.dll", "GetCurrentProcessId", h_GetCurrentProcessId},
    {"KERNEL32.dll", "GetCurrentThreadId", h_GetCurrentThreadId},
    {"KERNEL32.dll", "GetModuleHandleA", h_GetModuleHandleA},
    {"KERNEL32.dll", "GetStartupInfoA", h_GetStartupInfoA},
    {"KERNEL32.dll", "InitializeCriticalSection", h_ret0_1},
    {"KERNEL32.dll", "EnterCriticalSection", h_ret0_1},
    {"KERNEL32.dll", "LeaveCriticalSection", h_ret0_1},
    {"KERNEL32.dll", "SetUnhandledExceptionFilter", h_ret0_1},
    {"KERNEL32.dll", "UnhandledExceptionFilter", h_ret0_1},
    {"KERNEL32.dll", "IsDebuggerPresent", h_ret0_0},
    {"KERNEL32.dll", "GetLastError", h_ret0_0},
    {"KERNEL32.dll", "GetACP", h_ret1_0},
    {"KERNEL32.dll", "GetThreadLocale", h_ret1_0},
    {"KERNEL32.dll", "GetLocaleInfoA", h_ret1_4},
    {"KERNEL32.dll", "InterlockedExchange", h_InterlockedExchange},
    {"KERNEL32.dll", "InterlockedCompareExchange", h_InterlockedCompareExchange},
    {"KERNEL32.dll", "lstrlenA", h_lstrlenA},
    {"KERNEL32.dll", "OutputDebugStringA", h_OutputDebugStringA},
    {"KERNEL32.dll", "Sleep", h_Sleep},
    {"KERNEL32.dll", "MultiByteToWideChar", h_MultiByteToWideChar},
    {"KERNEL32.dll", "CreateFileA", h_CreateFileA},
    {"KERNEL32.dll", "WriteFile", h_WriteFile},
    {"KERNEL32.dll", "CloseHandle", h_CloseHandle},
    {"KERNEL32.dll", "GetLocalTime", h_GetLocalTime},
    {"KERNEL32.dll", "CreateThread", h_CreateThread},
    {"KERNEL32.dll", "TerminateProcess", h_TerminateProcess},

    {"MSVCR80.dll", "__getmainargs", h_getmainargs},
    {"MSVCR80.dll", "_initterm", h_initterm},
    {"MSVCR80.dll", "_initterm_e", h_initterm_e},
    {"MSVCR80.dll", "__set_app_type", h_cdecl0},
    {"MSVCR80.dll", "__p__commode", h_p_commode},
    {"MSVCR80.dll", "__p__fmode", h_p_fmode},
    {"MSVCR80.dll", "_controlfp_s", h_controlfp_s},
    {"MSVCR80.dll", "_configthreadlocale", h_cdecl0},
    {"MSVCR80.dll", "_encode_pointer", h_identity},
    {"MSVCR80.dll", "_decode_pointer", h_identity},
    {"MSVCR80.dll", "_lock", h_cdecl0},
    {"MSVCR80.dll", "_unlock", h_cdecl0},
    {"MSVCR80.dll", "__dllonexit", h_identity},
    {"MSVCR80.dll", "_onexit", h_identity},
    {"MSVCR80.dll", "_crt_debugger_hook", h_cdecl0},
    {"MSVCR80.dll", "__setusermatherr", h_cdecl0},
    {"MSVCR80.dll", "_setusermatherr", h_cdecl0},
    {"MSVCR80.dll", "_adjust_fdiv", h_cdecl0},
    {"MSVCR80.dll", "malloc", h_malloc},
    {"MSVCR80.dll", "calloc", h_calloc},
    {"MSVCR80.dll", "free", h_free},
    {"MSVCR80.dll", "memcpy", h_memcpy},
    {"MSVCR80.dll", "memset", h_memset},
    {"MSVCR80.dll", "??2@YAPAXI@Z", h_malloc},  /* operator new */
    {"MSVCR80.dll", "??_U@YAPAXI@Z", h_malloc}, /* operator new[] */
    {"MSVCR80.dll", "??3@YAXPAX@Z", h_free},    /* operator delete */
    {"MSVCR80.dll", "_ismbblead", h_cdecl0},
    {"MSVCR80.dll", "_XcptFilter", h_cdecl0},
    {"MSVCR80.dll", "__CxxFrameHandler3", h_cdecl0},
    {"MSVCR80.dll", "_except_handler4_common", h_cdecl0},
    {"MSVCR80.dll", "_invoke_watson", h_cdecl0},
    {"MSVCR80.dll", "?terminate@@YAXXZ", h_cdecl0},
    {"MSVCR80.dll", "_amsg_exit", h_exit},
    {"MSVCR80.dll", "exit", h_exit},
    {"MSVCR80.dll", "_exit", h_exit},
    {"MSVCR80.dll", "_cexit", h_cdecl0},
    {"MSVCR80.dll", "fopen", h_fopen},
    {"MSVCR80.dll", "fclose", h_fclose},
    {"MSVCR80.dll", "fgets", h_fgets},
    {"MSVCR80.dll", "feof", h_feof},
    {"MSVCR80.dll", "fprintf", h_fprintf},
    {"MSVCR80.dll", "sprintf", h_sprintf},
    {"MSVCR80.dll", "fscanf", h_fscanf},
    {"MSVCR80.dll", "sscanf", h_sscanf},
    {"MSVCR80.dll", "rand", h_rand},
    {"MSVCR80.dll", "srand", h_srand},
    {"MSVCR80.dll", "_time64", h_time64},
    {"MSVCR80.dll", "_localtime64", h_localtime64},
    {"MSVCR80.dll", "_getcwd", h_getcwd},
    {"MSVCR80.dll", "_chdir", h_chdir},

    {"WINMM.dll", "timeGetTime", h_timeGetTime},
    {"WINMM.dll", "mmioOpenA", h_mmioOpenA},
    {"WINMM.dll", "mmioClose", h_mmioClose},
    {"WINMM.dll", "mmioRead", h_mmioRead},
    {"WINMM.dll", "mmioDescend", h_mmioDescend},
    {"WINMM.dll", "mmioAscend", h_mmioAscend},
};

Handler host_lookup(const char *dll, const char *name)
{
    for (size_t i = 0; i < sizeof TABLE / sizeof TABLE[0]; i++) {
        if (strcmp(TABLE[i].dll, dll) == 0 && strcmp(TABLE[i].name, name) == 0) return TABLE[i].fn;
    }
    return NULL;
}

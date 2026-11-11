#include "guest_clock.h"

#include "environment.h"
#include "hostwin.h"
#include "lf2_log.h"

#include <stdlib.h>
#include <time.h>

/* Owed for waits the port credited instead of sleeping (see h_Sleep). */
static uint64_t offset_ns;

uint64_t guest_clock_ns(void)
{
    return (uint64_t)hostwin_frames() * (uint64_t)GUEST_FRAME_NS + offset_ns;
}

static int64_t wall_epoch_ns;
static int wall_epoch_set;

static void fix_wall_epoch(void)
{
    const char *fixed = lf2_environment_get(LF2_ENV_WALL_EPOCH);
    if (fixed) {
        wall_epoch_ns = (int64_t)strtoll(fixed, NULL, 10) * 1000000000ll;
    } else {
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        wall_epoch_ns = (int64_t)now.tv_sec * 1000000000ll + now.tv_nsec - (int64_t)guest_clock_ns();
    }
    wall_epoch_set = 1;
}

int64_t guest_clock_wall_ns(void)
{
    if (!wall_epoch_set) fix_wall_epoch();
    return wall_epoch_ns + (int64_t)guest_clock_ns();
}

void guest_clock_credit_ms(uint32_t ms)
{
    offset_ns += (uint64_t)ms * 1000000ull;
}

/* A spin is a long run of reads from one site with no Sleep between them; a deadline loop's run
 * is a handful. Both totals and runs are reported, and a run that saw no site says so. */
enum { CLOCK_SITES = 24 };
static uint32_t clk_site[CLOCK_SITES];
static long clk_site_n[CLOCK_SITES];
static long clk_site_max_run[CLOCK_SITES];
static long clk_site_max_at[CLOCK_SITES]; /* presented frame: load and play are different diagnoses */
static const char *clk_site_api[CLOCK_SITES];
static int clk_nsites;
static long clk_reads_total, clk_run, clk_dropped;
static int clk_on = -1;

void guest_clock_note_read(const char *api, uint32_t return_address)
{
    if (clk_on < 0) clk_on = lf2_environment_get(LF2_ENV_CLOCK_SITES) != NULL;
    if (!clk_on) return;

    clk_reads_total++;
    clk_run++;
    int k = 0;
    for (; k < clk_nsites; k++)
        if (clk_site[k] == return_address) break;
    if (k == clk_nsites) {
        if (clk_nsites >= CLOCK_SITES) {
            clk_dropped++;
            return;
        }
        clk_site[clk_nsites] = return_address;
        clk_site_api[clk_nsites] = api;
        clk_nsites++;
    }
    clk_site_n[k]++;
    if (clk_run > clk_site_max_run[k]) {
        clk_site_max_run[k] = clk_run;
        clk_site_max_at[k] = hostwin_frames();
    }
}

void guest_clock_note_sleep(void)
{
    clk_run = 0;
}

void clock_sites_report(void)
{
    if (!lf2_environment_get(LF2_ENV_CLOCK_SITES)) return;
    if (clk_reads_total == 0) {
        lf2_log_writef(LF2_LOG_INFO, "guest-clock",
                       "clock sites: the guest NEVER read its clock -- no site to name, and "
                       "nothing here is evidence about spinning\n");
        return;
    }
    lf2_log_writef(LF2_LOG_INFO, "guest-clock",
                   "clock sites: %ld reads from %d call site(s)%s. `run` is reads since the "
                   "last Sleep -- a large one is a loop watching the clock without "
                   "sleeping\n",
                   clk_reads_total, clk_nsites,
                   clk_dropped ? " (and more sites than this build can hold; some were DROPPED)" : "");
    for (int k = 0; k < clk_nsites; k++)
        lf2_log_writef(LF2_LOG_INFO, "guest-clock", "  from=%08x  %-24s reads=%-9ld longest run=%-7ld at frame %ld\n",
                       clk_site[k], clk_site_api[k], clk_site_n[k], clk_site_max_run[k], clk_site_max_at[k]);
    if (clk_dropped)
        lf2_log_writef(LF2_LOG_INFO, "guest-clock",
                       "  ... and %ld reads from call sites past the %d this build records, "
                       "which are NOT in the list above\n",
                       clk_dropped, CLOCK_SITES);
}

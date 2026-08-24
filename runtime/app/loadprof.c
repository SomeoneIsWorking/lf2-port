#include "loadprof.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int lf2_loading_now(void);                /* imports.c */
long lf2_load_active_ms(void);            /* imports.c */
extern long decrypt_files, decrypt_bytes; /* assets.c: the ported decryptors */

static const char *const NAMES[LP_N] = {
    "surf_Blt (sprite/layer composition)",
    "StretchBlt (GDI scaling path)",
    "present (backbuffer -> window)",
    "colour fill (DDBLT_COLORFILL)",
};

static unsigned long long total_ns[LP_N];
static long calls[LP_N];

unsigned long long loadprof_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000000000ull + (unsigned long long)ts.tv_nsec;
}

int loadprof_on(void)
{
    static int on = -1;
    if (on < 0) on = getenv("LF2_LOAD_PROF") != NULL;
    /* Only while the game is actually loading its data. Timing these sections across a
     * whole run would mostly measure the menu and the match, which is not the question. */
    return on && lf2_loading_now();
}

void loadprof_add(int slot, unsigned long long ns)
{
    if (slot < 0 || slot >= LP_N) return;
    total_ns[slot] += ns;
    calls[slot]++;
}

void loadprof_report(void)
{
    if (!getenv("LF2_LOAD_PROF")) return;

    const long active = lf2_load_active_ms();
    if (!active) {
        fprintf(stderr, "load profile: the game never opened a data file in this run, so "
                        "NOTHING was profiled -- this is not a measurement of zero cost.\n");
        return;
    }

    unsigned long long sum = 0;
    for (int i = 0; i < LP_N; i++) sum += total_ns[i];

    fprintf(stderr,
            "load profile: %.3f s actively loading; the sections below account "
            "for %.3f s of it (%.0f%%)\n",
            (double)active / 1000.0, (double)sum / 1e9, active ? (double)sum / 1e6 / (double)active * 100.0 : 0.0);
    for (int i = 0; i < LP_N; i++) {
        if (!calls[i]) {
            fprintf(stderr, "  %-38s NEVER ENTERED while loading\n", NAMES[i]);
            continue;
        }
        fprintf(stderr, "  %-38s %8.3f s over %8ld calls (%.1f us each)\n", NAMES[i], (double)total_ns[i] / 1e9,
                calls[i], (double)total_ns[i] / 1000.0 / (double)calls[i]);
    }
    if (decrypt_files)
        fprintf(stderr,
                "  decrypt: %ld files, %.2f MB, done natively (LF2_SLOW_DECRYPT=1 "
                "restores the game's own byte-at-a-time loop)\n",
                decrypt_files, (double)decrypt_bytes / 1048576.0);
    else fprintf(stderr, "  decrypt: the ported decryptor NEVER RAN in this run\n");
    fprintf(stderr, "  the remainder is guest resource construction and recompiled control flow.\n");
}

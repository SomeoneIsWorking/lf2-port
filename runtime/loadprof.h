/* Where the data load's wall clock actually goes.
 *
 * The load costs 3.4 s and 315 data files, and every earlier attempt to cut it aimed at
 * something that turned out not to be the cost: the frame-pacing Sleep (fixed, and worth
 * 5-7 s), driving the loader in a loop (it loads zero files that way), and accelerating the
 * guest clock (measured: 3.6 s at 1x, 3.5 s at 4x, 4.7 s at 16x, 7 s at 32x -- it makes the
 * load SLOWER, because the game answers a jumping clock with more catch-up ticks).
 *
 * Each of those was a guess about which part of a loader step is expensive. This measures it
 * instead, with a denominator: the sections below are timed only while the game is opening
 * its own data files, and the report prints their total against the active loading time, so
 * "the profiled sections account for 0.2 of 3.4 s" is a visible answer rather than a silent
 * one. A section that is never entered prints as such rather than being omitted.
 */
#ifndef LF2_LOADPROF_H
#define LF2_LOADPROF_H

enum { LP_BLT, LP_STRETCH, LP_PRESENT, LP_FILL, LP_N };

/* Enabled by LF2_LOAD_PROF=1. Zero cost otherwise: the flag is read once. */
int  loadprof_on(void);
void loadprof_add(int slot, unsigned long long ns);
unsigned long long loadprof_now_ns(void);
void loadprof_report(void);

/* The slot is mutable: a function can discover partway through which section it really is
 * (surf_Blt turns out to be a colour fill), and charging that time to the wrong bucket is
 * how a profile lies. */
#define LOADPROF_SCOPE(slot) \
    const int _lp_on = loadprof_on(); \
    const unsigned long long _lp_t0 = _lp_on ? loadprof_now_ns() : 0ull; \
    int _lp_slot = (slot)
#define LOADPROF_END() \
    do { if (_lp_on) loadprof_add(_lp_slot, loadprof_now_ns() - _lp_t0); } while (0)

#endif

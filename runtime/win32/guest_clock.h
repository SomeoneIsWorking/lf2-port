#ifndef LF2_WIN32_GUEST_CLOCK_H
#define LF2_WIN32_GUEST_CLOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Guest time: presented frames at GUEST_FRAME_NS each, plus the waits credited instead of slept.
 * It never reads the wall, so the game's timeline is a property of its frames, not the host. */
uint64_t guest_clock_ns(void);
/* The guest's calendar: an epoch fixed once, advanced by guest time. Unix epoch, nanoseconds.
 * The epoch is the host's time at first use, or LF2_WALL_EPOCH (seconds) for runs that must agree. */
int64_t guest_clock_wall_ns(void);
/* A wait the guest asked for advances its clock by at least that long. */
void guest_clock_credit_ms(uint32_t ms);

/* LF2_CLOCK_SITES: who reads the clock, and who reads it without sleeping (a spin). */
void guest_clock_note_read(const char *api, uint32_t return_address);
void guest_clock_note_sleep(void);
void clock_sites_report(void);

#ifdef __cplusplus
}
#endif

#endif

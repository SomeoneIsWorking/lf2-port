#ifndef LF2_WIN32_GUEST_CLOCK_H
#define LF2_WIN32_GUEST_CLOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { GUEST_FRAME_NS = 33333333 }; /* the guest clock's tick */

/* Guest time: the frames the guest presented at GUEST_FRAME_NS each, plus the waits credited
 * instead of slept. It never reads the wall, so the game's timeline is a property of its frames. */
uint64_t guest_clock_ns(void);
/* Frames the guest presented, whether or not the host showed them (a resimulated frame is not shown). */
uint64_t guest_clock_frames(void);
void guest_clock_frame_presented(void);

/* What a snapshot keeps of the clock. */
typedef struct {
    uint64_t frames;
    uint64_t offset_ns;
} GuestClockState;
GuestClockState guest_clock_state(void);
void guest_clock_restore(GuestClockState state);
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

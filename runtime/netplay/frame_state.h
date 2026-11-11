#ifndef LF2_NETPLAY_FRAME_STATE_H
#define LF2_NETPLAY_FRAME_STATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Digests of the session state at a frame boundary. `logic` covers every guest span the game's
 * logic reads plus the guest-visible host state; `media` covers surfaces and sound buffers. */
typedef struct {
    uint64_t logic;
    uint64_t media;
} Lf2FrameDigest;

Lf2FrameDigest lf2_frame_state_digest(void);
/* One log line per frame with both digests; the determinism check compares two runs' lines. */
void lf2_frame_state_log(uint32_t frame, long presented);

#ifdef __cplusplus
}
#endif

#endif

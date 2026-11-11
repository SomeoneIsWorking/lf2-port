#ifndef LF2_NETPLAY_ONLINE_SESSION_H
#define LF2_NETPLAY_ONLINE_SESSION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Two LF2s meeting by a pasted code over Lucent's peer-to-peer transport. Until the shared rollback
 * engine plays the frames, a session proves the meeting: the peers authenticate the path, refuse a
 * different lf2.exe, and trade one input frame. LF2_ONLINE=host shows an invite; LF2_ONLINE=<invite>
 * joins it. */
void lf2_online_start(void);
/* Advances the session at a frame boundary without waiting; `local_buttons` is this machine's
 * player's seven-bit mask for `frame`. */
void lf2_online_frame(uint32_t frame, uint8_t local_buttons);

#ifdef __cplusplus
}
#endif

#endif

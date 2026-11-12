#ifndef LF2_DSOUND_H
#define LF2_DSOUND_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Keep SDL's pull thread quiescent while the guest publishes its startup buffers. */
void audio_initialization_begin(void);
void audio_initialization_end(void);
void dsound_shutdown(void);
/* Off while a rollback reruns frames: the guest's Play, Stop, seek and volume calls are acknowledged
 * and not performed, since the frame's first run already performed them. */
void dsound_set_audible(int audible);
/* Where the next sound buffer's samples go, for a snapshot. */
uint32_t pcm_next(void);
void pcm_restore(uint32_t next);

#ifdef __cplusplus
}
#endif

#endif

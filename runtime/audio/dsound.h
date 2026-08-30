#ifndef LF2_DSOUND_H
#define LF2_DSOUND_H

/* Keep SDL's pull thread quiescent while the guest publishes its startup buffers. */
void audio_initialization_begin(void);
void audio_initialization_end(void);
void dsound_shutdown(void);

#endif

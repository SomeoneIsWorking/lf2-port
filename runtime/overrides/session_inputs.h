/* What a netplay session needs from the native overrides: the gather's device pool fed from the
 * session's frame inputs, the local player's buttons sampled outside the frame, and the overrides'
 * own state as snapshot sections. */
#ifndef LF2_OVERRIDES_SESSION_INPUTS_H
#define LF2_OVERRIDES_SESSION_INPUTS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Device k is session slot k, bit b of its byte button b; count 0 gives the host's devices back. */
void input_use_session(const uint8_t *buttons, int count);
/* Every host device's buttons merged, as one session slot's byte. */
uint8_t input_sample_local(void);

/* The device claims (input.c) and late-join selections (coop.c) as snapshot sections. */
size_t input_state_size(void);
void input_state_save(void *out);
void input_state_load(const void *in);
size_t coop_state_size(void);
void coop_state_save(void *out);
void coop_state_load(const void *in);

#ifdef __cplusplus
}
#endif

#endif

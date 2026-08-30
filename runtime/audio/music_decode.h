#ifndef LF2_MUSIC_DECODE_H
#define LF2_MUSIC_DECODE_H

#include <stdint.h>
#include <stddef.h>

/* LF2's shipped tracks are under four minutes. A ten-minute ceiling keeps a malformed
 * replacement from making the complete-track decoder consume the Android process heap. */
static inline size_t music_decode_frame_limit(int sample_rate)
{
    enum { MAX_SECONDS = 10 * 60 };
    if (sample_rate <= 0 || (size_t)sample_rate > SIZE_MAX / MAX_SECONDS) return 0;
    return (size_t)sample_rate * MAX_SECONDS;
}

/* Decode one complete music track to interleaved signed 16-bit PCM. The caller owns
 * samples on success. A failure leaves both outputs empty and explains the decoder boundary. */
int music_decode_file(const char *path, int sample_rate, int channels, int16_t **samples, size_t *frames, char *error,
                      size_t error_capacity);

#endif

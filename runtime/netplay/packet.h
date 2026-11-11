#ifndef LF2_NETPLAY_PACKET_H
#define LF2_NETPLAY_PACKET_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LF2_NETPLAY_HELLO_BYTES = 68,
};

/* The first message each peer sends: which session, which lf2.exe, and the host's calendar,
 * which the friend's guest adopts so both boot into the same state. */
typedef struct {
    uint8_t session_id[16];
    uint8_t executable_sha256[32];
    int64_t epoch_ns;
    int32_t utc_offset_s;
} Lf2NetplayHello;

/* Wire format is byte-exact and transport-independent. Encode returns the encoded size, or zero
 * for insufficient capacity. Decode refuses unknown versions, types, lengths and reserved bits,
 * leaving `out` untouched. */
size_t lf2_netplay_hello_encode(const Lf2NetplayHello *hello, uint8_t *out, size_t capacity);
int lf2_netplay_hello_decode(const uint8_t *bytes, size_t size, Lf2NetplayHello *out);

#ifdef __cplusplus
}
#endif

#endif

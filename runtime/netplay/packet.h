#ifndef LF2_NETPLAY_PACKET_H
#define LF2_NETPLAY_PACKET_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LF2_NETPLAY_HELLO_BYTES = 88,
};

/* The first message each peer sends: which session, which lf2.exe and which port build. The
 * friend's game then takes the host's state, which only an identical build can read. */
typedef struct {
    uint8_t session_id[16];
    uint8_t executable_sha256[32];
    uint8_t port_sha256[32];
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

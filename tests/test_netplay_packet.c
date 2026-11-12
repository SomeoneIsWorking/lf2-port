#include "packet.h"

#undef NDEBUG
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    uint8_t bytes[LF2_NETPLAY_HELLO_BYTES];
    Lf2NetplayHello hello = {0};
    for (unsigned i = 0; i < 16; ++i) hello.session_id[i] = (uint8_t)i;
    for (unsigned i = 0; i < 32; ++i) hello.executable_sha256[i] = (uint8_t)(i + 16);
    for (unsigned i = 0; i < 32; ++i) hello.port_sha256[i] = (uint8_t)(i + 48);
    assert(lf2_netplay_hello_encode(&hello, bytes, sizeof bytes) == LF2_NETPLAY_HELLO_BYTES);
    assert(!lf2_netplay_hello_encode(&hello, bytes, LF2_NETPLAY_HELLO_BYTES - 1));
    const uint8_t header[] = {'L', 'F', '2', 'N', 3, 1, 0, 0};
    assert(!memcmp(bytes, header, sizeof header));

    Lf2NetplayHello decoded = {0};
    assert(lf2_netplay_hello_decode(bytes, sizeof bytes, &decoded));
    assert(!memcmp(decoded.session_id, hello.session_id, 16));
    assert(!memcmp(decoded.executable_sha256, hello.executable_sha256, 32));
    assert(!memcmp(decoded.port_sha256, hello.port_sha256, 32));

    /* Each invalid case must leave the caller's previous valid hello untouched. */
    const Lf2NetplayHello previous = decoded;
    const size_t corrupt[] = {0, 4, 5, 6, 7};
    const uint8_t replacement[] = {'X', 1, 2, 1, 1};
    for (size_t i = 0; i < sizeof corrupt / sizeof corrupt[0]; ++i) {
        uint8_t copy[sizeof bytes];
        memcpy(copy, bytes, sizeof copy);
        copy[corrupt[i]] = replacement[i];
        assert(!lf2_netplay_hello_decode(copy, sizeof copy, &decoded));
        assert(!memcmp(&decoded, &previous, sizeof decoded));
    }
    assert(!lf2_netplay_hello_decode(bytes, sizeof bytes - 1, &decoded));
    assert(!lf2_netplay_hello_decode(NULL, sizeof bytes, &decoded));
    assert(!lf2_netplay_hello_decode(bytes, sizeof bytes, NULL));
    puts("netplay packet: the hello round-trips; malformed hellos refused");
    return 0;
}

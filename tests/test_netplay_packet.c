#include "packet.h"

#undef NDEBUG
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    uint8_t bytes[LF2_NETPLAY_HELLO_BYTES];
    Lf2NetplayPacket decoded = {.type = LF2_NETPLAY_HELLO};
    Lf2NetplayPacket hello = {.type = LF2_NETPLAY_HELLO};
    for (unsigned i = 0; i < 16; ++i) hello.data.hello.session_id[i] = (uint8_t)i;
    for (unsigned i = 0; i < 32; ++i) hello.data.hello.executable_sha256[i] = (uint8_t)(i + 16);
    assert(lf2_netplay_packet_encode(&hello, bytes, sizeof bytes) == LF2_NETPLAY_HELLO_BYTES);
    assert(!lf2_netplay_packet_encode(&hello, bytes, LF2_NETPLAY_HELLO_BYTES - 1));
    assert(lf2_netplay_packet_decode(bytes, sizeof bytes, &decoded));
    assert(decoded.type == LF2_NETPLAY_HELLO);
    assert(!memcmp(&decoded.data.hello, &hello.data.hello, sizeof hello.data.hello));

    Lf2NetplayPacket input = {.type = LF2_NETPLAY_INPUT,
                              .data.input = {.frame = 0x01020304u, .player_slot = 7, .buttons = 0x55}};
    for (unsigned index = 0; index < 16; ++index)
        input.data.input.session_id[index] = hello.data.hello.session_id[index];
    assert(lf2_netplay_packet_encode(&input, bytes, sizeof bytes) == LF2_NETPLAY_INPUT_BYTES);
    const uint8_t expected[] = {'L', 'F', '2', 'N', 1,  2,  0,  0,  0, 1, 2, 3, 4, 5,    6, 7,
                                8,   9,   10,  11,  12, 13, 14, 15, 1, 2, 3, 4, 7, 0x55, 0, 0};
    assert(!memcmp(bytes, expected, sizeof expected));
    assert(lf2_netplay_packet_decode(bytes, sizeof expected, &decoded));
    assert(decoded.data.input.frame == input.data.input.frame);
    assert(!memcmp(decoded.data.input.session_id, input.data.input.session_id, 16));
    assert(decoded.data.input.player_slot == input.data.input.player_slot);
    assert(decoded.data.input.buttons == input.data.input.buttons);

    /* Each invalid case must leave the caller's previous valid packet untouched. */
    const Lf2NetplayPacket previous = decoded;
    const size_t corrupt[] = {0, 4, 5, 6, 7, 28, 29, 30};
    const uint8_t replacement[] = {'X', 2, 99, 1, 1, 8, 0x80, 1};
    for (size_t i = 0; i < sizeof corrupt / sizeof corrupt[0]; ++i) {
        uint8_t copy[sizeof expected];
        for (size_t index = 0; index < sizeof copy; ++index) copy[index] = expected[index];
        copy[corrupt[i]] = replacement[i];
        assert(!lf2_netplay_packet_decode(copy, sizeof copy, &decoded));
        assert(decoded.type == previous.type);
        assert(decoded.data.input.frame == previous.data.input.frame);
        assert(decoded.data.input.player_slot == previous.data.input.player_slot);
        assert(decoded.data.input.buttons == previous.data.input.buttons);
        assert(!memcmp(decoded.data.input.session_id, previous.data.input.session_id, 16));
    }
    assert(!lf2_netplay_packet_decode(bytes, sizeof expected - 1, &decoded));
    assert(!lf2_netplay_packet_decode(bytes, sizeof expected + 1, &decoded));
    assert(!lf2_netplay_packet_decode(NULL, sizeof expected, &decoded));
    assert(!lf2_netplay_packet_decode(bytes, sizeof expected, NULL));
    input.data.input.buttons = 0x80;
    assert(!lf2_netplay_packet_encode(&input, bytes, sizeof bytes));
    input.data.input.buttons = 0;
    input.data.input.player_slot = 8;
    assert(!lf2_netplay_packet_encode(&input, bytes, sizeof bytes));
    puts("netplay packet: hello and input frames round-trip; malformed packets refused");
    return 0;
}

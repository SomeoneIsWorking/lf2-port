#ifndef LF2_NETPLAY_PACKET_H
#define LF2_NETPLAY_PACKET_H

#include <stddef.h>
#include <stdint.h>

enum {
    LF2_NETPLAY_PLAYER_SLOTS = 8,
    LF2_NETPLAY_BUTTON_MASK = 0x7f,
    LF2_NETPLAY_HELLO_BYTES = 56,
    LF2_NETPLAY_INPUT_BYTES = 32,
};

typedef enum {
    LF2_NETPLAY_HELLO = 1,
    LF2_NETPLAY_INPUT = 2,
} Lf2NetplayPacketType;

typedef struct {
    Lf2NetplayPacketType type;
    union {
        struct {
            uint8_t session_id[16];
            uint8_t executable_sha256[32];
        } hello;
        struct {
            uint8_t session_id[16];
            uint32_t frame;
            uint8_t player_slot;
            uint8_t buttons;
        } input;
    } data;
} Lf2NetplayPacket;

/* Wire format is byte-exact and transport-independent. Return the encoded size, or zero
 * for an invalid packet or insufficient capacity. Decode refuses unknown versions, types,
 * lengths, reserved bits, player slots, and buttons. */
size_t lf2_netplay_packet_encode(const Lf2NetplayPacket *packet, uint8_t *out, size_t capacity);
int lf2_netplay_packet_decode(const uint8_t *bytes, size_t size, Lf2NetplayPacket *out);

#endif

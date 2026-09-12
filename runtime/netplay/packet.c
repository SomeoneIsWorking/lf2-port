#include "packet.h"

#include <string.h>

enum { HEADER_BYTES = 8, WIRE_VERSION = 1 };

static void put_u32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value >> 24);
    out[1] = (uint8_t)(value >> 16);
    out[2] = (uint8_t)(value >> 8);
    out[3] = (uint8_t)value;
}

static uint32_t get_u32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

static void copy_bytes(uint8_t *destination, const uint8_t *source, size_t count)
{
    for (size_t index = 0; index < count; ++index) destination[index] = source[index];
}

size_t lf2_netplay_packet_encode(const Lf2NetplayPacket *packet, uint8_t *out, size_t capacity)
{
    if (!packet || !out) return 0;
    size_t size;
    switch (packet->type) {
    case LF2_NETPLAY_HELLO: size = LF2_NETPLAY_HELLO_BYTES; break;
    case LF2_NETPLAY_INPUT:
        if (packet->data.input.player_slot >= LF2_NETPLAY_PLAYER_SLOTS ||
            (packet->data.input.buttons & (uint8_t)~LF2_NETPLAY_BUTTON_MASK))
            return 0;
        size = LF2_NETPLAY_INPUT_BYTES;
        break;
    default: return 0;
    }
    if (capacity < size) return 0;
    for (size_t index = 0; index < size; ++index) out[index] = 0;
    out[0] = 'L';
    out[1] = 'F';
    out[2] = '2';
    out[3] = 'N';
    out[4] = WIRE_VERSION;
    out[5] = (uint8_t)packet->type;
    if (packet->type == LF2_NETPLAY_HELLO) {
        copy_bytes(out + HEADER_BYTES, packet->data.hello.session_id, 16);
        copy_bytes(out + HEADER_BYTES + 16, packet->data.hello.executable_sha256, 32);
    } else {
        copy_bytes(out + HEADER_BYTES, packet->data.input.session_id, 16);
        put_u32(out + HEADER_BYTES + 16, packet->data.input.frame);
        out[28] = packet->data.input.player_slot;
        out[29] = packet->data.input.buttons;
    }
    return size;
}

int lf2_netplay_packet_decode(const uint8_t *bytes, size_t size, Lf2NetplayPacket *out)
{
    if (!bytes || !out || size < HEADER_BYTES || memcmp(bytes, "LF2N", 4) != 0 || bytes[4] != WIRE_VERSION ||
        bytes[6] != 0 || bytes[7] != 0)
        return 0;
    Lf2NetplayPacket decoded = {.type = LF2_NETPLAY_HELLO};
    decoded.type = (Lf2NetplayPacketType)bytes[5];
    if (decoded.type == LF2_NETPLAY_HELLO && size == LF2_NETPLAY_HELLO_BYTES) {
        copy_bytes(decoded.data.hello.session_id, bytes + HEADER_BYTES, 16);
        copy_bytes(decoded.data.hello.executable_sha256, bytes + HEADER_BYTES + 16, 32);
    } else if (decoded.type == LF2_NETPLAY_INPUT && size == LF2_NETPLAY_INPUT_BYTES && bytes[30] == 0 &&
               bytes[31] == 0 && bytes[28] < LF2_NETPLAY_PLAYER_SLOTS &&
               !(bytes[29] & (uint8_t)~LF2_NETPLAY_BUTTON_MASK)) {
        copy_bytes(decoded.data.input.session_id, bytes + HEADER_BYTES, 16);
        decoded.data.input.frame = get_u32(bytes + HEADER_BYTES + 16);
        decoded.data.input.player_slot = bytes[28];
        decoded.data.input.buttons = bytes[29];
    } else {
        return 0;
    }
    *out = decoded;
    return 1;
}

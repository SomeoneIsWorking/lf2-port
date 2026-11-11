#include "packet.h"

#include <string.h>

enum { HEADER_BYTES = 8, WIRE_VERSION = 2, TYPE_HELLO = 1 };

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

size_t lf2_netplay_hello_encode(const Lf2NetplayHello *hello, uint8_t *out, size_t capacity)
{
    if (!hello || !out || capacity < LF2_NETPLAY_HELLO_BYTES) return 0;
    memset(out, 0, LF2_NETPLAY_HELLO_BYTES);
    out[0] = 'L';
    out[1] = 'F';
    out[2] = '2';
    out[3] = 'N';
    out[4] = WIRE_VERSION;
    out[5] = TYPE_HELLO;
    memcpy(out + HEADER_BYTES, hello->session_id, 16);
    memcpy(out + HEADER_BYTES + 16, hello->executable_sha256, 32);
    const uint64_t epoch = (uint64_t)hello->epoch_ns;
    put_u32(out + HEADER_BYTES + 48, (uint32_t)(epoch >> 32));
    put_u32(out + HEADER_BYTES + 52, (uint32_t)epoch);
    put_u32(out + HEADER_BYTES + 56, (uint32_t)hello->utc_offset_s);
    return LF2_NETPLAY_HELLO_BYTES;
}

int lf2_netplay_hello_decode(const uint8_t *bytes, size_t size, Lf2NetplayHello *out)
{
    if (!bytes || !out || size != LF2_NETPLAY_HELLO_BYTES || memcmp(bytes, "LF2N", 4) != 0 ||
        bytes[4] != WIRE_VERSION || bytes[5] != TYPE_HELLO || bytes[6] != 0 || bytes[7] != 0)
        return 0;
    Lf2NetplayHello decoded;
    memcpy(decoded.session_id, bytes + HEADER_BYTES, 16);
    memcpy(decoded.executable_sha256, bytes + HEADER_BYTES + 16, 32);
    const uint64_t epoch =
        ((uint64_t)get_u32(bytes + HEADER_BYTES + 48) << 32) | get_u32(bytes + HEADER_BYTES + 52);
    decoded.epoch_ns = (int64_t)epoch;
    decoded.utc_offset_s = (int32_t)get_u32(bytes + HEADER_BYTES + 56);
    *out = decoded;
    return 1;
}

#include "packet.h"

#include <string.h>

enum { HEADER_BYTES = 8, WIRE_VERSION = 3, TYPE_HELLO = 1 };

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
    memcpy(out + HEADER_BYTES + 48, hello->port_sha256, 32);
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
    memcpy(decoded.port_sha256, bytes + HEADER_BYTES + 48, 32);
    *out = decoded;
    return 1;
}

#include "lan_socket.h"
#include "packet.h"

#undef NDEBUG
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    Lf2LanSocket *host = lf2_lan_socket_open(0);
    Lf2LanSocket *guest = lf2_lan_socket_open(0);
    assert(host && guest && lf2_lan_socket_port(host) && lf2_lan_socket_port(guest));

    Lf2NetplayPacket sent = {.type = LF2_NETPLAY_INPUT, .data.input = {.frame = 19, .player_slot = 1, .buttons = 0x45}};
    uint8_t datagram[LF2_NETPLAY_HELLO_BYTES + 1];
    const size_t size = lf2_netplay_packet_encode(&sent, datagram, sizeof datagram);
    assert(size == LF2_NETPLAY_INPUT_BYTES);
    assert(!lf2_lan_socket_send(guest, "not-an-address", lf2_lan_socket_port(host), datagram, size));
    assert(lf2_lan_socket_send(guest, "127.0.0.1", lf2_lan_socket_port(host), datagram, size));

    char address[16];
    uint16_t port = 0;
    const int received =
        lf2_lan_socket_receive(host, datagram, LF2_NETPLAY_HELLO_BYTES, address, sizeof address, &port, 1000);
    assert(received == (int)size);
    assert(!strcmp(address, "127.0.0.1"));
    assert(port == lf2_lan_socket_port(guest));
    Lf2NetplayPacket parsed = {.type = LF2_NETPLAY_HELLO};
    assert(lf2_netplay_packet_decode(datagram, (size_t)received, &parsed));
    assert(parsed.data.input.frame == 19 && parsed.data.input.player_slot == 1 && parsed.data.input.buttons == 0x45);

    Lf2NetplayPacket hello = {.type = LF2_NETPLAY_HELLO};
    for (size_t index = 0; index < sizeof hello.data.hello.session_id; ++index)
        hello.data.hello.session_id[index] = 0x5a;
    for (size_t index = 0; index < sizeof hello.data.hello.executable_sha256; ++index)
        hello.data.hello.executable_sha256[index] = 0xa5;
    const size_t hello_size = lf2_netplay_packet_encode(&hello, datagram, sizeof datagram);
    assert(hello_size == LF2_NETPLAY_HELLO_BYTES);
    assert(lf2_lan_socket_send(host, "127.0.0.1", lf2_lan_socket_port(guest), datagram, hello_size));
    assert(lf2_lan_socket_receive(guest, datagram, LF2_NETPLAY_HELLO_BYTES, address, sizeof address, &port, 1000) ==
           LF2_NETPLAY_HELLO_BYTES);
    assert(lf2_netplay_packet_decode(datagram, LF2_NETPLAY_HELLO_BYTES, &parsed));
    assert(parsed.type == LF2_NETPLAY_HELLO && !memcmp(parsed.data.hello.session_id, hello.data.hello.session_id, 16));

    for (size_t index = 0; index < sizeof datagram; ++index) datagram[index] = 0xff;
    assert(lf2_lan_socket_send(guest, "127.0.0.1", lf2_lan_socket_port(host), datagram, sizeof datagram));
    assert(lf2_lan_socket_receive(host, datagram, LF2_NETPLAY_HELLO_BYTES, address, sizeof address, &port, 1000) == -1);
    assert(lf2_lan_socket_receive(host, datagram, LF2_NETPLAY_HELLO_BYTES, address, sizeof address, &port, 0) == 0);

    lf2_lan_socket_close(guest);
    lf2_lan_socket_close(host);
    puts("LAN socket: encoded LF2 input crossed loopback and decoded at the receiving peer");
    return 0;
}

#include "lan_socket.h"
#include "packet.h"

#include <stdlib.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET NativeSocket;
#define BAD_SOCKET INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int NativeSocket;
#define BAD_SOCKET (-1)
#endif

struct Lf2LanSocket {
    NativeSocket native;
    uint16_t port;
};

static void close_native(NativeSocket native)
{
#ifdef _WIN32
    closesocket(native);
#else
    close(native);
#endif
}

Lf2LanSocket *lf2_lan_socket_open(uint16_t port)
{
#ifdef _WIN32
    WSADATA startup;
    if (WSAStartup(MAKEWORD(2, 2), &startup) != 0) return NULL;
#endif
    NativeSocket native = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (native == BAD_SOCKET) goto fail;
    struct sockaddr_in local = {0};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(port);
    if (bind(native, (const struct sockaddr *)&local, sizeof local) != 0) goto fail_socket;
#ifdef _WIN32
    int length = sizeof local;
#else
    socklen_t length = sizeof local;
#endif
    if (getsockname(native, (struct sockaddr *)&local, &length) != 0) goto fail_socket;
    Lf2LanSocket *result = malloc(sizeof *result);
    if (!result) goto fail_socket;
    result->native = native;
    result->port = ntohs(local.sin_port);
    return result;

fail_socket:
    close_native(native);
fail:
#ifdef _WIN32
    WSACleanup();
#endif
    return NULL;
}

void lf2_lan_socket_close(Lf2LanSocket *socket)
{
    if (!socket) return;
    close_native(socket->native);
#ifdef _WIN32
    WSACleanup();
#endif
    free(socket);
}

uint16_t lf2_lan_socket_port(const Lf2LanSocket *socket)
{
    return socket ? socket->port : 0;
}

int lf2_lan_socket_send(Lf2LanSocket *socket, const char *numeric_address, uint16_t port, const uint8_t *bytes,
                        size_t size)
{
    if (!socket || !numeric_address || !bytes || size == 0 || size > 65507) return 0;
    struct sockaddr_in remote = {0};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(port);
    if (inet_pton(AF_INET, numeric_address, &remote.sin_addr) != 1) return 0;
#ifdef _WIN32
    const int written =
        sendto(socket->native, (const char *)bytes, (int)size, 0, (const struct sockaddr *)&remote, sizeof remote);
#else
    const ssize_t written = sendto(socket->native, bytes, size, 0, (const struct sockaddr *)&remote, sizeof remote);
#endif
    return written >= 0 && (size_t)written == size;
}

int lf2_lan_socket_receive(Lf2LanSocket *socket, uint8_t *bytes, size_t capacity, char *address,
                           size_t address_capacity, uint16_t *port, unsigned timeout_ms)
{
    if (!socket || !bytes || !capacity || capacity > LF2_NETPLAY_HELLO_BYTES || !address ||
        address_capacity < INET_ADDRSTRLEN || !port)
        return -1;
    fd_set ready;
    FD_ZERO(&ready);
    FD_SET(socket->native, &ready);
    struct timeval limit = {.tv_sec = (long)(timeout_ms / 1000), .tv_usec = (long)(timeout_ms % 1000) * 1000};
#ifdef _WIN32
    const int selected = select(0, &ready, NULL, NULL, &limit);
#else
    const int selected = select(socket->native + 1, &ready, NULL, NULL, &limit);
#endif
    if (selected <= 0) return selected;
    struct sockaddr_in remote;
    uint8_t datagram[LF2_NETPLAY_HELLO_BYTES + 1];
#ifdef _WIN32
    int length = sizeof remote;
    const int received =
        recvfrom(socket->native, (char *)datagram, sizeof datagram, 0, (struct sockaddr *)&remote, &length);
#else
    socklen_t length = sizeof remote;
    const ssize_t received =
        recvfrom(socket->native, datagram, sizeof datagram, 0, (struct sockaddr *)&remote, &length);
#endif
    if (received <= 0 || (size_t)received > capacity) return -1;
    if (!inet_ntop(AF_INET, &remote.sin_addr, address, address_capacity)) return -1;
    for (size_t index = 0; index < (size_t)received; ++index) bytes[index] = datagram[index];
    *port = ntohs(remote.sin_port);
    return (int)received;
}

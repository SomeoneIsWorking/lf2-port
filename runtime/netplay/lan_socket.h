#ifndef LF2_NETPLAY_LAN_SOCKET_H
#define LF2_NETPLAY_LAN_SOCKET_H

#include <stddef.h>
#include <stdint.h>

typedef struct Lf2LanSocket Lf2LanSocket;

/* IPv4 numeric addresses cover the first explicit-IP LAN route. The socket accepts
 * port zero for an OS-selected test port. Return zero on failure. */
Lf2LanSocket *lf2_lan_socket_open(uint16_t port);
void lf2_lan_socket_close(Lf2LanSocket *socket);
uint16_t lf2_lan_socket_port(const Lf2LanSocket *socket);
int lf2_lan_socket_send(Lf2LanSocket *socket, const char *numeric_address, uint16_t port, const uint8_t *bytes,
                        size_t size);
/* Returns bytes received, zero on timeout, or -1 on error/truncation. */
int lf2_lan_socket_receive(Lf2LanSocket *socket, uint8_t *bytes, size_t capacity, char *address,
                           size_t address_capacity, uint16_t *port, unsigned timeout_ms);

#endif

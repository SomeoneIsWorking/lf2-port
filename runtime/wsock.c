/* Winsock, stubbed.
 *
 * Netplay is not ported, but the game initialises the socket layer during match setup
 * even for a local game, so these have to exist. They report "started successfully, but
 * every operation fails", which is the state the game already knows how to handle -- it
 * is what a machine with no network looks like.
 *
 * The imports are by ordinal, which is how wsock32 was normally linked. Argument counts
 * matter: these are stdcall and the callee pops, so a wrong count corrupts the caller's
 * stack in a way that surfaces far away.
 */
#include "guest_ops.h"

#include <stdio.h>
#include <string.h>

#define ARG(n) LD32(R(ESP) + 4 + 4 * (n))

enum { SOCKET_ERROR = 0xFFFFFFFFu, INVALID_SOCKET = 0xFFFFFFFFu };

static void ret_stdcall(int nargs, uint32_t value)
{
    R(EAX) = value;
    R(ESP) += 4 + 4u * (unsigned)nargs;
}

/* WSAStartup(wVersionRequested, lpWSAData): claim success and fill in a plausible
 * WSADATA, or the game reports "no sockets" before it reaches the match. */
static void h_WSAStartup(void)
{
    const uint32_t data = ARG(1);
    if (data) {
        ST16(data, 0x0202);                      /* wVersion  */
        ST16(data + 2, 0x0202);                  /* wHighVersion */
        for (uint32_t i = 4; i < 400; i += 4) ST32(data + i, 0);
        snprintf((char *)(g_mem + data + 4), 128, "lf2-port stub");
        ST16(data + 388, 1);                     /* iMaxSockets */
        ST16(data + 390, 1024);                  /* iMaxUdpDg   */
    }
    ret_stdcall(2, 0);
}

static void h_gethostname(void)
{
    const uint32_t name = ARG(0), len = ARG(1);
    if (name && len) snprintf((char *)(g_mem + name), len, "localhost");
    ret_stdcall(2, 0);
}

static void h_htons(void)
{
    const uint32_t v = ARG(0) & 0xffff;
    ret_stdcall(1, ((v & 0xff) << 8) | (v >> 8));
}

/* Everything that actually moves data fails. */
static void h_fail1(void) { ret_stdcall(1, SOCKET_ERROR); }
static void h_fail2(void) { ret_stdcall(2, SOCKET_ERROR); }
static void h_fail3(void) { ret_stdcall(3, SOCKET_ERROR); }
static void h_fail4(void) { ret_stdcall(4, SOCKET_ERROR); }
static void h_fail5(void) { ret_stdcall(5, SOCKET_ERROR); }
static void h_null1(void) { ret_stdcall(1, 0); }
static void h_null2(void) { ret_stdcall(2, 0); }
static void h_ok0(void)   { ret_stdcall(0, 0); }
static void h_ok1(void)   { ret_stdcall(1, 0); }
static void h_ok4(void)   { ret_stdcall(4, 0); }

typedef void (*Handler)(void);

Handler wsock_lookup(const char *dll, const char *name)
{
    /* ordinal -> handler. Counts are the documented stdcall signatures. */
    static const struct { const char *ord; Handler fn; } T[] = {
        { "#1",   h_fail3 },   /* accept        */
        { "#2",   h_fail3 },   /* bind          */
        { "#3",   h_ok1   },   /* closesocket   */
        { "#4",   h_fail3 },   /* connect       */
        { "#9",   h_htons },   /* htons         */
        { "#10",  h_fail1 },   /* inet_addr     */
        { "#11",  h_null1 },   /* inet_ntoa     */
        { "#12",  h_fail3 },   /* ioctlsocket   */
        { "#13",  h_fail2 },   /* listen        */
        { "#16",  h_fail4 },   /* recv          */
        { "#19",  h_fail5 },   /* select        */
        { "#20",  h_fail4 },   /* send          */
        { "#23",  h_fail5 },   /* setsockopt    */
        { "#51",  h_null1 },   /* gethostbyname */
        { "#52",  h_gethostname },
        { "#57",  h_null2 },   /* getservbyname */
        { "#101", h_ok4   },   /* WSAAsyncSelect */
        { "#115", h_WSAStartup },
        { "#116", h_ok0   },   /* WSACleanup    */
    };
    if (strcmp(dll, "WSOCK32.dll") != 0) return NULL;
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++)
        if (strcmp(T[i].ord, name) == 0) return T[i].fn;
    return NULL;
}

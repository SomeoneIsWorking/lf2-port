/* Minimal COM for the guest.
 *
 * DirectDrawCreate is the only ddraw import; everything else reaches the game through
 * vtables. So we build those vtables ourselves in guest memory, filled with sentinel
 * addresses that dispatch() routes back here. A guest object is:
 *
 *     [0] vtable address
 *     [4] host slot index
 *
 * Real host state (SDL surfaces, etc.) lives in the slot table, never in guest memory. */
#ifndef COM_H
#define COM_H

#include "guest.h"

enum { COM_SENTINEL = 0xF1000000u };

enum { IF_DDRAW, IF_SURFACE, IF_CLIPPER, IF_PALETTE, IF_DSOUND, IF_DSBUFFER,
       IF_GRAPH, IF_MCONTROL, IF_MPOSITION, IF_BAUDIO, IF_MEVENT, IF_MSEEK,
       IF_COUNT };

/* A COM method: `this` is the guest object address, args follow on the guest stack.
 * Return with com_ret(nargs_including_this, hresult). */
typedef void (*ComMethod)(uint32_t self);

typedef struct {
    const char *name;
    int         nmethods;
    ComMethod   method[64];
    const char *mname[64];   /* method names, for the oracle-comparable trace */
} ComClass;

extern ComClass com_class[IF_COUNT];

/* Which method is currently executing -- lets a shared handler pop the right number of
 * stdcall arguments from a per-interface table instead of needing one function each. */
extern int com_cur_iface, com_cur_method;

void     com_init(void);
uint32_t com_create(int iface, void *host);   /* -> guest object address */
void    *com_host(uint32_t self);
void     com_call(uint32_t sentinel);
void     com_ret(int nargs, uint32_t hresult);

/* Call a guest function from the host (window procedures, callbacks). */
uint32_t guest_call(uint32_t addr, const uint32_t *args, int nargs);

enum { DD_OK = 0, E_FAIL = 0x80004005u, E_NOINTERFACE = 0x80004002u };

void com_release_report(void);

#endif /* COM_H */

#include "com.h"
#include "guest_ops.h"

#include <stdio.h>
#include <stdlib.h>

ComClass com_class[IF_COUNT];

enum { MAX_OBJECTS = 512 };
static struct { uint32_t self; void *host; int iface; } objects[MAX_OBJECTS];
static int nobjects;

static uint32_t vtable_addr[IF_COUNT];

/* Objects and their vtables live in a dedicated slab so they never collide with the
 * guest heap or the loaded image. */
enum { COM_ARENA = 0x30000000u };
static uint32_t com_next = COM_ARENA;

static uint32_t arena_alloc(uint32_t n)
{
    uint32_t p = com_next;
    com_next = (com_next + n + 15u) & ~15u;
    return p;
}

void com_init(void)
{
    for (int i = 0; i < IF_COUNT; i++) {
        const int n = com_class[i].nmethods;
        if (!n) continue;
        vtable_addr[i] = arena_alloc((uint32_t)n * 4);
        for (int m = 0; m < n; m++)
            ST32(vtable_addr[i] + (uint32_t)m * 4,
                 COM_SENTINEL | ((uint32_t)i << 8) | (uint32_t)m);
    }
}

uint32_t com_create(int iface, void *host)
{
    if (nobjects >= MAX_OBJECTS) { fprintf(stderr, "too many COM objects\n"); abort(); }
    uint32_t self = arena_alloc(8);
    ST32(self, vtable_addr[iface]);
    ST32(self + 4, (uint32_t)nobjects);
    objects[nobjects].self = self;
    objects[nobjects].host = host;
    objects[nobjects].iface = iface;
    nobjects++;
    return self;
}

void *com_host(uint32_t self)
{
    uint32_t i = LD32(self + 4);
    return i < (uint32_t)nobjects ? objects[i].host : NULL;
}

void com_ret(int nargs, uint32_t hresult)
{
    R(EAX) = hresult;
    R(ESP) += 4 + 4u * (unsigned)nargs;   /* stdcall: callee pops, `this` included */
}

void com_call(uint32_t sentinel)
{
    const int iface = (int)((sentinel >> 8) & 0xff);
    const int m = (int)(sentinel & 0xff);
    if (iface >= IF_COUNT || m >= com_class[iface].nmethods || !com_class[iface].method[m]) {
        fprintf(stderr, "unimplemented COM method %s::[%d]\n",
                iface < IF_COUNT && com_class[iface].name ? com_class[iface].name : "?", m);
        abort();
    }
    const uint32_t self = LD32(R(ESP) + 4);
    com_class[iface].method[m](self);
}

uint32_t guest_call(uint32_t addr, const uint32_t *args, int nargs)
{
    for (int i = nargs - 1; i >= 0; i--) PUSH32(args[i]);
    PUSH32(0xDEAD0000u);            /* return address the guest's RET will pop */
    dispatch(addr);
    return R(EAX);
}

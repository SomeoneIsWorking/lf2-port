#include "lf2_log.h"
#include "environment.h"
#include "com.h"
#include "guest.h"

#include <stdio.h>
#include <stdlib.h>

ComClass com_class[IF_COUNT];
int com_cur_iface, com_cur_method;

/* Loading a match creates a surface per character sprite sheet, and LF2 ships around
 * forty characters with several sheets each, so 512 ran out mid-load. The table is only
 * pointers; the surfaces themselves live in guest memory. */
enum { MAX_OBJECTS = 8192 };
static struct {
    uint32_t self;
    void *host;
    int iface;
} objects[MAX_OBJECTS];
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
            ST32(vtable_addr[i] + (uint32_t)m * 4, COM_SENTINEL | ((uint32_t)i << 8) | (uint32_t)m);
    }
}

uint32_t com_create(int iface, void *host)
{
    if (nobjects >= MAX_OBJECTS) {
        lf2_log_writef(LF2_LOG_INFO, "com", "too many COM objects\n");
        abort();
    }
    uint32_t self = arena_alloc(8);
    ST32(self, vtable_addr[iface]);
    ST32(self + 4, (uint32_t)nobjects);
    objects[nobjects].self = self;
    objects[nobjects].host = host;
    objects[nobjects].iface = iface;
    nobjects++;
    if (lf2_environment_get(LF2_ENV_COM_TRACE))
        lf2_log_writef(LF2_LOG_INFO, "com", "com_create #%d %s -> %08x (vtbl %08x)\n", nobjects - 1,
                       com_class[iface].name ? com_class[iface].name : "?", self, vtable_addr[iface]);
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
    R(ESP) += 4 + 4u * (unsigned)nargs; /* stdcall: callee pops, `this` included */
}

/* Method 2 of every interface is IUnknown::Release. Counting calls per interface answers
 * whether anything is ever released at all -- which decides whether the surface arena's
 * lack of a free path can actually bite, or is only theoretical. */
long com_releases[IF_COUNT];

void com_release_report(void)
{
    lf2_log_writef(LF2_LOG_INFO, "com", "com releases:");
    int any = 0;
    for (int i = 0; i < IF_COUNT; i++)
        if (com_releases[i]) {
            lf2_log_writef(LF2_LOG_INFO, "com", " %s=%ld", com_class[i].name ? com_class[i].name : "?",
                           com_releases[i]);
            any = 1;
        }
    if (!any) lf2_log_writef(LF2_LOG_INFO, "com", " none -- nothing is ever released");
    lf2_log_writef(LF2_LOG_INFO, "com", "\n");
}

void com_call(uint32_t sentinel)
{
    const int iface = (int)((sentinel >> 8) & 0xff);
    const int m = (int)(sentinel & 0xff);
    if (iface >= IF_COUNT || m >= com_class[iface].nmethods || !com_class[iface].method[m]) {
        lf2_log_writef(LF2_LOG_INFO, "com", "unimplemented COM method %s::[%d]\n",
                       iface < IF_COUNT && com_class[iface].name ? com_class[iface].name : "?", m);
        abort();
    }
    if (lf2_environment_get(LF2_ENV_COM_TRACE)) {
        const char *mn = com_class[iface].mname[m];
        if (mn)
            lf2_log_writef(LF2_LOG_INFO, "com", "TRACE %s::%s this=%08x\n", com_class[iface].name, mn,
                           LD32(R(ESP) + 4));
        else lf2_log_writef(LF2_LOG_INFO, "com", "TRACE %s::[%d]\n", com_class[iface].name, m);
    }
    if (m == 2) com_releases[iface]++; /* IUnknown::Release */
    com_cur_iface = iface;
    com_cur_method = m;
    const uint32_t self = LD32(R(ESP) + 4);
    com_class[iface].method[m](self);
}

uint32_t guest_call(uint32_t addr, const uint32_t *args, int nargs)
{
    for (int i = nargs - 1; i >= 0; i--) PUSH32(args[i]);
    PUSH32(0xDEAD0000u); /* return address the guest's RET will pop */
    dispatch(addr);
    return R(EAX);
}

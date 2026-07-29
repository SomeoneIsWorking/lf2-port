/* Host implementations of the imports lf2.exe calls.
 *
 * Calling convention: the generated code pushes a return address and dispatches, so on
 * entry [ESP] is the return address and [ESP+4+4n] is argument n. stdcall (Win32) pops
 * its own arguments; cdecl (CRT) leaves that to the caller. */
#include "guest_ops.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define ARG(n) LD32(R(ESP) + 4 + 4 * (n))

static void ret_stdcall(int nargs, uint32_t value)
{
    R(EAX) = value;
    R(ESP) += 4 + 4u * (unsigned)nargs;
}

static void ret_cdecl(uint32_t value)
{
    R(EAX) = value;
    R(ESP) += 4;
}

/* ---- guest heap ----
 * Bump allocator over a dedicated region. Free is a no-op for now: the game allocates
 * its sprite and stage data once at load, so this holds for a session, but it will need
 * a real free list before anything long-running. */
enum { HEAP_BASE = 0x20000000u, HEAP_SIZE = 0x20000000u };
static uint32_t heap_next = HEAP_BASE;

static uint32_t guest_alloc(uint32_t size)
{
    size = (size + 15u) & ~15u;
    if (heap_next + size > HEAP_BASE + HEAP_SIZE) {
        fprintf(stderr, "guest heap exhausted\n");
        abort();
    }
    uint32_t p = heap_next;
    heap_next += size;
    return p;
}

/* ---- handlers ---- */

typedef void (*Handler)(void);

static void h_GetSystemTimeAsFileTime(void)
{
    /* Windows epoch is 1601-01-01, in 100 ns ticks. */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t ft = (uint64_t)ts.tv_sec * 10000000ull + (uint64_t)ts.tv_nsec / 100ull
                + 116444736000000000ull;
    uint32_t out = ARG(0);
    ST32(out, (uint32_t)ft);
    ST32(out + 4, (uint32_t)(ft >> 32));
    ret_stdcall(1, 0);
}

static void h_GetTickCount(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ret_stdcall(0, (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000));
}

static void h_QueryPerformanceCounter(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t v = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    ST32(ARG(0), (uint32_t)v);
    ST32(ARG(0) + 4, (uint32_t)(v >> 32));
    ret_stdcall(1, 1);
}

static void h_GetVersionExA(void)
{
    /* Report Windows XP SP3; the game only version-gates on >= 5.1. */
    uint32_t p = ARG(0);
    ST32(p + 4, 5);       /* major */
    ST32(p + 8, 1);       /* minor */
    ST32(p + 12, 2600);   /* build */
    ST32(p + 16, 2);      /* VER_PLATFORM_WIN32_NT */
    ret_stdcall(1, 1);
}

static void h_ret0_0(void) { ret_stdcall(0, 0); }
static void h_ret1_0(void) { ret_stdcall(0, 1); }
static void h_ret0_1(void) { ret_stdcall(1, 0); }
static void h_ret0_2(void) { ret_stdcall(2, 0); }
static void h_ret0_3(void) { ret_stdcall(3, 0); }
static void h_ret0_4(void) { ret_stdcall(4, 0); }
static void h_ret1_1(void) { ret_stdcall(1, 1); }
static void h_ret1_2(void) { ret_stdcall(2, 1); }
static void h_ret1_4(void) { ret_stdcall(4, 1); }

static void h_GetCurrentProcess(void)   { ret_stdcall(0, 0xFFFFFFFFu); }
static void h_GetCurrentProcessId(void) { ret_stdcall(0, 0x1234); }
static void h_GetCurrentThreadId(void)  { ret_stdcall(0, 0x5678); }
static void h_GetModuleHandleA(void)    { ret_stdcall(1, 0x400000); }

static void h_GetStartupInfoA(void)
{
    uint32_t p = ARG(0);
    for (int i = 0; i < 68; i += 4) ST32(p + (uint32_t)i, 0);
    ST32(p, 68);
    ret_stdcall(1, 0);
}

static void h_InterlockedExchange(void)
{
    uint32_t addr = ARG(0), val = ARG(1), old = LD32(addr);
    ST32(addr, val);
    ret_stdcall(2, old);
}

static void h_InterlockedCompareExchange(void)
{
    uint32_t addr = ARG(0), val = ARG(1), cmp = ARG(2), old = LD32(addr);
    if (old == cmp) ST32(addr, val);
    ret_stdcall(3, old);
}

static void h_lstrlenA(void)
{
    uint32_t p = ARG(0), n = 0;
    while (LD8(p + n)) n++;
    ret_stdcall(1, n);
}

static void h_OutputDebugStringA(void)
{
    uint32_t p = ARG(0);
    fprintf(stderr, "[dbg] %s\n", (const char *)(g_mem + p));
    ret_stdcall(1, 0);
}

/* ---- CRT (cdecl: caller pops) ---- */

static void h_malloc(void)  { ret_cdecl(guest_alloc(ARG(0))); }
static void h_calloc(void)
{
    uint32_t n = ARG(0) * ARG(1), p = guest_alloc(n);
    memset(g_mem + p, 0, n);
    ret_cdecl(p);
}
static void h_free(void)    { ret_cdecl(0); }
static void h_memcpy(void)  { memmove(g_mem + ARG(0), g_mem + ARG(1), ARG(2)); ret_cdecl(ARG(0)); }
static void h_memset(void)  { memset(g_mem + ARG(0), (int)ARG(1), ARG(2)); ret_cdecl(ARG(0)); }

static void h_getmainargs(void)
{
    /* argc = 1, argv = { "lf2.exe", NULL }, env = { NULL } */
    static uint32_t argv_block;
    if (!argv_block) {
        uint32_t name = guest_alloc(16);
        memcpy(g_mem + name, "lf2.exe", 8);
        argv_block = guest_alloc(8);
        ST32(argv_block, name);
        ST32(argv_block + 4, 0);
    }
    ST32(ARG(0), 1);
    ST32(ARG(1), argv_block);
    ST32(ARG(2), argv_block + 4);
    ret_cdecl(0);
}

static void h_initterm(void)
{
    /* Walk the function-pointer table and call each non-null entry. */
    uint32_t p = ARG(0), end = ARG(1);
    for (; p < end; p += 4) {
        uint32_t fn = LD32(p);
        if (fn) dispatch(fn);
    }
    ret_cdecl(0);
}

static void h_initterm_e(void)
{
    uint32_t p = ARG(0), end = ARG(1);
    for (; p < end; p += 4) {
        uint32_t fn = LD32(p);
        if (fn) dispatch(fn);
    }
    ret_cdecl(0);
}

static uint32_t commode_slot, fmode_slot;
static void h_p_commode(void) { if (!commode_slot) commode_slot = guest_alloc(4); ret_cdecl(commode_slot); }
static void h_p_fmode(void)   { if (!fmode_slot)   fmode_slot   = guest_alloc(4); ret_cdecl(fmode_slot); }

static void h_cdecl0(void)  { ret_cdecl(0); }
static void h_identity(void) { ret_cdecl(ARG(0)); }   /* _encode_pointer / _decode_pointer */

static void h_controlfp_s(void)
{
    if (ARG(0)) ST32(ARG(0), 0x8001f);
    ret_cdecl(0);
}


/* ---- CRT file I/O ----
 * Guest FILE* is an opaque token; the host FILE* lives in a side table so guest code
 * never sees a 64-bit pointer. */
enum { MAX_FILES = 64 };
static FILE *files[MAX_FILES];

static uint32_t file_token(FILE *fh)
{
    for (int i = 1; i < MAX_FILES; i++)
        if (!files[i]) { files[i] = fh; return 0xFE000000u + (uint32_t)i; }
    return 0;
}

static FILE *file_of(uint32_t tok)
{
    uint32_t i = tok - 0xFE000000u;
    return (i > 0 && i < MAX_FILES) ? files[i] : NULL;
}

static const char *gstr(uint32_t p) { return (const char *)(g_mem + p); }

static void h_fopen(void)
{
    FILE *fh = fopen(gstr(ARG(0)), gstr(ARG(1)));
    ret_cdecl(fh ? file_token(fh) : 0);
}

static void h_fclose(void)
{
    FILE *fh = file_of(ARG(0));
    if (fh) { fclose(fh); files[ARG(0) - 0xFE000000u] = NULL; }
    ret_cdecl(0);
}

static void h_fgets(void)
{
    FILE *fh = file_of(ARG(2));
    char *r = fh ? fgets((char *)(g_mem + ARG(0)), (int)ARG(1), fh) : NULL;
    ret_cdecl(r ? ARG(0) : 0);
}

static void h_feof(void)
{
    FILE *fh = file_of(ARG(0));
    ret_cdecl(fh ? (uint32_t)feof(fh) : 1);
}

/* ---- printf family ----
 * Formats against guest varargs (already on the guest stack) into guest memory.
 * Only the conversions this binary actually uses are handled; anything else aborts
 * rather than silently emitting the wrong text. */
static int gformat(char *out, size_t cap, const char *fmt, uint32_t argp)
{
    size_t o = 0;
    for (const char *f = fmt; *f && o + 1 < cap; f++) {
        if (*f != '%') { out[o++] = *f; continue; }
        char spec[32]; int n = 0;
        spec[n++] = *f++;
        while (*f && !strchr("diouxXcsfgeEp%", *f) && n < 30) spec[n++] = *f++;
        if (!*f) break;
        spec[n++] = *f;
        spec[n] = 0;

        char tmp[512];
        switch (*f) {
        case '%': tmp[0] = '%'; tmp[1] = 0; break;
        case 'd': case 'i': case 'u': case 'o': case 'x': case 'X': case 'c':
            snprintf(tmp, sizeof tmp, spec, (int)LD32(argp)); argp += 4; break;
        case 's':
            snprintf(tmp, sizeof tmp, spec, gstr(LD32(argp))); argp += 4; break;
        case 'f': case 'g': case 'e': case 'E': {
            double d; __builtin_memcpy(&d, g_mem + argp, 8);
            snprintf(tmp, sizeof tmp, spec, d); argp += 8; break;
        }
        case 'p':
            snprintf(tmp, sizeof tmp, "%08x", LD32(argp)); argp += 4; break;
        default:
            fprintf(stderr, "unsupported printf conversion '%s'\n", spec); abort();
        }
        for (const char *t = tmp; *t && o + 1 < cap; t++) out[o++] = *t;
    }
    out[o] = 0;
    return (int)o;
}

static void h_sprintf(void)
{
    char buf[4096];
    int n = gformat(buf, sizeof buf, gstr(ARG(1)), R(ESP) + 4 + 8);
    memcpy(g_mem + ARG(0), buf, (size_t)n + 1);
    ret_cdecl((uint32_t)n);
}

static void h_fprintf(void)
{
    char buf[4096];
    int n = gformat(buf, sizeof buf, gstr(ARG(1)), R(ESP) + 4 + 8);
    FILE *fh = file_of(ARG(0));
    if (fh) fwrite(buf, 1, (size_t)n, fh);
    ret_cdecl((uint32_t)n);
}

static void h_rand(void)  { ret_cdecl((uint32_t)(rand() & 0x7fff)); }
static void h_srand(void) { srand(ARG(0)); ret_cdecl(0); }
static void h_time64(void)
{
    int64_t t = (int64_t)time(NULL);
    if (ARG(0)) { ST32(ARG(0), (uint32_t)t); ST32(ARG(0) + 4, (uint32_t)(t >> 32)); }
    R(EAX) = (uint32_t)t; R(EDX) = (uint32_t)(t >> 32);
    R(ESP) += 4;
}
static void h_exit(void) { exit((int)ARG(0)); }
static void h_getcwd(void)
{
    if (ARG(0) && getcwd((char *)(g_mem + ARG(0)), ARG(1))) ret_cdecl(ARG(0));
    else ret_cdecl(0);
}
static void h_chdir(void) { ret_cdecl((uint32_t)chdir(gstr(ARG(0)))); }

static void h_timeGetTime(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ret_stdcall(0, (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000));
}

/* Joystick: reports no devices for now. The SDL3 gamepad backend replaces these, and
 * that is where hotplug support arrives -- see docs/platform-boundary.md. */
enum { JOYERR_UNPLUGGED = 167 };
static void h_joyGetNumDevs(void)  { ret_stdcall(0, 0); }
static void h_joyGetDevCaps(void)  { ret_stdcall(3, JOYERR_UNPLUGGED); }
static void h_joyGetPosEx(void)    { ret_stdcall(2, JOYERR_UNPLUGGED); }
static void h_joyGetPos(void)      { ret_stdcall(2, JOYERR_UNPLUGGED); }
static void h_joySetCapture(void)  { ret_stdcall(4, JOYERR_UNPLUGGED); }
static void h_joySetThreshold(void){ ret_stdcall(2, JOYERR_UNPLUGGED); }

/* ---- table ---- */

static const struct { const char *dll, *name; Handler fn; } TABLE[] = {
    { "KERNEL32.dll", "GetSystemTimeAsFileTime", h_GetSystemTimeAsFileTime },
    { "KERNEL32.dll", "GetTickCount",            h_GetTickCount },
    { "KERNEL32.dll", "QueryPerformanceCounter", h_QueryPerformanceCounter },
    { "KERNEL32.dll", "GetVersionExA",           h_GetVersionExA },
    { "KERNEL32.dll", "GetCurrentProcess",       h_GetCurrentProcess },
    { "KERNEL32.dll", "GetCurrentProcessId",     h_GetCurrentProcessId },
    { "KERNEL32.dll", "GetCurrentThreadId",      h_GetCurrentThreadId },
    { "KERNEL32.dll", "GetModuleHandleA",        h_GetModuleHandleA },
    { "KERNEL32.dll", "GetStartupInfoA",         h_GetStartupInfoA },
    { "KERNEL32.dll", "InitializeCriticalSection", h_ret0_1 },
    { "KERNEL32.dll", "EnterCriticalSection",    h_ret0_1 },
    { "KERNEL32.dll", "LeaveCriticalSection",    h_ret0_1 },
    { "KERNEL32.dll", "SetUnhandledExceptionFilter", h_ret0_1 },
    { "KERNEL32.dll", "UnhandledExceptionFilter",    h_ret0_1 },
    { "KERNEL32.dll", "IsDebuggerPresent",       h_ret0_0 },
    { "KERNEL32.dll", "GetLastError",            h_ret0_0 },
    { "KERNEL32.dll", "GetACP",                  h_ret1_0 },
    { "KERNEL32.dll", "GetThreadLocale",         h_ret1_0 },
    { "KERNEL32.dll", "GetLocaleInfoA",          h_ret1_4 },
    { "KERNEL32.dll", "InterlockedExchange",     h_InterlockedExchange },
    { "KERNEL32.dll", "InterlockedCompareExchange", h_InterlockedCompareExchange },
    { "KERNEL32.dll", "lstrlenA",                h_lstrlenA },
    { "KERNEL32.dll", "OutputDebugStringA",      h_OutputDebugStringA },
    { "KERNEL32.dll", "Sleep",                   h_ret0_1 },
    { "KERNEL32.dll", "MultiByteToWideChar",     h_ret1_2 },

    { "MSVCR80.dll", "__getmainargs",       h_getmainargs },
    { "MSVCR80.dll", "_initterm",           h_initterm },
    { "MSVCR80.dll", "_initterm_e",         h_initterm_e },
    { "MSVCR80.dll", "__set_app_type",      h_cdecl0 },
    { "MSVCR80.dll", "__p__commode",        h_p_commode },
    { "MSVCR80.dll", "__p__fmode",          h_p_fmode },
    { "MSVCR80.dll", "_controlfp_s",        h_controlfp_s },
    { "MSVCR80.dll", "_configthreadlocale", h_cdecl0 },
    { "MSVCR80.dll", "_encode_pointer",     h_identity },
    { "MSVCR80.dll", "_decode_pointer",     h_identity },
    { "MSVCR80.dll", "_lock",               h_cdecl0 },
    { "MSVCR80.dll", "_unlock",             h_cdecl0 },
    { "MSVCR80.dll", "__dllonexit",         h_identity },
    { "MSVCR80.dll", "_onexit",             h_identity },
    { "MSVCR80.dll", "_crt_debugger_hook",  h_cdecl0 },
    { "MSVCR80.dll", "__setusermatherr",    h_cdecl0 },
    { "MSVCR80.dll", "_setusermatherr",     h_cdecl0 },
    { "MSVCR80.dll", "_adjust_fdiv",        h_cdecl0 },
    { "MSVCR80.dll", "malloc",              h_malloc },
    { "MSVCR80.dll", "calloc",              h_calloc },
    { "MSVCR80.dll", "free",                h_free },
    { "MSVCR80.dll", "memcpy",              h_memcpy },
    { "MSVCR80.dll", "memset",              h_memset },
    { "MSVCR80.dll", "??2@YAPAXI@Z",        h_malloc },   /* operator new */
    { "MSVCR80.dll", "??_U@YAPAXI@Z",       h_malloc },   /* operator new[] */
    { "MSVCR80.dll", "??3@YAXPAX@Z",        h_free },     /* operator delete */
    { "MSVCR80.dll", "_ismbblead",          h_cdecl0 },
    { "MSVCR80.dll", "_XcptFilter",         h_cdecl0 },
    { "MSVCR80.dll", "__CxxFrameHandler3",  h_cdecl0 },
    { "MSVCR80.dll", "_except_handler4_common", h_cdecl0 },
    { "MSVCR80.dll", "_invoke_watson",      h_cdecl0 },
    { "MSVCR80.dll", "?terminate@@YAXXZ",   h_cdecl0 },
    { "MSVCR80.dll", "_amsg_exit",          h_exit },
    { "MSVCR80.dll", "exit",                h_exit },
    { "MSVCR80.dll", "_exit",               h_exit },
    { "MSVCR80.dll", "_cexit",              h_cdecl0 },
    { "MSVCR80.dll", "fopen",               h_fopen },
    { "MSVCR80.dll", "fclose",              h_fclose },
    { "MSVCR80.dll", "fgets",               h_fgets },
    { "MSVCR80.dll", "feof",                h_feof },
    { "MSVCR80.dll", "fprintf",             h_fprintf },
    { "MSVCR80.dll", "sprintf",             h_sprintf },
    { "MSVCR80.dll", "rand",                h_rand },
    { "MSVCR80.dll", "srand",               h_srand },
    { "MSVCR80.dll", "_time64",             h_time64 },
    { "MSVCR80.dll", "_getcwd",             h_getcwd },
    { "MSVCR80.dll", "_chdir",              h_chdir },

    { "WINMM.dll", "timeGetTime",      h_timeGetTime },
    { "WINMM.dll", "joyGetNumDevs",    h_joyGetNumDevs },
    { "WINMM.dll", "joyGetDevCapsA",   h_joyGetDevCaps },
    { "WINMM.dll", "joyGetDevCapsW",   h_joyGetDevCaps },
    { "WINMM.dll", "joyGetPosEx",      h_joyGetPosEx },
    { "WINMM.dll", "joyGetPos",        h_joyGetPos },
    { "WINMM.dll", "joySetCapture",    h_joySetCapture },
    { "WINMM.dll", "joySetThreshold",  h_joySetThreshold },
};

Handler host_lookup(const char *dll, const char *name)
{
    for (size_t i = 0; i < sizeof TABLE / sizeof TABLE[0]; i++) {
        if (strcmp(TABLE[i].dll, dll) == 0 && strcmp(TABLE[i].name, name) == 0)
            return TABLE[i].fn;
    }
    return NULL;
}

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
#include <dirent.h>
#include <strings.h>

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

/* ---- path translation ----
 * The game stores Windows paths ("data\\m_ok.wav"). Backslashes become slashes, and if
 * that still misses we retry component-by-component case-insensitively, because the
 * original filesystem was case-insensitive and the data files are not consistent. */
static const char *host_path(uint32_t guest_str);

static int find_ci(const char *dir, const char *want, char *out, size_t cap)
{
    DIR *d = opendir(dir[0] ? dir : ".");
    if (!d) return 0;
    struct dirent *e;
    int found = 0;
    while ((e = readdir(d))) {
        if (strcasecmp(e->d_name, want) == 0) {
            snprintf(out, cap, "%s", e->d_name);
            found = 1;
            break;
        }
    }
    closedir(d);
    return found;
}

static const char *host_path(uint32_t guest_str)
{
    static char path[1024];
    const char *src = (const char *)(g_mem + guest_str);
    size_t n = 0;
    for (; src[n] && n + 1 < sizeof path; n++) path[n] = (src[n] == '\\') ? '/' : src[n];
    path[n] = 0;

    if (access(path, F_OK) == 0) return path;

    /* Rebuild the path one component at a time, matching case-insensitively. */
    char built[1024] = "";
    char work[1024];
    snprintf(work, sizeof work, "%s", path);
    char *save = NULL;
    for (char *tok = strtok_r(work, "/", &save); tok; tok = strtok_r(NULL, "/", &save)) {
        char match[256];
        char probe[1024];
        snprintf(probe, sizeof probe, "%s%s", built, tok);
        if (access(probe, F_OK) == 0) {
            snprintf(built + strlen(built), sizeof built - strlen(built), "%s/", tok);
            continue;
        }
        char dir[1024];
        snprintf(dir, sizeof dir, "%s", built[0] ? built : ".");
        if (!find_ci(dir, tok, match, sizeof match)) return path;   /* give up, report original */
        snprintf(built + strlen(built), sizeof built - strlen(built), "%s/", match);
    }
    n = strlen(built);
    if (n && built[n - 1] == '/') built[n - 1] = 0;
    snprintf(path, sizeof path, "%s", built);
    return path;
}


const char *host_path_of(uint32_t g) { return host_path(g); }

/* Text-mode translation.
 *
 * MSVC's CRT opens files in TEXT mode unless the mode string says "b", and translates
 * CRLF to LF on the way in. Linux does no such thing, so every line the game read
 * carried a trailing \r it does not expect -- enough to send a parse down a branch the
 * real program never takes. The file is slurped, translated, and handed back as an
 * in-memory stream so fscanf/fgets see what they would see on Windows. */
static char *text_buf[MAX_FILES];

static FILE *open_translated(const char *path)
{
    FILE *raw = fopen(path, "rb");
    if (!raw) return NULL;
    fseek(raw, 0, SEEK_END);
    long n = ftell(raw);
    rewind(raw);
    if (n < 0) { fclose(raw); return NULL; }

    char *buf = malloc((size_t)n + 1);
    const size_t got = fread(buf, 1, (size_t)n, raw);
    fclose(raw);

    size_t out = 0;
    for (size_t i = 0; i < got; i++) {
        if (buf[i] == '\r' && i + 1 < got && buf[i + 1] == '\n') continue;
        buf[out++] = buf[i];
    }
    buf[out] = 0;

    FILE *fh = fmemopen(buf, out, "r");
    if (!fh) { free(buf); return NULL; }
    return fh;
}

static void h_fopen(void)
{
    const char *mode = gstr(ARG(1));
    const int text = !strchr(mode, 'b');
    const int reading = !strchr(mode, 'w') && !strchr(mode, 'a');

    FILE *fh = (text && reading) ? open_translated(host_path(ARG(0)))
                                 : fopen(host_path(ARG(0)), mode);
    if (!fh) { ret_cdecl(0); return; }
    const uint32_t tok = file_token(fh);
    ret_cdecl(tok);
}

static void h_fclose(void)
{
    FILE *fh = file_of(ARG(0));
    if (fh) {
        const uint32_t i = ARG(0) - 0xFE000000u;
        fclose(fh);
        files[i] = NULL;
        free(text_buf[i]);
        text_buf[i] = NULL;
    }
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
    if (getenv("LF2_STR_DEBUG"))
        fprintf(stderr, "sprintf -> %08x (%d bytes) fmt=\"%s\" out=\"%.60s\"\n",
                ARG(0), n, gstr(ARG(1)), buf);
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


/* ---- scanf family ----
 * Directives are executed one at a time against the host, so the host does the stream
 * positioning and the numeric parsing; only the destination store is ours. Literal and
 * whitespace runs are passed through so they still have to match. */
static int scan_directive(FILE *fh, const char **cur, const char *spec, char conv,
                          uint32_t out, int suppress)
{
    char sp[64];
    snprintf(sp, sizeof sp, "%s%%n", spec);
    int consumed = -1, got = 0;

    if (conv == 0 || suppress) {                 /* literal run, or assignment-suppressed */
        if (fh) { if (fscanf(fh, spec) == EOF) return -1; }
        else {
            if (sscanf(*cur, sp, &consumed) == EOF) return -1;
            if (consumed >= 0) *cur += consumed;
        }
        return 0;
    }

    switch (conv) {
    case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': {
        int v = 0;
        got = fh ? fscanf(fh, spec, &v) : sscanf(*cur, sp, &v, &consumed);
        if (got >= 1) ST32(out, (uint32_t)v);
        break;
    }
    case 'f': case 'g': case 'e': {
        float v = 0;
        got = fh ? fscanf(fh, spec, &v) : sscanf(*cur, sp, &v, &consumed);
        if (got >= 1) { uint32_t b; __builtin_memcpy(&b, &v, 4); ST32(out, b); }
        break;
    }
    case 'l': return -2;                          /* %lf and friends: not used here */
    case 's': case 'c': {
        char buf[512] = {0};
        got = fh ? fscanf(fh, spec, buf) : sscanf(*cur, sp, buf, &consumed);
        if (got >= 1) {
            size_t n = strlen(buf);
            if (getenv("LF2_STR_DEBUG"))
                fprintf(stderr, "  %%s -> %08x len=%zu tok=[%.20s]\n", out, n, buf);
            memcpy(g_mem + out, buf, n);
            if (conv == 's') ST8(out + (uint32_t)n, 0);
        }
        break;
    }
    default: return -2;
    }
    if (!fh && consumed >= 0) *cur += consumed;
    return got >= 1 ? 1 : (got == EOF ? -1 : 0);
}

static int gscan(FILE *fh, const char *start, const char *fmt, uint32_t argp)
{
    const char *cur = start;
    int assigned = 0;

    for (const char *f = fmt; *f;) {
        if (*f != '%') {                          /* literal / whitespace run */
            char lit[128]; int n = 0;
            while (*f && *f != '%' && n < 120) lit[n++] = *f++;
            lit[n] = 0;
            if (scan_directive(fh, &cur, lit, 0, 0, 1) < 0) return assigned ? assigned : -1;
            continue;
        }
        char spec[64]; int n = 0;
        spec[n++] = *f++;
        int suppress = 0;
        if (*f == '*') { suppress = 1; spec[n++] = *f++; }
        while (*f && !strchr("diouxXcsfgeE%", *f) && n < 60) spec[n++] = *f++;
        if (!*f) break;
        const char conv = *f;
        spec[n++] = *f++;
        spec[n] = 0;
        if (conv == '%') { continue; }

        uint32_t out = 0;
        if (!suppress) { out = LD32(argp); argp += 4; }
        const int r = scan_directive(fh, &cur, spec, conv, out, suppress);
        if (r == -2) { fprintf(stderr, "unsupported scanf conversion '%s'\n", spec); abort(); }
        if (r < 0) return assigned ? assigned : -1;
        if (r == 0) break;
        assigned += !suppress;
    }
    return assigned;
}

static void h_fscanf(void)
{
    FILE *fh = file_of(ARG(0));
    const int n = fh ? gscan(fh, NULL, gstr(ARG(1)), R(ESP) + 4 + 8) : -1;
    if (getenv("LF2_STR_DEBUG")) {
        fprintf(stderr, "fscanf -> %d fmt=\"", n);
        for (const char *c = gstr(ARG(1)); *c; c++)
            fputs(*c == '\n' ? "\\n" : *c == '\r' ? "\\r" : (char[]){*c, 0}, stderr);
        fprintf(stderr, "\"\n");
    }
    ret_cdecl((uint32_t)n);
}

static void h_sscanf(void)
{
    const int n = gscan(NULL, gstr(ARG(0)), gstr(ARG(1)), R(ESP) + 4 + 8);
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

/* MultiByteToWideChar(CodePage, dwFlags, src, cbSrc, dst, cchDst).
 * Six parameters -- popping the wrong number leaks guest stack on every call, which
 * shows up much later as a POP taking a return address. */
static void h_MultiByteToWideChar(void)
{
    const uint32_t src = ARG(2), dst = ARG(4);
    const int32_t cb = (int32_t)ARG(3);
    const uint32_t cch = ARG(5);

    uint32_t n = 0;
    if (cb < 0) { while (LD8(src + n)) n++; n++; }    /* -1: NUL-terminated, NUL included */
    else n = (uint32_t)cb;

    if (cch == 0) { ret_stdcall(6, n); return; }      /* size query */

    uint32_t written = 0;
    for (; written < n && written < cch; written++)
        ST16(dst + written * 2, LD8(src + written));  /* the game's text is 8-bit */
    ret_stdcall(6, written);
}

static void h_localtime64(void)
{
    /* MSVC struct tm: nine ints. Returned in a static guest buffer, as the CRT does. */
    static uint32_t buf;
    if (!buf) buf = guest_alloc(36);
    int64_t t = (int64_t)LD32(ARG(0)) | ((int64_t)LD32(ARG(0) + 4) << 32);
    time_t tt = (time_t)t;
    struct tm *g = localtime(&tt);
    const int v[9] = { g->tm_sec, g->tm_min, g->tm_hour, g->tm_mday,
                       g->tm_mon, g->tm_year, g->tm_wday, g->tm_yday, g->tm_isdst };
    for (int i = 0; i < 9; i++) ST32(buf + (uint32_t)i * 4, (uint32_t)v[i]);
    ret_cdecl(buf);
}
static void h_getcwd(void)
{
    if (ARG(0) && getcwd((char *)(g_mem + ARG(0)), ARG(1))) ret_cdecl(ARG(0));
    else ret_cdecl(0);
}
static void h_chdir(void) { ret_cdecl((uint32_t)chdir(gstr(ARG(0)))); }


/* ---- MMIO ----
 * The game reads its WAVs through the RIFF chunk API rather than plain fread.
 * MMCKINFO is { ckid, cksize, fccType, dwDataOffset, dwFlags }. */
enum { MMIO_FINDCHUNK = 0x0010, MMIO_FINDRIFF = 0x0020, MMIO_FINDLIST = 0x0040 };
enum { MMSYSERR_NOERROR = 0, MMIOERR_CHUNKNOTFOUND = 261 };

static void h_mmioOpenA(void)
{
    FILE *fh = fopen(host_path(ARG(0)), "rb");
    ret_stdcall(3, fh ? file_token(fh) : 0);
}

static void h_mmioClose(void)
{
    FILE *fh = file_of(ARG(0));
    if (fh) { fclose(fh); files[ARG(0) - 0xFE000000u] = NULL; }
    ret_stdcall(2, 0);
}

static void h_mmioRead(void)
{
    FILE *fh = file_of(ARG(0));
    long n = fh ? (long)fread(g_mem + ARG(1), 1, ARG(2), fh) : -1;
    ret_stdcall(3, (uint32_t)n);
}

static void h_mmioDescend(void)
{
    FILE *fh = file_of(ARG(0));
    const uint32_t ck = ARG(1), flags = ARG(3);
    if (!fh) { ret_stdcall(4, MMIOERR_CHUNKNOTFOUND); return; }

    const uint32_t want = (flags & (MMIO_FINDRIFF | MMIO_FINDLIST | MMIO_FINDCHUNK))
                        ? LD32(ck + ((flags & MMIO_FINDCHUNK) ? 0 : 8)) : 0;

    for (;;) {
        uint8_t hdr[8];
        if (fread(hdr, 1, 8, fh) != 8) { ret_stdcall(4, MMIOERR_CHUNKNOTFOUND); return; }
        const uint32_t id = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8)
                          | ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
        const uint32_t size = (uint32_t)hdr[4] | ((uint32_t)hdr[5] << 8)
                            | ((uint32_t)hdr[6] << 16) | ((uint32_t)hdr[7] << 24);

        uint32_t type = 0;
        const int is_container = (flags & (MMIO_FINDRIFF | MMIO_FINDLIST)) != 0;
        if (is_container) {
            uint8_t t[4];
            if (fread(t, 1, 4, fh) != 4) { ret_stdcall(4, MMIOERR_CHUNKNOTFOUND); return; }
            type = (uint32_t)t[0] | ((uint32_t)t[1] << 8)
                 | ((uint32_t)t[2] << 16) | ((uint32_t)t[3] << 24);
        }

        const uint32_t data_off = (uint32_t)ftell(fh);
        const uint32_t match = is_container ? type : id;
        if (!want || match == want) {
            ST32(ck, id);
            ST32(ck + 4, size);
            ST32(ck + 8, type);
            ST32(ck + 12, data_off);
            ST32(ck + 16, 0);
            ret_stdcall(4, MMSYSERR_NOERROR);
            return;
        }
        /* Not the chunk asked for: skip its body (chunks are word-aligned) and retry. */
        const long skip = (long)size - (is_container ? 4 : 0);
        if (fseek(fh, skip + (skip & 1), SEEK_CUR) != 0) {
            ret_stdcall(4, MMIOERR_CHUNKNOTFOUND);
            return;
        }
    }
}

static void h_mmioAscend(void)
{
    FILE *fh = file_of(ARG(0));
    const uint32_t ck = ARG(1);
    if (fh) {
        const uint32_t end = LD32(ck + 12) + LD32(ck + 4);
        fseek(fh, (long)(end + (end & 1)), SEEK_SET);
    }
    ret_stdcall(3, MMSYSERR_NOERROR);
}


/* ---- Win32 file API ----
 * The import table has CreateFileA/WriteFile/CloseHandle but no ReadFile, so this path
 * is write-only: settings and recorded matches. */
static void h_CreateFileA(void)
{
    const uint32_t access = ARG(1), disp = ARG(4);
    const char *mode = (access & 0x40000000u) ? ((disp == 2 /*CREATE_ALWAYS*/) ? "wb" : "r+b")
                                              : "rb";
    FILE *fh = fopen(host_path(ARG(0)), mode);
    if (!fh && (access & 0x40000000u)) fh = fopen(host_path(ARG(0)), "wb");
    ret_stdcall(7, fh ? file_token(fh) : 0xFFFFFFFFu);   /* INVALID_HANDLE_VALUE */
}

static void h_WriteFile(void)
{
    FILE *fh = file_of(ARG(0));
    size_t n = fh ? fwrite(g_mem + ARG(1), 1, ARG(2), fh) : 0;
    if (ARG(3)) ST32(ARG(3), (uint32_t)n);
    ret_stdcall(5, fh ? 1 : 0);
}

static void h_CloseHandle(void)
{
    FILE *fh = file_of(ARG(0));
    if (fh) { fclose(fh); files[ARG(0) - 0xFE000000u] = NULL; }
    ret_stdcall(1, 1);
}

static void h_GetLocalTime(void)
{
    time_t t = time(NULL);
    struct tm *g = localtime(&t);
    const uint32_t p = ARG(0);
    const uint16_t v[8] = { (uint16_t)(g->tm_year + 1900), (uint16_t)(g->tm_mon + 1),
                            (uint16_t)g->tm_wday, (uint16_t)g->tm_mday,
                            (uint16_t)g->tm_hour, (uint16_t)g->tm_min,
                            (uint16_t)g->tm_sec, 0 };
    for (int i = 0; i < 8; i++) ST16(p + (uint32_t)i * 2, v[i]);
    ret_stdcall(1, 0);
}

/* Netplay is out of scope, and the thread this creates is the network thread. It is not
 * started: running it inline would block the caller, and the game must not observe a
 * silently-succeeding thread that never runs, so this is logged. */
static void h_CreateThread(void)
{
    static int warned;
    if (!warned) {
        fprintf(stderr, "note: CreateThread ignored (netplay is not ported)\n");
        warned = 1;
    }
    if (ARG(5)) ST32(ARG(5), 0);
    ret_stdcall(6, 0xFD000001u);
}

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
    { "KERNEL32.dll", "MultiByteToWideChar",     h_MultiByteToWideChar },
    { "KERNEL32.dll", "CreateFileA",             h_CreateFileA },
    { "KERNEL32.dll", "WriteFile",               h_WriteFile },
    { "KERNEL32.dll", "CloseHandle",             h_CloseHandle },
    { "KERNEL32.dll", "GetLocalTime",            h_GetLocalTime },
    { "KERNEL32.dll", "CreateThread",            h_CreateThread },
    { "KERNEL32.dll", "TerminateProcess",        h_ret0_2 },

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
    { "MSVCR80.dll", "fscanf",              h_fscanf },
    { "MSVCR80.dll", "sscanf",              h_sscanf },
    { "MSVCR80.dll", "rand",                h_rand },
    { "MSVCR80.dll", "srand",               h_srand },
    { "MSVCR80.dll", "_time64",             h_time64 },
    { "MSVCR80.dll", "_localtime64",        h_localtime64 },
    { "MSVCR80.dll", "_getcwd",             h_getcwd },
    { "MSVCR80.dll", "_chdir",              h_chdir },

    { "WINMM.dll", "timeGetTime",      h_timeGetTime },
    { "WINMM.dll", "mmioOpenA",        h_mmioOpenA },
    { "WINMM.dll", "mmioClose",        h_mmioClose },
    { "WINMM.dll", "mmioRead",         h_mmioRead },
    { "WINMM.dll", "mmioDescend",      h_mmioDescend },
    { "WINMM.dll", "mmioAscend",       h_mmioAscend },
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

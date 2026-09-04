#include "jit_executor.h"

#include "com.h"
#include "cpu.h"
#include "guest.h"
#include "jit_engine.h"
#include "lf2_log.h"
#include "native_override.h"

#include <limits.h>
#include <stdlib.h>

enum {
    JIT_CODE_BYTES = 16 * 1024 * 1024,
    JIT_CACHE_BLOCKS = 65536,
    JIT_SLICE_STEPS = 1024 * 1024,
    JIT_MAX_SLICES_PER_CALL = 256,
};

typedef struct {
    uint32_t return_address;
    uint32_t excluded_override;
} CallBoundary;

typedef struct {
    uint32_t guest_address;
    uint32_t excluded_override;
} CallRequest;

static X86pJitEngine *engine;
static X86pMem memory;
static X86pCpu jit_cpu;

void com_call(uint32_t sentinel);
void host_import(uint32_t sentinel);

static void view_from_jit(void)
{
    for (unsigned index = 0; index < 8; ++index) cpu.r[index] = jit_cpu.reg[index];
    cpu.eip = jit_cpu.eip;
}

static void jit_from_view(void)
{
    for (unsigned index = 0; index < 8; ++index) jit_cpu.reg[index] = cpu.r[index];
    jit_cpu.eip = cpu.eip;
}

static int intercept(const X86pCpu *state, void *user)
{
    const CallBoundary *boundary = user;
    if (state->eip == boundary->return_address || state->eip >= IMPORT_SENTINEL) return 1;
    return lf2_native_override_find(state->eip, boundary->excluded_override) != NULL;
}

static int translation_boundary(uint32_t address, void *user)
{
    const CallBoundary *boundary = user;
    if (address == boundary->return_address || address >= IMPORT_SENTINEL) return 1;
    return lf2_native_override_find(address, boundary->excluded_override) != NULL;
}

static void report_stats(void)
{
    if (!engine) return;
    X86pJitEngineStats stats;
    x86p_jit_engine_stats(engine, &stats);
    lf2_log_writef(LF2_LOG_INFO, "jit",
                   "blocks entered=%llu translated=%llu; instructions translated=%llu; "
                   "translation refusals=%llu; cache flushes=%llu; code bytes=%llu",
                   (unsigned long long)stats.blocks_entered, (unsigned long long)stats.blocks_translated,
                   (unsigned long long)stats.guest_insns_translated, (unsigned long long)stats.translate_refusals,
                   (unsigned long long)stats.cache_flushes, (unsigned long long)stats.code_bytes_used);
}

static void destroy_engine(void)
{
    report_stats();
    x86p_jit_engine_destroy(engine);
    engine = NULL;
}

static void fail(const char *operation, uint32_t address, X86pJitRunStatus status, const char *reason)
{
    report_stats();
    lf2_log_writef(LF2_LOG_ERROR, "jit", "%s at guest %08x: %s: %s", operation, address,
                   x86p_jit_run_status_name(status), reason && *reason ? reason : "no detail supplied");
    abort();
}

static void ensure_engine(void)
{
    if (engine) return;
    if (!x86p_jit_available()) {
        lf2_log_write(LF2_LOG_ERROR, "jit", "x86port has no JIT backend for this host architecture");
        abort();
    }
    memory.host = g_mem;
    memory.lo = 0;
    memory.size = UINT32_MAX;
    x86p_cpu_reset(&jit_cpu);
    jit_cpu.fs_base = TIB_BASE;
    char reason[256] = {0};
    engine = x86p_jit_engine_create(&memory, JIT_CODE_BYTES, JIT_CACHE_BLOCKS, reason, sizeof reason);
    if (!engine) {
        lf2_log_write(LF2_LOG_ERROR, "jit", reason[0] ? reason : "x86port JIT creation failed without a reason");
        abort();
    }
    if (atexit(destroy_engine) != 0) {
        lf2_log_write(LF2_LOG_ERROR, "jit", "could not register JIT shutdown");
        abort();
    }
}

static void dispatch_intercept(uint32_t address)
{
    view_from_jit();
    const uint32_t return_address = LD32(R(ESP));
    Lf2NativeOverride function = lf2_native_override_find(address, 0);
    if (function) {
        function();
    } else if (address >= 0xF1000000u && address < 0xF2000000u) {
        com_call(address);
    } else if (address >= IMPORT_SENTINEL) {
        host_import(address);
    } else {
        fail("unowned interception", address, kX86pRunIntercept, "no native or HLE owner accepted it");
    }
    cpu.eip = return_address;
    jit_from_view();
}

static X86pJitDispatchResult dispatch_inline(X86pCpu *state, void *user)
{
    const CallBoundary *boundary = user;
    const uint32_t address = state->eip;
    if (address == boundary->return_address || lf2_native_override_find(address, boundary->excluded_override)) {
        return kX86pDispatchUnwind;
    }
    dispatch_intercept(address);
    return kX86pDispatchContinue;
}

static void execute(CallRequest request)
{
    ensure_engine();
    jit_from_view();
    jit_cpu.eip = request.guest_address;
    const CallBoundary boundary = {
        .return_address = LD32(R(ESP)),
        .excluded_override = request.excluded_override,
    };
    char reason[256];

    for (unsigned slice = 0; slice < JIT_MAX_SLICES_PER_CALL; ++slice) {
        reason[0] = '\0';
        x86p_jit_engine_set_intercept(engine, intercept, (void *)&boundary);
        x86p_jit_engine_set_dispatch(engine, dispatch_inline, (void *)&boundary);
        x86p_jit_engine_set_boundary(engine, translation_boundary, (void *)&boundary);
        const X86pJitRunStatus status = x86p_jit_engine_run(engine, &jit_cpu, JIT_SLICE_STEPS, reason, sizeof reason);
        if (status == kX86pRunBudget) continue;
        if (status != kX86pRunIntercept) fail("JIT execution stopped", jit_cpu.eip, status, reason);
        if (jit_cpu.eip == boundary.return_address) {
            view_from_jit();
            return;
        }
        dispatch_intercept(jit_cpu.eip);
    }
    fail("JIT call budget exhausted", jit_cpu.eip, kX86pRunBudget,
         "guest function did not reach its captured return address");
}

void lf2_jit_call(uint32_t guest_address)
{
    execute((CallRequest){.guest_address = guest_address, .excluded_override = 0});
}

void lf2_jit_call_original(uint32_t guest_address)
{
    execute((CallRequest){.guest_address = guest_address, .excluded_override = guest_address});
}

void lf2_jit_invalidate(uint32_t guest_address, uint32_t length)
{
    if (!engine || length == 0) return;
    const uint32_t end = guest_address > UINT32_MAX - length ? UINT32_MAX : guest_address + length;
    x86p_jit_engine_invalidate(engine, guest_address, end);
}

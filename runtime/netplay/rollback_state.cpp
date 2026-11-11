#include "rollback_state.h"

#include "com.h"
#include "guest_heap.h"
#include "guest_journal.h"
#include "jit_executor.h"
#include "lf2_log.h"

#include <algorithm>
#include <cstdlib>

namespace lf2::netplay {
namespace {

void refresh_page(std::uint32_t address, std::uint32_t size, void *context)
{
    static_cast<StateHash *>(context)->refresh(address, size);
}

} // namespace

std::unique_ptr<RollbackState> RollbackState::start()
{
    if (!guest_journal_start([](std::uint32_t address) { return is_media(address) ? 0 : 1; })) {
        return nullptr;
    }
    return std::unique_ptr<RollbackState>(new RollbackState());
}

void RollbackState::refresh_open_pages()
{
    guest_journal_for_each_open_page(refresh_page, &hash_);
}

void RollbackState::save(int slot)
{
    refresh_open_pages();
    Slot &saved = slots_.at(static_cast<std::size_t>(slot));
    saved.mark = guest_journal_mark();
    lf2_jit_save_cpu(&saved.cpu);
    saved.random = *crt_random_guest();
    saved.clock = guest_clock_state();
    saved.mapping = guest_memory_mapping_state();
    saved.heap_next = guest_heap_next();
    saved.com_objects = com_object_count();
    saved.valid = true;
    std::uint64_t oldest = saved.mark;
    for (const Slot &each : slots_) {
        if (each.valid) {
            oldest = std::min(oldest, each.mark);
        }
    }
    guest_journal_release(oldest);
}

void RollbackState::load(int slot)
{
    const Slot &saved = slots_.at(static_cast<std::size_t>(slot));
    if (!saved.valid) {
        lf2_log_writef(LF2_LOG_ERROR, "rollback", "slot %d loaded before it was saved", slot);
        std::abort();
    }
    if (com_object_count() != saved.com_objects) {
        lf2_log_writef(LF2_LOG_ERROR, "rollback",
                       "a rolled-back frame created %d COM objects, whose host side cannot roll back yet",
                       com_object_count() - saved.com_objects);
        std::abort();
    }
    guest_journal_rewind(saved.mark, refresh_page, &hash_);
    guest_memory_mapping_restore(saved.mapping);
    guest_heap_restore(saved.heap_next);
    lf2_jit_load_cpu(&saved.cpu);
    *crt_random_guest() = saved.random;
    guest_clock_restore(saved.clock);
    for (Slot &each : slots_) {
        if (each.mark > saved.mark) {
            each.valid = false;
        }
    }
}

std::uint64_t RollbackState::hash()
{
    refresh_open_pages();
    return hash_.logic();
}

} // namespace lf2::netplay

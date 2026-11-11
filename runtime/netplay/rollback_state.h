#ifndef LF2_NETPLAY_ROLLBACK_STATE_H
#define LF2_NETPLAY_ROLLBACK_STATE_H

#include "crt_random.h"
#include "frame_state.h"
#include "guest_clock.h"
#include "guest_memory.h"
#include "main_loop.h"

#include "cpu.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace lf2::netplay {

// Snapshots of the session state in ring slots, taken and loaded between frames: guest logic
// memory through the write journal, and the host state the guest reads. Surfaces and sound
// buffers are left out: each frame draws them again from logic. The hash is kept current from
// the pages each frame wrote.
class RollbackState {
  public:
    static constexpr int kSlots = 9;

    // nullptr, logged, where the write journal cannot run.
    static std::unique_ptr<RollbackState> start();
    ~RollbackState();
    RollbackState(const RollbackState &) = delete;
    RollbackState &operator=(const RollbackState &) = delete;
    RollbackState(RollbackState &&) = delete;
    RollbackState &operator=(RollbackState &&) = delete;

    void save(int slot);
    void load(int slot);
    [[nodiscard]] std::uint64_t hash();

  private:
    RollbackState() : hash_(HashScope::Logic)
    {
    }

    struct Slot {
        bool valid = false;
        std::uint64_t mark = 0;
        X86pCpu cpu{};
        CrtRandom random{};
        GuestClockState clock{};
        GuestMappingState mapping{};
        MainLoopState loop{};
        std::uint32_t heap_next = 0;
        int com_objects = 0;
        // Host state the guest reads: its message queue and the native overrides' state.
        std::vector<std::uint8_t> sections;
    };

    void refresh_open_pages();

    std::array<Slot, kSlots> slots_{};
    StateHash hash_;
};

} // namespace lf2::netplay

#endif

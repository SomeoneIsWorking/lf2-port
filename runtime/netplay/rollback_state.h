#ifndef LF2_NETPLAY_ROLLBACK_STATE_H
#define LF2_NETPLAY_ROLLBACK_STATE_H

#include "crt_random.h"
#include "frame_state.h"
#include "guest_clock.h"
#include "guest_memory.h"
#include "main_loop.h"

#include "com.h"
#include "cpu.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace lf2::netplay {

// Snapshots of the session state in ring slots, taken and loaded between frames: guest logic
// memory through the write journal, and the host state the guest reads. Surfaces and sound
// buffers are left out: each frame draws them again from logic. The hash is kept current from
// the pages each frame wrote. The same state carries a host's game to a friend joining it.
class RollbackState {
  public:
    static constexpr int kSlots = 9;
    using PageHash = std::pair<std::uint32_t, std::uint64_t>;

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

    // The host state and the guest's mapped spans, for a friend's `adopt`; only the same build reads it.
    [[nodiscard]] std::vector<std::uint8_t> host_image();
    // Takes a host's image; false, with why, when this guest's spans are not the host's.
    bool adopt(std::span<const std::uint8_t> image, std::string &why);
    // Every logic page's address and hash.
    [[nodiscard]] std::vector<PageHash> page_hashes();
    // A logic page as a host sends it, and as a friend writes it.
    [[nodiscard]] static std::span<const std::uint8_t> page(std::uint32_t address);
    void write_page(std::uint32_t address, std::span<const std::uint8_t> bytes);

  private:
    RollbackState() : hash_(HashScope::Logic)
    {
    }

    // What a snapshot holds beside guest memory.
    struct HostState {
        X86pCpu cpu{};
        CrtRandom random{};
        GuestClockState clock{};
        MainLoopState loop{};
        std::uint32_t heap_next = 0;
        ComState com{};
        std::uint32_t vram_next = 0;
        std::uint32_t pcm_next = 0;
        // Host state the guest reads: its message queue and the native overrides' state.
        std::vector<std::uint8_t> sections;
    };

    struct Slot {
        bool valid = false;
        std::uint64_t mark = 0;
        GuestMappingState mapping{};
        HostState host;
    };

    static HostState capture();
    static void restore(const HostState &host);
    void refresh_open_pages();

    std::array<Slot, kSlots> slots_{};
    StateHash hash_;
};

} // namespace lf2::netplay

#endif

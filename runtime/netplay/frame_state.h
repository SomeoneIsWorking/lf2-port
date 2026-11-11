#ifndef LF2_NETPLAY_FRAME_STATE_H
#define LF2_NETPLAY_FRAME_STATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Digests of the session state at a frame boundary. `logic` covers every guest span the game's
 * logic reads plus the guest-visible host state; `media` covers surfaces and sound buffers. */
typedef struct {
    uint64_t logic;
    uint64_t media;
} Lf2FrameDigest;

Lf2FrameDigest lf2_frame_state_digest(void);
/* One log line per frame with both digests; the determinism check compares two runs' lines. */
void lf2_frame_state_log(uint32_t frame, long presented);

#ifdef __cplusplus
}

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lf2::netplay {

// The state's one hash: every 4 KiB guest page hashed with its address and summed by kind, then
// mixed with the host state. Fixed at 4 KiB so hosts with larger pages agree with 4 KiB ones.
inline constexpr std::uint32_t kStatePageSize = 4096;

// Surfaces and sound buffers: drawn and played from logic, never read by it.
bool is_media(std::uint32_t address);

enum class HashScope : std::uint8_t { Logic, LogicAndMedia };

class StateHash {
  public:
    // Hashes every page of every mapped span in scope.
    explicit StateHash(HashScope scope);

    // [address, address + size) may have changed since it was last hashed.
    void refresh(std::uint32_t address, std::uint32_t size);
    // Spans mapped since are hashed in first.
    [[nodiscard]] std::uint64_t logic();
    [[nodiscard]] std::uint64_t media();

  private:
    void track_spans();
    void add_page(std::uint32_t page);

    HashScope scope_;
    std::vector<std::uint64_t> pages_;
    std::vector<bool> member_;
    std::uint64_t logic_sum_ = 0;
    std::uint64_t media_sum_ = 0;
    std::size_t spans_seen_ = 0;
};

} // namespace lf2::netplay

#endif

#endif

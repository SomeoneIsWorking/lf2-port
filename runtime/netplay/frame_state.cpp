#include "frame_state.h"

#include "crt_random.h"
#include "guest_map.h"
#include "guest_memory.h"
#include "lf2_log.h"

#include <lucent/content.h>

#include <cstddef>
#include <cstring>
#include <span>
#include <vector>

namespace lf2::netplay {
namespace {

class DigestBuilder {
  public:
    void add(std::span<const std::byte> bytes)
    {
        const lucent::content::Sha256 digest = lucent::content::sha256(bytes);
        const auto *first = reinterpret_cast<const std::byte *>(digest.data());
        parts_.insert(parts_.end(), first, first + digest.size());
    }

    void add_word(std::uint32_t value)
    {
        std::byte bytes[sizeof value];
        std::memcpy(bytes, &value, sizeof value);
        parts_.insert(parts_.end(), bytes, bytes + sizeof value);
    }

    [[nodiscard]] std::uint64_t finish() const
    {
        const lucent::content::Sha256 digest = lucent::content::sha256(parts_);
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < sizeof value; i++) {
            value = (value << 8U) | digest[i];
        }
        return value;
    }

  private:
    std::vector<std::byte> parts_;
};

bool is_media(std::uint32_t address)
{
    return (address >= GUEST_VRAM_BASE && address < GUEST_VRAM_END) ||
           (address >= GUEST_PCM_BASE && address < GUEST_PCM_END);
}

struct Builders {
    DigestBuilder logic;
    DigestBuilder media;
};

void visit_span(std::uint32_t address, std::uint32_t size, const std::uint8_t *host, void *context)
{
    auto &builders = *static_cast<Builders *>(context);
    DigestBuilder &target = is_media(address) ? builders.media : builders.logic;
    target.add_word(address);
    target.add_word(size);
    target.add(std::span(reinterpret_cast<const std::byte *>(host), size));
}

} // namespace
} // namespace lf2::netplay

extern "C" Lf2FrameDigest lf2_frame_state_digest(void)
{
    lf2::netplay::Builders builders;
    guest_memory_for_each_span(lf2::netplay::visit_span, &builders);
    builders.logic.add_word(crt_random_guest()->holdrand);
    return Lf2FrameDigest{builders.logic.finish(), builders.media.finish()};
}

extern "C" void lf2_frame_state_log(uint32_t frame, long presented)
{
    const Lf2FrameDigest digest = lf2_frame_state_digest();
    lf2_log_writef(LF2_LOG_INFO, "frame-state", "frame %u logic=%016llx media=%016llx presented=%ld\n", frame,
                   static_cast<unsigned long long>(digest.logic), static_cast<unsigned long long>(digest.media),
                   presented);
}

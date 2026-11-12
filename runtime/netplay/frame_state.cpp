#include "frame_state.h"

#include "crt_random.h"
#include "guest_clock.h"
#include "guest_map.h"
#include "guest_memory.h"
#include "jit_executor.h"
#include "lf2_log.h"

#include "cpu.h"

#include <cstdlib>
#include <cstring>

namespace lf2::netplay {
namespace {

constexpr std::uint32_t kPageShift = 12;
constexpr std::size_t kPageCount = std::size_t{1} << (32 - kPageShift);
static_assert(kStatePageSize == 1U << kPageShift);

std::uint64_t mix(std::uint64_t value)
{
    value ^= value >> 33U;
    value *= 0xff51afd7ed558ccdULL;
    value ^= value >> 33U;
    value *= 0xc4ceb9fe1a85ec53ULL;
    value ^= value >> 33U;
    return value;
}

std::uint64_t page_hash(std::uint32_t address, const std::uint8_t *bytes)
{
    std::uint64_t value = 0x9e3779b97f4a7c15ULL ^ address;
    for (std::uint32_t offset = 0; offset < kStatePageSize; offset += sizeof(std::uint64_t)) {
        std::uint64_t word = 0;
        std::memcpy(&word, bytes + offset, sizeof word);
        value = (value ^ word) * 0x100000001b3ULL;
    }
    return mix(value);
}

class Mixer {
  public:
    void add(std::uint64_t part)
    {
        value_ = mix(value_ ^ part);
    }
    [[nodiscard]] std::uint64_t value() const
    {
        return value_;
    }

  private:
    std::uint64_t value_ = 0x2545f4914f6cdd1dULL;
};

// The guest-visible state that lives on the host. x87 register contents are left out: their host
// format differs between hosts, and no frame leaves values on the x87 stack.
std::uint64_t host_state_hash()
{
    Mixer mixer;
    X86pCpu cpu;
    lf2_jit_save_cpu(&cpu);
    for (const std::uint32_t reg : cpu.reg) {
        mixer.add(reg);
    }
    mixer.add(cpu.eip);
    mixer.add(cpu.df);
    mixer.add(cpu.mxcsr);
    mixer.add(cpu.tsc);
    mixer.add(cpu.fs_base);
    mixer.add(cpu.gs_base);
    mixer.add(cpu.x87.top);
    mixer.add(cpu.x87.control);
    mixer.add(cpu.x87.status);
    for (const std::uint8_t tag : cpu.x87.tag) {
        mixer.add(tag);
    }
    mixer.add(crt_random_guest()->holdrand);
    const GuestClockState clock = guest_clock_state();
    mixer.add(clock.frames);
    mixer.add(clock.offset_ns);
    return mixer.value();
}

struct SpanWalk {
    StateHash *hash;
    std::size_t count = 0;
};

} // namespace

bool is_media(std::uint32_t address)
{
    return (address >= GUEST_VRAM_BASE && address < GUEST_VRAM_END) ||
           (address >= GUEST_PCM_BASE && address < GUEST_PCM_END);
}

StateHash::StateHash(HashScope scope) : scope_(scope), pages_(kPageCount), member_(kPageCount)
{
    track_spans();
}

void StateHash::add_page(std::uint32_t page)
{
    const std::uint32_t address = page << kPageShift;
    if (member_[page] || (scope_ == HashScope::Logic && is_media(address))) {
        return;
    }
    member_[page] = true;
    pages_[page] = page_hash(address, g_mem + address);
    (is_media(address) ? media_sum_ : logic_sum_) += pages_[page];
}

void StateHash::track_spans()
{
    std::size_t count = 0;
    guest_memory_for_each_span([](std::uint32_t, std::uint32_t, const std::uint8_t *,
                                  void *context) { ++*static_cast<std::size_t *>(context); },
                               &count);
    if (count == spans_seen_) {
        return;
    }
    spans_seen_ = count;
    guest_memory_for_each_span(
        [](std::uint32_t address, std::uint32_t size, const std::uint8_t *, void *context) {
            auto &self = *static_cast<StateHash *>(context);
            const std::uint32_t last = static_cast<std::uint32_t>((std::uint64_t{address} + size - 1) >> kPageShift);
            for (std::uint64_t page = address >> kPageShift; page <= last; ++page) {
                self.add_page(static_cast<std::uint32_t>(page));
            }
        },
        this);
}

void StateHash::refresh(std::uint32_t address, std::uint32_t size)
{
    if (size == 0) {
        return;
    }
    const std::uint32_t last = static_cast<std::uint32_t>((std::uint64_t{address} + size - 1) >> kPageShift);
    for (std::uint64_t index = address >> kPageShift; index <= last; ++index) {
        const auto page = static_cast<std::uint32_t>(index);
        if (!member_[page]) {
            continue;
        }
        const std::uint32_t at = page << kPageShift;
        const std::uint64_t fresh = page_hash(at, g_mem + at);
        std::uint64_t &sum = is_media(at) ? media_sum_ : logic_sum_;
        sum += fresh - pages_[page];
        pages_[page] = fresh;
    }
}

std::uint64_t StateHash::logic()
{
    track_spans();
    Mixer logic;
    logic.add(logic_sum_);
    logic.add(host_state_hash());
    return logic.value();
}

std::vector<std::pair<std::uint32_t, std::uint64_t>> StateHash::logic_pages()
{
    track_spans();
    std::vector<std::pair<std::uint32_t, std::uint64_t>> pages;
    for (std::uint32_t page = 0; page < kPageCount; ++page) {
        if (member_[page] && !is_media(page << kPageShift)) {
            pages.emplace_back(page << kPageShift, pages_[page]);
        }
    }
    return pages;
}

std::uint64_t StateHash::media()
{
    if (scope_ != HashScope::LogicAndMedia) {
        lf2_log_writef(LF2_LOG_ERROR, "frame-state", "media hash asked of a logic-only state hash");
        std::abort();
    }
    track_spans();
    return mix(media_sum_);
}

} // namespace lf2::netplay

extern "C" Lf2FrameDigest lf2_frame_state_digest(void)
{
    lf2::netplay::StateHash hash{lf2::netplay::HashScope::LogicAndMedia};
    return Lf2FrameDigest{hash.logic(), hash.media()};
}

extern "C" void lf2_frame_state_log(uint32_t frame, long presented)
{
    const Lf2FrameDigest digest = lf2_frame_state_digest();
    lf2_log_writef(LF2_LOG_INFO, "frame-state", "frame %u logic=%016llx media=%016llx presented=%ld\n", frame,
                   static_cast<unsigned long long>(digest.logic), static_cast<unsigned long long>(digest.media),
                   presented);
}

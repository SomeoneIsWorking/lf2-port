#include "rollback_state.h"

#include "dsound.h"
#include "guest_heap.h"
#include "guest_journal.h"
#include "hostwin.h"
#include "jit_executor.h"
#include "lf2_log.h"
#include "session_inputs.h"
#include "surface_memory.h"

#include "guest.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <type_traits>

namespace lf2::netplay {
namespace {

constexpr std::uint32_t kImageMagic = 0x4c463253; // "LF2S"

void refresh_page(std::uint32_t address, std::uint32_t size, void *context)
{
    static_cast<StateHash *>(context)->refresh(address, size);
}

struct Section {
    std::size_t (*size)();
    void (*save)(void *);
    void (*load)(const void *);
};

constexpr std::array<Section, 3> kHostSections{{
    {hostwin_messages_size, hostwin_messages_save, hostwin_messages_load},
    {input_state_size, input_state_save, input_state_load},
    {coop_state_size, coop_state_save, coop_state_load},
}};

void save_sections(std::vector<std::uint8_t> &out)
{
    std::size_t total = 0;
    for (const Section &section : kHostSections) {
        total += section.size();
    }
    out.resize(total);
    std::size_t at = 0;
    for (const Section &section : kHostSections) {
        section.save(out.data() + at);
        at += section.size();
    }
}

void load_sections(const std::vector<std::uint8_t> &in)
{
    std::size_t at = 0;
    for (const Section &section : kHostSections) {
        section.load(in.data() + at);
        at += section.size();
    }
}

struct Span {
    std::uint32_t address;
    std::uint32_t size;
    bool operator==(const Span &) const = default;
};

std::vector<Span> mapped_spans()
{
    std::vector<Span> spans;
    guest_memory_for_each_span(
        [](std::uint32_t address, std::uint32_t size, const std::uint8_t *, void *context) {
            static_cast<std::vector<Span> *>(context)->push_back(Span{address, size});
        },
        &spans);
    return spans;
}

class ImageWriter {
  public:
    template <typename T> void put(const T &value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        const auto *bytes = reinterpret_cast<const std::uint8_t *>(&value);
        out_.insert(out_.end(), bytes, bytes + sizeof value);
    }
    void put_bytes(std::span<const std::uint8_t> bytes)
    {
        put(static_cast<std::uint32_t>(bytes.size()));
        out_.insert(out_.end(), bytes.begin(), bytes.end());
    }
    std::vector<std::uint8_t> take()
    {
        return std::move(out_);
    }

  private:
    std::vector<std::uint8_t> out_;
};

class ImageReader {
  public:
    explicit ImageReader(std::span<const std::uint8_t> in) : in_(in)
    {
    }
    template <typename T> bool get(T &value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        if (in_.size() - at_ < sizeof value) {
            return false;
        }
        std::memcpy(&value, in_.data() + at_, sizeof value);
        at_ += sizeof value;
        return true;
    }
    bool get_bytes(std::vector<std::uint8_t> &out)
    {
        std::uint32_t size = 0;
        if (!get(size) || in_.size() - at_ < size) {
            return false;
        }
        out.assign(in_.begin() + static_cast<std::ptrdiff_t>(at_),
                   in_.begin() + static_cast<std::ptrdiff_t>(at_ + size));
        at_ += size;
        return true;
    }
    [[nodiscard]] bool done() const noexcept
    {
        return at_ == in_.size();
    }

  private:
    std::span<const std::uint8_t> in_;
    std::size_t at_ = 0;
};

} // namespace

std::unique_ptr<RollbackState> RollbackState::start()
{
    if (!guest_journal_start([](std::uint32_t address) { return is_media(address) ? 0 : 1; })) {
        return nullptr;
    }
    return std::unique_ptr<RollbackState>(new RollbackState());
}

RollbackState::~RollbackState()
{
    guest_journal_stop();
}

void RollbackState::refresh_open_pages()
{
    guest_journal_for_each_open_page(refresh_page, &hash_);
}

RollbackState::HostState RollbackState::capture()
{
    HostState host;
    lf2_jit_save_cpu(&host.cpu);
    host.random = *crt_random_guest();
    host.clock = guest_clock_state();
    host.loop = main_loop_state();
    host.heap_next = guest_heap_next();
    host.com = com_state();
    host.vram_next = vram_next();
    host.pcm_next = pcm_next();
    save_sections(host.sections);
    return host;
}

void RollbackState::restore(const HostState &host)
{
    com_restore(host.com);
    vram_restore(host.vram_next);
    pcm_restore(host.pcm_next);
    guest_heap_restore(host.heap_next);
    lf2_jit_load_cpu(&host.cpu);
    *crt_random_guest() = host.random;
    guest_clock_restore(host.clock);
    main_loop_restore(host.loop);
    load_sections(host.sections);
}

void RollbackState::save(int slot)
{
    refresh_open_pages();
    Slot &saved = slots_.at(static_cast<std::size_t>(slot));
    saved.mark = guest_journal_mark();
    saved.mapping = guest_memory_mapping_state();
    saved.host = capture();
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
    guest_journal_rewind(saved.mark, refresh_page, &hash_);
    guest_memory_mapping_restore(saved.mapping);
    restore(saved.host);
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

std::vector<std::uint8_t> RollbackState::host_image()
{
    const HostState host = capture();
    ImageWriter out;
    out.put(kImageMagic);
    out.put(host.cpu);
    out.put(host.random);
    out.put(host.clock);
    out.put(host.loop);
    out.put(host.heap_next);
    out.put(host.com);
    out.put(host.vram_next);
    out.put(host.pcm_next);
    out.put(guest_clock_calendar());
    out.put_bytes(host.sections);
    const std::vector<Span> spans = mapped_spans();
    out.put(static_cast<std::uint32_t>(spans.size()));
    for (const Span &span : spans) {
        out.put(span);
    }
    return out.take();
}

bool RollbackState::adopt(std::span<const std::uint8_t> image, std::string &why)
{
    ImageReader in{image};
    std::uint32_t magic = 0;
    HostState host;
    GuestCalendar calendar{};
    std::uint32_t span_count = 0;
    if (!in.get(magic) || magic != kImageMagic || !in.get(host.cpu) || !in.get(host.random) || !in.get(host.clock) ||
        !in.get(host.loop) || !in.get(host.heap_next) || !in.get(host.com) || !in.get(host.vram_next) ||
        !in.get(host.pcm_next) || !in.get(calendar) || !in.get_bytes(host.sections) || !in.get(span_count)) {
        why = "the host's state does not read";
        return false;
    }
    std::vector<Span> spans(span_count);
    for (Span &span : spans) {
        if (!in.get(span)) {
            why = "the host's state does not read";
            return false;
        }
    }
    std::vector<std::uint8_t> own_sections;
    save_sections(own_sections);
    if (!in.done() || host.sections.size() != own_sections.size()) {
        why = "the host's state does not read";
        return false;
    }
    if (spans != mapped_spans()) {
        why = "the host's game has memory mapped where this one has not";
        return false;
    }
    restore(host);
    guest_clock_take_calendar(calendar);
    return true;
}

std::vector<RollbackState::PageHash> RollbackState::page_hashes()
{
    refresh_open_pages();
    return hash_.logic_pages();
}

std::span<const std::uint8_t> RollbackState::page(std::uint32_t address)
{
    return {g_mem + address, kStatePageSize};
}

void RollbackState::write_page(std::uint32_t address, std::span<const std::uint8_t> bytes)
{
    guest_journal_note_write(address, kStatePageSize);
    std::memcpy(g_mem + address, bytes.data(), kStatePageSize);
    hash_.refresh(address, kStatePageSize);
}

} // namespace lf2::netplay

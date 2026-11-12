#include "rollback_check.h"

#include "guest_journal.h"
#include "hostwin.h"
#include "lf2_log.h"
#include "main_loop.h"
#include "rollback_state.h"

#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

namespace lf2::netplay {
namespace {

void count_page(std::uint32_t, std::uint32_t, void *context)
{
    ++*static_cast<unsigned long long *>(context);
}

class RollbackCheck {
  public:
    explicit RollbackCheck(std::unique_ptr<RollbackState> state) : state_(std::move(state))
    {
        hostwin_set_input_mode(HOST_INPUT_AT_BOUNDARY);
    }

    bool frame(std::uint32_t frame)
    {
        hostwin_pump();
        state_->save(0);
        hostwin_set_presenting(0);
        const bool ran = main_loop_advance() != 0;
        hostwin_set_presenting(1);
        if (!ran) {
            return false;
        }
        const std::uint64_t first = state_->hash();
        const std::vector<RollbackState::PageHash> first_pages = state_->page_hashes();
        state_->load(0);
        if (main_loop_advance() == 0) {
            return false;
        }
        const std::uint64_t second = state_->hash();
        guest_journal_for_each_open_page(count_page, &pages_);
        ++frames_;
        if (first != second) {
            ++differing_;
            lf2_log_writef(LF2_LOG_WARNING, "rollback-check", "frame %u differs on rerun: %016llx then %016llx", frame,
                           static_cast<unsigned long long>(first), static_cast<unsigned long long>(second));
            log_differing_pages(first_pages, state_->page_hashes());
        }
        if (frames_ % 100 == 0) {
            lf2_log_writef(LF2_LOG_INFO, "rollback-check",
                           "%llu frames rerun, %llu differed, %llu pages written a frame",
                           static_cast<unsigned long long>(frames_), static_cast<unsigned long long>(differing_),
                           static_cast<unsigned long long>(pages_ / frames_));
        }
        return true;
    }

  private:
    static void log_differing_pages(const std::vector<RollbackState::PageHash> &first,
                                    const std::vector<RollbackState::PageHash> &second)
    {
        std::size_t a = 0;
        std::size_t b = 0;
        while (a < first.size() || b < second.size()) {
            if (b == second.size() || (a < first.size() && first[a].first < second[b].first)) {
                lf2_log_writef(LF2_LOG_WARNING, "rollback-check", "  page %08x only in the first run",
                               first[a++].first);
            } else if (a == first.size() || second[b].first < first[a].first) {
                lf2_log_writef(LF2_LOG_WARNING, "rollback-check", "  page %08x only in the rerun", second[b++].first);
            } else {
                if (first[a].second != second[b].second) {
                    lf2_log_writef(LF2_LOG_WARNING, "rollback-check", "  page %08x differs", first[a].first);
                }
                ++a;
                ++b;
            }
        }
    }

    std::unique_ptr<RollbackState> state_;
    unsigned long long frames_ = 0;
    unsigned long long differing_ = 0;
    unsigned long long pages_ = 0;
};

std::unique_ptr<RollbackCheck> check;

} // namespace
} // namespace lf2::netplay

extern "C" int lf2_rollback_check_frame(uint32_t frame)
{
    using lf2::netplay::check;
    if (!check) {
        auto state = lf2::netplay::RollbackState::start();
        if (!state) {
            lf2_log_writef(LF2_LOG_ERROR, "rollback-check", "no rollback state on this platform");
            std::abort();
        }
        check = std::make_unique<lf2::netplay::RollbackCheck>(std::move(state));
    }
    return check->frame(frame) ? 1 : 0;
}

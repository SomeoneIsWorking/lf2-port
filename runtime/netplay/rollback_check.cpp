#include "rollback_check.h"

#include "guest_journal.h"
#include "hostwin.h"
#include "lf2_log.h"
#include "main_loop.h"
#include "rollback_state.h"

#include <cstdlib>
#include <memory>
#include <utility>

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

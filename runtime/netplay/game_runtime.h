#ifndef LF2_NETPLAY_GAME_RUNTIME_H
#define LF2_NETPLAY_GAME_RUNTIME_H

#include "rollback_state.h"

#include <lucent/rollback/session.h>

#include <memory>

namespace lf2::netplay {

// LF2 as the rollback engine drives it: a logic frame is the main loop's turns up to one game
// frame, its devices are the session's slots, and a resimulated frame is not shown.
class GameRuntime final : public lucent::rollback::Runtime {
  public:
    explicit GameRuntime(std::unique_ptr<RollbackState> state);
    ~GameRuntime() override;

    void save(int slot) override;
    void load(int slot) override;
    void setInputs(std::span<const std::uint8_t> all) override;
    void advance(bool resimulating) override;
    [[nodiscard]] std::uint64_t hash() const override;

    // A quit came in the middle of a frame.
    [[nodiscard]] bool quit() const noexcept
    {
        return quit_;
    }

  private:
    std::unique_ptr<RollbackState> state_;
    bool quit_ = false;
};

} // namespace lf2::netplay

#endif

#include "game_runtime.h"

#include "dsound.h"
#include "hostwin.h"
#include "main_loop.h"
#include "session_inputs.h"

#include <utility>

namespace lf2::netplay {

GameRuntime::GameRuntime(std::unique_ptr<RollbackState> state) : state_(std::move(state))
{
    hostwin_set_input_mode(HOST_INPUT_WITHHELD);
}

GameRuntime::~GameRuntime()
{
    input_use_session(nullptr, 0);
    hostwin_set_input_mode(HOST_INPUT_LIVE);
}

void GameRuntime::save(int slot)
{
    state_->save(slot);
}

void GameRuntime::load(int slot)
{
    state_->load(slot);
}

void GameRuntime::setInputs(std::span<const std::uint8_t> all)
{
    input_use_session(all.data(), static_cast<int>(all.size()));
}

void GameRuntime::advance(bool resimulating)
{
    hostwin_set_presenting(resimulating ? 0 : 1);
    dsound_set_audible(resimulating ? 0 : 1);
    if (main_loop_advance() == 0) {
        quit_ = true;
    }
    dsound_set_audible(1);
    hostwin_set_presenting(1);
}

std::uint64_t GameRuntime::hash() const
{
    return state_->hash();
}

} // namespace lf2::netplay

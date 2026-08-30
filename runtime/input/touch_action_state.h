#pragma once

#include "touch_layout.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace lf2::touch {

struct ActionState {
    std::array<int, 8> references{};
};

inline constexpr std::array<std::uint32_t, 8> action_flags = {
    action_up, action_down, action_left, action_right, action_attack, action_jump, action_defend, action_pause,
};

template <typename Emit> inline void apply_actions(ActionState &state, std::uint32_t actions, bool down, Emit emit)
{
    for (int action = 0; action < static_cast<int>(action_flags.size()); ++action) {
        if ((actions & action_flags[static_cast<std::size_t>(action)]) == 0) continue;
        int &references = state.references[static_cast<std::size_t>(action)];
        const bool was_down = references > 0;
        references = down ? references + 1 : std::max(0, references - 1);
        const bool is_down = references > 0;
        if (was_down != is_down) emit(action, is_down);
    }
}

template <typename Emit> inline void cancel_actions(ActionState &state, Emit emit)
{
    for (int action = 0; action < static_cast<int>(state.references.size()); ++action) {
        if (state.references[static_cast<std::size_t>(action)] <= 0) continue;
        state.references[static_cast<std::size_t>(action)] = 0;
        emit(action, false);
    }
}

} // namespace lf2::touch

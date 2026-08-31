#pragma once

#include <cstdint>

namespace lf2::touch {

enum class PointerPhase : std::uint8_t { began, moved, ended, canceled };

struct PointerState {
    std::int64_t finger = 0;
    float x = 0.0F;
    float y = 0.0F;
    bool active = false;
};

template <typename Emit>
inline bool route_pointer(PointerState &state, std::int64_t finger, float x, float y, PointerPhase phase,
                          bool claimed_by_controls, Emit emit)
{
    if (phase == PointerPhase::began) {
        if (claimed_by_controls || state.active) return false;
        state.finger = finger;
        state.x = x;
        state.y = y;
        state.active = true;
        emit(x, y, 1);
        return true;
    }
    if (!state.active || state.finger != finger) return false;
    state.x = x;
    state.y = y;
    if (phase == PointerPhase::moved) {
        emit(x, y, -1);
    } else {
        emit(x, y, 0);
        state.active = false;
    }
    return true;
}

template <typename Emit> inline void cancel_pointer(PointerState &state, Emit emit)
{
    if (!state.active) return;
    emit(state.x, state.y, 0);
    state.active = false;
}

} // namespace lf2::touch

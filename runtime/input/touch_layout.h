#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace lf2::touch {

enum ActionFlag : std::uint32_t {
    action_up = 1U << 0,
    action_down = 1U << 1,
    action_left = 1U << 2,
    action_right = 1U << 3,
    action_attack = 1U << 4,
    action_jump = 1U << 5,
    action_defend = 1U << 6,
    action_pause = 1U << 7,
};

struct Rect {
    float left = 0.0F;
    float top = 0.0F;
    float right = 0.0F;
    float bottom = 0.0F;
};

struct Zone {
    std::uint32_t id = 0;
    Rect bounds;
    std::uint32_t actions = 0;
};

enum class VisualKind : std::uint8_t { up, down, left, right, attack, jump, defend, pause };

struct Visual {
    Rect bounds;
    VisualKind kind = VisualKind::up;
    std::uint32_t action = 0;
};

struct Layout {
    std::array<Zone, 12> zones{};
    std::array<Visual, 8> visuals{};
};

inline Rect cell(float left, float top, float size)
{
    return {left, top, left + size, top + size};
}

inline Layout make_layout(Rect safe)
{
    Layout layout;
    const float width = std::max(1.0F, safe.right - safe.left);
    const float height = std::max(1.0F, safe.bottom - safe.top);
    const float unit = std::clamp(std::min(width, height) * 0.125F, 52.0F, 132.0F);
    const float edge = unit * 0.28F;
    const float dpad_left = safe.left + edge;
    const float dpad_top = safe.bottom - edge - unit * 3.0F;

    constexpr std::array<std::uint32_t, 8> direction_actions = {
        action_up | action_left,   action_up,   action_up | action_right,  action_left, action_right,
        action_down | action_left, action_down, action_down | action_right};
    constexpr std::array<std::array<int, 2>, 8> direction_cells = {
        std::array<int, 2>{0, 0}, {1, 0}, {2, 0}, {0, 1}, {2, 1}, {0, 2}, {1, 2}, {2, 2}};
    for (std::size_t index = 0; index < direction_cells.size(); ++index) {
        const auto coordinate = direction_cells[index];
        layout.zones[index] = {static_cast<std::uint32_t>(index + 1),
                               cell(dpad_left + coordinate[0] * unit, dpad_top + coordinate[1] * unit, unit),
                               direction_actions[index]};
    }

    const float buttons_left = safe.right - edge - unit * 3.0F;
    const float buttons_top = safe.bottom - edge - unit * 2.55F;
    layout.zones[8] = {9, cell(buttons_left + unit * 2.0F, buttons_top + unit * 0.78F, unit), action_attack};
    layout.zones[9] = {10, cell(buttons_left + unit, buttons_top + unit * 1.55F, unit), action_jump};
    layout.zones[10] = {11, cell(buttons_left, buttons_top + unit * 0.78F, unit), action_defend};
    layout.zones[11] = {12, cell(safe.right - edge - unit, safe.top + edge, unit), action_pause};

    layout.visuals = {
        Visual{cell(dpad_left + unit, dpad_top, unit), VisualKind::up, action_up},
        Visual{cell(dpad_left + unit, dpad_top + unit * 2.0F, unit), VisualKind::down, action_down},
        Visual{cell(dpad_left, dpad_top + unit, unit), VisualKind::left, action_left},
        Visual{cell(dpad_left + unit * 2.0F, dpad_top + unit, unit), VisualKind::right, action_right},
        Visual{layout.zones[8].bounds, VisualKind::attack, action_attack},
        Visual{layout.zones[9].bounds, VisualKind::jump, action_jump},
        Visual{layout.zones[10].bounds, VisualKind::defend, action_defend},
        Visual{layout.zones[11].bounds, VisualKind::pause, action_pause},
    };
    return layout;
}

} // namespace lf2::touch

#include "touch_input.h"

#include "bindings.h"
#include "options.h"
#include "touch_action_state.h"
#include "touch_layout.h"

#include <lucent/touch.h>

#include <array>
#include <cstdint>
#include <vector>

namespace {

struct TouchState {
    lucent::touch::Router router;
    lf2::touch::Layout layout;
    lf2::touch::ActionState actions;
    int output_width = 0;
    int output_height = 0;
    SDL_Rect safe_area{};
    bool layout_ready = false;
    bool observed = false;
    bool enabled = false;
    TouchInputEmitKey emit_key = nullptr;
};

TouchState state;

std::uint32_t virtual_key_for_action(int action)
{
    return action == 7 ? 0x1BU : binding_key_vk(action);
}

void emit_transition(int action, bool down, TouchInputEmitKey emit_key)
{
    if (emit_key) emit_key(virtual_key_for_action(action), down ? 1 : 0);
}

void apply_actions(std::uint32_t actions, bool down, TouchInputEmitKey emit_key)
{
    lf2::touch::apply_actions(state.actions, actions, down,
                              [emit_key](int action, bool is_down) { emit_transition(action, is_down, emit_key); });
}

const lf2::touch::Zone *zone_by_id(std::uint32_t id)
{
    for (const auto &zone : state.layout.zones)
        if (zone.id == id) return &zone;
    return nullptr;
}

SDL_Rect safe_area_in_render_coordinates(SDL_Renderer *renderer, SDL_Window *window)
{
    int output_width = 0;
    int output_height = 0;
    SDL_GetCurrentRenderOutputSize(renderer, &output_width, &output_height);
    SDL_Rect safe{0, 0, output_width, output_height};
    SDL_Rect window_safe{};
    if (!window || !SDL_GetWindowSafeArea(window, &window_safe)) return safe;

    float left = 0.0F;
    float top = 0.0F;
    float right = static_cast<float>(output_width);
    float bottom = static_cast<float>(output_height);
    if (SDL_RenderCoordinatesFromWindow(renderer, static_cast<float>(window_safe.x), static_cast<float>(window_safe.y),
                                        &left, &top) &&
        SDL_RenderCoordinatesFromWindow(renderer, static_cast<float>(window_safe.x + window_safe.w),
                                        static_cast<float>(window_safe.y + window_safe.h), &right, &bottom)) {
        safe.x = static_cast<int>(left);
        safe.y = static_cast<int>(top);
        safe.w = static_cast<int>(right - left);
        safe.h = static_cast<int>(bottom - top);
    }
    return safe;
}

void cancel_all(TouchInputEmitKey emit_key);

void update_layout(SDL_Renderer *renderer, SDL_Window *window)
{
    if (!renderer) return;
    int width = 0;
    int height = 0;
    SDL_GetCurrentRenderOutputSize(renderer, &width, &height);
    const SDL_Rect safe = safe_area_in_render_coordinates(renderer, window);
    if (state.layout_ready && width == state.output_width && height == state.output_height &&
        SDL_memcmp(&safe, &state.safe_area, sizeof safe) == 0)
        return;

    if (state.layout_ready) cancel_all(state.emit_key);

    state.output_width = width;
    state.output_height = height;
    state.safe_area = safe;
    state.layout = lf2::touch::make_layout({static_cast<float>(safe.x), static_cast<float>(safe.y),
                                            static_cast<float>(safe.x + safe.w), static_cast<float>(safe.y + safe.h)});
    std::array<lucent::touch::Zone, 12> zones{};
    for (std::size_t index = 0; index < zones.size(); ++index) {
        const auto &source = state.layout.zones[index];
        zones[index] = {source.id, source.bounds.left, source.bounds.top, source.bounds.right, source.bounds.bottom, 0};
    }
    state.router.set_zones(zones);
    state.layout_ready = true;
}

void cancel_all(TouchInputEmitKey emit_key)
{
    for (const auto &event : state.router.cancel()) {
        const auto *zone = zone_by_id(event.zone_id);
        if (zone) apply_actions(zone->actions, false, emit_key);
    }
    lf2::touch::cancel_actions(state.actions,
                               [emit_key](int action, bool down) { emit_transition(action, down, emit_key); });
}

bool should_enable(bool controller_connected)
{
    if (!opt_touch_controls()) return false;
#if defined(__ANDROID__)
    return !controller_connected;
#else
    return state.observed && !controller_connected;
#endif
}

void sync_enabled(bool controller_connected, TouchInputEmitKey emit_key)
{
    const bool enabled = should_enable(controller_connected);
    if (state.enabled && !enabled) cancel_all(emit_key);
    state.enabled = enabled;
}

lucent::touch::Phase phase_for(Uint32 type)
{
    switch (type) {
    case SDL_EVENT_FINGER_DOWN: return lucent::touch::Phase::began;
    case SDL_EVENT_FINGER_UP: return lucent::touch::Phase::ended;
    case SDL_EVENT_FINGER_CANCELED: return lucent::touch::Phase::canceled;
    default: return lucent::touch::Phase::moved;
    }
}

} // namespace

extern "C" int touch_input_handle_event(const SDL_Event *event, SDL_Renderer *renderer, SDL_Window *window,
                                        int controller_connected, TouchInputEmitKey emit_key)
{
    if (!event) return 0;
    if (emit_key) state.emit_key = emit_key;
    const bool is_finger = event->type == SDL_EVENT_FINGER_DOWN || event->type == SDL_EVENT_FINGER_UP ||
                           event->type == SDL_EVENT_FINGER_MOTION || event->type == SDL_EVENT_FINGER_CANCELED;
    if (event->type == SDL_EVENT_FINGER_DOWN) state.observed = true;
    sync_enabled(controller_connected != 0, emit_key);
    if (event->type == SDL_EVENT_WILL_ENTER_BACKGROUND || event->type == SDL_EVENT_WINDOW_FOCUS_LOST)
        cancel_all(emit_key);
    if (!is_finger || !state.enabled) return is_finger ? 1 : 0;

    update_layout(renderer, window);
    SDL_Event converted = *event;
    if (!renderer || !SDL_ConvertEventToRenderCoordinates(renderer, &converted)) return 1;
    const lucent::touch::Contact contact = {static_cast<std::int64_t>(converted.tfinger.fingerID),
                                            {converted.tfinger.x, converted.tfinger.y},
                                            phase_for(converted.type)};
    const auto events = state.router.route(std::span<const lucent::touch::Contact>(&contact, 1));
    for (const auto &routed : events) {
        const auto *zone = zone_by_id(routed.zone_id);
        if (!zone) continue;
        if (routed.phase == lucent::touch::Phase::began) apply_actions(zone->actions, true, emit_key);
        else if (routed.phase == lucent::touch::Phase::ended || routed.phase == lucent::touch::Phase::canceled)
            apply_actions(zone->actions, false, emit_key);
    }
    return 1;
}

extern "C" void touch_input_cancel(TouchInputEmitKey emit_key)
{
    cancel_all(emit_key);
}

extern "C" int touch_input_visuals(SDL_Renderer *renderer, SDL_Window *window, int controller_connected,
                                   TouchVisual *output, int capacity)
{
    sync_enabled(controller_connected != 0, nullptr);
    if (!state.enabled || !output || capacity <= 0) return 0;
    update_layout(renderer, window);
    const int count = std::min(capacity, static_cast<int>(state.layout.visuals.size()));
    for (int index = 0; index < count; ++index) {
        const auto &visual = state.layout.visuals[static_cast<std::size_t>(index)];
        output[index].bounds = {visual.bounds.left, visual.bounds.top, visual.bounds.right - visual.bounds.left,
                                visual.bounds.bottom - visual.bounds.top};
        output[index].kind = static_cast<TouchVisualKind>(visual.kind);
        output[index].pressed = 0;
        for (std::size_t action = 0; action < lf2::touch::action_flags.size(); ++action)
            if ((visual.action & lf2::touch::action_flags[action]) != 0 && state.actions.references[action] > 0)
                output[index].pressed = 1;
    }
    return count;
}

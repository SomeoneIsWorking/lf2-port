#include "lf2_log.h"
#include "environment.h"
#include "rmlui_input.h"

extern "C" {
#include "bindings.h"
#include "gamepad.h"
#include "keyboard.h"
}

#include "RmlUi_Platform_SDL.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

constexpr double kRepeatInitialDelay = 0.32;
constexpr double kRepeatStartInterval = 0.12;
constexpr double kRepeatMinInterval = 0.045;
constexpr double kRepeatRampDuration = 1.0;

struct ActionState {
    bool previous = false;
    bool latched = false;
    bool latched_controller = false;
    double pressed_at = 0.0;
    double next_repeat_at = 0.0;
};

std::array<ActionState, B_N> actions;
bool block_until_release = true;

double now_seconds()
{
    return static_cast<double>(SDL_GetTicksNS()) / 1'000'000'000.0;
}

bool repeatable(int action)
{
    return action == B_UP || action == B_DOWN || action == B_LEFT || action == B_RIGHT;
}

double repeat_interval(double held_for)
{
    const double ramp = std::clamp(held_for / kRepeatRampDuration, 0.0, 1.0);
    return kRepeatStartInterval + (kRepeatMinInterval - kRepeatStartInterval) * ramp;
}

void latch_action(int action, bool controller)
{
    actions[action].latched = true;
    actions[action].latched_controller = controller;
}

int fallback_pad_action(SDL_GamepadButton button)
{
    /* Keep conventional UI aliases for unbound native buttons. A button already
     * assigned to a game action follows that assignment instead of producing two commands. */
    switch (button) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return B_UP;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return B_DOWN;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return B_LEFT;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return B_RIGHT;
    case SDL_GAMEPAD_BUTTON_SOUTH: return B_ATTACK;
    case SDL_GAMEPAD_BUTTON_EAST: return B_JUMP;
    default: return -1;
    }
}

void move_focus(Rml::Context &context, Rml::ElementDocument &document, bool forward)
{
    Rml::Element *from = context.GetFocusElement();
    if (!from) return;
    if (Rml::Element *next = document.FindNextTabElement(from, forward)) {
        context.ProcessMouseLeave();
        next->Focus(true);
        next->ScrollIntoView(Rml::ScrollAlignment::Nearest);
    }
}

void key_tap(Rml::Context &context, Rml::Input::KeyIdentifier key)
{
    context.ProcessMouseLeave();
    context.ProcessKeyDown(key, 0);
    context.ProcessKeyUp(key, 0);
}

void dispatch_action(int action, bool controller, Rml::Context &context, Rml::ElementDocument &document,
                     const RmlUiInputCallbacks &callbacks)
{
    if (lf2_environment_get(LF2_ENV_RMLUI_DEBUG))
        lf2_log_writef(LF2_LOG_INFO, "rmlui_input", "rmlui input: %s %s\n", controller ? "controller" : "keyboard",
                       binding_action_id(action));

    switch (action) {
    case B_UP: move_focus(context, document, false); break;
    case B_DOWN: move_focus(context, document, true); break;
    case B_LEFT: key_tap(context, Rml::Input::KI_LEFT); break;
    case B_RIGHT: key_tap(context, Rml::Input::KI_RIGHT); break;
    case B_ATTACK:
        context.ProcessMouseLeave();
        if (callbacks.activate) callbacks.activate(controller);
        break;
    case B_JUMP:
        if (callbacks.cancel) callbacks.cancel();
        break;
    default: break;
    }
}

struct DeviceState {
    std::array<bool, B_N> held{};
    std::array<bool, B_N> controller{};
};

DeviceState read_devices()
{
    DeviceState result;
    for (int action = 0; action < B_N; ++action) result.held[action] = keyboard_held(binding_key_vk(action)) != 0;

    unsigned char pad[B_N] = {};
    gamepad_all_player_buttons(pad);
    for (int action = 0; action < B_N; ++action) {
        if (!pad[action]) continue;
        result.held[action] = true;
        result.controller[action] = true;
    }
    return result;
}

} // namespace

void rmlui_input_reset()
{
    actions = {};
    block_until_release = true;
}

void rmlui_input_block_until_release()
{
    block_until_release = true;
}

bool rmlui_input_note_event(const SDL_Event &event)
{
    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
        const unsigned vk = keyboard_vk_from_scancode(event.key.scancode);
        bool mapped = false;
        for (int action = 0; action < B_N; ++action) {
            if (vk == 0 || binding_key_vk(action) != vk) continue;
            mapped = true;
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) latch_action(action, false);
        }
        return mapped;
    }

    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
        const auto button = static_cast<SDL_GamepadButton>(event.gbutton.button);
        bool mapped = false;
        for (int action = 0; action < B_N; ++action) {
            if (binding_pad_button(action) != button) continue;
            mapped = true;
            if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) latch_action(action, true);
        }
        if (!mapped) {
            const int fallback = fallback_pad_action(button);
            if (fallback < 0) return false;
            if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) latch_action(fallback, true);
        }
        return true;
    }

    return false;
}

bool rmlui_input_pointer_event(Rml::Context &context, SDL_Renderer &renderer, const SDL_Event &event)
{
    SDL_Event mapped = event;
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (!SDL_ConvertEventToRenderCoordinates(&renderer, &mapped)) {
            lf2_log_writef(LF2_LOG_INFO, "rmlui_input", "rmlui: pointer coordinate conversion failed: %s\n",
                           SDL_GetError());
            return true;
        }
        break;
    default: break;
    }

    const int modifiers = RmlSDL::GetKeyModifierState();
    switch (mapped.type) {
    case SDL_EVENT_MOUSE_MOTION:
        context.ProcessMouseMove(static_cast<int>(std::floor(mapped.motion.x)),
                                 static_cast<int>(std::floor(mapped.motion.y)), modifiers);
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        context.ProcessMouseMove(static_cast<int>(std::floor(mapped.button.x)),
                                 static_cast<int>(std::floor(mapped.button.y)), modifiers);
        context.ProcessMouseButtonDown(RmlSDL::ConvertMouseButton(mapped.button.button), modifiers);
        SDL_CaptureMouse(true);
        return true;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        context.ProcessMouseMove(static_cast<int>(std::floor(mapped.button.x)),
                                 static_cast<int>(std::floor(mapped.button.y)), modifiers);
        context.ProcessMouseButtonUp(RmlSDL::ConvertMouseButton(mapped.button.button), modifiers);
        SDL_CaptureMouse(false);
        return true;
    case SDL_EVENT_MOUSE_WHEEL: context.ProcessMouseWheel(-mapped.wheel.y, modifiers); return true;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE: context.ProcessMouseLeave(); return true;
    default: return false;
    }
}

void rmlui_input_update(Rml::Context &context, Rml::ElementDocument &document, const RmlUiInputCallbacks &callbacks)
{
    const DeviceState devices = read_devices();
    const bool any_held = std::any_of(devices.held.begin(), devices.held.end(), [](bool held) { return held; });
    if (block_until_release) {
        for (int action = 0; action < B_N; ++action) {
            actions[action].previous = devices.held[action];
            actions[action].latched = false;
        }
        if (!any_held) block_until_release = false;
        return;
    }

    const double now = now_seconds();
    for (int action = 0; action < B_N; ++action) {
        ActionState &state = actions[action];
        const bool pressed = state.latched || (devices.held[action] && !state.previous);
        bool fire = pressed;
        if (pressed) {
            state.pressed_at = now;
            state.next_repeat_at = repeatable(action) ? now + kRepeatInitialDelay : 0.0;
        } else if (devices.held[action] && repeatable(action) && now >= state.next_repeat_at) {
            fire = true;
            state.next_repeat_at = now + repeat_interval(now - state.pressed_at);
        }

        const bool controller = state.latched ? state.latched_controller : devices.controller[action];
        state.latched = false;
        state.previous = devices.held[action];
        if (fire) dispatch_action(action, controller, context, document, callbacks);
    }
}

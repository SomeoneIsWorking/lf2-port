#include "touch_action_state.h"
#include "touch_layout.h"
#include "touch_pointer_state.h"

#include <cassert>
#include <iostream>
#include <vector>

struct Transition {
    int action;
    bool down;
};

struct PointerTransition {
    float x;
    float y;
    int down;
};

int main()
{
    const auto layout = lf2::touch::make_layout({80.0F, 40.0F, 2320.0F, 1040.0F});
    for (const auto &zone : layout.zones) {
        assert(zone.id != 0);
        assert(zone.bounds.left >= 80.0F);
        assert(zone.bounds.top >= 40.0F);
        assert(zone.bounds.right <= 2320.0F);
        assert(zone.bounds.bottom <= 1040.0F);
        assert(zone.bounds.left < zone.bounds.right);
        assert(zone.bounds.top < zone.bounds.bottom);
    }
    assert(layout.zones[0].actions == (lf2::touch::action_up | lf2::touch::action_left));
    assert(layout.zones[2].actions == (lf2::touch::action_up | lf2::touch::action_right));
    assert(layout.zones[7].actions == (lf2::touch::action_down | lf2::touch::action_right));
    assert(layout.zones[8].actions == lf2::touch::action_attack);
    assert(layout.zones[11].actions == lf2::touch::action_pause);

    const auto narrow = lf2::touch::make_layout({0.0F, 0.0F, 640.0F, 360.0F});
    assert(narrow.zones[0].bounds.left >= 0.0F);
    assert(narrow.zones[11].bounds.right <= 640.0F);
    assert(narrow.zones[7].bounds.bottom <= 360.0F);

    lf2::touch::ActionState actions;
    std::vector<Transition> transitions;
    auto capture = [&transitions](int action, bool down) { transitions.push_back({action, down}); };
    lf2::touch::apply_actions(actions, lf2::touch::action_up | lf2::touch::action_left, true, capture);
    lf2::touch::apply_actions(actions, lf2::touch::action_up, true, capture);
    lf2::touch::apply_actions(actions, lf2::touch::action_up | lf2::touch::action_left, false, capture);
    assert(actions.references[0] == 1);
    assert(actions.references[2] == 0);
    assert(transitions.size() == 3);
    assert(transitions[0].action == 0 && transitions[0].down);
    assert(transitions[1].action == 2 && transitions[1].down);
    assert(transitions[2].action == 2 && !transitions[2].down);
    lf2::touch::cancel_actions(actions, capture);
    assert(actions.references[0] == 0);
    assert(transitions.size() == 4 && transitions[3].action == 0 && !transitions[3].down);

    lf2::touch::PointerState pointer;
    std::vector<PointerTransition> pointer_transitions;
    auto capture_pointer = [&pointer_transitions](float x, float y, int down) {
        pointer_transitions.push_back({x, y, down});
    };
    assert(
        lf2::touch::route_pointer(pointer, 41, 120.0F, 90.0F, lf2::touch::PointerPhase::began, false, capture_pointer));
    assert(
        lf2::touch::route_pointer(pointer, 41, 140.0F, 95.0F, lf2::touch::PointerPhase::moved, false, capture_pointer));
    assert(!lf2::touch::route_pointer(pointer, 42, 200.0F, 100.0F, lf2::touch::PointerPhase::began, false,
                                      capture_pointer));
    assert(
        lf2::touch::route_pointer(pointer, 41, 140.0F, 95.0F, lf2::touch::PointerPhase::ended, false, capture_pointer));
    assert(pointer_transitions.size() == 3);
    assert(pointer_transitions[0].down == 1 && pointer_transitions[0].x == 120.0F);
    assert(pointer_transitions[1].down == -1 && pointer_transitions[1].x == 140.0F);
    assert(pointer_transitions[2].down == 0);
    assert(!pointer.active);
    assert(
        !lf2::touch::route_pointer(pointer, 43, 50.0F, 50.0F, lf2::touch::PointerPhase::began, true, capture_pointer));
    assert(pointer_transitions.size() == 3);
    assert(
        lf2::touch::route_pointer(pointer, 44, 75.0F, 80.0F, lf2::touch::PointerPhase::began, false, capture_pointer));
    assert(
        lf2::touch::route_pointer(pointer, 44, 85.0F, 90.0F, lf2::touch::PointerPhase::moved, false, capture_pointer));
    lf2::touch::cancel_pointer(pointer, capture_pointer);
    assert(pointer_transitions.size() == 6);
    assert(pointer_transitions[5].down == 0);
    assert(pointer_transitions[5].x == 85.0F && pointer_transitions[5].y == 90.0F);
    assert(!pointer.active);
    std::cout << "touch layout: all checks passed\n";
}

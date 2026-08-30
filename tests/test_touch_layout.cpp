#include "touch_action_state.h"
#include "touch_layout.h"

#include <cassert>
#include <iostream>
#include <vector>

struct Transition {
    int action;
    bool down;
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
    std::cout << "touch layout: all checks passed\n";
}

/* Global port-menu lifecycle.
 *
 * Dusklight's menu command opens its RmlUi document stack directly. LF2 follows the same
 * ownership: this module tracks where the shell opened and exposes LF2 actions, while
 * runtime/ui owns every visible menu element and all navigation. A running match uses the
 * game's own pause pipeline while its normal draw/present path continues behind the document.
 * There is deliberately no second, hand-painted Escape menu here.
 */
#include "hostwin.h"
#include "cheats.h"
#include "function_keys.h"
#include "gamepad.h"
#include "guest_ops.h"
#include "keyboard.h"
#include "rmlui.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int opened_in_match;
static int opening_device = -1;
static unsigned leave_f4_pulses;
static int frozen;
static uint32_t saved_pause[3];

enum { VK_F4 = 0x73, GX_PAUSE_EFFECTIVE = 0x00450bfc, GX_PAUSE_NEXT = 0x0044fb60, GX_PAUSE_REQUEST = 0x0044fcb0 };

static int edge(int *was, int now)
{
    const int fired = now && !*was;
    *was = now;
    return fired;
}

static int menu_device(void)
{
    const int pad = gamepad_start_index();
    return pad >= 0 ? pad + 1 : 0;
}

static void open_menu(void)
{
    opened_in_match = panel_hud_up() != 0;
    opening_device = menu_device();
    if (opened_in_match) {
        saved_pause[0] = LD32(GX_PAUSE_EFFECTIVE);
        saved_pause[1] = LD32(GX_PAUSE_NEXT);
        saved_pause[2] = LD32(GX_PAUSE_REQUEST);
        /* fn_0041bc90 treats effective==1 as paused but still runs its draw/present tail.
         * Pinning its three-stage pause pipeline freezes the current match without reviving
         * the retained-frame path removed by issue #94. */
        ST32(GX_PAUSE_EFFECTIVE, 1);
        ST32(GX_PAUSE_NEXT, 1);
        ST32(GX_PAUSE_REQUEST, 1);
        frozen = 1;
    }
    rmlui_open();
}

int pause_menu_in_match(void)
{
    return opened_in_match;
}

int pause_menu_can_drop(void)
{
    const int slot = opening_device >= 0 ? device_player(opening_device) : -1;
    return opened_in_match && slot >= 0 && coop_owns(slot);
}

void pause_menu_close(void)
{
    rmlui_close();
    if (frozen) {
        ST32(GX_PAUSE_EFFECTIVE, saved_pause[0]);
        ST32(GX_PAUSE_NEXT, saved_pause[1]);
        ST32(GX_PAUSE_REQUEST, saved_pause[2]);
        frozen = 0;
    }
    opened_in_match = 0;
    opening_device = -1;
}

void pause_menu_drop_out(void)
{
    const int slot = opening_device >= 0 ? device_player(opening_device) : -1;
    if (!opened_in_match || slot < 0 || !coop_owns(slot)) return;
    pause_menu_close();
    coop_drop_out(slot);
}

void pause_menu_leave_match(void)
{
    if (!opened_in_match) return;
    pause_menu_close();
    if (function_key_request(VK_F4)) leave_f4_pulses++;
}

void pause_tick(void)
{
    static int was_start;
    cheats_tick();
    const int escape_edge = keyboard_take_escape();
    const int start = gamepad_start_held() != 0;
    const int start_edge = edge(&was_start, start);
    const int toggle = escape_edge | start_edge;

    if (getenv("LF2_RMLUI_DEBUG") && (escape_edge || start_edge))
        fprintf(stderr, "rmlui menu command: escape=%d start=%d active=%d\n", escape_edge, start_edge, rmlui_active());

    if (!toggle) return;
    if (rmlui_active()) pause_menu_close();
    else open_menu();
}

void pause_report(void)
{
    if (leave_f4_pulses)
        fprintf(stderr, "pause leave: %u F4 pulse(s); key %s; post-match overlay %s at shutdown\n", leave_f4_pulses,
                hostwin_injected_key(VK_F4) ? "DOWN" : "released", panel_overlay_up() ? "up" : "not up");
    cheats_report();
}

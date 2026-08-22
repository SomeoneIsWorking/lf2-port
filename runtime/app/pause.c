/* Global port-menu lifecycle.
 *
 * Dusklight's menu command opens its RmlUi document stack directly. LF2 follows the same
 * ownership: this module decides when the game must be frozen and exposes LF2 actions, while
 * runtime/ui owns every visible menu element and all navigation. There is deliberately no
 * second, hand-painted Escape menu here.
 */
#include "hostwin.h"
#include "gamepad.h"
#include "keyboard.h"
#include "rmlui.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int frozen;
static int opened_in_match;
static int opening_device = -1;
static int leave_f4_boundaries;
static unsigned leave_f4_pulses;

enum { VK_F4 = 0x73, LEAVE_F4_BOUNDARIES = 2 };

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
    rmlui_open();
    /* Only a running match must stop advancing. Front-end screens keep rendering behind the
     * modal document, exactly as Dusklight keeps its UI global without inventing a second game
     * state machine. Input is still withheld from the guest while the document is active. */
    frozen = rmlui_active() && opened_in_match;
}

int pause_active(void) { return frozen; }

int pause_menu_in_match(void) { return opened_in_match; }

int pause_menu_can_drop(void)
{
    const int slot = opening_device >= 0 ? device_player(opening_device) : -1;
    return opened_in_match && slot >= 0 && coop_owns(slot);
}

void pause_menu_close(void)
{
    rmlui_close();
    frozen = 0;
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
    /* RmlUi can activate this callback during rendering or while the Win32 pump handles a
     * physical event. Two update boundaries make both phases equivalent: keep F4 down
     * through the next guest update, then release it before the following one. Sending down
     * and up together could leave LF2's message-fed key array released before fn_0041bc90
     * takes the same branch as a physical F4 press. */
    hostwin_inject_key(VK_F4, 1);
    leave_f4_boundaries = LEAVE_F4_BOUNDARIES;
    leave_f4_pulses++;
}

void pause_tick(void)
{
    static int was_start;
    if (leave_f4_boundaries > 0 && --leave_f4_boundaries == 0) hostwin_inject_key(VK_F4, 0);
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
    if (!leave_f4_pulses) return;
    fprintf(stderr, "pause leave: %u F4 pulse(s); key %s; post-match overlay %s at shutdown\n", leave_f4_pulses,
            hostwin_injected_key(VK_F4) ? "DOWN" : "released", panel_overlay_up() ? "up" : "not up");
}

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
#include "render.h"
#include "rmlui.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int frozen;
static int opened_in_match;
static int opening_device = -1;

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
    const int device = opening_device >= 0 ? opening_device : 0;
    pause_menu_close();
    exit_to_menu_begin(device);
}

void pause_tick(void)
{
    static int was_start;
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

/* The old software and display-list painters remain as stable call boundaries while their
 * implementation is gone. RmlUi is the sole visual owner. */
void pause_draw(uint32_t pixels, int width, int height, int pitch)
{
    (void)pixels;
    (void)width;
    (void)height;
    (void)pitch;
}

int pause_draw_list(uint32_t dst_pixels, int width, int height)
{
    (void)width;
    (void)height;
    if (!frozen || !rmlui_active() || !dst_pixels) return 0;
    /* Rewind to the retained gameplay frame before RmlUi is composited. Without this, the
     * document is drawn over the empty list assembled while the game update is frozen. */
    return render_hold_begin();
}

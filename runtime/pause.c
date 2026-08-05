/* Pause menu.
 *
 * The game has no pause. This is the port's own, and it is built on the one thing the port
 * reliably controls: whether the game's per-frame update runs at all. fn_004246b0 is the
 * function the main loop calls to advance and draw everything, so declining to call the
 * original body freezes the world exactly where it was -- no state to save, nothing to
 * restore, and no risk of half-advancing a frame.
 *
 * The frame keeps being presented while that happens, because the present lives in the main
 * loop rather than in the update, so what stays on screen is the last drawn frame with this
 * menu painted over it. That is also why the menu is drawn into the PRIMARY surface after
 * the copy rather than composed with everything else: the composition is frozen.
 *
 * It takes all three devices, like every other menu in the port: arrows or the d-pad move,
 * Z or A or Enter activate, Escape or Start toggles, and the mouse hovers and clicks.
 */
#include "guest_ops.h"
#include "hostwin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int  game_glyph_draw(int ch, int x, int y, uint32_t ink,
                     uint32_t dpix, int dwid, int dhei, int dpitch);
int  hostwin_pointer(int *x, int *y);
int  hostwin_mouse_clicked(void);

/* Four entries at most, and every one of them does exactly what it says. "Restart" and
 * "back to character select" are still deliberately absent: driving the game back to those
 * screens is reverse engineering that has not been done, and a menu item that half-works is
 * worse than one that is not there.
 *
 * DROP OUT is CONDITIONAL. It only appears when the device that opened this menu is driving
 * a player slot THIS PORT put there -- coop_drop_out refuses any other, because a fighter
 * the game's own character selection placed is not the port's to delete. That is also why
 * the menu records which device opened it: the menu is one screen, but drop-out is per
 * player, and one that guessed would drop the wrong fighter out of the fight.
 *
 * LEAVE MATCH is named for what it VERIFIABLY does, which is not quite what was asked for.
 * It drives the game's own way out of a fight -- F4, then the pre-fight overlay's own Exit
 * item -- and the game lands on its own character-select screen with the roster cleared,
 * ready for another match. Reaching the FRONT-END menu from there is one more step that has
 * not been established (Escape at that screen does nothing, measured), so the item does not
 * claim it. See issue #22.
 *
 * The rows are built per pause rather than being a fixed table, so the geometry, the hit
 * test and the drawing all agree without any of them knowing which items exist. */
enum { IT_RESUME, IT_DROP, IT_EXIT, IT_QUIT, IT_KINDS };
static const char *const ITEM_TEXT[IT_KINDS] = {
    "RESUME", "DROP OUT", "LEAVE MATCH", "QUIT GAME"
};

static int paused, sel;
static int rows[IT_KINDS], row_n;   /* the item kinds on screen this pause, in order */
static int pause_dev = -1;          /* the device that opened it; drop-out belongs to it */
static int drop_slot = -1;          /* the player slot that device is driving, or -1 */

/* Which items this pause offers. Called once when the menu opens, so the list cannot change
 * under the player's fingers between the frame they aimed and the frame they pressed. */
static void build_rows(void)
{
    row_n = 0;
    rows[row_n++] = IT_RESUME;
    drop_slot = pause_dev >= 0 ? device_player(pause_dev) : -1;
    if (drop_slot >= 0 && coop_owns(drop_slot)) rows[row_n++] = IT_DROP;
    rows[row_n++] = IT_EXIT;
    rows[row_n++] = IT_QUIT;
}

/* Panel geometry, in the primary surface's own pixels. Centred on whatever the viewport is,
 * so it lands correctly in widescreen without knowing anything about it. */
enum { PANEL_W = 260, ROW_H = 26, GLYPH_W = 8, GLYPH_H = 16 };
enum { PANEL_TOP = 36, PANEL_PAD = 10 };   /* title band above the rows, margin below */

static int panel_h(void) { return PANEL_TOP + row_n * ROW_H + PANEL_PAD; }

int pause_active(void) { return paused; }

static void panel_origin(int w, int h, int *px, int *py)
{
    *px = (w - PANEL_W) / 2;
    *py = (h - panel_h()) / 2;
}

/* Row rectangles have to agree between the hit test and the draw, so both come from here. */
static void row_rect(int w, int h, int i, int *x0, int *y0, int *x1, int *y1)
{
    int px, py;
    panel_origin(w, h, &px, &py);
    *x0 = px + 16;
    *x1 = px + PANEL_W - 16;
    *y0 = py + PANEL_TOP + i * ROW_H;
    *y1 = *y0 + ROW_H - 4;
}

static int row_at(int w, int h, int x, int y)
{
    for (int i = 0; i < row_n; i++) {
        int x0, y0, x1, y1;
        row_rect(w, h, i, &x0, &y0, &x1, &y1);
        if (x >= x0 && x < x1 && y >= y0 && y < y1) return i;
    }
    return -1;
}

/* ---- input ----
 *
 * Read straight from the devices rather than from the game's player buttons: the gather that
 * fills those runs inside the update, and the update is exactly what is not running. */
static int edge(int *was, int now)
{
    const int fired = now && !*was;
    *was = now;
    return fired;
}

static void pad_state(int *up, int *down, int *confirm, int *start)
{
    *up = *down = *confirm = *start = 0;
    for (int i = 0; i < 4; i++) {
        unsigned char b[7];
        if (!gamepad_player_buttons(i, b)) continue;
        *up      |= b[0];
        *down    |= b[1];
        *confirm |= b[4] || b[5];          /* attack or jump */
    }
    *start = gamepad_start_held();
}

/* Devices are numbered as the input gather numbers them: 0 is the keyboard, 1..4 the pads.
 * A pause opened with Escape belongs to the keyboard; one opened with Start belongs to the
 * pad that is holding it. */
static int pausing_device(void)
{
    const int pad = gamepad_start_index();
    if (pad >= 0) return pad + 1;
    return 0;
}

static void activate(void)
{
    const int kind = sel >= 0 && sel < row_n ? rows[sel] : -1;
    switch (kind) {
    case IT_RESUME:
        paused = 0;
        break;
    case IT_DROP:
        /* Unpause FIRST. The drop-out itself is a write to the game's own state, but the
         * player is left looking at a match they are no longer in, and a pause menu still
         * up over it would offer to drop them out a second time. */
        paused = 0;
        if (drop_slot >= 0) coop_drop_out(drop_slot);
        break;
    case IT_EXIT:
        /* Unpause FIRST, and this one is not a nicety: pausing works by declining to call
         * the game's update, and the transition out of a match is something the GAME does.
         * Frozen, F4 would be delivered to a game that never runs another frame. */
        paused = 0;
        exit_to_menu_begin(pause_dev >= 0 ? pause_dev : 0);
        break;
    case IT_QUIT:
        hostwin_request_quit();
        break;
    default:
        break;
    }
}

void pause_tick(void)
{
    static int was_esc, was_start, was_up, was_down, was_confirm;
    int p_up, p_down, p_confirm, p_start;
    pad_state(&p_up, &p_down, &p_confirm, &p_start);

    const int toggle = edge(&was_esc,   hostwin_key_held(0x1B) != 0)   /* Escape */
                     | edge(&was_start, p_start != 0);

    /* Pausing is only offered while a match is on screen -- the menus already have their own
     * navigation, and a pause overlay on top of one would be two menus taking the same keys.
     * Unpausing is always allowed, or a pause that outlived its match would be a lock-up. */
    if (toggle) {
        if (paused) paused = 0;
        else if (panel_hud_up()) {
            paused = 1;
            sel = 0;                       /* RESUME is always the first row */
            pause_dev = pausing_device();
            build_rows();
        }
    }
    if (!paused) return;

    if (edge(&was_up,   hostwin_key_held(0x26) || p_up))   sel = (sel + row_n - 1) % row_n;
    if (edge(&was_down, hostwin_key_held(0x28) || p_down)) sel = (sel + 1) % row_n;

    int mx, my;
    if (hostwin_pointer(&mx, &my)) {
        static int last_x = -1, last_y = -1;
        const int moved = (mx != last_x || my != last_y);
        last_x = mx; last_y = my;
        const int row = row_at(hostwin_width(), hostwin_height(), mx, my);
        if (row >= 0) {
            if (moved) sel = row;
            if (hostwin_mouse_clicked()) { sel = row; activate(); return; }
        } else if (hostwin_mouse_clicked()) {
            /* A click outside the panel is not a stray activation of whatever happened to be
             * selected. Swallowed on purpose. */
        }
    }

    if (edge(&was_confirm, hostwin_key_held(0x5A) || hostwin_key_held(0x0D) || p_confirm))
        activate();
}

/* ---- drawing ---- */

static void fill(uint32_t pix, int pitch, int w, int h,
                 int x0, int y0, int x1, int y1, uint32_t colour)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    for (int y = y0; y < y1; y++) {
        uint32_t *row = (uint32_t *)(g_mem + pix + (size_t)y * (size_t)pitch);
        for (int x = x0; x < x1; x++) row[x] = colour;
    }
}

/* Darken rather than paint over: the frozen frame stays readable underneath, which is what
 * makes it obvious the game is paused rather than gone. */
static void dim(uint32_t pix, int pitch, int w, int h)
{
    for (int y = 0; y < h; y++) {
        uint32_t *row = (uint32_t *)(g_mem + pix + (size_t)y * (size_t)pitch);
        for (int x = 0; x < w; x++) {
            const uint32_t c = row[x];
            row[x] = ((c >> 1) & 0x007f7f7fu);
        }
    }
}

static void text(const char *s, int x, int y, uint32_t ink,
                 uint32_t pix, int w, int h, int pitch)
{
    for (int i = 0; s[i]; i++)
        game_glyph_draw(s[i], x + i * GLYPH_W, y, ink, pix, w, h, pitch);
}

/* The frozen frame, kept because the menu is drawn straight onto the primary and the game
 * is not redrawing it. Without a snapshot the dim compounds every frame and the picture
 * fades to black in about a second. */
static uint32_t *snap;
static int snap_w, snap_h;

void pause_draw(uint32_t pix, int w, int h, int pitch)
{
    if (!paused) { free(snap); snap = NULL; snap_w = snap_h = 0; return; }

    const size_t n = (size_t)w * (size_t)h;
    if (!snap || snap_w != w || snap_h != h) {
        free(snap);
        snap = malloc(n * sizeof *snap);
        snap_w = w; snap_h = h;
        if (!snap) return;
        for (int y = 0; y < h; y++)
            memcpy(snap + (size_t)y * (size_t)w,
                   g_mem + pix + (size_t)y * (size_t)pitch, (size_t)w * sizeof *snap);
    } else {
        for (int y = 0; y < h; y++)
            memcpy(g_mem + pix + (size_t)y * (size_t)pitch,
                   snap + (size_t)y * (size_t)w, (size_t)w * sizeof *snap);
    }

    dim(pix, pitch, w, h);

    int px, py;
    panel_origin(w, h, &px, &py);
    const int ph = panel_h();
    fill(pix, pitch, w, h, px, py, px + PANEL_W, py + ph, 0x00203050u);
    fill(pix, pitch, w, h, px, py, px + PANEL_W, py + 2, 0x006080b0u);
    fill(pix, pitch, w, h, px, py + ph - 2, px + PANEL_W, py + ph, 0x006080b0u);

    static const char TITLE[] = "PAUSED";
    text(TITLE, px + (PANEL_W - (int)(sizeof TITLE - 1) * GLYPH_W) / 2, py + 10,
         0x00ffffffu, pix, w, h, pitch);

    for (int i = 0; i < row_n; i++) {
        int x0, y0, x1, y1;
        row_rect(w, h, i, &x0, &y0, &x1, &y1);
        if (i == sel) fill(pix, pitch, w, h, x0, y0, x1, y1, 0x004870a0u);
        const char *label = ITEM_TEXT[rows[i]];
        const int len = (int)strlen(label);
        text(label, x0 + ((x1 - x0) - len * GLYPH_W) / 2, y0 + (ROW_H - 4 - GLYPH_H) / 2,
             i == sel ? 0x00ffffffu : 0x00b0c0d0u, pix, w, h, pitch);
    }
}

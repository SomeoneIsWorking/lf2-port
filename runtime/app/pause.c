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
#include "hd2d.h"
#include "render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int  game_glyph_draw(int ch, int x, int y, uint32_t ink,
                     uint32_t dpix, int dwid, int dhei, int dpitch);
int  game_glyph_tile(int ch, int x, int y, uint32_t ink, uint32_t dst_pixels);
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
enum { IT_RESUME, IT_DROP, IT_EXIT, IT_OPTIONS, IT_QUIT,
       IT_LIGHT_ANGLE, IT_LIGHT_HEIGHT, IT_BACK, IT_KINDS };
static const char *const ITEM_TEXT[IT_KINDS] = {
    "RESUME", "DROP OUT", "LEAVE MATCH", "OPTIONS", "QUIT GAME",
    "LIGHT ANGLE", "LIGHT HEIGHT", "BACK"
};

/* ---- OPTIONS, and why it is built out of the same rows as everything else ----
 *
 * The light's direction was a compiled-in constant and is now the player's (issue #37). It is
 * two numbers -- which way the light comes from, and how high it is -- and both are things
 * you want to see change while you look at the picture, which is exactly what a pause menu
 * over a frozen frame gives for free.
 *
 * RmlUi was raised for this, as Dusklight uses it for its game-facing UI. It is the right
 * answer to the problem Dusklight has and the wrong one here. That port has a whole UI to
 * build -- documents, components, a settings tree -- and RmlUi earns its place. This is two
 * numbers on a menu that already exists, already takes keyboard, pad and mouse, and is
 * already drawn with the game's own glyphs so it looks like the game. RmlUi is C++ with its
 * own build, font stack and render backend; adding it would make it the largest dependency in
 * a port whose whole build is a C compiler and SDL. If the port ever grows a real settings
 * screen that judgement should be revisited, and Dusklight's src/dusk/ui is where to start.
 */
enum { PAGE_MAIN, PAGE_OPTIONS };

static int paused, sel, page;
static int rows[IT_KINDS], row_n;   /* the item kinds on screen this pause, in order */
static int pause_dev = -1;          /* the device that opened it; drop-out belongs to it */
static int drop_slot = -1;          /* the player slot that device is driving, or -1 */

/* Which items this pause offers. Called once when the menu opens, so the list cannot change
 * under the player's fingers between the frame they aimed and the frame they pressed. */
static void build_rows(void)
{
    row_n = 0;
    if (page == PAGE_OPTIONS) {
        rows[row_n++] = IT_LIGHT_ANGLE;
        rows[row_n++] = IT_LIGHT_HEIGHT;
        rows[row_n++] = IT_BACK;
        return;
    }
    rows[row_n++] = IT_RESUME;
    drop_slot = pause_dev >= 0 ? device_player(pause_dev) : -1;
    if (drop_slot >= 0 && coop_owns(drop_slot)) rows[row_n++] = IT_DROP;
    rows[row_n++] = IT_EXIT;
    rows[row_n++] = IT_OPTIONS;
    rows[row_n++] = IT_QUIT;
}

/* A row's value, or NULL when it is a plain item. Written into the caller's buffer so the
 * draw and nothing else owns the formatting. */
static const char *row_value(int kind, char *buf, size_t n)
{
    float az, el;
    hd2d_light_angles(&az, &el);
    if (kind == IT_LIGHT_ANGLE)  { snprintf(buf, n, "%+d", (int)(az + (az < 0 ? -0.5f : 0.5f))); return buf; }
    if (kind == IT_LIGHT_HEIGHT) { snprintf(buf, n, "%d", (int)(el + 0.5f)); return buf; }
    return NULL;
}

/* Left and right on a value row. The step is 5 degrees: fine enough to place a shadow where
 * you want it, coarse enough to cross the whole range without holding the key for a minute. */
static void adjust(int delta)
{
    const int kind = sel >= 0 && sel < row_n ? rows[sel] : -1;
    float az, el;
    hd2d_light_angles(&az, &el);
    if (kind == IT_LIGHT_ANGLE)       hd2d_light_set_angles(az + 5.0f * (float)delta, el);
    else if (kind == IT_LIGHT_HEIGHT) hd2d_light_set_angles(az, el + 5.0f * (float)delta);
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

static void pad_state(int *up, int *down, int *left, int *right, int *confirm, int *start)
{
    *up = *down = *left = *right = *confirm = *start = 0;
    for (int i = 0; i < 4; i++) {
        unsigned char b[7];
        if (!gamepad_player_buttons(i, b)) continue;
        *up      |= b[0];
        *down    |= b[1];
        *left    |= b[2];
        *right   |= b[3];
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
    case IT_OPTIONS:
        page = PAGE_OPTIONS;
        sel = 0;
        build_rows();
        break;
    case IT_BACK:
        page = PAGE_MAIN;
        sel = 0;
        build_rows();
        break;
    case IT_LIGHT_ANGLE:
    case IT_LIGHT_HEIGHT:
        /* Confirm on a value row nudges it, so the menu is usable with a device that has no
         * left and right of its own rather than doing nothing and looking broken. */
        adjust(+1);
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
    static int was_esc, was_start, was_up, was_down, was_left, was_right, was_confirm;
    int p_up, p_down, p_left, p_right, p_confirm, p_start;
    pad_state(&p_up, &p_down, &p_left, &p_right, &p_confirm, &p_start);

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
            page = PAGE_MAIN;              /* never reopen inside a submenu */
            pause_dev = pausing_device();
            build_rows();
        }
    }
    if (!paused) return;

    if (edge(&was_up,   hostwin_key_held(0x26) || p_up))   sel = (sel + row_n - 1) % row_n;
    if (edge(&was_down, hostwin_key_held(0x28) || p_down)) sel = (sel + 1) % row_n;
    if (edge(&was_left,  hostwin_key_held(0x25) || p_left))  adjust(-1);
    if (edge(&was_right, hostwin_key_held(0x27) || p_right)) adjust(+1);

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

/* ---- one layout, two painters ----
 *
 * The menu is drawn twice on a GPU frame: as PIXELS on the primary, which is what the
 * software compositor presents, and as DISPLAY-LIST entries over the retained frame, which is
 * what the native renderer presents (issue #52). Those are two different destinations, not
 * two different menus, so the layout below is written once and the painter is a parameter.
 * Duplicating the layout is how the two would quietly drift apart -- a row highlighted in one
 * renderer and not the other, and no test that compares a paused frame to notice.
 */
typedef struct {
    uint32_t list_dst;      /* non-zero: paint into this surface's display list */
    uint32_t pix;           /* zero list_dst: paint pixels into this surface */
    int w, h, pitch;
} Paint;

static void p_fill(const Paint *p, int x0, int y0, int x1, int y1, uint32_t colour)
{
    if (p->list_dst) render_fill(p->list_dst, x0, y0, x1, y1, colour);
    else             fill(p->pix, p->pitch, p->w, p->h, x0, y0, x1, y1, colour);
}

static void p_text(const Paint *p, const char *s, int x, int y, uint32_t ink)
{
    for (int i = 0; s[i]; i++) {
        const int gx = x + i * GLYPH_W;
        if (p->list_dst) game_glyph_tile(s[i], gx, y, ink, p->list_dst);
        else             game_glyph_draw(s[i], gx, y, ink, p->pix, p->w, p->h, p->pitch);
    }
}

/* The dim over the whole frame. On the list it is ONE 2x2 premultiplied tile stretched over
 * the composition rather than a full-window buffer: the renderer composites a tile as
 * `src + dst*(1-a)`, so a tile of pure zero at alpha 128 IS a halving of whatever is behind
 * it, and it costs four texels instead of eight megabytes a frame. */
static void p_dim(const Paint *p)
{
    if (!p->list_dst) { dim(p->pix, p->pitch, p->w, p->h); return; }
    uint32_t *t = render_tile_begin(p->list_dst, 0, 0, p->w, p->h, 2, 2);
    if (!t) return;
    for (int i = 0; i < 4; i++) t[i] = 0x80000000u;
    render_tile_end();
}

/* The frozen frame, kept because the menu is drawn straight onto the primary and the game
 * is not redrawing it. Without a snapshot the dim compounds every frame and the picture
 * fades to black in about a second. */
static uint32_t *snap;
static int snap_w, snap_h;

/* The layout. Everything below is in the destination's own pixels, and the destination is
 * whichever the painter names. */
static void pause_paint(const Paint *p, int w, int h)
{
    p_dim(p);

    int px, py;
    panel_origin(w, h, &px, &py);
    const int ph = panel_h();
    p_fill(p, px, py, px + PANEL_W, py + ph, 0x00203050u);
    p_fill(p, px, py, px + PANEL_W, py + 2, 0x006080b0u);
    p_fill(p, px, py + ph - 2, px + PANEL_W, py + ph, 0x006080b0u);

    const char *title = page == PAGE_OPTIONS ? "OPTIONS" : "PAUSED";
    p_text(p, title, px + (PANEL_W - (int)strlen(title) * GLYPH_W) / 2, py + 10, 0x00ffffffu);

    for (int i = 0; i < row_n; i++) {
        int x0, y0, x1, y1;
        row_rect(w, h, i, &x0, &y0, &x1, &y1);
        if (i == sel) p_fill(p, x0, y0, x1, y1, 0x004870a0u);
        const char *label = ITEM_TEXT[rows[i]];
        const int len = (int)strlen(label);
        const uint32_t ink = i == sel ? 0x00ffffffu : 0x00b0c0d0u;
        const int ty = y0 + (ROW_H - 4 - GLYPH_H) / 2;

        char vbuf[16];
        const char *value = row_value(rows[i], vbuf, sizeof vbuf);
        if (!value) {
            p_text(p, label, x0 + ((x1 - x0) - len * GLYPH_W) / 2, ty, ink);
            continue;
        }
        /* A value row reads left-to-right: the name against the left edge, the number against
         * the right, and arrows on the selected one so it is obvious it can be changed rather
         * than only chosen. */
        p_text(p, label, x0 + 4, ty, ink);
        const int vlen = (int)strlen(value);
        p_text(p, value, x1 - 12 - vlen * GLYPH_W, ty, ink);
        if (i == sel) {
            p_text(p, "<", x1 - 20 - (vlen + 1) * GLYPH_W, ty, ink);
            p_text(p, ">", x1 - 10, ty, ink);
        }
    }
}

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

    const Paint p = { 0, pix, w, h, pitch };
    pause_paint(&p, w, h);
}

/* The same menu over the RETAINED frame, for the native renderer. The restore-from-snapshot
 * above has no counterpart here and needs none: render_hold_begin rewinds the list to the
 * frame the game last built, so the dim is applied to a clean picture every time rather than
 * compounding. That is the same defect the snapshot exists to prevent, solved by the
 * renderer's own bookkeeping instead of by a copy of the screen.
 *
 * Returns 1 if it drew, 0 if there was no retained frame to draw over -- in which case the
 * caller must keep the software present, or the menu would be invisible while still
 * swallowing input. */
int pause_draw_list(uint32_t dst_pixels, int w, int h)
{
    if (!paused || !dst_pixels) return 0;
    if (!render_hold_begin()) return 0;
    const Paint p = { dst_pixels, 0, w, h, 0 };
    pause_paint(&p, w, h);
    return 1;
}

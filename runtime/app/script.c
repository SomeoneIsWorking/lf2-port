/* Scripted input: the shared timing model and the exit report. See script.h.
 *
 * This used to live in gamepad.c, which meant the keyboard and mouse scripts had neither
 * half: they could not be keyed to a screen at all (they parsed a bare strtol), so four of
 * the nine route tests were still stopwatches aimed at a moving target, and nothing told
 * anyone when one of their presses missed. Issue #25.
 */
#include "lf2_log.h"
#include "environment.h"
#include "script.h"
#include "environment.h"
#include "hostwin.h"

#include <SDL3/SDL.h>
#include <guest.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the screens a route can be keyed to ----
 *
 * Taken from what the game DRAWS, not from a .data flag: the one candidate for a flag
 * (0x0044d070) is the game mode wearing a screen's disguise and reads the same in VS mode
 * whether the overlay is up or not. See runtime/overrides/menu.c.
 *
 * `modemenu` is the VS / Stage / Championship list and the first screen direct startup presents.
 * `charselect` is the following player-selection panel. They share a blit destination and can
 * share a picture, so both anchors come from what the game distinctly draws, not a frame dump or
 * a sampled state word. */
enum { SCREEN_N = 4 };
static long screen_first[SCREEN_N] = {-1, -1, -1, -1};
static const char *const SCREEN_NAME[SCREEN_N] = {"modemenu", "charselect", "overlay", "match"};

void script_observe_screens(long frame)
{
    const int up[SCREEN_N] = {panel_modemenu_up(), panel_charselect_up(), panel_overlay_up(), panel_hud_up()};
    for (int i = 0; i < SCREEN_N; i++)
        if (up[i] && screen_first[i] < 0) {
            screen_first[i] = frame;
            lf2_log_writef(LF2_LOG_INFO, "script", "scripted input: screen %s first up at frame %ld\n", SCREEN_NAME[i],
                           frame);
        }
}

long script_when(const char *spec, int *unresolved)
{
    *unresolved = 0;
    if (*spec != '@') return strtol(spec, NULL, 10);
    spec++;
    for (int i = 0; i < SCREEN_N; i++) {
        const size_t n = strlen(SCREEN_NAME[i]);
        if (strncmp(spec, SCREEN_NAME[i], n) != 0) continue;
        const char *rest = spec + n;
        const long off = (*rest == '+') ? strtol(rest + 1, NULL, 10) : 0;
        if (screen_first[i] < 0) {
            *unresolved = 1;
            return -1;
        }
        return screen_first[i] + off;
    }
    /* A screen name this build does not know can never resolve, and saying "not yet" about
     * it would hide it forever behind the same silence a missed screen hides behind. */
    *unresolved = 1;
    return -1;
}

/* ---- per-item accounting ----
 *
 * Fired-ness is a property of the ITEM, set when the input goes down. It used to be one
 * sticky "something could not be resolved this frame" flag, which was true on frame 0 of
 * every screen-keyed route and never cleared -- so the report warned on every run including
 * clean ones, and a warning that is always on is one nobody reads (issue #24). */
enum { MAX_ITEMS = 128 };
enum { ITEM_NEVER = 0, ITEM_FIRED = 1, ITEM_BAD = 2 };
static unsigned char item_state[SCRIPT_STREAMS][MAX_ITEMS];
static int item_count[SCRIPT_STREAMS];
static int item_overflow[SCRIPT_STREAMS];

static const struct {
    Lf2EnvironmentKey env;
    char sep;
} STREAM[SCRIPT_STREAMS] = {
    {LF2_ENV_VIRTUAL_PAD, ','},  {LF2_ENV_VIRTUAL_PAD2, ','}, {LF2_ENV_VIRTUAL_PAD3, ','},
    {LF2_ENV_VIRTUAL_PAD4, ','}, {LF2_ENV_KEY_SCRIPT, ','},   {LF2_ENV_CLICK_SCRIPT, ';'},
};

static int in_range(int stream, int idx)
{
    if (stream < 0 || stream >= SCRIPT_STREAMS) return 0;
    if (idx < 0 || idx >= MAX_ITEMS) {
        item_overflow[stream] = 1;
        return 0;
    }
    return 1;
}

void script_seen(int stream, int idx)
{
    if (!in_range(stream, idx)) return;
    if (idx >= item_count[stream]) item_count[stream] = idx + 1;
}

void script_fired(int stream, int idx)
{
    if (!in_range(stream, idx)) return;
    if (idx >= item_count[stream]) item_count[stream] = idx + 1;
    item_state[stream][idx] = ITEM_FIRED;
}

void script_bad_item(int stream, int idx)
{
    if (!in_range(stream, idx)) return;
    if (idx >= item_count[stream]) item_count[stream] = idx + 1;
    item_state[stream][idx] = ITEM_BAD;
}

void script_report(void)
{
    int configured = 0;
    for (int s = 0; s < SCRIPT_STREAMS; s++)
        if (lf2_environment_enabled(STREAM[s].env)) configured = 1;
    if (!configured) return;
    lf2_log_writef(LF2_LOG_INFO, "script", "scripted input: screens reached --");
    int any = 0;
    for (int i = 0; i < SCREEN_N; i++) {
        if (screen_first[i] < 0) continue;
        lf2_log_writef(LF2_LOG_INFO, "script", " %s@%ld", SCREEN_NAME[i], screen_first[i]);
        any = 1;
    }
    if (!any) lf2_log_writef(LF2_LOG_INFO, "script", " NONE");
    lf2_log_writef(LF2_LOG_INFO, "script", "\n");

    /* One line per configured script, ALWAYS, with its denominator. "15 of 15 items fired"
     * is the negative this report has to be able to print: without it, silence is
     * indistinguishable from a report that was never reached. */
    for (int s = 0; s < SCRIPT_STREAMS; s++) {
        const char *script = lf2_environment_get(STREAM[s].env);
        if (!script) continue;

        int fired = 0;
        for (int i = 0; i < item_count[s]; i++)
            if (item_state[s][i] == ITEM_FIRED) fired++;
        lf2_log_writef(LF2_LOG_INFO, "script", "%s: %d of %d items fired\n", lf2_environment_name(STREAM[s].env), fired,
                       item_count[s]);
        if (item_overflow[s])
            lf2_log_writef(LF2_LOG_INFO, "script",
                           "%s: route longer than %d items -- the ones past that were "
                           "NEVER PLAYED and are not counted above\n",
                           lf2_environment_name(STREAM[s].env), MAX_ITEMS);
        if (fired == item_count[s] && !item_overflow[s]) continue;

        /* Name them. "Something did not fire" makes the next person bisect the route to
         * find out which; the text is right here. */
        int idx = 0;
        for (const char *c = script; *c;) {
            const char *item = c;
            while (*c && *c != STREAM[s].sep && *c != ' ') c++;
            const int n = (int)(c - item);
            while (*c == STREAM[s].sep || *c == ' ') c++;
            const int i = idx++;
            if (i >= item_count[s]) break;
            if (item_state[s][i] == ITEM_FIRED) continue;
            lf2_log_writef(
                LF2_LOG_INFO, "script", "%s: item %d `%.*s' %s\n", lf2_environment_name(STREAM[s].env), i, n, item,
                item_state[s][i] == ITEM_BAD ? "NEVER FIRED -- this build could not parse that"
                                             : "NEVER FIRED -- its screen never appeared, so any assertion about "
                                               "what it should have done is about an input that did not happen");
        }
    }
}

/* ---- live keyboard/mouse script state ----
 *
 * One parsed copy of each environment string, refreshed never: the environment is fixed for
 * a process's lifetime. Only @screen items stay dynamic, resolved per poll through
 * script_when against the screens seen so far. */

/* Scripted input is held SCRIPT_HOLD_FRAMES presented frames. */
enum { KEY_SCRIPT_HOLD = SCRIPT_HOLD_FRAMES };

/* One key-script item: the key and its when-spec kept verbatim, because an @screen frame
 * is known only once the screen appears. */
struct ScriptItem {
    uint32_t key;
    char when[64];
};

enum { PARSED_ITEMS = 128 };

static int key_script_parse(const char *script, struct ScriptItem *items, int cap)
{
    int n = 0;
    for (const char *c = script; *c;) {
        const uint32_t key = (uint32_t)strtoul(c, (char **)&c, 16);
        const char *when = c;
        if (*c == ':') {
            c++;
            when = c;
        }
        size_t len = 0;
        while (*c && *c != ',' && *c != ' ') {
            if (len < sizeof items->when - 1) len++;
            c++;
        }
        if (n < cap) {
            items[n].key = key;
            memcpy(items[n].when, when, len);
            items[n].when[len] = 0;
            n++;
        }
        while (*c == ',' || *c == ' ') c++;
    }
    return n;
}

int input_script_key_configured(void)
{
    static int configured = -1;
    if (configured < 0) {
        configured = lf2_environment_get(LF2_ENV_KEY_SCRIPT) != NULL || lf2_environment_get(LF2_ENV_AUTOKEY) != NULL;
    }
    return configured;
}

static struct ScriptItem key_items[PARSED_ITEMS];
static int nkey_items;

/* The autokey schedule and its point list, resolved once rather than on every pump. */
static struct {
    const char *script;
    uint32_t keys[PARSED_ITEMS];
    unsigned count;
    uint64_t begin, every, hold;
    int after_first_poll;
    int once;
} autokey;

static void parse_key_scripts_once(void)
{
    static int done;
    if (done) return;
    done = 1;

    const char *script = lf2_environment_get(LF2_ENV_KEY_SCRIPT);
    nkey_items = script ? key_script_parse(script, key_items, PARSED_ITEMS) : 0;
    if (nkey_items < 0) nkey_items = 0;

    autokey.script = lf2_environment_get(LF2_ENV_AUTOKEY);
    autokey.begin = 6000;
    autokey.every = 1200;
    autokey.hold = 150;
    const char *s_env = lf2_environment_get(LF2_ENV_AUTOKEY_START);
    const char *e_env = lf2_environment_get(LF2_ENV_AUTOKEY_EVERY);
    const char *h_env = lf2_environment_get(LF2_ENV_AUTOKEY_HOLD);
    if (s_env) autokey.begin = strtoull(s_env, NULL, 10);
    if (e_env) autokey.every = strtoull(e_env, NULL, 10);
    if (h_env) autokey.hold = strtoull(h_env, NULL, 10);
    if (!autokey.every) autokey.every = 1; /* a zero period would trap the modulo below */
    autokey.after_first_poll = lf2_environment_get(LF2_ENV_AUTOKEY_AFTER) != NULL;
    autokey.once = lf2_environment_get(LF2_ENV_AUTOKEY_ONCE) != NULL;
    for (const char *c = autokey.script; c && *c && autokey.count < PARSED_ITEMS;) {
        autokey.keys[autokey.count++] = (uint32_t)strtoul(c, (char **)&c, 16);
        while (*c == ',' || *c == ' ') c++;
    }
}

/* The keyboard script's down-state for one virtual key this frame. */
static int key_script_pressed(uint32_t vk)
{
    if (!nkey_items) return 0;
    const long frame = hostwin_frames();

    for (int i = 0; i < nkey_items; i++) {
        script_seen(SCRIPT_KEYS, i);
        int un = 0;
        const long at = script_when(key_items[i].when, &un);
        if (un) continue; /* its screen has not appeared YET -- not never */
        if (frame < at || frame >= at + KEY_SCRIPT_HOLD) continue;

        /* Recorded for every item whose window this frame is in, not only the one being
         * asked about: this is polled per-vk, and an item's own key being queried is a
         * property of the caller's loop rather than of the script. */
        script_fired(SCRIPT_KEYS, i);
        if (key_items[i].key == vk) return 1;
    }
    return 0;
}

/* The autokey schedule's synthetic press for one virtual key. */
static int autokey_pressed(uint32_t vk)
{
    if (!autokey.script || !autokey.count) return 0;

    /* With LF2_AUTOKEY_AFTER the clock starts when the game is first seen polling that
     * key, not at process start -- so the script tracks the game's state instead of
     * drifting with however long the data load happened to take. */
    static uint64_t base_ms;
    if (autokey.after_first_poll) {
        if (!rwatch_triggered()) return 0;
        if (!base_ms) base_ms = SDL_GetTicks();
    } else if (!base_ms) {
        base_ms = SDL_GetTicks();
    }

    const uint64_t now = SDL_GetTicks() - base_ms;
    if (now < autokey.begin) return 0;
    const uint64_t elapsed = now - autokey.begin;
    if (elapsed % autokey.every >= autokey.hold) return 0;

    /* As with clicks, a menu path is one-way: cycling the list keeps navigating and
     * overshoots the screen you were aiming for. LF2_AUTOKEY_ONCE plays it once. */
    const unsigned step = (unsigned)(elapsed / autokey.every);
    if (autokey.once && step >= autokey.count) return 0;
    return autokey.keys[step % autokey.count] == vk;
}

int input_script_key_down(uint32_t vk)
{
    parse_key_scripts_once();
    if (key_script_pressed(vk)) return 1;
    return autokey_pressed(vk);
}

/* One click-script item: the point and its when-spec verbatim. */
struct ClickItem {
    int px, py;
    char when[64];
};
static struct ClickItem click_items[PARSED_ITEMS];
static int nclick_items;

/* The periodic clicker's points and schedule, resolved once. */
static struct {
    int px[64], py[64];
    unsigned points;
    uint64_t begin, every;
    int once, done;
} autoclick;

static void parse_click_scripts_once(void)
{
    static int done;
    if (done) return;
    done = 1;

    const char *script = lf2_environment_get(LF2_ENV_CLICK_SCRIPT);
    if (script)
        for (const char *c = script; *c && nclick_items < PARSED_ITEMS;) {
            struct ClickItem *it = &click_items[nclick_items];
            it->px = (int)strtol(c, (char **)&c, 10);
            while (*c == ',' || *c == ' ') c++;
            it->py = (int)strtol(c, (char **)&c, 10);
            const char *when = c;
            if (*c == ':') c++;
            size_t len = 0;
            while (*c && *c != ';' && *c != ' ') {
                if (len < sizeof it->when - 1) len++;
                c++;
            }
            memcpy(it->when, when, len);
            it->when[len] = 0;
            while (*c == ';' || *c == ' ') c++;
            nclick_items++;
        }

    const char *spec = lf2_environment_get(LF2_ENV_AUTOCLICK);
    autoclick.begin = 6000;
    autoclick.every = 2500;
    autoclick.once = lf2_environment_get(LF2_ENV_AUTOCLICK_ONCE) != NULL;
    const char *s_env = lf2_environment_get(LF2_ENV_AUTOCLICK_START);
    const char *e_env = lf2_environment_get(LF2_ENV_AUTOCLICK_EVERY);
    /* Clicks default to the key schedule but can be given their own. They have to be
     * separable: reaching the game means one click on "game start", then a ~25 s data
     * load, then keys -- on a shared clock the keys are all consumed during the load. */
    if (!s_env) s_env = lf2_environment_get(LF2_ENV_AUTOKEY_START);
    if (!e_env) e_env = lf2_environment_get(LF2_ENV_AUTOKEY_EVERY);
    if (s_env) autoclick.begin = strtoul(s_env, NULL, 10);
    if (e_env) autoclick.every = strtoul(e_env, NULL, 10);
    if (!autoclick.every) autoclick.every = 1; /* a zero period would trap the modulo below */
    for (const char *c = spec; c && *c && autoclick.points < sizeof autoclick.px / sizeof autoclick.px[0];) {
        autoclick.px[autoclick.points] = (int)strtol(c, (char **)&c, 10);
        while (*c == ',' || *c == ' ') c++;
        autoclick.py[autoclick.points] = (int)strtol(c, (char **)&c, 10);
        autoclick.points++;
        while (*c == ';' || *c == ' ') c++;
    }
}

int input_script_click_configured(void)
{
    static int configured = -1;
    if (configured < 0)
        configured =
            lf2_environment_get(LF2_ENV_CLICK_SCRIPT) != NULL || lf2_environment_get(LF2_ENV_AUTOCLICK) != NULL;
    return configured;
}

int input_script_click_state(int *x, int *y)
{
    parse_click_scripts_once();

    /* The frame-scheduled click script takes priority, exactly as before. */
    if (nclick_items > 0) {
        const long frame = hostwin_frames();
        for (int i = 0; i < nclick_items; i++) {
            script_seen(SCRIPT_CLICKS, i);
            int un = 0;
            const long at = script_when(click_items[i].when, &un);
            if (un) continue; /* its screen has not appeared YET -- not never */

            /* The pointer is placed a few frames early and the button pressed after,
             * because the menu hit-tests where the pointer IS when the click arrives --
             * moving and clicking on the same frame races the game's own read. */
            if (frame >= at - 4 && frame < at + KEY_SCRIPT_HOLD) {
                *x = click_items[i].px;
                *y = click_items[i].py;
                if (frame >= at) {
                    script_fired(SCRIPT_CLICKS, i);
                    return 1;
                }
                return 0;
            }
        }
        return 0;
    }

    if (!autoclick.points) return 0;

    static uint64_t start_ms;
    if (!start_ms) start_ms = SDL_GetTicks();
    const uint64_t now = SDL_GetTicks() - start_ms;
    if (now < autoclick.begin) return 0;
    const uint64_t elapsed = now - autoclick.begin;

    /* Cycling suits probing one screen, but a menu path is one-way: re-clicking the list
     * from the top walks back out again, which reads as the game oscillating between two
     * screens rather than as the script looping. LF2_AUTOCLICK_ONCE walks the list once
     * and then stops clicking. */
    const unsigned step = (unsigned)(elapsed / autoclick.every);
    if (autoclick.once && step >= autoclick.points) return 0;
    const unsigned want = step % autoclick.points;
    *x = autoclick.px[want];
    *y = autoclick.py[want];
    return (elapsed % autoclick.every) < 150; /* button held briefly */
}

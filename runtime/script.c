/* Scripted input: the shared timing model and the exit report. See script.h.
 *
 * This used to live in gamepad.c, which meant the keyboard and mouse scripts had neither
 * half: they could not be keyed to a screen at all (they parsed a bare strtol), so four of
 * the nine route tests were still stopwatches aimed at a moving target, and nothing told
 * anyone when one of their presses missed. Issue #25.
 */
#include "script.h"
#include "hostwin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the screens a route can be keyed to ----
 *
 * Taken from what the game DRAWS, not from a .data flag: the one candidate for a flag
 * (0x0044d070) is the game mode wearing a screen's disguise and reads the same in VS mode
 * whether the overlay is up or not. See runtime/overrides/menu.c.
 *
 * `charselect` is the post-load panel, and that panel is ALSO the mode menu -- the two share
 * a blit destination, so this signal goes up when the mode menu appears, a little before
 * character selection proper. That is what makes it a usable reference point for a route
 * (it is the first thing after the load, and the load is the part that moves), but it is not
 * a claim that character selection is on screen. A route that must distinguish the two
 * counts frames from here; nothing in the port asks this to tell them apart. */
enum { SCREEN_N = 3 };
static long screen_first[SCREEN_N] = { -1, -1, -1 };
static const char *const SCREEN_NAME[SCREEN_N] = { "charselect", "overlay", "match" };

void script_observe_screens(long frame)
{
    const int up[SCREEN_N] = { panel_charselect_up(), panel_overlay_up(), panel_hud_up() };
    for (int i = 0; i < SCREEN_N; i++)
        if (up[i] && screen_first[i] < 0) {
            screen_first[i] = frame;
            fprintf(stderr, "scripted input: screen %s first up at frame %ld\n",
                    SCREEN_NAME[i], frame);
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
        if (screen_first[i] < 0) { *unresolved = 1; return -1; }
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

static const struct { const char *env; char sep; } STREAM[SCRIPT_STREAMS] = {
    { "LF2_VIRTUAL_PAD",  ',' },
    { "LF2_VIRTUAL_PAD2", ',' },
    { "LF2_KEY_SCRIPT",   ',' },
    { "LF2_CLICK_SCRIPT", ';' },
};

static int in_range(int stream, int idx)
{
    if (stream < 0 || stream >= SCRIPT_STREAMS) return 0;
    if (idx < 0 || idx >= MAX_ITEMS) { item_overflow[stream] = 1; return 0; }
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
    for (int s = 0; s < SCRIPT_STREAMS; s++) if (getenv(STREAM[s].env)) configured = 1;
    if (!configured) return;

    fprintf(stderr, "scripted input: screens reached --");
    int any = 0;
    for (int i = 0; i < SCREEN_N; i++) {
        if (screen_first[i] < 0) continue;
        fprintf(stderr, " %s@%ld", SCREEN_NAME[i], screen_first[i]);
        any = 1;
    }
    if (!any) fprintf(stderr, " NONE");
    fprintf(stderr, "\n");

    /* One line per configured script, ALWAYS, with its denominator. "15 of 15 items fired"
     * is the negative this report has to be able to print: without it, silence is
     * indistinguishable from a report that was never reached. */
    for (int s = 0; s < SCRIPT_STREAMS; s++) {
        const char *script = getenv(STREAM[s].env);
        if (!script) continue;

        int fired = 0;
        for (int i = 0; i < item_count[s]; i++)
            if (item_state[s][i] == ITEM_FIRED) fired++;
        fprintf(stderr, "%s: %d of %d items fired\n", STREAM[s].env, fired, item_count[s]);
        if (item_overflow[s])
            fprintf(stderr, "%s: route longer than %d items -- the ones past that were "
                            "NEVER PLAYED and are not counted above\n",
                    STREAM[s].env, MAX_ITEMS);
        if (fired == item_count[s] && !item_overflow[s]) continue;

        /* Name them. "Something did not fire" makes the next person bisect the route to
         * find out which; the text is right here. */
        int idx = 0;
        for (const char *c = script; *c; ) {
            const char *item = c;
            while (*c && *c != STREAM[s].sep && *c != ' ') c++;
            const int n = (int)(c - item);
            while (*c == STREAM[s].sep || *c == ' ') c++;
            const int i = idx++;
            if (i >= item_count[s]) break;
            if (item_state[s][i] == ITEM_FIRED) continue;
            fprintf(stderr, "%s: item %d `%.*s' %s\n", STREAM[s].env, i, n, item,
                    item_state[s][i] == ITEM_BAD
                        ? "NEVER FIRED -- this build could not parse that"
                        : "NEVER FIRED -- its screen never appeared, so any assertion about "
                          "what it should have done is about an input that did not happen");
        }
    }
}

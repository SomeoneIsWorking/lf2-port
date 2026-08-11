/* runtime/app/framespec.h -- the frame-spec grammar LF2_FRAME_DUMP and LF2_MEM_DUMP take.
 *
 * WHY THIS EXISTS. The anchored form was added because a dump aimed at frame 2250 to catch "a
 * frame with fighters on it" quietly became a different moment the instant anything upstream
 * moved (issue #57). A parser bug in that form is invisible: nothing crashes and nothing
 * prints, the frame simply is not dumped, and the route that wanted it says "the run never
 * reached that frame" -- which reads as a problem with the game. So the grammar is walked here
 * rather than by a three-minute route run, and the negative cases matter as much as the
 * positives: an UNRESOLVED anchor must match nothing at all.
 *
 * ddraw.c includes the same header, so this is exercising the code that ships.
 */
#include "framespec.h"

#include <stdio.h>
#include <string.h>

static int fails;
static int checks;

static void eq(const char *what, int got, int want)
{
    checks++;
    if (got == want) return;
    fprintf(stderr, "FAIL %s: got %d, want %d\n", what, got, want);
    fails++;
}

/* The screens this stub knows, at fixed frames -- the point of passing the resolver in is that
 * a test can have screens without running a game to draw them. `overlay` is deliberately NOT
 * here, so there is a name that parses and still cannot resolve.
 *
 * ON THE UNRESOLVED PATH IT RETURNS A PLAUSIBLE FRAME, NOT -1, AND THAT IS THE WHOLE POINT.
 * The first version of this stub returned -1 there, and the negatives below were vacuous: a
 * mutant of the header that ignored `unresolved` entirely PASSED all 26 checks, because -1 is
 * never the frame anyone asks about, so "did it honour the flag" and "did it return something
 * that happened not to match" were indistinguishable. Returning the offset alone is what a
 * naive resolver would plausibly do, and it makes the flag the only thing that can produce the
 * right answer. Verified by re-running the mutant against this version: it fails. */
static long stub_resolve(const char *item, int *unresolved)
{
    *unresolved = 0;
    long base;
    const char *rest;
    if (strncmp(item, "@charselect", 11) == 0) { base = 7;    rest = item + 11; }
    else if (strncmp(item, "@match", 6) == 0)  { base = 1069; rest = item + 6;  }
    else {
        *unresolved = 1;
        base = 0;                                  /* see the note above: NOT -1 */
        rest = item;
        while (*rest && *rest != '+') rest++;
    }
    if (*rest == '+') return base + strtol(rest + 1, NULL, 10);
    return base;
}

int main(void)
{
    /* ---- bare frame numbers, the form that predates the anchors ---- */
    eq("single number hits",        framespec_matches("1200", 1200, stub_resolve), 1);
    eq("single number misses",      framespec_matches("1200", 1201, stub_resolve), 0);
    eq("list, first",               framespec_matches("460,1410", 460, stub_resolve), 1);
    eq("list, second",              framespec_matches("460,1410", 1410, stub_resolve), 1);
    eq("list, neither",             framespec_matches("460,1410", 900, stub_resolve), 0);
    eq("space separated",           framespec_matches("460 1410", 1410, stub_resolve), 1);
    eq("NULL spec matches nothing", framespec_matches(NULL, 0, stub_resolve), 0);
    eq("empty spec",                framespec_matches("", 0, stub_resolve), 0);

    /* ---- anchors ---- */
    eq("anchor with offset",        framespec_matches("@match+282", 1351, stub_resolve), 1);
    eq("anchor off by one",         framespec_matches("@match+282", 1350, stub_resolve), 0);
    eq("anchor, no offset",         framespec_matches("@match", 1069, stub_resolve), 1);
    eq("charselect anchor",         framespec_matches("@charselect+394", 401, stub_resolve), 1);

    /* The real render_test spec: a menu frame and a match frame in one list. */
    eq("mixed list, menu frame",  framespec_matches("@charselect+394,@match+282", 401, stub_resolve), 1);
    eq("mixed list, match frame", framespec_matches("@charselect+394,@match+282", 1351, stub_resolve), 1);
    eq("mixed list, neither",     framespec_matches("@charselect+394,@match+282", 700, stub_resolve), 0);
    eq("number and anchor mixed", framespec_matches("500,@match+282", 500, stub_resolve), 1);
    eq("anchor after number",     framespec_matches("500,@match+282", 1351, stub_resolve), 1);

    /* ---- THE NEGATIVES, which are the whole reason this file exists ----
     *
     * An anchor whose screen has not been reached must match NOTHING. If it fell back to some
     * frame it might mean, a run that never got to the match would dump an arbitrary frame and
     * the comparison downstream would be of the wrong picture while looking perfectly valid. */
    eq("unresolved anchor matches nothing",     framespec_matches("@overlay+99", 99, stub_resolve), 0);
    eq("unresolved anchor, any frame",          framespec_matches("@overlay+99", 0, stub_resolve), 0);
    eq("unknown name does not swallow the list",
       framespec_matches("@overlay+99,@match+282", 1351, stub_resolve), 1);
    eq("no resolver: anchors match nothing",    framespec_matches("@match+282", 1351, NULL), 0);
    eq("no resolver: numbers still work",       framespec_matches("1200", 1200, NULL), 1);

    /* An item longer than the buffer must be truncated and resolve to nothing rather than run
     * off the end -- and it must not eat the item after it. */
    {
        char big[256];
        memset(big, 'x', sizeof big);
        big[0] = '@';
        big[sizeof big - 1] = '\0';
        eq("overlong item resolves to nothing", framespec_matches(big, 0, stub_resolve), 0);

        char spec[320];
        snprintf(spec, sizeof spec, "%s,@match+282", big);
        eq("overlong item does not eat the next one",
           framespec_matches(spec, 1351, stub_resolve), 1);
    }

    /* Garbage stops the walk rather than looping on it: the parser must terminate. */
    eq("garbage matches nothing",  framespec_matches("banana", 0, stub_resolve), 0);
    eq("garbage after a number",   framespec_matches("500,banana", 500, stub_resolve), 1);

    printf("framespec: %d checks, %d failure(s)\n", checks, fails);
    return fails ? 1 : 0;
}

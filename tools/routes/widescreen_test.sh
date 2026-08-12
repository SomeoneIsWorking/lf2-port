#!/bin/sh
# The game's wideness follows the WINDOW, and nothing else (issue #20).
#
# THE RULE CHANGED TWICE, and this file is the record of both. It first followed the window's
# ASPECT and kept the game's own 550 rows, letting SDL scale the finished frame up. Then it
# became the window's real pixel WIDTH at 550 rows drawn 1:1, with black bands for the rows
# the game has no world to fill. Now (issue #41) it is BOTH numbers, kept apart:
#
#   SCALE      = min(win_h/550, win_w/794)   -- the picture FILLS the window
#   COMPOSITION = win_w / scale, floored at 794, i.e. how much WORLD is on screen
#
# The composition width is what this test reads, because it is what the game is told. At 16:9
# it comes out at the same 978 the ASPECT rule gave -- and that similarity is a trap worth
# naming, because the two are not the same design. The old one composed 978x550 into a small
# buffer and had SDL blow the whole picture up, quantising text and lighting to the small grid
# first. The new one hands 978x550 of world to the game and the native renderer scales EVERY
# QUAD as it draws into a full-resolution target. This test cannot tell those apart; the check
# that can is tools/routes/render_test.sh, and the frame dumps under it.
#
# Four windows, four expected compositions, and the set is chosen so that no single wrong
# implementation satisfies all of them:
#
#   794x550    the game's own picture, and the scale must be EXACTLY 1. Composition 794.
#              A build that always widened, or always scaled, fails here alone -- and every
#              byte-identity arm in the suite depends on this case being untouched.
#   1600x550   the game's own HEIGHT and twice the width. The scale is still 1, so all 1600
#              pixels are world: composition 1600. This is the case that shows extra width
#              becomes FIELD OF VIEW rather than magnification.
#   1920x1080  the scale is 1080/550 = 1.963, so the world on screen is 1920/1.963 = 978.
#              A build that kept the pixel-width rule says 1920 and fails here alone.
#   800x900    the window is TALL and barely wider than the game, so the WIDTH binds the
#              scale (800/794) and the composition floors at 794. A build that took the
#              height unconditionally would scale by 900/550 and ask for 489 here.
#
# Each run only has to reach the point where the window exists, so they are short.
#
# Nothing here reads LF2_WIDESCREEN, because there is no such variable any more. What the
# runs do set is LF2_WINDOW_SIZE, and that is not the old knob renamed: it names a WINDOW,
# and the composition is derived from it by exactly the code a user's window manager drives
# when someone drags an edge. A headless run has nobody to drag one.
set -eu

BUILD=$(cd "${BUILD:-scratch/build}" 2>/dev/null && pwd) || BUILD=${BUILD:-scratch/build}
GAME=$(cd "${GAME:-game}" 2>/dev/null && pwd) || GAME=${GAME:-game}
LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT

if [ ! -x "$BUILD/lf2" ]; then echo "SKIP: $BUILD/lf2 not built"; exit 77; fi
if [ ! -f "$GAME/lf2.exe" ]; then echo "SKIP: no game tree at $GAME"; exit 77; fi

fail=0
say_ok()   { echo "  ok    $1"; }
say_fail() { echo "  FAIL  $1"; fail=1; }

# $1 window, $2 expected composition width, $3 fill|band -- whether the drawn picture is
# expected to cover the whole window. That second assertion is the one issue #41 added: a
# composition width alone cannot say whether the player sees black bands, and for the whole
# life of the pixel-width rule a 1080-row window had 530 rows of them while this test passed.
check() {
    ( cd "$GAME" && \
      SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy LF2_UNPACED=1 \
      LF2_WINDOW_SIZE="$1" LF2_QUIT_AFTER=120 timeout -k 5 120 "$BUILD/lf2" lf2.exe ) \
      > "$LOG" 2>&1 || true

    # The line names both sides, so a mismatch says what was asked for as well as what came
    # out. Its absence is a failure in itself: no line means the geometry was never computed,
    # which is a different thing from computing it wrongly and must not read as a pass.
    line=$(grep -m1 "widescreen: window $1 -> composition" "$LOG" || true)
    if [ -z "$line" ]; then
        say_fail "$1: no composition was reported at all, so the window drove NOTHING"
        grep -m3 -i "widescreen\|window:" "$LOG" || echo "        no window output at all"
        return
    fi
    got=$(echo "$line" | sed 's/.*composition \([0-9]*\)x.*/\1/')
    if [ "$got" = "$2" ]; then
        say_ok "$1 window -> ${got}x550 of world"
    else
        say_fail "$1 window -> ${got}x550, expected ${2}x550"
        say_fail "      ($line)"
    fi

    # The same line says what rectangle the picture is drawn into, and whether that covers the
    # window. Read from the run rather than recomputed here, so this is not the port's
    # arithmetic checking itself.
    case "$line" in
    *"fills the window"*) got_fill=fill ;;
    *"with a band"*)      got_fill=band ;;
    *)                    got_fill=unreported ;;
    esac
    if [ "$got_fill" = "$3" ]; then
        [ "$3" = fill ] && say_ok "$1: the picture fills the window" \
                        || say_ok "$1: a band, correctly -- the window is narrower than the game"
    else
        say_fail "$1: expected '$3', got '$got_fill'"
        say_fail "      ($line)"
    fi
}

echo "widescreen: the composition follows the window (quick)..."
check 794x550   794  fill
check 1600x550  1600 fill
check 1920x1080 978  fill
# The one window here whose picture CANNOT fill: it is taller in aspect than the game, so the
# width binds and the leftover rows have no world to put in them. Present as the negative --
# "fills the window" would otherwise pass on a build that stretched everything unconditionally,
# which is exactly the whole-screen scaling issue #41 rules out.
check 800x900   794  band

# MID-RUN, which is the actual headline: the field of view changes while the game is running,
# not only at startup. No scripted run can drag a window edge -- offscreen SDL has no window
# manager and never delivers SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED -- so LF2_WINDOW_RESIZE
# stands in for one, driving the same entry point the real event does.
#
# Asserted three ways, because each alone has a way of passing while nothing happened: the
# step FIRED (the script says so, and says at exit if it never did), the composition CHANGED
# to the new width, and it was 794 BEFORE that -- so a build that simply started wide would
# fail the third.
( cd "$GAME" && \
  SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy LF2_UNPACED=1 \
  LF2_WINDOW_SIZE=794x550 LF2_WINDOW_RESIZE=200:1600x550 \
  LF2_QUIT_AFTER=400 timeout -k 5 180 "$BUILD/lf2" lf2.exe ) > "$LOG" 2>&1 || true

if grep -q "NEVER FIRED" "$LOG"; then
    say_fail "mid-run: the resize step never fired, so nothing about resizing was measured"
    grep -m2 "window resize script" "$LOG" || true
elif ! grep -q "window resize script: frame .* -- 1600x550" "$LOG"; then
    say_fail "mid-run: no resize step ran at all -- LF2_WINDOW_RESIZE was not honoured"
    grep -m3 -i "resize\|widescreen" "$LOG" || echo "        no resize or widescreen output"
else
    before=$(grep -m1 "widescreen: window 794x550 -> composition" "$LOG" || true)
    after=$(grep -m1 "widescreen: window 1600x550 -> composition 1600x" "$LOG" || true)
    if [ -n "$before" ] && [ -n "$after" ]; then
        say_ok "mid-run: 794 at startup, then 1600 after the window changed under the game"
    else
        say_fail "mid-run: the resize fired but the composition did not follow it"
        [ -z "$before" ] && say_fail "      (it never reported the 794x550 it started at)"
        [ -z "$after" ]  && say_fail "      (it never reported a 1600-wide composition)"
    fi
fi

# ---------------------------------------------------------------------------
# PER-SCREEN FRAMING (issue #44). The composition width above says how much world is on
# screen; it says nothing about WHERE a fixed-794 screen sits inside it, and three screens now
# want three different answers:
#
#   the front end        LEFT    -- its character portrait is drawn at a hard literal x = 0
#   the mode menu        LEFT    -- the same portrait sprite, the same literal
#   the loading screen   CENTRED, with its side bands extended from its own edge columns,
#                        because its backdrop is a PICTURE and the game has nothing authored
#                        to put beside it. That extension is a declared port choice and the
#                        run says so in as many words.
#   character selection  CENTRED and UNCHANGED. The reporter said it was already right, so it
#                        is the NEGATIVE: a change that left-aligned everything would satisfy
#                        the three checks above and fail this one alone.
#
# Read out of the run's own framing report rather than recomputed here, so this checks the
# port and not a copy of its arithmetic.
echo "widescreen: each screen is framed its own way (issue #44)..."
FLOG=$(mktemp)
( cd "$GAME" && \
  SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy LF2_UNPACED=1 LF2_RENDERER=soft \
  LF2_WINDOW_SIZE=1710x370 LF2_FRAMING_DEBUG=1 \
  LF2_CLICK_SCRIPT="403,228@frontend+0;400,241@frontend+450;200,150@charselect+98" \
  LF2_QUIT_AFTER=1500 timeout -k 5 200 "$BUILD/lf2" lf2.exe ) > "$FLOG" 2>&1 || true

if ! grep -q "^framing:" "$FLOG"; then
    say_fail "no framing report at all -- the run never reached a fixed-width screen, so"
    say_fail "      NOTHING about per-screen framing was measured. This is not a pass."
else
    # THE MENU IS CENTRED; ONLY ITS BACKDROP ART IS LEFT-ANCHORED. Both halves are asserted,
    # because each alone passes on a build that gets the other wrong: "centred" alone passes
    # when the portrait was centred with everything else, and "backdrop at x 0" alone passes
    # when the whole screen was dragged to the edge, which is what the first attempt did.
    grep -qE "^framing:.*10206c -> CENTRED, backdrop art LEFT at x 0" "$FLOG" \
        && say_ok "the front end is centred with its backdrop art left-anchored" \
        || say_fail "the front end is not reported centred-with-left-backdrop"
    grep -qE "^framing:.*122565 -> CENTRED, backdrop art LEFT at x 0" "$FLOG" \
        && say_ok "the mode menu is centred with its backdrop art left-anchored" \
        || say_fail "the mode menu is not reported centred-with-left-backdrop"
    # And the art must actually have been DRAWN at x 0, not merely promised: a screen can be
    # labelled without a single draw ever matching the identification.
    # head -1, not tail -1: the matched text is "(1800 such draw(s) kept at x 0)" and its LAST
    # number is the 0 in "x 0", so tail took the zero and the check failed on a working build.
    kept=$(grep -oE "\(([0-9]+) such draw\(s\) kept at x 0\)" "$FLOG" | grep -oE "[0-9]+" | head -1)
    if [ "${kept:-0}" -gt 0 ]; then
        say_ok "the backdrop art was kept at x 0 on $kept draw(s)"
    else
        say_fail "NO draw was ever kept at x 0 -- the two menus are labelled left-anchored but"
        say_fail "      nothing matched the backdrop identification, so the picture is centred"
    fi
    grep -qE "^framing:.*PICTURE backdrop.*-> CENTRED" "$FLOG" \
        && say_ok "the loading screen is CENTRED with its bands extended" \
        || say_fail "the loading screen is not reported CENTRED with extended bands"
    # THE NEGATIVE, and it names CHARACTER SELECTION specifically -- fill 000000 -- rather
    # than accepting any centred screen. "some screen was centred at 874" is satisfied by the
    # LOADING screen, which is centred too, so it would pass on a build that left-aligned
    # character selection: the one thing the reporter said must not move. Naming the screen is
    # the difference between a control and a check that looks like one.
    grep -qE "^framing:.*fill 000000 -> CENTRED, offset 874" "$FLOG" \
        && say_ok "character selection is still CENTRED at the full 874 offset (unchanged)" \
        || say_fail "character selection was not reported centred at offset 874 -- issue #44
      says it is already correct and must not move"
    # An extrapolation the game does not have must announce itself, or it reads as fidelity.
    grep -qE "not the game's" "$FLOG" \
        && say_ok "the loading screen's band extension is declared a port choice" \
        || say_fail "the band extension does not declare itself as the port's choice"
    if grep -q "^framing:" "$FLOG"; then
        echo "        (report: $(grep -c '^framing:' "$FLOG") lines)"
    fi
fi
rm -f "$FLOG"

[ "$fail" = 0 ] && echo "widescreen: ok" || echo "widescreen: FAILED"
exit "$fail"

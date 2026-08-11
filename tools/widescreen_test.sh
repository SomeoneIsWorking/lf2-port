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
# that can is tools/render_test.sh, and the frame dumps under it.
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
      LF2_WINDOW_SIZE="$1" LF2_QUIT_AFTER=120 timeout 120 "$BUILD/lf2" lf2.exe ) \
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
  LF2_QUIT_AFTER=400 timeout 180 "$BUILD/lf2" lf2.exe ) > "$LOG" 2>&1 || true

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

[ "$fail" = 0 ] && echo "widescreen: ok" || echo "widescreen: FAILED"
exit "$fail"

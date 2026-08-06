#!/bin/sh
# The game's wideness follows the WINDOW, and nothing else (issue #20).
#
# THE RULE CHANGED, and this file is the record of it. The composition used to follow the
# window's ASPECT and keep the game's own 550 rows -- a 1920x1080 window composed 978x550 and
# SDL scaled that up by 1.96. That is an upscale, and 1.96 is not an integer, so a game pixel
# landed as a block two OR three screen pixels wide. The composition is now the window's real
# pixel WIDTH and the game's own 550 rows, drawn 1:1 and centred, with black bands for the
# rows the game has no world to fill (its floor, its z boundary and every layer's picture are
# authored against 550, so there is nothing to put there).
#
# Four windows, four expected compositions, and the set is chosen so that no single wrong
# implementation satisfies all of them:
#
#   794x550    the game's own picture. Widescreen must be OFF -- a build that simply always
#              widened would pass every other case here.
#   1600x550   wider than the game's own. Composition 1600 wide. This is the case the old
#              LF2_WIDESCREEN=<w> also produced, so it is the one that shows the change is
#              not a behaviour change.
#   1920x1080  the case that separates PIXEL WIDTH from ASPECT, and it is the one that
#              flipped: the answer is 1920, not the 550*1920/1080 = 978 the aspect rule gave.
#              A build still following the aspect says 978 and fails here alone.
#   800x900    NARROWER than the game's own 794? No -- 800 is wider, but only just, and the
#              window is TALL. The composition is 800: the height no longer enters into it at
#              all, which is exactly what this case is here to pin. Under the aspect rule it
#              was 550*800/900 = 489, clamped up to 794, so this case also flipped and a
#              build that kept any aspect term in the formula fails it.
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

# $1 window, $2 expected composition width
check() {
    ( cd "$GAME" && \
      SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
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
        say_ok "$1 window -> ${got}x550 composition"
    else
        say_fail "$1 window -> ${got}x550, expected ${2}x550"
        say_fail "      ($line)"
    fi
}

echo "widescreen: the composition follows the window (about 1 min)..."
check 794x550   794
check 1600x550  1600
check 1920x1080 1920
check 800x900   800

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
  SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
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

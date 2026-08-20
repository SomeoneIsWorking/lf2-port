#!/bin/sh
# LEAVE MATCH from the pause menu reaches the game's FRONT-END MENU (issue #22).
#
# WHAT IS ASSERTED, and why the verdict is a word rather than a picture. The game's screen
# selector is 0x0044d020: fn_0041bc90 runs the match while it is 0 and otherwise hands it by
# address to fn_00429730, which dispatches -- 1 is character selection, 10 is the front-end
# menu (fn_00431d10, the eight-item list). Those two screens share a blit destination and can
# share a picture, so a frame dump cannot tell them apart (issue #59); the word can, and the
# word is what the game itself branches on. runtime/overrides/screens.c reports it.
#
# WHAT THE EXIT IS, and what this guards. LEAVE MATCH calls the game's OWN exit code now
# (screens.c's guest_end_match + guest_overlay_exit, read off the decompilation of
# fn_0041bc90 and fn_00429730) instead of injecting an F4 keystroke and a synthetic attack.
# The old failure it replaces: the synthetic press ran for two gathers and fn_00431c70 --
# which the game runs on every way out of a match -- cleared the held-button latch
# fn_00431b70 edge-detects against, so the second gather landed on the front-end menu as a
# FRESH press, the menu confirmed whatever the cursor was on, and the exit sailed straight
# through 10 to character selection. A run that lands on 1 is that bug. The route is
# deliberately the plain one-player case where it always reproduced, and it is the same
# verdict now: does LEAVE MATCH land on 10, or does it overshoot?
set -eu

BUILD=$(cd "${BUILD:-scratch/build}" 2>/dev/null && pwd) || BUILD=${BUILD:-scratch/build}
GAME=$(cd "${GAME:-game}" 2>/dev/null && pwd) || GAME=${GAME:-game}
LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT

if [ ! -x "$BUILD/lf2" ]; then echo "SKIP: $BUILD/lf2 not built"; exit 77; fi
if [ ! -f "$GAME/lf2.exe" ]; then echo "SKIP: no game tree at $GAME"; exit 77; fi

# Pad one takes the deterministic route into a VS match, keyed to the screens the game DRAWS
# (issue #25), then pauses and takes the second row. Pad one joined through the game's own
# character selection, so coop_owns is false for it and DROP OUT does not appear: the rows are
# RESUME, LEAVE MATCH, ... and one press of DOWN lands on the item this route is about.
PAD1="south@modemenu+60"
PAD1="$PAD1,south@charselect+58,south@charselect+118,south@charselect+178,south@charselect+238,up@charselect+298,up@charselect+358,south@charselect+418"
PAD1="$PAD1,south@charselect+618,south@charselect+838"
PAD1="$PAD1,up@overlay+99,up@overlay+159,south@overlay+219"        # 2 -> 1 -> 0 = Fight!
PAD1="$PAD1,start@match+300,down@match+360,south@match+420"        # pause, LEAVE MATCH, take it

echo "exit to menu: LEAVE MATCH should reach the front-end menu..."
( cd "$GAME" && \
  SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy LF2_UNPACED=1 \
  LF2_VIRTUAL_PAD="$PAD1" \
  LF2_QUIT_AFTER=2400 timeout -k 5 300 "$BUILD/lf2" lf2.exe ) > "$LOG" 2>&1

fail=0
say_ok()   { echo "  ok    $1"; }
say_fail() { echo "  FAIL  $1"; fail=1; }

# The chain first, because a break anywhere in it makes the verdict vacuous rather than wrong.
if ! grep -q "exit to menu: the game's own match-end ran" "$LOG"; then
    echo "  FAIL  the pause menu's LEAVE MATCH was never taken, so this run proves NOTHING"
    echo "        about where the exit lands"
    grep -m3 "^pause" "$LOG" || echo "        no pause-menu output at all"
    exit 1
fi
say_ok "taken: $(grep -m1 "exit to menu: the game's own match-end ran" "$LOG")"

if ! grep -q "exit to menu: the overlay is up" "$LOG"; then
    say_fail "overlay: the game's post-match overlay never appeared, so the match-end did not land"
else
    say_ok "overlay: $(grep -m1 'exit to menu: the overlay is up' "$LOG")"
fi

if ! grep -q "exit to menu: the game's own Exit dispatch ran" "$LOG"; then
    say_fail "confirm: the game's own Exit dispatch never ran"
else
    say_ok "confirm: $(grep -m1 "exit to menu: the game's own Exit dispatch ran" "$LOG")"
fi

# THE VERDICT. It names the screen either way -- a landing report that only ever printed on
# failure would be silent on the run that matters.
land=$(grep -m1 "exit to menu: LANDED on screen" "$LOG" || true)
if [ -z "$land" ]; then
    say_fail "landing: the run never reported which screen the exit reached, so nothing here"
    say_fail "         measured the thing this route is for"
else
    num=$(echo "$land" | sed 's/.*LANDED on screen \([0-9]*\).*/\1/')
    case "$num" in
    10) say_ok   "landing: $land" ;;
    1|2|3)
        say_fail "landing: $land"
        say_fail "         that is CHARACTER SELECTION -- the exit passed through the"
        say_fail "         front-end menu and confirmed something on it (issue #22)" ;;
    *)  say_fail "landing: $land"
        say_fail "         which is neither the front-end menu (10) nor character selection" ;;
    esac
fi

[ "$fail" = 0 ] && echo "exit to menu: ok" || echo "exit to menu: FAILED"
exit "$fail"

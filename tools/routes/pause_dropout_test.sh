#!/bin/sh
# A joined player leaves a match deliberately, from the pause menu on its own pad (issue #22).
#
# The chain has four links and each is asserted, because a break in any of them looks from
# outside like the same thing -- a run where nothing left the match:
#
#   join     pad two claims a slot mid-match and locks a character in. Without this the run
#            has no drop-out to make and every later assertion is vacuous.
#   own      the slot that leaves is the one PAD TWO joined, not merely some slot. Drop-out
#            is per player: a menu that dropped whoever was first would pass an assertion
#            that only counted leavings, and would take the wrong fighter out of the fight.
#   leave    the join is undone FIELD FOR FIELD -- gate, device selector and joined-mask bit
#            -- which is what makes a slot that left indistinguishable from one that never
#            joined. Asserted through coop_leave's own report rather than by re-reading state
#            the same code wrote.
#   intact   player one is STILL IN THE MATCH afterwards. This is the negative, and it is the
#            one that catches a drop-out that emptied more than it was asked to.
#
# The pause menu is opened with START on pad two, which is what makes the drop-out that pad's
# rather than the keyboard's: the menu records the device that opened it, and DROP OUT only
# appears for a device driving a slot this port filled.
set -eu

BUILD=$(cd "${BUILD:-scratch/build}" 2>/dev/null && pwd) || BUILD=${BUILD:-scratch/build}
GAME=$(cd "${GAME:-game}" 2>/dev/null && pwd) || GAME=${GAME:-game}
LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT

if [ ! -x "$BUILD/lf2" ]; then echo "SKIP: $BUILD/lf2 not built"; exit 77; fi
if [ ! -f "$GAME/lf2.exe" ]; then echo "SKIP: no game tree at $GAME"; exit 77; fi

# Pad one takes the usual deterministic route into a VS match; see tools/routes/controller_test.sh.
#
# Keyed to the screens the game DRAWS, not to frame numbers. The frame a screen arrives on
# moves with the data load and with how busy the box is, so a frame-numbered route is a
# stopwatch aimed at a moving target -- issue #18, and this route was one of the last three
# still exposed to it (issue #25).
PAD1="south@frontend+0,south@frontend+60,south@frontend+120,south@frontend+180"        # the front end, before any screen
PAD1="$PAD1,south@charselect+58,south@charselect+118,south@charselect+178,south@charselect+238,up@charselect+298,up@charselect+358,south@charselect+418"
PAD1="$PAD1,south@charselect+618,south@charselect+838"  # join, then open the overlay
PAD1="$PAD1,up@overlay+99,up@overlay+159,south@overlay+219"   # 2 -> 1 -> 0 = Fight!

# Pad two: claim and open the choice, lock a character in, then START to pause, DOWN to move
# from RESUME to DROP OUT, and attack to take it. One row down and no further: DROP OUT is
# the second row exactly when this pad owns a slot, so landing anywhere else is itself the
# failure this asserts.
#
# All five are keyed to the MATCH, which is the screen every one of them is about: a join
# that lands before the match starts claims nothing, and the pause that follows would then
# be a pause with no drop-out in it. The gaps are the ones the frame-numbered version used.
PAD2="south@match+158,south@match+258,start@match+458,down@match+518,south@match+578"

echo "pause drop-out: a joined player leaves from the pause menu..."
( cd "$GAME" && \
  SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy LF2_UNPACED=1 \
  LF2_VIRTUAL_PAD="$PAD1" LF2_VIRTUAL_PAD2="$PAD2" LF2_RENDER_DEBUG=1 \
  LF2_QUIT_AFTER=2360 timeout -k 5 300 "$BUILD/lf2" lf2.exe ) > "$LOG" 2>&1

fail=0
say_ok()   { echo "  ok    $1"; }
say_fail() { echo "  FAIL  $1"; fail=1; }

lock=$(grep -m1 "coop select: slot .* LOCKED IN" "$LOG" || true)
if [ -z "$lock" ]; then
    echo "  FAIL  pad two never joined and locked in, so this run proves NOTHING about"
    echo "        dropping out (most likely the route never reached a running match)"
    grep -m3 "^coop" "$LOG" || echo "        no coop output at all"
    exit 1
fi
slot=$(echo "$lock" | sed 's/.*slot \([0-9]*\) LOCKED IN.*/\1/')
say_ok "join: pad two locked in at slot $slot -- $lock"

leave=$(grep -m1 "coop leave: slot .* dropped out (the player chose to drop out from the pause menu)" "$LOG" || true)
if [ -z "$leave" ]; then
    say_fail "leave: nothing left the match from the pause menu"
    grep -m3 "coop leave" "$LOG" || echo "        no coop leave output at all"
else
    left=$(echo "$leave" | sed 's/.*slot \([0-9]*\) dropped out.*/\1/')
    if [ "$left" = "$slot" ]; then
        say_ok "own: the slot that left is slot $left, the one pad two joined"
    else
        say_fail "own: slot $left left, but pad two had joined slot $slot -- the menu dropped"
        say_fail "     out the wrong player"
    fi
    if echo "$leave" | grep -q "gate cleared, devsel -> 0, joined mask"; then
        say_ok "leave: the join was undone field for field -- $leave"
    else
        say_fail "leave: the drop-out did not report undoing gate, selector and mask"
    fi
fi

# The negative. Player one joined through the game's own character selection, so coop_leave
# would refuse it -- but "would refuse" is a claim about code, and this asserts the run.
if grep -q "coop leave: slot 0 dropped out" "$LOG"; then
    say_fail "intact: player one was dropped out too, which nothing asked for"
else
    say_ok "intact: player one was left in the match"
fi

# WHICH RENDERER DREW THE PAUSED FRAMES (issue #52). The menu freezes the game by declining
# to call its update, so no blit is recorded and the display list would be empty; the renderer
# redraws the list it already had, and counts those frames. A build where the menu falls back
# to the software compositor -- which is what every build before this one did, and it looked
# perfectly fine because the software path draws the menu too -- reports none.
#
# ONE ASSERTION, NO SECOND RUN. Whether this counter counts the right thing, and whether it
# reads zero when nothing is held, is `ctest framelife` -- runtime/video/framelife.h is pure
# bookkeeping and render.c includes it, so the negative control is offline where it belongs
# (issue #53). What is left here is the one thing that genuinely needs a running game: that a
# real pause, in a real match, is presented by the renderer.
#
# THE LAST REPORT, NOT THE FIRST. The render report is periodic (every 900 frames) and the
# counters are cumulative, so the first one covers the menus, long before the match this route
# pauses in -- reading it with `grep -m1` reported 0 for a run that had held 273 frames.
held=$(grep "frame(s) were drawn over a RETAINED list" "$LOG" | tail -1 | sed 's/^render: \([0-9]*\) .*/\1/')
if [ -z "$held" ]; then
    say_fail "render: the run never reported a retained-frame count at all, so nothing here"
    say_fail "        measured which renderer drew the pause menu"
elif [ "$held" -gt 0 ] 2>/dev/null; then
    say_ok "render: the native renderer drew $held paused frame(s) over its retained list"
else
    say_fail "render: the pause menu was presented by the SOFTWARE compositor on every frame"
    say_fail "        (0 retained-frame presents in a run that definitely paused)"
fi

[ "$fail" = 0 ] && echo "pause drop-out: ok" || echo "pause drop-out: FAILED"
exit "$fail"

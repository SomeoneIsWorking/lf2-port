#!/bin/sh
# Two controllers, two players.
#
# This exists because the claim "a second pad is player two" was written down twice with
# nothing behind it, and then RETRACTED on a bad measurement. Both were avoidable:
#
#   1. The feature was documented as working when only one virtual pad had ever been
#      attached, so the slot-assignment code had never run with two.
#   2. The retraction said a second pad drives a COMPUTER's fighter. It does not. On the
#      character-select screen every unjoined slot shows "Join?", and a slot is filled with
#      a computer only once player one PROCEEDS past the screen. The test had pressed after
#      that, so the slot was already taken -- a mistake in the test, read as a defect in the
#      port.
#
#      That it is not a timer was measured, not assumed: with one pad and no further input,
#      slot 2 was still "Join?" at frame 2400, and the word "Computer" was never drawn.
#
# So this asserts the discriminator directly: with a second pad joining in time, the word
# "Computer" must never be drawn, and it IS drawn without one. That is a two-sided check --
# a bare "no Computer" assertion would also pass if the run never reached the screen.
#
# Both runs therefore have to go past the point where player one proceeds, or "no Computer"
# means only "not yet". Two earlier versions of this test stopped too early and the control
# caught both -- which is what the control is for.
set -eu

BUILD=$(cd "${BUILD:-scratch/build}" 2>/dev/null && pwd) || BUILD=${BUILD:-scratch/build}
GAME=$(cd "${GAME:-game}" 2>/dev/null && pwd) || GAME=${GAME:-game}
LOG1=$(mktemp); LOG2=$(mktemp)
trap 'rm -f "$LOG1" "$LOG2"' EXIT

if [ ! -x "$BUILD/lf2" ]; then echo "SKIP: $BUILD/lf2 not built"; exit 77; fi
if [ ! -f "$GAME/lf2.exe" ]; then echo "SKIP: no game tree at $GAME"; exit 77; fi

# Player one walks to character selection and then proceeds, which is what fills the
# remaining slots with computers. The second pad, when present, joins before that.
PAD1="south:900,south:960,south:1020,south:1080,south:1140,south:1200,south:1260,south:1320"
PAD1="$PAD1,up:1380,up:1440,south:1500,south:1700"

run() {   # run <logfile> [second-pad script]
    # LF2_VIRTUAL_PAD2 is exported rather than set inline: `${2:+VAR="$2"}` expands to a
    # single word the shell tries to EXECUTE, not to a variable assignment.
    ( cd "$GAME" && \
      export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
             LF2_TEXT_DEBUG=1 LF2_VIRTUAL_PAD="$PAD1" LF2_QUIT_AFTER=1900 && \
      if [ -n "${2:-}" ]; then export LF2_VIRTUAL_PAD2="$2"; else unset LF2_VIRTUAL_PAD2; fi && \
      timeout 120 "$BUILD/lf2" lf2.exe ) > "$1" 2>&1
}

fail=0

echo "control run: one pad, nobody joins slot 2 (about 60s)..."
run "$LOG1"
if grep -q "^text .*Computer" "$LOG1"; then
    echo "  ok    control: player one proceeded and slot 2 became a computer"
else
    echo "  FAIL  control: no 'Computer' drawn, so this run never reached the screen"
    echo "        the two-pad assertion below would pass for the wrong reason"
    fail=1
fi

echo "two pads, the second joins before player one proceeds (about 60s)..."
# The join frame is a stopwatch, and it had to move when the data load got faster: with
# frame pacing skipped during the load the game reaches character selection sooner, and
# 1360 now lands after player one has already proceeded (the "How many Computer Players?"
# dialog is up by then). Measured window under the current pacing: 1160-1340 all join,
# 1360 does not, so this sits in the middle with margin either side.
run "$LOG2" "south:1250"
if grep -q "^text .*Computer" "$LOG2"; then
    echo "  FAIL  two pads: slot 2 still became a computer -- the second pad did not join"
    fail=1
else
    echo "  ok    two pads: slot 2 was joined, not filled with a computer"
fi

# Both pads must be bound, or "no Computer" could mean the second pad was never seen and
# something else claimed the slot.
if [ "$(grep -c '^controller [01] connected' "$LOG2")" -ge 2 ]; then
    echo "  ok    both pads bound"
else
    echo "  FAIL  both pads bound: $(grep -c '^controller [01] connected' "$LOG2") of 2"
    fail=1
fi

[ "$fail" -eq 0 ] && echo "two-controller test PASSED" || echo "two-controller test FAILED"
exit "$fail"

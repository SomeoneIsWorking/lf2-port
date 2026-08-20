#!/bin/sh
# Controller end-to-end: drive the game from a virtual gamepad and nothing else.
#
# This exists because the mouse-driven smoke test cannot see the controller path at all.
# A gate on the ported menu was once wrong in a way that disabled the port outright -- the
# game just used its original body, everything still worked, and every test stayed green.
# Silence looked identical to success. So this run supplies no keyboard input and no mouse
# clicks: if the pad path is dead, nothing advances and the assertions below fail.
#
# What it covers:
#   attach       -- the pad is attached AFTER startup, which is the hotswap case the stock
#                   game cannot handle (it probes joysticks once and never looks again)
#   mode menu    -- the game's post-load selection moves and activates from the pad
#   input gather -- the ported fn_00419a60 merges the pad into the game's own player
#                   buttons, which is what carries mode select and character selection
#   a match      -- the run goes all the way into VS mode and plays, so the assertions
#                   below cover gameplay input and not just menus
set -eu

BUILD=$(cd "${BUILD:-scratch/build}" 2>/dev/null && pwd) || BUILD=${BUILD:-scratch/build}
GAME=$(cd "${GAME:-game}" 2>/dev/null && pwd) || GAME=${GAME:-game}
LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT

if [ ! -x "$BUILD/lf2" ]; then echo "SKIP: $BUILD/lf2 not built"; exit 77; fi
if [ ! -f "$GAME/lf2.exe" ]; then echo "SKIP: no game tree at $GAME"; exit 77; fi

# The route a person takes, at frame numbers the data load makes possible (it runs to about
# frame 850, so nothing can be pressed before that).
#
# The last part used to be the flaky bit: the pre-fight overlay is Fight! / Reset All /
# Reset Random / Background / Difficulty / Exit, and pressing A blind landed on whichever
# item happened to be selected -- usually Reset Random, which re-rolls the characters and
# stays put. That is now deterministic, because the overlay's selection index was located
# (0x0044d06c, see docs/running.md) and MEASURED to start at 2. Two ups reach Fight!, every
# run.
PAD="south@modemenu+60"        # first visible screen: the mode menu
PAD="$PAD,south@charselect+58,south@charselect+118,south@charselect+178,south@charselect+238,up@charselect+298,up@charselect+358,south@charselect+418"
PAD="$PAD,south@charselect+618,south@charselect+838"   # join, then open the overlay
PAD="$PAD,up@overlay+99,up@overlay+159,south@overlay+219"    # 2 -> 1 -> 0 = Fight!
PAD="$PAD,right@match+108,south@match+158,left@match+218,south@match+278"  # play

echo "driving the game from a virtual gamepad into a match..."
( cd "$GAME" && \
  SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy LF2_UNPACED=1 LF2_RENDERER=soft \
  LF2_SCREEN_HASH=1 LF2_AUDIO_DEBUG=1 LF2_CK_DEBUG=1 \
  LF2_VIRTUAL_PAD="$PAD" LF2_QUIT_AFTER=2200 \
  timeout -k 5 150 "$BUILD/lf2" lf2.exe ) > "$LOG" 2>&1
rc=$?

fail=0
check() {   # check <description> <actual> <minimum>
    if [ "${2:-0}" -ge "$3" ]; then
        echo "  ok    $1: $2 (>= $3)"
    else
        echo "  FAIL  $1: $2 (want >= $3)"
        fail=1
    fi
}

# The counters are cumulative and printed every 900 frames, so take the last line.
in=$(grep "^input:" "$LOG" | tail -1)
num() { echo "${1:-}" | grep -oE "[0-9]+ $2" | head -1 | cut -d' ' -f1; }

if grep -q "^controller 0 connected" "$LOG"; then
    echo "  ok    pad attached after startup (hotswap path)"
else
    echo "  FAIL  pad attached after startup: no 'controller 0 connected' line"
    fail=1
fi

check "player slots with a pad" "$(num "$in" 'of them with a pad')" 100
check "buttons merged"          "$(num "$in" 'button presses merged')" 10
check "screen transitions"      "$(grep -c CHANGED "$LOG" || true)" 4

# Reaching a match is asserted through things that ONLY happen in one, rather than through
# a frame count that would pass while sitting on the character-select screen. Sound effects
# do not fire in the menus, and colour-keyed blits are how fighters are drawn.
au=$(grep "^audio:" "$LOG" | tail -1)
ck=$(grep "^colour-key:" "$LOG" | tail -1)
kv() { echo "${1:-}" | grep -oE "(^|[[:space:]])$2=[0-9]+" | head -1 | cut -d= -f2; }

check "sound effects (a match started)" "$(kv "$au" plays)" 2
check "keyed blits (fighters drawn)"    "$(kv "$ck" 'keyed blits')" 1000

if [ "$rc" -eq 0 ]; then
    echo "  ok    exit status: 0 (clean shutdown)"
else
    echo "  FAIL  exit status: $rc"
    fail=1
fi

[ "$fail" -eq 0 ] && echo "controller test PASSED" || echo "controller test FAILED"
exit "$fail"

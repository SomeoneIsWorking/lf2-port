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
#   front end    -- the ported menu's selection index moves and activates from the pad
#   input gather -- the ported fn_00419a60 merges the pad into the game's own player
#                   buttons, which is what carries mode select and character selection
#   screens      -- reaching character selection needs all of the above to work
set -eu

BUILD=$(cd "${BUILD:-scratch/build}" 2>/dev/null && pwd) || BUILD=${BUILD:-scratch/build}
GAME=$(cd "${GAME:-game}" 2>/dev/null && pwd) || GAME=${GAME:-game}
LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT

if [ ! -x "$BUILD/lf2" ]; then echo "SKIP: $BUILD/lf2 not built"; exit 77; fi
if [ ! -f "$GAME/lf2.exe" ]; then echo "SKIP: no game tree at $GAME"; exit 77; fi

# The data load runs to about frame 850, so the first press cannot come earlier. After that
# it is A to start, seven more to walk mode select / VS mode / the computer-player count,
# then two ups and a last A, which is the same route a person takes.
PAD="south:900,south:960,south:1020,south:1080,south:1140,south:1200,south:1260"
PAD="$PAD,south:1320,up:1380,up:1440,south:1500"

echo "driving the game from a virtual gamepad (about 60s)..."
( cd "$GAME" && \
  SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
  LF2_SCREEN_HASH=1 LF2_VIRTUAL_PAD="$PAD" LF2_QUIT_AFTER=1800 \
  timeout 120 "$BUILD/lf2" lf2.exe ) > "$LOG" 2>&1
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
check "screen transitions"      "$(grep -c CHANGED "$LOG" || true)" 2

if [ "$rc" -eq 0 ]; then
    echo "  ok    exit status: 0 (clean shutdown)"
else
    echo "  FAIL  exit status: $rc"
    fail=1
fi

[ "$fail" -eq 0 ] && echo "controller test PASSED" || echo "controller test FAILED"
exit "$fail"

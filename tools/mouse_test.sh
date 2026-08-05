#!/bin/sh
# Mouse end-to-end: drive the game from the pointer and nothing else.
#
# The counterpart to controller_test.sh, and it exists for the same reason: the smoke test
# drives one click and then a long key script, so every screen after the launcher was
# proved only for the keyboard. The mouse support on those screens could have been dead and
# nothing would have failed -- and part of it WAS. The scripted click path pushed the window
# messages the game reads but never armed the port's own click edge, so `LF2_CLICK_SCRIPT`
# tested hover and never once tested activation.
#
# So this run supplies NO keyboard input and NO pad. Each screen has to hand over to the
# next on a click alone:
#
#   launcher          click "game start"        (the front-end menu's own hit test)
#   mode menu         click "Stage mode"        (modemenu_mouse)
#   character select  click a portrait to join, click again to pick   (charselect_mouse)
#   pre-fight overlay click "Fight!"            (overlay_mouse)
#
# Reaching a match is asserted through things that only happen in one -- sound effects do
# not fire in the menus, and colour-keyed blits are how fighters are drawn -- rather than
# through a frame count, which would pass while sitting on the character-select screen.
#
# What it does NOT assert: that the mode-menu click chose Stage mode specifically. The
# "STAGE 1-1" banner is drawn pixels, not a log line, so there is nothing to grep; a run
# that fell into VS mode instead would pass every check here. Verify that from a frame dump
# when the mode-menu geometry changes.
# Run against both classes before being trusted, not reasoned about: with the click edge
# disabled in win32.c this run stops at the mode menu and reports "screen transitions: 2
# (want >= 3)" and "sound effects: 1 (want >= 2)". With it live it reports 4 and 10.
set -eu

BUILD=$(cd "${BUILD:-scratch/build}" 2>/dev/null && pwd) || BUILD=${BUILD:-scratch/build}
GAME=$(cd "${GAME:-game}" 2>/dev/null && pwd) || GAME=${GAME:-game}
LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT

if [ ! -x "$BUILD/lf2" ]; then echo "SKIP: $BUILD/lf2 not built"; exit 77; fi
if [ ! -f "$GAME/lf2.exe" ]; then echo "SKIP: no game tree at $GAME"; exit 77; fi

# Frame-scheduled, because a millisecond schedule drifts with however long the data load
# takes. The load runs to about frame 850, so nothing can be clicked before that.
#
# Coordinates are the game's own, not measured off a screenshot by eye: the launcher item
# from its hit-test constants, the mode-menu row from the label bands in a frame dump, the
# portrait from the panel rectangles, and "Fight!" from where the game blits the overlay
# highlight when the selection is pinned with LF2_OVERLAY_FORCE.
CLICKS="403,228:900"          # launcher: game start
CLICKS="$CLICKS;400,241:1350" # mode menu: Stage mode (row 1 of 8)
CLICKS="$CLICKS;200,150:1450" # character select: click a portrait to join
CLICKS="$CLICKS;200,150:1600" # and again to pick that fighter, which opens the overlay
CLICKS="$CLICKS;150,28:1750"  # overlay: Fight!

echo "driving the game from the mouse alone into a match (about 90s)..."
( cd "$GAME" && \
  SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
  LF2_SCREEN_HASH=1 LF2_AUDIO_DEBUG=1 LF2_CK_DEBUG=1 \
  LF2_CLICK_SCRIPT="$CLICKS" LF2_QUIT_AFTER=2500 \
  timeout 150 "$BUILD/lf2" lf2.exe ) > "$LOG" 2>&1
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

au=$(grep "^audio:" "$LOG" | tail -1)
ck=$(grep "^colour-key:" "$LOG" | tail -1)
kv() { echo "${1:-}" | grep -oE "(^|[[:space:]])$2=[0-9]+" | head -1 | cut -d= -f2; }

check "screen transitions"              "$(grep -c CHANGED "$LOG" || true)" 3
check "sound effects (a match started)" "$(kv "$au" plays)" 2
check "keyed blits (fighters drawn)"    "$(kv "$ck" 'keyed blits')" 1000

if [ "$rc" -eq 0 ]; then
    echo "  ok    exit status: 0 (clean shutdown)"
else
    echo "  FAIL  exit status: $rc ($([ "$rc" -eq 124 ] && echo 'timed out' || echo 'crashed or aborted'))"
    fail=1
fi

if grep -qE "unimplemented opcode|fell off the end|Aborted" "$LOG"; then
    echo "  FAIL  aborts: found in output"
    grep -E "unimplemented opcode|fell off the end" "$LOG" | head -3
    fail=1
else
    echo "  ok    aborts: none"
fi

[ "$fail" -eq 0 ] && echo "mouse test PASSED" || echo "mouse test FAILED"
exit "$fail"

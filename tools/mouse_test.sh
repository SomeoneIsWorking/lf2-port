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
# WHAT THIS PROVES, and it is less than it used to claim: the mouse alone gets through the
# launcher, the mode menu and into CHARACTER SELECTION, joins a player and answers the
# game's "How many Computer Players?" dialog. It does NOT reach a match, and it no longer
# says it does -- see issue #26.
#
# The old version asserted "sound effects (a match started) >= 2" and passed with four menu
# sounds while the run sat on the character-select screen the whole time. Nothing in the run
# reported which screens it had reached, so a test that never got near a match was green for
# as long as it existed. The assertion below is the screens the run actually reached, which
# is not a threshold anybody can drift under: if the mouse stops driving the launcher or the
# mode menu, `charselect@` never appears and this fails.
#
# VALIDATED AGAINST BOTH CLASSES, run rather than reasoned about:
#   route intact                 -> charselect@1352, 5 of 5 clicks fired, PASSED
#   the click at 1350 broken     -> "screens reached -- NONE", three clicks named as never
#                                   fired, FAILED
# In that failing run "keyed blits" measured 13489 against 6744 in the passing one, so the
# blit threshold this test used to lean on is not merely weak -- it reads HIGHER when the
# route breaks. That is why the assertion is the screens reached and not a count.
#
# A GAP, stated because the negative control found it: breaking the FIRST click (the
# launcher's "game start") does not fail this test. The run still reaches character
# selection, because the click at 1350 lands on the launcher instead and the screen-keyed
# clicks after it follow the game rather than the clock. So this test does not prove the
# launcher click does anything, and nothing here should be read as proving it.
#
# What it does NOT assert: that the mode-menu click chose Stage mode specifically. The
# "STAGE 1-1" banner is drawn pixels, not a log line, so there is nothing to grep; a run
# that fell into VS mode instead would pass every check here. (A frame dump says it lands on
# VS mode, so that comment is aspirational -- the row geometry wants re-measuring.) Verify
# from a frame dump when the mode-menu geometry changes.
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
# The two before any screen exists stay frame-numbered; everything after is keyed to the
# post-load panel, so it lands however long the load took (issues #18, #25).
CLICKS="403,228:900"          # launcher: game start
CLICKS="$CLICKS;400,241:1350" # mode menu: pick a mode
CLICKS="$CLICKS;200,150@charselect+98"   # character select: click a portrait to join
CLICKS="$CLICKS;200,150@charselect+248"  # and again to pick that fighter
CLICKS="$CLICKS;320,293@charselect+398"  # "How many Computer Players?": the digit 1

echo "driving the game from the mouse alone to character selection (about 90s)..."
( cd "$GAME" && \
  SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
  LF2_SCREEN_HASH=1 LF2_AUDIO_DEBUG=1 LF2_CK_DEBUG=1 \
  LF2_CLICK_SCRIPT="$CLICKS" LF2_QUIT_AFTER=2800 \
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

# The discriminating assertion: which screens the run actually reached. A threshold on
# sound effects or blit counts is satisfied by the menus, which is how this test spent its
# whole life green without ever leaving character selection.
screens=$(grep -m1 "^scripted input: screens reached" "$LOG" || true)
if echo "$screens" | grep -q "charselect@"; then
    echo "  ok    reached character selection by mouse alone: $screens"
else
    echo "  FAIL  never reached character selection -- $screens"
    fail=1
fi

# Every click has to land. One aimed at a screen the run never reached is reported by the
# run itself, and it means the assertions above are about inputs that did not happen.
if grep -q "NEVER FIRED" "$LOG"; then
    echo "  FAIL  a scripted click never fired:"
    grep "NEVER FIRED" "$LOG" | sed 's/^/        /'
    fail=1
else
    echo "  ok    every scripted click fired: $(grep -m1 'items fired' "$LOG" || true)"
fi

check "keyed blits (sprites drawn)"     "$(kv "$ck" 'keyed blits')" 1000

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

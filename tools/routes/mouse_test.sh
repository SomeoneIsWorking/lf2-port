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
# WHAT THIS PROVES: the mouse alone gets through the launcher and the mode menu, joins a
# player on character selection, and clicks "Fight!" on the pre-fight overlay to START A
# MATCH. The assertion is the screens the run actually REACHED, which is not a threshold
# anybody can drift under.
#
# It did not always. The first version asserted "sound effects (a match started) >= 2" and
# passed with four menu sounds while the run sat on the character-select screen the whole
# time; nothing in the run reported which screens it had reached, so a test that never got
# near a match was green for as long as it existed (issue #26). In that broken state "keyed
# blits" measured 13489 against 6744 in a working run -- the count it leaned on reads HIGHER
# when the route breaks, so no threshold on it could have discriminated in either direction.
#
# TWO PORT BUGS had to be fixed before the mouse could finish the job, and both were found by
# tracing this route rather than by reading the code:
#
#   THE OVERLAY'S ROW GEOMETRY was a uniform 24 px step from y 16, measured off three sampled
#   highlight blits. Ghidra on FUN_00429730 -- the only function that touches OVERLAY_SEL --
#   gives the rows verbatim as 16, 39, 64, 87, 111, 137, which is not uniform. The three rows
#   the blit measurement happened to sample are the three the uniform step gets nearly right.
#
#   AN IDLE POINTER COUNTED AS A MOVE on the frame a screen opened, because each handler's
#   last-position memory belonged to the handler and not to the screen. The last click on
#   character selection leaves the pointer at (200,150), inside the overlay's panel band: the
#   overlay opened with the game's selection on item 2, the idle pointer was read as a move
#   and dragged it to item 5 -- Exit -- and the next click activated it. The overlay was gone
#   50 frames later and no match ever started.
#
# A GAP, stated because the negative control found it: breaking the FIRST click (the
# launcher's "game start") does not fail this test. The run still reaches character selection,
# because the click at 1350 lands on the launcher instead and the screen-keyed clicks after it
# follow the game rather than the clock. So this test does not prove the launcher click does
# anything, and nothing here should be read as proving it.
#
# What it does NOT assert: which mode the mode-menu click chose. A run that fell into VS mode
# instead of Stage would pass every check here. `tools/e2e.sh stage_mode` is what pins a mode, using
# LF2_MODE.
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
# Three clicks on a portrait: join, pick, and confirm the roster. The game opens the pre-fight
# overlay on the third, so anything scheduled after it lands on the OVERLAY -- which is how an
# earlier version of this route spent eight clicks activating "Exit" and wondered why the
# overlay kept closing. Screen-keyed, so they land however long the load took (issues #18, #25).
CLICKS="$CLICKS;200,150@charselect+98"
CLICKS="$CLICKS;200,150@charselect+248"
CLICKS="$CLICKS;200,150@charselect+398"
# The overlay: move onto "Fight!" (row 0, y 16..38 from the decompile) and click it. Two, so a
# frame lost to the transition does not cost the run.
CLICKS="$CLICKS;150,25@overlay+60"
CLICKS="$CLICKS;150,25@overlay+120"

echo "driving the game from the mouse alone into a match (fast)..."
( cd "$GAME" && \
  SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy LF2_UNPACED=1 \
  LF2_SCREEN_HASH=1 LF2_AUDIO_DEBUG=1 LF2_CK_DEBUG=1 \
  LF2_CLICK_SCRIPT="$CLICKS" LF2_QUIT_AFTER=3200 \
  timeout -k 5 220 "$BUILD/lf2" lf2.exe ) > "$LOG" 2>&1
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
# The screens the run REACHED, in order, each named. Not a threshold: if the mouse stops
# driving any one of these screens, its marker never appears and this fails for the reason it
# is named after.
screens=$(grep -m1 "^scripted input: screens reached" "$LOG" || true)
for want in charselect overlay match; do
    if echo "$screens" | grep -q "$want@"; then
        echo "  ok    reached $want by mouse alone"
    else
        echo "  FAIL  never reached $want -- $screens"
        case "$want" in
        overlay) echo "        (character selection did not hand over: the roster clicks are"
                 echo "         not confirming, or the overlay opened and was dismissed)" ;;
        match)   echo "        (the overlay is up but 'Fight!' is not being clicked -- check the"
                 echo "         row geometry against FUN_00429730's highlight draw)" ;;
        esac
        fail=1
    fi
done

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

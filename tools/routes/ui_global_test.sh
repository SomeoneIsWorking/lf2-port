#!/bin/sh
# The port menu is one global RmlUi shell. Open and close it on every screen class the route
# can name: mode menu, character selection, post-selection overlay, and a running match.
set -eu

BUILD=$(cd "${BUILD:-scratch/build}" 2>/dev/null && pwd) || BUILD=${BUILD:-scratch/build}
GAME=$(cd "${GAME:-game}" 2>/dev/null && pwd) || GAME=${GAME:-game}
OUT=${LF2_SCRATCH:-scratch}/ui_global_test
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
LOG="$OUT/run.log"

if [ ! -x "$BUILD/lf2" ]; then echo "SKIP: $BUILD/lf2 not built"; exit 77; fi
if [ ! -f "$GAME/lf2.exe" ]; then echo "SKIP: no game tree at $GAME"; exit 77; fi

KEY="0x1B@modemenu+20,0x1B@modemenu+60"
PAD="south@modemenu+100"
PAD="$PAD,start@charselect+20,east@charselect+60"
PAD="$PAD,south@charselect+100,south@charselect+160,south@charselect+220,south@charselect+280"
PAD="$PAD,up@charselect+340,up@charselect+400,south@charselect+460"
PAD="$PAD,south@charselect+660,south@charselect+880"
PAD="$PAD,start@overlay+20,east@overlay+60,up@overlay+100,up@overlay+160,south@overlay+220"
PAD="$PAD,start@match+300,east@match+360"

echo "global RmlUi: opening the same shell on every game screen..."
( cd "$GAME" && \
  SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy LF2_UNPACED=1 \
  LF2_KEY_SCRIPT="$KEY" LF2_VIRTUAL_PAD="$PAD" LF2_RMLUI_DEBUG=1 LF2_QUIT_AFTER=2450 \
  timeout -k 5 300 "$BUILD/lf2" lf2.exe ) > "$LOG" 2>&1

line=$(grep '^rmlui: .*settings open(s)' "$LOG" | tail -1 || true)
opens=$(echo "$line" | sed 's/rmlui: \([0-9]*\) settings.*/\1/')
fail=0
if [ "$opens" = 4 ] 2>/dev/null; then
    echo "  ok    one direct RmlUi shell opening on modemenu, charselect, overlay, and match"
else
    echo "  FAIL  expected 4 shell openings, got: ${line:-no RmlUi report}"; fail=1
fi
for screen in modemenu charselect overlay match; do
    if grep -q "screens reached --.*$screen@" "$LOG"; then
        echo "  ok    route reached $screen"
    else
        echo "  FAIL  route never reached $screen"; fail=1
    fi
done
if grep -q 'LF2_KEY_SCRIPT: 2 of 2 items fired' "$LOG" \
   && grep -q 'LF2_VIRTUAL_PAD: 19 of 19 items fired' "$LOG"; then
    echo "  ok    every open, close, and route action fired"
else
    echo "  FAIL  scripted actions were missed"
    grep '^LF2_VIRTUAL_PAD:' "$LOG" | tail -1 || true
    fail=1
fi

[ "$fail" = 0 ] && echo "global RmlUi test PASSED" || echo "global RmlUi test FAILED"
exit "$fail"

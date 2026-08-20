#!/bin/sh
# The RmlUi settings document opens from the real pause menu and renders the shared keyboard
# and gamepad SVGs. This route stops at the document: mapping semantics are covered by the fast
# bindings test, while only a running renderer can prove the document and its assets are live.
set -eu

BUILD=$(cd "${BUILD:-scratch/build}" 2>/dev/null && pwd) || BUILD=${BUILD:-scratch/build}
GAME=$(cd "${GAME:-game}" 2>/dev/null && pwd) || GAME=${GAME:-game}
OUT=${LF2_SCRATCH:-scratch}/settings_test
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
LOG="$OUT/run.log"

if [ ! -x "$BUILD/lf2" ]; then echo "SKIP: $BUILD/lf2 not built"; exit 77; fi
if [ ! -f "$GAME/lf2.exe" ]; then echo "SKIP: no game tree at $GAME"; exit 77; fi

PAD="south@modemenu+60"
PAD="$PAD,south@charselect+58,south@charselect+118,south@charselect+178,south@charselect+238"
PAD="$PAD,up@charselect+298,up@charselect+358,south@charselect+418,south@charselect+618"
PAD="$PAD,south@charselect+838,up@overlay+99,up@overlay+159,south@overlay+219"
# Main pause rows for a normal player: RESUME, LEAVE MATCH, OPTIONS, SETTINGS, QUIT.
PAD="$PAD,start@match+300,down@match+360,down@match+420,down@match+480,south@match+540"

echo "RmlUi settings: opening the mapper and shared device artwork..."
( cd "$GAME" && \
  SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy LF2_UNPACED=1 \
  LF2_VIRTUAL_PAD="$PAD" LF2_RMLUI_DEBUG=1 LF2_QUIT_AFTER=1700 \
  timeout -k 5 300 "$BUILD/lf2" lf2.exe ) > "$LOG" 2>&1

line=$(grep "^rmlui: .*settings open(s)" "$LOG" | tail -1 || true)
if [ -z "$line" ]; then
    echo "  FAIL  no RmlUi shutdown report -- the document was never measured"
    exit 1
fi

opens=$(echo "$line" | sed 's/rmlui: \([0-9]*\) settings.*/\1/')
frames=$(echo "$line" | sed 's/.*open(s), \([0-9]*\) rendered.*/\1/')
textures=$(echo "$line" | sed 's/.*frame(s), \([0-9]*\) shared.*/\1/')
fail=0
if [ "$opens" -ge 1 ] 2>/dev/null; then
    echo "  ok    settings document opened: $opens"
else
    echo "  FAIL  settings document never opened: $line"; fail=1
fi
if [ "$frames" -ge 1 ] 2>/dev/null; then
    echo "  ok    RmlUi rendered while the match stayed paused: $frames frame(s)"
else
    echo "  FAIL  the document opened but rendered no frames: $line"; fail=1
fi
if [ "$textures" -eq 2 ] 2>/dev/null; then
    echo "  ok    keyboard and gamepad SVG textures loaded from shared embedded assets"
else
    echo "  FAIL  expected exactly 2 shared device SVG textures: $line"; fail=1
fi

[ "$fail" = 0 ] && echo "RmlUi settings test PASSED" || echo "RmlUi settings test FAILED"
exit "$fail"

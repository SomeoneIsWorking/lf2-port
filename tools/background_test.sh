#!/bin/sh
# runtime/overrides/background.c draws the stage's layers exactly as the recompiled body did.
#
# fn_0041a250 is now a hand-written override, and the whole value of replacing a recompiled
# function is that the original stays callable and the two can be DIFFED. So this drives the
# same route twice -- LF2_BG_ORIG=1 for the game's own body, unset for the port's -- and
# asserts the dumped frames are byte-identical.
#
# THREE ARMS, not two, because "the two runs agreed" is worthless on its own: a dump that
# would agree no matter what was drawn measures nothing. The third arm sets LF2_BG_SKEW=3,
# which shifts every layer's parallax offset by three pixels, and the test asserts that arm
# DIFFERS from the other two. If it does not, the comparison is blind and the pass is a lie.
#
# The frames are taken during a match, which is the only time a stage is drawn at all, and at
# two different camera positions -- a parallax bug that happens to vanish at one camera is
# exactly the kind this is meant to catch.
set -eu

BUILD=$(cd "${BUILD:-scratch/build}" 2>/dev/null && pwd) || BUILD=${BUILD:-scratch/build}
GAME=$(cd "${GAME:-game}" 2>/dev/null && pwd) || GAME=${GAME:-game}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

if [ ! -x "$BUILD/lf2" ]; then echo "SKIP: $BUILD/lf2 not built"; exit 77; fi
if [ ! -f "$GAME/lf2.exe" ]; then echo "SKIP: no game tree at $GAME"; exit 77; fi

# Into a VS match, then walk right so the later frame has a different camera.
PAD="south:900,south:960,south:1020,south:1080"
PAD="$PAD,south@charselect+58,south@charselect+118,south@charselect+178,south@charselect+238"
PAD="$PAD,up@charselect+298,up@charselect+358,south@charselect+418,south@charselect+618"
PAD="$PAD,south@charselect+838,up@overlay+99,up@overlay+159,south@overlay+219"
i=60
while [ "$i" -le 600 ]; do PAD="$PAD,right@match+$i"; i=$((i + 30)); done

FRAMES=2250,2700

# One arm. Everything but the dump directory and the one variable under test is identical
# across the three, which is what makes a byte difference attributable.
arm() {   # arm <dir> [VAR=value ...]
    mkdir -p "$OUT/$1"
    dir=$1; shift
    ( cd "$GAME" && \
      env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
          LF2_VIRTUAL_PAD="$PAD" LF2_WINDOW_SIZE=1600x550 \
          LF2_FRAME_DUMP="$FRAMES" LF2_DUMP_DIR="$OUT/$dir" \
          LF2_QUIT_AFTER=2750 "$@" \
          timeout 300 "$BUILD/lf2" lf2.exe ) >/dev/null 2>&1 || true
}

echo "background override vs the recompiled body: three runs, about 6 minutes..."
arm port
arm orig LF2_BG_ORIG=1
arm skew LF2_BG_SKEW=3

fail=0
frames=$(ls "$OUT/port" 2>/dev/null | wc -l)
if [ "$frames" -eq 0 ]; then
    echo "  FAIL  the port arm produced NO frame dumps -- the route never reached a match,"
    echo "        so nothing was compared. This is not a pass."
    exit 1
fi

for f in "$OUT/port"/*; do
    n=$(basename "$f")
    if [ ! -f "$OUT/orig/$n" ]; then
        echo "  FAIL  $n: the LF2_BG_ORIG arm produced no such frame"; fail=1; continue
    fi
    if cmp -s "$f" "$OUT/orig/$n"; then
        echo "  ok    $n: the override and the recompiled body drew the same bytes"
    else
        echo "  FAIL  $n: the override and the recompiled body DIFFER"
        echo "        first differing byte: $(cmp "$f" "$OUT/orig/$n" 2>&1 | head -1)"
        fail=1
    fi
    if [ ! -f "$OUT/skew/$n" ]; then
        echo "  FAIL  $n: the LF2_BG_SKEW arm produced no such frame, so the comparison"
        echo "        above has NOT been shown able to fail"; fail=1; continue
    fi
    if cmp -s "$f" "$OUT/skew/$n"; then
        echo "  FAIL  $n: a 3-pixel parallax shift produced an IDENTICAL dump -- the frames"
        echo "        being compared do not contain the background, so the match above"
        echo "        proves nothing"
        fail=1
    else
        echo "  ok    $n: a 3-pixel parallax shift does change the dump, so the check can fail"
    fi
done

[ "$fail" = 0 ] && echo "background override: ok ($frames frame(s) compared)" \
                || echo "background override: FAILED"
exit $fail

#!/bin/sh
# runtime/overrides/background.c: faithful at the game's own width, widened only above it.
#
# fn_0041a250 is a hand-written override, and the value of replacing a recompiled function is
# that the original stays callable and the two can be DIFFED. Both halves of the contract are
# asserted here, because each is worthless without the other:
#
#   at 794x550    the override must be BYTE-IDENTICAL to the recompiled body. This is what
#                 says the reimplementation is faithful -- the parallax, the loop-or-once
#                 split, the cc/c1/c2 animation window, the colour fills, all of it.
#   at 1600x550   it must DIFFER. The widescreen change is a real change; an arm that came
#                 out identical would mean the view width never reached the layer pass and
#                 the fix does nothing.
#
# Plus a negative control at 794: LF2_BG_SKEW=3 shifts every parallax offset by three pixels
# and must change the dump. Without it, "the two agreed" is indistinguishable from "these
# frames do not contain the background at all", and the identity above would prove nothing.
#
# Frames are taken during a match at two different camera positions -- a parallax bug that
# happens to vanish at one camera is exactly what this is meant to catch.
set -eu

BUILD=$(cd "${BUILD:-scratch/build}" 2>/dev/null && pwd) || BUILD=${BUILD:-scratch/build}
GAME=$(cd "${GAME:-game}" 2>/dev/null && pwd) || GAME=${GAME:-game}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

if [ ! -x "$BUILD/lf2" ]; then echo "SKIP: $BUILD/lf2 not built"; exit 77; fi
if [ ! -f "$GAME/lf2.exe" ]; then echo "SKIP: no game tree at $GAME"; exit 77; fi

# Into a VS match, then walk right so the later frame has a different camera -- and so the
# right-hand wall is actually reached, which is the case issue #28 is about.
PAD="south:900,south:960,south:1020,south:1080"
PAD="$PAD,south@charselect+58,south@charselect+118,south@charselect+178,south@charselect+238"
PAD="$PAD,up@charselect+298,up@charselect+358,south@charselect+418,south@charselect+618"
PAD="$PAD,south@charselect+838,up@overlay+99,up@overlay+159,south@overlay+219"
i=60
while [ "$i" -le 600 ]; do PAD="$PAD,right@match+$i"; i=$((i + 30)); done

FRAMES=2250,2700

arm() {   # arm <dir> <window> [VAR=value ...]
    dir=$1; win=$2; shift 2
    mkdir -p "$OUT/$dir"
    ( cd "$GAME" && \
      env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
          LF2_VIRTUAL_PAD="$PAD" LF2_WINDOW_SIZE="$win" \
          LF2_FRAME_DUMP="$FRAMES" LF2_DUMP_DIR="$OUT/$dir" \
          LF2_QUIT_AFTER=2750 "$@" \
          timeout 300 "$BUILD/lf2" lf2.exe ) >/dev/null 2>&1 || true
}

echo "background override: five runs, about 10 minutes..."
arm native_port 794x550
arm native_orig 794x550  LF2_BG_ORIG=1
arm native_skew 794x550  LF2_BG_SKEW=3
arm wide_port   1600x550
arm wide_orig   1600x550 LF2_BG_ORIG=1

fail=0
frames=$(ls "$OUT/native_port" 2>/dev/null | wc -l)
if [ "$frames" -eq 0 ]; then
    echo "  FAIL  the native arm produced NO frame dumps -- the route never reached a match,"
    echo "        so nothing was compared. This is not a pass."
    exit 1
fi

# $1 label, $2 dir a, $3 dir b, $4 same|differ, $5 what a failure means
check() {
    for f in "$OUT/$2"/*; do
        n=$(basename "$f")
        if [ ! -f "$OUT/$3/$n" ]; then
            echo "  FAIL  $1 $n: the $3 arm produced no such frame"; fail=1; continue
        fi
        if cmp -s "$f" "$OUT/$3/$n"; then got=same; else got=differ; fi
        if [ "$got" = "$4" ]; then
            echo "  ok    $1 $n: $4"
        else
            echo "  FAIL  $1 $n: expected $4, got $got"
            echo "        $5"
            fail=1
        fi
    done
}

check "native identity  " native_port native_orig same \
      "the override does not draw what the recompiled body drew at the game's own width"
check "native control   " native_port native_skew differ \
      "a 3-pixel parallax shift changed nothing, so these frames do not contain the
        background and the identity check above proves nothing"
check "widescreen change" wide_port wide_orig differ \
      "at 1600x550 the override drew exactly what the unwidened body drew, so the view
        width never reached the layer pass and issues #23/#28 are not fixed"

[ "$fail" = 0 ] && echo "background override: ok ($frames frame(s) per arm)" \
                || echo "background override: FAILED"
exit $fail

#!/bin/sh
# The stage's OBJECT PASS (fn_0041a5a0) draws the same frame every time, and a change to it
# would show.
#
# WHY THIS EXISTS, AND WHY IT EXISTS BEFORE THE THING IT GUARDS. Issue #55 needs fn_0041a5a0
# hand-ported: it is the pass that draws every fighter, their shadows, their name tags and
# their effects, and it clamps a name tag into the game's own 794-wide screen at four
# `MOV r32,0x31a` sites, so in a wide view the tag freezes 184 px early while the fighter
# walks on (claim C025, measured). 0x31a is an immediate in recompiled code, so no memory
# write reaches it -- the fix is a port, and the port needs an acceptance gate.
#
# That gate is byte-identity against the recompiled body at a 794 view, the shape
# tools/routes/background_test.sh already uses for the layer pass with LF2_BG_ORIG. This file
# is that gate, built FIRST, because a gate written after the change it is meant to catch is a
# gate nobody has ever seen fail.
#
# THREE ARMS, and the third is the point:
#
#   determinism   two default runs, same frames, byte-identical. The identity arm the port
#                 will use depends entirely on this being true, and it has never been asserted
#                 anywhere -- the byte-identity in background_test compares two DIFFERENT code
#                 paths and would not notice a pass that simply varied run to run.
#   skew          LF2_OBJ_SKEW=3 moves the pass's camera by 3, so every object it draws moves
#                 3 px and nothing else in the frame does. This MUST differ. It is what proves
#                 the comparison above can report a difference at all.
#   scale         the skew must be small but not trivial -- a difference of a handful of pixels
#                 is reported with its pixel count, so a build where the skew moved only a
#                 sliver cannot pass as a working negative.
#
# WHEN THE PORT LANDS: change the `orig` arm below from a second default run to
# LF2_OBJ_ORIG=1, and this becomes the real gate. Until then the first arm is a determinism
# check and says so rather than claiming to have compared the port with anything.
#
# Software renderer throughout: this is about what the GAME's pass draws, not about how the
# frame is presented, and issue #40 is why nothing runs on the GPU that does not have to.
set -eu

BUILD=$(cd "${BUILD:-scratch/build}" 2>/dev/null && pwd) || BUILD=${BUILD:-scratch/build}
GAME=$(cd "${GAME:-game}" 2>/dev/null && pwd) || GAME=${GAME:-game}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

if [ ! -x "$BUILD/lf2" ]; then echo "SKIP: $BUILD/lf2 not built"; exit 77; fi
if [ ! -f "$GAME/lf2.exe" ]; then echo "SKIP: no game tree at $GAME"; exit 77; fi
python3 -c "" 2>/dev/null || { echo "SKIP: no python3 to read the frame dumps"; exit 77; }

# Anchored, not counted (issue #57): a frame with fighters standing in the stage, which is the
# only kind of frame this pass draws anything interesting into.
FRAMES=@match+282,@match+732
PAD="south@frontend+0,south@frontend+60,south@frontend+120,south@frontend+180"
PAD="$PAD,south@charselect+58,south@charselect+118,south@charselect+178,south@charselect+238"
PAD="$PAD,up@charselect+298,up@charselect+358,south@charselect+418,south@charselect+618"
PAD="$PAD,south@charselect+838,up@overlay+99,up@overlay+159,south@overlay+219"
i=20
while [ "$i" -le 600 ]; do PAD="$PAD,right@match+$i"; i=$((i + 30)); done

arm() {   # arm <dir> [VAR=value ...]
    dir=$1; shift
    mkdir -p "$OUT/$dir"
    ( cd "$GAME" && \
      env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy LF2_UNPACED=1 LF2_RENDERER=soft \
          LF2_VIRTUAL_PAD="$PAD" LF2_WINDOW_SIZE=794x550 \
          LF2_FRAME_DUMP="$FRAMES" LF2_DUMP_DIR="$OUT/$dir" \
          LF2_QUIT_AFTER=1910 "$@" \
          timeout -k 5 300 "$BUILD/lf2" lf2.exe ) >/dev/null 2>&1 || true
}

echo "the stage's object pass: three runs..."
arm port
arm orig
arm skew LF2_OBJ_SKEW=3

fail=0
FRAMES_N=$(printf '%s' "$FRAMES" | tr ',' '\n' | grep -c '[^ ]')
n=$(ls "$OUT/port" 2>/dev/null | wc -l)
if [ "$n" -ne "$FRAMES_N" ]; then
    echo "  FAIL  the port arm dumped $n of the $FRAMES_N requested frame(s) ($FRAMES) -- the"
    echo "        route did not reach them, so NOTHING was compared. This is not a pass."
    exit 1
fi

# How many pixels differ, so a pass and a failure both carry a number rather than a verdict.
diff_px() {
    python3 - "$1" "$2" <<'PY'
import sys
def read(p):
    d = open(p, 'rb').read()
    f = d.split(b'\n', 3)
    return f[3] if len(f) > 3 else b''
a, b = read(sys.argv[1]), read(sys.argv[2])
if len(a) != len(b) or not a:
    print("ERR"); raise SystemExit
print(sum(1 for i in range(0, len(a), 3) if a[i:i+3] != b[i:i+3]))
PY
}

for f in "$OUT/port"/*.ppm; do
    [ -e "$f" ] || continue
    nm=$(basename "$f")

    if [ ! -f "$OUT/orig/$nm" ]; then
        echo "  FAIL  $nm: the orig arm produced no such frame"; fail=1; continue
    fi
    if cmp -s "$f" "$OUT/orig/$nm"; then
        echo "  ok    $nm: two default runs are byte-identical, so the pass is deterministic"
        echo "        and an identity comparison against it means something"
    else
        d=$(diff_px "$f" "$OUT/orig/$nm")
        echo "  FAIL  $nm: two DEFAULT runs differ on $d pixel(s). The pass is not"
        echo "        reproducible frame for frame, so no byte-identity gate can be built on"
        echo "        it -- fix that before porting anything (issue #55)"
        fail=1
    fi

    if [ ! -f "$OUT/skew/$nm" ]; then
        echo "  FAIL  $nm: the skew arm produced no such frame"; fail=1; continue
    fi
    d=$(diff_px "$f" "$OUT/skew/$nm")
    if [ "$d" = "ERR" ]; then
        echo "  FAIL  $nm: skew compare: the two dumps are not the same size"; fail=1; continue
    fi
    if [ "$d" -gt 500 ]; then
        echo "  ok    $nm: moving the pass's camera by 3 changes $d pixel(s), so the"
        echo "        comparison above can report a difference"
    else
        echo "  FAIL  $nm: with the object pass's camera moved by 3, only $d pixel(s)"
        echo "        changed. Either the pass drew nothing into this frame or the skew is"
        echo "        not reaching it -- either way the identity arm above is vacuous and"
        echo "        must not be read as a pass"
        fail=1
    fi
done

[ "$fail" = 0 ] && echo "object pass: ok ($FRAMES_N frame(s) per arm)" \
                || echo "object pass: FAILED"
exit $fail

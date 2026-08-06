#!/bin/sh
# The native renderer draws the same frame the software compositor does (issue #30).
#
# runtime/render.c records the game's draws as a display list and draws them as GPU geometry;
# runtime/ddraw.c's software blitter still composes every frame. Both build every frame and
# LF2_RENDERER=soft chooses which is presented, so the two can be diffed -- which is the whole
# reason the software path was kept.
#
# TOLERANCE, and why it is not zero. Antialiased text is composited by the CPU path as
# `ink*a + bg*(255-a)` against whatever was already there, and by the GPU path as a
# PREMULTIPLIED tile, `ink*a/255` written once and then blended. Those are the same function
# with the rounding in a different place, so glyph edges land within a couple of levels of
# each other. Everything else -- every sprite, every layer, every fill -- is expected exact.
# The test therefore asserts a MAXIMUM per-channel difference, not a pixel count, because a
# large error on one pixel matters and a 1-level error on a thousand does not.
#
# THREE ARMS, because "the two frames matched" is worth nothing on its own. This comparison
# has already been fooled once: the readback ran BEFORE the frame was drawn, so the dump was
# the previous GPU frame, and with the camera scrolling a pixel a frame that looked like a
# clean one-pixel shift rather than the ordering bug it was. So the third arm draws the frame
# deliberately WRONG (LF2_RENDER_SKIP=7 drops every seventh entry) and must come out
# different -- if it does not, the comparison is reading one buffer twice.
set -eu

BUILD=$(cd "${BUILD:-scratch/build}" 2>/dev/null && pwd) || BUILD=${BUILD:-scratch/build}
GAME=$(cd "${GAME:-game}" 2>/dev/null && pwd) || GAME=${GAME:-game}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

if [ ! -x "$BUILD/lf2" ]; then echo "SKIP: $BUILD/lf2 not built"; exit 77; fi
if [ ! -f "$GAME/lf2.exe" ]; then echo "SKIP: no game tree at $GAME"; exit 77; fi
python3 -c "" 2>/dev/null || { echo "SKIP: no python3 to read the frame dumps"; exit 77; }

# Character selection (1300) and a match (2250): a menu is flat panels and text, a match is
# scrolling parallax layers, keyed sprites and the HUD. A renderer can be wrong on one and
# right on the other.
FRAMES=1300,2250
PAD="south:900,south:960,south:1020,south:1080"
PAD="$PAD,south@charselect+58,south@charselect+118,south@charselect+178,south@charselect+238"
PAD="$PAD,up@charselect+298,up@charselect+358,south@charselect+418,south@charselect+618"
PAD="$PAD,south@charselect+838,up@overlay+99,up@overlay+159,south@overlay+219"

arm() {   # arm <dir> [VAR=value ...]
    dir=$1; shift
    mkdir -p "$OUT/$dir"
    ( cd "$GAME" && \
      env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
          LF2_VIRTUAL_PAD="$PAD" LF2_FRAME_DUMP="$FRAMES" LF2_DUMP_DIR="$OUT/$dir" \
          LF2_QUIT_AFTER=2300 "$@" \
          timeout 300 "$BUILD/lf2" lf2.exe ) >/dev/null 2>&1 || true
}

# The GEOMETRY arms run with the HD2D pass off: this comparison is about whether the renderer
# draws the same picture, and a bloom would swamp it. The pass gets its own arm below, which
# has to DIFFER -- an effect that changes nothing is an effect that is not running, and that
# is the failure a "looks fine to me" check would never catch.
echo "native renderer vs the software compositor: four runs, about 8 minutes..."
arm soft LF2_RENDERER=soft
arm gpu  LF2_HD2D=off
arm skip LF2_HD2D=off LF2_RENDER_SKIP=7
arm hd2d

# "<maxdiff> <differing> <total>" for two PPMs, or "ERR ..." -- never silence.
cmp_ppm() {
    python3 - "$1" "$2" <<'PY'
import sys
def read(p):
    d = open(p, 'rb').read()
    if not d.startswith(b'P6'): return None
    tok, i = [], 2
    while len(tok) < 3:
        while i < len(d) and d[i:i+1].isspace(): i += 1
        if d[i:i+1] == b'#':
            while d[i:i+1] != b'\n': i += 1
            continue
        j = i
        while j < len(d) and not d[j:j+1].isspace(): j += 1
        tok.append(int(d[i:j])); i = j
    return tok[0], tok[1], d[i+1:i+1+tok[0]*tok[1]*3]
a, b = read(sys.argv[1]), read(sys.argv[2])
if not a or not b: print("ERR not-ppm"); raise SystemExit
if a[0] != b[0] or a[1] != b[1]: print("ERR size-mismatch"); raise SystemExit
pa, pb = a[2], b[2]
if len(pa) != len(pb): print("ERR short-read"); raise SystemExit
mx = n = 0
for i in range(0, len(pa), 3):
    d = max(abs(pa[i]-pb[i]), abs(pa[i+1]-pb[i+1]), abs(pa[i+2]-pb[i+2]))
    if d:
        n += 1
        if d > mx: mx = d
print(mx, n, a[0]*a[1])
PY
}

TOL=4
fail=0
frames_done=0
for f in "$OUT/soft"/*.ppm; do
    [ -e "$f" ] || continue
    n=$(basename "$f")
    frames_done=$((frames_done + 1))
    for arm_dir in gpu skip; do
        if [ ! -f "$OUT/$arm_dir/$n" ]; then
            echo "  FAIL  $n: the $arm_dir arm produced no such frame"; fail=1
        fi
    done
    [ -f "$OUT/gpu/$n" ] && [ -f "$OUT/skip/$n" ] || continue

    set -- $(cmp_ppm "$f" "$OUT/gpu/$n")
    if [ "$1" = "ERR" ]; then echo "  FAIL  $n: gpu compare: $2"; fail=1; continue; fi
    gmax=$1; gn=$2; gtot=$3
    set -- $(cmp_ppm "$f" "$OUT/skip/$n")
    if [ "$1" = "ERR" ]; then echo "  FAIL  $n: skip compare: $2"; fail=1; continue; fi
    smax=$1; sn=$2

    if [ "$gmax" -le "$TOL" ]; then
        echo "  ok    $n: gpu matches software (max channel diff $gmax, $gn/$gtot px differ)"
    else
        echo "  FAIL  $n: gpu differs from software by $gmax levels on $gn/$gtot pixels"
        echo "        (tolerance $TOL, which covers premultiplied rounding on glyph edges"
        echo "        and nothing else)"
        fail=1
    fi

    # The negative. A frame drawn with every 7th entry dropped must be visibly different --
    # both in magnitude and in area, so that a tiny incidental difference cannot satisfy it.
    if [ "$smax" -gt "$TOL" ] && [ "$sn" -gt 1000 ]; then
        echo "  ok    $n: dropping every 7th draw changes $sn px by up to $smax, so the"
        echo "        comparison above can fail"
    else
        echo "  FAIL  $n: with every 7th draw DROPPED the frame still matched software"
        echo "        (max $smax over $sn px) -- these two dumps are not the two renderers,"
        echo "        so the match above proves nothing"
        fail=1
    fi

    # The HD2D pass must change the picture, over a wide area and by a visible amount. This
    # checks that it RAN; it does not check that it looks right, and nothing here can -- a
    # bloom composited with the wrong blend mode would still pass this. What it does catch is
    # the failure that would otherwise be silent: render targets that were never created, so
    # the pass quietly did nothing and the picture stayed the plain composition.
    if [ -f "$OUT/hd2d/$n" ]; then
        set -- $(cmp_ppm "$OUT/gpu/$n" "$OUT/hd2d/$n")
        if [ "$1" = "ERR" ]; then echo "  FAIL  $n: hd2d compare: $2"; fail=1; continue; fi
        hmax=$1; hn=$2
        if [ "$hmax" -gt 4 ] && [ "$hn" -gt 10000 ]; then
            echo "  ok    $n: the HD2D pass changes $hn px by up to $hmax, so it is running"
        else
            echo "  FAIL  $n: the HD2D pass changed $hn px by up to $hmax -- it is not"
            echo "        running, or its render targets were never created"
            fail=1
        fi
    else
        echo "  FAIL  $n: the hd2d arm produced no such frame"; fail=1
    fi
done

if [ "$frames_done" -eq 0 ]; then
    echo "  FAIL  the software arm produced no frame dumps at all -- the route never reached"
    echo "        the frames under test, so NOTHING was compared. This is not a pass."
    exit 1
fi

[ "$fail" = 0 ] && echo "native renderer: ok ($frames_done frame(s) compared)" \
                || echo "native renderer: FAILED"
exit $fail

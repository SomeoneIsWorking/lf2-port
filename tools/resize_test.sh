#!/bin/sh
# A window resize must not leave the previous size's pixels standing (issue #29).
#
# The composition is copied to the primary in ONE blit whose source is the whole compose
# surface, with the centring offset added to its destination -- so the copy hangs off the
# right and never writes the leftmost `offset` columns. At a steady size those columns are
# black because the primary started black; after a resize they hold a ghost of the previous,
# differently-centred screen. runtime/ddraw.c clears the primary when that geometry moves.
#
# THE CHECK is that the band to the left of the centred screen is entirely black in a frame
# taken after a resize. On its own that assertion is nearly worthless -- a frame that is black
# EVERYWHERE would pass it -- so the test also requires:
#
#   a) the frame is not blank: the centred screen itself must have plenty of non-black pixels
#   b) LF2_PRIMARY_STALE=1, which disables the clear, must FAIL the band check
#
# Without (b) this would still pass on a build where the clear was deleted, because the ghost
# only appears when the size actually changed and a broken test never notices.
#
# The route shrinks the window and grows it again while character selection is up, which is
# the case that was reported with a screenshot.
set -eu

BUILD=$(cd "${BUILD:-scratch/build}" 2>/dev/null && pwd) || BUILD=${BUILD:-scratch/build}
GAME=$(cd "${GAME:-game}" 2>/dev/null && pwd) || GAME=${GAME:-game}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

if [ ! -x "$BUILD/lf2" ]; then echo "SKIP: $BUILD/lf2 not built"; exit 77; fi
if [ ! -f "$GAME/lf2.exe" ]; then echo "SKIP: no game tree at $GAME"; exit 77; fi
python3 -c "" 2>/dev/null || { echo "SKIP: no python3 to read the frame dumps"; exit 77; }

# 1900x800 -> composition 1306x550, so the centred 794-wide screen sits at x 256..1050 and
# the band under test is x 0..255. Shrink to 1200x800 (composition 825, offset 15) and back,
# so the band holds pixels written at a DIFFERENT offset.
FRAME=1550

# PINNED TO THE SOFTWARE COMPOSITOR, and that is not a convenience. What this test guards is
# primary_clear_on_move(), which belongs to the software present: the centring offset is added
# to a full-width copy INTO THE PRIMARY, so the leftmost `offset` columns are never written and
# keep the previous size's pixels. The native renderer cannot have that bug -- it draws into a
# render target that is cleared every frame -- so under it the LF2_PRIMARY_STALE arm comes out
# clean and the test loses its negative. It said so and failed rather than reporting a pass it
# could not justify, which is exactly what the third arm is for.
arm() {   # arm <dir> [VAR=value ...]
    dir=$1; shift
    mkdir -p "$OUT/$dir"
    ( cd "$GAME" && \
      env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
          LF2_RENDERER=soft \
          LF2_VIRTUAL_PAD="south:900,south:960,south:1020,south:1080" \
          LF2_WINDOW_SIZE=1900x800 \
          LF2_WINDOW_RESIZE="1400:1200x800,1500:1900x800" \
          LF2_FRAME_DUMP="$FRAME" LF2_DUMP_DIR="$OUT/$dir" \
          LF2_QUIT_AFTER=1650 "$@" \
          timeout 200 "$BUILD/lf2" lf2.exe ) >/dev/null 2>&1 || true
}

echo "resize leaves no stale pixels: two runs, about 2 minutes..."
arm clean
arm stale LF2_PRIMARY_STALE=1

# Reports both numbers every time, so a failure says how much ghost there was and a pass says
# how much picture it was measured against. "0 stray pixels" out of a blank frame is not a
# pass and this prints enough to tell the difference.
band() {   # band <ppm> -> "<stray-in-band> <lit-in-screen> <width>"
    python3 - "$1" <<'PY'
import sys
d = open(sys.argv[1], 'rb').read()
if not d.startswith(b'P6'):
    print("ERR not-ppm"); raise SystemExit
tok, i = [], 2
while len(tok) < 3:
    while i < len(d) and d[i:i+1].isspace(): i += 1
    if d[i:i+1] == b'#':
        while d[i:i+1] != b'\n': i += 1
        continue
    j = i
    while j < len(d) and not d[j:j+1].isspace(): j += 1
    tok.append(int(d[i:j])); i = j
i += 1
w, h = tok[0], tok[1]
px = d[i:i + w*h*3]
off = (w - 794) // 2
if off <= 0:
    print("ERR no-offset"); raise SystemExit
stray = lit = 0
for y in range(h):
    row = px[y*w*3:(y+1)*w*3]
    for x in range(off):
        if row[x*3] or row[x*3+1] or row[x*3+2]: stray += 1
    for x in range(off, min(off+794, w)):
        if row[x*3] or row[x*3+1] or row[x*3+2]: lit += 1
print(stray, lit, w)
PY
}

f_clean="$OUT/clean/frame_00$FRAME.ppm"
f_stale="$OUT/stale/frame_00$FRAME.ppm"
fail=0
for f in "$f_clean" "$f_stale"; do
    if [ ! -f "$f" ]; then
        echo "  FAIL  $f was never written -- the route did not reach frame $FRAME, so"
        echo "        NOTHING was measured. This is not a pass."
        exit 1
    fi
done

set -- $(band "$f_clean")
clean_stray=$1; clean_lit=$2; width=$3
set -- $(band "$f_stale")
stale_stray=$1

if [ "$clean_lit" -lt 20000 ]; then
    echo "  FAIL  the centred screen has only $clean_lit lit pixels in a ${width}px frame --"
    echo "        this frame is blank, so 'the band is black' would pass trivially"
    fail=1
else
    echo "  ok    the frame has picture in it ($clean_lit lit pixels in the centred screen)"
fi

if [ "$clean_stray" -eq 0 ]; then
    echo "  ok    after the resize the band left of the screen is entirely black"
else
    echo "  FAIL  $clean_stray non-black pixels survive left of the centred screen -- the"
    echo "        previous size's picture is still standing there (issue #29)"
    fail=1
fi

if [ "$stale_stray" -gt 0 ]; then
    echo "  ok    with the clear disabled the band holds $stale_stray stray pixels, so the"
    echo "        check above can fail"
else
    echo "  FAIL  with LF2_PRIMARY_STALE=1 the band came out clean too, so this test cannot"
    echo "        detect the bug it exists for and its pass means nothing"
    fail=1
fi

[ "$fail" = 0 ] && echo "resize: ok" || echo "resize: FAILED"
exit $fail

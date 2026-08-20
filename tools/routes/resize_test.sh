#!/bin/sh
# A window resize must not leave the previous size's pixels standing (issue #29).
#
# WHAT GUARANTEES THAT CHANGED, and the test with it. The composition used to be copied to the
# primary with the centring offset added to its DESTINATION, so the copy hung off the right and
# never wrote the leftmost `offset` columns. At a steady size those were black because the
# primary started black; after a resize they held a ghost of the previous, differently-centred
# screen, and runtime/video/ddraw.c cleared the primary whenever that geometry moved.
#
# Issue #42 moved the centring into the composition -- draws that fit inside the game's own
# 794-wide screen are shifted as they are composed -- so the copy to the primary is 1:1 and
# covers every column of it. The ghost is gone by CONSTRUCTION: nothing is left unwritten.
# The clear, and the flag that used to disable it, were both dead, and this test went red
# saying its negative arm could no longer fail. That is the third arm doing its job.
#
# THE CHECK is still that the band to the left of the centred screen is entirely black in a
# frame taken after a resize. On its own that assertion is nearly worthless -- a frame that is
# black EVERYWHERE would pass it -- so the test also requires:
#
#   a) the frame is not blank: the centred screen itself must have plenty of non-black pixels
#   b) LF2_PRIMARY_STALE=1 must FAIL the band check. It is now a DEFECT INJECTOR rather than
#      the disabling of a fix: it leaves the leftmost 64 columns of the primary unwritten by
#      the copy, which is precisely the shape of the old bug, and those columns then hold the
#      previous size's picture.
#
# Without (b) this would still pass on a build where the copy stopped covering the primary,
# because the ghost only appears when the size actually changed and a broken test never
# notices.
#
# The route shrinks the window and grows it again while character selection is up, which is
# the case that was reported with a screenshot. Character selection has no full-screen colour
# fill of its own -- unlike the front end, whose background now spans the whole composition by
# design (issue #42) -- so the band beside it is genuinely black and the check still means
# what it says.
set -eu

BUILD=$(cd "${BUILD:-scratch/build}" 2>/dev/null && pwd) || BUILD=${BUILD:-scratch/build}
GAME=$(cd "${GAME:-game}" 2>/dev/null && pwd) || GAME=${GAME:-game}
# NOT mktemp -d, and the frames are NOT deleted on the way out. Two separate reasons, both
# learned the hard way:
#   /tmp here is a RAM-backed tmpfs with a per-user quota, and these dumps are ~1.3 MB a frame
#   per arm -- the project's rule is that run artefacts go to the gitignored scratch/, which is
#   on the real disk.
#   And a route that deletes its evidence on EXIT makes a failure unexaminable: the one thing
#   anybody wants after a failed frame comparison is the two frames it compared. They are
#   cleared at the START of the next run instead, so the last run's frames are always there.
OUT=${LF2_SCRATCH:-scratch}/resize_test
tools/build/scratch_clean.sh "$OUT"
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)          # absolute: each arm runs with cwd inside the game tree

if [ ! -x "$BUILD/lf2" ]; then echo "SKIP: $BUILD/lf2 not built"; exit 77; fi
if [ ! -f "$GAME/lf2.exe" ]; then echo "SKIP: no game tree at $GAME"; exit 77; fi
python3 -c "" 2>/dev/null || { echo "SKIP: no python3 to read the frame dumps"; exit 77; }

# 1900x800 -> composition 1306x550, so the centred 794-wide screen sits at x 256..1050 and
# the band under test is x 0..255. Shrink to 1200x800 (composition 825, offset 15) and back,
# so the band holds pixels written at a DIFFERENT offset.
FRAME=710

# PINNED TO THE SOFTWARE COMPOSITOR, and that is not a convenience. What this test guards is a
# property of the software present -- that its one copy covers every column of the primary --
# and the injector that gives it a negative acts on that copy. The native renderer cannot have
# the bug at all: it draws into a render target that is cleared every frame. Under it the
# LF2_PRIMARY_STALE arm comes out clean and the test loses its negative, which it reports as a
# failure rather than as a pass it could not justify.
arm() {   # arm <dir> [VAR=value ...]
    dir=$1; shift
    mkdir -p "$OUT/$dir"
    ( cd "$GAME" && \
      env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy LF2_UNPACED=1 \
          LF2_RENDERER=soft \
LF2_VIRTUAL_PAD="south@modemenu+60" \
          LF2_WINDOW_SIZE=1900x800 \
          LF2_WINDOW_RESIZE="560:1200x800,660:1900x800" \
          LF2_FRAME_DUMP="$FRAME" LF2_DUMP_DIR="$OUT/$dir" \
          LF2_QUIT_AFTER=810 "$@" \
          timeout -k 5 200 "$BUILD/lf2" lf2.exe ) >/dev/null 2>&1 || true
}

echo "resize leaves no stale pixels: two runs..."
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

# The dumper pads to six digits; building the name as frame_00$FRAME only happened to work
# while FRAME was four digits, and silently became "never written" -- a FAILURE that reads
# like the route regressed -- the moment the route got faster and the frame got shorter.
FRAME_FILE=$(printf "frame_%06d.ppm" "$FRAME")
f_clean="$OUT/clean/$FRAME_FILE"
f_stale="$OUT/stale/$FRAME_FILE"
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
    echo "  ok    with the copy leaving that band unwritten it holds $stale_stray stray pixels, so the"
    echo "        check above can fail"
else
    echo "  FAIL  with LF2_PRIMARY_STALE=1 the band came out clean too, so this test cannot"
    echo "        detect the bug it exists for and its pass means nothing"
    fail=1
fi

[ "$fail" = 0 ] && echo "resize: ok" || echo "resize: FAILED"
exit $fail

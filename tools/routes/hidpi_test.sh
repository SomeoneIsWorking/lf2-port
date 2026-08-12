#!/bin/sh
# The port on a HiDPI display, SIMULATED, because nobody here has one (issue #56).
#
# The report was "on a 4K panel the picture looks like a 1080p frame scaled up". The fix --
# SDL_WINDOW_HIGH_PIXEL_DENSITY on the window, geometry seeded from SDL_GetWindowSizeInPixels,
# the pointer multiplied by the density -- sat unverified for a day because a scaled display
# was assumed to be unavailable. It is not. A NESTED COMPOSITOR CAN BE ONE.
#
#   kwin_wayland --virtual --width 3840 --height 2160 -s <socket> -- <command>
#
# gives a headless Wayland session on a 4K virtual output, and `kscreen-doctor output.1.scale.2`
# inside it sets that output to 200%. SDL's Wayland backend then reports exactly what a real 4K
# panel at 200% reports: a 794x550-point window with a 1588x1100 drawable and a pixel density of
# 2.00. That is the whole configuration the issue could not reach.
#
# TWO THINGS THAT DO NOT WORK, recorded so the next session does not spend the afternoon on
# them again:
#   - Xvfb at -dpi 192 with Xft.dpi merged. SDL3's X11 backend reports points == pixels BY
#     CONSTRUCTION; density is 1.00 no matter what the server's DPI says.
#   - kwin_wayland's own `--scale 2`. Its help says "the scale for WINDOWED mode" and it means
#     it: with `--virtual` the output comes up at scale 1 and clients see density 1.00. The
#     scale has to be set on the running output, which is what kscreen-doctor is for here.
#
# WHAT IS ASSERTED. The whole chain, in the port's own two report lines:
#   density   SDL really did give a scaled drawable -- otherwise this run is the same
#             unscaled run every other route makes and proves nothing, and it says so.
#   pixels    the geometry was seeded from the PIXEL size, not the point size. This is the
#             reported bug: composing from 794 and drawing into 1588 is a 2x upscale.
#   world     the composition is the game's own 794 columns at a world scale of 2 -- the same
#             field of view as an unscaled run, drawn at four times the pixels. A HiDPI player
#             must not see more or less of the stage than anyone else.
#
# The pointer's half of issue #56 is NOT here: it is `ctest geometry`'s test_density, which
# walks five densities offline in a millisecond and has the no-density mutant as its negative.
# This route is the half that genuinely needs a display.
set -eu

BUILD=$(cd "${BUILD:-scratch/build}" 2>/dev/null && pwd) || BUILD=${BUILD:-scratch/build}
GAME=$(cd "${GAME:-game}" 2>/dev/null && pwd) || GAME=${GAME:-game}
LOG=$(mktemp)
trap 'rm -f "$LOG" "$LOG.sh"' EXIT

if [ ! -x "$BUILD/lf2" ]; then echo "SKIP: $BUILD/lf2 not built"; exit 77; fi
if [ ! -f "$GAME/lf2.exe" ]; then echo "SKIP: no game tree at $GAME"; exit 77; fi
command -v kwin_wayland   >/dev/null 2>&1 || { echo "SKIP: no kwin_wayland to nest in"; exit 77; }
command -v kscreen-doctor >/dev/null 2>&1 || { echo "SKIP: no kscreen-doctor to scale the output"; exit 77; }

# The body runs INSIDE the nested session, so it is a file rather than a -c string: kwin takes
# a command to launch and passes it no shell.
cat > "$LOG.sh" <<EOF
#!/bin/sh
sleep 3
kscreen-doctor output.1.scale.2 >/dev/null 2>&1 || echo "hidpi: kscreen-doctor could not set the scale"
sleep 2
cd "$GAME" || exit 1
SDL_VIDEODRIVER=wayland SDL_AUDIODRIVER=dummy LF2_RENDERER=soft LF2_UNPACED=1 \\
  LF2_WINDOW_SIZE=794x550 LF2_QUIT_AFTER=180 "$BUILD/lf2" lf2.exe 2>&1
EOF
chmod +x "$LOG.sh"

# LF2_RENDERER=soft: this route is about GEOMETRY, and a nested compositor is not where to find
# out what the GPU path does (issue #40). -k because a wedged client declines TERM.
echo "hidpi: the port on a simulated 4K display at 200%..."
timeout -k 5 200 kwin_wayland --virtual --width 3840 --height 2160 \
    -s wayland-lf2-hidpi -- "$LOG.sh" > "$LOG" 2>&1 || true

fail=0
say_ok()   { echo "  ok    $1"; }
say_fail() { echo "  FAIL  $1"; fail=1; }

win=$(grep -m1 "^window: .* points -> " "$LOG" || true)
if [ -z "$win" ]; then
    echo "  FAIL  the port never reported its window geometry, so it did not start inside the"
    echo "        nested session. This run measured NOTHING about HiDPI."
    grep -m5 -iE "error|cannot|fail" "$LOG" || echo "        (no diagnostic in the log either)"
    exit 1
fi

# THE PRECONDITION. Without it every assertion below is about an ordinary unscaled run, and
# the port's own line already refuses to claim anything at density 1.00 -- this reads that
# refusal rather than re-deriving it.
if echo "$win" | grep -q "unscaled, so this run says nothing about HiDPI"; then
    say_fail "density: the nested output came up UNSCALED, so this run is not a HiDPI run at"
    say_fail "         all. Nothing below it means anything -- $win"
    echo "hidpi: FAILED"
    exit 1
fi
say_ok "density: $win"

pix=$(echo "$win" | sed 's/.* -> \([0-9]*x[0-9]*\) pixels.*/\1/')
case "$pix" in
1588x1100) say_ok "pixels: the 794x550-point window has a 1588x1100 drawable, as at 200%" ;;
*)         say_fail "pixels: the drawable is $pix, which is not the 1588x1100 a 794x550 window"
           say_fail "        has at 200% -- the simulated scale is not the one this asserts" ;;
esac

# THE REPORTED BUG. The composition must come from 1588, and at a world scale of 2 that is the
# game's own 794 columns -- the same field of view as an unscaled run. A build that seeded from
# the point size would report "window 794x550 -> composition 794x550 at scale 1.000", which is
# the same 794 with the scale that gives it away, so the SCALE is asserted too.
ws=$(grep -m1 "^widescreen: window " "$LOG" || true)
if [ -z "$ws" ]; then
    say_fail "world: no composition was reported at all"
else
    say_ok "world: $ws"
    echo "$ws" | grep -q "window 1588x1100 ->" \
        || say_fail "world: the composition was derived from something other than the 1588x1100"
    echo "$ws" | grep -q "composition 794x550" \
        || say_fail "world: the field of view is not the game's own 794 columns, so a HiDPI"
    echo "$ws" | grep -q "at scale 2.000" \
        || say_fail "world: the world scale is not 2 -- the extra pixels became magnification"
    echo "$ws" | grep -q "drawn into 1588x1100" \
        || say_fail "world: the picture is not drawn into the full drawable, which is the"
    echo "$ws" | grep -q "drawn into 1588x1100" \
        || say_fail "       upscale this issue was reported for"
fi

[ "$fail" = 0 ] && echo "hidpi: ok" || echo "hidpi: FAILED"
exit "$fail"

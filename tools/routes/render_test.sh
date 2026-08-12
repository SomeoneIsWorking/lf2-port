#!/bin/sh
# The native renderer draws the same frame the software compositor does (issue #30).
#
# runtime/video/render.c records the game's draws as a display list and draws them as GPU geometry;
# runtime/video/ddraw.c's software blitter still composes every frame. Both build every frame and
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

# Character selection and a match: a menu is flat panels and text, a match is scrolling
# parallax layers, keyed sprites and the HUD. A renderer can be wrong on one and right on the
# other. ANCHORED, not counted: these were the frame numbers 1300 and 2250, and when the route
# stopped waiting 840 frames for a front end that was already up, the second one landed at a
# different point in the match and the arm failed for a reason that had nothing to do with the
# renderer. The offsets below are the SAME two moments, expressed against the screens.
FRAMES=@charselect+394,@match+282
PAD="south@frontend+0,south@frontend+60,south@frontend+120,south@frontend+180"
PAD="$PAD,south@charselect+58,south@charselect+118,south@charselect+178,south@charselect+238"
PAD="$PAD,up@charselect+298,up@charselect+358,south@charselect+418,south@charselect+618"
PAD="$PAD,south@charselect+838,up@overlay+99,up@overlay+159,south@overlay+219"

arm() {   # arm <dir> [VAR=value ...]
    dir=$1; shift
    mkdir -p "$OUT/$dir"
    ( cd "$GAME" && \
      env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy LF2_UNPACED=1 \
          LF2_VIRTUAL_PAD="$PAD" LF2_FRAME_DUMP="$FRAMES" LF2_DUMP_DIR="$OUT/$dir" \
          LF2_QUIT_AFTER=1460 "$@" \
          timeout -k 5 300 "$BUILD/lf2" lf2.exe ) >/dev/null 2>&1 || true
}

# The GEOMETRY arms run with the lighting off: this comparison is about whether the renderer
# draws the same picture. The light gets its own arm below, which has to change the MATCH
# frame and must NOT change the menu frame -- see the assertion for why that pair is the whole
# point.
echo "native renderer vs the software compositor: four runs..."
arm soft LF2_RENDERER=soft
arm gpu  LF2_HD2D=off
arm skip LF2_HD2D=off LF2_RENDER_SKIP=7
arm light
# THE PORT'S OWN ENGINE (issue #64), against the SAME software compositor and the SAME
# tolerance. Its first version is deliberately a REPRODUCTION rather than an improvement --
# one that both replaced the renderer and changed the shading would fail this comparison for
# two reasons at once and could not be told apart from a broken one. So it has to match, and
# `engskip` is its own negative: the engine honours LF2_RENDER_SKIP, so an engine frame with
# every 7th entry dropped must differ, or the two engine dumps are not the engine.
arm engine     LF2_HD2D=off LF2_ENGINE=1 LF2_DOF=off
arm engineskip LF2_HD2D=off LF2_ENGINE=1 LF2_DOF=off LF2_RENDER_SKIP=7
# THE DEFOCUS (issue #63), against the same engine frame with it switched off. Its two halves are
# opposite on the two frames and that pair IS the test: a depth of field must change a frame with
# a stage in it and change NOTHING on a frame without one. The second half is what catches an
# effect that has spread over the whole picture, which is why the previous bloom/DOF/haze/vignette
# cut was removed -- and it holds by construction here, because a pixel with no distance in the
# G-buffer takes an untouched branch and a menu frame has no layers at all.
arm dof        LF2_HD2D=off LF2_ENGINE=1

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
    for arm_dir in gpu skip engine engineskip dof; do
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

    # ---- THE PORT'S OWN ENGINE, held to the same bar (issue #64) ----
    if [ -f "$OUT/engine/$n" ] && [ -f "$OUT/engineskip/$n" ]; then
        set -- $(cmp_ppm "$f" "$OUT/engine/$n")
        if [ "$1" = "ERR" ]; then echo "  FAIL  $n: engine compare: $2"; fail=1; else
            emax=$1; en=$2
            set -- $(cmp_ppm "$f" "$OUT/engineskip/$n")
            if [ "$1" = "ERR" ]; then echo "  FAIL  $n: engineskip compare: $2"; fail=1; else
                esmax=$1; esn=$2
                if [ "$emax" -le "$TOL" ]; then
                    echo "  ok    $n: the ENGINE matches software (max channel diff $emax,"
                    echo "        $en/$gtot px differ) -- it reproduces the renderer it replaces"
                else
                    echo "  FAIL  $n: the engine differs from software by $emax levels on $en px"
                    echo "        (tolerance $TOL). Its first version must REPRODUCE the old"
                    echo "        picture; anything better comes after this passes."
                    fail=1
                fi
                # Without this the line above would read the same if the engine never ran and
                # the old path drew both dumps.
                if [ "$esmax" -gt "$TOL" ] && [ "$esn" -gt 1000 ]; then
                    echo "  ok    $n: the engine with every 7th draw dropped changes $esn px by"
                    echo "        up to $esmax, so the engine really is what drew the frame above"
                else
                    echo "  FAIL  $n: the ENGINE with every 7th draw DROPPED still matched"
                    echo "        software (max $esmax over $esn px) -- so LF2_ENGINE=1 did not"
                    echo "        select it and the match above is the old path's"
                    fail=1
                fi
            fi
        fi
    fi

    # ---- THE DEFOCUS, asserted the way the light arm is: opposite on the two frames ----
    if [ -f "$OUT/dof/$n" ] && [ -f "$OUT/engine/$n" ]; then
        set -- $(cmp_ppm "$OUT/engine/$n" "$OUT/dof/$n")
        if [ "$1" = "ERR" ]; then echo "  FAIL  $n: dof compare: $2"; fail=1; else
            dmax=$1; dn=$2
            case "$n" in
            *000401*)
                # Character selection: no stage, so no layer carries a distance and every pixel
                # takes the untouched branch. A single changed pixel here is an effect that has
                # escaped the depth it is supposed to be a function of.
                if [ "$dmax" = 0 ]; then
                    say_ok_dof="  ok    $n: the defocus changes NOTHING on a frame with no stage"
                    echo "$say_ok_dof"
                    echo "        in it, so it is a function of distance and not of the screen"
                else
                    echo "  FAIL  $n: the defocus changed $dn px by up to $dmax on a frame with"
                    echo "        NO stage in it. Every pixel there has no distance and must take"
                    echo "        the untouched branch -- this is the effect spreading over the"
                    echo "        whole picture, which is why the last one was deleted."
                    fail=1
                fi ;;
            *)
                # In a match the stage's layers carry real distances, so the defocus must bite.
                if [ "$dmax" -gt 0 ] && [ "$dn" -gt 1000 ]; then
                    echo "  ok    $n: the defocus changes $dn px by up to $dmax on a frame with a"
                    echo "        stage in it, so it is running"
                else
                    echo "  FAIL  $n: the defocus changed $dn px by up to $dmax in a MATCH, where"
                    echo "        the layers carry real distances -- it is not running, or the"
                    echo "        G-buffer reached it empty"
                    fail=1
                fi ;;
            esac
        fi
    fi

    # THE LIGHT ARM, and the reason it asserts two OPPOSITE things on the two frames.
    #
    # The lighting and the cast shadows apply to the objects standing in the stage and to
    # nothing else -- not the background layers, not the HUD, not the text. So:
    #
    #   the MATCH frame      has fighters in it and MUST change.
    #   the MENU frame       has none, and must come out BYTE-IDENTICAL.
    #
    # The second is the one worth having. "The effect changed some pixels" is satisfied by an
    # effect that has quietly spread over the whole frame, which is exactly what the version
    # before this one did -- a bloom and a haze that touched every pixel and left the game
    # looking washed out. A test that only checks the effect RAN cannot see that; a test that
    # also checks WHERE it stopped can.
    if [ -f "$OUT/light/$n" ]; then
        set -- $(cmp_ppm "$OUT/gpu/$n" "$OUT/light/$n")
        if [ "$1" = "ERR" ]; then echo "  FAIL  $n: light compare: $2"; fail=1; continue; fi
        hmax=$1; hn=$2
        # WHICH frame this is, by its ROLE rather than by its number. This matched the
        # filename *002250* -- the frame number the match dump happened to land on while the
        # route waited 840 frames for a front end that was already up. The moment the dumps
        # were anchored to the screens, the match frame arrived as 001351, fell through to the
        # menu branch, and the route reported "the light touched 182635 px on a frame with NO
        # fighters in it" -- an alarming FAILURE about the renderer, caused entirely by the
        # test misidentifying its own frame. The pixel counts were identical to the passing
        # run. FRAMES is ordered menu-then-match and the glob sorts ascending, so the position
        # is the role; nothing here depends on where in the run they land.
        case "$frames_done" in
        2)
            if [ "$hmax" -gt 8 ] && [ "$hn" -gt 2000 ]; then
                echo "  ok    $n: the light changes $hn px by up to $hmax on a frame with"
                echo "        fighters in it, so it is running"
            else
                echo "  FAIL  $n: the light changed $hn px by up to $hmax on a MATCH frame --"
                echo "        it is not running, the character buffer is empty, or its render"
                echo "        targets were never created"
                fail=1
            fi
            ;;
        *)
            if [ "$hn" -eq 0 ]; then
                echo "  ok    $n: the light changes NOTHING on a frame with no fighters in it,"
                echo "        so it is confined to the stage's characters"
            else
                echo "  FAIL  $n: the light changed $hn px by up to $hmax on a frame with NO"
                echo "        fighters in it. It is meant to touch the stage's characters and"
                echo "        nothing else -- the scenery, the HUD and the text must come"
                echo "        through as the game composed them"
                fail=1
            fi
            ;;
        esac
    else
        echo "  FAIL  $n: the light arm produced no such frame"; fail=1
    fi
done

if [ "$frames_done" -eq 0 ]; then
    echo "  FAIL  the software arm produced no frame dumps at all -- the route never reached"
    echo "        the frames under test, so NOTHING was compared. This is not a pass."
    exit 1
fi

# EVERY frame that was ASKED for, not merely one. This said "0 dumps is a failure" and nothing
# more, so a run that produced one of the two requested frames printed "ok (1 frame(s)
# compared)" -- a pass whose coverage had silently halved. That is exactly how it read when an
# anchor resolved past the end of the run.
FRAMES_N=$(printf '%s' "$FRAMES" | tr ',' '\n' | grep -c '[^ ]')
if [ "$frames_done" -ne "$FRAMES_N" ]; then
    echo "  FAIL  $frames_done of the $FRAMES_N requested frame(s) were dumped ($FRAMES) --"
    echo "        the run ended before the rest, so this is a pass over less than was asked"
    echo "        for. Check the anchors against 'screens reached' and LF2_QUIT_AFTER."
    exit 1
fi

[ "$fail" = 0 ] && echo "native renderer: ok ($frames_done frame(s) compared)" \
                || echo "native renderer: FAILED"
exit $fail

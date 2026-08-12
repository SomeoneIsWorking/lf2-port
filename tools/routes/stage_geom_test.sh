#!/bin/sh
# The hand-woven stage geometry really loads INSIDE the running game (issue #62).
#
# WHAT THIS ADDS OVER `ctest stagegeom`, which already walks the loader in a millisecond. That
# test feeds the loader a fixture and a fake layer table; it cannot see any of the three things
# that only exist once the game is running:
#
#   - whether `stages/` is FOUND at all. The port's cwd is the game tree, so the directory is
#     looked for beside the binary, and a copy step that silently did nothing would look exactly
#     like a stage nobody has woven yet.
#   - whether the stage is identified by its own name. That comes from the background record
#     (claim C033), and a wrong offset gives a name that matches no file -- again indistinguishable
#     from "no geometry authored".
#   - whether `depth: layer <file>` resolves against the LOADED STAGE'S layers. The lookup reads
#     the record's layer paths and its spans; nothing offline can check that it is reading the
#     stage the game actually loaded.
#
# Every one of those failures is SILENT and produces a game that draws exactly as it did before,
# which is also what success looks like on a stage with no geometry. That is the whole reason
# this route exists.
#
# THREE ARMS, AND TWO OF THEM MUST FAIL TO LOAD. A run that only ever asserts a positive cannot
# tell a working loader from one that prints its success line unconditionally:
#
#   present   a .stage naming the loaded stage's own layer  -> loads, at that layer's DERIVED
#             depth, and the depth is asserted rather than the vertex count alone. A solid at
#             the wrong depth still parses, still counts, and is still wrong.
#   absent    no file                                       -> says so, naming where it looked
#   bad       `depth: layer <a layer this stage does not have>`
#                                                           -> REFUSED, naming the layer. This
#             is the arm that proves the lookup is consulting the real record: a lookup that
#             accepted anything would load this happily.
#
# THE STAGE IS NOT SHIPPED. The fixture is written into the build directory for the run and
# removed afterwards -- a .stage committed under stages/ would put a stray solid in front of
# every player on that stage, which is the author's decision to make and not this test's.
#
# The route lands on Brokeback Clif; if that ever changes the run says which stage it got and
# fails rather than silently asserting against the wrong numbers.
set -eu

BUILD=$(cd "${BUILD:-scratch/build}" 2>/dev/null && pwd) || BUILD=${BUILD:-scratch/build}
GAME=$(cd "${GAME:-game}" 2>/dev/null && pwd) || GAME=${GAME:-game}

if [ ! -x "$BUILD/lf2" ]; then echo "SKIP: $BUILD/lf2 not built"; exit 77; fi
if [ ! -f "$GAME/lf2.exe" ]; then echo "SKIP: no game tree at $GAME"; exit 77; fi

STAGES="$BUILD/stages"
LOG=$(mktemp)
mkdir -p "$STAGES/models"
clean() { rm -f "$LOG" "$STAGES/Brokeback_Clif.stage" "$STAGES/models/_probe.obj"; }
trap clean EXIT

# Brokeback Clif's own numbers, from its bg.dat: stage width 1500, and bc1.bmp spans 1379.
# So its derived depth is (1500-794)/(1379-794) = 706/585 = 1.2068 (claim C031). That is the
# number the run must print, and it is written here rather than copied from the output.
# The UNDERSCORE form: `bg_stage_name` puts back the underscores fn_0040c160 turned
# into spaces, because the file name is what a .stage is keyed on. `bg table` prints
# the record's own spelling ("Brokeback Clif") and the two lines differ on purpose.
STAGE=Brokeback_Clif
LAYER=bc1.bmp
WANT_DEPTH=1.2068

cat > "$STAGES/models/_probe.obj" <<'OBJ'
# One triangle. This is a PROBE, not art: it exists to be counted and to carry a depth.
v 0 0 0
v 10 0 0
v 0 20 0
vt 0 0
vt 1 0
vt 0 1
vn 0 0 1
f 1/1/1 2/2/1 3/3/1
OBJ

arm() {   # arm <label> <extra-env...>; reads the .stage already in place
    label=$1; shift
    PAD="south@frontend+0,south@frontend+60,south@frontend+120,south@frontend+180"
    PAD="$PAD,south@charselect+58,south@charselect+118,south@charselect+178"
    PAD="$PAD,south@charselect+238,up@charselect+298,up@charselect+358"
    PAD="$PAD,south@charselect+418,south@charselect+618,south@charselect+838"
    PAD="$PAD,up@overlay+99,up@overlay+159,south@overlay+219"
    ( cd "$GAME" && \
      env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy LF2_UNPACED=1 LF2_RENDERER=soft \
          LF2_VIRTUAL_PAD="$PAD" LF2_BG_TABLE=1 LF2_QUIT_AFTER=1400 "$@" \
          timeout -k 5 300 "$BUILD/lf2" lf2.exe ) > "$LOG" 2>&1 || true
    echo "  --    arm $label"
}

fail=0
say_ok()   { echo "  ok    $1"; }
say_fail() { echo "  FAIL  $1"; fail=1; }

# ---- arm 1: the file is there and names a layer this stage HAS ----------------------------
cat > "$STAGES/$STAGE.stage" <<EOF
stage: $STAGE
solid:
  model: models/_probe.obj
  depth: layer $LAYER
  at: 700 0 400
solid_end
EOF
arm "present"

got=$(grep -m1 '^bg table: background .* <- loaded' "$LOG" || true)
case "$got" in
*"Brokeback Clif"*) say_ok "the route landed on Brokeback Clif, whose numbers this asserts" ;;
"") say_fail "the run never reported a loaded stage, so it never reached a match --"
    say_fail "      nothing below measured anything" ;;
*)  say_fail "the route landed on a different stage: $got"
    say_fail "      the depth asserted below is Brokeback Clif's, so this run is void" ;;
esac

line=$(grep -m1 "^stage geometry: $STAGE -- " "$LOG" || true)
if [ -z "$line" ]; then
    say_fail "the .stage in $STAGES was NOT loaded -- and every way that can happen looks"
    say_fail "      identical in the picture, which is why this route exists"
    grep -m5 "^stage geometry" "$LOG" || echo "        (it printed nothing at all)"
else
    case "$line" in
    *"1 solid(s), 3 vertices"*) say_ok "loaded: $line" ;;
    *) say_fail "loaded, but not what was written: $line" ;;
    esac
fi

depth=$(grep -m1 "^stage geometry:   solid at depth" "$LOG" || true)
case "$depth" in
*"$WANT_DEPTH"*) say_ok "depth: $depth" ;;
"") say_fail "no depth was reported, so the solid's PLANE was not measured -- a solid at the"
    say_fail "      wrong depth parses, counts and is still wrong" ;;
*)  say_fail "depth: $depth"
    say_fail "      expected $WANT_DEPTH, which is (1500-794)/(1379-794) from this stage's own"
    say_fail "      bg.dat -- so the lookup resolved $LAYER to the wrong span, or to nothing" ;;
esac

# ---- arm 2: the file names a layer this stage does NOT have -------------------------------
# The arm that proves the lookup reads the real record. Without it, a lookup that returned a
# constant would pass arm 1 whenever that constant happened to be right.
cat > "$STAGES/$STAGE.stage" <<EOF
stage: $STAGE
solid:
  model: models/_probe.obj
  depth: layer definitely_not_a_layer_of_this_stage.bmp
  at: 700 0 400
solid_end
EOF
arm "bad layer name"
bad=$(grep -m1 "^stage geometry: .*REFUSED" "$LOG" || true)
case "$bad" in
*"has no layer named"*) say_ok "refused: $bad" ;;
"") say_fail "a .stage naming a layer this stage does not have was NOT refused -- the lookup"
    say_fail "      is not consulting the loaded stage's layers, so arm 1's depth could have"
    say_fail "      come from anywhere"
    grep -m3 "^stage geometry" "$LOG" || true ;;
*)  say_fail "refused for the wrong reason: $bad" ;;
esac

# ---- arm 3: no file at all ----------------------------------------------------------------
rm -f "$STAGES/$STAGE.stage"
arm "absent" LF2_STAGE_GEOM=1
none=$(grep -m1 "^stage geometry: $STAGE has no" "$LOG" || true)
if [ -n "$none" ]; then
    say_ok "absent: $none"
    if grep -qF "stage geometry:   $STAGES" "$LOG"; then
        say_ok "...and it named $STAGES among the places it looked"
    else
        say_fail "...but it did not name $STAGES, so a reader cannot tell where to put the file"
        grep -m4 "^stage geometry:   " "$LOG" || true
    fi
else
    say_fail "with no .stage present the run said NOTHING about it, so silence in the other"
    say_fail "      arms cannot be read as 'the file was missing'"
fi
if grep -q "^stage geometry: $STAGE -- " "$LOG"; then
    say_fail "geometry was reported as LOADED after the file was removed -- it is cached"
    say_fail "      across stages, or the report is unconditional"
fi


# ---- arms 4 and 5: does any of it actually REACH THE FRAME? ---------------------------------
#
# Everything above proves the file was READ. None of it proves a single triangle was drawn: the
# pass runs on the GPU renderer, its finished target is placed in the display list, and both of
# those can fail into a game that looks exactly the same. `mesh=N` in the renderer's own report
# is the count of geometry passes that entered the list, and the geometry report says how many
# were submitted and how many were dropped for want of a composition surface.
#
# THE NEGATIVE IS THE SECOND RUN, and it is what makes the first one mean anything: with no
# .stage file, `mesh` must be 0. A count that is non-zero either way would be counting something
# else. These two are the only GPU instances this route starts (issue #40 counts them), and they
# run through gpuguard when it is installed so a device loss stops the run rather than the
# session.
gpu_arm() {
    label=$1; shift
    PAD="south@frontend+0,south@frontend+60,south@frontend+120,south@frontend+180"
    PAD="$PAD,south@charselect+58,south@charselect+118,south@charselect+178"
    PAD="$PAD,south@charselect+238,up@charselect+298,up@charselect+358"
    PAD="$PAD,south@charselect+418,south@charselect+618,south@charselect+838"
    PAD="$PAD,up@overlay+99,up@overlay+159,south@overlay+219"
    # 1900 frames, not 1400. Both counters below are printed on a 900-frame cadence, and the
    # match does not start until about frame 1000 -- so a run that stopped at 1400 reported the
    # state at frame 900, before any geometry existed, and read as "not one pass reached the
    # frame". The first report that can see the match is the one at 1800.
    RUN="timeout -k 5 300"
    command -v gpuguard >/dev/null 2>&1 && RUN="gpuguard run --timeout 300 --"
    ( cd "$GAME" && \
      env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy LF2_UNPACED=1 \
          LF2_VIRTUAL_PAD="$PAD" LF2_STAGE_GEOM=1 LF2_RENDER_DEBUG=1 \
          LF2_ENGINE=1 LF2_ENGINE_DEBUG=1 \
          LF2_QUIT_AFTER=1900 "$@" \
          $RUN "$BUILD/lf2" lf2.exe ) > "$LOG" 2>&1 || true
    echo "  --    arm $label (GPU)"
}

# THE COUNTERS. `engine: stage geometry -- N draw(s)` is geometry drawn INSIDE the engine's own
# pass; `render: ... mesh=N` is the OLD arrangement, a separate render pass per parallax gap
# composited back as a texture. On the engine path the first must be non-zero and the second
# ZERO, and neither number alone can tell those two apart.
geom_draws() { sed -n 's/^engine: stage geometry -- \([0-9][0-9]*\) draw.*/\1/p' "$LOG" | tail -1; }
mesh_passes() { sed -n 's/^render: .* mesh=\([0-9][0-9]*\).*/\1/p' "$LOG" | tail -1; }

cat > "$STAGES/$STAGE.stage" <<EOF
stage: $STAGE
solid:
  model: models/_probe.obj
  depth: layer $LAYER
  at: 700 0 400
solid_end
solid:
  model: models/_probe.obj
  depth: 0.5
  at: 300 0 480
solid_end
EOF
gpu_arm "reaches the frame, in two gaps"
gpu_on=$(grep -m1 "^render: gpu=on" "$LOG" || true)
eng_on=$(grep -m1 "^engine: up on" "$LOG" || true)
with=$(geom_draws)
mp=$(mesh_passes)
# The LAST report, not the first: both counters print on a 900-frame cadence, so the report at
# frame 900 is the pre-match state and reads as a clean zero -- exactly the shape of "nothing
# happened" that this whole route exists to tell apart from the real thing.
sub=$(grep "^stage geometry: .* pass(es) placed in the display list" "$LOG" | tail -1)
subn=$(printf '%s' "$sub" | sed -n 's/^stage geometry: [0-9]* frame(s) with geometry, \([0-9]*\) pass.*/\1/p')

if [ -z "$gpu_on" ] || [ -z "$eng_on" ]; then
    echo "  SKIP  the GPU renderer or the engine did not come up in this environment, so"
    echo "        whether the set reaches the frame was NOT measured. The read-side arms ran."
    grep -m1 "^render: gpu=" "$LOG" || true
    grep -m1 "^engine: " "$LOG" || true
elif [ -z "$with" ]; then
    say_fail "the engine did not report its geometry draws, so nothing was measured"
    grep -m1 "^engine: " "$LOG" || true
elif [ "$with" -gt 0 ]; then
    say_ok "in the frame: $with geometry draw(s) inside the engine's own pass"

    # ONE PASS, NOT ONE PER GAP. This is the whole of issue #64's third defect: the geometry
    # used to need a full-screen colour+depth pair per parallax gap because the two renderers
    # could only meet as a texture.
    if [ "${mp:-0}" = 0 ]; then
        say_ok "...and 0 separate mesh passes, so no render target per parallax gap"
    else
        say_fail "...but the separate mesh pass ALSO ran $mp time(s), so the geometry is being"
        say_fail "         composited as a texture as well -- the cost this engine removes"
    fi

    # TWO SOLIDS, TWO PLACES IN THE PAINTER ORDER. They straddle this stage's layers: one at
    # bc1.bmp's derived 1.2068 (equal to layers 0..2, so behind every layer) and one at 0.5
    # (nearer than bc4/bc5 at 1.0, so after all of them). One draw a frame would mean the two
    # were merged into a single placement, which is the failure the per-gap design prevents and
    # which looks perfectly fine on a stage whose solids happen to sit on one side.
    frames=$(printf '%s' "$sub" | sed -n 's/^stage geometry: \([0-9]*\) frame.*/\1/p')
    if [ -n "$frames" ] && [ "$frames" -gt 0 ]; then
        if [ "$with" -eq $((frames * 2)) ]; then
            say_ok "per gap: $with draws over $frames frames is exactly TWO a frame, so the two"
            say_ok "         solids were placed at their own points in the layer order"
        else
            say_fail "per gap: $with draws over $frames frames is not two a frame -- the two"
            say_fail "         solids straddle this stage's layers and must be submitted"
            say_fail "         separately, or one ends up on the wrong side of the layers"
        fi
    fi
    if [ -n "$subn" ] && [ "$subn" -gt 0 ]; then
        say_ok "submit: $sub"
    else
        say_fail "the override recorded $subn submissions while the engine drew $with -- the"
        say_fail "         two counters disagree, so one is not counting what it says: $sub"
    fi

    depths=$(grep -c "^stage geometry:   solid at depth" "$LOG" || true)
    if [ "$depths" -ge 2 ]; then
        say_ok "the loader reported $depths solids at distinct depths"
        grep "^stage geometry:   solid at depth" "$LOG" | sort -u | sed 's/^/        /'
    else
        say_fail "only $depths distinct depth(s) were reported for a two-solid stage"
    fi

    # ---- THE NEGATIVE, without which every count above proves nothing ----
    rm -f "$STAGES/$STAGE.stage"
    gpu_arm "no geometry"
    without=$(geom_draws)
    if [ -z "$without" ]; then
        say_fail "control: the run produced no geometry line, so it cannot serve as the negative"
    elif [ "$without" = 0 ]; then
        say_ok "control: with no .stage file the engine draws 0 geometry -- so the $with above"
        say_ok "         is this stage's authored set and not something the engine always does"
    else
        say_fail "control: with NO .stage file the engine still drew $without geometry, so the"
        say_fail "         count is not measuring the authored set"
    fi
else
    say_fail "the .stage was read but the engine drew 0 geometry -- the set is NOT in the frame"
    grep -m1 "^stage geometry: $STAGE -- " "$LOG" \
        || say_fail "      ...and it was not even LOADED in this run"
    grep -m1 "^engine: stage geometry" "$LOG" || true
fi

[ "$fail" = 0 ] && echo "stage geometry: ok" || echo "stage geometry: FAILED"
exit "$fail"

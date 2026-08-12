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

[ "$fail" = 0 ] && echo "stage geometry: ok" || echo "stage geometry: FAILED"
exit "$fail"

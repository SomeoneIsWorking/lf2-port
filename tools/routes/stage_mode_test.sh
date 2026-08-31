#!/bin/sh
# The port can drive itself into STAGE mode, and the stage-mode camera lock follows the view.
#
# WHY THIS EXISTS. Every scripted route this port had reached the game by pressing buttons at
# counted frames and taking whatever the mode menu was already sitting on -- VS mode. So one of
# the game's eight modes was tested and seven were not, and issue #36 (the stage-mode section
# lock, which holds the camera until a section is cleared) could not be verified at all: the
# code was a proven no-op in the only mode any test visited.
#
# LF2_MODE=<name> puts the GAME'S OWN mode-menu selection where the run asks and lets the
# route's existing confirm dispatch it, so the mode change is the game's. That is what makes
# this file possible, and it is why the first arm below is about the harness rather than the
# game: a route that silently entered VS while asking for stage would be a green test for a
# mode it never visited.
#
# THREE ARMS:
#
#   stage @794    the section lock must be SET. That is the evidence the run reached stage
#                 mode at all -- the lock is a stage-mode mechanism and nothing else sets it.
#   vs @794       the lock must NEVER be set. Without this the arm above would pass on a build
#                 where the lock was set in every mode, and would be measuring nothing.
#   stage @1100   the lock must BIND the camera. Setting it is not enough: the 794->view
#                 substitution only does work when the lock is the tighter of the two bounds,
#                 and a run where the stage's own bound always won would exercise none of it.
set -eu

BUILD=$(cd "${BUILD:-build/clang}" 2>/dev/null && pwd) || BUILD=${BUILD:-build/clang}
GAME=$(cd "${GAME:-game}" 2>/dev/null && pwd) || GAME=${GAME:-game}
LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT

if [ ! -x "$BUILD/lf2" ]; then echo "SKIP: $BUILD/lf2 not built"; exit 77; fi
if [ ! -f "$GAME/lf2.exe" ]; then echo "SKIP: no game tree at $GAME"; exit 77; fi

fail=0
say_ok()   { echo "  ok    $1"; }
say_fail() { echo "  FAIL  $1"; fail=1; }

PAD="south@modemenu+60"
PAD="$PAD,south@charselect+58,south@charselect+118,south@charselect+178,south@charselect+238"
PAD="$PAD,up@charselect+298,up@charselect+358,south@charselect+418,south@charselect+618"
PAD="$PAD,south@charselect+838,up@overlay+99,up@overlay+159,south@overlay+219"

run() {   # run <mode> <window>
    ( cd "$GAME" && \
      env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy LF2_UNPACED=1 LF2_RENDERER=soft LF2_WINDOW_SIZE="$2" \
          LF2_MODE="$1" LF2_CAMERA=1 LF2_VIRTUAL_PAD="$PAD" LF2_QUIT_AFTER=2160 \
          timeout -k 5 400 "$BUILD/lf2" lf2.exe ) > "$LOG" 2>&1 || true
}

# The mode hold has to have HAPPENED. Its absence means the mode menu was never reached, which
# is a different thing from reaching it and choosing wrongly, and must not read as a pass.
held() {
    if grep -q "LF2_MODE=$1 was NEVER held" "$LOG"; then
        say_fail "$1: the mode menu was never reached, so this run entered whatever the game"
        say_fail "      was already on and says NOTHING about $1 mode"
        return 1
    fi
    if ! grep -q "LF2_MODE=$1 was held on" "$LOG"; then
        say_fail "$1: no mode hold was reported at all -- LF2_MODE did nothing"
        return 1
    fi
    if ! grep -q "screens reached.*match@" "$LOG"; then
        say_fail "$1: the route never reached a match, so nothing was measured in play"
        return 1
    fi
    return 0
}

echo "stage mode: the port drives itself in, and the camera lock follows the view..."

run stage 794x550
if held stage; then
    if grep -q "section lock was set on" "$LOG"; then
        say_ok "stage@794: the section lock is set, so the run really is in stage mode"
        grep -m1 "section lock was set on" "$LOG" | sed 's/^/        /'
    else
        say_fail "stage@794: the section lock was never set -- LF2_MODE=stage was held but the"
        say_fail "      run did not end up in stage mode"
    fi
fi

run vs 794x550
if held vs; then
    if grep -q "section lock was NEVER set" "$LOG"; then
        say_ok "vs@794: the lock is never set, so the arm above can distinguish the two modes"
    else
        say_fail "vs@794: the section lock was set in VS mode too -- it is not a stage-mode"
        say_fail "      signal, so the arm above proves nothing"
    fi
fi

run stage 1100x550
if held stage; then
    if grep -q "substitution did work" "$LOG"; then
        say_ok "stage@1100: the lock BOUND the camera, so the 794->view substitution runs"
        grep -m1 "section lock was set on" "$LOG" | sed 's/^/        /'
    else
        say_fail "stage@1100: the lock never bound the camera, so issue #36's substitution was"
        say_fail "      not exercised by this run"
        grep -m1 "section lock" "$LOG" | sed 's/^/        /'
    fi
fi

# THE WALK BOUND FOLLOWS THE SCREEN'S RIGHT EDGE (issue #43). Asserted on the runs above
# rather than by adding any -- the three arms already cover the three answers the port can
# give, and each of them is a different sentence in the report:
#
#   VS            no section at all, so nothing to widen and nothing proved
#   stage@794     the game's own width, where geom_walk_max returns B by construction
#   stage@1100    a view wider than the section, where the camera clamps at 0 and the walk
#                 bound must move out to the screen's edge or a fighter cannot reach the
#                 stage they can see
#
# The 794 arm is the one that makes this a discriminator rather than a rubber stamp: a build
# that widened unconditionally passes the 1100 arm and fails that one alone.
walk_says() {   # walk_says <mode> <window> <grep> <what>
    run "$1" "$2"
    if grep -q "$3" "$LOG"; then
        say_ok "$4"
    else
        say_fail "$4 -- the report said instead:"
        grep -m2 "camera: .*walk bound\|camera: no walk bound" "$LOG" \
            || echo "        NO walk-bound line at all, so nothing measured it"
    fi
}
walk_says stage 794x550 \
    "no walk bound was widened, and correctly so" \
    "stage@794: the walk bound is the game's own B, untouched"
walk_says stage 1100x550 \
    "walk bound was widened to the screen's right edge" \
    "stage@1100: the walk bound moved out to the screen's right edge"
walk_says vs 1100x550 \
    "no walk bound was widened because this run set no section lock at all" \
    "vs@1100: no section, so no boundary was invented"

[ "$fail" = 0 ] && echo "stage mode: ok" || echo "stage mode: FAILED"
exit $fail

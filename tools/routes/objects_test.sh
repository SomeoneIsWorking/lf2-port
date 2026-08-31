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
# FOUR ARMS, and the ones that must DIFFER are the point:
#
#   identity      the hand-ported fn_0041a5a0 against the RECOMPILED body (LF2_OBJ_ORIG=1), at
#                 a 794 view where bg_view_width() is 794 and the port must therefore agree with
#                 it exactly. This is the arm the port is accepted on. It began life as a second
#                 DEFAULT run -- a determinism check -- and passing it in that form proved
#                 nothing about the port, because it compared the port with itself.
#   skew          LF2_OBJ_SKEW=3 moves the pass's camera by 3, so every object it draws moves
#                 3 px and nothing else in the frame does. This MUST differ. It is what proves
#                 the comparison above can report a difference at all.
#   state         .data and the heap, dumped at the same frames. The pixel arms cannot see the
#                 thing that makes this port risky: the pass does not only draw, its effects
#                 loop advances per-effect counters and decrements obj[0x36c], so a subtly
#                 wrong port can corrupt state while drawing a frame that still compares equal.
#   alt           the state arms need their OWN negative. LF2_OBJ_SKEW moves where the pass
#                 draws and nothing else, so it can never make state differ -- using it there
#                 would be a control that cannot fire. A run given one extra input diverges in
#                 the game's own state, and that is the honest negative.
#
# THE PORT HAS LANDED, and the `orig` arm now runs the recompiled body. Anyone tempted to turn
# it back into a second default run should note that it passes either way -- and only one of
# those two is a test.
#
# Software renderer throughout: this is about what the GAME's pass draws, not about how the
# frame is presented, and issue #40 is why nothing runs on the GPU that does not have to.
set -eu

BUILD=$(cd "${BUILD:-build/clang}" 2>/dev/null && pwd) || BUILD=${BUILD:-build/clang}
GAME=$(cd "${GAME:-game}" 2>/dev/null && pwd) || GAME=${GAME:-game}
# NOT mktemp -d. The heap dump is ~106 MB per frame per arm, and /tmp here is a RAM-backed
# tmpfs with a per-user quota that a single run of this would eat. scratch/ is gitignored and
# on the real disk.
OUT=${LF2_SCRATCH:-scratch}/objects_test.$$
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)          # absolute: each arm runs with cwd inside the game tree
trap 'rm -rf "$OUT"' EXIT

if [ ! -x "$BUILD/lf2" ]; then echo "SKIP: $BUILD/lf2 not built"; exit 77; fi
if [ ! -f "$GAME/lf2.exe" ]; then echo "SKIP: no game tree at $GAME"; exit 77; fi
python3 -c "" 2>/dev/null || { echo "SKIP: no python3 to read the frame dumps"; exit 77; }

# Anchored, not counted (issue #57): a frame with fighters standing in the stage, which is the
# only kind of frame this pass draws anything interesting into.
FRAMES=@match+282,@match+732
# The heap dump is ~106 MB, so it is taken at ONE frame rather than both. .data is small
# and is taken at both.
HEAP_FRAME=@match+282
PAD="south@modemenu+60"
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
          LF2_FRAME_DUMP="$FRAMES" LF2_MEM_DUMP="$FRAMES" LF2_HEAP_DUMP="$HEAP_FRAME" \
          LF2_DUMP_DIR="$OUT/$dir" \
          LF2_QUIT_AFTER=1910 "$@" \
          timeout -k 5 300 "$BUILD/lf2" lf2.exe ) >/dev/null 2>&1 || true
}

echo "the stage's object pass: four runs..."
arm port
arm orig LF2_OBJ_ORIG=1
arm skew LF2_OBJ_SKEW=3

# THE STATE ARMS' NEGATIVE, and it has to be a different one. LF2_OBJ_SKEW moves where the
# pass DRAWS and nothing else, so it cannot make .data or the heap differ -- using it as the
# negative for a state comparison would be a control that can never fire, which is the exact
# failure this file was written to avoid. A run given one more input diverges in the game's own
# state and is the honest negative for "can a state comparison report a difference".
# BEFORE the first dump frame (@match+282), not after. Timed at match+400 this arm produced
# state identical to the port arm at the first frame -- a negative that could not fire, which
# is the failure this whole file exists to prevent.
# TWO divergences, because .data and the heap are reached by different things. An extra
# in-match press moves the fighter and changes its RECORD, which lives in the heap -- but
# .data holds the object pointer table and the per-slot EXISTS bytes, which an ordinary input
# does not touch, so with only the match press the .data negative came out IDENTICAL and could
# not have failed. The charselect press picks a different fighter, which is what reaches .data.
# The divergence has to reach BOTH dumps and must not derail the route. An extra in-match
# press changes the fighter's RECORD, which lives in the heap, but not the object pointer
# table or the per-slot EXISTS bytes in .data -- with only that, the .data negative came out
# IDENTICAL and could not have failed. Steering the character cursor instead did reach .data
# and broke the route outright: the alt arm never got to a match and dumped nothing.
#
# A different game MODE reaches both and is the game's own selection rather than a poke at
# state (LF2_MODE writes the mode menu's own word and lets the route's confirm dispatch it,
# the same mechanism tools/routes/stage_mode_test.sh runs on).
PAD_ALT="$PAD,south@match+120,south@match+150"
( PAD="$PAD_ALT"; arm alt LF2_MODE=stage )

fail=0
FRAMES_N=$(printf '%s' "$FRAMES" | tr ',' '\n' | grep -c '[^ ]')
n=$(ls "$OUT/port"/*.ppm 2>/dev/null | wc -l)   # frames only: the dir also holds data_/heap_ dumps
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
        echo "  ok    $nm: the port draws exactly what the recompiled body draws"
    else
        d=$(diff_px "$f" "$OUT/orig/$nm")
        echo "  FAIL  $nm: the port differs from the recompiled body on $d pixel(s) at a"
        echo "        794 view, where bg_view_width() is 794 and they must agree exactly"
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

# ---- THE STATE ARMS ----
#
# The pixel arms above cannot see the thing that makes this port risky. fn_0041a5a0 does not
# only draw: its effects loop at obj+0x3c0 advances per-effect counters and decrements
# obj[0x36c], so a port that is subtly wrong corrupts the game's state while drawing a frame
# that still compares equal. .data carries the object table and the EXISTS bytes; the heap
# carries the object records themselves, counters included.
# WHAT IS MASKED, AND WHY IT IS NOT A FUDGE. Two runs of the identical route were diffed with
# tools/re/diff_data.py before this comparison was written, rather than after it failed:
#
#   .data   12745 dwords compared, FIVE differed
#             0044eea4 / 0044eea8   the __security_cookie and its complement -- read in every
#                                   function prologue, seeded per process
#             0044fda4 / 00451d58 / 00458360
#                                   ASCII fragments of the wall-clock date ("/16" vs "/22")
#   heap    26,704,508 dwords compared, ONE differed
#             20000040              a single ASCII digit, the same clock string
#
# So the guest's state is deterministic to one byte in 106 MB, and everything that is not is
# the clock or the cookie. Those exact locations are excluded and EVERYTHING ELSE must match
# exactly -- a single differing dword anywhere else is a failure. The mask is five addresses
# long and each one is named; it is not a tolerance.
# WHAT IS MASKED, AND WHY IT IS NOT A FUDGE. Two runs of the identical route were diffed with
# tools/re/diff_data.py BEFORE this comparison was written, rather than after it failed:
#
#   .data   12745 dwords compared, FIVE differed, the same five at both frames
#             0044eea4 / 0044eea8   __security_cookie and its complement, seeded per process
#             0044fda4 / 00451d58 / 00458360
#                                   ASCII fragments of the wall-clock date ("/06" vs "/22")
#   heap    26,704,508 dwords compared, ONE differed
#             20000040              a single ASCII digit of the same clock string
#
# The guest's state is therefore deterministic to ONE BYTE IN 106 MB, and everything that is
# not is the clock or the cookie. The cookie is two fixed dwords; the timestamps are STRINGS,
# and which of their dwords differ depends on how far the clock moved between runs -- masking
# the exact dwords from one sample left 2 residuals in the next -- so the string buffers are
# masked +/-16 bytes. Everything else must match EXACTLY; one differing dword is a failure.
#
# PAIRED BY POSITION, NOT BY FILENAME. Dump names carry the frame they landed on, and the alt
# arm runs a different game mode whose match starts earlier, so its anchored dumps are named
# for different frames. Comparing by name reported "the alt arm produced no such dump" -- a
# failure that looked like a broken negative and was really a naming mismatch.
state_cmp() {   # state_cmp <label> <dir> <same|differ> <why it matters>
    python3 - "$OUT/port" "$OUT/$2" "$1" "$3" "$4" <<'PY' || fail=1
import sys, os
a_dir, b_dir, label, want, why = sys.argv[1:6]
EXACT   = {0x0044eea4, 0x0044eea8}
STRINGS = {'data': [0x0044fda4, 0x00451d58, 0x00458360], 'heap': [0x20000040]}
BASE    = {'data': 0x0044d000, 'heap': 0x20000000}

def masked(kind, addr):
    if kind == 'data' and addr in EXACT:
        return True
    return any(t - 16 <= addr < t + 16 for t in STRINGS[kind])

def listing(d, pre):
    return sorted(f for f in os.listdir(d) if f.startswith(pre)) if os.path.isdir(d) else []

bad = False
compared = 0
for pre, kind in (('data_', 'data'), ('heap_', 'heap')):
    A, B = listing(a_dir, pre), listing(b_dir, pre)
    if not A:
        continue
    if len(A) != len(B):
        print("  FAIL  %s %s*: port dumped %d, that arm dumped %d -- nothing was compared"
              % (label, pre, len(A), len(B)))
        bad = True
        continue
    for fa, fb in zip(A, B):
        da = open(os.path.join(a_dir, fa), 'rb').read()
        db = open(os.path.join(b_dir, fb), 'rb').read()
        if not da:
            print("  FAIL  %s %s: the dump is EMPTY, so nothing was compared" % (label, fa))
            bad = True
            continue
        if len(da) != len(db):
            # A size difference IS a difference -- the heap in use is not the same size in two
            # different game modes. It satisfies `differ` and refutes `same`.
            compared += 1
            if want == 'differ':
                print("  ok    %s %s vs %s: differ (%d bytes of heap against %d)"
                      % (label, fa, fb, len(da), len(db)))
            else:
                print("  FAIL  %s %s vs %s: expected same, and the dumps are not even the "
                      "same SIZE (%d vs %d)" % (label, fa, fb, len(da), len(db)))
                print("        " + why)
                bad = True
            continue
        base = BASE[kind]
        n, first = 0, []
        for off in range(0, len(da) - 3, 4):
            if da[off:off+4] != db[off:off+4] and not masked(kind, base + off):
                n += 1
                if len(first) < 4:
                    first.append("%08x" % (base + off))
        got = 'same' if n == 0 else 'differ'
        compared += 1
        where = (" at " + " ".join(first)) if first else ""
        if got == want:
            print("  ok    %s %s: %s (%d dword(s) outside the clock/cookie mask)"
                  % (label, fa, want, n))
        else:
            print("  FAIL  %s %s: expected %s, got %s -- %d dword(s)%s"
                  % (label, fa, want, got, n, where))
            print("        " + why)
            bad = True
if compared == 0:
    print("  FAIL  %s: NO state dumps were compared at all. This is not a pass." % label)
    bad = True
raise SystemExit(1 if bad else 0)
PY
}

state_cmp "state vs orig" orig same \
    "the port left the game in a different state from the recompiled body -- it is not only drawing differently, it is CORRUPTING state"
state_cmp "state vs alt"  alt  differ \
    "a run in a DIFFERENT game mode produced identical state, so this comparison cannot report a difference and the arm above is vacuous"

[ "$fail" = 0 ] && echo "object pass: ok ($FRAMES_N frame(s) per arm, pixels and state)" \
                || echo "object pass: FAILED"
exit $fail

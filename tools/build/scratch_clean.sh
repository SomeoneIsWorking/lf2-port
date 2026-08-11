#!/bin/sh
# Empty a scratch output directory, creating it if it is not there.
#
# WHY THIS EXISTS RATHER THAN AN INLINE `rm -rf`: an inline recursive delete raises a
# permission prompt, and a prompt in an unattended run stops the work rather than merely
# pausing it. One reviewed script is approved once; an `rm -rf` typed fresh each time is not.
#
# It REFUSES to touch anything outside scratch/, which is the whole reason it is safe to
# approve: the argument is a path under the project's own gitignored scratch tree and nothing
# else is reachable through it.
#
#     tools/build/scratch_clean.sh screenshots/42        -> empties scratch/screenshots/42
#     tools/build/scratch_clean.sh logs                  -> empties scratch/logs
set -eu

# ../.. -- this script lives in tools/build/, so the repo root is two levels up.
cd "$(dirname "$0")/../.."
ROOT=$(pwd -P)

if [ "$#" -ne 1 ]; then
    echo "usage: tools/build/scratch_clean.sh <path under scratch/>" >&2
    exit 2
fi

case "$1" in
/*|*..*)
    echo "scratch_clean: refusing '$1' -- absolute paths and .. are not allowed" >&2
    exit 2 ;;
scratch/*)
    target=$1 ;;
*)
    target=scratch/$1 ;;
esac

# Resolved, so a symlink inside scratch/ cannot walk out of it.
mkdir -p "$target"
real=$(cd "$target" && pwd -P)
case "$real" in
"$ROOT"/scratch/*|"$ROOT"/scratch)
    ;;
*)
    echo "scratch_clean: refusing '$target' -- it resolves to $real, outside $ROOT/scratch" >&2
    exit 2 ;;
esac

# find, not `rm -rf dir`, so the directory itself survives and a caller can write into it
# immediately. Says how much it removed: "cleaned" with no number would look the same whether
# the directory had been full or had never existed.
n=$(find "$real" -mindepth 1 | wc -l)
find "$real" -mindepth 1 -delete
echo "scratch_clean: $target emptied ($n entr$([ "$n" = 1 ] && echo y || echo ies) removed)"

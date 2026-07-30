#!/bin/sh
# Build if needed, then run the port from the game tree (the game opens its
# data with relative paths, so the working directory must be game/).
# Extra arguments are passed through to lf2; environment switches are listed
# in docs/running.md.
set -e
cd "$(dirname "$0")"

cmake -S . -B scratch/build >/dev/null
cmake --build scratch/build -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)"

cd game
exec ../scratch/build/lf2 lf2.exe "$@"

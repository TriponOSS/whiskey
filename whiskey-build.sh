#!/bin/sh
# whiskey-build.sh - the link step for consumer apps.
#
#   whiskey-build.sh <your-main.tp>              # RALT on PATH
#   RALT=/path/to/ralt whiskey-build.sh <main>
#
# glue.c is the only C in whiskey: ralt folds lib .tp + your app into
# build/<stem>.c, then this script links glue.c in. If ralt's own cc step
# already failed on glue symbols, that's fine - this script finishes.

set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
RALT="${RALT:-ralt}"

SRC="$1"
stem="$(basename "$SRC" .tp)"
cd "$(dirname "$SRC")"

"$RALT" build "./$(basename "$SRC")" >/dev/null 2>&1 || true

"$RALT" build "./$(basename "$SRC")" >/dev/null 2>&1 || true
[ -f "build/${stem}.c" ] || {
    echo "whiskey-build.sh: expected $(pwd)/build/${stem}.c after ralt build" >&2
    exit 1
}
cc -O1 -std=c99 "build/${stem}.c" "$HERE/glue.c" -o "build/${stem}-app"
echo "whiskey build ok -> build/${stem}-app"

#!/bin/sh
# serve.sh - build and run the whiskey demo (fork-per-request, :8080).
# serving root = CWD: run from the repo root so /static/* maps ./static/.
set -e
cd "$(dirname "$0")"
RALT="${RALT:-../Tripon/build/ralt}"
"$RALT" build examples/demo.tp >/dev/null 2>&1 || true
if [ ! -f build/demo.c ]; then
    echo "serve.sh: ralt build failed to emit build/demo.c" >&2
    exit 1
fi
cc -O1 -std=c99 build/demo.c glue.c -o build/whiskey-demo
echo "whiskey demo -> build/whiskey-demo, starting on :8080"
exec ./build/whiskey-demo

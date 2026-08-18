#!/bin/sh
# Captures every suite's output against one wsgidav server, in all three
# output modes, into a directory that can be diffed against another
# capture.
#
#   ./tests/capture.sh OUTDIR
#
# This is the machinery behind the rule in TODO: any change to
# neon/test/common/tests.c or src/common.c must leave the default text
# output byte for byte identical.  The way to prove that is
#
#   git stash                       # or check out the older tree
#   make -f Makefile.w32 && ./tests/capture.sh /tmp/before
#   git stash pop
#   make -f Makefile.w32 && ./tests/capture.sh /tmp/after
#   ./tests/compare-captures.sh /tmp/before /tmp/after
#
# Two captures of the same build are byte for byte identical, so any
# difference is the change under test.
#
# Environment:
#   PORT     port for the server (default 8901)
#   DEADPORT port that refuses connections (default 49531)
#   TESTS    suites to capture (default: all of them)
#
# For each suite three files are written from a live server -- default,
# --quiet and --json output -- and three more from a connection that is
# refused, which is how the suites that are too slow or too destructive
# to run in full (largefile, lockbomb) still get their output paths
# covered.

set -e

OUT=$1
if [ -z "$OUT" ]; then
    echo "usage: capture.sh OUTDIR" >&2
    exit 1
fi

srcdir=`dirname "$0"`/..
cd "$srcdir"

PORT=${PORT-8901}
DEADPORT=${DEADPORT-49531}
LIVE=${TESTS-"basic copymove props locks http protected"}
ALL=${TESTS-"basic copymove props locks http protected largefile lockbomb lockbomb-single"}
VENV=dav-venv
DAVROOT=davroot

CLI=./litmus-cli
[ -x "$CLI" ] || CLI=./litmus-cli.exe
if [ ! -x "$CLI" ]; then
    echo "capture.sh: build litmus-cli first" >&2
    exit 1
fi

if [ -x "$VENV/Scripts/python.exe" ]; then
    VPYTHON="$VENV/Scripts/python.exe"
elif [ -x "$VENV/bin/python" ]; then
    VPYTHON="$VENV/bin/python"
else
    echo "capture.sh: no virtualenv; run ./tests/wsgidav.sh once first" >&2
    exit 1
fi

rm -rf "$OUT"
mkdir -p "$OUT"
# A file left locked by a killed run makes rm -rf a silent no-op on
# Windows, and the stale contents then look like a regression.
if [ -n "`ls -A "$OUT" 2>/dev/null`" ]; then
    echo "capture.sh: $OUT is not empty after rm -rf; stale files remain" >&2
    exit 1
fi

CAPROOT="$DAVROOT/capture"
rm -rf "$CAPROOT"
mkdir -p "$CAPROOT"

# wsgidav is a native Windows program on Windows, so it needs a
# Windows-style path.
ABSROOT=`cd "$CAPROOT" && pwd`
if command -v cygpath >/dev/null 2>&1; then
    ABSROOT=`cygpath -m "$ABSROOT"`
fi

cat > "$DAVROOT/capture.json" <<EOF
{
  "host": "127.0.0.1",
  "port": $PORT,
  "provider_mapping": {"/": "$ABSROOT"},
  "simple_dc": {"user_mapping": {"*": true}},
  "property_manager": true,
  "verbose": 1
}
EOF

"$VPYTHON" -m wsgidav.server.server_cli --config "$DAVROOT/capture.json" \
    > "$DAVROOT/capture.log" 2>&1 &
SERVER_PID=$!

cleanup() {
    if kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || :
        wait "$SERVER_PID" 2>/dev/null || :
    fi
}
trap cleanup EXIT INT TERM

"$VPYTHON" - "$PORT" <<'PY' || { echo "capture.sh: server did not start" >&2; exit 1; }
import socket, sys, time
port = int(sys.argv[1])
for _ in range(100):
    try:
        socket.create_connection(("127.0.0.1", port), 0.5).close()
        sys.exit(0)
    except OSError:
        time.sleep(0.1)
sys.exit(1)
PY

# Colour and debug logging are pinned so that a capture does not depend
# on whether it was run from a terminal.
run() {
    suite=$1; outfile=$2; shift 2
    TEST_NODEBUG=1 TEST_COLOUR=0 "$CLI" "$suite" "$@" >"$outfile" 2>&1 \
        || echo "exit=$?" >>"$outfile"
}

for t in $LIVE; do
    for mode in default quiet json; do
        mkdir -p "$CAPROOT/$t-$mode"
        case $mode in
            default) run "$t" "$OUT/$t.default" \
                         "http://127.0.0.1:$PORT/$t-$mode/" ;;
            quiet)   run "$t" "$OUT/$t.quiet" --quiet \
                         "http://127.0.0.1:$PORT/$t-$mode/" ;;
            json)    run "$t" "$OUT/$t.json" --json \
                         "http://127.0.0.1:$PORT/$t-$mode/" ;;
        esac
    done
done

for t in $ALL; do
    run "$t" "$OUT/$t.refused" "http://127.0.0.1:$DEADPORT/dav/"
    run "$t" "$OUT/$t.refused-quiet" --quiet "http://127.0.0.1:$DEADPORT/dav/"
    run "$t" "$OUT/$t.refused-json" --json "http://127.0.0.1:$DEADPORT/dav/"
done

echo "captured to $OUT"

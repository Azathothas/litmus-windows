#!/bin/sh
# Runs the suites against golang.org/x/net/webdav, as a second opinion
# to wsgidav.
#
#   ./tests/godav.sh [OUTDIR] [SUITE...]
#
# Needs a Go toolchain and, on the first run, network access to fetch
# golang.org/x/net.  The server source is in tests/godav.
#
# Neither server is complete on its own, which is the point of having
# both: wsgidav supports dead properties but its LOCK response has a
# broken Content-Type, so every lock test is skipped there; x/net/webdav
# locks correctly but has no dead property store, so the props suite
# fails against it.  A change to the lock tests is verified here, a
# change to the property tests against wsgidav, and neither result is
# taken as the whole picture.
#
# Environment:
#   PORT     port for the server (default 8909)
#   TESTS    suites to run (default: basic copymove props locks http)

set -e

srcdir=`dirname "$0"`/..
cd "$srcdir"

OUT=${1-godav-results}
[ $# -gt 0 ] && shift
SUITES=${*:-${TESTS-"basic copymove props locks http"}}
PORT=${PORT-8909}

CLI=./litmus-cli
[ -x "$CLI" ] || CLI=./litmus-cli.exe
if [ ! -x "$CLI" ]; then
    echo "godav.sh: build litmus-cli first" >&2
    exit 1
fi

if ! command -v go >/dev/null 2>&1; then
    echo "godav.sh: needs a Go toolchain (https://go.dev/dl/)" >&2
    exit 1
fi

SERVER=tests/godav/godav
[ -x "$SERVER" ] || SERVER=tests/godav/godav.exe
if [ ! -x "$SERVER" ]; then
    echo "-- Building the x/net/webdav server --"
    (cd tests/godav && go build -o godav .)
    SERVER=tests/godav/godav
    [ -x "$SERVER" ] || SERVER=tests/godav/godav.exe
fi

rm -rf "$OUT"
mkdir -p "$OUT"

ROOT="$OUT/root"
mkdir -p "$ROOT"
ABSROOT=`cd "$ROOT" && pwd`
if command -v cygpath >/dev/null 2>&1; then
    ABSROOT=`cygpath -m "$ABSROOT"`
fi

echo "-- Launching x/net/webdav on port $PORT --"
"$SERVER" -addr "127.0.0.1:$PORT" -dir "$ABSROOT" > "$OUT/server.log" 2>&1 &
SERVER_PID=$!

cleanup() {
    if kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || :
        wait "$SERVER_PID" 2>/dev/null || :
    fi
}
trap cleanup EXIT INT TERM

# Wait for the port rather than sleeping blindly.
n=0
while [ $n -lt 100 ]; do
    if (exec 3<>/dev/tcp/127.0.0.1/$PORT) 2>/dev/null; then
        exec 3>&- 2>/dev/null || :
        break
    fi
    n=`expr $n + 1`
    sleep 0.1 2>/dev/null || sleep 1
done

echo "-- Running tests --"
RV=0
for t in $SUITES; do
    mkdir -p "$ROOT/$t"
    TEST_NODEBUG=1 "$CLI" "$t" "http://127.0.0.1:$PORT/$t/" \
        > "$OUT/$t.txt" 2>&1 || RV=1
    grep '^<- summary' "$OUT/$t.txt" || echo "  $t: no summary, see $OUT/$t.txt"
done

echo "-- Output in $OUT --"
exit $RV

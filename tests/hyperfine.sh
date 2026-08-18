#!/bin/sh
# Times litmus itself with hyperfine, as opposed to timing the server,
# which is what `litmus-cli bench' does.
#
#   ./tests/hyperfine.sh
#
# Needs hyperfine (https://github.com/sharkdp/hyperfine) and a Go
# toolchain for the server in tests/godav.  The server is the fast one
# of the two available, so what is left in these numbers is mostly
# process start-up and the client's own work.
#
# hyperfine is run with -N so the command goes straight to the OS: with
# a shell in the way it would be cmd.exe on Windows, which does not
# understand a ./ prefix.
#
# Environment:
#   PORT        port for the server (default 8925)
#   HYPERFINE   path to the hyperfine executable

set -e

srcdir=`dirname "$0"`/..
cd "$srcdir"

PORT=${PORT-8925}
HYPERFINE=${HYPERFINE-hyperfine}

if ! command -v "$HYPERFINE" >/dev/null 2>&1; then
    echo "hyperfine.sh: hyperfine not found; set \$HYPERFINE" >&2
    exit 1
fi

CLI="$PWD/litmus-cli"
[ -x "$CLI" ] || CLI="$PWD/litmus-cli.exe"
if [ ! -x "$CLI" ]; then
    echo "hyperfine.sh: build litmus-cli first" >&2
    exit 1
fi
# -N runs the command without a shell, so the path must be one the OS
# understands directly.
if command -v cygpath >/dev/null 2>&1; then
    CLI=`cygpath -m "$CLI"`
fi

# Always rebuild; go caches, and a stale executable from an older
# main.go would otherwise be used silently.
(cd tests/godav && go build -o godav .)
SERVER=tests/godav/godav
[ -x "$SERVER" ] || SERVER=tests/godav/godav.exe

ROOT=hyperfine-root
rm -rf "$ROOT"
mkdir -p "$ROOT/basic" "$ROOT/http"
ABSROOT=`cd "$ROOT" && pwd`
if command -v cygpath >/dev/null 2>&1; then
    ABSROOT=`cygpath -m "$ABSROOT"`
fi

"$SERVER" -addr "127.0.0.1:$PORT" -dir "$ABSROOT" > "$ROOT/server.log" 2>&1 &
SERVER_PID=$!
trap 'kill "$SERVER_PID" 2>/dev/null || :' EXIT INT TERM

# The server binds before it logs this, so the line means the port is
# accepting connections.
n=0
while [ "$n" -lt 100 ]; do
    grep -q "listening on" "$ROOT/server.log" 2>/dev/null && break
    n=`expr $n + 1`
    sleep 0.1 2>/dev/null || sleep 1
done

TEST_NODEBUG=1 TEST_COLOUR=0 \
"$HYPERFINE" -N --warmup 3 --runs 20 --style basic \
    -n "version" "$CLI version" \
    -n "list" "$CLI list" \
    -n "http" "$CLI http --quiet http://127.0.0.1:$PORT/http/" \
    -n "basic" "$CLI basic --quiet http://127.0.0.1:$PORT/basic/"

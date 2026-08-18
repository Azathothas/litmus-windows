#!/bin/sh
# Runs the litmus suites against a local wsgidav server.
#
# This is the Windows counterpart to tests/httpd, which needs a
# container and so only works on Unix.  wsgidav is pure Python and runs
# natively on Windows, so this script works on both.
#
#   ./tests/wsgidav.sh              run the default suites
#   ./tests/wsgidav.sh --json       pass options through to each suite
#   TESTS="basic locks" ./tests/wsgidav.sh
#
# Environment:
#   PYTHON   python interpreter to build the venv with (default: python3
#            if present, else python)
#   PORT     port for the server (default 8899)
#   TESTS    suites to run (default: basic copymove props locks http)
#   TESTROOT directory holding the suite executables (default: the
#            top of the build tree)
#
# The virtualenv is created in ./dav-venv and the server data directory
# in ./davroot; both are gitignored and reused across runs.

set -e

srcdir=`dirname "$0"`/..
cd "$srcdir"

PORT=${PORT-8899}
TESTS=${TESTS-"basic copymove props locks http"}
TESTROOT=${TESTROOT-.}
VENV=dav-venv
DAVROOT=davroot

# --check runs every suite with --json and compares the totals against
# tests/expected-wsgidav.txt, so a run can be a pass/fail gate even
# though wsgidav itself fails some tests.
CHECK=0
if [ "${1-}" = "--check" ]; then
    CHECK=1
    shift
fi

# wsgidav needs a native Windows Python: bcrypt has no mingw wheel.
NEED_NATIVE_PYTHON=1
# shellcheck source=tests/python.sh
. tests/python.sh

if [ -z "${PYTHON-}" ]; then
    cat >&2 <<'MSG'
wsgidav.sh: no working Python interpreter found.

This script needs a native Windows Python (or any Python on Unix). If
one is installed but not on PATH, point at it directly:

    PYTHON=/c/Python313/python.exe ./tests/wsgidav.sh

Do not use MSYS2's own python here: wsgidav pulls in bcrypt, which has
no mingw wheel and needs Rust to build. Install Python from
https://www.python.org/downloads/ if you have none.
MSG
    exit 1
fi

# The venv layout differs between Windows and Unix.  Existing is not
# enough: a venv from an interrupted run has an interpreter and no
# packages, and one restored from a CI cache can point at an
# interpreter that is no longer installed.  Import what the server
# needs; if that fails, throw the directory away and build it again.
VPYTHON=
for candidate in "$VENV/Scripts/python.exe" "$VENV/bin/python"; do
    if [ -x "$candidate" ] \
       && "$candidate" -c "import wsgidav, cheroot" >/dev/null 2>&1; then
        VPYTHON=$candidate
        break
    fi
done

if [ -z "$VPYTHON" ]; then
    echo "-- Creating virtualenv in $VENV --"
    rm -rf "$VENV"
    "$PYTHON" -m venv "$VENV"
    if [ -x "$VENV/Scripts/python.exe" ]; then
        VPYTHON="$VENV/Scripts/python.exe"
    else
        VPYTHON="$VENV/bin/python"
    fi
    "$VPYTHON" -m pip install --quiet --upgrade pip
    "$VPYTHON" -m pip install --quiet wsgidav cheroot
fi

# Each run gets its own collection: litmus creates a collection called
# "litmus" under the URL it is given, and a leftover one from a previous
# run makes MKCOL return 405 so every suite aborts at "begin".
RUNDIR="run-$$"
mkdir -p "$DAVROOT/$RUNDIR"

# wsgidav is a native Windows program, so it needs a Windows-style path;
# a /c/... style MSYS path will not work.
ABSROOT=`cd "$DAVROOT" && pwd`
if command -v cygpath >/dev/null 2>&1; then
    ABSROOT=`cygpath -m "$ABSROOT"`
fi

# property_manager must be on, or every PROPPATCH returns 403 and the
# whole props suite fails in a way that looks like a litmus bug.
# lock_manager is deliberately not set: it is deprecated in wsgidav 4.x
# and setting it makes startup fail outright.
cat > "$DAVROOT/wsgidav.json" <<EOF
{
  "host": "127.0.0.1",
  "port": $PORT,
  "provider_mapping": {"/": "$ABSROOT"},
  "simple_dc": {"user_mapping": {"*": true}},
  "property_manager": true,
  "verbose": 1
}
EOF

echo "-- Launching wsgidav on port $PORT --"
"$VPYTHON" -m wsgidav.server.server_cli --config "$DAVROOT/wsgidav.json" \
    > "$DAVROOT/wsgidav.log" 2>&1 &
SERVER_PID=$!

cleanup() {
    if kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || :
        wait "$SERVER_PID" 2>/dev/null || :
    fi
}
trap cleanup EXIT INT TERM

# Wait for the port to accept connections rather than sleeping blindly.
"$PYTHON" - "$PORT" <<'PY' || { echo "-- Server did not start; see $DAVROOT/wsgidav.log --"; exit 1; }
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

echo "-- Running tests --"
RV=0
RESULTS="$DAVROOT/results-$$.jsonl"
: > "$RESULTS"

CLI="$TESTROOT/litmus-cli"
[ -x "$CLI" ] || CLI="$TESTROOT/litmus-cli.exe"
if [ ! -x "$CLI" ]; then
    echo "ERROR: could not find $TESTROOT/litmus-cli" >&2
    exit 1
fi

for t in $TESTS; do
    # A fresh collection per suite keeps one suite's leftovers from
    # aborting the next.
    mkdir -p "$DAVROOT/$RUNDIR/$t"
    if [ $CHECK -eq 1 ]; then
        "$CLI" "$t" --json "http://127.0.0.1:$PORT/$RUNDIR/$t/" >> "$RESULTS" || :
    else
        "$CLI" "$t" "$@" "http://127.0.0.1:$PORT/$RUNDIR/$t/" || RV=1
    fi
done

if [ $CHECK -eq 1 ]; then
    echo "-- Comparing against tests/expected-wsgidav.txt --"
    "$PYTHON" tests/check-expected.py tests/expected-wsgidav.txt "$RESULTS" || RV=1
fi

exit $RV

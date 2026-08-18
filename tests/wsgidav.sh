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

# Returns success if $1 is an interpreter this script can use.
#
# Running it is the only reliable test.  On Windows "python3" is usually
# a Microsoft Store stub that exists, resolves, and then refuses to run,
# and "py" is a launcher that is present even when no interpreter is
# registered with it.
#
# An MSYS2 python is rejected on purpose: it runs perfectly well, but
# wsgidav depends on bcrypt, which has no mingw wheel and needs a Rust
# toolchain to build from source.  sysconfig.get_platform() reports
# mingw_x86_64_ucrt_gnu there against win-amd64 for a native Python.
usable_python() {
    plat=`"$1" -c "import sysconfig; print(sysconfig.get_platform())" 2>/dev/null` \
        || return 1
    case $plat in
        mingw*) return 1 ;;
        "")     return 1 ;;
        *)      return 0 ;;
    esac
}

if [ -z "${PYTHON-}" ]; then
    for candidate in python3 python py; do
        if usable_python "$candidate"; then
            PYTHON=$candidate
            break
        fi
    done
fi

# An MSYS2 shell started from the Start menu has a minimal PATH that
# leaves out the Windows one, so a usable Python can be sitting in the
# normal place and still not be found above.  Look there before giving
# up.
if [ -z "${PYTHON-}" ] && command -v cygpath >/dev/null 2>&1; then
    for base in "${LOCALAPPDATA-}\\Programs\\Python" "${PROGRAMFILES-}" "C:\\"; do
        [ -n "$base" ] || continue
        dir=`cygpath -u "$base" 2>/dev/null` || continue
        [ -d "$dir" ] || continue
        for candidate in "$dir"/Python3*/python.exe "$dir"/Python*/python.exe; do
            if [ -x "$candidate" ] && usable_python "$candidate"; then
                PYTHON=$candidate
                echo "-- Using $candidate --"
                break 2
            fi
        done
    done
fi

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

# The venv layout differs between Windows and Unix.
if [ -x "$VENV/Scripts/python.exe" ]; then
    VPYTHON="$VENV/Scripts/python.exe"
elif [ -x "$VENV/bin/python" ]; then
    VPYTHON="$VENV/bin/python"
else
    echo "-- Creating virtualenv in $VENV --"
    # Start clean: a half-built venv from an interrupted run would
    # otherwise be picked up as usable on the next one.
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

for t in $TESTS; do
    prog="$TESTROOT/$t"
    [ -x "$prog" ] || prog="$TESTROOT/$t.exe"
    if [ ! -x "$prog" ]; then
        echo "ERROR: could not find $TESTROOT/$t" >&2
        exit 1
    fi
    # A fresh collection per suite keeps one suite's leftovers from
    # aborting the next.
    mkdir -p "$DAVROOT/$RUNDIR/$t"
    if [ $CHECK -eq 1 ]; then
        "$prog" --json "http://127.0.0.1:$PORT/$RUNDIR/$t/" >> "$RESULTS" || :
    else
        "$prog" "$@" "http://127.0.0.1:$PORT/$RUNDIR/$t/" || RV=1
    fi
done

if [ $CHECK -eq 1 ]; then
    echo "-- Comparing against tests/expected-wsgidav.txt --"
    "$PYTHON" tests/check-expected.py tests/expected-wsgidav.txt "$RESULTS" || RV=1
fi

exit $RV

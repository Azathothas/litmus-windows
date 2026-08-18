#!/bin/sh
# Checks the result harness itself, without a server.
#
#   ./tests/harness.sh              compare against the expected output
#   ./tests/harness.sh --regenerate rewrite the expected output
#
# `litmus-cli selftest' runs three synthetic suites, defined in
# src/selftest.c, whose tests return fixed results.  Nothing here talks
# to a network, so the output depends only on the harness: the statuses,
# the counting, the JSON emission and its escaping, the notrun
# bookkeeping, and the state reset between suites.  Every other test in
# this tree needs a live WebDAV server, which makes this the only check
# that runs anywhere in under a second.
#
# Each of the three output modes is captured with its exit status and
# compared against tests/harness-expected.  A change there is either a
# harness regression or a deliberate change that needs the expected
# files regenerated -- and the diff read before they are checked in.
#
# Normalisation, and why:
#
#   text   carriage returns are deleted, then `cat -v' renders what is
#          left of the control characters.  stdout is in text mode on
#          Windows, so every line ending carries a CR there and none on
#          Unix; and the harness itself writes a CR to rewrite the
#          progress line.  Deleting them is what makes one expected
#          file work on both platforms.  cat -v then keeps the file
#          readable ASCII rather than something git calls binary.
#   json   "duration" and "started" are wall-clock and differ between
#          any two runs by design, so they are blanked.  Everything
#          else, escapes included, is compared exactly.
#
# Environment:
#   TESTROOT  directory holding litmus-cli (default: top of the tree)

set -e

srcdir=`dirname "$0"`/..
cd "$srcdir"

REGEN=0
if [ "${1-}" = "--regenerate" ]; then
    REGEN=1
    shift
fi

TESTROOT=${TESTROOT-.}
EXPECTED=tests/harness-expected

CLI="$TESTROOT/litmus-cli"
[ -x "$CLI" ] || CLI="$TESTROOT/litmus-cli.exe"
if [ ! -x "$CLI" ]; then
    echo "harness.sh: build litmus-cli first" >&2
    exit 1
fi

WORK=`mktemp -d 2>/dev/null` || WORK=./harness-work.$$
mkdir -p "$WORK"
trap 'rm -rf "$WORK"' EXIT INT TERM

mkdir -p "$EXPECTED"

# Runs one mode into $WORK/$1.out, normalised and with the exit status
# appended so that it is compared too.
capture() {
    name=$1
    shift

    set +e
    TEST_NODEBUG=1 TEST_COLOUR=0 "$CLI" selftest "$@" > "$WORK/raw" 2>&1
    status=$?
    set -e

    case $name in
        json)
            sed -e 's/"duration":[0-9.]*/"duration":X/g' \
                -e 's/"started":"[^"]*"/"started":"X"/g' "$WORK/raw" \
                | tr -d '\r' > "$WORK/$name.out"
            ;;
        *)
            tr -d '\r' < "$WORK/raw" | cat -v > "$WORK/$name.out"
            ;;
    esac

    echo "exit=$status" >> "$WORK/$name.out"
}

capture default
capture quiet --quiet
capture json --json

rc=0
for name in default quiet json; do
    want="$EXPECTED/$name.txt"

    if [ $REGEN -eq 1 ]; then
        cp "$WORK/$name.out" "$want"
        echo "  regenerated $want"
        continue
    fi

    if [ ! -f "$want" ]; then
        echo "  MISSING $want; run ./tests/harness.sh --regenerate" >&2
        rc=1
    elif cmp -s "$WORK/$name.out" "$want"; then
        echo "  $name ok"
    else
        echo "  $name DIFFERS"
        diff -u "$want" "$WORK/$name.out" || :
        rc=1
    fi
done

if [ $REGEN -eq 1 ]; then
    echo "Expected output regenerated.  Read the diff before checking it in."
    exit 0
fi

if [ $rc -eq 0 ]; then
    echo "The harness produces exactly the expected output."
else
    echo "The harness output moved.  Either that is a regression, or it is" >&2
    echo "deliberate and ./tests/harness.sh --regenerate is what you want." >&2
fi

exit $rc

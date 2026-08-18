#!/bin/sh
# Compares two directories written by tests/capture.sh.
#
#   ./tests/compare-captures.sh BEFORE AFTER
#
# Text output must match byte for byte.  JSON output is compared with
# the wall-clock fields stripped -- "duration" and "started" differ
# between any two runs by design -- so a difference there is a real one.
#
# Exits 0 if the two agree, 1 otherwise, printing what moved.
#
# A difference confined to copymove is probably not yours: see the note
# in capture.sh.  Capture a third time and compare that against both.

A=$1
B=$2
if [ -z "$A" ] || [ -z "$B" ]; then
    echo "usage: compare-captures.sh BEFORE AFTER" >&2
    exit 1
fi

rc=0

norm() {
    sed -e 's/"duration":[0-9.]*/"duration":X/g' \
        -e 's/"started":"[^"]*"/"started":"X"/g' "$1"
}

for f in "$A"/*; do
    base=`basename "$f"`
    other="$B/$base"

    if [ ! -f "$other" ]; then
        echo "MISSING in $B: $base"
        rc=1
        continue
    fi

    case $base in
        *json*)
            norm "$f" > "$f.norm"
            norm "$other" > "$other.norm"
            if ! cmp -s "$f.norm" "$other.norm"; then
                echo "DIFFERS (json): $base"
                diff -u "$f.norm" "$other.norm" | head -20
                rc=1
            fi
            rm -f "$f.norm" "$other.norm"
            ;;
        *)
            if ! cmp -s "$f" "$other"; then
                echo "DIFFERS (text): $base"
                diff -u "$f" "$other" | head -20
                rc=1
            fi
            ;;
    esac
done

for f in "$B"/*; do
    base=`basename "$f"`
    case $base in *.norm) continue ;; esac
    [ -f "$A/$base" ] || { echo "EXTRA in $B: $base"; rc=1; }
done

if [ $rc -eq 0 ]; then
    echo "identical: $A and $B agree"
fi

exit $rc

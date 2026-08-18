"""Compare litmus JSON results against a table of expected totals.

usage: check-expected.py EXPECTED.txt RESULTS.jsonl

EXPECTED.txt holds one "suite passed failed skipped" row per suite,
with '#' comments.  RESULTS.jsonl holds one JSON object per suite, as
written by a suite run with --json.

Exits 0 if every suite matches, 1 otherwise, printing what moved.
"""

import json
import sys


def read_expected(path):
    expected = {}
    with open(path, encoding="utf-8") as fh:
        for lineno, line in enumerate(fh, 1):
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            fields = line.split()
            if len(fields) != 4:
                sys.exit("%s:%d: expected 4 fields, got %d"
                         % (path, lineno, len(fields)))
            suite, passed, failed, skipped = fields
            expected[suite] = (int(passed), int(failed), int(skipped))
    if not expected:
        sys.exit("%s: no expected results in this file" % path)
    return expected


def read_results(path):
    results = {}
    with open(path, encoding="utf-8") as fh:
        for lineno, line in enumerate(fh, 1):
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
            except ValueError as exc:
                sys.exit("%s:%d: not valid JSON: %s" % (path, lineno, exc))
            summary = obj["summary"]
            results[obj["suite"]] = (summary["passed"], summary["failed"],
                                     summary["skipped"])
    return results


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__.strip())

    expected = read_expected(sys.argv[1])
    results = read_results(sys.argv[2])
    failures = 0

    for suite in sorted(expected):
        want = expected[suite]
        got = results.get(suite)
        if got is None:
            print("  %-10s MISSING (suite produced no result)" % suite)
            failures += 1
        elif got != want:
            print("  %-10s CHANGED  expected %d/%d/%d, got %d/%d/%d"
                  " (passed/failed/skipped)" % ((suite,) + want + got))
            failures += 1
        else:
            print("  %-10s ok       %d passed, %d failed, %d skipped" %
                  ((suite,) + got))

    for suite in sorted(set(results) - set(expected)):
        print("  %-10s not listed in the expected results" % suite)

    if failures:
        print("\n%d suite(s) differ from the expected results." % failures)
        return 1

    print("\nAll suites match the expected results.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

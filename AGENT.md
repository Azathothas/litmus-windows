# Testing a WebDAV server with litmus

This is for an agent that is building or debugging a WebDAV server and
wants to use litmus to check it. It assumes you can run commands and
read JSON. For build instructions see [README.md](README.md).

The short version: run `basic` first, fix everything it reports, then
work outward. Use `--json`. Treat a failure as a real bug in your server
until you have evidence otherwise.

## Invoking a suite

Each suite is a separate executable taking a URL:

```bash
./basic.exe http://127.0.0.1:8080/dav/
```

With credentials, as two extra positional arguments:

```bash
./basic.exe http://127.0.0.1:8080/dav/ username password
```

Machine-readable, which is what you want:

```bash
./basic.exe --json http://127.0.0.1:8080/dav/
```

Three things matter about the URL:

* It must be a collection that already exists. litmus does not create it.
* It must end in a slash.
* litmus creates a collection called `litmus` inside it, and does not
  reliably clean up afterwards. **Use a fresh, empty directory for every
  run.** If a `litmus` collection is left over, `MKCOL` returns 405, the
  `begin` test fails fatally, and every other test in the suite reports
  `notrun`. This looks like a catastrophic server failure and is not
  one. It is the single most common way to waste time with this tool.

The exit status is the number of failed tests, so zero means everything
passed.

## Which suite to run

Run them in this order. Each one assumes the ones before it work.

| Suite | Tests | What it exercises |
| --- | --- | --- |
| `basic` | 16 | `OPTIONS` and the `DAV:` header, `PUT`/`GET` with a byte comparison, `MKCOL`, `DELETE`, and the error cases: `PUT` with no parent collection, `MKCOL` over an existing plain resource, `MKCOL` with a request body, `DELETE` of a URL with a fragment. Start here. If this fails nothing else is meaningful. |
| `http` | 4 | Protocol level rather than WebDAV level. Mainly that `Expect: 100-continue` is handled. Skipped over HTTPS. |
| `copymove` | 13 | `COPY` and `MOVE` across the combinations that matter: overwrite true and false, destination existing and not, collection and non-collection, absolute destination URLs, and `Depth: 0` copy of a collection. |
| `props` | 33 | `PROPFIND` and `PROPPATCH`. Setting, getting, replacing and deleting dead properties, XML namespace handling including high Unicode and namespace-valued properties, whether dead properties survive a `COPY`, `getlastmodified`, and malformed request handling. Needs a working property store. |
| `locks` | 40 | `LOCK` and `UNLOCK`. Exclusive and shared locks, lock discovery, refresh, `If:` conditional headers, locking a collection, indirect refresh through a member, and locking an unmapped URL. Requires DAV class 2. |
| `largefile` | 3 | A single `PUT` of about 2 GB, then a `GET` back. Catches 32-bit size arithmetic. Slow, and needs the disk space. |
| `protected` | 26 | Whether a metadata directory is reachable over DAV. Defaults to `.DAV`, which tests for CVE-2026-42535 in `mod_dav_fs`. Set `$TEST_PROTECTED` to check a different name. |
| `lockbomb`, `lockbomb-single` | 1 | Stress test: 20,000 `LOCK`/`UNLOCK` cycles. `lockbomb` runs 20 threads in parallel, `lockbomb-single` runs one. Not a compliance test. Use it to look for leaks and lock-table exhaustion. |

`locks` reports 40 tests but many are conditional. If your server does
not advertise class 2 in the `DAV:` header, the `precond` test returns
`SKIPREST` and the whole suite stops. That is correct behaviour, not a
failure.

## Reading the results

### Statuses

| Status | Meaning |
| --- | --- |
| `pass` | Test succeeded. |
| `fail` | Test failed. Read `context`. |
| `fatal` | Test failed and stopped the suite. Everything after it is `notrun`. |
| `skip` | Precondition not met, so the test did not run. |
| `notrun` | Never reached, because an earlier test returned `fatal` or skipped the rest. |
| `xfail` | Failed, and was expected to. Counts as a pass. |
| `oops` | The harness got a return value it does not understand. A litmus bug. |

**`skip` and `notrun` are usually consequences, not independent
findings.** A run showing four failures and twenty-eight skips does not
mean twenty-eight separate problems. It almost always means one early
failure left the server in a state the later tests could not use. Fix
the first failure and re-run before reading anything into the rest.

The clearest example is `locks`. If `lock_excl` cannot get a lock token,
every test that needs a token is skipped. That is one bug with
twenty-eight downstream effects.

Read failures in order and fix the first one first.

### JSON shape

One object per suite, on stdout, nothing else:

```json
{
  "suite": "copymove",
  "target": "http://127.0.0.1:8080/dav/",
  "duration": 5.790,
  "tests": [
    {"name": "copy_init", "status": "pass", "duration": 0.061},
    {"name": "copy_shallow", "status": "fail", "duration": 0.376,
     "context": "DELETE on `/dav/litmus/ccdest/foo' should fail with 404: got 204"},
    {"name": "move", "status": "pass", "duration": 0.122,
     "warnings": ["MOVE did not return 201"]}
  ],
  "summary": {"total": 13, "passed": 12, "failed": 1,
              "skipped": 0, "notrun": 0, "warnings": 0}
}
```

Fields:

* `suite` is the executable's name without path or `.exe`.
* `target` is the URL you passed.
* `duration` is seconds, for the suite and for each test.
* `context` is the failure message. Absent when the test set none. This
  is the field to read: it names the method, the path, and the
  difference between what was expected and what happened.
* `warnings` is an array of strings, present only when a test issued
  warnings. A warning is a spec deviation that litmus decided not to
  fail on, usually a wrong-but-tolerable status code. Worth fixing but
  not urgent.
* `summary.total` counts every test defined in the suite, so
  `passed + failed + skipped + notrun` equals it.

Running several suites through the `litmus` script with `--json` gives
one object per line, which is JSON Lines. Parse it line by line.

Suggested triage order: any test with status `fatal` first, then `fail`
in array order, then `warnings`, then ignore `skip` and `notrun` until
you have re-run.

## Telling a server bug from a harness problem

Default to assuming your server is wrong. litmus is old, widely used,
and its tests are derived directly from RFC 4918. When it says a
response was wrong it is usually right.

Signs it is your server:

* The `context` names a specific method, path and status code. That
  message is generated from an actual response.
* The failure is reproducible against a fresh directory.
* Reading the cited RFC section agrees with litmus.

Signs it is the environment, not your server:

* Everything fails at `begin` with 409 or 405. That is the leftover
  collection problem, or the parent collection does not exist.
* `props` fails almost entirely with 403 on `PROPPATCH`. Your server has
  no dead property storage enabled. That is a configuration problem.
* Every suite fails identically at the first request. Check the URL,
  the port, and authentication.
* The whole `locks` suite skips. Your server does not advertise class 2.

To see exactly what went over the wire, use `--verbose`, which writes
the full protocol trace to stderr while leaving stdout clean:

```bash
./locks.exe --json --verbose http://127.0.0.1:8080/dav/ > result.json 2> trace.log
```

Without `--verbose` the same trace is appended to `debug.log`. Every
request litmus sends carries an `X-Litmus` header naming the suite and
test, so you can match a request in your server's log to the test that
sent it. Tests using a second session send `X-Litmus-Second` instead.

## Do not suppress failures

A litmus failure is nearly always a real interoperability problem.
WebDAV clients in the wild depend on the behaviour these tests check.
Making the test pass by special-casing litmus, or filtering the failure
out of your CI, leaves the bug in place for real clients.

The exceptions are genuine: some tests check behaviour that a
deliberately restricted server will not implement, and a read-only
server will legitimately fail every write test. Decide those explicitly,
record the decision, and pin the expected results so a change shows up.
`tests/expected-wsgidav.txt` in this repository is an example of that
pattern, and `tests/check-expected.py` compares against it.

## Two worked examples

Both are real failures from running this fork against wsgidav 4.3.5, and
both are server bugs.

### copymove/copy_shallow

```json
{"name": "copy_shallow", "status": "fail",
 "context": "DELETE on `/dav/litmus/ccdest/foo' should fail with 404: got 204"}
```

Read it backwards. litmus copied a collection with `Depth: 0`, which per
RFC 4918 section 9.8.3 must copy the collection itself but none of its
members. It then tried to `DELETE` a member at the destination,
expecting 404 because that member should not exist. It got 204, meaning
the member was there and was deleted successfully.

So the server performed a deep copy when it was told `Depth: 0`. The bug
is in the `COPY` handler's depth handling. The `DELETE` in the message
is just the probe that detected it.

The lesson: the failing operation named in the message is often not the
broken one. It is the check. Work out what state it was verifying.

### locks/lock_excl

```json
{"name": "lock_excl", "status": "fail",
 "context": "LOCK on `/dav/litmus/lockme': Response missing activelock element"}
```

The server accepted the `LOCK` and returned success, but the response
body left out `activelock`. RFC 4918 section 9.10.1 requires the
`lockdiscovery` property in the response to contain it, because that is
where the lock token lives. Without a token the client cannot use,
refresh or release the lock.

Everything downstream then collapses: 28 of the 40 tests in `locks`
report `skip`, because they all need a token that was never obtained.
One missing XML element, one root cause, twenty-eight symptoms.

The lesson: a large number of skips points at one earlier failure. Find
it and fix it before reading anything into the rest of the run.

## Checking against a known server

If you want a server to try this against, or a way to confirm your build
works before pointing it at your own code:

```bash
./tests/wsgidav.sh --check
```

That starts wsgidav locally, runs the suites, and compares the results
against `tests/expected-wsgidav.txt`. Expected output is `basic`,
`props` and `http` fully passing, with the two known wsgidav bugs above
accounted for. If you see anything else, the build is suspect, not the
server.

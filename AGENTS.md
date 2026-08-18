# Testing a WebDAV server with litmus

This is written for an agent that is building or debugging a WebDAV
server and wants to use litmus to check it against RFC 4918. It assumes
you can run commands and parse JSON.

For build instructions see [README.md](README.md). This file is about
using the tool.

The short version:

1. Get a binary (below), or build one.
2. Point `litmus-cli basic` at an empty collection on your server.
3. Fix everything it reports, in order, then move to the next suite.
4. Use `--json` to read results and `--trace` to see what went wrong.
5. Treat a failure as a bug in your server until you have evidence
   otherwise. It usually is.

## Getting litmus

Check whether it is already here before downloading anything:

```bash
command -v litmus-cli.exe || ls ./litmus-cli.exe 2>/dev/null || echo "not installed"
```

If it is missing, take the latest release. Asset names carry no version
string, so one fixed URL always resolves to the newest one. No API call,
no `jq`, no parsing:

```bash
curl -fsSL -o litmus-cli.exe https://github.com/Azathothas/litmus-windows/releases/latest/download/litmus-cli.exe
```

```powershell
Invoke-WebRequest https://github.com/Azathothas/litmus-windows/releases/latest/download/litmus-cli.exe -OutFile litmus-cli.exe
```

It is a statically linked x86_64 Windows executable, 8595548 bytes
(8.2 MiB). It needs no MSYS2, no OpenSSL DLLs and no expat DLLs. There
is no installer and nothing to configure:

```bash
./litmus-cli.exe version
```

which prints the fork version and the bundled neon build:

```
litmus 0.18-win2
neon 0.37.1: Bundled build, Expat 2.8.1, LFS, OpenSSL 3.6.3 9 Jun 2026 (thread-safe).
```

The same release carries `litmus-windows-x86_64.zip`, holding the
executable plus `README.md`, this file and `COPYING`, at
`.../releases/latest/download/litmus-windows-x86_64.zip`.

Release tags look like `v0.18-win2`, where `0.18` is the upstream litmus
version tracked and the suffix counts fork releases against it. Every
request carries `User-Agent: litmus/0.18-win2 neon/0.37.1`, so a server
log identifies both the tool and the fork release.

## Invoking a suite

One executable, with the suite as a subcommand:

```bash
./litmus-cli.exe basic http://127.0.0.1:8080/dav/
```

With credentials, as two extra positional arguments:

```bash
./litmus-cli.exe basic http://127.0.0.1:8080/dav/ username password
```

Machine-readable, which is what you want:

```bash
./litmus-cli.exe basic --json http://127.0.0.1:8080/dav/
```

`litmus-cli list` prints the suites; `litmus-cli all` runs the standard
five in order against one URL; `litmus-cli <suite> --help` prints the
options.

Three things matter about the URL:

* It must be a collection that already exists. litmus does not create it.
* It must end in a slash.
* litmus creates a collection called `litmus` inside it, and does not
  reliably clean up afterwards. **Use a fresh, empty directory for every
  run.** If a `litmus` collection is left over, `MKCOL` returns 405, the
  `begin` test fails fatally, and every other test reports `notrun`.
  This looks like a catastrophic server failure and is not one. It is
  the single most common way to waste time with this tool.

The exit status is the number of failed tests, so zero means everything
passed. It is capped at 125, so it cannot be mistaken for a shell's
signalled-exit range. A suite that aborts early still exits non-zero.

`litmus-cli all` runs the suites against the same URL in one process. If
one suite leaves a collection the next cannot remove — a locked
collection, for instance — the next suite's `MKCOL` fails with 405. Give
each suite its own collection if that matters to you; that is what
`tests/wsgidav.sh` does.

Useful environment variables:

| Variable | Effect |
| --- | --- |
| `TEST_COLOUR` | `0` disables colour, `1` forces it. Set `0` when capturing output. |
| `TEST_QUIET` | `1` for the compact one-line-per-suite format. |
| `TEST_NODEBUG` | Set to anything to stop `debug.log` and `child.log` being written. |
| `TEST_PROTECTED` | Collection name the `protected` suite checks. Defaults to `.DAV`. |

## Which suite to run

Run them in this order. Each assumes the ones before it work.

| Suite | Tests | What it exercises |
| --- | --- | --- |
| `basic` | 17 | `OPTIONS` and the `DAV:` header, `PUT`/`GET` with a byte comparison, `MKCOL`, `DELETE`, and the error cases: `PUT` with no parent collection, `MKCOL` over an existing plain resource, a second `MKCOL` on an existing collection, `MKCOL` with a request body, `MKCOL` of a name containing an escaped space, `DELETE` of a URL with a fragment. Start here. If this fails nothing else is meaningful. |
| `http` | 4 | Protocol level rather than WebDAV level. Mainly that `Expect: 100-continue` is handled. Skipped over HTTPS. |
| `copymove` | 15 | `COPY` and `MOVE` across the combinations that matter: overwrite true and false, destination existing and not, collection and non-collection, absolute destination URLs, `Depth: 0` copy of a collection, and a byte-for-byte comparison of the body at the destination. |
| `props` | 33 | `PROPFIND` and `PROPPATCH`. Setting, getting, replacing and deleting dead properties, XML namespace handling including high Unicode and namespace-valued properties, whether dead properties survive a `COPY`, `getlastmodified`, and malformed request handling. Needs a working dead-property store. |
| `locks` | 43 | `LOCK` and `UNLOCK`. Exclusive and shared locks, lock discovery, refresh, `If:` conditional headers, locking a collection, indirect refresh through a member, locking an unmapped URL, and the lock-null resource that creates. Requires DAV class 2. |
| `largefile` | 5 | A single `PUT` of 2147549184 bytes (2.0 GiB), then a `GET` back. Catches 32-bit size arithmetic. Slow, and needs the disk space. |
| `protected` | 30 | Whether a metadata collection is reachable over DAV. Defaults to `.DAV`, which tests for CVE-2026-42535 in `mod_dav_fs`. Set `$TEST_PROTECTED` to check a different name. |
| `lockbomb`, `lockbomb-single` | 3 each | Stress test: 20000 `LOCK`/`UNLOCK` cycles. `lockbomb` runs 20 threads in parallel, `lockbomb-single` runs one; `--threads=N` overrides either. Not a compliance test. Use it to look for leaks and lock-table exhaustion. |

Counts include `begin` and `finish`, which every suite runs.

`locks` defines 43 tests but many are conditional. If your server does
not advertise class 2 in its `DAV:` header, the `precond` test returns
`SKIPREST` and the suite stops. That is correct behaviour, not a
failure. Implement class 2 first if you intend to support locking.

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
| `oops` | The harness got a return value it does not understand. A litmus bug, worth reporting. |

**`skip` and `notrun` are consequences, not independent findings.** A run
showing four failures and thirty-one skips does not mean thirty-one
problems. It almost always means one early failure left the server in a
state the later tests could not use. Fix the first failure and re-run
before reading anything into the rest.

Triage order: `fatal` first, then `fail` in array order, then warnings.
Ignore `skip` and `notrun` until you have re-run.

### JSON shape

One object per suite, on stdout, nothing else:

```json
{
  "suite": "copymove",
  "target": "http://127.0.0.1:8080/dav/",
  "started": "2026-08-18T13:04:54.429Z",
  "duration": 5.790,
  "tests": [
    {"name": "copy_init", "status": "pass", "duration": 0.061},
    {"name": "copy_shallow", "status": "fail", "duration": 0.376,
     "context": "DELETE on `/dav/litmus/ccdest/foo' should fail with 404: got 204",
     "error": {"op": "DELETE", "path": "/dav/litmus/ccdest/foo", "status": 204}},
    {"name": "move", "status": "pass", "duration": 0.122,
     "warnings": ["MOVE did not return 201"]}
  ],
  "summary": {"total": 15, "passed": 14, "failed": 1,
              "skipped": 0, "notrun": 0, "warnings": 0}
}
```

| Field | Notes |
| --- | --- |
| `suite` | Suite name, as given on the command line. |
| `target` | The URL you passed. |
| `started` | When the first test began, ISO 8601 in UTC with millisecond precision, always suffixed `Z`. Truncated rather than rounded, so it never names a moment that had not happened yet. Absent only if the clock could not be read. |
| `duration` | Seconds, to millisecond resolution. Present on the run and on each test. Wall-clock, so it includes server time and network time. |
| `tests[].context` | The failure message. Absent when the test set none. Human-readable: it names the method, the path, and the difference between expected and actual. Prose, and not a stable interface — branch on `error` instead. |
| `tests[].error` | Machine-readable classification of a failure. See below. |
| `tests[].warnings` | Array of strings, present only when a test issued warnings. A warning is a spec deviation litmus chose not to fail on, usually a tolerable but wrong status code. Worth fixing, not urgent. |
| `summary.total` | Every test defined in the suite, so `passed + failed + skipped + notrun` equals it. |
| `summary.warnings` | Total across the run, including any issued before the first test. |

### Classifying a failure

`context` is prose. To branch on the kind of failure without matching
strings, read `error`:

```json
"error": {"op": "DELETE", "path": "/dav/litmus/ccdest/foo", "status": 204}
```

It describes the last request the test made and the response it got,
which is the request the failure is about. This is part of the JSON
contract:

| Field | Notes |
| --- | --- |
| `error` | Present only on a test whose `status` is `fail` or `fatal`, and only if the test got as far as sending a request. A test that failed a precondition without touching the network has no `error`. |
| `error.op` | The HTTP method, as sent. One of `OPTIONS`, `GET`, `HEAD`, `PUT`, `DELETE`, `MKCOL`, `COPY`, `MOVE`, `PROPFIND`, `PROPPATCH`, `LOCK`, `UNLOCK`, plus `CONNECT` when tunnelling through a proxy. That set is closed: litmus chooses the method, so a server cannot introduce a new one. |
| `error.path` | The request target exactly as it went on the wire. Normally an absolute path; `*` for `OPTIONS *`, and `host:port` for `CONNECT`. |
| `error.status` | The response status as an integer, or `null` when no response arrived at all — a refused connection, a timeout, a TLS failure. `null` and a missing `error` mean different things: `null` means litmus asked and got nothing back. |

Two consequences worth knowing. First, `op` is the operation that was
performed, which is not always the operation that is broken: a test
often probes with one method to check what an earlier one did, as in the
`copy_shallow` example below. Second, a redirect or an authentication
challenge is retried by neon on the same request, so `status` is the
final response, not the intermediate one.

```python
if t["status"] in ("fail", "fatal"):
    err = t.get("error")
    if err is None:
        kind = "no request made"
    elif err["status"] is None:
        kind = "transport"          # never reached the server
    else:
        kind = "%s -> %d" % (err["op"], err["status"])
```

`litmus-cli all --json`, and the `litmus` driver script with `--json`,
give one object per suite, one per line, which is JSON Lines. Parse it
line by line.

A minimal consumer:

```python
import json, subprocess

def run(suite, url):
    p = subprocess.run(["./litmus-cli.exe", suite, "--json", url],
                       capture_output=True, text=True)
    return json.loads(p.stdout)

result = run("basic", "http://127.0.0.1:8080/dav/")
for t in result["tests"]:
    if t["status"] in ("fail", "fatal"):
        print(t["name"], "->", t.get("context", "no detail"))
        break          # fix this one first; the rest are probably fallout
```

## Seeing what actually happened

`--trace` is the flag that matters when a test fails and you do not know
why. It dumps every request and response, tagged with the test that
issued it:

```bash
./litmus-cli.exe copymove --json --trace=wire.log http://127.0.0.1:8080/dav/ > result.json
```

Sent lines are prefixed `>`, received lines `<`. Bodies are printed in
square brackets rather than prefixed, because PROPFIND and LOCK bodies
are XML and nearly every line of those starts with `<`:

```
--- basic 2 (put_get) ---
> PUT /dav/litmus/res HTTP/1.1
> User-Agent: litmus/0.18-win2 neon/0.37.1
> Host: 127.0.0.1:8080
> Content-Length: 41
> X-Litmus: basic: 2 (put_get)
Body block (41 bytes):
[This is
a test file.
]
< HTTP/1.1 201 Created
< etag: "6144f739-1787054952-41"
< content-length: 0
<
```

With no filename it goes to stderr. Use `-` for stdout. Stdout is only
ever used for results, so `--json --trace` gives clean JSON on stdout
and the trace wherever you asked for it.

To find the exchange behind a specific failure, grep the trace for the
test name:

```bash
grep -A 40 "(copy_shallow)" wire.log
```

`--verbose` widens the trace to everything neon reports, including
socket, XML parser and authentication detail. Combined with `--trace` it
writes that wider trace to the trace destination. Reach for it when the
problem looks like a connection, TLS or authentication issue rather than
a protocol one.

Every request also carries an `X-Litmus` header naming the suite and
test, so you can match a request in **your server's own log** to the
test that sent it. Requests from the second session, used by the tests
that need two clients, carry `X-Litmus-Second` instead.

The `lockbomb` worker threads and the `bench` transfers are not traced.
They open their own sessions on purpose: writing to one stream from
twenty threads would need locking, tracing 400000 lock/unlock round
trips is not useful, and dumping the benchmark's bodies would dominate
the measurement.

## Measuring throughput

`litmus-cli bench` is not a compliance test and passes or fails nothing.
It measures how fast the server moves bytes and how quickly it accepts
connections:

```bash
./litmus-cli.exe bench --json http://127.0.0.1:8080/dav/
```

```json
{"bench":"litmus","target":"http://127.0.0.1:8931/b/",
 "started":"2026-08-18T15:10:20.630Z","duration":0.653,"concurrency":8,
 "payload":{"bytes":4194304,"source":"prng"},
 "connection":{"keepalive":true,"connect_timeout":null,"read_timeout":null},
 "scenarios":[
   {"name":"upload-small","files":16,"bytes":1048576,"duration":0.010,"mib_per_s":100.22,"errors":0},
   {"name":"download-small","files":16,"bytes":1048576,"duration":0.357,"mib_per_s":2.80,"errors":0},
   {"name":"upload-large","files":1,"bytes":8388608,"duration":0.010,"mib_per_s":833.59,"errors":0},
   {"name":"download-large","files":1,"bytes":8388608,"duration":0.015,"mib_per_s":534.77,"errors":0}],
 "latency":{"unit":"ms","attempted":5,"failed":0,
            "min":0.360,"mean":0.425,"max":0.559,"jitter":0.063}}
```

Rates are MiB/s, latencies milliseconds, byte counts exact. `payload`
describes the buffer being streamed: 4194304 bytes (4.0 MiB) of
xorshift64\* output, filled once and read from a rotating offset, so a
server cannot compress or deduplicate its way to a better number.
`connection` reports keep-alive and the two timeouts, because runs made
with different settings are not comparable; `null` means neon's own
default. Tune the work with `--files`, `--size`, `--large`,
`--concurrency` and `--pings`.

Exit status is 0 if every transfer and probe succeeded, 1 otherwise.

## Telling a server bug from an environment problem

Default to assuming your server is wrong. litmus is old, widely used,
and its tests come directly from RFC 4918.

It is your server when:

* The `context` names a specific method, path and status code. That
  message is generated from a real response.
* It reproduces against a fresh directory.
* Reading the cited RFC section agrees with litmus.

It is the environment when:

* Everything fails at `begin` with 409 or 405. Leftover `litmus`
  collection, or the parent collection does not exist.
* `props` fails almost entirely with 403 on `PROPPATCH`. No dead
  property storage. That is configuration, not a bug.
* Every suite fails identically at the first request. Check URL, port,
  and credentials.
* The whole `locks` suite skips. Your server does not advertise class 2.

## Do not suppress failures

A litmus failure is nearly always a real interoperability problem.
WebDAV clients in the wild depend on the behaviour these tests check.
Making a test pass by special-casing litmus, or filtering the failure
out of CI, leaves the bug in place for real clients.

There are genuine exceptions. A deliberately read-only server will fail
every write test. A server that does not implement locking will skip all
of `locks`. Decide those explicitly, record the decision, and pin the
expected results so that a *change* still shows up. This repository does
exactly that in `tests/expected-wsgidav.txt`, compared by
`tests/check-expected.py`:

```
basic     17 0 0
copymove  14 1 0
props     33 0 0
locks      8 4 31
http       4 0 0
```

Four columns: suite, passed, failed, skipped. Copy the pattern.

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

So the server performed a deep copy when told `Depth: 0`. The bug is in
the `COPY` handler's depth handling. The `DELETE` is only the probe that
detected it.

The lesson: the operation named in the message is often not the broken
one. It is the check. Work out what state it was verifying.

### locks/lock_excl

```json
{"name": "lock_excl", "status": "fail",
 "context": "LOCK on `/dav/litmus/lockme': Response missing activelock for (null)"}
```

wsgidav accepted the `LOCK` and returned success, but labelled the
response `Content-Type: application; charset=utf-8`. That is not a media
type at all, let alone an XML one, so neon discarded the body unparsed
and litmus never saw the `activelock` element the lock token lives in.
Without a token the client cannot use, refresh or release the lock. (The
trailing `(null)` is the missing token being interpolated into neon's
own message; it carries no information.)

Everything downstream collapses: 31 of the 43 tests in `locks` report
`skip`, because they all need a token that was never obtained. One
malformed header, one root cause, thirty-one symptoms.

The lesson: a large number of skips points at one earlier failure. Find
it and fix that.

## A working loop

```bash
# 1. fresh directory on the server for this run
curl -X MKCOL http://127.0.0.1:8080/dav/run-$(date +%s)/

# 2. run the suite, capturing results and traffic separately
./litmus-cli.exe basic --json --trace=wire.log http://127.0.0.1:8080/dav/run-XXXX/ > result.json

# 3. first real failure
python -c "
import json
o = json.load(open('result.json'))
bad = [t for t in o['tests'] if t['status'] in ('fail','fatal')]
print(bad[0]['name'], '->', bad[0].get('context')) if bad else print('all passed')
"

# 4. the exchange behind it
grep -A 40 "(put_get)" wire.log
```

Repeat until `basic` is clean, then move to `http`, `copymove`, `props`,
`locks`.

## Checking your build works

Before blaming your server, confirm the binary is sound. This needs no
server, no network and no source tree, and takes under a second:

```bash
./litmus-cli.exe selftest
```

It runs three synthetic suites whose tests return fixed results, so the
output depends only on the result harness. Expect exit status 7: six
deliberate failures in the first suite and one in the second. Add
`--json` to see the machine-readable form of every status, including
the `error` object and the escaping rules, without needing a server that
will produce them.

If you have the source tree, `./tests/harness.sh` compares all three
output modes against checked-in expected output.

To check the suites against a real server instead:

```bash
./tests/wsgidav.sh --check
```

That starts a local wsgidav, runs the suites, and compares against
`tests/expected-wsgidav.txt`. Expected result is `basic`, `props` and
`http` fully passing, with the two known wsgidav bugs above accounted
for. Anything else means the build is suspect, not the server.

`./tests/godav.sh --check` does the same against
`golang.org/x/net/webdav`, which locks correctly but has no dead
property store, comparing against `tests/expected-godav.txt`. Use it
whenever lock behaviour is what you care about: **the lock tests cannot
run against wsgidav at all.** CI runs both gates.

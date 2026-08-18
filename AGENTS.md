# Testing a WebDAV server with litmus

This is written for an agent that is building or debugging a WebDAV
server and wants to use litmus to check it against RFC 4918. It assumes
you can run commands and parse JSON.

For build instructions see [README.md](README.md). This file is about
using the tool.

The short version:

1. Get a binary (below), or build one.
2. Point `basic` at an empty collection on your server.
3. Fix everything it reports, in order, then move to the next suite.
4. Use `--json` to read results and `--trace` to see what went wrong.
5. Treat a failure as a bug in your server until you have evidence
   otherwise. It usually is.

## Getting litmus

Check whether it is already here before downloading anything:

```bash
command -v basic.exe || ls ./basic.exe 2>/dev/null || echo "not installed"
```

If it is missing, take the latest release. These are statically linked
x86_64 Windows executables. They need no MSYS2, no OpenSSL DLLs and no
expat DLLs.

With the GitHub CLI, which resolves "latest" for you:

```bash
gh release download --repo Azathothas/litmus-windows --pattern "litmus-windows-x86_64.zip" --clobber
```

Without it, using the API so you do not have to know the version:

```bash
curl -sL "https://api.github.com/repos/Azathothas/litmus-windows/releases/latest" \
  | grep -o '"browser_download_url": *"[^"]*litmus-windows-x86_64.zip"' \
  | cut -d'"' -f4 \
  | xargs curl -sL -o litmus-windows-x86_64.zip
```

From PowerShell:

```powershell
$u = (Invoke-RestMethod https://api.github.com/repos/Azathothas/litmus-windows/releases/latest).assets |
     Where-Object name -eq 'litmus-windows-x86_64.zip' | Select-Object -First 1 -ExpandProperty browser_download_url
Invoke-WebRequest $u -OutFile litmus-windows-x86_64.zip
Expand-Archive litmus-windows-x86_64.zip -DestinationPath litmus -Force
```

Then unzip and check it runs:

```bash
unzip -o litmus-windows-x86_64.zip
./basic.exe --help
```

The zip contains one executable per suite, plus `README.md`, this file
and `COPYING`. There is no installer and nothing to configure.

Versions: this fork tracks litmus 0.18 and bundles neon 0.37.1. Release
tags look like `v0.18-win1`, where the suffix counts fork releases
against the same upstream version.

The executables do not report a version. `./litmus --version` prints
`litmus 0.18`, which is the upstream version, not the fork release; the
driver script is also not in the zip, because it needs a POSIX shell. To
know which fork release you have, keep the tag you downloaded. Every
request carries `User-Agent: litmus/0.18 neon/0.37.1`, so a server log
identifies the tool but not the fork release either.

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
  `begin` test fails fatally, and every other test reports `notrun`.
  This looks like a catastrophic server failure and is not one. It is
  the single most common way to waste time with this tool.

The exit status is the number of failed tests, so zero means everything
passed. A suite that aborts early still exits non-zero.

Useful environment variables:

| Variable | Effect |
| --- | --- |
| `TEST_COLOUR` | `0` disables colour, `1` forces it. Set `0` when capturing output. |
| `TEST_QUIET` | `1` for the compact one-line-per-suite format. |
| `TEST_NODEBUG` | Set to anything to stop `debug.log` and `child.log` being written. |
| `TEST_PROTECTED` | Directory name the `protected` suite checks. Defaults to `.DAV`. |

## Which suite to run

Run them in this order. Each assumes the ones before it work.

| Suite | Tests | What it exercises |
| --- | --- | --- |
| `basic` | 16 | `OPTIONS` and the `DAV:` header, `PUT`/`GET` with a byte comparison, `MKCOL`, `DELETE`, and the error cases: `PUT` with no parent collection, `MKCOL` over an existing plain resource, `MKCOL` with a request body, `DELETE` of a URL with a fragment. Start here. If this fails nothing else is meaningful. |
| `http` | 4 | Protocol level rather than WebDAV level. Mainly that `Expect: 100-continue` is handled. Skipped over HTTPS. |
| `copymove` | 13 | `COPY` and `MOVE` across the combinations that matter: overwrite true and false, destination existing and not, collection and non-collection, absolute destination URLs, and `Depth: 0` copy of a collection. |
| `props` | 33 | `PROPFIND` and `PROPPATCH`. Setting, getting, replacing and deleting dead properties, XML namespace handling including high Unicode and namespace-valued properties, whether dead properties survive a `COPY`, `getlastmodified`, and malformed request handling. Needs a working dead-property store. |
| `locks` | 40 | `LOCK` and `UNLOCK`. Exclusive and shared locks, lock discovery, refresh, `If:` conditional headers, locking a collection, indirect refresh through a member, and locking an unmapped URL. Requires DAV class 2. |
| `largefile` | 5 | A single `PUT` of about 2 GB, then a `GET` back. Catches 32-bit size arithmetic. Slow, and needs the disk space. |
| `protected` | 30 | Whether a metadata directory is reachable over DAV. Defaults to `.DAV`, which tests for CVE-2026-42535 in `mod_dav_fs`. Set `$TEST_PROTECTED` to check a different name. |
| `lockbomb`, `lockbomb-single` | 3 | Stress test: 20,000 `LOCK`/`UNLOCK` cycles. `lockbomb` runs 20 threads in parallel, `lockbomb-single` runs one. Not a compliance test. Use it to look for leaks and lock-table exhaustion. |

`locks` defines 40 tests but many are conditional. If your server does
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
showing four failures and twenty-eight skips does not mean twenty-eight
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

| Field | Notes |
| --- | --- |
| `suite` | Executable name, without path or `.exe`. |
| `target` | The URL you passed. |
| `duration` | Seconds. Present on the run and on each test. |
| `tests[].context` | The failure message. Absent when the test set none. This is the field to read: it names the method, the path, and the difference between expected and actual. |
| `tests[].warnings` | Array of strings, present only when a test issued warnings. A warning is a spec deviation litmus chose not to fail on, usually a tolerable but wrong status code. Worth fixing, not urgent. |
| `summary.total` | Every test defined in the suite, so `passed + failed + skipped + notrun` equals it. |
| `summary.warnings` | Total across the run, including any issued before the first test. |

Running several suites through the `litmus` driver script with `--json`
gives one object per suite, one per line, which is JSON Lines. Parse it
line by line.

A minimal consumer:

```python
import json, subprocess

def run(suite, url):
    p = subprocess.run([f"./{suite}.exe", "--json", url],
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
./copymove.exe --json --trace=wire.log http://127.0.0.1:8080/dav/ > result.json
```

Sent lines are prefixed `>`, received lines `<`. Bodies are printed in
square brackets rather than prefixed, because PROPFIND and LOCK bodies
are XML and nearly every line of those starts with `<`:

```
--- basic 2 (put_get) ---
> PUT /dav/litmus/res HTTP/1.1
> User-Agent: litmus/0.18 neon/0.37.1
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
basic     16 0 0
copymove  12 1 0
props     33 0 0
locks      8 4 28
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
 "context": "LOCK on `/dav/litmus/lockme': Response missing activelock element"}
```

The server accepted the `LOCK` and returned success, but the response
body left out `activelock`. RFC 4918 section 9.10.1 requires the
`lockdiscovery` property in that response to contain it, because that is
where the lock token lives. Without a token the client cannot use,
refresh or release the lock.

Everything downstream collapses: 28 of the 40 tests in `locks` report
`skip`, because they all need a token that was never obtained. One
missing XML element, one root cause, twenty-eight symptoms.

The lesson: a large number of skips points at one earlier failure. Find
it and fix that.

## A working loop

```bash
# 1. fresh directory on the server for this run
curl -X MKCOL http://127.0.0.1:8080/dav/run-$(date +%s)/

# 2. run the suite, capturing results and traffic separately
./basic.exe --json --trace=wire.log http://127.0.0.1:8080/dav/run-XXXX/ > result.json

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

If you want to confirm litmus itself is behaving before pointing it at
your own server, and you have the source tree:

```bash
./tests/wsgidav.sh --check
```

That starts a local wsgidav, runs the suites, and compares against
`tests/expected-wsgidav.txt`. Expected result is `basic`, `props` and
`http` fully passing, with the two known wsgidav bugs above accounted
for. Anything else means the build is suspect, not the server.

# litmus for Windows

litmus is a compliance test suite for WebDAV servers, checking them
against [RFC 4918](https://www.rfc-editor.org/rfc/rfc4918). This is a
port of [notroj/litmus](https://github.com/notroj/litmus) that builds
and runs on Windows as a native MinGW-w64 program: no Cygwin runtime, no
WSL, no MSYS2 needed at run time.

Everything is one executable, `litmus-cli`, with the suite as a
subcommand:

```bash
litmus-cli basic http://dav.example.com/path/
```

Beyond the suites it adds `--json` for machine-readable results,
`--trace` for a full wire dump, and a `bench` subcommand that measures
transfer throughput and connect latency.

This suite covers more than litmus 0.18, the version it was forked from.
The extra test cases are listed under [Test
coverage](#test-coverage).

## Getting a binary

Released builds are statically linked x86_64 Windows executables. They
need no MSYS2, no OpenSSL DLLs and no expat DLLs. Asset names carry no
version, so these URLs always resolve to the newest release:

```bash
curl -fsSL -o litmus-cli.exe https://github.com/Azathothas/litmus-windows/releases/latest/download/litmus-cli.exe
```

```powershell
Invoke-WebRequest https://github.com/Azathothas/litmus-windows/releases/latest/download/litmus-cli.exe -OutFile litmus-cli.exe
```

The same release also carries `litmus-windows-x86_64.zip`, which holds
the executable plus `README.md`, `AGENTS.md` and `COPYING`:

```bash
curl -fsSL -o litmus-windows-x86_64.zip https://github.com/Azathothas/litmus-windows/releases/latest/download/litmus-windows-x86_64.zip
```

Check it runs:

```bash
./litmus-cli.exe version
```

Release tags look like `v0.18-win2`. The `0.18` is the upstream litmus
version this fork tracks; the suffix counts releases of the fork against
it. Every release bundles neon 0.37.1.

## Building

You need MSYS2, and nothing else installed first.

### 1. Install MSYS2

Download the installer from <https://www.msys2.org/> and run it, or with
winget:

```bash
winget install MSYS2.MSYS2
```

The default install location is `C:\msys64`.

### 2. Install the toolchain

Open the **MSYS2 UCRT64** shortcut from the Start menu — not the plain
MSYS2 shell, and not MINGW64. Then:

```bash
pacman -S --needed autoconf automake libtool m4 make pkgconf mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-openssl mingw-w64-ucrt-x86_64-expat mingw-w64-ucrt-x86_64-pkgconf
```

If pacman asks you to close the terminal and reopen it, do that and run
the command again.

### 3. Clone

```bash
git clone https://github.com/Azathothas/litmus-windows
```

There is no `--recurse-submodules` step. neon is checked into the tree.

### 4. Build

Two build paths produce the same executable. Autotools:

```bash
./autogen.sh && ./configure --with-ssl=openssl && make
```

That gives `litmus-cli.exe` and a shell script called `litmus` that runs
the standard suites in turn. You do not need `--with-included-neon`;
there is no system neon on Windows to prefer, so the bundled copy is
used regardless.

Or without autotools, using the configuration checked in at
`win32/config.h`:

```bash
make -f Makefile.w32
```

This needs only GNU make and a MinGW-w64 compiler whose sysroot has
OpenSSL and expat, which a UCRT64 shell already has. It is faster and
has fewer moving parts.

To build an executable that runs on a machine with no MSYS2:

```bash
make -f Makefile.w32 STATIC=1
```

That links OpenSSL and expat in statically. The result is 8595548 bytes
(8.2 MiB) against 1392028 bytes (1.3 MiB) dynamically linked, and
depends only on DLLs that ship with Windows. This is how the released
binaries are built.

If OpenSSL and expat live outside the toolchain, point `PREFIX` at a
sysroot providing both:

```bash
make -f Makefile.w32 PREFIX=/some/sysroot
```

`PREFIX` must belong to the same toolchain as the compiler. Aiming a
standalone MinGW-w64 at MSYS2's `lib` directory pulls in MSYS2's C
runtime as well, and the link fails on `_gnu_exception_handler` and
`__mingw_oldexcpt_handler`.

## Running it

```bash
litmus-cli COMMAND [OPTIONS] URL [username password]
```

```bash
litmus-cli basic http://dav.example.com/path/
litmus-cli locks --json http://dav.example.com/path/
litmus-cli all http://dav.example.com/path/ jim 2518
```

`litmus-cli list` prints the suites; `litmus-cli COMMAND --help` prints
the options a command takes.

| Command | What it does |
| --- | --- |
| `basic` | `PUT`, `GET`, `DELETE` and `MKCOL` on plain resources and collections |
| `copymove` | `COPY` and `MOVE`, for resources and for collections |
| `props` | property handling: `PROPFIND` and `PROPPATCH` |
| `locks` | `LOCK` and `UNLOCK`, including collection and conditional requests |
| `http` | HTTP-level behaviour, currently `Expect: 100-continue` |
| `largefile` | a 2147549184 byte (2.0 GiB) `PUT` followed by a `GET` |
| `protected` | every method must be refused on a protected metadata collection |
| `lockbomb` | lock/unlock stress, 20 worker threads by default |
| `lockbomb-single` | lock/unlock stress in one thread |
| `all` | runs `basic`, `copymove`, `props`, `locks` and `http` in that order |
| `bench` | throughput and connect-latency measurement, not a compliance test |
| `list` | the suites, one name and summary per line |
| `selftest` | checks this executable's own result harness; no network, no URL |
| `version` | the fork version and the bundled neon build |

The URL must be a collection that already exists and ends in a slash.
litmus creates a collection called `litmus` inside it and does not
reliably remove it afterwards, so use a fresh directory per run: a
leftover `litmus` collection makes `MKCOL` return 405 and every suite
aborts at `begin`.

The exit status is the number of failed tests, capped at 125 so it
cannot collide with the range a shell uses for signalled exits.

After `make install` there is also a `litmus` shell script, which runs
the standard suites in turn and takes the same options:

```bash
./litmus http://dav.example.com/path/ jim 2518
```

`make URL=http://dav.example.com/path/ check` does the same from the
build tree. Both are shell scripts and need an MSYS2 or Git Bash shell;
`litmus-cli all` does the same job from any shell.

## Options

```
 -p, --proxy=URL            use given proxy server URL
 -s, --system-proxy         use proxy server configuration from system
 -c, --client-cert=CERT     use given PKCS#12 client cert
 -u, --client-cert-uri=URI  use given client cert URI
 -i, --insecure             ignore TLS certificate verification failures
 -q, --quiet                use abbreviated output
 -n, --no-colour            disable colour in output
 -o, --colour               enable colour in output
 -j, --json                 write results to stdout as one JSON object
 -v, --verbose              write the protocol trace to stderr
 -t, --trace[=FILE]         dump every request and response to FILE
                            (default stderr; use - for stdout)
     --threads=N            number of worker threads (lockbomb only)
     --connect-timeout=SEC  connection timeout
     --read-timeout=SEC     response timeout
     --no-keepalive         one connection per request

bench only:
     --files=N              small files to transfer
     --size=BYTES           size of each small file
     --large=BYTES          size of the large file, 0 to skip it
     --concurrency=N        transfers in flight at once
     --pings=N              TCP connect probes, 0 to skip them
```

`--json`, `--verbose`, `--trace`, `--threads`, `--connect-timeout`,
`--read-timeout` and `--no-keepalive` are additions in this fork.

Environment variables the harness reads:

| Variable | Effect |
| --- | --- |
| `TEST_COLOUR` | `0` disables colour, `1` forces it |
| `TEST_QUIET` | `1` selects the compact one-line-per-suite format |
| `TEST_NODEBUG` | set to anything to stop `debug.log` and `child.log` being written |
| `TEST_PROTECTED` | collection name the `protected` suite checks; default `.DAV` |

Default text output is byte for byte identical to upstream's, so a
script that parses it keeps working.

## Output

### JSON

```bash
litmus-cli copymove --json http://dav.example.com/path/
```

One object on stdout and nothing else:

```json
{
  "suite": "copymove",
  "target": "http://dav.example.com/path/",
  "started": "2026-08-18T13:04:54.429Z",
  "duration": 5.790,
  "tests": [
    {"name": "copy_simple", "status": "pass", "duration": 0.061},
    {"name": "copy_shallow", "status": "fail", "duration": 0.376,
     "context": "DELETE on `/litmus/ccdest/foo' should fail with 404: got 204",
     "error": {"op": "DELETE", "path": "/litmus/ccdest/foo", "status": 204}}
  ],
  "summary": {"total": 15, "passed": 14, "failed": 1,
              "skipped": 0, "notrun": 0, "warnings": 0}
}
```

`status` is one of `pass`, `fail`, `fatal`, `skip`, `xfail`, `notrun` or
`oops`. `context` is the failure message and is absent when the test set
none. A `warnings` array appears on a test that issued warnings.
`notrun` counts tests never reached because an earlier test aborted the
suite.

`error` appears on a failing test that reached the network, and is the
stable form of the same information: the method sent, the request target
as it went on the wire, and the status the server answered with, or
`null` if nothing came back. Branch on that rather than parsing
`context`.

`started` is ISO 8601 in UTC with millisecond precision, always ending
in `Z`, truncated rather than rounded. Durations are seconds to
millisecond resolution and are wall-clock, so they include server and
network time.

`litmus-cli all --json` and the `litmus` script with `--json` give one
object per suite, one per line, which is JSON Lines.

[AGENTS.md](AGENTS.md) documents the schema in full, written for a
program consuming the output rather than a person reading it.

### Wire trace

`--trace` dumps every request and response, introduced by the suite,
test number and test name that issued it:

```bash
litmus-cli props --trace=wire.log http://127.0.0.1:8080/dav/
```

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

Sent lines are prefixed `>`, received lines `<`. Message bodies are
printed inside square brackets rather than prefixed, because `PROPFIND`
and `LOCK` bodies are XML and nearly every line of those starts with
`<`.

With no filename the trace goes to stderr; `-` sends it to stdout.
`--verbose` widens it to everything neon can report, including socket,
XML parser and authentication detail, and on its own writes to stderr.
Combined with `--trace` the wider trace goes to the trace destination,
so `--trace=wire.log --verbose` puts everything in one file. With
neither flag the same detail is appended to `debug.log`.

stdout carries results only, so the flags compose:

```bash
litmus-cli locks --json --trace=wire.log http://127.0.0.1:8080/dav/ > result.json
```

`--trace` does not cover the `lockbomb` worker threads or the `bench`
transfers. Those create their own sessions, deliberately: writing to one
stream from twenty threads would need locking, a trace of 400000
lock/unlock round trips is not useful, and dumping the benchmark's
bodies would dominate the measurement.

## Benchmarking

`bench` is not a compliance test. It measures how fast a server moves
bytes and how quickly it accepts connections.

```bash
litmus-cli bench http://dav.example.com/path/
```

```
-> benchmarking `http://127.0.0.1:8931/b/':
   started      2026-08-18T15:10:19.259Z
   concurrency  8 parallel transfers
   payload      4194304 bytes (4.0 MiB) of incompressible pseudo-random data,
                streamed from a rotating offset
   keep-alive   on, connect timeout neon default, read timeout neon default

   upload-small       16 files 1048576 bytes (1.0 MiB)           0.007 s   140.04 MiB/s  0 errors
   download-small     16 files 1048576 bytes (1.0 MiB)           0.086 s    11.67 MiB/s  0 errors
   upload-large        1 file  8388608 bytes (8.0 MiB)           0.009 s   909.83 MiB/s  0 errors
   download-large      1 file  8388608 bytes (8.0 MiB)           0.014 s   572.08 MiB/s  0 errors

   latency      20 TCP connects to 127.0.0.1 port 8931
                min 0.309 ms  mean 0.346 ms  max 0.401 ms  jitter 0.020 ms  loss 0/20

<- benchmark of `http://127.0.0.1:8931/b/' took 1.312 s wall-clock, which includes server and network time.
```

Four transfer scenarios and a TCP-connect probe. The payload is 4194304
bytes (4.0 MiB) of xorshift64\* output, filled once and streamed from a
rotating offset so the server cannot benefit from compressing or
deduplicating it. Concurrency is bounded by a pool of exactly
`--concurrency` worker threads. Rates are MiB/s, latencies milliseconds.

`--json` gives the same figures as one object, with a `scenarios` array
and a `latency` object; the connection settings are reported alongside
because two runs made with different ones are not comparable.

neon 0.37.1 speaks HTTP/1.1 only, so there is no way to pin the protocol
version for a server that behaves differently over HTTP/2. That would
need a different HTTP client.

## Test coverage

| Suite | Tests |
| --- | --- |
| `basic` | 17 |
| `copymove` | 15 |
| `props` | 33 |
| `locks` | 43 |
| `http` | 4 |
| `largefile` | 5 |
| `protected` | 30 |
| `lockbomb`, `lockbomb-single` | 3 each |

Counts include the `begin` and `finish` tests, which every suite runs.
`locks` defines 43 tests but many are conditional: a server that does
not advertise DAV class 2 makes `precond` return `SKIPREST`, and the
rest of the suite is skipped.

Beyond litmus 0.18 this fork tests:

* `basic/mkcol_percent_20` — a collection whose name contains an escaped
  space, created, listed and removed.
* `basic/mkcol_again` — a second `MKCOL` on an existing collection must
  be refused.
* `copymove/copy_content`, `copymove/move_content` — the body at the
  destination is compared byte for byte with the source, so a `COPY` or
  `MOVE` that reports success while writing the wrong bytes is caught.
* `locks/locknull`, `locks/locknull_discover`, `locks/locknull_unlock` —
  RFC 4918 section 7.3 treatment of a `LOCK` on an unmapped URL: the
  resulting lock-null resource must appear in a `PROPFIND` of its parent
  and must disappear when the lock is released.

It also warns rather than staying silent when `MKCOL` answers with
something other than 201, or `DELETE` with something other than 204, and
`PROPPATCH` results are read from inside a 207 multistatus, so a server
that refuses a `PROPPATCH` with 207 wrapping a 423 is judged on the 423.

## Testing the build

`litmus-cli selftest` checks the executable's own result harness. It
runs three synthetic suites whose tests return fixed results, makes no
network requests and takes no URL, so the output is determined entirely
by the harness: the statuses, the counting, the JSON emission and its
escaping, the notrun bookkeeping, and the state reset between suites.

```bash
./tests/harness.sh
```

captures all three output modes and compares them against
`tests/harness-expected`, which is checked in. It runs in under a
second and needs nothing installed. `./tests/harness.sh --regenerate`
rewrites the expected files after a deliberate change; read the diff
before checking it in.

## Testing against a local server

Two harnesses run the suites against a real server. Neither server is
complete on its own, and the difference matters.

```bash
./tests/wsgidav.sh
```

[wsgidav](https://github.com/mar10/wsgidav) is pure Python and runs
natively on Windows. On the first run the script creates a virtualenv in
`dav-venv` and installs wsgidav into it, which needs a **native Windows
Python** from <https://www.python.org/downloads/> or the Microsoft Store
and network access. Later runs reuse it. Server data goes in `davroot`.
Both directories are gitignored.

An MSYS2 shell has a minimal `PATH` that leaves out the Windows one, so
the script looks in the usual install locations as well and normally
finds Python without help. If it does not, say where it is:

```bash
PYTHON=/c/Python313/python.exe ./tests/wsgidav.sh
```

Do not install MSYS2's own `python` package for this: wsgidav depends on
`bcrypt`, which has no mingw wheel and needs a Rust toolchain to build
from source. The script detects an MSYS2 python and skips over it.

To compare against known-good numbers rather than just printing them:

```bash
./tests/wsgidav.sh --check
```

That runs every suite with `--json` and compares the totals against
`tests/expected-wsgidav.txt`, exiting non-zero if anything moved. This
is the gate CI runs, and it must print `All suites match the expected
results` before and after any change.

wsgidav 4.3.5 is not fully RFC 4918 compliant, and the expected results
record where:

* `copymove/copy_shallow` fails because wsgidav copies a collection
  recursively when the request said `Depth: 0`.
* Four tests in `locks` fail, and 31 are skipped, because wsgidav
  answers `LOCK` with the `Content-Type` `application; charset=utf-8`,
  which is not a media type. neon discards the body unparsed, so litmus
  never sees the lock token and everything downstream of holding a lock
  is skipped. **No change to the lock tests is visible against wsgidav.**

```bash
./tests/godav.sh
```

runs the same suites against `golang.org/x/net/webdav` instead, from the
Go source in `tests/godav`. It locks correctly, including the RFC 4918
section 7.3 treatment of a `LOCK` on an unmapped URL, but has no dead
property store, so four `props` tests fail there. Verify lock work
against `godav.sh` and property work against `wsgidav.sh`.

`./tests/godav.sh --check` compares against `tests/expected-godav.txt`
the same way, and is the only gate that covers the lock tests at all.
CI runs both.

Two more scripts support changes to the shared harness:

* `tests/capture.sh OUTDIR` captures every suite in default, `--quiet`
  and `--json` form against one wsgidav server, plus the
  connection-refused path for every suite.
* `tests/compare-captures.sh BEFORE AFTER` diffs two such directories
  with the wall-clock JSON fields normalised.

Two captures of the same build are byte for byte identical, so any
difference is the change under test:

```bash
make -f Makefile.w32 && ./tests/capture.sh /tmp/before
```

`tests/hyperfine.sh` times litmus itself rather than the server, using
[hyperfine](https://github.com/sharkdp/hyperfine) and the Go server.

`make test-httpd` runs the suites against Apache httpd in a container.
It needs podman or docker and does not work on Windows; it is kept for
the Unix build.

## Windows notes

**The wrong DLLs.** Git Bash ships its own `libssl`, `libcrypto` and
`libexpat` in `/mingw64/bin`. If those come first on `PATH` they shadow
the ones a dynamically linked build was built against. Put `ucrt64/bin`
first, or build with `STATIC=1`.

**`win32/config.h` is generated.** It is the output of `configure`,
checked in so `Makefile.w32` works without autotools. If you change
`configure.ac` or update neon, re-run `configure` and copy the resulting
`config.h` over `win32/config.h`.

**`VERSION`.** The fork version lives in one file at the top of the
tree, read by `Makefile.w32` and by `configure.ac`, and reaches the code
as `-DLITMUS_VERSION`. Bumping it means regenerating `win32/config.h` as
above.

**IPv6.** `configure` does not define `USE_GETADDRINFO` on MinGW, so
neon falls back to the older resolver. IPv4 works; IPv6 may not.

**`test-common`** is a symlink in git, which Windows checks out as a
small text file. `configure` detects that and uses the real directory.

## Differences from the upstream source

The Windows portability work, the neon patches and the fork's own
additions are recorded in [NEWS](NEWS). The changes inside `neon/` are
listed separately in [PATCHES.md](PATCHES.md), because anyone updating
the bundled neon has to reapply them.

## Licensing

litmus is under the GNU GPL. See [COPYING](COPYING).

```
litmus is Copyright (C) 1999-2025 Joe Orton
```

The bundled neon library is under the GNU Library GPL. See
[neon/src/COPYING.LIB](neon/src/COPYING.LIB) and
[neon/AUTHORS](neon/AUTHORS).

```
neon is Copyright (C) 1999-2025 Joe Orton
```

Report anything that is not Windows specific to
[upstream](https://github.com/notroj/litmus/issues).

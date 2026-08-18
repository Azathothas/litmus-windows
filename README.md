# litmus for Windows

This is a fork of [notroj/litmus](https://github.com/notroj/litmus), the
WebDAV server compliance test suite. It was made quickly, for one
reason: to get litmus compiling and running on Windows as a native
program. Upstream targets Unix and does not build here.

If you are on Linux or macOS, use upstream. Nothing here helps you.

What is different from upstream:

* It builds on Windows with MinGW-w64, as native `.exe` files. No Cygwin
  runtime, no WSL.
* neon is checked into the tree instead of being a submodule, so a plain
  `git clone` is enough to build.
* There is a second build path, `Makefile.w32`, that needs only GNU make
  and a compiler. No autotools.
* The suites can emit JSON, with `--json`, so a program can read the
  results.
* `--trace` dumps every request and response, so you can see exactly
  what your server was asked and what it answered.
* `tests/wsgidav.sh` runs the suites against a real WebDAV server on
  Windows, which is what `make test-httpd` does on Unix.

The test logic is upstream's. This fork does not add or change test
cases, apart from one bug fix noted at the bottom.

## Getting a binary

Tagged builds are published on the
[releases page](https://github.com/Azathothas/litmus-windows/releases) as
`litmus-windows-x86_64.zip`. Those executables are statically linked.
They need no MSYS2, no OpenSSL DLLs and no expat DLLs. Unzip and run.

With the GitHub CLI:

```bash
gh release download --repo Azathothas/litmus-windows --pattern "litmus-windows-x86_64.zip" --clobber
```

From PowerShell, without any extra tooling:

```powershell
$u = (Invoke-RestMethod https://api.github.com/repos/Azathothas/litmus-windows/releases/latest).assets |
     Where-Object name -eq 'litmus-windows-x86_64.zip' | Select-Object -First 1 -ExpandProperty browser_download_url
Invoke-WebRequest $u -OutFile litmus-windows-x86_64.zip
Expand-Archive litmus-windows-x86_64.zip -DestinationPath litmus -Force
```

Then:

```bash
./basic.exe http://dav.example.com/path/
```

Release tags look like `v0.18-win1`. The `0.18` is the upstream litmus
version this fork tracks; the suffix counts releases of the fork against
it. Each release bundles neon 0.37.1.

Everything below is for building it yourself.

## Building on a fresh Windows machine

You need MSYS2. Nothing else has to be installed first.

### 1. Install MSYS2

Download the installer from <https://www.msys2.org/> and run it, or if
you have winget:

```bash
winget install MSYS2.MSYS2
```

This installs to `C:\msys64` by default.

### 2. Install the toolchain

Open the **MSYS2 UCRT64** shortcut from the Start menu. Not the plain
MSYS2 shell, and not MINGW64. Then:

```bash
pacman -S --needed autoconf automake libtool m4 make pkgconf mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-openssl mingw-w64-ucrt-x86_64-expat mingw-w64-ucrt-x86_64-pkgconf
```

Say yes when it asks. If pacman tells you to close the terminal and
reopen it, do that and run the command again.

### 3. Clone

```bash
git clone https://github.com/Azathothas/litmus-windows
cd litmus-windows
```

There is no `--recurse-submodules` step. neon is already in the tree.

### 4. Build

```bash
./autogen.sh && ./configure --with-ssl=openssl && make
```

You get `basic.exe`, `copymove.exe`, `props.exe`, `locks.exe`,
`http.exe`, `largefile.exe`, `protected.exe`, `lockbomb.exe`,
`lockbomb-single.exe`, and a shell script called `litmus`.

You do not need `--with-included-neon`. There is no system neon on
Windows to prefer, so the bundled copy is used anyway. Passing the flag
does no harm.

## Building without autotools

`Makefile.w32` builds the same executables using a configuration that is
checked in at `win32/config.h`. It needs GNU make and a MinGW-w64
compiler whose sysroot has OpenSSL and expat. A UCRT64 shell has both.

```bash
make -f Makefile.w32
```

This is faster and has fewer moving parts. Use it if `configure` is
giving you trouble, or if you do not want to install autoconf.

To build executables that run on a machine with no MSYS2 installed:

```bash
make -f Makefile.w32 STATIC=1
```

That links OpenSSL and expat in statically. The binaries get large,
around 8 MB each, and depend only on DLLs that ship with Windows.

### If OpenSSL and expat are somewhere else

Set `PREFIX` to a sysroot that provides both:

```bash
make -f Makefile.w32 PREFIX=/some/sysroot
```

`PREFIX` must belong to the same toolchain as the compiler. Pointing a
standalone MinGW-w64 at MSYS2's `lib` directory pulls in MSYS2's C
runtime as well, and the link dies on `_gnu_exception_handler` and
`__mingw_oldexcpt_handler`. If you see those symbols, this is why.

## Running it against your server

Each suite is a separate program that takes a URL:

```bash
./basic.exe http://dav.example.com/path/
```

Add a username and password if the server wants them:

```bash
./basic.exe http://dav.example.com/path/ jim 2518
```

The URL must be a collection that already exists, and litmus must be
able to create a collection called `litmus` inside it. If that MKCOL
fails you get a 409 and every test aborts at `begin`.

To run the standard set in one go:

```bash
make URL=http://dav.example.com/path/ check
```

or, after `make install`, the `litmus` script:

```bash
./litmus http://dav.example.com/path/ jim 2518
```

Both of those are shell scripts. They run in an MSYS2 or Git Bash shell.
They do not run in cmd.exe or PowerShell. The suite executables
themselves are native Windows programs and run anywhere.

## Running it against a local server

`tests/wsgidav.sh` sets up a WebDAV server and runs the suites against
it. Use it to check that a build works.

```bash
./tests/wsgidav.sh
```

On the first run it creates a virtualenv in `dav-venv` and installs
[wsgidav](https://github.com/mar10/wsgidav) into it. That needs Python
and network access. Later runs reuse the virtualenv. Server data goes in
`davroot`. Both directories are gitignored.

It needs a **native Windows Python**, from
<https://www.python.org/downloads/> or the Microsoft Store. An MSYS2
shell has a minimal `PATH` that leaves out the Windows one, so the
script also looks in the usual install locations and normally finds it
without help. If it does not, say where it is:

```bash
PYTHON=/c/Python313/python.exe ./tests/wsgidav.sh
```

Do not install MSYS2's own `python` package for this. It works fine as a
Python, but wsgidav depends on `bcrypt`, which has no mingw wheel and
tries to build from source, which needs a Rust toolchain. The script
detects an MSYS2 python and skips over it rather than failing halfway
through a pip install.

To check the results against known-good numbers instead of just printing
them:

```bash
./tests/wsgidav.sh --check
```

That runs every suite with `--json` and compares the totals against
`tests/expected-wsgidav.txt`. It exits non-zero if anything moved. This
is what CI runs.

wsgidav is not fully RFC 4918 compliant, so two suites do not reach
100%. `copymove/copy_shallow` fails because wsgidav copies a collection
recursively when the request said `Depth: 0`. Four tests in `locks` fail
because wsgidav's `LOCK` response leaves out the `activelock` element,
so litmus never gets a lock token. Those are server bugs, not litmus
bugs, and the expected-results file records them as expected.

There is also `make test-httpd`, which runs the suites against Apache
httpd in a container. It is upstream's, it needs podman or docker, and
it does not work on Windows. It is kept for the Unix build.

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
```

`--json`, `--verbose` and `--trace` are new in this fork. Everything
else is upstream's and behaves the same.

Default output is byte for byte identical to upstream's. If you have a
script that parses it, it keeps working.

### JSON output

```bash
./basic.exe --json http://dav.example.com/path/
```

One object on stdout, nothing else, so you can pipe it straight into a
parser. The exit status is the number of failed tests, same as always.

```json
{
  "suite": "copymove",
  "target": "http://dav.example.com/path/",
  "duration": 5.790,
  "tests": [
    {"name": "begin", "status": "pass", "duration": 0.061},
    {"name": "copy_shallow", "status": "fail", "duration": 0.376,
     "context": "DELETE on `/litmus/ccdest/foo' should fail with 404: got 204"}
  ],
  "summary": {"total": 13, "passed": 12, "failed": 1,
              "skipped": 0, "notrun": 0, "warnings": 0}
}
```

`status` is one of `pass`, `fail`, `fatal`, `skip`, `xfail`, `notrun` or
`oops`. `context` is the failure message, and is absent when the test
did not set one. A `warnings` array appears on a test that issued
warnings. `notrun` counts tests that were never reached because an
earlier test aborted the suite.

Running several suites through the `litmus` script with `--json` gives
one object per suite, one per line, which is JSON Lines.

There is more detail in [AGENT.md](AGENT.md), written for a program
consuming this output rather than a person reading it.

### Seeing the traffic

`--trace` dumps every request and response. This is the useful one when
you are writing a server and want to know what litmus actually asked
for.

```bash
./props.exe --trace=wire.log http://127.0.0.1:8080/dav/
```

With no filename it writes to stderr. Use `-` for stdout. Each request
is introduced by the suite, test number and test name that issued it,
so you can find the exact exchange behind a failure:

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

Sent lines are prefixed `>`, received lines `<`. Message bodies are
printed inside square brackets rather than prefixed, because PROPFIND
and LOCK bodies are XML and nearly every line of those starts with `<`.

`--verbose` widens the trace to everything neon can report, including
socket, XML parser and authentication detail. On its own it writes to
stderr. Combined with `--trace` it writes the wider trace to the trace
destination, so `--trace=wire.log --verbose` puts everything in one
file. Without either flag the same detail is appended to `debug.log`.

The flags compose, and stdout is only ever used for results:

```bash
./locks.exe --json --trace=wire.log http://127.0.0.1:8080/dav/ > result.json
```

## Things that will trip you up

**Leftover collections.** litmus creates a collection called `litmus`
under the URL you give it, and does not always clean it up. If a
previous run left one behind, `MKCOL` returns 405 and every suite aborts
at `begin`. Use a fresh directory per run.

**The wrong DLLs.** Git Bash ships its own `libssl`, `libcrypto` and
`libexpat` in `/mingw64/bin`. If those come first on `PATH` they shadow
the ones a dynamically linked build was built against, and you get
confusing failures. Put `ucrt64/bin` first, or build with `STATIC=1`.

**`win32/config.h` is generated.** It is the output of `configure`,
checked in so that `Makefile.w32` works without autotools. If you change
`configure.ac` or update neon, regenerate it: run `configure` as above
and copy the resulting `config.h` over `win32/config.h`.

**IPv6.** `configure` does not define `USE_GETADDRINFO` on MinGW, so
neon falls back to the older resolver. IPv4 works. IPv6 may not.

**`make distclean`** was broken upstream, because `neon/src/Makefile.in`
had no `distclean` target. It is fixed here.

## What was changed

Portability fixes, and one real bug:

1. `src/basic.c` wrote temporary files to `/tmp`, which does not exist on
   Windows. There is now a `litmus_tmpfile()` helper in `src/common.c`
   that uses `$TMPDIR`, `$TMP` or `$TEMP`.
2. `src/lockbomb.c` read from `/dev/zero`. Same helper.
3. Binary mode was only set on Cygwin. Native Windows needs it too, or
   CRLF translation corrupts every PUT and GET byte comparison.
4. `test-common` is a symlink in git. Windows checks it out as a small
   text file, so the build could not find `tests.c`. `configure` now
   detects that and uses the real directory.
5. `src/largefile.c` called `ne_set_request_body_provider64`, which was
   removed in neon 0.27. It never compiled on any platform that enables
   `NE_LFS`, which on 64-bit Linux is none of them, which is why nobody
   noticed.
6. `src/locks.c` had a use after free. `prep_collection()` pointed `res`
   and `coll` at the same allocation, and `unmapped_lock()` freed `res`.
   If `lock_collection()` failed before it reassigned `res`, which is
   what happens against any server that fails collection locking, then
   `coll` was left dangling and the failure message printed freed
   memory. The message was different on every run. This is upstream's
   bug and affects Unix too.

Plus `--json` and `--verbose`, described above.

The changes inside `neon/` are listed separately in
[PATCHES.md](PATCHES.md), because anyone updating the bundled neon needs
to reapply them.

## What was not changed

[Upstream issue 8](https://github.com/notroj/litmus/issues/8) asks for
the extra test cases from the [tolsen fork](https://github.com/tolsen/litmus)
to be merged. They are not in this fork, deliberately.

That fork is a 2008 SVN conversion with no history in common with
upstream, so there is nothing to merge against. The nearest usable
comparison is
[skissane's branch](https://github.com/notroj/litmus/compare/master...skissane:litmus:tolsen-merge),
which is 1643 added lines across `basic.c`, `copymove.c`, `locks.c` and
`props.c`, and is now 74 commits behind master. The person who led the
team that wrote those tests
[says himself](https://github.com/notroj/litmus/issues/8#issuecomment-1595709446)
that they should not be treated as a gold standard and need review by
someone with deep WebDAV knowledge. Upstream's maintainer is open to
them but wants them split into reviewable pieces first.

None of that is Windows portability work, and taking 1643 lines of
unreviewed test changes into this fork would change the expected results
in a way that makes real regressions hard to spot. If those tests are
worth having they should go upstream, reviewed, not sideways into a
port. This fork tracks upstream's test logic so results from it stay
comparable with results from upstream.

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

All the hard work here is Joe Orton's. This fork only makes it compile
on Windows. Report anything that is not Windows specific to
[upstream](https://github.com/notroj/litmus/issues).

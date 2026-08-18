# Handoff

Where the current round of work stopped, what is verified, and what is
left. Read this with [TODO](TODO), which has the remaining work in
priority order. Delete this file once the work it describes is finished.

Repository: `Azathothas/litmus-windows`, branch `master`.
Last released tag: `v0.18-win1`. The tree is now at `0.18-win2`
(see [VERSION](VERSION)) and **has not been released or pushed yet**.

## State

Three commits are on `master` locally, none pushed:

| Commit | What |
| --- | --- |
| `0d57b10` | Replace the nine suite executables with one `litmus-cli` |
| `943cac5` | Add lock-null, `%20` collection, COPY/MOVE body and PROPPATCH 207 tests |
| `f43bdba` | Add a `bench` subcommand for throughput and latency |

Plus a fourth commit carrying the verification scripts and this file.

`./tests/wsgidav.sh --check` is green:

```
basic      ok       17 passed, 0 failed, 0 skipped
copymove   ok       14 passed, 1 failed, 0 skipped
http       ok       4 passed, 0 failed, 0 skipped
locks      ok       8 passed, 4 failed, 31 skipped
props      ok       33 passed, 0 failed, 0 skipped
```

Both build paths work and produce no warnings:

```bash
make -f Makefile.w32
```

```bash
./autogen.sh && ./configure --with-ssl=openssl && make
```

## What changed, in one page

**One executable.** `basic.exe`, `copymove.exe` and the other seven are
gone. Everything is `litmus-cli` with the suite as a subcommand, plus
`all`, `list`, `bench` and `version`. `lockbomb-single` survives as a
subcommand that is `lockbomb` with a default of one thread; `--threads`
overrides either. The release drops from nine statically linked copies
of OpenSSL, expat and neon to one.

Mechanically: each suite's `ne_test tests[]` became `<suite>_tests[]`,
the harness `main()` became `run_suite()`, and `reset_state()` in
`neon/test/common/tests.c` plus `litmus_reset()` in `src/common.c`
return every counter, buffer, session and getopt variable to its
initial value between suites. `litmus_init()` no longer calls `exit()`:
it returns `TEST_INIT_DONE` or `TEST_INIT_USAGE`, which `run_suite()`
maps to 0 and 1.

**Five new tests**, each verified against two servers:
`basic/mkcol_percent_20`, `locks/locknull`, `locks/locknull_discover`,
`locks/locknull_unlock`, `copymove/copy_content`,
`copymove/move_content`. Plus MKCOL 201 and DELETE 204 status warnings,
and `litmus_proppatch()`, which reports the status from inside a 207
multistatus so a server that refuses a PROPPATCH with 207+423 is judged
on the 423 rather than on the wrapper.

**A benchmark.** `litmus-cli bench URL` measures four transfer
scenarios and a TCP-connect latency probe. The payload is
4194304 bytes (4.0 MiB) of xorshift64\* output, filled once and streamed
from a rotating offset; concurrency is bounded by a pool of exactly
`--concurrency` worker threads. `--connect-timeout`, `--read-timeout`
and `--no-keepalive` are new and apply to every subcommand.

**Version in one place.** `VERSION` at the top of the tree is read by
`Makefile.w32` via `$(shell cat VERSION)` and by `configure.ac` via
`m4_esyscmd_s`, and reaches the code as `-DLITMUS_VERSION`. It shows up
in `litmus-cli version` and in the User-Agent. `win32/config.h` was
regenerated to match; if you bump `VERSION`, re-run `configure` and
copy the resulting `config.h` over `win32/config.h`.

## How the work was verified

Do not take a green `--check` as proof on its own. Two more things were
used and should keep being used.

**Byte-for-byte output.** `tests/capture.sh` captures every suite in
default, `--quiet` and `--json` form against one wsgidav server, plus
the connection-refused path for all nine suites, and
`tests/compare-captures.sh` diffs two such directories with the
wall-clock JSON fields normalised. Two captures of the same build are
identical, so any difference is the change under test. This is how the
CLI unification was shown to change nothing, and how the new tests were
shown to add lines and change nothing else.

**A second server.** `tests/godav.sh` builds and runs
`golang.org/x/net/webdav` from `tests/godav/`. It is not optional
extra credit — wsgidav cannot exercise the lock tests at all, because
it answers LOCK with `Content-Type: application; charset=utf-8`, which
is not a media type, so neon discards the body and litmus never gets a
lock token. The three lock-null tests pass against x/net/webdav and are
skipped against wsgidav. Conversely x/net/webdav has no dead property
store, so four props tests fail there. Neither server is the whole
picture.

Current x/net/webdav results, for comparison after a change:

```
basic     17 of 17 passed
copymove  15 of 15 passed
props     10 of 14 passed   (no dead property support)
locks     32 of 36 passed   (1 warning)
http       4 of  4 passed
```

`tests/hyperfine.sh` times litmus itself rather than the server.

## Next steps, in order

1. **Rewrite the docs.** README.md, AGENTS.md, PATCHES.md and NEWS all
   still describe nine executables. This is the biggest gap and the
   first thing a user hits. TODO item 1 has the requirements: objective
   and technical, no narrative or fork history, and a plain statement
   that this suite covers more than the version it was forked from with
   no editorialising about upstream.

   PATCHES.md specifically needs the new neon changes recorded:
   `run_suite()` replacing `main()`, `reset_state()`, `close_logs()`,
   `free_records()`, `ne_test *tests` as a pointer, the
   `TEST_INIT_DONE`/`TEST_INIT_USAGE` contract, and
   `test_now_seconds()`, `test_now_iso8601()` and `test_json_string()`
   being made non-static so `src/bench.c` can use them.

2. **CI and releases.** TODO item 2. The workflow still names nine
   `.exe` files in places and must follow the single executable. Then
   the README download instructions can be one fixed URL.

3. **Deep reviews, then release.** At least three passes over the diff,
   then commit, push, cut `v0.18-win2`, download the published asset and
   run it against a real server end to end. Then a dependabot pass:
   give it a moment or trigger it manually, merge anything sensible,
   confirm no workflow uses a deprecated action, and finish with one
   more review.

## Things to know before touching the code

* **`tests` can be NULL.** The benchmark runs outside the harness, so
  `tests[test_num].name` is not always valid. `current_test()` in
  `src/common.c` is the accessor. A direct dereference segfaulted the
  first `bench` run.

* **Leftover collections.** `litmus-cli all` runs every suite against
  the same URL, exactly as the driver script did. Against wsgidav the
  `locks` suite leaves a locked collection that DELETE cannot remove,
  so `http` then fails its MKCOL with 405. This is not a state-reset
  bug: running the suites as separate processes in sequence against one
  collection produces byte-identical results. `tests/wsgidav.sh` gives
  each suite its own collection to avoid it.

* **Killed test runs keep writing.** A backgrounded `tests/*.sh` run
  spawns an `sh.exe` tree that survives killing the parent `bash.exe`,
  and it will keep overwriting capture output for minutes, which looks
  exactly like a regression. If a capture diff makes no sense, check
  for stray processes first:

  ```bash
  powershell -NoProfile -Command "Get-CimInstance Win32_Process | Where-Object { \$_.CommandLine -match 'litmus|wsgidav' } | Select-Object ProcessId,CommandLine"
  ```

* **Backslashes and the shell.** Writing `\\` through a Bash heredoc
  collapses it to `\`, which silently mangles Makefile line
  continuations. Use the file-editing tools for anything containing
  backslashes.

* **Build environment.** MSYS2 is a global scoop install at
  `C:\ProgramData\scoop\apps\msys2\current`; the UCRT64 toolchain is
  under `ucrt64/`, autotools under `usr/bin/`. Run builds as

  ```bash
  MSYSTEM=UCRT64 /c/ProgramData/scoop/apps/msys2/current/usr/bin/bash.exe -l script.sh
  ```

  `bash -l` starts in the home directory, so the script must `cd`
  itself. `tests/godav.sh` needs `GOPATH` and `GOCACHE` set in that
  shell, since the MSYS2 login environment does not carry the Windows
  ones.

* **Commit attribution.** Everything is authored as
  `Azathothas <AjamX101@gmail.com>`. No Claude or AI attribution
  anywhere, in commit messages, tags or PR text.

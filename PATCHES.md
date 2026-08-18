# Patches applied to the bundled neon

The `neon/` directory is a copy of [notroj/neon](https://github.com/notroj/neon)
checked in as ordinary files. It is not a submodule. The copy is neon
0.37.1, commit `170c36704bfc2ac8b0e5607c3e1fdd4c159674db`.

If you replace it with a newer neon, the changes below have to be
applied again. Upstream neon does not carry any of them.

To see the current state of the patches:

```bash
git log --oneline -- neon/
```

Two files hold nearly all of it: `neon/test/common/tests.c` and
`neon/test/common/tests.h`, the shared test harness litmus builds on.
The rest are three small portability fixes.

## Portability

### 1. `src/ne_socket.c`: setsockopt argument type

Two `setsockopt` calls passed an `int *` for the option value. POSIX
declares that parameter `const void *`, so this is fine on Unix, but the
Winsock declaration is `const char *`. GCC 14 and later treat the
mismatch as an error rather than a warning, so the build fails.

Both calls now cast to `const char *`. The affected calls are
`SO_REUSEADDR` in `do_bind()` and `TCP_NODELAY` in `ne_sock_connect()`.

### 2. `src/ne_defs.h`: symbol visibility on PE

`NE_PRIVATE` was defined as `__attribute__((visibility ("hidden")))` for
any GCC 3 or later. PE/COFF has no symbol visibility, so GCC ignores the
attribute and warns about it once per translation unit that uses one —
eight warnings in a clean build, none of them actionable.

The definition is now skipped on `_WIN32` and `__CYGWIN__`. The
unconditional `#ifndef NE_PRIVATE / #define NE_PRIVATE` further down the
same file already supplies the empty fallback, so nothing else changes.

### 3. `src/Makefile.in`: missing distclean target

litmus's own `distclean` runs `cd neon/src && $(MAKE) distclean`, but
`neon/src/Makefile.in` only defined `clean`, so `make distclean` failed
at that line. A `distclean` target that removes the generated `Makefile`
has been added.

### 4. `test/common/tests.c`: LC_MESSAGES is not portable

`setlocale(LC_MESSAGES, "")` was guarded on `HAVE_SETLOCALE` alone.
`setlocale` does exist on Windows, so the guard passes, but `LC_MESSAGES`
is a POSIX extension that the Microsoft C runtime does not define, and
compilation fails.

The guard is now `#if defined(HAVE_SETLOCALE) && defined(LC_MESSAGES)`.

### 5. `test/common/tests.c`: suite name on Windows

The suite name was derived from `basename(argv[0])`, which only looked
for a forward slash. On Windows every line of output was prefixed with
the full path of the executable instead of the suite name.

The derivation is gone entirely — `run_suite()` takes the name as an
argument, see below — but if you are porting these changes onto a newer
neon that still derives it, the lookup has to consider a backslash, take
whichever separator appears last, and strip a trailing `.exe`.

## The harness: several suites in one process

Upstream's harness assumes one executable per suite. It has a `main()`,
it reads the suite name from `argv[0]`, its counters are file-scope
statics initialised once, and it calls `exit()` from the command-line
parser. litmus is now a single executable that runs any suite as a
subcommand, and `litmus-cli all` runs five of them in turn, so all four
assumptions had to go.

### 6. `run_suite()` replaces `main()`

```c
int run_suite(const char *name, ne_test *suite, int argc, char **argv);
```

Same body as upstream's `main()`, with the suite and its name passed in
rather than being a link-time symbol and `argv[0]`. It returns the
number of failed tests, or a negative value if the run could not start.
`neon/test/common/child.c` and the rest of the harness are untouched.

`tests` is declared `ne_test *tests` rather than `ne_test tests[]`, so
`run_suite()` can point it at whichever array it was given. Each suite
defines its array under its own name (`basic_tests[]`, `props_tests[]`
and so on) instead of every one of them defining `tests[]`.

`tests` can also be NULL: the `bench` subcommand sets up a session
through the same code but runs outside the harness, so anything reading
`tests[test_num].name` has to cope with that. litmus does it through
`current_test()` in `src/common.c`.

### 7. `reset_state()`

Every counter, buffer and flag the harness owns is returned to its
initial value at the top of `run_suite()`: `passes`, `fails`, `skipped`,
`warnings`, `count`, `quiet`, `warned`, `aborted`, `test_num`,
`test_name`, `have_context`, `test_context`, `run_started_iso`, the
recorded request, and the per-test records. Without it the second suite
in a process starts with the first one's totals.

Anything added at file scope in this file has to be listed there too.

### 8. `close_logs()`

`debug.log` and `child.log` were opened by `main()` and closed at the
end of it. Closing them now happens on every exit path from
`run_suite()`, including the early ones, and clears both `FILE *`
variables and neon's debug stream so the next suite in the same process
does not inherit a dangling pointer.

### 9. `TEST_INIT_DONE` and `TEST_INIT_USAGE`

Upstream's `NEON_TEST_INIT` function (litmus's `litmus_init()`) called
`exit()` for `--help` and for a usage error, which kills the whole
process. It now returns one of two negative sentinels instead:

| Return | Meaning | `run_suite()` returns |
| --- | --- | --- |
| `TEST_INIT_DONE` (-2) | the command line was answered in full, e.g. `--help` | 0 |
| `TEST_INIT_USAGE` (-3) | usage error, message already written | 1 |

Any other non-zero return is still a parse failure and still reports
`Failed parsing command-line`.

## The harness: JSON and verbose output

### 10. `test/common/tests.c` and `tests.h`: `--json` and `--verbose`

A feature of this fork rather than a portability fix, but it lives in
the shared harness because that is where results are formatted and
counted.

`tests.h` declares four globals, which `litmus_init()` sets while
parsing the command line:

| Global | Effect |
| --- | --- |
| `test_json` | Emit one JSON object on stdout instead of the usual output |
| `test_verbose` | Send the protocol trace to stderr rather than `debug.log` |
| `test_target` | Description of what is under test, included in the JSON |
| `test_trace_fp` | Stream the `--trace` wire dump is going to, so the harness points neon's debug output at the same place and the two interleave correctly |

In `tests.c`:

* Per-test timing via `gettimeofday`, guarded on `HAVE_GETTIMEOFDAY` and
  `HAVE_SYS_TIME_H`, falling back to a duration of zero.
* An ISO 8601 UTC run timestamp with millisecond precision, from
  `gmtime` and `strftime`. Sub-second digits are truncated rather than
  rounded, so the stamp never names a moment that had not yet occurred.
* A `struct test_record` array collecting name, status, duration,
  failure context and any warnings for each test, released by
  `free_records()` — which `reset_state()` also calls, so a second suite
  in the same process does not leak the first one's records.
* `t_warning()` stores the message against the running test in JSON mode
  rather than printing it.
* Human-readable printing goes through `TPRINT`/`TPUTCHAR` macros that
  are suppressed in JSON mode. The surrounding logic is untouched, so
  the counters and exit status are identical either way.

### 11. `t_request_begin()` and `t_request_status()`

A failing test's `context` is prose, so a consumer that wants to branch
on the kind of failure has to pattern-match it. These two entry points
let the caller tell the harness what the running test asked for and what
it was answered:

```c
void t_request_begin(const char *method, const char *path);
void t_request_status(int status);
```

litmus feeds them from `ne_hook_create_request` and
`ne_hook_post_headers` on both sessions, so no test had to change.
`record_result()` copies the pair into the record of a test that failed,
and `emit_json()` writes it as the `"error"` object. Both are cleared at
the start of every test and by `reset_state()`.

Nothing is printed, so the text output is unaffected.

### 12. Three helpers made non-static

`test_now_seconds()`, `test_now_iso8601()` and `test_json_string()` were
static to `tests.c`. They are declared in `tests.h` and exported so that
`src/bench.c`, which runs outside the harness, emits the same timestamp
format and the same JSON string escaping rather than a second
implementation that could drift.

## Verifying a change here

Default text output must stay byte for byte identical. `tests/capture.sh`
and `tests/compare-captures.sh` exist to prove it:

```bash
make -f Makefile.w32 && ./tests/capture.sh /tmp/before
```

then make the change, rebuild, capture to `/tmp/after`, and

```bash
./tests/compare-captures.sh /tmp/before /tmp/after
```

Two captures of the same build are byte for byte identical, so any
difference is the change under test.

## 13. Removed `neon/.github`

The vendored copy brought neon's own CI workflows with it. GitHub only
reads workflows from the repository root, so they could never run, and
two `.github` directories in one tree is just confusing. Nothing else in
the vendored tree has been pruned, so diffing against a fresh upstream
checkout stays clean.

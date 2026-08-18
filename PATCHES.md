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

## 1. `src/ne_socket.c`: setsockopt argument type

Two `setsockopt` calls passed an `int *` for the option value. POSIX
declares that parameter `const void *`, so this is fine on Unix, but the
Winsock declaration is `const char *`. GCC 14 and later treat the
mismatch as an error rather than a warning, so the build fails.

Both calls now cast to `const char *`. The affected calls are
`SO_REUSEADDR` in `do_bind()` and `TCP_NODELAY` in `ne_sock_connect()`.

## 2. `test/common/tests.c`: LC_MESSAGES is not portable

`setlocale(LC_MESSAGES, "")` was guarded on `HAVE_SETLOCALE` alone.
`setlocale` does exist on Windows, so the guard passes, but `LC_MESSAGES`
is a POSIX extension that the Microsoft C runtime does not define, and
compilation fails.

The guard is now `#if defined(HAVE_SETLOCALE) && defined(LC_MESSAGES)`.

## 3. `test/common/tests.c`: suite name on Windows

The suite name is derived from `basename(argv[0])`, which only looked for
a forward slash. On Windows every line of output was prefixed with the
full path of the executable instead of the suite name.

The lookup now also considers a backslash, takes whichever separator
appears last, and strips a trailing `.exe` so the suite is reported as
`basic` rather than `basic.exe`.

## 4. `test/common/tests.c` and `tests.h`: JSON and verbose output

Support for the `--json` and `--verbose` options. This is a feature of
this fork rather than a portability fix, but it lives in the shared test
harness because that is where results are formatted and counted.

`tests.h` declares three new globals, which the `NEON_TEST_INIT`
function (litmus's `litmus_init()`) sets while parsing the command line:

| Global | Effect |
| --- | --- |
| `test_json` | Emit one JSON object on stdout instead of the usual output |
| `test_verbose` | Send the protocol trace to stderr rather than `debug.log` |
| `test_target` | Description of what is under test, included in the JSON |
| `test_trace_fp` | Stream the `--trace` wire dump is going to, so the harness points neon's debug output at the same place and the two interleave correctly |

In `tests.c`:

* Per-test timing via `gettimeofday`, guarded on `HAVE_GETTIMEOFDAY` and
  `HAVE_SYS_TIME_H`, falling back to a duration of zero.
* A `struct test_record` array collecting name, status, duration,
  failure context and any warnings for each test.
* `t_warning()` stores the message against the running test in JSON mode
  rather than printing it.
* Human-readable printing goes through `TPRINT`/`TPUTCHAR` macros that
  are suppressed in JSON mode. The surrounding logic is untouched, so
  the counters and exit status are identical either way.

Default output is unchanged. This was verified by building the harness
with and without the patch and diffing the output of every suite in both
default and `--quiet` mode; see the regression note in the README.

## 5. `src/Makefile.in`: missing distclean target

litmus's own `distclean` runs `cd neon/src && $(MAKE) distclean`, but
`neon/src/Makefile.in` only defined `clean`, so `make distclean` failed
at that line. A `distclean` target that removes the generated `Makefile`
has been added.

## 6. Removed `neon/.github`

The vendored copy brought neon's own CI workflows with it. GitHub only
reads workflows from the repository root, so they could never run, and
two `.github` directories in one tree is just confusing. Nothing else in
the vendored tree has been pruned, so diffing against a fresh upstream
checkout stays clean.

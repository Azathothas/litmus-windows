/*
   litmus: WebDAV server test suite: one executable, one subcommand per
   suite.
   Copyright (C) 2026, Azathothas <AjamX101@gmail.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <config.h>

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#include <ne_utils.h>
#include <ne_socket.h>
#include <ne_alloc.h>

#include "common.h"
#include "suites.h"

const struct litmus_suite litmus_suites[] = {
    { "basic", basic_tests,
      "PUT, GET, DELETE and MKCOL on plain resources and collections",
      1, 0 },
    { "copymove", copymove_tests,
      "COPY and MOVE, for resources and for collections",
      1, 0 },
    { "props", props_tests,
      "property handling: PROPFIND and PROPPATCH",
      1, 0 },
    { "locks", locks_tests,
      "LOCK and UNLOCK, including collection and conditional requests",
      1, 0 },
    { "http", http_tests,
      "HTTP-level behaviour, currently Expect: 100-continue",
      1, 0 },
    { "largefile", largefile_tests,
      "a 2147549184 byte (2 GiB) PUT followed by a GET",
      0, 0 },
    { "protected", protected_tests,
      "every method must be refused on a protected metadata collection",
      0, 0 },
    { "lockbomb", lockbomb_tests,
      "lock/unlock stress, 20 worker threads by default",
      0, LITMUS_THREADS_DEFAULT },
    { "lockbomb-single", lockbomb_single_tests,
      "lock/unlock stress in one thread",
      0, 1 },
    { NULL, NULL, NULL, 0, 0 }
};

const struct litmus_suite *litmus_suite_find(const char *name)
{
    const struct litmus_suite *s;

    for (s = litmus_suites; s->name; s++)
        if (strcmp(s->name, name) == 0) return s;

    return NULL;
}

static void usage(FILE *out, const char *argv0)
{
    const struct litmus_suite *s;

    fprintf(out,
            "Usage: %s COMMAND [OPTIONS] URL [username password]\n"
            "\n"
            "Suites:\n", argv0);

    for (s = litmus_suites; s->name; s++)
        fprintf(out, "  %-16s%s\n", s->name, s->summary);

    fprintf(out,
            "\n"
            "Other commands:\n"
            "  all             run the standard suites in order:");

    for (s = litmus_suites; s->name; s++)
        if (s->in_all) fprintf(out, " %s", s->name);

    fprintf(out,
            "\n"
            "  bench           measure throughput and connect latency\n"
            "  list            list the suites, one name and summary per line\n"
            "  selftest        check this executable's own result harness;\n"
            "                  makes no network requests and takes no URL\n"
            "  version         print the version\n"
            "\n"
            "Run `%s COMMAND --help' for the options a command takes.\n"
            "The URL must be an existing collection that litmus may create a\n"
            "collection called `litmus' inside.\n",
            argv0);
}

static void list_suites(void)
{
    const struct litmus_suite *s;

    for (s = litmus_suites; s->name; s++)
        printf("%-16s%s\n", s->name, s->summary);
}

/* argv[0] for the suite currently running: "litmus-cli basic" and so
 * on, so that a usage message names the command the user typed.  Held
 * until the next dispatch replaces it, since the suite keeps the
 * pointer in test_argv for as long as it runs. */
static char *cmdname;

/* Sets up a session the way a suite would and runs the benchmark.  It
 * is not a suite: nothing here passes or fails, so it does not go
 * through the harness. */
static int run_bench(const char *argv0, int argc, char **argv)
{
    int use_colour = 0, quiet = 0, ret;

    litmus_reset();

    if (cmdname) ne_free(cmdname);
    cmdname = ne_concat(argv0, " bench", NULL);
    argv[0] = cmdname;

    test_suite = "bench";
    test_argc = argc;
    test_argv = argv;

    ret = litmus_init(argc, (const char *const *)argv, &use_colour, &quiet);
    if (ret == TEST_INIT_DONE) return 0;
    if (ret == TEST_INIT_USAGE) return 1;
    if (ret != OK) {
        fprintf(stderr, "%s: %s\n", cmdname, test_context);
        return 1;
    }

    if (begin() != OK) {
        fprintf(stderr, "%s: %s\n", cmdname, test_context);
        return 1;
    }

    ret = litmus_bench(&litmus_bench_options);

    finish();

    return ret;
}

/* Runs one suite, giving it 'argc'/'argv' as its own command line. */
static int dispatch(const struct litmus_suite *suite, const char *argv0,
                    int argc, char **argv)
{
    litmus_reset();

    /* The subcommand chooses the default concurrency; --threads, parsed
     * inside run_suite() below, overrides it. */
    if (suite->threads) litmus_threads = suite->threads;

    if (cmdname) ne_free(cmdname);
    cmdname = ne_concat(argv0, " ", suite->name, NULL);
    argv[0] = cmdname;

    return run_suite(suite->name, suite->tests, argc, argv);
}

/* Runs the synthetic suites that check the harness itself.  They make
 * no requests, but litmus_init() insists on a URL, so one is appended
 * to whatever the user typed rather than being demanded from them; it
 * is never opened.  Returns the number of failures, which is a fixed
 * number that tests/harness.sh knows. */
static int run_selftest(const char *argv0, int argc, char **argv)
{
    static char placeholder[] = "http://selftest.invalid/";
    const struct litmus_suite *suite;
    char **args;
    int i, failures = 0, fatal = 0, ret;

    args = ne_malloc((argc + 2) * sizeof(*args));
    for (i = 0; i < argc; i++) args[i] = argv[i];
    args[argc] = placeholder;
    args[argc + 1] = NULL;

    for (suite = litmus_selftest_suites; suite->name; suite++) {
        /* getopt permutes the array it is given, and the next suite
         * has to see the same command line, so hand each one a fresh
         * copy of it. */
        char **round = ne_malloc((argc + 2) * sizeof(*round));

        memcpy(round, args, (argc + 2) * sizeof(*round));
        ret = dispatch(suite, argv0, argc + 1, round);
        ne_free(round);

        if (ret < 0) fatal = ret;
        else failures += ret;
    }

    ne_free(args);

    if (fatal) return fatal;
    return failures > 125 ? 125 : failures;
}

int main(int argc, char *argv[])
{
    const struct litmus_suite *suite;
    const char *cmd;
    int ret;

    if (argc < 2) {
        usage(stderr, argv[0]);
        return 1;
    }

    cmd = argv[1];

    if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0
        || strcmp(cmd, "help") == 0) {
        usage(stdout, argv[0]);
        return 0;
    }

    if (strcmp(cmd, "--version") == 0 || strcmp(cmd, "-V") == 0
        || strcmp(cmd, "version") == 0) {
        printf("litmus %s\n%s\n", LITMUS_VERSION, ne_version_string());
        return 0;
    }

    if (strcmp(cmd, "list") == 0) {
        list_suites();
        return 0;
    }

    /* One socket-library reference for the whole process, so that the
     * per-suite pairs inside run_suite() never take the count to zero
     * and tear down OpenSSL between suites. */
    if (ne_sock_init()) {
        fprintf(stderr, "%s: socket library initialization failed\n", argv[0]);
        return 1;
    }

    if (strcmp(cmd, "bench") == 0) {
        ret = run_bench(argv[0], argc - 1, argv + 1);
        litmus_cleanup();
    }
    else if (strcmp(cmd, "selftest") == 0) {
        ret = run_selftest(argv[0], argc - 1, argv + 1);
        litmus_cleanup();
    }
    else if (strcmp(cmd, "all") == 0) {
        int failures = 0, fatal = 0;

        for (suite = litmus_suites; suite->name; suite++) {
            int suite_ret;

            if (!suite->in_all) continue;

            /* argv is rewritten per suite: argv[0] is replaced with the
             * command name and argv[1] is the subcommand being dropped,
             * so each suite must start from the same slice. */
            suite_ret = dispatch(suite, argv[0], argc - 1, argv + 1);

            if (suite_ret < 0) fatal = suite_ret;
            else failures += suite_ret;
        }

        litmus_cleanup();

        if (fatal) ret = fatal;
        /* The exit status counts failed tests, and an exit status has
         * only 8 bits: stop short of the range the shell uses for
         * signalled exits rather than wrapping around to zero. */
        else ret = failures > 125 ? 125 : failures;
    }
    else if ((suite = litmus_suite_find(cmd)) != NULL) {
        ret = dispatch(suite, argv[0], argc - 1, argv + 1);
        litmus_cleanup();
    }
    else {
        fprintf(stderr, "%s: unknown command `%s'\n", argv[0], cmd);
        usage(stderr, argv[0]);
        ret = 1;
    }

    if (cmdname) {
        ne_free(cmdname);
        cmdname = NULL;
    }

    ne_sock_exit();

    return ret;
}

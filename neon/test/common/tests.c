/* 
   Stupidly simple test framework
   Copyright (C) 2001-2009, Joe Orton <joe@manyfish.co.uk>

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

#include "config.h"

#include <sys/types.h>

#include <stdio.h>
#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#include "ne_string.h"
#include "ne_alloc.h"
#include "ne_utils.h"
#include "ne_socket.h"
#include "ne_i18n.h"

#include "tests.h"
#include "child.h"

char test_context[BUFSIZ];
int have_context = 0;

static FILE *child_debug, *debug;

char **test_argv;
int test_argc;

const char *test_suite;
int test_num;

int test_json = 0;
int test_verbose = 0;
const char *test_target = NULL;
FILE *test_trace_fp = NULL;

/* Per-test record, collected for the JSON output. */
struct test_record {
    const char *name;
    const char *status;
    double duration;
    char *context;              /* NULL if the test set none */
    char **warns;
    unsigned nwarns;
};

static struct test_record *records;
static int nrecords;

static int quiet, count;

/* statistics for all tests so far */
static int passes = 0, fails = 0, skipped = 0, warnings = 0;

/* per-test globals: */
static int warned, aborted = 0;
static const char *test_name; /* current test name */

static int use_colour = 0, tty_output = 0;

static int flag_child;

static void print_prefix(int n);

/* resource for ANSI escape codes:
 * http://www.isthe.com/chongo/tech/comp/ansi_escapes.html */
#define COL(x) do { if (use_colour && !test_json) printf("\033[" x "m"); } while (0)

#define NOCOL COL("00")

/* In JSON mode stdout carries the JSON object and nothing else, so the
 * human-readable output is suppressed.  The surrounding code still runs
 * unchanged, so the counters and records stay correct either way. */
#define TPRINT(args) do { if (!test_json) printf args; } while (0)
#define TPUTCHAR(c) do { if (!test_json) putchar(c); } while (0)

/* Seconds since an arbitrary fixed point, or 0.0 if unavailable. */
static double now_seconds(void)
{
#if defined(HAVE_GETTIMEOFDAY) && defined(HAVE_SYS_TIME_H)
    struct timeval tv;

    if (gettimeofday(&tv, NULL) == 0)
        return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
#endif
    return 0.0;
}

/* Writes 'str' to stdout as a quoted JSON string. */
static void json_string(const char *str)
{
    putchar('"');
    for (; str && *str; str++) {
        unsigned char c = (unsigned char)*str;

        switch (c) {
        case '"': fputs("\\\"", stdout); break;
        case '\\': fputs("\\\\", stdout); break;
        case '\b': fputs("\\b", stdout); break;
        case '\f': fputs("\\f", stdout); break;
        case '\n': fputs("\\n", stdout); break;
        case '\r': fputs("\\r", stdout); break;
        case '\t': fputs("\\t", stdout); break;
        default:
            /* Escape the remaining controls; pass any high bytes through
             * so that UTF-8 in a server message survives intact. */
            if (c < 0x20)
                printf("\\u%04x", c);
            else
                putchar(c);
            break;
        }
    }
    putchar('"');
}

/* Records the outcome of test 'n'; a no-op unless JSON was requested. */
static void record_result(int n, const char *status, double duration)
{
    struct test_record *rec;

    if (!test_json || records == NULL || n >= count) return;

    rec = &records[n];
    rec->name = test_name;
    rec->status = status;
    rec->duration = duration;
    if (have_context) rec->context = ne_strdup(test_context);
    if (n >= nrecords) nrecords = n + 1;
}

/* Emits the whole run as a single JSON object on stdout.  "notrun"
 * covers tests never reached because an earlier one returned FAILHARD
 * or SKIPREST. */
static void emit_json(double duration)
{
    int i;

    printf("{\"suite\":");
    json_string(test_suite);
    if (test_target) {
        printf(",\"target\":");
        json_string(test_target);
    }
    printf(",\"duration\":%.3f,\"tests\":[", duration);

    for (i = 0; i < count; i++) {
        const struct test_record *rec = records ? &records[i] : NULL;
        const char *status = (rec && rec->status) ? rec->status : "notrun";

        if (i) putchar(',');
        printf("{\"name\":");
        json_string((rec && rec->name) ? rec->name : tests[i].name);
        printf(",\"status\":");
        json_string(status);
        printf(",\"duration\":%.3f", rec ? rec->duration : 0.0);

        if (rec && rec->context) {
            printf(",\"context\":");
            json_string(rec->context);
        }
        if (rec && rec->nwarns) {
            unsigned w;

            printf(",\"warnings\":[");
            for (w = 0; w < rec->nwarns; w++) {
                if (w) putchar(',');
                json_string(rec->warns[w]);
            }
            putchar(']');
        }
        putchar('}');
    }

    printf("],\"summary\":{\"total\":%d,\"passed\":%d,\"failed\":%d,"
           "\"skipped\":%d,\"notrun\":%d,\"warnings\":%d}}\n",
           count, passes, fails, skipped,
           count - nrecords, warnings);
    fflush(stdout);

    for (i = 0; records && i < count; i++) {
        unsigned w;

        if (records[i].context) ne_free(records[i].context);
        for (w = 0; w < records[i].nwarns; w++)
            ne_free(records[i].warns[w]);
        if (records[i].warns) ne_free(records[i].warns);
    }
    if (records) {
        ne_free(records);
        records = NULL;
    }
}

void t_context(const char *context, ...)
{
    va_list ap;
    va_start(ap, context);
    ne_vsnprintf(test_context, BUFSIZ, context, ap);
    va_end(ap);
    if (flag_child) {
        NE_DEBUG(NE_DBG_HTTP, "context: %s\n", test_context);
    }
    have_context = 1;
}

void t_warning(const char *str, ...)
{
    va_list ap;

    if (test_json) {
        /* Stash the message against the running test rather than
         * printing it; it is emitted in the JSON record. */
        char buf[BUFSIZ];

        va_start(ap, str);
        ne_vsnprintf(buf, sizeof buf, str, ap);
        va_end(ap);

        if (records != NULL && test_num < count) {
            struct test_record *rec = &records[test_num];

            rec->warns = ne_realloc(rec->warns,
                                    (rec->nwarns + 1) * sizeof(*rec->warns));
            rec->warns[rec->nwarns++] = ne_strdup(buf);
        }
    }
    else {
        if (warned) print_prefix(test_num);
        COL("43;01"); printf("WARNING:"); NOCOL;
        putchar(' ');
        va_start(ap, str);
        vprintf(str, ap);
        va_end(ap);
        putchar('\n');
    }

    warnings++;
    warned++;
}

#define TEST_DEBUG \
(NE_DBG_HTTP | NE_DBG_SOCKET | NE_DBG_HTTPBODY | NE_DBG_HTTPAUTH | \
 NE_DBG_LOCKS | NE_DBG_XMLPARSE | NE_DBG_XML | NE_DBG_SSL | \
 NE_DBG_HTTPPLAIN)

#define W(m) do { if (write(STDOUT_FILENO, m, strlen(m)) < 0) _exit(99); } while(0)

#define W_RED(m) do { if (use_colour) W("\033[41;37;01m"); \
W(m); if (use_colour) W("\033[00m\n"); } while (0);

#ifndef NEON_NO_TEST_CHILD

/* Signal handler for child processes. */
static void child_segv(int signo)
{
    signal(SIGSEGV, SIG_DFL); 
    signal(SIGABRT, SIG_DFL);
    W_RED("Fatal signal in child!");
    kill(getpid(), SIGSEGV);
    minisleep();
}

/* Signal handler for parent process. */
static void parent_segv(int signo)
{
    signal(SIGSEGV, SIG_DFL);
    signal(SIGABRT, SIG_DFL);
    if (signo == SIGSEGV) {
        W_RED("FAILED - Segmentation fault--\n");
    }
    else if (signo == SIGABRT) {
        W_RED("ABORTED\n");
    }
    else {
        W_RED("-- Unexpected signal! --\n");
    }
    reap_server();
    kill(getpid(), signo);
    minisleep();
}

void in_child(void)
{
    if (child_debug) {
        ne_debug_init(child_debug, TEST_DEBUG);
        NE_DEBUG(TEST_DEBUG, "**** Child forked for test %s ****\n", test_name);
        signal(SIGSEGV, child_segv);
        signal(SIGABRT, child_segv);
        flag_child = 1;
    }
}
#endif

static const char dots[] = "......................";

static void print_prefix(int n)
{
    if (test_json) return;

    if (quiet) {
        printf("\r%s%.*s %2u/%2u ", test_suite, 
               (int) (strlen(dots) - strlen(test_suite)), dots,
               n + 1, count);
    }
    else {
        if (warned) {
	    printf("    %s ", dots);
        }
        else {
            printf("\r%2d. %s%.*s ", n, test_name, 
               (int) (strlen(dots) - strlen(test_name)), dots);
        }
    }
    fflush(stdout);
}


int main(int argc, char *argv[])
{
    int n;
    char *tmp;
    double run_started;
    
    /* get basename(argv[0]) */
    test_suite = strrchr(argv[0], '/');
#ifdef _WIN32
    {
        /* Windows uses backslashes as path separators. */
        const char *bslash = strrchr(argv[0], '\\');

        if (bslash != NULL && (test_suite == NULL || bslash > test_suite))
            test_suite = bslash;
    }
#endif
    if (test_suite == NULL) {
	test_suite = argv[0];
    } else {
	test_suite++;
    }

    if (strncmp(test_suite, "lt-", 3) == 0)
        test_suite += 3;

#ifdef _WIN32
    {
        /* Strip the executable suffix from the suite name. */
        static char suite_name[64];
        size_t len = strlen(test_suite);

        if (len > 4 && len < sizeof(suite_name)
            && test_suite[len - 4] == '.'
            && (test_suite[len - 3] == 'e' || test_suite[len - 3] == 'E')
            && (test_suite[len - 2] == 'x' || test_suite[len - 2] == 'X')
            && (test_suite[len - 1] == 'e' || test_suite[len - 1] == 'E')) {
            memcpy(suite_name, test_suite, len - 4);
            suite_name[len - 4] = '\0';
            test_suite = suite_name;
        }
    }
#endif

#if defined(HAVE_SETLOCALE) && defined(LC_MESSAGES)
    setlocale(LC_MESSAGES, "");
#endif

    ne_i18n_init(NULL);

#if defined(HAVE_ISATTY) && defined(STDOUT_FILENO)
    tty_output = isatty(STDOUT_FILENO);
#endif

    if ((tmp = getenv("TEST_COLOUR")) != NULL)
        use_colour = strcmp(tmp, "1") == 0;
    else
        use_colour = tty_output;

    test_argc = argc;
    test_argv = argv;

    if ((tmp = getenv("TEST_NODEBUG")) == NULL) {
        debug = fopen("debug.log", "a");
        if (debug == NULL) {
            fprintf(stderr, "%s: Could not open debug.log: %s\n", test_suite,
                    strerror(errno));
            return -1;
        }
        child_debug = fopen("child.log", "a");
        if (child_debug == NULL) {
            fprintf(stderr, "%s: Could not open child.log: %s\n", test_suite,
                    strerror(errno));
            fclose(debug);
            return -1;
        }
    }

    if (tests[0].fn == NULL) {
	printf("-> no tests found in `%s'\n", test_suite);
	return -1;
    }

#ifndef NEON_NO_TEST_CHILD
    /* install special SEGV handler. */
    signal(SIGSEGV, parent_segv);
    signal(SIGABRT, parent_segv);
#endif

    /* test the "no-debugging" mode of ne_debug. */
    ne_debug_init(NULL, 0);
    NE_DEBUG(TEST_DEBUG, "This message should go to /dev/null");

    if (debug) {
        /* enable debugging for real. */
        ne_debug_init(debug, TEST_DEBUG);
        NE_DEBUG(TEST_DEBUG | NE_DBG_FLUSH, "Version string: %s\n", 
                 ne_version_string());
    }

    /* another silly test. */
    NE_DEBUG(0, "This message should also go to /dev/null");

    if (ne_sock_init()) {
	COL("43;01"); printf("WARNING:"); NOCOL;
	printf(" Socket library initialization failed.\n");
    }

#ifdef NEON_TEST_INIT
    if (NEON_TEST_INIT(test_argc, (const char *const *)test_argv, &use_colour, &quiet)) {
	fprintf(stderr, "%s: Failed parsing command-line.\n", test_suite);
        return -1;
    }
#endif

    if ((tmp = getenv("TEST_QUIET")) != NULL) {
        quiet = strcmp(tmp, "1") == 0;
    }

    /* One decision point for where the protocol trace goes.  A wire
     * dump takes the destination, since its whole purpose is to collect
     * the traffic in one place; --verbose then widens the mask from
     * message bodies alone to everything neon can report. */
    if (test_trace_fp != NULL) {
        ne_debug_init(test_trace_fp,
                      test_verbose ? TEST_DEBUG : NE_DBG_HTTPBODY);
    }
    else if (test_verbose) {
        ne_debug_init(stderr, TEST_DEBUG);
        NE_DEBUG(TEST_DEBUG | NE_DBG_FLUSH, "Verbose mode: %s\n",
                 ne_version_string());
    }

    if (!quiet)
        TPRINT(("-> running `%s':\n", test_suite));

    for (count = 0; tests[count].fn; count++)
        /* nullop */;

    if (test_json && count > 0)
        records = ne_calloc(count * sizeof(*records));

    run_started = now_seconds();

    for (n = 0; !aborted && tests[n].fn != NULL; n++) {
	int result, is_xfail = 0;
        double started, elapsed;
#ifdef NEON_MEMLEAK
        size_t allocated = ne_alloc_used;
        int is_xleaky = 0;
#endif

	test_name = tests[n].name;

        print_prefix(n);

	have_context = 0;
	test_num = n;
	warned = 0;
	fflush(stdout);
	NE_DEBUG(TEST_DEBUG, "******* Running test %d: %s ********\n",
		 n, test_name);

	/* run the test. */
        started = now_seconds();
	result = tests[n].fn();
        elapsed = now_seconds() - started;

#ifdef NEON_MEMLEAK
        /* issue warnings for memory leaks, if requested */
        if ((tests[n].flags & T_CHECK_LEAKS)
            && (result == OK || result == SKIP) &&
            ne_alloc_used > allocated) {
            t_context("memory leak of %" NE_FMT_SIZE_T " bytes",
                      ne_alloc_used - allocated);
            fprintf(debug, "Blocks leaked: ");
            ne_alloc_dump(debug);
            result = FAIL;
        } else if (tests[n].flags & T_EXPECT_LEAKS && result == OK &&
                   ne_alloc_used == allocated) {
            t_context("expected memory leak not detected");
            result = FAIL;
        } else if (tests[n].flags & T_EXPECT_LEAKS && result == OK) {
            fprintf(debug, "Blocks leaked (expected): ");
            ne_alloc_dump(debug);
            is_xleaky = 1;
        } 
#endif

        if (tests[n].flags & T_EXPECT_FAIL) {
            if (result == OK) {
                t_context("test passed but expected failure");
                result = FAIL;
            } else if (result == FAIL) {
                result = OK;
                is_xfail = 1;
            }
        }

        print_prefix(n);

	switch (result) {
	case OK:
	    passes++;
            record_result(n, is_xfail ? "xfail" : "pass", elapsed);
            if (is_xfail) {
                COL("32;07");
                TPRINT(("XFAIL"));
            } else if (!quiet) {
                COL("32");
                TPRINT(("pass"));
            }
            NOCOL;
            if (quiet && is_xfail) {
                TPRINT((" - %s", test_name));
                if (have_context) {
                    TPRINT((" (%s)", test_context));
                }
            }
	    if (warned && !quiet) {
		TPRINT((" (with %d warning%s)", warned, (warned > 1)?"s":""));
	    }
#ifdef NEON_MEMLEAK
            if (is_xleaky) {
                if (quiet) {
                    TPRINT(("expected leak - %s: %" NE_FMT_SIZE_T " bytes",
                            test_name, ne_alloc_used - allocated));
                }
                else {
                    TPRINT((" (expected leak, %" NE_FMT_SIZE_T " bytes)",
                            ne_alloc_used - allocated));
                }
            }
#endif
	    if (!quiet || is_xfail) TPUTCHAR('\n');
	    break;
	case FAILHARD:
	    aborted = 1;
            record_result(n, "fatal", elapsed);
	    COL("41;37;01"); TPRINT(("fatal error - ")); NOCOL;
	    /* fall-through */
	case FAIL:
            if (result == FAIL) record_result(n, "fail", elapsed);
	    COL("41;37;01"); TPRINT(("FAIL")); NOCOL;
            if (quiet) {
                TPRINT((" - %s", test_name));
            }
	    if (have_context) {
		TPRINT((" (%s)", test_context));
	    }
	    TPUTCHAR('\n');
	    fails++;
	    break;
	case SKIPREST:
	    aborted = 1;
	    /* fall-through */
	case SKIP:
            record_result(n, "skip", elapsed);
	    COL("44;37;01"); TPRINT(("SKIPPED")); NOCOL;
            if (quiet) {
                TPRINT((" - %s", test_name));
            }
	    if (have_context) {
		TPRINT((" (%s)", test_context));
	    }
	    TPUTCHAR('\n');
	    skipped++;
	    break;
	default:
            record_result(n, "oops", elapsed);
	    COL("41;37;01"); TPRINT(("OOPS")); NOCOL;
	    TPRINT((" unexpected test result `%d'\n", result));
	    break;
	}

#ifndef NEON_NO_TEST_CHILD
	reap_server();
#endif

        if (quiet) {
            print_prefix(n);
        }
    }

    if (test_json) {
        emit_json(now_seconds() - run_started);
    }

    /* discount skipped tests */
    if (skipped) {
        if (!quiet)
            TPRINT(("-> %d %s.\n", skipped,
                    skipped == 1 ? "test was skipped" : "tests were skipped"));
	n -= skipped;
    }
    /* print the summary. */
    if (test_json) {
        /* results already emitted above */
    } else if (skipped && n == 0) {
        if (quiet)
            puts("(all skipped)");
        else
            printf("<- all tests skipped for `%s'.\n", test_suite);
    } else {
        if (quiet) {
            printf("\r%s%.*s %2u/%2u ", test_suite, 
                   (int) (strlen(dots) - strlen(test_suite)), dots,
                   passes, count);
            if (fails == 0) {
                COL("32"); 
                printf("passed");
                NOCOL;
                putchar(' ');
            }
            else {
                printf("passed, %d failed ", fails);
            }                       
            if (skipped)
                printf("(%d skipped) ", skipped);
        }
        else /* !quiet */
            printf("<- summary for `%s': "
                   "of %d tests run: %d passed, %d failed. %.1f%%\n",
                   test_suite, n, passes, fails, 100*(float)passes/n);
	if (warnings) {
	    if (quiet) {
                printf("(%d warning%s)\n", warnings, 
                       warnings > 1 ? "s" : "");
            }
            else {
                printf("-> %d warning%s issued.\n", warnings, 
                       warnings==1?" was":"s were");
            }
        }
        else if (quiet) {
            putchar('\n');
        }
    }

    if (debug && fclose(debug)) {
	fprintf(stderr, "Error closing debug.log: %s\n", strerror(errno));
	fails = 1;
    }
       
    if (child_debug && fclose(child_debug)) {
	fprintf(stderr, "Error closing child.log: %s\n", strerror(errno));
	fails = 1;
    }

    ne_sock_exit();
    
    return fails;
}


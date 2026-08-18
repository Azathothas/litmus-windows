/*
   litmus: WebDAV server test suite: a synthetic suite that checks the
   result harness itself
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

/* Everything else in this tree needs a live WebDAV server to run, which
 * is why tests/wsgidav.sh and tests/godav.sh exist.  That leaves the
 * result harness itself -- the counting, the statuses, the JSON
 * emission and its escaping, the notrun bookkeeping, and the state
 * reset between suites -- checked only as a side effect of running real
 * tests against a real server, which is a slow and indirect way to find
 * out that a counter is wrong.
 *
 * These suites have no server behind them.  Each test just returns the
 * result it is named for, so the output is fully determined by the
 * harness.  tests/harness.sh runs them and compares against checked-in
 * expected output.
 *
 * What is deliberately not covered: the test logic in the other suites.
 * Checking that basic.c asks for the right thing needs something that
 * answers, and the two server harnesses already do that better than a
 * canned transport would.
 *
 * Adding a test here means regenerating the expected output; the script
 * has a --regenerate flag for it.  Read the diff before checking it in.
 */

#include <config.h>

#include <stdio.h>

#include "common.h"
#include "suites.h"

/* --- results, statuses, escaping ------------------------------- */

static int pass_plain(void)
{
    return OK;
}

static int pass_one_warning(void)
{
    t_warning("one warning, and the test still passes");
    return OK;
}

static int pass_two_warnings(void)
{
    t_warning("first warning");
    t_warning("second warning");
    return OK;
}

/* Declared T_XFAIL, so returning FAIL is the expected outcome and the
 * harness must report xfail and count it as a pass. */
static int xfail_as_expected(void)
{
    return FAIL;
}

static int fail_no_context(void)
{
    return FAIL;
}

static int fail_with_context(void)
{
    t_context("a plain failure message");
    return FAIL;
}

/* Every character the JSON writer has a rule for, plus a byte it must
 * escape numerically and two multi-byte UTF-8 sequences it must pass
 * through untouched.  In JSON this must come back as \", \\, \n, \r,
 * \t, \b, \f,  and the UTF-8 unaltered.
 *
 * tests/harness.sh strips carriage returns before comparing the text
 * output, because stdout is in text mode on Windows and every line
 * ending would otherwise differ from a Unix run.  The literal CR here
 * is therefore checked in the JSON comparison rather than the text
 * one, which is where an escaping bug would actually show up. */
static int fail_needing_escapes(void)
{
    t_context("quote \" backslash \\ newline \n return \r tab \t "
              "backspace \b formfeed \f control \001 "
              "utf-8 \xc3\xa9 \xe2\x86\x92 end");
    return FAIL;
}

/* A failure that reached the server: the harness must attach the
 * method, path and status as the "error" object. */
static int fail_with_response(void)
{
    t_request_begin("PROPFIND", "/dav/litmus/nosuch");
    t_request_status(507);
    t_context("PROPFIND on `/dav/litmus/nosuch': 507 Insufficient Storage");
    return FAIL;
}

/* A failure where the request never got an answer: same object, but
 * with a null status.  A consumer has to be able to tell this apart
 * from a failure that never made a request at all. */
static int fail_without_response(void)
{
    t_request_begin("PUT", "/dav/litmus/unreachable");
    t_context("PUT on `/dav/litmus/unreachable': could not connect");
    return FAIL;
}

/* Follows the two above to prove the recorded request is cleared
 * between tests rather than inherited. */
static int fail_after_request(void)
{
    t_context("no request was made by this test");
    return FAIL;
}

static int skip_quietly(void)
{
    return SKIP;
}

static int skip_with_context(void)
{
    t_context("precondition not met");
    return SKIP;
}

/* Anything the harness does not recognise must be reported as oops
 * rather than silently counted as something else. */
static int oops_unknown_result(void)
{
    return 42;
}

static int pass_after_oops(void)
{
    return OK;
}

ne_test selftest_results_tests[] = {
    T(pass_plain),
    T(pass_one_warning),
    T(pass_two_warnings),
    T_XFAIL(xfail_as_expected),
    T(fail_no_context),
    T(fail_with_context),
    T(fail_needing_escapes),
    T(fail_with_response),
    T(fail_without_response),
    T(fail_after_request),
    T(skip_quietly),
    T(skip_with_context),
    T(oops_unknown_result),
    T(pass_after_oops),
    T(NULL)
};

/* --- a suite that stops dead ----------------------------------- */

static int pass_before_fatal(void)
{
    return OK;
}

static int fatal_stops_the_suite(void)
{
    t_request_begin("MKCOL", "/dav/litmus/");
    t_request_status(405);
    t_context("MKCOL on `/dav/litmus/': 405 Method Not Allowed");
    return FAILHARD;
}

static int never_reached_1(void)
{
    return OK;
}

static int never_reached_2(void)
{
    return OK;
}

ne_test selftest_fatal_tests[] = {
    T(pass_before_fatal),
    T(fatal_stops_the_suite),
    T(never_reached_1),
    T(never_reached_2),
    T(NULL)
};

/* --- a suite that skips the rest ------------------------------- */

static int pass_before_skiprest(void)
{
    return OK;
}

static int skiprest_stops(void)
{
    t_context("server is not class 2, so the rest cannot run");
    return SKIPREST;
}

static int never_reached_3(void)
{
    return OK;
}

ne_test selftest_skiprest_tests[] = {
    T(pass_before_skiprest),
    T(skiprest_stops),
    T(never_reached_3),
    T(NULL)
};

/* Run in this order.  Running three suites in one process is also the
 * point: it is what proves reset_state() and litmus_reset() put every
 * counter back, which is the part of the single-executable change that
 * would fail silently. */
const struct litmus_suite litmus_selftest_suites[] = {
    { "selftest-results", selftest_results_tests,
      "every status, warnings, contexts and JSON escaping", 0, 0 },
    { "selftest-fatal", selftest_fatal_tests,
      "a fatal test, and the notrun bookkeeping after it", 0, 0 },
    { "selftest-skiprest", selftest_skiprest_tests,
      "a test that skips the rest of its suite", 0, 0 },
    { NULL, NULL, NULL, 0, 0 }
};

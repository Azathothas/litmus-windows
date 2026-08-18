/*
   litmus: WebDAV server test suite: the suite registry
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

#ifndef LITMUS_SUITES_H
#define LITMUS_SUITES_H 1

#include "tests.h"

/* Each suite defines its array under its own name, so that all of them
 * can be linked into one executable. */
extern ne_test basic_tests[];
extern ne_test copymove_tests[];
extern ne_test props_tests[];
extern ne_test locks_tests[];
extern ne_test http_tests[];
extern ne_test largefile_tests[];
extern ne_test protected_tests[];
extern ne_test lockbomb_tests[];
extern ne_test lockbomb_single_tests[];

struct litmus_suite {
    const char *name;
    ne_test *tests;
    const char *summary;        /* one line, for `litmus-cli list' */
    int in_all;                 /* run by `litmus-cli all' */
    int threads;                /* default --threads, 0 if not applicable */
};

/* NULL-terminated, in the order `litmus-cli all' runs them. */
extern const struct litmus_suite litmus_suites[];

/* Returns the suite called 'name', or NULL. */
const struct litmus_suite *litmus_suite_find(const char *name);

#endif /* LITMUS_SUITES_H */

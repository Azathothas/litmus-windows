/*
   litmus: WebDAV server test suite: throughput and latency measurement
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

#ifndef LITMUS_BENCH_H
#define LITMUS_BENCH_H 1

#include <ne_defs.h>

/* Settings for one benchmark run.  They are all reported alongside the
 * results, because two runs made with different connection settings are
 * not comparable. */
struct litmus_bench_options {
    unsigned files;             /* small files to transfer */
    ne_off_t size;              /* bytes in each small file */
    ne_off_t large;             /* bytes in the large file, 0 to skip */
    unsigned concurrency;       /* transfers in flight at once */
    unsigned pings;             /* TCP connect probes, 0 to skip */
    int connect_timeout;        /* seconds */
    int read_timeout;           /* seconds */
    int persist;                /* keep-alive */
};

/* Defaults, chosen to finish in a few seconds against a local server
 * while still being long enough to measure. */
#define LITMUS_BENCH_FILES (64)
#define LITMUS_BENCH_SIZE (65536)
#define LITMUS_BENCH_LARGE (64 * 1024 * 1024)
#define LITMUS_BENCH_CONCURRENCY (8)
#define LITMUS_BENCH_PINGS (20)

/* Ceilings, so that a typo cannot ask for something absurd. */
#define LITMUS_BENCH_MAX_FILES (1000000)
#define LITMUS_BENCH_MAX_CONCURRENCY (256)
#define LITMUS_BENCH_MAX_PINGS (10000)
/* 1 TiB, far past anything worth measuring over HTTP. */
#define LITMUS_BENCH_MAX_BYTES (1099511627776LL)

/* Runs the benchmark against the session already opened by begin().
 * Returns 0 if every transfer and probe succeeded, 1 otherwise. */
int litmus_bench(const struct litmus_bench_options *opts);

#endif /* LITMUS_BENCH_H */

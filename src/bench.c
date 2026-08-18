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

/* This measures how fast a server is, not whether it is correct, and
 * it is deliberately not part of any suite: nothing here passes or
 * fails.  All sizes are binary -- KiB, MiB, GiB are powers of 1024 --
 * and rates are MiB/s.  Every duration is wall-clock, so it includes
 * server and network time.
 */

#include "config.h"

#include <sys/types.h>

#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#if defined(HAVE_PTHREADS)
#include <pthread.h>
#endif

#include <ne_session.h>
#include <ne_request.h>
#include <ne_basic.h>
#include <ne_string.h>
#include <ne_alloc.h>
#include <ne_uri.h>
#include <ne_socket.h>

#include "common.h"
#include "bench.h"

/* The payload buffer, filled once with pseudo-random bytes.  It has to
 * be incompressible: a run of zeroes or a repeating block is squashed
 * by a server, a proxy or a filesystem somewhere in the path, and the
 * measured rate becomes fiction.  It also has to be cheap to produce,
 * so it is generated once here and streamed from at a rotating offset
 * rather than being regenerated per read. */
#define PAYLOAD_SIZE (4 * 1024 * 1024)

static unsigned char *payload;

/* xorshift64*, which is fast and whose output no general-purpose
 * compressor can shrink.  A cryptographic generator would do as well
 * but is not needed to make bytes that do not compress. */
static void fill_payload(unsigned long long seed)
{
    size_t n;

    payload = ne_malloc(PAYLOAD_SIZE);

    if (seed == 0) seed = 0x9E3779B97F4A7C15ULL;

    for (n = 0; n < PAYLOAD_SIZE; n++) {
        seed ^= seed >> 12;
        seed ^= seed << 25;
        seed ^= seed >> 27;
        payload[n] = (unsigned char)((seed * 0x2545F4914F6CDD1DULL) >> 56);
    }
}

/* One transfer's share of the results. */
struct outcome {
    ne_off_t bytes;
    unsigned errors;
    char *message;              /* first error seen, or NULL */
};

/* What one scenario measured. */
struct scenario {
    const char *name;
    const char *summary;
    unsigned files;
    ne_off_t bytes;
    double duration;            /* wall-clock seconds */
    unsigned errors;
    char *message;              /* first error seen, or NULL */
};

#define MAX_SCENARIOS (8)

static struct scenario scenarios[MAX_SCENARIOS];
static unsigned nscenarios;

/* Latency probe results, in milliseconds. */
struct latency {
    unsigned attempted, failed;
    double min, max, mean, jitter;
    char *message;
};

/* Streams from the payload buffer at a rotating offset, so that no two
 * concurrent transfers send byte-identical data. */
struct provider_state {
    ne_off_t remaining;
    ne_off_t total;
    size_t offset;
};

static ssize_t payload_provider(void *userdata, char *buffer, size_t buflen)
{
    struct provider_state *st = userdata;
    size_t want, first;

    if (buflen == 0) {
        /* neon rewinds the body before a retry. */
        st->remaining = st->total;
        return 0;
    }

    if (st->remaining == 0) return 0;

    want = buflen;
    if ((ne_off_t)want > st->remaining) want = (size_t)st->remaining;

    /* Copy across the wrap in at most two pieces. */
    first = PAYLOAD_SIZE - st->offset;
    if (first > want) first = want;
    memcpy(buffer, payload + st->offset, first);
    if (first < want)
        memcpy(buffer + first, payload, want - first);

    st->offset = (st->offset + want) % PAYLOAD_SIZE;
    st->remaining -= (ne_off_t)want;

    return (ssize_t)want;
}

/* Records an error against 'out' without stopping the run: one failed
 * transfer should not discard the whole measurement. */
static void note_error(struct outcome *out, ne_session *sess,
                       const char *what, const char *path)
{
    out->errors++;
    if (out->message == NULL)
        out->message = ne_concat(what, " ", path, ": ", ne_get_error(sess),
                                 NULL);
}

/* PUTs 'size' bytes to 'path'.  'seq' gives the transfer its own
 * starting offset in the payload. */
static void put_one(ne_session *sess, const char *path, ne_off_t size,
                    unsigned seq, struct outcome *out)
{
    ne_request *req = ne_request_create(sess, "PUT", path);
    struct provider_state st;

    st.total = st.remaining = size;
    /* An odd stride keeps the starting offsets apart for any file
     * count that is a power of two. */
    st.offset = (size_t)((seq * 1048573u) % PAYLOAD_SIZE);

    ne_set_request_body_provider(req, size, payload_provider, &st);

    if (ne_request_dispatch(req) != NE_OK
        || ne_get_status(req)->klass != 2)
        note_error(out, sess, "PUT", path);
    else
        out->bytes += size;

    ne_request_destroy(req);
}

/* GETs 'path', discarding the body but counting it. */
static void get_one(ne_session *sess, const char *path, ne_off_t expect,
                    struct outcome *out)
{
    ne_request *req = ne_request_create(sess, "GET", path);
    char block[65536];
    ne_off_t got = 0;
    ssize_t bytes;
    int ret;

    ret = ne_begin_request(req);
    if (ret == NE_OK && ne_get_status(req)->klass != 2) {
        ne_discard_response(req);
        ne_end_request(req);
        ret = NE_ERROR;
    }
    else if (ret == NE_OK) {
        while ((bytes = ne_read_response_block(req, block, sizeof block)) > 0)
            got += bytes;

        ret = bytes < 0 ? NE_ERROR : ne_end_request(req);
    }

    if (ret != NE_OK)
        note_error(out, sess, "GET", path);
    else if (expect >= 0 && got != expect) {
        out->errors++;
        if (out->message == NULL)
            out->message = ne_concat("GET ", path,
                                     ": wrong length returned", NULL);
    }
    else
        out->bytes += got;

    ne_request_destroy(req);
}

/* A pool of exactly 'concurrency' workers, which is how the number of
 * transfers in flight is bounded: there is no way for the pool to run
 * more of them at once than it has threads.  Each worker owns its own
 * session, since a neon session is not safe to share, and takes the
 * next file index from a mutex-protected counter. */
struct pool {
    int upload;
    unsigned files;
    ne_off_t size;
    const char *base;           /* collection the files live in */
    unsigned next;
    struct outcome out;
#if defined(HAVE_PTHREADS)
    pthread_mutex_t lock;
#endif
};

static unsigned pool_take(struct pool *p)
{
    unsigned index;

#if defined(HAVE_PTHREADS)
    pthread_mutex_lock(&p->lock);
#endif
    index = p->next < p->files ? p->next++ : p->files;
#if defined(HAVE_PTHREADS)
    pthread_mutex_unlock(&p->lock);
#endif

    return index;
}

static void pool_merge(struct pool *p, struct outcome *out)
{
#if defined(HAVE_PTHREADS)
    pthread_mutex_lock(&p->lock);
#endif
    p->out.bytes += out->bytes;
    p->out.errors += out->errors;
    if (p->out.message == NULL) {
        p->out.message = out->message;
        out->message = NULL;
    }
#if defined(HAVE_PTHREADS)
    pthread_mutex_unlock(&p->lock);
#endif

    if (out->message) ne_free(out->message);
}

static void *pool_worker(void *userdata)
{
    struct pool *p = userdata;
    struct outcome out = {0};
    ne_session *sess;
    unsigned index;

    sess = litmus_new_session();
    if (sess == NULL) {
        out.errors++;
        out.message = ne_strdup("could not create a session");
        pool_merge(p, &out);
        return NULL;
    }

    while ((index = pool_take(p)) < p->files) {
        char *path = ne_malloc(strlen(p->base) + 32);

        sprintf(path, "%sfile-%06u", p->base, index);

        if (p->upload)
            put_one(sess, path, p->size, index, &out);
        else
            get_one(sess, path, p->size, &out);

        ne_free(path);
    }

    ne_session_destroy(sess);
    pool_merge(p, &out);

    return NULL;
}

/* Records a scenario's numbers. */
static void add_scenario(const char *name, const char *summary,
                         unsigned files, const struct outcome *out,
                         double duration)
{
    struct scenario *s;

    if (nscenarios >= MAX_SCENARIOS) return;

    s = &scenarios[nscenarios++];
    s->name = name;
    s->summary = summary;
    s->files = files;
    s->bytes = out->bytes;
    s->duration = duration;
    s->errors = out->errors;
    s->message = out->message;
}

/* Runs one many-small-files scenario, uploading or downloading. */
static void run_pool(const char *name, const char *summary, int upload,
                     unsigned files, ne_off_t size, unsigned concurrency,
                     const char *base)
{
    struct pool p = {0};
    double started;
#if defined(HAVE_PTHREADS)
    pthread_t *threads;
    unsigned n, started_count = 0;
#endif

    p.upload = upload;
    p.files = files;
    p.size = size;
    p.base = base;

#if defined(HAVE_PTHREADS)
    pthread_mutex_init(&p.lock, NULL);
    threads = ne_calloc(concurrency * sizeof(*threads));
#endif

    started = test_now_seconds();

#if defined(HAVE_PTHREADS)
    for (n = 0; n < concurrency; n++) {
        if (pthread_create(&threads[n], NULL, pool_worker, &p) != 0) break;
        started_count++;
    }

    if (started_count == 0)
        pool_worker(&p);        /* no threads available; do it here */

    for (n = 0; n < started_count; n++)
        pthread_join(threads[n], NULL);

    ne_free(threads);
    pthread_mutex_destroy(&p.lock);
#else
    (void)concurrency;
    pool_worker(&p);
#endif

    add_scenario(name, summary, files, &p.out, test_now_seconds() - started);
}

/* Runs one large-file transfer on its own session. */
static void run_single(const char *name, const char *summary, int upload,
                       ne_off_t size, const char *path)
{
    struct outcome out = {0};
    ne_session *sess = litmus_new_session();
    double started;

    if (sess == NULL) {
        out.errors++;
        out.message = ne_strdup("could not create a session");
        add_scenario(name, summary, 1, &out, 0.0);
        return;
    }

    started = test_now_seconds();

    if (upload)
        put_one(sess, path, size, 0, &out);
    else
        get_one(sess, path, size, &out);

    add_scenario(name, summary, 1, &out, test_now_seconds() - started);

    ne_session_destroy(sess);
}

/* Connects a TCP socket to the host litmus is talking to, 'attempts'
 * times, and reports how long each took.  This needs no raw sockets and
 * no administrator rights, and it separates network round-trip time
 * from server processing time, which is otherwise guesswork.  For a TLS
 * target it measures the TCP connect only, not the handshake. */
static void probe_latency(unsigned attempts, struct latency *lat)
{
    const char *host = litmus_target_host();
    unsigned port = litmus_target_port();
    ne_sock_addr *addr;
    double *samples;
    double total = 0.0, spread = 0.0;
    unsigned n, ok = 0;

    memset(lat, 0, sizeof *lat);
    lat->attempted = attempts;

    if (attempts == 0) return;

    addr = ne_addr_resolve(host, 0);
    if (ne_addr_result(addr)) {
        char buf[256];

        lat->failed = attempts;
        lat->message = ne_concat("could not resolve `", host, "': ",
                                 ne_addr_error(addr, buf, sizeof buf), NULL);
        ne_addr_destroy(addr);
        return;
    }

    samples = ne_calloc(attempts * sizeof(*samples));

    for (n = 0; n < attempts; n++) {
        const ne_inet_addr *ia;
        ne_socket *sock = ne_sock_create();
        double started;
        int connected = 0;

        if (sock == NULL) {
            lat->failed++;
            continue;
        }

        ne_sock_connect_timeout(sock, 10);

        started = test_now_seconds();
        for (ia = ne_addr_first(addr); ia && !connected;
             ia = ne_addr_next(addr))
            connected = ne_sock_connect(sock, ia, port) == 0;

        if (connected) {
            samples[ok++] = (test_now_seconds() - started) * 1000.0;
            ne_sock_close(sock);
        }
        else {
            lat->failed++;
            if (lat->message == NULL)
                lat->message = ne_strdup(ne_sock_error(sock));
            ne_sock_close(sock);
        }

        /* A short pause so a run of probes does not look like a flood. */
        if (n + 1 < attempts) litmus_sleep_ms(50);
    }

    ne_addr_destroy(addr);

    if (ok == 0) {
        ne_free(samples);
        return;
    }

    lat->min = lat->max = samples[0];
    for (n = 0; n < ok; n++) {
        if (samples[n] < lat->min) lat->min = samples[n];
        if (samples[n] > lat->max) lat->max = samples[n];
        total += samples[n];
    }
    lat->mean = total / ok;

    /* Jitter as the mean absolute deviation from the mean: it says how
     * far a typical probe sits from the average, in the same units,
     * without a square root making outliers dominate. */
    for (n = 0; n < ok; n++) {
        double d = samples[n] - lat->mean;

        spread += d < 0 ? -d : d;
    }
    lat->jitter = spread / ok;

    ne_free(samples);
}

/* MiB/s over a wall-clock duration; zero if the duration is too small
 * to divide by. */
static double mib_per_s(ne_off_t bytes, double duration)
{
    if (duration <= 0.0) return 0.0;
    return ((double)bytes / (1024.0 * 1024.0)) / duration;
}

/* Writes a byte count as the exact figure with the rounded binary one
 * after it, e.g. "67108864 bytes (64.0 MiB)". */
static void format_bytes(char *buf, size_t buflen, ne_off_t bytes)
{
    static const char *units[] = { "bytes", "KiB", "MiB", "GiB", "TiB" };
    double value = (double)bytes;
    unsigned unit = 0;

    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        unit++;
    }

    if (unit == 0)
        ne_snprintf(buf, buflen, "%" NE_FMT_NE_OFF_T " bytes", bytes);
    else
        ne_snprintf(buf, buflen, "%" NE_FMT_NE_OFF_T " bytes (%.1f %s)",
                    bytes, value, units[unit]);
}

static void report_text(const struct litmus_bench_options *opts,
                        const struct latency *lat, const char *started,
                        double total)
{
    char sizes[64];
    unsigned n;

    printf("-> benchmarking `%s':\n", test_target ? test_target : "");

    if (started[0]) printf("   started      %s\n", started);
    printf("   concurrency  %u parallel transfers\n", opts->concurrency);

    format_bytes(sizes, sizeof sizes, (ne_off_t)PAYLOAD_SIZE);
    printf("   payload      %s of incompressible pseudo-random data,\n"
           "                streamed from a rotating offset\n", sizes);
    {
        char connect_to[32], read_to[32];

        /* Zero means the option was not given, so neon's own default
         * applies; saying "0s" would read as "no time at all". */
        if (opts->connect_timeout > 0)
            ne_snprintf(connect_to, sizeof connect_to, "%ds",
                        opts->connect_timeout);
        else
            ne_snprintf(connect_to, sizeof connect_to, "neon default");

        if (opts->read_timeout > 0)
            ne_snprintf(read_to, sizeof read_to, "%ds", opts->read_timeout);
        else
            ne_snprintf(read_to, sizeof read_to, "neon default");

        printf("   keep-alive   %s, connect timeout %s, read timeout %s\n",
               opts->persist ? "on" : "off", connect_to, read_to);
    }
    putchar('\n');

    for (n = 0; n < nscenarios; n++) {
        const struct scenario *s = &scenarios[n];

        format_bytes(sizes, sizeof sizes, s->bytes);
        printf("   %-15s %5u %-5s %-30s %8.3f s %8.2f MiB/s  %u error%s\n",
               s->name, s->files, s->files == 1 ? "file" : "files", sizes,
               s->duration, mib_per_s(s->bytes, s->duration), s->errors,
               s->errors == 1 ? "" : "s");
        if (s->message)
            printf("   %-15s first error: %s\n", "", s->message);
    }

    putchar('\n');
    if (lat->attempted == 0) {
        printf("   latency      not measured\n");
    }
    else if (lat->failed == lat->attempted) {
        printf("   latency      %u/%u probes to %s port %u failed%s%s\n",
               lat->failed, lat->attempted, litmus_target_host(),
               litmus_target_port(), lat->message ? ": " : "",
               lat->message ? lat->message : "");
    }
    else {
        printf("   latency      %u TCP connects to %s port %u\n",
               lat->attempted, litmus_target_host(), litmus_target_port());
        printf("                min %.3f ms  mean %.3f ms  max %.3f ms  "
               "jitter %.3f ms  loss %u/%u\n",
               lat->min, lat->mean, lat->max, lat->jitter,
               lat->failed, lat->attempted);
    }

    printf("\n<- benchmark of `%s' took %.3f s wall-clock, which includes "
           "server and network time.\n", test_target ? test_target : "",
           total);
}

static void report_json(const struct litmus_bench_options *opts,
                        const struct latency *lat, const char *started,
                        double total)
{
    unsigned n;

    printf("{\"bench\":\"litmus\"");
    if (test_target) {
        printf(",\"target\":");
        test_json_string(test_target);
    }
    if (started[0]) {
        printf(",\"started\":");
        test_json_string(started);
    }
    printf(",\"duration\":%.3f", total);
    printf(",\"concurrency\":%u", opts->concurrency);
    printf(",\"payload\":{\"bytes\":%d,\"source\":\"prng\"}", PAYLOAD_SIZE);
    /* A timeout of 0 means the option was not given and neon's own
     * default applies, so it is reported as null rather than as zero
     * seconds. */
    printf(",\"connection\":{\"keepalive\":%s,\"connect_timeout\":",
           opts->persist ? "true" : "false");
    if (opts->connect_timeout > 0) printf("%d", opts->connect_timeout);
    else fputs("null", stdout);
    printf(",\"read_timeout\":");
    if (opts->read_timeout > 0) printf("%d", opts->read_timeout);
    else fputs("null", stdout);
    putchar('}');

    printf(",\"scenarios\":[");
    for (n = 0; n < nscenarios; n++) {
        const struct scenario *s = &scenarios[n];

        if (n) putchar(',');
        printf("{\"name\":");
        test_json_string(s->name);
        printf(",\"files\":%u,\"bytes\":%" NE_FMT_NE_OFF_T
               ",\"duration\":%.3f,\"mib_per_s\":%.2f,\"errors\":%u",
               s->files, s->bytes, s->duration,
               mib_per_s(s->bytes, s->duration), s->errors);
        if (s->message) {
            printf(",\"message\":");
            test_json_string(s->message);
        }
        putchar('}');
    }
    putchar(']');

    printf(",\"latency\":{\"unit\":\"ms\",\"attempted\":%u,\"failed\":%u",
           lat->attempted, lat->failed);
    if (lat->failed < lat->attempted)
        printf(",\"min\":%.3f,\"mean\":%.3f,\"max\":%.3f,\"jitter\":%.3f",
               lat->min, lat->mean, lat->max, lat->jitter);
    if (lat->message) {
        printf(",\"message\":");
        test_json_string(lat->message);
    }
    putchar('}');

    printf("}\n");
    fflush(stdout);
}

/* Removes what the run created; failures here are not interesting
 * enough to report. */
static void cleanup(unsigned files, const char *base, const char *large)
{
    ne_session *sess = litmus_new_session();
    unsigned n;

    if (sess == NULL) return;

    (void) ne_delete(sess, large);

    for (n = 0; n < files; n++) {
        char *path = ne_malloc(strlen(base) + 32);

        sprintf(path, "%sfile-%06u", base, n);
        (void) ne_delete(sess, path);
        ne_free(path);
    }

    (void) ne_delete(sess, base);

    ne_session_destroy(sess);
}

int litmus_bench(const struct litmus_bench_options *opts)
{
    struct latency lat = {0};
    char started[40];
    char *base, *large;
    double run_started;
    unsigned n;
    int ret;

    test_now_iso8601(started, sizeof started);
    run_started = test_now_seconds();

    fill_payload((unsigned long long)time(NULL)
                 ^ ((unsigned long long)(size_t)&lat << 16));

    base = ne_concat(i_path, "bench/", NULL);
    large = ne_concat(i_path, "bench-large", NULL);

    /* A collection of its own, so the small files are not mixed in
     * with anything else and cleanup is unambiguous. */
    (void) ne_delete(i_session, base);
    ret = ne_mkcol(i_session, base);
    if (ret) {
        fprintf(stderr, "litmus-cli bench: could not create `%s': %s\n",
                base, ne_get_error(i_session));
        ne_free(base);
        ne_free(large);
        ne_free(payload);
        return 1;
    }

    run_pool("upload-small", "many small files, uploaded in parallel", 1,
             opts->files, opts->size, opts->concurrency, base);
    run_pool("download-small", "the same files, downloaded in parallel", 0,
             opts->files, opts->size, opts->concurrency, base);

    if (opts->large > 0) {
        run_single("upload-large", "one large file, uploaded", 1,
                   opts->large, large);
        run_single("download-large", "the same file, downloaded", 0,
                   opts->large, large);
    }

    probe_latency(opts->pings, &lat);

    cleanup(opts->files, base, large);

    if (test_json)
        report_json(opts, &lat, started, test_now_seconds() - run_started);
    else
        report_text(opts, &lat, started, test_now_seconds() - run_started);

    ret = 0;
    for (n = 0; n < nscenarios; n++) {
        if (scenarios[n].errors) ret = 1;
        if (scenarios[n].message) ne_free(scenarios[n].message);
    }
    if (lat.failed == lat.attempted && lat.attempted > 0) ret = 1;
    if (lat.message) ne_free(lat.message);

    nscenarios = 0;

    ne_free(base);
    ne_free(large);
    ne_free(payload);
    payload = NULL;

    return ret;
}

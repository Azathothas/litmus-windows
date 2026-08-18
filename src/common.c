/* 
   litmus: WebDAV server test suite: common routines
   Copyright (C) 2001-2025, Joe Orton <joe@manyfish.co.uk>
                                                                     
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

#include <sys/stat.h> /* for struct stat */

#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#include <fcntl.h>
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include <ne_uri.h>
#include <ne_auth.h>
#include <ne_ssl.h>
#include <ne_session.h>
#include <ne_locks.h>
#include <ne_string.h>
#include <ne_alloc.h>
#include <ne_207.h>
#include <ne_xml.h>
#include <ne_xmlreq.h>

#include "getopt.h"

#include "common.h"

int i_class2 = 0;

ne_session *i_session, *i_session2;

ne_uri i_origin;
ne_sock_addr *i_address;

int litmus_threads = LITMUS_THREADS_DEFAULT;

/* Connection settings, reported by the benchmark because two runs made
 * with different ones are not comparable.  Zero means "leave neon's
 * default alone". */
int litmus_connect_timeout = 0;
int litmus_read_timeout = 0;
int litmus_persist = 1;

static int use_tls, tls_trust_everything;

const char *i_username = NULL, *i_password;

static char *proxy_hostname = NULL;
static unsigned int proxy_port;
static int system_proxy;

static char *clicert_fn, *clicert_uri;

/* Destination for the --trace wire dump, NULL when not tracing.
 * trace_path records the file it was opened for, so that a run of
 * several suites appends to one dump rather than each suite truncating
 * the one before it. */
static FILE *trace_fp;
static char *trace_path;
static int trace_needs_close;

/* Option codes for the long-only options. */
enum {
    OPT_THREADS = 256,
    OPT_FILES,
    OPT_SIZE,
    OPT_LARGE,
    OPT_CONCURRENCY,
    OPT_PINGS,
    OPT_CONNECT_TIMEOUT,
    OPT_READ_TIMEOUT,
    OPT_NO_KEEPALIVE
};

/* Filled in by litmus_init() from the bench options below; only the
 * bench subcommand reads it. */
struct litmus_bench_options litmus_bench_options = {
    LITMUS_BENCH_FILES, LITMUS_BENCH_SIZE, LITMUS_BENCH_LARGE,
    LITMUS_BENCH_CONCURRENCY, LITMUS_BENCH_PINGS, 0, 0, 1
};

static const struct option longopts[] = {
    { "htdocs", required_argument, NULL, 'd' },
    { "help", no_argument, NULL, 'h' },
    { "quiet", no_argument, NULL, 'q' },
    { "no-colour", no_argument, NULL, 'n' },
    { "colour", no_argument, NULL, 'o' },
    { "proxy", required_argument, NULL, 'p' },
    { "system-proxy", no_argument, NULL, 's' },
    { "client-cert",  required_argument, NULL, 'c' },
    { "client-cert-uri",  required_argument, NULL, 'u' },
    { "insecure", no_argument, NULL, 'i' },
    { "json", no_argument, NULL, 'j' },
    { "verbose", no_argument, NULL, 'v' },
    { "trace", optional_argument, NULL, 't' },
    { "threads", required_argument, NULL, OPT_THREADS },
    { "files", required_argument, NULL, OPT_FILES },
    { "size", required_argument, NULL, OPT_SIZE },
    { "large", required_argument, NULL, OPT_LARGE },
    { "concurrency", required_argument, NULL, OPT_CONCURRENCY },
    { "pings", required_argument, NULL, OPT_PINGS },
    { "connect-timeout", required_argument, NULL, OPT_CONNECT_TIMEOUT },
    { "read-timeout", required_argument, NULL, OPT_READ_TIMEOUT },
    { "no-keepalive", no_argument, NULL, OPT_NO_KEEPALIVE },
    { NULL }
};

#define HELPOPTS                                                        \
" -p, --proxy=URL            use given proxy server URL\n"              \
" -s, --system-proxy         use proxy server configuration from system\n" \
" -c, --client-cert=CERT     use given PKCS#12 client cert\n"           \
" -u, --client-cert-uri=URI  use given client cert URI\n"               \
" -i, --insecure             ignore TLS certificate verification failures\n" \
" -q, --quiet                use abbreviated output\n"                  \
" -n, --no-colour            disable colour in output\n"                 \
" -o, --colour               enable colour in output\n"                  \
" -j, --json                 write results to stdout as one JSON object\n" \
" -v, --verbose              write the protocol trace to stderr\n"        \
" -t, --trace[=FILE]         dump every request and response to FILE\n"   \
"                            (default stderr; use - for stdout)\n"       \
"     --threads=N            number of worker threads (lockbomb only)\n"  \
"     --connect-timeout=SEC  connection timeout\n"                        \
"     --read-timeout=SEC     response timeout\n"                          \
"     --no-keepalive         one connection per request\n"                \
"\n"                                                                      \
"bench only:\n"                                                           \
"     --files=N              small files to transfer\n"                   \
"     --size=BYTES           size of each small file\n"                   \
"     --large=BYTES          size of the large file, 0 to skip it\n"      \
"     --concurrency=N        transfers in flight at once\n"               \
"     --pings=N              TCP connect probes, 0 to skip them\n"

static void usage(FILE *output)
{
    fprintf(output, 
	    "\rUsage: %s [OPTIONS] URL [username password]\n"
	    "Options are:\n" HELPOPTS, test_argv[0]);
}

static int test_connect(void)
{
    const ne_inet_addr *ia;
    ne_socket *sock = ne_sock_create();
    unsigned int port = proxy_hostname ? proxy_port : i_port;
    int success = 0;

    if (!sock) {
        t_context("could not create socket");
        return FAILHARD;
    }

    for (ia = ne_addr_first(i_address); ia && !success; 
	 ia = ne_addr_next(i_address))
	success = ne_sock_connect(sock, ia, port) == 0;
    
    if (!success) {
	t_context("connection refused by `%s' port %d: %s",
		  i_hostname, port, ne_sock_error(sock));
	return FAILHARD;
    }

    ne_sock_close(sock);
    return OK;
}

static int test_resolve(const char *hostname, const char *name)
{
    i_address = ne_addr_resolve(hostname, 0);
    if (ne_addr_result(i_address)) {
       char buf[256];
       t_context("%s hostname `%s' lookup failed: %s", name, hostname,
                 ne_addr_error(i_address, buf, sizeof buf));
       return FAILHARD;
    }
    return OK;
}

int direct_connect(void)
{
    if (proxy_hostname)
        CALL(test_resolve(proxy_hostname, "proxy server"));
    else
        CALL(test_resolve(i_hostname, "server"));

    return test_connect();
}

/* Puts getopt back to its initial state, so that the next suite in a
 * multi-suite run parses its own argv from the start. */
static void reset_getopt(void)
{
#if defined(optreset) || defined(__FreeBSD__) || defined(__NetBSD__) \
    || defined(__OpenBSD__) || defined(__APPLE__)
    optreset = 1;
    optind = 1;
#elif defined(__GLIBC__)
    /* glibc reinitialises fully, including the GNU permutation state,
     * only when optind is set to zero. */
    optind = 0;
#else
    optind = 1;
#endif
}

void litmus_reset(void)
{
    reset_getopt();

    ne_uri_free(&i_origin);
    memset(&i_origin, 0, sizeof i_origin);

    if (i_address) {
        ne_addr_destroy(i_address);
        i_address = NULL;
    }

    i_session = i_session2 = NULL;
    i_class2 = 0;
    use_tls = tls_trust_everything = 0;
    i_username = i_password = NULL;
    proxy_hostname = NULL;
    proxy_port = 0;
    system_proxy = 0;
    clicert_fn = clicert_uri = NULL;
    litmus_threads = LITMUS_THREADS_DEFAULT;
    litmus_connect_timeout = litmus_read_timeout = 0;
    litmus_persist = 1;

    /* trace_fp deliberately survives: a run of several suites writes
     * one dump, closed by litmus_cleanup(). */
}

void litmus_cleanup(void)
{
    if (trace_fp) {
        fflush(trace_fp);
        if (trace_needs_close) fclose(trace_fp);
        trace_fp = NULL;
        test_trace_fp = NULL;
        trace_needs_close = 0;
    }
    if (trace_path) {
        ne_free(trace_path);
        trace_path = NULL;
    }
}

/* Opens the --trace destination, or reuses the one already open for
 * the same destination.  Returns non-zero on failure. */
static int open_trace(const char *fname)
{
    if (fname == NULL) fname = "";      /* stderr */

    if (trace_fp != NULL && trace_path != NULL
        && strcmp(trace_path, fname) == 0) {
        test_trace_fp = trace_fp;
        return 0;
    }

    litmus_cleanup();

    if (*fname == '\0') {
        /* No argument: stdout is left free for results. */
        trace_fp = stderr;
    }
    else if (strcmp(fname, "-") == 0) {
        trace_fp = stdout;
    }
    else {
        trace_fp = fopen(fname, "w");
        if (trace_fp == NULL) {
            fprintf(stderr, "%s: could not open trace file `%s': %s\n",
                    test_suite, fname, strerror(errno));
            return 1;
        }
        trace_needs_close = 1;
    }

    trace_path = ne_strdup(fname);
    test_trace_fp = trace_fp;

    return 0;
}

/* Parses the current option's argument as a number in [lo, hi],
 * reporting a usage error if it is not one.  A macro because every use
 * bails out of litmus_init() the same way. */
#define CALL_NUMBER(name, lo, hi) do {                                  \
    char *_end;                                                         \
                                                                        \
    number = strtoll(optarg, &_end, 10);                                \
    if (*optarg == '\0' || *_end != '\0'                                \
        || number < (long long)(lo) || number > (long long)(hi)) {      \
        fprintf(stderr, "%s: %s must be between %lld and %lld\n",       \
                test_argv[0], name, (long long)(lo), (long long)(hi));  \
        return TEST_INIT_USAGE;                                         \
    }                                                                   \
} while (0)

int litmus_init(int argc, const char *const *argv, int *use_colour, int *quiet)
{
    ne_uri proxy = {0}, *server = &i_origin;
    int optc, n;
    long long number;
    char *proxy_url = NULL;

    while ((optc = getopt_long(argc, test_argv,
			       "c:d:hijnop:qst::u:v", longopts, NULL)) != -1) {
	switch (optc) {
        case 'c':
            clicert_fn = optarg;
            break;
        case 'j':
            test_json = 1;
            break;
        case 'v':
            test_verbose = 1;
            break;
        case 't':
            if (open_trace(optarg)) return 1;
            break;
        case 'u':
            clicert_uri = optarg;
            break;
        case OPT_THREADS:
            CALL_NUMBER("--threads", 1, LITMUS_THREADS_MAX);
            litmus_threads = (int)number;
            break;
        case OPT_FILES:
            CALL_NUMBER("--files", 0, LITMUS_BENCH_MAX_FILES);
            litmus_bench_options.files = (unsigned)number;
            break;
        case OPT_SIZE:
            CALL_NUMBER("--size", 0, LITMUS_BENCH_MAX_BYTES);
            litmus_bench_options.size = (ne_off_t)number;
            break;
        case OPT_LARGE:
            CALL_NUMBER("--large", 0, LITMUS_BENCH_MAX_BYTES);
            litmus_bench_options.large = (ne_off_t)number;
            break;
        case OPT_CONCURRENCY:
            CALL_NUMBER("--concurrency", 1, LITMUS_BENCH_MAX_CONCURRENCY);
            litmus_bench_options.concurrency = (unsigned)number;
            break;
        case OPT_PINGS:
            CALL_NUMBER("--pings", 0, LITMUS_BENCH_MAX_PINGS);
            litmus_bench_options.pings = (unsigned)number;
            break;
        case OPT_CONNECT_TIMEOUT:
            CALL_NUMBER("--connect-timeout", 0, 86400);
            litmus_connect_timeout = (int)number;
            break;
        case OPT_READ_TIMEOUT:
            CALL_NUMBER("--read-timeout", 0, 86400);
            litmus_read_timeout = (int)number;
            break;
        case OPT_NO_KEEPALIVE:
            litmus_persist = 0;
            break;
	case 'd':
            t_warning("the 'htdocs' argument is now ignored");
	    break;
	case 'h':
	    usage(stdout);
	    return TEST_INIT_DONE;
        case 'i':
            tls_trust_everything = 1;
            break;
        case 'n':
            *use_colour = 0;
            break;
        case 'o':
            *use_colour = 1;
            break;
	case 'p':
	    proxy_url = optarg;
	    break;
        case 'q':
            *quiet = 1;
            break;
	case 's':
	    system_proxy = 1;
	    break;
	default:
	    usage(stderr);
	    return TEST_INIT_USAGE;
	}
    }

    n = argc - optind;

    if (n == 0 || n > 3 || n == 2) {
	usage(stderr);
	return TEST_INIT_USAGE;
    }

    /* argv lives for the lifetime of the process, so the JSON output
     * can reference the target URL directly. */
    test_target = argv[optind];

    NE_DEBUG(NE_DBG_HTTP, "litmus: Parsing URI %s...\n", argv[optind]);

    if (ne_uri_parse(argv[optind], server) || !server->host
        || !server->path || !server->scheme) {
	t_context("couldn't parse server URL `%s'",
		  test_argv[optind]);
	return FAILHARD;
    }       

    if (proxy_url) {
	if (ne_uri_parse(proxy_url, &proxy) || !proxy.host) {
	    t_context("couldn't parse proxy URL `%s'", proxy_url);
	    return FAILHARD;
	}
	if (proxy.scheme && strcmp(proxy.scheme, "http") != 0) {
	    t_context("cannot use scheme `%s' for proxy", proxy.scheme);
	    return FAILHARD;
	}
	if (proxy.port > 0) {
	    proxy_port = proxy.port;
	} else {
	    proxy_port = 8080;
	}
	proxy_hostname = proxy.host;
    }

#ifdef NE_FEATURE_LIBPXY
    if (system_proxy && !ne_has_support(NE_FEATURE_LIBPXY)) {
        t_context("No system proxy support in neon");
        return FAILHARD;
    }
#endif

    use_tls = strcmp(server->scheme, "https") == 0;
    if (use_tls && !ne_has_support(NE_FEATURE_SSL)) {
        t_context("No SSL support, reconfigure using --with-ssl");
        return FAILHARD;
    }

    if (server->port == 0) {
        server->port = use_tls ? 443 : 80;
    }
    if (!ne_path_has_trailing_slash(server->path)) {
        char *newp = ne_concat(server->path, "/", NULL);
        ne_free(server->path);
        server->path = newp;
    }

    if (n > 2) {
	i_username = test_argv[optind+1];
	i_password = test_argv[optind+2];
	
	if (strlen(i_username) >= NE_ABUFSIZ) {
	    t_context("username must be <%d chars", NE_ABUFSIZ);
	    return FAILHARD;
	}

	if (strlen(i_password) >= NE_ABUFSIZ) {
	    t_context("password must be <%d chars", NE_ABUFSIZ);
	    return FAILHARD;
	}
    }
    
    return OK;
}

static int auth(void *ud, const char *realm, int attempt,
		char *username, char *password)
{
    strcpy(username, i_username);
    strcpy(password, i_password);
    return attempt;
}

/* The name of the test issuing the current request.  The benchmark
 * runs outside the harness, so there is no test array then. */
static const char *current_test(void)
{
    return tests ? tests[test_num].name : test_suite;
}

static void i_pre_send(ne_request *req, void *userdata, ne_buffer *hdr)
{
    const char *name = userdata;

    ne_buffer_snprintf(hdr, BUFSIZ, "%s: %s: %d (%s)\r\n",
                       name, test_suite, test_num, current_test());
}

/* Note what the running test asked for and what it was answered, so
 * that the harness can classify a failure in the JSON output without
 * the consumer having to parse the prose in "context".  Both sessions
 * are hooked; whichever made the most recent request wins, which is
 * the one a failure is about. */
static void i_create_request(ne_request *req, void *userdata,
                             const char *method, const char *target)
{
    t_request_begin(method, target);
}

static void i_post_headers(ne_request *req, void *userdata,
                           const ne_status *status)
{
    t_request_status(status->code);
}

/* Writes 'text' with every line prefixed by 'prefix', so that a request
 * or response block stays visually distinct in the trace. */
static void trace_block(const char *prefix, const char *text)
{
    const char *p = text;

    while (p && *p) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);

        /* Requests carry CRLF line endings; drop the CR so the trace
         * does not end up with stray carriage returns in it. */
        if (len > 0 && p[len - 1] == '\r') len--;

        fprintf(trace_fp, "%s %.*s\n", prefix, (int)len, p);

        if (!eol) break;
        p = eol + 1;
    }
}

/* Dumps the outgoing request line and headers. */
static void trace_pre_send(ne_request *req, void *userdata, ne_buffer *hdr)
{
    const char *label = userdata;

    fprintf(trace_fp, "\n--- %s %d (%s)%s ---\n", test_suite, test_num,
            current_test(), label);
    trace_block(">", hdr->data);
    fflush(trace_fp);
}

/* Dumps the response status line and headers. */
static void trace_post_headers(ne_request *req, void *userdata,
                               const ne_status *status)
{
    void *cursor = NULL;
    const char *name, *value;

    fprintf(trace_fp, "< HTTP/%d.%d %d %s\n", status->major_version,
            status->minor_version, status->code,
            status->reason_phrase ? status->reason_phrase : "");

    while ((cursor = ne_response_header_iterate(req, cursor,
                                                &name, &value)) != NULL) {
        fprintf(trace_fp, "< %s: %s\n", name, value);
    }

    fputs("<\n", trace_fp);
    fflush(trace_fp);
}

/* Attaches the tracing hooks to 'sess'.  'label' distinguishes the
 * second session in the output. */
static void trace_session(ne_session *sess, const char *label)
{
    if (!trace_fp) return;

    ne_hook_pre_send(sess, trace_pre_send, (void *)label);
    ne_hook_post_headers(sess, trace_post_headers, NULL);
}

/* Allow all certificates. */
static int ignore_verify(void *ud, int fs, const ne_ssl_certificate *cert)
{
    return 0;
}

static int init_ssl(ne_session *sess)
{
    int got_clicert = clicert_fn || clicert_uri;
    ne_ssl_client_cert *cc = NULL;

    ne_ssl_trust_default_ca(sess);

    if (tls_trust_everything) ne_ssl_set_verify(sess, ignore_verify, NULL);

    if (!got_clicert) return OK;

    if (clicert_fn)
        cc = ne_ssl_clicert_read(clicert_fn);
    else
#if NE_MINIMUM_VERSION(0, 35)
        cc = ne_ssl_clicert_fromuri(clicert_uri, 0);
#else
        t_warning("No client certificate URI support");
#endif

    if (!cc) {
        t_context("Can not read the client certificate '%s'",
                  clicert_fn ? clicert_fn : clicert_uri);
        return FAILHARD;
    }

    if (ne_ssl_clicert_encrypted(cc)) {
        t_context("Can not use encrypted client certificate '%s'",
                  clicert_fn ? clicert_fn : clicert_uri);
        return FAILHARD;
    }

    ne_ssl_set_clicert(sess, cc);
    ne_ssl_clicert_free(cc);

    return OK;
}

static int init_session(ne_session *sess)
{
    if (proxy_hostname) {
	ne_session_proxy(sess, proxy_hostname, proxy_port);
    }
    else if (system_proxy) {
        ne_session_system_proxy(sess, 0);
    }

    ne_set_useragent(sess, "litmus/" LITMUS_VERSION);

    if (i_username) {
	ne_set_server_auth(sess, auth, NULL);
    }

    if (use_tls) {
        CALL(init_ssl(sess));
    }
    
    return OK;
}    

static int make_space(void)
{
    char *space = ne_concat(i_path, "litmus/", NULL);
    
    ne_delete(i_session, space);

    if (ne_mkcol(i_session, space)) {
	t_context("Could not create new collection `%s' for tests: %s\n"
		  "Server must allow `MKCOL %s' for tests to proceed", 
		  space, ne_get_error(i_session), space);
	return FAILHARD;
    }
    
    free(i_path);
    i_path = space;    

    return OK;
}

ne_session *litmus_new_session(void)
{
    const ne_uri *u = &i_origin;
    ne_session *sess;

    if (u->scheme == NULL || u->host == NULL) return NULL;

    sess = ne_session_create(u->scheme, u->host, u->port);
    if (sess == NULL) return NULL;

    if (init_session(sess) != OK) {
        ne_session_destroy(sess);
        return NULL;
    }

    if (litmus_connect_timeout > 0)
        ne_set_connect_timeout(sess, litmus_connect_timeout);
    if (litmus_read_timeout > 0)
        ne_set_read_timeout(sess, litmus_read_timeout);
    if (!litmus_persist)
        ne_set_session_flag(sess, NE_SESSFLAG_PERSIST, 0);

    return sess;
}

const char *litmus_target_host(void)
{
    return proxy_hostname ? proxy_hostname : i_hostname;
}

unsigned int litmus_target_port(void)
{
    return proxy_hostname ? proxy_port : i_port;
}

void litmus_sleep_ms(unsigned ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec ts;

    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

int begin(void)
{
    const ne_uri *u = &i_origin;

    i_session = ne_session_create(u->scheme, u->host, u->port);
    i_session2 = ne_session_create(u->scheme, u->host, u->port);

    CALL(init_session(i_session));
    CALL(init_session(i_session2));

    /* Send header with every request associating the request with the
     * test number and session. */
    ne_hook_pre_send(i_session, i_pre_send, "X-Litmus");
    ne_hook_pre_send(i_session2, i_pre_send, "X-Litmus-Second");

    /* Feed the harness the method, path and status behind a failure. */
    ne_hook_create_request(i_session, i_create_request, NULL);
    ne_hook_create_request(i_session2, i_create_request, NULL);
    ne_hook_post_headers(i_session, i_post_headers, NULL);
    ne_hook_post_headers(i_session2, i_post_headers, NULL);

    /* Registered after i_pre_send so that the X-Litmus header is
     * already in the buffer by the time the request is dumped. */
    trace_session(i_session, "");
    trace_session(i_session2, " [second session]");

    CALL(make_space());

    return OK;
}

int finish(void)
{
    ne_session_destroy(i_session);
    ne_session_destroy(i_session2);
    i_session = i_session2 = NULL;

    /* Flushed but not closed: a run of several suites shares one trace
     * destination, which litmus_cleanup() closes at the end. */
    if (trace_fp) fflush(trace_fp);

    return OK;
}

/* Returns the directory to use for temporary files.  Windows has no
 * /tmp, so the environment must be consulted there. */
static const char *tmp_dir(void)
{
    static const char * const vars[] = { "TMPDIR", "TMP", "TEMP" };
    unsigned n;

    for (n = 0; n < sizeof(vars) / sizeof(vars[0]); n++) {
        const char *value = getenv(vars[n]);

        if (value && *value) return value;
    }

#ifdef _WIN32
    return ".";
#else
    return "/tmp";
#endif
}

int litmus_tmpfile(char **fname)
{
    char *tmpl = ne_concat(tmp_dir(), "/litmus2-XXXXXX", NULL);
    int fd = mkstemp(tmpl);

    if (fd < 0) {
        ne_free(tmpl);
        return -1;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    /* Prevent CRLF translation from corrupting file contents. */
    setmode(fd, O_BINARY);
#endif

    *fname = tmpl;
    return fd;
}

int put_buffer(ne_session *sess, const char *path, const char *content)
{
#if NE_VERSION_MAJOR > 0 || NE_VERSION_MINOR > 32
    return ne_putbuf(sess, path, content, strlen(content));
#else
    ne_request *req;
    int ret;

    req = ne_request_create(sess, "PUT", path);
    ne_lock_using_resource(req, path, 0);
    ne_lock_using_parent(req, path);
    ne_set_request_body_buffer(req, content, strlen(content));
    ret = ne_request_dispatch(req);

    if (ret == NE_OK && ne_get_status(req)->klass != 2)
	ret = NE_ERROR;

    ne_request_destroy(req);

    return ret;
#endif
}

int dummy_put(ne_session *sess, const char *path)
{
    return put_buffer(sess, path, "zero");
}

/* State for litmus_proppatch(): the status the server reported for the
 * resource, whether at the response level or inside a multistatus. */
struct proppatch_ctx {
    int response;               /* status of the HTTP response */
    int embedded;               /* first non-2xx status inside a 207 */
    char *reason;               /* its reason phrase, may be NULL */
};

/* Records the first non-2xx status found in the multistatus body.  A
 * 424 means "this would have worked but something else failed", so it
 * never carries the reason and is skipped, as neon does. */
static void note_status(struct proppatch_ctx *ctx, const ne_status *status)
{
    if (status && status->klass != 2 && status->code != 424
        && ctx->embedded == 0) {
        ctx->embedded = status->code;
        if (status->reason_phrase)
            ctx->reason = ne_strdup(status->reason_phrase);
    }
}

static void *pp_start_response(void *userdata, const ne_uri *uri)
{
    return NULL;
}

static void pp_end_response(void *userdata, void *response,
                            const ne_status *status, const char *description)
{
    note_status(userdata, status);
}

static void pp_end_propstat(void *userdata, void *propstat,
                            const ne_status *status, const char *description)
{
    note_status(userdata, status);
}

int litmus_proppatch(ne_session *sess, const char *path,
                     const ne_proppatch_operation *ops, int *status)
{
    ne_request *req = ne_request_create(sess, "PROPPATCH", path);
    ne_buffer *body = ne_buffer_create();
    struct proppatch_ctx ctx = {0};
    ne_xml_parser *parser = ne_xml_create();
    ne_207_parser *p207;
    ne_uri base = {0};
    const ne_status *st;
    int n, ret;

    /* The request body is built exactly as ne_proppatch() builds it,
     * so that this differs from that function only in what it reports
     * back. */
    ne_buffer_czappend(body, "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
                       "<D:propertyupdate xmlns:D=\"DAV:\">");

    for (n = 0; ops[n].name != NULL; n++) {
        const char *elm = (ops[n].type == ne_propset) ? "set" : "remove";

        ne_buffer_concat(body, "<D:", elm, "><D:prop>",
                         "<", ops[n].name->name, NULL);

        if (ops[n].name->nspace)
            ne_buffer_concat(body, " xmlns=\"", ops[n].name->nspace, "\"",
                             NULL);

        if (ops[n].type == ne_propset)
            ne_buffer_concat(body, ">", ops[n].value, NULL);
        else
            ne_buffer_append(body, ">", 1);

        ne_buffer_concat(body, "</", ops[n].name->name, "></D:prop></D:", elm,
                         ">\n", NULL);
    }

    ne_buffer_czappend(body, "</D:propertyupdate>\n");

    ne_set_request_body_buffer(req, body->data, ne_buffer_size(body));
    ne_add_request_header(req, "Content-Type", NE_XML_MEDIA_TYPE);

    ne_lock_using_resource(req, path, NE_DEPTH_ZERO);

    ne_fill_server_uri(sess, &base);
    base.path = ne_strdup("/");
    p207 = ne_207_create(parser, &base, &ctx);
    ne_uri_free(&base);

    ne_207_set_response_handlers(p207, pp_start_response, pp_end_response);
    ne_207_set_propstat_handlers(p207, NULL, pp_end_propstat);

    ret = ne_xml_dispatchif_request(req, parser, ne_accept_207, NULL);

    st = ne_get_status(req);
    ctx.response = st->code;

    if (ret == NE_OK) {
        if (ctx.embedded) {
            /* Report the embedded status the way a status line reads,
             * so that GETSTATUS and ne_get_error() see what the server
             * meant rather than the 207 wrapper around it. */
            ne_set_error(sess, "%d %s", ctx.embedded,
                         ctx.reason ? ctx.reason : "");
            ret = NE_ERROR;
        }
        else if (st->klass != 2) {
            ret = NE_ERROR;
        }
    }

    if (status)
        *status = ctx.embedded ? ctx.embedded : ctx.response;

    if (ctx.reason) ne_free(ctx.reason);
    ne_207_destroy(p207);
    ne_xml_destroy(parser);
    ne_buffer_destroy(body);
    ne_request_destroy(req);

    return ret;
}

const char litmus_foo_content[] =
    "This\nis\na\ntest\nfile\ncalled\nfoo\n";

int upload_foo(const char *path)
{
    char *uri = ne_concat(i_path, path, NULL);
    int ret;

    ret = put_buffer(i_session, uri, litmus_foo_content);

    ne_free(uri);
    return ret;
}

int litmus_fetch(ne_session *sess, const char *path, char **body, size_t *len)
{
    ne_request *req = ne_request_create(sess, "GET", path);
    ne_buffer *buf = ne_buffer_create();
    char block[BUFSIZ];
    ssize_t bytes;
    int ret;

    ret = ne_begin_request(req);
    if (ret == NE_OK) {
        if (ne_get_status(req)->klass != 2) {
            /* Read the body out so that the connection stays usable,
             * then report the status as an error. */
            ne_discard_response(req);
            ne_end_request(req);
            ret = NE_ERROR;
        }
        else {
            while ((bytes = ne_read_response_block(req, block,
                                                   sizeof block)) > 0)
                ne_buffer_append(buf, block, (size_t)bytes);

            if (bytes < 0)
                ret = NE_ERROR;
            else
                ret = ne_end_request(req);
        }
    }

    if (ret == NE_OK) {
        if (len) *len = ne_buffer_size(buf);
        *body = ne_buffer_finish(buf);
    }
    else {
        *body = NULL;
        if (len) *len = 0;
        ne_buffer_destroy(buf);
    }

    ne_request_destroy(req);

    return ret;
}

int litmus_compare(ne_session *sess, const char *path, const char *expected)
{
    char *body = NULL;
    size_t len = 0, want = strlen(expected);
    int ret;

    ret = litmus_fetch(sess, path, &body, &len);
    if (ret != NE_OK) return ret;

    if (len != want || memcmp(body, expected, want) != 0)
        ret = NE_ERROR;

    ne_free(body);

    return ret;
}

int options(void)
{
    ne_server_capabilities caps = {0};
    
    ONV(ne_options(i_session, i_path, &caps),
	("OPTIONS on base collection `%s': %s", i_path, 
	 ne_get_error(i_session)));

    ONN("server does not claim WebDAV compliance", caps.dav_class1 == 0);
    if (caps.dav_class2 == 0) {
	t_warning("server does not claim Class 2 compliance");
    }
    i_class2 = caps.dav_class2;

    return OK;
}

char *get_etag(const char *path)
{
    ne_request *req = ne_request_create(i_session, "HEAD", path);
    char *etag = NULL;

    if (ne_request_dispatch(req) == NE_OK && ne_get_status(req)->code == 200) {
        const char *value = ne_get_response_header(req, "Etag");
        if (value) etag = ne_strdup(value);
    }

    ne_request_destroy(req);
    return etag;
}

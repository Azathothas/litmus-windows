/* 
   WebDAV locking stress test
   Copyright (C) 2024-6, Red Hat, Inc.

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
#include <sys/stat.h>
#include <unistd.h>

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#if defined(HAVE_PTHREADS)
#include <pthread.h>
#endif

#include <ne_session.h>
#include <ne_basic.h>
#include <ne_uri.h>
#include <ne_locks.h>

#include "common.h"

#define ITERS 20000


struct thrarg {
#if defined(HAVE_PTHREADS)
    pthread_t thd;
#endif
    ne_uri uri;
};

/* Returns NULL on success, or a message the caller must ne_free().
 * Every exit runs through the bottom of the function so that the
 * session is always destroyed. */
static void *threadfn(void *varg)
{
    struct thrarg *arg = varg;
    ne_session *sess;
    unsigned int iter;
    char *tmp, *err = NULL;
    int fd = litmus_tmpfile(&tmp);

    if (fd < 0) return ne_strdup("could not create temporary file");

    sess = ne_session_create(i_origin.scheme, i_origin.host, i_origin.port);

    if (ne_put(sess, arg->uri.path, fd) != NE_OK)
        err = ne_concat("PUT: ", ne_get_error(sess), NULL);

    /* The temporary file is only needed for the PUT above.  Clean it up
     * whether or not that succeeded, otherwise a run against a server
     * that rejects the PUT leaves one stray file behind per thread. */
    close(fd);
    unlink(tmp);
    ne_free(tmp);

    for (iter = 0; err == NULL && iter < ITERS; iter++) {
        struct ne_lock *lock = ne_lock_create();

        memcpy(&lock->uri, &arg->uri, sizeof lock->uri);

        if (ne_lock(sess, lock) != NE_OK)
            err = ne_concat("LOCK failure: ", ne_get_error(sess), NULL);
        else if (ne_unlock(sess, lock) != NE_OK)
            err = ne_concat("UNLOCK failure: ", ne_get_error(sess), NULL);

        memset(&lock->uri, 0, sizeof lock->uri);
        ne_lock_destroy(lock);
    }

    ne_session_destroy(sess);

    return err;
}

/* One worker in this process, driving one resource. */
static int lockbomb_single(void)
{
    struct thrarg arg;
    char *retval;

    arg.uri = i_origin;
    arg.uri.path = ne_concat(i_origin.path, "lb-lock-single", NULL);

    retval = threadfn(&arg);
    ne_free(arg.uri.path);

    if (retval) {
        t_context("iteration failed: %s", retval);
        ne_free(retval);
        return FAIL;
    }

    return OK;
}

/* litmus_threads workers, each driving its own resource. */
static int lockbomb_threaded(void)
{
#if defined(HAVE_PTHREADS)
    struct thrarg *args;
    unsigned nthreads = (unsigned)litmus_threads, n, started;
    int ret, result = OK;

    args = ne_calloc(nthreads * sizeof(*args));

    for (started = 0; started < nthreads; started++) {
        char *path = ne_malloc(256);

        ne_snprintf(path, 256, "%s/lb-lock-%04u", i_origin.path, started);

        memcpy(&args[started].uri, &i_origin, sizeof i_origin);
        args[started].uri.path = path;

        ret = pthread_create(&args[started].thd, NULL, threadfn,
                             &args[started]);
        if (ret) {
            /* Leave the threads already running to be joined below,
             * rather than returning while they still write to args. */
            t_context("pthread_create failed: %s", strerror(ret));
            ne_free(path);
            result = FAIL;
            break;
        }
    }

    NE_DEBUG(NE_DBG_HTTP, "lockbomb: spawned %u threads, now waiting...\n",
             started);

    for (n = 0; n < started; n++) {
        char *retval = NULL;

        ret = pthread_join(args[n].thd, (void **)&retval);
        if (ret) {
            if (result == OK) {
                t_context("pthread_join failed: %s", strerror(ret));
                result = FAIL;
            }
        }
        else if (retval) {
            if (result == OK) {
                t_context("thread failed: %s", retval);
                result = FAIL;
            }
            ne_free(retval);
        }
        ne_free(args[n].uri.path);
    }

    ne_free(args);

    return result;
#else
    t_context("no pthreads support in this build; use --threads=1");
    return SKIP;
#endif
}

static int lockbomb(void)
{
    return litmus_threads > 1 ? lockbomb_threaded() : lockbomb_single();
}

ne_test lockbomb_tests[] = {
    INIT_TESTS,
    T_NAMED(lockbomb, "lockbomb_threaded"),
    FINISH_TESTS
};

ne_test lockbomb_single_tests[] = {
    INIT_TESTS,
    T_NAMED(lockbomb, "lockbomb_single"),
    FINISH_TESTS
};

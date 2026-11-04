/* Extracted from anet.c to work properly with Hiredis error reporting.
 *
 * Copyright (c) 2006-2011, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2010-2011, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "fmacros.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <poll.h>
#include <limits.h>

#include "net.h"
#include "sds.h"

/* Defined in hiredis.c */
void __redisSetError(redisContext *c, int type, const char *str);

static void redisContextCloseFd(redisContext *c) {
    if (c && c->fd >= 0) {
        close(c->fd);
        c->fd = -1;
    }
}

static void __redisSetErrorFromErrno(redisContext *c, int type, const char *prefix) {
    char buf[128] = { 0 };
    size_t len = 0;

    if (prefix != NULL)
        len = snprintf(buf,sizeof(buf),"%s: ",prefix);
    strerror_r(errno,buf+len,sizeof(buf)-len);
    __redisSetError(c,type,buf);
}

/* Only the Unix-domain path reaches this; the TCP path creates its socket from
 * an address the resolver returned, in whatever family that address is in. The
 * SO_REUSEADDR this function used to set for one hardcoded family went with
 * it -- the option means nothing on a Unix-domain socket, which is the only
 * kind of socket that is still created from a family named in the source. */
static int redisCreateSocket(redisContext *c, int type) {
    int s;
    if ((s = socket(type, SOCK_STREAM, 0)) == -1) {
        __redisSetErrorFromErrno(c,REDIS_ERR_IO,NULL);
        return REDIS_ERR;
    }
    c->fd = s;
    return REDIS_OK;
}

static int redisSetBlocking(redisContext *c, int blocking) {
    int flags;

    /* Set the socket nonblocking.
     * Note that fcntl(2) for F_GETFL and F_SETFL can't be
     * interrupted by a signal. */
    if ((flags = fcntl(c->fd, F_GETFL)) == -1) {
        __redisSetErrorFromErrno(c,REDIS_ERR_IO,"fcntl(F_GETFL)");
        redisContextCloseFd(c);
        return REDIS_ERR;
    }

    if (blocking)
        flags &= ~O_NONBLOCK;
    else
        flags |= O_NONBLOCK;

    if (fcntl(c->fd, F_SETFL, flags) == -1) {
        __redisSetErrorFromErrno(c,REDIS_ERR_IO,"fcntl(F_SETFL)");
        redisContextCloseFd(c);
        return REDIS_ERR;
    }
    return REDIS_OK;
}

int redisKeepAlive(redisContext *c, int interval) {
    int val = 1;
    int fd = c->fd;

    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val)) == -1){
        __redisSetError(c,REDIS_ERR_OTHER,strerror(errno));
        return REDIS_ERR;
    }

    val = interval;

#ifdef _OSX
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPALIVE, &val, sizeof(val)) < 0) {
        __redisSetError(c,REDIS_ERR_OTHER,strerror(errno));
        return REDIS_ERR;
    }
#else
#ifndef __sun
    val = interval;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &val, sizeof(val)) < 0) {
        __redisSetError(c,REDIS_ERR_OTHER,strerror(errno));
        return REDIS_ERR;
    }

    val = interval/3;
    if (val == 0) val = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &val, sizeof(val)) < 0) {
        __redisSetError(c,REDIS_ERR_OTHER,strerror(errno));
        return REDIS_ERR;
    }

    val = 3;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &val, sizeof(val)) < 0) {
        __redisSetError(c,REDIS_ERR_OTHER,strerror(errno));
        return REDIS_ERR;
    }
#endif
#endif

    return REDIS_OK;
}

static int redisSetTcpNoDelay(redisContext *c) {
    int yes = 1;
    if (setsockopt(c->fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) == -1) {
        __redisSetErrorFromErrno(c,REDIS_ERR_IO,"setsockopt(TCP_NODELAY)");
        redisContextCloseFd(c);
        return REDIS_ERR;
    }
    return REDIS_OK;
}

#define __MAX_MSEC (((LONG_MAX) - 999) / 1000)

static int redisContextWaitReady(redisContext *c, const struct timeval *timeout) {
    struct pollfd   wfd[1];
    long msec;

    msec          = -1;
    wfd[0].fd     = c->fd;
    wfd[0].events = POLLOUT;

    /* Only use timeout when not NULL. */
    if (timeout != NULL) {
        if (timeout->tv_usec > 1000000 || timeout->tv_sec > __MAX_MSEC) {
            __redisSetErrorFromErrno(c, REDIS_ERR_IO, NULL);
            redisContextCloseFd(c);
            return REDIS_ERR;
        }

        msec = (timeout->tv_sec * 1000) + ((timeout->tv_usec + 999) / 1000);

        if (msec < 0 || msec > INT_MAX) {
            msec = INT_MAX;
        }
    }

    if (errno == EINPROGRESS) {
        int res;

        if ((res = poll(wfd, 1, msec)) == -1) {
            __redisSetErrorFromErrno(c, REDIS_ERR_IO, "poll(2)");
            redisContextCloseFd(c);
            return REDIS_ERR;
        } else if (res == 0) {
            errno = ETIMEDOUT;
            __redisSetErrorFromErrno(c,REDIS_ERR_IO,NULL);
            redisContextCloseFd(c);
            return REDIS_ERR;
        }

        if (redisCheckSocketError(c) != REDIS_OK)
            return REDIS_ERR;

        return REDIS_OK;
    }

    __redisSetErrorFromErrno(c,REDIS_ERR_IO,NULL);
    redisContextCloseFd(c);
    return REDIS_ERR;
}

int redisCheckSocketError(redisContext *c) {
    int err = 0;
    socklen_t errlen = sizeof(err);

    if (getsockopt(c->fd, SOL_SOCKET, SO_ERROR, &err, &errlen) == -1) {
        __redisSetErrorFromErrno(c,REDIS_ERR_IO,"getsockopt(SO_ERROR)");
        return REDIS_ERR;
    }

    if (err) {
        errno = err;
        __redisSetErrorFromErrno(c,REDIS_ERR_IO,NULL);
        return REDIS_ERR;
    }

    return REDIS_OK;
}

int redisContextSetTimeout(redisContext *c, const struct timeval tv) {
    if (setsockopt(c->fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv)) == -1) {
        __redisSetErrorFromErrno(c,REDIS_ERR_IO,"setsockopt(SO_RCVTIMEO)");
        return REDIS_ERR;
    }
    if (setsockopt(c->fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv)) == -1) {
        __redisSetErrorFromErrno(c,REDIS_ERR_IO,"setsockopt(SO_SNDTIMEO)");
        return REDIS_ERR;
    }
    return REDIS_OK;
}

/* RFC 8305 section 5: "The RECOMMENDED value for the Connection Attempt Delay
 * is 250 milliseconds." It is how long the next attempt waits, not a timeout on
 * the attempt before it, which keeps running beside it. */
#define REDIS_ATTEMPT_DELAY_MS 250

/* How many of a name's addresses are raced at once. */
#define REDIS_MAX_ATTEMPTS 16

static long long redisMstime(void) {
    struct timeval tv;

    gettimeofday(&tv,NULL);
    return ((long long)tv.tv_sec)*1000 + tv.tv_usec/1000;
}

/* RFC 8305 section 4: keep the resolver's order (it has already sorted the
 * answer per RFC 6724) within each family, and interleave the two families so
 * the first attempt is IPv6 and neither family is left waiting behind a run of
 * the other's addresses.
 *
 * Only one family has to be named to do that, and it is the one the section
 * puts first: an address either is IPv6 or is not, and the addresses that are
 * not keep their own order among themselves. Nothing the resolver returned is
 * dropped, whatever family it turns out to be.
 *
 * Returns how many entries were written, which is every address the answer held
 * up to `max`. */
static int redisInterleaveByFamily(struct addrinfo *servinfo,
                                   struct addrinfo **out, int max) {
    struct addrinfo *v6[REDIS_MAX_ATTEMPTS], *rest[REDIS_MAX_ATTEMPTS], *p;
    int n6 = 0, nr = 0, n = 0, i;

    for (p = servinfo; p != NULL; p = p->ai_next) {
        if (p->ai_family == AF_INET6) {
            if (n6 < max) v6[n6++] = p;
        } else {
            if (nr < max) rest[nr++] = p;
        }
    }
    for (i = 0; n < max && (i < n6 || i < nr); i++) {
        if (i < n6) out[n++] = v6[i];
        if (n < max && i < nr) out[n++] = rest[i];
    }
    return n;
}

/* Start one attempt on `c->fd`, non-blocking, and report whether it connected
 * outright. Returns the descriptor, or -1 with the reason in *why. */
static int redisStartAttempt(redisContext *c, struct addrinfo *p,
                             const char *source_addr, int *connected,
                             int *why) {
    struct addrinfo hints, *bservinfo, *b;
    int s, rv;

    *connected = 0;
    if ((s = socket(p->ai_family,p->ai_socktype,p->ai_protocol)) == -1) {
        *why = errno;
        return -1;
    }
    c->fd = s;
    /* Every attempt is non-blocking for the duration of the race, whatever the
     * caller asked for: a thread inside a blocking connect() cannot start the
     * next attempt. The caller's mode is restored on the winner. */
    if (redisSetBlocking(c,0) != REDIS_OK) {
        *why = errno;
        redisContextCloseFd(c);
        return -1;
    }
    if (source_addr) {
        int bound = 0;
        memset(&hints,0,sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        /* Using getaddrinfo saves us from self-determining IPv4 vs IPv6 */
        if ((rv = getaddrinfo(source_addr, NULL, &hints, &bservinfo)) != 0) {
            char buf[128];
            snprintf(buf,sizeof(buf),"Can't get addr: %s",gai_strerror(rv));
            __redisSetError(c,REDIS_ERR_OTHER,buf);
            *why = EADDRNOTAVAIL;
            redisContextCloseFd(c);
            return -1;
        }
        for (b = bservinfo; b != NULL; b = b->ai_next) {
            if (bind(s,b->ai_addr,b->ai_addrlen) != -1) {
                bound = 1;
                break;
            }
        }
        freeaddrinfo(bservinfo);
        if (!bound) {
            char buf[128];
            snprintf(buf,sizeof(buf),"Can't bind socket: %s",strerror(errno));
            __redisSetError(c,REDIS_ERR_OTHER,buf);
            *why = errno;
            redisContextCloseFd(c);
            return -1;
        }
    }
    if (connect(s,p->ai_addr,p->ai_addrlen) == -1) {
        if (errno != EINPROGRESS) {
            *why = errno;
            redisContextCloseFd(c);
            return -1;
        }
    } else {
        *connected = 1;
    }
    return s;
}

/* Connect by RFC 8305: resolve without naming a family, interleave the
 * resolver's order by family starting with IPv6, start the first attempt at
 * once and each next one a Connection Attempt Delay later -- or immediately, if
 * nothing is in flight because the attempt before it already failed -- and keep
 * the first connection that completes, closing the rest.
 *
 * What this buys is the case the sequential walk it replaces cannot help with:
 * an address that answers nothing at all. A walk waits out the operating
 * system's own connect timeout on it before it reaches the next address, so one
 * blackholed address of one family is the whole connect. Here it holds up only
 * its own attempt.
 *
 * `timeout`, when the caller gave one, bounds the race rather than each attempt
 * in it: with attempts running beside each other there is no separate deadline
 * to give any one of them. */
static int _redisContextConnectTcp(redisContext *c, const char *addr, int port,
                                   const struct timeval *timeout,
                                   const char *source_addr) {
    int s = -1, rv, n, i, started = 0, nopen = 0, why = 0, last = -1;
    long long next_at = 0, grace_at = 0, end_at = -1;
    char _port[6];  /* strlen("65535"); */
    struct addrinfo hints, *servinfo;
    struct addrinfo *order[REDIS_MAX_ATTEMPTS];
    struct pollfd pfd[REDIS_MAX_ATTEMPTS];
    int open_fd[REDIS_MAX_ATTEMPTS], open_ix[REDIS_MAX_ATTEMPTS];
    int blocking = (c->flags & REDIS_BLOCK);

    snprintf(_port, 6, "%d", port);
    memset(&hints,0,sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    /* Ask for every address the name has, in the order the resolver puts them
     * in. The two-pass version this replaces asked for IPv4 and only fell back
     * to IPv6 when the *resolution* failed, so a name with an address in each
     * family never produced an IPv6 attempt at all -- the IPv4 lookup
     * succeeded, and that was the end of it. */
    if ((rv = getaddrinfo(addr,_port,&hints,&servinfo)) != 0) {
        __redisSetError(c,REDIS_ERR_OTHER,gai_strerror(rv));
        return REDIS_ERR;
    }
    n = redisInterleaveByFamily(servinfo,order,REDIS_MAX_ATTEMPTS);
    if (timeout != NULL) {
        if (timeout->tv_usec > 1000000 || timeout->tv_sec > __MAX_MSEC) {
            __redisSetErrorFromErrno(c, REDIS_ERR_IO, NULL);
            freeaddrinfo(servinfo);
            return REDIS_ERR;
        }
        end_at = redisMstime() +
                 (timeout->tv_sec * 1000) + ((timeout->tv_usec + 999) / 1000);
    }

    while (1) {
        long long now = redisMstime();
        int msec, connected = 0;

        /* The error of the attempt that failed furthest down the list is the
         * one the caller is told about, so an attempt that is about to start
         * clears whatever the one before it left behind: every caller decides
         * whether it is connected by looking at c->err, and a stale error would
         * make a connection that succeeded on a later address look like the
         * refusal of an earlier one. */
        if (started < n && now >= next_at) {
            c->err = 0;
            c->errstr[0] = '\0';
            i = redisStartAttempt(c,order[started],source_addr,&connected,&why);
            if (i < 0) {
                last = started;
                /* The delay is how long an attempt that is still running holds
                 * the next one back, so one that has already failed does not
                 * hold it back at all. */
                next_at = 0;
            } else {
                open_fd[nopen] = i;
                open_ix[nopen] = started;
                nopen++;
                next_at = redisMstime() + REDIS_ATTEMPT_DELAY_MS;
            }
            started++;
            /* A name with one address has no race to lose, so a caller that
             * asked not to block is not made to wait for one: it gets the
             * attempt in progress the moment it is started, exactly as
             * before. */
            if (started >= n)
                grace_at = redisMstime() + (n > 1 ? REDIS_ATTEMPT_DELAY_MS : 0);
            if (connected) { s = i; break; }
            continue;
        }
        if (nopen == 0) {
            errno = why ? why : EINVAL;
            __redisSetErrorFromErrno(c,REDIS_ERR_IO,NULL);
            break;
        }

        if (started < n) {
            msec = (int)(next_at - now);
            if (msec < 0) msec = 0;
        } else if (!blocking) {
            /* A non-blocking caller gets the attempt that was started first,
             * still in progress -- but only after every address has had an
             * attempt started and one further delay has passed with none of
             * them finishing. Handing it back the moment the last attempt
             * starts would return a blackholed descriptor in exactly the case
             * this race exists for. */
            msec = (int)(grace_at - now);
            if (msec <= 0) { s = open_fd[0]; break; }
        } else {
            msec = -1;
        }
        if (end_at >= 0) {
            int left = (int)(end_at - now);
            if (left <= 0) {
                errno = ETIMEDOUT;
                __redisSetErrorFromErrno(c,REDIS_ERR_IO,NULL);
                break;
            }
            if (msec < 0 || msec > left) msec = left;
        }

        for (i = 0; i < nopen; i++) {
            pfd[i].fd = open_fd[i];
            pfd[i].events = POLLOUT;
            pfd[i].revents = 0;
        }
        if (poll(pfd,nopen,msec) == -1) {
            if (errno == EINTR) continue;
            __redisSetErrorFromErrno(c, REDIS_ERR_IO, "poll(2)");
            break;
        }
        for (i = 0; i < nopen; i++) {
            int e = 0;
            socklen_t elen = sizeof(e);

            if (!pfd[i].revents) continue;
            if (getsockopt(pfd[i].fd,SOL_SOCKET,SO_ERROR,&e,&elen) == -1)
                e = errno;
            if (e == 0) { s = pfd[i].fd; break; }
            if (open_ix[i] > last) { last = open_ix[i]; why = e; }
            close(pfd[i].fd);
            open_fd[i] = -1;
            next_at = 0;  /* a failure lets the next address start at once */
        }
        if (s >= 0) break;
        for (i = 0, rv = 0; i < nopen; i++) {
            if (open_fd[i] < 0) continue;
            open_fd[rv] = open_fd[i];
            open_ix[rv] = open_ix[i];
            rv++;
        }
        nopen = rv;
    }

    /* The first connection that completes wins and every other attempt is
     * abandoned (RFC 8305 section 5). */
    for (i = 0; i < nopen; i++)
        if (open_fd[i] >= 0 && open_fd[i] != s) close(open_fd[i]);
    freeaddrinfo(servinfo);
    if (s < 0) {
        c->fd = -1;
        return REDIS_ERR;
    }
    c->err = 0;
    c->errstr[0] = '\0';
    c->fd = s;
    if (blocking && redisSetBlocking(c,1) != REDIS_OK)
        return REDIS_ERR;
    if (redisSetTcpNoDelay(c) != REDIS_OK)
        return REDIS_ERR;
    c->flags |= REDIS_CONNECTED;
    return REDIS_OK;
}

int redisContextConnectTcp(redisContext *c, const char *addr, int port,
                           const struct timeval *timeout) {
    return _redisContextConnectTcp(c, addr, port, timeout, NULL);
}

int redisContextConnectBindTcp(redisContext *c, const char *addr, int port,
                               const struct timeval *timeout,
                               const char *source_addr) {
    return _redisContextConnectTcp(c, addr, port, timeout, source_addr);
}

int redisContextConnectUnix(redisContext *c, const char *path, const struct timeval *timeout) {
    int blocking = (c->flags & REDIS_BLOCK);
    struct sockaddr_un sa;

    if (redisCreateSocket(c,AF_LOCAL) < 0)
        return REDIS_ERR;
    if (redisSetBlocking(c,0) != REDIS_OK)
        return REDIS_ERR;

    sa.sun_family = AF_LOCAL;
    strncpy(sa.sun_path,path,sizeof(sa.sun_path)-1);
    if (connect(c->fd, (struct sockaddr*)&sa, sizeof(sa)) == -1) {
        if (errno == EINPROGRESS && !blocking) {
            /* This is ok. */
        } else {
            if (redisContextWaitReady(c,timeout) != REDIS_OK)
                return REDIS_ERR;
        }
    }

    /* Reset socket to be blocking after connect(2). */
    if (blocking && redisSetBlocking(c,1) != REDIS_OK)
        return REDIS_ERR;

    c->flags |= REDIS_CONNECTED;
    return REDIS_OK;
}

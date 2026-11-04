/* anet.c -- Basic TCP socket stuff made a bit less boring
 *
 * Copyright (c) 2006-2012, Salvatore Sanfilippo <antirez at gmail dot com>
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
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <strings.h>
#include <netdb.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "anet.h"

static void anetSetError(char *err, const char *fmt, ...)
{
    va_list ap;

    if (!err) return;
    va_start(ap, fmt);
    vsnprintf(err, ANET_ERR_LEN, fmt, ap);
    va_end(ap);
}

int anetSetBlock(char *err, int fd, int non_block) {
    int flags;

    /* Set the socket blocking (if non_block is zero) or non-blocking.
     * Note that fcntl(2) for F_GETFL and F_SETFL can't be
     * interrupted by a signal. */
    if ((flags = fcntl(fd, F_GETFL)) == -1) {
        anetSetError(err, "fcntl(F_GETFL): %s", strerror(errno));
        return ANET_ERR;
    }

    if (non_block)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;

    if (fcntl(fd, F_SETFL, flags) == -1) {
        anetSetError(err, "fcntl(F_SETFL,O_NONBLOCK): %s", strerror(errno));
        return ANET_ERR;
    }
    return ANET_OK;
}

int anetNonBlock(char *err, int fd) {
    return anetSetBlock(err,fd,1);
}

int anetBlock(char *err, int fd) {
    return anetSetBlock(err,fd,0);
}

/* Set TCP keep alive option to detect dead peers. The interval option
 * is only used for Linux as we are using Linux-specific APIs to set
 * the probe send time, interval, and count. */
int anetKeepAlive(char *err, int fd, int interval)
{
    int val = 1;

    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val)) == -1)
    {
        anetSetError(err, "setsockopt SO_KEEPALIVE: %s", strerror(errno));
        return ANET_ERR;
    }

#ifdef __linux__
    /* Default settings are more or less garbage, with the keepalive time
     * set to 7200 by default on Linux. Modify settings to make the feature
     * actually useful. */

    /* Send first probe after interval. */
    val = interval;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &val, sizeof(val)) < 0) {
        anetSetError(err, "setsockopt TCP_KEEPIDLE: %s\n", strerror(errno));
        return ANET_ERR;
    }

    /* Send next probes after the specified interval. Note that we set the
     * delay as interval / 3, as we send three probes before detecting
     * an error (see the next setsockopt call). */
    val = interval/3;
    if (val == 0) val = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &val, sizeof(val)) < 0) {
        anetSetError(err, "setsockopt TCP_KEEPINTVL: %s\n", strerror(errno));
        return ANET_ERR;
    }

    /* Consider the socket in error state after three we send three ACK
     * probes without getting a reply. */
    val = 3;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &val, sizeof(val)) < 0) {
        anetSetError(err, "setsockopt TCP_KEEPCNT: %s\n", strerror(errno));
        return ANET_ERR;
    }
#else
    ((void) interval); /* Avoid unused var warning for non Linux systems. */
#endif

    return ANET_OK;
}

static int anetSetTcpNoDelay(char *err, int fd, int val)
{
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)) == -1)
    {
        anetSetError(err, "setsockopt TCP_NODELAY: %s", strerror(errno));
        return ANET_ERR;
    }
    return ANET_OK;
}

int anetEnableTcpNoDelay(char *err, int fd)
{
    return anetSetTcpNoDelay(err, fd, 1);
}

int anetDisableTcpNoDelay(char *err, int fd)
{
    return anetSetTcpNoDelay(err, fd, 0);
}


int anetSetSendBuffer(char *err, int fd, int buffsize)
{
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buffsize, sizeof(buffsize)) == -1)
    {
        anetSetError(err, "setsockopt SO_SNDBUF: %s", strerror(errno));
        return ANET_ERR;
    }
    return ANET_OK;
}

int anetTcpKeepAlive(char *err, int fd)
{
    int yes = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes)) == -1) {
        anetSetError(err, "setsockopt SO_KEEPALIVE: %s", strerror(errno));
        return ANET_ERR;
    }
    return ANET_OK;
}

/* Set the socket send timeout (SO_SNDTIMEO socket option) to the specified
 * number of milliseconds, or disable it if the 'ms' argument is zero. */
int anetSendTimeout(char *err, int fd, long long ms) {
    struct timeval tv;

    tv.tv_sec = ms/1000;
    tv.tv_usec = (ms%1000)*1000;
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == -1) {
        anetSetError(err, "setsockopt SO_SNDTIMEO: %s", strerror(errno));
        return ANET_ERR;
    }
    return ANET_OK;
}

/* Write the numeric form of a socket address, and its port, into the caller's
 * buffers. getnameinfo() takes the family out of the address it is handed, so
 * there is one code path here for every family the kernel can produce: the
 * caller no longer needs a list of the families it knows how to print, which is
 * exactly the list that used to stop at IPv4. */
static int anetGenericAddrToString(struct sockaddr *sa, socklen_t salen,
                                   char *ip, size_t ip_len, int *port)
{
    char service[NI_MAXSERV];

    if (ip == NULL) ip_len = 0;
    if (getnameinfo(sa,salen,ip,ip_len,service,sizeof(service),
                    NI_NUMERICHOST|NI_NUMERICSERV) != 0)
        return ANET_ERR;
    if (port) *port = atoi(service);
    return ANET_OK;
}

/* anetGenericResolve() is called by anetResolve() and anetResolveIP() to
 * do the actual work. It resolves the hostname "host" and set the string
 * representation of the IP address into the buffer pointed by "ipbuf".
 *
 * If flags is set to ANET_IP_ONLY the function only resolves hostnames
 * that are actually already IPv4 or IPv6 addresses. This turns the function
 * into a validating / normalizing function. */
int anetGenericResolve(char *err, char *host, char *ipbuf, size_t ipbuf_len,
                       int flags)
{
    struct addrinfo hints, *info;
    int rv;

    memset(&hints,0,sizeof(hints));
    if (flags & ANET_IP_ONLY) hints.ai_flags = AI_NUMERICHOST;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;  /* specify socktype to avoid dups */

    if ((rv = getaddrinfo(host, NULL, &hints, &info)) != 0) {
        anetSetError(err, "%s", gai_strerror(rv));
        return ANET_ERR;
    }
    if (anetGenericAddrToString(info->ai_addr,info->ai_addrlen,
                                ipbuf,ipbuf_len,NULL) == ANET_ERR)
    {
        anetSetError(err, "unable to format the resolved address");
        freeaddrinfo(info);
        return ANET_ERR;
    }

    freeaddrinfo(info);
    return ANET_OK;
}

int anetResolve(char *err, char *host, char *ipbuf, size_t ipbuf_len) {
    return anetGenericResolve(err,host,ipbuf,ipbuf_len,ANET_NONE);
}

int anetResolveIP(char *err, char *host, char *ipbuf, size_t ipbuf_len) {
    return anetGenericResolve(err,host,ipbuf,ipbuf_len,ANET_IP_ONLY);
}

static int anetSetReuseAddr(char *err, int fd) {
    int yes = 1;
    /* Make sure connection-intensive things like the redis benckmark
     * will be able to close/open sockets a zillion of times */
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        anetSetError(err, "setsockopt SO_REUSEADDR: %s", strerror(errno));
        return ANET_ERR;
    }
    return ANET_OK;
}

static int anetCreateSocket(char *err, int domain) {
    int s;
    if ((s = socket(domain, SOCK_STREAM, 0)) == -1) {
        anetSetError(err, "creating socket: %s", strerror(errno));
        return ANET_ERR;
    }

    /* Make sure connection-intensive things like the redis benchmark
     * will be able to close/open sockets a zillion of times */
    if (anetSetReuseAddr(err,s) == ANET_ERR) {
        close(s);
        return ANET_ERR;
    }
    return s;
}

#define ANET_CONNECT_NONE 0
#define ANET_CONNECT_NONBLOCK 1
#define ANET_CONNECT_BE_BINDING 2 /* Best effort binding. */

/* RFC 8305 section 5: "The RECOMMENDED value for the Connection Attempt Delay
 * is 250 milliseconds." An attempt that has not finished within it does not
 * stop the next one from starting; it is not a timeout on the attempt, which
 * keeps running until it succeeds, fails, or the whole connect gives up. */
#define ANET_ATTEMPT_DELAY_MS 250

/* How many of a name's addresses are raced. RFC 8305 leaves the number open;
 * this bounds the descriptors and the stack one connect can cost on a name with
 * a long answer. Addresses past it are still attempted -- they are simply the
 * tail of the same ordered list, reached as earlier attempts fail. */
#define ANET_MAX_ATTEMPTS 16

/* RFC 8305 section 4: the resolver has already sorted the answer (RFC 6724),
 * and that order is kept *within* each family; the two families are then
 * interleaved so that the first attempt is IPv6 and no two addresses of one
 * family are attempted in a row while the other family still has one to offer.
 *
 * Only one family has to be named to do that, and it is the one the section
 * puts first: an address either is IPv6 or is not, and the addresses that are
 * not keep their own order among themselves. Nothing the resolver returned is
 * dropped, whatever family it turns out to be.
 *
 * Returns how many entries were written, which is every address the answer held
 * up to `max`. */
static int anetInterleaveByFamily(struct addrinfo *servinfo,
                                  struct addrinfo **out, int max)
{
    struct addrinfo *v6[ANET_MAX_ATTEMPTS], *rest[ANET_MAX_ATTEMPTS], *p;
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

static long long anetMstime(void) {
    struct timeval tv;

    gettimeofday(&tv,NULL);
    return ((long long)tv.tv_sec)*1000 + tv.tv_usec/1000;
}

/* Start one attempt and hand back its descriptor, non-blocking and either
 * already connected (*connected is set) or in progress. On failure the
 * descriptor is closed, ANET_ERR is returned, and *why keeps the errno so the
 * caller can report the last attempt's failure rather than a generic one. */
static int anetStartAttempt(char *err, struct addrinfo *p, char *source_addr,
                            int flags, int *connected, int *why)
{
    struct addrinfo hints, *bservinfo, *b;
    int s, rv;

    *connected = 0;
    if ((s = socket(p->ai_family,p->ai_socktype,p->ai_protocol)) == -1) {
        *why = errno;
        return ANET_ERR;
    }
    if (anetSetReuseAddr(err,s) == ANET_ERR) { *why = errno; goto error; }
    /* Every attempt is made non-blocking whatever the caller asked for: a
     * blocking connect() cannot be raced against anything, because the thread
     * is inside it. What the caller asked for is restored on the winner. */
    if (anetNonBlock(err,s) != ANET_OK) { *why = errno; goto error; }
    if (source_addr) {
        int bound = 0;
        memset(&hints,0,sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        /* Using getaddrinfo saves us from self-determining IPv4 vs IPv6 */
        if ((rv = getaddrinfo(source_addr, NULL, &hints, &bservinfo)) != 0) {
            anetSetError(err, "%s", gai_strerror(rv));
            *why = EADDRNOTAVAIL;
            goto error;
        }
        for (b = bservinfo; b != NULL; b = b->ai_next) {
            if (bind(s,b->ai_addr,b->ai_addrlen) != -1) {
                bound = 1;
                break;
            }
        }
        freeaddrinfo(bservinfo);
        /* A source address has a family, and now the destinations do not all
         * share it: a replica bound to one IPv6 address cannot bind that
         * address as the source of an attempt to an IPv4 destination, so every
         * IPv4 attempt of the race would die here and the name's IPv4
         * addresses would become unreachable to it. That is what best-effort
         * binding is for, and this is where the effort now ends -- per attempt,
         * rather than per call. The single-address case is unchanged: what the
         * caller got before was the same unbound socket, by way of the retry at
         * the bottom of anetTcpGenericConnect(). A caller that did *not* ask
         * for best effort still gets a failed attempt, because for it the
         * source address is a requirement and not a preference. */
        if (!bound && !(flags & ANET_CONNECT_BE_BINDING)) {
            anetSetError(err, "bind: %s", strerror(errno));
            *why = errno;
            goto error;
        }
    }
    if (connect(s,p->ai_addr,p->ai_addrlen) == -1) {
        if (errno != EINPROGRESS) {
            *why = errno;
            goto error;
        }
    } else {
        *connected = 1;
    }
    return s;

error:
    close(s);
    return ANET_ERR;
}

/* Connect to `addr` by RFC 8305: resolve without naming a family, interleave
 * the resolver's order by family starting with IPv6, start the first attempt at
 * once and each next one a Connection Attempt Delay later -- sooner if an
 * attempt has already failed, since then nothing is in flight -- and take the
 * first connection that completes, abandoning the rest.
 *
 * The delay is what a blackholed address costs, instead of the whole connect:
 * an address that answers nothing holds up only its own attempt, and the next
 * family's attempt is already running beside it.
 *
 * A caller that asked for a non-blocking connect is handed a descriptor that
 * may still be in progress, as before -- but on a name with more than one
 * address, only once every one of them has had an attempt started and one
 * further delay has passed with none of them finishing. Returning the first
 * attempt the moment the last one starts would hand back a blackholed
 * descriptor in the very case this exists for. */
static int anetTcpGenericConnect(char *err, char *addr, int port,
                                 char *source_addr, int flags)
{
    int s = ANET_ERR, rv, i, n, started = 0, nopen = 0, why = 0, last = -1;
    long long next_at = 0, grace_at = 0;
    char portstr[6];  /* strlen("65535") + 1; */
    struct addrinfo hints, *servinfo;
    struct addrinfo *order[ANET_MAX_ATTEMPTS];
    struct pollfd pfd[ANET_MAX_ATTEMPTS];
    int open_fd[ANET_MAX_ATTEMPTS];
    int open_ix[ANET_MAX_ATTEMPTS];

    snprintf(portstr,sizeof(portstr),"%d",port);
    memset(&hints,0,sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(addr,portstr,&hints,&servinfo)) != 0) {
        anetSetError(err, "%s", gai_strerror(rv));
        return ANET_ERR;
    }
    n = anetInterleaveByFamily(servinfo,order,ANET_MAX_ATTEMPTS);

    while (1) {
        long long now = anetMstime();
        int timeout, connected = 0;

        /* Start the next attempt when it is due. The delay is how long an
         * attempt that is still running holds the next one back, so an attempt
         * that has already failed does not hold it back at all: `next_at` is
         * put in the past by every failure, here and in the poll below, and the
         * next address is attempted on the spot. */
        if (started < n && now >= next_at) {
            int fd = anetStartAttempt(err,order[started],source_addr,flags,
                                      &connected,&why);
            if (fd == ANET_ERR) {
                last = started;
                next_at = 0;
            } else {
                open_fd[nopen] = fd;
                open_ix[nopen] = started;
                nopen++;
                next_at = anetMstime() + ANET_ATTEMPT_DELAY_MS;
            }
            started++;
            /* A name with one address has no race to lose, so a caller that
             * asked not to block is not made to wait for one: it gets the
             * attempt in progress the moment it is started, exactly as before.
             * Every such caller here is an event loop -- the replication link,
             * the cluster bus, MIGRATE -- and making it wait on a single
             * address would cost the server its responsiveness for nothing. */
            if (started >= n)
                grace_at = anetMstime() + (n > 1 ? ANET_ATTEMPT_DELAY_MS : 0);
            if (connected) { s = fd; break; }
            continue;
        }
        if (nopen == 0) {
            /* Every address has been attempted and none of them is still
             * running. `why` is the last attempt's errno by construction. */
            anetSetError(err, "connect: %s",
                         why ? strerror(why) : "no address for the name");
            break;
        }

        if (started < n) {
            timeout = (int)(next_at - now);
        } else if (flags & ANET_CONNECT_NONBLOCK) {
            timeout = (int)(grace_at - now);
            if (timeout <= 0) {
                /* Hand the caller the attempt that was started first, still in
                 * progress: the contract it already has for a non-blocking
                 * connect. */
                s = open_fd[0];
                break;
            }
        } else {
            timeout = -1;
        }
        if (timeout < 0 && started < n) timeout = 0;

        for (i = 0; i < nopen; i++) {
            pfd[i].fd = open_fd[i];
            pfd[i].events = POLLOUT;
            pfd[i].revents = 0;
        }
        if (poll(pfd,nopen,timeout) < 0) {
            if (errno == EINTR) continue;
            anetSetError(err, "poll: %s", strerror(errno));
            break;
        }
        for (i = 0; i < nopen; i++) {
            int e = 0;
            socklen_t elen = sizeof(e);

            if (!pfd[i].revents) continue;
            if (getsockopt(pfd[i].fd,SOL_SOCKET,SO_ERROR,&e,&elen) == -1)
                e = errno;
            if (e == 0) { s = pfd[i].fd; break; }
            /* This address is out. The error reported if none of them connects
             * is the one belonging to the attempt furthest down the list, which
             * is the same rule the sequential walk had. */
            if (open_ix[i] > last) { last = open_ix[i]; why = e; }
            close(pfd[i].fd);
            open_fd[i] = -1;
            next_at = 0;  /* a failure lets the next address start at once */
        }
        if (s != ANET_ERR) break;
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
    if (s != ANET_ERR && !(flags & ANET_CONNECT_NONBLOCK) &&
        anetBlock(err,s) != ANET_OK)
    {
        close(s);
        s = ANET_ERR;
    }
    freeaddrinfo(servinfo);

    /* Handle best effort binding: if a binding address was used, but it is
     * not possible to create a socket, try again without a binding address. */
    if (s == ANET_ERR && source_addr && (flags & ANET_CONNECT_BE_BINDING)) {
        return anetTcpGenericConnect(err,addr,port,NULL,flags);
    } else {
        return s;
    }
}

int anetTcpConnect(char *err, char *addr, int port)
{
    return anetTcpGenericConnect(err,addr,port,NULL,ANET_CONNECT_NONE);
}

int anetTcpNonBlockConnect(char *err, char *addr, int port)
{
    return anetTcpGenericConnect(err,addr,port,NULL,ANET_CONNECT_NONBLOCK);
}

int anetTcpNonBlockBindConnect(char *err, char *addr, int port,
                               char *source_addr)
{
    return anetTcpGenericConnect(err,addr,port,source_addr,
            ANET_CONNECT_NONBLOCK);
}

int anetTcpNonBlockBestEffortBindConnect(char *err, char *addr, int port,
                                         char *source_addr)
{
    return anetTcpGenericConnect(err,addr,port,source_addr,
            ANET_CONNECT_NONBLOCK|ANET_CONNECT_BE_BINDING);
}

int anetUnixGenericConnect(char *err, char *path, int flags)
{
    int s;
    struct sockaddr_un sa;

    if ((s = anetCreateSocket(err,AF_LOCAL)) == ANET_ERR)
        return ANET_ERR;

    sa.sun_family = AF_LOCAL;
    strncpy(sa.sun_path,path,sizeof(sa.sun_path)-1);
    if (flags & ANET_CONNECT_NONBLOCK) {
        if (anetNonBlock(err,s) != ANET_OK)
            return ANET_ERR;
    }
    if (connect(s,(struct sockaddr*)&sa,sizeof(sa)) == -1) {
        if (errno == EINPROGRESS &&
            flags & ANET_CONNECT_NONBLOCK)
            return s;

        anetSetError(err, "connect: %s", strerror(errno));
        close(s);
        return ANET_ERR;
    }
    return s;
}

int anetUnixConnect(char *err, char *path)
{
    return anetUnixGenericConnect(err,path,ANET_CONNECT_NONE);
}

int anetUnixNonBlockConnect(char *err, char *path)
{
    return anetUnixGenericConnect(err,path,ANET_CONNECT_NONBLOCK);
}

/* Like read(2) but make sure 'count' is read before to return
 * (unless error or EOF condition is encountered) */
int anetRead(int fd, char *buf, int count)
{
    ssize_t nread, totlen = 0;
    while(totlen != count) {
        nread = read(fd,buf,count-totlen);
        if (nread == 0) return totlen;
        if (nread == -1) return -1;
        totlen += nread;
        buf += nread;
    }
    return totlen;
}

/* Like write(2) but make sure 'count' is written before to return
 * (unless error is encountered) */
int anetWrite(int fd, char *buf, int count)
{
    ssize_t nwritten, totlen = 0;
    while(totlen != count) {
        nwritten = write(fd,buf,count-totlen);
        if (nwritten == 0) return totlen;
        if (nwritten == -1) return -1;
        totlen += nwritten;
        buf += nwritten;
    }
    return totlen;
}

static int anetListen(char *err, int s, struct sockaddr *sa, socklen_t len, int backlog) {
    if (bind(s,sa,len) == -1) {
        anetSetError(err, "bind: %s", strerror(errno));
        close(s);
        return ANET_ERR;
    }

    if (listen(s, backlog) == -1) {
        anetSetError(err, "listen: %s", strerror(errno));
        close(s);
        return ANET_ERR;
    }
    return ANET_OK;
}

static int anetV6Only(char *err, int s) {
    int yes = 1;
    if (setsockopt(s,IPPROTO_IPV6,IPV6_V6ONLY,&yes,sizeof(yes)) == -1) {
        anetSetError(err, "setsockopt: %s", strerror(errno));
        close(s);
        return ANET_ERR;
    }
    return ANET_OK;
}

/* Open a listening socket for every address 'bindaddr' resolves to, and store
 * their descriptors in fds[], of which the caller has 'max' slots free.
 *
 * The resolver decides which families are involved and how many addresses there
 * are; this function is told neither. That is the whole difference from the
 * version that took an 'af': a name with an address in each family produced one
 * listener there and produces one listener per address here, so a client that
 * resolves the same name reaches the server whichever address it picks.
 *
 * Every address the resolution returned has to work, with one exception: an
 * address of a family this kernel does not implement is skipped, because the
 * resolver returns those too and no `bind` line can avoid them. Any other
 * address that cannot be listened on fails the whole call, exactly as an
 * unbindable literal address already does: a server that quietly came up on
 * some of the addresses its `bind` line named would be a server whose
 * configuration no longer says where it can be reached, and the operator would
 * find that out from a client.
 *
 * Returns the number of listening sockets created, which is at least one, or
 * ANET_ERR -- in which case 'err' holds the reason, and no descriptor has been
 * left in fds[]. */
int anetTcpServer(char *err, int port, char *bindaddr, int backlog,
                  int *fds, int max)
{
    int s, rv, count = 0;
    char _port[6];  /* strlen("65535") */
    struct addrinfo hints, *servinfo, *p;

    snprintf(_port,6,"%d",port);
    memset(&hints,0,sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;    /* No effect if bindaddr != NULL */

    if ((rv = getaddrinfo(bindaddr,_port,&hints,&servinfo)) != 0) {
        anetSetError(err, "%s", gai_strerror(rv));
        return ANET_ERR;
    }
    anetSetError(err, "unable to bind socket");
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if (count == max) {
            anetSetError(err, "too many listening sockets");
            goto error;
        }
        if ((s = socket(p->ai_family,p->ai_socktype,p->ai_protocol)) == -1) {
            anetSetError(err, "creating socket: %s", strerror(errno));
            /* A family the kernel does not implement is not a configuration
             * error. The resolver answers out of the host's name service,
             * which knows nothing about the running kernel, so a name with an
             * address in each family resolves the same on a host with no IPv4
             * stack at all, and the `bind` line that named it is already
             * right. Skip that address and listen on the rest; if every
             * address is skipped the loop ends with no listener and the call
             * fails below, carrying this message. */
            if (errno == EAFNOSUPPORT || errno == EPFNOSUPPORT ||
                errno == EPROTONOSUPPORT)
                continue;
            goto error;
        }
        /* One address per socket. An IPv6 socket that also accepted IPv4 would
         * own the port for both families, and the IPv4 address further down
         * this same list would then fail to bind. */
        if (p->ai_family == AF_INET6 && anetV6Only(err,s) == ANET_ERR)
            goto error;                 /* anetV6Only closed s */
        if (anetSetReuseAddr(err,s) == ANET_ERR) {
            close(s);
            goto error;
        }
        if (anetListen(err,s,p->ai_addr,p->ai_addrlen,backlog) == ANET_ERR)
            goto error;                 /* anetListen closed s */
        fds[count++] = s;
    }
    freeaddrinfo(servinfo);
    return count ? count : ANET_ERR;

error:
    while (count--) close(fds[count]);
    freeaddrinfo(servinfo);
    return ANET_ERR;
}

int anetUnixServer(char *err, char *path, mode_t perm, int backlog)
{
    int s;
    struct sockaddr_un sa;

    if ((s = anetCreateSocket(err,AF_LOCAL)) == ANET_ERR)
        return ANET_ERR;

    memset(&sa,0,sizeof(sa));
    sa.sun_family = AF_LOCAL;
    strncpy(sa.sun_path,path,sizeof(sa.sun_path)-1);
    if (anetListen(err,s,(struct sockaddr*)&sa,sizeof(sa),backlog) == ANET_ERR)
        return ANET_ERR;
    if (perm)
        chmod(sa.sun_path, perm);
    return s;
}

static int anetGenericAccept(char *err, int s, struct sockaddr *sa, socklen_t *len) {
    int fd;
    while(1) {
        fd = accept(s,sa,len);
        if (fd == -1) {
            if (errno == EINTR)
                continue;
            else {
                anetSetError(err, "accept: %s", strerror(errno));
                return ANET_ERR;
            }
        }
        break;
    }
    return fd;
}

int anetTcpAccept(char *err, int s, char *ip, size_t ip_len, int *port) {
    int fd;
    struct sockaddr_storage sa;
    socklen_t salen = sizeof(sa);
    if ((fd = anetGenericAccept(err,s,(struct sockaddr*)&sa,&salen)) == -1)
        return ANET_ERR;

    anetGenericAddrToString((struct sockaddr*)&sa,salen,ip,ip_len,port);
    return fd;
}

int anetUnixAccept(char *err, int s) {
    int fd;
    struct sockaddr_un sa;
    socklen_t salen = sizeof(sa);
    if ((fd = anetGenericAccept(err,s,(struct sockaddr*)&sa,&salen)) == -1)
        return ANET_ERR;

    return fd;
}

int anetPeerToString(int fd, char *ip, size_t ip_len, int *port) {
    struct sockaddr_storage sa;
    socklen_t salen = sizeof(sa);

    if (getpeername(fd,(struct sockaddr*)&sa,&salen) == -1) goto error;
    if (ip_len == 0) goto error;

    /* A Unix-domain peer has no address to print, so it is the one family that
     * is still named here. Everything else is an address the resolver could
     * have produced, and is printed without asking which family it is in. */
    if (sa.ss_family == AF_UNIX) {
        if (ip) strncpy(ip,"/unixsocket",ip_len);
        if (port) *port = 0;
    } else if (anetGenericAddrToString((struct sockaddr*)&sa,salen,
                                       ip,ip_len,port) == ANET_ERR) {
        goto error;
    }
    return 0;

error:
    if (ip) {
        if (ip_len >= 2) {
            ip[0] = '?';
            ip[1] = '\0';
        } else if (ip_len == 1) {
            ip[0] = '\0';
        }
    }
    if (port) *port = 0;
    return -1;
}

/* Format an IP,port pair into something easy to parse. If IP is IPv6
 * (matches for ":"), the ip is surrounded by []. IP and port are just
 * separated by colons. This the standard to display addresses within Redis. */
int anetFormatAddr(char *buf, size_t buf_len, char *ip, int port) {
    return snprintf(buf,buf_len, strchr(ip,':') ?
           "[%s]:%d" : "%s:%d", ip, port);
}

/* The inverse of anetFormatAddr(): take a string that joins an address to a
 * port and give back the two halves. The bracketed form is accepted --
 * "[::1]:6379" -- and so is a host that has no colon of its own, where the
 * port begins after the last one. Anything else is an error, so a caller can
 * tell a string that is not an address and a port from one it cannot use.
 *
 * Finding the delimiter this way rather than at the *first* colon is what
 * makes a bare IPv6 address work too: "::1:6379" is the address "::1" on port
 * 6379, which is what RFC 3986's grammar implies once the brackets are the
 * only thing that can contain a colon. */
int anetSplitAddr(const char *addr, char *host, size_t host_len, int *port) {
    const char *start, *sep, *close;
    size_t len;
    char *end;
    long n;

    if (addr == NULL || host == NULL || host_len == 0) return ANET_ERR;
    if (addr[0] == '[') {
        close = strchr(addr+1,']');
        if (close == NULL || close[1] != ':') return ANET_ERR;
        start = addr+1;
        len = close-start;
        sep = close+1;
    } else {
        sep = strrchr(addr,':');
        if (sep == NULL) return ANET_ERR;
        start = addr;
        len = sep-start;
    }
    if (len == 0 || len >= host_len) return ANET_ERR;
    n = strtol(sep+1,&end,10);
    if (end == sep+1 || *end != '\0' || n < 0 || n > 65535) return ANET_ERR;
    memcpy(host,start,len);
    host[len] = '\0';
    if (port) *port = (int)n;
    return ANET_OK;
}

/* Parse one address *literal* into the socket address it denotes.
 * AI_NUMERICHOST keeps this a parse rather than a lookup. The family asked for
 * is the one that can express every address there is: an IPv6 literal parses
 * to itself, and AI_V4MAPPED makes an IPv4 literal parse to the address that
 * embeds it (RFC 4291 section 2.5.5.2), which is how an IPv4 literal and
 * its "::ffff:" form come to be one host in the one place that decides it.
 * Nothing here opens a socket and no family is chosen for one; this is a
 * parser, and what comes out of it is a value rather than a destination. */
static int anetLiteralToSockaddr(const char *text, struct sockaddr_storage *sa,
                                 socklen_t *salen)
{
    struct addrinfo hints, *info;

    memset(sa,0,sizeof(*sa));
    memset(&hints,0,sizeof(hints));
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;  /* specify socktype to avoid dups */
    hints.ai_flags = AI_NUMERICHOST | AI_V4MAPPED;

    if (getaddrinfo(text,NULL,&hints,&info) != 0) return ANET_ERR;
    memcpy(sa,info->ai_addr,info->ai_addrlen);
    *salen = info->ai_addrlen;
    freeaddrinfo(info);
    return ANET_OK;
}

/* Do two address texts denote the same address? One address has many
 * spellings -- "::1" and "0:0:0:0:0:0:0:1" are the same 128 bits, and
 * "::ffff:127.0.0.1" and "::ffff:7f00:1" are the same host written two ways,
 * and so is the IPv4 literal they embed -- so wherever both sides are literals the answer is
 * decided by value. A host that is not a literal is compared the way this tree
 * compares one today, case insensitively, so a registered name is still equal
 * to itself and to nothing else. Two link-local addresses in different zones
 * are never equal, because sin6_scope_id is part of what is compared, and a
 * zone is part of the address it is written on (RFC 4007 section 11.2).
 *
 * This is the one place in the repository that decides it. The server, the
 * client and the benchmark all link anet.o, so all three can ask here. */
int anetAddrIsEqual(const char *a, const char *b) {
    struct sockaddr_storage sa, sb;
    socklen_t la, lb;

    if (a == NULL || b == NULL) return a == b;
    if (anetLiteralToSockaddr(a,&sa,&la) != ANET_OK ||
        anetLiteralToSockaddr(b,&sb,&lb) != ANET_OK)
        return strcasecmp(a,b) == 0;
    /* Both structures were zeroed before they were filled and neither was
     * given a service, so what is left in them is the family and the address:
     * two spellings of one address give identical bytes, and two families
     * never do. */
    return la == lb && memcmp(&sa,&sb,(size_t)la) == 0;
}

/* What kind of address is this? Decided from the bits and never from the text,
 * so that an address this tree has never seen before is answered correctly the
 * first time and two spellings of one address are never given two answers.
 *
 * A mapped address is classified as the IPv4 address it embeds, because that
 * is the address it is: "::ffff:127.0.0.1" is loopback and "::ffff:224.0.0.1"
 * is multicast, exactly as the addresses they embed are. Nothing about
 * *printing* changes with it, and nothing may -- anetGenericAddrToString()
 * renders a mapped address in the mixed form of RFC 5952 section 5 and keeps
 * doing so. What an address is called and which address it is are two
 * questions, they are answered in two places, and a tree that answers them in
 * one breaks the other. */
static int anetIn6Class(const struct in6_addr *a) {
    if (IN6_IS_ADDR_V4MAPPED(a)) {
        uint32_t h = ((uint32_t)a->s6_addr[12] << 24) |
                     ((uint32_t)a->s6_addr[13] << 16) |
                     ((uint32_t)a->s6_addr[14] << 8) |
                     ((uint32_t)a->s6_addr[15]);

        if (h == 0) return ANET_ADDR_UNSPECIFIED;             /* 0.0.0.0/32 */
        if ((h >> 24) == 0x7f) return ANET_ADDR_LOOPBACK;     /* 127/8 */
        if ((h >> 16) == 0xa9fe) return ANET_ADDR_LINK_LOCAL; /* 169.254/16 */
        if ((h >> 28) == 0xe) return ANET_ADDR_MULTICAST;     /* 224/4 */
        if ((h >> 24) == 0x0a ||                              /* 10/8 */
            (h >> 20) == 0xac1 ||                             /* 172.16/12 */
            (h >> 16) == 0xc0a8)                              /* 192.168/16 */
            return ANET_ADDR_UNIQUE_LOCAL;
        return ANET_ADDR_GLOBAL;
    }
    /* Order matters only where the prefixes overlap: the all-zero address and
     * the loopback address both sit inside the ninety-six zero bits that
     * IN6_IS_ADDR_V4COMPAT() looks for. Glibc's macro already excludes them;
     * asking about them first says so here rather than relying on it. */
    if (IN6_IS_ADDR_UNSPECIFIED(a)) return ANET_ADDR_UNSPECIFIED;
    if (IN6_IS_ADDR_LOOPBACK(a)) return ANET_ADDR_LOOPBACK;
    if (IN6_IS_ADDR_MULTICAST(a)) return ANET_ADDR_MULTICAST;
    if (IN6_IS_ADDR_V4COMPAT(a)) return ANET_ADDR_V4COMPAT;
    if (IN6_IS_ADDR_LINKLOCAL(a)) return ANET_ADDR_LINK_LOCAL;
    if ((a->s6_addr[0] & 0xfe) == 0xfc) return ANET_ADDR_UNIQUE_LOCAL;
    /* fec0::/10 is not a class of its own here: RFC 3879 withdrew site-local
     * and RFC 4291 section 2.5.7 removed it from the architecture, so an
     * address there is ordinary unicast sitting in a prefix nobody assigns.
     * IN6_IS_ADDR_SITELOCAL() still answers for it, and a macro existing is
     * not the same as the class being current. */
    return ANET_ADDR_GLOBAL;
}

/* A text that is not an address literal at all is ANET_ADDR_INVALID, and that
 * is not an error: this tree accepts a registered name almost everywhere it
 * accepts an address, and what a name denotes is the resolver's answer rather
 * than this function's. */
int anetAddrClass(const char *addr) {
    struct sockaddr_storage sa;
    socklen_t salen;

    if (addr == NULL) return ANET_ADDR_INVALID;
    if (anetLiteralToSockaddr(addr,&sa,&salen) != ANET_OK) return ANET_ADDR_INVALID;
    if (sa.ss_family != AF_INET6) return ANET_ADDR_INVALID;
    return anetIn6Class(&((struct sockaddr_in6*)&sa)->sin6_addr);
}

/* The name of a class, for an error message a person reads. */
const char *anetAddrClassName(int cls) {
    switch(cls) {
    case ANET_ADDR_UNSPECIFIED:  return "unspecified";
    case ANET_ADDR_LOOPBACK:     return "loopback";
    case ANET_ADDR_LINK_LOCAL:   return "link-local unicast";
    case ANET_ADDR_UNIQUE_LOCAL: return "unique-local unicast";
    case ANET_ADDR_MULTICAST:    return "multicast";
    case ANET_ADDR_V4COMPAT:     return "deprecated IPv4-compatible";
    case ANET_ADDR_GLOBAL:       return "global unicast";
    default:                     return "not an address literal";
    }
}

/* Can this address name one other machine? A peer is a single interface this
 * tree reaches out to, so it is a unicast address and nothing else:
 *
 *   - a multicast address names a group rather than a host, and RFC 4291
 *     section 2.7 leaves it no second reading: IPv6 has no broadcast address
 *     and this is what took its place;
 *   - the unspecified address names no host at all. It is what a *listener* is
 *     given to mean "every address this machine has", which is a different
 *     question from where another machine is;
 *   - an IPv4-compatible address was deprecated by RFC 4291 section 2.5.5.1
 *     along with the transition mechanism that used it, and is not the IPv4
 *     address it resembles.
 *
 * Loopback, link-local and unique-local are unicast and are all perfectly good
 * places for a peer to be -- this repository's own cluster and Sentinel suites
 * run on loopback and nothing else. A text that is not a literal is not judged
 * here: the caller is naming a host, and the resolver will say what it is. */
int anetAddrIsPeer(const char *addr) {
    switch(anetAddrClass(addr)) {
    case ANET_ADDR_UNSPECIFIED:
    case ANET_ADDR_MULTICAST:
    case ANET_ADDR_V4COMPAT:
        return 0;
    default:
        return 1;
    }
}

/* Like anetFormatAddr() but extract ip and port from the socket's peer. */
int anetFormatPeer(int fd, char *buf, size_t buf_len) {
    char ip[INET6_ADDRSTRLEN];
    int port;

    anetPeerToString(fd,ip,sizeof(ip),&port);
    return anetFormatAddr(buf, buf_len, ip, port);
}

int anetSockName(int fd, char *ip, size_t ip_len, int *port) {
    struct sockaddr_storage sa;
    socklen_t salen = sizeof(sa);

    if (getsockname(fd,(struct sockaddr*)&sa,&salen) == -1) {
        if (port) *port = 0;
        ip[0] = '?';
        ip[1] = '\0';
        return -1;
    }
    if (anetGenericAddrToString((struct sockaddr*)&sa,salen,ip,ip_len,port)
        == ANET_ERR)
    {
        if (port) *port = 0;
        ip[0] = '?';
        ip[1] = '\0';
        return -1;
    }
    return 0;
}

int anetFormatSock(int fd, char *fmt, size_t fmt_len) {
    char ip[INET6_ADDRSTRLEN];
    int port;

    anetSockName(fd,ip,sizeof(ip),&port);
    return anetFormatAddr(fmt, fmt_len, ip, port);
}

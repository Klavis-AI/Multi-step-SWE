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

#ifndef ANET_H
#define ANET_H

#define ANET_OK 0
#define ANET_ERR -1
#define ANET_ERR_LEN 256
#define ANET_ADDR_STR_LEN 64

/* What an address is, decided from its value rather than from its text.
 * RFC 4291 section 2.4 for the IPv6 classes and their prefixes, RFC 4193 for
 * fc00::/7, RFC 3879 for the withdrawal of fec0::/10, RFC 4291 section 2.5.5.1
 * for the deprecated IPv4-compatible form. An IPv4 address is classified from
 * the IANA IPv4 Special-Purpose Address Registry on the same terms, and an
 * IPv4-mapped address (RFC 4291 section 2.5.5.2) is classified as the IPv4
 * address it embeds -- it is that address, written the other way. */
#define ANET_ADDR_INVALID      0   /* not an address literal at all */
#define ANET_ADDR_UNSPECIFIED  1   /* ::/128, 0.0.0.0/8 */
#define ANET_ADDR_LOOPBACK     2   /* ::1/128, 127.0.0.0/8 */
#define ANET_ADDR_LINK_LOCAL   3   /* fe80::/10, 169.254.0.0/16 */
#define ANET_ADDR_UNIQUE_LOCAL 4   /* fc00::/7, and the RFC 1918 ranges */
#define ANET_ADDR_MULTICAST    5   /* ff00::/8, 224.0.0.0/4 */
#define ANET_ADDR_V4COMPAT     6   /* ::a.b.c.d, deprecated */
#define ANET_ADDR_GLOBAL       7   /* everything else that is unicast */

/* Flags used with certain functions. */
#define ANET_NONE 0
#define ANET_IP_ONLY (1<<0)

#if defined(__sun) || defined(_AIX)
#define AF_LOCAL AF_UNIX
#endif

#ifdef _AIX
#undef ip_len
#endif

int anetTcpConnect(char *err, char *addr, int port);
int anetTcpNonBlockConnect(char *err, char *addr, int port);
int anetTcpNonBlockBindConnect(char *err, char *addr, int port, char *source_addr);
int anetTcpNonBlockBestEffortBindConnect(char *err, char *addr, int port, char *source_addr);
int anetUnixConnect(char *err, char *path);
int anetUnixNonBlockConnect(char *err, char *path);
int anetRead(int fd, char *buf, int count);
int anetResolve(char *err, char *host, char *ipbuf, size_t ipbuf_len);
int anetResolveIP(char *err, char *host, char *ipbuf, size_t ipbuf_len);
/* Listens on every address 'bindaddr' resolves to, storing up to 'max'
 * descriptors in fds[] and returning how many it stored. There is no
 * per-family entry point: which families are involved is the resolver's
 * answer, not the caller's question. */
int anetTcpServer(char *err, int port, char *bindaddr, int backlog,
                  int *fds, int max);
int anetUnixServer(char *err, char *path, mode_t perm, int backlog);
int anetTcpAccept(char *err, int serversock, char *ip, size_t ip_len, int *port);
int anetUnixAccept(char *err, int serversock);
int anetWrite(int fd, char *buf, int count);
int anetNonBlock(char *err, int fd);
int anetBlock(char *err, int fd);
int anetEnableTcpNoDelay(char *err, int fd);
int anetDisableTcpNoDelay(char *err, int fd);
int anetTcpKeepAlive(char *err, int fd);
int anetSendTimeout(char *err, int fd, long long ms);
int anetPeerToString(int fd, char *ip, size_t ip_len, int *port);
int anetKeepAlive(char *err, int fd, int interval);
int anetSockName(int fd, char *ip, size_t ip_len, int *port);
int anetFormatAddr(char *fmt, size_t fmt_len, char *ip, int port);
int anetSplitAddr(const char *addr, char *host, size_t host_len, int *port);
int anetAddrIsEqual(const char *a, const char *b);
int anetAddrClass(const char *addr);
const char *anetAddrClassName(int cls);
int anetAddrIsPeer(const char *addr);
int anetFormatPeer(int fd, char *fmt, size_t fmt_len);
int anetFormatSock(int fd, char *fmt, size_t fmt_len);

#endif

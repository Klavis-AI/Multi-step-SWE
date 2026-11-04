#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "http.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define HTTP_MAX_HEAD   (64u * 1024u)   /* request line + headers */
#define HTTP_IO_TIMEOUT 10              /* seconds, per recv/send */
#define HTTP_IDLE_MS    30000           /* keep-alive idle before we hang up */

/* ------------------------------------------------------------------------ */

int http_listen(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int on = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on) < 0)
        goto fail;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) goto fail;
    if (listen(fd, 128) < 0) goto fail;
    return fd;

fail: {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
}

void http_tune(int fd)
{
    struct timeval tv;
    tv.tv_sec = HTTP_IO_TIMEOUT;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    int on = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);
}

void http_conn_init(http_conn *c, int fd, int listen_fd)
{
    memset(c, 0, sizeof *c);
    c->fd = fd;
    c->listen_fd = listen_fd;
}

void http_conn_free(http_conn *c)
{
    free(c->buf);
    c->buf = NULL;
    c->len = c->cap = 0;
}

void http_request_free(http_request *r)
{
    free(r->method);
    free(r->target);
    free(r->body);
    memset(r, 0, sizeof *r);
}

/* ------------------------------------------------------------------------ */

static int buf_reserve(http_conn *c, size_t extra)
{
    if (c->len + extra <= c->cap) return 0;
    size_t nc = c->cap ? c->cap : 8192;
    while (nc < c->len + extra) {
        if (nc > (size_t)-1 / 2) return -1;
        nc *= 2;
    }
    char *nb = realloc(c->buf, nc);
    if (!nb) return -1;
    c->buf = nb;
    c->cap = nc;
    return 0;
}

/* 1 = progress, 0 = peer closed, -1 = timeout or error. */
static int conn_fill(http_conn *c)
{
    if (buf_reserve(c, 8192) != 0) return -1;
    for (;;) {
        ssize_t n = recv(c->fd, c->buf + c->len, c->cap - c->len, 0);
        if (n > 0) { c->len += (size_t)n; return 1; }
        if (n == 0) return 0;
        if (errno == EINTR) continue;
        return -1;
    }
}

/* Waits for the next request on an already-used persistent connection. A peer
   waiting on the listening socket wins, since a server may close an idle
   persistent connection at any time (RFC 7230 6.5). */
static int wait_idle(http_conn *c)
{
    struct pollfd fds[2];
    fds[0].fd = c->fd;
    fds[0].events = POLLIN;
    fds[1].fd = c->listen_fd;
    fds[1].events = POLLIN;
    int nfds = c->listen_fd >= 0 ? 2 : 1;

    for (;;) {
        int n = poll(fds, (nfds_t)nfds, HTTP_IDLE_MS);
        if (n < 0) {
            if (errno == EINTR) continue;
            return 0;
        }
        if (n == 0) return 0;                                 /* idle timeout */
        if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) return 1;
        return 0;                            /* another connection is waiting */
    }
}

/* Locates the end of the header block, tolerating bare LF line endings. */
static int find_head(const char *b, size_t n, size_t *head_len, size_t *body_off)
{
    for (size_t i = 0; i < n; i++) {
        if (b[i] != '\n') continue;
        if (i + 1 < n && b[i + 1] == '\n') {
            *head_len = i + 1;
            *body_off = i + 2;
            return 1;
        }
        if (i + 2 < n && b[i + 1] == '\r' && b[i + 2] == '\n') {
            *head_len = i + 1;
            *body_off = i + 3;
            return 1;
        }
    }
    return 0;
}

static char *next_line(char **cursor)
{
    char *s = *cursor;
    if (!s || !*s) return NULL;
    char *nl = strchr(s, '\n');
    if (nl) { *nl = '\0'; *cursor = nl + 1; }
    else    { *cursor = s + strlen(s); }
    size_t l = strlen(s);
    if (l && s[l - 1] == '\r') s[l - 1] = '\0';
    return s;
}

/* Case-insensitive search for a token in a comma separated header value. */
static int has_token(const char *v, const char *tok)
{
    size_t tl = strlen(tok);
    while (*v) {
        while (*v == ' ' || *v == '\t' || *v == ',') v++;
        if (!*v) break;
        const char *s = v;
        while (*v && *v != ',') v++;
        const char *e = v;
        while (e > s && (e[-1] == ' ' || e[-1] == '\t')) e--;
        if ((size_t)(e - s) == tl && strncasecmp(s, tok, tl) == 0) return 1;
    }
    return 0;
}

static http_status parse_head(char *head, http_request *r,
                              size_t *content_length)
{
    char *cursor = head;
    char *line = next_line(&cursor);
    if (!line || !*line) return HTTP_MALFORMED;

    char *sp = strchr(line, ' ');
    if (!sp) return HTTP_MALFORMED;
    *sp = '\0';
    char *target = sp + 1;
    while (*target == ' ') target++;
    char *sp2 = strchr(target, ' ');
    if (!sp2) return HTTP_MALFORMED;
    *sp2 = '\0';
    char *version = sp2 + 1;
    while (*version == ' ') version++;

    if (!*line || !*target) return HTTP_MALFORMED;
    int http11;
    if (strcmp(version, "HTTP/1.1") == 0)      http11 = 1;
    else if (strcmp(version, "HTTP/1.0") == 0) http11 = 0;
    else return HTTP_MALFORMED;

    r->method = strdup(line);
    r->target = strdup(target);
    if (!r->method || !r->target) return HTTP_MALFORMED;
    r->keep_alive = http11;
    *content_length = 0;

    int saw_length = 0;
    while ((line = next_line(&cursor)) != NULL) {
        if (!*line) continue;
        if (*line == ' ' || *line == '\t') continue;     /* obsolete line fold */
        char *colon = strchr(line, ':');
        if (!colon) return HTTP_MALFORMED;
        *colon = '\0';
        char *val = colon + 1;
        while (*val == ' ' || *val == '\t') val++;
        size_t vl = strlen(val);
        while (vl && (val[vl - 1] == ' ' || val[vl - 1] == '\t')) val[--vl] = '\0';

        if (strcasecmp(line, "content-length") == 0) {
            if (!*val) return HTTP_MALFORMED;
            size_t cl = 0;
            for (const char *d = val; *d; d++) {
                if (*d < '0' || *d > '9') return HTTP_MALFORMED;
                if (cl > ((size_t)-1 - (size_t)(*d - '0')) / 10) return HTTP_TOO_LARGE;
                cl = cl * 10 + (size_t)(*d - '0');
            }
            if (saw_length && cl != *content_length) return HTTP_MALFORMED;
            if (cl > HTTP_MAX_BODY) return HTTP_TOO_LARGE;
            *content_length = cl;
            saw_length = 1;
        } else if (strcasecmp(line, "transfer-encoding") == 0) {
            return HTTP_MALFORMED;     /* chunked bodies are not supported */
        } else if (strcasecmp(line, "connection") == 0) {
            if (has_token(val, "close")) r->keep_alive = 0;
            else if (has_token(val, "keep-alive")) r->keep_alive = 1;
        }
    }
    return HTTP_OK;
}

http_status http_read_request(http_conn *c, http_request *r)
{
    memset(r, 0, sizeof *r);

    if (c->served > 0 && c->len == 0 && !wait_idle(c)) return HTTP_CLOSED;

    size_t head_len = 0, body_off = 0;
    while (!find_head(c->buf, c->len, &head_len, &body_off)) {
        if (c->len > HTTP_MAX_HEAD) return HTTP_MALFORMED;
        int n = conn_fill(c);
        if (n <= 0) return HTTP_CLOSED;     /* EOF, timeout or socket error */
    }

    char *head = malloc(head_len + 1);
    if (!head) return HTTP_CLOSED;
    memcpy(head, c->buf, head_len);
    head[head_len] = '\0';

    size_t content_length = 0;
    http_status st = parse_head(head, r, &content_length);
    free(head);
    if (st != HTTP_OK) { http_request_free(r); return st; }

    while (c->len < body_off + content_length) {
        int n = conn_fill(c);
        if (n <= 0) { http_request_free(r); return HTTP_CLOSED; }
    }

    if (content_length > 0) {
        r->body = malloc(content_length + 1);
        if (!r->body) { http_request_free(r); return HTTP_CLOSED; }
        memcpy(r->body, c->buf + body_off, content_length);
        r->body[content_length] = '\0';
        r->body_len = content_length;
    }

    size_t consumed = body_off + content_length;
    memmove(c->buf, c->buf + consumed, c->len - consumed);
    c->len -= consumed;
    c->served++;
    return HTTP_OK;
}

/* ------------------------------------------------------------------------ */

static const char *reason_phrase(int code)
{
    switch (code) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 500: return "Internal Server Error";
    default:  return "Unknown";
    }
}

static int send_all(int fd, const char *p, size_t n)
{
    while (n > 0) {
        ssize_t w = send(fd, p, n, 0);
        if (w > 0) { p += w; n -= (size_t)w; continue; }
        if (w < 0 && errno == EINTR) continue;
        return -1;
    }
    return 0;
}

int http_send_json(http_conn *c, int code, const char *body, size_t len,
                   int keep_alive)
{
    char head[256];
    int hn = snprintf(head, sizeof head,
                      "HTTP/1.1 %d %s\r\n"
                      "Content-Type: application/json; charset=utf-8\r\n"
                      "Content-Length: %zu\r\n"
                      "Cache-Control: no-cache\r\n"
                      "Connection: %s\r\n"
                      "\r\n",
                      code, reason_phrase(code), len,
                      keep_alive ? "keep-alive" : "close");
    if (hn < 0 || (size_t)hn >= sizeof head) return -1;

    char *msg = malloc((size_t)hn + len);
    if (!msg) return -1;
    memcpy(msg, head, (size_t)hn);
    if (len) memcpy(msg + hn, body, len);
    int rc = send_all(c->fd, msg, (size_t)hn + len);
    free(msg);
    return rc;
}

/* ------------------------------------------------------------------------ */

static int hex_val(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

char *http_percent_decode(const char *s, size_t n)
{
    char *out = malloc(n + 1);
    if (!out) return NULL;
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] != '%') { out[o++] = s[i]; continue; }
        if (i + 2 >= n) { free(out); return NULL; }
        int hi = hex_val(s[i + 1]), lo = hex_val(s[i + 2]);
        if (hi < 0 || lo < 0) { free(out); return NULL; }
        int ch = (hi << 4) | lo;
        if (ch == 0) { free(out); return NULL; }   /* %00 would truncate */
        out[o++] = (char)ch;
        i += 2;
    }
    out[o] = '\0';
    return out;
}

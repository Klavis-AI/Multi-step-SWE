#define _GNU_SOURCE
#include "http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <ctype.h>

#define MAX_BODY (64u * 1024u * 1024u)
#define MAX_HEAD (256u * 1024u)

const char *http_reason(int s) {
    switch (s) {
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 406: return "Not Acceptable";
        case 409: return "Conflict";
        case 410: return "Gone";
        case 412: return "Precondition Failed";
        case 415: return "Unsupported Media Type";
        case 422: return "Unprocessable Entity";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        default:  return "Unknown";
    }
}

const char *hreq_hdr(const hreq *r, const char *name) {
    for (size_t i = 0; i < r->nhdr; i++)
        if (str_ieq(r->hdrs[i].name, name)) return r->hdrs[i].value;
    return NULL;
}

void hresp_init(hresp *r) { r->status = 200; buf_init(&r->hdrs); buf_init(&r->body); }
void hresp_free(hresp *r) { buf_free(&r->hdrs); buf_free(&r->body); }

void hresp_hdr(hresp *r, const char *name, const char *value) {
    buf_printf(&r->hdrs, "%s: %s\r\n", name, value ? value : "");
}

void hresp_hdrf(hresp *r, const char *name, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    va_list ap2; va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    char *tmp = malloc((size_t)n + 1);
    vsnprintf(tmp, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    hresp_hdr(r, name, tmp);
    free(tmp);
}

typedef struct {
    int           fd;
    http_handler  h;
    void         *ctx;
} conn;

static ssize_t read_more(int fd, buf *b) {
    buf_reserve(b, 65536);
    ssize_t n = recv(fd, b->p + b->n, 65536, 0);
    if (n > 0) { b->n += (size_t)n; b->p[b->n] = 0; }
    return n;
}

static int parse_headers(char *head, hreq *req) {
    char *line = head;
    char *nl = strstr(line, "\r\n");
    if (!nl) return -1;
    *nl = 0;
    char *sp1 = strchr(line, ' ');
    if (!sp1) return -1;
    *sp1 = 0;
    char *sp2 = strchr(sp1 + 1, ' ');
    if (!sp2) return -1;
    *sp2 = 0;
    req->method = str_dup(line);
    req->target = str_dup(sp1 + 1);
    int http11 = strncmp(sp2 + 1, "HTTP/1.1", 8) == 0;
    req->keep_alive = http11;

    size_t cap = 32;
    req->hdrs = calloc(cap, sizeof(hhdr));
    req->nhdr = 0;
    line = nl + 2;
    while (*line) {
        nl = strstr(line, "\r\n");
        if (!nl) break;
        *nl = 0;
        if (!*line) break;
        char *colon = strchr(line, ':');
        if (colon) {
            *colon = 0;
            char *v = colon + 1;
            while (*v == ' ' || *v == '\t') v++;
            if (req->nhdr == cap) { cap *= 2; req->hdrs = realloc(req->hdrs, cap * sizeof(hhdr)); }
            req->hdrs[req->nhdr].name = str_dup(line);
            req->hdrs[req->nhdr].value = str_dup(v);
            req->nhdr++;
        }
        line = nl + 2;
    }
    const char *conn_hdr = hreq_hdr(req, "Connection");
    if (conn_hdr) {
        if (str_ieq(conn_hdr, "close")) req->keep_alive = 0;
        else if (str_ieq(conn_hdr, "keep-alive")) req->keep_alive = 1;
    }
    /* split target into path + query */
    char *q = strchr(req->target, '?');
    if (q) { req->path = str_ndup(req->target, (size_t)(q - req->target)); req->query = str_dup(q + 1); }
    else   { req->path = str_dup(req->target); req->query = str_dup(""); }
    url_decode(req->path);
    return 0;
}

static void free_req(hreq *r) {
    free(r->method); free(r->target); free(r->path); free(r->query); free(r->body);
    for (size_t i = 0; i < r->nhdr; i++) { free(r->hdrs[i].name); free(r->hdrs[i].value); }
    free(r->hdrs);
    memset(r, 0, sizeof *r);
}

static int send_all(int fd, const char *p, size_t n) {
    while (n) {
        ssize_t w = send(fd, p, n, MSG_NOSIGNAL);
        if (w <= 0) return -1;
        p += w; n -= (size_t)w;
    }
    return 0;
}

static void *conn_thread(void *arg) {
    conn *c = arg;
    buf in; buf_init(&in);
    for (;;) {
        /* read until end of headers */
        char *hend = NULL;
        while (!(hend = in.p ? strstr(in.p, "\r\n\r\n") : NULL)) {
            if (in.n > MAX_HEAD) goto done;
            ssize_t n = read_more(c->fd, &in);
            if (n <= 0) goto done;
        }
        size_t hlen = (size_t)(hend - in.p) + 4;
        char *head = str_ndup(in.p, hlen);
        hreq req; memset(&req, 0, sizeof req);
        if (parse_headers(head, &req) < 0) { free(head); free_req(&req); goto done; }
        free(head);

        size_t want = 0;
        const char *cl = hreq_hdr(&req, "Content-Length");
        const char *te = hreq_hdr(&req, "Transfer-Encoding");
        if (cl) {
            char *end = NULL;
            long long v = strtoll(cl, &end, 10);
            if (v < 0 || (end && *end) || (unsigned long long)v > MAX_BODY) { free_req(&req); goto done; }
            want = (size_t)v;
        }
        size_t have = in.n - hlen;
        if (te && strstr(te, "chunked")) {
            /* de-chunk in place */
            buf body; buf_init(&body);
            size_t off = hlen;
            for (;;) {
                char *lf;
                while (!(lf = memchr(in.p + off, '\n', in.n - off))) {
                    ssize_t n = read_more(c->fd, &in);
                    if (n <= 0) { buf_free(&body); free_req(&req); goto done; }
                }
                size_t linelen = (size_t)(lf - (in.p + off)) + 1;
                unsigned long long sz = strtoull(in.p + off, NULL, 16);
                off += linelen;
                if (sz == 0) break;
                while (in.n - off < sz + 2) {
                    ssize_t n = read_more(c->fd, &in);
                    if (n <= 0) { buf_free(&body); free_req(&req); goto done; }
                }
                buf_put(&body, in.p + off, (size_t)sz);
                off += (size_t)sz + 2;
            }
            req.blen = body.n;
            req.body = buf_detach(&body);
            in.n = 0; if (in.p) in.p[0] = 0;
        } else {
            while (have < want) {
                ssize_t n = read_more(c->fd, &in);
                if (n <= 0) { free_req(&req); goto done; }
                have = in.n - hlen;
            }
            req.body = str_ndup(in.p + hlen, want);
            req.blen = want;
            size_t consumed = hlen + want;
            memmove(in.p, in.p + consumed, in.n - consumed);
            in.n -= consumed;
            in.p[in.n] = 0;
        }

        hresp res; hresp_init(&res);
        c->h(c->ctx, &req, &res);
        buf out; buf_init(&out);
        buf_printf(&out, "HTTP/1.1 %d %s\r\n", res.status, http_reason(res.status));
        if (res.hdrs.p) buf_put(&out, res.hdrs.p, res.hdrs.n);
        buf_printf(&out, "Content-Length: %zu\r\n", res.body.n);
        buf_printf(&out, "Connection: %s\r\n\r\n", req.keep_alive ? "keep-alive" : "close");
        if (res.body.n) buf_put(&out, res.body.p, res.body.n);
        int bad = send_all(c->fd, out.p, out.n);
        buf_free(&out);
        int ka = req.keep_alive;
        hresp_free(&res);
        free_req(&req);
        if (bad || !ka) break;
    }
done:
    buf_free(&in);
    shutdown(c->fd, SHUT_RDWR);
    close(c->fd);
    free(c);
    return NULL;
}

int http_serve(const char *addr, int port, http_handler h, void *ctx) {
    signal(SIGPIPE, SIG_IGN);
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("socket"); return -1; }
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    sa.sin_addr.s_addr = addr ? inet_addr(addr) : htonl(INADDR_ANY);
    if (bind(s, (struct sockaddr *)&sa, sizeof sa) < 0) { perror("bind"); return -1; }
    if (listen(s, 512) < 0) { perror("listen"); return -1; }
    for (;;) {
        int fd = accept(s, NULL, NULL);
        if (fd < 0) { if (errno == EINTR) continue; break; }
        int one2 = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one2, sizeof one2);
        conn *c = calloc(1, sizeof(conn));
        c->fd = fd; c->h = h; c->ctx = ctx;
        pthread_t t;
        if (pthread_create(&t, NULL, conn_thread, c) != 0) { close(fd); free(c); continue; }
        pthread_detach(t);
    }
    close(s);
    return 0;
}

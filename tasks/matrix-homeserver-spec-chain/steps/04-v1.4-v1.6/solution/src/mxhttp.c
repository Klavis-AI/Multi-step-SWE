/* mxhttp.c - a small threaded HTTP/1.1 server and a libcurl-based client.
 *
 * One detached thread per connection, so a handler that blocks (a Matrix
 * /sync long-poll waiting up to 30 s) never delays another connection.
 * Listening sockets are independent: a homeserver takes client traffic on a
 * plaintext port and federation traffic on a TLS port, sharing one route table.
 */
#define _GNU_SOURCE

#include "mxhttp.h"
#include "mxjson.h"
#include "mxutil.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <curl/curl.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#define MAX_CONNECTIONS   512
#define IDLE_TIMEOUT_MS   120000        /* keep-alive: wait for the next request */
#define REQ_TIMEOUT_MS    60000         /* reading one request once it started   */
#define WRITE_TIMEOUT_MS  60000
#define MAX_BODY_BYTES    (64u * 1024u * 1024u)
#define MAX_LINE_BYTES    8192u
#define MAX_HEADER_COUNT  200
#define MAX_HEADER_BYTES  (64u * 1024u)
#define READ_CHUNK        16384u
#define LISTEN_BACKLOG    128
#define CONN_STACK_BYTES  (1024u * 1024u)
#define CLIENT_TIMEOUT_S  30L

/* ------------------------------------------------------------- allocation */

static void *xalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) abort();
    return p;
}

static void *xzalloc(size_t n)
{
    void *p = calloc(1, n ? n : 1);
    if (!p) abort();
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) abort();
    return q;
}

static char *dupn(const char *s, size_t n)
{
    char *d = xalloc(n + 1);
    memcpy(d, s, n);
    d[n] = 0;
    return d;
}

/* Percent-decode.  `+` is left alone: Matrix query strings are not
 * application/x-www-form-urlencoded.  A string url_decode() cannot handle is
 * passed through verbatim rather than failing the request. */
static char *pct(const char *s, size_t n)
{
    char *d = url_decode(s, n, NULL);
    return d ? d : dupn(s, n);
}

/* Query strings are form-encoded in practice: every mainstream client encodes
 * a space as `+` there (Python's urlencode, curl --data-urlencode, browsers),
 * and a literal plus as %2B, so `+` decodes to a space. */
static char *pct_query(const char *s, size_t n)
{
    char *tmp = dupn(s, n);
    for (char *c = tmp; *c; c++)
        if (*c == '+') *c = ' ';
    char *out = pct(tmp, strlen(tmp));
    free(tmp);
    return out;
}

static void free_strv(char **v, size_t n)
{
    if (!v) return;
    for (size_t i = 0; i < n; i++) free(v[i]);
    free(v);
}

/* ------------------------------------------------------------ header text */

static bool is_tchar(unsigned char c)
{
    if (isalnum(c)) return true;
    return strchr("!#$%&'*+-.^_`|~", c) != NULL;
}

static bool is_token(const char *s)
{
    if (!*s) return false;
    for (const char *p = s; *p; p++)
        if (!is_tchar((unsigned char)*p)) return false;
    return true;
}

/* Case-insensitive search for `tok` in a comma-separated header value. */
static bool tok_in_list(const char *list, const char *tok)
{
    if (!list) return false;
    size_t tl = strlen(tok);
    const char *p = list;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) break;
        const char *e = p;
        while (*e && *e != ',') e++;
        const char *q = e;
        while (q > p && (q[-1] == ' ' || q[-1] == '\t')) q--;
        if ((size_t)(q - p) == tl && strncasecmp(p, tok, tl) == 0) return true;
        p = *e ? e + 1 : e;
    }
    return false;
}

static const char *status_text(int st)
{
    switch (st) {
    case 100: return "Continue";
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 204: return "No Content";
    case 206: return "Partial Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 307: return "Temporary Redirect";
    case 308: return "Permanent Redirect";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 411: return "Length Required";
    case 413: return "Payload Too Large";
    case 414: return "URI Too Long";
    case 415: return "Unsupported Media Type";
    case 429: return "Too Many Requests";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    default:  return st < 400 ? "OK" : "Error";
    }
}

static const char *const cors_header[3][2] = {
    { "Access-Control-Allow-Origin",  "*" },
    { "Access-Control-Allow-Methods", "GET, HEAD, POST, PUT, DELETE, OPTIONS" },
    { "Access-Control-Allow-Headers", "X-Requested-With, Content-Type, Authorization" },
};

/* --------------------------------------------------------------- routing */

typedef struct {
    char *method;              /* upper-cased at registration */
    char *pattern;
    char **seg;                /* pattern split on '/' */
    int nseg;
    int nliteral;              /* ranking key: more literal segments wins */
    http_handler fn;
} route;

typedef struct listener {
    struct http_server *s;
    int fd;
    SSL_CTX *ctx;
    pthread_t th;
    bool started;
    struct listener *next;
} listener;

struct http_server {
    void *userdata;
    route *routes;
    size_t nroutes, routes_cap;
    listener *listeners;
    pthread_mutex_t lock;      /* guards nconn and the listener list */
    int nconn;
    volatile sig_atomic_t stopping;
    int stop_pipe[2];          /* http_server_stop() is signal-handler safe */
};

/* Split on '/'; "/a/b" yields {"", "a", "b"}, "/a/" yields {"", "a", ""}.
 * The empty last piece is what lets a `:` segment match the empty state key
 * of e.g. /_matrix/client/v3/rooms/{id}/state/m.room.name/ . */
static char **split_slash(const char *p, int *out_n, size_t **out_off)
{
    int n = 1;
    for (const char *q = p; *q; q++)
        if (*q == '/') n++;
    char **v = xalloc((size_t)n * sizeof *v);
    size_t *off = out_off ? xalloc((size_t)n * sizeof *off) : NULL;
    const char *start = p;
    int i = 0;
    for (const char *q = p;; q++) {
        if (*q == '/' || *q == 0) {
            v[i] = dupn(start, (size_t)(q - start));
            if (off) off[i] = (size_t)(start - p);
            i++;
            if (*q == 0) break;
            start = q + 1;
        }
    }
    *out_n = n;
    if (out_off) *out_off = off;
    return v;
}

typedef struct {
    const char *raw;           /* the raw (still encoded) path */
    char **dec;                /* each segment, percent-decoded */
    size_t *off;               /* each segment's offset within `raw` */
    int n;
} path_split;

static void path_split_init(path_split *ps, const char *rawpath)
{
    char **raw = split_slash(rawpath, &ps->n, &ps->off);
    ps->raw = rawpath;
    ps->dec = xalloc((size_t)ps->n * sizeof *ps->dec);
    for (int i = 0; i < ps->n; i++) ps->dec[i] = pct(raw[i], strlen(raw[i]));
    free_strv(raw, (size_t)ps->n);
}

static void path_split_free(path_split *ps)
{
    free_strv(ps->dec, (size_t)ps->n);
    free(ps->off);
    ps->dec = NULL;
    ps->off = NULL;
    ps->n = 0;
}

static bool seg_is(const char *s, char c)
{
    return s[0] == c && s[1] == 0;
}

/* `parts` may be NULL to test a route without allocating its parameters. */
static bool route_try(const route *rt, const path_split *ps, char **parts, int *nparts)
{
    int np = 0;
    for (int i = 0; i < rt->nseg; i++) {
        const char *pat = rt->seg[i];
        if (i == rt->nseg - 1 && seg_is(pat, '*')) {
            if (np >= MAX_PATH_PARTS) goto fail;
            if (parts) {
                const char *rest = i < ps->n ? ps->raw + ps->off[i] : "";
                parts[np] = pct(rest, strlen(rest));
            }
            np++;
            *nparts = np;
            return true;
        }
        if (i >= ps->n) goto fail;
        if (seg_is(pat, ':')) {
            if (np >= MAX_PATH_PARTS) goto fail;
            if (parts) parts[np] = dupn(ps->dec[i], strlen(ps->dec[i]));
            np++;
            continue;
        }
        if (strcmp(pat, ps->dec[i]) != 0) goto fail;
    }
    if (ps->n != rt->nseg) goto fail;
    *nparts = np;
    return true;
fail:
    if (parts)
        for (int k = 0; k < np; k++) free(parts[k]);
    return false;
}

static const route *route_find(http_server *s, const char *method, const path_split *ps)
{
    const route *best = NULL;
    int best_lit = -1, np = 0;
    for (size_t i = 0; i < s->nroutes; i++) {
        const route *rt = &s->routes[i];
        if (strcmp(rt->method, method) != 0) continue;
        if (!route_try(rt, ps, NULL, &np)) continue;
        if (rt->nliteral > best_lit) { best = rt; best_lit = rt->nliteral; }
    }
    return best;
}


/* ------------------------------------------------------- connection I/O */

typedef struct {
    http_server *s;
    int fd;
    SSL *ssl;                  /* NULL on a plaintext listener */
    bool over_cap;             /* accepted past MAX_CONNECTIONS: 503 and close */
    char *rb;                  /* read buffer */
    size_t rpos, rlen, rcap;
} conn;

static bool conn_buffered(const conn *c)
{
    return c->rpos < c->rlen;
}

static bool wait_io(conn *c, short events, int timeout_ms)
{
    if ((events & POLLIN) && c->ssl && (SSL_pending(c->ssl) > 0 || SSL_has_pending(c->ssl)))
        return true;
    struct pollfd p;
    p.fd = c->fd;
    p.events = events;
    p.revents = 0;
    for (;;) {
        int r = poll(&p, 1, timeout_ms);
        if (r > 0) return true;
        if (r == 0) return false;
        if (errno == EINTR) continue;
        return false;
    }
}

/* > 0 bytes read, 0 on orderly close, -1 on error or timeout. */
static ssize_t conn_read(conn *c, void *dst, size_t n, int timeout_ms)
{
    if (n > INT_MAX) n = INT_MAX;
    for (;;) {
        if (c->ssl) {
            if (SSL_pending(c->ssl) <= 0 && !wait_io(c, POLLIN, timeout_ms)) return -1;
            int r = SSL_read(c->ssl, dst, (int)n);
            if (r > 0) return r;
            switch (SSL_get_error(c->ssl, r)) {
            case SSL_ERROR_WANT_READ:
                continue;
            case SSL_ERROR_WANT_WRITE:
                if (!wait_io(c, POLLOUT, timeout_ms)) return -1;
                continue;
            case SSL_ERROR_ZERO_RETURN:
                return 0;
            default:
                return -1;
            }
        }
        if (!wait_io(c, POLLIN, timeout_ms)) return -1;
        ssize_t r = read(c->fd, dst, n);
        if (r < 0 && errno == EINTR) continue;
        return r;
    }
}

static bool conn_write(conn *c, const void *src, size_t n)
{
    const char *p = src;
    size_t off = 0;
    while (off < n) {
        size_t want = n - off;
        if (want > INT_MAX) want = INT_MAX;
        if (c->ssl) {
            int r = SSL_write(c->ssl, p + off, (int)want);
            if (r > 0) { off += (size_t)r; continue; }
            switch (SSL_get_error(c->ssl, r)) {
            case SSL_ERROR_WANT_WRITE:
                if (!wait_io(c, POLLOUT, WRITE_TIMEOUT_MS)) return false;
                continue;
            case SSL_ERROR_WANT_READ:
                if (!wait_io(c, POLLIN, WRITE_TIMEOUT_MS)) return false;
                continue;
            default:
                return false;
            }
        }
        ssize_t r = send(c->fd, p + off, want, MSG_NOSIGNAL);
        if (r > 0) { off += (size_t)r; continue; }
        if (r < 0 && errno == EINTR) continue;
        if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (!wait_io(c, POLLOUT, WRITE_TIMEOUT_MS)) return false;
            continue;
        }
        return false;
    }
    return true;
}

static bool conn_fill(conn *c, int timeout_ms)
{
    if (c->rpos == c->rlen) {
        c->rpos = c->rlen = 0;
    } else if (c->rpos > 0) {
        memmove(c->rb, c->rb + c->rpos, c->rlen - c->rpos);
        c->rlen -= c->rpos;
        c->rpos = 0;
    }
    if (c->rcap - c->rlen < READ_CHUNK) {
        size_t cap = c->rcap ? c->rcap * 2 : READ_CHUNK * 2;
        while (cap - c->rlen < READ_CHUNK) cap *= 2;
        c->rb = xrealloc(c->rb, cap);
        c->rcap = cap;
    }
    ssize_t r = conn_read(c, c->rb + c->rlen, c->rcap - c->rlen, timeout_ms);
    if (r <= 0) return false;
    c->rlen += (size_t)r;
    return true;
}

#define LINE_OK     0
#define LINE_EOF   (-1)
#define LINE_LONG  (-2)

/* Reads one CRLF- (or LF-) terminated line, without the terminator. */
static int conn_line(conn *c, char **out, int timeout_ms)
{
    size_t scanned = 0;
    for (;;) {
        for (size_t i = c->rpos + scanned; i < c->rlen; i++) {
            if (c->rb[i] != '\n') continue;
            size_t end = i;
            if (end > c->rpos && c->rb[end - 1] == '\r') end--;
            *out = dupn(c->rb + c->rpos, end - c->rpos);
            c->rpos = i + 1;
            return LINE_OK;
        }
        scanned = c->rlen - c->rpos;
        if (scanned > MAX_LINE_BYTES) return LINE_LONG;
        if (!conn_fill(c, timeout_ms)) return LINE_EOF;
    }
}

static bool conn_read_exact(conn *c, buf *out, size_t n, int timeout_ms)
{
    while (n > 0) {
        if (conn_buffered(c)) {
            size_t avail = c->rlen - c->rpos;
            size_t take = avail < n ? avail : n;
            buf_add(out, c->rb + c->rpos, take);
            c->rpos += take;
            n -= take;
            continue;
        }
        char tmp[READ_CHUNK];
        size_t want = n < sizeof tmp ? n : sizeof tmp;
        ssize_t k = conn_read(c, tmp, want, timeout_ms);
        if (k <= 0) return false;
        buf_add(out, tmp, (size_t)k);
        n -= (size_t)k;
    }
    return true;
}

/* --------------------------------------------------- request / response */

typedef struct {
    http_req pub;              /* must stay first: handlers see &pub */
    char **qname, **qvalue;    /* query parameters, percent-decoded */
    size_t nquery;
} req_impl;

static void req_free(req_impl *ri)
{
    http_req *r = &ri->pub;
    free(r->method);
    free(r->target);
    free(r->path);
    free(r->query);
    free_strv(r->hname, r->nheaders);
    free_strv(r->hvalue, r->nheaders);
    free(r->body);
    for (int i = 0; i < r->nparts; i++) free(r->part[i]);
    j_free(r->json_body);
    free_strv(ri->qname, ri->nquery);
    free_strv(ri->qvalue, ri->nquery);
    memset(ri, 0, sizeof *ri);
}

static void resp_free(http_resp *w)
{
    buf_free(&w->body);
    free(w->content_type);
    free_strv(w->hname, w->nheaders);
    free_strv(w->hvalue, w->nheaders);
    memset(w, 0, sizeof *w);
}

static void pair_push(char ***names, char ***values, size_t *n, char *nm, char *val)
{
    *names = xrealloc(*names, (*n + 1) * sizeof **names);
    *values = xrealloc(*values, (*n + 1) * sizeof **values);
    (*names)[*n] = nm;
    (*values)[*n] = val;
    (*n)++;
}

const char *http_header(const http_req *r, const char *name)
{
    if (!r || !name) return NULL;
    for (size_t i = 0; i < r->nheaders; i++)
        if (strcasecmp(r->hname[i], name) == 0) return r->hvalue[i];
    return NULL;
}

static bool req_header_has_token(const http_req *r, const char *name, const char *tok)
{
    for (size_t i = 0; i < r->nheaders; i++)
        if (strcasecmp(r->hname[i], name) == 0 && tok_in_list(r->hvalue[i], tok))
            return true;
    return false;
}

/* The query is split once, when the request is built, so that http_query()
 * can hand out borrowed pointers from a const request. */
static void query_parse(req_impl *ri)
{
    const char *q = ri->pub.query;
    while (q && *q) {
        const char *amp = strchr(q, '&');
        size_t len = amp ? (size_t)(amp - q) : strlen(q);
        if (len > 0) {
            const char *eq = memchr(q, '=', len);
            size_t nlen = eq ? (size_t)(eq - q) : len;
            char *nm = pct_query(q, nlen);
            char *val = eq ? pct_query(eq + 1, len - nlen - 1) : dupn("", 0);
            pair_push(&ri->qname, &ri->qvalue, &ri->nquery, nm, val);
        }
        q = amp ? amp + 1 : q + len;
    }
}

const char *http_query(const http_req *r, const char *name)
{
    if (!r || !name) return NULL;
    const req_impl *ri = (const req_impl *)r;
    for (size_t i = 0; i < ri->nquery; i++)
        if (strcmp(ri->qname[i], name) == 0) return ri->qvalue[i];
    return NULL;
}

int http_query_all(const http_req *r, const char *name, const char **out, int max)
{
    if (!r || !name || !out || max <= 0) return 0;
    const req_impl *ri = (const req_impl *)r;
    int n = 0;
    for (size_t i = 0; i < ri->nquery && n < max; i++)
        if (strcmp(ri->qname[i], name) == 0) out[n++] = ri->qvalue[i];
    return n;
}

json *http_json(http_req *r)
{
    if (!r) return NULL;
    if (r->json_body) return r->json_body;
    if (r->json_bad) return NULL;
    if (!r->body || r->body_len == 0) return NULL;
    json *v = j_parse(r->body, r->body_len, NULL);
    if (!v) {
        r->json_bad = true;
        return NULL;
    }
    r->json_body = v;
    return v;
}

/* A header value may not carry CR or LF: that would split the response. */
static char *sanitise(const char *s)
{
    char *d = dupn(s, strlen(s));
    for (char *p = d; *p; p++)
        if (*p == '\r' || *p == '\n') *p = ' ';
    return d;
}

void resp_set_header(http_resp *w, const char *name, const char *value)
{
    if (!w || !name || !value) return;
    for (size_t i = 0; i < w->nheaders; i++) {
        if (strcasecmp(w->hname[i], name) != 0) continue;
        free(w->hvalue[i]);
        w->hvalue[i] = sanitise(value);
        return;
    }
    pair_push(&w->hname, &w->hvalue, &w->nheaders, sanitise(name), sanitise(value));
}

static bool resp_has_header(const http_resp *w, const char *name)
{
    for (size_t i = 0; i < w->nheaders; i++)
        if (strcasecmp(w->hname[i], name) == 0) return true;
    return false;
}

void resp_bytes(http_resp *w, int status, const void *p, size_t n, const char *ctype)
{
    if (!w) return;
    w->status = status;
    buf_free(&w->body);
    memset(&w->body, 0, sizeof w->body);
    if (p && n) buf_add(&w->body, p, n);
    free(w->content_type);
    w->content_type = ctype ? sanitise(ctype) : NULL;
}

void resp_json(http_resp *w, int status, json *body)
{
    if (!w) {
        j_free(body);
        return;
    }
    size_t n = 0;
    char *text = body ? j_dump(body, &n) : NULL;
    if (!text) {                    /* no body, or a value j_dump cannot emit */
        text = dupn("{}", 2);
        n = 2;
    }
    resp_bytes(w, status, text, n, "application/json");
    free(text);
    j_free(body);
}

void resp_error(http_resp *w, int status, const char *errcode, const char *msg)
{
    json *o = j_obj();
    j_set(o, "errcode", j_str(errcode ? errcode : "M_UNKNOWN"));
    j_set(o, "error", j_str(msg ? msg : ""));
    resp_json(w, status, o);
}

/* ------------------------------------------------------ serving requests */

static bool send_response(conn *c, http_resp *w, bool with_body, bool close_after)
{
    buf h;
    memset(&h, 0, sizeof h);
    buf_printf(&h, "HTTP/1.1 %d %s\r\n", w->status, status_text(w->status));
    for (size_t i = 0; i < w->nheaders; i++) {
        /* framing and connection handling belong to the server */
        if (strcasecmp(w->hname[i], "content-length") == 0) continue;
        if (strcasecmp(w->hname[i], "connection") == 0) continue;
        buf_puts(&h, w->hname[i]);
        buf_puts(&h, ": ");
        buf_puts(&h, w->hvalue[i]);
        buf_puts(&h, "\r\n");
    }
    if (w->content_type && !resp_has_header(w, "content-type")) {
        buf_puts(&h, "Content-Type: ");
        buf_puts(&h, w->content_type);
        buf_puts(&h, "\r\n");
    }
    buf_printf(&h, "Content-Length: %zu\r\n", w->body.n);
    for (size_t i = 0; i < sizeof cors_header / sizeof cors_header[0]; i++) {
        if (resp_has_header(w, cors_header[i][0])) continue;
        buf_puts(&h, cors_header[i][0]);
        buf_puts(&h, ": ");
        buf_puts(&h, cors_header[i][1]);
        buf_puts(&h, "\r\n");
    }
    buf_puts(&h, close_after ? "Connection: close\r\n" : "Connection: keep-alive\r\n");
    buf_puts(&h, "\r\n");

    /* Send the head and a small body as one segment; a large body (media) is
     * written separately so it is not copied again. */
    bool send_body = with_body && w->body.n > 0;
    if (send_body && w->body.n <= 65536) {
        buf_add(&h, w->body.b, w->body.n);
        send_body = false;
    }
    bool ok = conn_write(c, h.b, h.n);
    buf_free(&h);
    if (ok && send_body) ok = conn_write(c, w->body.b, w->body.n);
    return ok;
}

static void send_error_now(conn *c, int status, const char *errcode, const char *msg)
{
    http_resp w;
    memset(&w, 0, sizeof w);
    resp_error(&w, status, errcode, msg);
    send_response(c, &w, true, true);
    resp_free(&w);
}

typedef enum { BODY_OK, BODY_BAD, BODY_TOO_LARGE, BODY_IO } body_result;

static bool parse_u64(const char *s, unsigned long long cap, unsigned long long *out)
{
    if (!*s) return false;
    unsigned long long n = 0;
    for (const char *p = s; *p; p++) {
        if (!isdigit((unsigned char)*p)) return false;
        if (n <= cap) n = n * 10 + (unsigned long long)(*p - '0');
        if (n > cap) n = cap + 1;          /* clamp: only the limit matters */
    }
    *out = n;
    return true;
}

static body_result read_chunked_body(conn *c, buf *out)
{
    size_t total = 0;
    for (;;) {
        char *line = NULL;
        int rc = conn_line(c, &line, REQ_TIMEOUT_MS);
        if (rc == LINE_LONG) return BODY_BAD;
        if (rc != LINE_OK) return BODY_IO;
        char *semi = strchr(line, ';');    /* chunk extensions are ignored */
        if (semi) *semi = 0;
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        char *e = p;
        while (isxdigit((unsigned char)*e)) e++;
        bool empty = (e == p);
        char *tail = e;
        while (*tail == ' ' || *tail == '\t') tail++;
        unsigned long long sz = empty ? 0 : strtoull(p, NULL, 16);
        bool bad = empty || *tail != 0;
        free(line);
        if (bad) return BODY_BAD;
        if (sz == 0) break;
        if (sz > MAX_BODY_BYTES || (unsigned long long)total + sz > MAX_BODY_BYTES)
            return BODY_TOO_LARGE;
        if (!conn_read_exact(c, out, (size_t)sz, REQ_TIMEOUT_MS)) return BODY_IO;
        total += (size_t)sz;
        char *crlf = NULL;
        rc = conn_line(c, &crlf, REQ_TIMEOUT_MS);
        if (rc != LINE_OK) return rc == LINE_LONG ? BODY_BAD : BODY_IO;
        bad = (*crlf != 0);
        free(crlf);
        if (bad) return BODY_BAD;
    }
    for (;;) {                             /* trailer section, discarded */
        char *line = NULL;
        int rc = conn_line(c, &line, REQ_TIMEOUT_MS);
        if (rc == LINE_LONG) return BODY_BAD;
        if (rc != LINE_OK) return BODY_IO;
        bool done = (*line == 0);
        free(line);
        if (done) return BODY_OK;
    }
}

/* Only promise to read a body once the framing has been accepted: a request
 * that is about to be refused gets its final status instead of a 100. */
static bool send_continue(conn *c, const http_req *r, bool http11)
{
    if (!http11 || !req_header_has_token(r, "expect", "100-continue")) return true;
    static const char cont[] = "HTTP/1.1 100 Continue\r\n\r\n";
    return conn_write(c, cont, sizeof cont - 1);
}

static body_result read_request_body(conn *c, http_req *r, bool http11)
{
    const char *te = http_header(r, "transfer-encoding");
    const char *cl = http_header(r, "content-length");
    if (!te && !cl) return BODY_OK;

    buf b;
    memset(&b, 0, sizeof b);
    body_result res;
    if (te) {
        /* A body framed twice is a request-smuggling hazard: refuse it. */
        if (strcasecmp(te, "chunked") != 0 || cl) return BODY_BAD;
        if (!send_continue(c, r, http11)) return BODY_IO;
        res = read_chunked_body(c, &b);
    } else {
        unsigned long long len = 0, first = 0;
        bool seen = false;
        for (size_t i = 0; i < r->nheaders; i++) {
            if (strcasecmp(r->hname[i], "content-length") != 0) continue;
            if (!parse_u64(r->hvalue[i], MAX_BODY_BYTES, &len)) return BODY_BAD;
            if (seen && len != first) return BODY_BAD;   /* conflicting framing */
            first = len;
            seen = true;
        }
        if (len > MAX_BODY_BYTES) return BODY_TOO_LARGE;
        if (len == 0) return BODY_OK;
        if (!send_continue(c, r, http11)) return BODY_IO;
        res = conn_read_exact(c, &b, (size_t)len, REQ_TIMEOUT_MS) ? BODY_OK : BODY_IO;
    }
    if (res != BODY_OK) {
        buf_free(&b);
        return res;
    }
    size_t n = 0;
    char *body = buf_take(&b, &n);
    memset(&b, 0, sizeof b);
    r->body = body ? body : dupn("", 0);
    r->body_len = body ? n : 0;
    return BODY_OK;
}

/* Returns true when the connection may be reused for another request. */
static bool handle_one(conn *c)
{
    http_server *s = c->s;
    req_impl ri;
    http_resp w;
    memset(&ri, 0, sizeof ri);
    memset(&w, 0, sizeof w);
    w.status = 200;
    http_req *r = &ri.pub;
    r->server = s->userdata;

    char *line = NULL, *rawpath = NULL;
    bool keep = false, head = false, http11 = true;
    int bad = 0;
    const char *bad_msg = "Bad request";

    /* Waiting for the first byte of a request is the idle keep-alive window;
     * everything after it is bounded by the request-read timeout. */
    if (!conn_buffered(c) && !wait_io(c, POLLIN, IDLE_TIMEOUT_MS)) goto done;

    for (int skipped = 0;; skipped++) {         /* tolerate a stray leading CRLF */
        int rc = conn_line(c, &line, REQ_TIMEOUT_MS);
        if (rc == LINE_LONG) { bad = 400; bad_msg = "Request line too long"; goto respond; }
        if (rc != LINE_OK) goto done;
        if (*line) break;
        free(line);
        line = NULL;
        if (skipped >= 2) { bad = 400; bad_msg = "Malformed request line"; goto respond; }
    }

    {
        char *sp1 = strchr(line, ' ');
        char *sp2 = sp1 ? strchr(sp1 + 1, ' ') : NULL;
        if (!sp1 || !sp2 || strchr(sp2 + 1, ' ')) {
            bad = 400;
            bad_msg = "Malformed request line";
            goto respond;
        }
        *sp1 = 0;
        *sp2 = 0;
        const char *method = line, *target = sp1 + 1, *version = sp2 + 1;
        if (!is_token(method) || !*target) {
            bad = 400;
            bad_msg = "Malformed request line";
            goto respond;
        }
        if (strcmp(version, "HTTP/1.1") == 0) {
            http11 = true;
        } else if (strcmp(version, "HTTP/1.0") == 0) {
            http11 = false;
        } else {
            bad = 400;
            bad_msg = "Unsupported HTTP version";
            goto respond;
        }
        r->method = dupn(method, strlen(method));
        r->target = dupn(target, strlen(target));
        head = strcmp(r->method, "HEAD") == 0;

        const char *t = target;
        if (strncasecmp(t, "http://", 7) == 0) t += 7;
        else if (strncasecmp(t, "https://", 8) == 0) t += 8;
        if (t != target) {                      /* absolute-form: drop the authority */
            const char *slash = strchr(t, '/');
            t = slash ? slash : "/";
        }
        if (strcmp(t, "*") != 0 && *t != '/') {
            bad = 400;
            bad_msg = "Malformed request target";
            goto respond;
        }
        const char *q = strchr(t, '?');
        rawpath = dupn(t, q ? (size_t)(q - t) : strlen(t));
        r->query = q ? dupn(q + 1, strlen(q + 1)) : NULL;
        r->path = pct(rawpath, strlen(rawpath));
        query_parse(&ri);
    }
    free(line);
    line = NULL;

    for (size_t total = 0;;) {
        int rc = conn_line(c, &line, REQ_TIMEOUT_MS);
        if (rc == LINE_LONG) { bad = 400; bad_msg = "Header line too long"; goto respond; }
        if (rc != LINE_OK) goto done;
        if (!*line) break;
        total += strlen(line) + 2;
        if (total > MAX_HEADER_BYTES || r->nheaders >= MAX_HEADER_COUNT) {
            bad = 400;
            bad_msg = "Too many header bytes";
            goto respond;
        }
        if (*line == ' ' || *line == '\t') {    /* obsolete line folding */
            bad = 400;
            bad_msg = "Malformed header";
            goto respond;
        }
        char *colon = strchr(line, ':');
        if (!colon || colon == line) {
            bad = 400;
            bad_msg = "Malformed header";
            goto respond;
        }
        *colon = 0;
        if (!is_token(line)) {
            bad = 400;
            bad_msg = "Malformed header";
            goto respond;
        }
        char *name = line;
        for (char *p = name; *p; p++) *p = (char)tolower((unsigned char)*p);
        char *val = colon + 1;
        while (*val == ' ' || *val == '\t') val++;
        size_t vlen = strlen(val);
        while (vlen && (val[vlen - 1] == ' ' || val[vlen - 1] == '\t')) vlen--;
        pair_push(&r->hname, &r->hvalue, &r->nheaders,
                  dupn(name, strlen(name)), dupn(val, vlen));
        free(line);
        line = NULL;
    }
    free(line);
    line = NULL;

    keep = http11 ? !req_header_has_token(r, "connection", "close")
                  : req_header_has_token(r, "connection", "keep-alive");

    {
        switch (read_request_body(c, r, http11)) {
        case BODY_OK:
            break;
        case BODY_BAD:
            bad = 400;
            bad_msg = "Malformed request body";
            goto respond;
        case BODY_TOO_LARGE:
            bad = 413;
            bad_msg = "Request body too large";
            goto respond;
        case BODY_IO:
            keep = false;
            goto done;
        }
    }

    if (strcmp(r->method, "OPTIONS") == 0) {    /* CORS preflight, no handler */
        resp_bytes(&w, 200, NULL, 0, NULL);
        goto respond;
    }

    {
        path_split ps;
        path_split_init(&ps, rawpath);
        const route *rt = route_find(s, r->method, &ps);
        if (!rt && head) rt = route_find(s, "GET", &ps);
        if (rt) {
            route_try(rt, &ps, r->part, &r->nparts);
            path_split_free(&ps);
            rt->fn(r, &w, s->userdata);
            if (w.status < 100 || w.status > 599) w.status = 200;
        } else {
            path_split_free(&ps);
            resp_error(&w, 404, "M_UNRECOGNIZED", "Unrecognized request");
        }
    }

respond:
    if (bad) {
        keep = false;
        resp_error(&w, bad, bad == 413 ? "M_TOO_LARGE" : "M_UNRECOGNIZED", bad_msg);
    }
    if (!send_response(c, &w, !head, !keep)) keep = false;

done:
    free(line);
    free(rawpath);
    req_free(&ri);
    resp_free(&w);
    return keep;
}

/* -------------------------------------------------------- server lifecycle */

/* Writing to a socket whose peer is gone must not kill the process.  Only
 * installed when the application has expressed no opinion of its own. */
static pthread_once_t sigpipe_once = PTHREAD_ONCE_INIT;

static void ignore_sigpipe(void)
{
    struct sigaction cur;
    if (sigaction(SIGPIPE, NULL, &cur) != 0) return;
    if (cur.sa_handler != SIG_DFL) return;
    struct sigaction ign;
    memset(&ign, 0, sizeof ign);
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    sigaction(SIGPIPE, &ign, NULL);
}

static void *conn_thread(void *arg)
{
    conn *c = arg;
    http_server *s = c->s;

    if (c->ssl) {
        SSL_set_fd(c->ssl, c->fd);
        if (SSL_accept(c->ssl) != 1) {
            ERR_clear_error();
            goto finish;
        }
    }
    if (c->over_cap) {
        send_error_now(c, 503, "M_UNKNOWN", "Too many connections");
        goto finish;
    }
    while (!s->stopping && handle_one(c))
        ;

finish:
    if (c->ssl) {
        SSL_shutdown(c->ssl);
        SSL_free(c->ssl);
    }
    close(c->fd);
    pthread_mutex_lock(&s->lock);
    s->nconn--;
    pthread_mutex_unlock(&s->lock);
    free(c->rb);
    free(c);
    return NULL;
}

static void set_conn_sockopts(int fd)
{
    int one = 1;
    struct timeval tv;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    /* A backstop only: every read and write is preceded by poll(). */
    tv.tv_sec = REQ_TIMEOUT_MS / 1000;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
}

static void *accept_thread(void *arg)
{
    listener *l = arg;
    http_server *s = l->s;
    pthread_attr_t attr;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attr, CONN_STACK_BYTES);

    while (!s->stopping) {
        struct pollfd p;
        p.fd = l->fd;
        p.events = POLLIN;
        p.revents = 0;
        int r = poll(&p, 1, 200);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (r == 0) continue;

        int fd = accept4(l->fd, NULL, NULL, SOCK_CLOEXEC);
        if (fd < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK ||
                errno == ECONNABORTED || errno == EPROTO)
                continue;
            if (errno == EMFILE || errno == ENFILE) {
                struct timespec ts = { 0, 50 * 1000 * 1000 };
                nanosleep(&ts, NULL);
                continue;
            }
            break;
        }
        set_conn_sockopts(fd);

        conn *c = xzalloc(sizeof *c);
        c->s = s;
        c->fd = fd;
        if (l->ctx) {
            c->ssl = SSL_new(l->ctx);   /* holds a reference on the context */
            if (!c->ssl) {
                close(fd);
                free(c);
                continue;
            }
        }
        pthread_mutex_lock(&s->lock);
        s->nconn++;
        c->over_cap = s->nconn > MAX_CONNECTIONS;
        pthread_mutex_unlock(&s->lock);

        pthread_t th;
        if (pthread_create(&th, &attr, conn_thread, c) != 0) {
            pthread_mutex_lock(&s->lock);
            s->nconn--;
            pthread_mutex_unlock(&s->lock);
            if (c->ssl) SSL_free(c->ssl);
            close(fd);
            free(c);
        }
    }

    pthread_attr_destroy(&attr);
    close(l->fd);
    l->fd = -1;
    if (l->ctx) {
        SSL_CTX_free(l->ctx);
        l->ctx = NULL;
    }
    return NULL;
}

http_server *http_server_new(void *userdata)
{
    pthread_once(&sigpipe_once, ignore_sigpipe);
    http_server *s = xzalloc(sizeof *s);
    s->userdata = userdata;
    pthread_mutex_init(&s->lock, NULL);
    if (pipe2(s->stop_pipe, O_CLOEXEC | O_NONBLOCK) != 0) {
        s->stop_pipe[0] = s->stop_pipe[1] = -1;
    }
    return s;
}

void *http_server_userdata(http_server *s)
{
    return s ? s->userdata : NULL;
}

void http_route(http_server *s, const char *method, const char *pattern, http_handler fn)
{
    if (!s || !method || !pattern || !fn) return;
    if (s->nroutes == s->routes_cap) {
        s->routes_cap = s->routes_cap ? s->routes_cap * 2 : 32;
        s->routes = xrealloc(s->routes, s->routes_cap * sizeof *s->routes);
    }
    route *rt = &s->routes[s->nroutes];
    memset(rt, 0, sizeof *rt);
    rt->method = dupn(method, strlen(method));
    for (char *p = rt->method; *p; p++) *p = (char)toupper((unsigned char)*p);
    rt->pattern = dupn(pattern, strlen(pattern));
    rt->seg = split_slash(pattern, &rt->nseg, NULL);
    rt->fn = fn;
    for (int i = 0; i < rt->nseg; i++) {
        bool wild = (i == rt->nseg - 1) && seg_is(rt->seg[i], '*');
        if (!wild && !seg_is(rt->seg[i], ':')) rt->nliteral++;
    }
    s->nroutes++;
}

bool http_listen(http_server *s, const char *host, int port,
                 const char *tls_cert, const char *tls_key)
{
    if (!s || port < 0 || port > 65535) return false;

    char portstr[16];
    snprintf(portstr, sizeof portstr, "%d", port);
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    const char *node = (host && *host && strcmp(host, "*") != 0) ? host : NULL;
    int rc = getaddrinfo(node, portstr, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "http: getaddrinfo(%s:%d): %s\n",
                node ? node : "*", port, gai_strerror(rc));
        return false;
    }

    int fd = -1;
    /* Prefer AF_INET6 with V6ONLY off, so one wildcard socket serves both
     * families; fall back to whatever else the resolver offered. */
    for (int pass = 0; pass < 2 && fd < 0; pass++) {
        for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
            if ((pass == 0) != (ai->ai_family == AF_INET6)) continue;
            fd = socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC | SOCK_NONBLOCK,
                        ai->ai_protocol);
            if (fd < 0) continue;
            int one = 1;
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
            if (ai->ai_family == AF_INET6) {
                int off = 0;
                setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof off);
            }
            if (bind(fd, ai->ai_addr, ai->ai_addrlen) == 0 &&
                listen(fd, LISTEN_BACKLOG) == 0)
                break;
            close(fd);
            fd = -1;
        }
    }
    freeaddrinfo(res);
    if (fd < 0) {
        fprintf(stderr, "http: cannot listen on %s:%d: %s\n",
                node ? node : "*", port, strerror(errno));
        return false;
    }

    SSL_CTX *ctx = NULL;
    if (tls_cert && tls_key) {
        ctx = SSL_CTX_new(TLS_server_method());
        bool ok = ctx != NULL;
        if (ok) {
            SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
            SSL_CTX_set_mode(ctx, SSL_MODE_AUTO_RETRY);
            ok = SSL_CTX_use_certificate_chain_file(ctx, tls_cert) == 1 &&
                 SSL_CTX_use_PrivateKey_file(ctx, tls_key, SSL_FILETYPE_PEM) == 1 &&
                 SSL_CTX_check_private_key(ctx) == 1;
        }
        if (!ok) {
            fprintf(stderr, "http: TLS setup failed for %s / %s\n", tls_cert, tls_key);
            ERR_print_errors_fp(stderr);
            if (ctx) SSL_CTX_free(ctx);
            close(fd);
            return false;
        }
    }

    listener *l = xzalloc(sizeof *l);
    l->s = s;
    l->fd = fd;
    l->ctx = ctx;
    pthread_mutex_lock(&s->lock);
    l->next = s->listeners;
    s->listeners = l;
    pthread_mutex_unlock(&s->lock);

    if (pthread_create(&l->th, NULL, accept_thread, l) != 0) {
        fprintf(stderr, "http: cannot start accept thread: %s\n", strerror(errno));
        pthread_mutex_lock(&s->lock);
        s->listeners = l->next;
        pthread_mutex_unlock(&s->lock);
        if (ctx) SSL_CTX_free(ctx);
        close(fd);
        free(l);
        return false;
    }
    l->started = true;
    return true;
}

void http_server_run(http_server *s)
{
    if (!s) return;
    while (!s->stopping) {
        if (s->stop_pipe[0] < 0) {
            struct timespec ts = { 0, 100 * 1000 * 1000 };
            nanosleep(&ts, NULL);
            continue;
        }
        struct pollfd p;
        p.fd = s->stop_pipe[0];
        p.events = POLLIN;
        p.revents = 0;
        int r = poll(&p, 1, 1000);
        if (r < 0 && errno != EINTR) break;
        if (r > 0) {
            char drain[64];
            while (read(s->stop_pipe[0], drain, sizeof drain) > 0)
                ;
        }
    }
    for (listener *l = s->listeners; l; l = l->next)
        if (l->started) {
            pthread_join(l->th, NULL);
            l->started = false;
        }
}

void http_server_stop(http_server *s)
{
    if (!s) return;
    s->stopping = 1;
    if (s->stop_pipe[1] >= 0) {
        ssize_t n = write(s->stop_pipe[1], "x", 1);   /* signal-handler safe */
        (void)n;
    }
}

/* -------------------------------------------------------- outbound client */

/* libcurl insists on a one-time process-wide initialisation before any easy
 * handle exists; pthread_once keeps that safe from several request threads. */
static pthread_once_t curl_once = PTHREAD_ONCE_INIT;

static void curl_init_once(void)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

static size_t curl_sink(char *p, size_t sz, size_t nm, void *ud)
{
    size_t n = sz * nm;
    buf_add((buf *)ud, p, n);
    return n;
}

http_client_resp *http_send(const char *method, const char *url,
                            const void *body, size_t body_len,
                            const char **headers, int nheaders)
{
    if (!method || !url) return NULL;
    pthread_once(&curl_once, curl_init_once);

    CURL *h = curl_easy_init();
    if (!h) return NULL;

    buf out;
    memset(&out, 0, sizeof out);
    struct curl_slist *hl = NULL;
    for (int i = 0; headers && i < nheaders; i++)
        if (headers[i]) hl = curl_slist_append(hl, headers[i]);

    curl_easy_setopt(h, CURLOPT_URL, url);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);            /* thread safety */
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, CLIENT_TIMEOUT_S);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 10L);
    /* federation_api.disable_tls_validation: homeservers are reached over
     * self-signed certificates in this deployment. */
    curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(h, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, curl_sink);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &out);
    if (hl) curl_easy_setopt(h, CURLOPT_HTTPHEADER, hl);

    bool is_head = strcmp(method, "HEAD") == 0;
    if (is_head) curl_easy_setopt(h, CURLOPT_NOBODY, 1L);
    if (body && body_len) {
        curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)body_len);
        curl_easy_setopt(h, CURLOPT_COPYPOSTFIELDS, body);
    } else if (!is_head && (strcmp(method, "POST") == 0 || strcmp(method, "PUT") == 0)) {
        curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)0);
        curl_easy_setopt(h, CURLOPT_COPYPOSTFIELDS, "");
    }
    /* CUSTOMREQUEST overrides the verb that POSTFIELDS would otherwise imply */
    if (!is_head && (strcmp(method, "GET") != 0 || (body && body_len)))
        curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST, method);

    CURLcode rc = curl_easy_perform(h);
    http_client_resp *resp = NULL;
    if (rc == CURLE_OK) {
        long status = 0;
        char *ctype = NULL;
        curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
        curl_easy_getinfo(h, CURLINFO_CONTENT_TYPE, &ctype);
        resp = xzalloc(sizeof *resp);
        resp->status = (int)status;
        size_t n = 0;
        char *b = buf_take(&out, &n);
        memset(&out, 0, sizeof out);
        resp->body = b ? b : dupn("", 0);
        resp->body_len = b ? n : 0;
        resp->content_type = ctype ? dupn(ctype, strlen(ctype)) : NULL;
    } else {
        fprintf(stderr, "http: %s %s: %s\n", method, url, curl_easy_strerror(rc));
        buf_free(&out);
    }

    if (hl) curl_slist_free_all(hl);
    curl_easy_cleanup(h);
    return resp;
}

http_client_resp *http_get(const char *url, const char **headers, int nheaders)
{
    return http_send("GET", url, NULL, 0, headers, nheaders);
}

void http_client_resp_free(http_client_resp *r)
{
    if (!r) return;
    free(r->body);
    free(r->content_type);
    free(r);
}

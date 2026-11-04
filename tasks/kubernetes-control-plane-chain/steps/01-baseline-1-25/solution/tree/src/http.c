#define _GNU_SOURCE
#include "http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <openssl/err.h>

struct hconn {
    int fd; SSL *ssl;
    int streaming;
    int dead;
};

static int cio_read(hconn *c, void *p, int n){
    return c->ssl ? SSL_read(c->ssl, p, n) : (int)read(c->fd, p, n);
}
static int cio_write(hconn *c, const void *p, int n){
    int off = 0;
    while(off < n){
        int w = c->ssl ? SSL_write(c->ssl, (const char*)p+off, n-off)
                       : (int)write(c->fd, (const char*)p+off, n-off);
        if(w <= 0){ c->dead = 1; return -1; }
        off += w;
    }
    return off;
}

const char *hreq_header(const hreq *r, const char *name){
    for(int i=0;i<r->nhdr;i++) if(strcasecmp(r->hdr[i].name, name)==0) return r->hdr[i].value;
    return NULL;
}
const char *hreq_query(const hreq *r, const char *name){
    for(int i=0;i<r->nqp;i++) if(strcmp(r->qp_names[i], name)==0) return r->qp_vals[i];
    return NULL;
}

static const char *reason_of(int s){
    switch(s){
    case 200: return "OK"; case 201: return "Created"; case 202: return "Accepted";
    case 204: return "No Content"; case 304: return "Not Modified";
    case 400: return "Bad Request"; case 401: return "Unauthorized";
    case 403: return "Forbidden"; case 404: return "Not Found";
    case 405: return "Method Not Allowed"; case 406: return "Not Acceptable";
    case 409: return "Conflict"; case 410: return "Gone";
    case 413: return "Request Entity Too Large";
    case 415: return "Unsupported Media Type";
    case 422: return "Unprocessable Entity"; case 429: return "Too Many Requests";
    case 500: return "Internal Server Error"; case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    default: return "Status";
    }
}

void h_reply(hconn *c, int status, const char *ctype, const char *body, size_t n){
    buf b = {0};
    buf_printf(&b, "HTTP/1.1 %d %s\r\n", status, reason_of(status));
    buf_printf(&b, "Content-Type: %s\r\n", ctype ? ctype : "application/json");
    buf_printf(&b, "Content-Length: %zu\r\n", n);
    buf_str(&b, "Cache-Control: no-cache, private\r\n");
    buf_str(&b, "Connection: close\r\n\r\n");
    if(n) buf_add(&b, body, n);
    cio_write(c, b.p, (int)b.n);
    buf_free(&b);
}
void h_reply_json(hconn *c, int status, const char *t){ h_reply(c, status, "application/json", t, strlen(t)); }

void h_stream_begin(hconn *c, const char *ctype){
    buf b = {0};
    buf_str(&b, "HTTP/1.1 200 OK\r\n");
    buf_printf(&b, "Content-Type: %s\r\n", ctype);
    buf_str(&b, "Cache-Control: no-cache, private\r\n");
    buf_str(&b, "Transfer-Encoding: chunked\r\n\r\n");
    cio_write(c, b.p, (int)b.n);
    buf_free(&b);
    c->streaming = 1;
}
int h_stream_write(hconn *c, const char *d, size_t n){
    char head[32];
    int hn = snprintf(head, sizeof head, "%zx\r\n", n);
    if(cio_write(c, head, hn) < 0) return -1;
    if(cio_write(c, d, (int)n) < 0) return -1;
    if(cio_write(c, "\r\n", 2) < 0) return -1;
    return 0;
}
void h_stream_end(hconn *c){ cio_write(c, "0\r\n\r\n", 5); }
int h_conn_alive(hconn *c){ return !c->dead; }

/* ------------------------- server ------------------------- */
typedef struct { hconn *c; http_handler h; void *ud; } cargs;

static void free_req(hreq *r){
    free(r->method); free(r->target); free(r->path); free(r->query);
    for(int i=0;i<r->nhdr;i++){ free(r->hdr[i].name); free(r->hdr[i].value); }
    free(r->hdr); free(r->body);
    for(int i=0;i<r->nqp;i++){ free(r->qp_names[i]); free(r->qp_vals[i]); }
}

static void parse_query(hreq *r){
    if(!r->query) return;
    char *q = r->query;
    while(*q && r->nqp < 64){
        char *amp = strchr(q, '&');
        size_t seg = amp ? (size_t)(amp - q) : strlen(q);
        char *eq = memchr(q, '=', seg);
        if(eq){
            r->qp_names[r->nqp] = url_decode(q, (size_t)(eq - q));
            r->qp_vals[r->nqp] = url_decode(eq+1, seg - (size_t)(eq - q) - 1);
        } else {
            r->qp_names[r->nqp] = url_decode(q, seg);
            r->qp_vals[r->nqp] = xstrdup2("");
        }
        r->nqp++;
        if(!amp) break;
        q = amp + 1;
    }
}

static void *conn_thread(void *v){
    cargs *a = v;
    hconn *c = a->c;
    buf in = {0};
    char tmp[16384];
    /* read headers */
    size_t hdr_end = 0;
    for(;;){
        char *p = in.p ? strstr(in.p, "\r\n\r\n") : NULL;
        if(p){ hdr_end = (size_t)(p - in.p) + 4; break; }
        int n = cio_read(c, tmp, sizeof tmp);
        if(n <= 0) goto done;
        buf_add(&in, tmp, (size_t)n);
        if(in.n > 8u*1024*1024) goto done;
    }
    {
        hreq r = {0};
        char *hdrs = malloc(hdr_end+1); memcpy(hdrs, in.p, hdr_end); hdrs[hdr_end]=0;
        char *save=NULL;
        char *line = strtok_r(hdrs, "\r\n", &save);
        if(!line){ free(hdrs); goto done; }
        char *sp1 = strchr(line, ' ');
        if(!sp1){ free(hdrs); goto done; }
        *sp1 = 0;
        char *sp2 = strchr(sp1+1, ' ');
        if(sp2) *sp2 = 0;
        r.method = xstrdup2(line);
        r.target = xstrdup2(sp1+1);
        char *qm = strchr(r.target, '?');
        if(qm){ r.path = url_decode(r.target, (size_t)(qm - r.target)); r.query = xstrdup2(qm+1); }
        else   { r.path = url_decode(r.target, strlen(r.target)); r.query = NULL; }
        parse_query(&r);
        int cap = 32; r.hdr = calloc((size_t)cap, sizeof(hhdr));
        while((line = strtok_r(NULL, "\r\n", &save))){
            char *colon = strchr(line, ':');
            if(!colon) continue;
            *colon = 0;
            char *v = colon+1;
            while(*v==' ') v++;
            if(r.nhdr == cap){ cap *= 2; r.hdr = realloc(r.hdr, (size_t)cap*sizeof(hhdr)); }
            r.hdr[r.nhdr].name = xstrdup2(line);
            r.hdr[r.nhdr].value = xstrdup2(v);
            r.nhdr++;
        }
        free(hdrs);
        const char *cl = hreq_header(&r, "Content-Length");
        size_t want = cl ? (size_t)strtoull(cl, NULL, 10) : 0;
        const char *te = hreq_header(&r, "Transfer-Encoding");
        size_t have = in.n - hdr_end;
        if(te && strcasestr(te, "chunked")){
            buf body = {0};
            buf rest = {0};
            buf_add(&rest, in.p + hdr_end, have);
            size_t pos = 0;
            for(;;){
                char *nl = NULL;
                while(!(nl = rest.p ? strstr(rest.p + pos, "\r\n") : NULL)){
                    int n = cio_read(c, tmp, sizeof tmp);
                    if(n <= 0) break;
                    buf_add(&rest, tmp, (size_t)n);
                }
                if(!nl) break;
                size_t csz = (size_t)strtoull(rest.p + pos, NULL, 16);
                size_t data_off = (size_t)(nl - rest.p) + 2;
                while(rest.n < data_off + csz + 2){
                    int n = cio_read(c, tmp, sizeof tmp);
                    if(n <= 0) break;
                    buf_add(&rest, tmp, (size_t)n);
                }
                if(rest.n < data_off + csz) break;
                if(csz == 0) break;
                buf_add(&body, rest.p + data_off, csz);
                pos = data_off + csz + 2;
            }
            r.body = body.p ? body.p : xstrdup2("");
            r.blen = body.n;
            buf_free(&rest);
        } else {
            while(have < want){
                int n = cio_read(c, tmp, sizeof tmp);
                if(n <= 0) break;
                buf_add(&in, tmp, (size_t)n);
                have = in.n - hdr_end;
            }
            if(want > have) want = have;
            r.body = malloc(want+1);
            memcpy(r.body, in.p + hdr_end, want);
            r.body[want] = 0;
            r.blen = want;
        }
        a->h(c, &r, a->ud);
        free_req(&r);
    }
done:
    buf_free(&in);
    if(c->ssl){ SSL_shutdown(c->ssl); SSL_free(c->ssl); }
    close(c->fd);
    free(c);
    free(a);
    return NULL;
}

int http_serve(const char *addr, int port, const char *cert, const char *key,
               http_handler h, void *ud){
    SSL_CTX *ctx = NULL;
    if(cert && key){
        SSL_library_init();
        ctx = SSL_CTX_new(TLS_server_method());
        if(!ctx) return -1;
        SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
        if(SSL_CTX_use_certificate_chain_file(ctx, cert) <= 0){ fprintf(stderr,"cert load failed\n"); return -1; }
        if(SSL_CTX_use_PrivateKey_file(ctx, key, SSL_FILETYPE_PEM) <= 0){ fprintf(stderr,"key load failed\n"); return -1; }
    }
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    sa.sin_addr.s_addr = (addr && strcmp(addr,"0.0.0.0")) ? inet_addr(addr) : INADDR_ANY;
    if(bind(ls, (struct sockaddr*)&sa, sizeof sa) < 0){ perror("bind"); return -1; }
    if(listen(ls, 512) < 0){ perror("listen"); return -1; }
    for(;;){
        int fd = accept(ls, NULL, NULL);
        if(fd < 0){ if(errno==EINTR) continue; break; }
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        hconn *c = calloc(1, sizeof(hconn));
        c->fd = fd;
        if(ctx){
            c->ssl = SSL_new(ctx);
            SSL_set_fd(c->ssl, fd);
            if(SSL_accept(c->ssl) <= 0){ SSL_free(c->ssl); close(fd); free(c); continue; }
        }
        cargs *a = malloc(sizeof(cargs));
        a->c = c; a->h = h; a->ud = ud;
        pthread_t t;
        if(pthread_create(&t, NULL, conn_thread, a) != 0){ free(a); close(fd); free(c); continue; }
        pthread_detach(t);
    }
    return 0;
}

/* ------------------------- client ------------------------- */
struct hclient {
    int fd; SSL *ssl; SSL_CTX *ctx;
    buf in; size_t pos;
    int chunked; size_t chunk_left; int done;
};

static int dial(const char *host, int port, int timeout_sec){
    char ps[16]; snprintf(ps, sizeof ps, "%d", port);
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    if(getaddrinfo(host, ps, &hints, &res) != 0) return -1;
    int fd = socket(res->ai_family, res->ai_socktype, 0);
    if(fd < 0){ freeaddrinfo(res); return -1; }
    struct timeval tv = { timeout_sec, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    if(connect(fd, res->ai_addr, res->ai_addrlen) < 0){ close(fd); freeaddrinfo(res); return -1; }
    freeaddrinfo(res);
    int one = 1; setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    return fd;
}

hclient *hc_open(const char *host, int port, int tls, const char *method,
                 const char *path, const char *const *hdrs, int nhdrs,
                 const char *body, size_t blen, int *status){
    int fd = dial(host, port, 3600);
    if(fd < 0) return NULL;
    hclient *c = calloc(1, sizeof(hclient));
    c->fd = fd;
    if(tls){
        c->ctx = SSL_CTX_new(TLS_client_method());
        SSL_CTX_set_verify(c->ctx, SSL_VERIFY_NONE, NULL);
        c->ssl = SSL_new(c->ctx);
        SSL_set_fd(c->ssl, fd);
        if(SSL_connect(c->ssl) <= 0){ hc_close(c); return NULL; }
    }
    buf req = {0};
    buf_printf(&req, "%s %s HTTP/1.1\r\nHost: %s:%d\r\n", method, path, host, port);
    for(int i=0;i<nhdrs;i++) buf_printf(&req, "%s\r\n", hdrs[i]);
    buf_printf(&req, "Content-Length: %zu\r\nConnection: close\r\n\r\n", blen);
    if(blen) buf_add(&req, body, blen);
    int off = 0;
    while(off < (int)req.n){
        int w = c->ssl ? SSL_write(c->ssl, req.p+off, (int)req.n-off)
                       : (int)write(fd, req.p+off, req.n-off);
        if(w <= 0){ buf_free(&req); hc_close(c); return NULL; }
        off += w;
    }
    buf_free(&req);
    /* read headers */
    char tmp[16384];
    size_t hdr_end = 0;
    for(;;){
        char *p = c->in.p ? strstr(c->in.p, "\r\n\r\n") : NULL;
        if(p){ hdr_end = (size_t)(p - c->in.p) + 4; break; }
        int n = c->ssl ? SSL_read(c->ssl, tmp, sizeof tmp) : (int)read(fd, tmp, sizeof tmp);
        if(n <= 0){ hc_close(c); return NULL; }
        buf_add(&c->in, tmp, (size_t)n);
    }
    int st = 0;
    if(sscanf(c->in.p, "HTTP/1.%*d %d", &st) != 1) st = 0;
    if(status) *status = st;
    char *hdr = malloc(hdr_end+1); memcpy(hdr, c->in.p, hdr_end); hdr[hdr_end]=0;
    c->chunked = strcasestr(hdr, "\r\ntransfer-encoding: chunked") ||
                 strcasestr(hdr, "\r\nTransfer-Encoding: chunked") ? 1 : 0;
    free(hdr);
    c->pos = hdr_end;
    return c;
}

static int hc_fill(hclient *c){
    char tmp[16384];
    int n = c->ssl ? SSL_read(c->ssl, tmp, sizeof tmp) : (int)read(c->fd, tmp, sizeof tmp);
    if(n <= 0){ c->done = 1; return 0; }
    buf_add(&c->in, tmp, (size_t)n);
    return n;
}

/* returns one line of the (possibly chunked) body */
char *hc_readline(hclient *c){
    for(;;){
        if(c->chunked){
            if(c->chunk_left == 0){
                char *nl;
                while(!(nl = strstr(c->in.p + c->pos, "\r\n"))){
                    if(!hc_fill(c)) return NULL;
                }
                c->chunk_left = (size_t)strtoull(c->in.p + c->pos, NULL, 16);
                c->pos = (size_t)(nl - c->in.p) + 2;
                if(c->chunk_left == 0){ c->done = 1; return NULL; }
            }
        }
        /* find a newline within the available body bytes */
        size_t avail = c->in.n - c->pos;
        size_t limit = c->chunked ? (avail < c->chunk_left ? avail : c->chunk_left) : avail;
        char *nl = memchr(c->in.p + c->pos, '\n', limit);
        if(nl){
            size_t len = (size_t)(nl - (c->in.p + c->pos));
            char *line = malloc(len+1);
            memcpy(line, c->in.p + c->pos, len);
            line[len] = 0;
            if(c->chunked) c->chunk_left -= (len + 1);
            c->pos += len + 1;
            if(c->chunked && c->chunk_left <= 2 && c->chunk_left > 0){
                c->pos += c->chunk_left; c->chunk_left = 0;
            } else if(c->chunked && c->chunk_left == 0){
                c->pos += 2;   /* trailing CRLF of the chunk */
            }
            return line;
        }
        if(!hc_fill(c)) return NULL;
    }
}

void hc_close(hclient *c){
    if(!c) return;
    if(c->ssl){ SSL_free(c->ssl); }
    if(c->ctx) SSL_CTX_free(c->ctx);
    if(c->fd >= 0) close(c->fd);
    buf_free(&c->in);
    free(c);
}

int http_request(const char *host, int port, int tls, const char *method,
                 const char *path, const char *const *hdrs, int nhdrs,
                 const char *body, size_t blen, hresp *out, int timeout_sec){
    memset(out, 0, sizeof *out);
    int fd = dial(host, port, timeout_sec > 0 ? timeout_sec : 60);
    if(fd < 0) return -1;
    SSL_CTX *ctx = NULL; SSL *ssl = NULL;
    if(tls){
        ctx = SSL_CTX_new(TLS_client_method());
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
        ssl = SSL_new(ctx);
        SSL_set_fd(ssl, fd);
        if(SSL_connect(ssl) <= 0){ SSL_free(ssl); SSL_CTX_free(ctx); close(fd); return -1; }
    }
    buf req = {0};
    buf_printf(&req, "%s %s HTTP/1.1\r\nHost: %s:%d\r\n", method, path, host, port);
    for(int i=0;i<nhdrs;i++) buf_printf(&req, "%s\r\n", hdrs[i]);
    buf_printf(&req, "Content-Length: %zu\r\nConnection: close\r\n\r\n", blen);
    if(blen) buf_add(&req, body, blen);
    size_t off = 0;
    while(off < req.n){
        int w = ssl ? SSL_write(ssl, req.p+off, (int)(req.n-off)) : (int)write(fd, req.p+off, req.n-off);
        if(w <= 0) break;
        off += (size_t)w;
    }
    buf_free(&req);
    buf in = {0};
    char tmp[16384];
    for(;;){
        int n = ssl ? SSL_read(ssl, tmp, sizeof tmp) : (int)read(fd, tmp, sizeof tmp);
        if(n <= 0) break;
        buf_add(&in, tmp, (size_t)n);
    }
    if(ssl){ SSL_free(ssl); SSL_CTX_free(ctx); }
    close(fd);
    if(!in.p){ return -1; }
    char *hend = strstr(in.p, "\r\n\r\n");
    if(!hend){ buf_free(&in); return -1; }
    int st = 0; sscanf(in.p, "HTTP/1.%*d %d", &st);
    out->status = st;
    size_t hlen = (size_t)(hend - in.p) + 4;
    char *hdr = malloc(hlen+1); memcpy(hdr, in.p, hlen); hdr[hlen] = 0;
    int chunked = (strcasestr(hdr, "transfer-encoding: chunked") != NULL);
    free(hdr);
    if(chunked){
        buf body_b = {0};
        size_t p = hlen;
        while(p < in.n){
            char *nl = memchr(in.p + p, '\n', in.n - p);
            if(!nl) break;
            size_t csz = (size_t)strtoull(in.p + p, NULL, 16);
            p = (size_t)(nl - in.p) + 1;
            if(csz == 0) break;
            if(p + csz > in.n) break;
            buf_add(&body_b, in.p + p, csz);
            p += csz + 2;
        }
        out->body = body_b.p ? body_b.p : xstrdup2("");
        out->blen = body_b.n;
    } else {
        out->blen = in.n - hlen;
        out->body = malloc(out->blen + 1);
        memcpy(out->body, in.p + hlen, out->blen);
        out->body[out->blen] = 0;
    }
    buf_free(&in);
    return 0;
}
void hresp_free(hresp *r){ free(r->body); free(r->ctype); r->body=NULL; r->ctype=NULL; }

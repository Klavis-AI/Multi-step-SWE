/* http.h -- minimal HTTP/1.1 server: keep-alive, chunked-free, bounded bodies. */
#ifndef FHIR_HTTP_H
#define FHIR_HTTP_H

#include "util.h"

typedef struct {
    char *name;
    char *value;
} hhdr;

typedef struct {
    char   *method;
    char   *target;      /* raw request-target, e.g. "/baseR4/Patient?name=x" */
    char   *path;        /* decoded path portion                              */
    char   *query;       /* raw query string (not decoded)                    */
    hhdr   *hdrs;
    size_t  nhdr;
    char   *body;
    size_t  blen;
    int     keep_alive;
} hreq;

typedef struct {
    int     status;
    buf     hdrs;
    buf     body;
} hresp;

const char *hreq_hdr(const hreq *r, const char *name);
void        hresp_init(hresp *r);
void        hresp_hdr(hresp *r, const char *name, const char *value);
void        hresp_hdrf(hresp *r, const char *name, const char *fmt, ...);
void        hresp_free(hresp *r);
const char *http_reason(int status);

typedef void (*http_handler)(void *ctx, hreq *req, hresp *res);

int http_serve(const char *bind_addr, int port, http_handler h, void *ctx);

/* A one-shot HTTP/1.1 client, enough to deliver a subscription notification:
   connect, send one request with Content-Length, read the status line, close.
   -> the response status, or 0 when the endpoint could not be reached.  `url`
   must be absolute and http (a notification endpoint is a plain URL). */
int http_post_url(const char *url, const char *content_type,
                  const char *body, size_t len, int timeout_sec);

#endif

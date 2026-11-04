/* A small threaded HTTP/1.1 server and client. */
#ifndef MXHTTP_H
#define MXHTTP_H

#include <stdbool.h>
#include <stddef.h>
#include "mxjson.h"
#include "mxutil.h"

#define MAX_PATH_PARTS 16

typedef struct http_req {
    char *method;               /* "GET" … */
    char *target;               /* raw request target, including the query */
    char *path;                 /* percent-decoded path, no query */
    char *query;                /* raw query string or NULL */
    /* headers, lower-cased names */
    char **hname;
    char **hvalue;
    size_t nheaders;
    char *body;
    size_t body_len;
    /* filled in by the router */
    char *part[MAX_PATH_PARTS]; /* percent-decoded path parameters, in order */
    int nparts;
    json *json_body;            /* parsed lazily by http_json(), owned by req */
    bool json_bad;
    void *server;               /* struct hs * */
} http_req;

typedef struct http_resp {
    int status;
    buf body;
    char *content_type;
    char **hname;
    char **hvalue;
    size_t nheaders;
} http_resp;

const char *http_header(const http_req *r, const char *name);
const char *http_query(const http_req *r, const char *name);   /* first value */
/* every value of a repeated query parameter; returns count, fills out[] */
int http_query_all(const http_req *r, const char *name, const char **out, int max);
json *http_json(http_req *r);                                  /* NULL if absent/bad */

void resp_set_header(http_resp *w, const char *name, const char *value);
void resp_json(http_resp *w, int status, json *body);          /* takes ownership */
void resp_bytes(http_resp *w, int status, const void *p, size_t n, const char *ctype);
void resp_error(http_resp *w, int status, const char *errcode, const char *msg);

/* A handler returns nothing; it fills *w.  `pattern` uses `:` for a parameter
 * and `*` for a trailing wildcard, e.g.
 *   "/_matrix/client/v3/rooms/:/state/:/:"                                     */
typedef void (*http_handler)(http_req *r, http_resp *w, void *ud);

typedef struct http_server http_server;
http_server *http_server_new(void *userdata);
void http_route(http_server *s, const char *method, const char *pattern,
                http_handler fn);
/* Start listening.  `tls_cert`/`tls_key` may be NULL for a plaintext listener.
 * Returns false on failure.  Non-blocking: serving happens on worker threads. */
bool http_listen(http_server *s, const char *host, int port,
                 const char *tls_cert, const char *tls_key);
void http_server_run(http_server *s);      /* blocks until http_server_stop() */
void http_server_stop(http_server *s);
void *http_server_userdata(http_server *s);

/* Outbound requests (used from step 2 onwards for federation). */
typedef struct {
    int status;
    char *body;
    size_t body_len;
    char *content_type;
} http_client_resp;
http_client_resp *http_get(const char *url, const char **headers, int nheaders);
http_client_resp *http_send(const char *method, const char *url,
                            const void *body, size_t body_len,
                            const char **headers, int nheaders);
void http_client_resp_free(http_client_resp *r);

#endif

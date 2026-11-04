#ifndef KCP_HTTP_H
#define KCP_HTTP_H
#include <stddef.h>
#include <openssl/ssl.h>
#include "util.h"

typedef struct { char *name, *value; } hhdr;

typedef struct {
    char *method, *target, *path, *query;
    hhdr *hdr; int nhdr;
    char *body; size_t blen;
    char *qp_names[64]; char *qp_vals[64]; int nqp;   /* decoded query params */
} hreq;

typedef struct hconn hconn;

const char *hreq_header(const hreq *r, const char *name);
const char *hreq_query(const hreq *r, const char *name);

/* buffered response */
void h_reply(hconn *c, int status, const char *ctype, const char *body, size_t n);
void h_reply_json(hconn *c, int status, const char *json_text);
/* streaming response (watch) */
void h_stream_begin(hconn *c, const char *ctype);
int  h_stream_write(hconn *c, const char *data, size_t n);
void h_stream_end(hconn *c);
int  h_conn_alive(hconn *c);

typedef void (*http_handler)(hconn *c, hreq *r, void *ud);
int http_serve(const char *bind_addr, int port, const char *cert, const char *key,
               http_handler h, void *ud);

/* client */
typedef struct {
    int status;
    char *body; size_t blen;
    char *ctype;
} hresp;
int http_request(const char *host, int port, int tls, const char *method,
                 const char *path, const char *const *hdrs, int nhdrs,
                 const char *body, size_t blen, hresp *out, int timeout_sec);
void hresp_free(hresp *r);

/* streaming client: used by the controller manager / scheduler to watch */
typedef struct hclient hclient;
hclient *hc_open(const char *host, int port, int tls, const char *method,
                 const char *path, const char *const *hdrs, int nhdrs,
                 const char *body, size_t blen, int *status);
char *hc_readline(hclient *c);     /* caller frees; NULL at EOF */
void  hc_close(hclient *c);
#endif

/* Blocking HTTP/1.1 server front end: one connection at a time, persistent
   connections supported. */
#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

/* WebDriver payloads are tiny; anything larger is refused with 400. */
#define HTTP_MAX_BODY (8u * 1024u * 1024u)

typedef struct {
    char   *method;      /* e.g. "GET" */
    char   *target;      /* raw request target, percent-escapes intact */
    char   *body;        /* NUL-terminated; NULL when no body was sent */
    size_t  body_len;
    int     keep_alive;
} http_request;

typedef struct {
    int     fd;
    int     listen_fd;   /* polled while idle so a waiting peer is not starved */
    char   *buf;
    size_t  len, cap;
    unsigned long served;
} http_conn;

typedef enum {
    HTTP_OK = 0,
    HTTP_CLOSED,      /* clean EOF, timeout, or the connection should be recycled */
    HTTP_MALFORMED,   /* unparseable request: answer 400 and close */
    HTTP_TOO_LARGE    /* body exceeds HTTP_MAX_BODY: answer 400 and close */
} http_status;

/* Binds 127.0.0.1:port with SO_REUSEADDR. Returns the listening fd, or -1
   with errno set. */
int  http_listen(int port);
/* Receive/send timeouts and TCP_NODELAY on an accepted socket. */
void http_tune(int fd);

void        http_conn_init(http_conn *c, int fd, int listen_fd);
void        http_conn_free(http_conn *c);
http_status http_read_request(http_conn *c, http_request *r);
void        http_request_free(http_request *r);

int http_send_json(http_conn *c, int code, const char *body, size_t len,
                   int keep_alive);

/* Percent-decodes n bytes. Returns a NUL-terminated heap string, or NULL on a
   malformed escape or an encoded NUL. */
char *http_percent_decode(const char *s, size_t n);

#endif

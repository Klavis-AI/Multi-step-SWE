/* server.h -- the FHIR server context shared by the REST layer. */
#ifndef FHIR_SERVER_H
#define FHIR_SERVER_H

#include "defs.h"
#include "store.h"
#include "http.h"

typedef struct {
    const char *base_path;     /* "baseR4"  */
    fhirdefs   *defs;
    store      *st;
} fhirbase;

typedef struct {
    char      *base_url;       /* BASE_URL, no trailing slash */
    int        port;
    fhirbase **bases;
    size_t     nbase;
} server;

fhirbase *server_base(server *s, const char *path_seg);
void      fhir_handle(void *ctx, hreq *req, hresp *res);
jval     *capability_statement(arena *a, server *s, fhirbase *b);

#endif

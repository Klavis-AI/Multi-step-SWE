/* sub.h -- topic-based subscriptions: matching, notification, $status, $events. */
#ifndef FHIR_SUB_H
#define FHIR_SUB_H

#include "util.h"
#include "json.h"
#include "defs.h"
#include "store.h"
#include "server.h"

/* One resource change, recorded while a request is handled and dispatched once
   the response has been built, so that a transaction notifies once per resource
   it wrote and a failed write notifies not at all. */
typedef struct {
    const char *interaction;      /* "create" | "update" | "delete" */
    const char *type;
    const char *id;
    jval       *before;           /* NULL for a create */
    jval       *after;            /* NULL for a delete */
} subevent;

void  sub_record(arena *a, vec *events, const char *interaction,
                 const char *type, const char *id, jval *before, jval *after);
void  sub_dispatch(server *srv, fhirbase *b, vec *events);

/* A Subscription that has just been created or moved back to `requested` gets a
   handshake; the endpoint's answer decides `active` or `error`. */
void  sub_handshake(server *srv, fhirbase *b, const char *id);

/* `ids` is a vec of char*; NULL means every Subscription at this base. */
jval *sub_status(arena *a, server *srv, fhirbase *b, vec *ids, const char *want_status);
jval *sub_events(arena *a, server *srv, fhirbase *b, const char *id,
                 long long since, long long until, const char *content);

#endif

/* Shared plumbing for the Client-Server API handlers. */
#ifndef MXAPI_H
#define MXAPI_H

#include <stdbool.h>
#include "mxhttp.h"
#include "mxjson.h"
#include "mxroom.h"
#include "mxserver.h"

typedef struct {
    char *user_id;
    char *device_id;
    bool is_guest;
} principal;

/* Resolves the bearer token (Authorization header, or the deprecated
 * access_token query parameter).  On failure it writes the standard error and
 * returns false. */
bool require_auth(http_req *r, http_resp *w, principal *out);
void principal_free(principal *p);

/* Body helpers: on a malformed body these write M_NOT_JSON / M_BAD_JSON. */
json *require_body(http_req *r, http_resp *w);

/* User-interactive authentication.  Returns true when the request carries a
 * completed flow; otherwise writes the 401 challenge and returns false.
 * `kind` selects the flow set: "register" offers m.login.dummy, everything
 * else offers m.login.password. */
bool uia_check(http_req *r, http_resp *w, const char *kind,
               const char *user_id, json *body);

/* Transaction-id replay: returns the stored response for (user, device, txn)
 * or NULL.  `remember` stores one. */
json *txn_lookup(const principal *p, const char *path, const char *txn);
void txn_remember(const principal *p, const char *path, const char *txn,
                  const json *response);

/* Registration of all the route groups. */
void api_register_account(http_server *s);
void api_register_rooms(http_server *s);
void api_register_sync(http_server *s);
void api_register_media(http_server *s);
void api_register_misc(http_server *s);

/* Helpers shared by the room handlers. */
char *resolve_room(const char *id_or_alias);   /* malloc'd room id or NULL */
bool check_membership(http_resp *w, const char *room_id, const char *user_id,
                      const char *needed);
json *filter_load(const char *user_id, const char *spec);  /* id or inline JSON */
bool filter_event_allowed(const json *filter, const json *ev, const char *room_id);
int64_t filter_timeline_limit(const json *filter, int64_t dflt);

#endif

/* A module names the specification releases it brings. */
void spec_advertise(const char *release);

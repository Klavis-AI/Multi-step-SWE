/* Simplified Sliding Sync (MSC4186), account locking and suspension, and the
 * remaining Client-Server additions of releases v1.12 and v1.13.
 *
 * One call wires the whole thing up:
 *
 *     api_register_slide(server);
 *
 * It registers
 *
 *   POST /_matrix/client/unstable/org.matrix.simplified_msc3575/sync
 *   POST /_matrix/client/{v3,r0}/rooms/{roomId}/report
 *   POST /_dendrite/admin/users/{userId}/lock       (lock / unlock)
 *   POST /_dendrite/admin/users/{userId}/suspend    (suspend / unsuspend)
 *   POST /_dendrite/admin/users/{userId}/admin      (grant / revoke admin)
 *   GET  /_dendrite/admin/users/{userId}            (the three flags)
 *
 * creates its tables with CREATE TABLE IF NOT EXISTS (so mxdb.c does not have
 * to know about them) and installs the request gate described below.
 *
 * ------------------------------------------------------------------------
 * The request gate
 *
 * Account locking has to refuse *every* authenticated request and account
 * suspension has to refuse the write ones, and MSC3861 has to replace the way
 * an access token is resolved.  None of that belongs to one endpoint, and
 * mxapi_account.c's require_auth() is not ours to change, so the gate wraps
 * the router instead: slide_install_gate() walks the server's route table and
 * replaces every handler with one that runs
 *
 *     oauth_before()  ->  slide_before()  ->  the original handler
 *                     ->  slide_after()   ->  oauth_after()
 *
 * Handlers registered after the gate was installed are picked up on the next
 * request, so the call order in main.c does not matter.  The gate reaches the
 * route table through the first fields of mxhttp.c's `struct http_server`,
 * which it verifies at install time by registering a probe route and checking
 * that it lands where the mirror says it should; when that check fails the
 * gate is not installed and the server keeps its plain behaviour. */
#ifndef MXSLIDE_H
#define MXSLIDE_H

#include <stdbool.h>
#include <stdint.h>

#include "mxhttp.h"
#include "mxjson.h"

/* ------------------------------------------------------------ lifecycle --- */
void api_register_slide(http_server *s);

/* Wraps every route registered so far.  Idempotent; safe to call again after
 * more routes have been added.  Returns false when the route table could not
 * be reached, in which case nothing was changed. */
bool slide_install_gate(http_server *s);

/* ---------------------------------------------------------- user  flags --- */
/* A locked account is refused everywhere with M_USER_LOCKED; a suspended one
 * is refused on the write paths with M_USER_SUSPENDED. */
bool slide_user_locked(const char *user_id);
bool slide_user_suspended(const char *user_id);
/* `flag` is "locked", "suspended" or "admin". */
void slide_set_user_flag(const char *user_id, const char *flag, bool on);
bool slide_get_user_flag(const char *user_id, const char *flag);

/* --------------------------------------------------------- gate helpers --- */
/* The percent-encoded path of `r`, without the query; caller frees. */
char *slide_rawpath(const http_req *r);
/* Renames every occurrence of the query parameter `from` to `to`, so that a
 * handler reading `to` sees the values that arrived as `from`.  Returns the
 * number renamed, or -1 when the request's query table could not be reached. */
int slide_query_rename(http_req *r, const char *from, const char *to);
/* Replaces the request body with `v`; the caller keeps ownership of `v`. */
void slide_set_body(http_req *r, const json *v);
/* The parsed response body of a finished handler, or NULL; caller frees.
 * slide_resp_replace() writes one back, keeping the status. */
json *slide_resp_json(const http_resp *w);
void slide_resp_replace(http_resp *w, json *v);

/* The bearer token of `r` (header or the deprecated query parameter), or
 * NULL; caller frees. */
char *slide_bearer_token(const http_req *r);

#endif

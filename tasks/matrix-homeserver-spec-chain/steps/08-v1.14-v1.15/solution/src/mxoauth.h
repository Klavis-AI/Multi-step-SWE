/* Next-generation authentication (MSC3861 and its dependencies) and the
 * remaining Client-Server additions of releases v1.14 and v1.15.
 *
 * One call wires the whole thing up:
 *
 *     api_register_oauth(server);
 *
 * It registers
 *
 *   GET  /_matrix/client/v1/auth_metadata
 *   GET  /_matrix/client/v1/room_summary/{roomIdOrAlias}
 *   POST /_matrix/client/{v3,r0}/users/{userId}/report
 *   GET  /_matrix/client/v1/rooms/{roomId}/hierarchy        (only when no
 *   GET  /_matrix/federation/v1/hierarchy/{roomId}           other module has
 *                                                            claimed them)
 *
 * creates its tables with CREATE TABLE IF NOT EXISTS and hangs the rest of its
 * behaviour off the request gate in mxslide.h: token introspection, the
 * refusal of the legacy password endpoints, the `m.topic` content block, the
 * extra hierarchy fields and the removal of the deprecated `server_name`
 * parameter all happen there.
 *
 * Everything in this module is inert unless the configuration file carries an
 * `experimental_features.msc3861` block with `enabled: true`; without one the
 * server behaves exactly as it did before, except that the four endpoints
 * above are served. */
#ifndef MXOAUTH_H
#define MXOAUTH_H

#include <stdbool.h>

#include "mxhttp.h"
#include "mxjson.h"

/* ------------------------------------------------------------ lifecycle --- */
void api_register_oauth(http_server *s);

/* True when experimental_features.msc3861.enabled is set: access tokens are
 * resolved by OAuth 2.0 token introspection and the password endpoints are
 * refused. */
bool oauth_delegated(void);
/* The configured issuer, or NULL. */
const char *oauth_issuer(void);
/* The configured account management URL, or NULL. */
const char *oauth_account_management_url(void);
/* True when `token` equals the configured admin_token. */
bool oauth_is_admin_token(const char *token);

/* ------------------------------------------------------- the gate  hooks --- */
/* Runs before the routed handler.  Returns false when it has answered the
 * request itself (an unknown token, a refused legacy endpoint...). */
bool oauth_before(http_req *r, http_resp *w);
/* Runs after the routed handler and may rewrite the response. */
void oauth_after(http_req *r, http_resp *w);

#endif

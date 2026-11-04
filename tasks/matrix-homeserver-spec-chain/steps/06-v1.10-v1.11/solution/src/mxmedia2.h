/* Authenticated media: the content repository as specification releases v1.10
 * and v1.11 leave it.
 *
 * One call wires the whole thing up:
 *
 *     api_register_media2(server);
 *
 * It registers the new authenticated client endpoints below
 * /_matrix/client/v1/media, the two federation endpoints below
 * /_matrix/federation/v1/media whose responses are multipart/mixed, the frozen
 * replacements for the deprecated download and thumbnail endpoints below
 * /_matrix/media, and GET /.well-known/matrix/support.  It creates its own
 * table with CREATE TABLE IF NOT EXISTS, so mxdb.c does not have to know about
 * it.  Uploads stay where they are, on POST /_matrix/media/v3/upload in
 * mxapi_media.c, which this module reads through the shared `media` table.
 *
 * ---------------------------------------------------------------------------
 * Registration order
 * ---------------------------------------------------------------------------
 * The deprecated download and thumbnail endpoints are registered by
 * mxapi_media.c as well.  mxhttp.c's router keeps the *first* route registered
 * for a pattern, so api_register_media2() must be called before
 * api_register_media() for the freeze to take effect.  The handlers here serve
 * exactly what mxapi_media.c served for media uploaded before the freeze
 * instant, and refuse everything uploaded after it.
 *
 * ---------------------------------------------------------------------------
 * The freeze instant
 * ---------------------------------------------------------------------------
 * "By Matrix 1.12, servers SHOULD freeze the deprecated, unauthenticated,
 * endpoints to prevent newly-uploaded media from being downloaded.  This
 * SHOULD mean that any media uploaded before the freeze remains accessible via
 * the deprecated endpoints, and any media uploaded after (or during) the
 * freeze SHOULD only be accessible through the new, authenticated, endpoints."
 *
 * The instant is written to the database the first time a server carrying this
 * module starts, so media a previous version of the server accepted keeps
 * working and everything uploaded from then on does not.
 */
#ifndef MXMEDIA2_H
#define MXMEDIA2_H

#include <stdbool.h>
#include <stdint.h>

#include "mxhttp.h"

/* ------------------------------------------------------------ lifecycle --- */
void api_register_media2(http_server *s);

/* The instant the unauthenticated download and thumbnail endpoints stopped
 * accepting newly uploaded content, in milliseconds since the epoch.  Media
 * whose `created_ts` is before it is still served there. */
int64_t media_freeze_instant(void);
/* Adds MSC4115's `unsigned.membership` to an event being served. */
void media2_membership_hook(json *ev, json *un, const char *user_id);

/* Erasure: record it, and decide whether an event is hidden from a viewer. */
void media2_erasure_record(const char *user_id);
bool media2_erased_from(const json *ev, const char *viewer);

#endif

/* End-to-end encryption: the key-distribution, cross-signing, server-side
 * key-backup and send-to-device surfaces of the Client-Server API, and the
 * key endpoints and device/signing-key EDUs of the Server-Server API.
 *
 * One call wires the whole thing up:
 *
 *     api_register_keys(server);
 *
 * It registers every route (under both /_matrix/client/v3 and the legacy
 * /_matrix/client/r0 prefix, plus the two /_matrix/federation/v1/user/keys
 * endpoints) and creates the tables this module needs with CREATE TABLE IF
 * NOT EXISTS, so mxdb.c does not have to know about them. */
#ifndef MXKEYS_H
#define MXKEYS_H

#include <stdbool.h>
#include <stdint.h>

#include "mxhttp.h"
#include "mxjson.h"

/* ------------------------------------------------------------ lifecycle --- */
void api_register_keys(http_server *s);

/* ----------------------------------------------------------- inbound EDUs -- */
/* Handles the EDUs this module owns: m.device_list_update (the remote user's
 * device list is stored), m.signing_key_update (their cross-signing keys are
 * stored) and m.direct_to_device (the messages are queued for the local
 * target devices).  Returns true when `edu` was one of those.
 *
 * mxfed.c's fed_process_edu() is where inbound EDUs are dispatched; it
 * already stores m.device_list_update itself.  Adding
 *
 *     if (keys_handle_edu(origin, edu)) return;
 *
 * at the top of fed_process_edu() is all this needs. */
bool keys_handle_edu(const char *origin, const json *edu);

/* --------------------------------------------------- device-list changes --- */
/* Records that a local user's device list changed and tells the other servers
 * in their rooms.  This module calls it whenever keys are uploaded, and
 * notices additions and removals made elsewhere (a login, a logout, DELETE
 * /devices/{deviceId}) lazily; call it directly from those handlers to have
 * the change published immediately instead. */
void keys_device_list_changed(const char *user_id, const char *device_id,
                              bool deleted);

/* ------------------------------------------------------- published keys --- */
/* device_id -> DeviceKeys for every device of a local user that has published
 * identity keys, as GET /_matrix/federation/v1/user/devices/{userId} needs to
 * report them.  mxfed.c's h_user_devices() still sends an empty `keys` object
 * (see the report); this is what it should send instead.  Caller frees. */
json *keys_user_device_keys(const char *user_id);

/* -------------------------------------------------------- /sync additions -- */
/* The three properties the End-to-End Encryption module adds to /sync.  They
 * are not emitted today because mxapi_sync.c's sync_build() does not call
 * them; each returns a freshly allocated value the caller frees.
 *
 *   device_lists                     {"changed": [...], "left": [...]}
 *   device_one_time_keys_count       {"<algorithm>": <count>, ...}
 *   device_unused_fallback_key_types ["<algorithm>", ...]                   */
json *keys_sync_device_lists(const char *user_id, int64_t since, int64_t now);
json *keys_sync_otk_counts(const char *user_id, const char *device_id);
json *keys_sync_unused_fallback_key_types(const char *user_id,
                                          const char *device_id);

#endif

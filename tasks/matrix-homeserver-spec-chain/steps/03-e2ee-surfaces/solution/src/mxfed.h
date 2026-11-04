/* The Matrix Server-Server (federation) API.
 *
 * One call wires the whole thing up:
 *
 *     api_register_federation(server);
 *
 * It registers every federation and key route on `server`, creates the tables
 * the federation code needs (with CREATE TABLE IF NOT EXISTS, so mxdb.c does
 * not have to know about them) and starts the background thread that pushes
 * locally created events out to the other servers in each room.  Everything
 * else declared here is for the client-server handlers and main.c to use once
 * they are wired up to federation. */
#ifndef MXFED_H
#define MXFED_H

#include <stdbool.h>
#include <stdint.h>

#include "mxhttp.h"
#include "mxjson.h"

/* ------------------------------------------------------------ lifecycle --- */
/* Registers the routes, creates the tables and starts the sender thread. */
void api_register_federation(http_server *s);
/* Stops the sender thread.  Safe to call more than once, or never. */
void fed_shutdown(void);

/* ---------------------------------------------------------- server names -- */
bool fed_is_local_server(const char *server_name);
bool fed_is_local_user(const char *user_id);
/* "https://host:port" for a server name, following "Resolving Server Names":
 * a name carrying an explicit port is contacted directly on that port, every
 * other name on port 8448.  Caller frees. */
char *fed_base_url(const char *server_name);

/* ------------------------------------------------------------------ keys -- */
/* This server's published key document (server_name, verify_keys,
 * old_verify_keys, valid_until_ts), signed by this server.  Caller frees. */
json *fed_local_keys(void);
/* Resolves `server_name`'s `key_id` to its 32-byte Ed25519 public key, from
 * the cache while it is still valid and from GET /_matrix/key/v2/server
 * otherwise.  Returns false when the key cannot be obtained. */
bool fed_fetch_key(const char *server_name, const char *key_id,
                   unsigned char pub[32]);

/* -------------------------------------------------------------- requests -- */
/* Performs `method path` against `destination` with an X-Matrix Authorization
 * header signed over {method, uri, origin, destination, content?}.  `path`
 * must start with "/_matrix/" and include the query string.  `body` may be
 * NULL.  Returns NULL when the request could not be made; free the result
 * with http_client_resp_free(). */
http_client_resp *fed_request(const char *destination, const char *method,
                              const char *path, const json *body);

/* Verifies the inbound X-Matrix Authorization header.  On success returns true
 * and stores the origin server name in *origin_out (caller frees); on failure
 * it writes 401 M_UNAUTHORIZED into `w` and returns false. */
bool fed_authenticate(http_req *r, http_resp *w, char **origin_out);

/* --------------------------------------------------- outbound federation -- */
/* Nudges the sender thread; call after writing events that must go out. */
void fed_wake(void);

/* The remote join handshake: make_join -> sign -> send_join -> store the
 * returned state and auth chain, after which the room is live locally.
 * `servers` are candidate resident servers (may be empty when the room id or
 * alias already names one).  Returns the room id (caller frees) or NULL, in
 * which case status, errcode and errmsg describe the failure (the error
 * message is malloc'd, or NULL). */
char *fed_join_remote(const char *user_id, const char *room_id_or_alias,
                      const char *const *servers, int nservers,
                      int *status, const char **errcode, char **errmsg);

/* make_leave -> sign -> send_leave, used to reject an invite or retract a
 * knock for a room this server is not otherwise part of. */
bool fed_leave_remote(const char *user_id, const char *room_id,
                      const char *const *servers, int nservers,
                      int *status, const char **errcode, char **errmsg);

/* make_knock -> sign -> send_knock (room version 7 and later). */
bool fed_knock_remote(const char *user_id, const char *room_id_or_alias,
                      const char *const *servers, int nservers, json *content,
                      int *status, const char **errcode, char **errmsg);

/* Asks the invited user's server to counter-sign `invite_event` and returns
 * the doubly signed event (caller frees), or NULL. */
json *fed_invite_remote(const char *room_id, const char *event_id,
                        const json *invite_event, const json *invite_room_state,
                        int *status, const char **errcode, char **errmsg);

/* Federation queries against the user's / alias' own server.  Each returns a
 * freshly allocated JSON object, or NULL. */
json *fed_query_profile(const char *user_id, const char *field);
json *fed_query_directory(const char *room_alias);
json *fed_query_user_devices(const char *user_id);

/* Every remote server that has a joined or invited member in `room_id`, as a
 * JSON array of strings.  Caller frees. */
json *fed_room_servers(const char *room_id);

/* Broadcasts one EDU to every remote server in `room_id`.  Takes ownership of
 * `content`. */
void fed_broadcast_edu(const char *room_id, const char *edu_type, json *content);

#endif

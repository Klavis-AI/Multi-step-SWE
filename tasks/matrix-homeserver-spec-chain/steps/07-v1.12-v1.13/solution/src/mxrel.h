/* Event relationships, threads, spaces, the refined receipt model and
 * per-thread notifications.
 *
 * One call wires the whole thing up:
 *
 *     api_register_relations(server);
 *
 * It registers every route this module owns and creates the tables it needs
 * (with CREATE TABLE IF NOT EXISTS, so mxdb.c does not have to know about
 * them).  Register it *before* the other route groups: a few of its routes
 * deliberately stand in front of the plain ones -- the receipts, read markers,
 * /messages and /publicRooms handlers all grew behaviour that belongs to this
 * module -- and the router keeps the first route registered at a given
 * specificity.
 *
 * Everything else declared here is a hook the existing modules call. */
#ifndef MXREL_H
#define MXREL_H

#include <stdbool.h>
#include <stdint.h>

#include "mxhttp.h"
#include "mxjson.h"

/* ------------------------------------------------------------ lifecycle --- */
void api_register_relations(http_server *s);

/* ---------------------------------------------------------------- hooks --- */
/* Called from mxroom.c's event_to_client() once `unsigned` exists, for every
 * event this server hands to a client.  It attaches the bundled aggregations
 * ("Aggregations of child events": unsigned.m.relations) and applies the
 * room-version-11 compatibility fixes the specification asks for when serving
 * events over the Client-Server API.  `ev` is the client form of the event and
 * `un` its `unsigned` object; `user_id` may be NULL. */
void rel_client_event_hook(json *ev, json *un, const char *user_id);

/* May this event's `m.relates_to` be accepted in this room? */
bool rel_relation_allowed(const char *room_id, const json *content,
                          const char **errcode, const char **message);

/* Called from mxapi_sync.c's collect_timeline().  False drops the event from
 * the timeline.  `f_timeline` is the room.timeline RoomEventFilter (may be
 * NULL); `stored` is the stored event JSON.  This is where the "not in a
 * thread" filter lives: with not_related_by_rel_types: ["m.thread"] a thread's
 * replies leave the main timeline while its root stays. */
bool rel_timeline_allows(const json *f_timeline, const char *room_id,
                         const json *stored);

/* Called from mxapi_sync.c's join_section() with a finished joined-room
 * section.  Adds unread_thread_notifications, recomputes unread_notifications
 * under the refined receipt model (private and threaded receipts both count)
 * and rewrites the room's m.receipt so that private read receipts reach only
 * the user who sent them and every receipt carries its thread_id.
 * `filter` is the whole sync filter (may be NULL). */
/* Has this module news for the room in this window, when the core sync
 * code would otherwise drop it as an empty delta? */
bool rel_room_has_news(const char *room_id, const char *user_id,
                       int64_t since, int64_t now);

void rel_sync_room_section(json *sec, const char *room_id, const char *user_id,
                           const json *filter, int64_t since, int64_t now);

#endif

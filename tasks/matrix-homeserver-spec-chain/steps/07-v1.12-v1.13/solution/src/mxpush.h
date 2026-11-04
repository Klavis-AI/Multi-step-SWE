/* Notifications and push rules: the Push Notifications, Event Reports,
 * Presence and login-token surfaces the Client-Server API grew between
 * specification releases v1.7 and v1.9.
 *
 * One call wires the whole thing up:
 *
 *     api_register_push(server);
 *
 * It registers every route this module owns and creates its tables with
 * CREATE TABLE IF NOT EXISTS, so mxdb.c does not have to know about them.
 *
 * ---------------------------------------------------------------------------
 * Registration order
 * ---------------------------------------------------------------------------
 * Four of the routes registered here already exist elsewhere, and this module
 * has to serve them because the releases it absorbs change their responses:
 *
 *   GET  /capabilities              adds m.get_login_token   (mxapi_account.c)
 *   GET  /login                     adds the m.login.token flow
 *   POST /login                     adds m.login.token and refresh tokens
 *   GET  /presence/{userId}/status  adds last_active_ago     (mxapi_misc.c)
 *
 * mxhttp.c's router keeps the *first* route registered for a pattern, so
 * api_register_push() must be called before api_register_account() and
 * api_register_misc() for these to take effect.  Each of the four is a strict
 * superset of the handler it stands in front of; keep it that way when the
 * handler behind it changes.
 *
 * ---------------------------------------------------------------------------
 * Evaluation
 * ---------------------------------------------------------------------------
 * Push rules are evaluated lazily: the module walks the events a user has been
 * delivered since it last ran, runs each through that user's ruleset and
 * records the ones a rule asked to `notify` about.  Everything that reads
 * notifications does that walk first, so the result is the same as evaluating
 * at delivery time without needing a hook inside mxroom.c's room_send().
 */
#ifndef MXPUSH_H
#define MXPUSH_H

#include <stdbool.h>
#include <stdint.h>

#include "mxhttp.h"
#include "mxjson.h"

/* ------------------------------------------------------------ lifecycle --- */
void api_register_push(http_server *s);

/* ----------------------------------------------------------- push rules --- */
/* `user_id`'s complete ruleset: an object with the five kinds ("override",
 * "content", "room", "sender", "underride"), each a JSON array of push rules
 * in priority order, most important first.  The server-default rules the
 * specification lists are included, with whatever `enabled` / `actions` the
 * user has changed applied.  Caller frees. */
json *push_ruleset(const char *user_id);

/* Runs `event` (the stored event JSON) through `user_id`'s rules as if it had
 * just been delivered to them in `room_id` and reports what the highest
 * priority matching rule asked for.  Returns that rule's actions (caller
 * frees) or NULL when no rule matched. */
json *push_actions_for(const char *user_id, const char *room_id,
                       const json *event, bool *notify, bool *highlight);

/* What the rules decided about one event when it reached `user_id`, rather
 * than what they would decide about it now.  Returns false when the event has
 * not been through the rules yet, leaving the judgement to the caller.
 * `mxrel.c`'s per-thread counts ask this first so that a later change to the
 * rules -- or to the room's power levels -- cannot retroactively silence or
 * highlight an event that has already been counted. */
bool push_decision(const char *user_id, const char *room_id, int64_t stream_id,
                   const char *event_id, bool *notify, bool *highlight);

/* ------------------------------------------------------ /sync additions --- */
/* The unread notification and highlight counts for one room, as the Push
 * Notifications module defines them: how many events since the user's read
 * receipt their push rules asked to notify about, and how many of those also
 * set the `highlight` tweak.  `upto` bounds the scan at a stream position
 * (0 for "everything so far").
 *
 * mxapi_sync.c's join_section() fills `unread_notifications` from its own
 * static unread_counts(), which predates push rules: it counts every event
 * after the read receipt and highlights on the display name or user id
 * appearing in `content.body`.  Replacing
 *
 *     unread_counts(c, room_id, &notif, &highlight);
 *
 * with
 *
 *     push_unread_counts(c->user_id, room_id, c->now, &notif, &highlight);
 *
 * is all /sync needs to report the push-rule counts instead. */
void push_unread_counts(const char *user_id, const char *room_id, int64_t upto,
                        int64_t *notif, int64_t *highlight);

#endif

/* The room engine: building, authorising, signing and persisting events. */
#ifndef MXROOM_H
#define MXROOM_H

#include <stdbool.h>
#include <stdint.h>
#include "mxjson.h"
#include "mxevent.h"

typedef struct {
    int status;                 /* 0 on success */
    const char *errcode;
    char *errmsg;               /* malloc'd or NULL */
    char *event_id;             /* malloc'd on success */
    int64_t stream_id;
} send_result;

void sr_free(send_result *r);

/* Creates one event in `room_id`.  `state_key` is NULL for a message event.
 * Takes ownership of `content`.  Applies the room version's authorisation
 * rules against the room's current state and refuses with 403 M_FORBIDDEN when
 * they reject it.  The caller must hold the write lock. */
send_result room_send(const char *room_id, const char *sender, const char *type,
                      const char *state_key, json *content, json *extra_unsigned);

/* Room metadata. */
char *room_version_of(const char *room_id);          /* malloc'd, NULL if unknown */
bool room_exists(const char *room_id);
/* Does this server still have a user joined to the room? */
bool room_has_local_member(const char *room_id);
char *room_membership(const char *room_id, const char *user_id);  /* malloc'd */
int64_t room_latest_stream(const char *room_id);

/* State access.  The returned object maps "<type>\x1f<state_key>" to the full
 * stored event JSON; the caller frees it. */
json *room_state_map(const char *room_id);
json *room_state_at(const char *room_id, int64_t stream_id);
json *room_state_event(const char *room_id, const char *type, const char *sk);

/* Event access. */
json *event_by_id(const char *event_id);             /* stored JSON, caller frees */
char *event_room(const char *event_id);
bool event_get_stream(const char *event_id, int64_t *stream_id, int64_t *topo);

/* Client-facing serialisation: adds event_id/room_id, strips server-only keys,
 * applies a redaction when the event has been redacted, and fills unsigned.age.
 * `for_device` (may be NULL) receives unsigned.transaction_id. */
json *event_to_client(const json *stored, const char *user_id, const char *device_id,
                      bool include_room_id);
/* The stripped-state form used in invites. */
json *event_to_stripped(const json *stored);

/* Creates a room and its initial state events.  Returns the room id or NULL. */
char *room_create(const char *creator, const json *body, int *status,
                  const char **errcode, char **errmsg);

/* Membership transitions used by /join, /invite, /leave, /kick, /ban, /unban. */
send_result room_set_membership(const char *room_id, const char *sender,
                                const char *target, const char *membership,
                                const char *reason, json *extra_content);

/* Power-level helpers over the room's current state. */
int64_t room_user_level(const char *room_id, const char *user_id);

/* History visibility: may `user_id` see `event` given the state at that point? */
bool room_can_see(const char *room_id, const char *user_id, int64_t stream_id);

#endif

/* Matrix events: room versions, hashes, event ids, signing, redaction, auth. */
#ifndef MXEVENT_H
#define MXEVENT_H

#include <stdbool.h>
#include "mxjson.h"

/* One row of the room-version table in https://spec.matrix.org/.../rooms/ */
typedef struct {
    const char *id;
    int  event_format;          /* 1: event carries its own id; 3: id is the ref hash */
    bool url_safe_event_ids;    /* v4+: RFC 4648 §5 alphabet for the event id */
    bool enforce_key_validity;  /* v5+ */
    bool redaction_keeps_aliases;   /* v1–v5 keep m.room.aliases `aliases` */
    bool strict_canonical_json; /* v6+: reject floats, NaN and Infinity */
    bool special_case_aliases_auth; /* v1–v5 auth rule 4 for m.room.aliases */
    int  state_res;             /* 1 or 2 */
    bool knock;                 /* v7+ */
    bool restricted_join;       /* v8+ */
    bool knock_restricted;      /* v10+ */
    bool integer_power_levels;  /* v10+: reject non-integer power levels */
    bool redaction_keeps_join_authorised;  /* v9+ */
    bool updated_redaction_rules;   /* v11+ */
} roomver;

const roomver *rv_get(const char *version);     /* NULL when unsupported */
const char *rv_default(void);
int rv_count(void);
const roomver *rv_index(int i);

/* --- hashing and identifiers (Server-Server API, "Signing Events") -------- */
/* Content hash: SHA-256 of the canonical JSON of the event with `signatures`,
 * `unsigned` and `hashes` removed. */
char *event_content_hash_b64(const json *pdu);
/* Reference hash: SHA-256 of the canonical JSON of the redacted event with
 * `signatures`, `age_ts` and `unsigned` removed. */
bool event_reference_hash(const json *pdu, const roomver *rv, unsigned char out[32]);
/* Event id for this room version: "$opaque:server" (v1/v2) or "$" + unpadded
 * base64 of the reference hash (v3+). */
char *event_compute_id(const json *pdu, const roomver *rv, const char *server_name);

/* --- signing -------------------------------------------------------------- */
/* Adds `signatures[server][key_id]` over the canonical JSON of the object with
 * `signatures` and `unsigned` removed. */
bool json_sign(json *obj, const char *server_name, const char *key_id,
               const unsigned char seed[32]);
bool json_verify(const json *obj, const char *server_name, const char *key_id,
                 const unsigned char pub[32]);

/* --- redaction ------------------------------------------------------------ */
/* Returns a new event with the non-preserved keys stripped, per this room
 * version's redaction algorithm. */
json *event_redact(const json *pdu, const roomver *rv);

/* --- power levels --------------------------------------------------------- */
int64_t pl_user_level(const json *pl_content, const char *user_id);
int64_t pl_event_level(const json *pl_content, const char *type, bool is_state);
int64_t pl_action_level(const json *pl_content, const char *action, int64_t dflt);
bool pl_content_valid(const json *content, const roomver *rv);

/* --- event authorisation (Client-Server + Server-Server "Authorization
 *     rules") ---------------------------------------------------------------
 * `state` is an object mapping "<type>\x1f<state_key>" to the event JSON.
 * Returns true when the event is allowed; `why` receives a short reason. */
bool event_auth_allowed(const json *ev, const json *state, const roomver *rv,
                        const char **why);

/* Which state events authorise this event, per the "Auth events selection"
 * rules.  Returns a JSON array of state keys "<type>\x1f<state_key>". */
json *event_auth_keys(const json *ev);

#define STATE_KEY_SEP "\x1f"
char *state_key_of(const char *type, const char *state_key);
json *state_lookup(const json *state, const char *type, const char *state_key);

#endif

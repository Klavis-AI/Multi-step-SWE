#define _POSIX_C_SOURCE 200809L
/* Rooms: creation, state, membership, history, the directory and upgrades. */
#include "mxapi.h"

/* The relations module, when a later milestone has introduced it. */
extern bool rel_relation_allowed(const char *room_id, const json *content,
                                 const char **errcode,
                                 const char **message)
    __attribute__((weak));
#include "mxfed.h"
#include "mxdb.h"
#include "mxutil.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CS  "/_matrix/client/v3"
#define CSR "/_matrix/client/r0"

#define DEFAULT_LIMIT   10
#define MAX_LIMIT       1000
#define MAX_EVENT_BYTES 65536   /* Client-Server API, "Size limits" */
#define MAX_ID_BYTES    255

/* ================================================================ helpers */

/* Pagination tokens.  Every event carries a monotonic events.stream_id; a
 * token names the gap after that stream id, so backwards paging returns the
 * events at or below it and forwards paging the events above it.  `/sync`
 * hands out the same positions with an `s` prefix. */
static bool token_parse(const char *tok, int64_t *out) {
    if (!tok || !*tok) return false;
    const char *p = tok;
    while (*p && (*p < '0' || *p > '9') && *p != '-') p++;
    if (!*p) return false;
    char *end = NULL;
    long long v = strtoll(p, &end, 10);
    if (end == p) return false;
    *out = (int64_t)v;
    return true;
}

/* A `/messages` position is a pair: where an event sits in the room's own
 * ordering, and -- only to break ties -- when this server learned of it.  The
 * two differ whenever history arrives late: an event backfilled from another
 * server is given a stream position below every live one, so that a room's past
 * does not arrive in `/sync` as new timeline, but it belongs among its
 * neighbours in the room's history.  `t<topological>.<stream>`; a bare position
 * (a `/sync` token, or one this server minted before) still parses, and its
 * topological half is looked up. */
static bool token_parse2(const char *room_id, const char *tok,
                         int64_t *topo, int64_t *stream) {
    if (!token_parse(tok, stream)) return false;
    const char *dot = tok ? strchr(tok, '.') : NULL;
    if (dot && dot[1]) {
        *topo = *stream;
        char *end = NULL;
        long long v = strtoll(dot + 1, &end, 10);
        if (end != dot + 1) { *stream = (int64_t)v; return true; }
    }
    *topo = 0;
    db_int("SELECT COALESCE(MAX(topological), 0) FROM events WHERE room_id=?1 "
           "AND stream_id<=?2", topo, "si", room_id, *stream);
    return true;
}

static char *token_make2(int64_t topo, int64_t stream) {
    return str_fmt("t%" PRId64 ".%" PRId64, topo, stream);
}

static int64_t query_int(http_req *r, const char *name, int64_t dflt) {
    const char *q = http_query(r, name);
    if (!q || !*q) return dflt;
    char *end = NULL;
    long long v = strtoll(q, &end, 10);
    if (end == q) return dflt;
    return (int64_t)v;
}

/* An explicit limit of 0 means zero events; the default only applies when the
 * client supplied no limit at all (query_int has already substituted it). */
static int64_t clamp_limit(int64_t n) {
    if (n < 0) return 0;
    return n > MAX_LIMIT ? MAX_LIMIT : n;
}

static void send_failed(http_resp *w, send_result *sr) {
    resp_error(w, sr->status ? sr->status : 403,
               sr->errcode ? sr->errcode : "M_FORBIDDEN",
               sr->errmsg ? sr->errmsg : "The event was rejected");
}

/* The body of a request that may legitimately be empty. */
static json *optional_body(http_req *r, http_resp *w, bool *bad) {
    *bad = false;
    json *b = http_json(r);
    if (!b && r->json_bad) {
        resp_error(w, 400, "M_NOT_JSON", "Request body is not valid JSON");
        *bad = true;
    }
    return b;
}

static bool room_world_readable(const char *room_id) {
    json *hv = room_state_event(room_id, "m.room.history_visibility", "");
    bool wr = false;
    if (hv) {
        json *c = j_geto(hv, "content");
        wr = c && str_eq(j_gets(c, "history_visibility", "shared"), "world_readable");
        j_free(hv);
    }
    return wr;
}

/* The stream position at which the user's membership last changed. */
static int64_t member_stream(const char *room_id, const char *user_id) {
    int64_t s = 0;
    db_int("SELECT stream_id FROM room_members WHERE room_id=?1 AND user_id=?2",
           &s, "ss", room_id, user_id);
    return s;
}

/* Members and former members may read a room's history; so may anybody at all
 * when the room is world_readable (the room-preview module). */
static bool may_read_room(const char *room_id, const char *user_id) {
    char *ms = room_membership(room_id, user_id);
    bool ok = ms && (str_eq(ms, "join") || str_eq(ms, "leave") ||
                     str_eq(ms, "invite"));
    free(ms);
    return ok || room_world_readable(room_id);
}

static bool may_see_state(const char *room_id, const char *user_id) {
    char *ms = room_membership(room_id, user_id);
    bool ok = ms && (str_eq(ms, "join") || str_eq(ms, "leave"));
    free(ms);
    return ok || room_world_readable(room_id);
}

/* "If the user is joined to the room then the state is taken from the current
 * state of the room.  If the user has left the room then the state is taken
 * from the state of the room when they left." */
static json *state_for_user(const char *room_id, const char *user_id, int64_t at) {
    char *ms = room_membership(room_id, user_id);
    bool left = ms && (str_eq(ms, "leave") || str_eq(ms, "ban"));
    free(ms);
    int64_t cap = left ? member_stream(room_id, user_id) : 0;
    if (at > 0) {
        if (cap > 0 && at > cap) at = cap;      /* never past the point they left */
        return room_state_at(room_id, at);
    }
    return cap > 0 ? room_state_at(room_id, cap) : room_state_map(room_id);
}

static int64_t room_joined_count(const char *room_id) {
    int64_t n = 0;
    db_int("SELECT COUNT(*) FROM room_members WHERE room_id=?1 AND "
           "membership='join'", &n, "s", room_id);
    return n;
}

/* The content of one current state event, cloned, or NULL. */
static json *state_content(const char *room_id, const char *type, const char *sk) {
    json *ev = room_state_event(room_id, type, sk);
    if (!ev) return NULL;
    json *c = j_geto(ev, "content");
    json *out = c ? j_clone(c) : NULL;
    j_free(ev);
    return out;
}

/* ==================================================== shared API helpers */

char *resolve_room(const char *id_or_alias) {
    if (!id_or_alias || !*id_or_alias) return NULL;
    if (id_or_alias[0] == '!')
        return room_exists(id_or_alias) ? xstrdup_(id_or_alias) : NULL;
    if (id_or_alias[0] == '#') {
        char *local = db_str("SELECT room_id FROM aliases WHERE alias=?1", "s",
                             id_or_alias);
        if (local) return local;
        /* An alias on another server is resolved by asking that server. */
        const char *colon = strchr(id_or_alias, ':');
        if (colon && !fed_is_local_server(colon + 1)) {
            json *res = fed_query_directory(id_or_alias);
            const char *rid = res ? j_gets(res, "room_id", NULL) : NULL;
            char *out = rid ? xstrdup_(rid) : NULL;
            j_free(res);
            return out;
        }
    }
    return NULL;
}

bool check_membership(http_resp *w, const char *room_id, const char *user_id,
                      const char *needed) {
    char *ms = room_membership(room_id, user_id);
    bool ok = ms && str_eq(ms, needed);
    free(ms);
    if (!ok) {
        if (str_eq(needed, "join")) {
            resp_error(w, 403, "M_FORBIDDEN", "You aren't a member of the room.");
        } else {
            char *m = str_fmt("The user is not in the '%s' membership state in "
                              "this room.", needed);
            resp_error(w, 403, "M_FORBIDDEN", m);
            free(m);
        }
    }
    return ok;
}

json *filter_load(const char *user_id, const char *spec) {
    if (!spec || !*spec) return NULL;
    json *f = NULL;
    const char *err = NULL;
    if (spec[0] == '{') {
        f = j_parse(spec, strlen(spec), &err);
    } else if (user_id) {
        char *s = db_str("SELECT content FROM filters WHERE user_id=?1 AND "
                         "filter_id=?2", "ss", user_id, spec);
        if (s) {
            f = j_parse(s, strlen(s), &err);
            free(s);
        }
    }
    if (f && f->type != J_OBJECT) { j_free(f); f = NULL; }
    return f;
}

/* '*' matches any sequence of characters; everything else is literal. */
static bool glob_match(const char *pat, const char *s) {
    const char *star = NULL, *retry = NULL;
    while (*s) {
        if (*pat == '*') { star = pat++; retry = s; }
        else if (*pat && *pat == *s) { pat++; s++; }
        else if (star) { pat = star + 1; s = ++retry; }
        else return false;
    }
    while (*pat == '*') pat++;
    return *pat == '\0';
}

static bool list_has(const json *arr, const char *s, bool wild) {
    for (size_t i = 0; i < j_len(arr); i++) {
        const json *v = j_at(arr, i);
        if (!j_is_str(v)) continue;
        if (wild ? glob_match(v->s, s) : str_eq(v->s, s)) return true;
    }
    return false;
}

/* A caller may hand us a whole sync filter; the room event filter it carries
 * is what applies to a timeline event. */
static const json *room_event_filter(const json *f) {
    if (!f || f->type != J_OBJECT) return NULL;
    if (j_get(f, "types") || j_get(f, "not_types") || j_get(f, "senders") ||
        j_get(f, "not_senders") || j_get(f, "rooms") || j_get(f, "not_rooms") ||
        j_get(f, "contains_url"))
        return f;
    json *room = j_geto(f, "room");
    json *tl = room ? j_geto(room, "timeline") : NULL;
    return tl ? tl : f;
}

bool filter_event_allowed(const json *filter, const json *ev, const char *room_id) {
    const json *f = room_event_filter(filter);
    if (!f) return true;
    const char *type = j_gets(ev, "type", "");
    const char *sender = j_gets(ev, "sender", "");
    const char *room = room_id ? room_id : j_gets(ev, "room_id", "");

    json *v;
    if ((v = j_geta(f, "not_rooms")) && list_has(v, room, false)) return false;
    if ((v = j_geta(f, "rooms")) && !list_has(v, room, false)) return false;
    if ((v = j_geta(f, "not_senders")) && list_has(v, sender, false)) return false;
    if ((v = j_geta(f, "senders")) && !list_has(v, sender, false)) return false;
    if ((v = j_geta(f, "not_types")) && list_has(v, type, true)) return false;
    if ((v = j_geta(f, "types")) && !list_has(v, type, true)) return false;

    json *cu = j_get(f, "contains_url");
    if (cu && cu->type == J_BOOL) {
        json *c = j_geto(ev, "content");
        bool has_url = c && j_is_str(j_get(c, "url"));
        if (has_url != cu->b) return false;
    }
    return true;
}

int64_t filter_timeline_limit(const json *filter, int64_t dflt) {
    if (!filter || filter->type != J_OBJECT) return dflt;
    json *v = j_deep_get(filter, "room.timeline.limit");
    if (!v) v = j_deep_get(filter, "timeline.limit");
    if (!v) v = j_get(filter, "limit");
    if (v && v->type == J_INT) return v->i;
    if (v && v->type == J_DOUBLE) return (int64_t)v->d;
    return dflt;
}

/* ============================================================== creation */

static void h_create_room(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *body = require_body(r, w);
    if (!body) { principal_free(&p); return; }

    int status = 0;
    const char *errcode = NULL;
    char *errmsg = NULL;
    hs_lock();
    char *room_id = room_create(p.user_id, body, &status, &errcode, &errmsg);
    hs_unlock();
    stream_notify();

    if (!room_id) {
        resp_error(w, status ? status : 400, errcode ? errcode : "M_UNKNOWN",
                   errmsg ? errmsg : "The room could not be created");
        free(errmsg);
        principal_free(&p);
        return;
    }
    free(errmsg);
    json *b = j_obj();
    j_set(b, "room_id", j_str(room_id));
    resp_json(w, 200, b);
    free(room_id);
    principal_free(&p);
}

/* ================================================================ sending */

/* The keys `event_to_client` turns back into unsigned.transaction_id. */
static json *txn_unsigned(const principal *p, const char *txn) {
    json *un = j_obj();
    j_set(un, "__txn_user", j_str(p->user_id));
    j_set(un, "__txn_device", j_str(p->device_id ? p->device_id : ""));
    j_set(un, "__txn_id", j_str(txn));
    return un;
}

/* Client-Server API, "Size limits". */
static bool event_size_ok(http_resp *w, const json *content, const char *type,
                          const char *state_key) {
    if (type && strlen(type) > MAX_ID_BYTES) {
        resp_error(w, 400, "M_TOO_LARGE", "Event type is too long");
        return false;
    }
    if (state_key && strlen(state_key) > MAX_ID_BYTES) {
        resp_error(w, 400, "M_TOO_LARGE", "State key is too long");
        return false;
    }
    size_t n = 0;
    char *js = j_dump(content, &n);
    free(js);
    if (n > MAX_EVENT_BYTES) {
        resp_error(w, 413, "M_TOO_LARGE", "Event is too large");
        return false;
    }
    return true;
}

static void h_send(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *body = require_body(r, w);
    if (!body) { principal_free(&p); return; }

    char *room_id = resolve_room(r->part[0]);
    if (!room_id) {
        resp_error(w, 404, "M_NOT_FOUND", "Unknown room");
        principal_free(&p);
        return;
    }
    const char *type = r->part[1];
    const char *txn = r->part[2];
    char *txn_path = str_fmt("send:%s:%s", room_id, type);
    json *prev = txn_lookup(&p, txn_path, txn);
    if (prev) {
        resp_json(w, 200, prev);
        free(txn_path); free(room_id); principal_free(&p);
        return;
    }
    if (!event_size_ok(w, body, type, NULL)) {
        free(txn_path); free(room_id); principal_free(&p);
        return;
    }
    /* Before room version 11 a redaction names its target in a top-level
     * `redacts`, which this endpoint cannot produce: everything a client sends
     * here becomes `content`.  Such an event could only ever be a redaction
     * that redacts nothing, so it is refused rather than stored. */
    if (str_eq(type, "m.room.redaction")) {
        char *ver = room_version_of(room_id);
        const roomver *rv = rv_get(ver ? ver : rv_default());
        bool in_content = rv && rv->updated_redaction_rules;
        free(ver);
        if (!in_content) {
            resp_error(w, 400, "M_INVALID_PARAM",
                       "In this room version a redaction must be sent through "
                       "/rooms/{roomId}/redact/{eventId}/{txnId}");
            free(txn_path); free(room_id); principal_free(&p);
            return;
        }
    }
    if (rel_relation_allowed) {
        const char *code = NULL, *msg = NULL;
        if (!rel_relation_allowed(room_id, body, &code, &msg)) {
            resp_error(w, 400, code ? code : "M_INVALID_PARAM",
                       msg ? msg : "This relation cannot be accepted");
            free(txn_path); free(room_id); principal_free(&p);
            return;
        }
    }

    hs_lock();
    send_result sr = room_send(room_id, p.user_id, type, NULL, j_clone(body),
                               txn_unsigned(&p, txn));
    /* From room version 11 a redaction names its target inside `content`, so
     * one sent as an ordinary event is a redaction like any other and takes
     * effect -- if its sender may redact that event. */
    if (!sr.status && str_eq(type, "m.room.redaction")) {
        char *ver = room_version_of(room_id);
        const roomver *rv = rv_get(ver ? ver : rv_default());
        const char *victim = j_gets(body, "redacts", NULL);
        if (rv && rv->updated_redaction_rules && victim && *victim) {
            char *owner = db_str("SELECT sender FROM events WHERE event_id=?1 "
                                 "AND room_id=?2", "ss", victim, room_id);
            if (owner) {
                json *plc = state_content(room_id, "m.room.power_levels", "");
                bool allowed = str_eq(owner, p.user_id) ||
                               room_user_level(room_id, p.user_id) >=
                                   pl_action_level(plc, "redact", 50);
                j_free(plc);
                if (allowed)
                    db_run("UPDATE events SET redacted_by=?1 WHERE event_id=?2 "
                           "AND room_id=?3", "sss", sr.event_id, victim, room_id);
                free(owner);
            }
        }
        free(ver);
    }
    hs_unlock();
    stream_notify();

    if (sr.status) {
        send_failed(w, &sr);
    } else {
        json *b = j_obj();
        j_set(b, "event_id", j_str(sr.event_id));
        txn_remember(&p, txn_path, txn, b);
        resp_json(w, 200, b);
    }
    sr_free(&sr);
    free(txn_path);
    free(room_id);
    principal_free(&p);
}

/* ================================================================== state */

static bool alias_grammar_ok(const char *a) {
    return a && a[0] == '#' && strchr(a + 1, ':') != NULL;
}

/* "Servers SHOULD ensure that any new aliases being listed in the event are
 * valid ... and that they point to the room ID where the state event is to be
 * sent."  Aliases that are already in the event are not re-validated. */
static bool canonical_alias_ok(http_resp *w, const char *room_id,
                               const json *content) {
    json *cur = room_state_event(room_id, "m.room.canonical_alias", "");
    json *curc = cur ? j_geto(cur, "content") : NULL;
    const char *cur_alias = curc ? j_gets(curc, "alias", NULL) : NULL;
    json *cur_alt = curc ? j_geta(curc, "alt_aliases") : NULL;

    json *cands = j_arr();
    if (j_is_str(j_get(content, "alias")))
        j_push(cands, j_str(j_gets(content, "alias", "")));
    json *alt = j_geta(content, "alt_aliases");
    for (size_t i = 0; i < j_len(alt); i++)
        if (j_is_str(j_at(alt, i))) j_push(cands, j_clone(j_at(alt, i)));

    bool ok = true;
    for (size_t i = 0; ok && i < j_len(cands); i++) {
        const char *a = j_at(cands, i)->s;
        if (str_eq(a, cur_alias) || list_has(cur_alt, a, false)) continue;
        if (!alias_grammar_ok(a)) {
            char *m = str_fmt("The alias '%s' is not a valid room alias", a);
            resp_error(w, 400, "M_INVALID_PARAMETER", m);
            free(m);
            ok = false;
            break;
        }
        char *target = resolve_room(a);
        if (!target || !str_eq(target, room_id)) {
            char *m = str_fmt("The alias '%s' does not point to this room.", a);
            resp_error(w, 400, "M_BAD_ALIAS", m);
            free(m);
            ok = false;
        }
        free(target);
    }
    j_free(cands);
    j_free(cur);
    return ok;
}

static void h_state_put(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *body = require_body(r, w);
    if (!body) { principal_free(&p); return; }

    char *room_id = resolve_room(r->part[0]);
    if (!room_id) {
        resp_error(w, 404, "M_NOT_FOUND", "Unknown room");
        principal_free(&p);
        return;
    }
    const char *type = r->part[1];
    const char *sk = r->nparts > 2 ? r->part[2] : "";
    if (!event_size_ok(w, body, type, sk)) {
        free(room_id); principal_free(&p);
        return;
    }
    if (str_eq(type, "m.room.canonical_alias") &&
        !canonical_alias_ok(w, room_id, body)) {
        free(room_id); principal_free(&p);
        return;
    }
    /* The published content schemas type their properties: a `topic` that is
     * not a string is not an `m.room.topic`, and storing it would leave a room
     * whose topic no client can render. */
    if (str_eq(type, "m.room.topic")) {
        const json *t = j_get(body, "topic");
        if (t && !j_is_str(t)) {
            resp_error(w, 400, "M_INVALID_PARAM", "topic must be a string");
            free(room_id); principal_free(&p);
            return;
        }
    }

    hs_lock();
    send_result sr = room_send(room_id, p.user_id, type, sk, j_clone(body), NULL);
    hs_unlock();
    stream_notify();

    if (sr.status) {
        send_failed(w, &sr);
    } else {
        json *b = j_obj();
        j_set(b, "event_id", j_str(sr.event_id));
        resp_json(w, 200, b);
    }
    sr_free(&sr);
    free(room_id);
    principal_free(&p);
}

static void h_state_all(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    char *room_id = resolve_room(r->part[0]);
    if (!room_id) {
        resp_error(w, 404, "M_NOT_FOUND", "Unknown room");
        principal_free(&p);
        return;
    }
    if (!may_see_state(room_id, p.user_id)) {
        resp_error(w, 403, "M_FORBIDDEN",
                   "You aren't a member of the room and weren't previously a "
                   "member of the room.");
        free(room_id); principal_free(&p);
        return;
    }
    json *state = state_for_user(room_id, p.user_id, 0);
    json *out = j_arr();
    for (size_t i = 0; i < j_len(state); i++) {
        json *ev = event_to_client(j_at(state, i), p.user_id, p.device_id, true);
        if (ev) j_push(out, ev);
    }
    j_free(state);
    resp_json(w, 200, out);
    free(room_id);
    principal_free(&p);
}

/* v1.1 returns only the content of the state event, not the whole event. */
static void h_state_key(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    char *room_id = resolve_room(r->part[0]);
    if (!room_id) {
        resp_error(w, 404, "M_NOT_FOUND", "Unknown room");
        principal_free(&p);
        return;
    }
    if (!may_see_state(room_id, p.user_id)) {
        resp_error(w, 403, "M_FORBIDDEN",
                   "You aren't a member of the room and weren't previously a "
                   "member of the room.");
        free(room_id); principal_free(&p);
        return;
    }
    const char *type = r->part[1];
    const char *sk = r->nparts > 2 ? r->part[2] : "";
    json *state = state_for_user(room_id, p.user_id, 0);
    json *stored = state_lookup(state, type, sk);
    json *ev = stored ? event_to_client(stored, p.user_id, p.device_id, true) : NULL;
    j_free(state);
    if (!ev) {
        resp_error(w, 404, "M_NOT_FOUND",
                   "The room has no state with the given type or key.");
    } else {
        json *c = j_geto(ev, "content");
        resp_json(w, 200, c ? j_clone(c) : j_obj());
        j_free(ev);
    }
    free(room_id);
    principal_free(&p);
}

/* ================================================================ members */

static void h_members(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    char *room_id = resolve_room(r->part[0]);
    if (!room_id) {
        resp_error(w, 404, "M_NOT_FOUND", "Unknown room");
        principal_free(&p);
        return;
    }
    if (!may_see_state(room_id, p.user_id)) {
        resp_error(w, 403, "M_FORBIDDEN",
                   "You aren't a member of the room and weren't previously a "
                   "member of the room.");
        free(room_id); principal_free(&p);
        return;
    }
    const char *want = http_query(r, "membership");
    const char *unwanted = http_query(r, "not_membership");
    int64_t at = 0;
    const char *atq = http_query(r, "at");
    if (atq && *atq && !token_parse(atq, &at)) {
        resp_error(w, 400, "M_INVALID_PARAM", "Invalid 'at' token");
        free(room_id); principal_free(&p);
        return;
    }

    json *state = state_for_user(room_id, p.user_id, at);
    json *chunk = j_arr();
    for (size_t i = 0; i < j_len(state); i++) {
        json *stored = j_at(state, i);
        if (!str_eq(j_gets(stored, "type", ""), "m.room.member")) continue;
        json *c = j_geto(stored, "content");
        const char *ms = c ? j_gets(c, "membership", "leave") : "leave";
        bool keep;
        if (want && unwanted) keep = str_eq(ms, want) || !str_eq(ms, unwanted);
        else if (want) keep = str_eq(ms, want);
        else if (unwanted) keep = !str_eq(ms, unwanted);
        else keep = true;
        if (!keep) continue;
        json *ev = event_to_client(stored, p.user_id, p.device_id, true);
        if (ev) j_push(chunk, ev);
    }
    j_free(state);
    json *b = j_obj();
    j_set(b, "chunk", chunk);
    resp_json(w, 200, b);
    free(room_id);
    principal_free(&p);
}

static void h_joined_members(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    char *room_id = resolve_room(r->part[0]);
    if (!room_id) {
        resp_error(w, 404, "M_NOT_FOUND", "Unknown room");
        principal_free(&p);
        return;
    }
    if (!check_membership(w, room_id, p.user_id, "join")) {
        free(room_id); principal_free(&p);
        return;
    }
    json *joined = j_obj();
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(), "SELECT user_id FROM room_members WHERE "
                                 "room_id=?1 AND membership='join' ORDER BY user_id",
                           -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, room_id, -1, SQLITE_TRANSIENT);
        while (sqlite3_step(st) == SQLITE_ROW) {
            const char *uid = (const char *)sqlite3_column_text(st, 0);
            json *c = state_content(room_id, "m.room.member", uid);
            json *m = j_obj();
            const char *dn = c ? j_gets(c, "displayname", NULL) : NULL;
            const char *av = c ? j_gets(c, "avatar_url", NULL) : NULL;
            if (dn) j_set(m, "display_name", j_str(dn));
            if (av) j_set(m, "avatar_url", j_str(av));
            j_free(c);
            j_set(joined, uid, m);
        }
        sqlite3_finalize(st);
    }
    json *b = j_obj();
    j_set(b, "joined", joined);
    resp_json(w, 200, b);
    free(room_id);
    principal_free(&p);
}

static void h_joined_rooms(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *arr = j_arr();
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(), "SELECT room_id FROM room_members WHERE "
                                 "user_id=?1 AND membership='join' ORDER BY room_id",
                           -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, p.user_id, -1, SQLITE_TRANSIENT);
        while (sqlite3_step(st) == SQLITE_ROW)
            j_push(arr, j_str((const char *)sqlite3_column_text(st, 0)));
        sqlite3_finalize(st);
    }
    json *b = j_obj();
    j_set(b, "joined_rooms", arr);
    resp_json(w, 200, b);
    principal_free(&p);
}

/* ================================================================ history */

/* Turns one stored row into the client form, applying history visibility and
 * the request's RoomEventFilter.  Returns NULL when the event is not to be
 * shown. */
static json *visible_event(const char *js, int64_t sid, const char *room_id,
                           const principal *p, const json *filter) {
    if (!js || !room_can_see(room_id, p->user_id, sid)) return NULL;
    const char *err = NULL;
    json *stored = j_parse(js, strlen(js), &err);
    if (!stored) return NULL;
    json *ev = event_to_client(stored, p->user_id, p->device_id, true);
    j_free(stored);
    if (ev && filter && !filter_event_allowed(filter, ev, room_id)) {
        j_free(ev);
        return NULL;
    }
    return ev;
}

/* Walks the room's event stream in one direction, collecting at most `limit`
 * visible events.  `*boundary` receives the token position just past the last
 * row consumed and `*more` whether the stream continued beyond it. */
static json *paginate(const char *room_id, const principal *p, const json *filter,
                      bool backwards, int64_t from_topo, int64_t from_stream,
                      bool have_to, int64_t to_topo, int64_t to_stream,
                      int64_t limit, int64_t *b_topo, int64_t *b_stream,
                      bool *more) {
    json *chunk = j_arr();
    *b_topo = from_topo;
    *b_stream = from_stream;
    *more = false;

    /* Ordered by where an event sits in the room, with the stream position
     * only breaking ties, so that history this server learned late still reads
     * in the order it happened. */
    char *sql;
    if (backwards)
        sql = str_fmt("SELECT topological, stream_id, json FROM events WHERE "
                      "room_id=?1 AND (topological<?2 OR (topological=?2 AND "
                      "stream_id<=?3))%s ORDER BY topological DESC, "
                      "stream_id DESC",
                      have_to ? " AND (topological>?4 OR (topological=?4 AND "
                                "stream_id>?5))" : "");
    else
        sql = str_fmt("SELECT topological, stream_id, json FROM events WHERE "
                      "room_id=?1 AND (topological>?2 OR (topological=?2 AND "
                      "stream_id>?3))%s ORDER BY topological ASC, "
                      "stream_id ASC",
                      have_to ? " AND (topological<?4 OR (topological=?4 AND "
                                "stream_id<=?5))" : "");

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(), sql, -1, &st, NULL) != SQLITE_OK) {
        free(sql);
        return chunk;
    }
    free(sql);
    sqlite3_bind_text(st, 1, room_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, from_topo);
    sqlite3_bind_int64(st, 3, from_stream);
    if (have_to) {
        sqlite3_bind_int64(st, 4, to_topo);
        sqlite3_bind_int64(st, 5, to_stream);
    }

    while (sqlite3_step(st) == SQLITE_ROW) {
        if ((int64_t)j_len(chunk) >= limit) { *more = true; break; }
        int64_t topo = sqlite3_column_int64(st, 0);
        int64_t sid = sqlite3_column_int64(st, 1);
        json *ev = visible_event((const char *)sqlite3_column_text(st, 2), sid,
                                 room_id, p, filter);
        *b_topo = topo;
        *b_stream = backwards ? sid - 1 : sid;
        if (ev) j_push(chunk, ev);
    }
    sqlite3_finalize(st);
    return chunk;
}

static void h_messages(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    char *room_id = resolve_room(r->part[0]);
    if (!room_id) {
        resp_error(w, 404, "M_NOT_FOUND", "Unknown room");
        principal_free(&p);
        return;
    }
    if (!may_read_room(room_id, p.user_id)) {
        resp_error(w, 403, "M_FORBIDDEN", "You aren't a member of the room.");
        free(room_id); principal_free(&p);
        return;
    }

    const char *dir = http_query(r, "dir");
    if (dir && *dir && !str_eq(dir, "b") && !str_eq(dir, "f")) {
        resp_error(w, 400, "M_INVALID_PARAM", "dir must be 'b' or 'f'");
        free(room_id); principal_free(&p);
        return;
    }
    bool backwards = !(dir && dir[0] == 'f');

    const char *fromq = http_query(r, "from");
    int64_t from_topo = 0, from_stream = 0;
    if (fromq && *fromq) {
        if (!token_parse2(room_id, fromq, &from_topo, &from_stream)) {
            resp_error(w, 400, "M_INVALID_PARAM", "Invalid 'from' token");
            free(room_id); principal_free(&p);
            return;
        }
    } else if (backwards) {
        from_stream = room_latest_stream(room_id);
        db_int("SELECT COALESCE(MAX(topological), 0) FROM events WHERE "
               "room_id=?1", &from_topo, "s", room_id);
    } else {
        from_topo = 0;
        from_stream = 0;
    }
    int64_t to_topo = 0, to_stream = 0;
    const char *toq = http_query(r, "to");
    bool have_to = toq && *toq && token_parse2(room_id, toq, &to_topo, &to_stream);

    const char *filter_q = http_query(r, "filter");
    json *filter = filter_load(p.user_id, filter_q);
    if (filter_q && *filter_q && !filter) {
        resp_error(w, 400, filter_q[0] == '{' ? "M_NOT_JSON" : "M_INVALID_PARAM",
                   "Malformed filter");
        free(room_id); principal_free(&p);
        return;
    }
    int64_t limit = query_int(r, "limit", filter_timeline_limit(filter, DEFAULT_LIMIT));
    limit = clamp_limit(limit);

    int64_t b_topo = from_topo, b_stream = from_stream;
    bool more = false;
    json *chunk = paginate(room_id, &p, filter, backwards, from_topo, from_stream,
                           have_to, to_topo, to_stream, limit, &b_topo,
                           &b_stream, &more);

    json *b = j_obj();
    if (fromq && *fromq) {
        j_set(b, "start", j_str(fromq));
    } else {
        char *t = token_make2(from_topo, from_stream);
        j_set(b, "start", j_str(t));
        free(t);
    }
    if (more) {
        char *t = token_make2(b_topo, b_stream);
        j_set(b, "end", j_str(t));
        free(t);
    }
    j_set(b, "chunk", chunk);
    j_set(b, "state", j_arr());
    resp_json(w, 200, b);

    j_free(filter);
    free(room_id);
    principal_free(&p);
}

static void h_context(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    char *room_id = resolve_room(r->part[0]);
    if (!room_id) {
        resp_error(w, 404, "M_NOT_FOUND", "Unknown room");
        principal_free(&p);
        return;
    }
    if (!may_read_room(room_id, p.user_id)) {
        resp_error(w, 403, "M_FORBIDDEN", "You aren't a member of the room.");
        free(room_id); principal_free(&p);
        return;
    }
    const char *event_id = r->part[1];
    int64_t sid = 0, topo = 0;
    char *evroom = event_room(event_id);
    bool known = event_get_stream(event_id, &sid, &topo) &&
                 str_eq(evroom, room_id);
    if (known && !room_can_see(room_id, p.user_id, sid)) {
        /* The event exists in this room but history visibility hides it from
         * this user, which is a refusal rather than a missing resource. */
        resp_error(w, 403, "M_FORBIDDEN",
                   "You don't have permission to view this event");
        free(evroom); free(room_id); principal_free(&p);
        return;
    }
    if (!known) {
        resp_error(w, 404, "M_NOT_FOUND", "Event not found.");
        free(evroom); free(room_id); principal_free(&p);
        return;
    }
    free(evroom);

    const char *filter_q = http_query(r, "filter");
    json *filter = filter_load(p.user_id, filter_q);
    if (filter_q && *filter_q && !filter) {
        resp_error(w, 400, filter_q[0] == '{' ? "M_NOT_JSON" : "M_INVALID_PARAM",
                   "Malformed filter");
        free(room_id); principal_free(&p);
        return;
    }
    int64_t limit = query_int(r, "limit", filter_timeline_limit(filter, DEFAULT_LIMIT));
    limit = clamp_limit(limit);
    int64_t before_limit = limit / 2;
    int64_t after_limit = limit - before_limit;

    int64_t at_topo = 0;
    db_int("SELECT COALESCE(topological, 0) FROM events WHERE event_id=?1",
           &at_topo, "s", event_id);
    int64_t start_topo = at_topo, start_at = sid - 1;
    int64_t end_topo = at_topo, end_at = sid;
    bool more = false;
    json *before = paginate(room_id, &p, filter, true, at_topo, sid - 1, false, 0, 0,
                            before_limit, &start_topo, &start_at, &more);
    json *after = paginate(room_id, &p, filter, false, at_topo, sid, false, 0, 0,
                           after_limit, &end_topo, &end_at, &more);

    json *stored = event_by_id(event_id);
    json *ev = event_to_client(stored, p.user_id, p.device_id, true);
    j_free(stored);

    /* "The state of the room at the last event returned." */
    json *state = room_state_at(room_id, end_at);
    json *state_arr = j_arr();
    for (size_t i = 0; i < j_len(state); i++) {
        json *se = event_to_client(j_at(state, i), p.user_id, p.device_id, true);
        if (!se) continue;
        if (filter && !filter_event_allowed(filter, se, room_id)) {
            j_free(se);
            continue;
        }
        j_push(state_arr, se);
    }
    j_free(state);

    json *b = j_obj();
    char *t = token_make2(start_topo, start_at);
    j_set(b, "start", j_str(t));
    free(t);
    t = token_make2(end_topo, end_at);
    j_set(b, "end", j_str(t));
    free(t);
    j_set(b, "events_before", before);
    j_set(b, "event", ev ? ev : j_obj());
    j_set(b, "events_after", after);
    j_set(b, "state", state_arr);
    resp_json(w, 200, b);

    j_free(filter);
    free(room_id);
    principal_free(&p);
}

static void h_event(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    char *room_id = resolve_room(r->part[0]);
    if (!room_id) {
        resp_error(w, 404, "M_NOT_FOUND", "Unknown room");
        principal_free(&p);
        return;
    }
    const char *event_id = r->part[1];
    int64_t sid = 0, topo = 0;
    char *evroom = event_room(event_id);
    bool ok = may_read_room(room_id, p.user_id) &&
              event_get_stream(event_id, &sid, &topo) &&
              str_eq(evroom, room_id) && room_can_see(room_id, p.user_id, sid);
    free(evroom);
    if (!ok) {
        resp_error(w, 404, "M_NOT_FOUND", "Event not found.");
        free(room_id); principal_free(&p);
        return;
    }
    json *stored = event_by_id(event_id);
    json *ev = event_to_client(stored, p.user_id, p.device_id, true);
    j_free(stored);
    resp_json(w, 200, ev ? ev : j_obj());
    free(room_id);
    principal_free(&p);
}

/* ============================================================== redaction */

static void h_redact(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    bool bad = false;
    json *body = optional_body(r, w, &bad);
    if (bad) { principal_free(&p); return; }

    char *room_id = resolve_room(r->part[0]);
    if (!room_id) {
        resp_error(w, 404, "M_NOT_FOUND", "Unknown room");
        principal_free(&p);
        return;
    }
    const char *target_id = r->part[1];
    const char *txn = r->part[2];
    char *txn_path = str_fmt("redact:%s", room_id);
    json *prev = txn_lookup(&p, txn_path, txn);
    if (prev) {
        resp_json(w, 200, prev);
        free(txn_path); free(room_id); principal_free(&p);
        return;
    }
    if (!check_membership(w, room_id, p.user_id, "join")) {
        free(txn_path); free(room_id); principal_free(&p);
        return;
    }
    json *target = event_by_id(target_id);
    char *target_room = event_room(target_id);
    if (target && !str_eq(target_room, room_id)) {
        /* A known event in a different room is a mistake; an event this server
         * has never seen is not, since the redaction may precede it. */
        resp_error(w, 400, "M_INVALID_PARAM", "Event is not in this room");
        j_free(target); free(target_room);
        free(txn_path); free(room_id); principal_free(&p);
        return;
    }
    free(target_room);

    if (target && str_eq(j_gets(target, "type", ""), "m.room.create")) {
        resp_error(w, 403, "M_FORBIDDEN", "Cannot redact the create event");
        j_free(target); free(txn_path); free(room_id); principal_free(&p);
        return;
    }

    /* A user may always redact their own event; redacting somebody else's
     * needs the room's `redact` power level.  An unknown target is not an
     * error: the redaction event is still sent, and applies if the target
     * ever arrives. */
    const char *victim = target ? j_gets(target, "sender", "") : p.user_id;
    if (!str_eq(victim, p.user_id)) {
        json *plc = state_content(room_id, "m.room.power_levels", "");
        int64_t mine = room_user_level(room_id, p.user_id);
        int64_t need = pl_action_level(plc, "redact", 50);
        j_free(plc);
        if (mine < need) {
            resp_error(w, 403, "M_FORBIDDEN",
                       "You don't have permission to redact this event");
            j_free(target); free(txn_path); free(room_id); principal_free(&p);
            return;
        }
    }
    j_free(target);

    json *content = j_obj();
    const char *reason = body ? j_gets(body, "reason", NULL) : NULL;
    if (reason) j_set(content, "reason", j_str(reason));
    j_set(content, "redacts", j_str(target_id));

    hs_lock();
    send_result sr = room_send(room_id, p.user_id, "m.room.redaction", NULL,
                               content, txn_unsigned(&p, txn));
    if (!sr.status)
        db_run("UPDATE events SET redacted_by=?1 WHERE event_id=?2 AND room_id=?3",
               "sss", sr.event_id, target_id, room_id);
    hs_unlock();
    stream_notify();

    if (sr.status) {
        send_failed(w, &sr);
    } else {
        json *b = j_obj();
        j_set(b, "event_id", j_str(sr.event_id));
        txn_remember(&p, txn_path, txn, b);
        resp_json(w, 200, b);
    }
    sr_free(&sr);
    free(txn_path);
    free(room_id);
    principal_free(&p);
}

/* ============================================================ membership */

/* Runs one membership transition under the write lock and writes the standard
 * error on rejection.  Returns true when the transition was made. */
static bool apply_membership(http_resp *w, const char *room_id, const char *sender,
                             const char *target, const char *membership,
                             const char *reason, json *extra) {
    hs_lock();
    send_result sr = room_set_membership(room_id, sender, target, membership,
                                         reason, extra);
    hs_unlock();
    stream_notify();
    bool ok = sr.status == 0;
    if (!ok) send_failed(w, &sr);
    sr_free(&sr);
    return ok;
}

/* The endpoints that name their target in the body: invite, kick, ban, unban. */
static void target_action(http_req *r, http_resp *w, const char *membership) {
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *body = require_body(r, w);
    if (!body) { principal_free(&p); return; }

    char *room_id = resolve_room(r->part[0]);
    if (!room_id) {
        resp_error(w, 404, "M_NOT_FOUND", "Unknown room");
        principal_free(&p);
        return;
    }
    const char *target = j_gets(body, "user_id", NULL);
    if (!target) {
        /* The third-party-identifier form of /invite is a different endpoint
         * which this server does not implement. */
        if (j_gets(body, "address", NULL))
            resp_error(w, 400, "M_UNRECOGNIZED",
                       "Third party invites are not supported by this server");
        else
            resp_error(w, 400, "M_MISSING_PARAM", "user_id is required");
        free(room_id); principal_free(&p);
        return;
    }
    if (!valid_user_id(target)) {
        resp_error(w, 400, "M_INVALID_PARAM", "user_id is not a valid user id");
        free(room_id); principal_free(&p);
        return;
    }
    const char *reason = j_gets(body, "reason", NULL);
    char *current = room_membership(room_id, target);

    if (str_eq(membership, "leave")) {                       /* kick */
        if (!current || (!str_eq(current, "join") && !str_eq(current, "invite"))) {
            resp_error(w, 403, "M_FORBIDDEN", "The target user is not in the room");
            free(current); free(room_id); principal_free(&p);
            return;
        }
    } else if (str_eq(membership, "unban")) {
        if (!current || !str_eq(current, "ban")) {
            resp_error(w, 403, "M_FORBIDDEN", "The target user is not banned");
            free(current); free(room_id); principal_free(&p);
            return;
        }
        membership = "leave";
    }
    free(current);

    /* An invitation to a user on another server carries the profile that
     * server publishes, so the invited user is recognisable in the stripped
     * state a client is shown before it joins.  The lookup is a network call,
     * so it happens here rather than inside the room engine, which runs under
     * the write lock. */
    json *extra = NULL;
    if (str_eq(membership, "invite") && !fed_is_local_user(target)) {
        json *profile = fed_query_profile(target, NULL);
        if (profile) {
            const char *dn = j_gets(profile, "displayname", NULL);
            const char *av = j_gets(profile, "avatar_url", NULL);
            if (dn || av) {
                extra = j_obj();
                if (dn) j_set(extra, "displayname", j_str(dn));
                if (av) j_set(extra, "avatar_url", j_str(av));
            }
            j_free(profile);
        }
    }

    /* `room_set_membership` takes ownership of the extra content. */
    if (apply_membership(w, room_id, p.user_id, target, membership, reason, extra))
        resp_json(w, 200, j_obj());
    free(room_id);
    principal_free(&p);
}

static void h_invite(http_req *r, http_resp *w, void *ud) {
    (void)ud; target_action(r, w, "invite");
}
static void h_kick(http_req *r, http_resp *w, void *ud) {
    (void)ud; target_action(r, w, "leave");
}
static void h_ban(http_req *r, http_resp *w, void *ud) {
    (void)ud; target_action(r, w, "ban");
}
static void h_unban(http_req *r, http_resp *w, void *ud) {
    (void)ud; target_action(r, w, "unban");
}

/* POST /rooms/{roomId}/join and POST /join/{roomIdOrAlias}. */
static void join_room(http_req *r, http_resp *w, const char *id_or_alias) {
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    bool bad = false;
    json *body = optional_body(r, w, &bad);
    if (bad) { principal_free(&p); return; }

    const char *servers[8];
    int nservers = http_query_all(r, "server_name", servers, 8);

    char *room_id = resolve_room(id_or_alias);
    /* "In this server" means this server still has a member in the room, not
     * merely that it once did: once the last local member leaves, rejoining has
     * to go back over federation, which is what picks up everything said while
     * this server was away. */
    bool joined_here = room_id && room_exists(room_id) &&
                       room_latest_stream(room_id) > 0 &&
                       room_has_local_member(room_id);
    if (!joined_here) {
        /* Not a room this server is in: join it over federation, through the
         * servers the client suggested or the one named by the identifier. */
        int fstatus = 0;
        const char *ferrcode = NULL;
        char *errmsg = NULL;
        char *remote = fed_join_remote(p.user_id, room_id ? room_id : id_or_alias,
                                       servers, nservers, &fstatus, &ferrcode,
                                       &errmsg);
        if (remote) {
            free(room_id);
            json *b = j_obj();
            j_set(b, "room_id", j_str(remote));
            resp_json(w, 200, b);
            free(remote); free(errmsg); principal_free(&p);
            return;
        }
        /* The remote server's own refusal is the answer, not a local 404. */
        if (fstatus) {
            resp_error(w, fstatus, ferrcode ? ferrcode : "M_UNKNOWN",
                       errmsg ? errmsg : "The room could not be joined");
            free(errmsg); free(room_id); principal_free(&p);
            return;
        }
        free(errmsg);
    }
    if (!room_id) {
        resp_error(w, 404, "M_NOT_FOUND", "Unknown room");
        principal_free(&p);
        return;
    }
    char *current = room_membership(room_id, p.user_id);
    bool already = current && str_eq(current, "join");
    free(current);

    const char *reason = body ? j_gets(body, "reason", NULL) : NULL;
    if (already ||
        apply_membership(w, room_id, p.user_id, p.user_id, "join", reason, NULL)) {
        json *b = j_obj();
        j_set(b, "room_id", j_str(room_id));
        resp_json(w, 200, b);
    }
    free(room_id);
    principal_free(&p);
}

/* POST /knock/{roomIdOrAlias} — room version 7 and later. */
static void h_knock(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    bool bad = false;
    json *body = optional_body(r, w, &bad);
    if (bad) { principal_free(&p); return; }
    const char *servers[8];
    int nservers = http_query_all(r, "server_name", servers, 8);
    const char *reason = body ? j_gets(body, "reason", NULL) : NULL;

    char *room_id = resolve_room(r->part[0]);
    if (room_id && room_latest_stream(room_id) > 0) {
        if (apply_membership(w, room_id, p.user_id, p.user_id, "knock",
                             reason, NULL)) {
            json *b = j_obj();
            j_set(b, "room_id", j_str(room_id));
            resp_json(w, 200, b);
        }
        free(room_id);
        principal_free(&p);
        return;
    }
    int fstatus = 0;
    const char *ferrcode = NULL;
    char *errmsg = NULL;
    json *knock_content = j_obj();
    if (reason) j_set(knock_content, "reason", j_str(reason));
    /* `fed_knock_remote` takes ownership of the content it is given. */
    bool ok = fed_knock_remote(p.user_id, room_id ? room_id : r->part[0],
                               servers, nservers, knock_content, &fstatus,
                               &ferrcode, &errmsg);
    if (ok) {
        char *rid = room_id ? xstrdup_(room_id) : resolve_room(r->part[0]);
        json *b = j_obj();
        j_set(b, "room_id", j_str(rid ? rid : r->part[0]));
        resp_json(w, 200, b);
        free(rid);
    } else {
        /* With no server named and no room here, there was nothing to ask:
         * that is a room this server does not know, not a failure of one. */
        int status = fstatus ? fstatus : 404;
        const char *code = ferrcode ? ferrcode : "M_NOT_FOUND";
        if (status >= 500 && nservers == 0 && !room_id) {
            status = 404;
            code = "M_NOT_FOUND";
        }
        resp_error(w, status, code, errmsg ? errmsg : "Unknown room");
    }
    free(errmsg);
    free(room_id);
    principal_free(&p);
}

static void h_join_by_id(http_req *r, http_resp *w, void *ud) {
    (void)ud; join_room(r, w, r->part[0]);
}
static void h_join_by_alias(http_req *r, http_resp *w, void *ud) {
    (void)ud; join_room(r, w, r->part[0]);
}

static void h_leave(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    bool bad = false;
    json *body = optional_body(r, w, &bad);
    if (bad) { principal_free(&p); return; }

    char *room_id = resolve_room(r->part[0]);
    if (!room_id) {
        resp_error(w, 404, "M_NOT_FOUND", "Unknown room");
        principal_free(&p);
        return;
    }
    char *current = room_membership(room_id, p.user_id);
    bool gone = !current || str_eq(current, "leave");
    const char *reason = body ? j_gets(body, "reason", NULL) : NULL;
    if (current && (str_eq(current, "invite") || str_eq(current, "knock")) &&
        room_latest_stream(room_id) == 0) {
        int fstatus = 0;
        const char *ferrcode = NULL;
        char *errmsg = NULL;
        bool ok = fed_leave_remote(p.user_id, room_id, NULL, 0, &fstatus,
                                   &ferrcode, &errmsg);
        if (ok) resp_json(w, 200, j_obj());
        else resp_error(w, fstatus ? fstatus : 403,
                        ferrcode ? ferrcode : "M_FORBIDDEN",
                        errmsg ? errmsg : "Could not leave");
        free(errmsg); free(current); free(room_id); principal_free(&p);
        return;
    }
    free(current);
    if (gone) {
        resp_error(w, 403, "M_FORBIDDEN", "You are not in the room");
    } else if (apply_membership(w, room_id, p.user_id, p.user_id, "leave",
                                reason, NULL)) {
        resp_json(w, 200, j_obj());
    }
    free(room_id);
    principal_free(&p);
}

static void h_forget(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    char *room_id = resolve_room(r->part[0]);
    if (!room_id) {
        resp_error(w, 404, "M_NOT_FOUND", "Unknown room");
        principal_free(&p);
        return;
    }
    char *current = room_membership(room_id, p.user_id);
    if (current && str_eq(current, "join")) {
        char *m = str_fmt("User %s is in room %s", p.user_id, room_id);
        resp_error(w, 400, "M_UNKNOWN", m);
        free(m); free(current); free(room_id); principal_free(&p);
        return;
    }
    free(current);
    hs_lock();
    db_run("DELETE FROM room_members WHERE room_id=?1 AND user_id=?2",
           "ss", room_id, p.user_id);
    hs_unlock();
    stream_notify();
    resp_json(w, 200, j_obj());
    free(room_id);
    principal_free(&p);
}

/* ============================================================== directory */

static void h_alias_put(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *body = require_body(r, w);
    if (!body) { principal_free(&p); return; }
    const char *alias = r->part[0];
    const char *room_id = j_gets(body, "room_id", NULL);
    if (!room_id) {
        resp_error(w, 400, "M_MISSING_PARAM", "room_id is required");
        principal_free(&p);
        return;
    }
    if (!alias_grammar_ok(alias)) {
        resp_error(w, 400, "M_INVALID_PARAM", "Invalid room alias");
        principal_free(&p);
        return;
    }
    const char *domain = strchr(alias, ':');
    if (!domain || !str_eq(domain + 1, CFG.server_name)) {
        resp_error(w, 400, "M_INVALID_PARAM",
                   "Room alias must be on this server");
        principal_free(&p);
        return;
    }
    char *existing = db_str("SELECT room_id FROM aliases WHERE alias=?1", "s", alias);
    if (existing) {
        free(existing);
        char *m = str_fmt("Room alias %s already exists.", alias);
        resp_error(w, 409, "M_ROOM_IN_USE", m);
        free(m);
        principal_free(&p);
        return;
    }
    hs_lock();
    db_run("INSERT INTO aliases(alias,room_id,creator) VALUES(?1,?2,?3)",
           "sss", alias, room_id, p.user_id);
    hs_unlock();
    stream_notify();
    resp_json(w, 200, j_obj());
    principal_free(&p);
}

static void h_alias_get(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    const char *alias = r->part[0];
    char *room_id = db_str("SELECT room_id FROM aliases WHERE alias=?1", "s", alias);
    if (!room_id) {
        char *m = str_fmt("Room alias %s not found.", alias);
        resp_error(w, 404, "M_NOT_FOUND", m);
        free(m);
        return;
    }
    json *b = j_obj();
    j_set(b, "room_id", j_str(room_id));
    json *servers = j_arr();
    j_push(servers, j_str(CFG.server_name));
    j_set(b, "servers", servers);
    resp_json(w, 200, b);
    free(room_id);
}

static void h_alias_delete(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    const char *alias = r->part[0];
    char *room_id = db_str("SELECT room_id FROM aliases WHERE alias=?1", "s", alias);
    if (!room_id) {
        char *m = str_fmt("Room alias %s not found.", alias);
        resp_error(w, 404, "M_NOT_FOUND", m);
        free(m);
        principal_free(&p);
        return;
    }
    /* The alias' creator may remove it; so may anyone who could publish it as
     * the room's canonical alias. */
    char *creator = db_str("SELECT creator FROM aliases WHERE alias=?1", "s", alias);
    bool allowed = str_eq(creator, p.user_id);
    free(creator);
    if (!allowed) {
        json *plc = state_content(room_id, "m.room.power_levels", "");
        allowed = room_user_level(room_id, p.user_id) >=
                  pl_event_level(plc, "m.room.canonical_alias", true);
        j_free(plc);
    }
    if (!allowed) {
        resp_error(w, 403, "M_FORBIDDEN",
                   "You don't have permission to delete this alias.");
        free(room_id); principal_free(&p);
        return;
    }
    hs_lock();
    db_run("DELETE FROM aliases WHERE alias=?1", "s", alias);
    hs_unlock();
    stream_notify();
    resp_json(w, 200, j_obj());
    free(room_id);
    principal_free(&p);
}

static void h_room_aliases(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    char *room_id = resolve_room(r->part[0]);
    if (!room_id) {
        resp_error(w, 404, "M_NOT_FOUND", "Unknown room");
        principal_free(&p);
        return;
    }
    if (!may_see_state(room_id, p.user_id)) {
        resp_error(w, 403, "M_FORBIDDEN", "You are not a member of the room.");
        free(room_id); principal_free(&p);
        return;
    }
    json *arr = j_arr();
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(), "SELECT alias FROM aliases WHERE room_id=?1 "
                                 "ORDER BY alias", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, room_id, -1, SQLITE_TRANSIENT);
        while (sqlite3_step(st) == SQLITE_ROW)
            j_push(arr, j_str((const char *)sqlite3_column_text(st, 0)));
        sqlite3_finalize(st);
    }
    json *b = j_obj();
    j_set(b, "aliases", arr);
    resp_json(w, 200, b);
    free(room_id);
    principal_free(&p);
}

static void h_visibility_get(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    const char *room_id = r->part[0];
    if (!room_exists(room_id)) {
        resp_error(w, 404, "M_NOT_FOUND", "Room not found");
        return;
    }
    int64_t pub = 0;
    db_int("SELECT published FROM rooms WHERE room_id=?1", &pub, "s", room_id);
    json *b = j_obj();
    j_set(b, "visibility", j_str(pub ? "public" : "private"));
    resp_json(w, 200, b);
}

static void h_visibility_put(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *body = require_body(r, w);
    if (!body) { principal_free(&p); return; }
    const char *room_id = r->part[0];
    if (!room_exists(room_id)) {
        resp_error(w, 404, "M_NOT_FOUND", "Room not found");
        principal_free(&p);
        return;
    }
    const char *vis = j_gets(body, "visibility", "public");
    if (!str_eq(vis, "public") && !str_eq(vis, "private")) {
        resp_error(w, 400, "M_INVALID_PARAM",
                   "visibility must be 'public' or 'private'");
        principal_free(&p);
        return;
    }
    hs_lock();
    db_run("UPDATE rooms SET published=?2 WHERE room_id=?1", "si", room_id,
           (int64_t)(str_eq(vis, "public") ? 1 : 0));
    hs_unlock();
    stream_notify();
    resp_json(w, 200, j_obj());
    principal_free(&p);
}

/* =========================================================== public rooms */

static bool contains_ci(const char *hay, const char *needle) {
    if (!hay || !needle || !*needle) return false;
    size_t n = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] && (p[i] | 0x20) == (needle[i] | 0x20)) i++;
        if (i == n) return true;
    }
    return false;
}

static json *public_room_entry(const char *room_id) {
    json *e = j_obj();
    j_set(e, "room_id", j_str(room_id));
    j_set(e, "num_joined_members", j_int(room_joined_count(room_id)));

    json *c = state_content(room_id, "m.room.name", "");
    if (c && j_is_str(j_get(c, "name"))) j_set(e, "name", j_str(j_gets(c, "name", "")));
    j_free(c);
    c = state_content(room_id, "m.room.topic", "");
    if (c && j_is_str(j_get(c, "topic"))) j_set(e, "topic", j_str(j_gets(c, "topic", "")));
    j_free(c);
    c = state_content(room_id, "m.room.avatar", "");
    if (c && j_is_str(j_get(c, "url"))) j_set(e, "avatar_url", j_str(j_gets(c, "url", "")));
    j_free(c);
    c = state_content(room_id, "m.room.canonical_alias", "");
    if (c && j_is_str(j_get(c, "alias")))
        j_set(e, "canonical_alias", j_str(j_gets(c, "alias", "")));
    j_free(c);

    c = state_content(room_id, "m.room.join_rules", "");
    const char *jr = c ? j_gets(c, "join_rule", "public") : "public";
    j_set(e, "join_rule", j_str(jr));
    j_free(c);

    c = state_content(room_id, "m.room.history_visibility", "");
    bool wr = c && str_eq(j_gets(c, "history_visibility", "shared"), "world_readable");
    j_free(c);
    j_set(e, "world_readable", j_bool(wr));

    c = state_content(room_id, "m.room.guest_access", "");
    bool guests = c && str_eq(j_gets(c, "guest_access", "forbidden"), "can_join");
    j_free(c);
    j_set(e, "guest_can_join", j_bool(guests));

    json *aliases = j_arr();
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(), "SELECT alias FROM aliases WHERE room_id=?1 "
                                 "ORDER BY alias", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, room_id, -1, SQLITE_TRANSIENT);
        while (sqlite3_step(st) == SQLITE_ROW)
            j_push(aliases, j_str((const char *)sqlite3_column_text(st, 0)));
        sqlite3_finalize(st);
    }
    j_set(e, "aliases", aliases);
    return e;
}

static int by_members_desc(const void *a, const void *b) {
    const json *x = *(json *const *)a, *y = *(json *const *)b;
    int64_t nx = j_geti(x, "num_joined_members", 0);
    int64_t ny = j_geti(y, "num_joined_members", 0);
    if (nx != ny) return nx < ny ? 1 : -1;
    return strcmp(j_gets(x, "room_id", ""), j_gets(y, "room_id", ""));
}

static bool entry_matches(const json *e, const char *term) {
    if (!term || !*term) return true;
    return contains_ci(j_gets(e, "name", NULL), term) ||
           contains_ci(j_gets(e, "topic", NULL), term) ||
           contains_ci(j_gets(e, "canonical_alias", NULL), term) ||
           contains_ci(j_gets(e, "room_id", NULL), term);
}

static void public_rooms(http_resp *w, int64_t limit, const char *since,
                         const char *term) {
    json **rooms = NULL;
    size_t n = 0, cap = 0;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(), "SELECT room_id FROM rooms WHERE published=1",
                           -1, &st, NULL) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            json *e = public_room_entry((const char *)sqlite3_column_text(st, 0));
            if (!entry_matches(e, term)) { j_free(e); continue; }
            if (n == cap) {
                cap = cap ? cap * 2 : 16;
                rooms = realloc(rooms, cap * sizeof *rooms);
                if (!rooms) abort();
            }
            rooms[n++] = e;
        }
        sqlite3_finalize(st);
    }
    if (n > 1) qsort(rooms, n, sizeof *rooms, by_members_desc);

    int64_t offset = 0;
    if (since && *since) token_parse(since, &offset);
    if (offset < 0) offset = 0;
    if (offset > (int64_t)n) offset = (int64_t)n;
    if (limit <= 0) limit = (int64_t)n;

    json *chunk = j_arr();
    int64_t i = offset;
    for (; i < (int64_t)n && (int64_t)j_len(chunk) < limit; i++) {
        j_push(chunk, rooms[i]);
        rooms[i] = NULL;
    }
    for (size_t k = 0; k < n; k++) j_free(rooms[k]);
    free(rooms);

    json *b = j_obj();
    j_set(b, "chunk", chunk);
    j_set(b, "total_room_count_estimate", j_int((int64_t)n));
    if (i < (int64_t)n) {
        char *t = str_fmt("p%" PRId64, i);
        j_set(b, "next_batch", j_str(t));
        free(t);
    }
    if (offset > 0) {
        int64_t prev = offset - limit;
        char *t = str_fmt("p%" PRId64, prev < 0 ? 0 : prev);
        j_set(b, "prev_batch", j_str(t));
        free(t);
    }
    resp_json(w, 200, b);
}

static void h_public_rooms_get(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    int64_t limit = query_int(r, "limit", 0);
    public_rooms(w, limit, http_query(r, "since"), NULL);
}

static void h_public_rooms_post(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    bool bad = false;
    json *body = optional_body(r, w, &bad);
    if (bad) { principal_free(&p); return; }
    int64_t limit = body ? j_geti(body, "limit", 0) : 0;
    const char *since = body ? j_gets(body, "since", NULL) : NULL;
    json *f = body ? j_geto(body, "filter") : NULL;
    const char *term = f ? j_gets(f, "generic_search_term", NULL) : NULL;
    public_rooms(w, limit, since, term);
    principal_free(&p);
}

/* ================================================================ upgrade */

/* The state the room-upgrades module recommends transferring.  Power levels
 * come last so that copying them cannot demote the upgrading user before the
 * rest of the state has been written. */
static const char *UPGRADE_STATE[] = {
    "m.room.server_acl", "m.room.encryption", "m.room.name", "m.room.avatar",
    "m.room.topic", "m.room.guest_access", "m.room.history_visibility",
    "m.room.join_rules", "m.room.power_levels", NULL
};

static void h_upgrade(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *body = require_body(r, w);
    if (!body) { principal_free(&p); return; }

    const char *new_version = j_gets(body, "new_version", NULL);
    if (!new_version) {
        resp_error(w, 400, "M_MISSING_PARAM", "new_version is required");
        principal_free(&p);
        return;
    }
    if (!rv_get(new_version)) {
        resp_error(w, 400, "M_UNSUPPORTED_ROOM_VERSION",
                   "This server does not support that room version");
        principal_free(&p);
        return;
    }
    char *room_id = resolve_room(r->part[0]);
    if (!room_id) {
        resp_error(w, 404, "M_NOT_FOUND", "Unknown room");
        principal_free(&p);
        return;
    }
    if (!check_membership(w, room_id, p.user_id, "join")) {
        free(room_id); principal_free(&p);
        return;
    }
    json *plc = state_content(room_id, "m.room.power_levels", "");
    if (room_user_level(room_id, p.user_id) <
        pl_event_level(plc, "m.room.tombstone", true)) {
        resp_error(w, 403, "M_FORBIDDEN", "You cannot upgrade this room");
        j_free(plc); free(room_id); principal_free(&p);
        return;
    }
    j_free(plc);

    hs_lock();

    /* 1. the replacement room, whose m.room.create points back here */
    json *create_body = j_obj();
    j_set(create_body, "room_version", j_str(new_version));
    int64_t published = 0;
    db_int("SELECT published FROM rooms WHERE room_id=?1", &published, "s", room_id);
    j_set(create_body, "visibility", j_str(published ? "public" : "private"));
    json *old_jr = state_content(room_id, "m.room.join_rules", "");
    bool was_public = old_jr && str_eq(j_gets(old_jr, "join_rule", "invite"), "public");
    j_free(old_jr);
    j_set(create_body, "preset", j_str(was_public ? "public_chat" : "private_chat"));

    json *cc = j_obj();
    json *old_create = state_content(room_id, "m.room.create", "");
    for (size_t i = 0; old_create && i < j_len(old_create); i++) {
        const char *k = j_key(old_create, i);
        if (str_eq(k, "creator") || str_eq(k, "room_version") ||
            str_eq(k, "predecessor")) continue;
        j_set(cc, k, j_clone(j_at(old_create, i)));
    }
    j_free(old_create);
    char *last_event = db_str("SELECT event_id FROM events WHERE room_id=?1 "
                              "ORDER BY stream_id DESC LIMIT 1", "s", room_id);
    json *pred = j_obj();
    j_set(pred, "room_id", j_str(room_id));
    if (last_event) j_set(pred, "event_id", j_str(last_event));
    free(last_event);
    j_set(cc, "predecessor", pred);
    j_set(create_body, "creation_content", cc);

    int status = 0;
    const char *errcode = NULL;
    char *errmsg = NULL;
    char *new_room = room_create(p.user_id, create_body, &status, &errcode, &errmsg);
    j_free(create_body);
    if (!new_room) {
        hs_unlock();
        resp_error(w, status ? status : 400, errcode ? errcode : "M_UNKNOWN",
                   errmsg ? errmsg : "The replacement room could not be created");
        free(errmsg); free(room_id); principal_free(&p);
        return;
    }
    free(errmsg);

    /* 2. transferable state */
    for (int i = 0; UPGRADE_STATE[i]; i++) {
        json *content = state_content(room_id, UPGRADE_STATE[i], "");
        if (!content) continue;
        send_result sr = room_send(new_room, p.user_id, UPGRADE_STATE[i], "",
                                   content, NULL);
        sr_free(&sr);
    }

    /* 3. the tombstone in the old room */
    json *tomb = j_obj();
    j_set(tomb, "body", j_str("This room has been replaced"));
    j_set(tomb, "replacement_room", j_str(new_room));
    send_result sr = room_send(room_id, p.user_id, "m.room.tombstone", "", tomb, NULL);
    sr_free(&sr);

    /* 4. make the old room read-only */
    json *old_pl = state_content(room_id, "m.room.power_levels", "");
    if (old_pl) {
        int64_t users_default = pl_action_level(old_pl, "users_default", 0);
        int64_t floor = users_default + 1 > 50 ? users_default + 1 : 50;
        if (pl_action_level(old_pl, "events_default", 0) < floor)
            j_set(old_pl, "events_default", j_int(floor));
        if (pl_action_level(old_pl, "invite", 0) < floor)
            j_set(old_pl, "invite", j_int(floor));
        sr = room_send(room_id, p.user_id, "m.room.power_levels", "", old_pl, NULL);
        sr_free(&sr);
    }

    /* 5. local aliases follow the room */
    db_run("UPDATE aliases SET room_id=?1 WHERE room_id=?2", "ss", new_room, room_id);

    hs_unlock();
    stream_notify();

    json *b = j_obj();
    j_set(b, "replacement_room", j_str(new_room));
    resp_json(w, 200, b);
    free(new_room);
    free(room_id);
    principal_free(&p);
}

/* ================================================================ routing */

static void route_both(http_server *s, const char *method, const char *suffix,
                       http_handler fn) {
    char *a = str_fmt("%s%s", CS, suffix);
    char *b = str_fmt("%s%s", CSR, suffix);
    http_route(s, method, a, fn);
    http_route(s, method, b, fn);
    free(a); free(b);
}

void api_register_rooms(http_server *s) {
    route_both(s, "POST", "/createRoom", h_create_room);

    route_both(s, "PUT", "/rooms/:/send/:/:", h_send);

    route_both(s, "PUT", "/rooms/:/state/:", h_state_put);
    route_both(s, "PUT", "/rooms/:/state/:/:", h_state_put);
    route_both(s, "GET", "/rooms/:/state", h_state_all);
    route_both(s, "GET", "/rooms/:/state/:", h_state_key);
    route_both(s, "GET", "/rooms/:/state/:/:", h_state_key);

    route_both(s, "GET", "/rooms/:/members", h_members);
    route_both(s, "GET", "/rooms/:/joined_members", h_joined_members);
    route_both(s, "GET", "/joined_rooms", h_joined_rooms);

    route_both(s, "GET", "/rooms/:/messages", h_messages);
    route_both(s, "GET", "/rooms/:/context/:", h_context);
    route_both(s, "GET", "/rooms/:/event/:", h_event);

    route_both(s, "POST", "/rooms/:/redact/:/:", h_redact);
    route_both(s, "PUT", "/rooms/:/redact/:/:", h_redact);

    route_both(s, "POST", "/rooms/:/invite", h_invite);
    route_both(s, "POST", "/rooms/:/join", h_join_by_id);
    route_both(s, "POST", "/rooms/:/leave", h_leave);
    route_both(s, "POST", "/rooms/:/forget", h_forget);
    route_both(s, "POST", "/rooms/:/kick", h_kick);
    route_both(s, "POST", "/rooms/:/ban", h_ban);
    route_both(s, "POST", "/rooms/:/unban", h_unban);
    route_both(s, "POST", "/join/:", h_join_by_alias);
    route_both(s, "POST", "/knock/:", h_knock);

    route_both(s, "PUT", "/directory/room/:", h_alias_put);
    route_both(s, "GET", "/directory/room/:", h_alias_get);
    route_both(s, "DELETE", "/directory/room/:", h_alias_delete);
    route_both(s, "GET", "/rooms/:/aliases", h_room_aliases);

    route_both(s, "GET", "/directory/list/room/:", h_visibility_get);
    route_both(s, "PUT", "/directory/list/room/:", h_visibility_put);
    route_both(s, "GET", "/publicRooms", h_public_rooms_get);
    route_both(s, "POST", "/publicRooms", h_public_rooms_post);

    route_both(s, "POST", "/rooms/:/upgrade", h_upgrade);
}

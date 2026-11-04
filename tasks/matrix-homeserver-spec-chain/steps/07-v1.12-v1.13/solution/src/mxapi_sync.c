#define _POSIX_C_SOURCE 200809L
/* Syncing: GET /sync, and the filter upload / download API. */
#include "mxapi.h"
#include "mxdb.h"
#include "mxkeys.h"

/* The push-rule engine arrives in a later milestone; until then this is NULL
 * and the local counter below is used. */
extern bool push_rules_enabled(void) __attribute__((weak));
extern void push_ensure_account_data(const char *user_id) __attribute__((weak));
extern bool rel_room_has_news(const char *room_id, const char *user_id,
                              int64_t since, int64_t now) __attribute__((weak));
extern void push_unread_counts(const char *user_id, const char *room_id,
                               int64_t upto, int64_t *notif, int64_t *highlight)
    __attribute__((weak));
#include "mxrel.h"
#include "mxutil.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CS "/_matrix/client/v3"
#define CSR "/_matrix/client/r0"

/* The timeline limit used when the filter carries no room.timeline.limit. */
#define TIMELINE_DEFAULT 10
#define TIMELINE_MAX     1000
/* "100 messages is recommended as a reasonable limit" (Send-to-Device). */
#define TO_DEVICE_MAX    100
#define TIMEOUT_MAX      300000
/* How far back unread_notifications counts; a read receipt normally bounds it. */
#define UNREAD_SCAN_MAX  1000

/* ------------------------------------------------------------------ misc */
/* A sync token is "s<stream_id>": one counter orders every stream. */
static char *tok_of(int64_t stream_id) { return str_fmt("s%" PRId64, stream_id); }

static bool tok_parse(const char *s, int64_t *out) {
    if (!s || s[0] != 's' || !isdigit((unsigned char)s[1])) return false;
    long long v = strtoll(s + 1, NULL, 10);
    if (v < 0) return false;
    *out = (int64_t)v;
    return true;
}

static json *parse_text(const char *text) {
    return text ? j_parse(text, strlen(text), NULL) : NULL;
}

static json *parse_col(sqlite3_stmt *st, int col) {
    return parse_text((const char *)sqlite3_column_text(st, col));
}

static const char *col_str(sqlite3_stmt *st, int col) {
    const char *s = (const char *)sqlite3_column_text(st, col);
    return s ? s : "";
}

static bool in_list(const json *arr, const char *s) {
    if (!arr || arr->type != J_ARRAY || !s) return false;
    for (size_t i = 0; i < j_len(arr); i++) {
        json *v = j_at(arr, i);
        if (j_is_str(v) && str_eq(v->s, s)) return true;
    }
    return false;
}

/* Wraps an event array in the {"events": [...]} batch every section uses. */
static json *batch(json *events) {
    json *o = j_obj();
    j_set(o, "events", events);
    return o;
}

static bool ci_contains(const char *hay, const char *needle) {
    if (!hay || !needle || !*needle) return false;
    size_t n = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == n) return true;
    }
    return false;
}

static bool query_bool(const http_req *r, const char *name, bool dflt) {
    const char *v = http_query(r, name);
    if (!v || !*v) return dflt;
    return str_eq(v, "true") || str_eq(v, "1");
}

static int64_t query_int(const http_req *r, const char *name, int64_t dflt) {
    const char *v = http_query(r, name);
    if (!v || !*v) return dflt;
    char *end = NULL;
    long long n = strtoll(v, &end, 10);
    return end == v ? dflt : (int64_t)n;
}

/* ---------------------------------------------------------- event_fields */
/* Splits one event_fields entry into its path components.  A literal '.' is
 * escaped with a backslash. */
static json *field_path(const char *spec) {
    json *parts = j_arr();
    buf cur = {0};
    for (const char *p = spec; *p; p++) {
        if (*p == '\\' && p[1]) { buf_add(&cur, p + 1, 1); p++; continue; }
        if (*p == '.') {
            char *s = buf_take(&cur, NULL);
            j_push(parts, j_str(s));
            free(s);
            continue;
        }
        buf_add(&cur, p, 1);
    }
    char *s = buf_take(&cur, NULL);
    j_push(parts, j_str(s));
    free(s);
    buf_free(&cur);
    return parts;
}

static void copy_field(const json *src, json *dst, const json *parts, size_t i) {
    if (!src || src->type != J_OBJECT || i >= j_len(parts)) return;
    const char *key = j_at(parts, i)->s;
    json *v = j_get(src, key);
    if (!v) return;
    if (i + 1 == j_len(parts)) { j_set(dst, key, j_clone(v)); return; }
    if (v->type != J_OBJECT) return;
    json *sub = j_get(dst, key);
    if (!sub || sub->type != J_OBJECT) { sub = j_obj(); j_set(dst, key, sub); }
    copy_field(v, sub, parts, i + 1);
}

/* Consumes `ev` and returns the projection asked for by `event_fields`. */
static json *project(json *ev, const json *fields) {
    if (!fields || fields->type != J_ARRAY || j_len(fields) == 0) return ev;
    json *out = j_obj();
    for (size_t i = 0; i < j_len(fields); i++) {
        json *f = j_at(fields, i);
        if (!j_is_str(f)) continue;
        json *parts = field_path(f->s);
        copy_field(ev, out, parts, 0);
        j_free(parts);
    }
    j_free(ev);
    return out;
}

/* --------------------------------------------------------------- context */
typedef struct {
    const char *user_id;
    const char *device_id;
    const json *filter;         /* borrowed, may be NULL */
    const json *f_room;
    const json *f_timeline;     /* room.timeline   RoomEventFilter */
    const json *f_state;        /* room.state      StateFilter     */
    const json *f_ephemeral;    /* room.ephemeral  RoomEventFilter */
    const json *f_room_acct;    /* room.account_data               */
    const json *f_acct;         /* account_data    EventFilter     */
    const json *f_presence;     /* presence        EventFilter     */
    const json *event_fields;
    bool federation;            /* event_format: "federation" */
    bool lazy_load;
    bool include_leave;
    int64_t limit;
    int64_t since;              /* 0 on an initial sync */
    int64_t now;                /* the token this response is built at */
    bool initial;
    bool full_state;
} sync_ctx;

static void ctx_init(sync_ctx *c, const char *user_id, const char *device_id,
                     const json *filter, int64_t since, int64_t now,
                     bool initial, bool full_state) {
    memset(c, 0, sizeof *c);
    c->user_id = user_id;
    c->device_id = device_id;
    c->filter = filter;
    c->since = since;
    c->now = now;
    c->initial = initial;
    c->full_state = full_state;
    if (filter) {
        c->f_room = j_geto(filter, "room");
        c->f_acct = j_geto(filter, "account_data");
        c->f_presence = j_geto(filter, "presence");
        c->event_fields = j_geta(filter, "event_fields");
        c->federation = str_eq(j_gets(filter, "event_format", "client"), "federation");
    }
    if (c->f_room) {
        c->f_timeline = j_geto(c->f_room, "timeline");
        c->f_state = j_geto(c->f_room, "state");
        c->f_ephemeral = j_geto(c->f_room, "ephemeral");
        c->f_room_acct = j_geto(c->f_room, "account_data");
        c->include_leave = j_getb(c->f_room, "include_leave", false);
    }
    c->lazy_load = c->f_state && j_getb(c->f_state, "lazy_load_members", false);
    c->limit = filter ? filter_timeline_limit(filter, TIMELINE_DEFAULT)
                      : TIMELINE_DEFAULT;
    if (c->limit < 0) c->limit = 0;
    if (c->limit > TIMELINE_MAX) c->limit = TIMELINE_MAX;
}

/* room.rooms / room.not_rooms are applied before every per-section filter. */
static bool room_selected(const sync_ctx *c, const char *room_id) {
    if (!c->f_room) return true;
    const json *deny = j_geta(c->f_room, "not_rooms");
    const json *only = j_geta(c->f_room, "rooms");
    if (deny && in_list(deny, room_id)) return false;
    if (only && !in_list(only, room_id)) return false;
    return true;
}

static bool pass(const json *f, const json *ev, const char *room_id) {
    return f ? filter_event_allowed(f, ev, room_id) : true;
}

/* The client form of a stored event as it appears inside a room section: no
 * room_id, and unsigned.transaction_id only for the device that sent it. */
static json *sync_event(const sync_ctx *c, const json *stored) {
    json *ev;
    if (c->federation) {
        ev = j_clone(stored);
        json *un = j_geto(ev, "unsigned");
        if (un) {
            j_del(un, "__txn_user");
            j_del(un, "__txn_device");
            j_del(un, "__txn_id");
            if (j_len(un) == 0) j_del(ev, "unsigned");
        }
    } else {
        ev = event_to_client(stored, c->user_id, c->device_id, false);
    }
    return project(ev, c->event_fields);
}

/* ------------------------------------------------------------- timelines */
typedef struct {
    json **ev;              /* stored event JSON, owned */
    int64_t *sid;
    size_t n, cap;
} evlist;

static void ev_push(evlist *l, json *e, int64_t sid) {
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        l->ev = realloc(l->ev, l->cap * sizeof *l->ev);
        l->sid = realloc(l->sid, l->cap * sizeof *l->sid);
        if (!l->ev || !l->sid) abort();
    }
    l->ev[l->n] = e;
    l->sid[l->n] = sid;
    l->n++;
}

static void ev_free(evlist *l) {
    for (size_t i = 0; i < l->n; i++) j_free(l->ev[i]);
    free(l->ev);
    free(l->sid);
    memset(l, 0, sizeof *l);
}

static void ev_reverse(evlist *l) {
    if (l->n < 2) return;
    for (size_t i = 0, k = l->n - 1; i < k; i++, k--) {
        json *e = l->ev[i]; l->ev[i] = l->ev[k]; l->ev[k] = e;
        int64_t s = l->sid[i]; l->sid[i] = l->sid[k]; l->sid[k] = s;
    }
}

/* Walks (lo, hi] backwards collecting the newest `limit` events the user may
 * see and the filter allows; `limited` says whether one more was available. */
static void collect_timeline(const sync_ctx *c, const char *room_id,
                             int64_t lo, int64_t hi, evlist *out, bool *limited) {
    *limited = false;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT stream_id, json FROM events WHERE room_id=?1 AND stream_id>?2 "
            "AND stream_id<=?3 ORDER BY stream_id DESC", -1, &st, NULL) != SQLITE_OK)
        return;
    sqlite3_bind_text(st, 1, room_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, lo);
    sqlite3_bind_int64(st, 3, hi);
    while (sqlite3_step(st) == SQLITE_ROW) {
        int64_t sid = sqlite3_column_int64(st, 0);
        if (!room_can_see(room_id, c->user_id, sid)) continue;
        json *ev = parse_col(st, 1);
        if (!ev) continue;
        if (!pass(c->f_timeline, ev, room_id) ||
            !rel_timeline_allows(c->f_timeline, room_id, ev)) { j_free(ev); continue; }
        if ((int64_t)out->n >= c->limit) { j_free(ev); *limited = true; break; }
        ev_push(out, ev, sid);
    }
    sqlite3_finalize(st);
    ev_reverse(out);
}

/* ------------------------------------------------------------------ state */
/* `full` returns the whole state at `upper`; otherwise only what changed in
 * (since, upper].  Applies room.state and, when asked, lazy-loading. */
static json *collect_state(const sync_ctx *c, const char *room_id, int64_t upper,
                           bool full, const json *senders) {
    json *map;
    if (full) {
        map = room_state_at(room_id, upper);
    } else {
        map = j_obj();
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db(),
                "SELECT type, state_key, json FROM events WHERE room_id=?1 AND "
                "is_state=1 AND stream_id>?2 AND stream_id<=?3 ORDER BY stream_id ASC",
                -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, room_id, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(st, 2, c->since);
            sqlite3_bind_int64(st, 3, upper);
            while (sqlite3_step(st) == SQLITE_ROW) {
                char *key = state_key_of(col_str(st, 0), col_str(st, 1));
                json *ev = parse_col(st, 2);
                if (ev) j_set(map, key, ev); /* the latest wins, in place */
                free(key);
            }
            sqlite3_finalize(st);
        }
    }
    json *out = j_arr();
    for (size_t i = 0; i < j_len(map); i++) {
        json *ev = j_at(map, i);
        if (c->lazy_load && str_eq(j_gets(ev, "type", ""), "m.room.member")) {
            const char *sk = j_gets(ev, "state_key", "");
            if (!str_eq(sk, c->user_id) && !in_list(senders, sk)) continue;
        }
        if (!pass(c->f_state, ev, room_id)) continue;
        j_push(out, sync_event(c, ev));
    }
    j_free(map);
    return out;
}

/* -------------------------------------------------------------- ephemeral */
static json *room_ephemeral(const sync_ctx *c, const char *room_id) {
    json *out = j_arr();

    /* m.typing: one event per room naming everyone currently typing. */
    int64_t changed = 0;
    db_int("SELECT COUNT(*) FROM typing WHERE room_id=?1 AND stream_id>?2 AND "
           "stream_id<=?3", &changed, "sii", room_id, c->since, c->now);
    if (changed > 0) {
        json *ids = j_arr();
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db(),
                "SELECT user_id FROM typing WHERE room_id=?1 AND expires_ms>?2 "
                "ORDER BY user_id ASC", -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, room_id, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(st, 2, now_ms());
            while (sqlite3_step(st) == SQLITE_ROW) j_push(ids, j_str(col_str(st, 0)));
            sqlite3_finalize(st);
        }
        /* An initial sync only reports typing that is actually in progress. */
        if (c->initial && j_len(ids) == 0) {
            j_free(ids);
        } else {
            json *content = j_obj();
            j_set(content, "user_ids", ids);
            json *ev = j_obj();
            j_set(ev, "type", j_str("m.typing"));
            j_set(ev, "content", content);
            if (pass(c->f_ephemeral, ev, room_id)) j_push(out, ev); else j_free(ev);
        }
    }

    /* m.receipt: every receipt in the room batched into one event. */
    json *content = j_obj();
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT event_id, receipt_type, user_id, ts FROM receipts WHERE "
            "room_id=?1 AND stream_id>?2 AND stream_id<=?3 ORDER BY stream_id ASC",
            -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, room_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 2, c->since);
        sqlite3_bind_int64(st, 3, c->now);
        while (sqlite3_step(st) == SQLITE_ROW) {
            const char *eid = col_str(st, 0), *rt = col_str(st, 1);
            json *by_event = j_geto(content, eid);
            if (!by_event) { by_event = j_obj(); j_set(content, eid, by_event); }
            json *by_type = j_geto(by_event, rt);
            if (!by_type) { by_type = j_obj(); j_set(by_event, rt, by_type); }
            json *who = j_obj();
            j_set(who, "ts", j_int(sqlite3_column_int64(st, 3)));
            j_set(by_type, col_str(st, 2), who);
        }
        sqlite3_finalize(st);
    }
    if (j_len(content) == 0) {
        j_free(content);
    } else {
        json *ev = j_obj();
        j_set(ev, "type", j_str("m.receipt"));
        j_set(ev, "content", content);
        if (pass(c->f_ephemeral, ev, room_id)) j_push(out, ev); else j_free(ev);
    }
    return out;
}

/* ----------------------------------------------------------- account data */
/* `room_id` is "" for the global account data. */
static json *account_data_events(const sync_ctx *c, const char *room_id,
                                 const json *f) {
    if (!*room_id && push_ensure_account_data)
        push_ensure_account_data(c->user_id);
    json *out = j_arr();
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT type, content FROM account_data WHERE user_id=?1 AND "
            "room_id=?2 AND stream_id>?3 AND stream_id<=?4 ORDER BY stream_id ASC",
            -1, &st, NULL) != SQLITE_OK) return out;
    sqlite3_bind_text(st, 1, c->user_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, room_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, c->since);
    sqlite3_bind_int64(st, 4, c->now);
    while (sqlite3_step(st) == SQLITE_ROW) {
        json *content = parse_col(st, 1);
        json *ev = j_obj();
        j_set(ev, "type", j_str(col_str(st, 0)));
        j_set(ev, "content", content ? content : j_obj());
        if (pass(f, ev, room_id)) j_push(out, ev); else j_free(ev);
    }
    sqlite3_finalize(st);
    return out;
}

static json *room_account_data(const sync_ctx *c, const char *room_id) {
    json *out = account_data_events(c, room_id, c->f_room_acct);

    /* m.tag carries the room's whole tag set whenever one of them moved. */
    int64_t changed = 0;
    db_int("SELECT COUNT(*) FROM room_tags WHERE user_id=?1 AND room_id=?2 AND "
           "stream_id>?3 AND stream_id<=?4", &changed, "ssii", c->user_id, room_id,
           c->since, c->now);
    if (changed <= 0) return out;

    json *tags = j_obj();
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT tag, content FROM room_tags WHERE user_id=?1 AND room_id=?2 AND content IS NOT NULL "
            "ORDER BY tag ASC", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, c->user_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, room_id, -1, SQLITE_TRANSIENT);
        while (sqlite3_step(st) == SQLITE_ROW) {
            json *v = parse_col(st, 1);
            j_set(tags, col_str(st, 0), v ? v : j_obj());
        }
        sqlite3_finalize(st);
    }
    json *content = j_obj();
    j_set(content, "tags", tags);
    json *ev = j_obj();
    j_set(ev, "type", j_str("m.tag"));
    j_set(ev, "content", content);
    if (pass(c->f_room_acct, ev, room_id)) j_push(out, ev); else j_free(ev);
    return out;
}

/* --------------------------------------------------- unread notifications */
/* Everything after the user's m.read receipt that somebody else sent.  A
 * highlight is an event whose body names the user. */
static void unread_counts(const sync_ctx *c, const char *room_id,
                          int64_t *notif, int64_t *highlight) {
    *notif = 0;
    *highlight = 0;
    int64_t after = 0;
    char *read = db_str("SELECT event_id FROM receipts WHERE room_id=?1 AND "
                        "user_id=?2 AND receipt_type='m.read'", "ss",
                        room_id, c->user_id);
    if (read) {
        int64_t s = 0;
        if (event_get_stream(read, &s, NULL)) after = s;
        free(read);
    }
    char *display = db_str("SELECT displayname FROM users WHERE user_id=?1",
                           "s", c->user_id);
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT json FROM events WHERE room_id=?1 AND stream_id>?2 AND "
            "stream_id<=?3 AND sender<>?4 ORDER BY stream_id ASC LIMIT ?5",
            -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, room_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 2, after);
        sqlite3_bind_int64(st, 3, c->now);
        sqlite3_bind_text(st, 4, c->user_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 5, UNREAD_SCAN_MAX);
        while (sqlite3_step(st) == SQLITE_ROW) {
            (*notif)++;
            json *ev = parse_col(st, 0);
            const char *body = ev ? j_gets(j_geto(ev, "content"), "body", NULL) : NULL;
            if (body && (ci_contains(body, c->user_id) ||
                         (display && *display && ci_contains(body, display))))
                (*highlight)++;
            j_free(ev);
        }
        sqlite3_finalize(st);
    }
    free(display);
}

/* -------------------------------------------------------- room  sections */
static json *timeline_batch(const sync_ctx *c, const evlist *tl, bool limited,
                            int64_t empty_at) {
    json *events = j_arr();
    for (size_t i = 0; i < tl->n; i++) j_push(events, sync_event(c, tl->ev[i]));
    json *out = j_obj();
    j_set(out, "events", events);
    j_set(out, "limited", j_bool(limited));
    char *prev = tok_of(tl->n ? tl->sid[0] - 1 : empty_at);
    j_set(out, "prev_batch", j_str(prev));
    free(prev);
    return out;
}

static json *timeline_senders(const evlist *tl) {
    json *out = j_arr();
    for (size_t i = 0; i < tl->n; i++) {
        const char *s = j_gets(tl->ev[i], "sender", NULL);
        if (s && !in_list(out, s)) j_push(out, j_str(s));
    }
    return out;
}

/* Returns NULL when an incremental sync has nothing to say about the room. */
static json *join_section(const sync_ctx *c, const char *room_id) {
    evlist tl = {0};
    bool limited = false;
    collect_timeline(c, room_id, c->initial ? 0 : c->since, c->now, &tl, &limited);

    json *senders = timeline_senders(&tl);
    int64_t upper = tl.n ? tl.sid[0] - 1 : c->now;
    json *state = collect_state(c, room_id, upper, c->initial || c->full_state,
                                senders);
    j_free(senders);

    json *timeline = timeline_batch(c, &tl, limited, c->now);
    size_t ntl = tl.n;
    ev_free(&tl);

    json *ephemeral = room_ephemeral(c, room_id);
    json *acct = room_account_data(c, room_id);

    bool rel_news = rel_room_has_news &&
                    rel_room_has_news(room_id, c->user_id, c->since, c->now);
    if (!c->initial && !c->full_state && ntl == 0 && j_len(state) == 0 &&
        j_len(ephemeral) == 0 && j_len(acct) == 0 && !rel_news) {
        j_free(state);
        j_free(timeline);
        j_free(ephemeral);
        j_free(acct);
        return NULL;
    }

    int64_t notif = 0, highlight = 0;
    /* Once the push-rule engine exists it owns this count; before that the
     * local counter stands in. */
    if (push_rules_enabled)
        push_unread_counts(c->user_id, room_id, c->now, &notif, &highlight);
    else
        unread_counts(c, room_id, &notif, &highlight);
    json *unread = j_obj();
    j_set(unread, "highlight_count", j_int(highlight));
    j_set(unread, "notification_count", j_int(notif));

    json *sec = j_obj();
    j_set(sec, "state", batch(state));
    j_set(sec, "timeline", timeline);
    j_set(sec, "ephemeral", batch(ephemeral));
    j_set(sec, "account_data", batch(acct));
    j_set(sec, "unread_notifications", unread);
    rel_sync_room_section(sec, room_id, c->user_id, c->filter, c->since, c->now);
    return sec;
}

/* The timeline runs up to and including the event that removed the user. */
static json *leave_section(const sync_ctx *c, const char *room_id, int64_t upto) {
    evlist tl = {0};
    bool limited = false;
    collect_timeline(c, room_id, c->initial ? 0 : c->since, upto, &tl, &limited);

    json *senders = timeline_senders(&tl);
    int64_t upper = tl.n ? tl.sid[0] - 1 : upto;
    json *state = collect_state(c, room_id, upper, c->initial || c->full_state,
                                senders);
    j_free(senders);

    json *timeline = timeline_batch(c, &tl, limited, upto);
    ev_free(&tl);

    json *sec = j_obj();
    j_set(sec, "state", batch(state));
    j_set(sec, "timeline", timeline);
    j_set(sec, "account_data", batch(room_account_data(c, room_id)));
    return sec;
}

/* The stripped state an invited user is shown before they accept. */
static json *invite_section(const sync_ctx *c, const char *room_id, int64_t at) {
    static const char *WANTED[] = {
        "m.room.create", "m.room.join_rules", "m.room.name", "m.room.topic",
        "m.room.avatar", "m.room.canonical_alias", "m.room.encryption", NULL
    };
    json *state = room_state_at(room_id, at);
    json *events = j_arr();
    for (size_t i = 0; i < j_len(state); i++) {
        json *ev = j_at(state, i);
        const char *type = j_gets(ev, "type", "");
        bool want = str_eq(type, "m.room.member");
        for (int k = 0; !want && WANTED[k]; k++) want = str_eq(type, WANTED[k]);
        if (!want) continue;
        j_push(events, project(event_to_stripped(ev), c->event_fields));
    }
    j_free(state);
    json *sec = j_obj();
    j_set(sec, "invite_state", batch(events));
    return sec;
}

/* ------------------------------------------------------------- top level */
/* Presence of the syncing user and of everyone they share a room with. */
static json *presence_section(const sync_ctx *c) {
    json *out = j_arr();
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT p.user_id, p.presence, p.status_msg, p.last_active_ts "
            "FROM presence p WHERE p.stream_id>?1 AND p.stream_id<=?2 AND "
            "(p.user_id=?3 OR EXISTS (SELECT 1 FROM room_members a "
            "  JOIN room_members b ON a.room_id=b.room_id "
            "  WHERE a.user_id=?3 AND a.membership='join' "
            "    AND b.user_id=p.user_id AND b.membership='join')) "
            "ORDER BY p.stream_id ASC", -1, &st, NULL) != SQLITE_OK) return out;
    sqlite3_bind_int64(st, 1, c->since);
    sqlite3_bind_int64(st, 2, c->now);
    sqlite3_bind_text(st, 3, c->user_id, -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *state = col_str(st, 1);
        json *content = j_obj();
        j_set(content, "presence", j_str(*state ? state : "offline"));
        if (sqlite3_column_type(st, 2) != SQLITE_NULL)
            j_set(content, "status_msg", j_str(col_str(st, 2)));
        int64_t last = sqlite3_column_int64(st, 3);
        if (last > 0) j_set(content, "last_active_ago", j_int(now_ms() - last));
        j_set(content, "currently_active", j_bool(str_eq(state, "online")));
        json *ev = j_obj();
        j_set(ev, "type", j_str("m.presence"));
        j_set(ev, "sender", j_str(col_str(st, 0)));
        j_set(ev, "content", content);
        if (pass(c->f_presence, ev, "")) j_push(out, ev); else j_free(ev);
    }
    sqlite3_finalize(st);
    return out;
}

static json *to_device_section(const sync_ctx *c) {
    json *out = j_arr();
    if (!c->device_id) return out;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT sender, type, content FROM to_device WHERE user_id=?1 AND "
            "(device_id=?2 OR device_id='*') AND stream_id>?3 AND stream_id<=?4 "
            "ORDER BY stream_id ASC LIMIT ?5", -1, &st, NULL) != SQLITE_OK) return out;
    sqlite3_bind_text(st, 1, c->user_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, c->device_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, c->since);
    sqlite3_bind_int64(st, 4, c->now);
    sqlite3_bind_int64(st, 5, TO_DEVICE_MAX);
    while (sqlite3_step(st) == SQLITE_ROW) {
        json *content = parse_col(st, 2);
        json *ev = j_obj();
        j_set(ev, "sender", j_str(col_str(st, 0)));
        j_set(ev, "type", j_str(col_str(st, 1)));
        j_set(ev, "content", content ? content : j_obj());
        j_push(out, ev);
    }
    sqlite3_finalize(st);
    return out;
}

/* Builds one /sync response body.  Not static: the unit drivers call it. */
json *sync_build(const char *user_id, const char *device_id, const json *filter,
                 int64_t since, int64_t now, bool initial, bool full_state) {
    sync_ctx c;
    ctx_init(&c, user_id, device_id, filter, since, now, initial, full_state);

    json *join = j_obj(), *invite = j_obj(), *leave = j_obj();

    /* Snapshot the memberships first: every section runs more queries. */
    json *rows = j_arr();
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT room_id, membership, stream_id FROM room_members "
            "WHERE user_id=?1 ORDER BY room_id ASC", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, user_id, -1, SQLITE_TRANSIENT);
        while (sqlite3_step(st) == SQLITE_ROW) {
            json *row = j_obj();
            j_set(row, "room_id", j_str(col_str(st, 0)));
            j_set(row, "membership", j_str(col_str(st, 1)));
            j_set(row, "stream_id", j_int(sqlite3_column_int64(st, 2)));
            j_push(rows, row);
        }
        sqlite3_finalize(st);
    }

    for (size_t i = 0; i < j_len(rows); i++) {
        json *row = j_at(rows, i);
        const char *room_id = j_gets(row, "room_id", "");
        const char *membership = j_gets(row, "membership", "");
        int64_t msid = j_geti(row, "stream_id", 0);
        int64_t at = msid > now ? now : msid;
        if (!*room_id || !room_selected(&c, room_id)) continue;

        if (str_eq(membership, "join")) {
            json *sec = join_section(&c, room_id);
            if (sec) j_set(join, room_id, sec);
        } else if (str_eq(membership, "invite")) {
            if (initial || msid > since) j_set(invite, room_id,
                                               invite_section(&c, room_id, at));
        } else if (str_eq(membership, "leave") || str_eq(membership, "ban")) {
            bool want = initial ? c.include_leave : (msid > since);
            if (want) j_set(leave, room_id, leave_section(&c, room_id, at));
        }
    }
    j_free(rows);

    json *rooms = j_obj();
    j_set(rooms, "join", join);
    j_set(rooms, "invite", invite);
    j_set(rooms, "leave", leave);

    json *body = j_obj();
    char *next = tok_of(now);
    if (!initial) {
        json *dl = keys_sync_device_lists(c.user_id, since, now);
        if (dl) j_set(body, "device_lists", dl);
    }
    json *otk = keys_sync_otk_counts(c.user_id, c.device_id);
    if (otk) j_set(body, "device_one_time_keys_count", otk);
    json *fb = keys_sync_unused_fallback_key_types(c.user_id, c.device_id);
    if (fb) j_set(body, "device_unused_fallback_key_types", fb);
    j_set(body, "next_batch", j_str(next));
    free(next);
    j_set(body, "rooms", rooms);
    j_set(body, "presence", batch(presence_section(&c)));
    j_set(body, "account_data", batch(account_data_events(&c, "", c.f_acct)));
    j_set(body, "to_device", batch(to_device_section(&c)));
    return body;
}

/* True when there is nothing for the client, so a long poll keeps waiting. */
static bool sync_empty(const json *body) {
    const json *rooms = j_geto(body, "rooms");
    if (j_len(j_geto(rooms, "join")) || j_len(j_geto(rooms, "invite")) ||
        j_len(j_geto(rooms, "leave"))) return false;
    if (j_len(j_deep_get(body, "presence.events"))) return false;
    if (j_len(j_deep_get(body, "account_data.events"))) return false;
    if (j_len(j_deep_get(body, "to_device.events"))) return false;
    return true;
}

/* --------------------------------------------------------------- handlers */
/* set_presence: polling /sync marks the client online unless it says not to.
 * Only a real change takes a stream id, or every poll would wake itself. */
static void bump_presence(const char *user_id, const char *state) {
    char *cur = db_str("SELECT presence FROM presence WHERE user_id=?1", "s", user_id);
    if (cur && str_eq(cur, state)) {
        hs_lock();
        db_run("UPDATE presence SET last_active_ts=?2 WHERE user_id=?1", "si",
               user_id, now_ms());
        hs_unlock();
    } else {
        hs_lock();
        int64_t sid = stream_next();
        db_run("INSERT INTO presence(user_id,presence,status_msg,last_active_ts,"
               "stream_id) VALUES(?1,?2,NULL,?3,?4) ON CONFLICT(user_id) DO UPDATE "
               "SET presence=?2, last_active_ts=?3, stream_id=?4", "ssii",
               user_id, state, now_ms(), sid);
        hs_unlock();
        stream_notify();
    }
    free(cur);
}

static void h_sync(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;

    int64_t since = 0;
    bool initial = true;
    const char *since_q = http_query(r, "since");
    if (since_q && *since_q) {
        if (!tok_parse(since_q, &since)) {
            resp_error(w, 400, "M_INVALID_PARAM", "Invalid since token");
            principal_free(&p);
            return;
        }
        initial = false;
    }
    bool full_state = query_bool(r, "full_state", false);
    int64_t timeout = query_int(r, "timeout", 0);
    if (timeout < 0) timeout = 0;
    if (timeout > TIMEOUT_MAX) timeout = TIMEOUT_MAX;

    json *filter = NULL;
    const char *filter_q = http_query(r, "filter");
    if (filter_q && *filter_q) {
        filter = filter_load(p.user_id, filter_q);
        if (!filter) {
            /* An inline filter that will not parse is a malformed JSON body in
             * the query string; a filter id that names nothing is not found. */
            if (filter_q[0] == '{')
                resp_error(w, 400, "M_NOT_JSON", "Malformed filter");
            else
                resp_error(w, 400, "M_INVALID_PARAM", "Unknown filter");
            principal_free(&p);
            return;
        }
    }

    const char *set_presence = http_query(r, "set_presence");
    if (!set_presence) {
        bump_presence(p.user_id, "online");
    } else if (str_eq(set_presence, "online") || str_eq(set_presence, "unavailable")
               || str_eq(set_presence, "offline")) {
        /* "offline" asks not to be marked online at all. */
        if (!str_eq(set_presence, "offline"))
            bump_presence(p.user_id, set_presence);
    } else {
        /* The parameter is an enumeration; anything else is not a value the
         * release defines. */
        resp_error(w, 400, "M_INVALID_PARAM",
                   "set_presence must be one of offline, online, unavailable");
        j_free(filter);
        principal_free(&p);
        return;
    }

    /* The previous response's next_batch acknowledges its to-device messages. */
    if (!initial && p.device_id) {
        hs_lock();
        db_run("DELETE FROM to_device WHERE user_id=?1 AND device_id=?2 AND "
               "stream_id<=?3", "ssi", p.user_id, p.device_id, since);
        hs_unlock();
    }

    int64_t deadline = now_ms() + timeout;
    json *body = NULL;
    for (;;) {
        int64_t now = stream_current();
        if (now < since) now = since;
        body = sync_build(p.user_id, p.device_id, filter, since, now,
                          initial, full_state);
        /* full_state ignores timeout, and an initial sync never blocks. */
        if (initial || full_state || timeout <= 0 || !sync_empty(body)) break;
        int64_t left = deadline - now_ms();
        if (left <= 0) break;
        j_free(body);
        body = NULL;
        stream_wait(now, (int)left);
    }

    j_free(filter);
    resp_json(w, 200, body);
    principal_free(&p);
}

static void h_filter_post(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    if (!str_eq(p.user_id, r->part[0])) {
        resp_error(w, 403, "M_FORBIDDEN", "Cannot create filters for another user");
        principal_free(&p);
        return;
    }
    json *body = require_body(r, w);   /* 400 M_BAD_JSON when not an object */
    if (!body) { principal_free(&p); return; }

    size_t n = 0;
    char *text = j_dump(body, &n);
    hs_lock();
    int64_t next = 0;
    db_int("SELECT COALESCE(MAX(CAST(filter_id AS INTEGER)),-1)+1 FROM filters "
           "WHERE user_id=?1", &next, "s", p.user_id);
    char *filter_id = str_fmt("%" PRId64, next);
    db_run("INSERT OR REPLACE INTO filters(user_id,filter_id,content) "
           "VALUES(?1,?2,?3)", "sss", p.user_id, filter_id, text);
    hs_unlock();
    free(text);

    json *out = j_obj();
    j_set(out, "filter_id", j_str(filter_id));
    free(filter_id);
    resp_json(w, 200, out);
    principal_free(&p);
}

static void h_filter_get(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    if (!str_eq(p.user_id, r->part[0])) {
        resp_error(w, 403, "M_FORBIDDEN", "Cannot read another user's filters");
        principal_free(&p);
        return;
    }
    char *text = db_str("SELECT content FROM filters WHERE user_id=?1 AND "
                        "filter_id=?2", "ss", p.user_id, r->part[1]);
    if (!text) {
        resp_error(w, 404, "M_NOT_FOUND", "No such filter");
        principal_free(&p);
        return;
    }
    json *v = parse_text(text);
    free(text);
    resp_json(w, 200, v ? v : j_obj());
    principal_free(&p);
}

/* --------------------------------------------------------------- routing */
static void route_both(http_server *s, const char *method, const char *suffix,
                       http_handler fn) {
    char *a = str_fmt("%s%s", CS, suffix);
    char *b = str_fmt("%s%s", CSR, suffix);
    http_route(s, method, a, fn);
    http_route(s, method, b, fn);
    free(a); free(b);
}

void api_register_sync(http_server *s) {
    route_both(s, "GET", "/sync", h_sync);
    route_both(s, "POST", "/user/:/filter", h_filter_post);
    route_both(s, "GET", "/user/:/filter/:", h_filter_get);
}

#define _POSIX_C_SOURCE 200809L
/* Event relationships, threading, spaces, the refined receipt model and
 * per-thread notifications.
 *
 * The Client-Server API grew a family of features that all hang off one idea:
 * an event's `content.m.relates_to` makes it the *child* of another event
 * ("Forming relationships between events").  From that follow the relations
 * API, the bundled aggregations servers attach to `unsigned.m.relations`,
 * threads (`rel_type: m.thread`), edits (`rel_type: m.replace`), the split of
 * a room's timeline into a "main timeline" plus one timeline per thread --
 * which is what threaded read receipts and per-thread notification counts are
 * counted against -- and, sitting alongside them, spaces, which are rooms
 * whose `m.space.child` state forms a tree that `/hierarchy` walks.
 *
 * Relationships are found through `rel_index`, a small table this module
 * maintains itself by walking `events` forward from a watermark.  Nothing else
 * in the server has to know that relations exist. */
#include "mxrel.h"

#include "mxapi.h"
#include "mxdb.h"
#include "mxevent.h"
#include "mxfed.h"
#include "mxroom.h"
#include "mxserver.h"
#include "mxutil.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CS1  "/_matrix/client/v1"
#define CS   "/_matrix/client/v3"
#define CSR  "/_matrix/client/r0"
#define FED1 "/_matrix/federation/v1"

#define REL_DEFAULT_LIMIT   10
#define REL_MAX_LIMIT       100
#define MSG_DEFAULT_LIMIT   10
#define MSG_MAX_LIMIT       1000
#define HIER_DEFAULT_LIMIT  50
#define HIER_MAX_LIMIT      500
#define HIER_DEFAULT_DEPTH  5
#define HIER_MAX_DEPTH      16
#define HIER_MAX_ROOMS      2000
#define HIER_MAX_REMOTE     4       /* remote /hierarchy fetches per request */
/* "Implementations should not recurse infinitely, though: a maximum of 3 hops
 * is recommended to cover indirect relationships." (Receipts, threaded) */
#define REL_MAX_HOPS        3
/* How far back the notification counters scan when no receipt bounds them. */
#define UNREAD_SCAN_MAX     1000
#define REL_INDEX_BATCH     4000

/* The three relationship types this server aggregates. */
#define REL_THREAD  "m.thread"
#define REL_REPLACE "m.replace"
/* Not a relation type the specification defines: the index's own name for
 * a rich reply, which has a parent but no `rel_type`. */
#define REL_REPLY   "m.in_reply_to"

/* ================================================================ helpers */

static const char *qs(const http_req *r, const char *name, const char *dflt) {
    const char *v = http_query(r, name);
    return (v && *v) ? v : dflt;
}

static int64_t qint(const http_req *r, const char *name, int64_t dflt) {
    const char *v = http_query(r, name);
    if (!v || !*v) return dflt;
    char *end = NULL;
    long long n = strtoll(v, &end, 10);
    return end == v ? dflt : (int64_t)n;
}

static bool qbool(const http_req *r, const char *name, bool dflt) {
    const char *v = http_query(r, name);
    if (!v || !*v) return dflt;
    return str_eq(v, "true") || str_eq(v, "1");
}

/* Pagination tokens are the same stream positions /messages and /sync hand
 * out, so a token from any of them may be fed to any other. */
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

static char *token_make(int64_t v) { return str_fmt("t%" PRId64, v); }

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

static bool arr_has(const json *arr, const char *s) {
    for (size_t i = 0; arr && i < j_len(arr); i++) {
        json *v = j_at(arr, i);
        if (j_is_str(v) && str_eq(v->s, s)) return true;
    }
    return false;
}

/* `room_types` may carry a JSON null, meaning "rooms without a type". */
static bool arr_has_null(const json *arr) {
    for (size_t i = 0; arr && i < j_len(arr); i++) {
        json *v = j_at(arr, i);
        if (v && v->type == J_NULL) return true;
    }
    return false;
}

static bool ci_contains(const char *hay, const char *needle) {
    if (!hay || !needle || !*needle) return false;
    size_t n = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] && (p[i] | 0x20) == (needle[i] | 0x20)) i++;
        if (i == n) return true;
    }
    return false;
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

/* ======================================================= relation index ==

 * `m.relates_to` lives in the cleartext content of the child event, so the
 * index is just a forward walk of `events`: every event whose content carries
 * a well-formed `m.relates_to` becomes one row pointing at its parent.  A
 * watermark in `rel_meta` makes the walk incremental, and the catch-up runs
 * from any reader, which keeps the writers (room_send, the federation
 * transaction handler) unaware of relations entirely. */

static const char *REL_SCHEMA =
"CREATE TABLE IF NOT EXISTS rel_index("
"  event_id TEXT PRIMARY KEY, room_id TEXT NOT NULL, parent_id TEXT NOT NULL,"
"  rel_type TEXT NOT NULL, type TEXT, sender TEXT, origin_server_ts INTEGER,"
"  stream_id INTEGER);"
"CREATE INDEX IF NOT EXISTS rel_parent ON rel_index(parent_id, rel_type, stream_id);"
"CREATE INDEX IF NOT EXISTS rel_room ON rel_index(room_id, rel_type, stream_id);"
"CREATE TABLE IF NOT EXISTS rel_meta(name TEXT PRIMARY KEY, value INTEGER);"
/* m.read.private never leaves this server, so it is kept out of `receipts`,
 * which /sync batches into m.receipt and the federation sender ships as an
 * m.receipt EDU.  ("Servers MUST NOT send the m.read.private receipt to any
 * other user than the one which originally sent it.") */
"CREATE TABLE IF NOT EXISTS rel_private_receipts("
"  room_id TEXT, user_id TEXT, thread_id TEXT, event_id TEXT, ts INTEGER,"
"  stream_id INTEGER, PRIMARY KEY(room_id, user_id, thread_id));"
;

static pthread_mutex_t g_index_lock = PTHREAD_MUTEX_INITIALIZER;
static int64_t g_indexed = -1;      /* cached rel_meta watermark */

/* The parent named by an event's content, or false when it names none.  A
 * rich reply carries only `m.in_reply_to` and no `rel_type`: it is not a
 * relationship in this sense and is deliberately not indexed. */
static bool relates_to(const json *ev, const char **parent, const char **rel_type) {
    json *c = j_geto(ev, "content");
    json *rt = c ? j_geto(c, "m.relates_to") : NULL;
    if (!rt || rt->type != J_OBJECT) return false;
    const char *p = j_gets(rt, "event_id", NULL);
    const char *t = j_gets(rt, "rel_type", NULL);
    if (p && *p && t && *t) {
        *parent = p;
        *rel_type = t;
        return true;
    }
    /* A rich reply carries no `rel_type`.  It is not a relation the
     * `/relations` endpoints serve, but an event "has a parent event which
     * qualifies" through it: a client with no thread support replies to a
     * message in a thread, and that reply belongs to the thread.  Indexed
     * under its own name so the two are never confused. */
    const json *irt = j_geto(rt, "m.in_reply_to");
    const char *rp = irt ? j_gets(irt, "event_id", NULL) : NULL;
    if (rp && *rp) {
        *parent = rp;
        *rel_type = REL_REPLY;
        return true;
    }
    return false;
}

static void rel_index_catchup(void) {
    int64_t newest = 0;
    db_int("SELECT COALESCE(MAX(stream_id),0) FROM events", &newest, "");
    pthread_mutex_lock(&g_index_lock);
    if (g_indexed < 0) {
        int64_t v = 0;
        if (db_int("SELECT value FROM rel_meta WHERE name='indexed'", &v, ""))
            g_indexed = v;
        else
            g_indexed = 0;
    }
    while (g_indexed < newest) {
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db(),
                "SELECT stream_id, event_id, room_id, type, sender, "
                "origin_server_ts, state_key, json FROM events WHERE stream_id>?1 "
                "ORDER BY stream_id ASC LIMIT ?2", -1, &st, NULL) != SQLITE_OK)
            break;
        sqlite3_bind_int64(st, 1, g_indexed);
        sqlite3_bind_int64(st, 2, REL_INDEX_BATCH);
        int64_t last = g_indexed;
        int rows = 0;
        while (sqlite3_step(st) == SQLITE_ROW) {
            rows++;
            last = sqlite3_column_int64(st, 0);
            /* "State events do not currently receive bundled aggregations"; a
             * state event is never a child either. */
            if (sqlite3_column_type(st, 6) != SQLITE_NULL) continue;
            json *ev = parse_col(st, 7);
            const char *parent = NULL, *rtype = NULL;
            if (ev && relates_to(ev, &parent, &rtype))
                db_run("INSERT OR REPLACE INTO rel_index(event_id,room_id,"
                       "parent_id,rel_type,type,sender,origin_server_ts,stream_id) "
                       "VALUES(?1,?2,?3,?4,?5,?6,?7,?8)", "ssssssii",
                       col_str(st, 1), col_str(st, 2), parent, rtype,
                       col_str(st, 3), col_str(st, 4),
                       sqlite3_column_int64(st, 5), last);
            j_free(ev);
        }
        sqlite3_finalize(st);
        /* `newest` is the highest stream id in the events table, so a range
         * above `g_indexed` that reads back empty is a read that did not see a
         * row rather than a range with nothing in it.  Leaving the position
         * where it is costs one more pass; moving it to `newest` would step
         * over that row for good, and the event it belongs to would never be
         * indexed -- which shows up much later as a relation that exists and
         * is never served. */
        if (rows == 0) break;
        g_indexed = last;
        db_run("INSERT OR REPLACE INTO rel_meta(name,value) VALUES('indexed',?1)",
               "i", g_indexed);
        if (rows < REL_INDEX_BATCH) break;
    }
    pthread_mutex_unlock(&g_index_lock);
}

/* True when `event_id` has at least one child at all. */
static bool has_children(const char *event_id) {
    int64_t n = 0;
    db_int("SELECT 1 FROM rel_index WHERE parent_id=?1 LIMIT 1", &n, "s", event_id);
    return n != 0;
}

/* The parent this event points at and how, or false when it points at
 * nothing. */
static bool rel_parent_of(const char *event_id, char **parent, char **rel_type) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(), "SELECT parent_id, rel_type FROM rel_index "
                                 "WHERE event_id=?1", -1, &st, NULL) != SQLITE_OK)
        return false;
    sqlite3_bind_text(st, 1, event_id, -1, SQLITE_TRANSIENT);
    bool got = false;
    if (sqlite3_step(st) == SQLITE_ROW) {
        *parent = xstrdup_(col_str(st, 0));
        *rel_type = xstrdup_(col_str(st, 1));
        got = true;
    }
    sqlite3_finalize(st);
    return got;
}

/* The thread an event belongs to, or NULL when it sits in the main timeline.
 * A thread *root* is in the main timeline: the receipts module's worked
 * example marks the roots A and B read with a `main` threaded receipt. */
static char *thread_root_of(const char *event_id) {
    char *cur = xstrdup_(event_id);
    for (int hop = 0; hop < REL_MAX_HOPS && cur; hop++) {
        char *parent = NULL, *rtype = NULL;
        if (!rel_parent_of(cur, &parent, &rtype)) break;
        free(cur);
        cur = parent;
        bool thread = str_eq(rtype, REL_THREAD);
        free(rtype);
        if (thread) return cur;         /* `cur` is the thread root */
    }
    free(cur);
    return NULL;
}

/* ========================================================= aggregations ==

 * "When an event is served to the client through the APIs listed below, a
 * `m.relations` property is included under `unsigned` if the event has child
 * events which can be aggregated and point at it." */

/* event_to_client() calls back into this file, and a thread's latest_event is
 * itself serialised that way, so the recursion is bounded: the root gets a
 * bundle, its latest_event gets one, and that is the end. */
static _Thread_local int g_bundle_depth;

/* m.thread: the newest reply, how many there are, and whether this user has
 * taken part. */
static json *thread_bundle(const char *root_id, const json *root_ev,
                           const char *user_id) {
    int64_t count = 0;
    db_int("SELECT COUNT(*) FROM rel_index r JOIN events e ON e.event_id=r.event_id "
           "WHERE r.parent_id=?1 AND r.rel_type=?2 AND e.redacted_by IS NULL",
           &count, "ss", root_id, REL_THREAD);
    if (count <= 0) return NULL;

    /* "the most recent event in the thread (topologically)" */
    char *latest = db_str(
        "SELECT r.event_id FROM rel_index r JOIN events e ON e.event_id=r.event_id "
        "WHERE r.parent_id=?1 AND r.rel_type=?2 AND e.redacted_by IS NULL "
        "ORDER BY e.topological DESC, e.stream_id DESC LIMIT 1",
        "ss", root_id, REL_THREAD);

    /* "`current_user_participated` is `true` when the authenticated user is
     * either the `sender` of the event receiving the bundle, or the `sender`
     * of an event which references the thread root with m.thread." */
    bool mine = user_id && str_eq(j_gets(root_ev, "sender", ""), user_id);
    if (!mine && user_id) {
        int64_t n = 0;
        db_int("SELECT 1 FROM rel_index WHERE parent_id=?1 AND rel_type=?2 AND "
               "sender=?3 LIMIT 1", &n, "sss", root_id, REL_THREAD, user_id);
        mine = n != 0;
    }

    json *b = j_obj();
    if (latest) {
        json *stored = event_by_id(latest);
        json *client = stored ? event_to_client(stored, user_id, NULL, true) : NULL;
        j_free(stored);
        if (client) j_set(b, "latest_event", client);
        free(latest);
    }
    j_set(b, "count", j_int(count));
    j_set(b, "current_user_participated", j_bool(mine));
    return b;
}

/* "Validity of replacement events": same room, same sender, same type, no
 * state key on either, the original is not itself an edit, and the
 * replacement carries m.new_content. */
static bool replace_valid(const json *original, const json *edit) {
    if (!original || !edit) return false;
    if (j_get(original, "state_key") || j_get(edit, "state_key")) return false;
    if (!str_eq(j_gets(original, "type", ""), j_gets(edit, "type", "!"))) return false;
    if (!str_eq(j_gets(original, "sender", ""), j_gets(edit, "sender", "!"))) return false;
    const char *p = NULL, *t = NULL;
    if (relates_to(original, &p, &t) && str_eq(t, REL_REPLACE)) return false;
    json *c = j_geto(edit, "content");
    return c && j_get(c, "m.new_content") != NULL;
}

/* m.replace: "the aggregation format ... gives the most recent replacement
 * event", most recent by origin_server_ts and then by the lexicographically
 * largest event id. */
static json *replace_bundle(const char *event_id, const json *original) {
    const char *room_id = j_gets(original, "room_id", NULL);
    char *owned_room = room_id ? NULL : event_room(event_id);
    const char *room = room_id ? room_id : owned_room;

    sqlite3_stmt *st = NULL;
    json *out = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT r.event_id, r.origin_server_ts, r.sender, r.room_id "
            "FROM rel_index r JOIN events e ON e.event_id=r.event_id "
            "WHERE r.parent_id=?1 AND r.rel_type=?2 AND e.redacted_by IS NULL "
            "ORDER BY r.origin_server_ts DESC, r.event_id DESC LIMIT 50",
            -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, event_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, REL_REPLACE, -1, SQLITE_TRANSIENT);
        while (!out && sqlite3_step(st) == SQLITE_ROW) {
            if (room && !str_eq(col_str(st, 3), room)) continue;
            json *edit = event_by_id(col_str(st, 0));
            if (replace_valid(original, edit)) {
                /* "The most recent replacement event ... is bundled" -- the
                 * whole event, so a client can read the replacement content
                 * without fetching it. */
                out = event_to_client(edit, NULL, NULL, room_id != NULL);
                if (!out) {
                    out = j_obj();
                    j_set(out, "event_id", j_str(col_str(st, 0)));
                    j_set(out, "origin_server_ts",
                          j_int(sqlite3_column_int64(st, 1)));
                    j_set(out, "sender", j_str(col_str(st, 2)));
                }
            }
            j_free(edit);
        }
        sqlite3_finalize(st);
    }
    free(owned_room);
    return out;
}

/* Room version 11 changed how two events look on the wire, and asks servers to
 * paper over the difference in both directions when serving events to
 * clients. */
static void v11_client_compat(json *ev) {
    const char *type = j_gets(ev, "type", "");
    if (str_eq(type, "m.room.redaction")) {
        json *c = j_geto(ev, "content");
        const char *top = j_gets(ev, "redacts", NULL);
        const char *inner = c ? j_gets(c, "redacts", NULL) : NULL;
        /* v11 rooms: "servers should add a `redacts` property to the top level
         * of m.room.redaction events". */
        if (inner && !top) j_set(ev, "redacts", j_str(inner));
        /* older rooms: "servers should add a `redacts` property to the
         * `content` of m.room.redaction events in older room versions". */
        else if (top && !inner && c) j_set(c, "redacts", j_str(top));
        return;
    }
    if (str_eq(type, "m.room.create")) {
        json *c = j_geto(ev, "content");
        if (!c || !j_get(c, "creator")) return;
        const roomver *rv = rv_get(j_gets(c, "room_version", ""));
        /* "The `content` of a m.room.create event no longer has a `creator`
         * property"; clients rely on `sender` instead. */
        if (rv && rv->updated_redaction_rules) j_del(c, "creator");
    }
}

void rel_client_event_hook(json *ev, json *un, const char *user_id) {
    if (!ev || !un) return;
    v11_client_compat(ev);
    /* "State events do not currently receive bundled aggregations." */
    if (j_get(ev, "state_key")) return;
    if (g_bundle_depth >= 2) return;
    const char *eid = j_gets(ev, "event_id", NULL);
    if (!eid || !*eid) return;

    rel_index_catchup();
    if (!has_children(eid)) return;

    g_bundle_depth++;
    json *rel = j_obj();
    json *th = thread_bundle(eid, ev, user_id);
    if (th) j_set(rel, REL_THREAD, th);
    /* "If the original event is redacted, any m.replace relationship should
     * not be bundled with it." */
    if (!j_get(un, "redacted_because")) {
        json *rp = replace_bundle(eid, ev);
        if (rp) j_set(rel, REL_REPLACE, rp);
    }
    g_bundle_depth--;

    if (j_len(rel)) j_set(un, "m.relations", rel);
    else j_free(rel);
}

/* ======================================================= thread filters ==

 * "An event is considered to be 'in a thread' if it meets any of the
 * following criteria: it has a `rel_type` of `m.thread`; it has child events
 * with a `rel_type` of `m.thread` (in which case it'd be the thread root);
 * following the event relationships, it has a parent event which qualifies
 * for one of the above."  A thread root stays in the main timeline: the
 * worked example marks the roots read with a `main` threaded receipt, and
 * clients show the root in the timeline with the thread behind a disclosure.
 *
 * A RoomEventFilter can therefore ask for the main timeline alone:
 *
 *     "not_related_by_rel_types": ["m.thread"]   -- drop the thread replies
 *     "related_by_rel_types":     ["m.thread"]   -- keep only thread roots   */

/* True when the event, or something at most REL_MAX_HOPS above it, is related
 * by one of `types`. */
static bool related_by(const char *event_id, const json *types) {
    char *cur = xstrdup_(event_id);
    bool hit = false;
    for (int hop = 0; hop < REL_MAX_HOPS && cur && !hit; hop++) {
        char *parent = NULL, *rtype = NULL;
        if (!rel_parent_of(cur, &parent, &rtype)) break;
        free(cur);
        cur = parent;
        hit = arr_has(types, rtype);
        free(rtype);
    }
    free(cur);
    return hit;
}

/* True when something points at the event with one of `types`. */
static bool has_child_of_type(const char *event_id, const json *types) {
    for (size_t i = 0; i < j_len(types); i++) {
        json *t = j_at(types, i);
        if (!j_is_str(t)) continue;
        int64_t n = 0;
        db_int("SELECT 1 FROM rel_index WHERE parent_id=?1 AND rel_type=?2 LIMIT 1",
               &n, "ss", event_id, t->s);
        if (n) return true;
    }
    return false;
}

bool rel_timeline_allows(const json *f, const char *room_id,
                         const json *stored) {
    (void)room_id;
    if (!f || f->type != J_OBJECT || !stored) return true;
    /* A caller may hand us a whole sync filter; the room event filter it
     * carries is what applies to a timeline event. */
    const json *f_timeline = j_deep_get(f, "room.timeline");
    if (!f_timeline) f_timeline = f;
    json *deny = j_geta(f_timeline, "not_related_by_rel_types");
    json *only = j_geta(f_timeline, "related_by_rel_types");
    if (!deny && !only) return true;
    const char *eid = j_gets(stored, "event_id", NULL);
    if (!eid) return true;
    rel_index_catchup();
    if (deny && related_by(eid, deny)) return false;
    if (only && !has_child_of_type(eid, only)) return false;
    return true;
}

/* =================================================== receipts and counts ==

 * Read receipts come in three forms: unthreaded (no thread_id, covering the
 * whole room), threaded on the main timeline (thread_id `main`) and threaded
 * inside one thread (thread_id being the thread root's event id).  Between
 * m.read and m.read.private "the receipt which is more 'ahead' or 'recent' is
 * used when determining the highest read-up-to mark". */

static int64_t event_stream_of(const char *event_id) {
    int64_t sid = 0;
    if (event_id && event_get_stream(event_id, &sid, NULL)) return sid;
    return 0;
}

/* The user's read-up-to position for one bucket, public or private. */
static int64_t marker_at(const char *room_id, const char *user_id,
                         const char *thread_id) {
    int64_t best = 0;
    char *e = db_str("SELECT event_id FROM receipts WHERE room_id=?1 AND "
                     "user_id=?2 AND receipt_type='m.read' AND thread_id=?3",
                     "sss", room_id, user_id, thread_id);
    if (e) { int64_t s = event_stream_of(e); if (s > best) best = s; free(e); }
    e = db_str("SELECT event_id FROM rel_private_receipts WHERE room_id=?1 AND "
               "user_id=?2 AND thread_id=?3", "sss", room_id, user_id, thread_id);
    if (e) { int64_t s = event_stream_of(e); if (s > best) best = s; free(e); }
    return best;
}

/* Everything unread in the room, split per thread.  The result maps a bucket
 * ("main", or a thread root's event id) to {notification_count,
 * highlight_count}. */
/* The push-rule engine, when a later milestone has introduced it.  The weak
 * pointer is not the test -- a generated stub defines the symbol too -- so the
 * engine announces itself through `push_rules_enabled()`, which a stub answers
 * `false`. */
extern bool push_rules_enabled(void) __attribute__((weak));
extern json *push_actions_for(const char *user_id, const char *room_id,
                              const json *event, bool *notify, bool *highlight)
    __attribute__((weak));
extern bool push_decision(const char *user_id, const char *room_id,
                          int64_t stream_id, const char *event_id,
                          bool *notify, bool *highlight) __attribute__((weak));

static json *unread_by_thread(const char *room_id, const char *user_id,
                              int64_t now) {
    json *out = j_obj();
    json *markers = j_obj();            /* bucket -> stream position */
    int64_t base = marker_at(room_id, user_id, "");   /* the unthreaded receipt */
    char *display = db_str("SELECT displayname FROM users WHERE user_id=?1",
                           "s", user_id);
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT stream_id, event_id, json FROM events WHERE room_id=?1 AND "
            "stream_id>?2 AND stream_id<=?3 AND sender<>?4 ORDER BY stream_id ASC "
            "LIMIT ?5", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, room_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 2, base);
        sqlite3_bind_int64(st, 3, now);
        sqlite3_bind_text(st, 4, user_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 5, UNREAD_SCAN_MAX);
        while (sqlite3_step(st) == SQLITE_ROW) {
            int64_t sid = sqlite3_column_int64(st, 0);
            char *root = thread_root_of(col_str(st, 1));
            const char *bucket = root ? root : "main";
            json *m = j_get(markers, bucket);
            if (!m) {
                int64_t v = marker_at(room_id, user_id, bucket);
                if (v < base) v = base;
                m = j_int(v);
                j_set(markers, bucket, m);
            }
            if (sid > m->i) {
                json *ev = parse_col(st, 2);
                bool notify = true, highlight = false;
                if (push_rules_enabled && push_rules_enabled()) {
                    /* Once the push-rule engine exists it decides what counts:
                     * the room's own state events, notices and a user's own
                     * edits are all events the default rules pass over.  The
                     * decision is the one taken when the event arrived; only
                     * an event the rules have not seen yet is judged here. */
                    if (!push_decision ||
                        !push_decision(user_id, room_id, sid,
                                       j_gets(ev, "event_id", NULL),
                                       &notify, &highlight)) {
                        json *actions = push_actions_for(user_id, room_id, ev,
                                                         &notify, &highlight);
                        j_free(actions);
                    }
                } else {
                    /* Before the push-rule engine arrives, stand in for the
                     * default rules: they notify on messages, and pass over
                     * state events and notices. */
                    const char *etype = ev ? j_gets(ev, "type", "") : "";
                    const json *content = ev ? j_geto((json *)ev, "content") : NULL;
                    const char *msgtype = j_gets(content, "msgtype", NULL);
                    notify = (str_eq(etype, "m.room.message") ||
                              str_eq(etype, "m.room.encrypted")) &&
                             !(msgtype && str_eq(msgtype, "m.notice")) &&
                             !(ev && j_get((json *)ev, "state_key"));
                    const char *body = j_gets(content, "body", NULL);
                    highlight = notify && body &&
                                (ci_contains(body, user_id) ||
                                 (display && *display &&
                                  ci_contains(body, display)));
                }
                if (notify) {
                    json *c = j_geto(out, bucket);
                    if (!c) {
                        c = j_obj();
                        j_set(c, "highlight_count", j_int(0));
                        j_set(c, "notification_count", j_int(0));
                        j_set(out, bucket, c);
                    }
                    j_get(c, "notification_count")->i++;
                    if (highlight) j_get(c, "highlight_count")->i++;
                }
                j_free(ev);
            }
            free(root);
        }
        sqlite3_finalize(st);
    }
    free(display);
    j_free(markers);
    return out;
}

/* Rebuilds the room's m.receipt from the database: private receipts reach only
 * their own sender, and every receipt carries the thread it was sent for. */
static void rewrite_receipts(json *sec, const char *room_id, const char *user_id,
                             const json *f_ephemeral, int64_t since, int64_t now) {
    json *eph = j_geto(sec, "ephemeral");
    if (!eph) return;

    json *content = j_obj();
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT event_id, receipt_type, user_id, ts, thread_id FROM receipts "
            "WHERE room_id=?1 AND stream_id>?2 AND stream_id<=?3 "
            "UNION ALL "
            "SELECT event_id, 'm.read.private', user_id, ts, thread_id FROM "
            "rel_private_receipts WHERE room_id=?1 AND user_id=?4 AND "
            "stream_id>?2 AND stream_id<=?3", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, room_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 2, since);
        sqlite3_bind_int64(st, 3, now);
        sqlite3_bind_text(st, 4, user_id, -1, SQLITE_TRANSIENT);
        while (sqlite3_step(st) == SQLITE_ROW) {
            const char *eid = col_str(st, 0), *rt = col_str(st, 1);
            json *by_event = j_geto(content, eid);
            if (!by_event) { by_event = j_obj(); j_set(content, eid, by_event); }
            json *by_type = j_geto(by_event, rt);
            if (!by_type) { by_type = j_obj(); j_set(by_event, rt, by_type); }
            json *who = j_obj();
            j_set(who, "ts", j_int(sqlite3_column_int64(st, 3)));
            const char *thread = col_str(st, 4);
            if (*thread) j_set(who, "thread_id", j_str(thread));
            j_set(by_type, col_str(st, 2), who);
        }
        sqlite3_finalize(st);
    }

    json *events = j_geta(eph, "events");
    json *out = j_arr();
    bool placed = false;
    for (size_t i = 0; i < j_len(events); i++) {
        json *ev = j_at(events, i);
        if (!str_eq(j_gets(ev, "type", ""), "m.receipt")) {
            j_push(out, j_clone(ev));
            continue;
        }
        if (!placed && j_len(content)) {
            json *copy = j_clone(ev);
            j_set(copy, "content", content);
            content = NULL;
            j_push(out, copy);
            placed = true;
        }
        /* otherwise the batch held nothing this user may see: drop it */
    }
    if (!placed && content && j_len(content)) {
        json *ev = j_obj();
        j_set(ev, "type", j_str("m.receipt"));
        j_set(ev, "content", content);
        content = NULL;
        if (!f_ephemeral || filter_event_allowed(f_ephemeral, ev, room_id))
            j_push(out, ev);
        else
            j_free(ev);
    }
    j_free(content);
    j_set(eph, "events", out);
}

void rel_sync_room_section(json *sec, const char *room_id, const char *user_id,
                           const json *filter, int64_t since, int64_t now) {
    if (!sec || !room_id || !user_id) return;
    rel_index_catchup();

    json *f_room = j_geto(filter, "room");
    json *f_timeline = j_geto(f_room, "timeline");
    json *f_ephemeral = j_geto(f_room, "ephemeral");

    rewrite_receipts(sec, room_id, user_id, f_ephemeral, since, now);

    json *counts = unread_by_thread(room_id, user_id, now);
    /* "If `unread_thread_notifications` was specified as `true` on the
     * RoomEventFilter, these counts will only be for the main timeline rather
     * than all events in the room." */
    bool split = f_timeline &&
                 j_getb(f_timeline, "unread_thread_notifications", false);
    int64_t notif = 0, highlight = 0;
    json *threads = j_obj();
    for (size_t i = 0; i < j_len(counts); i++) {
        const char *bucket = j_key(counts, i);
        json *c = j_at(counts, i);
        bool main_tl = str_eq(bucket, "main");
        if (main_tl || !split) {
            notif += j_geti(c, "notification_count", 0);
            highlight += j_geti(c, "highlight_count", 0);
        }
        /* "If `unread_thread_notifications` was specified as `true` ..." -- when
         * it was not, there is no split to report and the object is omitted
         * entirely. */
        if (split && !main_tl) j_set(threads, bucket, j_clone(c));
    }
    j_free(counts);

    json *unread = j_obj();
    j_set(unread, "highlight_count", j_int(highlight));
    j_set(unread, "notification_count", j_int(notif));
    j_set(sec, "unread_notifications", unread);
    /* "If a thread does not have any notifications it can be omitted from this
     * object.  If no threads have notification counts, this whole object can
     * be omitted." */
    if (j_len(threads)) j_set(sec, "unread_thread_notifications", threads);
    else j_free(threads);
}

/* ====================================================== reading history ==

 * The same access rules the history endpoints use: members and former members
 * may read, and so may anybody at all when the room is world_readable. */

static bool room_world_readable(const char *room_id) {
    json *c = state_content(room_id, "m.room.history_visibility", "");
    bool wr = c && str_eq(j_gets(c, "history_visibility", "shared"), "world_readable");
    j_free(c);
    return wr;
}

static bool may_read_room(const char *room_id, const char *user_id) {
    char *ms = room_membership(room_id, user_id);
    bool ok = ms && (str_eq(ms, "join") || str_eq(ms, "leave") ||
                     str_eq(ms, "invite"));
    free(ms);
    return ok || room_world_readable(room_id);
}

/* The client form of one stored row, or NULL when the user may not see it or a
 * filter rejects it. */
static json *visible_event(const char *js, int64_t sid, const char *room_id,
                           const principal *p, const json *filter) {
    if (!js || !room_can_see(room_id, p->user_id, sid)) return NULL;
    json *stored = parse_text(js);
    if (!stored) return NULL;
    if ((filter && !filter_event_allowed(filter, stored, room_id)) ||
        !rel_timeline_allows(filter, room_id, stored)) {
        j_free(stored);
        return NULL;
    }
    json *ev = event_to_client(stored, p->user_id, p->device_id, true);
    j_free(stored);
    return ev;
}

/* ============================================================ /relations ==

 * "Retrieve all of the child events for a given parent event."  Child events
 * are ordered topologically, most recent first by default; `dir=f` walks the
 * other way.  `from` and `to` are the same stream positions /messages and
 * /sync hand out. */

/* The answer for a recursive `/relations` query: the transitive children
 * gathered by the caller, filtered and paginated the same way the direct query
 * is.  `recursion_depth` reports how far the server was willing to walk. */
static void rel_recursive_answer(http_resp *w, const char *room_id,
                                 const principal *p, const json *ids,
                                 const char *rel_type, const char *ev_type,
                                 bool backwards, int64_t from, bool have_to,
                                 int64_t to, int64_t limit) {
    json *rows = j_arr();
    for (size_t i = 0; i < j_len((json *)ids); i++) {
        const char *eid = j_key((json *)ids, i);
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db(),
                "SELECT e.stream_id, e.json FROM rel_index r JOIN events e ON "
                "e.event_id=r.event_id WHERE r.event_id=?1", -1, &st, NULL)
            != SQLITE_OK)
            continue;
        sqlite3_bind_text(st, 1, eid, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) == SQLITE_ROW) {
            int64_t sid = sqlite3_column_int64(st, 0);
            bool in_window = backwards ? sid <= from : sid > from;
            if (have_to)
                in_window = in_window && (backwards ? sid > to : sid <= to);
            if (in_window) {
                json *row = j_obj();
                j_set(row, "sid", j_int(sid));
                j_set(row, "json", j_str(col_str(st, 1)));
                j_set(row, "event_id", j_str(eid));
                j_push(rows, row);
            }
        }
        sqlite3_finalize(st);
    }
    /* Ordered the way the direct query orders, then filtered and cut. */
    for (size_t i = 0; i + 1 < j_len(rows); i++)
        for (size_t k = i + 1; k < j_len(rows); k++) {
            int64_t a = j_geti(j_at(rows, i), "sid", 0);
            int64_t b = j_geti(j_at(rows, k), "sid", 0);
            if (backwards ? b > a : b < a) {
                json tmp = *j_at(rows, i);
                *j_at(rows, i) = *j_at(rows, k);
                *j_at(rows, k) = tmp;
            }
        }
    json *chunk = j_arr();
    bool more = false;
    for (size_t i = 0; i < j_len(rows); i++) {
        if ((int64_t)j_len(chunk) >= limit) { more = true; break; }
        json *row = j_at(rows, i);
        json *ev = visible_event(j_gets(row, "json", ""),
                                 j_geti(row, "sid", 0), room_id, p, NULL);
        if (!ev) continue;
        const char *t = j_gets(ev, "type", "");
        const json *rt = j_geto(j_geto(ev, "content"), "m.relates_to");
        const char *kind = j_gets(rt, "rel_type", "");
        if (rel_type && !str_eq(kind, rel_type)) { j_free(ev); continue; }
        if (ev_type && !str_eq(t, ev_type)) { j_free(ev); continue; }
        j_push(chunk, ev);
    }
    j_free(rows);
    json *b = j_obj();
    j_set(b, "chunk", chunk);
    j_set(b, "recursion_depth", j_int(REL_MAX_HOPS));
    if (more) j_set(b, "next_batch", j_str("rel-more"));
    resp_json(w, 200, b);
}

static void h_relations(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    char *room_id = resolve_room(r->part[0]);
    const char *event_id = r->part[1];
    const char *rel_type = r->nparts > 2 ? r->part[2] : NULL;
    const char *ev_type  = r->nparts > 3 ? r->part[3] : NULL;

    if (!room_id || !may_read_room(room_id, p.user_id)) {
        resp_error(w, 404, "M_NOT_FOUND", "Event not found.");
        free(room_id); principal_free(&p);
        return;
    }
    int64_t parent_sid = 0;
    char *parent_room = event_room(event_id);
    bool known = event_get_stream(event_id, &parent_sid, NULL) &&
                 str_eq(parent_room, room_id) &&
                 room_can_see(room_id, p.user_id, parent_sid);
    free(parent_room);
    if (!known) {
        resp_error(w, 404, "M_NOT_FOUND", "Event not found.");
        free(room_id); principal_free(&p);
        return;
    }

    const char *dir = qs(r, "dir", "b");
    if (!str_eq(dir, "b") && !str_eq(dir, "f")) {
        resp_error(w, 400, "M_INVALID_PARAM", "dir must be 'b' or 'f'");
        free(room_id); principal_free(&p);
        return;
    }
    bool backwards = dir[0] == 'b';
    const char *fromq = http_query(r, "from");
    int64_t from;
    if (fromq && *fromq) {
        if (!token_parse(fromq, &from)) {
            resp_error(w, 400, "M_INVALID_PARAM", "Invalid 'from' token");
            free(room_id); principal_free(&p);
            return;
        }
    } else {
        from = backwards ? room_latest_stream(room_id) : 0;
    }
    int64_t to = 0;
    const char *toq = http_query(r, "to");
    bool have_to = toq && *toq && token_parse(toq, &to);
    int64_t limit = qint(r, "limit", REL_DEFAULT_LIMIT);
    if (limit < 0) limit = 0;
    if (limit > REL_MAX_LIMIT) limit = REL_MAX_LIMIT;

    /* v1.10: "recurse ... whether to additionally include events which relate
     * indirectly to the given event", to a maximum of three hops. */
    const char *rq = http_query(r, "recurse");
    bool recurse = rq && (str_eq(rq, "true") || str_eq(rq, "1"));

    rel_index_catchup();

    if (recurse) {
        json *ids = j_arr();
        j_push(ids, j_str(event_id));
        json *seen = j_obj();
        j_set(seen, event_id, j_bool(true));
        for (int hop = 0; hop < REL_MAX_HOPS; hop++) {
            json *next = j_arr();
            for (size_t i = 0; i < j_len(ids); i++) {
                sqlite3_stmt *cs = NULL;
                if (sqlite3_prepare_v2(db(),
                        "SELECT event_id FROM rel_index WHERE parent_id=?1",
                        -1, &cs, NULL) != SQLITE_OK)
                    continue;
                sqlite3_bind_text(cs, 1, j_at(ids, i)->s, -1, SQLITE_TRANSIENT);
                while (sqlite3_step(cs) == SQLITE_ROW) {
                    const char *kid = col_str(cs, 0);
                    if (j_get(seen, kid)) continue;
                    j_set(seen, kid, j_bool(true));
                    j_push(next, j_str(kid));
                }
                sqlite3_finalize(cs);
            }
            if (!j_len(next)) { j_free(next); break; }
            j_free(ids);
            ids = next;
        }
        j_free(ids);
        /* The parent itself is not one of its own relations. */
        j_del(seen, event_id);
        rel_recursive_answer(w, room_id, &p, seen, rel_type, ev_type, backwards,
                             from, have_to, to, limit);
        j_free(seen);
        free(room_id);
        principal_free(&p);
        return;
    }

    char *sql = str_fmt(
        "SELECT e.stream_id, e.json FROM rel_index r JOIN events e ON "
        "e.event_id=r.event_id WHERE r.parent_id=?1%s%s AND e.stream_id%s?2%s "
        "ORDER BY e.stream_id %s",
        /* Without a filter the endpoint serves "all relations" -- which a rich
         * reply is not: it carries no `rel_type`, and it is in the index only
         * so that thread membership can be walked through it. */
        rel_type ? " AND r.rel_type=?4" : " AND r.rel_type<>'" REL_REPLY "'",
        ev_type ? " AND r.type=?5" : "",
        backwards ? "<=" : ">",
        have_to ? (backwards ? " AND e.stream_id>?3" : " AND e.stream_id<=?3") : "",
        backwards ? "DESC" : "ASC");

    json *chunk = j_arr();
    int64_t boundary = from, first = from;
    bool more = false, got_first = false;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(), sql, -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, event_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 2, from);
        if (have_to) sqlite3_bind_int64(st, 3, to);
        if (rel_type) sqlite3_bind_text(st, 4, rel_type, -1, SQLITE_TRANSIENT);
        if (ev_type) sqlite3_bind_text(st, 5, ev_type, -1, SQLITE_TRANSIENT);
        while (sqlite3_step(st) == SQLITE_ROW) {
            if ((int64_t)j_len(chunk) >= limit) { more = true; break; }
            int64_t sid = sqlite3_column_int64(st, 0);
            json *ev = visible_event(col_str(st, 1), sid, room_id, &p, NULL);
            boundary = backwards ? sid - 1 : sid;
            if (!ev) continue;
            if (!got_first) { first = sid; got_first = true; }
            j_push(chunk, ev);
        }
        sqlite3_finalize(st);
    }
    free(sql);

    json *b = j_obj();
    j_set(b, "chunk", chunk);
    if (more) {
        char *t = token_make(boundary);
        j_set(b, "next_batch", j_str(t));
        free(t);
    }
    /* "The absence of this token means this is the start of the result set." */
    if (fromq && *fromq && got_first) {
        char *t = token_make(backwards ? first : first - 1);
        j_set(b, "prev_batch", j_str(t));
        free(t);
    }
    resp_json(w, 200, b);
    free(room_id);
    principal_free(&p);
}

/* ============================================================== /threads ==

 * "getting all threads in a room is done through a dedicated API". */

static void h_threads(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    char *room_id = resolve_room(r->part[0]);
    if (!room_id || !may_read_room(room_id, p.user_id)) {
        resp_error(w, 403, "M_FORBIDDEN", "You are not allowed to view this room.");
        free(room_id); principal_free(&p);
        return;
    }
    const char *include = qs(r, "include", "all");
    if (!str_eq(include, "all") && !str_eq(include, "participated")) {
        resp_error(w, 400, "M_INVALID_PARAM", "include must be 'all' or 'participated'");
        free(room_id); principal_free(&p);
        return;
    }
    int64_t limit = qint(r, "limit", REL_DEFAULT_LIMIT);
    if (limit < 0) limit = 0;
    if (limit > REL_MAX_LIMIT) limit = REL_MAX_LIMIT;
    int64_t offset = 0;
    const char *fromq = http_query(r, "from");
    if (fromq && *fromq && !token_parse(fromq, &offset)) {
        resp_error(w, 400, "M_INVALID_PARAM", "Invalid 'from' token");
        free(room_id); principal_free(&p);
        return;
    }
    if (offset < 0) offset = 0;

    rel_index_catchup();

    json *chunk = j_arr();
    int64_t seen = 0, next = 0;
    bool more = false;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT r.parent_id, MAX(e.stream_id) AS newest FROM rel_index r "
            "JOIN events e ON e.event_id=r.event_id WHERE r.room_id=?1 AND "
            "r.rel_type=?2 GROUP BY r.parent_id ORDER BY newest DESC",
            -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, room_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, REL_THREAD, -1, SQLITE_TRANSIENT);
        while (sqlite3_step(st) == SQLITE_ROW) {
            const char *root = col_str(st, 0);
            int64_t sid = 0;
            if (!event_get_stream(root, &sid, NULL)) continue;
            if (!room_can_see(room_id, p.user_id, sid)) continue;
            json *stored = event_by_id(root);
            if (!stored) continue;
            if (str_eq(include, "participated")) {
                int64_t n = 0;
                bool mine = str_eq(j_gets(stored, "sender", ""), p.user_id);
                if (!mine) {
                    db_int("SELECT 1 FROM rel_index WHERE parent_id=?1 AND "
                           "rel_type=?2 AND sender=?3 LIMIT 1", &n, "sss",
                           root, REL_THREAD, p.user_id);
                    mine = n != 0;
                }
                if (!mine) { j_free(stored); continue; }
            }
            seen++;
            if (seen <= offset) { j_free(stored); continue; }
            if ((int64_t)j_len(chunk) >= limit) {
                j_free(stored);
                more = true;
                next = seen - 1;
                break;
            }
            j_push(chunk, event_to_client(stored, p.user_id, p.device_id, true));
            j_free(stored);
        }
        sqlite3_finalize(st);
    }

    json *b = j_obj();
    j_set(b, "chunk", chunk);
    if (more) {
        char *t = str_fmt("t%" PRId64, next);
        j_set(b, "next_batch", j_str(t));
        free(t);
    }
    resp_json(w, 200, b);
    free(room_id);
    principal_free(&p);
}

/* ============================================================= /messages ==

 * The history endpoint, standing in front of the plain one so that a
 * RoomEventFilter asking for the main timeline alone is honoured and so that
 * the events it returns carry their bundled aggregations. */

static json *paginate(const char *room_id, const principal *p, const json *filter,
                      bool backwards, int64_t from, bool have_to, int64_t to,
                      int64_t limit, int64_t *boundary, bool *more) {
    json *chunk = j_arr();
    *boundary = from;
    *more = false;

    char *sql;
    if (backwards)
        sql = str_fmt("SELECT stream_id, json FROM events WHERE room_id=?1 AND "
                      "stream_id<=?2%s ORDER BY stream_id DESC",
                      have_to ? " AND stream_id>?3" : "");
    else
        sql = str_fmt("SELECT stream_id, json FROM events WHERE room_id=?1 AND "
                      "stream_id>?2%s ORDER BY stream_id ASC",
                      have_to ? " AND stream_id<=?3" : "");

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(), sql, -1, &st, NULL) != SQLITE_OK) {
        free(sql);
        return chunk;
    }
    free(sql);
    sqlite3_bind_text(st, 1, room_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, from);
    if (have_to) sqlite3_bind_int64(st, 3, to);

    while (sqlite3_step(st) == SQLITE_ROW) {
        if ((int64_t)j_len(chunk) >= limit) { *more = true; break; }
        int64_t sid = sqlite3_column_int64(st, 0);
        json *ev = visible_event(col_str(st, 1), sid, room_id, p, filter);
        *boundary = backwards ? sid - 1 : sid;
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
    int64_t from;
    if (fromq && *fromq) {
        if (!token_parse(fromq, &from)) {
            resp_error(w, 400, "M_INVALID_PARAM", "Invalid 'from' token");
            free(room_id); principal_free(&p);
            return;
        }
    } else {
        from = backwards ? room_latest_stream(room_id) : 0;
    }
    int64_t to = 0;
    const char *toq = http_query(r, "to");
    bool have_to = toq && *toq && token_parse(toq, &to);

    const char *filter_q = http_query(r, "filter");
    json *filter = filter_load(p.user_id, filter_q);
    if (filter_q && *filter_q && !filter) {
        resp_error(w, 400, filter_q[0] == '{' ? "M_NOT_JSON" : "M_INVALID_PARAM",
                   "Malformed filter");
        free(room_id); principal_free(&p);
        return;
    }
    int64_t limit = qint(r, "limit", filter_timeline_limit(filter, MSG_DEFAULT_LIMIT));
    if (limit < 0) limit = 0;
    if (limit > MSG_MAX_LIMIT) limit = MSG_MAX_LIMIT;

    rel_index_catchup();
    int64_t boundary = from;
    bool more = false;
    json *chunk = paginate(room_id, &p, filter, backwards, from, have_to, to,
                           limit, &boundary, &more);

    json *b = j_obj();
    if (fromq && *fromq) {
        j_set(b, "start", j_str(fromq));
    } else {
        char *t = token_make(from);
        j_set(b, "start", j_str(t));
        free(t);
    }
    if (more) {
        char *t = token_make(boundary);
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

/* ================================================================ spaces ==

 * "A space is defined by the `m.space` room type" and its children by
 * `m.space.child` state events whose state key is the child's room id.  The
 * hierarchy API walks that tree depth-first. */

static char *room_type_of(const char *room_id) {
    json *c = state_content(room_id, "m.room.create", "");
    char *t = c ? xstrdup_(j_gets(c, "type", NULL)) : NULL;
    j_free(c);
    return t;
}

static bool room_is_space(const char *room_id) {
    char *t = room_type_of(room_id);
    bool sp = t && str_eq(t, "m.space");
    free(t);
    return sp;
}

static const char *join_rule_of(json *jr_content) {
    return jr_content ? j_gets(jr_content, "join_rule", "public") : "public";
}

/* "If the room is a restricted room, these are the room IDs which are
 * specified by the join rules." */
static json *allowed_room_ids(const char *room_id) {
    json *out = j_arr();
    json *c = state_content(room_id, "m.room.join_rules", "");
    json *allow = c ? j_geta(c, "allow") : NULL;
    for (size_t i = 0; allow && i < j_len(allow); i++) {
        json *e = j_at(allow, i);
        if (!e || e->type != J_OBJECT) continue;
        if (!str_eq(j_gets(e, "type", ""), "m.room_membership")) continue;
        const char *rid = j_gets(e, "room_id", NULL);
        if (rid) j_push(out, j_str(rid));
    }
    j_free(c);
    return out;
}

/* A PublicRoomsChunk for one room, optionally with the space extras. */
static json *room_summary(const char *room_id, bool with_children,
                          bool suggested_only, bool with_allowed);

/* An `order` is valid when it is a string of at most 50 characters, each in
 * the range \x20 to \x7E. */
static const char *valid_order(const json *content) {
    const char *o = j_gets(content, "order", NULL);
    if (!o) return NULL;
    size_t n = strlen(o);
    if (n == 0 || n > 50) return NULL;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)o[i] < 0x20 || (unsigned char)o[i] > 0x7E) return NULL;
    return o;
}

static int by_space_order(const void *a, const void *b) {
    const json *x = *(json *const *)a, *y = *(json *const *)b;
    const char *ox = j_gets(x, "order", NULL), *oy = j_gets(y, "order", NULL);
    if ((ox != NULL) != (oy != NULL)) return ox ? -1 : 1;
    if (ox && oy) {
        int c = strcmp(ox, oy);
        if (c) return c;
    }
    int64_t tx = j_geti(x, "origin_server_ts", 0), ty = j_geti(y, "origin_server_ts", 0);
    if (tx != ty) return tx < ty ? -1 : 1;
    return strcmp(j_gets(x, "room_id", ""), j_gets(y, "room_id", ""));
}

/* The ordered `m.space.child` links of a space.  Each entry carries the child
 * room id, the `via` list and the stripped state event the hierarchy API
 * reports. */
static json *space_children(const char *room_id, bool suggested_only) {
    json **v = NULL;
    size_t n = 0, cap = 0;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT e.json FROM room_state s JOIN events e ON e.event_id=s.event_id "
            "WHERE s.room_id=?1 AND s.type='m.space.child'", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, room_id, -1, SQLITE_TRANSIENT);
        while (sqlite3_step(st) == SQLITE_ROW) {
            json *ev = parse_col(st, 0);
            json *c = ev ? j_geto(ev, "content") : NULL;
            json *via = c ? j_geta(c, "via") : NULL;
            const char *child = ev ? j_gets(ev, "state_key", NULL) : NULL;
            /* "Child rooms can be removed from a space by omitting the `via`
             * key of `content`". */
            if (!child || !*child || !via) { j_free(ev); continue; }
            if (suggested_only && !j_getb(c, "suggested", false)) { j_free(ev); continue; }

            json *entry = j_obj();
            j_set(entry, "room_id", j_str(child));
            const char *ord = valid_order(c);
            if (ord) j_set(entry, "order", j_str(ord));
            j_set(entry, "origin_server_ts", j_int(j_geti(ev, "origin_server_ts", 0)));
            j_set(entry, "via", j_clone(via));
            /* "represented as Stripped State Events with an added
             * origin_server_ts key" */
            json *stripped = event_to_stripped(ev);
            j_set(stripped, "origin_server_ts", j_int(j_geti(ev, "origin_server_ts", 0)));
            j_set(entry, "event", stripped);
            j_free(ev);

            if (n == cap) {
                cap = cap ? cap * 2 : 8;
                v = realloc(v, cap * sizeof *v);
                if (!v) abort();
            }
            v[n++] = entry;
        }
        sqlite3_finalize(st);
    }
    if (n > 1) qsort(v, n, sizeof *v, by_space_order);
    json *out = j_arr();
    for (size_t i = 0; i < n; i++) j_push(out, v[i]);
    free(v);
    return out;
}

static json *room_summary(const char *room_id, bool with_children,
                          bool suggested_only, bool with_allowed) {
    json *e = j_obj();
    int64_t members = 0;
    db_int("SELECT COUNT(*) FROM room_members WHERE room_id=?1 AND "
           "membership='join'", &members, "s", room_id);
    j_set(e, "room_id", j_str(room_id));
    j_set(e, "num_joined_members", j_int(members));

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
    j_set(e, "join_rule", j_str(join_rule_of(c)));
    j_free(c);
    j_set(e, "world_readable", j_bool(room_world_readable(room_id)));
    c = state_content(room_id, "m.room.guest_access", "");
    j_set(e, "guest_can_join",
          j_bool(c && str_eq(j_gets(c, "guest_access", "forbidden"), "can_join")));
    j_free(c);

    /* "The `type` of room (from m.room.create), if any." */
    char *rt = room_type_of(room_id);
    if (rt) { j_set(e, "room_type", j_str(rt)); free(rt); }

    if (with_allowed) j_set(e, "allowed_room_ids", allowed_room_ids(room_id));
    if (with_children) {
        json *kids = space_children(room_id, suggested_only);
        json *states = j_arr();
        for (size_t i = 0; i < j_len(kids); i++)
            j_push(states, j_clone(j_geto(j_at(kids, i), "event")));
        j_free(kids);
        /* "If the room is not a space-room, this should be empty." */
        j_set(e, "children_state", states);
    }
    return e;
}

/* Which rooms of a hierarchy a user may be shown: the ones they are in, and
 * the ones they could peek or join. */
static bool hierarchy_visible(const char *room_id, const char *user_id) {
    char *ms = room_membership(room_id, user_id);
    bool joined = ms && (str_eq(ms, "join") || str_eq(ms, "invite"));
    free(ms);
    if (joined) return true;
    if (room_world_readable(room_id)) return true;
    json *c = state_content(room_id, "m.room.join_rules", "");
    const char *jr = join_rule_of(c);
    bool open = str_eq(jr, "public") || str_eq(jr, "knock") ||
                str_eq(jr, "knock_restricted");
    bool restricted = str_eq(jr, "restricted") || str_eq(jr, "knock_restricted");
    json *allow = c ? j_geta(c, "allow") : NULL;
    if (!open && restricted) {
        for (size_t i = 0; allow && i < j_len(allow) && !open; i++) {
            const char *rid = j_gets(j_at(allow, i), "room_id", NULL);
            if (!rid) continue;
            char *m = room_membership(rid, user_id);
            open = m && str_eq(m, "join");
            free(m);
        }
    }
    j_free(c);
    return open;
}

/* The same question for a remote server: it may see a room it already has a
 * member in, or one that is open to the world. */
static bool hierarchy_visible_to_server(const char *room_id, const char *origin) {
    if (room_world_readable(room_id)) return true;
    json *c = state_content(room_id, "m.room.join_rules", "");
    const char *jr = join_rule_of(c);
    bool open = str_eq(jr, "public") || str_eq(jr, "knock") ||
                str_eq(jr, "knock_restricted") || str_eq(jr, "restricted");
    j_free(c);
    if (open) return true;
    int64_t n = 0;
    char *like = str_fmt("%%:%s", origin ? origin : "");
    db_int("SELECT 1 FROM room_members WHERE room_id=?1 AND membership='join' AND "
           "user_id LIKE ?2 LIMIT 1", &n, "ss", room_id, like);
    free(like);
    return n != 0;
}

/* A child this server knows nothing about is asked for over federation:
 * "Where a child room is unknown to the local server, federation is used to
 * fill in the details." */
static json *remote_summary(const char *child_id, const json *via,
                            bool suggested_only, int *budget) {
    if (CFG.disable_federation || *budget <= 0) return NULL;
    char *enc = url_encode(child_id);
    char *path = str_fmt(FED1 "/hierarchy/%s?suggested_only=%s", enc,
                         suggested_only ? "true" : "false");
    free(enc);
    json *out = NULL;
    for (size_t i = 0; !out && i < j_len(via) && i < 2; i++) {
        json *srv = j_at(via, i);
        if (!j_is_str(srv) || fed_is_local_server(srv->s)) continue;
        (*budget)--;
        http_client_resp *res = fed_request(srv->s, "GET", path, NULL);
        if (res && res->status == 200 && res->body) {
            json *b = j_parse(res->body, res->body_len, NULL);
            json *room = b ? j_geto(b, "room") : NULL;
            if (room) out = j_clone(room);
            j_free(b);
        }
        http_client_resp_free(res);
        if (*budget <= 0) break;
    }
    free(path);
    return out;
}

/* GET /_matrix/client/v1/rooms/{roomId}/hierarchy */
static void h_hierarchy(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    char *room_id = resolve_room(r->part[0]);
    if (!room_id || !hierarchy_visible(room_id, p.user_id)) {
        resp_error(w, 403, "M_FORBIDDEN", "You are not allowed to view this room.");
        free(room_id); principal_free(&p);
        return;
    }
    bool suggested = qbool(r, "suggested_only", false);
    int64_t max_depth = qint(r, "max_depth", HIER_DEFAULT_DEPTH);
    int64_t limit = qint(r, "limit", HIER_DEFAULT_LIMIT);
    /* "limit: Optional (integer): a client-defined limit to the maximum number
     * of rooms to return per page."  Nought or fewer rooms per page is not a
     * page size the server can honour, and silently substituting its own would
     * hide the client's mistake. */
    const char *limitq = http_query(r, "limit");
    if (limitq && *limitq && limit <= 0) {
        resp_error(w, 400, "M_INVALID_PARAM", "'limit' must be a positive integer");
        free(room_id); principal_free(&p);
        return;
    }
    int64_t offset = 0;
    const char *fromq = http_query(r, "from");
    if (fromq && *fromq) {
        int sug = 0;
        long long md = 0, off = 0;
        if (sscanf(fromq, "h%d_%lld_%lld", &sug, &md, &off) != 3) {
            resp_error(w, 400, "M_INVALID_PARAM", "Unknown 'from' token");
            free(room_id); principal_free(&p);
            return;
        }
        /* "suggested_only or max_depth cannot change on paginated requests" */
        if ((http_query(r, "suggested_only") && (sug != 0) != suggested) ||
            (http_query(r, "max_depth") && md != max_depth)) {
            resp_error(w, 400, "M_INVALID_PARAM",
                       "suggested_only and max_depth cannot change on paginated requests");
            free(room_id); principal_free(&p);
            return;
        }
        suggested = sug != 0;
        max_depth = md;
        offset = off;
    }
    if (max_depth < 0) max_depth = 0;
    if (max_depth > HIER_MAX_DEPTH) max_depth = HIER_MAX_DEPTH;
    if (limit <= 0) limit = HIER_DEFAULT_LIMIT;
    if (limit > HIER_MAX_LIMIT) limit = HIER_MAX_LIMIT;
    if (offset < 0) offset = 0;

    /* A depth-first walk: "when it encounters another space as a child it
     * recurses into that space before returning non-space children."  A room
     * already visited is not walked again, which is how loops are broken. */
    json *seen = j_obj();
    json *flat = j_arr();
    char **stack_id = NULL;
    int64_t *stack_depth = NULL;
    size_t sp = 0, scap = 0;
    int budget = HIER_MAX_REMOTE;

    #define PUSH(id, d) do {                                        \
        if (sp == scap) {                                           \
            scap = scap ? scap * 2 : 16;                            \
            stack_id = realloc(stack_id, scap * sizeof *stack_id);  \
            stack_depth = realloc(stack_depth, scap * sizeof *stack_depth); \
            if (!stack_id || !stack_depth) abort();                 \
        }                                                           \
        stack_id[sp] = xstrdup_(id);                                \
        stack_depth[sp] = (d);                                      \
        sp++;                                                       \
    } while (0)

    PUSH(room_id, 0);
    while (sp && (int64_t)j_len(flat) < HIER_MAX_ROOMS) {
        char *cur = stack_id[--sp];
        int64_t depth = stack_depth[sp];
        if (j_get(seen, cur)) { free(cur); continue; }
        j_set(seen, cur, j_bool(true));

        bool local = room_exists(cur);
        if (local) {
            if (depth > 0 && !hierarchy_visible(cur, p.user_id)) { free(cur); continue; }
            bool space = room_is_space(cur);
            j_push(flat, room_summary(cur, true, suggested, false));
            if (space && depth < max_depth) {
                json *kids = space_children(cur, suggested);
                for (size_t i = j_len(kids); i > 0; i--) {
                    json *entry = j_at(kids, i - 1);
                    PUSH(j_gets(entry, "room_id", ""), depth + 1);
                }
                j_free(kids);
            }
        }
        free(cur);
    }
    /* Children this server has never seen are filled in from the servers the
     * m.space.child event named.  They are appended after the local walk
     * rather than in their depth-first position, since fetching them is what
     * tells us whether they are spaces in the first place. */
    if (!CFG.disable_federation) {
        json *pending = j_arr();
        for (size_t i = 0; i < j_len(flat); i++) {
            json *kids = j_geta(j_at(flat, i), "children_state");
            for (size_t k = 0; k < j_len(kids); k++) {
                json *ev = j_at(kids, k);
                const char *cid = j_gets(ev, "state_key", NULL);
                if (!cid || j_get(seen, cid)) continue;
                json *via = j_geta(j_geto(ev, "content"), "via");
                json *want = j_obj();
                j_set(want, "room_id", j_str(cid));
                j_set(want, "via", via ? j_clone(via) : j_arr());
                j_set(seen, cid, j_bool(true));
                j_push(pending, want);
            }
        }
        for (size_t i = 0; i < j_len(pending) && budget > 0; i++) {
            json *want = j_at(pending, i);
            json *sum = remote_summary(j_gets(want, "room_id", ""),
                                       j_geta(want, "via"), suggested, &budget);
            if (sum) j_push(flat, sum);
        }
        j_free(pending);
    }
    for (size_t i = 0; i < sp; i++) free(stack_id[i]);
    free(stack_id);
    free(stack_depth);
    j_free(seen);
    #undef PUSH

    json *rooms = j_arr();
    int64_t i = offset;
    for (; i < (int64_t)j_len(flat) && (int64_t)j_len(rooms) < limit; i++)
        j_push(rooms, j_clone(j_at(flat, i)));
    bool more = i < (int64_t)j_len(flat);
    j_free(flat);

    json *b = j_obj();
    j_set(b, "rooms", rooms);
    if (more) {
        char *t = str_fmt("h%d_%" PRId64 "_%" PRId64, suggested ? 1 : 0,
                          max_depth, i);
        j_set(b, "next_batch", j_str(t));
        free(t);
    }
    resp_json(w, 200, b);
    free(room_id);
    principal_free(&p);
}

/* GET /_matrix/federation/v1/hierarchy/{roomId}: "Unlike the Client-Server API
 * version, this endpoint does not paginate." */
static void h_fed_hierarchy(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    char *origin = NULL;
    if (!fed_authenticate(r, w, &origin)) return;
    const char *room_id = r->part[0];
    if (!room_exists(room_id) || !hierarchy_visible_to_server(room_id, origin)) {
        resp_error(w, 404, "M_NOT_FOUND", "Unknown room");
        free(origin);
        return;
    }
    bool suggested = qbool(r, "suggested_only", false);

    json *b = j_obj();
    j_set(b, "room", room_summary(room_id, true, suggested, true));
    json *children = j_arr();
    json *inaccessible = j_arr();
    json *kids = space_children(room_id, suggested);
    for (size_t i = 0; i < j_len(kids); i++) {
        const char *cid = j_gets(j_at(kids, i), "room_id", "");
        if (!*cid) continue;
        if (room_exists(cid) && hierarchy_visible_to_server(cid, origin))
            j_push(children, room_summary(cid, true, suggested, true));
        else
            j_push(inaccessible, j_str(cid));
    }
    j_free(kids);
    j_set(b, "children", children);
    j_set(b, "inaccessible_children", inaccessible);
    resp_json(w, 200, b);
    free(origin);
}

/* ========================================================== public rooms ==

 * The directory listing, standing in front of the plain one so that entries
 * carry `room_type` and the `room_types` filter is honoured. */

static int by_members_desc(const void *a, const void *b) {
    const json *x = *(json *const *)a, *y = *(json *const *)b;
    int64_t nx = j_geti(x, "num_joined_members", 0);
    int64_t ny = j_geti(y, "num_joined_members", 0);
    if (nx != ny) return nx < ny ? 1 : -1;
    return strcmp(j_gets(x, "room_id", ""), j_gets(y, "room_id", ""));
}

static bool entry_matches(const json *e, const char *term) {
    if (!term || !*term) return true;
    return ci_contains(j_gets(e, "name", NULL), term) ||
           ci_contains(j_gets(e, "topic", NULL), term) ||
           ci_contains(j_gets(e, "canonical_alias", NULL), term) ||
           ci_contains(j_gets(e, "room_id", NULL), term);
}

/* "An optional list of room types to search for.  To include rooms without a
 * room type, specify `null` within this list." */
static bool entry_type_wanted(const json *e, const json *room_types) {
    if (!room_types || room_types->type != J_ARRAY) return true;
    const char *t = j_gets(e, "room_type", NULL);
    return t ? arr_has(room_types, t) : arr_has_null(room_types);
}

static void public_rooms(http_resp *w, int64_t limit, const char *since,
                         const char *term, const json *room_types) {
    json **rooms = NULL;
    size_t n = 0, cap = 0;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(), "SELECT room_id FROM rooms WHERE published=1",
                           -1, &st, NULL) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            const char *rid = col_str(st, 0);
            json *e = room_summary(rid, false, false, false);
            json *aliases = j_arr();
            sqlite3_stmt *al = NULL;
            if (sqlite3_prepare_v2(db(), "SELECT alias FROM aliases WHERE "
                                         "room_id=?1 ORDER BY alias", -1, &al,
                                   NULL) == SQLITE_OK) {
                sqlite3_bind_text(al, 1, rid, -1, SQLITE_TRANSIENT);
                while (sqlite3_step(al) == SQLITE_ROW)
                    j_push(aliases, j_str(col_str(al, 0)));
                sqlite3_finalize(al);
            }
            j_set(e, "aliases", aliases);
            if (!entry_matches(e, term) || !entry_type_wanted(e, room_types)) {
                j_free(e);
                continue;
            }
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
    public_rooms(w, qint(r, "limit", 0), http_query(r, "since"), NULL, NULL);
}

static void h_public_rooms_post(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *body = http_json(r);
    if (!body && r->json_bad) {
        resp_error(w, 400, "M_NOT_JSON", "Request body is not valid JSON");
        principal_free(&p);
        return;
    }
    int64_t limit = body ? j_geti(body, "limit", 0) : 0;
    const char *since = body ? j_gets(body, "since", NULL) : NULL;
    json *f = body ? j_geto(body, "filter") : NULL;
    const char *term = f ? j_gets(f, "generic_search_term", NULL) : NULL;
    json *types = f ? j_geta(f, "room_types") : NULL;
    public_rooms(w, limit, since, term, types);
    principal_free(&p);
}

/* =============================================================== receipts ==

 * Read receipts in their refined form: three receipt types, an optional
 * thread, and m.fully_read routed to room account data. */

/* "The `event_id` is not related to the `thread_id`" is a 400.  The check only
 * fires for events this server actually holds. */
static bool receipt_thread_ok(const char *event_id, const char *thread_id) {
    if (!thread_id || !*thread_id) return true;
    int64_t known = 0;
    db_int("SELECT 1 FROM events WHERE event_id=?1", &known, "s", event_id);
    if (!known) return true;
    char *root = thread_root_of(event_id);
    bool ok;
    if (str_eq(thread_id, "main"))
        ok = root == NULL;
    else
        ok = (root && str_eq(root, thread_id)) || str_eq(event_id, thread_id);
    free(root);
    return ok;
}

static void set_fully_read(const char *room_id, const char *user_id,
                          const char *event_id, int64_t sid) {
    json *c = j_obj();
    j_set(c, "event_id", j_str(event_id));
    size_t n = 0;
    char *s = j_dump(c, &n);
    j_free(c);
    db_run("INSERT OR REPLACE INTO account_data(user_id,room_id,type,content,"
           "stream_id) VALUES(?1,?2,'m.fully_read',?3,?4)", "sssi",
           user_id, room_id, s, sid);
    free(s);
}

static void store_receipt(const char *room_id, const char *user_id,
                          const char *rtype, const char *thread_id,
                          const char *event_id, int64_t sid) {
    if (str_eq(rtype, "m.read.private"))
        db_run("INSERT OR REPLACE INTO rel_private_receipts(room_id,user_id,"
               "thread_id,event_id,ts,stream_id) VALUES(?1,?2,?3,?4,?5,?6)",
               "ssssii", room_id, user_id, thread_id, event_id, now_ms(), sid);
    else
        db_run("INSERT OR REPLACE INTO receipts(room_id,user_id,receipt_type,"
               "thread_id,event_id,ts,stream_id) VALUES(?1,?2,?3,?4,?5,?6,?7)",
               "sssssii", room_id, user_id, rtype, thread_id, event_id,
               now_ms(), sid);
}

static void h_receipt(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    const char *room_id = r->part[0], *rtype = r->part[1], *event_id = r->part[2];
    if (!str_eq(rtype, "m.read") && !str_eq(rtype, "m.read.private") &&
        !str_eq(rtype, "m.fully_read")) {
        resp_error(w, 400, "M_INVALID_PARAM", "Unknown receipt type");
        principal_free(&p);
        return;
    }
    json *body = http_json(r);
    if (!body && r->json_bad) {
        resp_error(w, 400, "M_NOT_JSON", "Request body is not valid JSON");
        principal_free(&p);
        return;
    }
    const char *thread_id = NULL;
    json *tid = body ? j_get(body, "thread_id") : NULL;
    if (tid) {
        if (!j_is_str(tid) || !*tid->s) {
            resp_error(w, 400, "M_INVALID_PARAM",
                       "thread_id field must be a non-empty string");
            principal_free(&p);
            return;
        }
        if (str_eq(rtype, "m.fully_read")) {
            resp_error(w, 400, "M_INVALID_PARAM",
                       "thread_id is not supported for this receipt type");
            principal_free(&p);
            return;
        }
        thread_id = tid->s;
    }
    char *ms = room_membership(room_id, p.user_id);
    if (!ms || !str_eq(ms, "join")) {
        free(ms);
        resp_error(w, 403, "M_FORBIDDEN", "You are not in the room");
        principal_free(&p);
        return;
    }
    free(ms);
    rel_index_catchup();
    if (!receipt_thread_ok(event_id, thread_id)) {
        resp_error(w, 400, "M_INVALID_PARAM",
                   "The event is not related to the given thread");
        principal_free(&p);
        return;
    }

    hs_lock();
    int64_t sid = stream_next();
    /* "this endpoint effectively calls /read_markers internally when presented
     * with a receipt type of m.fully_read." */
    if (str_eq(rtype, "m.fully_read"))
        set_fully_read(room_id, p.user_id, event_id, sid);
    else
        store_receipt(room_id, p.user_id, rtype, thread_id ? thread_id : "",
                      event_id, sid);
    hs_unlock();
    stream_notify();
    resp_json(w, 200, j_obj());
    principal_free(&p);
}

static void h_read_markers(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *body = require_body(r, w);
    if (!body) { principal_free(&p); return; }
    const char *room_id = r->part[0];
    char *ms = room_membership(room_id, p.user_id);
    if (!ms || !str_eq(ms, "join")) {
        free(ms);
        resp_error(w, 403, "M_FORBIDDEN", "You are not in the room");
        principal_free(&p);
        return;
    }
    free(ms);
    const char *thread_id = j_gets(body, "thread_id", "");
    hs_lock();
    int64_t sid = stream_next();
    const char *fully = j_gets(body, "m.fully_read", NULL);
    if (fully) set_fully_read(room_id, p.user_id, fully, sid);
    /* "the server must additionally ensure that it treats the presence of
     * m.read and m.read.private in the /read_markers request the same as how
     * it would for a request to /receipt/{receiptType}/{eventId}." */
    const char *read = j_gets(body, "m.read", NULL);
    if (read) store_receipt(room_id, p.user_id, "m.read", thread_id, read, sid);
    const char *priv = j_gets(body, "m.read.private", NULL);
    if (priv)
        store_receipt(room_id, p.user_id, "m.read.private", thread_id, priv, sid);
    hs_unlock();
    stream_notify();
    resp_json(w, 200, j_obj());
    principal_free(&p);
}

/* =============================================================== routing ==

 * Several of these stand in front of the plain handlers, which is why
 * main.c registers this group first: the router keeps the first route it was
 * given at a particular specificity. */

static void route_both(http_server *s, const char *method, const char *suffix,
                       http_handler fn) {
    char *a = str_fmt("%s%s", CS, suffix);
    char *b = str_fmt("%s%s", CSR, suffix);
    http_route(s, method, a, fn);
    http_route(s, method, b, fn);
    free(a);
    free(b);
}

void api_register_relations(http_server *s) {
    spec_advertise("v1.4");
    spec_advertise("v1.5");
    spec_advertise("v1.6");
    if (!s) return;
    if (!db_exec(REL_SCHEMA))
        fprintf(stderr, "relations: could not create the relation tables\n");
    rel_index_catchup();

    /* Event relationships and threads */
    http_route(s, "GET", CS1 "/rooms/:/relations/:", h_relations);
    http_route(s, "GET", CS1 "/rooms/:/relations/:/:", h_relations);
    http_route(s, "GET", CS1 "/rooms/:/relations/:/:/:", h_relations);
    http_route(s, "GET", CS1 "/rooms/:/threads", h_threads);

    /* Spaces */
    http_route(s, "GET", CS1 "/rooms/:/hierarchy", h_hierarchy);
    http_route(s, "GET", FED1 "/hierarchy/:", h_fed_hierarchy);

    /* Handlers that take over from the plain ones */
    route_both(s, "GET", "/rooms/:/messages", h_messages);
    route_both(s, "POST", "/rooms/:/receipt/:/:", h_receipt);
    route_both(s, "POST", "/rooms/:/read_markers", h_read_markers);
    route_both(s, "GET", "/publicRooms", h_public_rooms_get);
    route_both(s, "POST", "/publicRooms", h_public_rooms_post);
}

/* ------------------------------------------------------- send-time check --
 * "Threads are not intended to be nested": a thread can only be started on an
 * event that is not itself part of a relation, and it can only be started on
 * an event this server has.  Refused with 400, so a client learns at once
 * rather than through an aggregation that never appears. */
bool rel_relation_allowed(const char *room_id, const json *content,
                          const char **errcode, const char **message) {
    if (errcode) *errcode = "M_INVALID_PARAM";
    if (!content) return true;
    const json *rt = j_geto((json *)content, "m.relates_to");
    if (!rt) return true;
    const char *rel_type = j_gets(rt, "rel_type", NULL);
    const char *parent = j_gets(rt, "event_id", NULL);
    if (!rel_type || !parent || !*parent) return true;
    if (!str_eq(rel_type, REL_THREAD)) return true;

    rel_index_catchup();
    json *target = event_by_id(parent);
    if (!target) {
        if (message) *message = "The thread root is not an event in this room";
        return false;
    }
    const char *troom = j_gets(target, "room_id", NULL);
    bool wrong_room = room_id && troom && !str_eq(troom, room_id);
    j_free(target);
    if (wrong_room) {
        if (message) *message = "The thread root is not an event in this room";
        return false;
    }
    char *pparent = NULL, *prtype = NULL;
    if (rel_parent_of(parent, &pparent, &prtype)) {
        free(pparent);
        bool nested = prtype && (str_eq(prtype, REL_THREAD) ||
                                 str_eq(prtype, REL_REPLACE));
        free(prtype);
        if (nested) {
            if (message)
                *message = "Threads cannot be started on an event that is "
                           "itself part of a relation";
            return false;
        }
    }
    return true;
}

/* Does this module have anything to add to a room's slice of an incremental
 * sync?  A private read receipt is delivered only to the user who sent it, so
 * it lives outside the shared `receipts` table -- which means a delta whose
 * only news is that receipt looks empty to the core sync code, and the room
 * would be dropped before `rel_sync_room_section` ever ran. */
bool rel_room_has_news(const char *room_id, const char *user_id,
                       int64_t since, int64_t now) {
    if (!room_id || !user_id) return false;
    int64_t n = 0;
    db_int("SELECT COUNT(*) FROM rel_private_receipts WHERE room_id=?1 AND "
           "user_id=?2 AND stream_id>?3 AND stream_id<=?4", &n, "ssii",
           room_id, user_id, since, now);
    return n > 0;
}

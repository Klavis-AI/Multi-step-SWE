#define _POSIX_C_SOURCE 200809L
/* Account data, tags, receipts, read markers, typing, presence, directory. */
#include "mxapi.h"
#include "mxdb.h"
#include "mxutil.h"
#include "mxfed.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CS "/_matrix/client/v3"
#define CSR "/_matrix/client/r0"

static void route_both(http_server *s, const char *method, const char *suffix,
                       http_handler fn) {
    char *a = str_fmt("%s%s", CS, suffix);
    char *b = str_fmt("%s%s", CSR, suffix);
    http_route(s, method, a, fn);
    http_route(s, method, b, fn);
    free(a); free(b);
}

static bool own_user(http_resp *w, const principal *p, const char *uid) {
    if (str_eq(p->user_id, uid)) return true;
    resp_error(w, 403, "M_FORBIDDEN", "Cannot access another user's data");
    return false;
}

/* ---------------------------------------------------------- account data */
static void account_data(http_req *r, http_resp *w, const char *room_id,
                         const char *type) {
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    if (!own_user(w, &p, r->part[0])) { principal_free(&p); return; }
    if (str_eq(r->method, "GET")) {
        char *c = db_str("SELECT content FROM account_data WHERE user_id=?1 AND "
                         "room_id=?2 AND type=?3", "sss", p.user_id,
                         room_id ? room_id : "", type);
        if (!c) resp_error(w, 404, "M_NOT_FOUND", "Account data not found");
        else {
            const char *err = NULL;
            json *v = j_parse(c, strlen(c), &err);
            free(c);
            resp_json(w, 200, v ? v : j_obj());
        }
        principal_free(&p);
        return;
    }
    json *body = require_body(r, w);
    if (!body) { principal_free(&p); return; }
    size_t n = 0;
    char *s = j_dump(body, &n);
    hs_lock();
    int64_t sid = stream_next();
    db_run("INSERT OR REPLACE INTO account_data(user_id,room_id,type,content,"
           "stream_id) VALUES(?1,?2,?3,?4,?5)", "ssssi", p.user_id,
           room_id ? room_id : "", type, s, sid);
    hs_unlock();
    stream_notify();
    free(s);
    resp_json(w, 200, j_obj());
    principal_free(&p);
}

static void h_account_data(http_req *r, http_resp *w, void *ud) {
    (void)ud; account_data(r, w, NULL, r->part[1]);
}
static void h_room_account_data(http_req *r, http_resp *w, void *ud) {
    (void)ud; account_data(r, w, r->part[1], r->part[2]);
}

/* ------------------------------------------------------------------ tags */
static void h_tags_get(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    if (!own_user(w, &p, r->part[0])) { principal_free(&p); return; }
    json *tags = j_obj();
    sqlite3_stmt *st = NULL;
    sqlite3_prepare_v2(db(), "SELECT tag, content FROM room_tags WHERE user_id=?1 "
                             "AND room_id=?2 AND content IS NOT NULL", -1, &st, NULL);
    sqlite3_bind_text(st, 1, p.user_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, r->part[1], -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *c = (const char *)sqlite3_column_text(st, 1);
        const char *err = NULL;
        json *v = c ? j_parse(c, strlen(c), &err) : NULL;
        j_set(tags, (const char *)sqlite3_column_text(st, 0), v ? v : j_obj());
    }
    sqlite3_finalize(st);
    json *b = j_obj();
    j_set(b, "tags", tags);
    resp_json(w, 200, b);
    principal_free(&p);
}

static void h_tag_put(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    if (!own_user(w, &p, r->part[0])) { principal_free(&p); return; }
    json *body = require_body(r, w);
    if (!body) { principal_free(&p); return; }
    size_t n = 0;
    char *s = j_dump(body, &n);
    hs_lock();
    int64_t sid = stream_next();
    db_run("INSERT OR REPLACE INTO room_tags(user_id,room_id,tag,content,stream_id) "
           "VALUES(?1,?2,?3,?4,?5)", "ssssi", p.user_id, r->part[1], r->part[2],
           s, sid);
    hs_unlock();
    stream_notify();
    free(s);
    resp_json(w, 200, j_obj());
    principal_free(&p);
}

static void h_tag_delete(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    if (!own_user(w, &p, r->part[0])) { principal_free(&p); return; }
    hs_lock();
    int64_t sid = stream_next();
    /* Keep a tombstone so an incremental /sync still reports the change; the
     * readers below skip rows whose content is NULL. */
    db_run("UPDATE room_tags SET content=NULL, stream_id=?4 WHERE user_id=?1 AND "
           "room_id=?2 AND tag=?3", "sssi", p.user_id, r->part[1], r->part[2], sid);
    hs_unlock();
    stream_notify();
    resp_json(w, 200, j_obj());
    principal_free(&p);
}

/* -------------------------------------------------------------- receipts */
static void h_receipt(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    const char *room_id = r->part[0], *rtype = r->part[1], *event_id = r->part[2];
    if (!str_eq(rtype, "m.read") && !str_eq(rtype, "m.fully_read")) {
        resp_error(w, 400, "M_INVALID_PARAM", "Unknown receipt type");
        principal_free(&p);
        return;
    }
    char *ms = room_membership(room_id, p.user_id);
    if (!ms || !str_eq(ms, "join")) {
        free(ms);
        resp_error(w, 403, "M_FORBIDDEN", "You are not in the room");
        principal_free(&p);
        return;
    }
    free(ms);
    hs_lock();
    int64_t sid = stream_next();
    if (str_eq(rtype, "m.fully_read")) {
        json *c = j_obj();
        j_set(c, "event_id", j_str(event_id));
        size_t n = 0;
        char *s = j_dump(c, &n);
        j_free(c);
        db_run("INSERT OR REPLACE INTO account_data(user_id,room_id,type,content,"
               "stream_id) VALUES(?1,?2,'m.fully_read',?3,?4)", "sssi",
               p.user_id, room_id, s, sid);
        free(s);
    } else {
        db_run("INSERT OR REPLACE INTO receipts(room_id,user_id,receipt_type,"
               "thread_id,event_id,ts,stream_id) VALUES(?1,?2,?3,'',?4,?5,?6)",
               "ssssii", room_id, p.user_id, rtype, event_id, now_ms(), sid);
    }
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
    hs_lock();
    int64_t sid = stream_next();
    const char *fully = j_gets(body, "m.fully_read", NULL);
    if (fully) {
        json *c = j_obj();
        j_set(c, "event_id", j_str(fully));
        size_t n = 0;
        char *s = j_dump(c, &n);
        j_free(c);
        db_run("INSERT OR REPLACE INTO account_data(user_id,room_id,type,content,"
               "stream_id) VALUES(?1,?2,'m.fully_read',?3,?4)", "sssi",
               p.user_id, room_id, s, sid);
        free(s);
    }
    const char *read = j_gets(body, "m.read", NULL);
    if (read)
        db_run("INSERT OR REPLACE INTO receipts(room_id,user_id,receipt_type,"
               "thread_id,event_id,ts,stream_id) VALUES(?1,?2,'m.read','',?3,?4,?5)",
               "sssii", room_id, p.user_id, read, now_ms(), sid);
    hs_unlock();
    stream_notify();
    resp_json(w, 200, j_obj());
    principal_free(&p);
}

/* ---------------------------------------------------------------- typing */
static void h_typing(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *body = require_body(r, w);
    if (!body) { principal_free(&p); return; }
    if (!str_eq(p.user_id, r->part[1])) {
        resp_error(w, 403, "M_FORBIDDEN", "Cannot set another user's typing state");
        principal_free(&p);
        return;
    }
    char *ms = room_membership(r->part[0], p.user_id);
    if (!ms || !str_eq(ms, "join")) {
        free(ms);
        resp_error(w, 403, "M_FORBIDDEN", "You are not in the room");
        principal_free(&p);
        return;
    }
    free(ms);
    hs_lock();
    int64_t sid = stream_next();
    if (j_getb(body, "typing", false)) {
        int64_t timeout = j_geti(body, "timeout", 30000);
        db_run("INSERT OR REPLACE INTO typing(room_id,user_id,expires_ms,stream_id) "
               "VALUES(?1,?2,?3,?4)", "ssii", r->part[0], p.user_id,
               now_ms() + timeout, sid);
    } else {
        db_run("DELETE FROM typing WHERE room_id=?1 AND user_id=?2",
               "ss", r->part[0], p.user_id);
        db_run("INSERT OR REPLACE INTO typing(room_id,user_id,expires_ms,stream_id) "
               "VALUES(?1,?2,0,?3)", "ssi", r->part[0], p.user_id, sid);
    }
    hs_unlock();
    stream_notify();
    resp_json(w, 200, j_obj());
    principal_free(&p);
}

/* -------------------------------------------------------------- presence */
static void h_presence(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    const char *uid = r->part[0];
    if (str_eq(r->method, "GET")) {
        if (!str_eq(p.user_id, uid)) {
            int64_t shared = 0;
            db_int("SELECT COUNT(*) FROM room_members a JOIN room_members b "
                   "ON a.room_id=b.room_id WHERE a.user_id=?1 AND b.user_id=?2 "
                   "AND a.membership='join' AND b.membership='join'", &shared,
                   "ss", p.user_id, uid);
            if (shared == 0) {
                resp_error(w, 403, "M_FORBIDDEN",
                           "You are not allowed to see their presence");
                principal_free(&p);
                return;
            }
        }
        /* A user on another server has no row in `users`; their presence is
         * whatever that server last told us in an m.presence EDU. */
        if (fed_is_local_user(uid)) {
            char *found = db_str("SELECT user_id FROM users WHERE user_id=?1",
                                 "s", uid);
            if (!found) {
                resp_error(w, 404, "M_NOT_FOUND", "Unknown user");
                principal_free(&p);
                return;
            }
            free(found);
        }
        char *pr = db_str("SELECT presence FROM presence WHERE user_id=?1", "s", uid);
        char *sm = db_str("SELECT status_msg FROM presence WHERE user_id=?1", "s", uid);
        json *b = j_obj();
        j_set(b, "presence", j_str(pr ? pr : "offline"));
        if (sm) j_set(b, "status_msg", j_str(sm));
        j_set(b, "currently_active", j_bool(pr && str_eq(pr, "online")));
        free(pr); free(sm);
        resp_json(w, 200, b);
        principal_free(&p);
        return;
    }
    if (!own_user(w, &p, uid)) { principal_free(&p); return; }
    json *body = require_body(r, w);
    if (!body) { principal_free(&p); return; }
    const char *pres = j_gets(body, "presence", NULL);
    if (!pres || (!str_eq(pres, "online") && !str_eq(pres, "offline") &&
                  !str_eq(pres, "unavailable"))) {
        resp_error(w, 400, "M_INVALID_PARAM", "Invalid presence state");
        principal_free(&p);
        return;
    }
    hs_lock();
    int64_t sid = stream_next();
    db_run("INSERT OR REPLACE INTO presence(user_id,presence,status_msg,"
           "last_active_ts,stream_id) VALUES(?1,?2,?3,?4,?5)", "sssii",
           uid, pres, j_gets(body, "status_msg", NULL), now_ms(), sid);
    hs_unlock();
    stream_notify();
    resp_json(w, 200, j_obj());
    principal_free(&p);
}

/* -------------------------------------------------------- user directory */
static void h_user_directory(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *body = require_body(r, w);
    if (!body) { principal_free(&p); return; }
    const char *term = j_gets(body, "search_term", "");
    int64_t limit = j_geti(body, "limit", 10);
    char *pattern = str_fmt("%%%s%%", term);
    json *results = j_arr();
    sqlite3_stmt *st = NULL;
    sqlite3_prepare_v2(db(),
        "SELECT user_id, displayname, avatar_url FROM users WHERE deactivated=0 "
        "AND (user_id LIKE ?1 OR COALESCE(displayname,'') LIKE ?1) "
        "ORDER BY user_id LIMIT ?2", -1, &st, NULL);
    sqlite3_bind_text(st, 1, pattern, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, limit + 1);
    int64_t n = 0;
    bool more = false;
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (n++ >= limit) { more = true; break; }
        json *u = j_obj();
        j_set(u, "user_id", j_str((const char *)sqlite3_column_text(st, 0)));
        if (sqlite3_column_type(st, 1) != SQLITE_NULL)
            j_set(u, "display_name", j_str((const char *)sqlite3_column_text(st, 1)));
        if (sqlite3_column_type(st, 2) != SQLITE_NULL)
            j_set(u, "avatar_url", j_str((const char *)sqlite3_column_text(st, 2)));
        j_push(results, u);
    }
    sqlite3_finalize(st);
    free(pattern);
    json *b = j_obj();
    j_set(b, "results", results);
    j_set(b, "limited", j_bool(more));
    resp_json(w, 200, b);
    principal_free(&p);
}

/* ------------------------------------------------- the older sync endpoints */
/* GET /rooms/{roomId}/initialSync and GET /initialSync, as release v1.1
 * documents them in old_sync.yaml. */
static json *room_initial_sync(const char *room_id, const principal *p,
                               int64_t limit) {
    json *out = j_obj();
    j_set(out, "room_id", j_str(room_id));
    /* `membership` is the caller's own membership of the room.  Someone
     * peeking into a world-readable room has none, and reporting "leave" would
     * claim they had been in it and left. */
    char *ms = room_membership(room_id, p->user_id);
    if (ms) j_set(out, "membership", j_str(ms));
    free(ms);

    json *state_map = room_state_map(room_id);
    json *state = j_arr();
    for (size_t i = 0; i < j_len(state_map); i++) {
        json *ev = event_to_client(j_at(state_map, i), p->user_id, p->device_id, true);
        if (ev) j_push(state, ev);
    }
    j_free(state_map);
    j_set(out, "state", state);

    json *chunk = j_arr();
    int64_t lo = 0, hi = 0;
    sqlite3_stmt *st = NULL;
    sqlite3_prepare_v2(db(),
        "SELECT stream_id, json FROM events WHERE room_id=?1 "
        "ORDER BY stream_id DESC LIMIT ?2", -1, &st, NULL);
    sqlite3_bind_text(st, 1, room_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, limit);
    json *rev = j_arr();
    while (sqlite3_step(st) == SQLITE_ROW) {
        int64_t sid = sqlite3_column_int64(st, 0);
        if (!room_can_see(room_id, p->user_id, sid)) continue;
        if (!hi) hi = sid;
        lo = sid;
        const char *js = (const char *)sqlite3_column_text(st, 1);
        const char *err = NULL;
        json *stored = js ? j_parse(js, strlen(js), &err) : NULL;
        json *ev = stored ? event_to_client(stored, p->user_id, p->device_id, true)
                          : NULL;
        j_free(stored);
        if (ev) j_push(rev, ev);
    }
    sqlite3_finalize(st);
    for (size_t i = j_len(rev); i > 0; i--) j_push(chunk, j_clone(j_at(rev, i - 1)));
    j_free(rev);
    json *messages = j_obj();
    j_set(messages, "chunk", chunk);
    char *tok = str_fmt("t%lld", (long long)(lo ? lo - 1 : 0));
    j_set(messages, "start", j_str(tok));
    free(tok);
    tok = str_fmt("t%lld", (long long)hi);
    j_set(messages, "end", j_str(tok));
    free(tok);
    j_set(out, "messages", messages);

    json *ad = j_arr();
    sqlite3_prepare_v2(db(), "SELECT type, content FROM account_data WHERE "
                             "user_id=?1 AND room_id=?2", -1, &st, NULL);
    sqlite3_bind_text(st, 1, p->user_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, room_id, -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *c = (const char *)sqlite3_column_text(st, 1);
        const char *err = NULL;
        json *v = c ? j_parse(c, strlen(c), &err) : NULL;
        json *e = j_obj();
        j_set(e, "type", j_str((const char *)sqlite3_column_text(st, 0)));
        j_set(e, "content", v ? v : j_obj());
        j_push(ad, e);
    }
    sqlite3_finalize(st);
    /* Room tags are room account data too. */
    json *tags = j_obj();
    sqlite3_prepare_v2(db(), "SELECT tag, content FROM room_tags WHERE user_id=?1 "
                             "AND room_id=?2 AND content IS NOT NULL", -1, &st, NULL);
    sqlite3_bind_text(st, 1, p->user_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, room_id, -1, SQLITE_TRANSIENT);
    bool any_tag = false;
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *c = (const char *)sqlite3_column_text(st, 1);
        const char *err = NULL;
        json *v = c ? j_parse(c, strlen(c), &err) : NULL;
        j_set(tags, (const char *)sqlite3_column_text(st, 0), v ? v : j_obj());
        any_tag = true;
    }
    sqlite3_finalize(st);
    if (any_tag) {
        json *e = j_obj();
        j_set(e, "type", j_str("m.tag"));
        json *c2 = j_obj();
        j_set(c2, "tags", tags);
        j_set(e, "content", c2);
        j_push(ad, e);
    } else {
        j_free(tags);
    }
    j_set(out, "account_data", ad);
    return out;
}

static void h_room_initial_sync(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    const char *lq = http_query(r, "limit");
    int64_t limit = lq ? atoll(lq) : 20;
    if (limit <= 0) limit = 20;
    char *ms = room_membership(r->part[0], p.user_id);
    bool member = ms && !str_eq(ms, "leave");
    free(ms);
    /* A room whose history is world-readable may be peeked into by anyone:
     * that is what world-readable means, and `old_sync.yaml` documents the
     * `visibility` this answer carries for exactly that case. */
    if (!member) {
        json *hv = room_state_event(r->part[0], "m.room.history_visibility", "");
        const char *vis = hv ? j_gets(j_geto(hv, "content"),
                                      "history_visibility", "shared") : "shared";
        bool readable = str_eq(vis, "world_readable");
        j_free(hv);
        if (!readable) {
            resp_error(w, 403, "M_FORBIDDEN", "You are not in the room");
            principal_free(&p);
            return;
        }
    }
    resp_json(w, 200, room_initial_sync(r->part[0], &p, limit));
    principal_free(&p);
}

static void h_initial_sync(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    const char *lq = http_query(r, "limit");
    int64_t limit = lq ? atoll(lq) : 20;
    if (limit <= 0) limit = 20;
    bool archived = str_eq(http_query(r, "archived"), "true");

    json *rooms = j_arr();
    sqlite3_stmt *st = NULL;
    sqlite3_prepare_v2(db(), "SELECT room_id, membership FROM room_members WHERE "
                             "user_id=?1 ORDER BY room_id", -1, &st, NULL);
    sqlite3_bind_text(st, 1, p.user_id, -1, SQLITE_TRANSIENT);
    json *ids = j_arr();
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *ms = (const char *)sqlite3_column_text(st, 1);
        if (!archived && str_eq(ms, "leave")) continue;
        j_push(ids, j_str((const char *)sqlite3_column_text(st, 0)));
    }
    sqlite3_finalize(st);
    for (size_t i = 0; i < j_len(ids); i++)
        j_push(rooms, room_initial_sync(j_at(ids, i)->s, &p, limit));
    j_free(ids);

    json *b = j_obj();
    char *tok = str_fmt("s%lld", (long long)stream_current());
    j_set(b, "next_batch", j_str(tok));
    j_set(b, "end", j_str(tok));
    free(tok);
    j_set(b, "rooms", rooms);
    j_set(b, "presence", j_arr());
    json *ad = j_arr();
    sqlite3_prepare_v2(db(), "SELECT type, content FROM account_data WHERE "
                             "user_id=?1 AND room_id=''", -1, &st, NULL);
    sqlite3_bind_text(st, 1, p.user_id, -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *c = (const char *)sqlite3_column_text(st, 1);
        const char *err = NULL;
        json *v = c ? j_parse(c, strlen(c), &err) : NULL;
        json *e = j_obj();
        j_set(e, "type", j_str((const char *)sqlite3_column_text(st, 0)));
        j_set(e, "content", v ? v : j_obj());
        j_push(ad, e);
    }
    sqlite3_finalize(st);
    j_set(b, "account_data", ad);
    resp_json(w, 200, b);
    principal_free(&p);
}

void api_register_misc(http_server *s) {
    route_both(s, "GET", "/rooms/:/initialSync", h_room_initial_sync);
    route_both(s, "GET", "/initialSync", h_initial_sync);
    route_both(s, "PUT", "/user/:/account_data/:", h_account_data);
    route_both(s, "GET", "/user/:/account_data/:", h_account_data);
    route_both(s, "PUT", "/user/:/rooms/:/account_data/:", h_room_account_data);
    route_both(s, "GET", "/user/:/rooms/:/account_data/:", h_room_account_data);
    route_both(s, "GET", "/user/:/rooms/:/tags", h_tags_get);
    route_both(s, "PUT", "/user/:/rooms/:/tags/:", h_tag_put);
    route_both(s, "DELETE", "/user/:/rooms/:/tags/:", h_tag_delete);
    route_both(s, "POST", "/rooms/:/receipt/:/:", h_receipt);
    route_both(s, "POST", "/rooms/:/read_markers", h_read_markers);
    route_both(s, "PUT", "/rooms/:/typing/:", h_typing);
    route_both(s, "GET", "/presence/:/status", h_presence);
    route_both(s, "PUT", "/presence/:/status", h_presence);
    route_both(s, "POST", "/user_directory/search", h_user_directory);
}

#define _POSIX_C_SOURCE 200809L
#include "mxroom.h"
#include "mxdb.h"
#include "mxserver.h"
#include "mxutil.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

hs_config CFG;

/* The federation module, when it is linked in, wants to know about a new event
 * immediately rather than on its next poll. */
extern void fed_wake(void) __attribute__((weak));

/* The relations module, when it is linked in, gets a look at every event on
 * its way to a client: it attaches unsigned.m.relations and applies the
 * room-version-11 compatibility fixes. */
/* MSC4115: the sender's membership at the event, added by the milestone whose
 * release publishes it. */
extern void media2_membership_hook(json *ev, json *un, const char *user_id)
    __attribute__((weak));
/* An erased account's events are served redacted to anyone who could not
 * already see them. */
extern bool media2_erased_from(const json *ev, const char *viewer)
    __attribute__((weak));
/* The rich-topic block, from the milestone whose release publishes it. */
extern bool topic_compose(json *content) __attribute__((weak));
extern void rel_client_event_hook(json *ev, json *un, const char *user_id)
    __attribute__((weak));

/* ------------------------------------------------------- stream + locking */
static pthread_mutex_t g_write = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_wait = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;
static int64_t g_stream;

void hs_lock(void) { pthread_mutex_lock(&g_write); }
void hs_unlock(void) { pthread_mutex_unlock(&g_write); }

int64_t stream_next(void) {
    db_exec("UPDATE seq SET value = value + 1 WHERE name='stream'");
    int64_t v = 0;
    db_int("SELECT value FROM seq WHERE name='stream'", &v, "");
    pthread_mutex_lock(&g_wait);
    if (v > g_stream) g_stream = v;
    pthread_mutex_unlock(&g_wait);
    return v;
}

int64_t stream_current(void) {
    int64_t v = 0;
    db_int("SELECT value FROM seq WHERE name='stream'", &v, "");
    return v;
}

void stream_notify(void) {
    pthread_mutex_lock(&g_wait);
    g_stream = stream_current();
    pthread_cond_broadcast(&g_cond);
    pthread_mutex_unlock(&g_wait);
}

void stream_wait(int64_t after, int timeout_ms) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    pthread_mutex_lock(&g_wait);
    while (g_stream <= after) {
        if (pthread_cond_timedwait(&g_cond, &g_wait, &ts) != 0) break;
    }
    pthread_mutex_unlock(&g_wait);
}

void sr_free(send_result *r) {
    if (!r) return;
    free(r->errmsg);
    free(r->event_id);
    r->errmsg = r->event_id = NULL;
}

/* ------------------------------------------------------------- metadata -- */
char *room_version_of(const char *room_id) {
    return db_str("SELECT version FROM rooms WHERE room_id=?1", "s", room_id);
}

bool room_exists(const char *room_id) {
    char *v = room_version_of(room_id);
    bool ok = v != NULL;
    free(v);
    return ok;
}

bool room_has_local_member(const char *room_id) {
    char *suffix = str_fmt(":%s", CFG.server_name);
    /* SQLite's LIKE has no anchor, so the pattern carries the wildcard. */
    char *pattern = str_fmt("%%%s", suffix);
    int64_t n = 0;
    db_int("SELECT COUNT(*) FROM room_members WHERE room_id=?1 AND "
           "membership='join' AND user_id LIKE ?2", &n, "ss", room_id, pattern);
    free(suffix);
    free(pattern);
    return n > 0;
}

char *room_membership(const char *room_id, const char *user_id) {
    return db_str("SELECT membership FROM room_members WHERE room_id=?1 AND user_id=?2",
                  "ss", room_id, user_id);
}

int64_t room_latest_stream(const char *room_id) {
    int64_t v = 0;
    db_int("SELECT COALESCE(MAX(stream_id),0) FROM events WHERE room_id=?1", &v,
           "s", room_id);
    return v;
}

/* --------------------------------------------------------------- events -- */
static json *parse_json_col(char *text) {
    if (!text) return NULL;
    const char *err = NULL;
    json *v = j_parse(text, strlen(text), &err);
    free(text);
    return v;
}

json *event_by_id(const char *event_id) {
    return parse_json_col(db_str("SELECT json FROM events WHERE event_id=?1",
                                 "s", event_id));
}

char *event_room(const char *event_id) {
    return db_str("SELECT room_id FROM events WHERE event_id=?1", "s", event_id);
}

bool event_get_stream(const char *event_id, int64_t *stream_id, int64_t *topo) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT stream_id, topological FROM events WHERE event_id=?1",
            -1, &st, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, event_id, -1, SQLITE_TRANSIENT);
    bool got = false;
    if (sqlite3_step(st) == SQLITE_ROW) {
        if (stream_id) *stream_id = sqlite3_column_int64(st, 0);
        if (topo) *topo = sqlite3_column_int64(st, 1);
        got = true;
    }
    sqlite3_finalize(st);
    return got;
}

/* ---------------------------------------------------------------- state -- */
json *room_state_map(const char *room_id) {
    json *out = j_obj();
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT s.type, s.state_key, e.json FROM room_state s "
            "JOIN events e ON e.event_id = s.event_id WHERE s.room_id=?1",
            -1, &st, NULL) != SQLITE_OK) return out;
    sqlite3_bind_text(st, 1, room_id, -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
        char *key = state_key_of((const char *)sqlite3_column_text(st, 0),
                                 (const char *)sqlite3_column_text(st, 1));
        const char *js = (const char *)sqlite3_column_text(st, 2);
        const char *err = NULL;
        json *ev = js ? j_parse(js, strlen(js), &err) : NULL;
        if (ev) j_set(out, key, ev);
        free(key);
    }
    sqlite3_finalize(st);
    return out;
}

json *room_state_at(const char *room_id, int64_t stream_id) {
    json *out = j_obj();
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT type, state_key, json FROM events WHERE room_id=?1 "
            "AND is_state=1 AND stream_id<=?2 ORDER BY stream_id ASC",
            -1, &st, NULL) != SQLITE_OK) return out;
    sqlite3_bind_text(st, 1, room_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, stream_id);
    while (sqlite3_step(st) == SQLITE_ROW) {
        char *key = state_key_of((const char *)sqlite3_column_text(st, 0),
                                 (const char *)sqlite3_column_text(st, 1));
        const char *js = (const char *)sqlite3_column_text(st, 2);
        const char *err = NULL;
        json *ev = js ? j_parse(js, strlen(js), &err) : NULL;
        if (ev) j_set(out, key, ev);
        free(key);
    }
    sqlite3_finalize(st);
    return out;
}

json *room_state_event(const char *room_id, const char *type, const char *sk) {
    char *eid = db_str("SELECT event_id FROM room_state WHERE room_id=?1 AND "
                       "type=?2 AND state_key=?3", "sss", room_id, type,
                       sk ? sk : "");
    if (!eid) return NULL;
    json *ev = event_by_id(eid);
    free(eid);
    return ev;
}

int64_t room_user_level(const char *room_id, const char *user_id) {
    json *pl = room_state_event(room_id, "m.room.power_levels", "");
    int64_t lvl;
    if (pl) {
        lvl = pl_user_level(j_geto(pl, "content"), user_id);
    } else {
        char *creator = db_str("SELECT creator FROM rooms WHERE room_id=?1",
                               "s", room_id);
        lvl = (creator && str_eq(creator, user_id)) ? 100 : 0;
        free(creator);
    }
    j_free(pl);
    return lvl;
}

/* ------------------------------------------------------------ serialise -- */
json *event_to_client(const json *stored, const char *user_id,
                      const char *device_id, bool include_room_id) {
    if (!stored) return NULL;
    json *ev = j_clone(stored);
    const char *eid = j_gets(ev, "event_id", NULL);
    if (media2_erased_from && media2_erased_from(ev, user_id)) {
        char *roomid = j_gets(ev, "room_id", NULL)
            ? xstrdup_(j_gets(ev, "room_id", "")) : (eid ? event_room(eid) : NULL);
        char *ver = roomid ? room_version_of(roomid) : NULL;
        const roomver *rv = rv_get(ver ? ver : rv_default());
        json *r = event_redact(ev, rv ? rv : rv_get(rv_default()));
        j_free(ev);
        ev = r;
        free(ver);
        free(roomid);
    }
    char *redacted_by = eid ? db_str("SELECT redacted_by FROM events WHERE "
                                     "event_id=?1", "s", eid) : NULL;
    if (redacted_by) {
        char *roomid = j_gets(ev, "room_id", NULL)
            ? xstrdup_(j_gets(ev, "room_id", "")) : event_room(eid);
        char *ver = roomid ? room_version_of(roomid) : NULL;
        const roomver *rv = rv_get(ver ? ver : rv_default());
        json *r = event_redact(ev, rv ? rv : rv_get(rv_default()));
        j_free(ev);
        ev = r;
        json *un = j_geto(ev, "unsigned");
        if (!un) { un = j_obj(); j_set(ev, "unsigned", un); }
        json *because = event_by_id(redacted_by);
        if (because) {
            json *bc = event_to_client(because, NULL, NULL, true);
            j_free(because);
            j_set(un, "redacted_because", bc);
        }
        free(ver);
        free(roomid);
        free(redacted_by);
    }
    j_del(ev, "hashes");
    j_del(ev, "signatures");
    j_del(ev, "depth");
    j_del(ev, "prev_events");
    j_del(ev, "auth_events");
    j_del(ev, "prev_state");
    j_del(ev, "origin");
    if (!include_room_id) j_del(ev, "room_id");

    json *un = j_geto(ev, "unsigned");
    if (!un) { un = j_obj(); j_set(ev, "unsigned", un); }
    char *txn_owner_user = xstrdup_(j_gets(un, "__txn_user", NULL));
    char *txn_owner_dev = xstrdup_(j_gets(un, "__txn_device", NULL));
    char *txn = xstrdup_(j_gets(un, "__txn_id", NULL));
    j_del(un, "__txn_user");
    j_del(un, "__txn_device");
    j_del(un, "__txn_id");
    if (txn && user_id && device_id && str_eq(txn_owner_user, user_id) &&
        str_eq(txn_owner_dev, device_id)) {
        j_set(un, "transaction_id", j_str(txn));
    }
    free(txn_owner_user);
    free(txn_owner_dev);
    free(txn);
    int64_t ts = j_geti(ev, "origin_server_ts", 0);
    j_set(un, "age", j_int(now_ms() - ts));
    if (rel_client_event_hook) rel_client_event_hook(ev, un, user_id);
    if (media2_membership_hook) media2_membership_hook(ev, un, user_id);
    if (j_len(un) == 0) j_del(ev, "unsigned");
    return ev;
}

json *event_to_stripped(const json *stored) {
    json *out = j_obj();
    j_set(out, "type", j_str(j_gets(stored, "type", "")));
    j_set(out, "state_key", j_str(j_gets(stored, "state_key", "")));
    j_set(out, "sender", j_str(j_gets(stored, "sender", "")));
    json *c = j_geto((json *)stored, "content");
    j_set(out, "content", c ? j_clone(c) : j_obj());
    return out;
}

/* -------------------------------------------------------------- sending -- */
static void put_event_ref(json *arr, const char *event_id, const roomver *rv,
                          const json *ev) {
    if (rv->event_format == 1) {
        json *pair = j_arr();
        j_push(pair, j_str(event_id));
        json *h = j_obj();
        unsigned char ref[32];
        if (ev && event_reference_hash(ev, rv, ref)) {
            char *b = b64_encode(ref, 32, false, false);
            j_set(h, "sha256", j_str(b));
            free(b);
        } else {
            j_set(h, "sha256", j_str(""));
        }
        j_push(pair, h);
        j_push(arr, pair);
    } else {
        j_push(arr, j_str(event_id));
    }
}

send_result room_send(const char *room_id, const char *sender, const char *type,
                      const char *state_key, json *content, json *extra_unsigned) {
    send_result res = {0};
    char *ver = room_version_of(room_id);
    const roomver *rv = rv_get(ver ? ver : "");
    if (!rv) {
        free(ver);
        j_free(content);
        j_free(extra_unsigned);
        res.status = 404;
        res.errcode = "M_NOT_FOUND";
        res.errmsg = xstrdup_("Unknown room");
        return res;
    }
    if (rv->strict_canonical_json &&
        (j_has_float(content) || j_has_unsafe_int(content))) {
        free(ver); j_free(content); j_free(extra_unsigned);
        res.status = 400;
        res.errcode = "M_BAD_JSON";
        res.errmsg = xstrdup_("This room version strictly enforces canonical JSON");
        return res;
    }

    if (str_eq(type, "m.room.power_levels") && content &&
        content->type == J_OBJECT) {
        static const char *SCALARS[] = {"ban", "kick", "redact", "invite",
                                        "events_default", "state_default",
                                        "users_default", NULL};
        bool bad = false;
        for (int i = 0; !bad && SCALARS[i]; i++) {
            json *v = j_get(content, SCALARS[i]);
            if (v && v->type != J_INT) bad = true;
        }
        static const char *MAPS[] = {"users", "events", "notifications", NULL};
        for (int i = 0; !bad && MAPS[i]; i++) {
            json *m = j_geto(content, MAPS[i]);
            for (size_t k = 0; m && !bad && k < j_len(m); k++)
                if (j_at(m, k)->type != J_INT) bad = true;
        }
        if (bad) {
            free(ver); j_free(content); j_free(extra_unsigned);
            res.status = 400;
            res.errcode = "M_BAD_JSON";
            res.errmsg = xstrdup_("Power levels must be integers");
            return res;
        }
    }

    json *state = room_state_map(room_id);

    /* prev_events: the room's single forward extremity. */
    char *prev_id = db_str("SELECT event_id FROM events WHERE room_id=?1 "
                           "ORDER BY topological DESC, stream_id DESC LIMIT 1",
                           "s", room_id);
    int64_t depth = 1;
    db_int("SELECT COALESCE(MAX(depth),0)+1 FROM events WHERE room_id=?1",
           &depth, "s", room_id);

    json *ev = j_obj();
    j_set(ev, "room_id", j_str(room_id));
    j_set(ev, "type", j_str(type));
    j_set(ev, "sender", j_str(sender));
    if (state_key) j_set(ev, "state_key", j_str(state_key));
    /* Before room version 11 a redaction names its target at the top level of
     * the event rather than inside `content`. */
    if (!rv->updated_redaction_rules && str_eq(type, "m.room.redaction")) {
        const char *target = j_gets(content, "redacts", NULL);
        if (target) {
            j_set(ev, "redacts", j_str(target));
            j_del(content, "redacts");
        }
    }
    j_set(ev, "content", content);
    j_set(ev, "depth", j_int(depth));
    j_set(ev, "origin_server_ts", j_int(now_ms()));

    json *prevs = j_arr();
    if (prev_id) {
        json *pe = event_by_id(prev_id);
        put_event_ref(prevs, prev_id, rv, pe);
        j_free(pe);
    }
    j_set(ev, "prev_events", prevs);

    json *auth = j_arr();
    json *keys = event_auth_keys(ev);
    for (size_t i = 0; i < j_len(keys); i++) {
        const char *k = j_at(keys, i)->s;
        json *se = j_get(state, k);
        if (!se) continue;
        const char *sid = j_gets(se, "event_id", NULL);
        if (sid) put_event_ref(auth, sid, rv, se);
    }
    j_free(keys);
    j_set(ev, "auth_events", auth);

    const char *why = "not allowed";
    if (!event_auth_allowed(ev, state, rv, &why)) {
        j_free(state);
        j_free(ev);
        j_free(extra_unsigned);
        free(prev_id);
        free(ver);
        res.status = 403;
        res.errcode = "M_FORBIDDEN";
        res.errmsg = xstrdup_(why);
        return res;
    }

    /* The Server-Server API's "Signing Events": for room versions whose event
     * carries its own id, that id is part of what is hashed and signed, so it
     * is minted first. */
    j_set(ev, "origin", j_str(CFG.server_name));
    if (rv->event_format == 1) {
        char *pre = event_compute_id(ev, rv, CFG.server_name);
        j_set(ev, "event_id", j_str(pre));
        free(pre);
    }

    /* content hash, signature, event id */
    char *chash = event_content_hash_b64(ev);
    if (!chash) {
        j_free(state); j_free(ev); j_free(extra_unsigned);
        free(prev_id); free(ver);
        res.status = 400;
        res.errcode = "M_BAD_JSON";
        res.errmsg = xstrdup_("Event content cannot be encoded as canonical JSON");
        return res;
    }
    json *hashes = j_obj();
    j_set(hashes, "sha256", j_str(chash));
    free(chash);
    j_set(ev, "hashes", hashes);

    /* The signature covers the *redacted* event, so that redacting it later
     * does not invalidate the signature. */
    json *red = event_redact(ev, rv);
    j_del(red, "unsigned");
    json_sign(red, CFG.server_name, CFG.key_id, CFG.key_seed);
    json *sigs = j_get(red, "signatures");
    if (sigs) j_set(ev, "signatures", j_clone(sigs));
    j_free(red);

    char *eid = rv->event_format == 1
        ? xstrdup_(j_gets(ev, "event_id", ""))
        : event_compute_id(ev, rv, CFG.server_name);
    j_set(ev, "event_id", j_str(eid));

    /* unsigned: prev_content for state events, plus caller extras */
    json *un = j_obj();
    if (state_key) {
        char *k = state_key_of(type, state_key);
        json *old = j_get(state, k);
        free(k);
        if (old) {
            json *oc = j_geto(old, "content");
            if (oc) j_set(un, "prev_content", j_clone(oc));
        }
    }
    if (extra_unsigned) {
        for (size_t i = 0; i < j_len(extra_unsigned); i++)
            j_set(un, j_key(extra_unsigned, i), j_clone(j_at(extra_unsigned, i)));
        j_free(extra_unsigned);
    }
    if (j_len(un)) j_set(ev, "unsigned", un); else j_free(un);

    int64_t sid = stream_next();
    size_t jn = 0;
    char *js = j_dump(ev, &jn);
    db_run("INSERT INTO events(stream_id,event_id,room_id,type,state_key,sender,"
           "depth,origin_server_ts,json,is_state,topological) "
           "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11)",
           "isssssiisii", sid, eid, room_id, type, state_key ? state_key : NULL,
           sender, depth, j_geti(ev, "origin_server_ts", 0), js,
           (int64_t)(state_key ? 1 : 0), depth);
    free(js);

    if (state_key) {
        db_run("INSERT INTO room_state(room_id,type,state_key,event_id) "
               "VALUES(?1,?2,?3,?4) ON CONFLICT(room_id,type,state_key) "
               "DO UPDATE SET event_id=?4", "ssss", room_id, type, state_key, eid);
        if (str_eq(type, "m.room.member")) {
            json *c = j_geto(ev, "content");
            const char *ms = c ? j_gets(c, "membership", "leave") : "leave";
            db_run("INSERT INTO room_members(room_id,user_id,membership,event_id,"
                   "stream_id) VALUES(?1,?2,?3,?4,?5) ON CONFLICT(room_id,user_id) "
                   "DO UPDATE SET membership=?3, event_id=?4, stream_id=?5",
                   "ssssi", room_id, state_key, ms, eid, sid);
        }
    }

    j_free(state);
    j_free(ev);
    free(prev_id);
    free(ver);
    res.event_id = eid;
    res.stream_id = sid;
    if (fed_wake) fed_wake();
    return res;
}

/* ---------------------------------------------------------- membership --- */
send_result room_set_membership(const char *room_id, const char *sender,
                                const char *target, const char *membership,
                                const char *reason, json *extra_content) {
    json *content = extra_content ? extra_content : j_obj();
    j_set(content, "membership", j_str(membership));
    if (reason && *reason) j_set(content, "reason", j_str(reason));
    if (!str_eq(membership, "leave") && !str_eq(membership, "ban")) {
        char *dn = db_str("SELECT displayname FROM users WHERE user_id=?1", "s", target);
        char *av = db_str("SELECT avatar_url FROM users WHERE user_id=?1", "s", target);
        if (dn) { j_set(content, "displayname", j_str(dn)); free(dn); }
        if (av) { j_set(content, "avatar_url", j_str(av)); free(av); }
    }
    return room_send(room_id, sender, "m.room.member", target, content, NULL);
}

/* ------------------------------------------------------------- creation -- */
static const char *preset_join_rule(const char *preset) {
    if (str_eq(preset, "public_chat")) return "public";
    return "invite";
}

char *room_create(const char *creator, const json *body, int *status,
                  const char **errcode, char **errmsg) {
    const char *version = j_gets(body, "room_version", NULL);
    if (!version) version = rv_default();
    const roomver *rv = rv_get(version);
    if (!rv) {
        *status = 400;
        *errcode = "M_UNSUPPORTED_ROOM_VERSION";
        *errmsg = str_fmt("Room version %s is not supported", version);
        return NULL;
    }
    /* Every third-party invitation must name the identity service and the
     * address it is for; an incomplete one is a malformed request. */
    json *inv3 = j_geta((json *)body, "invite_3pid");
    for (size_t i = 0; inv3 && i < j_len(inv3); i++) {
        json *e = j_at(inv3, i);
        if (!e || e->type != J_OBJECT || !j_gets(e, "address", NULL) ||
            !j_gets(e, "id_server", NULL) || !j_gets(e, "medium", NULL)) {
            *status = 400;
            *errcode = "M_BAD_JSON";
            *errmsg = xstrdup_("invite_3pid entries need medium, address and "
                               "id_server");
            return NULL;
        }
    }
    const char *visibility = j_gets(body, "visibility", "private");
    const char *preset = j_gets(body, "preset", NULL);
    if (!preset)
        preset = str_eq(visibility, "public") ? "public_chat" : "private_chat";
    if (!str_eq(preset, "public_chat") && !str_eq(preset, "private_chat") &&
        !str_eq(preset, "trusted_private_chat")) {
        *status = 400;
        *errcode = "M_INVALID_PARAM";
        *errmsg = str_fmt("Unknown preset %s", preset);
        return NULL;
    }

    char *local = random_string(18);
    char *room_id = str_fmt("!%s:%s", local, CFG.server_name);
    free(local);

    const char *alias_name = j_gets(body, "room_alias_name", NULL);
    char *alias = NULL;
    if (alias_name) {
        alias = str_fmt("#%s:%s", alias_name, CFG.server_name);
        char *existing = db_str("SELECT room_id FROM aliases WHERE alias=?1",
                                "s", alias);
        if (existing) {
            free(existing); free(alias); free(room_id);
            *status = 400;
            *errcode = "M_ROOM_IN_USE";
            *errmsg = xstrdup_("Room alias already taken");
            return NULL;
        }
    }

    db_run("INSERT INTO rooms(room_id,version,creator,created_ts,published) "
           "VALUES(?1,?2,?3,?4,?5)", "sssii", room_id, version, creator,
           now_ms(), (int64_t)(str_eq(visibility, "public") ? 1 : 0));

    /* 1. m.room.create */
    json *cc = j_obj();
    json *ccin = j_geto((json *)body, "creation_content");
    if (ccin)
        for (size_t i = 0; i < j_len(ccin); i++)
            j_set(cc, j_key(ccin, i), j_clone(j_at(ccin, i)));
    /* Room version 11 drops `creator` from the create event: the creator is
     * the event's sender. */
    if (!rv->updated_redaction_rules) j_set(cc, "creator", j_str(creator));
    j_set(cc, "room_version", j_str(version));
    send_result r = room_send(room_id, creator, "m.room.create", "", cc, NULL);
    if (r.status) { *status = r.status; *errcode = r.errcode; *errmsg = r.errmsg;
                    free(room_id); free(alias); return NULL; }
    sr_free(&r);

    /* 2. creator joins */
    r = room_set_membership(room_id, creator, creator, "join", NULL, NULL);
    sr_free(&r);

    /* 3. m.room.power_levels */
    json *pl = j_obj();
    json *users = j_obj();
    j_set(users, creator, j_int(100));
    if (str_eq(preset, "trusted_private_chat")) {
        json *inv = j_geta((json *)body, "invite");
        for (size_t i = 0; inv && i < j_len(inv); i++) {
            json *u = j_at(inv, i);
            if (j_is_str(u)) j_set(users, u->s, j_int(100));
        }
    }
    j_set(pl, "users", users);
    j_set(pl, "users_default", j_int(0));
    json *events = j_obj();
    j_set(events, "m.room.name", j_int(50));
    j_set(events, "m.room.power_levels", j_int(100));
    j_set(events, "m.room.history_visibility", j_int(100));
    j_set(events, "m.room.canonical_alias", j_int(50));
    j_set(events, "m.room.avatar", j_int(50));
    j_set(events, "m.room.tombstone", j_int(100));
    j_set(events, "m.room.server_acl", j_int(100));
    j_set(events, "m.room.encryption", j_int(100));
    j_set(pl, "events", events);
    j_set(pl, "events_default", j_int(0));
    j_set(pl, "state_default", j_int(50));
    j_set(pl, "ban", j_int(50));
    j_set(pl, "kick", j_int(50));
    j_set(pl, "redact", j_int(50));
    j_set(pl, "invite", j_int(0));
    j_set(pl, "historical", j_int(100));
    json *over = j_geto((json *)body, "power_level_content_override");
    if (over)
        for (size_t i = 0; i < j_len(over); i++)
            j_set(pl, j_key(over, i), j_clone(j_at(over, i)));
    r = room_send(room_id, creator, "m.room.power_levels", "", pl, NULL);
    sr_free(&r);

    /* 4. join rules, history visibility, guest access */
    json *jr = j_obj();
    j_set(jr, "join_rule", j_str(preset_join_rule(preset)));
    r = room_send(room_id, creator, "m.room.join_rules", "", jr, NULL);
    sr_free(&r);

    json *hv = j_obj();
    j_set(hv, "history_visibility", j_str("shared"));
    r = room_send(room_id, creator, "m.room.history_visibility", "", hv, NULL);
    sr_free(&r);

    json *ga = j_obj();
    j_set(ga, "guest_access",
          j_str(str_eq(preset, "public_chat") ? "forbidden" : "can_join"));
    r = room_send(room_id, creator, "m.room.guest_access", "", ga, NULL);
    sr_free(&r);

    /* 5. initial_state */
    json *init = j_geta((json *)body, "initial_state");
    for (size_t i = 0; init && i < j_len(init); i++) {
        json *e = j_at(init, i);
        const char *t = j_gets(e, "type", NULL);
        if (!t) continue;
        json *c = j_geto(e, "content");
        r = room_send(room_id, creator, t, j_gets(e, "state_key", ""),
                      c ? j_clone(c) : j_obj(), NULL);
        sr_free(&r);
    }

    /* 6. name, topic */
    const char *name = j_gets(body, "name", NULL);
    if (name) {
        json *c = j_obj();
        j_set(c, "name", j_str(name));
        r = room_send(room_id, creator, "m.room.name", "", c, NULL);
        sr_free(&r);
    }
    const char *topic = j_gets(body, "topic", NULL);
    if (topic) {
        json *c = j_obj();
        j_set(c, "topic", j_str(topic));
        /* The server is composing this event, so it writes both
         * representations where the milestone that defines the second one is
         * present. */
        if (topic_compose) topic_compose(c);
        r = room_send(room_id, creator, "m.room.topic", "", c, NULL);
        sr_free(&r);
    }

    /* 7. alias */
    if (alias) {
        db_run("INSERT INTO aliases(alias,room_id,creator) VALUES(?1,?2,?3)",
               "sss", alias, room_id, creator);
        json *c = j_obj();
        j_set(c, "alias", j_str(alias));
        r = room_send(room_id, creator, "m.room.canonical_alias", "", c, NULL);
        sr_free(&r);
    }

    /* 8. invites */
    json *inv = j_geta((json *)body, "invite");
    for (size_t i = 0; inv && i < j_len(inv); i++) {
        json *u = j_at(inv, i);
        if (!j_is_str(u)) continue;
        json *extra = NULL;
        if (j_getb(body, "is_direct", false)) {
            extra = j_obj();
            j_set(extra, "is_direct", j_bool(true));
        }
        r = room_set_membership(room_id, creator, u->s, "invite", NULL, extra);
        sr_free(&r);
    }

    free(alias);
    stream_notify();
    return room_id;
}

/* -------------------------------------------------- history visibility --- */
bool room_can_see(const char *room_id, const char *user_id, int64_t stream_id) {
    json *state = room_state_at(room_id, stream_id);
    json *hv = state_lookup(state, "m.room.history_visibility", "");
    const char *vis = "shared";
    if (hv) {
        json *c = j_geto(hv, "content");
        if (c) vis = j_gets(c, "history_visibility", "shared");
    }
    if (str_eq(vis, "world_readable")) { j_free(state); return true; }

    /* An event is judged against the membership around it: the state after it
     * (so a user sees their own join) and the state before it (so a user sees
     * their own leave, and the events that preceded it). */
    json *me = state_lookup(state, "m.room.member", user_id);
    const char *ms_then = "leave";
    if (me) {
        json *c = j_geto(me, "content");
        if (c) ms_then = j_gets(c, "membership", "leave");
    }
    json *before = room_state_at(room_id, stream_id - 1);
    json *me_before = state_lookup(before, "m.room.member", user_id);
    const char *ms_before = "leave";
    if (me_before) {
        json *c = j_geto(me_before, "content");
        if (c) ms_before = j_gets(c, "membership", "leave");
    }
    bool joined_around = str_eq(ms_then, "join") || str_eq(ms_before, "join");
    j_free(before);
    bool ok = false;
    if (joined_around) {
        ok = true;
    } else if (str_eq(vis, "invited")) {
        ok = str_eq(ms_then, "invite") || str_eq(ms_before, "invite");
    } else if (str_eq(vis, "shared")) {
        /* "shared" hands the room's history to anyone who is or was a member:
         * a late joiner sees everything that came before, and a user who has
         * left keeps the history up to their own departure -- but nothing
         * after it, which is where the membership row's stream position (the
         * user's most recent membership event) draws the line. */
        char *now = room_membership(room_id, user_id);
        if (now && str_eq(now, "join")) {
            ok = true;
        } else if (now && str_eq(now, "leave")) {
            int64_t left_at = 0;
            db_int("SELECT COALESCE(stream_id,0) FROM room_members "
                   "WHERE room_id=?1 AND user_id=?2", &left_at, "ss",
                   room_id, user_id);
            ok = stream_id <= left_at;
        }
        free(now);
    }
    j_free(state);
    return ok;
}

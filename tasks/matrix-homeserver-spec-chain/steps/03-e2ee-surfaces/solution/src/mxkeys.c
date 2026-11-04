#define _POSIX_C_SOURCE 200809L
/* End-to-end encryption: the /keys and /room_keys endpoints, /sendToDevice
 * federation user-key endpoints and EDUs.  See mxkeys.h. */
#include "mxkeys.h"

#include "mxapi.h"
#include "mxdb.h"
#include "mxevent.h"
#include "mxfed.h"
#include "mxroom.h"
#include "mxserver.h"
#include "mxutil.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CS  "/_matrix/client/v3"
#define CSR "/_matrix/client/r0"
#define FED1 "/_matrix/federation/v1"

/* A /sync response carries at most this many to-device messages, so one
 * transaction must not be allowed to be unboundedly larger. */
#define MAX_TO_DEVICE_TARGETS 2000

/* ============================================================== schema === */
/* Everything this module needs that mxdb.c's schema does not already carry.
 * Created from api_register_keys() so mxdb.c stays untouched. */
static const char *KEYS_SCHEMA =
/* One row per device that has published identity keys.  `keys_json` is the
 * DeviceKeys object exactly as it was uploaded. */
"CREATE TABLE IF NOT EXISTS e2e_device_keys("
"  user_id TEXT, device_id TEXT, keys_json TEXT, stream_id INTEGER,"
"  created_ts INTEGER, PRIMARY KEY(user_id, device_id));"
/* Unclaimed one-time keys.  `seq` orders them so the oldest is served first;
 * `key_json` is the serialised string or KeyObject. */
"CREATE TABLE IF NOT EXISTS e2e_one_time_keys("
"  user_id TEXT, device_id TEXT, algorithm TEXT, key_id TEXT, key_json TEXT,"
"  seq INTEGER, created_ts INTEGER,"
"  PRIMARY KEY(user_id, device_id, algorithm, key_id));"
"CREATE INDEX IF NOT EXISTS e2e_otk_pick ON"
"  e2e_one_time_keys(user_id, device_id, algorithm, seq);"
/* At most one fallback key per device and algorithm; it is handed out again
 * and again once the one-time keys run out, until it is replaced. */
"CREATE TABLE IF NOT EXISTS e2e_fallback_keys("
"  user_id TEXT, device_id TEXT, algorithm TEXT, key_id TEXT, key_json TEXT,"
"  used INTEGER DEFAULT 0, created_ts INTEGER,"
"  PRIMARY KEY(user_id, device_id, algorithm));"
/* master / self_signing / user_signing, one row each per user. */
"CREATE TABLE IF NOT EXISTS e2e_cross_signing_keys("
"  user_id TEXT, usage TEXT, key_json TEXT, stream_id INTEGER,"
"  PRIMARY KEY(user_id, usage));"
/* Signatures uploaded through /keys/signatures/upload.  `target_key` is the
 * device id for a device key and the unpadded base64 public key for a
 * cross-signing key. */
"CREATE TABLE IF NOT EXISTS e2e_signatures("
"  target_user TEXT, target_key TEXT, signer_user TEXT, signer_key TEXT,"
"  signature TEXT, stream_id INTEGER,"
"  PRIMARY KEY(target_user, target_key, signer_user, signer_key));"
/* The device-list change stream that /keys/changes and /sync's device_lists
 * are built from. */
"CREATE TABLE IF NOT EXISTS e2e_device_changes("
"  stream_id INTEGER PRIMARY KEY, user_id TEXT, device_id TEXT,"
"  deleted INTEGER DEFAULT 0, ts INTEGER);"
"CREATE INDEX IF NOT EXISTS e2e_dc_user ON e2e_device_changes(user_id, stream_id);"
/* The per-user counter carried in m.device_list_update's `stream_id`. */
"CREATE TABLE IF NOT EXISTS e2e_user_stream("
"  user_id TEXT PRIMARY KEY, value INTEGER NOT NULL);"
/* Remote users' cross-signing keys, learned from m.signing_key_update or from
 * /user/keys/query.  (Their device keys live in mxfed.c's fed_devices.) */
"CREATE TABLE IF NOT EXISTS e2e_remote_cross_signing("
"  user_id TEXT, usage TEXT, key_json TEXT, ts INTEGER,"
"  PRIMARY KEY(user_id, usage));"
/* Server-side key backups. */
"CREATE TABLE IF NOT EXISTS e2e_backup_versions("
"  user_id TEXT, version INTEGER, algorithm TEXT, auth_data TEXT,"
"  etag INTEGER DEFAULT 0, deleted INTEGER DEFAULT 0, created_ts INTEGER,"
"  PRIMARY KEY(user_id, version));"
"CREATE TABLE IF NOT EXISTS e2e_backup_keys("
"  user_id TEXT, version INTEGER, room_id TEXT, session_id TEXT,"
"  first_message_index INTEGER, forwarded_count INTEGER, is_verified INTEGER,"
"  session_data TEXT, PRIMARY KEY(user_id, version, room_id, session_id));"
/* The ordering counter for one-time keys, shared by every device. */
"INSERT OR IGNORE INTO seq(name,value) VALUES('e2e_otk',0);"
;

/* ============================================================ utilities == */
static json *parse_text(const char *t) {
    return t ? j_parse(t, strlen(t), NULL) : NULL;
}

static char *dupn_(const char *s, size_t n) {
    char *out = malloc(n + 1);
    if (!out) abort();
    memcpy(out, s, n);
    out[n] = 0;
    return out;
}

/* Parses and frees a string that came from db_str(). */
static json *parse_owned(char *t) {
    json *v = parse_text(t);
    free(t);
    return v;
}

static char *dump_str(const json *v) {
    size_t n = 0;
    return j_dump(v, &n);
}

static const char *col(sqlite3_stmt *st, int i) {
    const char *s = (const char *)sqlite3_column_text(st, i);
    return s ? s : "";
}

/* Canonical JSON of an object with `signatures` and `unsigned` removed: the
 * part of a key object that may never change once it has been published. */
static char *key_material(const json *obj) {
    json *c = j_clone(obj);
    j_del(c, "signatures");
    j_del(c, "unsigned");
    size_t n = 0;
    char *s = j_canonical(c, &n);
    if (!s) s = j_dump(c, &n);
    j_free(c);
    return s;
}

/* "ed25519:AAAA" -> the 32 raw bytes of AAAA. */
static bool pub_from_b64(const char *b64, unsigned char out[32]) {
    if (!b64 || !*b64) return false;
    size_t n = 0;
    unsigned char *raw = b64_decode(b64, &n);
    if (!raw || n != 32) { free(raw); return false; }
    memcpy(out, raw, 32);
    free(raw);
    return true;
}

static const char *after_colon(const char *s) {
    const char *c = s ? strchr(s, ':') : NULL;
    return c ? c + 1 : NULL;
}

/* j_set into obj[a][b], creating the intermediate object. */
static void set_nested(json *obj, const char *a, const char *b, json *v) {
    json *inner = j_geto(obj, a);
    if (!inner) { inner = j_obj(); j_set(obj, a, inner); }
    j_set(inner, b, v);
}

static json *obj_at(json *obj, const char *key) {
    json *inner = j_geto(obj, key);
    if (!inner) { inner = j_obj(); j_set(obj, key, inner); }
    return inner;
}

static void arr_add_unique(json *arr, const char *s) {
    if (!s) return;
    for (size_t i = 0; i < j_len(arr); i++) {
        json *v = j_at(arr, i);
        if (v && v->type == J_STRING && str_eq(v->s, s)) return;
    }
    j_push(arr, j_str(s));
}

/* The M_WRONG_ROOM_KEYS_VERSION error carries the current version alongside
 * errcode and error, which resp_error() cannot express. */
static void resp_error_extra(http_resp *w, int status, const char *errcode,
                             const char *msg, const char *extra_key,
                             json *extra_value) {
    json *o = j_obj();
    j_set(o, "errcode", j_str(errcode));
    j_set(o, "error", j_str(msg));
    if (extra_key) j_set(o, extra_key, extra_value);
    else j_free(extra_value);
    resp_json(w, status, o);
}

static bool user_is_local(const char *user_id) {
    return user_id && fed_is_local_user(user_id);
}

/* ================================================= device-list bookkeeping */
/* The per-user counter that goes into m.device_list_update.stream_id.  The
 * caller holds the write lock. */
static int64_t user_stream_next(const char *user_id, int64_t *prev_out) {
    int64_t prev = 0;
    db_int("SELECT value FROM e2e_user_stream WHERE user_id=?1", &prev, "s",
           user_id);
    int64_t next = prev + 1;
    db_run("INSERT INTO e2e_user_stream(user_id,value) VALUES(?1,?2) "
           "ON CONFLICT(user_id) DO UPDATE SET value=?2", "si", user_id, next);
    if (prev_out) *prev_out = prev;
    return next;
}

/* Every remote server that shares a room with `user_id`, as an array of
 * "<room_id>" values chosen so that each server appears in at least one of
 * them and no room is broadcast to twice for the same set of servers. */
static json *rooms_covering_servers(const char *user_id) {
    json *rooms = j_arr();
    json *covered = j_obj();
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(), "SELECT room_id FROM room_members WHERE "
                                 "user_id=?1 AND membership='join' "
                                 "ORDER BY room_id", -1, &st, NULL) != SQLITE_OK) {
        j_free(covered);
        return rooms;
    }
    sqlite3_bind_text(st, 1, user_id, -1, SQLITE_TRANSIENT);
    json *all = j_arr();
    while (sqlite3_step(st) == SQLITE_ROW) j_push(all, j_str(col(st, 0)));
    sqlite3_finalize(st);

    for (size_t i = 0; i < j_len(all); i++) {
        const char *room_id = j_at(all, i)->s;
        json *servers = fed_room_servers(room_id);
        bool useful = false;
        for (size_t k = 0; k < j_len(servers); k++) {
            json *sv = j_at(servers, k);
            if (!sv || sv->type != J_STRING) continue;
            if (!j_get(covered, sv->s)) { useful = true; j_set(covered, sv->s, j_bool(true)); }
        }
        j_free(servers);
        if (useful) j_push(rooms, j_str(room_id));
    }
    j_free(all);
    j_free(covered);
    return rooms;
}

/* Sends one EDU to every remote server that shares a room with `user_id`.
 * Takes ownership of `content`. */
static void broadcast_user_edu(const char *user_id, const char *edu_type,
                               json *content) {
    if (CFG.disable_federation) { j_free(content); return; }
    json *rooms = rooms_covering_servers(user_id);
    for (size_t i = 0; i < j_len(rooms); i++)
        fed_broadcast_edu(j_at(rooms, i)->s, edu_type, j_clone(content));
    j_free(rooms);
    j_free(content);
}

static json *device_keys_raw(const char *user_id, const char *device_id) {
    return parse_owned(db_str("SELECT keys_json FROM e2e_device_keys WHERE "
                              "user_id=?1 AND device_id=?2", "ss",
                              user_id, device_id));
}

/* Records the change and tells the other servers.  The caller must NOT hold
 * the write lock: this takes it. */
void keys_device_list_changed(const char *user_id, const char *device_id,
                              bool deleted) {
    if (!user_id || !device_id || !user_is_local(user_id)) return;
    hs_lock();
    int64_t sid = stream_next();
    db_run("INSERT INTO e2e_device_changes(stream_id,user_id,device_id,deleted,ts) "
           "VALUES(?1,?2,?3,?4,?5)", "issii", sid, user_id, device_id,
           (int64_t)(deleted ? 1 : 0), now_ms());
    int64_t prev = 0;
    int64_t ustream = user_stream_next(user_id, &prev);
    hs_unlock();
    stream_notify();

    json *c = j_obj();
    j_set(c, "user_id", j_str(user_id));
    j_set(c, "device_id", j_str(device_id));
    j_set(c, "stream_id", j_int(ustream));
    json *prev_ids = j_arr();
    if (prev > 0) j_push(prev_ids, j_int(prev));
    j_set(c, "prev_id", prev_ids);
    if (deleted) {
        j_set(c, "deleted", j_bool(true));
    } else {
        char *dn = db_str("SELECT display_name FROM devices WHERE user_id=?1 "
                          "AND device_id=?2", "ss", user_id, device_id);
        if (dn) { j_set(c, "device_display_name", j_str(dn)); free(dn); }
        json *keys = device_keys_raw(user_id, device_id);
        if (keys) j_set(c, "keys", keys);
    }
    broadcast_user_edu(user_id, "m.device_list_update", c);
}

/* A cross-signing key change is announced with m.signing_key_update and, so
 * that clients notice, as a device-list change too. */
static void signing_keys_changed(const char *user_id) {
    if (!user_id || !user_is_local(user_id)) return;
    hs_lock();
    int64_t sid = stream_next();
    db_run("INSERT INTO e2e_device_changes(stream_id,user_id,device_id,deleted,ts) "
           "VALUES(?1,?2,'',0,?3)", "isi", sid, user_id, now_ms());
    hs_unlock();
    stream_notify();

    json *c = j_obj();
    j_set(c, "user_id", j_str(user_id));
    json *mk = parse_owned(db_str("SELECT key_json FROM e2e_cross_signing_keys "
                                  "WHERE user_id=?1 AND usage='master'", "s",
                                  user_id));
    if (mk) j_set(c, "master_key", mk);
    json *sk = parse_owned(db_str("SELECT key_json FROM e2e_cross_signing_keys "
                                  "WHERE user_id=?1 AND usage='self_signing'",
                                  "s", user_id));
    if (sk) j_set(c, "self_signing_key", sk);
    broadcast_user_edu(user_id, "m.signing_key_update", c);
}

/* Devices are created and destroyed in mxapi_account.c, which cannot call
 * this module.  Every handler here therefore reconciles first: a device that
 * has published keys but no longer exists is a removal, and its keys go with
 * it.  Returns the stream position before the reconciliation ran, so a change
 * we only just learned of can still be reported for an older window. */
static int64_t reconcile_devices(void) {
    int64_t floor_sid = stream_current();
    json *gone = j_arr();
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT k.user_id, k.device_id FROM e2e_device_keys k "
            "WHERE NOT EXISTS (SELECT 1 FROM devices d WHERE "
            "  d.user_id=k.user_id AND d.device_id=k.device_id)",
            -1, &st, NULL) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            json *row = j_obj();
            j_set(row, "user_id", j_str(col(st, 0)));
            j_set(row, "device_id", j_str(col(st, 1)));
            j_push(gone, row);
        }
        sqlite3_finalize(st);
    }
    for (size_t i = 0; i < j_len(gone); i++) {
        json *row = j_at(gone, i);
        const char *u = j_gets(row, "user_id", "");
        const char *d = j_gets(row, "device_id", "");
        hs_lock();
        db_run("DELETE FROM e2e_device_keys WHERE user_id=?1 AND device_id=?2",
               "ss", u, d);
        db_run("DELETE FROM e2e_one_time_keys WHERE user_id=?1 AND device_id=?2",
               "ss", u, d);
        db_run("DELETE FROM e2e_fallback_keys WHERE user_id=?1 AND device_id=?2",
               "ss", u, d);
        db_run("DELETE FROM e2e_signatures WHERE target_user=?1 AND target_key=?2",
               "ss", u, d);
        hs_unlock();
        keys_device_list_changed(u, d, true);
    }
    j_free(gone);
    return floor_sid;
}

/* ============================================================ key access = */
/* Is `key_id` ("ed25519:AAA") the user-signing key of `user`?  Signatures made
 * with one are never shown to anybody else, so that the social graph does not
 * leak. */
static bool is_user_signing_key(const char *user, const char *key_id) {
    json *usk = parse_owned(db_str("SELECT key_json FROM e2e_cross_signing_keys "
                                   "WHERE user_id=?1 AND usage='user_signing'",
                                   "s", user));
    if (!usk) {
        usk = parse_owned(db_str("SELECT key_json FROM e2e_remote_cross_signing "
                                 "WHERE user_id=?1 AND usage='user_signing'",
                                 "s", user));
    }
    bool hit = false;
    json *keys = usk ? j_geto(usk, "keys") : NULL;
    for (size_t i = 0; keys && i < j_len(keys); i++)
        if (str_eq(j_key(keys, i), key_id)) hit = true;
    j_free(usk);
    return hit;
}

/* Merges the signatures stored for (target_user, target_key) into `obj`,
 * dropping the ones `requester` is not allowed to see. */
static void merge_signatures(json *obj, const char *target_user,
                             const char *target_key, const char *requester) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(), "SELECT signer_user, signer_key, signature "
                                 "FROM e2e_signatures WHERE target_user=?1 AND "
                                 "target_key=?2", -1, &st, NULL) != SQLITE_OK)
        return;
    sqlite3_bind_text(st, 1, target_user, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, target_key, -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *su = col(st, 0), *sk = col(st, 1), *sig = col(st, 2);
        if (!str_eq(su, requester ? requester : "") &&
            is_user_signing_key(su, sk))
            continue;
        json *sigs = obj_at(obj, "signatures");
        j_set(obj_at(sigs, su), sk, j_str(sig));
    }
    sqlite3_finalize(st);
}

/* The DeviceKeys object for one local device, with unsigned.device_display_name
 * and every signature `requester` may see.  NULL when the device published no
 * keys. */
static json *device_keys_for(const char *user_id, const char *device_id,
                             const char *requester) {
    json *k = device_keys_raw(user_id, device_id);
    if (!k) return NULL;
    char *dn = db_str("SELECT display_name FROM devices WHERE user_id=?1 AND "
                      "device_id=?2", "ss", user_id, device_id);
    if (dn) {
        json *un = obj_at(k, "unsigned");
        j_set(un, "device_display_name", j_str(dn));
        free(dn);
    }
    merge_signatures(k, user_id, device_id, requester);
    return k;
}

/* One of "master", "self_signing", "user_signing" for a local user, with the
 * visible signatures merged in.  NULL when it was never uploaded. */
static json *cross_signing_key_for(const char *user_id, const char *usage,
                                   const char *requester) {
    json *k = parse_owned(db_str("SELECT key_json FROM e2e_cross_signing_keys "
                                 "WHERE user_id=?1 AND usage=?2", "ss",
                                 user_id, usage));
    if (!k) {
        k = parse_owned(db_str("SELECT key_json FROM e2e_remote_cross_signing "
                               "WHERE user_id=?1 AND usage=?2", "ss",
                               user_id, usage));
        if (k) return k;            /* remote keys arrive with their signatures */
    }
    if (!k) return NULL;
    json *keys = j_geto(k, "keys");
    for (size_t i = 0; keys && i < j_len(keys); i++) {
        const char *pub = after_colon(j_key(keys, i));
        if (pub) merge_signatures(k, user_id, pub, requester);
    }
    return k;
}

/* device_id -> DeviceKeys for every device of a local user that the request
 * asked about.  `wanted` may be an empty array, meaning all of them. */
static json *local_user_device_keys(const char *user_id, const json *wanted,
                                    const char *requester) {
    json *out = j_obj();
    if (wanted && j_len(wanted) > 0) {
        for (size_t i = 0; i < j_len(wanted); i++) {
            json *d = j_at(wanted, i);
            if (!d || d->type != J_STRING) continue;
            json *k = device_keys_for(user_id, d->s, requester);
            if (k) j_set(out, d->s, k);
        }
        return out;
    }
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(), "SELECT device_id FROM e2e_device_keys WHERE "
                                 "user_id=?1 ORDER BY device_id", -1, &st,
                           NULL) != SQLITE_OK) return out;
    sqlite3_bind_text(st, 1, user_id, -1, SQLITE_TRANSIENT);
    json *ids = j_arr();
    while (sqlite3_step(st) == SQLITE_ROW) j_push(ids, j_str(col(st, 0)));
    sqlite3_finalize(st);
    for (size_t i = 0; i < j_len(ids); i++) {
        json *k = device_keys_for(user_id, j_at(ids, i)->s, requester);
        if (k) j_set(out, j_at(ids, i)->s, k);
    }
    j_free(ids);
    return out;
}

json *keys_user_device_keys(const char *user_id) {
    return local_user_device_keys(user_id, NULL, "");
}

/* Counts of unclaimed one-time keys, by algorithm, for one device. */
static json *otk_counts(const char *user_id, const char *device_id) {
    json *out = j_obj();
    /* Every algorithm this server takes one-time keys in is reported, whether
     * or not any are held: a client reads this to decide when to upload more,
     * and an absent algorithm and a zero are not the same answer. */
    j_set(out, "signed_curve25519", j_int(0));
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(), "SELECT algorithm, COUNT(*) FROM "
                                 "e2e_one_time_keys WHERE user_id=?1 AND "
                                 "device_id=?2 GROUP BY algorithm", -1, &st,
                           NULL) != SQLITE_OK) return out;
    sqlite3_bind_text(st, 1, user_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, device_id ? device_id : "", -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW)
        j_set(out, col(st, 0), j_int(sqlite3_column_int64(st, 1)));
    sqlite3_finalize(st);
    return out;
}

json *keys_sync_otk_counts(const char *user_id, const char *device_id) {
    return otk_counts(user_id, device_id);
}

json *keys_sync_unused_fallback_key_types(const char *user_id,
                                          const char *device_id) {
    json *out = j_arr();
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(), "SELECT algorithm FROM e2e_fallback_keys WHERE "
                                 "user_id=?1 AND device_id=?2 AND used=0",
                           -1, &st, NULL) != SQLITE_OK) return out;
    sqlite3_bind_text(st, 1, user_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, device_id ? device_id : "", -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) j_push(out, j_str(col(st, 0)));
    sqlite3_finalize(st);
    return out;
}

/* ========================================== POST /keys/upload[/{deviceId}] */
static void h_keys_upload(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *body = require_body(r, w);
    if (!body) { principal_free(&p); return; }
    reconcile_devices();

    const char *device_id = p.device_id ? p.device_id : "";
    if (r->nparts > 0 && r->part[0] && *r->part[0] &&
        !str_eq(r->part[0], device_id)) {
        resp_error(w, 403, "M_FORBIDDEN",
                   "Cannot upload keys for another device");
        principal_free(&p);
        return;
    }
    if (!*device_id) {
        resp_error(w, 400, "M_MISSING_PARAM",
                   "This access token is not bound to a device");
        principal_free(&p);
        return;
    }

    json *dk = j_geto(body, "device_keys");
    bool changed = false;
    if (dk) {
        const char *u = j_gets(dk, "user_id", NULL);
        const char *d = j_gets(dk, "device_id", NULL);
        if (!u || !str_eq(u, p.user_id) || !d || !str_eq(d, device_id)) {
            resp_error(w, 400, "M_INVALID_PARAM",
                       "device_keys must carry this user's id and this device's id");
            principal_free(&p);
            return;
        }
        /* A device that uploads different identity keys replaces what it had:
         * the release puts no immutability on them, and the newest upload is
         * what `/keys/query` serves from then on. */
        char *text = dump_str(dk);
        char *stored = db_str("SELECT keys_json FROM e2e_device_keys WHERE "
                              "user_id=?1 AND device_id=?2", "ss",
                              p.user_id, device_id);
        /* Re-uploading exactly what is already there changes nothing, and
         * must not make the user's device list look as though it moved. */
        if (!stored || !str_eq(stored, text)) {
            hs_lock();
            int64_t sid = stream_next();
            db_run("INSERT INTO e2e_device_keys(user_id,device_id,keys_json,"
                   "stream_id,created_ts) VALUES(?1,?2,?3,?4,?5) "
                   "ON CONFLICT(user_id,device_id) DO UPDATE SET keys_json=?3,"
                   "stream_id=?4", "sssii", p.user_id, device_id, text, sid,
                   now_ms());
            hs_unlock();
            changed = true;
        }
        free(stored);
        free(text);
    }

    json *otk = j_geto(body, "one_time_keys");
    for (size_t i = 0; otk && i < j_len(otk); i++) {
        const char *name = j_key(otk, i);
        const char *c = strchr(name ? name : "", ':');
        if (!c || c == name || !c[1]) {
            resp_error(w, 400, "M_INVALID_PARAM",
                       "One-time key names are <algorithm>:<key_id>");
            principal_free(&p);
            return;
        }
        char *algo = dupn_(name, (size_t)(c - name));
        const char *key_id = c + 1;
        char *text = dump_str(j_at(otk, i));
        char *existing = db_str("SELECT key_json FROM e2e_one_time_keys WHERE "
                                "user_id=?1 AND device_id=?2 AND algorithm=?3 "
                                "AND key_id=?4", "ssss", p.user_id, device_id,
                                algo, key_id);
        if (existing) {
            bool same = str_eq(existing, text);
            free(existing);
            if (!same) {
                char *msg = str_fmt("One-time key %s already exists", name);
                resp_error(w, 400, "M_INVALID_PARAM", msg);
                free(msg); free(algo); free(text);
                principal_free(&p);
                return;
            }
            free(algo); free(text);
            continue;
        }
        hs_lock();
        db_exec("UPDATE seq SET value=value+1 WHERE name='e2e_otk'");
        int64_t seq = 0;
        db_int("SELECT value FROM seq WHERE name='e2e_otk'", &seq, "");
        db_run("INSERT INTO e2e_one_time_keys(user_id,device_id,algorithm,"
               "key_id,key_json,seq,created_ts) VALUES(?1,?2,?3,?4,?5,?6,?7)",
               "sssssii", p.user_id, device_id, algo, key_id, text, seq,
               now_ms());
        hs_unlock();
        free(algo);
        free(text);
    }

    /* Fallback keys (MSC2732): one per algorithm, replaced on every upload. */
    json *fb = j_geto(body, "fallback_keys");
    if (!fb) fb = j_geto(body, "org.matrix.msc2732.fallback_keys");
    for (size_t i = 0; fb && i < j_len(fb); i++) {
        const char *name = j_key(fb, i);
        const char *c = strchr(name ? name : "", ':');
        if (!c || c == name || !c[1]) continue;
        char *algo = dupn_(name, (size_t)(c - name));
        char *text = dump_str(j_at(fb, i));
        hs_lock();
        db_run("INSERT INTO e2e_fallback_keys(user_id,device_id,algorithm,"
               "key_id,key_json,used,created_ts) VALUES(?1,?2,?3,?4,?5,0,?6) "
               "ON CONFLICT(user_id,device_id,algorithm) DO UPDATE SET "
               "key_id=?4, key_json=?5, used=0, created_ts=?6",
               "sssssi", p.user_id, device_id, algo, c + 1, text, now_ms());
        hs_unlock();
        free(algo);
        free(text);
    }

    stream_notify();
    if (changed) keys_device_list_changed(p.user_id, device_id, false);

    json *out = j_obj();
    j_set(out, "one_time_key_counts", otk_counts(p.user_id, device_id));
    resp_json(w, 200, out);
    principal_free(&p);
}

/* =============================================== federation, outbound ==== */
/* POST `path` to `dest` and return the parsed body, or NULL.  *status gets
 * the HTTP status (0 when the request could not be made at all). */
static json *fed_post(const char *dest, const char *path, const json *body,
                      int *status) {
    if (status) *status = 0;
    if (CFG.disable_federation) return NULL;
    http_client_resp *resp = fed_request(dest, "POST", path, body);
    if (!resp) return NULL;
    if (status) *status = resp->status;
    json *out = (resp->body && resp->body_len)
                    ? j_parse(resp->body, resp->body_len, NULL) : NULL;
    http_client_resp_free(resp);
    if (out && out->type != J_OBJECT) { j_free(out); return NULL; }
    return out;
}

static void record_failure(json *failures, const char *server,
                           const char *message) {
    if (j_get(failures, server)) return;
    json *f = j_obj();
    j_set(f, "status", j_int(503));
    j_set(f, "message", j_str(message));
    j_set(failures, server, f);
}

/* The device list cached from m.device_list_update EDUs, used when the remote
 * server cannot be reached. */
static json *cached_remote_device_keys(const char *user_id, const json *wanted) {
    json *out = j_obj();
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(), "SELECT device_id, display_name, keys FROM "
                                 "fed_devices WHERE user_id=?1 AND deleted=0",
                           -1, &st, NULL) != SQLITE_OK) return out;
    sqlite3_bind_text(st, 1, user_id, -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *did = col(st, 0);
        if (wanted && j_len(wanted) > 0) {
            bool want = false;
            for (size_t i = 0; i < j_len(wanted); i++) {
                json *v = j_at(wanted, i);
                if (v && v->type == J_STRING && str_eq(v->s, did)) want = true;
            }
            if (!want) continue;
        }
        json *k = parse_text((const char *)sqlite3_column_text(st, 2));
        if (!k) continue;
        if (sqlite3_column_type(st, 1) != SQLITE_NULL)
            set_nested(k, "unsigned", "device_display_name", j_str(col(st, 1)));
        j_set(out, did, k);
    }
    sqlite3_finalize(st);
    return out;
}

static void remember_remote_cross_signing(const json *resp) {
    static const char *W[] = {"master_keys", "master",
                              "self_signing_keys", "self_signing",
                              "user_signing_keys", "user_signing", NULL};
    for (int i = 0; W[i]; i += 2) {
        json *m = j_geto((json *)resp, W[i]);
        for (size_t k = 0; m && k < j_len(m); k++) {
            const char *uid = j_key(m, k);
            char *text = dump_str(j_at(m, k));
            hs_lock();
            db_run("INSERT INTO e2e_remote_cross_signing(user_id,usage,"
                   "key_json,ts) VALUES(?1,?2,?3,?4) ON CONFLICT(user_id,usage) "
                   "DO UPDATE SET key_json=?3, ts=?4", "sssi", uid, W[i + 1],
                   text, now_ms());
            hs_unlock();
            free(text);
        }
    }
}

/* Groups the remote users of a /keys/query by server and asks each one. */
static void query_remote_users(const json *device_keys, json *out_device_keys,
                               json *master, json *self_signing,
                               json *failures) {
    json *by_server = j_obj();
    for (size_t i = 0; i < j_len(device_keys); i++) {
        const char *uid = j_key(device_keys, i);
        if (!uid || user_is_local(uid)) continue;
        const char *srv = user_id_server(uid);
        if (!srv || !*srv) continue;
        json *bucket = obj_at(by_server, srv);
        json *devs = j_at(device_keys, i);
        j_set(bucket, uid, devs && devs->type == J_ARRAY ? j_clone(devs) : j_arr());
    }
    for (size_t i = 0; i < j_len(by_server); i++) {
        const char *srv = j_key(by_server, i);
        json *req = j_obj();
        j_set(req, "device_keys", j_clone(j_at(by_server, i)));
        int status = 0;
        json *resp = fed_post(srv, FED1 "/user/keys/query", req, &status);
        j_free(req);
        if (!resp || status != 200) {
            j_free(resp);
            /* Fall back on what the m.device_list_update EDUs left behind. */
            json *bucket = j_at(by_server, i);
            bool any = false;
            for (size_t u = 0; u < j_len(bucket); u++) {
                json *cached = cached_remote_device_keys(j_key(bucket, u),
                                                         j_at(bucket, u));
                if (j_len(cached)) {
                    j_set(out_device_keys, j_key(bucket, u), cached);
                    any = true;
                } else {
                    j_free(cached);
                }
                json *mk = parse_owned(db_str("SELECT key_json FROM "
                        "e2e_remote_cross_signing WHERE user_id=?1 AND "
                        "usage='master'", "s", j_key(bucket, u)));
                if (mk) { j_set(master, j_key(bucket, u), mk); any = true; }
                json *sk = parse_owned(db_str("SELECT key_json FROM "
                        "e2e_remote_cross_signing WHERE user_id=?1 AND "
                        "usage='self_signing'", "s", j_key(bucket, u)));
                if (sk) { j_set(self_signing, j_key(bucket, u), sk); any = true; }
            }
            if (!any) record_failure(failures, srv, "Failed to fetch keys");
            continue;
        }
        remember_remote_cross_signing(resp);
        json *dk = j_geto(resp, "device_keys");
        for (size_t u = 0; dk && u < j_len(dk); u++) {
            /* One user's devices, keyed by device id.  A device-list update
             * carried a display name for some of them; an answer that does not
             * mention one has not taken it away, so the live answer wins on
             * the keys and the remembered update fills in what it left out. */
            json *devices = j_clone(j_at(dk, u));
            json *cached = cached_remote_device_keys(j_key(dk, u), NULL);
            for (size_t d = 0; devices && d < j_len(devices); d++) {
                json *one = j_at(devices, d);
                if (!one || one->type != J_OBJECT) continue;
                if (j_gets(j_geto(one, "unsigned"), "device_display_name", NULL))
                    continue;
                const char *name = j_gets(
                    j_geto(j_geto(cached, j_key(devices, d)), "unsigned"),
                    "device_display_name", NULL);
                if (name)
                    set_nested(one, "unsigned", "device_display_name",
                               j_str(name));
            }
            j_free(cached);
            j_set(out_device_keys, j_key(dk, u), devices);
        }
        json *mk = j_geto(resp, "master_keys");
        for (size_t u = 0; mk && u < j_len(mk); u++)
            j_set(master, j_key(mk, u), j_clone(j_at(mk, u)));
        json *sk = j_geto(resp, "self_signing_keys");
        for (size_t u = 0; sk && u < j_len(sk); u++)
            j_set(self_signing, j_key(sk, u), j_clone(j_at(sk, u)));
        /* Likewise for the cross-signing keys: an `m.signing_key_update` is a
         * statement about this user that a device-key query does not revisit,
         * so a query that says nothing about them leaves them standing. */
        json *bucket = j_at(by_server, i);
        for (size_t u = 0; u < j_len(bucket); u++) {
            const char *uid = j_key(bucket, u);
            if (!j_get(master, uid)) {
                json *cm = parse_owned(db_str("SELECT key_json FROM "
                        "e2e_remote_cross_signing WHERE user_id=?1 AND "
                        "usage='master'", "s", uid));
                if (cm) j_set(master, uid, cm);
            }
            if (!j_get(self_signing, uid)) {
                json *cs = parse_owned(db_str("SELECT key_json FROM "
                        "e2e_remote_cross_signing WHERE user_id=?1 AND "
                        "usage='self_signing'", "s", uid));
                if (cs) j_set(self_signing, uid, cs);
            }
        }
        j_free(resp);
    }
    j_free(by_server);
}

/* ====================================================== POST /keys/query = */
static void h_keys_query(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *body = require_body(r, w);
    if (!body) { principal_free(&p); return; }
    reconcile_devices();

    json *want = j_geto(body, "device_keys");
    if (!want) {
        resp_error(w, 400, "M_MISSING_PARAM", "device_keys is required");
        principal_free(&p);
        return;
    }

    json *out_keys = j_obj();
    json *master = j_obj(), *self_signing = j_obj(), *user_signing = j_obj();
    json *failures = j_obj();

    for (size_t i = 0; i < j_len(want); i++) {
        const char *uid = j_key(want, i);
        if (!uid || !user_is_local(uid)) continue;
        json *devs = j_at(want, i);
        json *k = local_user_device_keys(uid, devs && devs->type == J_ARRAY
                                                  ? devs : NULL, p.user_id);
        if (j_len(k)) j_set(out_keys, uid, k); else j_free(k);
        json *mk = cross_signing_key_for(uid, "master", p.user_id);
        if (mk) j_set(master, uid, mk);
        json *sk = cross_signing_key_for(uid, "self_signing", p.user_id);
        if (sk) j_set(self_signing, uid, sk);
        /* The user-signing key is only ever visible to its owner. */
        if (str_eq(uid, p.user_id)) {
            json *uk = cross_signing_key_for(uid, "user_signing", p.user_id);
            if (uk) j_set(user_signing, uid, uk);
        }
    }
    query_remote_users(want, out_keys, master, self_signing, failures);
    /* A signature this user made about a remote user's master key lives here,
     * not on that user's server, so it has to be merged into the answer. */
    for (size_t i = 0; i < j_len(master); i++) {
        const char *uid = j_key(master, i);
        if (!uid || user_is_local(uid)) continue;
        json *k = j_at(master, i);
        json *keys = j_geto(k, "keys");
        for (size_t n = 0; keys && n < j_len(keys); n++) {
            const char *pub = after_colon(j_key(keys, n));
            if (pub) merge_signatures(k, uid, pub, p.user_id);
        }
    }

    json *out = j_obj();
    j_set(out, "device_keys", out_keys);
    j_set(out, "failures", failures);
    if (j_len(master)) j_set(out, "master_keys", master); else j_free(master);
    if (j_len(self_signing)) j_set(out, "self_signing_keys", self_signing);
    else j_free(self_signing);
    if (j_len(user_signing)) j_set(out, "user_signing_keys", user_signing);
    else j_free(user_signing);
    resp_json(w, 200, out);
    principal_free(&p);
}

/* ====================================================== POST /keys/claim = */
/* Takes the oldest unclaimed one-time key of `algorithm` for a local device
 * and removes it, falling back to the device's fallback key.  Returns the
 * "<algorithm>:<key_id>" -> key pair, or NULL. */
static json *claim_one(const char *user_id, const char *device_id,
                       const char *algorithm) {
    sqlite3_stmt *st = NULL;
    char *key_id = NULL, *key_json = NULL;
    /* Picking a key and removing it has to be one step, or two callers could
     * be handed the same one-time key. */
    hs_lock();
    if (sqlite3_prepare_v2(db(), "SELECT key_id, key_json FROM e2e_one_time_keys "
                                 "WHERE user_id=?1 AND device_id=?2 AND "
                                 "algorithm=?3 ORDER BY seq ASC LIMIT 1",
                           -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, user_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, device_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 3, algorithm, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) == SQLITE_ROW) {
            key_id = xstrdup_(col(st, 0));
            key_json = xstrdup_(col(st, 1));
        }
        sqlite3_finalize(st);
    }
    if (key_id) {
        /* "A homeserver should discard the public part of a one time key once
         * it has given that key to another user." */
        db_run("DELETE FROM e2e_one_time_keys WHERE user_id=?1 AND device_id=?2 "
               "AND algorithm=?3 AND key_id=?4", "ssss", user_id, device_id,
               algorithm, key_id);
    } else {
        if (sqlite3_prepare_v2(db(), "SELECT key_id, key_json FROM "
                                     "e2e_fallback_keys WHERE user_id=?1 AND "
                                     "device_id=?2 AND algorithm=?3",
                               -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, user_id, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 2, device_id, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 3, algorithm, -1, SQLITE_TRANSIENT);
            if (sqlite3_step(st) == SQLITE_ROW) {
                key_id = xstrdup_(col(st, 0));
                key_json = xstrdup_(col(st, 1));
            }
            sqlite3_finalize(st);
        }
        if (!key_id) { hs_unlock(); return NULL; }
        db_run("UPDATE e2e_fallback_keys SET used=1 WHERE user_id=?1 AND "
               "device_id=?2 AND algorithm=?3", "sss", user_id, device_id,
               algorithm);
    }
    hs_unlock();
    json *val = parse_text(key_json);
    free(key_json);
    json *out = j_obj();
    char *name = str_fmt("%s:%s", algorithm, key_id);
    j_set(out, name, val ? val : j_str(""));
    free(name);
    free(key_id);
    return out;
}

/* Claims for every local user named in `want`, merging into `out`. */
static void claim_local(const json *want, json *out) {
    for (size_t i = 0; i < j_len(want); i++) {
        const char *uid = j_key(want, i);
        json *devs = j_at(want, i);
        if (!uid || !user_is_local(uid) || !devs || devs->type != J_OBJECT)
            continue;
        for (size_t d = 0; d < j_len(devs); d++) {
            const char *did = j_key(devs, d);
            json *alg = j_at(devs, d);
            if (!did || !alg || alg->type != J_STRING) continue;
            json *one = claim_one(uid, did, alg->s);
            if (!one) continue;
            json *udev = obj_at(out, uid);
            json *cur = j_geto(udev, did);
            if (!cur) { j_set(udev, did, one); continue; }
            for (size_t k = 0; k < j_len(one); k++)
                j_set(cur, j_key(one, k), j_clone(j_at(one, k)));
            j_free(one);
        }
    }
}

static void claim_remote(const json *want, json *out, json *failures) {
    json *by_server = j_obj();
    for (size_t i = 0; i < j_len(want); i++) {
        const char *uid = j_key(want, i);
        if (!uid || user_is_local(uid)) continue;
        const char *srv = user_id_server(uid);
        json *devs = j_at(want, i);
        if (!srv || !*srv || !devs || devs->type != J_OBJECT) continue;
        j_set(obj_at(by_server, srv), uid, j_clone(devs));
    }
    for (size_t i = 0; i < j_len(by_server); i++) {
        const char *srv = j_key(by_server, i);
        json *req = j_obj();
        j_set(req, "one_time_keys", j_clone(j_at(by_server, i)));
        int status = 0;
        json *resp = fed_post(srv, FED1 "/user/keys/claim", req, &status);
        j_free(req);
        if (!resp || status != 200) {
            j_free(resp);
            record_failure(failures, srv, "Failed to claim one-time keys");
            continue;
        }
        json *otk = j_geto(resp, "one_time_keys");
        for (size_t u = 0; otk && u < j_len(otk); u++)
            j_set(out, j_key(otk, u), j_clone(j_at(otk, u)));
        j_free(resp);
    }
    j_free(by_server);
}

static void h_keys_claim(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *body = require_body(r, w);
    if (!body) { principal_free(&p); return; }
    reconcile_devices();

    json *want = j_geto(body, "one_time_keys");
    if (!want) {
        resp_error(w, 400, "M_MISSING_PARAM", "one_time_keys is required");
        principal_free(&p);
        return;
    }
    json *out_keys = j_obj(), *failures = j_obj();
    claim_local(want, out_keys);
    claim_remote(want, out_keys, failures);
    stream_notify();

    json *out = j_obj();
    j_set(out, "one_time_keys", out_keys);
    j_set(out, "failures", failures);
    resp_json(w, 200, out);
    principal_free(&p);
}

/* ===================================================== GET /keys/changes = */
static bool tok_parse(const char *s, int64_t *out) {
    if (!s || s[0] != 's' || s[1] < '0' || s[1] > '9') return false;
    long long v = strtoll(s + 1, NULL, 10);
    if (v < 0) return false;
    *out = (int64_t)v;
    return true;
}

/* Everybody who shares a joined room with `user_id`, `user_id` included. */
static json *users_sharing_rooms(const char *user_id) {
    json *out = j_arr();
    arr_add_unique(out, user_id);
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT DISTINCT b.user_id FROM room_members a "
            "JOIN room_members b ON a.room_id=b.room_id "
            "WHERE a.user_id=?1 AND a.membership='join' AND b.membership='join'",
            -1, &st, NULL) != SQLITE_OK) return out;
    sqlite3_bind_text(st, 1, user_id, -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) arr_add_unique(out, col(st, 0));
    sqlite3_finalize(st);
    return out;
}

/* `changed`: everyone sharing a room with `user_id` whose device list or
 * cross-signing keys moved in (from, to].  `floor_sid` is the stream position
 * before this request reconciled the device table: a removal only noticed now
 * carries a stream id beyond it and still belongs to the caller's window. */
static json *changed_users(const char *user_id, int64_t from, int64_t to,
                           int64_t floor_sid) {
    json *peers = users_sharing_rooms(user_id);
    json *out = j_arr();
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT DISTINCT user_id FROM e2e_device_changes "
            "WHERE stream_id>?1 AND (stream_id<=?2 OR stream_id>?3)",
            -1, &st, NULL) != SQLITE_OK) { j_free(peers); return out; }
    sqlite3_bind_int64(st, 1, from);
    sqlite3_bind_int64(st, 2, to);
    sqlite3_bind_int64(st, 3, floor_sid);
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *u = col(st, 0);
        for (size_t i = 0; i < j_len(peers); i++)
            if (str_eq(j_at(peers, i)->s, u)) { arr_add_unique(out, u); break; }
    }
    sqlite3_finalize(st);
    j_free(peers);
    return out;
}

/* `left`: users who stopped sharing every room with the caller in the window. */
static json *left_users(const char *user_id, int64_t from, int64_t to) {
    json *out = j_arr();
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT DISTINCT b.user_id FROM room_members b "
            "WHERE b.membership IN ('leave','ban') AND b.stream_id>?2 AND "
            "      b.stream_id<=?3 AND b.user_id<>?1 AND "
            "EXISTS (SELECT 1 FROM room_members a WHERE a.room_id=b.room_id AND "
            "        a.user_id=?1 AND a.membership='join') AND "
            "NOT EXISTS (SELECT 1 FROM room_members c "
            "            JOIN room_members d ON c.room_id=d.room_id "
            "            WHERE c.user_id=?1 AND c.membership='join' AND "
            "                  d.user_id=b.user_id AND d.membership='join')",
            -1, &st, NULL) != SQLITE_OK) return out;
    sqlite3_bind_text(st, 1, user_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, from);
    sqlite3_bind_int64(st, 3, to);
    while (sqlite3_step(st) == SQLITE_ROW) arr_add_unique(out, col(st, 0));
    sqlite3_finalize(st);
    return out;
}

/* A membership that makes someone a user whose keys the caller may need. */
static bool tracked_membership(const char *m) {
    return str_eq(m, "join") || str_eq(m, "invite") || str_eq(m, "knock");
}

static void arr_discard(json *arr, const char *s) {
    for (size_t i = 0; i < j_len(arr); i++) {
        json *v = j_at(arr, i);
        if (v && v->type == J_STRING && str_eq(v->s, s)) {
            j_remove_at(arr, i);
            return;
        }
    }
}

static const char *membership_of(const json *ev) {
    json *c = ev ? j_geto((json *)ev, "content") : NULL;
    return c ? j_gets(c, "membership", "leave") : "leave";
}

/* Everyone joined to `room_id` right now. */
static void add_room_members(json *out, const char *room_id) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(), "SELECT user_id FROM room_members WHERE "
                           "room_id=?1 AND membership='join'", -1, &st,
                           NULL) != SQLITE_OK) return;
    sqlite3_bind_text(st, 1, room_id, -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) arr_add_unique(out, col(st, 0));
    sqlite3_finalize(st);
}

/* Membership moves device lists as surely as a key upload does: a user who
 * joins a room the caller is in is one whose keys the caller now needs, and a
 * user who stops being joined, invited or knocking is one they stop tracking.
 * Walks the member events in (from, to] in the caller's rooms and folds them
 * into `changed` and `left` -- a user who left and came back inside the same
 * window ends up in `changed` only, which is why the two lists discard from
 * each other rather than simply accumulating. */
static void membership_moves(const char *user_id, int64_t from, int64_t to,
                             json *changed, json *left) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT e.stream_id, e.state_key, e.room_id, e.json FROM events e "
            "WHERE e.type='m.room.member' AND e.stream_id>?2 AND e.stream_id<=?3 "
            "  AND EXISTS (SELECT 1 FROM room_members a WHERE a.room_id=e.room_id "
            "              AND a.user_id=?1 AND a.membership='join') "
            "ORDER BY e.stream_id ASC", -1, &st, NULL) != SQLITE_OK) return;
    sqlite3_bind_text(st, 1, user_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, from);
    sqlite3_bind_int64(st, 3, to);
    while (sqlite3_step(st) == SQLITE_ROW) {
        int64_t sid = sqlite3_column_int64(st, 0);
        const char *who = col(st, 1), *room = col(st, 2);
        if (!who || !*who) continue;
        char *who_own = xstrdup_(who), *room_own = xstrdup_(room ? room : "");
        json *ev = parse_text((const char *)sqlite3_column_text(st, 3));
        const char *now_m = membership_of(ev);
        char *prev_js = db_str("SELECT json FROM events WHERE room_id=?1 AND "
                               "type='m.room.member' AND state_key=?2 AND "
                               "stream_id<?3 ORDER BY stream_id DESC LIMIT 1",
                               "ssi", room_own, who_own, sid);
        json *prev = prev_js ? parse_text(prev_js) : NULL;
        free(prev_js);
        const char *prev_m = prev ? membership_of(prev) : "leave";
        if (tracked_membership(now_m)) {
            if (!tracked_membership(prev_m)) {
                arr_add_unique(changed, who_own);
                arr_discard(left, who_own);
                /* The caller joining a room makes every one of its members
                 * someone whose keys they have never seen. */
                if (str_eq(who_own, user_id) && str_eq(now_m, "join"))
                    add_room_members(changed, room_own);
            }
        } else if (tracked_membership(prev_m)) {
            arr_add_unique(left, who_own);
            arr_discard(changed, who_own);
            if (str_eq(who_own, user_id) && str_eq(prev_m, "join"))
                add_room_members(left, room_own);
        }
        j_free(prev);
        j_free(ev);
        free(who_own);
        free(room_own);
    }
    sqlite3_finalize(st);
}

/* A user the caller still shares a room with is never "left". */
static void prune_still_shared(const char *user_id, json *left) {
    json *peers = users_sharing_rooms(user_id);
    for (size_t i = 0; i < j_len(peers); i++) {
        json *v = j_at(peers, i);
        if (v && v->type == J_STRING) arr_discard(left, v->s);
    }
    j_free(peers);
}

/* Both /sync's device_lists and /keys/changes answer the same question over a
 * window, so they are built the same way. */
static void device_list_window(const char *user_id, int64_t from, int64_t to,
                               int64_t floor_sid, json **changed, json **left) {
    *changed = changed_users(user_id, from, to, floor_sid);
    *left = left_users(user_id, from, to);
    membership_moves(user_id, from, to, *changed, *left);
    prune_still_shared(user_id, *left);
}

json *keys_sync_device_lists(const char *user_id, int64_t since, int64_t now) {
    json *out = j_obj();
    json *changed = NULL, *left = NULL;
    device_list_window(user_id, since, now, now, &changed, &left);
    j_set(out, "changed", changed);
    j_set(out, "left", left);
    return out;
}

static void h_keys_changes(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    int64_t from = 0, to = 0;
    const char *fq = http_query(r, "from"), *tq = http_query(r, "to");
    if (!fq || !tok_parse(fq, &from)) {
        resp_error(w, 400, "M_INVALID_PARAM", "Invalid from token");
        principal_free(&p);
        return;
    }
    if (!tq || !tok_parse(tq, &to)) to = stream_current();
    int64_t floor_sid = reconcile_devices();
    if (to < from) to = from;

    json *out = j_obj();
    json *changed = NULL, *left = NULL;
    device_list_window(p.user_id, from, to, floor_sid, &changed, &left);
    j_set(out, "changed", changed);
    j_set(out, "left", left);
    resp_json(w, 200, out);
    principal_free(&p);
}

/* ============================================== cross-signing key upload = */
/* A CrossSigningKey must name its owner, its usage and exactly one key. */
static bool cross_signing_valid(const json *k, const char *user_id,
                                const char *usage, const char **why) {
    if (!k || k->type != J_OBJECT) { *why = "not an object"; return false; }
    const char *u = j_gets(k, "user_id", NULL);
    if (!u || !str_eq(u, user_id)) { *why = "wrong user_id"; return false; }
    json *use = j_geta((json *)k, "usage");
    bool has = false;
    for (size_t i = 0; use && i < j_len(use); i++) {
        json *v = j_at(use, i);
        if (v && v->type == J_STRING && str_eq(v->s, usage)) has = true;
    }
    if (!has) { *why = "usage does not name this key"; return false; }
    json *keys = j_geto((json *)k, "keys");
    if (!keys || j_len(keys) != 1) { *why = "keys must hold exactly one key"; return false; }
    if (!after_colon(j_key(keys, 0)) || !j_is_str(j_at(keys, 0))) {
        *why = "key name must be <algorithm>:<public key>";
        return false;
    }
    return true;
}

/* The single "<algorithm>:<public key>" name of a cross-signing key. */
static const char *cs_key_id(const json *k) {
    json *keys = k ? j_geto((json *)k, "keys") : NULL;
    return (keys && j_len(keys)) ? j_key(keys, 0) : NULL;
}

/* Verifies that `obj` carries a signature by `user_id` with the key whose
 * public half is named by `signing_key_id` ("ed25519:AAA"). */
static bool verify_with_key_id(const json *obj, const char *user_id,
                               const char *signing_key_id) {
    unsigned char pub[32];
    const char *b64 = after_colon(signing_key_id);
    if (!pub_from_b64(b64, pub)) return false;
    return json_verify(obj, user_id, signing_key_id, pub);
}

/* Would this public key collide with one of the user's device ids?  "Servers
 * therefore must ensure that device IDs will not collide with cross-signing
 * public keys." */
static bool collides_with_device(const char *user_id, const json *k) {
    const char *name = cs_key_id(k);
    const char *pub = after_colon(name);
    if (!pub) return false;
    char *hit = db_str("SELECT device_id FROM devices WHERE user_id=?1 AND "
                       "device_id=?2", "ss", user_id, pub);
    bool bad = hit != NULL;
    free(hit);
    return bad;
}

/* Would this upload replace a cross-signing key this user already has with a
 * different one?  A first upload has nothing to replace, and re-uploading what
 * is already stored changes nothing -- but a *new* key of any of the three
 * kinds is a replacement, and the user is asked to authenticate again. */
static bool replaces_stored_key(const char *user_id, const char *usage,
                                const json *supplied) {
    if (!supplied) return false;
    char *stored = db_str("SELECT key_json FROM e2e_cross_signing_keys WHERE "
                          "user_id=?1 AND usage=?2", "ss", user_id, usage);
    if (!stored) return false;            /* nothing of this kind yet */
    json *have = j_parse(stored, strlen(stored), NULL);
    free(stored);
    bool same = false;
    const json *a = have ? j_geto((json *)have, "keys") : NULL;
    const json *b = j_geto((json *)supplied, "keys");
    if (a && b && j_len(a) == j_len(b)) {
        same = true;
        for (size_t i = 0; i < j_len(a); i++) {
            const json *bv = j_get((json *)b, j_key((json *)a, i));
            const json *av = j_at((json *)a, i);
            if (!bv || !j_is_str(av) || !j_is_str(bv) || !str_eq(av->s, bv->s)) {
                same = false;
                break;
            }
        }
    }
    j_free(have);
    return !same;
}

static bool cross_signing_is_being_replaced(const char *user_id, const json *mk,
                                            const json *sk, const json *uk) {
    return replaces_stored_key(user_id, "master", mk) ||
           replaces_stored_key(user_id, "self_signing", sk) ||
           replaces_stored_key(user_id, "user_signing", uk);
}

static void h_device_signing_upload(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *body = require_body(r, w);
    if (!body) { principal_free(&p); return; }
    reconcile_devices();

    json *mk = j_geto(body, "master_key");
    json *sk = j_geto(body, "self_signing_key");
    json *uk = j_geto(body, "user_signing_key");

    /* "This endpoint requires ... authentication ... when the master key is
     * being replaced."  A first upload, and an upload that stores the master
     * key this user already has, are not a replacement: only a *different*
     * master key is, and only then is the user asked to authenticate again. */
    if (cross_signing_is_being_replaced(p.user_id, mk, sk, uk) &&
        !uia_check(r, w, "keys", p.user_id, body)) {
        principal_free(&p);
        return;
    }
    if (!mk && !sk && !uk) {
        resp_error(w, 400, "M_MISSING_PARAM", "No keys were supplied");
        principal_free(&p);
        return;
    }

    const char *why = NULL;
    if (mk && !cross_signing_valid(mk, p.user_id, "master", &why)) {
        char *m = str_fmt("Invalid master_key: %s", why);
        resp_error(w, 400, "M_INVALID_PARAM", m);
        free(m); principal_free(&p);
        return;
    }
    if (sk && !cross_signing_valid(sk, p.user_id, "self_signing", &why)) {
        char *m = str_fmt("Invalid self_signing_key: %s", why);
        resp_error(w, 400, "M_INVALID_PARAM", m);
        free(m); principal_free(&p);
        return;
    }
    if (uk && !cross_signing_valid(uk, p.user_id, "user_signing", &why)) {
        char *m = str_fmt("Invalid user_signing_key: %s", why);
        resp_error(w, 400, "M_INVALID_PARAM", m);
        free(m); principal_free(&p);
        return;
    }
    if ((mk && collides_with_device(p.user_id, mk)) ||
        (sk && collides_with_device(p.user_id, sk)) ||
        (uk && collides_with_device(p.user_id, uk))) {
        resp_error(w, 403, "M_FORBIDDEN", "Key ID in use");
        principal_free(&p);
        return;
    }

    /* "Must be signed by the accompanying master key, or by the user's most
     * recently uploaded master key if no master key is included." */
    json *stored_master = parse_owned(db_str("SELECT key_json FROM "
            "e2e_cross_signing_keys WHERE user_id=?1 AND usage='master'",
            "s", p.user_id));
    const json *master = mk ? mk : stored_master;
    if ((sk || uk) && !master) {
        j_free(stored_master);
        resp_error(w, 400, "M_MISSING_PARAM", "No master key is available");
        principal_free(&p);
        return;
    }
    /* Replacing an existing master key is allowed (the request is behind
     * user-interactive authentication), but a master key that is already
     * there and unchanged must not silently become a different one without
     * the new subordinate keys being signed by it. */
    const char *master_id = master ? cs_key_id(master) : NULL;
    if (sk && !verify_with_key_id(sk, p.user_id, master_id)) {
        j_free(stored_master);
        resp_error(w, 400, "M_INVALID_SIGNATURE",
                   "self_signing_key is not signed by the master key");
        principal_free(&p);
        return;
    }
    if (uk && !verify_with_key_id(uk, p.user_id, master_id)) {
        j_free(stored_master);
        resp_error(w, 400, "M_INVALID_SIGNATURE",
                   "user_signing_key is not signed by the master key");
        principal_free(&p);
        return;
    }
    j_free(stored_master);

    static const char *SLOTS[] = {"master_key", "master",
                                  "self_signing_key", "self_signing",
                                  "user_signing_key", "user_signing", NULL};
    hs_lock();
    int64_t sid = stream_next();
    for (int i = 0; SLOTS[i]; i += 2) {
        json *k = j_geto(body, SLOTS[i]);
        if (!k) continue;
        char *text = dump_str(k);
        db_run("INSERT INTO e2e_cross_signing_keys(user_id,usage,key_json,"
               "stream_id) VALUES(?1,?2,?3,?4) ON CONFLICT(user_id,usage) "
               "DO UPDATE SET key_json=?3, stream_id=?4", "sssi",
               p.user_id, SLOTS[i + 1], text, sid);
        free(text);
        /* Carry over the signatures that came with the key itself. */
        json *sigs = j_geto(k, "signatures");
        const char *target = after_colon(cs_key_id(k));
        for (size_t a = 0; sigs && target && a < j_len(sigs); a++) {
            json *inner = j_at(sigs, a);
            for (size_t b = 0; inner && b < j_len(inner); b++) {
                json *v = j_at(inner, b);
                if (!v || v->type != J_STRING) continue;
                db_run("INSERT OR REPLACE INTO e2e_signatures(target_user,"
                       "target_key,signer_user,signer_key,signature,stream_id) "
                       "VALUES(?1,?2,?3,?4,?5,?6)", "sssssi", p.user_id,
                       target, j_key(sigs, a), j_key(inner, b), v->s, sid);
            }
        }
    }
    hs_unlock();
    stream_notify();
    signing_keys_changed(p.user_id);
    resp_json(w, 200, j_obj());
    principal_free(&p);
}

/* ============================================ POST /keys/signatures/upload */
/* The ed25519 public key that `signer_key` ("ed25519:X") names, where X is
 * either one of `signer`'s device ids or a cross-signing public key. */
static bool signer_public_key(const char *signer, const char *signer_key,
                              unsigned char pub[32]) {
    const char *x = after_colon(signer_key);
    if (!x) return false;
    /* A device: take its published ed25519 identity key. */
    json *dk = device_keys_raw(signer, x);
    if (dk) {
        json *keys = j_geto(dk, "keys");
        const char *b64 = keys ? j_gets(keys, signer_key, NULL) : NULL;
        bool ok = b64 && pub_from_b64(b64, pub);
        j_free(dk);
        if (ok) return true;
    }
    /* Otherwise the key id carries the public key itself. */
    return pub_from_b64(x, pub);
}

/* The stored object that `target_key` names, and the identifier the signature
 * is filed under.  Returns NULL when nothing of that name is known. */
static json *signature_target(const char *target_user, const char *target_key) {
    json *dk = device_keys_raw(target_user, target_key);
    if (dk) return dk;
    static const char *USAGES[] = {"master", "self_signing", "user_signing", NULL};
    for (int i = 0; USAGES[i]; i++) {
        json *k = parse_owned(db_str("SELECT key_json FROM "
                "e2e_cross_signing_keys WHERE user_id=?1 AND usage=?2", "ss",
                target_user, USAGES[i]));
        if (!k) continue;
        const char *pub = after_colon(cs_key_id(k));
        if (pub && str_eq(pub, target_key)) return k;
        j_free(k);
    }
    /* A remote user's master key may be signed with our user-signing key. */
    for (int i = 0; USAGES[i]; i++) {
        json *k = parse_owned(db_str("SELECT key_json FROM "
                "e2e_remote_cross_signing WHERE user_id=?1 AND usage=?2", "ss",
                target_user, USAGES[i]));
        if (!k) continue;
        const char *pub = after_colon(cs_key_id(k));
        if (pub && str_eq(pub, target_key)) return k;
        j_free(k);
    }
    return NULL;
}

static void sig_failure(json *failures, const char *user, const char *key,
                        const char *errcode, const char *msg) {
    json *inner = obj_at(failures, user);
    json *e = j_obj();
    j_set(e, "errcode", j_str(errcode));
    j_set(e, "error", j_str(msg));
    j_set(inner, key, e);
}

static void h_signatures_upload(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *body = require_body(r, w);
    if (!body) { principal_free(&p); return; }
    reconcile_devices();

    json *failures = j_obj();
    bool wrote_any = false;
    for (size_t i = 0; i < j_len(body); i++) {
        const char *target_user = j_key(body, i);
        json *keys = j_at(body, i);
        if (!target_user || !keys || keys->type != J_OBJECT) continue;
        for (size_t k = 0; k < j_len(keys); k++) {
            const char *target_key = j_key(keys, k);
            json *given = j_at(keys, k);
            if (!target_key || !given || given->type != J_OBJECT) {
                sig_failure(failures, target_user, target_key ? target_key : "",
                            "M_INVALID_PARAM", "Not a signed JSON object");
                continue;
            }
            json *stored = signature_target(target_user, target_key);
            if (!stored) {
                sig_failure(failures, target_user, target_key, "M_NOT_FOUND",
                            "Unknown key");
                continue;
            }
            /* The signed object has to be the key we hold, or the signature
             * would cover something the owner never published. */
            char *a = key_material(stored), *b = key_material(given);
            bool same = a && b && str_eq(a, b);
            free(a); free(b);
            j_free(stored);
            if (!same) {
                sig_failure(failures, target_user, target_key,
                            "M_INVALID_SIGNATURE",
                            "Signed object does not match the stored key");
                continue;
            }
            json *sigs = j_geto(given, "signatures");
            if (!sigs || !j_len(sigs)) {
                sig_failure(failures, target_user, target_key,
                            "M_INVALID_SIGNATURE", "No signatures supplied");
                continue;
            }
            /* Only the signatures the caller made are theirs to publish; a
             * signature by anybody else in the same object is ignored.  Every
             * one of the caller's own has to verify, or none of them is
             * stored and the key is reported under `failures`. */
            json *good = j_obj();
            int own = 0, bad = 0;
            for (size_t a2 = 0; a2 < j_len(sigs); a2++) {
                const char *signer = j_key(sigs, a2);
                json *inner = j_at(sigs, a2);
                if (!signer || !inner || inner->type != J_OBJECT) continue;
                if (!str_eq(signer, p.user_id)) continue;
                for (size_t b2 = 0; b2 < j_len(inner); b2++) {
                    const char *signer_key = j_key(inner, b2);
                    json *sv = j_at(inner, b2);
                    if (!signer_key || !sv || sv->type != J_STRING) continue;
                    own++;
                    unsigned char pub[32];
                    if (!signer_public_key(signer, signer_key, pub) ||
                        !json_verify(given, signer, signer_key, pub)) {
                        bad++;
                        continue;
                    }
                    j_set(good, signer_key, j_str(sv->s));
                }
            }
            if (!own) {
                j_free(good);
                sig_failure(failures, target_user, target_key, "M_FORBIDDEN",
                            "Cannot upload signatures made by another user");
                continue;
            }
            if (bad) {
                j_free(good);
                sig_failure(failures, target_user, target_key,
                            "M_INVALID_SIGNATURE", "Invalid signature");
                continue;
            }
            for (size_t g = 0; g < j_len(good); g++) {
                hs_lock();
                int64_t sid = stream_next();
                db_run("INSERT OR REPLACE INTO e2e_signatures(target_user,"
                       "target_key,signer_user,signer_key,signature,"
                       "stream_id) VALUES(?1,?2,?3,?4,?5,?6)", "sssssi",
                       target_user, target_key, p.user_id, j_key(good, g),
                       j_at(good, g)->s, sid);
                hs_unlock();
                wrote_any = true;
            }
            j_free(good);
        }
    }
    if (wrote_any) stream_notify();

    json *out = j_obj();
    j_set(out, "failures", failures);
    resp_json(w, 200, out);
    principal_free(&p);
}

/* ======================================================= key backups ===== */
/* The latest version that has not been deleted, or 0. */
static int64_t backup_current_version(const char *user_id) {
    int64_t v = 0;
    db_int("SELECT COALESCE(MAX(version),0) FROM e2e_backup_versions WHERE "
           "user_id=?1 AND deleted=0", &v, "s", user_id);
    return v;
}

static bool backup_exists(const char *user_id, int64_t version) {
    int64_t n = 0;
    db_int("SELECT COUNT(*) FROM e2e_backup_versions WHERE user_id=?1 AND "
           "version=?2 AND deleted=0", &n, "si", user_id, version);
    return n > 0;
}

static int64_t backup_count(const char *user_id, int64_t version) {
    int64_t n = 0;
    db_int("SELECT COUNT(*) FROM e2e_backup_keys WHERE user_id=?1 AND "
           "version=?2", &n, "si", user_id, version);
    return n;
}

static int64_t backup_etag(const char *user_id, int64_t version) {
    int64_t n = 0;
    db_int("SELECT etag FROM e2e_backup_versions WHERE user_id=?1 AND "
           "version=?2", &n, "si", user_id, version);
    return n;
}

static void backup_bump_etag(const char *user_id, int64_t version) {
    hs_lock();
    db_run("UPDATE e2e_backup_versions SET etag=etag+1 WHERE user_id=?1 AND "
           "version=?2", "si", user_id, version);
    hs_unlock();
    stream_notify();
}

/* {"etag": "...", "count": N} */
static json *backup_update_response(const char *user_id, int64_t version) {
    json *out = j_obj();
    char *e = str_fmt("%" PRId64, backup_etag(user_id, version));
    j_set(out, "etag", j_str(e));
    free(e);
    j_set(out, "count", j_int(backup_count(user_id, version)));
    return out;
}

/* Parses the ?version= parameter and checks it against the stored versions.
 * `must_be_current` distinguishes storing (which may only touch the current
 * backup) from reading and deleting.  Returns false having written the error
 * response. */
static bool backup_resolve(http_req *r, http_resp *w, const char *user_id,
                           bool must_be_current, int64_t *out_version) {
    const char *vq = http_query(r, "version");
    if (!vq || !*vq) {
        resp_error(w, 400, "M_MISSING_PARAM", "version is required");
        return false;
    }
    char *end = NULL;
    long long v = strtoll(vq, &end, 10);
    int64_t version = (end && !*end && v > 0) ? (int64_t)v : 0;
    int64_t current = backup_current_version(user_id);
    /* A version that was never created, or has been deleted, is not found;
     * M_WRONG_ROOM_KEYS_VERSION is for a version that exists but is not the
     * one keys may be written to. */
    if (!version || !backup_exists(user_id, version)) {
        resp_error(w, 404, "M_NOT_FOUND", "Unknown backup version");
        return false;
    }
    if (must_be_current && version != current) {
        char *cv = str_fmt("%" PRId64, current);
        resp_error_extra(w, 403, "M_WRONG_ROOM_KEYS_VERSION",
                         "Wrong backup version.", "current_version", j_str(cv));
        free(cv);
        return false;
    }
    *out_version = version;
    return true;
}

static json *backup_version_info(const char *user_id, int64_t version) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(), "SELECT algorithm, auth_data, etag FROM "
                                 "e2e_backup_versions WHERE user_id=?1 AND "
                                 "version=?2 AND deleted=0", -1, &st,
                           NULL) != SQLITE_OK) return NULL;
    sqlite3_bind_text(st, 1, user_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, version);
    json *out = NULL;
    if (sqlite3_step(st) == SQLITE_ROW) {
        out = j_obj();
        j_set(out, "algorithm", j_str(col(st, 0)));
        json *ad = parse_text(col(st, 1));
        j_set(out, "auth_data", ad ? ad : j_obj());
        j_set(out, "count", j_int(backup_count(user_id, version)));
        char *e = str_fmt("%" PRId64, (int64_t)sqlite3_column_int64(st, 2));
        j_set(out, "etag", j_str(e));
        free(e);
        char *v = str_fmt("%" PRId64, version);
        j_set(out, "version", j_str(v));
        free(v);
    }
    sqlite3_finalize(st);
    return out;
}

static void h_backup_version_post(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *body = require_body(r, w);
    if (!body) { principal_free(&p); return; }
    const char *algo = j_gets(body, "algorithm", NULL);
    json *auth = j_geto(body, "auth_data");
    if (!algo || !auth) {
        resp_error(w, 400, "M_MISSING_PARAM",
                   "algorithm and auth_data are required");
        principal_free(&p);
        return;
    }
    char *ad = dump_str(auth);
    hs_lock();
    int64_t next = 0;
    db_int("SELECT COALESCE(MAX(version),0)+1 FROM e2e_backup_versions WHERE "
           "user_id=?1", &next, "s", p.user_id);
    db_run("INSERT INTO e2e_backup_versions(user_id,version,algorithm,auth_data,"
           "etag,deleted,created_ts) VALUES(?1,?2,?3,?4,0,0,?5)", "sissi",
           p.user_id, next, algo, ad, now_ms());
    hs_unlock();
    free(ad);
    stream_notify();

    json *out = j_obj();
    char *v = str_fmt("%" PRId64, next);
    j_set(out, "version", j_str(v));
    free(v);
    resp_json(w, 200, out);
    principal_free(&p);
}

static void h_backup_version_get_current(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    int64_t v = backup_current_version(p.user_id);
    json *info = v ? backup_version_info(p.user_id, v) : NULL;
    if (!info) resp_error(w, 404, "M_NOT_FOUND", "No current backup version");
    else resp_json(w, 200, info);
    principal_free(&p);
}

/* The {version} path parameter, or 0 when it is not a version at all. */
static int64_t path_version(const char *s) {
    char *end = NULL;
    long long v = s ? strtoll(s, &end, 10) : 0;
    return (end && !*end && v > 0) ? (int64_t)v : 0;
}

static void h_backup_version_get(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    int64_t v = path_version(r->part[0]);
    json *info = v ? backup_version_info(p.user_id, v) : NULL;
    if (!info) resp_error(w, 404, "M_NOT_FOUND", "Unknown backup version");
    else resp_json(w, 200, info);
    principal_free(&p);
}

static void h_backup_version_put(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *body = require_body(r, w);
    if (!body) { principal_free(&p); return; }
    int64_t v = path_version(r->part[0]);
    if (!v || !backup_exists(p.user_id, v)) {
        resp_error(w, 404, "M_NOT_FOUND", "Unknown backup version");
        principal_free(&p);
        return;
    }
    const char *bv = j_gets(body, "version", NULL);
    if (bv && !str_eq(bv, r->part[0])) {
        resp_error(w, 400, "M_INVALID_PARAM",
                   "Version in body does not match the path");
        principal_free(&p);
        return;
    }
    const char *algo = j_gets(body, "algorithm", NULL);
    json *auth = j_geto(body, "auth_data");
    if (!algo || !auth) {
        resp_error(w, 400, "M_MISSING_PARAM",
                   "algorithm and auth_data are required");
        principal_free(&p);
        return;
    }
    char *cur = db_str("SELECT algorithm FROM e2e_backup_versions WHERE "
                       "user_id=?1 AND version=?2", "si", p.user_id, v);
    bool same = cur && str_eq(cur, algo);
    free(cur);
    if (!same) {
        resp_error(w, 400, "M_INVALID_PARAM", "Algorithm does not match");
        principal_free(&p);
        return;
    }
    char *ad = dump_str(auth);
    hs_lock();
    db_run("UPDATE e2e_backup_versions SET auth_data=?3 WHERE user_id=?1 AND "
           "version=?2", "sis", p.user_id, v, ad);
    hs_unlock();
    free(ad);
    stream_notify();
    resp_json(w, 200, j_obj());
    principal_free(&p);
}

static void h_backup_version_delete(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    int64_t v = path_version(r->part[0]);
    int64_t known = 0;
    if (v) db_int("SELECT COUNT(*) FROM e2e_backup_versions WHERE user_id=?1 "
                  "AND version=?2", &known, "si", p.user_id, v);
    if (!known) {
        resp_error(w, 404, "M_NOT_FOUND", "Unknown backup version");
        principal_free(&p);
        return;
    }
    hs_lock();
    db_run("UPDATE e2e_backup_versions SET deleted=1, etag=etag+1 WHERE "
           "user_id=?1 AND version=?2", "si", p.user_id, v);
    db_run("DELETE FROM e2e_backup_keys WHERE user_id=?1 AND version=?2",
           "si", p.user_id, v);
    hs_unlock();
    stream_notify();
    resp_json(w, 200, j_obj());
    principal_free(&p);
}

/* --------------------------------------------------------- the keys ----- */
static json *key_row_json(sqlite3_stmt *st, int base) {
    json *o = j_obj();
    j_set(o, "first_message_index", j_int(sqlite3_column_int64(st, base)));
    j_set(o, "forwarded_count", j_int(sqlite3_column_int64(st, base + 1)));
    j_set(o, "is_verified", j_bool(sqlite3_column_int64(st, base + 2) != 0));
    json *sd = parse_text(col(st, base + 3));
    j_set(o, "session_data", sd ? sd : j_obj());
    return o;
}

/* "if the keys have different values for is_verified, keep the key that has
 * is_verified true; if they are the same, keep the lower first_message_index;
 * if those are equal too, keep the lower forwarded_count." */
static bool should_replace(bool old_verified, int64_t old_index,
                           int64_t old_forwards, bool new_verified,
                           int64_t new_index, int64_t new_forwards) {
    if (new_verified != old_verified) return new_verified;
    if (new_index != old_index) return new_index < old_index;
    return new_forwards < old_forwards;
}

/* Stores one session, honouring the replacement rules.  Returns true when the
 * stored backup actually changed. */
static bool store_one_key(const char *user_id, int64_t version,
                          const char *room_id, const char *session_id,
                          const json *data) {
    if (!data || data->type != J_OBJECT) return false;
    int64_t index = j_geti(data, "first_message_index", 0);
    int64_t fwd = j_geti(data, "forwarded_count", 0);
    bool verified = j_getb(data, "is_verified", false);
    json *sd = j_geto((json *)data, "session_data");
    if (!sd) return false;
    char *text = dump_str(sd);

    sqlite3_stmt *st = NULL;
    bool have_old = false, old_verified = false;
    int64_t old_index = 0, old_fwd = 0;
    /* Reading the stored key and replacing it is one step, so two uploads of
     * the same session cannot both decide that they win. */
    hs_lock();
    if (sqlite3_prepare_v2(db(), "SELECT first_message_index, forwarded_count, "
                                 "is_verified FROM e2e_backup_keys WHERE "
                                 "user_id=?1 AND version=?2 AND room_id=?3 AND "
                                 "session_id=?4", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, user_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 2, version);
        sqlite3_bind_text(st, 3, room_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 4, session_id, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) == SQLITE_ROW) {
            have_old = true;
            old_index = sqlite3_column_int64(st, 0);
            old_fwd = sqlite3_column_int64(st, 1);
            old_verified = sqlite3_column_int64(st, 2) != 0;
        }
        sqlite3_finalize(st);
    }
    if (have_old && !should_replace(old_verified, old_index, old_fwd,
                                    verified, index, fwd)) {
        hs_unlock();
        free(text);
        return false;
    }
    db_run("INSERT INTO e2e_backup_keys(user_id,version,room_id,session_id,"
           "first_message_index,forwarded_count,is_verified,session_data) "
           "VALUES(?1,?2,?3,?4,?5,?6,?7,?8) "
           "ON CONFLICT(user_id,version,room_id,session_id) DO UPDATE SET "
           "first_message_index=?5, forwarded_count=?6, is_verified=?7, "
           "session_data=?8", "sissiiis", user_id, version, room_id, session_id,
           index, fwd, (int64_t)(verified ? 1 : 0), text);
    hs_unlock();
    free(text);
    return true;
}

/* PUT /room_keys/keys, /room_keys/keys/{roomId}, /room_keys/keys/{roomId}/{s} */
static void h_room_keys_put(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *body = require_body(r, w);
    if (!body) { principal_free(&p); return; }
    int64_t version = 0;
    if (!backup_resolve(r, w, p.user_id, true, &version)) {
        principal_free(&p);
        return;
    }
    bool changed = false;
    if (r->nparts >= 2) {
        changed = store_one_key(p.user_id, version, r->part[0], r->part[1], body);
    } else if (r->nparts == 1) {
        json *sessions = j_geto(body, "sessions");
        for (size_t i = 0; sessions && i < j_len(sessions); i++)
            changed |= store_one_key(p.user_id, version, r->part[0],
                                     j_key(sessions, i), j_at(sessions, i));
    } else {
        json *rooms = j_geto(body, "rooms");
        for (size_t i = 0; rooms && i < j_len(rooms); i++) {
            json *sessions = j_geto(j_at(rooms, i), "sessions");
            for (size_t k = 0; sessions && k < j_len(sessions); k++)
                changed |= store_one_key(p.user_id, version, j_key(rooms, i),
                                         j_key(sessions, k), j_at(sessions, k));
        }
    }
    if (changed) backup_bump_etag(p.user_id, version);
    resp_json(w, 200, backup_update_response(p.user_id, version));
    principal_free(&p);
}

static void h_room_keys_get(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    int64_t version = 0;
    if (!backup_resolve(r, w, p.user_id, false, &version)) {
        principal_free(&p);
        return;
    }
    sqlite3_stmt *st = NULL;
    const char *sql =
        r->nparts >= 2
            ? "SELECT room_id, session_id, first_message_index, forwarded_count,"
              " is_verified, session_data FROM e2e_backup_keys WHERE user_id=?1 "
              "AND version=?2 AND room_id=?3 AND session_id=?4"
        : r->nparts == 1
            ? "SELECT room_id, session_id, first_message_index, forwarded_count,"
              " is_verified, session_data FROM e2e_backup_keys WHERE user_id=?1 "
              "AND version=?2 AND room_id=?3"
            : "SELECT room_id, session_id, first_message_index, forwarded_count,"
              " is_verified, session_data FROM e2e_backup_keys WHERE user_id=?1 "
              "AND version=?2";
    json *rooms = j_obj();
    json *single = NULL;
    if (sqlite3_prepare_v2(db(), sql, -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, p.user_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 2, version);
        if (r->nparts >= 1) sqlite3_bind_text(st, 3, r->part[0], -1, SQLITE_TRANSIENT);
        if (r->nparts >= 2) sqlite3_bind_text(st, 4, r->part[1], -1, SQLITE_TRANSIENT);
        while (sqlite3_step(st) == SQLITE_ROW) {
            json *k = key_row_json(st, 2);
            if (r->nparts >= 2) { j_free(single); single = k; continue; }
            json *room = obj_at(rooms, col(st, 0));
            j_set(obj_at(room, "sessions"), col(st, 1), k);
        }
        sqlite3_finalize(st);
    }
    if (r->nparts >= 2) {
        j_free(rooms);
        if (!single) resp_error(w, 404, "M_NOT_FOUND", "Key not found.");
        else resp_json(w, 200, single);
    } else if (r->nparts == 1) {
        json *room = j_geto(rooms, r->part[0]);
        json *out = j_obj();
        j_set(out, "sessions", room ? j_clone(j_geto(room, "sessions")) : j_obj());
        j_free(rooms);
        resp_json(w, 200, out);
    } else {
        /* Every room must carry a `sessions` object, even an empty one. */
        for (size_t i = 0; i < j_len(rooms); i++) obj_at(j_at(rooms, i), "sessions");
        json *out = j_obj();
        j_set(out, "rooms", rooms);
        resp_json(w, 200, out);
    }
    principal_free(&p);
}

static void h_room_keys_delete(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    int64_t version = 0;
    if (!backup_resolve(r, w, p.user_id, false, &version)) {
        principal_free(&p);
        return;
    }
    int64_t before = backup_count(p.user_id, version);
    hs_lock();
    if (r->nparts >= 2)
        db_run("DELETE FROM e2e_backup_keys WHERE user_id=?1 AND version=?2 AND "
               "room_id=?3 AND session_id=?4", "siss", p.user_id, version,
               r->part[0], r->part[1]);
    else if (r->nparts == 1)
        db_run("DELETE FROM e2e_backup_keys WHERE user_id=?1 AND version=?2 AND "
               "room_id=?3", "sis", p.user_id, version, r->part[0]);
    else
        db_run("DELETE FROM e2e_backup_keys WHERE user_id=?1 AND version=?2",
               "si", p.user_id, version);
    hs_unlock();
    if (backup_count(p.user_id, version) != before)
        backup_bump_etag(p.user_id, version);
    else
        stream_notify();
    resp_json(w, 200, backup_update_response(p.user_id, version));
    principal_free(&p);
}

/* ================================================ send-to-device ========= */
/* Queues one message for a local user.  `device_id` may be "*", which
 * mxapi_sync.c's to_device_section() expands to every device of the user. */
static void queue_to_device(const char *sender, const char *user_id,
                            const char *device_id, const char *type,
                            const json *content) {
    char *text = dump_str(content);
    hs_lock();
    int64_t sid = stream_next();
    db_run("INSERT INTO to_device(stream_id,user_id,device_id,sender,type,"
           "content) VALUES(?1,?2,?3,?4,?5,?6)", "isssss", sid, user_id,
           device_id, sender, type, text);
    hs_unlock();
    free(text);
}

/* Sends one m.direct_to_device EDU straight to `dest`.  mxfed.h exposes
 * fed_broadcast_edu(), which goes to every server in a room; a to-device
 * message must reach exactly one server, so the transaction is built here and
 * handed to fed_request(). */
static void send_direct_to_device(const char *dest, const char *sender,
                                  const char *type, json *messages) {
    if (CFG.disable_federation) { j_free(messages); return; }
    char *rnd = random_string(16);
    char *msg_id = str_fmt("e2e%s", rnd);
    json *content = j_obj();
    j_set(content, "sender", j_str(sender));
    j_set(content, "type", j_str(type));
    j_set(content, "message_id", j_str(msg_id));
    j_set(content, "messages", messages);
    json *edu = j_obj();
    j_set(edu, "edu_type", j_str("m.direct_to_device"));
    j_set(edu, "content", content);
    json *edus = j_arr();
    j_push(edus, edu);
    json *txn = j_obj();
    j_set(txn, "origin", j_str(CFG.server_name));
    j_set(txn, "origin_server_ts", j_int(now_ms()));
    j_set(txn, "pdus", j_arr());
    j_set(txn, "edus", edus);
    char *path = str_fmt(FED1 "/send/%s", msg_id);
    http_client_resp *resp = fed_request(dest, "PUT", path, txn);
    http_client_resp_free(resp);
    free(path);
    j_free(txn);
    free(msg_id);
    free(rnd);
}

static void h_send_to_device(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *body = require_body(r, w);
    if (!body) { principal_free(&p); return; }
    const char *type = r->part[0];
    const char *txn = r->part[1];

    char *txn_path = str_fmt("sendToDevice:%s", type);
    json *prev = txn_lookup(&p, txn_path, txn);
    if (prev) {
        resp_json(w, 200, prev);
        free(txn_path);
        principal_free(&p);
        return;
    }
    json *messages = j_geto(body, "messages");
    if (!messages) {
        resp_error(w, 400, "M_MISSING_PARAM", "messages is required");
        free(txn_path);
        principal_free(&p);
        return;
    }

    int targets = 0;
    json *remote = j_obj();          /* server -> {user -> {device -> content}} */
    for (size_t i = 0; i < j_len(messages); i++) {
        const char *uid = j_key(messages, i);
        json *devs = j_at(messages, i);
        if (!uid || !devs || devs->type != J_OBJECT) continue;
        if (!user_is_local(uid)) {
            const char *srv = user_id_server(uid);
            if (!srv || !*srv) continue;
            j_set(obj_at(remote, srv), uid, j_clone(devs));
            continue;
        }
        for (size_t d = 0; d < j_len(devs); d++) {
            const char *did = j_key(devs, d);
            json *content = j_at(devs, d);
            if (!did || !content) continue;
            if (++targets > MAX_TO_DEVICE_TARGETS) break;
            if (!str_eq(did, "*")) {
                char *known = db_str("SELECT device_id FROM devices WHERE "
                                     "user_id=?1 AND device_id=?2", "ss",
                                     uid, did);
                if (!known) continue;     /* an unknown device is dropped */
                free(known);
            }
            queue_to_device(p.user_id, uid, did, type, content);
        }
    }
    for (size_t i = 0; i < j_len(remote); i++)
        send_direct_to_device(j_key(remote, i), p.user_id, type,
                              j_clone(j_at(remote, i)));
    j_free(remote);
    stream_notify();

    json *out = j_obj();
    txn_remember(&p, txn_path, txn, out);
    free(txn_path);
    resp_json(w, 200, out);
    principal_free(&p);
}

/* ===================== Server-Server: /user/keys/query and /claim ======= */
static void h_fed_keys_query(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    char *origin = NULL;
    if (!fed_authenticate(r, w, &origin)) return;
    json *body = http_json(r);
    json *want = body ? j_geto(body, "device_keys") : NULL;
    if (!want || want->type != J_OBJECT) {
        resp_error(w, 400, "M_BAD_JSON", "device_keys is required");
        free(origin);
        return;
    }
    reconcile_devices();
    json *keys = j_obj(), *master = j_obj(), *self_signing = j_obj();
    for (size_t i = 0; i < j_len(want); i++) {
        const char *uid = j_key(want, i);
        if (!uid || !user_is_local(uid)) continue;     /* only our own users */
        json *devs = j_at(want, i);
        /* The asking server proxies for its own users, so it sees what any
         * other user sees: everything except signatures made with a
         * user-signing key.  An empty requester matches nobody. */
        json *k = local_user_device_keys(uid, devs && devs->type == J_ARRAY
                                                  ? devs : NULL, "");
        if (j_len(k)) j_set(keys, uid, k); else j_free(k);
        json *mk = cross_signing_key_for(uid, "master", "");
        if (mk) j_set(master, uid, mk);
        json *sk = cross_signing_key_for(uid, "self_signing", "");
        if (sk) j_set(self_signing, uid, sk);
    }
    json *out = j_obj();
    j_set(out, "device_keys", keys);
    if (j_len(master)) j_set(out, "master_keys", master); else j_free(master);
    if (j_len(self_signing)) j_set(out, "self_signing_keys", self_signing);
    else j_free(self_signing);
    resp_json(w, 200, out);
    free(origin);
}

static void h_fed_keys_claim(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    char *origin = NULL;
    if (!fed_authenticate(r, w, &origin)) return;
    json *body = http_json(r);
    json *want = body ? j_geto(body, "one_time_keys") : NULL;
    if (!want || want->type != J_OBJECT) {
        resp_error(w, 400, "M_BAD_JSON", "one_time_keys is required");
        free(origin);
        return;
    }
    reconcile_devices();
    json *keys = j_obj();
    claim_local(want, keys);
    stream_notify();
    json *out = j_obj();
    j_set(out, "one_time_keys", keys);
    resp_json(w, 200, out);
    free(origin);
}

/* ================================================ inbound EDUs ========== */
static void edu_device_list_update(const char *origin, const json *c) {
    const char *user_id = j_gets(c, "user_id", NULL);
    const char *device_id = j_gets(c, "device_id", NULL);
    if (!user_id || !device_id) return;
    const char *srv = user_id_server(user_id);
    if (!srv || !str_eq(srv, origin)) return;      /* only your own users */
    json *keys = j_geto((json *)c, "keys");
    char *ktext = keys ? dump_str(keys) : NULL;
    bool deleted = j_getb(c, "deleted", false);
    hs_lock();
    db_run("INSERT INTO fed_devices(user_id,device_id,display_name,stream_id,"
           "keys,deleted) VALUES(?1,?2,?3,?4,?5,?6) "
           "ON CONFLICT(user_id,device_id) DO UPDATE SET display_name=?3,"
           "stream_id=?4, keys=?5, deleted=?6", "sssisi", user_id, device_id,
           j_gets(c, "device_display_name", NULL), j_geti(c, "stream_id", 0),
           ktext, (int64_t)(deleted ? 1 : 0));
    int64_t sid = stream_next();
    db_run("INSERT INTO e2e_device_changes(stream_id,user_id,device_id,deleted,ts) "
           "VALUES(?1,?2,?3,?4,?5)", "issii", sid, user_id, device_id,
           (int64_t)(deleted ? 1 : 0), now_ms());
    hs_unlock();
    free(ktext);
    stream_notify();
}

static void edu_signing_key_update(const char *origin, const json *c) {
    const char *user_id = j_gets(c, "user_id", NULL);
    if (!user_id) return;
    const char *srv = user_id_server(user_id);
    if (!srv || !str_eq(srv, origin)) return;
    static const char *SLOTS[] = {"master_key", "master",
                                  "self_signing_key", "self_signing", NULL};
    hs_lock();
    for (int i = 0; SLOTS[i]; i += 2) {
        json *k = j_geto((json *)c, SLOTS[i]);
        if (!k) continue;
        char *text = dump_str(k);
        db_run("INSERT INTO e2e_remote_cross_signing(user_id,usage,key_json,ts) "
               "VALUES(?1,?2,?3,?4) ON CONFLICT(user_id,usage) DO UPDATE SET "
               "key_json=?3, ts=?4", "sssi", user_id, SLOTS[i + 1], text,
               now_ms());
        free(text);
    }
    int64_t sid = stream_next();
    db_run("INSERT INTO e2e_device_changes(stream_id,user_id,device_id,deleted,ts) "
           "VALUES(?1,?2,'',0,?3)", "isi", sid, user_id, now_ms());
    hs_unlock();
    stream_notify();
}

static void edu_direct_to_device(const char *origin, const json *c) {
    const char *sender = j_gets(c, "sender", NULL);
    const char *type = j_gets(c, "type", NULL);
    json *messages = j_geto((json *)c, "messages");
    if (!sender || !type || !messages) return;
    const char *srv = user_id_server(sender);
    if (!srv || !str_eq(srv, origin)) return;
    for (size_t i = 0; i < j_len(messages); i++) {
        const char *uid = j_key(messages, i);
        json *devs = j_at(messages, i);
        if (!uid || !user_is_local(uid) || !devs || devs->type != J_OBJECT)
            continue;
        for (size_t d = 0; d < j_len(devs); d++) {
            const char *did = j_key(devs, d);
            json *content = j_at(devs, d);
            if (!did || !content) continue;
            if (!str_eq(did, "*")) {
                char *known = db_str("SELECT device_id FROM devices WHERE "
                                     "user_id=?1 AND device_id=?2", "ss",
                                     uid, did);
                if (!known) continue;
                free(known);
            }
            queue_to_device(sender, uid, did, type, content);
        }
    }
    stream_notify();
}

bool keys_handle_edu(const char *origin, const json *edu) {
    if (!origin || !edu || edu->type != J_OBJECT) return false;
    const char *type = j_gets(edu, "edu_type", NULL);
    json *c = j_geto((json *)edu, "content");
    if (!type || !c) return false;
    if (str_eq(type, "m.device_list_update")) { edu_device_list_update(origin, c); return true; }
    if (str_eq(type, "m.signing_key_update")) { edu_signing_key_update(origin, c); return true; }
    if (str_eq(type, "m.direct_to_device")) { edu_direct_to_device(origin, c); return true; }
    return false;
}

/* =================================================== routing ============ */
static void route_both(http_server *s, const char *method, const char *suffix,
                       http_handler fn) {
    char *a = str_fmt("%s%s", CS, suffix);
    char *b = str_fmt("%s%s", CSR, suffix);
    http_route(s, method, a, fn);
    http_route(s, method, b, fn);
    free(a); free(b);
}

void api_register_keys(http_server *s) {
    spec_advertise("v1.3");
    if (!s) return;
    if (!db_exec(KEYS_SCHEMA))
        fprintf(stderr, "keys: could not create the end-to-end key tables\n");

    /* --- key distribution --- */
    route_both(s, "POST", "/keys/upload", h_keys_upload);
    route_both(s, "POST", "/keys/upload/:", h_keys_upload);
    route_both(s, "POST", "/keys/query", h_keys_query);
    route_both(s, "POST", "/keys/claim", h_keys_claim);
    route_both(s, "GET", "/keys/changes", h_keys_changes);

    /* --- cross-signing --- */
    route_both(s, "POST", "/keys/device_signing/upload", h_device_signing_upload);
    route_both(s, "POST", "/keys/signatures/upload", h_signatures_upload);

    /* --- server-side key backups --- */
    route_both(s, "POST", "/room_keys/version", h_backup_version_post);
    route_both(s, "GET", "/room_keys/version", h_backup_version_get_current);
    route_both(s, "GET", "/room_keys/version/:", h_backup_version_get);
    route_both(s, "PUT", "/room_keys/version/:", h_backup_version_put);
    route_both(s, "DELETE", "/room_keys/version/:", h_backup_version_delete);
    route_both(s, "PUT", "/room_keys/keys", h_room_keys_put);
    route_both(s, "GET", "/room_keys/keys", h_room_keys_get);
    route_both(s, "DELETE", "/room_keys/keys", h_room_keys_delete);
    route_both(s, "PUT", "/room_keys/keys/:", h_room_keys_put);
    route_both(s, "GET", "/room_keys/keys/:", h_room_keys_get);
    route_both(s, "DELETE", "/room_keys/keys/:", h_room_keys_delete);
    route_both(s, "PUT", "/room_keys/keys/:/:", h_room_keys_put);
    route_both(s, "GET", "/room_keys/keys/:/:", h_room_keys_get);
    route_both(s, "DELETE", "/room_keys/keys/:/:", h_room_keys_delete);

    /* --- send-to-device --- */
    route_both(s, "PUT", "/sendToDevice/:/:", h_send_to_device);

    /* --- Server-Server --- */
    http_route(s, "POST", FED1 "/user/keys/query", h_fed_keys_query);
    http_route(s, "POST", FED1 "/user/keys/claim", h_fed_keys_claim);
}

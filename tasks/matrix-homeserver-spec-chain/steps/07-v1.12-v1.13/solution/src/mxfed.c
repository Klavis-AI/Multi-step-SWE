#define _POSIX_C_SOURCE 200809L
/* The Server-Server (federation) API: request authentication, the key server,
 * transactions, the event and state endpoints, the join/leave/invite/knock
 * handshakes, the federation queries, and the outbound side of all of it. */
#include "mxfed.h"
#include "mxkeys.h"

#include "mxapi.h"
#include "mxdb.h"
#include "mxevent.h"
#include "mxhttp.h"
#include "mxjson.h"
#include "mxroom.h"
#include "mxserver.h"
#include "mxutil.h"

#include <ctype.h>
#include <openssl/evp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define FED1 "/_matrix/federation/v1"
#define FED2 "/_matrix/federation/v2"
#define KEY2 "/_matrix/key/v2"

#define MAX_PDUS_PER_TXN  50
#define MAX_EDUS_PER_TXN 100
/* "Servers MUST use the lesser of this field and 7 days into the future when
 * determining if a key is valid." */
#define KEY_MAX_VALIDITY_MS (7LL * 24 * 3600 * 1000)
#define SEND_ATTEMPTS 3

/* ------------------------------------------------------------- schema ---- */
/* Everything federation needs that mxdb.c's schema does not already carry.
 * Created from api_register_federation() so mxdb.c stays untouched. */
static const char *FED_SCHEMA =
"CREATE TABLE IF NOT EXISTS fed_keys("
"  server_name TEXT PRIMARY KEY, doc TEXT, valid_until_ts INTEGER,"
"  fetched_ts INTEGER);"
"CREATE TABLE IF NOT EXISTS fed_inbound_txns("
"  origin TEXT, txn_id TEXT, response TEXT, received_ts INTEGER,"
"  PRIMARY KEY(origin, txn_id));"
"CREATE TABLE IF NOT EXISTS fed_pos(name TEXT PRIMARY KEY, value INTEGER);"
"CREATE TABLE IF NOT EXISTS fed_devices("
"  user_id TEXT, device_id TEXT, display_name TEXT, stream_id INTEGER,"
"  keys TEXT, deleted INTEGER DEFAULT 0, PRIMARY KEY(user_id, device_id));"
/* The state of the room *before* each event, as a JSON object mapping
 * "<type>\x1f<state_key>" to an event id: S(E), worked out once and kept, so
 * that an event arriving on a fork is authorised against the state at its own
 * place in the DAG rather than against whatever the room looks like now. */
"CREATE TABLE IF NOT EXISTS fed_state(event_id TEXT PRIMARY KEY, state TEXT);"
/* Events that passed the authorization rules at their own place in the DAG but
 * not against the room's current state: "soft failed", and so not forward
 * extremities. */
"CREATE TABLE IF NOT EXISTS fed_soft(event_id TEXT PRIMARY KEY);"
;

/* ------------------------------------------------------------ utilities -- */
static void jset_str(json *o, const char *k, const char *v) {
    if (v) j_set(o, k, j_str(v));
}

static const char *jstr(const json *v) {
    return (v && v->type == J_STRING) ? v->s : NULL;
}

/* An entry of prev_events / auth_events: a plain id from room version 3, and
 * an [id, {"sha256": ...}] pair before that. */
static const char *ev_ref_id(const json *item) {
    if (!item) return NULL;
    if (item->type == J_STRING) return item->s;
    if (item->type == J_ARRAY) return jstr(j_at(item, 0));
    return NULL;
}

static bool arr_has_str(const json *arr, const char *s) {
    for (size_t i = 0; arr && i < j_len(arr); i++) {
        const char *v = jstr(j_at(arr, i));
        if (v && strcmp(v, s) == 0) return true;
    }
    return false;
}

static void arr_add_unique(json *arr, const char *s) {
    if (!arr_has_str(arr, s)) j_push(arr, j_str(s));
}

/* The errcode slot is a string literal, so a code a peer sent is mapped onto
 * one this server knows rather than pointing into freed JSON. */
static const char *known_errcode(const char *code) {
    static const char *CODES[] = {
        "M_FORBIDDEN", "M_NOT_FOUND", "M_BAD_JSON", "M_UNAUTHORIZED",
        "M_INCOMPATIBLE_ROOM_VERSION", "M_UNSUPPORTED_ROOM_VERSION",
        "M_MISSING_PARAM", "M_INVALID_PARAM", "M_NOT_JSON", "M_LIMIT_EXCEEDED",
        "M_UNRECOGNIZED", "M_TOO_LARGE", "M_UNKNOWN", NULL };
    for (int i = 0; code && CODES[i]; i++)
        if (strcmp(code, CODES[i]) == 0) return CODES[i];
    return "M_UNKNOWN";
}

static json *parse_str(const char *text, size_t n) {
    if (!text) return NULL;
    const char *err = NULL;
    return j_parse(text, n, &err);
}

static json *parse_owned(char *text) {
    if (!text) return NULL;
    json *v = parse_str(text, strlen(text));
    free(text);
    return v;
}

/* -------------------------------------------------------- server names --- */
bool fed_is_local_server(const char *server_name) {
    return server_name && CFG.server_name && strcmp(server_name, CFG.server_name) == 0;
}

bool fed_is_local_user(const char *user_id) {
    const char *srv = user_id_server(user_id);
    return srv && fed_is_local_server(srv);
}

static bool all_digits(const char *s) {
    if (!s || !*s) return false;
    for (; *s; s++) if (!isdigit((unsigned char)*s)) return false;
    return true;
}

/* "Resolving Server Names".  A name with an explicit port is contacted on that
 * port over HTTPS; anything else falls back to the default federation port.
 * `.well-known` delegation and SRV lookups are deliberately not performed -
 * see the report. */
char *fed_base_url(const char *server_name) {
    if (!server_name || !*server_name) return NULL;
    /* [v6]:port and [v6] */
    if (server_name[0] == '[') {
        const char *close = strchr(server_name, ']');
        if (!close) return NULL;
        if (close[1] == ':' && all_digits(close + 2))
            return str_fmt("https://%s", server_name);
        return str_fmt("https://%s:8448", server_name);
    }
    const char *colon = strrchr(server_name, ':');
    if (colon && all_digits(colon + 1))
        return str_fmt("https://%s", server_name);
    return str_fmt("https://%s:8448", server_name);
}

/* ------------------------------------------------------------ key store -- */
static char *our_public_key_b64(void) {
    unsigned char pub[32];
    if (!ed25519_public_from_seed(CFG.key_seed, pub)) return NULL;
    return b64_encode(pub, 32, false, false);
}

json *fed_local_keys(void) {
    json *doc = j_obj();
    jset_str(doc, "server_name", CFG.server_name);
    json *vk = j_obj();
    char *pub = our_public_key_b64();
    if (pub) {
        json *one = j_obj();
        j_set(one, "key", j_str(pub));
        j_set(vk, CFG.key_id ? CFG.key_id : "ed25519:auto", one);
        free(pub);
    }
    j_set(doc, "verify_keys", vk);
    j_set(doc, "old_verify_keys", j_obj());
    int64_t validity = CFG.key_validity_ms > 0 ? CFG.key_validity_ms
                                               : KEY_MAX_VALIDITY_MS;
    if (validity > KEY_MAX_VALIDITY_MS) validity = KEY_MAX_VALIDITY_MS;
    j_set(doc, "valid_until_ts", j_int(now_ms() + validity));
    json_sign(doc, CFG.server_name, CFG.key_id, CFG.key_seed);
    return doc;
}

/* Pulls the 32-byte key out of a published key document. */
static bool key_from_doc(const json *doc, const char *key_id,
                         unsigned char pub[32]) {
    static const char *WHERE[] = {"verify_keys", "old_verify_keys", NULL};
    for (int i = 0; WHERE[i]; i++) {
        json *m = j_geto((json *)doc, WHERE[i]);
        json *one = m ? j_geto(m, key_id) : NULL;
        const char *b64 = one ? j_gets(one, "key", NULL) : NULL;
        if (!b64) continue;
        size_t n = 0;
        unsigned char *raw = b64_decode(b64, &n);
        if (!raw) return false;
        bool ok = n == 32;
        if (ok) memcpy(pub, raw, 32);
        free(raw);
        return ok;
    }
    return false;
}

/* A published key document must be self-signed by the keys it publishes. */
static bool key_doc_self_signed(const json *doc, const char *server_name) {
    json *sigs = j_geto((json *)doc, "signatures");
    json *mine = sigs ? j_geto(sigs, server_name) : NULL;
    if (!mine) return false;
    for (size_t i = 0; i < j_len(mine); i++) {
        const char *kid = j_key(mine, i);
        unsigned char pub[32];
        if (!key_from_doc(doc, kid, pub)) continue;
        if (json_verify(doc, server_name, kid, pub)) return true;
    }
    return false;
}

static int64_t doc_valid_until(const json *doc) {
    int64_t v = j_geti(doc, "valid_until_ts", 0);
    int64_t cap = now_ms() + KEY_MAX_VALIDITY_MS;
    if (v <= 0 || v > cap) v = cap;
    return v;
}

static void key_cache_store(const char *server_name, const json *doc) {
    size_t n = 0;
    char *text = j_dump(doc, &n);
    if (!text) return;
    hs_lock();
    db_run("INSERT INTO fed_keys(server_name,doc,valid_until_ts,fetched_ts) "
           "VALUES(?1,?2,?3,?4) ON CONFLICT(server_name) DO UPDATE SET "
           "doc=?2, valid_until_ts=?3, fetched_ts=?4",
           "ssii", server_name, text, doc_valid_until(doc), now_ms());
    hs_unlock();
    free(text);
}

/* The cached document, still inside its valid_until_ts, or NULL. */
static json *key_cache_get(const char *server_name, bool allow_expired) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(), "SELECT doc, valid_until_ts FROM fed_keys "
                                 "WHERE server_name=?1", -1, &st, NULL) != SQLITE_OK)
        return NULL;
    sqlite3_bind_text(st, 1, server_name, -1, SQLITE_TRANSIENT);
    json *doc = NULL;
    if (sqlite3_step(st) == SQLITE_ROW) {
        int64_t until = sqlite3_column_int64(st, 1);
        const char *text = (const char *)sqlite3_column_text(st, 0);
        if (text && (allow_expired || until > now_ms()))
            doc = parse_str(text, strlen(text));
    }
    sqlite3_finalize(st);
    return doc;
}

/* GET https://<server>/_matrix/key/v2/server, validated and cached. */
static json *key_doc_fetch(const char *server_name) {
    char *base = fed_base_url(server_name);
    if (!base) return NULL;
    char *url = str_fmt("%s%s/server", base, KEY2);
    free(base);
    const char *hdrs[] = {"Accept: application/json"};
    http_client_resp *resp = http_get(url, hdrs, 1);
    free(url);
    if (!resp) return NULL;
    json *doc = NULL;
    if (resp->status == 200 && resp->body)
        doc = parse_str(resp->body, resp->body_len);
    http_client_resp_free(resp);
    if (!doc) return NULL;
    if (doc->type != J_OBJECT ||
        !str_eq(j_gets(doc, "server_name", ""), server_name) ||
        !key_doc_self_signed(doc, server_name)) {
        fprintf(stderr, "federation: rejected key document from %s\n", server_name);
        j_free(doc);
        return NULL;
    }
    key_cache_store(server_name, doc);
    return doc;
}

bool fed_fetch_key(const char *server_name, const char *key_id,
                   unsigned char pub[32]) {
    if (!server_name || !key_id) return false;
    if (fed_is_local_server(server_name)) {
        if (!str_eq(key_id, CFG.key_id)) return false;
        return ed25519_public_from_seed(CFG.key_seed, pub);
    }
    json *doc = key_cache_get(server_name, false);
    if (doc && key_from_doc(doc, key_id, pub)) { j_free(doc); return true; }
    j_free(doc);
    doc = key_doc_fetch(server_name);
    if (!doc) {
        /* Fall back on a stale cache entry rather than dropping the request
         * altogether; old events still need their signatures checked. */
        doc = key_cache_get(server_name, true);
    }
    bool ok = doc && key_from_doc(doc, key_id, pub);
    j_free(doc);
    return ok;
}

/* The published document for `server_name`, cached or fetched, for the notary
 * endpoints.  Returns NULL when it cannot be obtained. */
static json *fed_key_doc(const char *server_name) {
    if (fed_is_local_server(server_name)) return fed_local_keys();
    json *doc = key_cache_get(server_name, false);
    if (doc) return doc;
    doc = key_doc_fetch(server_name);
    if (doc) return doc;
    return key_cache_get(server_name, true);
}

/* ------------------------------------------- request authentication ------ */
/* The object that is signed for every federation request, per the
 * "Request Authentication" section:
 *   {"method", "uri", "origin", "destination", "content"?}                   */
static json *auth_object(const char *method, const char *uri,
                         const char *origin, const char *destination,
                         const json *content) {
    json *o = j_obj();
    jset_str(o, "method", method);
    jset_str(o, "uri", uri);
    jset_str(o, "origin", origin);
    jset_str(o, "destination", destination);
    if (content) j_set(o, "content", j_clone(content));
    return o;
}

/* X-Matrix origin="…",destination="…",key="ed25519:…",sig="…" */
static char *xmatrix_header(const char *method, const char *uri,
                            const char *destination, const json *content) {
    json *o = auth_object(method, uri, CFG.server_name, destination, content);
    if (!json_sign(o, CFG.server_name, CFG.key_id, CFG.key_seed)) {
        j_free(o);
        return NULL;
    }
    json *sigs = j_geto(o, "signatures");
    json *mine = sigs ? j_geto(sigs, CFG.server_name) : NULL;
    const char *sig = mine ? j_gets(mine, CFG.key_id, NULL) : NULL;
    char *hdr = NULL;
    if (sig)
        hdr = str_fmt("Authorization: X-Matrix origin=\"%s\",destination=\"%s\","
                      "key=\"%s\",sig=\"%s\"",
                      CFG.server_name, destination, CFG.key_id, sig);
    j_free(o);
    return hdr;
}

typedef struct { char *origin, *destination, *key, *sig; } xmatrix;

static void xmatrix_free(xmatrix *x) {
    free(x->origin); free(x->destination); free(x->key); free(x->sig);
    memset(x, 0, sizeof *x);
}

static void xm_store(xmatrix *x, const char *name, size_t nlen,
                     const char *val, size_t vlen) {
    char **slot = NULL;
    if (nlen == 6 && strncasecmp(name, "origin", 6) == 0) slot = &x->origin;
    else if (nlen == 11 && strncasecmp(name, "destination", 11) == 0) slot = &x->destination;
    else if (nlen == 3 && strncasecmp(name, "key", 3) == 0) slot = &x->key;
    else if (nlen == 3 && strncasecmp(name, "sig", 3) == 0) slot = &x->sig;
    if (!slot || *slot) return;
    char *v = malloc(vlen + 1);
    if (!v) return;
    memcpy(v, val, vlen);
    v[vlen] = 0;
    *slot = v;
}

/* Parses the credentials of an `X-Matrix` Authorization header.  Whitespace
 * around the commas and the equals signs is tolerated, and values may be
 * quoted or bare. */
static bool xmatrix_parse(const char *hdr, xmatrix *out) {
    memset(out, 0, sizeof *out);
    if (!hdr) return false;
    while (*hdr == ' ' || *hdr == '\t') hdr++;
    if (strncasecmp(hdr, "X-Matrix", 8) != 0) return false;
    const char *p = hdr + 8;
    if (*p && *p != ' ' && *p != '\t') return false;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) break;
        const char *name = p;
        while (*p && *p != '=' && *p != ',' && *p != ' ' && *p != '\t') p++;
        size_t nlen = (size_t)(p - name);
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '=') { if (*p == ',') continue; break; }
        p++;
        while (*p == ' ' || *p == '\t') p++;
        const char *val;
        size_t vlen;
        if (*p == '"') {
            p++;
            val = p;
            while (*p && *p != '"') { if (*p == '\\' && p[1]) p++; p++; }
            vlen = (size_t)(p - val);
            if (*p == '"') p++;
        } else {
            val = p;
            while (*p && *p != ',') p++;
            vlen = (size_t)(p - val);
            while (vlen > 0 && (val[vlen - 1] == ' ' || val[vlen - 1] == '\t'))
                vlen--;
        }
        xm_store(out, name, nlen, val, vlen);
    }
    return out->origin && out->key && out->sig;
}

bool fed_authenticate(http_req *r, http_resp *w, char **origin_out) {
    if (origin_out) *origin_out = NULL;
    const char *hdr = http_header(r, "authorization");
    xmatrix x;
    if (!xmatrix_parse(hdr, &x)) {
        xmatrix_free(&x);
        resp_error(w, 401, "M_UNAUTHORIZED",
                   "Missing or unparsable X-Matrix Authorization header");
        return false;
    }
    if (x.destination && !fed_is_local_server(x.destination)) {
        xmatrix_free(&x);
        resp_error(w, 401, "M_UNAUTHORIZED",
                   "Destination does not match this server");
        return false;
    }
    if (fed_is_local_server(x.origin)) {
        xmatrix_free(&x);
        resp_error(w, 401, "M_UNAUTHORIZED", "Origin is this server");
        return false;
    }

    json *content = NULL;
    if (r->body && r->body_len) {
        content = http_json(r);
        if (!content) {
            xmatrix_free(&x);
            resp_error(w, 400, "M_NOT_JSON", "Request body is not valid JSON");
            return false;
        }
    }
    json *o = auth_object(r->method, r->target, x.origin,
                          x.destination ? x.destination : CFG.server_name,
                          content);
    json *sigs = j_obj();
    json *mine = j_obj();
    j_set(mine, x.key, j_str(x.sig));
    j_set(sigs, x.origin, mine);
    j_set(o, "signatures", sigs);

    unsigned char pub[32];
    bool ok = fed_fetch_key(x.origin, x.key, pub) &&
              json_verify(o, x.origin, x.key, pub);
    j_free(o);
    if (!ok) {
        char *msg = str_fmt("Signature from %s (%s) could not be verified",
                            x.origin, x.key);
        resp_error(w, 401, "M_UNAUTHORIZED", msg);
        free(msg);
        xmatrix_free(&x);
        return false;
    }
    if (origin_out) { *origin_out = x.origin; x.origin = NULL; }
    xmatrix_free(&x);
    return true;
}

/* ------------------------------------------------- outbound requests ----- */
http_client_resp *fed_request(const char *destination, const char *method,
                              const char *path, const json *body) {
    if (!destination || !method || !path) return NULL;
    char *base = fed_base_url(destination);
    if (!base) return NULL;
    char *url = str_fmt("%s%s", base, path);
    free(base);
    char *auth = xmatrix_header(method, path, destination, body);
    if (!auth) { free(url); return NULL; }

    size_t blen = 0;
    char *btext = body ? j_dump(body, &blen) : NULL;
    char *host = str_fmt("Host: %s", destination);
    const char *hdrs[4];
    int nh = 0;
    hdrs[nh++] = auth;
    hdrs[nh++] = host;
    hdrs[nh++] = "Content-Type: application/json";
    hdrs[nh++] = "Accept: application/json";
    http_client_resp *resp = http_send(method, url, btext, blen, hdrs, nh);
    free(btext);
    free(host);
    free(auth);
    free(url);
    return resp;
}

/* Performs the request and returns its parsed JSON body, or NULL.  *status
 * receives the HTTP status (0 when the request could not be made). */
static json *fed_request_json(const char *destination, const char *method,
                              const char *path, const json *body, int *status) {
    if (status) *status = 0;
    http_client_resp *resp = fed_request(destination, method, path, body);
    if (!resp) return NULL;
    if (status) *status = resp->status;
    json *out = resp->body ? parse_str(resp->body, resp->body_len) : NULL;
    http_client_resp_free(resp);
    return out;
}

/* ------------------------------------------------------ rooms and PDUs --- */
/* The room's version row, plus (optionally) its id.  NULL when the room is
 * unknown or its version is not supported. */
static const roomver *room_rv(const char *room_id, char **version_out) {
    char *v = room_version_of(room_id);
    const roomver *rv = rv_get(v ? v : "");
    if (version_out && rv) *version_out = v; else free(v);
    return rv;
}

/* The remote servers an event has to be delivered to, mapped to the earliest
 * stream position at which that server appeared in the room.
 *
 * The membership that decides this is the one in force *before* the event, so
 * that a kick or a ban still reaches the server whose user it removes; a
 * membership event additionally always goes to the server of the user it names.
 * The stream position lets the sender skip a room's pre-join history for a
 * server that joined later - it was handed the state at its join.
 *
 * `before` of 0 means "the room as it stands now". */
static json *room_servers_map(const char *room_id, int64_t before) {
    json *members = j_obj();        /* user_id -> membership, as of `before` */
    json *first = j_obj();          /* server  -> first stream position seen  */
    sqlite3_stmt *st = NULL;
    const char *sql = before > 0
        ? "SELECT state_key, json, stream_id FROM events WHERE room_id=?1 AND "
          "type='m.room.member' AND is_state=1 AND stream_id<?2 "
          "ORDER BY stream_id ASC"
        : "SELECT state_key, json, stream_id FROM events WHERE room_id=?1 AND "
          "type='m.room.member' AND is_state=1 AND ?2=?2 ORDER BY stream_id ASC";
    if (sqlite3_prepare_v2(db(), sql, -1, &st, NULL) != SQLITE_OK) {
        j_free(members); j_free(first);
        return j_obj();
    }
    sqlite3_bind_text(st, 1, room_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, before);
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *uid = (const char *)sqlite3_column_text(st, 0);
        const char *text = (const char *)sqlite3_column_text(st, 1);
        int64_t sid = sqlite3_column_int64(st, 2);
        if (!uid || !text) continue;
        const char *srv = user_id_server(uid);
        if (srv && !fed_is_local_server(srv) && !j_get(first, srv))
            j_set(first, srv, j_int(sid));
        json *ev = parse_str(text, strlen(text));
        json *c = ev ? j_geto(ev, "content") : NULL;
        j_set(members, uid, j_str(c ? j_gets(c, "membership", "leave") : "leave"));
        j_free(ev);
    }
    sqlite3_finalize(st);

    json *out = j_obj();
    for (size_t i = 0; i < j_len(members); i++) {
        const char *ms = jstr(j_at(members, i));
        if (!ms || (strcmp(ms, "join") && strcmp(ms, "invite") &&
                    strcmp(ms, "knock")))
            continue;
        const char *srv = user_id_server(j_key(members, i));
        if (!srv || fed_is_local_server(srv) || j_get(out, srv)) continue;
        json *f = j_get(first, srv);
        j_set(out, srv, j_int(f ? f->i : 0));
    }
    j_free(members);
    j_free(first);
    return out;
}

json *fed_room_servers(const char *room_id) {
    json *m = room_servers_map(room_id, 0);
    json *out = j_arr();
    for (size_t i = 0; i < j_len(m); i++) j_push(out, j_str(j_key(m, i)));
    j_free(m);
    return out;
}

/* Does `server` have anyone in this room?  Used to decide whether it may read
 * the room's state and history. */
static bool server_in_room(const char *room_id, const char *server) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT user_id FROM room_members WHERE room_id=?1 AND "
            "membership IN ('join','invite','knock','leave','ban')",
            -1, &st, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, room_id, -1, SQLITE_TRANSIENT);
    bool found = false;
    while (!found && sqlite3_step(st) == SQLITE_ROW) {
        const char *uid = (const char *)sqlite3_column_text(st, 0);
        const char *srv = user_id_server(uid);
        found = srv && server && strcmp(srv, server) == 0;
    }
    sqlite3_finalize(st);
    return found;
}

/* The wire form of a stored event: this server keeps `event_id` inside the
 * stored JSON for every room version, but from room version 3 the event id is
 * the reference hash and is not carried on the wire.  Server-private keys
 * under `unsigned` are dropped too. */
static json *fed_pdu_out(const json *stored, const roomver *rv) {
    json *p = j_clone(stored);
    if (!p) return NULL;
    if (!rv || rv->event_format != 1) j_del(p, "event_id");
    json *un = j_geto(p, "unsigned");
    if (un) {
        for (;;) {
            char *victim = NULL;
            for (size_t i = 0; i < j_len(un); i++) {
                const char *k = j_key(un, i);
                if (k && strncmp(k, "__", 2) == 0) { victim = xstrdup_(k); break; }
            }
            if (!victim) break;
            j_del(un, victim);
            free(victim);
        }
        if (j_len(un) == 0) j_del(p, "unsigned");
    }
    return p;
}

static json *pdu_out_by_id(const char *event_id, const roomver *rv) {
    json *stored = event_by_id(event_id);
    if (!stored) return NULL;
    json *out = fed_pdu_out(stored, rv);
    j_free(stored);
    return out;
}

/* "Adding hashes and signatures to outgoing events": the content hash covers
 * the whole event, but the *signature* covers the redacted event, so that
 * redacting it later does not invalidate it.  mxroom.c signs the events it
 * builds the same way. */
static bool event_sign(json *ev, const roomver *rv) {
    json *red = event_redact(ev, rv);
    if (!red) return false;
    j_del(red, "unsigned");
    bool ok = json_sign(red, CFG.server_name, CFG.key_id, CFG.key_seed);
    json *sigs = ok ? j_get(red, "signatures") : NULL;
    if (sigs) j_set(ev, "signatures", j_clone(sigs));
    else ok = false;
    j_free(red);
    return ok;
}

/* Is there a signature from `server` on `obj` that verifies?
 *
 * "Validating hashes and signatures on received events" has the signature
 * cover the *redacted* event, while mxroom.c signs the event as a whole, so
 * both payloads are accepted: either one is a signature by `server` over at
 * least the redacted core of the event.  See the report. */
static bool sig_ok(const json *obj, const char *server, const roomver *rv) {
    json *sigs = j_geto((json *)obj, "signatures");
    json *mine = server ? (sigs ? j_geto(sigs, server) : NULL) : NULL;
    if (!mine) return false;
    json *redacted = rv ? event_redact(obj, rv) : NULL;
    bool ok = false;
    for (size_t i = 0; !ok && i < j_len(mine); i++) {
        const char *kid = j_key(mine, i);
        unsigned char pub[32];
        if (!fed_fetch_key(server, kid, pub)) continue;
        if (json_verify(obj, server, kid, pub)) ok = true;
        else if (redacted && json_verify(redacted, server, kid, pub)) ok = true;
    }
    j_free(redacted);
    return ok;
}

/* The server that minted a version 1 / version 2 event id, "$opaque:server". */
static char *event_id_server(const char *event_id) {
    const char *colon = event_id ? strchr(event_id, ':') : NULL;
    return colon ? xstrdup_(colon + 1) : NULL;
}

/* Basic shape checks shared by every inbound PDU. */
static bool pdu_well_formed(const json *pdu, const char **why) {
    if (!pdu || pdu->type != J_OBJECT) { *why = "PDU is not a JSON object"; return false; }
    if (!j_gets(pdu, "type", NULL))    { *why = "PDU has no type"; return false; }
    if (!j_gets(pdu, "room_id", NULL)) { *why = "PDU has no room_id"; return false; }
    const char *sender = j_gets(pdu, "sender", NULL);
    if (!sender || !valid_user_id(sender)) { *why = "PDU has no valid sender"; return false; }
    if (!j_geto((json *)pdu, "content")) { *why = "PDU has no content object"; return false; }
    json *ts = j_get((json *)pdu, "origin_server_ts");
    if (!ts || ts->type != J_INT) { *why = "PDU has no origin_server_ts"; return false; }
    if (!j_geta((json *)pdu, "prev_events")) { *why = "PDU has no prev_events"; return false; }
    if (!j_geta((json *)pdu, "auth_events") &&
        !str_eq(j_gets(pdu, "type", ""), "m.room.create")) {
        *why = "PDU has no auth_events";
        return false;
    }
    return true;
}

/* Checks the hashes and signatures of a received PDU and returns the copy that
 * should be processed - the event itself, or the redacted form when the
 * content hash does not match, as "Validating hashes and signatures on
 * received events" requires.  Stores the event id in *event_id_out. */
static json *pdu_validate(const json *pdu, const roomver *rv,
                          char **event_id_out, const char **why) {
    *why = "invalid PDU";
    if (!pdu_well_formed(pdu, why)) return NULL;

    json *work = j_clone(pdu);
    /* From room version 3 the event id is not part of the event. */
    if (rv->event_format != 1) j_del(work, "event_id");

    const char *sender = j_gets(work, "sender", "");
    char *sender_srv = xstrdup_(user_id_server(sender));
    char *idsrv = NULL;
    if (rv->event_format == 1)
        idsrv = event_id_server(j_gets(work, "event_id", NULL));

    /* "The signatures expected on an event are: the sender's server ... and,
     * for room versions 1 and 2, the server which created the event_id."
     * The `origin` field is deliberately *not* one of them: it is a legacy
     * field that a resident server puts in a make_join template and the
     * joining server is free to leave alone, so requiring a signature from it
     * would demand one from a server that never touched the event. */
    const char *want[2] = { sender_srv, idsrv };
    bool ok = true;
    for (int i = 0; ok && i < 2; i++) {
        const char *srv = want[i];
        if (!srv) continue;
        bool dup = false;
        for (int k = 0; k < i; k++)
            if (want[k] && strcmp(want[k], srv) == 0) dup = true;
        if (dup) continue;
        if (sig_ok(work, srv, rv)) continue;
        /* mxroom.c stamps `event_id` onto a stored event after hashing and
         * signing it, so a version 1 / version 2 event from this family of
         * servers verifies without that key.  See the report. */
        if (rv->event_format == 1 && j_get(work, "event_id")) {
            json *alt = j_clone(work);
            j_del(alt, "event_id");
            bool alt_ok = sig_ok(alt, srv, rv);
            j_free(alt);
            if (alt_ok) continue;
        }
        ok = false;
    }
    free(sender_srv);
    free(idsrv);
    if (!ok) {
        *why = "PDU is not correctly signed by its origin";
        j_free(work);
        return NULL;
    }

    /* Content hash.  A mismatch means we were handed a redacted copy, so the
     * redacted form is what gets processed. */
    json *hashes = j_geto(work, "hashes");
    const char *want_hash = hashes ? j_gets(hashes, "sha256", NULL) : NULL;
    char *have_hash = event_content_hash_b64(work);
    if (!want_hash || !have_hash || strcmp(want_hash, have_hash) != 0) {
        json *red = event_redact(work, rv);
        if (red) { j_free(work); work = red; }
    }
    free(have_hash);

    char *eid = event_compute_id(work, rv, j_gets(work, "origin", CFG.server_name));
    if (!eid) {
        *why = "cannot compute the event id";
        j_free(work);
        return NULL;
    }
    if (event_id_out) *event_id_out = eid; else free(eid);
    return work;
}

/* --------------------------------------------------------- persistence --- */
/* History this server imports - the state and auth chain a send_join returns,
 * a backfill, a gap fill - is not part of the live stream: a client that was
 * already syncing must not see a room's past arrive as new events.  Imported
 * events are given stream positions below zero, ascending, so that they order
 * before anything the local stream has produced while keeping their own order
 * among themselves. */
#define FED_IMPORT_BASE (-1000000000LL)

/* Reserves `n` consecutive imported positions and returns the first. */
static int64_t import_block(int64_t n) {
    int64_t v = FED_IMPORT_BASE;
    if (!db_int("SELECT value FROM fed_pos WHERE name='import'", &v, ""))
        v = FED_IMPORT_BASE;
    if (v + n >= 0) v = FED_IMPORT_BASE;          /* wrap rather than collide */
    db_run("INSERT INTO fed_pos(name,value) VALUES('import',?1) "
           "ON CONFLICT(name) DO UPDATE SET value=?1", "i", v + n);
    return v;
}

/* Writes an already validated event into the room.  The caller holds the write
 * lock.  `sid` of 0 takes the next live stream position; anything else is used
 * as given.  Returns false when the event was already stored. */
static bool fed_store_event_at(const char *room_id, const json *ev,
                               const char *event_id, bool update_state,
                               int64_t sid) {
    char *existing = db_str("SELECT event_id FROM events WHERE event_id=?1",
                            "s", event_id);
    if (existing) { free(existing); return false; }

    json *stored = j_clone(ev);
    j_set(stored, "event_id", j_str(event_id));
    if (!j_gets(stored, "room_id", NULL)) j_set(stored, "room_id", j_str(room_id));
    const char *type = j_gets(stored, "type", "");
    const char *state_key = j_gets(stored, "state_key", NULL);
    int64_t depth = j_geti(stored, "depth", 0);
    if (sid == 0) sid = stream_next();
    size_t n = 0;
    char *text = j_dump(stored, &n);
    db_run("INSERT INTO events(stream_id,event_id,room_id,type,state_key,sender,"
           "depth,origin_server_ts,json,is_state,topological) "
           "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11)",
           "isssssiisii", sid, event_id, room_id, type,
           state_key, j_gets(stored, "sender", ""), depth,
           j_geti(stored, "origin_server_ts", 0), text,
           (int64_t)(state_key ? 1 : 0), depth);
    free(text);

    if (state_key && update_state) {
        db_run("INSERT INTO room_state(room_id,type,state_key,event_id) "
               "VALUES(?1,?2,?3,?4) ON CONFLICT(room_id,type,state_key) "
               "DO UPDATE SET event_id=?4", "ssss", room_id, type, state_key,
               event_id);
        if (str_eq(type, "m.room.member")) {
            json *c = j_geto(stored, "content");
            const char *ms = c ? j_gets(c, "membership", "leave") : "leave";
            db_run("INSERT INTO room_members(room_id,user_id,membership,event_id,"
                   "stream_id) VALUES(?1,?2,?3,?4,?5) ON CONFLICT(room_id,user_id) "
                   "DO UPDATE SET membership=?3, event_id=?4, stream_id=?5",
                   "ssssi", room_id, state_key, ms, event_id, sid);
        }
    }
    j_free(stored);
    return true;
}

static bool fed_store_event(const char *room_id, const json *ev,
                            const char *event_id, bool update_state) {
    return fed_store_event_at(room_id, ev, event_id, update_state, 0);
}

/* Makes sure a `rooms` row exists for a room we learned about over federation. */
static void fed_ensure_room(const char *room_id, const char *version,
                            const char *creator) {
    char *have = room_version_of(room_id);
    if (have) { free(have); return; }
    db_run("INSERT OR IGNORE INTO rooms(room_id,version,creator,created_ts,"
           "published) VALUES(?1,?2,?3,?4,0)", "sssi", room_id,
           version ? version : rv_default(), creator, now_ms());
}

/* ------------------------------------------------------- state and auth --- */
/* The transitive closure of `seeds` over auth_events.  `include_seeds` adds the
 * seed events themselves to the result. */
static json *auth_chain_ids(const json *seeds, bool include_seeds) {
    json *queue = j_arr();
    if (include_seeds) {
        for (size_t i = 0; i < j_len(seeds); i++) {
            const char *id = jstr(j_at(seeds, i));
            if (id) arr_add_unique(queue, id);
        }
    } else {
        for (size_t i = 0; i < j_len(seeds); i++) {
            const char *id = jstr(j_at(seeds, i));
            json *ev = id ? event_by_id(id) : NULL;
            json *ae = ev ? j_geta(ev, "auth_events") : NULL;
            for (size_t k = 0; k < j_len(ae); k++) {
                const char *aid = ev_ref_id(j_at(ae, k));
                if (aid) arr_add_unique(queue, aid);
            }
            j_free(ev);
        }
    }
    json *out = j_arr();
    for (size_t i = 0; i < j_len(queue) && i < 4096; i++) {
        const char *id = jstr(j_at(queue, i));
        json *ev = id ? event_by_id(id) : NULL;
        if (!ev) continue;
        arr_add_unique(out, id);
        json *ae = j_geta(ev, "auth_events");
        for (size_t k = 0; k < j_len(ae); k++) {
            const char *aid = ev_ref_id(j_at(ae, k));
            if (aid) arr_add_unique(queue, aid);
        }
        j_free(ev);
    }
    j_free(queue);
    return out;
}

/* ======================================================================== */
/*                  the state at an event, and its resolution               */
/* ======================================================================== */
/* Two shapes of map are used throughout this section, both JSON objects:
 *
 *   a *state map*     "<type>\x1f<state_key>" -> event id
 *   an *event cache*  event id -> the stored event, or JSON null for an event
 *                     this server does not have
 *
 * "The room state S'(E) after an event E ... is S(E), except that its entry
 * corresponding to E's event_type and state_key is replaced by E's event_id",
 * and "the room state S(E) before E is the resolution of the set of states
 * {S'(E1), S'(E2), ...} consisting of the states after each of E's
 * prev_events".
 * Every S(E) this server works out is written to `fed_state`, so the walk back
 * through the DAG stops at the first event whose state is already known. */

/* The stored event `id`, borrowed from `cache`, or NULL when we do not have
 * it.  A miss is remembered as a JSON null so it is looked up only once. */
static json *sr_event(json *cache, const char *id) {
    if (!id || !*id) return NULL;
    json *v = j_get(cache, id);
    if (!v) {
        json *ev = event_by_id(id);
        j_set(cache, id, ev ? ev : j_null());
        v = j_get(cache, id);
    }
    return (v && v->type == J_OBJECT) ? v : NULL;
}

/* "<type>\x1f<state_key>" for a state event; NULL when it is not one. */
static char *sr_key_of(const json *ev) {
    const char *sk = j_gets(ev, "state_key", NULL);
    if (!sk) return NULL;
    return state_key_of(j_gets(ev, "type", ""), sk);
}

/* A state map turned into the "<type>\x1f<key>" -> event form the
 * authorisation rules read. */
static json *sr_state_events(const json *ids, json *cache) {
    json *out = j_obj();
    for (size_t i = 0; i < j_len(ids); i++) {
        const char *key = j_key(ids, i);
        const char *id = jstr(j_at(ids, i));
        json *ev = id ? sr_event(cache, id) : NULL;
        if (key && ev) j_set(out, key, j_clone(ev));
    }
    return out;
}

/* The distinct event ids of a state map, as an array. */
static json *sr_ids_of(const json *state) {
    json *out = j_arr();
    for (size_t i = 0; i < j_len(state); i++) {
        const char *id = jstr(j_at(state, i));
        if (id) arr_add_unique(out, id);
    }
    return out;
}

/* Every event reachable from `ids` through auth_events, `ids` included: the
 * "full auth chain" of a state, as an object used as a set. */
static json *sr_auth_reach(json *cache, const json *ids) {
    json *seen = j_obj();
    json *queue = j_arr();
    for (size_t i = 0; i < j_len(ids); i++) {
        const char *id = jstr(j_at(ids, i));
        if (!id || j_get(seen, id)) continue;
        j_set(seen, id, j_bool(true));
        j_push(queue, j_str(id));
    }
    for (size_t i = 0; i < j_len(queue) && i < 100000; i++) {
        const char *id = jstr(j_at(queue, i));
        json *ev = id ? sr_event(cache, id) : NULL;
        json *ae = ev ? j_geta(ev, "auth_events") : NULL;
        for (size_t k = 0; k < j_len(ae); k++) {
            const char *aid = ev_ref_id(j_at(ae, k));
            if (!aid || j_get(seen, aid)) continue;
            j_set(seen, aid, j_bool(true));
            j_push(queue, j_str(aid));
        }
    }
    j_free(queue);
    return seen;
}

/* "The auth difference is calculated by first calculating the full auth chain
 * for each state Si ... and then taking every event that doesn't appear in
 * every auth chain." */
static json *sr_auth_difference(json *cache, json *const *sets, size_t n) {
    json *diff = j_arr();
    json **reach = calloc(n ? n : 1, sizeof *reach);
    if (!reach) return diff;
    for (size_t i = 0; i < n; i++) {
        json *ids = sr_ids_of(sets[i]);
        reach[i] = sr_auth_reach(cache, ids);
        j_free(ids);
    }
    for (size_t i = 0; i < n; i++) {
        for (size_t k = 0; k < j_len(reach[i]); k++) {
            const char *id = j_key(reach[i], k);
            bool in_all = true;
            for (size_t m = 0; in_all && m < n; m++)
                if (!j_get(reach[m], id)) in_all = false;
            if (!in_all) arr_add_unique(diff, id);
        }
    }
    for (size_t i = 0; i < n; i++) j_free(reach[i]);
    free(reach);
    return diff;
}

/* "The unconflicted state map is the state where the value of each key exists
 * and is the same in each state Si.  The conflicted state set is the set of
 * all other state events." */
static void sr_separate(json *const *sets, size_t n, json **unconf_out,
                        json **conf_out) {
    json *unconf = j_obj(), *conf = j_obj(), *seen = j_obj();
    for (size_t i = 0; i < n; i++) {
        for (size_t k = 0; k < j_len(sets[i]); k++) {
            const char *key = j_key(sets[i], k);
            if (!key || j_get(seen, key)) continue;
            j_set(seen, key, j_bool(true));
            const char *first = NULL;
            bool same = true, everywhere = true;
            for (size_t m = 0; m < n; m++) {
                const char *v = j_gets(sets[m], key, NULL);
                if (!v) { everywhere = false; continue; }
                if (!first) first = v;
                else if (strcmp(first, v) != 0) same = false;
            }
            if (everywhere && same && first) {
                j_set(unconf, key, j_str(first));
                continue;
            }
            json *ids = j_arr();
            for (size_t m = 0; m < n; m++) {
                const char *v = j_gets(sets[m], key, NULL);
                if (v) arr_add_unique(ids, v);
            }
            j_set(conf, key, ids);
        }
    }
    j_free(seen);
    *unconf_out = unconf;
    *conf_out = conf;
}

/* "A power event is a state event with type m.room.power_levels or
 * m.room.join_rules, or a state event with type m.room.member where the
 * membership is leave or ban and the sender does not match the state_key."
 * The create event is one too: everything is authorised against it. */
static bool sr_is_power_event(const json *ev) {
    const char *type = j_gets(ev, "type", "");
    const char *sk = j_gets(ev, "state_key", NULL);
    if (!sk) return false;
    if (!*sk && (str_eq(type, "m.room.power_levels") ||
                 str_eq(type, "m.room.join_rules") ||
                 str_eq(type, "m.room.create")))
        return true;
    if (str_eq(type, "m.room.member")) {
        json *c = j_geto((json *)ev, "content");
        const char *ms = c ? j_gets(c, "membership", "") : "";
        if (str_eq(ms, "leave") || str_eq(ms, "ban"))
            return !str_eq(j_gets(ev, "sender", ""), sk);
    }
    return false;
}

/* The power level of an event's sender "when looking at their respective
 * auth_events" - not at any resolved state. */
static int64_t sr_sender_level(json *cache, const json *ev, const roomver *rv) {
    json *pl = NULL, *create = NULL;
    json *ae = j_geta((json *)ev, "auth_events");
    for (size_t i = 0; i < j_len(ae); i++) {
        json *a = sr_event(cache, ev_ref_id(j_at(ae, i)));
        if (!a) continue;
        const char *sk = j_gets(a, "state_key", NULL);
        if (!sk || *sk) continue;
        const char *t = j_gets(a, "type", "");
        if (str_eq(t, "m.room.power_levels")) pl = a;
        else if (str_eq(t, "m.room.create")) create = a;
    }
    const char *sender = j_gets(ev, "sender", "");
    if (pl) return pl_user_level(j_geto(pl, "content"), sender);
    if (create) {
        json *cc = j_geto(create, "content");
        const char *creator = cc ? j_gets(cc, "creator", NULL) : NULL;
        if (!creator || rv->updated_redaction_rules)
            creator = j_gets(create, "sender", NULL);
        if (creator && str_eq(creator, sender)) return 100;
    }
    return 0;
}

/* ------------------------------------- the reverse topological power order */
typedef struct {
    const char *id;         /* borrowed from the node-id array */
    int64_t pl, ts;
    json *deps;             /* the auth events of this node inside the graph */
    size_t left;            /* how many of them have not been emitted yet */
    bool done;
} pw_node;

/* "x < y if x's sender has greater power level than y's sender ...; the
 * senders have the same power level, but x's origin_server_ts is less ...;
 * ... but x's event_id is less than y's event_id." */
static int sr_power_cmp(const pw_node *a, const pw_node *b) {
    if (a->pl != b->pl) return a->pl > b->pl ? -1 : 1;
    if (a->ts != b->ts) return a->ts < b->ts ? -1 : 1;
    return strcmp(a->id, b->id);
}

/* Kahn's algorithm over the DAG the auth events form, taking the smallest
 * available vertex by the relation above at every step.  `seeds` are the power
 * events; the graph grows to their auth chains inside `full`. */
static json *sr_power_sort(json *cache, const roomver *rv, const json *seeds,
                           const json *full) {
    json *ids = j_arr(), *idx = j_obj(), *queue = j_arr();
    for (size_t i = 0; i < j_len(seeds); i++) {
        const char *s = jstr(j_at(seeds, i));
        if (s) j_push(queue, j_str(s));
    }
    for (size_t i = 0; i < j_len(queue) && i < 100000; i++) {
        const char *id = jstr(j_at(queue, i));
        if (!id || j_get(idx, id)) continue;
        j_set(idx, id, j_int((int64_t)j_len(ids)));
        j_push(ids, j_str(id));
        json *ev = sr_event(cache, id);
        json *ae = ev ? j_geta(ev, "auth_events") : NULL;
        for (size_t k = 0; k < j_len(ae); k++) {
            const char *aid = ev_ref_id(j_at(ae, k));
            if (aid && j_get((json *)full, aid) && !j_get(idx, aid))
                j_push(queue, j_str(aid));
        }
    }
    j_free(queue);

    json *out = j_arr();
    size_t n = j_len(ids);
    pw_node *nodes = n ? calloc(n, sizeof *nodes) : NULL;
    if (!nodes) { j_free(ids); j_free(idx); return out; }
    for (size_t i = 0; i < n; i++) {
        const char *id = jstr(j_at(ids, i));
        json *ev = sr_event(cache, id);
        nodes[i].id = id;
        nodes[i].ts = ev ? j_geti(ev, "origin_server_ts", 0) : 0;
        nodes[i].pl = ev ? sr_sender_level(cache, ev, rv) : 0;
        nodes[i].deps = j_arr();
        json *ae = ev ? j_geta(ev, "auth_events") : NULL;
        for (size_t k = 0; k < j_len(ae); k++) {
            const char *aid = ev_ref_id(j_at(ae, k));
            if (aid && j_get(idx, aid)) arr_add_unique(nodes[i].deps, aid);
        }
        nodes[i].left = j_len(nodes[i].deps);
    }
    for (size_t emitted = 0; emitted < n; emitted++) {
        long best = -1;
        for (size_t i = 0; i < n; i++) {
            if (nodes[i].done || nodes[i].left) continue;
            if (best < 0 || sr_power_cmp(&nodes[i], &nodes[best]) < 0)
                best = (long)i;
        }
        if (best < 0) break;                  /* a cycle: stop, do not spin */
        nodes[best].done = true;
        j_push(out, j_str(nodes[best].id));
        for (size_t i = 0; i < n; i++) {
            if (nodes[i].done || !nodes[i].left) continue;
            if (arr_has_str(nodes[i].deps, nodes[best].id)) nodes[i].left--;
        }
    }
    for (size_t i = 0; i < n; i++) j_free(nodes[i].deps);
    free(nodes);
    j_free(idx);
    j_free(ids);
    return out;
}

/* ----------------------------------------------------- mainline ordering - */
/* "the mainline of P is the list of events generated by starting with P and
 * recursively taking the m.room.power_levels events from the auth_events,
 * ordered such that P is last": the map gives each one its position, counting
 * from 1 at the far end. */
static json *sr_mainline(json *cache, const char *pl_id) {
    json *list = j_arr(), *seen = j_obj();
    const char *id = pl_id;
    while (id && !j_get(seen, id)) {
        j_set(seen, id, j_bool(true));
        j_push(list, j_str(id));
        json *ev = sr_event(cache, id);
        id = NULL;
        if (!ev) break;
        json *ae = j_geta(ev, "auth_events");
        for (size_t i = 0; i < j_len(ae); i++) {
            const char *aid = ev_ref_id(j_at(ae, i));
            json *a = aid ? sr_event(cache, aid) : NULL;
            if (a && str_eq(j_gets(a, "type", ""), "m.room.power_levels") &&
                str_eq(j_gets(a, "state_key", ""), "")) { id = aid; break; }
        }
    }
    j_free(seen);
    json *map = j_obj();
    size_t n = j_len(list);
    for (size_t i = 0; i < n; i++) {
        const char *e = jstr(j_at(list, n - 1 - i));
        if (e) j_set(map, e, j_int((int64_t)i + 1));
    }
    j_free(list);
    return map;
}

/* "the closest mainline event to e is the first event encountered in the
 * mainline when iteratively descending through the m.room.power_levels events
 * in the auth_events starting at e"; a dummy event before all of them is 0. */
static int64_t sr_mainline_depth(json *cache, const json *map, const json *ev) {
    const json *cur = ev;
    for (int guard = 0; cur && guard < 4096; guard++) {
        const char *id = j_gets(cur, "event_id", NULL);
        json *d = id ? j_get((json *)map, id) : NULL;
        if (d && d->type == J_INT) return d->i;
        json *next = NULL;
        json *ae = j_geta((json *)cur, "auth_events");
        for (size_t i = 0; i < j_len(ae); i++) {
            json *a = sr_event(cache, ev_ref_id(j_at(ae, i)));
            if (a && str_eq(j_gets(a, "type", ""), "m.room.power_levels") &&
                str_eq(j_gets(a, "state_key", ""), "")) { next = a; break; }
        }
        cur = next;
    }
    return 0;
}

typedef struct { const char *id; int64_t depth, ts; } ml_item;

static int sr_mainline_cmp(const void *a, const void *b) {
    const ml_item *x = a, *y = b;
    if (x->depth != y->depth) return x->depth < y->depth ? -1 : 1;
    if (x->ts != y->ts) return x->ts < y->ts ? -1 : 1;
    return strcmp(x->id, y->id);
}

static json *sr_mainline_order(json *cache, const char *pl_id, const json *ids) {
    json *out = j_arr();
    size_t n = j_len(ids);
    if (n == 0) return out;
    json *map = pl_id ? sr_mainline(cache, pl_id) : j_obj();
    ml_item *items = calloc(n, sizeof *items);
    if (!items) { j_free(map); return out; }
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        const char *id = jstr(j_at(ids, i));
        json *ev = id ? sr_event(cache, id) : NULL;
        if (!ev) continue;
        items[k].id = id;
        items[k].depth = sr_mainline_depth(cache, map, ev);
        items[k].ts = j_geti(ev, "origin_server_ts", 0);
        k++;
    }
    qsort(items, k, sizeof *items, sr_mainline_cmp);
    for (size_t i = 0; i < k; i++) j_push(out, j_str(items[i].id));
    free(items);
    j_free(map);
    return out;
}

/* ------------------------------------------------- iterative auth checks - */
/* "constructs a new room state by iterating through the event list and
 * applying the state event to the room state if the state event is allowed by
 * the authorization rules ... If a (event_type, state_key) key that is
 * required for checking the authorization rules is not present in the state,
 * then the appropriate state event from the event's auth_events is used." */
static json *sr_iterative_auth(json *cache, const roomver *rv, const json *base,
                               const json *order) {
    json *resolved = j_clone(base);
    for (size_t i = 0; i < j_len(order); i++) {
        const char *id = jstr(j_at(order, i));
        json *ev = id ? sr_event(cache, id) : NULL;
        if (!ev) continue;
        json *auth = j_obj();
        json *ae = j_geta(ev, "auth_events");
        for (size_t k = 0; k < j_len(ae); k++) {
            json *a = sr_event(cache, ev_ref_id(j_at(ae, k)));
            char *key = a ? sr_key_of(a) : NULL;
            if (key) { j_set(auth, key, j_clone(a)); free(key); }
        }
        json *keys = event_auth_keys(ev);
        for (size_t k = 0; k < j_len(keys); k++) {
            const char *key = jstr(j_at(keys, k));
            const char *rid = key ? j_gets(resolved, key, NULL) : NULL;
            json *re = rid ? sr_event(cache, rid) : NULL;
            if (re) j_set(auth, key, j_clone(re));
        }
        j_free(keys);
        if (event_auth_allowed(ev, auth, rv, NULL)) {
            char *key = sr_key_of(ev);
            if (key) { j_set(resolved, key, j_str(id)); free(key); }
        }
        j_free(auth);
    }
    return resolved;
}

/* ------------------------------------------- the version 2 algorithm --- */
static json *sr_resolve_v2(const roomver *rv, json *const *sets, size_t n,
                           json *cache) {
    json *unconf = NULL, *conf = NULL;
    sr_separate(sets, n, &unconf, &conf);
    if (j_len(conf) == 0) { j_free(conf); return unconf; }

    /* "The full conflicted set is the union of the conflicted state set and
     * the auth difference", restricted to events this server actually has. */
    json *full = j_obj();
    for (size_t i = 0; i < j_len(conf); i++) {
        json *list = j_at(conf, i);
        for (size_t k = 0; k < j_len(list); k++) {
            const char *id = jstr(j_at(list, k));
            if (id && sr_event(cache, id)) j_set(full, id, j_bool(true));
        }
    }
    json *diff = sr_auth_difference(cache, sets, n);
    for (size_t i = 0; i < j_len(diff); i++) {
        const char *id = jstr(j_at(diff, i));
        if (id && sr_event(cache, id)) j_set(full, id, j_bool(true));
    }
    j_free(diff);

    /* 1. the power events and their auth chains, in reverse topological power
     *    order; 2. the iterative auth checks from the unconflicted state. */
    json *power = j_arr();
    for (size_t i = 0; i < j_len(full); i++) {
        const char *id = j_key(full, i);
        json *ev = sr_event(cache, id);
        if (ev && sr_is_power_event(ev)) j_push(power, j_str(id));
    }
    json *sorted = sr_power_sort(cache, rv, power, full);
    j_free(power);
    json *partial = sr_iterative_auth(cache, rv, unconf, sorted);

    /* 3. everything left, in the mainline order of the power levels that came
     *    out of step 2; 4. the iterative auth checks again. */
    json *rest = j_arr();
    for (size_t i = 0; i < j_len(full); i++) {
        const char *id = j_key(full, i);
        if (id && !arr_has_str(sorted, id)) j_push(rest, j_str(id));
    }
    const char *plid = j_gets(partial, "m.room.power_levels" STATE_KEY_SEP, NULL);
    json *ordered = sr_mainline_order(cache, plid, rest);
    json *resolved = sr_iterative_auth(cache, rv, partial, ordered);
    j_free(ordered);
    j_free(rest);
    j_free(partial);
    j_free(sorted);
    j_free(full);
    j_free(conf);

    /* 5. "Update the result by replacing any event with the event with the
     *    same key from the unconflicted state map." */
    for (size_t i = 0; i < j_len(unconf); i++)
        j_set(resolved, j_key(unconf, i), j_clone(j_at(unconf, i)));
    j_free(unconf);
    return resolved;
}

/* ---------------------------------------------------- the version 1 one -- */
/* Room version 1 orders the events of a conflict "by ascending depth then
 * descending sha1(event_id)". */
static void sr_sha1_hex(const char *s, char out[41]) {
    unsigned char d[20];
    unsigned int len = sizeof d;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    memset(d, 0, sizeof d);
    if (ctx) {
        EVP_DigestInit_ex(ctx, EVP_sha1(), NULL);
        EVP_DigestUpdate(ctx, s, strlen(s));
        EVP_DigestFinal_ex(ctx, d, &len);
        EVP_MD_CTX_free(ctx);
    }
    for (int i = 0; i < 20; i++) snprintf(out + 2 * i, 3, "%02x", d[i]);
    out[40] = 0;
}

typedef struct { const char *id; int64_t depth; char sha[41]; } v1_item;

/* Descending depth, then ascending sha1: the order the "highest depth and
 * lowest sha1" wording picks from.  The auth passes walk it backwards. */
static int sr_v1_cmp(const void *a, const void *b) {
    const v1_item *x = a, *y = b;
    if (x->depth != y->depth) return x->depth > y->depth ? -1 : 1;
    return strcmp(x->sha, y->sha);
}

static v1_item *sr_v1_sorted(json *cache, const json *ids, size_t *out_n) {
    size_t n = j_len(ids);
    v1_item *items = n ? calloc(n, sizeof *items) : NULL;
    size_t k = 0;
    for (size_t i = 0; items && i < n; i++) {
        const char *id = jstr(j_at(ids, i));
        json *ev = id ? sr_event(cache, id) : NULL;
        if (!ev) continue;
        items[k].id = id;
        items[k].depth = j_geti(ev, "depth", 0);
        sr_sha1_hex(id, items[k].sha);
        k++;
    }
    if (items) qsort(items, k, sizeof *items, sr_v1_cmp);
    *out_n = k;
    return items;
}

/* Adds `id`'s event to the auth map under its own state key. */
static void sr_v1_put(json *auth, json *cache, const char *id) {
    json *ev = sr_event(cache, id);
    char *key = ev ? sr_key_of(ev) : NULL;
    if (key) { j_set(auth, key, j_clone(ev)); free(key); }
}

/* "Add the first event in the list to R.  For each subsequent event ... check
 * that the event would be allowed by the authorization rules for a room in
 * state R ... If it would not be allowed, stop."
 *
 * The candidates are walked against a *copy* of R.  Each conflicted key is
 * resolved against the state as it stood when the pass began, and R takes all
 * of that pass's answers at once, at the end: a member event resolved earlier
 * in the same pass is not visible to one resolved later.  That is what makes
 * room version 1 reset state the way its page warns it does -- a ban whose
 * sender's own membership is also in conflict has no sender membership to
 * authorise it, so the ban is dropped and the banned user stays in the room.
 * Threading one shared map through the pass instead quietly turns version 1's
 * algorithm into something closer to version 2's. */
static const char *sr_v1_auth_pick(json *cache, const roomver *rv,
                                   const json *ids, const json *auth) {
    size_t n = 0;
    v1_item *items = sr_v1_sorted(cache, ids, &n);
    if (!items || n == 0) { free(items); return NULL; }
    json *r = j_clone(auth);
    const char *prev = items[n - 1].id;
    for (size_t i = n - 1; i-- > 0; ) {
        sr_v1_put(r, cache, prev);
        json *ev = sr_event(cache, items[i].id);
        if (!ev || !event_auth_allowed(ev, r, rv, NULL)) break;
        prev = items[i].id;
    }
    j_free(r);
    free(items);
    return prev;
}

/* "pick the event with the highest depth and lowest sha1(event_id) that passes
 * authentication in R". */
static const char *sr_v1_normal_pick(json *cache, const roomver *rv,
                                     const json *ids, const json *auth) {
    size_t n = 0;
    v1_item *items = sr_v1_sorted(cache, ids, &n);
    if (!items || n == 0) { free(items); return NULL; }
    const char *pick = items[n - 1].id;
    for (size_t i = 0; i < n; i++) {
        json *ev = sr_event(cache, items[i].id);
        if (ev && event_auth_allowed(ev, auth, rv, NULL)) {
            pick = items[i].id;
            break;
        }
    }
    free(items);
    return pick;
}

static json *sr_resolve_v1(const roomver *rv, json *const *sets, size_t n,
                           json *cache) {
    /* Version 1 calls a key conflicting only when the states give it different
     * event ids; a key only some states carry is not a conflict. */
    json *resolved = j_obj(), *conf = j_obj(), *seen = j_obj();
    for (size_t i = 0; i < n; i++) {
        for (size_t k = 0; k < j_len(sets[i]); k++) {
            const char *key = j_key(sets[i], k);
            if (!key || j_get(seen, key)) continue;
            j_set(seen, key, j_bool(true));
            json *ids = j_arr();
            for (size_t m = 0; m < n; m++) {
                const char *v = j_gets(sets[m], key, NULL);
                if (v) arr_add_unique(ids, v);
            }
            if (j_len(ids) == 1) {
                j_set(resolved, key, j_clone(j_at(ids, 0)));
                j_free(ids);
            } else {
                j_set(conf, key, ids);
            }
        }
    }
    j_free(seen);
    if (j_len(conf) == 0) { j_free(conf); return resolved; }

    /* The state the conflicting events are checked against starts as the
     * unconflicted state, restricted to the keys that authorise them. */
    json *auth = j_obj();
    for (size_t i = 0; i < j_len(resolved); i++) {
        const char *id = jstr(j_at(resolved, i));
        json *ev = id ? sr_event(cache, id) : NULL;
        if (ev) j_set(auth, j_key(resolved, i), j_clone(ev));
    }

    static const char *PASSES[] = {"m.room.power_levels", "m.room.join_rules",
                                   "m.room.member", NULL};
    for (int p = 0; PASSES[p]; p++) {
        for (size_t i = 0; i < j_len(conf); i++) {
            const char *key = j_key(conf, i);
            if (!key || !str_prefix(key, PASSES[p])) continue;
            if (key[strlen(PASSES[p])] != STATE_KEY_SEP[0]) continue;
            const char *pick = sr_v1_auth_pick(cache, rv, j_at(conf, i), auth);
            if (pick) j_set(resolved, key, j_str(pick));
        }
        for (size_t i = 0; i < j_len(resolved); i++) {
            const char *id = jstr(j_at(resolved, i));
            json *ev = id ? sr_event(cache, id) : NULL;
            if (ev) j_set(auth, j_key(resolved, i), j_clone(ev));
        }
    }
    /* "No other events affect the authorization rules, so for all other
     * conflicts, just pick ..." */
    for (size_t i = 0; i < j_len(conf); i++) {
        const char *key = j_key(conf, i);
        if (!key || j_get(resolved, key)) continue;
        const char *pick = sr_v1_normal_pick(cache, rv, j_at(conf, i), auth);
        if (pick) j_set(resolved, key, j_str(pick));
    }
    j_free(auth);
    j_free(conf);
    return resolved;
}

/* The resolution of a set of state maps, by this room version's algorithm. */
static json *sr_resolve(const roomver *rv, json *const *sets, size_t n,
                        json *cache) {
    if (n == 0) return j_obj();
    if (n == 1) return j_clone(sets[0]);
    if (rv && rv->state_res == 1) return sr_resolve_v1(rv, sets, n, cache);
    return sr_resolve_v2(rv, sets, n, cache);
}

/* ------------------------------------------- the state at an event, stored */
static json *fed_state_row(const char *event_id) {
    char *text = db_str("SELECT state FROM fed_state WHERE event_id=?1",
                        "s", event_id);
    if (!text) return NULL;
    json *v = parse_owned(text);
    if (v && v->type == J_OBJECT) return v;
    j_free(v);
    return j_obj();
}

static void fed_state_store(const char *event_id, const json *state) {
    size_t n = 0;
    char *text = j_dump((json *)state, &n);
    if (!text) return;
    db_run("INSERT INTO fed_state(event_id,state) VALUES(?1,?2) "
           "ON CONFLICT(event_id) DO UPDATE SET state=?2", "ss", event_id, text);
    free(text);
}

/* S'(E): the stored state before `id` plus the event itself when it is a state
 * event.  NULL when this server has not worked out S(E). */
static json *fed_state_after(const char *id, json *cache) {
    json *st = fed_state_row(id);
    if (!st) return NULL;
    json *ev = sr_event(cache, id);
    char *key = ev ? sr_key_of(ev) : NULL;
    if (key) { j_set(st, key, j_str(id)); free(key); }
    return st;
}

/* The resolution of the states after `ids`.  NULL when none of them has a
 * known state, so that the caller can fall back. */
static json *fed_state_at(const roomver *rv, const json *ids, json *cache) {
    size_t n = j_len(ids);
    if (n == 0) return j_obj();
    json **sets = calloc(n, sizeof *sets);
    if (!sets) return NULL;
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        json *s = fed_state_after(jstr(j_at(ids, i)), cache);
        if (s) sets[k++] = s;
    }
    json *out = NULL;
    if (k == 1) { out = sets[0]; sets[0] = NULL; }
    else if (k > 1) out = sr_resolve(rv, sets, k, cache);
    for (size_t i = 0; i < k; i++) j_free(sets[i]);
    free(sets);
    return out;
}

typedef struct { const char *id; int64_t depth; } prime_item;

static int fed_prime_cmp(const void *a, const void *b) {
    const prime_item *x = a, *y = b;
    if (x->depth != y->depth) return x->depth < y->depth ? -1 : 1;
    return strcmp(x->id, y->id);
}

/* Works out, and stores, the state before every ancestor of `root` that does
 * not have one yet, oldest first - so that by the time an event's own state is
 * computed the states after its prev_events are all known. */
static void fed_state_prime(const roomver *rv, const char *root, json *cache) {
    if (!root) return;
    json *need = j_arr(), *seen = j_obj(), *queue = j_arr();
    j_push(queue, j_str(root));
    j_set(seen, root, j_bool(true));
    for (size_t i = 0; i < j_len(queue) && i < 100000; i++) {
        const char *id = jstr(j_at(queue, i));
        if (!id) continue;
        char *have = db_str("SELECT event_id FROM fed_state WHERE event_id=?1",
                            "s", id);
        if (have) { free(have); continue; }
        json *ev = sr_event(cache, id);
        if (!ev) continue;
        j_push(need, j_str(id));
        json *pe = j_geta(ev, "prev_events");
        for (size_t k = 0; k < j_len(pe); k++) {
            const char *pid = ev_ref_id(j_at(pe, k));
            if (!pid || j_get(seen, pid)) continue;
            j_set(seen, pid, j_bool(true));
            j_push(queue, j_str(pid));
        }
    }
    j_free(queue);
    j_free(seen);

    size_t n = j_len(need);
    prime_item *items = n ? calloc(n, sizeof *items) : NULL;
    for (size_t i = 0; items && i < n; i++) {
        items[i].id = jstr(j_at(need, i));
        json *ev = sr_event(cache, items[i].id);
        items[i].depth = ev ? j_geti(ev, "depth", 0) : 0;
    }
    if (items) qsort(items, n, sizeof *items, fed_prime_cmp);
    for (size_t i = 0; items && i < n; i++) {
        json *ev = sr_event(cache, items[i].id);
        if (!ev) continue;
        json *prevs = j_arr();
        json *pe = j_geta(ev, "prev_events");
        for (size_t k = 0; k < j_len(pe); k++) {
            const char *pid = ev_ref_id(j_at(pe, k));
            if (pid) arr_add_unique(prevs, pid);
        }
        json *st = fed_state_at(rv, prevs, cache);
        if (st) { fed_state_store(items[i].id, st); j_free(st); }
        j_free(prevs);
    }
    free(items);
    j_free(need);
}

/* The state at `ev`'s own prev_events, as a state map, or NULL. */
static json *fed_state_at_prevs(const roomver *rv, const json *ev, json *cache) {
    json *prevs = j_arr();
    json *pe = j_geta((json *)ev, "prev_events");
    for (size_t i = 0; i < j_len(pe); i++) {
        const char *pid = ev_ref_id(j_at(pe, i));
        if (pid) arr_add_unique(prevs, pid);
    }
    for (size_t i = 0; i < j_len(prevs); i++)
        fed_state_prime(rv, jstr(j_at(prevs, i)), cache);
    json *st = fed_state_at(rv, prevs, cache);
    j_free(prevs);
    return st;
}

/* ----------------------------------------------- forward extremities ----- */
/* The events in the room's live timeline that nothing else in it points back
 * at.  A soft failed event "should not ... be referenced by new events created
 * by the homeserver", so it is not one.  Imported history - a send_join's
 * state and auth chain, a backfill - sits at negative stream positions and is
 * not part of the timeline at all. */
static json *fed_extremities(const char *room_id) {
    json *live = j_arr(), *refd = j_obj();
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT event_id, json FROM events WHERE room_id=?1 AND stream_id>0 "
            "ORDER BY stream_id", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, room_id, -1, SQLITE_TRANSIENT);
        while (sqlite3_step(st) == SQLITE_ROW) {
            const char *id = (const char *)sqlite3_column_text(st, 0);
            const char *text = (const char *)sqlite3_column_text(st, 1);
            if (id) j_push(live, j_str(id));
            json *ev = text ? parse_str(text, strlen(text)) : NULL;
            json *pe = ev ? j_geta(ev, "prev_events") : NULL;
            for (size_t i = 0; i < j_len(pe); i++) {
                const char *pid = ev_ref_id(j_at(pe, i));
                if (pid) j_set(refd, pid, j_bool(true));
            }
            j_free(ev);
        }
        sqlite3_finalize(st);
    }
    json *out = j_arr();
    for (int pass = 0; pass < 2 && j_len(out) == 0; pass++) {
        for (size_t i = 0; i < j_len(live); i++) {
            const char *id = jstr(j_at(live, i));
            if (!id || j_get(refd, id)) continue;
            if (pass == 0) {
                char *soft = db_str("SELECT event_id FROM fed_soft WHERE "
                                    "event_id=?1", "s", id);
                if (soft) { free(soft); continue; }
            }
            j_push(out, j_str(id));
        }
    }
    j_free(live);
    j_free(refd);
    return out;
}

/* Writes one state event into the room's current state. */
static void fed_apply_state(const char *room_id, const char *type,
                            const char *state_key, const char *event_id,
                            const json *ev) {
    db_run("INSERT INTO room_state(room_id,type,state_key,event_id) "
           "VALUES(?1,?2,?3,?4) ON CONFLICT(room_id,type,state_key) "
           "DO UPDATE SET event_id=?4", "ssss", room_id, type, state_key,
           event_id);
    if (!str_eq(type, "m.room.member")) return;
    json *c = ev ? j_geto((json *)ev, "content") : NULL;
    const char *ms = c ? j_gets(c, "membership", "leave") : "leave";
    int64_t sid = 0, topo = 0;
    event_get_stream(event_id, &sid, &topo);
    db_run("INSERT INTO room_members(room_id,user_id,membership,event_id,"
           "stream_id) VALUES(?1,?2,?3,?4,?5) ON CONFLICT(room_id,user_id) "
           "DO UPDATE SET membership=?3, event_id=?4, stream_id=?5",
           "ssssi", room_id, state_key, ms, event_id, sid);
}

/* "the resolved state across all forward extremities" - the room's current
 * state.  Returns false when it could not be worked out, so the caller can
 * fall back to folding the new event in on its own. */
static bool fed_refresh_room_state(const char *room_id, const roomver *rv) {
    json *tips = fed_extremities(room_id);
    if (j_len(tips) == 0) { j_free(tips); return false; }
    json *cache = j_obj();
    for (size_t i = 0; i < j_len(tips); i++)
        fed_state_prime(rv, jstr(j_at(tips, i)), cache);
    json *state = fed_state_at(rv, tips, cache);
    j_free(tips);
    bool ok = state && j_get(state, "m.room.create" STATE_KEY_SEP) != NULL;
    if (ok) {
        /* One transaction, so that a reader never catches the room between the
         * old state going and the new state arriving. */
        bool txn = sqlite3_get_autocommit(db()) != 0 && db_begin();
        db_run("DELETE FROM room_state WHERE room_id=?1", "s", room_id);
        for (size_t i = 0; i < j_len(state); i++) {
            const char *key = j_key(state, i);
            const char *id = jstr(j_at(state, i));
            const char *sep = key ? strchr(key, STATE_KEY_SEP[0]) : NULL;
            if (!id || !sep) continue;
            char *type = str_fmt("%.*s", (int)(sep - key), key);
            fed_apply_state(room_id, type, sep + 1, id, sr_event(cache, id));
            free(type);
        }
        if (txn) db_commit();
    }
    j_free(state);
    j_free(cache);
    return ok;
}

/* The state of the room before `event_id`, as a map of "<type>\x1f<key>" to
 * the state event itself: the resolution of the states after its prev_events.
 * Falls back to the room's linear history for an event whose ancestry this
 * server never saw. */
static json *state_before_event(const char *room_id, const char *event_id) {
    int64_t sid = 0, topo = 0;
    if (!event_get_stream(event_id, &sid, &topo)) return NULL;
    const roomver *rv = room_rv(room_id, NULL);
    json *ev = rv ? event_by_id(event_id) : NULL;
    json *out = NULL;
    if (ev) {
        json *cache = j_obj();
        hs_lock();
        json *ids = fed_state_at_prevs(rv, ev, cache);
        hs_unlock();
        if (ids) { out = sr_state_events(ids, cache); j_free(ids); }
        j_free(cache);
        j_free(ev);
    }
    return out ? out : room_state_at(room_id, sid - 1);
}

/* The event ids of a state map, as a JSON array. */
static json *state_map_ids(const json *state) {
    json *out = j_arr();
    for (size_t i = 0; i < j_len(state); i++) {
        const char *id = j_gets(j_at(state, i), "event_id", NULL);
        if (id) arr_add_unique(out, id);
    }
    return out;
}

/* The stored events named by an array of ids, in wire form. */
static json *pdus_for_ids(const json *ids, const roomver *rv) {
    json *out = j_arr();
    for (size_t i = 0; i < j_len(ids); i++) {
        const char *id = jstr(j_at(ids, i));
        json *p = id ? pdu_out_by_id(id, rv) : NULL;
        if (p) j_push(out, p);
    }
    return out;
}

/* Defined with the outbound handshakes: validates a batch of PDUs and returns
 * [{event_id, event}, ...] in depth order, dropping any that cannot be
 * verified. */
static json *validate_bundle(const json *arr, const roomver *rv);

/* Stores a batch of historical PDUs at imported positions, oldest first.
 * Returns how many were new. */
static int import_pdus(const char *room_id, const roomver *rv, const json *pdus) {
    json *items = validate_bundle(pdus, rv);          /* validated, depth order */
    int stored = 0;
    if (j_len(items)) {
        hs_lock();
        int64_t sid = import_block((int64_t)j_len(items));
        for (size_t i = 0; i < j_len(items); i++) {
            json *item = j_at(items, i);
            if (fed_store_event_at(room_id, j_geto(item, "event"),
                                   j_gets(item, "event_id", ""), false, sid)) {
                sid++;
                stored++;
            }
        }
        hs_unlock();
        if (stored) stream_notify();
    }
    j_free(items);
    return stored;
}

/* GET /backfill on `dest` for the events at and before `ids`, stored as
 * history.  Returns how many new events arrived. */
static int backfill_from(const char *dest, const char *room_id,
                         const roomver *rv, const json *ids, int64_t limit) {
    if (!dest || j_len(ids) == 0) return 0;
    char *enc_room = url_encode(room_id);
    buf q = {0};
    buf_printf(&q, "%s/backfill/%s?limit=%lld", FED1, enc_room, (long long)limit);
    for (size_t i = 0; i < j_len(ids) && i < 10; i++) {
        const char *id = jstr(j_at(ids, i));
        if (!id) continue;
        char *e = url_encode(id);
        buf_printf(&q, "&v=%s", e);
        free(e);
    }
    free(enc_room);
    char *path = buf_take(&q, NULL);
    int st = 0;
    json *resp = fed_request_json(dest, "GET", path, NULL, &st);
    free(path);
    int n = 0;
    if (st == 200 && resp) n = import_pdus(room_id, rv, j_geta(resp, "pdus"));
    j_free(resp);
    return n;
}

/* The prev_events of `ev` this server does not have. */
static json *missing_parents(const json *ev) {
    json *out = j_arr();
    json *pe = j_geta((json *)ev, "prev_events");
    for (size_t i = 0; i < j_len(pe); i++) {
        const char *pid = ev_ref_id(j_at(pe, i));
        if (!pid) continue;
        char *have = db_str("SELECT event_id FROM events WHERE event_id=?1",
                            "s", pid);
        if (have) { free(have); continue; }
        arr_add_unique(out, pid);
    }
    return out;
}

/* "Backfilling and retrieving missing events": an event whose parents this
 * server has never seen leaves a hole in the room, so the hole is filled from
 * the server that sent it before the event is accepted. */
static void fill_gap(const char *origin, const char *room_id,
                     const roomver *rv, const json *ev) {
    for (int round = 0; round < 3; round++) {
        json *missing = missing_parents(ev);
        size_t n = j_len(missing);
        if (n == 0) { j_free(missing); return; }
        int got = backfill_from(origin, room_id, rv, missing, 50);
        j_free(missing);
        if (got == 0) return;
    }
}

/* ----------------------------------------------------- inbound PDUs ------ */
/* Runs the "Checks performed on receipt of a PDU" list over one event and, if
 * it survives them, persists it.  Returns NULL on success or a malloc'd error
 * string describing why the event was rejected. */
static char *fed_process_pdu(const char *origin, const json *pdu) {
    const char *room_id = j_gets(pdu, "room_id", NULL);
    if (!room_id) return xstrdup_("PDU has no room_id");

    char *version = NULL;
    const roomver *rv = room_rv(room_id, &version);
    if (!rv) {
        free(version);
        char *v = room_version_of(room_id);
        char *msg = v ? str_fmt("Unsupported room version %s", v)
                      : xstrdup_("Unknown room");
        free(v);
        return msg;
    }
    free(version);

    char *eid = NULL;
    const char *why = NULL;
    json *ev = pdu_validate(pdu, rv, &eid, &why);
    if (!ev) return xstrdup_(why ? why : "invalid PDU");

    json *known = event_by_id(eid);
    if (known) {                       /* already in the graph: not an error */
        j_free(known); j_free(ev); free(eid);
        return NULL;
    }
    if (origin) fill_gap(origin, room_id, rv, ev);

    char *err = NULL;
    hs_lock();
    json *cache = j_obj();
    const char *deny = NULL;

    /* 4. "Passes authorization rules based on the event's auth events,
     *     otherwise it is rejected." */
    json *declared = j_obj();
    json *ae = j_geta(ev, "auth_events");
    for (size_t i = 0; i < j_len(ae); i++) {
        json *a = sr_event(cache, ev_ref_id(j_at(ae, i)));
        char *k = a ? sr_key_of(a) : NULL;
        if (k) { j_set(declared, k, j_clone(a)); free(k); }
    }
    bool ok = event_auth_allowed(ev, declared, rv, &deny);
    j_free(declared);

    /* 5. "Passes authorization rules based on the state at the event,
     *     otherwise it is rejected."  That is the resolution of the states
     *     after the event's own prev_events - not the room's current state,
     *     which for an event that arrives on the far side of a fork belongs
     *     to the other branch entirely. */
    json *at = NULL;
    if (ok) {
        at = fed_state_at_prevs(rv, ev, cache);
        json *before = at ? sr_state_events(at, cache) : room_state_map(room_id);
        ok = event_auth_allowed(ev, before, rv, &deny);
        j_free(before);
    }

    if (!ok) {
        err = str_fmt("Event rejected by the authorization rules: %s",
                      deny ? deny : "not allowed");
    } else {
        fed_store_event(room_id, ev, eid, false);
        if (at) fed_state_store(eid, at);
        /* 6. "If the event does not pass the auth checks based on the current
         *     state of the room (but does pass the auth checks based on the
         *     state at that event) it should be soft failed", and so must not
         *     become a forward extremity. */
        json *current = room_state_map(room_id);
        if (!event_auth_allowed(ev, current, rv, NULL))
            db_run("INSERT OR IGNORE INTO fed_soft(event_id) VALUES(?1)",
                   "s", eid);
        j_free(current);
        if (!fed_refresh_room_state(room_id, rv)) {
            const char *sk = j_gets(ev, "state_key", NULL);
            if (sk)
                fed_apply_state(room_id, j_gets(ev, "type", ""), sk, eid, ev);
        }
    }
    j_free(at);
    j_free(cache);
    hs_unlock();

    j_free(ev);
    free(eid);
    if (!err) { stream_notify(); fed_wake(); }
    return err;
}

/* ----------------------------------------------------- inbound EDUs ------ */
static void edu_typing(const char *origin, const json *c) {
    const char *room_id = j_gets(c, "room_id", NULL);
    const char *user_id = j_gets(c, "user_id", NULL);
    if (!room_id || !user_id) return;
    const char *srv = user_id_server(user_id);
    if (!srv || !str_eq(srv, origin)) return;      /* only your own users */
    char *ms = room_membership(room_id, user_id);
    bool joined = ms && str_eq(ms, "join");
    free(ms);
    if (!joined) return;
    hs_lock();
    int64_t sid = stream_next();
    int64_t expires = j_getb(c, "typing", false) ? now_ms() + 30000 : 0;
    db_run("INSERT OR REPLACE INTO typing(room_id,user_id,expires_ms,stream_id) "
           "VALUES(?1,?2,?3,?4)", "ssii", room_id, user_id, expires, sid);
    hs_unlock();
    stream_notify();
}

static void edu_receipt(const char *origin, const json *c) {
    hs_lock();
    for (size_t i = 0; i < j_len(c); i++) {
        const char *room_id = j_key(c, i);
        json *room = j_at(c, i);
        for (size_t t = 0; room && t < j_len(room); t++) {
            const char *rtype = j_key(room, t);
            json *users = j_at(room, t);
            for (size_t u = 0; users && u < j_len(users); u++) {
                const char *user_id = j_key(users, u);
                json *rec = j_at(users, u);
                const char *srv = user_id_server(user_id);
                if (!srv || !str_eq(srv, origin)) continue;
                json *ids = rec ? j_geta(rec, "event_ids") : NULL;
                const char *eid = jstr(j_at(ids, 0));
                if (!eid) continue;
                json *data = j_geto(rec, "data");
                int64_t ts = data ? j_geti(data, "ts", now_ms()) : now_ms();
                int64_t sid = stream_next();
                db_run("INSERT OR REPLACE INTO receipts(room_id,user_id,"
                       "receipt_type,thread_id,event_id,ts,stream_id) "
                       "VALUES(?1,?2,?3,'',?4,?5,?6)", "ssssii",
                       room_id, user_id, rtype, eid, ts, sid);
            }
        }
    }
    hs_unlock();
    stream_notify();
}

static void edu_presence(const char *origin, const json *c) {
    if (!CFG.presence_in) return;
    json *push = j_geta((json *)c, "push");
    if (!push) return;
    hs_lock();
    for (size_t i = 0; i < j_len(push); i++) {
        json *p = j_at(push, i);
        const char *user_id = j_gets(p, "user_id", NULL);
        const char *pres = j_gets(p, "presence", NULL);
        if (!user_id || !pres) continue;
        const char *srv = user_id_server(user_id);
        if (!srv || !str_eq(srv, origin)) continue;
        int64_t ago = j_geti(p, "last_active_ago", 0);
        int64_t sid = stream_next();
        db_run("INSERT OR REPLACE INTO presence(user_id,presence,status_msg,"
               "last_active_ts,stream_id) VALUES(?1,?2,?3,?4,?5)", "sssii",
               user_id, pres, j_gets(p, "status_msg", NULL),
               now_ms() - (ago > 0 ? ago : 0), sid);
    }
    hs_unlock();
    stream_notify();
}

/* Stored so that /user/devices and the end-to-end key endpoints can use it
 * later; nothing in this server acts on it yet. */
static void edu_device_list(const char *origin, const json *c) {
    const char *user_id = j_gets(c, "user_id", NULL);
    const char *device_id = j_gets(c, "device_id", NULL);
    if (!user_id || !device_id) return;
    const char *srv = user_id_server(user_id);
    if (!srv || !str_eq(srv, origin)) return;
    json *keys = j_geto((json *)c, "keys");
    size_t n = 0;
    char *ktext = keys ? j_dump(keys, &n) : NULL;
    bool deleted = j_getb(c, "deleted", false);
    hs_lock();
    db_run("INSERT INTO fed_devices(user_id,device_id,display_name,stream_id,"
           "keys,deleted) VALUES(?1,?2,?3,?4,?5,?6) "
           "ON CONFLICT(user_id,device_id) DO UPDATE SET display_name=?3,"
           "stream_id=?4, keys=?5, deleted=?6", "sssisi", user_id, device_id,
           j_gets(c, "device_display_name", NULL), j_geti(c, "stream_id", 0),
           ktext, (int64_t)(deleted ? 1 : 0));
    hs_unlock();
    free(ktext);
}

static void fed_process_edu(const char *origin, const json *edu) {
    if (!edu || edu->type != J_OBJECT) return;
    const char *type = j_gets(edu, "edu_type", NULL);
    json *c = j_geto((json *)edu, "content");
    if (!type || !c) return;
    /* The end-to-end-encryption module owns m.device_list_update,
     * m.signing_key_update and m.direct_to_device. */
    if (keys_handle_edu(origin, edu)) return;
    if (str_eq(type, "m.typing")) edu_typing(origin, c);
    else if (str_eq(type, "m.receipt")) edu_receipt(origin, c);
    else if (str_eq(type, "m.presence")) edu_presence(origin, c);
    else if (str_eq(type, "m.device_list_update")) edu_device_list(origin, c);
    /* Anything else is accepted and ignored, as the specification allows. */
}

/* --------------------------------- PUT /_matrix/federation/v1/send/{txn} -- */
static void h_send_txn(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    char *origin = NULL;
    if (!fed_authenticate(r, w, &origin)) return;
    const char *txn_id = r->part[0];

    json *body = http_json(r);
    if (!body || body->type != J_OBJECT) {
        resp_error(w, 400, "M_BAD_JSON", "Transaction body must be a JSON object");
        free(origin);
        return;
    }
    json *pdus = j_get(body, "pdus");
    json *edus = j_get(body, "edus");
    json *ts = j_get(body, "origin_server_ts");
    const char *body_origin = j_gets(body, "origin", NULL);
    if (!pdus || pdus->type != J_ARRAY || !ts || ts->type != J_INT ||
        !body_origin || (edus && edus->type != J_ARRAY)) {
        resp_error(w, 400, "M_BAD_JSON",
                   "Transaction must carry origin, origin_server_ts and pdus");
        free(origin);
        return;
    }
    if (j_len(pdus) > MAX_PDUS_PER_TXN) {
        resp_error(w, 400, "M_BAD_JSON", "Transactions carry at most 50 PDUs");
        free(origin);
        return;
    }
    if (edus && j_len(edus) > MAX_EDUS_PER_TXN) {
        resp_error(w, 400, "M_BAD_JSON", "Transactions carry at most 100 EDUs");
        free(origin);
        return;
    }
    if (!str_eq(body_origin, origin)) {
        resp_error(w, 401, "M_UNAUTHORIZED",
                   "Transaction origin does not match the signing server");
        free(origin);
        return;
    }

    /* "The sending server must wait and retry for a 200 OK response before
     * sending a transaction with a different txnId": replaying one must be
     * free of side effects. */
    json *cached = parse_owned(db_str("SELECT response FROM fed_inbound_txns "
                                      "WHERE origin=?1 AND txn_id=?2",
                                      "ss", origin, txn_id));
    if (cached) { resp_json(w, 200, cached); free(origin); return; }

    json *results = j_obj();
    for (size_t i = 0; i < j_len(pdus); i++) {
        json *pdu = j_at(pdus, i);
        char *err = fed_process_pdu(origin, pdu);
        /* The event id has to be reported even for events we threw away. */
        char *eid = NULL;
        const char *rid = j_gets(pdu, "room_id", NULL);
        char *ver = rid ? room_version_of(rid) : NULL;
        const roomver *rv = rv_get(ver ? ver : rv_default());
        free(ver);
        if (rv) {
            json *tmp = j_clone(pdu);
            if (rv->event_format != 1) j_del(tmp, "event_id");
            eid = event_compute_id(tmp, rv, j_gets(pdu, "origin", CFG.server_name));
            j_free(tmp);
        }
        if (!eid) eid = xstrdup_(j_gets(pdu, "event_id", "$unknown"));
        json *one = j_obj();
        if (err) { j_set(one, "error", j_str(err)); free(err); }
        j_set(results, eid, one);
        free(eid);
    }
    for (size_t i = 0; edus && i < j_len(edus); i++)
        fed_process_edu(origin, j_at(edus, i));

    json *out = j_obj();
    j_set(out, "pdus", results);
    size_t n = 0;
    char *text = j_dump(out, &n);
    hs_lock();
    db_run("INSERT OR REPLACE INTO fed_inbound_txns(origin,txn_id,response,"
           "received_ts) VALUES(?1,?2,?3,?4)", "sssi", origin, txn_id, text,
           now_ms());
    hs_unlock();
    free(text);
    resp_json(w, 200, out);
    free(origin);
}

/* ======================================================================== */
/*                          inbound: the key server                         */
/* ======================================================================== */

/* GET /_matrix/key/v2/server[/{keyId}] - unauthenticated by definition. */
static void h_key_server(http_req *r, http_resp *w, void *ud) {
    (void)r; (void)ud;
    resp_json(w, 200, fed_local_keys());
}

/* A queried server's document, counter-signed by this (notary) server. */
static json *notary_doc(const char *server_name, int64_t min_valid_until) {
    json *doc = fed_key_doc(server_name);
    if (!doc) return NULL;
    if (min_valid_until > 0 && j_geti(doc, "valid_until_ts", 0) < min_valid_until) {
        /* The cached copy is too old for the caller: try a live fetch. */
        json *fresh = fed_is_local_server(server_name) ? NULL
                                                       : key_doc_fetch(server_name);
        if (fresh) { j_free(doc); doc = fresh; }
    }
    json_sign(doc, CFG.server_name, CFG.key_id, CFG.key_seed);
    return doc;
}

/* GET /_matrix/key/v2/query/{serverName}[/{keyId}] */
static void h_key_query_get(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    const char *server_name = r->part[0];
    const char *mv = http_query(r, "minimum_valid_until_ts");
    int64_t min_valid = mv ? strtoll(mv, NULL, 10) : 0;
    json *arr = j_arr();
    json *doc = notary_doc(server_name, min_valid);
    if (doc) j_push(arr, doc);
    json *out = j_obj();
    j_set(out, "server_keys", arr);
    resp_json(w, 200, out);
}

/* POST /_matrix/key/v2/query */
static void h_key_query_post(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    json *body = http_json(r);
    if (!body || body->type != J_OBJECT) {
        resp_error(w, 400, "M_BAD_JSON", "Request body must be a JSON object");
        return;
    }
    json *want = j_geto(body, "server_keys");
    json *arr = j_arr();
    for (size_t i = 0; want && i < j_len(want); i++) {
        const char *server_name = j_key(want, i);
        json *criteria = j_at(want, i);
        int64_t min_valid = 0;
        for (size_t k = 0; criteria && k < j_len(criteria); k++) {
            json *one = j_at(criteria, k);
            int64_t v = one ? j_geti(one, "minimum_valid_until_ts", 0) : 0;
            if (v > min_valid) min_valid = v;
        }
        json *doc = notary_doc(server_name, min_valid);
        if (doc) j_push(arr, doc);
    }
    json *out = j_obj();
    j_set(out, "server_keys", arr);
    resp_json(w, 200, out);
}

/* GET /_matrix/federation/v1/version - unauthenticated. */
static void h_version(http_req *r, http_resp *w, void *ud) {
    (void)r; (void)ud;
    json *srv = j_obj();
    j_set(srv, "name", j_str("dendrite"));
    j_set(srv, "version", j_str("0.13.8"));
    json *out = j_obj();
    j_set(out, "server", srv);
    resp_json(w, 200, out);
}

/* GET /.well-known/matrix/server */
static void h_wellknown(http_req *r, http_resp *w, void *ud) {
    (void)r; (void)ud;
    json *out = j_obj();
    j_set(out, "m.server", j_str(CFG.server_name));
    resp_json(w, 200, out);
}

/* ======================================================================== */
/*                    inbound: events, state and history                    */
/* ======================================================================== */

/* Common preamble: authenticate, look the room up, and check that the calling
 * server has a reason to see it. */
static bool room_request(http_req *r, http_resp *w, const char *room_id,
                         char **origin_out, const roomver **rv_out) {
    char *origin = NULL;
    if (!fed_authenticate(r, w, &origin)) return false;
    const roomver *rv = room_rv(room_id, NULL);
    if (!rv) {
        resp_error(w, 404, "M_NOT_FOUND", "Unknown room");
        free(origin);
        return false;
    }
    if (!server_in_room(room_id, origin)) {
        resp_error(w, 403, "M_FORBIDDEN", "Your server is not in this room");
        free(origin);
        return false;
    }
    *origin_out = origin;
    *rv_out = rv;
    return true;
}

/* GET /_matrix/federation/v1/event/{eventId} */
static void h_get_event(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    char *origin = NULL;
    if (!fed_authenticate(r, w, &origin)) return;
    const char *event_id = r->part[0];
    char *room_id = event_room(event_id);
    if (!room_id) {
        resp_error(w, 404, "M_NOT_FOUND", "Unknown event");
        free(origin);
        return;
    }
    if (!server_in_room(room_id, origin)) {
        resp_error(w, 403, "M_FORBIDDEN", "Your server is not in this room");
        free(room_id); free(origin);
        return;
    }
    const roomver *rv = room_rv(room_id, NULL);
    json *pdu = pdu_out_by_id(event_id, rv);
    json *out = j_obj();
    jset_str(out, "origin", CFG.server_name);
    j_set(out, "origin_server_ts", j_int(now_ms()));
    json *arr = j_arr();
    if (pdu) j_push(arr, pdu);
    j_set(out, "pdus", arr);
    resp_json(w, 200, out);
    free(room_id);
    free(origin);
}

/* GET /_matrix/federation/v1/state/{roomId}?event_id= */
static void h_get_state(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    const char *room_id = r->part[0];
    char *origin = NULL;
    const roomver *rv = NULL;
    if (!room_request(r, w, room_id, &origin, &rv)) return;
    const char *event_id = http_query(r, "event_id");
    if (!event_id) {
        resp_error(w, 400, "M_MISSING_PARAM", "event_id is required");
        free(origin);
        return;
    }
    json *state = state_before_event(room_id, event_id);
    if (!state) {
        resp_error(w, 404, "M_NOT_FOUND", "Unknown event");
        free(origin);
        return;
    }
    json *ids = state_map_ids(state);
    json *chain = auth_chain_ids(ids, true);
    json *out = j_obj();
    j_set(out, "pdus", pdus_for_ids(ids, rv));
    j_set(out, "auth_chain", pdus_for_ids(chain, rv));
    j_free(ids);
    j_free(chain);
    j_free(state);
    resp_json(w, 200, out);
    free(origin);
}

/* GET /_matrix/federation/v1/state_ids/{roomId}?event_id= */
static void h_get_state_ids(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    const char *room_id = r->part[0];
    char *origin = NULL;
    const roomver *rv = NULL;
    if (!room_request(r, w, room_id, &origin, &rv)) return;
    const char *event_id = http_query(r, "event_id");
    if (!event_id) {
        resp_error(w, 400, "M_MISSING_PARAM", "event_id is required");
        free(origin);
        return;
    }
    json *state = state_before_event(room_id, event_id);
    if (!state) {
        resp_error(w, 404, "M_NOT_FOUND", "Unknown event");
        free(origin);
        return;
    }
    json *ids = state_map_ids(state);
    json *chain = auth_chain_ids(ids, true);
    json *out = j_obj();
    j_set(out, "pdu_ids", j_clone(ids));
    j_set(out, "auth_chain_ids", j_clone(chain));
    j_free(ids);
    j_free(chain);
    j_free(state);
    resp_json(w, 200, out);
    free(origin);
}

/* GET /_matrix/federation/v1/event_auth/{roomId}/{eventId} */
static void h_event_auth(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    const char *room_id = r->part[0], *event_id = r->part[1];
    char *origin = NULL;
    const roomver *rv = NULL;
    if (!room_request(r, w, room_id, &origin, &rv)) return;
    json *ev = event_by_id(event_id);
    if (!ev) {
        resp_error(w, 404, "M_NOT_FOUND", "Unknown event");
        free(origin);
        return;
    }
    json *seeds = j_arr();
    j_push(seeds, j_str(event_id));
    j_free(ev);
    json *chain = auth_chain_ids(seeds, false);
    json *out = j_obj();
    j_set(out, "auth_chain", pdus_for_ids(chain, rv));
    j_free(seeds);
    j_free(chain);
    resp_json(w, 200, out);
    free(origin);
}

/* GET /_matrix/federation/v1/backfill/{roomId}?v=&limit= */
static void h_backfill(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    const char *room_id = r->part[0];
    char *origin = NULL;
    const roomver *rv = NULL;
    if (!room_request(r, w, room_id, &origin, &rv)) return;

    const char *vs[32];
    int nv = http_query_all(r, "v", vs, 32);
    const char *limit_s = http_query(r, "limit");
    int64_t limit = limit_s ? strtoll(limit_s, NULL, 10) : 10;
    if (limit <= 0 || limit > 100) limit = 100;

    int64_t topo = -1, stream = -1;
    for (int i = 0; i < nv; i++) {
        int64_t t = 0, s = 0;
        if (!event_get_stream(vs[i], &s, &t)) continue;
        if (t > topo || (t == topo && s > stream)) { topo = t; stream = s; }
    }
    if (topo < 0) {     /* no usable start point: begin at the room's tip */
        db_int("SELECT COALESCE(MAX(topological),0) FROM events WHERE room_id=?1",
               &topo, "s", room_id);
        stream = room_latest_stream(room_id);
    }

    json *pdus = j_arr();
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT json FROM events WHERE room_id=?1 AND "
            "(topological < ?2 OR (topological = ?2 AND stream_id <= ?3)) "
            "ORDER BY topological DESC, stream_id DESC LIMIT ?4",
            -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, room_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 2, topo);
        sqlite3_bind_int64(st, 3, stream);
        sqlite3_bind_int64(st, 4, limit);
        while (sqlite3_step(st) == SQLITE_ROW) {
            const char *text = (const char *)sqlite3_column_text(st, 0);
            json *stored = text ? parse_str(text, strlen(text)) : NULL;
            if (!stored) continue;
            json *pdu = fed_pdu_out(stored, rv);
            j_free(stored);
            if (pdu) j_push(pdus, pdu);
        }
        sqlite3_finalize(st);
    }
    json *out = j_obj();
    jset_str(out, "origin", CFG.server_name);
    j_set(out, "origin_server_ts", j_int(now_ms()));
    j_set(out, "pdus", pdus);
    resp_json(w, 200, out);
    free(origin);
}

/* POST /_matrix/federation/v1/get_missing_events/{roomId} */
static void h_get_missing(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    const char *room_id = r->part[0];
    char *origin = NULL;
    const roomver *rv = NULL;
    if (!room_request(r, w, room_id, &origin, &rv)) return;
    json *body = http_json(r);
    if (!body || body->type != J_OBJECT) {
        resp_error(w, 400, "M_BAD_JSON", "Request body must be a JSON object");
        free(origin);
        return;
    }
    json *latest = j_geta(body, "latest_events");
    json *earliest = j_geta(body, "earliest_events");
    if (!latest) {
        resp_error(w, 400, "M_BAD_JSON", "latest_events is required");
        free(origin);
        return;
    }
    int64_t limit = j_geti(body, "limit", 10);
    if (limit <= 0 || limit > 100) limit = 100;
    int64_t min_depth = j_geti(body, "min_depth", 0);

    /* A breadth-first walk of prev_events, skipping what the caller has. */
    json *queue = j_arr();
    for (size_t i = 0; i < j_len(latest); i++) {
        const char *id = jstr(j_at(latest, i));
        json *ev = id ? event_by_id(id) : NULL;
        json *pe = ev ? j_geta(ev, "prev_events") : NULL;
        for (size_t k = 0; k < j_len(pe); k++) {
            const char *pid = ev_ref_id(j_at(pe, k));
            if (pid && !arr_has_str(earliest, pid)) arr_add_unique(queue, pid);
        }
        j_free(ev);
    }
    json *events = j_arr();
    for (size_t i = 0; i < j_len(queue) && (int64_t)j_len(events) < limit; i++) {
        const char *id = jstr(j_at(queue, i));
        json *ev = id ? event_by_id(id) : NULL;
        if (!ev) continue;
        if (j_geti(ev, "depth", 0) < min_depth) { j_free(ev); continue; }
        char *erid = event_room(id);
        bool same = erid && str_eq(erid, room_id);
        free(erid);
        if (!same) { j_free(ev); continue; }
        json *pdu = fed_pdu_out(ev, rv);
        if (pdu) j_push(events, pdu);
        json *pe = j_geta(ev, "prev_events");
        for (size_t k = 0; k < j_len(pe); k++) {
            const char *pid = ev_ref_id(j_at(pe, k));
            if (pid && !arr_has_str(earliest, pid)) arr_add_unique(queue, pid);
        }
        j_free(ev);
    }
    j_free(queue);
    json *out = j_obj();
    j_set(out, "events", events);
    resp_json(w, 200, out);
    free(origin);
}

/* ======================================================================== */
/*                            inbound: queries                              */
/* ======================================================================== */

/* GET /_matrix/federation/v1/query/profile?user_id=&field= */
static void h_query_profile(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    char *origin = NULL;
    if (!fed_authenticate(r, w, &origin)) return;
    free(origin);
    const char *user_id = http_query(r, "user_id");
    const char *field = http_query(r, "field");
    if (!user_id) {
        resp_error(w, 400, "M_MISSING_PARAM", "user_id is required");
        return;
    }
    if (!fed_is_local_user(user_id)) {
        resp_error(w, 400, "M_INVALID_PARAM", "User is not local to this server");
        return;
    }
    char *exists = db_str("SELECT user_id FROM users WHERE user_id=?1", "s", user_id);
    if (!exists) {
        resp_error(w, 404, "M_NOT_FOUND", "User does not exist.");
        return;
    }
    free(exists);
    char *dn = db_str("SELECT displayname FROM users WHERE user_id=?1", "s", user_id);
    char *av = db_str("SELECT avatar_url FROM users WHERE user_id=?1", "s", user_id);
    json *out = j_obj();
    if (!field || str_eq(field, "displayname")) jset_str(out, "displayname", dn);
    if (!field || str_eq(field, "avatar_url")) jset_str(out, "avatar_url", av);
    free(dn); free(av);
    resp_json(w, 200, out);
}

/* GET /_matrix/federation/v1/query/directory?room_alias= */
static void h_query_directory(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    char *origin = NULL;
    if (!fed_authenticate(r, w, &origin)) return;
    free(origin);
    const char *alias = http_query(r, "room_alias");
    if (!alias) {
        resp_error(w, 400, "M_MISSING_PARAM", "room_alias is required");
        return;
    }
    char *room_id = db_str("SELECT room_id FROM aliases WHERE alias=?1", "s", alias);
    if (!room_id) {
        resp_error(w, 404, "M_NOT_FOUND", "Room alias not found.");
        return;
    }
    json *servers = fed_room_servers(room_id);
    /* This server holds the room, so it belongs at the head of the list. */
    json *all = j_arr();
    j_push(all, j_str(CFG.server_name));
    for (size_t i = 0; i < j_len(servers); i++)
        arr_add_unique(all, jstr(j_at(servers, i)));
    j_free(servers);
    json *out = j_obj();
    j_set(out, "room_id", j_str(room_id));
    j_set(out, "servers", all);
    free(room_id);
    resp_json(w, 200, out);
}

/* GET /_matrix/federation/v1/query/{queryType} - anything else. */
static void h_query_other(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    char *origin = NULL;
    if (!fed_authenticate(r, w, &origin)) return;
    free(origin);
    char *msg = str_fmt("Unknown query type %s", r->part[0]);
    resp_error(w, 404, "M_NOT_FOUND", msg);
    free(msg);
}

/* GET /_matrix/federation/v1/user/devices/{userId} */
static void h_user_devices(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    char *origin = NULL;
    if (!fed_authenticate(r, w, &origin)) return;
    free(origin);
    const char *user_id = r->part[0];
    if (!fed_is_local_user(user_id)) {
        resp_error(w, 400, "M_INVALID_PARAM", "User is not local to this server");
        return;
    }
    json *devices = j_arr();
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(), "SELECT device_id, display_name FROM devices "
                                 "WHERE user_id=?1", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, user_id, -1, SQLITE_TRANSIENT);
        while (sqlite3_step(st) == SQLITE_ROW) {
            json *d = j_obj();
            j_set(d, "device_id", j_str((const char *)sqlite3_column_text(st, 0)));
            const char *dn = (const char *)sqlite3_column_text(st, 1);
            if (dn) j_set(d, "device_display_name", j_str(dn));
            json *uploaded = keys_user_device_keys(user_id);
            json *keys = uploaded
                ? j_clone(j_get(uploaded,
                                (const char *)sqlite3_column_text(st, 0)))
                : NULL;
            j_free(uploaded);
            if (!keys) {
                keys = j_obj();
                j_set(keys, "user_id", j_str(user_id));
                j_set(keys, "device_id",
                      j_str((const char *)sqlite3_column_text(st, 0)));
                j_set(keys, "algorithms", j_arr());
                j_set(keys, "keys", j_obj());
                j_set(keys, "signatures", j_obj());
            }
            j_set(d, "keys", keys);
            j_push(devices, d);
        }
        sqlite3_finalize(st);
    }
    json *out = j_obj();
    j_set(out, "user_id", j_str(user_id));
    j_set(out, "stream_id", j_int(stream_current()));
    j_set(out, "devices", devices);
    resp_json(w, 200, out);
}

/* ======================================================================== */
/*              inbound: the join / leave / knock handshakes                */
/* ======================================================================== */

/* An error object carrying the extra `room_version` field that the
 * M_INCOMPATIBLE_ROOM_VERSION responses are defined to include. */
static void resp_error_rv(http_resp *w, int status, const char *code,
                          const char *msg, const char *room_version) {
    json *b = j_obj();
    j_set(b, "errcode", j_str(code));
    j_set(b, "error", j_str(msg));
    jset_str(b, "room_version", room_version);
    resp_json(w, status, b);
}

/* Builds the unsigned m.room.member template that make_join / make_leave /
 * make_knock hand back.  Caller frees. */
static json *membership_template(const char *room_id, const char *user_id,
                                 const char *membership, const roomver *rv,
                                 const json *state, json *extra_content) {
    json *ev = j_obj();
    j_set(ev, "type", j_str("m.room.member"));
    j_set(ev, "room_id", j_str(room_id));
    j_set(ev, "sender", j_str(user_id));
    j_set(ev, "state_key", j_str(user_id));
    json *c = j_obj();
    j_set(c, "membership", j_str(membership));
    for (size_t i = 0; extra_content && i < j_len(extra_content); i++)
        j_set(c, j_key(extra_content, i), j_clone(j_at(extra_content, i)));
    j_free(extra_content);
    j_set(ev, "content", c);
    /* `origin` names the server that will send the event, which is the one the
     * user belongs to - not this, the resident, server.  A joining server is
     * entitled to leave the template's value alone. */
    jset_str(ev, "origin", user_id_server(user_id));
    j_set(ev, "origin_server_ts", j_int(now_ms()));

    int64_t depth = 1;
    db_int("SELECT COALESCE(MAX(depth),0)+1 FROM events WHERE room_id=?1",
           &depth, "s", room_id);
    j_set(ev, "depth", j_int(depth));

    json *prevs = j_arr();
    char *prev_id = db_str("SELECT event_id FROM events WHERE room_id=?1 "
                           "ORDER BY topological DESC, stream_id DESC LIMIT 1",
                           "s", room_id);
    if (prev_id) {
        if (rv->event_format == 1) {
            json *pair = j_arr();
            j_push(pair, j_str(prev_id));
            json *h = j_obj();
            json *pe = event_by_id(prev_id);
            unsigned char ref[32];
            if (pe && event_reference_hash(pe, rv, ref)) {
                char *b = b64_encode(ref, 32, false, false);
                j_set(h, "sha256", j_str(b));
                free(b);
            } else {
                j_set(h, "sha256", j_str(""));
            }
            j_free(pe);
            j_push(pair, h);
            j_push(prevs, pair);
        } else {
            j_push(prevs, j_str(prev_id));
        }
        free(prev_id);
    }
    j_set(ev, "prev_events", prevs);

    json *auth = j_arr();
    json *keys = event_auth_keys(ev);
    for (size_t i = 0; i < j_len(keys); i++) {
        const char *k = jstr(j_at(keys, i));
        json *se = k ? j_get((json *)state, k) : NULL;
        const char *sid = se ? j_gets(se, "event_id", NULL) : NULL;
        if (!sid) continue;
        if (rv->event_format == 1) {
            json *pair = j_arr();
            j_push(pair, j_str(sid));
            json *h = j_obj();
            unsigned char ref[32];
            if (event_reference_hash(se, rv, ref)) {
                char *b = b64_encode(ref, 32, false, false);
                j_set(h, "sha256", j_str(b));
                free(b);
            } else {
                j_set(h, "sha256", j_str(""));
            }
            j_push(pair, h);
            j_push(auth, pair);
        } else {
            j_push(auth, j_str(sid));
        }
    }
    j_free(keys);
    j_set(ev, "auth_events", auth);
    return ev;
}

/* Room version 8's restricted join rule.  The joining server cannot decide
 * whether the user satisfies the rule - only a server already in the room can -
 * so the resident server checks the allow list and names a local user who is in
 * the room and may issue invites.  Returns that user (caller frees), or NULL
 * when the rule is not satisfied. */
static char *restricted_authoriser(const char *room_id, const char *user_id,
                                   const json *state) {
    json *jr = state_lookup(state, "m.room.join_rules", "");
    json *jc = jr ? j_geto(jr, "content") : NULL;
    json *allow = jc ? j_geta(jc, "allow") : NULL;
    bool eligible = false;
    for (size_t i = 0; !eligible && i < j_len(allow); i++) {
        json *e = j_at(allow, i);
        if (!e || e->type != J_OBJECT) continue;
        if (!str_eq(j_gets(e, "type", ""), "m.room_membership")) continue;
        const char *other = j_gets(e, "room_id", NULL);
        if (!other) continue;
        char *ms = room_membership(other, user_id);
        eligible = ms && str_eq(ms, "join");
        free(ms);
    }
    if (!eligible) return NULL;

    json *pl = state_lookup(state, "m.room.power_levels", "");
    json *plc = pl ? j_geto(pl, "content") : NULL;
    int64_t need = pl_action_level(plc, "invite", 0);
    char *best = NULL;
    int64_t best_level = 0;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT user_id FROM room_members WHERE room_id=?1 AND "
            "membership='join' ORDER BY user_id ASC", -1, &st, NULL) != SQLITE_OK)
        return NULL;
    sqlite3_bind_text(st, 1, room_id, -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *uid = (const char *)sqlite3_column_text(st, 0);
        if (!uid || !fed_is_local_user(uid)) continue;
        int64_t lvl = plc ? pl_user_level(plc, uid) : 0;
        if (lvl < need) continue;
        if (!best || lvl > best_level) {
            free(best);
            best = xstrdup_(uid);
            best_level = lvl;
        }
    }
    sqlite3_finalize(st);
    return best;
}

/* GET /_matrix/federation/v1/make_{join,leave,knock}/{roomId}/{userId} */
static void make_membership(http_req *r, http_resp *w, const char *membership,
                            bool ver_required) {
    const char *room_id = r->part[0], *user_id = r->part[1];
    char *origin = NULL;
    if (!fed_authenticate(r, w, &origin)) return;
    const char *usrv = user_id_server(user_id);
    if (!usrv || !str_eq(usrv, origin)) {
        resp_error(w, 403, "M_FORBIDDEN", "The user does not belong to your server");
        free(origin);
        return;
    }
    char *version = NULL;
    const roomver *rv = room_rv(room_id, &version);
    if (!rv) {
        resp_error(w, 404, "M_NOT_FOUND", "Unknown room");
        free(origin); free(version);
        return;
    }
    /* make_leave carries no `ver`: leaving does not negotiate a version. */
    if (!str_eq(membership, "leave")) {
        const char *vers[16];
        int nver = http_query_all(r, "ver", vers, 16);
        bool supported = false;
        for (int i = 0; i < nver; i++)
            if (str_eq(vers[i], version)) supported = true;
        /* "The room versions the sending server has support for.  Defaults to
         * [1]." */
        if (nver == 0 && !ver_required) supported = str_eq(version, "1");
        if (!supported) {
            resp_error_rv(w, 400, "M_INCOMPATIBLE_ROOM_VERSION",
                          "Your homeserver does not support the features "
                          "required to join this room", version);
            free(origin); free(version);
            return;
        }
    }
    if (str_eq(membership, "knock") && !rv->knock) {
        resp_error_rv(w, 403, "M_FORBIDDEN",
                      "This room version does not support knocking", version);
        free(origin); free(version);
        return;
    }

    json *state = room_state_map(room_id);
    /* A restricted room needs the authorising user in the template's content
     * before the auth events are chosen, because that user's membership is one
     * of them. */
    json *extra = NULL;
    if (str_eq(membership, "join") && rv->restricted_join) {
        json *jr = state_lookup(state, "m.room.join_rules", "");
        json *jc = jr ? j_geto(jr, "content") : NULL;
        const char *rule = jc ? j_gets(jc, "join_rule", "") : "";
        char *already = room_membership(room_id, user_id);
        bool in_already = already && (str_eq(already, "join") ||
                                      str_eq(already, "invite"));
        free(already);
        if (!in_already &&
            (str_eq(rule, "restricted") ||
             (rv->knock_restricted && str_eq(rule, "knock_restricted")))) {
            char *via = restricted_authoriser(room_id, user_id, state);
            if (!via) {
                j_free(state);
                resp_error(w, 403, "M_FORBIDDEN",
                           "You are not a member of any room this room allows "
                           "in, so no user here can authorise your join");
                free(origin); free(version);
                return;
            }
            extra = j_obj();
            j_set(extra, "join_authorised_via_users_server", j_str(via));
            free(via);
        }
    }
    json *ev = membership_template(room_id, user_id, membership, rv, state,
                                   extra);
    const char *why = NULL;
    bool allowed = event_auth_allowed(ev, state, rv, &why);
    j_free(state);
    if (!allowed) {
        char *msg = str_fmt("%s is not permitted: %s", membership,
                            why ? why : "not allowed");
        resp_error(w, 403, "M_FORBIDDEN", msg);
        free(msg);
        j_free(ev);
        free(origin); free(version);
        return;
    }
    json *out = j_obj();
    j_set(out, "room_version", j_str(version));
    j_set(out, "event", ev);
    resp_json(w, 200, out);
    free(origin);
    free(version);
}

static void h_make_join(http_req *r, http_resp *w, void *ud) {
    (void)ud; make_membership(r, w, "join", false);
}
static void h_make_leave(http_req *r, http_resp *w, void *ud) {
    (void)ud; make_membership(r, w, "leave", false);
}
static void h_make_knock(http_req *r, http_resp *w, void *ud) {
    (void)ud; make_membership(r, w, "knock", true);
}

/* The stripped state a knocking or invited server is handed back. */
static json *stripped_room_state(const char *room_id, const char *sender) {
    static const char *WANT[][2] = {
        {"m.room.join_rules", ""}, {"m.room.canonical_alias", ""},
        {"m.room.avatar", ""}, {"m.room.name", ""}, {"m.room.encryption", ""},
        {"m.room.topic", ""}, {"m.room.create", ""},
    };
    json *out = j_arr();
    for (size_t i = 0; i < sizeof WANT / sizeof WANT[0]; i++) {
        json *ev = room_state_event(room_id, WANT[i][0], WANT[i][1]);
        if (!ev) continue;
        json *s = event_to_stripped(ev);
        j_set(s, "room_id", j_str(room_id));
        j_push(out, s);
        j_free(ev);
    }
    /* The invited user has to be able to see who invited them, so the
     * inviter's own membership goes in the stripped state too. */
    if (sender) {
        json *ev = room_state_event(room_id, "m.room.member", sender);
        if (ev) {
            json *s = event_to_stripped(ev);
            j_set(s, "room_id", j_str(room_id));
            j_push(out, s);
            j_free(ev);
        }
    }
    return out;
}

/* Shared body of send_join / send_leave / send_knock: validate the submitted
 * event and accept it into the room.  Returns the event id (caller frees) or
 * NULL after writing an error into `w`. */
static char *accept_membership(http_req *r, http_resp *w, const char *membership,
                               const char *room_id, const char *path_event_id,
                               json **state_before_out, json **event_out) {
    if (state_before_out) *state_before_out = NULL;
    if (event_out) *event_out = NULL;
    char *origin = NULL;
    if (!fed_authenticate(r, w, &origin)) return NULL;
    char *version = NULL;
    const roomver *rv = room_rv(room_id, &version);
    if (!rv) {
        resp_error(w, 404, "M_NOT_FOUND", "Unknown room");
        free(origin);
        free(version);
        return NULL;
    }
    json *pdu = http_json(r);
    if (!pdu || pdu->type != J_OBJECT) {
        resp_error(w, 400, "M_BAD_JSON", "Request body must be a JSON object");
        free(origin); free(version);
        return NULL;
    }
    const char *sender = j_gets(pdu, "sender", NULL);
    const char *state_key = j_gets(pdu, "state_key", NULL);
    json *content = j_geto(pdu, "content");
    const char *ms = content ? j_gets(content, "membership", NULL) : NULL;
    const char *ssrv = sender ? user_id_server(sender) : NULL;
    if (!str_eq(j_gets(pdu, "type", ""), "m.room.member") ||
        !sender || !state_key || !str_eq(sender, state_key) ||
        !str_eq(ms, membership)) {
        resp_error(w, 400, "M_BAD_JSON",
                   "The event must be an m.room.member event for the sender");
        free(origin); free(version);
        return NULL;
    }
    if (!ssrv || !str_eq(ssrv, origin)) {
        resp_error(w, 403, "M_FORBIDDEN", "The user does not belong to your server");
        free(origin); free(version);
        return NULL;
    }
    if (!str_eq(j_gets(pdu, "room_id", ""), room_id)) {
        resp_error(w, 400, "M_BAD_JSON", "The event is for a different room");
        free(origin); free(version);
        return NULL;
    }

    char *eid = NULL;
    const char *why = NULL;
    json *ev = pdu_validate(pdu, rv, &eid, &why);
    if (!ev) {
        resp_error(w, 403, "M_FORBIDDEN", why ? why : "invalid event");
        free(origin); free(version);
        return NULL;
    }
    if (path_event_id && *path_event_id && !str_eq(eid, path_event_id)) {
        resp_error(w, 400, "M_BAD_JSON",
                   "The event id does not match the one in the request path");
        j_free(ev); free(eid); free(origin); free(version);
        return NULL;
    }

    /* A restricted join is authorised by a user here, so this server attests
     * to it with its own signature - the joining server cannot.  The event id
     * is the hash of the redacted event, so this does not change it. */
    json *content2 = j_geto(ev, "content");
    const char *via = content2
        ? j_gets(content2, "join_authorised_via_users_server", NULL) : NULL;
    if (rv->restricted_join && str_eq(membership, "join") && via &&
        fed_is_local_user(via))
        event_sign(ev, rv);

    hs_lock();
    json *state = room_state_map(room_id);
    const char *deny = NULL;
    bool allowed = event_auth_allowed(ev, state, rv, &deny);
    if (allowed) {
        json *cache = j_obj();
        json *at = fed_state_at_prevs(rv, ev, cache);
        if (!at) {          /* an ancestry we never saw: the state we have */
            at = j_obj();
            for (size_t i = 0; i < j_len(state); i++) {
                const char *sid = j_gets(j_at(state, i), "event_id", NULL);
                if (sid) j_set(at, j_key(state, i), j_str(sid));
            }
        }
        fed_store_event(room_id, ev, eid, true);
        fed_state_store(eid, at);
        j_free(at);
        j_free(cache);
        fed_refresh_room_state(room_id, rv);
    }
    hs_unlock();

    if (!allowed) {
        char *msg = str_fmt("Event rejected by the authorization rules: %s",
                            deny ? deny : "not allowed");
        resp_error(w, 403, "M_FORBIDDEN", msg);
        free(msg);
        j_free(state); j_free(ev); free(eid); free(origin); free(version);
        return NULL;
    }
    if (state_before_out) *state_before_out = state; else j_free(state);
    if (event_out) *event_out = ev; else j_free(ev);
    free(origin);
    free(version);
    stream_notify();
    fed_wake();
    return eid;
}

/* PUT /_matrix/federation/v{1,2}/send_join/{roomId}/{eventId} */
static void send_join(http_req *r, http_resp *w, bool v1) {
    const char *room_id = r->part[0], *event_id = r->part[1];
    json *state = NULL, *joined = NULL;
    char *eid = accept_membership(r, w, "join", room_id, event_id, &state,
                                  &joined);
    if (!eid) { j_free(state); j_free(joined); return; }
    const roomver *rv = room_rv(room_id, NULL);
    json *ids = state_map_ids(state);
    json *chain = auth_chain_ids(ids, true);
    json *out = j_obj();
    jset_str(out, "origin", CFG.server_name);
    /* The accepted event, so the joining server picks up any signature this
     * server added to it. */
    if (joined) j_set(out, "event", fed_pdu_out(joined, rv));
    j_free(joined);
    j_set(out, "state", pdus_for_ids(ids, rv));
    j_set(out, "auth_chain", pdus_for_ids(chain, rv));
    j_free(ids);
    j_free(chain);
    j_free(state);
    free(eid);
    if (v1) {
        json *arr = j_arr();
        j_push(arr, j_int(200));
        j_push(arr, out);
        resp_json(w, 200, arr);
    } else {
        resp_json(w, 200, out);
    }
}

static void h_send_join_v1(http_req *r, http_resp *w, void *ud) {
    (void)ud; send_join(r, w, true);
}
static void h_send_join_v2(http_req *r, http_resp *w, void *ud) {
    (void)ud; send_join(r, w, false);
}

/* PUT /_matrix/federation/v{1,2}/send_leave/{roomId}/{eventId} */
static void send_leave(http_req *r, http_resp *w, bool v1) {
    const char *room_id = r->part[0], *event_id = r->part[1];
    char *eid = accept_membership(r, w, "leave", room_id, event_id, NULL, NULL);
    if (!eid) return;
    free(eid);
    if (v1) {
        json *arr = j_arr();
        j_push(arr, j_int(200));
        j_push(arr, j_obj());
        resp_json(w, 200, arr);
    } else {
        resp_json(w, 200, j_obj());
    }
}

static void h_send_leave_v1(http_req *r, http_resp *w, void *ud) {
    (void)ud; send_leave(r, w, true);
}
static void h_send_leave_v2(http_req *r, http_resp *w, void *ud) {
    (void)ud; send_leave(r, w, false);
}

/* PUT /_matrix/federation/v1/send_knock/{roomId}/{eventId} */
static void h_send_knock(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    const char *room_id = r->part[0], *event_id = r->part[1];
    char *version = NULL;
    const roomver *rv = room_rv(room_id, &version);
    if (rv && !rv->knock) {
        resp_error_rv(w, 403, "M_FORBIDDEN",
                      "This room version does not support knocking", version);
        free(version);
        return;
    }
    free(version);
    char *eid = accept_membership(r, w, "knock", room_id, event_id, NULL, NULL);
    if (!eid) return;
    free(eid);
    json *out = j_obj();
    j_set(out, "knock_room_state", stripped_room_state(room_id, NULL));
    resp_json(w, 200, out);
}

/* ======================================================================== */
/*                           inbound: invites                               */
/* ======================================================================== */

/* PUT /_matrix/federation/v{1,2}/invite/{roomId}/{eventId} */
static void do_invite(http_req *r, http_resp *w, bool v2) {
    const char *room_id = r->part[0], *path_event_id = r->part[1];
    char *origin = NULL;
    if (!fed_authenticate(r, w, &origin)) return;
    json *body = http_json(r);
    if (!body || body->type != J_OBJECT) {
        resp_error(w, 400, "M_BAD_JSON", "Request body must be a JSON object");
        free(origin);
        return;
    }
    const char *version = NULL;
    json *pdu = NULL, *invite_state = NULL;
    if (v2) {
        version = j_gets(body, "room_version", NULL);
        pdu = j_geto(body, "event");
        invite_state = j_geta(body, "invite_room_state");
        if (!version || !pdu) {
            resp_error(w, 400, "M_BAD_JSON",
                       "room_version and event are required");
            free(origin);
            return;
        }
    } else {
        /* "Servers which receive a v1 invite request must assume that the room
         * version is either 1 or 2." */
        version = "1";
        pdu = body;
        json *un = j_geto(body, "unsigned");
        invite_state = un ? j_geta(un, "invite_room_state") : NULL;
    }
    const roomver *rv = rv_get(version);
    if (!rv) {
        resp_error_rv(w, 400, "M_UNSUPPORTED_ROOM_VERSION",
                      "This server does not support that room version", version);
        free(origin);
        return;
    }

    const char *sender = j_gets(pdu, "sender", NULL);
    const char *target = j_gets(pdu, "state_key", NULL);
    json *content = j_geto(pdu, "content");
    if (!str_eq(j_gets(pdu, "type", ""), "m.room.member") || !sender || !target ||
        !content || !str_eq(j_gets(content, "membership", ""), "invite")) {
        resp_error(w, 400, "M_BAD_JSON", "The event must be an invite membership event");
        free(origin);
        return;
    }
    if (!fed_is_local_user(target)) {
        resp_error(w, 403, "M_FORBIDDEN", "The invited user is not on this server");
        free(origin);
        return;
    }
    const char *ssrv = user_id_server(sender);
    if (!ssrv || !str_eq(ssrv, origin)) {
        resp_error(w, 403, "M_FORBIDDEN", "The inviter does not belong to your server");
        free(origin);
        return;
    }

    char *eid = NULL;
    const char *why = NULL;
    json *ev = pdu_validate(pdu, rv, &eid, &why);
    if (!ev) {
        resp_error(w, 403, "M_FORBIDDEN", why ? why : "invalid event");
        free(origin);
        return;
    }
    if (path_event_id && *path_event_id && !str_eq(eid, path_event_id) &&
        rv->event_format != 1) {
        resp_error(w, 400, "M_BAD_JSON",
                   "The event id does not match the one in the request path");
        j_free(ev); free(eid); free(origin);
        return;
    }

    /* Counter-sign it: the invited server's signature is what the inviting
     * server sends on to the rest of the room. */
    json *signed_ev = j_clone(ev);
    event_sign(signed_ev, rv);

    /* Remember it locally so the invited user sees the invite. */
    json *stored = j_clone(signed_ev);
    if (invite_state) {
        json *un = j_geto(stored, "unsigned");
        if (!un) { un = j_obj(); j_set(stored, "unsigned", un); }
        j_set(un, "invite_room_state", j_clone(invite_state));
    }
    hs_lock();
    fed_ensure_room(room_id, version, sender);
    fed_store_event(room_id, stored, eid, true);
    hs_unlock();
    j_free(stored);
    stream_notify();

    json *out_ev = j_clone(signed_ev);
    if (invite_state) {
        json *un = j_geto(out_ev, "unsigned");
        if (!un) { un = j_obj(); j_set(out_ev, "unsigned", un); }
        j_set(un, "invite_room_state", j_clone(invite_state));
    }
    json *container = j_obj();
    j_set(container, "event", out_ev);
    if (v2) {
        resp_json(w, 200, container);
    } else {
        json *arr = j_arr();
        j_push(arr, j_int(200));
        j_push(arr, container);
        resp_json(w, 200, arr);
    }
    j_free(signed_ev);
    j_free(ev);
    free(eid);
    free(origin);
}

static void h_invite_v1(http_req *r, http_resp *w, void *ud) {
    (void)ud; do_invite(r, w, false);
}
static void h_invite_v2(http_req *r, http_resp *w, void *ud) {
    (void)ud; do_invite(r, w, true);
}

/* ======================================================================== */
/*                          outbound federation                             */
/* ======================================================================== */

static pthread_mutex_t g_wake_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_wake_c = PTHREAD_COND_INITIALIZER;
static pthread_t g_sender;
static bool g_sender_running;
static volatile int g_stop;
static int64_t g_txn_counter;

void fed_wake(void) {
    pthread_mutex_lock(&g_wake_m);
    pthread_cond_broadcast(&g_wake_c);
    pthread_mutex_unlock(&g_wake_m);
}

static void nap_ms(int ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static char *next_txn_id(void) {
    pthread_mutex_lock(&g_wake_m);
    int64_t n = ++g_txn_counter;
    pthread_mutex_unlock(&g_wake_m);
    return str_fmt("%lld-%lld", (long long)now_ms(), (long long)n);
}

/* One transaction, retried a couple of times.  Takes ownership of `pdus` and
 * `edus`. */
static bool fed_send_txn(const char *dest, json *pdus, json *edus) {
    json *txn = j_obj();
    jset_str(txn, "origin", CFG.server_name);
    j_set(txn, "origin_server_ts", j_int(now_ms()));
    j_set(txn, "pdus", pdus ? pdus : j_arr());
    j_set(txn, "edus", edus ? edus : j_arr());
    char *txn_id = next_txn_id();
    char *enc = url_encode(txn_id);
    char *path = str_fmt("%s/send/%s", FED1, enc);
    free(enc);

    bool ok = false;
    for (int attempt = 0; attempt < SEND_ATTEMPTS && !ok && !g_stop; attempt++) {
        int status = 0;
        json *resp = fed_request_json(dest, "PUT", path, txn, &status);
        if (status == 200) {
            ok = true;
            json *results = resp ? j_geto(resp, "pdus") : NULL;
            for (size_t i = 0; results && i < j_len(results); i++) {
                const char *err = j_gets(j_at(results, i), "error", NULL);
                if (err)
                    fprintf(stderr, "federation: %s rejected %s: %s\n", dest,
                            j_key(results, i), err);
            }
        } else {
            fprintf(stderr, "federation: transaction to %s failed (status %d)\n",
                    dest, status);
            if (attempt + 1 < SEND_ATTEMPTS) nap_ms(250 * (attempt + 1));
        }
        j_free(resp);
    }
    free(path);
    free(txn_id);
    j_free(txn);
    return ok;
}

void fed_broadcast_edu(const char *room_id, const char *edu_type, json *content) {
    json *servers = fed_room_servers(room_id);
    if (j_len(servers) == 0) { j_free(servers); j_free(content); return; }
    for (size_t i = 0; i < j_len(servers); i++) {
        const char *dest = jstr(j_at(servers, i));
        if (!dest) continue;
        json *edu = j_obj();
        j_set(edu, "edu_type", j_str(edu_type));
        j_set(edu, "content", j_clone(content));
        json *edus = j_arr();
        j_push(edus, edu);
        fed_send_txn(dest, j_arr(), edus);
    }
    j_free(servers);
    j_free(content);
}

/* Replaces the stored signatures of an event with the merged set, so that an
 * invite counter-signed by the invited server can be relayed on. */
static void merge_signatures(const char *event_id, const json *extra) {
    if (!extra || extra->type != J_OBJECT) return;
    hs_lock();
    json *stored = event_by_id(event_id);
    if (stored) {
        json *sigs = j_geto(stored, "signatures");
        if (!sigs) { sigs = j_obj(); j_set(stored, "signatures", sigs); }
        for (size_t i = 0; i < j_len(extra); i++)
            j_set(sigs, j_key(extra, i), j_clone(j_at(extra, i)));
        size_t n = 0;
        char *text = j_dump(stored, &n);
        db_run("UPDATE events SET json=?2 WHERE event_id=?1", "ss", event_id, text);
        free(text);
        j_free(stored);
    }
    hs_unlock();
}

json *fed_invite_remote(const char *room_id, const char *event_id,
                        const json *invite_event, const json *invite_room_state,
                        int *status, const char **errcode, char **errmsg) {
    const char *target = j_gets(invite_event, "state_key", NULL);
    const char *dest = target ? user_id_server(target) : NULL;
    if (status) *status = 500;
    if (errcode) *errcode = "M_UNKNOWN";
    if (errmsg) *errmsg = NULL;
    if (!dest) return NULL;
    char *version = room_version_of(room_id);
    char *rid = url_encode(room_id), *eid = url_encode(event_id);

    json *body2 = j_obj();
    jset_str(body2, "room_version", version ? version : rv_default());
    j_set(body2, "event", j_clone(invite_event));
    if (invite_room_state)
        j_set(body2, "invite_room_state", j_clone(invite_room_state));
    char *path = str_fmt("%s/invite/%s/%s", FED2, rid, eid);
    int st = 0;
    json *resp = fed_request_json(dest, "PUT", path, body2, &st);
    free(path);
    j_free(body2);

    if (st == 400 || st == 404) {       /* the peer may only speak v1 */
        j_free(resp);
        json *body1 = j_clone(invite_event);
        if (invite_room_state) {
            json *un = j_geto(body1, "unsigned");
            if (!un) { un = j_obj(); j_set(body1, "unsigned", un); }
            j_set(un, "invite_room_state", j_clone(invite_room_state));
        }
        path = str_fmt("%s/invite/%s/%s", FED1, rid, eid);
        resp = fed_request_json(dest, "PUT", path, body1, &st);
        free(path);
        j_free(body1);
        if (resp && resp->type == J_ARRAY) {          /* [200, {event}] */
            json *second = j_at(resp, 1);
            json *lifted = second ? j_clone(second) : NULL;
            j_free(resp);
            resp = lifted;
        }
    }
    free(rid); free(eid); free(version);
    if (status) *status = st;
    json *ev = resp ? j_geto(resp, "event") : NULL;
    json *out = ev ? j_clone(ev) : NULL;
    if (!out && resp) {
        if (errcode) *errcode = known_errcode(j_gets(resp, "errcode", NULL));
        if (errmsg) *errmsg = xstrdup_(j_gets(resp, "error", NULL));
    }
    j_free(resp);
    if (out && errcode) *errcode = NULL;
    return out;
}

/* ---------------------------------------------------- the sender thread --- */
/* Pushes newly persisted events to every other server in their room.  An event
 * is never sent back to the server it came from. */
static void sender_pass(void) {
    int64_t pos = 0;
    if (!db_int("SELECT value FROM fed_pos WHERE name='outbound'", &pos, ""))
        return;
    int64_t newpos = pos;
    json *by_dest = j_obj();

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT stream_id, event_id, room_id, sender, type, state_key, json "
            "FROM events WHERE stream_id > ?1 ORDER BY stream_id ASC LIMIT ?2",
            -1, &st, NULL) != SQLITE_OK) { j_free(by_dest); return; }
    sqlite3_bind_int64(st, 1, pos);
    sqlite3_bind_int64(st, 2, MAX_PDUS_PER_TXN);

    json *pending_invites = j_arr();
    while (sqlite3_step(st) == SQLITE_ROW) {
        int64_t sid = sqlite3_column_int64(st, 0);
        const char *event_id = (const char *)sqlite3_column_text(st, 1);
        const char *room_id = (const char *)sqlite3_column_text(st, 2);
        const char *sender = (const char *)sqlite3_column_text(st, 3);
        const char *type = (const char *)sqlite3_column_text(st, 4);
        const char *state_key = (const char *)sqlite3_column_text(st, 5);
        const char *text = (const char *)sqlite3_column_text(st, 6);
        newpos = sid;
        if (!event_id || !room_id || !text) continue;

        const roomver *rv = room_rv(room_id, NULL);
        if (!rv) continue;
        json *stored = parse_str(text, strlen(text));
        if (!stored) continue;

        /* An invite for a user elsewhere goes through /invite, not /send. */
        const char *invite_target = NULL;
        if (str_eq(type, "m.room.member") && state_key &&
            !fed_is_local_user(state_key)) {
            json *c = j_geto(stored, "content");
            if (c && str_eq(j_gets(c, "membership", ""), "invite"))
                invite_target = state_key;
        }
        if (invite_target && fed_is_local_user(sender)) {
            json *item = j_obj();
            j_set(item, "room_id", j_str(room_id));
            j_set(item, "event_id", j_str(event_id));
            j_set(item, "target", j_str(invite_target));
            j_push(pending_invites, item);
        }

        json *pdu = fed_pdu_out(stored, rv);
        json *servers = room_servers_map(room_id, sid);
        /* A membership event always reaches the server of the user it names,
         * whatever that user's membership has just become. */
        if (str_eq(type, "m.room.member") && state_key) {
            const char *tsrv = user_id_server(state_key);
            if (tsrv && !fed_is_local_server(tsrv)) j_set(servers, tsrv, j_int(0));
        }
        const char *from = user_id_server(sender);
        for (size_t i = 0; i < j_len(servers); i++) {
            const char *dest = j_key(servers, i);
            if (!dest || (from && strcmp(dest, from) == 0)) continue;
            if (invite_target && str_eq(dest, user_id_server(invite_target)))
                continue;                 /* handled by the /invite call */
            /* Do not replay a room's past at a server that joined later: it
             * was handed the state at its join and can backfill the rest. */
            json *since = j_at(servers, i);
            if (since && sid < since->i) continue;
            json *list = j_geta(by_dest, dest);
            if (!list) { list = j_arr(); j_set(by_dest, dest, list); }
            j_push(list, j_clone(pdu));
        }
        j_free(servers);
        j_free(pdu);
        j_free(stored);
    }
    sqlite3_finalize(st);

    for (size_t i = 0; i < j_len(pending_invites); i++) {
        json *item = j_at(pending_invites, i);
        const char *room_id = j_gets(item, "room_id", "");
        const char *event_id = j_gets(item, "event_id", "");
        json *stored = event_by_id(event_id);
        const roomver *rv = room_rv(room_id, NULL);
        if (stored && rv) {
            json *pdu = fed_pdu_out(stored, rv);
            json *strip = stripped_room_state(room_id, j_gets(stored, "sender", NULL));
            int status = 0;
            const char *errcode = NULL;
            char *errmsg = NULL;
            json *signed_ev = fed_invite_remote(room_id, event_id, pdu, strip,
                                                &status, &errcode, &errmsg);
            if (signed_ev) {
                merge_signatures(event_id, j_geto(signed_ev, "signatures"));
                j_free(signed_ev);
            } else {
                fprintf(stderr, "federation: invite of %s failed (%d %s %s)\n",
                        j_gets(item, "target", "?"), status,
                        errcode ? errcode : "", errmsg ? errmsg : "");
            }
            free(errmsg);
            j_free(strip);
            j_free(pdu);
        }
        j_free(stored);
    }
    j_free(pending_invites);

    for (size_t i = 0; i < j_len(by_dest); i++) {
        const char *dest = j_key(by_dest, i);
        json *list = j_at(by_dest, i);
        if (!dest || j_len(list) == 0) continue;
        fed_send_txn(dest, j_clone(list), j_arr());
    }
    j_free(by_dest);

    if (newpos != pos) {
        hs_lock();
        db_run("UPDATE fed_pos SET value=?1 WHERE name='outbound'", "i", newpos);
        hs_unlock();
    }
}

/* Every remote server that shares a room with `user_id`. */
static json *servers_sharing_room(const char *user_id) {
    json *out = j_arr();
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT DISTINCT b.user_id FROM room_members a "
            "JOIN room_members b ON a.room_id = b.room_id "
            "WHERE a.user_id=?1 AND a.membership='join' AND "
            "b.membership IN ('join','invite')", -1, &st, NULL) != SQLITE_OK)
        return out;
    sqlite3_bind_text(st, 1, user_id, -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *srv = user_id_server((const char *)sqlite3_column_text(st, 0));
        if (srv && !fed_is_local_server(srv)) arr_add_unique(out, srv);
    }
    sqlite3_finalize(st);
    return out;
}

/* Typing notifications, read receipts and presence for local users are pushed
 * to the servers that are interested in them. */
static void sender_edu_pass(void) {
    int64_t pos = 0;
    if (!db_int("SELECT value FROM fed_pos WHERE name='edu_out'", &pos, ""))
        return;
    int64_t newpos = pos;

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT room_id, user_id, expires_ms, stream_id FROM typing "
            "WHERE stream_id > ?1 ORDER BY stream_id ASC LIMIT 200",
            -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(st, 1, pos);
        json *rows = j_arr();
        while (sqlite3_step(st) == SQLITE_ROW) {
            int64_t sid = sqlite3_column_int64(st, 3);
            if (sid > newpos) newpos = sid;
            const char *uid = (const char *)sqlite3_column_text(st, 1);
            if (!fed_is_local_user(uid)) continue;
            json *c = j_obj();
            j_set(c, "room_id", j_str((const char *)sqlite3_column_text(st, 0)));
            j_set(c, "user_id", j_str(uid));
            j_set(c, "typing", j_bool(sqlite3_column_int64(st, 2) > now_ms()));
            j_push(rows, c);
        }
        sqlite3_finalize(st);
        for (size_t i = 0; i < j_len(rows); i++) {
            json *c = j_at(rows, i);
            fed_broadcast_edu(j_gets(c, "room_id", ""), "m.typing", j_clone(c));
        }
        j_free(rows);
    }

    st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT room_id, user_id, receipt_type, event_id, ts, stream_id "
            "FROM receipts WHERE stream_id > ?1 ORDER BY stream_id ASC LIMIT 200",
            -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(st, 1, pos);
        json *rows = j_arr();
        while (sqlite3_step(st) == SQLITE_ROW) {
            int64_t sid = sqlite3_column_int64(st, 5);
            if (sid > newpos) newpos = sid;
            const char *uid = (const char *)sqlite3_column_text(st, 1);
            if (!fed_is_local_user(uid)) continue;
            const char *room_id = (const char *)sqlite3_column_text(st, 0);
            json *data = j_obj();
            j_set(data, "ts", j_int(sqlite3_column_int64(st, 4)));
            json *ids = j_arr();
            j_push(ids, j_str((const char *)sqlite3_column_text(st, 3)));
            json *one = j_obj();
            j_set(one, "event_ids", ids);
            j_set(one, "data", data);
            json *users = j_obj();
            j_set(users, uid, one);
            json *byroom = j_obj();
            j_set(byroom, (const char *)sqlite3_column_text(st, 2), users);
            json *c = j_obj();
            j_set(c, room_id, byroom);
            json *item = j_obj();
            j_set(item, "room_id", j_str(room_id));
            j_set(item, "content", c);
            j_push(rows, item);
        }
        sqlite3_finalize(st);
        for (size_t i = 0; i < j_len(rows); i++) {
            json *item = j_at(rows, i);
            fed_broadcast_edu(j_gets(item, "room_id", ""), "m.receipt",
                              j_clone(j_geto(item, "content")));
        }
        j_free(rows);
    }

    if (CFG.presence_out) {
        st = NULL;
        if (sqlite3_prepare_v2(db(),
                "SELECT user_id, presence, status_msg, last_active_ts, stream_id "
                "FROM presence WHERE stream_id > ?1 ORDER BY stream_id ASC LIMIT 200",
                -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_int64(st, 1, pos);
            json *rows = j_arr();
            while (sqlite3_step(st) == SQLITE_ROW) {
                int64_t sid = sqlite3_column_int64(st, 4);
                if (sid > newpos) newpos = sid;
                const char *uid = (const char *)sqlite3_column_text(st, 0);
                if (!fed_is_local_user(uid)) continue;
                json *p = j_obj();
                j_set(p, "user_id", j_str(uid));
                j_set(p, "presence",
                      j_str((const char *)sqlite3_column_text(st, 1)));
                const char *msg = (const char *)sqlite3_column_text(st, 2);
                if (msg) j_set(p, "status_msg", j_str(msg));
                int64_t last = sqlite3_column_int64(st, 3);
                j_set(p, "last_active_ago", j_int(last ? now_ms() - last : 0));
                j_push(rows, p);
            }
            sqlite3_finalize(st);
            for (size_t i = 0; i < j_len(rows); i++) {
                json *p = j_at(rows, i);
                json *dests = servers_sharing_room(j_gets(p, "user_id", ""));
                for (size_t k = 0; k < j_len(dests); k++) {
                    json *push = j_arr();
                    j_push(push, j_clone(p));
                    json *c = j_obj();
                    j_set(c, "push", push);
                    json *edu = j_obj();
                    j_set(edu, "edu_type", j_str("m.presence"));
                    j_set(edu, "content", c);
                    json *edus = j_arr();
                    j_push(edus, edu);
                    fed_send_txn(jstr(j_at(dests, k)), j_arr(), edus);
                }
                j_free(dests);
            }
            j_free(rows);
        }
    }

    if (newpos != pos) {
        hs_lock();
        db_run("UPDATE fed_pos SET value=?1 WHERE name='edu_out'", "i", newpos);
        hs_unlock();
    }
}

static void *sender_main(void *arg) {
    (void)arg;
    while (!g_stop) {
        sender_pass();
        sender_edu_pass();
        pthread_mutex_lock(&g_wake_m);
        if (!g_stop) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 1;
            pthread_cond_timedwait(&g_wake_c, &g_wake_m, &ts);
        }
        pthread_mutex_unlock(&g_wake_m);
    }
    return NULL;
}

void fed_shutdown(void) {
    if (!g_sender_running) return;
    g_stop = 1;
    fed_wake();
    pthread_join(g_sender, NULL);
    g_sender_running = false;
}

/* ------------------------------------------------- the joining side ------ */
static void fed_fail(int *status, const char **errcode, char **errmsg,
                     int s, const char *code, char *msg) {
    if (status) *status = s;
    if (errcode) *errcode = code;
    if (errmsg) { free(*errmsg); *errmsg = msg; } else { free(msg); }
}

/* ?ver=... for every room version this server knows. */
static char *supported_ver_query(void) {
    buf b = {0};
    for (int i = 0; i < rv_count(); i++) {
        const roomver *rv = rv_index(i);
        buf_printf(&b, "%sver=%s", i ? "&" : "?", rv->id);
    }
    return buf_take(&b, NULL);
}

static int cmp_depth(const void *a, const void *b) {
    const json *x = *(const json *const *)a, *y = *(const json *const *)b;
    int64_t dx = j_geti(j_geto((json *)x, "event"), "depth", 0);
    int64_t dy = j_geti(j_geto((json *)y, "event"), "depth", 0);
    return dx < dy ? -1 : (dx > dy ? 1 : 0);
}

/* Validates every PDU in `arr` and returns [{event_id, event}, ...] ordered by
 * depth.  Events that fail validation are dropped with a warning. */
static json *validate_bundle(const json *arr, const roomver *rv) {
    json *out = j_arr();
    for (size_t i = 0; i < j_len(arr); i++) {
        char *eid = NULL;
        const char *why = NULL;
        json *ev = pdu_validate(j_at(arr, i), rv, &eid, &why);
        if (!ev) {
            fprintf(stderr, "federation: dropping unverifiable event: %s\n",
                    why ? why : "?");
            continue;
        }
        json *item = j_obj();
        j_set(item, "event_id", j_str(eid));
        j_set(item, "event", ev);
        j_push(out, item);
        free(eid);
    }
    size_t n = j_len(out);
    if (n > 1) {
        json **items = calloc(n, sizeof *items);
        if (items) {
            for (size_t i = 0; i < n; i++) items[i] = j_at(out, i);
            qsort(items, n, sizeof *items, cmp_depth);
            json *sorted = j_arr();
            for (size_t i = 0; i < n; i++) j_push(sorted, j_clone(items[i]));
            free(items);
            j_free(out);
            out = sorted;
        }
    }
    return out;
}

/* Writes the state and auth chain a send_join handed back, then the join event
 * itself, and makes the room live. */
static void store_join_bundle(const char *room_id, const char *version,
                              const json *chain, const json *state,
                              const json *join_ev, const char *join_eid) {
    const char *creator = NULL;
    for (size_t i = 0; i < j_len(state); i++) {
        json *ev = j_geto(j_at(state, i), "event");
        if (ev && str_eq(j_gets(ev, "type", ""), "m.room.create"))
            creator = j_gets(ev, "sender", NULL);
    }
    hs_lock();
    fed_ensure_room(room_id, version, creator);
    int64_t sid = import_block((int64_t)(j_len(chain) + j_len(state)));
    for (size_t i = 0; i < j_len(chain); i++) {
        json *item = j_at(chain, i);
        if (fed_store_event_at(room_id, j_geto(item, "event"),
                               j_gets(item, "event_id", ""), false, sid))
            sid++;
    }
    for (size_t i = 0; i < j_len(state); i++) {
        json *item = j_at(state, i);
        if (fed_store_event_at(room_id, j_geto(item, "event"),
                               j_gets(item, "event_id", ""), false, sid))
            sid++;
    }
    /* The `state` array is the resolved state, so it decides room_state. */
    for (size_t i = 0; i < j_len(state); i++) {
        json *item = j_at(state, i);
        json *ev = j_geto(item, "event");
        const char *sk = ev ? j_gets(ev, "state_key", NULL) : NULL;
        if (!sk) continue;
        const char *type = j_gets(ev, "type", "");
        const char *eid = j_gets(item, "event_id", "");
        db_run("INSERT INTO room_state(room_id,type,state_key,event_id) "
               "VALUES(?1,?2,?3,?4) ON CONFLICT(room_id,type,state_key) "
               "DO UPDATE SET event_id=?4", "ssss", room_id, type, sk, eid);
        if (str_eq(type, "m.room.member")) {
            json *c = j_geto(ev, "content");
            const char *ms = c ? j_gets(c, "membership", "leave") : "leave";
            int64_t sid = 0, topo = 0;
            event_get_stream(eid, &sid, &topo);
            db_run("INSERT INTO room_members(room_id,user_id,membership,event_id,"
                   "stream_id) VALUES(?1,?2,?3,?4,?5) ON CONFLICT(room_id,user_id) "
                   "DO UPDATE SET membership=?3, event_id=?4, stream_id=?5",
                   "ssssi", room_id, sk, ms, eid, sid);
        }
    }
    fed_store_event(room_id, join_ev, join_eid, true);
    /* The `state` array is the state *before* the join, which is exactly
     * S(E) for it: this server never sees the events that led up to it, so
     * this is the only chance to record where in the DAG the room stands. */
    json *before = j_obj();
    for (size_t i = 0; i < j_len(state); i++) {
        json *item = j_at(state, i);
        json *e = j_geto(item, "event");
        const char *sk = e ? j_gets(e, "state_key", NULL) : NULL;
        if (!sk) continue;
        char *k = state_key_of(j_gets(e, "type", ""), sk);
        j_set(before, k, j_str(j_gets(item, "event_id", "")));
        free(k);
    }
    fed_state_store(join_eid, before);
    j_free(before);
    hs_unlock();
    stream_notify();
}

/* Fills in the fields a make_* template leaves to the joining server and signs
 * the result.  Returns the event id; `*out_ev` receives the signed event. */
static char *sign_template(json *tmpl, const roomver *rv, const char *user_id,
                           json **out_ev) {
    json *ev = j_clone(tmpl);
    j_del(ev, "event_id");
    j_del(ev, "hashes");
    j_del(ev, "signatures");
    j_del(ev, "unsigned");
    j_del(ev, "age_ts");
    jset_str(ev, "origin", CFG.server_name);
    j_set(ev, "origin_server_ts", j_int(now_ms()));
    json *c = j_geto(ev, "content");
    if (c) {
        char *dn = db_str("SELECT displayname FROM users WHERE user_id=?1",
                          "s", user_id);
        char *av = db_str("SELECT avatar_url FROM users WHERE user_id=?1",
                          "s", user_id);
        if (dn) { j_set(c, "displayname", j_str(dn)); free(dn); }
        if (av) { j_set(c, "avatar_url", j_str(av)); free(av); }
    }
    char *chash = event_content_hash_b64(ev);
    if (!chash) { j_free(ev); return NULL; }
    json *hashes = j_obj();
    j_set(hashes, "sha256", j_str(chash));
    free(chash);
    j_set(ev, "hashes", hashes);
    if (!event_sign(ev, rv)) {
        j_free(ev);
        return NULL;
    }
    char *eid = event_compute_id(ev, rv, CFG.server_name);
    if (!eid) { j_free(ev); return NULL; }
    /* Room versions 1 and 2 carry the event id on the wire. */
    if (rv->event_format == 1) j_set(ev, "event_id", j_str(eid));
    *out_ev = ev;
    return eid;
}

char *fed_join_remote(const char *user_id, const char *room_id_or_alias,
                      const char *const *servers, int nservers,
                      int *status, const char **errcode, char **errmsg) {
    if (status) *status = 500;
    if (errcode) *errcode = "M_UNKNOWN";
    if (errmsg) *errmsg = NULL;
    if (!user_id || !room_id_or_alias) return NULL;

    json *candidates = j_arr();
    for (int i = 0; i < nservers; i++)
        if (servers[i] && !fed_is_local_server(servers[i]))
            arr_add_unique(candidates, servers[i]);

    char *room_id = NULL;
    if (room_id_or_alias[0] == '#') {
        json *dir = fed_query_directory(room_id_or_alias);
        if (!dir || !j_gets(dir, "room_id", NULL)) {
            j_free(dir); j_free(candidates);
            fed_fail(status, errcode, errmsg, 404, "M_NOT_FOUND",
                     str_fmt("Room alias %s not found", room_id_or_alias));
            return NULL;
        }
        room_id = xstrdup_(j_gets(dir, "room_id", ""));
        json *srvs = j_geta(dir, "servers");
        for (size_t i = 0; i < j_len(srvs); i++) {
            const char *s = jstr(j_at(srvs, i));
            if (s && !fed_is_local_server(s)) arr_add_unique(candidates, s);
        }
        j_free(dir);
    } else {
        room_id = xstrdup_(room_id_or_alias);
    }
    const char *rsrv = user_id_server(room_id);
    if (rsrv && !fed_is_local_server(rsrv)) arr_add_unique(candidates, rsrv);
    if (j_len(candidates) == 0) {
        j_free(candidates); free(room_id);
        fed_fail(status, errcode, errmsg, 404, "M_NOT_FOUND",
                 xstrdup_("No server known to be in that room"));
        return NULL;
    }

    char *enc_room = url_encode(room_id);
    char *enc_user = url_encode(user_id);
    char *vq = supported_ver_query();
    bool done = false;

    for (size_t i = 0; i < j_len(candidates) && !done; i++) {
        const char *dest = jstr(j_at(candidates, i));
        if (!dest) continue;
        char *path = str_fmt("%s/make_join/%s/%s%s", FED1, enc_room, enc_user, vq);
        int st = 0;
        json *tmpl = fed_request_json(dest, "GET", path, NULL, &st);
        free(path);
        if (st != 200 || !tmpl) {
            fed_fail(status, errcode, errmsg, st ? st : 502,
                     known_errcode(tmpl ? j_gets(tmpl, "errcode", NULL) : NULL),
                     xstrdup_(tmpl ? j_gets(tmpl, "error", "make_join failed")
                                   : "make_join failed"));
            j_free(tmpl);
            continue;
        }
        const char *version = j_gets(tmpl, "room_version", "1");
        const roomver *rv = rv_get(version);
        json *proto = j_geto(tmpl, "event");
        if (!rv || !proto) {
            fed_fail(status, errcode, errmsg, 400, "M_UNSUPPORTED_ROOM_VERSION",
                     str_fmt("Room version %s is not supported", version));
            j_free(tmpl);
            continue;
        }
        char *vercopy = xstrdup_(version);
        json *ev = NULL;
        char *eid = sign_template(proto, rv, user_id, &ev);
        j_free(tmpl);
        if (!eid) {
            free(vercopy);
            fed_fail(status, errcode, errmsg, 500, "M_UNKNOWN",
                     xstrdup_("Could not sign the join event"));
            continue;
        }

        char *enc_eid = url_encode(eid);
        path = str_fmt("%s/send_join/%s/%s", FED2, enc_room, enc_eid);
        st = 0;
        json *resp = fed_request_json(dest, "PUT", path, ev, &st);
        free(path);
        if (st == 400 || st == 404) {           /* fall back to the v1 shape */
            j_free(resp);
            path = str_fmt("%s/send_join/%s/%s", FED1, enc_room, enc_eid);
            resp = fed_request_json(dest, "PUT", path, ev, &st);
            free(path);
            if (resp && resp->type == J_ARRAY) {
                json *second = j_at(resp, 1);
                json *lifted = second ? j_clone(second) : NULL;
                j_free(resp);
                resp = lifted;
            }
        }
        free(enc_eid);
        if (st != 200 || !resp) {
            fed_fail(status, errcode, errmsg, st ? st : 502,
                     known_errcode(resp ? j_gets(resp, "errcode", NULL) : NULL),
                     xstrdup_(resp ? j_gets(resp, "error", "send_join failed")
                                   : "send_join failed"));
            j_free(resp); j_free(ev); free(eid); free(vercopy);
            continue;
        }

        json *chain = validate_bundle(j_geta(resp, "auth_chain"), rv);
        json *state = validate_bundle(j_geta(resp, "state"), rv);
        store_join_bundle(room_id, vercopy, chain, state, ev, eid);
        j_free(chain);
        j_free(state);
        j_free(resp);
        /* The join hands over the state, not the history: a user who has just
         * joined still expects to be able to read back into the room. */
        json *tips = j_arr();
        json *pe = j_geta(ev, "prev_events");
        for (size_t k = 0; k < j_len(pe); k++) {
            const char *pid = ev_ref_id(j_at(pe, k));
            if (pid) arr_add_unique(tips, pid);
        }
        backfill_from(dest, room_id, rv, tips, 50);
        j_free(tips);
        j_free(ev);
        free(eid);
        free(vercopy);
        done = true;
        if (status) *status = 200;
        if (errcode) *errcode = NULL;
        if (errmsg) { free(*errmsg); *errmsg = NULL; }
        fed_wake();
    }
    free(vq);
    free(enc_room);
    free(enc_user);
    j_free(candidates);
    if (!done) { free(room_id); return NULL; }
    return room_id;
}

/* make_leave / make_knock followed by send_leave / send_knock. */
static bool make_and_send(const char *user_id, const char *room_id,
                          const char *const *servers, int nservers,
                          const char *kind, json *extra_content,
                          int *status, const char **errcode, char **errmsg) {
    if (status) *status = 500;
    if (errcode) *errcode = "M_UNKNOWN";
    if (errmsg) *errmsg = NULL;
    bool knock = str_eq(kind, "knock");

    json *candidates = j_arr();
    for (int i = 0; i < nservers; i++)
        if (servers[i] && !fed_is_local_server(servers[i]))
            arr_add_unique(candidates, servers[i]);
    const char *rsrv = user_id_server(room_id);
    if (rsrv && !fed_is_local_server(rsrv)) arr_add_unique(candidates, rsrv);
    json *known = fed_room_servers(room_id);
    for (size_t i = 0; i < j_len(known); i++)
        arr_add_unique(candidates, jstr(j_at(known, i)));
    j_free(known);

    char *enc_room = url_encode(room_id);
    char *enc_user = url_encode(user_id);
    char *vq = supported_ver_query();
    bool done = false;
    for (size_t i = 0; i < j_len(candidates) && !done; i++) {
        const char *dest = jstr(j_at(candidates, i));
        if (!dest) continue;
        char *path = knock
            ? str_fmt("%s/make_knock/%s/%s%s", FED1, enc_room, enc_user, vq)
            : str_fmt("%s/make_leave/%s/%s", FED1, enc_room, enc_user);
        int st = 0;
        json *tmpl = fed_request_json(dest, "GET", path, NULL, &st);
        free(path);
        if (st != 200 || !tmpl) {
            fed_fail(status, errcode, errmsg, st ? st : 502,
                     known_errcode(tmpl ? j_gets(tmpl, "errcode", NULL) : NULL),
                     xstrdup_(tmpl ? j_gets(tmpl, "error", "make_* failed")
                                   : "make_* failed"));
            j_free(tmpl);
            continue;
        }
        const char *version = j_gets(tmpl, "room_version", "1");
        const roomver *rv = rv_get(version);
        json *proto = j_geto(tmpl, "event");
        if (!rv || !proto) {
            fed_fail(status, errcode, errmsg, 400, "M_UNSUPPORTED_ROOM_VERSION",
                     str_fmt("Room version %s is not supported", version));
            j_free(tmpl);
            continue;
        }
        if (extra_content) {
            json *c = j_geto(proto, "content");
            for (size_t k = 0; c && k < j_len(extra_content); k++)
                j_set(c, j_key(extra_content, k), j_clone(j_at(extra_content, k)));
        }
        char *vercopy = xstrdup_(version);
        json *ev = NULL;
        char *eid = sign_template(proto, rv, user_id, &ev);
        j_free(tmpl);
        if (!eid) { free(vercopy); continue; }

        char *enc_eid = url_encode(eid);
        path = knock ? str_fmt("%s/send_knock/%s/%s", FED1, enc_room, enc_eid)
                     : str_fmt("%s/send_leave/%s/%s", FED2, enc_room, enc_eid);
        st = 0;
        json *resp = fed_request_json(dest, "PUT", path, ev, &st);
        free(path);
        if (!knock && (st == 400 || st == 404)) {
            j_free(resp);
            path = str_fmt("%s/send_leave/%s/%s", FED1, enc_room, enc_eid);
            resp = fed_request_json(dest, "PUT", path, ev, &st);
            free(path);
        }
        free(enc_eid);
        if (st != 200) {
            fed_fail(status, errcode, errmsg, st ? st : 502,
                     known_errcode(resp ? j_gets(resp, "errcode", NULL) : NULL),
                     xstrdup_(resp ? j_gets(resp, "error", "send_* failed")
                                   : "send_* failed"));
            j_free(resp); j_free(ev); free(eid); free(vercopy);
            continue;
        }
        /* Record it locally so the user's membership reflects what happened. */
        hs_lock();
        fed_ensure_room(room_id, vercopy, NULL);
        fed_store_event(room_id, ev, eid, true);
        hs_unlock();
        stream_notify();
        if (knock && resp) {
            json *ks = j_geta(resp, "knock_room_state");
            if (ks) {
                size_t n = 0;
                char *text = j_dump(ks, &n);
                hs_lock();
                db_run("INSERT OR REPLACE INTO account_data(user_id,room_id,type,"
                       "content,stream_id) VALUES(?1,?2,'m.knock_room_state',?3,?4)",
                       "sssi", user_id, room_id, text, stream_next());
                hs_unlock();
                free(text);
            }
        }
        j_free(resp);
        j_free(ev);
        free(eid);
        free(vercopy);
        done = true;
        if (status) *status = 200;
        if (errcode) *errcode = NULL;
        if (errmsg) { free(*errmsg); *errmsg = NULL; }
    }
    free(vq);
    free(enc_room);
    free(enc_user);
    j_free(candidates);
    j_free(extra_content);
    return done;
}

bool fed_leave_remote(const char *user_id, const char *room_id,
                      const char *const *servers, int nservers,
                      int *status, const char **errcode, char **errmsg) {
    return make_and_send(user_id, room_id, servers, nservers, "leave", NULL,
                         status, errcode, errmsg);
}

bool fed_knock_remote(const char *user_id, const char *room_id_or_alias,
                      const char *const *servers, int nservers, json *content,
                      int *status, const char **errcode, char **errmsg) {
    return make_and_send(user_id, room_id_or_alias, servers, nservers, "knock",
                         content, status, errcode, errmsg);
}

/* ------------------------------------------------- outbound queries ------ */
json *fed_query_profile(const char *user_id, const char *field) {
    const char *srv = user_id_server(user_id);
    if (!srv || fed_is_local_server(srv)) return NULL;
    char *u = url_encode(user_id);
    char *path = field ? str_fmt("%s/query/profile?user_id=%s&field=%s", FED1, u,
                                 field)
                       : str_fmt("%s/query/profile?user_id=%s", FED1, u);
    free(u);
    int st = 0;
    json *out = fed_request_json(srv, "GET", path, NULL, &st);
    free(path);
    if (st != 200) { j_free(out); return NULL; }
    return out;
}

json *fed_query_directory(const char *room_alias) {
    const char *srv = user_id_server(room_alias);
    if (!srv || fed_is_local_server(srv)) return NULL;
    char *a = url_encode(room_alias);
    char *path = str_fmt("%s/query/directory?room_alias=%s", FED1, a);
    free(a);
    int st = 0;
    json *out = fed_request_json(srv, "GET", path, NULL, &st);
    free(path);
    if (st != 200) { j_free(out); return NULL; }
    return out;
}

json *fed_query_user_devices(const char *user_id) {
    const char *srv = user_id_server(user_id);
    if (!srv || fed_is_local_server(srv)) return NULL;
    char *u = url_encode(user_id);
    char *path = str_fmt("%s/user/devices/%s", FED1, u);
    free(u);
    int st = 0;
    json *out = fed_request_json(srv, "GET", path, NULL, &st);
    free(path);
    if (st != 200) { j_free(out); return NULL; }
    return out;
}

/* ======================================================================== */
/*         local entry points into the outbound handshakes                  */
/* ======================================================================== */
/* The client-server handlers in mxapi_rooms.c only know how to act on rooms
 * this server already has, so until they are wired up to the functions above
 * these administrative routes are how a local user reaches a remote room.
 * They live outside the /_matrix namespace so that they can never shadow a
 * specified endpoint, and they take an ordinary client access token. */

static void h_admin_join(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    const char *servers[8];
    int n = http_query_all(r, "server_name", servers, 8);
    int status = 0;
    const char *errcode = NULL;
    char *errmsg = NULL;
    char *room_id = fed_join_remote(p.user_id, r->part[0], servers, n,
                                    &status, &errcode, &errmsg);
    if (!room_id) {
        resp_error(w, status >= 400 ? status : 502,
                   errcode ? errcode : "M_UNKNOWN",
                   errmsg ? errmsg : "Remote join failed");
    } else {
        json *b = j_obj();
        j_set(b, "room_id", j_str(room_id));
        resp_json(w, 200, b);
        free(room_id);
    }
    free(errmsg);
    principal_free(&p);
}

static void h_admin_leave(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    const char *servers[8];
    int n = http_query_all(r, "server_name", servers, 8);
    int status = 0;
    const char *errcode = NULL;
    char *errmsg = NULL;
    bool ok = fed_leave_remote(p.user_id, r->part[0], servers, n,
                               &status, &errcode, &errmsg);
    if (!ok)
        resp_error(w, status >= 400 ? status : 502,
                   errcode ? errcode : "M_UNKNOWN",
                   errmsg ? errmsg : "Remote leave failed");
    else
        resp_json(w, 200, j_obj());
    free(errmsg);
    principal_free(&p);
}

static void h_admin_knock(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    const char *servers[8];
    int n = http_query_all(r, "server_name", servers, 8);
    json *body = http_json(r);
    json *content = j_obj();
    const char *reason = body ? j_gets(body, "reason", NULL) : NULL;
    if (reason) j_set(content, "reason", j_str(reason));
    int status = 0;
    const char *errcode = NULL;
    char *errmsg = NULL;
    bool ok = fed_knock_remote(p.user_id, r->part[0], servers, n, content,
                               &status, &errcode, &errmsg);
    if (!ok)
        resp_error(w, status >= 400 ? status : 502,
                   errcode ? errcode : "M_UNKNOWN",
                   errmsg ? errmsg : "Remote knock failed");
    else
        resp_json(w, 200, j_obj());
    free(errmsg);
    principal_free(&p);
}

/* ======================================================================== */
/*                              registration                                */
/* ======================================================================== */
void api_register_federation(http_server *s) {
    spec_advertise("v1.2");
    if (!s) return;
    if (!db_exec(FED_SCHEMA))
        fprintf(stderr, "federation: could not create the federation tables\n");
    /* Start the outbound cursor at "now": anything already in the database was
     * created before federation was running. */
    db_run("INSERT OR IGNORE INTO fed_pos(name,value) VALUES('outbound',?1)",
           "i", stream_current());
    db_run("INSERT OR IGNORE INTO fed_pos(name,value) VALUES('edu_out',?1)",
           "i", stream_current());
    db_run("INSERT OR IGNORE INTO fed_pos(name,value) VALUES('import',?1)",
           "i", (int64_t)FED_IMPORT_BASE);

    /* --- key server (unauthenticated by definition) --- */
    http_route(s, "GET",  KEY2 "/server", h_key_server);
    http_route(s, "GET",  KEY2 "/server/:", h_key_server);
    http_route(s, "GET",  KEY2 "/query/:", h_key_query_get);
    http_route(s, "GET",  KEY2 "/query/:/:", h_key_query_get);
    http_route(s, "POST", KEY2 "/query", h_key_query_post);
    http_route(s, "GET",  "/.well-known/matrix/server", h_wellknown);

    /* --- server implementation --- */
    http_route(s, "GET",  FED1 "/version", h_version);

    /* --- transactions --- */
    http_route(s, "PUT",  FED1 "/send/:", h_send_txn);

    /* --- events, state and history --- */
    http_route(s, "GET",  FED1 "/event/:", h_get_event);
    http_route(s, "GET",  FED1 "/state/:", h_get_state);
    http_route(s, "GET",  FED1 "/state_ids/:", h_get_state_ids);
    http_route(s, "GET",  FED1 "/event_auth/:/:", h_event_auth);
    http_route(s, "GET",  FED1 "/backfill/:", h_backfill);
    http_route(s, "POST", FED1 "/get_missing_events/:", h_get_missing);

    /* --- joining, leaving, knocking, inviting --- */
    http_route(s, "GET",  FED1 "/make_join/:/:", h_make_join);
    http_route(s, "PUT",  FED1 "/send_join/:/:", h_send_join_v1);
    http_route(s, "PUT",  FED2 "/send_join/:/:", h_send_join_v2);
    http_route(s, "GET",  FED1 "/make_leave/:/:", h_make_leave);
    http_route(s, "PUT",  FED1 "/send_leave/:/:", h_send_leave_v1);
    http_route(s, "PUT",  FED2 "/send_leave/:/:", h_send_leave_v2);
    http_route(s, "GET",  FED1 "/make_knock/:/:", h_make_knock);
    http_route(s, "PUT",  FED1 "/send_knock/:/:", h_send_knock);
    http_route(s, "PUT",  FED1 "/invite/:/:", h_invite_v1);
    http_route(s, "PUT",  FED2 "/invite/:/:", h_invite_v2);

    /* --- queries --- */
    http_route(s, "GET",  FED1 "/query/profile", h_query_profile);
    http_route(s, "GET",  FED1 "/query/directory", h_query_directory);
    http_route(s, "GET",  FED1 "/query/:", h_query_other);
    http_route(s, "GET",  FED1 "/user/devices/:", h_user_devices);

    /* --- local entry points into the outbound handshakes --- */
    http_route(s, "POST", "/_dendrite/admin/federation/join/:", h_admin_join);
    http_route(s, "POST", "/_dendrite/admin/federation/leave/:", h_admin_leave);
    http_route(s, "POST", "/_dendrite/admin/federation/knock/:", h_admin_knock);

    if (!CFG.disable_federation && !g_sender_running) {
        g_stop = 0;
        if (pthread_create(&g_sender, NULL, sender_main, NULL) == 0)
            g_sender_running = true;
        else
            fprintf(stderr, "federation: cannot start the sender thread\n");
    }
}

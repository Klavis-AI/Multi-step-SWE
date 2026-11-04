#define _POSIX_C_SOURCE 200809L
/* Accounts: /versions, /capabilities, registration, login, devices, profile. */
#include "mxapi.h"
#include "mxfed.h"

/* Erasure arrives with the milestone whose release describes it. */
extern void media2_erasure_record(const char *user_id)
    __attribute__((weak));
extern void push_access_token_used(const char *token) __attribute__((weak));
#include "mxkeys.h"
#include "mxdb.h"
#include "mxutil.h"

#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CS "/_matrix/client/v3"
#define CSR "/_matrix/client/r0"

/* ------------------------------------------------------------- passwords */
static char *hash_password(const char *pw) {
    unsigned char salt[16], out[32];
    random_bytes(salt, sizeof salt);
    PKCS5_PBKDF2_HMAC(pw, (int)strlen(pw), salt, sizeof salt, 4096,
                      EVP_sha256(), sizeof out, out);
    char *s = b64_encode(salt, sizeof salt, false, true);
    char *h = b64_encode(out, sizeof out, false, true);
    char *res = str_fmt("pbkdf2$4096$%s$%s", s, h);
    free(s); free(h);
    return res;
}

static bool check_password(const char *stored, const char *pw) {
    if (!stored || !pw) return false;
    char copy[512];
    snprintf(copy, sizeof copy, "%s", stored);
    char *save = NULL;
    char *algo = strtok_r(copy, "$", &save);
    char *iter = strtok_r(NULL, "$", &save);
    char *salt_b64 = strtok_r(NULL, "$", &save);
    char *hash_b64 = strtok_r(NULL, "$", &save);
    if (!algo || !iter || !salt_b64 || !hash_b64 || strcmp(algo, "pbkdf2")) return false;
    size_t sn = 0;
    unsigned char *salt = b64_decode(salt_b64, &sn);
    unsigned char out[32];
    PKCS5_PBKDF2_HMAC(pw, (int)strlen(pw), salt, (int)sn, atoi(iter),
                      EVP_sha256(), sizeof out, out);
    free(salt);
    char *h = b64_encode(out, sizeof out, false, true);
    bool ok = strcmp(h, hash_b64) == 0;
    free(h);
    return ok;
}

/* ------------------------------------------------------------------ auth */
bool require_auth(http_req *r, http_resp *w, principal *out) {
    const char *hdr = http_header(r, "authorization");
    char *token = NULL;
    if (hdr && str_prefix(hdr, "Bearer ")) token = xstrdup_(hdr + 7);
    if (!token) {
        const char *q = http_query(r, "access_token");
        if (q) token = xstrdup_(q);
    }
    if (!token) {
        resp_error(w, 401, "M_MISSING_TOKEN", "Missing access token");
        return false;
    }
    char *user = db_str("SELECT user_id FROM access_tokens WHERE token=?1", "s", token);
    if (!user) {
        free(token);
        json *b = j_obj();
        j_set(b, "errcode", j_str("M_UNKNOWN_TOKEN"));
        j_set(b, "error", j_str("Invalid access token passed."));
        j_set(b, "soft_logout", j_bool(false));
        resp_json(w, 401, b);
        return false;
    }
    /* Using an access token that came from a refresh spends the refresh token
     * it replaced.  Only such tokens carry this prefix, so an ordinary request
     * costs nothing. */
    if (push_access_token_used && strncmp(token, "syr_", 4) == 0)
        push_access_token_used(token);
    int64_t deact = 0;
    db_int("SELECT deactivated FROM users WHERE user_id=?1", &deact, "s", user);
    if (deact) {
        free(token); free(user);
        resp_error(w, 403, "M_USER_DEACTIVATED", "This account has been deactivated");
        return false;
    }
    out->user_id = user;
    out->device_id = db_str("SELECT device_id FROM access_tokens WHERE token=?1",
                            "s", token);
    int64_t guest = 0;
    db_int("SELECT is_guest FROM users WHERE user_id=?1", &guest, "s", user);
    out->is_guest = guest != 0;
    free(token);
    return true;
}

void principal_free(principal *p) {
    if (!p) return;
    free(p->user_id);
    free(p->device_id);
    p->user_id = p->device_id = NULL;
}

json *require_body(http_req *r, http_resp *w) {
    json *b = http_json(r);
    if (!b) {
        resp_error(w, 400, r->json_bad ? "M_NOT_JSON" : "M_NOT_JSON",
                   "Request body is not valid JSON");
        return NULL;
    }
    if (b->type != J_OBJECT) {
        resp_error(w, 400, "M_BAD_JSON", "Request body must be a JSON object");
        return NULL;
    }
    return b;
}

/* ------------------------------------------------------------------- UIA */
static json *uia_flows(const char *kind) {
    json *flows = j_arr();
    json *f = j_obj();
    json *stages = j_arr();
    j_push(stages, j_str(str_eq(kind, "register") ? "m.login.dummy" : "m.login.password"));
    j_set(f, "stages", stages);
    j_push(flows, f);
    return flows;
}

bool uia_check(http_req *r, http_resp *w, const char *kind,
               const char *user_id, json *body) {
    json *auth = j_geto(body, "auth");
    const char *want = str_eq(kind, "register") ? "m.login.dummy" : "m.login.password";
    if (auth) {
        const char *type = j_gets(auth, "type", "");
        const char *session = j_gets(auth, "session", NULL);
        if (str_eq(type, want)) {
            bool ok = true;
            if (str_eq(want, "m.login.password")) {
                json *ident = j_geto(auth, "identifier");
                const char *who = ident ? j_gets(ident, "user", NULL)
                                        : j_gets(auth, "user", NULL);
                char *full = NULL;
                if (who && who[0] == '@') full = xstrdup_(who);
                else if (who) full = str_fmt("@%s:%s", who, CFG.server_name);
                const char *pw = j_gets(auth, "password", "");
                char *stored = full ? db_str("SELECT password_hash FROM users "
                                             "WHERE user_id=?1", "s", full) : NULL;
                ok = full && user_id && str_eq(full, user_id) &&
                     check_password(stored, pw);
                free(stored);
                free(full);
            }
            if (ok) {
                if (session) db_run("DELETE FROM uia WHERE session=?1", "s", session);
                return true;
            }
        }
        /* a wrong or unsupported stage: repeat the challenge with an error */
        json *b = j_obj();
        j_set(b, "flows", uia_flows(kind));
        j_set(b, "params", j_obj());
        char *s = session ? xstrdup_(session) : random_string(24);
        j_set(b, "session", j_str(s));
        db_run("INSERT OR REPLACE INTO uia(session,user_id,kind,completed,params,"
               "created_ts) VALUES(?1,?2,?3,'',NULL,?4)", "sssi", s,
               user_id ? user_id : "", kind, now_ms());
        free(s);
        j_set(b, "errcode", j_str("M_FORBIDDEN"));
        j_set(b, "error", j_str("Invalid authentication"));
        resp_json(w, 401, b);
        return false;
    }
    (void)r;
    json *b = j_obj();
    j_set(b, "flows", uia_flows(kind));
    j_set(b, "params", j_obj());
    char *s = random_string(24);
    j_set(b, "session", j_str(s));
    db_run("INSERT OR REPLACE INTO uia(session,user_id,kind,completed,params,"
           "created_ts) VALUES(?1,?2,?3,'',NULL,?4)", "sssi", s,
           user_id ? user_id : "", kind, now_ms());
    free(s);
    resp_json(w, 401, b);
    return false;
}

/* ---------------------------------------------------------- transactions */
json *txn_lookup(const principal *p, const char *path, const char *txn) {
    char *s = db_str("SELECT response FROM transactions WHERE user_id=?1 AND "
                     "device_id=?2 AND txn_id=?3 AND path=?4", "ssss",
                     p->user_id, p->device_id ? p->device_id : "", txn, path);
    if (!s) return NULL;
    const char *err = NULL;
    json *v = j_parse(s, strlen(s), &err);
    free(s);
    return v;
}

void txn_remember(const principal *p, const char *path, const char *txn,
                  const json *response) {
    size_t n = 0;
    char *s = j_dump(response, &n);
    db_run("INSERT OR REPLACE INTO transactions(user_id,device_id,txn_id,path,"
           "response) VALUES(?1,?2,?3,?4,?5)", "sssss", p->user_id,
           p->device_id ? p->device_id : "", txn, path, s);
    free(s);
}

/* --------------------------------------------------------------- handlers */
/* Which specification releases this deployment claims.  A module registers the
 * releases it brings when it registers its routes, so the list is whatever this
 * build actually implements rather than a constant that has to be remembered. */
static char g_releases[32][8];
static int g_nreleases;

void spec_advertise(const char *release) {
    if (!release || g_nreleases >= 32) return;
    for (int i = 0; i < g_nreleases; i++)
        if (str_eq(g_releases[i], release)) return;
    snprintf(g_releases[g_nreleases], sizeof g_releases[0], "%s", release);
    g_nreleases++;
}

static void h_versions(http_req *r, http_resp *w, void *ud) {
    (void)r; (void)ud;
    json *b = j_obj();
    json *v = j_arr();
    static const char *VS[] = {"r0.0.1", "r0.1.0", "r0.2.0", "r0.3.0", "r0.4.0",
                               "r0.5.0", "r0.6.0", "r0.6.1", "v1.1", NULL};
    for (int i = 0; VS[i]; i++) j_push(v, j_str(VS[i]));
    for (int i = 0; i < g_nreleases; i++) j_push(v, j_str(g_releases[i]));
    j_set(b, "versions", v);
    json *uf = j_obj();
    j_set(b, "unstable_features", uf);
    resp_json(w, 200, b);
}

static void h_capabilities(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *b = j_obj();
    json *caps = j_obj();
    json *cp = j_obj();
    j_set(cp, "enabled", j_bool(true));
    j_set(caps, "m.change_password", cp);
    json *rv = j_obj();
    j_set(rv, "default", j_str(rv_default()));
    json *avail = j_obj();
    for (int i = 0; i < rv_count(); i++)
        j_set(avail, rv_index(i)->id, j_str("stable"));
    j_set(rv, "available", avail);
    j_set(caps, "m.room_versions", rv);
    json *sd = j_obj(); j_set(sd, "enabled", j_bool(true));
    j_set(caps, "m.set_displayname", sd);
    json *sa = j_obj(); j_set(sa, "enabled", j_bool(true));
    j_set(caps, "m.set_avatar_url", sa);
    json *tp = j_obj(); j_set(tp, "enabled", j_bool(false));
    j_set(caps, "m.3pid_changes", tp);
    j_set(b, "capabilities", caps);
    resp_json(w, 200, b);
    principal_free(&p);
}

static void h_login_get(http_req *r, http_resp *w, void *ud) {
    (void)r; (void)ud;
    json *b = j_obj();
    json *flows = j_arr();
    json *f = j_obj();
    j_set(f, "type", j_str("m.login.password"));
    j_push(flows, f);
    j_set(b, "flows", flows);
    resp_json(w, 200, b);
}

static char *new_token(const char *user_id, const char *device_id) {
    char *rnd = random_string(32);
    char *tok = str_fmt("syt_%s", rnd);
    free(rnd);
    db_run("INSERT INTO access_tokens(token,user_id,device_id,created_ts) "
           "VALUES(?1,?2,?3,?4)", "sssi", tok, user_id, device_id, now_ms());
    return tok;
}

static void h_register(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    json *body = require_body(r, w);
    if (!body) return;
    const char *kind = http_query(r, "kind");
    if (kind && str_eq(kind, "guest")) {
        if (CFG.guests_disabled) {
            resp_error(w, 403, "M_GUEST_ACCESS_FORBIDDEN",
                       "Guest access is disabled");
            return;
        }
    }
    if (CFG.registration_disabled) {
        resp_error(w, 403, "M_FORBIDDEN", "Registration is disabled");
        return;
    }
    const char *username = j_gets(body, "username", NULL);
    const char *password = j_gets(body, "password", NULL);
    if (username && !valid_localpart(username)) {
        resp_error(w, 400, "M_INVALID_USERNAME", "Invalid username");
        return;
    }
    char *user_id = username ? str_fmt("@%s:%s", username, CFG.server_name) : NULL;
    if (user_id) {
        char *existing = db_str("SELECT user_id FROM users WHERE user_id=?1",
                                "s", user_id);
        if (existing) {
            free(existing); free(user_id);
            resp_error(w, 400, "M_USER_IN_USE", "Desired user ID is already taken.");
            return;
        }
    }
    hs_lock();
    if (!uia_check(r, w, "register", user_id, body)) { hs_unlock(); free(user_id); return; }
    if (!user_id) {
        char *rnd = random_string(10);
        for (char *p2 = rnd; *p2; p2++) *p2 = (char)(*p2 | 0x20);
        user_id = str_fmt("@%s:%s", rnd, CFG.server_name);
        free(rnd);
    }
    char *ph = password ? hash_password(password) : NULL;
    db_run("INSERT INTO users(user_id,password_hash,created_ts,deactivated,"
           "displayname,avatar_url,is_guest) VALUES(?1,?2,?3,0,NULL,NULL,0)",
           "ssi", user_id, ph, now_ms());
    free(ph);

    json *b = j_obj();
    j_set(b, "user_id", j_str(user_id));
    if (!j_getb(body, "inhibit_login", false)) {
        const char *did = j_gets(body, "device_id", NULL);
        char *device = did ? xstrdup_(did) : random_string(10);
        for (char *p2 = device; !did && *p2; p2++)
            *p2 = (char)(*p2 & ~0x20);            /* device ids are upper case */
        db_run("INSERT OR REPLACE INTO devices(user_id,device_id,display_name,"
               "created_ts) VALUES(?1,?2,?3,?4)", "sssi", user_id, device,
               j_gets(body, "initial_device_display_name", NULL), now_ms());
        char *tok = new_token(user_id, device);
        j_set(b, "access_token", j_str(tok));
        j_set(b, "device_id", j_str(device));
        free(tok);
        free(device);
    }
    hs_unlock();
    free(user_id);
    resp_json(w, 200, b);
}

static void h_register_available(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    const char *u = http_query(r, "username");
    if (!u || !valid_localpart(u)) {
        resp_error(w, 400, "M_INVALID_USERNAME", "Invalid username");
        return;
    }
    char *uid = str_fmt("@%s:%s", u, CFG.server_name);
    char *existing = db_str("SELECT user_id FROM users WHERE user_id=?1", "s", uid);
    free(uid);
    if (existing) {
        free(existing);
        resp_error(w, 400, "M_USER_IN_USE", "Desired user ID is already taken.");
        return;
    }
    json *b = j_obj();
    j_set(b, "available", j_bool(true));
    resp_json(w, 200, b);
}

static void h_login_post(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    json *body = require_body(r, w);
    if (!body) return;
    const char *type = j_gets(body, "type", "");
    if (!str_eq(type, "m.login.password")) {
        resp_error(w, 400, "M_UNKNOWN", "Bad login type.");
        return;
    }
    json *ident = j_geto(body, "identifier");
    const char *who = NULL;
    if (ident) {
        const char *itype = j_gets(ident, "type", "");
        if (!str_eq(itype, "m.id.user")) {
            resp_error(w, 400, "M_UNKNOWN", "Unsupported identifier type");
            return;
        }
        who = j_gets(ident, "user", NULL);
    }
    if (!who) who = j_gets(body, "user", NULL);
    if (!who) {
        resp_error(w, 400, "M_UNKNOWN", "No identifier supplied");
        return;
    }
    char *uid = who[0] == '@' ? xstrdup_(who)
                              : str_fmt("@%s:%s", who, CFG.server_name);
    char *stored = db_str("SELECT password_hash FROM users WHERE user_id=?1 AND "
                          "deactivated=0", "s", uid);
    if (!check_password(stored, j_gets(body, "password", ""))) {
        free(stored); free(uid);
        resp_error(w, 403, "M_FORBIDDEN", "Invalid username or password");
        return;
    }
    free(stored);
    hs_lock();
    const char *did = j_gets(body, "device_id", NULL);
    char *device = did ? xstrdup_(did) : random_string(10);
    if (!did) for (char *p2 = device; *p2; p2++) *p2 = (char)(*p2 & ~0x20);
    if (did) {
        /* reusing a device id invalidates that device's previous token */
        db_run("DELETE FROM access_tokens WHERE user_id=?1 AND device_id=?2",
               "ss", uid, device);
    }
    db_run("INSERT OR REPLACE INTO devices(user_id,device_id,display_name,created_ts) "
           "VALUES(?1,?2,COALESCE((SELECT display_name FROM devices WHERE user_id=?1 "
           "AND device_id=?2),?3),?4)", "sssi", uid, device,
           j_gets(body, "initial_device_display_name", NULL), now_ms());
    char *tok = new_token(uid, device);
    hs_unlock();
    /* Announcing the device takes the write lock itself, so it has to happen
     * outside this one: the lock is not recursive. */
    keys_device_list_changed(uid, device, false);
    json *b = j_obj();
    j_set(b, "user_id", j_str(uid));
    j_set(b, "access_token", j_str(tok));
    j_set(b, "device_id", j_str(device));
    resp_json(w, 200, b);
    free(tok); free(device); free(uid);
}

static void h_logout(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    db_run("DELETE FROM access_tokens WHERE user_id=?1 AND device_id=?2",
           "ss", p.user_id, p.device_id ? p.device_id : "");
    db_run("DELETE FROM devices WHERE user_id=?1 AND device_id=?2",
           "ss", p.user_id, p.device_id ? p.device_id : "");
    resp_json(w, 200, j_obj());
    principal_free(&p);
}

static void h_logout_all(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    db_run("DELETE FROM access_tokens WHERE user_id=?1", "s", p.user_id);
    db_run("DELETE FROM devices WHERE user_id=?1", "s", p.user_id);
    resp_json(w, 200, j_obj());
    principal_free(&p);
}

static void h_whoami(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *b = j_obj();
    j_set(b, "user_id", j_str(p.user_id));
    if (p.device_id) j_set(b, "device_id", j_str(p.device_id));
    j_set(b, "is_guest", j_bool(p.is_guest));
    resp_json(w, 200, b);
    principal_free(&p);
}

static json *device_json(const char *user_id, const char *device_id) {
    char *dn = db_str("SELECT display_name FROM devices WHERE user_id=?1 AND "
                      "device_id=?2", "ss", user_id, device_id);
    json *d = j_obj();
    j_set(d, "device_id", j_str(device_id));
    if (dn) { j_set(d, "display_name", j_str(dn)); free(dn); }
    return d;
}

static void h_devices(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *b = j_obj();
    json *arr = j_arr();
    sqlite3_stmt *st = NULL;
    sqlite3_prepare_v2(db(), "SELECT device_id FROM devices WHERE user_id=?1 "
                             "ORDER BY created_ts, device_id", -1, &st, NULL);
    sqlite3_bind_text(st, 1, p.user_id, -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW)
        j_push(arr, device_json(p.user_id, (const char *)sqlite3_column_text(st, 0)));
    sqlite3_finalize(st);
    j_set(b, "devices", arr);
    resp_json(w, 200, b);
    principal_free(&p);
}

static void h_device_get(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    char *found = db_str("SELECT device_id FROM devices WHERE user_id=?1 AND "
                         "device_id=?2", "ss", p.user_id, r->part[0]);
    if (!found) {
        resp_error(w, 404, "M_NOT_FOUND", "Unknown device");
    } else {
        resp_json(w, 200, device_json(p.user_id, r->part[0]));
        free(found);
    }
    principal_free(&p);
}

static void h_device_put(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *body = require_body(r, w);
    if (!body) { principal_free(&p); return; }
    char *found = db_str("SELECT device_id FROM devices WHERE user_id=?1 AND "
                         "device_id=?2", "ss", p.user_id, r->part[0]);
    if (!found) {
        resp_error(w, 404, "M_NOT_FOUND", "Unknown device");
    } else {
        db_run("UPDATE devices SET display_name=?3 WHERE user_id=?1 AND device_id=?2",
               "sss", p.user_id, r->part[0], j_gets(body, "display_name", NULL));
        resp_json(w, 200, j_obj());
        free(found);
    }
    principal_free(&p);
}

static void h_device_delete(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *body = http_json(r);
    json *tmp = body ? NULL : j_obj();
    if (!uia_check(r, w, "device", p.user_id, body ? body : tmp)) {
        j_free(tmp); principal_free(&p); return;
    }
    j_free(tmp);
    db_run("DELETE FROM access_tokens WHERE user_id=?1 AND device_id=?2",
           "ss", p.user_id, r->part[0]);
    db_run("DELETE FROM devices WHERE user_id=?1 AND device_id=?2",
           "ss", p.user_id, r->part[0]);
    keys_device_list_changed(p.user_id, r->part[0], true);
    resp_json(w, 200, j_obj());
    principal_free(&p);
}

static void h_password(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *body = require_body(r, w);
    if (!body) { principal_free(&p); return; }
    if (!uia_check(r, w, "password", p.user_id, body)) { principal_free(&p); return; }
    const char *np = j_gets(body, "new_password", NULL);
    if (!np) {
        resp_error(w, 400, "M_MISSING_PARAM", "new_password is required");
        principal_free(&p);
        return;
    }
    char *ph = hash_password(np);
    db_run("UPDATE users SET password_hash=?2 WHERE user_id=?1", "ss", p.user_id, ph);
    free(ph);
    if (!j_getb(body, "logout_devices", true)) {
        /* keep other sessions */
    } else {
        db_run("DELETE FROM access_tokens WHERE user_id=?1 AND device_id<>?2",
               "ss", p.user_id, p.device_id ? p.device_id : "");
        db_run("DELETE FROM devices WHERE user_id=?1 AND device_id<>?2",
               "ss", p.user_id, p.device_id ? p.device_id : "");
    }
    resp_json(w, 200, j_obj());
    principal_free(&p);
}

static void h_deactivate(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *body = require_body(r, w);
    if (!body) { principal_free(&p); return; }
    if (!uia_check(r, w, "deactivate", p.user_id, body)) { principal_free(&p); return; }
    db_run("UPDATE users SET deactivated=1, password_hash=NULL WHERE user_id=?1",
           "s", p.user_id);
    if (media2_erasure_record && j_getb(body, "erase", false))
        media2_erasure_record(p.user_id);
    db_run("DELETE FROM access_tokens WHERE user_id=?1", "s", p.user_id);
    db_run("DELETE FROM devices WHERE user_id=?1", "s", p.user_id);
    json *b = j_obj();
    /* This server binds no third-party identifiers, so there is nothing left
     * bound to fail to unbind: the result is `success`, not `no-support`,
     * which reports that the *identity server* refused. */
    j_set(b, "id_server_unbind_result", j_str("success"));
    resp_json(w, 200, b);
    principal_free(&p);
}

/* ------------------------------------------------------------- profiles */
static void profile_broadcast(const char *user_id) {
    sqlite3_stmt *st = NULL;
    sqlite3_prepare_v2(db(), "SELECT room_id FROM room_members WHERE user_id=?1 "
                             "AND membership='join'", -1, &st, NULL);
    sqlite3_bind_text(st, 1, user_id, -1, SQLITE_TRANSIENT);
    json *rooms = j_arr();
    while (sqlite3_step(st) == SQLITE_ROW)
        j_push(rooms, j_str((const char *)sqlite3_column_text(st, 0)));
    sqlite3_finalize(st);
    for (size_t i = 0; i < j_len(rooms); i++) {
        send_result sr = room_set_membership(j_at(rooms, i)->s, user_id, user_id,
                                             "join", NULL, NULL);
        sr_free(&sr);
    }
    j_free(rooms);
}

static void h_profile_get(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    const char *uid = r->part[0];
    if (!fed_is_local_user(uid)) {
        json *remote = fed_query_profile(uid, NULL);
        if (remote) { resp_json(w, 200, remote); return; }
        resp_error(w, 404, "M_NOT_FOUND", "Profile was not found");
        return;
    }
    char *found = db_str("SELECT user_id FROM users WHERE user_id=?1", "s", uid);
    if (!found) {
        resp_error(w, 404, "M_NOT_FOUND", "Profile was not found");
        return;
    }
    free(found);
    char *dn = db_str("SELECT displayname FROM users WHERE user_id=?1", "s", uid);
    char *av = db_str("SELECT avatar_url FROM users WHERE user_id=?1", "s", uid);
    json *b = j_obj();
    if (dn) { j_set(b, "displayname", j_str(dn)); free(dn); }
    if (av) { j_set(b, "avatar_url", j_str(av)); free(av); }
    resp_json(w, 200, b);
}

static void profile_field(http_req *r, http_resp *w, const char *column,
                          const char *field) {
    const char *uid = r->part[0];
    if (str_eq(r->method, "GET") && !fed_is_local_user(uid)) {
        json *remote = fed_query_profile(uid, field);
        if (remote) { resp_json(w, 200, remote); return; }
        resp_error(w, 404, "M_NOT_FOUND", "Profile was not found");
        return;
    }
    if (str_eq(r->method, "GET")) {
        char *found = db_str("SELECT user_id FROM users WHERE user_id=?1", "s", uid);
        if (!found) { resp_error(w, 404, "M_NOT_FOUND", "Profile was not found"); return; }
        free(found);
        char *sql = str_fmt("SELECT %s FROM users WHERE user_id=?1", column);
        char *v = db_str(sql, "s", uid);
        free(sql);
        json *b = j_obj();
        if (v) { j_set(b, field, j_str(v)); free(v); }
        resp_json(w, 200, b);
        return;
    }
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    if (!str_eq(p.user_id, uid)) {
        resp_error(w, 403, "M_FORBIDDEN", "Cannot set another user's profile");
        principal_free(&p);
        return;
    }
    json *body = require_body(r, w);
    if (!body) { principal_free(&p); return; }
    const char *val = j_gets(body, field, NULL);
    hs_lock();
    char *sql = str_fmt("UPDATE users SET %s=?2 WHERE user_id=?1", column);
    db_run(sql, "ss", uid, val);
    free(sql);
    profile_broadcast(uid);
    hs_unlock();
    stream_notify();
    resp_json(w, 200, j_obj());
    principal_free(&p);
}

static void h_displayname(http_req *r, http_resp *w, void *ud) {
    (void)ud; profile_field(r, w, "displayname", "displayname");
}
static void h_avatar(http_req *r, http_resp *w, void *ud) {
    (void)ud; profile_field(r, w, "avatar_url", "avatar_url");
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

void api_register_account(http_server *s) {
    http_route(s, "GET", "/_matrix/client/versions", h_versions);
    route_both(s, "GET", "/capabilities", h_capabilities);
    route_both(s, "GET", "/login", h_login_get);
    route_both(s, "POST", "/login", h_login_post);
    route_both(s, "POST", "/logout", h_logout);
    route_both(s, "POST", "/logout/all", h_logout_all);
    route_both(s, "POST", "/register", h_register);
    route_both(s, "GET", "/register/available", h_register_available);
    route_both(s, "GET", "/account/whoami", h_whoami);
    route_both(s, "POST", "/account/password", h_password);
    route_both(s, "POST", "/account/deactivate", h_deactivate);
    route_both(s, "GET", "/devices", h_devices);
    route_both(s, "GET", "/devices/:", h_device_get);
    route_both(s, "PUT", "/devices/:", h_device_put);
    route_both(s, "DELETE", "/devices/:", h_device_delete);
    route_both(s, "GET", "/profile/:", h_profile_get);
    route_both(s, "GET", "/profile/:/displayname", h_displayname);
    route_both(s, "PUT", "/profile/:/displayname", h_displayname);
    route_both(s, "GET", "/profile/:/avatar_url", h_avatar);
    route_both(s, "PUT", "/profile/:/avatar_url", h_avatar);
}

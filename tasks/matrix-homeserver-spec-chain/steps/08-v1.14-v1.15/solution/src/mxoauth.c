#define _POSIX_C_SOURCE 200809L
/* Next-generation authentication (MSC3861) and the rest of the Client-Server
 * additions of releases v1.14 and v1.15. */
#include "mxoauth.h"

#include "mxapi.h"
#include "mxdb.h"
#include "mxevent.h"
#include "mxfed.h"
#include "mxroom.h"
#include "mxserver.h"
#include "mxslide.h"
#include "mxutil.h"

#include <ctype.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <yaml.h>

#define CS   "/_matrix/client/v3"
#define CSR  "/_matrix/client/r0"
#define CS1  "/_matrix/client/v1"
#define FED1 "/_matrix/federation/v1"

/* How long an introspection result is trusted before it is asked for again. */
#define INTROSPECT_TTL_MS   60000
/* How long the authorization server metadata is cached. */
#define METADATA_TTL_MS     300000
#define HIER_MAX_ROOMS      100

/* ============================================================= config ===== */
/* main.c parses the configuration file and keeps only what mxserver.h
 * declares, and mxserver.h is not ours to extend, so the msc3861 block is read
 * from the same file again.  The path comes from the command line the process
 * was started with. */
static pthread_mutex_t g_cfg_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_cfg_loaded;
static bool g_enabled;
static char *g_issuer, *g_client_id, *g_client_auth_method, *g_client_secret;
static char *g_admin_token, *g_account_management_url;

static json *yaml_node(yaml_parser_t *p, yaml_event_t *ev) {
    switch (ev->type) {
    case YAML_SCALAR_EVENT: {
        const char *v = (const char *)ev->data.scalar.value;
        size_t len = ev->data.scalar.length;
        bool quoted = ev->data.scalar.style == YAML_SINGLE_QUOTED_SCALAR_STYLE ||
                      ev->data.scalar.style == YAML_DOUBLE_QUOTED_SCALAR_STYLE;
        if (!quoted) {
            if (!strcmp(v, "true")) return j_bool(true);
            if (!strcmp(v, "false")) return j_bool(false);
            if (!strcmp(v, "null") || !strcmp(v, "~") || len == 0) return j_null();
            char *end = NULL;
            long long n = strtoll(v, &end, 10);
            if (end && *end == 0 && len > 0) return j_int((int64_t)n);
        }
        return j_strn(v, len);
    }
    case YAML_SEQUENCE_START_EVENT: {
        json *arr = j_arr();
        for (;;) {
            yaml_event_t e;
            if (!yaml_parser_parse(p, &e)) return arr;
            if (e.type == YAML_SEQUENCE_END_EVENT) { yaml_event_delete(&e); break; }
            json *v = yaml_node(p, &e);
            yaml_event_delete(&e);
            if (v) j_push(arr, v);
        }
        return arr;
    }
    case YAML_MAPPING_START_EVENT: {
        json *obj = j_obj();
        for (;;) {
            yaml_event_t k;
            if (!yaml_parser_parse(p, &k)) return obj;
            if (k.type == YAML_MAPPING_END_EVENT) { yaml_event_delete(&k); break; }
            char key[256];
            snprintf(key, sizeof key, "%s", (const char *)k.data.scalar.value);
            yaml_event_delete(&k);
            yaml_event_t v;
            if (!yaml_parser_parse(p, &v)) return obj;
            json *val = yaml_node(p, &v);
            yaml_event_delete(&v);
            if (val) j_set(obj, key, val);
        }
        return obj;
    }
    default:
        return NULL;
    }
}

static json *load_yaml(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    yaml_parser_t p;
    yaml_parser_initialize(&p);
    yaml_parser_set_input_file(&p, f);
    json *root = NULL;
    for (;;) {
        yaml_event_t ev;
        if (!yaml_parser_parse(&p, &ev)) break;
        if (ev.type == YAML_STREAM_END_EVENT) { yaml_event_delete(&ev); break; }
        if (ev.type == YAML_MAPPING_START_EVENT ||
            ev.type == YAML_SEQUENCE_START_EVENT) {
            root = yaml_node(&p, &ev);
            yaml_event_delete(&ev);
            break;
        }
        yaml_event_delete(&ev);
    }
    yaml_parser_delete(&p);
    fclose(f);
    return root;
}

/* The --config argument this process was started with. */
static char *config_path(void) {
    const char *env = getenv("DENDRITE_CONFIG");
    if (env && *env) return xstrdup_(env);
    FILE *f = fopen("/proc/self/cmdline", "rb");
    if (!f) return xstrdup_("dendrite.yaml");
    char buf[8192];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    char *out = NULL;
    for (size_t i = 0; i < n;) {
        const char *a = buf + i;
        size_t len = strlen(a);
        const char *f2 = a;
        while (*f2 == '-') f2++;
        if (str_prefix(f2, "config=")) { out = xstrdup_(f2 + 7); break; }
        if (!strcmp(f2, "config") && i + len + 1 < n) {
            out = xstrdup_(buf + i + len + 1);
            break;
        }
        i += len + 1;
    }
    return out ? out : xstrdup_("dendrite.yaml");
}

static void cfg_load(void) {
    pthread_mutex_lock(&g_cfg_lock);
    if (g_cfg_loaded) { pthread_mutex_unlock(&g_cfg_lock); return; }
    g_cfg_loaded = true;
    char *path = config_path();
    json *cfg = load_yaml(path);
    free(path);
    json *exp = cfg ? j_geto(cfg, "experimental_features") : NULL;
    json *blk = exp ? j_geto(exp, "msc3861") : NULL;
    if (blk) {
        g_enabled = j_getb(blk, "enabled", false);
        const char *s;
        if ((s = j_gets(blk, "issuer", NULL))) g_issuer = xstrdup_(s);
        if ((s = j_gets(blk, "client_id", NULL))) g_client_id = xstrdup_(s);
        if ((s = j_gets(blk, "client_auth_method", NULL)))
            g_client_auth_method = xstrdup_(s);
        if ((s = j_gets(blk, "client_secret", NULL))) g_client_secret = xstrdup_(s);
        if ((s = j_gets(blk, "admin_token", NULL))) g_admin_token = xstrdup_(s);
        if ((s = j_gets(blk, "account_management_url", NULL)))
            g_account_management_url = xstrdup_(s);
    }
    if (!g_client_auth_method) g_client_auth_method = xstrdup_("client_secret_basic");
    j_free(cfg);
    if (g_enabled)
        fprintf(stderr, "mxoauth: delegated authentication enabled, issuer=%s\n",
                g_issuer ? g_issuer : "(unset)");
    pthread_mutex_unlock(&g_cfg_lock);
}

bool oauth_delegated(void) {
    cfg_load();
    return g_enabled && g_issuer && *g_issuer;
}

const char *oauth_issuer(void) { cfg_load(); return g_issuer; }
const char *oauth_account_management_url(void) {
    cfg_load();
    return g_account_management_url;
}

bool oauth_is_admin_token(const char *token) {
    cfg_load();
    return token && g_admin_token && *g_admin_token && str_eq(token, g_admin_token);
}

/* ========================================== authorization server metadata = */
static pthread_mutex_t g_meta_lock = PTHREAD_MUTEX_INITIALIZER;
static char *g_meta_text;
static int64_t g_meta_ts;

static char *metadata_url(void) {
    if (!g_issuer || !*g_issuer) return NULL;
    size_t n = strlen(g_issuer);
    while (n > 0 && g_issuer[n - 1] == '/') n--;
    char *base = malloc(n + 1);
    if (!base) abort();
    memcpy(base, g_issuer, n);
    base[n] = 0;
    char *url = str_fmt("%s/.well-known/oauth-authorization-server", base);
    free(base);
    return url;
}

/* The cached RFC 8414 document, refetched when it is older than the TTL.
 * Caller frees. */
static json *metadata_get(bool force) {
    cfg_load();
    if (!oauth_delegated()) return NULL;
    pthread_mutex_lock(&g_meta_lock);
    if (!force && g_meta_text && now_ms() - g_meta_ts < METADATA_TTL_MS) {
        json *v = j_parse(g_meta_text, strlen(g_meta_text), NULL);
        pthread_mutex_unlock(&g_meta_lock);
        return v;
    }
    pthread_mutex_unlock(&g_meta_lock);

    char *url = metadata_url();
    if (!url) return NULL;
    const char *hdrs[] = { "Accept: application/json" };
    http_client_resp *resp = http_get(url, hdrs, 1);
    free(url);
    json *doc = NULL;
    if (resp && resp->status == 200 && resp->body)
        doc = j_parse(resp->body, resp->body_len, NULL);
    http_client_resp_free(resp);
    if (!doc || doc->type != J_OBJECT) {
        j_free(doc);
        /* Fall back on whatever was cached before, however old. */
        pthread_mutex_lock(&g_meta_lock);
        json *v = g_meta_text ? j_parse(g_meta_text, strlen(g_meta_text), NULL) : NULL;
        pthread_mutex_unlock(&g_meta_lock);
        return v;
    }
    size_t n = 0;
    char *text = j_dump(doc, &n);
    pthread_mutex_lock(&g_meta_lock);
    free(g_meta_text);
    g_meta_text = text;
    g_meta_ts = now_ms();
    pthread_mutex_unlock(&g_meta_lock);
    return doc;
}

/* ================================================ RFC 7662 introspection == */
static void oauth_tables(void) {
    db_exec("CREATE TABLE IF NOT EXISTS oauth_tokens("
            "  token TEXT PRIMARY KEY, user_id TEXT, device_id TEXT,"
            "  checked_ms INTEGER, active INTEGER DEFAULT 1);");
    db_exec("CREATE TABLE IF NOT EXISTS oauth_subjects("
            "  sub TEXT PRIMARY KEY, user_id TEXT, created_ts INTEGER);");
    db_exec("CREATE TABLE IF NOT EXISTS content_reports("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT, kind TEXT, target TEXT,"
            "  event_id TEXT, sender TEXT, reason TEXT, score INTEGER,"
            "  created_ts INTEGER);");
}

/* MSC2967's device scope, under the unstable prefix MSC3861 deployments use
 * and under the stable one. */
static char *device_from_scope(const char *scope) {
    static const char *PREFIX[] = {
        "urn:matrix:org.matrix.msc2967.client:device:",
        "urn:matrix:client:device:",
        NULL
    };
    if (!scope) return NULL;
    const char *p = scope;
    while (*p) {
        while (*p == ' ') p++;
        const char *end = strchr(p, ' ');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (!len) break;
        for (int i = 0; PREFIX[i]; i++) {
            size_t pl = strlen(PREFIX[i]);
            if (len > pl && strncmp(p, PREFIX[i], pl) == 0) {
                char *out = malloc(len - pl + 1);
                if (!out) abort();
                memcpy(out, p + pl, len - pl);
                out[len - pl] = 0;
                return out;
            }
        }
        if (!end) break;
        p = end + 1;
    }
    return NULL;
}

static bool scope_has_api(const char *scope) {
    if (!scope) return false;
    return strstr(scope, "urn:matrix:org.matrix.msc2967.client:api:*") != NULL ||
           strstr(scope, "urn:matrix:client:api:*") != NULL;
}

/* `sub` names the user at the issuer.  A full Matrix id is used as it stands;
 * otherwise the `username` claim names the localpart, and failing that the
 * subject is remembered against a localpart derived from it. */
static char *user_from_subject(const json *resp) {
    const char *sub = j_gets(resp, "sub", NULL);
    const char *username = j_gets(resp, "username", NULL);
    if (sub && sub[0] == '@' && strchr(sub, ':')) return xstrdup_(sub);
    if (username && username[0] == '@' && strchr(username, ':'))
        return xstrdup_(username);
    if (username && *username && valid_localpart(username))
        return str_fmt("@%s:%s", username, CFG.server_name);
    if (!sub || !*sub) return NULL;
    char *known = db_str("SELECT user_id FROM oauth_subjects WHERE sub=?1", "s", sub);
    if (known) return known;
    buf lp = {0};
    for (const char *p = sub; *p; p++) {
        char c = (char)tolower((unsigned char)*p);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '.' || c == '_' || c == '-' || c == '/' || c == '=')
            buf_add(&lp, &c, 1);
        else
            buf_puts(&lp, "-");
    }
    char *local = buf_take(&lp, NULL);
    buf_free(&lp);
    char *uid = str_fmt("@%s:%s", local && *local ? local : "oauth", CFG.server_name);
    free(local);
    hs_lock();
    db_run("INSERT OR IGNORE INTO oauth_subjects(sub,user_id,created_ts) "
           "VALUES(?1,?2,?3)", "ssi", sub, uid, now_ms());
    hs_unlock();
    stream_notify();
    return uid;
}

/* Performs the introspection call.  Returns the parsed response or NULL. */
static json *introspect(const char *token) {
    json *meta = metadata_get(false);
    const char *endpoint = meta ? j_gets(meta, "introspection_endpoint", NULL) : NULL;
    if (!endpoint) { j_free(meta); return NULL; }
    char *url = xstrdup_(endpoint);
    j_free(meta);

    char *enc_tok = url_encode(token);
    buf body = {0};
    buf_printf(&body, "token=%s&token_type_hint=access_token", enc_tok);
    free(enc_tok);

    char *auth = NULL;
    if (str_eq(g_client_auth_method, "client_secret_post")) {
        char *ci = url_encode(g_client_id ? g_client_id : "");
        char *cs = url_encode(g_client_secret ? g_client_secret : "");
        buf_printf(&body, "&client_id=%s&client_secret=%s", ci, cs);
        free(ci); free(cs);
    } else if (str_eq(g_client_auth_method, "none")) {
        char *ci = url_encode(g_client_id ? g_client_id : "");
        buf_printf(&body, "&client_id=%s", ci);
        free(ci);
    } else {                                   /* client_secret_basic */
        char *ci = url_encode(g_client_id ? g_client_id : "");
        char *cs = url_encode(g_client_secret ? g_client_secret : "");
        char *pair = str_fmt("%s:%s", ci, cs);
        char *b64 = b64_encode(pair, strlen(pair), false, true);
        auth = str_fmt("Authorization: Basic %s", b64);
        free(ci); free(cs); free(pair); free(b64);
    }

    const char *hdrs[3];
    int nh = 0;
    hdrs[nh++] = "Content-Type: application/x-www-form-urlencoded";
    hdrs[nh++] = "Accept: application/json";
    if (auth) hdrs[nh++] = auth;

    size_t blen = 0;
    char *btext = buf_take(&body, &blen);
    buf_free(&body);
    http_client_resp *resp = http_send("POST", url, btext, blen, hdrs, nh);
    free(btext);
    free(url);
    free(auth);

    json *out = NULL;
    if (resp && resp->status == 200 && resp->body)
        out = j_parse(resp->body, resp->body_len, NULL);
    http_client_resp_free(resp);
    if (out && out->type != J_OBJECT) { j_free(out); out = NULL; }
    return out;
}

/* Makes the (user, device) the token stands for resolvable by the ordinary
 * require_auth() path, so that every handler keeps working unchanged. */
static void materialise(const char *token, const char *user_id,
                        const char *device_id) {
    hs_lock();
    db_run("INSERT OR IGNORE INTO users(user_id,password_hash,created_ts,"
           "deactivated,displayname,avatar_url,is_guest) "
           "VALUES(?1,NULL,?2,0,NULL,NULL,0)", "si", user_id, now_ms());
    if (device_id && *device_id)
        db_run("INSERT OR IGNORE INTO devices(user_id,device_id,display_name,"
               "created_ts) VALUES(?1,?2,NULL,?3)", "ssi", user_id, device_id,
               now_ms());
    db_run("INSERT INTO access_tokens(token,user_id,device_id,created_ts) "
           "VALUES(?1,?2,?3,?4) ON CONFLICT(token) DO UPDATE SET user_id=?2, "
           "device_id=?3", "sssi", token, user_id, device_id, now_ms());
    db_run("INSERT INTO oauth_tokens(token,user_id,device_id,checked_ms,active) "
           "VALUES(?1,?2,?3,?4,1) ON CONFLICT(token) DO UPDATE SET user_id=?2, "
           "device_id=?3, checked_ms=?4, active=1", "sssi", token, user_id,
           device_id, now_ms());
    hs_unlock();
    stream_notify();
}

static void forget_token(const char *token) {
    hs_lock();
    db_run("DELETE FROM access_tokens WHERE token=?1", "s", token);
    db_run("INSERT INTO oauth_tokens(token,user_id,device_id,checked_ms,active) "
           "VALUES(?1,NULL,NULL,?2,0) ON CONFLICT(token) DO UPDATE SET "
           "checked_ms=?2, active=0", "si", token, now_ms());
    hs_unlock();
    stream_notify();
}

static void write_unknown_token(http_resp *w) {
    json *b = j_obj();
    j_set(b, "errcode", j_str("M_UNKNOWN_TOKEN"));
    j_set(b, "error", j_str("Invalid access token passed."));
    j_set(b, "soft_logout", j_bool(false));
    resp_json(w, 401, b);
}

/* Resolves one bearer token through the issuer, with a short-lived cache.
 * Returns false when the token is not active, having answered `w`. */
static bool resolve_token(const char *token, http_resp *w) {
    int64_t checked = 0, active = 0;
    bool cached = db_int("SELECT checked_ms FROM oauth_tokens WHERE token=?1",
                         &checked, "s", token);
    if (cached) {
        db_int("SELECT active FROM oauth_tokens WHERE token=?1", &active, "s", token);
        if (now_ms() - checked < INTROSPECT_TTL_MS) {
            if (active) return true;
            write_unknown_token(w);
            return false;
        }
    }
    json *resp = introspect(token);
    if (!resp || !j_getb(resp, "active", false)) {
        j_free(resp);
        forget_token(token);
        write_unknown_token(w);
        return false;
    }
    const char *scope = j_gets(resp, "scope", "");
    char *user_id = user_from_subject(resp);
    char *device_id = device_from_scope(scope);
    if (!user_id || (!scope_has_api(scope) && !device_id)) {
        free(user_id); free(device_id);
        j_free(resp);
        forget_token(token);
        write_unknown_token(w);
        return false;
    }
    materialise(token, user_id, device_id ? device_id : "");
    free(user_id);
    free(device_id);
    j_free(resp);
    return true;
}

/* ================================================ path + response helpers = */
/* strstr over a response body, which is not NUL-terminated. */
static bool body_has(const http_resp *w, const char *needle) {
    if (!w || !w->body.b || !w->body.n || !needle) return false;
    size_t n = strlen(needle);
    if (n == 0 || n > w->body.n) return false;
    for (size_t i = 0; i + n <= w->body.n; i++)
        if (memcmp(w->body.b + i, needle, n) == 0) return true;
    return false;
}

/* `pattern` uses ':' for one path segment, exactly like a route. */
static bool path_match(const char *path, const char *pattern) {
    if (!path || !pattern) return false;
    const char *p = path, *q = pattern;
    for (;;) {
        const char *pe = strchr(p, '/');
        const char *qe = strchr(q, '/');
        size_t pl = pe ? (size_t)(pe - p) : strlen(p);
        size_t ql = qe ? (size_t)(qe - q) : strlen(q);
        if (!(ql == 1 && q[0] == ':')) {
            if (pl != ql || strncmp(p, q, pl) != 0) return false;
        }
        if (!pe && !qe) return true;
        if (!pe || !qe) return false;
        p = pe + 1;
        q = qe + 1;
    }
}

/* The content of a room's zero-length-state-key state event; caller frees. */
static json *state_content_of(const char *room_id, const char *type) {
    json *ev = room_state_event(room_id, type, "");
    if (!ev) return NULL;
    json *c = j_geto(ev, "content");
    json *out = c ? j_clone(c) : NULL;
    j_free(ev);
    return out;
}

/* The tail of a Client-Server path: "/_matrix/client/v3/login" -> "/login". */
static const char *cs_tail(const char *path) {
    if (!path || !str_prefix(path, "/_matrix/client/")) return NULL;
    return strchr(path + 16, '/');
}

/* ================================================== the m.topic block ===== */
/* v1.15 adds `m.topic`, a TextContentBlock, to m.room.topic.  `topic` "SHOULD
 * duplicate the content of the text/plain representation", so whichever half
 * the sender supplied, both are stored and both are served back. */
static bool topic_normalise_ex(json *content, bool expand) {
    if (!content || content->type != J_OBJECT) return false;
    json *blk = j_geto(content, "m.topic");
    json *plain = j_get(content, "topic");
    if (blk) {
        json *texts = j_geta(blk, "m.text");
        const char *chosen = NULL;
        for (size_t i = 0; texts && i < j_len(texts); i++) {
            json *rep = j_at(texts, i);
            if (!rep || rep->type != J_OBJECT) continue;
            /* "text/plain" is the default, so an explicit one says nothing. */
            const char *mt = j_gets(rep, "mimetype", "text/plain");
            const char *body = j_gets(rep, "body", NULL);
            if (!body) continue;
            if (!chosen) chosen = body;
            if (str_eq(mt, "text/plain")) { chosen = body; break; }
        }
        if (!chosen) return false;
        if (plain && j_is_str(plain) && str_eq(plain->s, chosen)) return false;
        j_set(content, "topic", j_str(chosen));
        return true;
    }
    if (plain && j_is_str(plain) && expand) {
        /* Only where the server is *composing* the event -- `createRoom`'s
         * `topic` -- does it write both representations.  An `m.room.topic`
         * a client sent is stored as the client sent it: the plain text
         * "SHOULD duplicate the content of the text/plain representation",
         * which constrains the sender, not the server.  `text/plain` is the
         * default mimetype, so writing it out says nothing. */
        json *rep = j_obj();
        j_set(rep, "body", j_str(plain->s));
        json *texts = j_arr();
        j_push(texts, rep);
        json *b = j_obj();
        j_set(b, "m.text", texts);
        j_set(content, "m.topic", b);
        return true;
    }
    return false;
}

static bool topic_normalise(json *content) {
    return topic_normalise_ex(content, false);
}

/* `createRoom` composes the event itself, so it writes both halves. */
bool topic_compose(json *content) {
    return topic_normalise_ex(content, true);
}

/* Walks a response looking for m.room.topic events and keeps their two
 * representations in step. */
static bool topic_walk(json *v) {
    bool changed = false;
    if (!v) return false;
    if (v->type == J_OBJECT) {
        if (str_eq(j_gets(v, "type", ""), "m.room.topic")) {
            json *c = j_geto(v, "content");
            if (c) changed |= topic_normalise(c);
            json *pc = j_deep_get(v, "unsigned.prev_content");
            if (pc) changed |= topic_normalise(pc);
        }
        for (size_t i = 0; i < j_len(v); i++) changed |= topic_walk(j_at(v, i));
    } else if (v->type == J_ARRAY) {
        for (size_t i = 0; i < j_len(v); i++) changed |= topic_walk(j_at(v, i));
    }
    return changed;
}

static void topic_request_fix(http_req *r) {
    const char *t = cs_tail(r->path);
    if (!t) return;
    bool state_put = str_eq(r->method, "PUT") &&
                     (path_match(t, "/rooms/:/state/:") ||
                      path_match(t, "/rooms/:/state/:/:")) &&
                     r->nparts > 1 && str_eq(r->part[1], "m.room.topic");
    bool create = str_eq(r->method, "POST") && str_eq(t, "/createRoom");
    if (!state_put && !create) return;
    json *body = http_json(r);
    if (!body || body->type != J_OBJECT) return;
    bool changed = false;
    if (state_put) {
        changed = topic_normalise(body);
    } else {
        json *init = j_geta(body, "initial_state");
        for (size_t i = 0; init && i < j_len(init); i++) {
            json *ev = j_at(init, i);
            if (!ev || !str_eq(j_gets(ev, "type", ""), "m.room.topic")) continue;
            json *c = j_geto(ev, "content");
            if (c) changed |= topic_normalise(c);
        }
    }
    if (changed) slide_set_body(r, body);
}

/* ================================================ hierarchy  enrichment === */
/* v1.15 adds allowed_room_ids, encryption and room_version to the entries of
 * /hierarchy (and of the federation form). */
static void hierarchy_extra(json *entry) {
    if (!entry || entry->type != J_OBJECT) return;
    const char *room_id = j_gets(entry, "room_id", NULL);
    if (!room_id || !room_exists(room_id)) return;
    if (!j_get(entry, "room_version")) {
        char *ver = room_version_of(room_id);
        if (ver) { j_set(entry, "room_version", j_str(ver)); free(ver); }
    }
    if (!j_get(entry, "encryption")) {
        json *enc = room_state_event(room_id, "m.room.encryption", "");
        const char *algo = enc ? j_gets(j_geto(enc, "content"), "algorithm", NULL)
                               : NULL;
        if (algo) j_set(entry, "encryption", j_str(algo));
        j_free(enc);
    }
    if (!j_get(entry, "allowed_room_ids")) {
        json *jr = room_state_event(room_id, "m.room.join_rules", "");
        json *c = jr ? j_geto(jr, "content") : NULL;
        const char *rule = c ? j_gets(c, "join_rule", "") : "";
        json *allow = c ? j_geta(c, "allow") : NULL;
        if (allow && (str_eq(rule, "restricted") || str_eq(rule, "knock_restricted"))) {
            json *ids = j_arr();
            for (size_t i = 0; i < j_len(allow); i++) {
                json *e = j_at(allow, i);
                if (!e || e->type != J_OBJECT) continue;
                if (!str_eq(j_gets(e, "type", ""), "m.room_membership")) continue;
                const char *rid = j_gets(e, "room_id", NULL);
                if (rid) j_push(ids, j_str(rid));
            }
            if (j_len(ids)) j_set(entry, "allowed_room_ids", ids);
            else j_free(ids);
        }
        j_free(jr);
    }
}

static void hierarchy_fix(const http_req *r, http_resp *w) {
    bool client = path_match(r->path, CS1 "/rooms/:/hierarchy");
    bool fed = path_match(r->path, FED1 "/hierarchy/:");
    if (!client && !fed) return;
    json *b = slide_resp_json(w);
    if (!b || b->type != J_OBJECT) { j_free(b); return; }
    json *rooms = j_geta(b, "rooms");
    for (size_t i = 0; rooms && i < j_len(rooms); i++) hierarchy_extra(j_at(rooms, i));
    json *children = j_geta(b, "children");
    for (size_t i = 0; children && i < j_len(children); i++)
        hierarchy_extra(j_at(children, i));
    hierarchy_extra(j_geto(b, "room"));
    slide_resp_replace(w, b);
}

/* ================================================= the gate hooks ========= */
static bool legacy_refused(http_req *r, http_resp *w) {
    const char *t = cs_tail(r->path);
    if (!t || !str_eq(r->method, "POST")) return false;
    if (!str_eq(t, "/login") && !str_eq(t, "/register") &&
        !str_eq(t, "/account/password") && !str_eq(t, "/account/deactivate"))
        return false;
    char *msg;
    const char *url = oauth_account_management_url();
    if (url)
        msg = str_fmt("This homeserver uses OAuth 2.0 for authentication; "
                      "use %s instead.", url);
    else
        msg = xstrdup_("This homeserver uses OAuth 2.0 for authentication.");
    resp_error(w, 404, "M_UNRECOGNIZED", msg);
    free(msg);
    return true;
}

bool oauth_before(http_req *r, http_resp *w) {
    topic_request_fix(r);
    if (!oauth_delegated()) return true;
    if (legacy_refused(r, w)) return false;
    char *tok = slide_bearer_token(r);
    if (!tok) return true;
    if (oauth_is_admin_token(tok) ||
        (CFG.registration_shared_secret && *CFG.registration_shared_secret &&
         str_eq(tok, CFG.registration_shared_secret))) {
        free(tok);
        return true;
    }
    bool ok = resolve_token(tok, w);
    free(tok);
    return ok;
}

void oauth_after(http_req *r, http_resp *w) {
    if (w->status != 200) return;
    hierarchy_fix(r, w);

    const char *t = cs_tail(r->path);
    if (t && (path_match(t, "/rooms/:/state/m.room.topic") ||
              path_match(t, "/rooms/:/state/m.room.topic/"))) {
        json *b = slide_resp_json(w);
        if (b && b->type == J_OBJECT && topic_normalise(b)) slide_resp_replace(w, b);
        else j_free(b);
    } else if (body_has(w, "m.room.topic")) {
        json *b = slide_resp_json(w);
        if (b && topic_walk(b)) slide_resp_replace(w, b);
        else j_free(b);
    }

    if (!oauth_delegated() || !t) return;
    if (str_eq(t, "/capabilities")) {
        json *b = slide_resp_json(w);
        json *caps = b ? j_geto(b, "capabilities") : NULL;
        if (!caps) { j_free(b); return; }
        json *cp = j_obj();
        j_set(cp, "enabled", j_bool(false));
        j_set(caps, "m.change_password", cp);
        json *tp = j_obj();
        j_set(tp, "enabled", j_bool(false));
        j_set(caps, "m.3pid_changes", tp);
        slide_resp_replace(w, b);
    } else if (str_eq(t, "/login") && str_eq(r->method, "GET")) {
        json *b = j_obj();
        j_set(b, "flows", j_arr());
        slide_resp_replace(w, b);
    }
}

/* ================================================= GET /auth_metadata ==== */
static void h_auth_metadata(http_req *r, http_resp *w, void *ud) {
    (void)r; (void)ud;
    if (!oauth_delegated()) {
        resp_error(w, 404, "M_UNRECOGNIZED",
                   "This homeserver does not use delegated authentication");
        return;
    }
    json *meta = metadata_get(false);
    if (!meta) {
        resp_error(w, 503, "M_UNKNOWN",
                   "The authorization server metadata could not be fetched");
        return;
    }
    resp_set_header(w, "Cache-Control", "max-age=300, public");
    resp_json(w, 200, meta);
}

/* ============================================ POST /users/{userId}/report = */
static void h_report_user(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    json *body = require_body(r, w);
    if (!body) { principal_free(&p); return; }
    json *reason = j_get(body, "reason");
    if (!reason || !j_is_str(reason)) {
        resp_error(w, 400, "M_INVALID_PARAM", "'reason' must be a string");
        principal_free(&p);
        return;
    }
    const char *uid = r->part[0];
    if (!uid || uid[0] != '@') {
        resp_error(w, 400, "M_INVALID_PARAM", "Invalid user id");
        principal_free(&p);
        return;
    }
    /* v1.15 lets a server hide whether the reported user exists, and this one
     * does: an unknown local user and a user on another server are answered
     * exactly as a known local user is. */
    hs_lock();
    db_run("INSERT INTO content_reports(kind,target,event_id,sender,reason,"
           "score,created_ts) VALUES('user',?1,NULL,?2,?3,NULL,?4)", "sssi",
           uid, p.user_id, reason->s, now_ms());
    hs_unlock();
    stream_notify();
    resp_json(w, 200, j_obj());
    principal_free(&p);
}

/* ======================================== room summaries and  hierarchy === */
static bool room_world_readable(const char *room_id) {
    json *c = state_content_of(room_id, "m.room.history_visibility");
    bool wr = c && str_eq(j_gets(c, "history_visibility", "shared"), "world_readable");
    j_free(c);
    return wr;
}

/* The PublishedRoomsChunk plus the v1.4 and v1.15 extensions. */
static json *room_summary_json(const char *room_id) {
    json *e = j_obj();
    j_set(e, "room_id", j_str(room_id));
    json *c = state_content_of(room_id, "m.room.name");
    if (c && j_is_str(j_get(c, "name"))) j_set(e, "name", j_str(j_gets(c, "name", "")));
    j_free(c);
    c = state_content_of(room_id, "m.room.topic");
    if (c) {
        topic_normalise(c);
        if (j_is_str(j_get(c, "topic"))) j_set(e, "topic", j_str(j_gets(c, "topic", "")));
    }
    j_free(c);
    c = state_content_of(room_id, "m.room.avatar");
    if (c && j_is_str(j_get(c, "url")))
        j_set(e, "avatar_url", j_str(j_gets(c, "url", "")));
    j_free(c);
    c = state_content_of(room_id, "m.room.canonical_alias");
    if (c && j_is_str(j_get(c, "alias")))
        j_set(e, "canonical_alias", j_str(j_gets(c, "alias", "")));
    j_free(c);
    c = state_content_of(room_id, "m.room.create");
    if (c && j_is_str(j_get(c, "type")))
        j_set(e, "room_type", j_str(j_gets(c, "type", "")));
    j_free(c);
    c = state_content_of(room_id, "m.room.join_rules");
    j_set(e, "join_rule", j_str(c ? j_gets(c, "join_rule", "invite") : "invite"));
    j_free(c);
    c = state_content_of(room_id, "m.room.guest_access");
    j_set(e, "guest_can_join",
          j_bool(c && str_eq(j_gets(c, "guest_access", "forbidden"), "can_join")));
    j_free(c);
    j_set(e, "world_readable", j_bool(room_world_readable(room_id)));
    int64_t joined = 0;
    db_int("SELECT COUNT(*) FROM room_members WHERE room_id=?1 AND "
           "membership='join'", &joined, "s", room_id);
    j_set(e, "num_joined_members", j_int(joined));
    hierarchy_extra(e);
    return e;
}

/* The stripped m.space.child events of `room_id`. */
static json *children_state(const char *room_id, bool suggested_only) {
    json *out = j_arr();
    json *map = room_state_map(room_id);
    for (size_t i = 0; i < j_len(map); i++) {
        json *ev = j_at(map, i);
        if (!str_eq(j_gets(ev, "type", ""), "m.space.child")) continue;
        json *c = j_geto(ev, "content");
        if (!c || !j_geta(c, "via")) continue;
        if (suggested_only && !j_getb(c, "suggested", false)) continue;
        json *st = event_to_stripped(ev);
        j_set(st, "origin_server_ts", j_int(j_geti(ev, "origin_server_ts", 0)));
        j_push(out, st);
    }
    j_free(map);
    return out;
}

/* May `user_id` (which may be NULL) see this room in a hierarchy or summary? */
static bool may_summarise(const char *room_id, const char *user_id) {
    if (user_id) {
        char *m = room_membership(room_id, user_id);
        bool in = m && (str_eq(m, "join") || str_eq(m, "invite"));
        free(m);
        if (in) return true;
    }
    json *c = state_content_of(room_id, "m.room.join_rules");
    const char *rule = c ? j_gets(c, "join_rule", "invite") : "invite";
    /* Unauthenticated callers only get the rooms the specification lists:
     * public, knock and knock_restricted, or a world-readable history.  A
     * restricted room is reachable, but only once we know who is asking. */
    bool open = str_eq(rule, "public") || str_eq(rule, "knock") ||
                str_eq(rule, "knock_restricted") ||
                (user_id && str_eq(rule, "restricted"));
    j_free(c);
    return open || room_world_readable(room_id);
}

static json *hierarchy_entry(const char *room_id, bool suggested_only) {
    json *e = room_summary_json(room_id);
    j_set(e, "children_state", children_state(room_id, suggested_only));
    return e;
}

/* Walks m.space.child breadth-first from `root`. */
static json *hierarchy_walk(const char *root, const char *user_id,
                            bool suggested_only, int64_t max_depth,
                            int64_t limit, bool skip_root) {
    json *out = j_arr();
    json *seen = j_arr();
    json *queue = j_arr();
    json *depth = j_arr();
    j_push(queue, j_str(root));
    j_push(depth, j_int(0));
    size_t head = 0;
    while (head < j_len(queue) && (int64_t)j_len(out) < limit) {
        const char *rid = j_at(queue, head)->s;
        int64_t d = j_at(depth, head)->i;
        head++;
        bool dup = false;
        for (size_t i = 0; i < j_len(seen) && !dup; i++)
            dup = str_eq(j_at(seen, i)->s, rid);
        if (dup) continue;
        j_push(seen, j_str(rid));
        if (!room_exists(rid)) continue;
        if (!may_summarise(rid, user_id)) continue;
        if (!(skip_root && head == 1))
            j_push(out, hierarchy_entry(rid, suggested_only));
        if (max_depth >= 0 && d >= max_depth) continue;
        json *kids = children_state(rid, suggested_only);
        for (size_t i = 0; i < j_len(kids); i++) {
            const char *sk = j_gets(j_at(kids, i), "state_key", NULL);
            if (sk && *sk) { j_push(queue, j_str(sk)); j_push(depth, j_int(d + 1)); }
        }
        j_free(kids);
    }
    j_free(seen);
    j_free(queue);
    j_free(depth);
    return out;
}

/* The user behind the request, without writing an error when there is none. */
static char *soft_auth(const http_req *r) {
    char *tok = slide_bearer_token(r);
    if (!tok) return NULL;
    char *user = db_str("SELECT user_id FROM access_tokens WHERE token=?1",
                        "s", tok);
    free(tok);
    return user;
}

static int64_t query_int(const http_req *r, const char *name, int64_t dflt) {
    const char *v = http_query(r, name);
    if (!v || !*v) return dflt;
    char *end = NULL;
    long long n = strtoll(v, &end, 10);
    return end == v ? dflt : (int64_t)n;
}

/* GET /_matrix/client/v1/room_summary/{roomIdOrAlias}?via=... (v1.15) */
static void h_room_summary(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    /* A token that was presented has to be a token this server knows: a
     * summary is readable without one, but not with a bad one. */
    char *presented = slide_bearer_token(r);
    char *user = soft_auth(r);
    if (presented && !user) {
        free(presented);
        json *b = j_obj();
        j_set(b, "errcode", j_str("M_UNKNOWN_TOKEN"));
        j_set(b, "error", j_str("Unrecognised access token"));
        j_set(b, "soft_logout", j_bool(false));
        resp_json(w, 401, b);
        return;
    }
    free(presented);
    char *room_id = resolve_room(r->part[0]);

    if (room_id && room_exists(room_id) && room_latest_stream(room_id) > 0) {
        /* "The room is not accessible to the user" is reported the same way a
         * room this server does not know is: not found.  Saying "you may not
         * see this" would tell an unauthorised caller that the room exists. */
        if (!may_summarise(room_id, user)) {
            free(room_id);
            free(user);
            resp_error(w, 404, "M_NOT_FOUND",
                       "Not a room this server can summarise for you");
            return;
        }
        json *e = room_summary_json(room_id);
        j_del(e, "children_state");
        if (user) {
            char *m = room_membership(room_id, user);
            j_set(e, "membership", j_str(m ? m : "leave"));
            free(m);
        }
        free(room_id);
        free(user);
        resp_json(w, 200, e);
        return;
    }

    /* Not a room this server is in: ask one of the `via` servers. */
    const char *via[8];
    int nvia = http_query_all(r, "via", via, 8);
    if (!nvia) nvia = http_query_all(r, "server_name", via, 8);
    const char *target = room_id ? room_id : r->part[0];
    char *enc = url_encode(target);
    for (int i = 0; i < nvia; i++) {
        char *path = str_fmt(FED1 "/hierarchy/%s?suggested_only=false", enc);
        http_client_resp *resp = fed_request(via[i], "GET", path, NULL);
        free(path);
        if (resp && resp->status == 200 && resp->body) {
            json *doc = j_parse(resp->body, resp->body_len, NULL);
            json *room = doc ? j_geto(doc, "room") : NULL;
            if (room) {
                json *out = j_clone(room);
                j_del(out, "children_state");
                http_client_resp_free(resp);
                j_free(doc);
                free(enc); free(room_id); free(user);
                resp_json(w, 200, out);
                return;
            }
            j_free(doc);
        }
        http_client_resp_free(resp);
    }
    free(enc);
    free(room_id);
    free(user);
    resp_error(w, 404, "M_NOT_FOUND", "Room not found.");
}

/* GET /_matrix/client/v1/rooms/{roomId}/hierarchy.  Registered last, so it
 * only takes effect when no other module has claimed the route. */
static void h_hierarchy(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;
    char *room_id = resolve_room(r->part[0]);
    if (!room_id || !room_exists(room_id)) {
        free(room_id);
        resp_error(w, 404, "M_NOT_FOUND", "Room not found.");
        principal_free(&p);
        return;
    }
    if (!may_summarise(room_id, p.user_id)) {
        free(room_id);
        resp_error(w, 403, "M_FORBIDDEN", "You are not allowed to view this room");
        principal_free(&p);
        return;
    }
    bool suggested = false;
    const char *sq = http_query(r, "suggested_only");
    if (sq && (str_eq(sq, "true") || str_eq(sq, "1"))) suggested = true;
    /* A limit that is not a positive number is not a limit. */
    const char *lq = http_query(r, "limit");
    int64_t limit = query_int(r, "limit", HIER_MAX_ROOMS);
    if (lq && *lq && limit <= 0) {
        resp_error(w, 400, "M_INVALID_PARAM", "limit must be a positive integer");
        free(room_id);
        principal_free(&p);
        return;
    }
    if (limit <= 0 || limit > HIER_MAX_ROOMS) limit = HIER_MAX_ROOMS;
    int64_t max_depth = query_int(r, "max_depth", -1);

    /* "If a token is provided which is not valid, the server MUST respond with
     * 400 and M_INVALID_PARAM."  This server hands out no continuation token,
     * so any token it is given is one it did not mint. */
    const char *from = http_query(r, "from");
    if (from && *from) {
        resp_error(w, 400, "M_INVALID_PARAM",
                   "This server issued no such pagination token");
        free(room_id);
        principal_free(&p);
        return;
    }

    json *rooms = hierarchy_walk(room_id, p.user_id, suggested, max_depth,
                                 limit, false);
    json *b = j_obj();
    j_set(b, "rooms", rooms);
    free(room_id);
    resp_json(w, 200, b);
    principal_free(&p);
}

/* GET /_matrix/federation/v1/hierarchy/{roomId}. */
static void h_fed_hierarchy(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    char *origin = NULL;
    if (!fed_authenticate(r, w, &origin)) return;
    free(origin);
    char *room_id = resolve_room(r->part[0]);
    if (!room_id || !room_exists(room_id)) {
        free(room_id);
        resp_error(w, 404, "M_NOT_FOUND", "Room not found.");
        return;
    }
    bool suggested = false;
    const char *sq = http_query(r, "suggested_only");
    if (sq && (str_eq(sq, "true") || str_eq(sq, "1"))) suggested = true;
    json *b = j_obj();
    j_set(b, "room", hierarchy_entry(room_id, suggested));
    j_set(b, "children", hierarchy_walk(room_id, NULL, suggested, 1,
                                        HIER_MAX_ROOMS, true));
    j_set(b, "inaccessible_children", j_arr());
    free(room_id);
    resp_json(w, 200, b);
}

/* ============================================================== routing === */
static void route_both(http_server *s, const char *method, const char *suffix,
                       http_handler fn) {
    char *a = str_fmt("%s%s", CS, suffix);
    char *b = str_fmt("%s%s", CSR, suffix);
    http_route(s, method, a, fn);
    http_route(s, method, b, fn);
    free(a); free(b);
}

void api_register_oauth(http_server *s) {
    spec_advertise("v1.14");
    spec_advertise("v1.15");
    oauth_tables();
    cfg_load();

    http_route(s, "GET", CS1 "/auth_metadata", h_auth_metadata);
    http_route(s, "GET", CS1 "/room_summary/:", h_room_summary);
    route_both(s, "POST", "/users/:/report", h_report_user);

    /* Fallbacks: the router keeps the first registration at a given
     * specificity, so these only serve when nothing else claimed them. */
    http_route(s, "GET", CS1 "/rooms/:/hierarchy", h_hierarchy);
    http_route(s, "GET", FED1 "/hierarchy/:", h_fed_hierarchy);

    slide_install_gate(s);
}

#define _POSIX_C_SOURCE 200809L
/* Simplified Sliding Sync (MSC4186), account locking and suspension, and the
 * rest of the Client-Server additions of releases v1.12 and v1.13. */
#include "mxslide.h"

#include "mxapi.h"
#include "mxdb.h"
#include "mxevent.h"
#include "mxkeys.h"
#include "mxoauth.h"
#include "mxroom.h"
#include "mxserver.h"
#include "mxutil.h"

#include <ctype.h>
#include <strings.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CS   "/_matrix/client/v3"
#define CSR  "/_matrix/client/r0"
#define SS   "/_matrix/client/unstable/org.matrix.simplified_msc3575"
#define ADMIN "/_dendrite/admin"

#define SLIDE_TIMELINE_DEFAULT 20
#define SLIDE_TIMELINE_MAX     1000
#define SLIDE_TIMEOUT_MAX      300000
#define SLIDE_MAX_LISTS        100
#define SLIDE_MAX_SUBS         100
#define SLIDE_TO_DEVICE_MAX    100
#define SLIDE_UNREAD_SCAN_MAX  1000
/* How many connections one device may keep alive; the oldest go first. */
#define SLIDE_MAX_CONNS        8

/* Defined by mxpush.c, which does not declare it in its header. */
void push_ensure_account_data(const char *user_id);

/* ======================================================= small utilities == */
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

static bool arr_has_str(const json *arr, const char *s) {
    if (!arr || arr->type != J_ARRAY || !s) return false;
    for (size_t i = 0; i < j_len(arr); i++) {
        json *v = j_at(arr, i);
        if (j_is_str(v) && str_eq(v->s, s)) return true;
    }
    return false;
}

static bool arr_has_null(const json *arr) {
    if (!arr || arr->type != J_ARRAY) return false;
    for (size_t i = 0; i < j_len(arr); i++)
        if (j_at(arr, i)->type == J_NULL) return true;
    return false;
}

static bool ends_with(const char *s, const char *suffix) {
    if (!s || !suffix) return false;
    size_t n = strlen(s), m = strlen(suffix);
    return m <= n && strcmp(s + n - m, suffix) == 0;
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

static char *tok_of(int64_t stream_id) { return str_fmt("s%" PRId64, stream_id); }

/* Accepts "s42", "t42" and a bare "42". */
static bool tok_num(const char *s, int64_t *out) {
    if (!s || !*s) return false;
    const char *p = s;
    while (*p && !isdigit((unsigned char)*p)) p++;
    if (!*p) return false;
    char *end = NULL;
    long long v = strtoll(p, &end, 10);
    if (end == p || v < 0) return false;
    *out = (int64_t)v;
    return true;
}

char *slide_rawpath(const http_req *r) {
    const char *target = r && r->target ? r->target : "/";
    const char *t = target;
    if (strncasecmp(t, "http://", 7) == 0) t += 7;
    else if (strncasecmp(t, "https://", 8) == 0) t += 8;
    if (t != target) {                      /* absolute-form: drop the authority */
        const char *slash = strchr(t, '/');
        t = slash ? slash : "/";
    }
    const char *q = strchr(t, '?');
    size_t n = q ? (size_t)(q - t) : strlen(t);
    char *out = malloc(n + 1);
    if (!out) abort();
    memcpy(out, t, n);
    out[n] = 0;
    return out;
}

char *slide_bearer_token(const http_req *r) {
    const char *hdr = http_header(r, "authorization");
    if (hdr && str_prefix(hdr, "Bearer ")) return xstrdup_(hdr + 7);
    const char *q = http_query(r, "access_token");
    return q && *q ? xstrdup_(q) : NULL;
}

/* ================================================== the request gate ====== */
/* These mirror the head of mxhttp.c's private `route` and `struct
 * http_server`.  Only the first fields are needed and only those are
 * declared; gate_probe() checks that they line up before anything uses them. */
typedef struct {
    char *method;
    char *pattern;
    char **seg;
    int nseg;
    int nliteral;
    http_handler fn;
} gate_route;

typedef struct {
    void *userdata;
    gate_route *routes;
    size_t nroutes, routes_cap;
} gate_server;

/* And this mirrors the head of mxhttp.c's private `req_impl`, whose first
 * member is the public http_req.  slide_query_rename() checks it the same
 * way before touching it. */
typedef struct {
    http_req pub;
    char **qname, **qvalue;
    size_t nquery;
} gate_req;

static pthread_mutex_t g_gate_lock = PTHREAD_MUTEX_INITIALIZER;
static gate_server *g_srv;
static http_handler *g_orig;        /* g_orig[i] is route i's real handler */
static size_t g_orig_n;
static bool g_gate_ok;

#define GATE_PROBE ADMIN "/_gate_probe"

static void h_gate_probe(http_req *r, http_resp *w, void *ud) {
    (void)r; (void)ud;
    json *b = j_obj();
    j_set(b, "gate", j_bool(true));
    resp_json(w, 200, b);
}

static void gate_dispatch(http_req *r, http_resp *w, void *ud);

/* Wraps every route from g_orig_n onwards.  Caller holds g_gate_lock. */
static void gate_wrap_new(void) {
    size_t n = g_srv->nroutes;
    if (n <= g_orig_n) return;
    http_handler *grown = realloc(g_orig, n * sizeof *grown);
    if (!grown) return;
    g_orig = grown;
    for (size_t i = g_orig_n; i < n; i++) {
        g_orig[i] = g_srv->routes[i].fn;
        if (g_orig[i] != gate_dispatch) g_srv->routes[i].fn = gate_dispatch;
    }
    g_orig_n = n;
}

/* Registers a probe route and checks that the mirror sees it where it should
 * be.  A layout mismatch shows up as a wrong count or a wrong handler. */
static bool gate_probe(http_server *s) {
    gate_server *m = (gate_server *)s;
    size_t before = m->nroutes;
    if (before > 1000000u || !m->routes) return false;
    http_route(s, "GET", GATE_PROBE, h_gate_probe);
    if (m->nroutes != before + 1) return false;
    gate_route *rt = &m->routes[before];
    if (rt->fn != h_gate_probe) return false;
    if (!rt->method || strcmp(rt->method, "GET") != 0) return false;
    if (!rt->pattern || strcmp(rt->pattern, GATE_PROBE) != 0) return false;
    if (rt->nseg != 4) return false;
    return true;
}

bool slide_install_gate(http_server *s) {
    if (!s) return false;
    pthread_mutex_lock(&g_gate_lock);
    if (!g_gate_ok) {
        if (!gate_probe(s)) {
            pthread_mutex_unlock(&g_gate_lock);
            fprintf(stderr, "mxslide: route table layout not recognised, "
                            "locking/suspension gate disabled\n");
            return false;
        }
        g_srv = (gate_server *)s;
        g_gate_ok = true;
    }
    gate_wrap_new();
    pthread_mutex_unlock(&g_gate_lock);
    return true;
}

static bool seg_is(const char *s, char c) { return s[0] == c && s[1] == 0; }

static bool gate_try(const gate_route *rt, char **dec, int n) {
    for (int i = 0; i < rt->nseg; i++) {
        const char *pat = rt->seg[i];
        if (i == rt->nseg - 1 && seg_is(pat, '*')) return true;
        if (i >= n) return false;
        if (seg_is(pat, ':')) continue;
        if (strcmp(pat, dec[i]) != 0) return false;
    }
    return n == rt->nseg;
}

/* The same choice mxhttp.c's route_find() makes: most literal segments wins,
 * and the earliest registration wins a tie. */
static size_t gate_find(const char *method, const char *rawpath, bool *found) {
    *found = false;
    int n = 1;
    for (const char *q = rawpath; *q; q++) if (*q == '/') n++;
    char **dec = calloc((size_t)n, sizeof *dec);
    if (!dec) abort();
    const char *start = rawpath;
    int i = 0;
    for (const char *q = rawpath;; q++) {
        if (*q == '/' || *q == 0) {
            size_t len = (size_t)(q - start);
            char *d = url_decode(start, len, NULL);
            if (!d) { d = malloc(len + 1); if (!d) abort(); memcpy(d, start, len); d[len] = 0; }
            dec[i++] = d;
            if (*q == 0) break;
            start = q + 1;
        }
    }
    size_t best = 0;
    int best_lit = -1;
    for (size_t k = 0; k < g_orig_n && k < g_srv->nroutes; k++) {
        const gate_route *rt = &g_srv->routes[k];
        if (strcmp(rt->method, method) != 0) continue;
        if (!gate_try(rt, dec, n)) continue;
        if (rt->nliteral > best_lit) { best = k; best_lit = rt->nliteral; *found = true; }
    }
    for (int k = 0; k < n; k++) free(dec[k]);
    free(dec);
    return best;
}

static bool slide_before(http_req *r, http_resp *w, const char *pattern);
static void slide_after(http_req *r, http_resp *w, const char *pattern);

static void gate_dispatch(http_req *r, http_resp *w, void *ud) {
    char *rawpath = slide_rawpath(r);
    http_handler orig = NULL;
    char *pattern = NULL;
    /* The table and the saved handlers are only read under the lock: a module
     * registering routes after the gate went in reallocates both. */
    pthread_mutex_lock(&g_gate_lock);
    if (g_srv && g_srv->nroutes > g_orig_n) gate_wrap_new();
    bool found = false;
    size_t idx = gate_find(r->method, rawpath, &found);
    /* mxhttp.c answers a HEAD from the GET route when there is no HEAD one. */
    if (!found && str_eq(r->method, "HEAD"))
        idx = gate_find("GET", rawpath, &found);
    if (found) {
        orig = g_orig[idx];
        pattern = xstrdup_(g_srv->routes[idx].pattern);
    }
    pthread_mutex_unlock(&g_gate_lock);
    free(rawpath);
    if (!oauth_before(r, w)) { free(pattern); return; }
    if (!slide_before(r, w, pattern)) { free(pattern); return; }
    if (!orig || orig == gate_dispatch) {
        resp_error(w, 404, "M_UNRECOGNIZED", "Unrecognized request");
        free(pattern);
        return;
    }
    orig(r, w, ud);
    slide_after(r, w, pattern);
    oauth_after(r, w);
    free(pattern);
}

/* --------------------------------------------- request / response surgery -- */
int slide_query_rename(http_req *r, const char *from, const char *to) {
    gate_req *ri = (gate_req *)r;
    if (!r || !from || !to) return -1;
    if (ri->nquery > 4096u) return -1;
    /* Verify the mirror: every name must resolve back through http_query(). */
    for (size_t i = 0; i < ri->nquery; i++) {
        if (!ri->qname[i] || !ri->qvalue[i]) return -1;
        if (http_query(r, ri->qname[i]) == NULL) return -1;
    }
    int n = 0;
    for (size_t i = 0; i < ri->nquery; i++) {
        if (strcmp(ri->qname[i], from) != 0) continue;
        free(ri->qname[i]);
        ri->qname[i] = xstrdup_(to);
        n++;
    }
    return n;
}

void slide_set_body(http_req *r, const json *v) {
    if (!r || !v) return;
    size_t n = 0;
    char *text = j_dump(v, &n);
    if (!text) return;
    /* `v` is usually r->json_body itself, so copy before releasing it. */
    json *copy = j_clone(v);
    free(r->body);
    r->body = text;
    r->body_len = n;
    j_free(r->json_body);
    r->json_body = copy;
    r->json_bad = false;
}

json *slide_resp_json(const http_resp *w) {
    if (!w || !w->body.b || w->body.n == 0) return NULL;
    if (!w->content_type || !strstr(w->content_type, "json")) return NULL;
    return j_parse(w->body.b, w->body.n, NULL);
}

void slide_resp_replace(http_resp *w, json *v) {
    if (!w) { j_free(v); return; }
    int status = w->status;
    resp_json(w, status, v);
}

/* ===================================================== locking + suspension */
static void slide_tables(void) {
    db_exec("CREATE TABLE IF NOT EXISTS user_flags("
            "  user_id TEXT PRIMARY KEY, locked INTEGER DEFAULT 0,"
            "  suspended INTEGER DEFAULT 0, admin INTEGER DEFAULT 0,"
            "  reason TEXT, updated_ts INTEGER);");
    db_exec("CREATE TABLE IF NOT EXISTS content_reports("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT, kind TEXT, target TEXT,"
            "  event_id TEXT, sender TEXT, reason TEXT, score INTEGER,"
            "  created_ts INTEGER);");
    db_exec("CREATE TABLE IF NOT EXISTS slide_conn("
            "  conn_key TEXT PRIMARY KEY, user_id TEXT, device_id TEXT,"
            "  conn_id TEXT, pos TEXT, prev_pos TEXT, stream_id INTEGER,"
            "  prev_stream INTEGER, to_device_ack INTEGER DEFAULT 0,"
            "  created_ts INTEGER, updated_ts INTEGER);");
    db_exec("CREATE INDEX IF NOT EXISTS slide_conn_dev "
            "ON slide_conn(user_id, device_id, updated_ts);");
    db_exec("CREATE TABLE IF NOT EXISTS slide_room("
            "  conn_key TEXT, room_id TEXT, sent_stream INTEGER,"
            "  timeline_limit INTEGER, PRIMARY KEY(conn_key, room_id));");
    /* The same rows as they stood when `slide_conn.prev_pos` was handed out.
     * Re-sending a `pos` has to report what that `pos` reported the first
     * time, so the per-room "sent up to here" marks are kept for both of the
     * positions a connection still accepts, not only for the newest one. */
    db_exec("CREATE TABLE IF NOT EXISTS slide_room_prev("
            "  conn_key TEXT, room_id TEXT, sent_stream INTEGER,"
            "  timeline_limit INTEGER, PRIMARY KEY(conn_key, room_id));");
}

bool slide_get_user_flag(const char *user_id, const char *flag) {
    if (!user_id || !flag) return false;
    char *sql = str_fmt("SELECT %s FROM user_flags WHERE user_id=?1", flag);
    int64_t v = 0;
    bool got = db_int(sql, &v, "s", user_id);
    free(sql);
    return got && v != 0;
}

bool slide_user_locked(const char *user_id) {
    return slide_get_user_flag(user_id, "locked");
}

bool slide_user_suspended(const char *user_id) {
    return slide_get_user_flag(user_id, "suspended");
}

void slide_set_user_flag(const char *user_id, const char *flag, bool on) {
    if (!user_id || !flag) return;
    char *sql = str_fmt("INSERT INTO user_flags(user_id,%s,updated_ts) "
                        "VALUES(?1,?2,?3) ON CONFLICT(user_id) DO UPDATE SET "
                        "%s=?2, updated_ts=?3", flag, flag);
    hs_lock();
    db_run(sql, "sii", user_id, (int64_t)(on ? 1 : 0), now_ms());
    hs_unlock();
    stream_notify();
    free(sql);
}

/* The tail of a Client-Server pattern: "/_matrix/client/v3/createRoom" and
 * "/_matrix/client/r0/createRoom" both become "/createRoom". */
static const char *cs_tail(const char *pattern) {
    if (!pattern || !str_prefix(pattern, "/_matrix/client/")) return NULL;
    const char *p = strchr(pattern + 16, '/');
    return p;
}

/* The endpoints a suspended account may not use.  "General purpose endpoints
 * like /send/{eventType} MAY return the error depending on the path
 * parameters" -- redaction of one's own events stays allowed, so /redact is
 * not here. */
static bool suspend_blocked(const char *method, const char *pattern) {
    if (!method || !pattern) return false;
    if (str_eq(method, "POST") &&
        (ends_with(pattern, "/upload") || ends_with(pattern, "/upload/:/:")))
        return str_prefix(pattern, "/_matrix/media/");
    const char *t = cs_tail(pattern);
    if (!t) return false;
    static const struct { const char *m, *p; } W[] = {
        { "PUT",    "/rooms/:/send/:/:" },
        { "POST",   "/rooms/:/send/:/:" },
        { "PUT",    "/rooms/:/state/:" },
        { "PUT",    "/rooms/:/state/:/:" },
        { "POST",   "/createRoom" },
        { "POST",   "/rooms/:/invite" },
        { "POST",   "/rooms/:/join" },
        { "POST",   "/join/:" },
        { "POST",   "/knock/:" },
        { "POST",   "/rooms/:/kick" },
        { "POST",   "/rooms/:/ban" },
        { "POST",   "/rooms/:/unban" },
        { "POST",   "/rooms/:/upgrade" },
        { "PUT",    "/profile/:/displayname" },
        { "PUT",    "/profile/:/avatar_url" },
        { "PUT",    "/profile/:/:" },
        { "DELETE", "/profile/:/:" },
        { NULL, NULL }
    };
    for (int i = 0; W[i].m; i++)
        if (str_eq(method, W[i].m) && str_eq(t, W[i].p)) return true;
    return false;
}

/* POST /logout and POST /logout/all keep working on a locked account. */
static bool locked_allowed(const char *method, const char *pattern) {
    const char *t = cs_tail(pattern);
    if (!t || !str_eq(method, "POST")) return false;
    return str_eq(t, "/logout") || str_eq(t, "/logout/all");
}

static void write_locked(http_resp *w) {
    json *b = j_obj();
    j_set(b, "errcode", j_str("M_USER_LOCKED"));
    j_set(b, "error", j_str("This account has been locked"));
    j_set(b, "soft_logout", j_bool(true));
    resp_json(w, 401, b);
}

static void write_suspended(http_resp *w) {
    resp_error(w, 403, "M_USER_SUSPENDED",
               "You cannot perform this action while suspended.");
}

/* The user the request's access token belongs to, or NULL.  By the time this
 * runs mxoauth.c has already materialised an introspected token, so one
 * lookup covers both the local and the delegated case. */
static char *gate_user(const http_req *r) {
    char *tok = slide_bearer_token(r);
    if (!tok) return NULL;
    char *user = db_str("SELECT user_id FROM access_tokens WHERE token=?1",
                        "s", tok);
    free(tok);
    return user;
}

/* `via` on /join and /knock (v1.12): "Homeservers MUST ignore all server_name
 * parameters if any via parameters are supplied."  Under MSC3861 the
 * deprecated parameter is not honoured at all (v1.15). */
static void rewrite_via(http_req *r, const char *pattern) {
    const char *t = cs_tail(pattern);
    if (!t) return;
    if (!str_eq(t, "/join/:") && !str_eq(t, "/knock/:") &&
        !str_eq(t, "/rooms/:/join")) return;
    const char *first = http_query(r, "via");
    if (first) {
        slide_query_rename(r, "server_name", "_server_name");
        slide_query_rename(r, "via", "server_name");
    } else if (oauth_delegated()) {
        slide_query_rename(r, "server_name", "_server_name");
    }
}

/* m.marked_unread (v1.13) carries a boolean `unread`. */
static bool check_marked_unread(http_req *r, http_resp *w, const char *pattern) {
    const char *t = cs_tail(pattern);
    if (!t || !str_eq(r->method, "PUT")) return true;
    if (!str_eq(t, "/user/:/rooms/:/account_data/:")) return true;
    if (r->nparts < 3 || !str_eq(r->part[2], "m.marked_unread")) return true;
    json *body = http_json(r);
    if (!body || body->type != J_OBJECT) return true;   /* the handler complains */
    json *u = j_get(body, "unread");
    if (!u || u->type != J_BOOL) {
        resp_error(w, 400, "M_BAD_JSON",
                   "m.marked_unread requires a boolean 'unread' property");
        return false;
    }
    return true;
}

/* "Some ... event types are not allowed to be set through this API": the
 * read marker is moved with POST /rooms/{roomId}/read_markers and the push
 * rules with the /pushrules endpoints, so writing either as account data is
 * refused with 405 M_BAD_JSON rather than silently accepted. */
static bool check_reserved_account_data(http_req *r, http_resp *w,
                                        const char *pattern) {
    const char *t = cs_tail(pattern);
    if (!t || !str_eq(r->method, "PUT")) return true;
    const char *type = NULL;
    if (str_eq(t, "/user/:/rooms/:/account_data/:") && r->nparts >= 3)
        type = r->part[2];
    else if (str_eq(t, "/user/:/account_data/:") && r->nparts >= 2)
        type = r->part[1];
    if (!type) return true;
    if (!str_eq(type, "m.fully_read") && !str_eq(type, "m.push_rules"))
        return true;
    char *msg = str_fmt("Cannot set %s through this API", type);
    resp_error(w, 405, "M_BAD_JSON", msg);
    free(msg);
    return false;
}

/* A locked account may not log in either. */
static bool check_login_locked(http_req *r, http_resp *w, const char *pattern) {
    const char *t = cs_tail(pattern);
    if (!t || !str_eq(t, "/login") || !str_eq(r->method, "POST")) return true;
    json *body = http_json(r);
    if (!body || body->type != J_OBJECT) return true;
    json *ident = j_geto(body, "identifier");
    const char *who = ident ? j_gets(ident, "user", NULL) : NULL;
    if (!who) who = j_gets(body, "user", NULL);
    if (!who) return true;
    char *uid = who[0] == '@' ? xstrdup_(who)
                              : str_fmt("@%s:%s", who, CFG.server_name);
    bool locked = slide_user_locked(uid);
    free(uid);
    if (locked) { write_locked(w); return false; }
    return true;
}

static bool slide_before(http_req *r, http_resp *w, const char *pattern) {
    if (!pattern) return true;
    if (!check_login_locked(r, w, pattern)) return false;
    if (!check_marked_unread(r, w, pattern)) return false;
    if (!check_reserved_account_data(r, w, pattern)) return false;

    char *user = gate_user(r);
    if (user) {
        if (slide_user_locked(user) && !locked_allowed(r->method, pattern)) {
            free(user);
            write_locked(w);
            return false;
        }
        if (slide_user_suspended(user) && suspend_blocked(r->method, pattern)) {
            free(user);
            write_suspended(w);
            return false;
        }
        free(user);
    }
    rewrite_via(r, pattern);
    return true;
}

static void slide_after(http_req *r, http_resp *w, const char *pattern) {
    (void)r;
    if (!pattern || w->status != 200) return;
    if (!str_eq(pattern, "/_matrix/client/versions")) return;
    json *b = slide_resp_json(w);
    if (!b || b->type != J_OBJECT) { j_free(b); return; }
    json *uf = j_geto(b, "unstable_features");
    if (!uf) { uf = j_obj(); j_set(b, "unstable_features", uf); }
    j_set(uf, "org.matrix.simplified_msc3575", j_bool(true));
    j_set(uf, "org.matrix.msc4186", j_bool(true));
    if (oauth_delegated()) j_set(uf, "org.matrix.msc3861", j_bool(true));
    slide_resp_replace(w, b);
}

/* ===================================================== simplified sliding ==
 * POST /_matrix/client/unstable/org.matrix.simplified_msc3575/sync
 *
 * The request shape is the one the unstable endpoint carries: `pos` and
 * `timeout` in the query string, `conn_id` in the body, `lists` with `ranges`
 * and a `required_state` of [type, state_key] pairs, and `ops` alongside
 * `count` in each list result. */

typedef struct {
    char *room_id;
    char *membership;
    char *mem_sender;
    int64_t mem_stream;
    int64_t last_stream;
    int64_t bump;
} sroom;

typedef struct {
    sroom *v;
    size_t n, cap;
} sroomlist;

static void sr_push(sroomlist *l, sroom s) {
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        l->v = realloc(l->v, l->cap * sizeof *l->v);
        if (!l->v) abort();
    }
    l->v[l->n++] = s;
}

static void srl_free(sroomlist *l) {
    for (size_t i = 0; i < l->n; i++) {
        free(l->v[i].room_id);
        free(l->v[i].membership);
        free(l->v[i].mem_sender);
    }
    free(l->v);
    memset(l, 0, sizeof *l);
}

static int sr_cmp(const void *a, const void *b) {
    const sroom *x = a, *y = b;
    if (x->last_stream != y->last_stream)
        return x->last_stream > y->last_stream ? -1 : 1;
    return strcmp(x->room_id, y->room_id);
}

static int64_t room_bump_stamp(const char *room_id) {
    int64_t v = 0;
    db_int("SELECT COALESCE(MAX(stream_id),0) FROM events WHERE room_id=?1 AND "
           "type IN ('m.room.create','m.room.message','m.room.encrypted',"
           "'m.sticker','m.call.invite','m.poll.start','m.beacon_info')",
           &v, "s", room_id);
    return v;
}

static int64_t room_count_members(const char *room_id, const char *membership) {
    int64_t v = 0;
    db_int("SELECT COUNT(*) FROM room_members WHERE room_id=?1 AND membership=?2",
           &v, "ss", room_id, membership);
    return v;
}

static json *state_content(const char *room_id, const char *type, const char *sk) {
    json *ev = room_state_event(room_id, type, sk);
    if (!ev) return NULL;
    json *c = j_geto(ev, "content");
    json *out = c ? j_clone(c) : NULL;
    j_free(ev);
    return out;
}

static char *room_type_of(const char *room_id) {
    json *c = state_content(room_id, "m.room.create", "");
    const char *t = c ? j_gets(c, "type", NULL) : NULL;
    char *out = t ? xstrdup_(t) : NULL;
    j_free(c);
    return out;
}

static bool room_is_encrypted(const char *room_id) {
    json *c = state_content(room_id, "m.room.encryption", "");
    bool yes = c != NULL;
    j_free(c);
    return yes;
}

/* The set of room ids named by the user's m.direct account data. */
static json *direct_rooms(const char *user_id) {
    json *out = j_arr();
    char *text = db_str("SELECT content FROM account_data WHERE user_id=?1 AND "
                        "room_id='' AND type='m.direct'", "s", user_id);
    json *c = parse_text(text);
    free(text);
    if (c && c->type == J_OBJECT) {
        for (size_t i = 0; i < j_len(c); i++) {
            json *arr = j_at(c, i);
            if (!arr || arr->type != J_ARRAY) continue;
            for (size_t k = 0; k < j_len(arr); k++) {
                json *v = j_at(arr, k);
                if (j_is_str(v) && !arr_has_str(out, v->s)) j_push(out, j_str(v->s));
            }
        }
    }
    j_free(c);
    return out;
}

static void room_unread(const char *user_id, const char *room_id, int64_t now,
                        int64_t *notif, int64_t *highlight) {
    *notif = 0;
    *highlight = 0;
    int64_t after = 0;
    char *read = db_str("SELECT event_id FROM receipts WHERE room_id=?1 AND "
                        "user_id=?2 AND receipt_type='m.read'", "ss",
                        room_id, user_id);
    if (read) {
        int64_t s = 0;
        if (event_get_stream(read, &s, NULL)) after = s;
        free(read);
    }
    char *display = db_str("SELECT displayname FROM users WHERE user_id=?1",
                           "s", user_id);
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT json FROM events WHERE room_id=?1 AND stream_id>?2 AND "
            "stream_id<=?3 AND sender<>?4 AND is_state=0 "
            "ORDER BY stream_id ASC LIMIT ?5", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, room_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 2, after);
        sqlite3_bind_int64(st, 3, now);
        sqlite3_bind_text(st, 4, user_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 5, SLIDE_UNREAD_SCAN_MAX);
        while (sqlite3_step(st) == SQLITE_ROW) {
            (*notif)++;
            json *ev = parse_col(st, 0);
            const char *body = ev ? j_gets(j_geto(ev, "content"), "body", NULL) : NULL;
            if (body && (ci_contains(body, user_id) ||
                         (display && *display && ci_contains(body, display))))
                (*highlight)++;
            j_free(ev);
        }
        sqlite3_finalize(st);
    }
    free(display);
}

/* ------------------------------------------------------------- filtering -- */
static bool list_filters_match(const json *f, const sroom *rm, const json *direct) {
    if (!f || f->type != J_OBJECT) return true;
    json *v;
    if ((v = j_get(f, "is_dm")) && v->type == J_BOOL) {
        bool dm = arr_has_str(direct, rm->room_id);
        if (dm != v->b) return false;
    }
    if ((v = j_get(f, "is_encrypted")) && v->type == J_BOOL) {
        if (room_is_encrypted(rm->room_id) != v->b) return false;
    }
    /* `is_invite` is the name the unstable endpoint uses. */
    if (!(v = j_get(f, "is_invite"))) v = j_get(f, "is_invited");
    if (v && v->type == J_BOOL) {
        bool inv = str_eq(rm->membership, "invite");
        if (inv != v->b) return false;
    }
    const json *want = j_geta(f, "room_types");
    const json *deny = j_geta(f, "not_room_types");
    if (want || deny) {
        char *type = room_type_of(rm->room_id);
        bool in_deny = deny && (type ? arr_has_str(deny, type) : arr_has_null(deny));
        bool in_want = want && (type ? arr_has_str(want, type) : arr_has_null(want));
        free(type);
        if (in_deny) return false;
        if (want && !in_want) return false;
    }
    return true;
}

/* ------------------------------------------------------- required  state -- */
/* One entry is ["<type>", "<state_key>"]; "*" is a wildcard for either half,
 * "$ME" the syncing user's own member event and "$LAZY" the member events of
 * the senders in the timeline.  An object {"type":..,"state_key":..} is
 * accepted too, for clients following the newer draft. */
static void rs_entry(const json *e, const char **type, const char **sk) {
    *type = "*";
    *sk = "*";
    if (!e) return;
    if (e->type == J_ARRAY) {
        json *a = j_at(e, 0), *b = j_at(e, 1);
        if (j_is_str(a)) *type = a->s;
        if (j_is_str(b)) *sk = b->s;
    } else if (e->type == J_OBJECT) {
        *type = j_gets(e, "type", "*");
        *sk = j_gets(e, "state_key", "*");
    }
}

static bool rs_wants(const json *spec, const char *type, const char *sk,
                     const char *user_id, const json *senders) {
    if (!spec || spec->type != J_ARRAY) return false;
    for (size_t i = 0; i < j_len(spec); i++) {
        const char *t = NULL, *k = NULL;
        rs_entry(j_at(spec, i), &t, &k);
        if (!str_eq(t, "*") && !str_eq(t, type)) continue;
        if (str_eq(k, "*")) return true;
        if (str_eq(k, "$ME")) {
            if (str_eq(sk, user_id)) return true;
            continue;
        }
        if (str_eq(k, "$LAZY")) {
            if (str_eq(type, "m.room.member") && arr_has_str(senders, sk)) return true;
            continue;
        }
        if (str_eq(k, sk)) return true;
    }
    return false;
}

static void rs_union(json *into, const json *from) {
    if (!from || from->type != J_ARRAY) return;
    for (size_t i = 0; i < j_len(from); i++) {
        const char *t = NULL, *k = NULL;
        rs_entry(j_at(from, i), &t, &k);
        bool seen = false;
        for (size_t g = 0; g < j_len(into) && !seen; g++) {
            const char *t2 = NULL, *k2 = NULL;
            rs_entry(j_at(into, g), &t2, &k2);
            seen = str_eq(t, t2) && str_eq(k, k2);
        }
        if (seen) continue;
        json *pair = j_arr();
        j_push(pair, j_str(t));
        j_push(pair, j_str(k));
        j_push(into, pair);
    }
}

/* ---------------------------------------------------------- one  room ---- */
/* The newest `limit` events of (lo, hi] the user may see, oldest first. */
static json *slide_timeline(const char *user_id, const char *device_id,
                            const char *room_id, int64_t lo, int64_t hi,
                            int64_t limit, bool *limited, int64_t *first_sid,
                            int64_t since, int64_t *num_live, json *senders) {
    *limited = false;
    *first_sid = 0;
    *num_live = 0;
    json *rev = j_arr();
    json *sids = j_arr();
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT stream_id, json FROM events WHERE room_id=?1 AND "
            "stream_id>?2 AND stream_id<=?3 ORDER BY stream_id DESC",
            -1, &st, NULL) != SQLITE_OK) { j_free(sids); return rev; }
    sqlite3_bind_text(st, 1, room_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, lo);
    sqlite3_bind_int64(st, 3, hi);
    while (sqlite3_step(st) == SQLITE_ROW) {
        int64_t sid = sqlite3_column_int64(st, 0);
        if (!room_can_see(room_id, user_id, sid)) continue;
        if ((int64_t)j_len(rev) >= limit) { *limited = true; break; }
        json *stored = parse_col(st, 1);
        if (!stored) continue;
        const char *sender = j_gets(stored, "sender", NULL);
        if (sender && !arr_has_str(senders, sender)) j_push(senders, j_str(sender));
        if (str_eq(j_gets(stored, "type", ""), "m.room.member")) {
            const char *sk = j_gets(stored, "state_key", NULL);
            if (sk && !arr_has_str(senders, sk)) j_push(senders, j_str(sk));
        }
        json *ev = event_to_client(stored, user_id, device_id, false);
        j_free(stored);
        if (!ev) continue;
        j_push(rev, ev);
        j_push(sids, j_int(sid));
        if (sid > since) (*num_live)++;
    }
    sqlite3_finalize(st);

    json *out = j_arr();
    for (size_t i = j_len(rev); i > 0; i--) j_push(out, j_clone(j_at(rev, i - 1)));
    if (j_len(sids)) *first_sid = j_at(sids, j_len(sids) - 1)->i;
    j_free(rev);
    j_free(sids);
    return out;
}

static json *slide_state(const char *user_id, const char *device_id,
                         const char *room_id, const json *spec, int64_t lo,
                         int64_t hi, bool full, const json *senders) {
    json *map;
    if (full) {
        map = room_state_at(room_id, hi);
    } else {
        map = j_obj();
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db(),
                "SELECT type, state_key, json FROM events WHERE room_id=?1 AND "
                "is_state=1 AND stream_id>?2 AND stream_id<=?3 "
                "ORDER BY stream_id ASC", -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, room_id, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(st, 2, lo);
            sqlite3_bind_int64(st, 3, hi);
            while (sqlite3_step(st) == SQLITE_ROW) {
                char *key = state_key_of(col_str(st, 0), col_str(st, 1));
                json *ev = parse_col(st, 2);
                if (ev) j_set(map, key, ev);
                free(key);
            }
            sqlite3_finalize(st);
        }
    }
    json *out = j_arr();
    for (size_t i = 0; i < j_len(map); i++) {
        json *stored = j_at(map, i);
        const char *type = j_gets(stored, "type", "");
        const char *sk = j_gets(stored, "state_key", "");
        if (!rs_wants(spec, type, sk, user_id, senders)) continue;
        json *ev = event_to_client(stored, user_id, device_id, false);
        if (ev) j_push(out, ev);
    }
    j_free(map);
    return out;
}

static json *slide_stripped(const char *room_id, int64_t at) {
    static const char *WANTED[] = {
        "m.room.create", "m.room.join_rules", "m.room.name", "m.room.topic",
        "m.room.avatar", "m.room.canonical_alias", "m.room.encryption", NULL
    };
    json *state = room_state_at(room_id, at);
    json *out = j_arr();
    for (size_t i = 0; i < j_len(state); i++) {
        json *ev = j_at(state, i);
        const char *type = j_gets(ev, "type", "");
        bool want = str_eq(type, "m.room.member");
        for (int k = 0; !want && WANTED[k]; k++) want = str_eq(type, WANTED[k]);
        if (want) j_push(out, event_to_stripped(ev));
    }
    j_free(state);
    return out;
}

static json *slide_room_result(const char *user_id, const char *device_id,
                               const sroom *rm, const json *cfg, int64_t sent,
                               bool initial, int64_t since, int64_t now,
                               const json *direct, bool expanded) {
    json *out = j_obj();
    const json *spec = j_geta(cfg, "required_state");
    int64_t limit = j_geti(cfg, "timeline_limit", SLIDE_TIMELINE_DEFAULT);
    if (limit < 0) limit = 0;
    if (limit > SLIDE_TIMELINE_MAX) limit = SLIDE_TIMELINE_MAX;

    bool joined_ever = str_eq(rm->membership, "join") ||
                       str_eq(rm->membership, "leave") ||
                       str_eq(rm->membership, "ban");
    j_set(out, "membership", j_str(rm->membership));
    if (initial) j_set(out, "initial", j_bool(true));
    j_set(out, "is_dm", j_bool(arr_has_str(direct, rm->room_id)));
    j_set(out, "bump_stamp", j_int(rm->bump));
    json *lists = j_geta(cfg, "lists");
    if (lists) j_set(out, "lists", j_clone(lists));

    /* The name and the avatar come from the room's state whatever the
     * membership is: an invitation is shown to the user by name too. */
    char *name = NULL, *avatar = NULL;
    json *c = state_content(rm->room_id, "m.room.name", "");
    if (c && j_is_str(j_get(c, "name"))) name = xstrdup_(j_gets(c, "name", ""));
    j_free(c);
    c = state_content(rm->room_id, "m.room.avatar", "");
    if (c && j_is_str(j_get(c, "url"))) avatar = xstrdup_(j_gets(c, "url", ""));
    j_free(c);
    if (name) { j_set(out, "name", j_str(name)); free(name); }
    else if (initial) j_set(out, "name", j_null());
    if (avatar) { j_set(out, "avatar", j_str(avatar)); free(avatar); }
    else if (initial) j_set(out, "avatar", j_null());

    if (!joined_ever) {
        /* An invite or a knock: only the stripped state may be shown. */
        json *stripped = slide_stripped(rm->room_id,
                                        rm->mem_stream > now ? now : rm->mem_stream);
        j_set(out, "invite_state", j_clone(stripped));
        j_set(out, "stripped_state", stripped);
        j_set(out, "joined_count", j_int(room_count_members(rm->room_id, "join")));
        j_set(out, "invited_count", j_int(room_count_members(rm->room_id, "invite")));
        return out;
    }

    json *senders = j_arr();
    bool limited = false;
    int64_t first_sid = 0, num_live = 0;
    /* A timeline_limit of zero asks for no timeline, and a room that carries
     * none carries neither `limited` nor `prev_batch` nor `num_live`: there
     * is no window for them to describe. */
    if (limit > 0) {
        /* An initial send, or one whose timeline_limit just grew, walks the
         * room from the beginning and keeps the newest `limit` events; an
         * incremental one only looks at what arrived since it was last sent. */
        int64_t lo = (initial || expanded) ? 0 : sent;
        json *timeline = slide_timeline(user_id, device_id, rm->room_id, lo, now,
                                        limit, &limited, &first_sid, since,
                                        &num_live, senders);
        j_set(out, "timeline", timeline);
        j_set(out, "limited", j_bool(limited));
        j_set(out, "num_live", j_int(num_live));
        char *prev = tok_of(first_sid ? first_sid - 1 : now);
        j_set(out, "prev_batch", j_str(prev));
        free(prev);
    }
    if (expanded && !initial) j_set(out, "unstable_expanded_timeline", j_bool(true));

    int64_t upper = first_sid ? first_sid - 1 : now;
    json *state = slide_state(user_id, device_id, rm->room_id, spec,
                              sent, initial || expanded ? now : upper,
                              initial || expanded, senders);
    j_free(senders);
    j_set(out, "required_state", state);

    j_set(out, "joined_count", j_int(room_count_members(rm->room_id, "join")));
    j_set(out, "invited_count", j_int(room_count_members(rm->room_id, "invite")));
    int64_t notif = 0, hl = 0;
    room_unread(user_id, rm->room_id, now, &notif, &hl);
    j_set(out, "notification_count", j_int(notif));
    j_set(out, "highlight_count", j_int(hl));
    return out;
}

/* ------------------------------------------------------------ extensions -- */
static bool ext_on(const json *exts, const char *name, json **cfg_out) {
    json *cfg = exts ? j_geto(exts, name) : NULL;
    if (cfg_out) *cfg_out = cfg;
    if (!cfg) return false;
    return j_getb(cfg, "enabled", true);
}

static json *ext_to_device(const char *user_id, const char *device_id,
                           const json *cfg, int64_t stored_ack, int64_t now,
                           int64_t *ack_out) {
    json *out = j_obj();
    int64_t since = stored_ack;
    const char *sq = cfg ? j_gets(cfg, "since", NULL) : NULL;
    if (sq) tok_num(sq, &since);
    *ack_out = since;
    json *events = j_arr();
    if (device_id) {
        hs_lock();
        db_run("DELETE FROM to_device WHERE user_id=?1 AND device_id=?2 AND "
               "stream_id<=?3", "ssi", user_id, device_id, since);
        hs_unlock();
        stream_notify();
    }
    int64_t last = 0, count = 0;
    sqlite3_stmt *st = NULL;
    if (device_id && sqlite3_prepare_v2(db(),
            "SELECT stream_id, sender, type, content FROM to_device WHERE "
            "user_id=?1 AND (device_id=?2 OR device_id='*') AND stream_id>?3 "
            "AND stream_id<=?4 ORDER BY stream_id ASC LIMIT ?5",
            -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, user_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, device_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 3, since);
        sqlite3_bind_int64(st, 4, now);
        sqlite3_bind_int64(st, 5, SLIDE_TO_DEVICE_MAX);
        while (sqlite3_step(st) == SQLITE_ROW) {
            last = sqlite3_column_int64(st, 0);
            count++;
            json *content = parse_col(st, 3);
            json *ev = j_obj();
            j_set(ev, "sender", j_str(col_str(st, 1)));
            j_set(ev, "type", j_str(col_str(st, 2)));
            j_set(ev, "content", content ? content : j_obj());
            j_push(events, ev);
        }
        sqlite3_finalize(st);
    }
    int64_t next = count >= SLIDE_TO_DEVICE_MAX ? last : now;
    char *nb = str_fmt("%" PRId64, next);
    j_set(out, "next_batch", j_str(nb));
    free(nb);
    j_set(out, "events", events);
    return out;
}

static json *ext_e2ee(const char *user_id, const char *device_id,
                      int64_t since, int64_t now, bool fresh) {
    json *out = j_obj();
    /* Device-list movement is movement *since* something.  A request that
     * carries no `pos` has no since, so there is nothing to report -- the
     * client is starting over and queries the keys it needs.  And a section
     * with nothing in it says nothing, so it is left out rather than sent
     * with two empty lists in it. */
    json *dl = fresh ? NULL : keys_sync_device_lists(user_id, since, now);
    if (dl && (j_len(j_geta(dl, "changed")) || j_len(j_geta(dl, "left"))))
        j_set(out, "device_lists", dl);
    else j_free(dl);
    json *otk = keys_sync_otk_counts(user_id, device_id);
    j_set(out, "device_one_time_keys_count", otk ? otk : j_obj());
    json *fb = keys_sync_unused_fallback_key_types(user_id, device_id);
    j_set(out, "device_unused_fallback_key_types", fb ? fb : j_arr());
    return out;
}

static json *account_data_for(const char *user_id, const char *room_id,
                              int64_t since, int64_t now) {
    json *out = j_arr();
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT type, content FROM account_data WHERE user_id=?1 AND "
            "room_id=?2 AND stream_id>?3 AND stream_id<=?4 ORDER BY stream_id ASC",
            -1, &st, NULL) != SQLITE_OK) return out;
    sqlite3_bind_text(st, 1, user_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, room_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, since);
    sqlite3_bind_int64(st, 4, now);
    while (sqlite3_step(st) == SQLITE_ROW) {
        json *content = parse_col(st, 1);
        json *ev = j_obj();
        j_set(ev, "type", j_str(col_str(st, 0)));
        j_set(ev, "content", content ? content : j_obj());
        j_push(out, ev);
    }
    sqlite3_finalize(st);
    return out;
}

static json *ext_account_data(const char *user_id, const json *rooms,
                              int64_t since, int64_t now) {
    json *out = j_obj();
    /* The push rules are global account data from the first read, not only
     * after they are first changed. */
    push_ensure_account_data(user_id);
    j_set(out, "global", account_data_for(user_id, "", since, now));
    json *per = j_obj();
    for (size_t i = 0; i < j_len(rooms); i++) {
        const char *rid = j_key(rooms, i);
        json *evs = account_data_for(user_id, rid, since, now);
        if (j_len(evs)) j_set(per, rid, evs); else j_free(evs);
    }
    j_set(out, "rooms", per);
    return out;
}

static json *ext_receipts(const char *user_id, const json *rooms, int64_t since,
                          int64_t now) {
    (void)user_id;
    json *out = j_obj();
    json *per = j_obj();
    for (size_t i = 0; i < j_len(rooms); i++) {
        const char *rid = j_key(rooms, i);
        json *content = j_obj();
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db(),
                "SELECT event_id, receipt_type, user_id, ts, thread_id FROM receipts "
                "WHERE room_id=?1 AND stream_id>?2 AND stream_id<=?3 "
                "ORDER BY stream_id ASC", -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, rid, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(st, 2, since);
            sqlite3_bind_int64(st, 3, now);
            while (sqlite3_step(st) == SQLITE_ROW) {
                const char *eid = col_str(st, 0), *rt = col_str(st, 1);
                json *by_event = j_geto(content, eid);
                if (!by_event) { by_event = j_obj(); j_set(content, eid, by_event); }
                json *by_type = j_geto(by_event, rt);
                if (!by_type) { by_type = j_obj(); j_set(by_event, rt, by_type); }
                json *who = j_obj();
                j_set(who, "ts", j_int(sqlite3_column_int64(st, 3)));
                const char *thread = col_str(st, 4);
                if (thread && *thread) j_set(who, "thread_id", j_str(thread));
                j_set(by_type, col_str(st, 2), who);
            }
            sqlite3_finalize(st);
        }
        if (j_len(content) == 0) { j_free(content); continue; }
        json *ev = j_obj();
        j_set(ev, "type", j_str("m.receipt"));
        j_set(ev, "room_id", j_str(rid));
        j_set(ev, "content", content);
        j_set(per, rid, ev);
    }
    j_set(out, "rooms", per);
    return out;
}

static json *ext_typing(const json *rooms, int64_t since, int64_t now) {
    json *out = j_obj();
    json *per = j_obj();
    for (size_t i = 0; i < j_len(rooms); i++) {
        const char *rid = j_key(rooms, i);
        int64_t changed = 0;
        db_int("SELECT COUNT(*) FROM typing WHERE room_id=?1 AND stream_id>?2 "
               "AND stream_id<=?3", &changed, "sii", rid, since, now);
        if (changed <= 0) continue;
        json *ids = j_arr();
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db(),
                "SELECT user_id FROM typing WHERE room_id=?1 AND expires_ms>?2 "
                "ORDER BY user_id ASC", -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, rid, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(st, 2, now_ms());
            while (sqlite3_step(st) == SQLITE_ROW) j_push(ids, j_str(col_str(st, 0)));
            sqlite3_finalize(st);
        }
        json *content = j_obj();
        j_set(content, "user_ids", ids);
        json *ev = j_obj();
        j_set(ev, "type", j_str("m.typing"));
        j_set(ev, "room_id", j_str(rid));
        j_set(ev, "content", content);
        j_set(per, rid, ev);
    }
    j_set(out, "rooms", per);
    return out;
}

/* ------------------------------------------------------- building it all -- */
static void gather_rooms(const char *user_id, const char *conn_key,
                         sroomlist *out) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db(),
            "SELECT m.room_id, m.membership, m.stream_id, COALESCE(e.sender,'') "
            "FROM room_members m LEFT JOIN events e ON e.event_id=m.event_id "
            "WHERE m.user_id=?1", -1, &st, NULL) != SQLITE_OK) return;
    sqlite3_bind_text(st, 1, user_id, -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *rid = col_str(st, 0), *mem = col_str(st, 1);
        if (!*rid) continue;
        bool keep = str_eq(mem, "join") || str_eq(mem, "invite") ||
                    str_eq(mem, "knock") || str_eq(mem, "ban");
        if (str_eq(mem, "leave")) {
            /* Rooms the user left themselves stay out unless this connection
             * has already seen them; a kick always shows up. */
            const char *sender = col_str(st, 3);
            int64_t seen = 0;
            keep = !str_eq(sender, user_id) ||
                   db_int("SELECT 1 FROM slide_room WHERE conn_key=?1 AND room_id=?2",
                          &seen, "ss", conn_key, rid);
        }
        if (!keep) continue;
        sroom s;
        memset(&s, 0, sizeof s);
        s.room_id = xstrdup_(rid);
        s.membership = xstrdup_(mem);
        s.mem_sender = xstrdup_(col_str(st, 3));
        s.mem_stream = sqlite3_column_int64(st, 2);
        s.last_stream = room_latest_stream(rid);
        s.bump = room_bump_stamp(rid);
        if (!str_eq(mem, "join")) s.last_stream = s.mem_stream;
        sr_push(out, s);
    }
    sqlite3_finalize(st);
    if (out->n > 1) qsort(out->v, out->n, sizeof *out->v, sr_cmp);
}

/* Merges one rule's room config into `configs[room_id]`. */
static void cfg_merge(json *configs, const char *room_id, const json *rule,
                      const char *list_name) {
    json *cfg = j_geto(configs, room_id);
    if (!cfg) {
        cfg = j_obj();
        j_set(cfg, "timeline_limit", j_int(0));
        j_set(cfg, "required_state", j_arr());
        j_set(cfg, "lists", j_arr());
        j_set(configs, room_id, cfg);
    }
    int64_t want = j_geti(rule, "timeline_limit", SLIDE_TIMELINE_DEFAULT);
    if (want > j_geti(cfg, "timeline_limit", 0))
        j_set(cfg, "timeline_limit", j_int(want));
    rs_union(j_geta(cfg, "required_state"), j_geta(rule, "required_state"));
    if (list_name) {
        json *lists = j_geta(cfg, "lists");
        if (!arr_has_str(lists, list_name)) j_push(lists, j_str(list_name));
    }
}

static bool slide_empty(const json *b) {
    if (j_len(j_geto(b, "rooms"))) return false;
    const json *ext = j_geto(b, "extensions");
    if (!ext) return true;
    if (j_len(j_deep_get(ext, "to_device.events"))) return false;
    if (j_len(j_deep_get(ext, "account_data.global"))) return false;
    if (j_len(j_deep_get(ext, "account_data.rooms"))) return false;
    if (j_len(j_deep_get(ext, "receipts.rooms"))) return false;
    if (j_len(j_deep_get(ext, "typing.rooms"))) return false;
    if (j_len(j_deep_get(ext, "e2ee.device_lists.changed"))) return false;
    if (j_len(j_deep_get(ext, "e2ee.device_lists.left"))) return false;
    return true;
}

static json *slide_build(const char *user_id, const char *device_id,
                         const char *conn_key, const json *req, int64_t since,
                         int64_t now, bool fresh, int64_t stored_ack,
                         int64_t *ack_out, json **sent_out) {
    sroomlist all = {0};
    gather_rooms(user_id, conn_key, &all);
    json *direct = direct_rooms(user_id);
    json *configs = j_obj();
    json *lists_out = j_obj();

    const json *lists = j_geto((json *)req, "lists");
    for (size_t li = 0; lists && li < j_len(lists) && li < SLIDE_MAX_LISTS; li++) {
        const char *name = j_key(lists, li);
        const json *rule = j_at(lists, li);
        if (!rule || rule->type != J_OBJECT) continue;
        const json *filters = j_geto((json *)rule, "filters");

        json *matched = j_arr();
        for (size_t i = 0; i < all.n; i++)
            if (list_filters_match(filters, &all.v[i], direct))
                j_push(matched, j_str(all.v[i].room_id));

        json *ops = j_arr();
        const json *ranges = j_geta((json *)rule, "ranges");
        json *single = NULL;
        if (!ranges) {
            const json *one = j_geta((json *)rule, "range");
            if (one) { single = j_arr(); j_push(single, j_clone(one)); ranges = single; }
        }
        if (!ranges) {
            json *op = j_obj();
            json *rng = j_arr();
            j_push(rng, j_int(0));
            j_push(rng, j_int(j_len(matched) ? (int64_t)j_len(matched) - 1 : 0));
            j_set(op, "op", j_str("SYNC"));
            j_set(op, "range", rng);
            json *ids = j_arr();
            for (size_t i = 0; i < j_len(matched); i++) {
                const char *rid = j_at(matched, i)->s;
                j_push(ids, j_str(rid));
                cfg_merge(configs, rid, rule, name);
            }
            j_set(op, "room_ids", ids);
            j_push(ops, op);
        } else {
            for (size_t ri = 0; ri < j_len(ranges); ri++) {
                const json *rg = j_at(ranges, ri);
                if (!rg || rg->type != J_ARRAY || j_len(rg) < 2) continue;
                int64_t lo = j_at(rg, 0)->i, hi = j_at(rg, 1)->i;
                if (lo < 0) lo = 0;
                if (hi >= (int64_t)j_len(matched)) hi = (int64_t)j_len(matched) - 1;
                json *ids = j_arr();
                for (int64_t i = lo; i <= hi; i++) {
                    const char *rid = j_at(matched, (size_t)i)->s;
                    j_push(ids, j_str(rid));
                    cfg_merge(configs, rid, rule, name);
                }
                json *op = j_obj();
                json *rng = j_arr();
                j_push(rng, j_int(lo));
                j_push(rng, j_int(hi < lo ? lo : hi));
                j_set(op, "op", j_str("SYNC"));
                j_set(op, "range", rng);
                j_set(op, "room_ids", ids);
                j_push(ops, op);
            }
        }
        j_free(single);
        json *lr = j_obj();
        j_set(lr, "count", j_int((int64_t)j_len(matched)));
        j_set(lr, "ops", ops);
        j_set(lists_out, name, lr);
        j_free(matched);
    }

    const json *subs = j_geto((json *)req, "room_subscriptions");
    for (size_t i = 0; subs && i < j_len(subs) && i < SLIDE_MAX_SUBS; i++) {
        const char *rid = j_key(subs, i);
        const json *rule = j_at(subs, i);
        bool known = false;
        for (size_t k = 0; k < all.n && !known; k++)
            known = str_eq(all.v[k].room_id, rid);
        if (!known || !rule || rule->type != J_OBJECT) continue;
        cfg_merge(configs, rid, rule, NULL);
    }

    json *rooms = j_obj();
    json *sent = j_obj();
    for (size_t i = 0; i < all.n; i++) {
        const sroom *rm = &all.v[i];
        json *cfg = j_geto(configs, rm->room_id);
        if (!cfg) continue;
        int64_t was_sent = -1, was_limit = 0;
        if (!fresh) {
            int64_t v = 0;
            if (db_int("SELECT sent_stream FROM slide_room WHERE conn_key=?1 AND "
                       "room_id=?2", &v, "ss", conn_key, rm->room_id)) {
                was_sent = v;
                db_int("SELECT timeline_limit FROM slide_room WHERE conn_key=?1 "
                       "AND room_id=?2", &was_limit, "ss", conn_key, rm->room_id);
            }
        }
        bool initial = was_sent < 0;
        /* A room this connection last saw as an invitation (or as a room the
         * user was not in) and has since been joined starts again from the
         * beginning: none of its history was ever sent, so the delta the
         * stored mark describes is not the delta the client needs. */
        if (!initial && str_eq(rm->membership, "join") && rm->mem_stream > was_sent)
            initial = true;
        int64_t limit = j_geti(cfg, "timeline_limit", SLIDE_TIMELINE_DEFAULT);
        bool expanded = !initial && limit > was_limit;
        if (!initial && !expanded && rm->last_stream <= was_sent) continue;
        json *res = slide_room_result(user_id, device_id, rm, cfg,
                                      initial ? 0 : was_sent, initial, since,
                                      now, direct, expanded);
        j_set(rooms, rm->room_id, res);
        j_set(sent, rm->room_id, j_int(limit));
    }

    const json *exts = j_geto((json *)req, "extensions");
    json *ext_out = j_obj();
    json *cfg = NULL;
    *ack_out = stored_ack;
    if (ext_on(exts, "to_device", &cfg))
        j_set(ext_out, "to_device",
              ext_to_device(user_id, device_id, cfg, stored_ack, now, ack_out));
    if (ext_on(exts, "e2ee", &cfg))
        j_set(ext_out, "e2ee", ext_e2ee(user_id, device_id, since, now, fresh));
    /* The per-room extensions cover every room the request's rules match, not
     * just the ones with new timeline data: a read receipt or a typing notice
     * is an update to a room whose events have not moved. */
    if (ext_on(exts, "account_data", &cfg))
        j_set(ext_out, "account_data",
              ext_account_data(user_id, configs, fresh ? 0 : since, now));
    if (ext_on(exts, "receipts", &cfg))
        j_set(ext_out, "receipts",
              ext_receipts(user_id, configs, fresh ? 0 : since, now));
    if (ext_on(exts, "typing", &cfg))
        j_set(ext_out, "typing", ext_typing(configs, fresh ? 0 : since, now));

    json *out = j_obj();
    j_set(out, "lists", lists_out);
    j_set(out, "rooms", rooms);
    j_set(out, "extensions", ext_out);
    j_free(configs);
    j_free(direct);
    srl_free(&all);
    *sent_out = sent;
    return out;
}

/* ------------------------------------------------------------ connections -- */
/* "<nonce>_<stream>"; the nonce is minted when the connection is created and
 * ties every pos of that connection to this user and device. */
static char *pos_nonce(const char *pos) {
    const char *us = pos ? strrchr(pos, '_') : NULL;
    if (!us) return NULL;
    size_t n = (size_t)(us - pos);
    char *out = malloc(n + 1);
    if (!out) abort();
    memcpy(out, pos, n);
    out[n] = 0;
    return out;
}

static char *conn_key_of(const char *user_id, const char *device_id,
                         const char *conn_id) {
    return str_fmt("%s\x1f%s\x1f%s", user_id, device_id ? device_id : "",
                   conn_id ? conn_id : "");
}

/* Keeps at most SLIDE_MAX_CONNS connections per device. */
static void conn_trim(const char *user_id, const char *device_id) {
    db_run("DELETE FROM slide_room WHERE conn_key IN (SELECT conn_key FROM "
           "slide_conn WHERE user_id=?1 AND device_id=?2 ORDER BY updated_ts "
           "DESC LIMIT -1 OFFSET ?3)", "ssi", user_id,
           device_id ? device_id : "", (int64_t)SLIDE_MAX_CONNS);
    db_run("DELETE FROM slide_room_prev WHERE conn_key IN (SELECT conn_key FROM "
           "slide_conn WHERE user_id=?1 AND device_id=?2 ORDER BY updated_ts "
           "DESC LIMIT -1 OFFSET ?3)", "ssi", user_id,
           device_id ? device_id : "", (int64_t)SLIDE_MAX_CONNS);
    db_run("DELETE FROM slide_conn WHERE conn_key IN (SELECT conn_key FROM "
           "slide_conn WHERE user_id=?1 AND device_id=?2 ORDER BY updated_ts "
           "DESC LIMIT -1 OFFSET ?3)", "ssi", user_id,
           device_id ? device_id : "", (int64_t)SLIDE_MAX_CONNS);
}

static void h_slide_sync(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    principal p = {0};
    if (!require_auth(r, w, &p)) return;

    json *req = http_json(r);
    json *empty = NULL;
    if (!req || req->type != J_OBJECT) {
        if (r->json_bad) {
            resp_error(w, 400, "M_NOT_JSON", "Request body is not valid JSON");
            principal_free(&p);
            return;
        }
        req = empty = j_obj();
    }
    const char *conn_id = j_gets(req, "conn_id", "");
    const char *pos = http_query(r, "pos");
    if (!pos || !*pos) pos = j_gets(req, "pos", NULL);
    int64_t timeout = 0;
    const char *tq = http_query(r, "timeout");
    if (tq && *tq) timeout = strtoll(tq, NULL, 10);
    else timeout = j_geti(req, "timeout", 0);
    if (timeout < 0) timeout = 0;
    if (timeout > SLIDE_TIMEOUT_MAX) timeout = SLIDE_TIMEOUT_MAX;

    char *conn_key = conn_key_of(p.user_id, p.device_id, conn_id);
    int64_t since = 0, stored_ack = 0;
    bool fresh = true, replay = false;
    char *nonce = NULL;

    if (pos && *pos) {
        char *cur = db_str("SELECT pos FROM slide_conn WHERE conn_key=?1",
                           "s", conn_key);
        char *prev = db_str("SELECT prev_pos FROM slide_conn WHERE conn_key=?1",
                            "s", conn_key);
        bool ok = false;
        if (cur && str_eq(cur, pos)) {
            db_int("SELECT stream_id FROM slide_conn WHERE conn_key=?1", &since,
                   "s", conn_key);
            ok = true;
        } else if (prev && str_eq(prev, pos)) {
            db_int("SELECT prev_stream FROM slide_conn WHERE conn_key=?1", &since,
                   "s", conn_key);
            ok = true;
            replay = true;
        }
        free(cur); free(prev);
        if (!ok) {
            free(conn_key);
            j_free(empty);
            resp_error(w, 400, "M_UNKNOWN_POS",
                       "Unknown position token; start a new connection");
            principal_free(&p);
            return;
        }
        db_int("SELECT to_device_ack FROM slide_conn WHERE conn_key=?1",
               &stored_ack, "s", conn_key);
        nonce = pos_nonce(pos);
        fresh = false;
        /* The answer to a `pos` must not depend on how often it has been
         * asked: a client that loses a response re-sends the same one.  The
         * per-room marks are therefore moved onto the generation this `pos`
         * names before anything is built, and `prev` is left naming it, so
         * that every repeat of this `pos` starts from the same place.  A
         * replay drops the rooms that were only sent after it. */
        hs_lock();
        if (replay) {
            db_run("DELETE FROM slide_room WHERE conn_key=?1", "s", conn_key);
            db_run("INSERT INTO slide_room(conn_key,room_id,sent_stream,"
                   "timeline_limit) SELECT conn_key,room_id,sent_stream,"
                   "timeline_limit FROM slide_room_prev WHERE conn_key=?1",
                   "s", conn_key);
        } else {
            db_run("DELETE FROM slide_room_prev WHERE conn_key=?1", "s", conn_key);
            db_run("INSERT INTO slide_room_prev(conn_key,room_id,sent_stream,"
                   "timeline_limit) SELECT conn_key,room_id,sent_stream,"
                   "timeline_limit FROM slide_room WHERE conn_key=?1",
                   "s", conn_key);
            db_run("UPDATE slide_conn SET prev_pos=?2, prev_stream=?3 "
                   "WHERE conn_key=?1", "ssi", conn_key, pos, since);
        }
        hs_unlock();
    }
    if (fresh) {
        nonce = random_string(16);
        char *first = str_fmt("%s_0", nonce);
        hs_lock();
        db_run("DELETE FROM slide_room WHERE conn_key=?1", "s", conn_key);
        db_run("DELETE FROM slide_room_prev WHERE conn_key=?1", "s", conn_key);
        db_run("INSERT INTO slide_conn(conn_key,user_id,device_id,conn_id,pos,"
               "prev_pos,stream_id,prev_stream,to_device_ack,created_ts,"
               "updated_ts) VALUES(?1,?2,?3,?4,?5,NULL,0,0,0,?6,?6) "
               "ON CONFLICT(conn_key) DO UPDATE SET pos=?5, prev_pos=NULL, "
               "stream_id=0, prev_stream=0, to_device_ack=0, updated_ts=?6",
               "sssssi", conn_key, p.user_id, p.device_id ? p.device_id : "",
               conn_id, first, now_ms());
        conn_trim(p.user_id, p.device_id);
        hs_unlock();
        stream_notify();
        free(first);
    }
    if (!nonce) nonce = random_string(16);

    int64_t deadline = now_ms() + timeout;
    json *body = NULL, *sent = NULL;
    int64_t now = 0, ack = stored_ack;
    for (;;) {
        now = stream_current();
        if (now < since) now = since;
        j_free(body);
        j_free(sent);
        body = sent = NULL;
        body = slide_build(p.user_id, p.device_id, conn_key, req, since, now,
                           fresh, stored_ack, &ack, &sent);
        if (fresh || timeout <= 0 || !slide_empty(body)) break;
        int64_t left = deadline - now_ms();
        if (left <= 0) break;
        stream_wait(now, (int)left);
    }

    char *newpos = str_fmt("%s_%" PRId64, nonce, now);
    hs_lock();
    for (size_t i = 0; i < j_len(sent); i++) {
        db_run("INSERT INTO slide_room(conn_key,room_id,sent_stream,"
               "timeline_limit) VALUES(?1,?2,?3,?4) ON CONFLICT(conn_key,room_id) "
               "DO UPDATE SET sent_stream=?3, timeline_limit=?4", "ssii",
               conn_key, j_key(sent, i), now, j_at(sent, i)->i);
    }
    db_run("UPDATE slide_conn SET pos=?2, stream_id=?3, to_device_ack=?4, "
           "updated_ts=?5 WHERE conn_key=?1", "ssiii", conn_key, newpos, now,
           ack, now_ms());
    hs_unlock();
    stream_notify();

    j_set(body, "pos", j_str(newpos));
    free(newpos);
    free(nonce);
    free(conn_key);
    j_free(sent);
    j_free(empty);
    resp_json(w, 200, body);
    principal_free(&p);
}

/* ==================================================== reporting a room ==== */
/* POST /_matrix/client/v3/rooms/{roomId}/report  (v1.13) */
static void h_report_room(http_req *r, http_resp *w, void *ud) {
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
    char *room_id = resolve_room(r->part[0]);
    if (!room_id || !room_exists(room_id)) {
        free(room_id);
        resp_error(w, 404, "M_NOT_FOUND", "The room was not found.");
        principal_free(&p);
        return;
    }
    hs_lock();
    db_run("INSERT INTO content_reports(kind,target,event_id,sender,reason,"
           "score,created_ts) VALUES('room',?1,NULL,?2,?3,NULL,?4)", "sssi",
           room_id, p.user_id, reason->s, now_ms());
    hs_unlock();
    stream_notify();
    free(room_id);
    resp_json(w, 200, j_obj());
    principal_free(&p);
}

/* ========================================================= admin  routes == */
/* Locking and suspension are set through /_dendrite/admin, deliberately
 * outside /_matrix so that they can never shadow a specified endpoint.  The
 * caller proves itself with the deployment's registration_shared_secret (or
 * the MSC3861 admin_token), or with the access token of a user that already
 * carries the `admin` flag. */
static bool admin_ok(http_req *r) {
    char *tok = slide_bearer_token(r);
    if (!tok) return false;
    bool ok = false;
    if (CFG.registration_shared_secret && *CFG.registration_shared_secret &&
        str_eq(tok, CFG.registration_shared_secret)) ok = true;
    if (!ok && oauth_is_admin_token(tok)) ok = true;
    if (!ok) {
        char *user = db_str("SELECT user_id FROM access_tokens WHERE token=?1",
                            "s", tok);
        if (user) ok = slide_get_user_flag(user, "admin");
        free(user);
    }
    free(tok);
    return ok;
}

static json *flags_json(const char *user_id) {
    json *b = j_obj();
    j_set(b, "user_id", j_str(user_id));
    j_set(b, "locked", j_bool(slide_get_user_flag(user_id, "locked")));
    j_set(b, "suspended", j_bool(slide_get_user_flag(user_id, "suspended")));
    j_set(b, "admin", j_bool(slide_get_user_flag(user_id, "admin")));
    return b;
}

static void admin_flag(http_req *r, http_resp *w, const char *flag) {
    if (!admin_ok(r)) {
        resp_error(w, 403, "M_FORBIDDEN", "This endpoint is for server admins");
        return;
    }
    const char *uid = r->part[0];
    char *known = db_str("SELECT user_id FROM users WHERE user_id=?1", "s", uid);
    if (!known) {
        resp_error(w, 404, "M_NOT_FOUND", "The user was not found.");
        return;
    }
    free(known);
    json *body = http_json(r);
    bool on = body ? j_getb(body, flag, true) : true;
    slide_set_user_flag(uid, flag, on);
    resp_json(w, 200, flags_json(uid));
}

static void h_admin_lock(http_req *r, http_resp *w, void *ud) {
    (void)ud; admin_flag(r, w, "locked");
}
static void h_admin_suspend(http_req *r, http_resp *w, void *ud) {
    (void)ud; admin_flag(r, w, "suspended");
}
static void h_admin_admin(http_req *r, http_resp *w, void *ud) {
    (void)ud; admin_flag(r, w, "admin");
}
static void h_admin_get(http_req *r, http_resp *w, void *ud) {
    (void)ud;
    if (!admin_ok(r)) {
        resp_error(w, 403, "M_FORBIDDEN", "This endpoint is for server admins");
        return;
    }
    resp_json(w, 200, flags_json(r->part[0]));
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

void api_register_slide(http_server *s) {
    spec_advertise("v1.12");
    spec_advertise("v1.13");
    slide_tables();

    http_route(s, "POST", SS "/sync", h_slide_sync);
    /* The same handler under the MSC's eventual home, for clients that have
     * already moved on. */
    http_route(s, "POST", "/_matrix/client/v4/sync", h_slide_sync);

    route_both(s, "POST", "/rooms/:/report", h_report_room);

    http_route(s, "GET", ADMIN "/users/:", h_admin_get);
    http_route(s, "POST", ADMIN "/users/:/lock", h_admin_lock);
    http_route(s, "POST", ADMIN "/users/:/suspend", h_admin_suspend);
    http_route(s, "POST", ADMIN "/users/:/admin", h_admin_admin);

    slide_install_gate(s);
}

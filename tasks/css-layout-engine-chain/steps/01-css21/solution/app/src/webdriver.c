/* W3C WebDriver Level 1 remote end for the CSS layout engine.
   https://www.w3.org/TR/webdriver1/ */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "engine.h"
#include "http.h"
#include "json.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* §13.1: the web element identifier is this exact, fixed string. */
#define WEB_ELEMENT_ID "element-6066-11e4-a52e-4f735466cecf"

#define BROWSER_NAME    "cssdriver"
#define BROWSER_VERSION "1.0"
#define PLATFORM_NAME   "linux"

#define DEFAULT_VIEWPORT_W 800.0
#define DEFAULT_VIEWPORT_H 600.0

/* §6.5 error codes. */
#define ERR_INVALID_ARGUMENT    "invalid argument"
#define ERR_INVALID_SELECTOR    "invalid selector"
#define ERR_INVALID_SESSION     "invalid session id"
#define ERR_NO_SUCH_ELEMENT     "no such element"
#define ERR_SESSION_NOT_CREATED "session not created"
#define ERR_STALE_ELEMENT       "stale element reference"
#define ERR_UNKNOWN_COMMAND     "unknown command"
#define ERR_UNKNOWN_ERROR       "unknown error"

#define MAX_SEGS 8

/* ------------------------------------------------------------------------ */
/* session                                                                   */

typedef struct {
    int           active;
    char          id[40];
    double        vw, vh;      /* viewport, also the window content size */
    double        wx, wy;
    eng_doc      *doc;
    eng_node    **handles;
    size_t        handle_count, handle_cap;
    unsigned long nav;         /* navigation generation, encoded in handle ids */
} session;

static session g_session;

static void handles_reset(session *s)
{
    free(s->handles);
    s->handles = NULL;
    s->handle_count = s->handle_cap = 0;
}

static void session_clear(session *s)
{
    eng_free(s->doc);
    s->doc = NULL;
    handles_reset(s);
    memset(s, 0, sizeof *s);
}

/* Web element references are stable: an element already seen keeps its id. */
static int handle_for(session *s, eng_node *n, char *out, size_t outlen)
{
    for (size_t i = 0; i < s->handle_count; i++) {
        if (s->handles[i] == n) {
            snprintf(out, outlen, "%lu-%zu", s->nav, i);
            return 0;
        }
    }
    if (s->handle_count == s->handle_cap) {
        size_t nc = s->handle_cap ? s->handle_cap * 2 : 16;
        eng_node **nh = realloc(s->handles, nc * sizeof *nh);
        if (!nh) return -1;
        s->handles = nh;
        s->handle_cap = nc;
    }
    s->handles[s->handle_count] = n;
    snprintf(out, outlen, "%lu-%zu", s->nav, s->handle_count);
    s->handle_count++;
    return 0;
}

typedef enum { H_OK, H_STALE, H_NONE } handle_status;

static handle_status handle_lookup(session *s, const char *id, eng_node **out)
{
    if (!id || *id < '0' || *id > '9') return H_NONE;
    char *end;
    errno = 0;
    unsigned long gen = strtoul(id, &end, 10);
    if (errno != 0 || *end != '-') return H_NONE;
    const char *ip = end + 1;
    if (*ip < '0' || *ip > '9') return H_NONE;
    errno = 0;
    unsigned long idx = strtoul(ip, &end, 10);
    if (errno != 0 || *end != '\0') return H_NONE;

    if (gen > 0 && gen < s->nav) return H_STALE;   /* issued before a navigation */
    if (gen != s->nav) return H_NONE;
    if (idx >= s->handle_count) return H_NONE;
    *out = s->handles[idx];
    return H_OK;
}

/* ------------------------------------------------------------------------ */
/* responses                                                                 */

static int err_http_status(const char *code)
{
    if (strcmp(code, ERR_INVALID_ARGUMENT) == 0)    return 400;
    if (strcmp(code, ERR_INVALID_SELECTOR) == 0)    return 400;
    if (strcmp(code, ERR_INVALID_SESSION) == 0)     return 404;
    if (strcmp(code, ERR_NO_SUCH_ELEMENT) == 0)     return 404;
    if (strcmp(code, ERR_STALE_ELEMENT) == 0)       return 404;
    if (strcmp(code, ERR_UNKNOWN_COMMAND) == 0)     return 404;
    if (strcmp(code, ERR_SESSION_NOT_CREATED) == 0) return 500;
    return 500;                                      /* unknown error */
}

static void respond(http_conn *c, int status, json_writer *w, int ka)
{
    if (w->err) {
        static const char oom[] =
            "{\"value\":{\"error\":\"unknown error\","
            "\"message\":\"out of memory\",\"stacktrace\":\"\"}}";
        http_send_json(c, 500, oom, sizeof oom - 1, ka);
        return;
    }
    http_send_json(c, status, jw_text(w), jw_len(w), ka);
}

static void send_error(http_conn *c, const char *code, const char *msg, int ka)
{
    json_writer w;
    jw_init(&w);
    jw_obj_open(&w);
    jw_key(&w, "value");
    jw_obj_open(&w);
    jw_key(&w, "error");      jw_str(&w, code);
    jw_key(&w, "message");    jw_str(&w, msg ? msg : "");
    jw_key(&w, "stacktrace"); jw_str(&w, "");
    jw_obj_close(&w);
    jw_obj_close(&w);
    respond(c, err_http_status(code), &w, ka);
    jw_free(&w);
}

static void send_null(http_conn *c, int ka)
{
    static const char body[] = "{\"value\":null}";
    http_send_json(c, 200, body, sizeof body - 1, ka);
}

/* ------------------------------------------------------------------------ */
/* helpers                                                                   */

static void make_session_id(char *out, size_t outlen)
{
    unsigned char b[16];
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f || fread(b, 1, sizeof b, f) != sizeof b) {
        unsigned seed = (unsigned)time(NULL) ^ ((unsigned)getpid() << 16);
        for (size_t i = 0; i < sizeof b; i++) {
            seed = seed * 1103515245u + 12345u;
            b[i] = (unsigned char)(seed >> 16);
        }
    }
    if (f) fclose(f);
    b[6] = (unsigned char)((b[6] & 0x0F) | 0x40);   /* version 4 */
    b[8] = (unsigned char)((b[8] & 0x3F) | 0x80);   /* variant 1 */
    snprintf(out, outlen,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
             b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}

static void free_segs(char **segs, int n)
{
    for (int i = 0; i < n; i++) free(segs[i]);
}

/* Splits an origin-form request target into percent-decoded segments.
   Returns the count, -1 on a bad escape, -2 if the target is unroutable. */
static int split_target(const char *target, char **segs, int max)
{
    if (*target != '/') return -2;
    const char *p = target + 1;
    const char *end = target + strcspn(target, "?#");
    if (end < p) end = p;

    int n = 0;
    while (p <= end) {
        const char *slash = memchr(p, '/', (size_t)(end - p));
        const char *stop = slash ? slash : end;
        /* Drop a trailing empty segment so "/status/" routes like "/status". */
        if (!(stop == end && stop == p && n > 0)) {
            if (n >= max) { free_segs(segs, n); return -2; }
            char *d = http_percent_decode(p, (size_t)(stop - p));
            if (!d) { free_segs(segs, n); return -1; }
            segs[n++] = d;
        }
        if (!slash) break;
        p = slash + 1;
    }
    return n;
}

/* §6.3: the body of a command with parameters must be a JSON Object. */
static json_value *require_object_body(http_conn *c, const http_request *r, int ka)
{
    json_value *v = r->body ? json_parse(r->body, r->body_len) : NULL;
    if (!json_is(v, JSON_OBJECT)) {
        json_free(v);
        send_error(c, ERR_INVALID_ARGUMENT,
                   "request body must be a JSON object", ka);
        return NULL;
    }
    return v;
}

/* ------------------------------------------------------------------------ */
/* §8.3 Status                                                               */

static void cmd_status(http_conn *c, int ka)
{
    json_writer w;
    jw_init(&w);
    jw_obj_open(&w);
    jw_key(&w, "value");
    jw_obj_open(&w);
    /* "ready" reports whether a *new* session could be created; this remote
       end supports exactly one at a time. */
    jw_key(&w, "ready");   jw_bool(&w, !g_session.active);
    jw_key(&w, "message"); jw_str(&w, g_session.active
                                  ? "a session is already active"
                                  : "ready for a new session");
    jw_obj_close(&w);
    jw_obj_close(&w);
    respond(c, 200, &w, ka);
    jw_free(&w);
}

/* ------------------------------------------------------------------------ */
/* §8.1 New Session                                                          */

/* §7.2: standard capabilities have fixed value types; extension capabilities
   are passed through untouched. */
static int capability_type_ok(const char *name, const json_value *v)
{
    if (json_typeof(v) == JSON_NULL) return 1;
    if (strcmp(name, "browserName") == 0 || strcmp(name, "browserVersion") == 0 ||
        strcmp(name, "platformName") == 0 || strcmp(name, "pageLoadStrategy") == 0 ||
        strcmp(name, "unhandledPromptBehavior") == 0)
        return json_is(v, JSON_STRING);
    if (strcmp(name, "acceptInsecureCerts") == 0 ||
        strcmp(name, "setWindowRect") == 0 ||
        strcmp(name, "strictFileInteractability") == 0)
        return json_is(v, JSON_BOOL);
    if (strcmp(name, "proxy") == 0 || strcmp(name, "timeouts") == 0)
        return json_is(v, JSON_OBJECT);
    return 1;
}

static int validate_caps_object(const json_value *o, const char **bad_key)
{
    for (size_t i = 0; i < json_obj_len(o); i++) {
        const char *k = json_obj_key(o, i);
        if (!capability_type_ok(k, json_obj_val(o, i))) { *bad_key = k; return 0; }
    }
    return 1;
}

static const json_value *lookup_cap(const json_value *always,
                                    const json_value *first, const char *name)
{
    const json_value *v = first ? json_obj_get(first, name) : NULL;
    if (!v && always) v = json_obj_get(always, name);
    return v;
}

/* We can only fail to match on the browser and platform names. */
static int caps_match(const json_value *always, const json_value *first)
{
    const json_value *v = lookup_cap(always, first, "browserName");
    if (json_is(v, JSON_STRING) && strcmp(json_str(v), BROWSER_NAME) != 0)
        return 0;
    v = lookup_cap(always, first, "platformName");
    if (json_is(v, JSON_STRING) && strcmp(json_str(v), PLATFORM_NAME) != 0 &&
        strcmp(json_str(v), "any") != 0)
        return 0;
    return 1;
}

static int is_standard_cap(const char *k)
{
    static const char *const names[] = {
        "browserName", "browserVersion", "platformName", "acceptInsecureCerts",
        "pageLoadStrategy", "proxy", "setWindowRect",
        "strictFileInteractability", "timeouts", "unhandledPromptBehavior"
    };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        if (strcmp(k, names[i]) == 0) return 1;
    return 0;
}

static void write_matched_caps(json_writer *w, const json_value *always,
                               const json_value *first)
{
    jw_obj_open(w);
    jw_key(w, "browserName");               jw_str(w, BROWSER_NAME);
    jw_key(w, "browserVersion");            jw_str(w, BROWSER_VERSION);
    jw_key(w, "platformName");              jw_str(w, PLATFORM_NAME);
    jw_key(w, "acceptInsecureCerts");       jw_bool(w, 0);
    jw_key(w, "pageLoadStrategy");          jw_str(w, "normal");
    jw_key(w, "setWindowRect");             jw_bool(w, 1);
    jw_key(w, "strictFileInteractability"); jw_bool(w, 0);
    jw_key(w, "unhandledPromptBehavior");   jw_str(w, "dismiss and notify");
    jw_key(w, "proxy");                     jw_obj_open(w); jw_obj_close(w);
    jw_key(w, "timeouts");
    jw_obj_open(w);
    jw_key(w, "implicit"); jw_num(w, 0);
    jw_key(w, "pageLoad"); jw_num(w, 300000);
    jw_key(w, "script");   jw_num(w, 30000);
    jw_obj_close(w);

    /* Echo back extension capabilities the client asked for. */
    const json_value *src[2] = { always, first };
    for (int s = 0; s < 2; s++) {
        for (size_t i = 0; i < json_obj_len(src[s]); i++) {
            const char *k = json_obj_key(src[s], i);
            if (is_standard_cap(k)) continue;
            jw_key(w, k);
            jw_val(w, json_obj_val(src[s], i));
        }
    }
    jw_obj_close(w);
}

static void cmd_new_session(http_conn *c, const json_value *params, int ka)
{
    /* §8.1 step 1: the session limit is checked before capability processing. */
    if (g_session.active) {
        send_error(c, ERR_SESSION_NOT_CREATED,
                   "a session is already active; this remote end supports one "
                   "session at a time", ka);
        return;
    }

    const json_value *caps = json_obj_get(params, "capabilities");
    if (!json_is(caps, JSON_OBJECT)) {
        send_error(c, ERR_INVALID_ARGUMENT,
                   "'capabilities' must be a JSON object", ka);
        return;
    }

    const json_value *always = json_obj_get(caps, "alwaysMatch");
    if (json_typeof(always) == JSON_NULL) always = NULL;
    if (always && !json_is(always, JSON_OBJECT)) {
        send_error(c, ERR_INVALID_ARGUMENT,
                   "'alwaysMatch' must be a JSON object", ka);
        return;
    }
    const json_value *fm = json_obj_get(caps, "firstMatch");
    if (json_typeof(fm) == JSON_NULL) fm = NULL;
    if (fm && !json_is(fm, JSON_ARRAY)) {
        send_error(c, ERR_INVALID_ARGUMENT, "'firstMatch' must be an array", ka);
        return;
    }

    char msg[160];
    const char *bad = NULL;
    if (always && !validate_caps_object(always, &bad)) {
        snprintf(msg, sizeof msg, "capability '%s' has the wrong type", bad);
        send_error(c, ERR_INVALID_ARGUMENT, msg, ka);
        return;
    }

    const json_value *chosen = NULL;
    int found = 0;
    if (!fm) {
        found = caps_match(always, NULL);
    } else {
        for (size_t i = 0; i < json_arr_len(fm); i++) {
            const json_value *e = json_arr_get(fm, i);
            if (!json_is(e, JSON_OBJECT)) {
                send_error(c, ERR_INVALID_ARGUMENT,
                           "each 'firstMatch' entry must be a JSON object", ka);
                return;
            }
            if (!validate_caps_object(e, &bad)) {
                snprintf(msg, sizeof msg, "capability '%s' has the wrong type", bad);
                send_error(c, ERR_INVALID_ARGUMENT, msg, ka);
                return;
            }
            /* §7.2: a key may not appear in both alwaysMatch and firstMatch. */
            if (always) {
                for (size_t k = 0; k < json_obj_len(e); k++) {
                    const char *key = json_obj_key(e, k);
                    if (json_obj_get(always, key)) {
                        snprintf(msg, sizeof msg,
                                 "capability '%s' appears in both alwaysMatch "
                                 "and firstMatch", key);
                        send_error(c, ERR_INVALID_ARGUMENT, msg, ka);
                        return;
                    }
                }
            }
            if (!found && caps_match(always, e)) { found = 1; chosen = e; }
        }
    }

    if (!found) {
        send_error(c, ERR_SESSION_NOT_CREATED,
                   "no capability set matched this remote end", ka);
        return;
    }

    session_clear(&g_session);
    g_session.active = 1;
    make_session_id(g_session.id, sizeof g_session.id);
    g_session.vw = DEFAULT_VIEWPORT_W;
    g_session.vh = DEFAULT_VIEWPORT_H;

    json_writer w;
    jw_init(&w);
    jw_obj_open(&w);
    jw_key(&w, "value");
    jw_obj_open(&w);
    jw_key(&w, "sessionId");    jw_str(&w, g_session.id);
    jw_key(&w, "capabilities"); write_matched_caps(&w, always, chosen);
    jw_obj_close(&w);
    jw_obj_close(&w);
    respond(c, 200, &w, ka);
    jw_free(&w);
}

/* ------------------------------------------------------------------------ */
/* §10.7.3 Set Window Rect                                                   */

static int rect_param(const json_value *params, const char *name,
                      double lo, double hi, double *out, int *present)
{
    const json_value *v = json_obj_get(params, name);
    if (json_typeof(v) == JSON_NULL) { *present = 0; return 0; }
    double d;
    if (!json_num(v, &d)) return -1;
    if (!(d >= lo && d <= hi)) return -1;
    *out = d;
    *present = 1;
    return 0;
}

static void cmd_set_window_rect(http_conn *c, const json_value *params, int ka)
{
    double w_val = 0, h_val = 0, x_val = 0, y_val = 0;
    int hw = 0, hh = 0, hx = 0, hy = 0;
    const double i32max = 2147483647.0, i32min = -2147483648.0;

    if (rect_param(params, "width",  0, i32max, &w_val, &hw) != 0 ||
        rect_param(params, "height", 0, i32max, &h_val, &hh) != 0) {
        send_error(c, ERR_INVALID_ARGUMENT,
                   "'width' and 'height' must be null or a number in "
                   "[0, 2^31-1]", ka);
        return;
    }
    if (rect_param(params, "x", i32min, i32max, &x_val, &hx) != 0 ||
        rect_param(params, "y", i32min, i32max, &y_val, &hy) != 0) {
        send_error(c, ERR_INVALID_ARGUMENT,
                   "'x' and 'y' must be null or a number in "
                   "[-2^31, 2^31-1]", ka);
        return;
    }

    /* The window content box is the viewport used by later navigations. */
    if (hw) g_session.vw = w_val;
    if (hh) g_session.vh = h_val;
    if (hx) g_session.wx = x_val;
    if (hy) g_session.wy = y_val;

    json_writer w;
    jw_init(&w);
    jw_obj_open(&w);
    jw_key(&w, "value");
    jw_obj_open(&w);
    jw_key(&w, "x");      jw_num(&w, g_session.wx);
    jw_key(&w, "y");      jw_num(&w, g_session.wy);
    jw_key(&w, "width");  jw_num(&w, g_session.vw);
    jw_key(&w, "height"); jw_num(&w, g_session.vh);
    jw_obj_close(&w);
    jw_obj_close(&w);
    respond(c, 200, &w, ka);
    jw_free(&w);
}

/* ------------------------------------------------------------------------ */
/* §9.1 Navigate To                                                          */

static void cmd_navigate(http_conn *c, const json_value *params, int ka)
{
    const json_value *u = json_obj_get(params, "url");
    if (!json_is(u, JSON_STRING)) {
        send_error(c, ERR_INVALID_ARGUMENT, "'url' must be a string", ka);
        return;
    }
    const char *url = json_str(u);

    size_t scheme_len = strcspn(url, ":");
    if (url[scheme_len] != ':' || scheme_len != 4 ||
        strncasecmp(url, "file", 4) != 0) {
        send_error(c, ERR_UNKNOWN_ERROR,
                   "only file: URLs can be loaded by this remote end", ka);
        return;
    }
    const char *rest = url + scheme_len + 1;
    if (rest[0] != '/' || rest[1] != '/') {
        send_error(c, ERR_UNKNOWN_ERROR, "malformed file: URL", ka);
        return;
    }
    rest += 2;

    /* An empty host or "localhost" denotes the local filesystem (RFC 8089). */
    const char *slash = strchr(rest, '/');
    if (!slash) {
        send_error(c, ERR_UNKNOWN_ERROR, "file: URL has no path", ka);
        return;
    }
    size_t hostlen = (size_t)(slash - rest);
    if (hostlen != 0 && !(hostlen == 9 && strncasecmp(rest, "localhost", 9) == 0)) {
        send_error(c, ERR_UNKNOWN_ERROR, "non-local file: URL", ka);
        return;
    }

    size_t pathlen = strcspn(slash, "?#");
    char *path = http_percent_decode(slash, pathlen);
    if (!path) {
        send_error(c, ERR_UNKNOWN_ERROR,
                   "malformed percent-escape in file: URL", ka);
        return;
    }

    eng_doc *doc = eng_load_file(path, g_session.vw, g_session.vh);
    if (!doc) {
        char msg[512];
        snprintf(msg, sizeof msg, "cannot load '%s'", path);
        free(path);
        send_error(c, ERR_UNKNOWN_ERROR, msg, ka);
        return;
    }
    free(path);

    /* A successful navigation replaces the document and invalidates handles. */
    eng_free(g_session.doc);
    g_session.doc = doc;
    handles_reset(&g_session);
    g_session.nav++;
    send_null(c, ka);
}

/* ------------------------------------------------------------------------ */
/* §12.2.3 Find Elements / §12.2.5 Find Elements From Element                */

static void cmd_find_elements(http_conn *c, const json_value *params,
                              const char *root_eid, int ka)
{
    const json_value *using = json_obj_get(params, "using");
    if (!json_is(using, JSON_STRING)) {
        send_error(c, ERR_INVALID_ARGUMENT, "'using' must be a string", ka);
        return;
    }
    if (strcmp(json_str(using), "css selector") != 0) {
        send_error(c, ERR_INVALID_ARGUMENT,
                   "the only supported location strategy is 'css selector'", ka);
        return;
    }
    const json_value *value = json_obj_get(params, "value");
    if (!json_is(value, JSON_STRING)) {
        send_error(c, ERR_INVALID_ARGUMENT, "'value' must be a string", ka);
        return;
    }
    const char *selector = json_str(value);

    /* §12.2.5 resolves the start node after validating the parameters. */
    eng_node *root = NULL;
    if (root_eid) {
        switch (handle_lookup(&g_session, root_eid, &root)) {
        case H_STALE:
            send_error(c, ERR_STALE_ELEMENT,
                       "the element belongs to a previous document", ka);
            return;
        case H_NONE:
            send_error(c, ERR_NO_SUCH_ELEMENT, "unknown element reference", ka);
            return;
        case H_OK:
            break;
        }
    }

    eng_node **found = NULL;
    int n = 0;
    if (g_session.doc) {
        int cap = 64;
        for (;;) {
            eng_node **grown = realloc(found, (size_t)cap * sizeof *grown);
            if (!grown) {
                free(found);
                send_error(c, ERR_UNKNOWN_ERROR, "out of memory", ka);
                return;
            }
            found = grown;
            n = eng_query(g_session.doc, selector, root, found, cap);
            if (n < 0) {
                free(found);
                send_error(c, ERR_INVALID_SELECTOR,
                           "the selector is not valid CSS", ka);
                return;
            }
            /* A full buffer may mean the result was truncated: retry larger. */
            if (n < cap || cap >= (1 << 20)) break;
            cap *= 2;
        }
    }

    json_writer w;
    jw_init(&w);
    jw_obj_open(&w);
    jw_key(&w, "value");
    jw_arr_open(&w);
    for (int i = 0; i < n; i++) {
        char eid[48];
        if (handle_for(&g_session, found[i], eid, sizeof eid) != 0) {
            free(found);
            jw_free(&w);
            send_error(c, ERR_UNKNOWN_ERROR, "out of memory", ka);
            return;
        }
        jw_obj_open(&w);
        jw_key(&w, WEB_ELEMENT_ID);
        jw_str(&w, eid);
        jw_obj_close(&w);
    }
    jw_arr_close(&w);
    jw_obj_close(&w);
    free(found);
    respond(c, 200, &w, ka);
    jw_free(&w);
}

/* ------------------------------------------------------------------------ */
/* §13.6 Get Element Rect / §13.4 Get Element CSS Value                      */

static int resolve_element(http_conn *c, const char *eid, eng_node **out, int ka)
{
    switch (handle_lookup(&g_session, eid, out)) {
    case H_OK:
        return 0;
    case H_STALE:
        send_error(c, ERR_STALE_ELEMENT,
                   "the element belongs to a previous document", ka);
        return -1;
    default:
        send_error(c, ERR_NO_SUCH_ELEMENT, "unknown element reference", ka);
        return -1;
    }
}

static void cmd_element_rect(http_conn *c, const char *eid, int ka)
{
    eng_node *n;
    if (resolve_element(c, eid, &n, ka) != 0) return;

    double x = 0, y = 0, width = 0, height = 0;
    eng_rect(g_session.doc, n, &x, &y, &width, &height);

    json_writer w;
    jw_init(&w);
    jw_obj_open(&w);
    jw_key(&w, "value");
    jw_obj_open(&w);
    jw_key(&w, "x");      jw_num(&w, x);
    jw_key(&w, "y");      jw_num(&w, y);
    jw_key(&w, "width");  jw_num(&w, width);
    jw_key(&w, "height"); jw_num(&w, height);
    jw_obj_close(&w);
    jw_obj_close(&w);
    respond(c, 200, &w, ka);
    jw_free(&w);
}

static void cmd_element_css(http_conn *c, const char *eid, const char *prop,
                            int ka)
{
    eng_node *n;
    if (resolve_element(c, eid, &n, ka) != 0) return;

    char buf[4096];
    buf[0] = '\0';
    /* §13.4: an unsupported property yields the empty string, not an error. */
    (void)eng_css(g_session.doc, n, prop, buf, sizeof buf);
    buf[sizeof buf - 1] = '\0';

    json_writer w;
    jw_init(&w);
    jw_obj_open(&w);
    jw_key(&w, "value");
    jw_str(&w, buf);
    jw_obj_close(&w);
    respond(c, 200, &w, ka);
    jw_free(&w);
}

/* ------------------------------------------------------------------------ */
/* dispatch (§6.3)                                                           */

typedef enum {
    R_NONE = 0,
    R_DELETE_SESSION,
    R_WINDOW_RECT,
    R_NAVIGATE,
    R_FIND,
    R_FIND_FROM,
    R_ELEM_RECT,
    R_ELEM_CSS
} route_id;

static void handle_session_route(http_conn *c, const http_request *r,
                                 char **segs, int n, int ka)
{
    const char *method = r->method;
    int is_get  = strcmp(method, "GET") == 0;
    int is_post = strcmp(method, "POST") == 0;
    int is_del  = strcmp(method, "DELETE") == 0;

    route_id route = R_NONE;
    if (n == 2 && is_del)
        route = R_DELETE_SESSION;
    else if (n == 3 && is_post && strcmp(segs[2], "url") == 0)
        route = R_NAVIGATE;
    else if (n == 3 && is_post && strcmp(segs[2], "elements") == 0)
        route = R_FIND;
    else if (n == 4 && is_post && strcmp(segs[2], "window") == 0 &&
             strcmp(segs[3], "rect") == 0)
        route = R_WINDOW_RECT;
    else if (n == 5 && is_post && strcmp(segs[2], "element") == 0 &&
             strcmp(segs[4], "elements") == 0)
        route = R_FIND_FROM;
    else if (n == 5 && is_get && strcmp(segs[2], "element") == 0 &&
             strcmp(segs[4], "rect") == 0)
        route = R_ELEM_RECT;
    else if (n == 6 && is_get && strcmp(segs[2], "element") == 0 &&
             strcmp(segs[4], "css") == 0)
        route = R_ELEM_CSS;

    if (route == R_NONE) {
        send_error(c, ERR_UNKNOWN_COMMAND, "unknown command", ka);
        return;
    }
    /* §6.3: the session is matched before the parameters are parsed. */
    if (!g_session.active || strcmp(segs[1], g_session.id) != 0) {
        send_error(c, ERR_INVALID_SESSION, "no such session", ka);
        return;
    }

    json_value *params = NULL;
    if (is_post) {
        params = require_object_body(c, r, ka);
        if (!params) return;
    }

    switch (route) {
    case R_DELETE_SESSION:
        session_clear(&g_session);
        send_null(c, ka);
        break;
    case R_NAVIGATE:     cmd_navigate(c, params, ka); break;
    case R_WINDOW_RECT:  cmd_set_window_rect(c, params, ka); break;
    case R_FIND:         cmd_find_elements(c, params, NULL, ka); break;
    case R_FIND_FROM:    cmd_find_elements(c, params, segs[3], ka); break;
    case R_ELEM_RECT:    cmd_element_rect(c, segs[3], ka); break;
    case R_ELEM_CSS:     cmd_element_css(c, segs[3], segs[5], ka); break;
    default:             send_error(c, ERR_UNKNOWN_COMMAND, "unknown command", ka);
    }
    json_free(params);
}

static void handle_request(http_conn *c, const http_request *r)
{
    int ka = r->keep_alive;
    char *segs[MAX_SEGS];
    int n = split_target(r->target, segs, MAX_SEGS);

    if (n == -1) {
        send_error(c, ERR_INVALID_ARGUMENT,
                   "malformed percent-escape in the request path", ka);
        return;
    }
    if (n < 0) {
        send_error(c, ERR_UNKNOWN_COMMAND, "unknown command", ka);
        return;
    }

    if (n == 1 && strcmp(segs[0], "status") == 0) {
        if (strcmp(r->method, "GET") == 0) cmd_status(c, ka);
        else send_error(c, ERR_UNKNOWN_COMMAND, "unknown command", ka);
    } else if (n == 1 && strcmp(segs[0], "session") == 0) {
        if (strcmp(r->method, "POST") == 0) {
            json_value *params = require_object_body(c, r, ka);
            if (params) {
                cmd_new_session(c, params, ka);
                json_free(params);
            }
        } else {
            send_error(c, ERR_UNKNOWN_COMMAND, "unknown command", ka);
        }
    } else if (n >= 2 && strcmp(segs[0], "session") == 0) {
        handle_session_route(c, r, segs, n, ka);
    } else {
        send_error(c, ERR_UNKNOWN_COMMAND, "unknown command", ka);
    }

    free_segs(segs, n);
}

/* ------------------------------------------------------------------------ */
/* main                                                                      */

static void serve(int fd, int listen_fd)
{
    http_conn c;
    http_conn_init(&c, fd, listen_fd);
    for (;;) {
        http_request req;
        http_status st = http_read_request(&c, &req);
        if (st == HTTP_CLOSED) break;
        if (st == HTTP_MALFORMED) {
            send_error(&c, ERR_INVALID_ARGUMENT, "malformed HTTP request", 0);
            break;
        }
        if (st == HTTP_TOO_LARGE) {
            send_error(&c, ERR_INVALID_ARGUMENT,
                       "request body exceeds the 8 MiB limit", 0);
            break;
        }
        int keep_alive = req.keep_alive;
        handle_request(&c, &req);
        http_request_free(&req);
        if (!keep_alive) break;
    }
    http_conn_free(&c);
}

int main(int argc, char **argv)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, NULL);

    if (argc != 2 || strncmp(argv[1], "--port=", 7) != 0) {
        fprintf(stderr, "usage: %s --port=<n>\n", argv[0]);
        return 2;
    }
    const char *pstr = argv[1] + 7;
    char *end;
    errno = 0;
    long port = strtol(pstr, &end, 10);
    if (errno != 0 || *pstr == '\0' || *end != '\0' || port < 1 || port > 65535) {
        fprintf(stderr, "%s: invalid port '%s'\n", argv[0], pstr);
        return 2;
    }

    int listen_fd = http_listen((int)port);
    if (listen_fd < 0) {
        fprintf(stderr, "%s: cannot bind 127.0.0.1:%ld: %s\n",
                argv[0], port, strerror(errno));
        return 1;
    }
    fprintf(stderr, "cssdriver listening on 127.0.0.1:%ld\n", port);
    fflush(stderr);

    for (;;) {
        int fd = accept(listen_fd, NULL, NULL);
        if (fd < 0) {
            if (errno == EINTR || errno == ECONNABORTED || errno == EAGAIN)
                continue;
            fprintf(stderr, "%s: accept: %s\n", argv[0], strerror(errno));
            break;
        }
        http_tune(fd);
        serve(fd, listen_fd);
        close(fd);
    }

    session_clear(&g_session);
    close(listen_fd);
    return 1;
}

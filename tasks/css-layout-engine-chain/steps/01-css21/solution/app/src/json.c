#include "json.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JSON_MAX_DEPTH 100

struct json_value {
    json_type type;
    union {
        int    b;
        double num;
        struct { char *ptr; size_t len; } str;
        struct { json_value **items; size_t count, cap; } arr;
        struct { char **keys; json_value **vals; size_t count, cap; } obj;
    } u;
};

/* ------------------------------------------------------------------------ */
/* growable byte buffer                                                      */

static int sb_append(char **buf, size_t *cap, size_t *len,
                     const char *src, size_t n)
{
    if (*len + n + 1 > *cap) {
        size_t nc = *cap ? *cap : 32;
        while (nc < *len + n + 1) {
            if (nc > (size_t)-1 / 2) return -1;
            nc *= 2;
        }
        char *nb = realloc(*buf, nc);
        if (!nb) return -1;
        *buf = nb;
        *cap = nc;
    }
    if (n) memcpy(*buf + *len, src, n);
    *len += n;
    (*buf)[*len] = '\0';
    return 0;
}

static size_t utf8_encode(unsigned cp, char *out)
{
    if (cp < 0x80u) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800u) {
        out[0] = (char)(0xC0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 0x3Fu));
        return 2;
    }
    if (cp < 0x10000u) {
        out[0] = (char)(0xE0u | (cp >> 12));
        out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (cp & 0x3Fu));
        return 3;
    }
    out[0] = (char)(0xF0u | (cp >> 18));
    out[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
    out[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[3] = (char)(0x80u | (cp & 0x3Fu));
    return 4;
}

/* ------------------------------------------------------------------------ */
/* parser                                                                    */

typedef struct {
    const char *p;
    const char *end;
    int         depth;
} pctx;

static json_value *parse_value(pctx *c);

static json_value *new_value(json_type t)
{
    json_value *v = calloc(1, sizeof *v);
    if (v) v->type = t;
    return v;
}

static void skip_ws(pctx *c)
{
    while (c->p < c->end &&
           (*c->p == ' ' || *c->p == '\t' || *c->p == '\n' || *c->p == '\r'))
        c->p++;
}

static int hex_nibble(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

/* Reads four hex digits, advancing past them. */
static int read_hex4(pctx *c, unsigned *out)
{
    if (c->end - c->p < 4) return -1;
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        int n = hex_nibble(c->p[i]);
        if (n < 0) return -1;
        v = (v << 4) | (unsigned)n;
    }
    c->p += 4;
    *out = v;
    return 0;
}

static int parse_string_raw(pctx *c, char **out, size_t *outlen)
{
    if (c->p >= c->end || *c->p != '"') return -1;
    c->p++;

    char  *buf = NULL;
    size_t cap = 0, len = 0;
    if (sb_append(&buf, &cap, &len, "", 0) != 0) return -1;

    for (;;) {
        if (c->p >= c->end) goto fail;
        unsigned char ch = (unsigned char)*c->p;
        if (ch == '"') { c->p++; break; }
        /* RFC 8259: raw control characters are not allowed inside strings. */
        if (ch < 0x20) goto fail;
        if (ch != '\\') {
            if (sb_append(&buf, &cap, &len, c->p, 1) != 0) goto fail;
            c->p++;
            continue;
        }
        c->p++;
        if (c->p >= c->end) goto fail;
        char esc = *c->p++;
        char tmp[4];
        size_t n = 1;
        switch (esc) {
        case '"':  tmp[0] = '"';  break;
        case '\\': tmp[0] = '\\'; break;
        case '/':  tmp[0] = '/';  break;
        case 'b':  tmp[0] = '\b'; break;
        case 'f':  tmp[0] = '\f'; break;
        case 'n':  tmp[0] = '\n'; break;
        case 'r':  tmp[0] = '\r'; break;
        case 't':  tmp[0] = '\t'; break;
        case 'u': {
            unsigned cp;
            if (read_hex4(c, &cp) != 0) goto fail;
            if (cp >= 0xD800u && cp <= 0xDBFFu) {
                /* High surrogate: pair it with a following low surrogate. */
                const char *save = c->p;
                unsigned lo;
                if (c->end - c->p >= 6 && c->p[0] == '\\' && c->p[1] == 'u') {
                    c->p += 2;
                    if (read_hex4(c, &lo) == 0 && lo >= 0xDC00u && lo <= 0xDFFFu)
                        cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                    else { c->p = save; cp = 0xFFFDu; }
                } else {
                    cp = 0xFFFDu;
                }
            } else if (cp >= 0xDC00u && cp <= 0xDFFFu) {
                cp = 0xFFFDu;   /* lone low surrogate */
            }
            n = utf8_encode(cp, tmp);
            break;
        }
        default: goto fail;
        }
        if (sb_append(&buf, &cap, &len, tmp, n) != 0) goto fail;
    }

    *out = buf;
    if (outlen) *outlen = len;
    return 0;

fail:
    free(buf);
    return -1;
}

static json_value *parse_string(pctx *c)
{
    char  *s;
    size_t len;
    if (parse_string_raw(c, &s, &len) != 0) return NULL;
    json_value *v = new_value(JSON_STRING);
    if (!v) { free(s); return NULL; }
    v->u.str.ptr = s;
    v->u.str.len = len;
    return v;
}

static json_value *parse_number(pctx *c)
{
    const char *s = c->p;
    if (c->p < c->end && *c->p == '-') c->p++;
    if (c->p >= c->end) return NULL;
    if (*c->p == '0') {
        c->p++;
    } else if (*c->p >= '1' && *c->p <= '9') {
        while (c->p < c->end && *c->p >= '0' && *c->p <= '9') c->p++;
    } else {
        return NULL;
    }
    if (c->p < c->end && *c->p == '.') {
        c->p++;
        if (c->p >= c->end || *c->p < '0' || *c->p > '9') return NULL;
        while (c->p < c->end && *c->p >= '0' && *c->p <= '9') c->p++;
    }
    if (c->p < c->end && (*c->p == 'e' || *c->p == 'E')) {
        c->p++;
        if (c->p < c->end && (*c->p == '+' || *c->p == '-')) c->p++;
        if (c->p >= c->end || *c->p < '0' || *c->p > '9') return NULL;
        while (c->p < c->end && *c->p >= '0' && *c->p <= '9') c->p++;
    }

    size_t n = (size_t)(c->p - s);
    char   small[64];
    char  *txt = small;
    if (n + 1 > sizeof small) {
        txt = malloc(n + 1);
        if (!txt) return NULL;
    }
    memcpy(txt, s, n);
    txt[n] = '\0';

    double d = strtod(txt, NULL);
    if (txt != small) free(txt);

    json_value *v = new_value(JSON_NUMBER);
    if (!v) return NULL;
    v->u.num = d;
    return v;
}

static int arr_push(json_value *a, json_value *item)
{
    if (a->u.arr.count == a->u.arr.cap) {
        size_t nc = a->u.arr.cap ? a->u.arr.cap * 2 : 8;
        json_value **ni = realloc(a->u.arr.items, nc * sizeof *ni);
        if (!ni) return -1;
        a->u.arr.items = ni;
        a->u.arr.cap = nc;
    }
    a->u.arr.items[a->u.arr.count++] = item;
    return 0;
}

static int obj_push(json_value *o, char *key, json_value *val)
{
    if (o->u.obj.count == o->u.obj.cap) {
        size_t nc = o->u.obj.cap ? o->u.obj.cap * 2 : 8;
        char **nk = realloc(o->u.obj.keys, nc * sizeof *nk);
        if (!nk) return -1;
        o->u.obj.keys = nk;
        json_value **nv = realloc(o->u.obj.vals, nc * sizeof *nv);
        if (!nv) return -1;
        o->u.obj.vals = nv;
        o->u.obj.cap = nc;
    }
    o->u.obj.keys[o->u.obj.count] = key;
    o->u.obj.vals[o->u.obj.count] = val;
    o->u.obj.count++;
    return 0;
}

static json_value *parse_array(pctx *c)
{
    json_value *a = new_value(JSON_ARRAY);
    if (!a) return NULL;
    c->p++;                     /* '[' */
    skip_ws(c);
    if (c->p < c->end && *c->p == ']') { c->p++; return a; }
    for (;;) {
        json_value *item = parse_value(c);
        if (!item) goto fail;
        if (arr_push(a, item) != 0) { json_free(item); goto fail; }
        skip_ws(c);
        if (c->p >= c->end) goto fail;
        if (*c->p == ',') { c->p++; skip_ws(c); continue; }
        if (*c->p == ']') { c->p++; return a; }
        goto fail;
    }
fail:
    json_free(a);
    return NULL;
}

static json_value *parse_object(pctx *c)
{
    json_value *o = new_value(JSON_OBJECT);
    if (!o) return NULL;
    c->p++;                     /* '{' */
    skip_ws(c);
    if (c->p < c->end && *c->p == '}') { c->p++; return o; }
    for (;;) {
        char *key;
        if (parse_string_raw(c, &key, NULL) != 0) goto fail;
        skip_ws(c);
        if (c->p >= c->end || *c->p != ':') { free(key); goto fail; }
        c->p++;
        json_value *val = parse_value(c);
        if (!val) { free(key); goto fail; }
        if (obj_push(o, key, val) != 0) { free(key); json_free(val); goto fail; }
        skip_ws(c);
        if (c->p >= c->end) goto fail;
        if (*c->p == ',') { c->p++; skip_ws(c); continue; }
        if (*c->p == '}') { c->p++; return o; }
        goto fail;
    }
fail:
    json_free(o);
    return NULL;
}

static json_value *parse_literal(pctx *c, const char *word, json_type t, int b)
{
    size_t n = strlen(word);
    if ((size_t)(c->end - c->p) < n || memcmp(c->p, word, n) != 0) return NULL;
    c->p += n;
    json_value *v = new_value(t);
    if (v) v->u.b = b;
    return v;
}

static json_value *parse_value(pctx *c)
{
    if (c->depth >= JSON_MAX_DEPTH) return NULL;
    skip_ws(c);
    if (c->p >= c->end) return NULL;

    json_value *v;
    switch (*c->p) {
    case '{': c->depth++; v = parse_object(c); c->depth--; return v;
    case '[': c->depth++; v = parse_array(c);  c->depth--; return v;
    case '"': return parse_string(c);
    case 't': return parse_literal(c, "true",  JSON_BOOL, 1);
    case 'f': return parse_literal(c, "false", JSON_BOOL, 0);
    case 'n': return parse_literal(c, "null",  JSON_NULL, 0);
    default:  return parse_number(c);
    }
}

json_value *json_parse(const char *text, size_t len)
{
    if (!text) return NULL;
    pctx c;
    c.p = text;
    c.end = text + len;
    c.depth = 0;
    json_value *v = parse_value(&c);
    if (!v) return NULL;
    skip_ws(&c);
    if (c.p != c.end) { json_free(v); return NULL; }
    return v;
}

void json_free(json_value *v)
{
    if (!v) return;
    switch (v->type) {
    case JSON_STRING:
        free(v->u.str.ptr);
        break;
    case JSON_ARRAY:
        for (size_t i = 0; i < v->u.arr.count; i++) json_free(v->u.arr.items[i]);
        free(v->u.arr.items);
        break;
    case JSON_OBJECT:
        for (size_t i = 0; i < v->u.obj.count; i++) {
            free(v->u.obj.keys[i]);
            json_free(v->u.obj.vals[i]);
        }
        free(v->u.obj.keys);
        free(v->u.obj.vals);
        break;
    default:
        break;
    }
    free(v);
}

/* ------------------------------------------------------------------------ */
/* accessors                                                                 */

json_type json_typeof(const json_value *v) { return v ? v->type : JSON_NULL; }
int json_is(const json_value *v, json_type t) { return v != NULL && v->type == t; }

const char *json_str(const json_value *v)
{
    return json_is(v, JSON_STRING) ? v->u.str.ptr : NULL;
}

int json_num(const json_value *v, double *out)
{
    if (!json_is(v, JSON_NUMBER)) return 0;
    if (out) *out = v->u.num;
    return 1;
}

int json_bool(const json_value *v, int *out)
{
    if (!json_is(v, JSON_BOOL)) return 0;
    if (out) *out = v->u.b;
    return 1;
}

size_t json_arr_len(const json_value *v)
{
    return json_is(v, JSON_ARRAY) ? v->u.arr.count : 0;
}

const json_value *json_arr_get(const json_value *v, size_t i)
{
    if (!json_is(v, JSON_ARRAY) || i >= v->u.arr.count) return NULL;
    return v->u.arr.items[i];
}

size_t json_obj_len(const json_value *v)
{
    return json_is(v, JSON_OBJECT) ? v->u.obj.count : 0;
}

const char *json_obj_key(const json_value *v, size_t i)
{
    if (!json_is(v, JSON_OBJECT) || i >= v->u.obj.count) return NULL;
    return v->u.obj.keys[i];
}

const json_value *json_obj_val(const json_value *v, size_t i)
{
    if (!json_is(v, JSON_OBJECT) || i >= v->u.obj.count) return NULL;
    return v->u.obj.vals[i];
}

const json_value *json_obj_get(const json_value *v, const char *key)
{
    if (!json_is(v, JSON_OBJECT) || !key) return NULL;
    for (size_t i = 0; i < v->u.obj.count; i++)
        if (strcmp(v->u.obj.keys[i], key) == 0) return v->u.obj.vals[i];
    return NULL;
}

/* ------------------------------------------------------------------------ */
/* writer                                                                    */

void jw_init(json_writer *w)
{
    memset(w, 0, sizeof *w);
}

void jw_free(json_writer *w)
{
    free(w->data);
    w->data = NULL;
    w->len = w->cap = 0;
}

const char *jw_text(const json_writer *w)
{
    if (w->err || !w->data) return "";
    return w->data;
}

size_t jw_len(const json_writer *w)
{
    return w->err ? 0 : w->len;
}

static void jw_put(json_writer *w, const char *s, size_t n)
{
    if (w->err) return;
    if (sb_append(&w->data, &w->cap, &w->len, s, n) != 0) w->err = 1;
}

static void jw_sep(json_writer *w)
{
    if (w->depth > 0) {
        if (w->comma[w->depth - 1]) jw_put(w, ",", 1);
        w->comma[w->depth - 1] = 1;
    }
}

/* A value directly after a key must not be preceded by a separator. */
static void jw_pre_value(json_writer *w)
{
    if (w->pending) { w->pending = 0; return; }
    jw_sep(w);
}

static void jw_push(json_writer *w)
{
    if (w->depth >= JW_MAX_DEPTH) { w->err = 1; return; }
    w->comma[w->depth++] = 0;
}

static void jw_pop(json_writer *w)
{
    if (w->depth > 0) w->depth--;
}

void jw_obj_open(json_writer *w)  { jw_pre_value(w); jw_put(w, "{", 1); jw_push(w); }
void jw_obj_close(json_writer *w) { jw_pop(w); jw_put(w, "}", 1); }
void jw_arr_open(json_writer *w)  { jw_pre_value(w); jw_put(w, "[", 1); jw_push(w); }
void jw_arr_close(json_writer *w) { jw_pop(w); jw_put(w, "]", 1); }

/* Emits a quoted, escaped string. Input is assumed UTF-8; malformed byte
   sequences are replaced with U+FFFD so the output is always valid JSON. */
static void jw_quoted(json_writer *w, const char *s)
{
    static const char hex[] = "0123456789abcdef";
    if (!s) s = "";
    jw_put(w, "\"", 1);
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        unsigned char ch = *p;
        if (ch < 0x80u) {
            switch (ch) {
            case '"':  jw_put(w, "\\\"", 2); break;
            case '\\': jw_put(w, "\\\\", 2); break;
            case '\b': jw_put(w, "\\b", 2);  break;
            case '\f': jw_put(w, "\\f", 2);  break;
            case '\n': jw_put(w, "\\n", 2);  break;
            case '\r': jw_put(w, "\\r", 2);  break;
            case '\t': jw_put(w, "\\t", 2);  break;
            default:
                if (ch < 0x20u) {
                    char esc[6] = { '\\', 'u', '0', '0', hex[ch >> 4], hex[ch & 0xF] };
                    jw_put(w, esc, 6);
                } else {
                    jw_put(w, (const char *)p, 1);
                }
            }
            p++;
            continue;
        }
        /* Multi-byte sequence: validate before passing it through. */
        size_t need = 0;
        unsigned cp = 0;
        if ((ch & 0xE0u) == 0xC0u) { need = 1; cp = ch & 0x1Fu; }
        else if ((ch & 0xF0u) == 0xE0u) { need = 2; cp = ch & 0x0Fu; }
        else if ((ch & 0xF8u) == 0xF0u) { need = 3; cp = ch & 0x07u; }
        else { jw_put(w, "\xEF\xBF\xBD", 3); p++; continue; }

        size_t i;
        for (i = 1; i <= need; i++) {
            if ((p[i] & 0xC0u) != 0x80u) break;
            cp = (cp << 6) | (p[i] & 0x3Fu);
        }
        if (i <= need) { jw_put(w, "\xEF\xBF\xBD", 3); p++; continue; }
        if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) {
            jw_put(w, "\xEF\xBF\xBD", 3);
        } else {
            jw_put(w, (const char *)p, need + 1);
        }
        p += need + 1;
    }
    jw_put(w, "\"", 1);
}

void jw_key(json_writer *w, const char *key)
{
    jw_sep(w);
    jw_quoted(w, key);
    jw_put(w, ":", 1);
    w->pending = 1;
}

void jw_str(json_writer *w, const char *s)
{
    jw_pre_value(w);
    jw_quoted(w, s);
}

void jw_num(json_writer *w, double d)
{
    jw_pre_value(w);
    char buf[40];
    if (!isfinite(d)) {
        jw_put(w, "null", 4);   /* JSON has no NaN/Infinity */
        return;
    }
    if (d == floor(d) && fabs(d) < 1e15) {
        int n = snprintf(buf, sizeof buf, "%lld", (long long)d);
        if (n > 0) jw_put(w, buf, (size_t)n);
        return;
    }
    /* Shortest representation that round-trips exactly. */
    for (int prec = 15; prec <= 17; prec++) {
        int n = snprintf(buf, sizeof buf, "%.*g", prec, d);
        if (n <= 0 || (size_t)n >= sizeof buf) continue;
        if (prec == 17 || strtod(buf, NULL) == d) { jw_put(w, buf, (size_t)n); return; }
    }
    jw_put(w, "0", 1);
}

void jw_bool(json_writer *w, int b)
{
    jw_pre_value(w);
    if (b) jw_put(w, "true", 4);
    else   jw_put(w, "false", 5);
}

void jw_null(json_writer *w)
{
    jw_pre_value(w);
    jw_put(w, "null", 4);
}

void jw_val(json_writer *w, const json_value *v)
{
    switch (json_typeof(v)) {
    case JSON_NULL:   jw_null(w); break;
    case JSON_BOOL:   jw_bool(w, v->u.b); break;
    case JSON_NUMBER: jw_num(w, v->u.num); break;
    case JSON_STRING: jw_str(w, v->u.str.ptr); break;
    case JSON_ARRAY:
        jw_arr_open(w);
        for (size_t i = 0; i < v->u.arr.count; i++) jw_val(w, v->u.arr.items[i]);
        jw_arr_close(w);
        break;
    case JSON_OBJECT:
        jw_obj_open(w);
        for (size_t i = 0; i < v->u.obj.count; i++) {
            jw_key(w, v->u.obj.keys[i]);
            jw_val(w, v->u.obj.vals[i]);
        }
        jw_obj_close(w);
        break;
    }
}

#define _POSIX_C_SOURCE 200809L
#include "mxjson.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ alloc */
static void *xmalloc(size_t n) {
    void *p = calloc(1, n ? n : 1);
    if (!p) abort();
    return p;
}

static char *xstrndup(const char *s, size_t n) {
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static json *mk(jtype t) { json *v = xmalloc(sizeof(json)); v->type = t; return v; }

json *j_null(void) { return mk(J_NULL); }
json *j_bool(bool b) { json *v = mk(J_BOOL); v->b = b; return v; }
json *j_int(int64_t i) { json *v = mk(J_INT); v->i = i; return v; }
json *j_double(double d) { json *v = mk(J_DOUBLE); v->d = d; return v; }
json *j_strn(const char *s, size_t n) {
    json *v = mk(J_STRING); v->s = xstrndup(s, n); v->slen = n; return v;
}
json *j_str(const char *s) { return j_strn(s ? s : "", s ? strlen(s) : 0); }
json *j_arr(void) { return mk(J_ARRAY); }
json *j_obj(void) { return mk(J_OBJECT); }

void j_free(json *v) {
    if (!v) return;
    if (v->type == J_STRING) free(v->s);
    for (size_t i = 0; i < v->n; i++) {
        if (v->keys) free(v->keys[i]);
        j_free(v->items[i]);
    }
    free(v->items);
    free(v->keys);
    free(v);
}

static void grow(json *v) {
    if (v->n < v->cap) return;
    size_t cap = v->cap ? v->cap * 2 : 8;
    v->items = realloc(v->items, cap * sizeof(json *));
    if (!v->items) abort();
    if (v->type == J_OBJECT) {
        v->keys = realloc(v->keys, cap * sizeof(char *));
        if (!v->keys) abort();
    }
    v->cap = cap;
}

void j_push(json *a, json *v) {
    if (!a || a->type != J_ARRAY) { j_free(v); return; }
    grow(a);
    a->items[a->n++] = v;
}

void j_set(json *o, const char *key, json *v) {
    if (!o || o->type != J_OBJECT) { j_free(v); return; }
    for (size_t i = 0; i < o->n; i++) {
        if (strcmp(o->keys[i], key) == 0) {
            j_free(o->items[i]);
            o->items[i] = v;
            return;
        }
    }
    grow(o);
    o->keys[o->n] = xstrndup(key, strlen(key));
    o->items[o->n] = v;
    o->n++;
}

void j_del(json *o, const char *key) {
    if (!o || o->type != J_OBJECT) return;
    for (size_t i = 0; i < o->n; i++) {
        if (strcmp(o->keys[i], key) == 0) {
            free(o->keys[i]);
            j_free(o->items[i]);
            memmove(&o->keys[i], &o->keys[i + 1], (o->n - i - 1) * sizeof(char *));
            memmove(&o->items[i], &o->items[i + 1], (o->n - i - 1) * sizeof(json *));
            o->n--;
            return;
        }
    }
}

/* Drops one element of an array, keeping the order of the rest. */
void j_remove_at(json *a, size_t i) {
    if (!a || a->type != J_ARRAY || i >= a->n) return;
    j_free(a->items[i]);
    memmove(&a->items[i], &a->items[i + 1], (a->n - i - 1) * sizeof(json *));
    a->n--;
}

json *j_get(const json *o, const char *key) {
    if (!o || o->type != J_OBJECT || !key) return NULL;
    for (size_t i = 0; i < o->n; i++)
        if (strcmp(o->keys[i], key) == 0) return o->items[i];
    return NULL;
}

json *j_at(const json *a, size_t i) {
    if (!a || (a->type != J_ARRAY && a->type != J_OBJECT) || i >= a->n) return NULL;
    return a->items[i];
}

size_t j_len(const json *v) { return v ? v->n : 0; }
const char *j_key(const json *o, size_t i) {
    return (o && o->type == J_OBJECT && i < o->n) ? o->keys[i] : NULL;
}

const char *j_gets(const json *o, const char *k, const char *dflt) {
    json *v = j_get(o, k);
    return (v && v->type == J_STRING) ? v->s : dflt;
}
int64_t j_geti(const json *o, const char *k, int64_t dflt) {
    json *v = j_get(o, k);
    if (!v) return dflt;
    if (v->type == J_INT) return v->i;
    if (v->type == J_DOUBLE) return (int64_t)v->d;
    return dflt;
}
bool j_getb(const json *o, const char *k, bool dflt) {
    json *v = j_get(o, k);
    return (v && v->type == J_BOOL) ? v->b : dflt;
}
json *j_geto(const json *o, const char *k) {
    json *v = j_get(o, k);
    return (v && v->type == J_OBJECT) ? v : NULL;
}
json *j_geta(const json *o, const char *k) {
    json *v = j_get(o, k);
    return (v && v->type == J_ARRAY) ? v : NULL;
}
bool j_is_str(const json *v) { return v && v->type == J_STRING; }

json *j_clone(const json *v) {
    if (!v) return NULL;
    switch (v->type) {
    case J_NULL: return j_null();
    case J_BOOL: return j_bool(v->b);
    case J_INT: return j_int(v->i);
    case J_DOUBLE: return j_double(v->d);
    case J_STRING: return j_strn(v->s, v->slen);
    case J_ARRAY: {
        json *a = j_arr();
        for (size_t i = 0; i < v->n; i++) j_push(a, j_clone(v->items[i]));
        return a;
    }
    case J_OBJECT: {
        json *o = j_obj();
        for (size_t i = 0; i < v->n; i++) j_set(o, v->keys[i], j_clone(v->items[i]));
        return o;
    }
    }
    return j_null();
}

json *j_deep_get(const json *v, const char *path) {
    char buf[512];
    snprintf(buf, sizeof buf, "%s", path);
    char *save = NULL, *tok = strtok_r(buf, ".", &save);
    const json *cur = v;
    while (tok && cur) { cur = j_get(cur, tok); tok = strtok_r(NULL, ".", &save); }
    return (json *)cur;
}

bool j_has_unsafe_int(const json *v) {
    if (!v) return false;
    if (v->type == J_INT && (v->i > 9007199254740991LL || v->i < -9007199254740991LL))
        return true;
    for (size_t i = 0; i < v->n; i++)
        if (j_has_unsafe_int(v->items[i])) return true;
    return false;
}

bool j_has_float(const json *v) {
    if (!v) return false;
    if (v->type == J_DOUBLE) return true;
    for (size_t i = 0; i < v->n; i++)
        if (j_has_float(v->items[i])) return true;
    return false;
}

/* ----------------------------------------------------------------- parser */
typedef struct { const char *p, *end; const char *err; int depth; } P;

static void skipws(P *s) {
    while (s->p < s->end && (*s->p == ' ' || *s->p == '\t' || *s->p == '\n' ||
                             *s->p == '\r')) s->p++;
}

static json *pval(P *s);

static int hex4(const char *p) {
    int v = 0;
    for (int i = 0; i < 4; i++) {
        int c = (unsigned char)p[i], d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return -1;
        v = v * 16 + d;
    }
    return v;
}

static size_t utf8_put(char *out, unsigned cp) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static char *pstring(P *s, size_t *outlen) {
    if (s->p >= s->end || *s->p != '"') { s->err = "expected string"; return NULL; }
    s->p++;
    size_t cap = 32, n = 0;
    char *buf = xmalloc(cap);
    while (s->p < s->end && *s->p != '"') {
        if (n + 8 >= cap) { cap *= 2; buf = realloc(buf, cap); if (!buf) abort(); }
        unsigned char c = (unsigned char)*s->p;
        if (c == '\\') {
            s->p++;
            if (s->p >= s->end) { s->err = "truncated escape"; free(buf); return NULL; }
            char e = *s->p++;
            switch (e) {
            case '"': buf[n++] = '"'; break;
            case '\\': buf[n++] = '\\'; break;
            case '/': buf[n++] = '/'; break;
            case 'b': buf[n++] = '\b'; break;
            case 'f': buf[n++] = '\f'; break;
            case 'n': buf[n++] = '\n'; break;
            case 'r': buf[n++] = '\r'; break;
            case 't': buf[n++] = '\t'; break;
            case 'u': {
                if (s->end - s->p < 4) { s->err = "bad \\u"; free(buf); return NULL; }
                int cp = hex4(s->p);
                if (cp < 0) { s->err = "bad \\u"; free(buf); return NULL; }
                s->p += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF && s->end - s->p >= 6 &&
                    s->p[0] == '\\' && s->p[1] == 'u') {
                    int lo = hex4(s->p + 2);
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        s->p += 6;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                }
                n += utf8_put(buf + n, (unsigned)cp);
                break;
            }
            default: s->err = "bad escape"; free(buf); return NULL;
            }
        } else if (c < 0x20) {
            s->err = "control character in string"; free(buf); return NULL;
        } else {
            buf[n++] = (char)c;
            s->p++;
        }
    }
    if (s->p >= s->end) { s->err = "unterminated string"; free(buf); return NULL; }
    s->p++;
    buf[n] = 0;
    if (outlen) *outlen = n;
    return buf;
}

static json *pval(P *s) {
    if (++s->depth > 96) { s->err = "too deep"; return NULL; }
    skipws(s);
    if (s->p >= s->end) { s->err = "unexpected end"; s->depth--; return NULL; }
    json *out = NULL;
    char c = *s->p;
    if (c == '{') {
        s->p++;
        out = j_obj();
        skipws(s);
        if (s->p < s->end && *s->p == '}') { s->p++; s->depth--; return out; }
        for (;;) {
            skipws(s);
            size_t klen = 0;
            char *k = pstring(s, &klen);
            if (!k) { j_free(out); s->depth--; return NULL; }
            skipws(s);
            if (s->p >= s->end || *s->p != ':') {
                s->err = "expected :"; free(k); j_free(out); s->depth--; return NULL;
            }
            s->p++;
            json *v = pval(s);
            if (!v) { free(k); j_free(out); s->depth--; return NULL; }
            j_set(out, k, v);
            free(k);
            skipws(s);
            if (s->p < s->end && *s->p == ',') { s->p++; continue; }
            if (s->p < s->end && *s->p == '}') { s->p++; break; }
            s->err = "expected , or }"; j_free(out); s->depth--; return NULL;
        }
    } else if (c == '[') {
        s->p++;
        out = j_arr();
        skipws(s);
        if (s->p < s->end && *s->p == ']') { s->p++; s->depth--; return out; }
        for (;;) {
            json *v = pval(s);
            if (!v) { j_free(out); s->depth--; return NULL; }
            j_push(out, v);
            skipws(s);
            if (s->p < s->end && *s->p == ',') { s->p++; continue; }
            if (s->p < s->end && *s->p == ']') { s->p++; break; }
            s->err = "expected , or ]"; j_free(out); s->depth--; return NULL;
        }
    } else if (c == '"') {
        size_t n = 0;
        char *str = pstring(s, &n);
        if (!str) { s->depth--; return NULL; }
        out = j_strn(str, n);
        free(str);
    } else if (!strncmp(s->p, "true", (size_t)(s->end - s->p) < 4 ? (size_t)(s->end - s->p) : 4)
               && s->end - s->p >= 4) {
        s->p += 4; out = j_bool(true);
    } else if (s->end - s->p >= 5 && !strncmp(s->p, "false", 5)) {
        s->p += 5; out = j_bool(false);
    } else if (s->end - s->p >= 4 && !strncmp(s->p, "null", 4)) {
        s->p += 4; out = j_null();
    } else if (c == '-' || (c >= '0' && c <= '9')) {
        const char *start = s->p;
        bool isfloat = false;
        if (*s->p == '-') s->p++;
        while (s->p < s->end && *s->p >= '0' && *s->p <= '9') s->p++;
        if (s->p < s->end && *s->p == '.') { isfloat = true; s->p++;
            while (s->p < s->end && *s->p >= '0' && *s->p <= '9') s->p++; }
        if (s->p < s->end && (*s->p == 'e' || *s->p == 'E')) {
            isfloat = true; s->p++;
            if (s->p < s->end && (*s->p == '+' || *s->p == '-')) s->p++;
            while (s->p < s->end && *s->p >= '0' && *s->p <= '9') s->p++;
        }
        char tmp[64];
        size_t len = (size_t)(s->p - start);
        if (len >= sizeof tmp) { s->err = "number too long"; s->depth--; return NULL; }
        memcpy(tmp, start, len);
        tmp[len] = 0;
        if (isfloat) out = j_double(strtod(tmp, NULL));
        else {

            char *endp = NULL;
            long long ll = strtoll(tmp, &endp, 10);
            if (endp && *endp) { s->err = "bad number"; s->depth--; return NULL; }
            out = j_int((int64_t)ll);
        }
    } else {
        s->err = "unexpected token";
    }
    s->depth--;
    return out;
}

json *j_parse(const char *text, size_t len, const char **err) {
    P s = {text, text + len, NULL, 0};
    json *v = pval(&s);
    if (v) {
        skipws(&s);
        if (s.p != s.end) { j_free(v); v = NULL; s.err = "trailing data"; }
    }
    if (err) *err = s.err ? s.err : (v ? NULL : "parse error");
    return v;
}

/* -------------------------------------------------------------- serialise */
typedef struct { char *b; size_t n, cap; } SB;

static void sb_need(SB *s, size_t k) {
    if (s->n + k + 1 <= s->cap) return;
    size_t cap = s->cap ? s->cap : 128;
    while (cap < s->n + k + 1) cap *= 2;
    s->b = realloc(s->b, cap);
    if (!s->b) abort();
    s->cap = cap;
}
static void sb_putc(SB *s, char c) { sb_need(s, 1); s->b[s->n++] = c; }
static void sb_put(SB *s, const char *p, size_t n) {
    sb_need(s, n); memcpy(s->b + s->n, p, n); s->n += n;
}
static void sb_puts(SB *s, const char *p) { sb_put(s, p, strlen(p)); }

/* Matrix canonical JSON escapes only what RFC 8259 requires: `"`, `\` and the
 * C0 controls, with \b \f \n \r \t preferred and \u00XX otherwise. */
static void sb_str(SB *s, const char *p, size_t n) {
    sb_putc(s, '"');
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)p[i];
        switch (c) {
        case '"': sb_puts(s, "\\\""); break;
        case '\\': sb_puts(s, "\\\\"); break;
        case '\b': sb_puts(s, "\\b"); break;
        case '\f': sb_puts(s, "\\f"); break;
        case '\n': sb_puts(s, "\\n"); break;
        case '\r': sb_puts(s, "\\r"); break;
        case '\t': sb_puts(s, "\\t"); break;
        default:
            if (c < 0x20) {
                char tmp[8];
                snprintf(tmp, sizeof tmp, "\\u%04x", c);
                sb_puts(s, tmp);
            } else {
                sb_putc(s, (char)c);
            }
        }
    }
    sb_putc(s, '"');
}

static int keycmp(const void *a, const void *b) {
    /* byte-wise comparison of UTF-8 is the same order as comparing unicode
     * codepoints, which is what canonical JSON requires. */
    const char *const *x = a, *const *y = b;
    return strcmp(*x, *y);
}

static bool emit(SB *s, const json *v, bool canonical) {
    if (!v) { sb_puts(s, "null"); return true; }
    char tmp[64];
    switch (v->type) {
    case J_NULL: sb_puts(s, "null"); return true;
    case J_BOOL: sb_puts(s, v->b ? "true" : "false"); return true;
    case J_INT:
        snprintf(tmp, sizeof tmp, "%lld", (long long)v->i);
        sb_puts(s, tmp);
        return true;
    case J_DOUBLE:
        /* A float is outside canonical JSON, but room versions before 6 tell
         * servers not to enforce that, so it is serialised with the shortest
         * representation that round-trips - the same rule Python's repr uses,
         * which is what the reference implementation hashes. */
        if (isnan(v->d) || isinf(v->d)) return false;
        for (int prec = 15; prec <= 17; prec++) {
            snprintf(tmp, sizeof tmp, "%.*g", prec, v->d);
            if (strtod(tmp, NULL) == v->d) break;
        }
        sb_puts(s, tmp);
        return true;
    case J_STRING: sb_str(s, v->s, v->slen); return true;
    case J_ARRAY:
        sb_putc(s, '[');
        for (size_t i = 0; i < v->n; i++) {
            if (i) sb_putc(s, ',');
            if (!emit(s, v->items[i], canonical)) return false;
        }
        sb_putc(s, ']');
        return true;
    case J_OBJECT: {
        sb_putc(s, '{');
        size_t n = v->n;
        const char **order = xmalloc((n ? n : 1) * sizeof(char *));
        for (size_t i = 0; i < n; i++) order[i] = v->keys[i];
        if (canonical) qsort(order, n, sizeof(char *), keycmp);
        for (size_t i = 0; i < n; i++) {
            if (i) sb_putc(s, ',');
            sb_str(s, order[i], strlen(order[i]));
            sb_putc(s, ':');
            if (!emit(s, j_get(v, order[i]), canonical)) { free(order); return false; }
        }
        free(order);
        sb_putc(s, '}');
        return true;
    }
    }
    return false;
}

static char *dump(const json *v, size_t *outlen, bool canonical) {
    SB s = {0};
    if (!emit(&s, v, canonical)) { free(s.b); return NULL; }
    sb_need(&s, 1);
    s.b[s.n] = 0;
    if (outlen) *outlen = s.n;
    return s.b;
}

char *j_dump(const json *v, size_t *n) { return dump(v, n, false); }
char *j_canonical(const json *v, size_t *n) { return dump(v, n, true); }

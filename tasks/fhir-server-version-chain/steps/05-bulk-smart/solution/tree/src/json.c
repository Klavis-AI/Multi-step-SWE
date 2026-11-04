#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static jval *jnew(arena *a, jkind k) { jval *v = arena_calloc(a, sizeof(jval)); v->kind = k; return v; }

jval *j_null(arena *a) { return jnew(a, J_NULL); }
jval *j_bool(arena *a, int b) { jval *v = jnew(a, J_BOOL); v->u.b = b != 0; return v; }
jval *j_num(arena *a, const char *lex) { jval *v = jnew(a, J_NUM); v->u.num = arena_strdup(a, lex); return v; }
jval *j_int(arena *a, long long n) { return j_num(a, arena_printf(a, "%lld", n)); }
jval *j_str(arena *a, const char *s) { jval *v = jnew(a, J_STR); v->u.str = arena_strdup(a, s ? s : ""); return v; }
jval *j_arr(arena *a) { return jnew(a, J_ARR); }
jval *j_obj(arena *a) { return jnew(a, J_OBJ); }

void j_push(arena *a, jval *arr, jval *v) {
    if (arr->u.arr.n == arr->u.arr.cap) {
        size_t cap = arr->u.arr.cap ? arr->u.arr.cap * 2 : 4;
        jval **ni = arena_alloc(a, cap * sizeof(jval *));
        if (arr->u.arr.items) memcpy(ni, arr->u.arr.items, arr->u.arr.n * sizeof(jval *));
        arr->u.arr.items = ni; arr->u.arr.cap = cap;
    }
    arr->u.arr.items[arr->u.arr.n++] = v;
}

void j_set(arena *a, jval *obj, const char *key, jval *v) {
    for (jmem *m = obj->u.obj.first; m; m = m->next)
        if (strcmp(m->key, key) == 0) { m->val = v; return; }
    jmem *m = arena_calloc(a, sizeof(jmem));
    m->key = arena_strdup(a, key); m->val = v; m->next = NULL;
    if (obj->u.obj.last) obj->u.obj.last->next = m; else obj->u.obj.first = m;
    obj->u.obj.last = m;
    obj->u.obj.n++;
}

void j_del(jval *obj, const char *key) {
    if (!obj || obj->kind != J_OBJ) return;
    jmem *prev = NULL;
    for (jmem *m = obj->u.obj.first; m; prev = m, m = m->next) {
        if (strcmp(m->key, key) == 0) {
            if (prev) prev->next = m->next; else obj->u.obj.first = m->next;
            if (obj->u.obj.last == m) obj->u.obj.last = prev;
            obj->u.obj.n--;
            return;
        }
    }
}

jval *j_get(const jval *obj, const char *key) {
    if (!obj || obj->kind != J_OBJ || !key) return NULL;
    for (jmem *m = obj->u.obj.first; m; m = m->next)
        if (strcmp(m->key, key) == 0) return m->val;
    return NULL;
}

const char *j_gets(const jval *obj, const char *key) {
    jval *v = j_get(obj, key);
    return (v && v->kind == J_STR) ? v->u.str : NULL;
}

const char *j_num_of(const jval *v) { return (v && v->kind == J_NUM) ? v->u.num : NULL; }
const char *j_str_of(const jval *v) { return (v && v->kind == J_STR) ? v->u.str : NULL; }
int j_is(const jval *v, jkind k) { return v && v->kind == k; }
int j_len(const jval *v) { if (!v) return 0; if (v->kind == J_ARR) return (int)v->u.arr.n; if (v->kind == J_OBJ) return (int)v->u.obj.n; return 0; }
jval *j_at(const jval *arr, int i) { if (!arr || arr->kind != J_ARR || i < 0 || (size_t)i >= arr->u.arr.n) return NULL; return arr->u.arr.items[i]; }

/* ---------------- parser ---------------- */

typedef struct {
    const char *s, *e;
    arena      *a;
    char       *err;
    size_t      errlen;
    int         depth;
} jp;

static jval *p_value(jp *p);

static void p_fail(jp *p, const char *msg) {
    if (p->err && p->errlen && !p->err[0])
        snprintf(p->err, p->errlen, "%s at offset %ld", msg, (long)(p->s - (p->e - (p->e - p->s)) ));
}

static void p_err(jp *p, const char *msg, const char *at) {
    if (p->err && p->errlen && !p->err[0])
        snprintf(p->err, p->errlen, "%s", msg);
    (void)at;
}

static void p_ws(jp *p) {
    while (p->s < p->e && (*p->s == ' ' || *p->s == '\t' || *p->s == '\n' || *p->s == '\r')) p->s++;
}

static int p_lit(jp *p, const char *lit) {
    size_t n = strlen(lit);
    if ((size_t)(p->e - p->s) >= n && memcmp(p->s, lit, n) == 0) { p->s += n; return 1; }
    return 0;
}

static void utf8_put(buf *b, unsigned cp) {
    if (cp < 0x80) buf_putc(b, (char)cp);
    else if (cp < 0x800) { buf_putc(b, (char)(0xC0 | (cp >> 6))); buf_putc(b, (char)(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) { buf_putc(b, (char)(0xE0 | (cp >> 12))); buf_putc(b, (char)(0x80 | ((cp >> 6) & 0x3F))); buf_putc(b, (char)(0x80 | (cp & 0x3F))); }
    else { buf_putc(b, (char)(0xF0 | (cp >> 18))); buf_putc(b, (char)(0x80 | ((cp >> 12) & 0x3F))); buf_putc(b, (char)(0x80 | ((cp >> 6) & 0x3F))); buf_putc(b, (char)(0x80 | (cp & 0x3F))); }
}

static const char *p_string(jp *p) {
    if (p->s >= p->e || *p->s != '"') { p_err(p, "expected string", p->s); return NULL; }
    p->s++;
    buf b; buf_init(&b);
    while (p->s < p->e && *p->s != '"') {
        unsigned char c = (unsigned char)*p->s;
        if (c == '\\') {
            p->s++;
            if (p->s >= p->e) { buf_free(&b); p_err(p, "truncated escape", p->s); return NULL; }
            char e = *p->s++;
            switch (e) {
                case '"': buf_putc(&b, '"'); break;
                case '\\': buf_putc(&b, '\\'); break;
                case '/': buf_putc(&b, '/'); break;
                case 'b': buf_putc(&b, '\b'); break;
                case 'f': buf_putc(&b, '\f'); break;
                case 'n': buf_putc(&b, '\n'); break;
                case 'r': buf_putc(&b, '\r'); break;
                case 't': buf_putc(&b, '\t'); break;
                case 'u': {
                    if (p->e - p->s < 4) { buf_free(&b); p_err(p, "truncated \\u escape", p->s); return NULL; }
                    unsigned cp = 0;
                    for (int i = 0; i < 4; i++) { int h = hex_val((unsigned char)p->s[i]); if (h < 0) { buf_free(&b); p_err(p, "bad \\u escape", p->s); return NULL; } cp = cp * 16 + (unsigned)h; }
                    p->s += 4;
                    if (cp >= 0xD800 && cp <= 0xDBFF && p->e - p->s >= 6 && p->s[0] == '\\' && p->s[1] == 'u') {
                        unsigned lo = 0; int ok = 1;
                        for (int i = 0; i < 4; i++) { int h = hex_val((unsigned char)p->s[2 + i]); if (h < 0) { ok = 0; break; } lo = lo * 16 + (unsigned)h; }
                        if (ok && lo >= 0xDC00 && lo <= 0xDFFF) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); p->s += 6; }
                    }
                    utf8_put(&b, cp);
                    break;
                }
                default: buf_free(&b); p_err(p, "bad escape", p->s); return NULL;
            }
        } else if (c < 0x20) {
            buf_free(&b); p_err(p, "control character in string", p->s); return NULL;
        } else { buf_putc(&b, (char)c); p->s++; }
    }
    if (p->s >= p->e) { buf_free(&b); p_err(p, "unterminated string", p->s); return NULL; }
    p->s++;
    const char *out = arena_strndup(p->a, b.p ? b.p : "", b.n);
    buf_free(&b);
    return out;
}

static jval *p_number(jp *p) {
    const char *st = p->s;
    if (p->s < p->e && *p->s == '-') p->s++;
    if (p->s >= p->e || !isdigit((unsigned char)*p->s)) { p_err(p, "bad number", st); return NULL; }
    if (*p->s == '0') p->s++;
    else while (p->s < p->e && isdigit((unsigned char)*p->s)) p->s++;
    if (p->s < p->e && *p->s == '.') {
        p->s++;
        if (p->s >= p->e || !isdigit((unsigned char)*p->s)) { p_err(p, "bad number", st); return NULL; }
        while (p->s < p->e && isdigit((unsigned char)*p->s)) p->s++;
    }
    if (p->s < p->e && (*p->s == 'e' || *p->s == 'E')) {
        p->s++;
        if (p->s < p->e && (*p->s == '+' || *p->s == '-')) p->s++;
        if (p->s >= p->e || !isdigit((unsigned char)*p->s)) { p_err(p, "bad number", st); return NULL; }
        while (p->s < p->e && isdigit((unsigned char)*p->s)) p->s++;
    }
    jval *v = jnew(p->a, J_NUM);
    v->u.num = arena_strndup(p->a, st, (size_t)(p->s - st));
    return v;
}

static jval *p_value(jp *p) {
    if (++p->depth > 200) { p_err(p, "nesting too deep", p->s); return NULL; }
    p_ws(p);
    if (p->s >= p->e) { p_err(p, "unexpected end of input", p->s); p->depth--; return NULL; }
    jval *r = NULL;
    char c = *p->s;
    if (c == '{') {
        p->s++;
        r = j_obj(p->a);
        p_ws(p);
        if (p->s < p->e && *p->s == '}') { p->s++; p->depth--; return r; }
        for (;;) {
            p_ws(p);
            const char *k = p_string(p);
            if (!k) { p->depth--; return NULL; }
            if (j_get(r, k)) { p_err(p, "duplicate object key", p->s); p->depth--; return NULL; }
            p_ws(p);
            if (p->s >= p->e || *p->s != ':') { p_err(p, "expected ':'", p->s); p->depth--; return NULL; }
            p->s++;
            jval *v = p_value(p);
            if (!v) { p->depth--; return NULL; }
            j_set(p->a, r, k, v);
            p_ws(p);
            if (p->s < p->e && *p->s == ',') { p->s++; continue; }
            if (p->s < p->e && *p->s == '}') { p->s++; break; }
            p_err(p, "expected ',' or '}'", p->s); p->depth--; return NULL;
        }
        p->depth--;
        return r;
    }
    if (c == '[') {
        p->s++;
        r = j_arr(p->a);
        p_ws(p);
        if (p->s < p->e && *p->s == ']') { p->s++; p->depth--; return r; }
        for (;;) {
            jval *v = p_value(p);
            if (!v) { p->depth--; return NULL; }
            j_push(p->a, r, v);
            p_ws(p);
            if (p->s < p->e && *p->s == ',') { p->s++; continue; }
            if (p->s < p->e && *p->s == ']') { p->s++; break; }
            p_err(p, "expected ',' or ']'", p->s); p->depth--; return NULL;
        }
        p->depth--;
        return r;
    }
    if (c == '"') {
        const char *s = p_string(p);
        if (!s) { p->depth--; return NULL; }
        jval *v = jnew(p->a, J_STR); v->u.str = s;
        p->depth--;
        return v;
    }
    if (p_lit(p, "true")) { p->depth--; return j_bool(p->a, 1); }
    if (p_lit(p, "false")) { p->depth--; return j_bool(p->a, 0); }
    if (p_lit(p, "null")) { p->depth--; return j_null(p->a); }
    r = p_number(p);
    p->depth--;
    if (!r) p_err(p, "unexpected token", p->s);
    return r;
}

jval *j_parse(arena *a, const char *text, size_t len, char *err, size_t errlen) {
    /* a UTF-8 byte order mark is legal in a file and carries no JSON meaning */
    if (len >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
        (unsigned char)text[2] == 0xBF) { text += 3; len -= 3; }
    jp p = { text, text + len, a, err, errlen, 0 };
    if (err && errlen) err[0] = 0;
    jval *v = p_value(&p);
    if (!v) { p_fail(&p, "parse error"); return NULL; }
    p_ws(&p);
    if (p.s != p.e) { p_err(&p, "trailing content after JSON value", p.s); return NULL; }
    return v;
}

/* ---------------- writer ---------------- */

static void w_string(buf *b, const char *s) {
    buf_putc(b, '"');
    for (const unsigned char *p = (const unsigned char *)s; p && *p; p++) {
        switch (*p) {
            case '"': buf_puts(b, "\\\""); break;
            case '\\': buf_puts(b, "\\\\"); break;
            case '\b': buf_puts(b, "\\b"); break;
            case '\f': buf_puts(b, "\\f"); break;
            case '\n': buf_puts(b, "\\n"); break;
            case '\r': buf_puts(b, "\\r"); break;
            case '\t': buf_puts(b, "\\t"); break;
            default:
                if (*p < 0x20) buf_printf(b, "\\u%04x", *p);
                else buf_putc(b, (char)*p);
        }
    }
    buf_putc(b, '"');
}

static void w_val(buf *b, const jval *v, int pretty, int ind) {
    if (!v) { buf_puts(b, "null"); return; }
    switch (v->kind) {
        case J_NULL: buf_puts(b, "null"); break;
        case J_BOOL: buf_puts(b, v->u.b ? "true" : "false"); break;
        case J_NUM:  buf_puts(b, v->u.num); break;
        case J_STR:  w_string(b, v->u.str); break;
        case J_ARR: {
            if (v->u.arr.n == 0) { buf_puts(b, "[]"); break; }
            buf_putc(b, '[');
            for (size_t i = 0; i < v->u.arr.n; i++) {
                if (i) buf_putc(b, ',');
                if (pretty) { buf_putc(b, '\n'); for (int k = 0; k <= ind; k++) buf_puts(b, "  "); }
                w_val(b, v->u.arr.items[i], pretty, ind + 1);
            }
            if (pretty) { buf_putc(b, '\n'); for (int k = 0; k < ind; k++) buf_puts(b, "  "); }
            buf_putc(b, ']');
            break;
        }
        case J_OBJ: {
            if (v->u.obj.n == 0) { buf_puts(b, "{}"); break; }
            buf_putc(b, '{');
            int first = 1;
            for (jmem *m = v->u.obj.first; m; m = m->next) {
                if (!first) buf_putc(b, ',');
                first = 0;
                if (pretty) { buf_putc(b, '\n'); for (int k = 0; k <= ind; k++) buf_puts(b, "  "); }
                w_string(b, m->key);
                buf_putc(b, ':');
                if (pretty) buf_putc(b, ' ');
                w_val(b, m->val, pretty, ind + 1);
            }
            if (pretty) { buf_putc(b, '\n'); for (int k = 0; k < ind; k++) buf_puts(b, "  "); }
            buf_putc(b, '}');
            break;
        }
    }
}

void j_write(buf *b, const jval *v, int pretty) { w_val(b, v, pretty, 0); }

jval *j_clone(arena *a, const jval *v) {
    if (!v) return NULL;
    switch (v->kind) {
        case J_NULL: return j_null(a);
        case J_BOOL: return j_bool(a, v->u.b);
        case J_NUM:  return j_num(a, v->u.num);
        case J_STR:  return j_str(a, v->u.str);
        case J_ARR: { jval *r = j_arr(a); for (size_t i = 0; i < v->u.arr.n; i++) j_push(a, r, j_clone(a, v->u.arr.items[i])); return r; }
        case J_OBJ: { jval *r = j_obj(a); for (jmem *m = v->u.obj.first; m; m = m->next) j_set(a, r, m->key, j_clone(a, m->val)); return r; }
    }
    return j_null(a);
}

int j_equal(const jval *x, const jval *y) {
    if (!x || !y) return x == y;
    if (x->kind != y->kind) return 0;
    switch (x->kind) {
        case J_NULL: return 1;
        case J_BOOL: return x->u.b == y->u.b;
        case J_NUM:  return strcmp(x->u.num, y->u.num) == 0;
        case J_STR:  return strcmp(x->u.str, y->u.str) == 0;
        case J_ARR:
            if (x->u.arr.n != y->u.arr.n) return 0;
            for (size_t i = 0; i < x->u.arr.n; i++) if (!j_equal(x->u.arr.items[i], y->u.arr.items[i])) return 0;
            return 1;
        case J_OBJ: {
            if (x->u.obj.n != y->u.obj.n) return 0;
            for (jmem *m = x->u.obj.first; m; m = m->next) {
                jval *o = j_get(y, m->key);
                if (!o || !j_equal(m->val, o)) return 0;
            }
            return 1;
        }
    }
    return 0;
}

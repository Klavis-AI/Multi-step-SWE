#include "json.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct { const char *p, *end; } jp;

static void skip_ws(jp *s) { while (s->p < s->end && (*s->p == ' ' || *s->p == '\t' || *s->p == '\n' || *s->p == '\r')) s->p++; }
static jval *parse_val(jp *s);

static jval *jnew(jtype t)
{
    jval *v = (jval *)calloc(1, sizeof(jval));
    v->type = t;
    return v;
}

static void jpush(jval *v, char *key, jval *child)
{
    if (v->n == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 8;
        v->items = (jval **)realloc(v->items, v->cap * sizeof(jval *));
        v->keys = (char **)realloc(v->keys, v->cap * sizeof(char *));
    }
    v->keys[v->n] = key;
    v->items[v->n] = child;
    v->n++;
}

static char *parse_string_raw(jp *s)
{
    if (s->p >= s->end || *s->p != '"') return NULL;
    s->p++;
    buf_t b; buf_init(&b);
    while (s->p < s->end && *s->p != '"') {
        if (*s->p == '\\') {
            s->p++;
            if (s->p >= s->end) { buf_free(&b); return NULL; }
            char c = *s->p++;
            switch (c) {
            case 'n': buf_putc(&b, '\n'); break;
            case 't': buf_putc(&b, '\t'); break;
            case 'r': buf_putc(&b, '\r'); break;
            case 'b': buf_putc(&b, '\b'); break;
            case 'f': buf_putc(&b, '\f'); break;
            case '/': buf_putc(&b, '/'); break;
            case '\\': buf_putc(&b, '\\'); break;
            case '"': buf_putc(&b, '"'); break;
            case 'u': {
                if (s->end - s->p < 4) { buf_free(&b); return NULL; }
                unsigned cp = 0;
                for (int i = 0; i < 4; i++) {
                    char h = s->p[i];
                    cp <<= 4;
                    if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                    else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                    else { buf_free(&b); return NULL; }
                }
                s->p += 4;
                if (cp < 0x80) buf_putc(&b, (uint8_t)cp);
                else if (cp < 0x800) { buf_putc(&b, (uint8_t)(0xc0 | (cp >> 6))); buf_putc(&b, (uint8_t)(0x80 | (cp & 0x3f))); }
                else { buf_putc(&b, (uint8_t)(0xe0 | (cp >> 12))); buf_putc(&b, (uint8_t)(0x80 | ((cp >> 6) & 0x3f))); buf_putc(&b, (uint8_t)(0x80 | (cp & 0x3f))); }
                break;
            }
            default: buf_free(&b); return NULL;
            }
        } else {
            buf_putc(&b, (uint8_t)*s->p++);
        }
    }
    if (s->p >= s->end) { buf_free(&b); return NULL; }
    s->p++;
    buf_putc(&b, 0);
    return (char *)b.data;
}

static jval *parse_val(jp *s)
{
    skip_ws(s);
    if (s->p >= s->end) return NULL;
    char c = *s->p;
    if (c == '{') {
        s->p++;
        jval *v = jnew(JOBJ);
        skip_ws(s);
        if (s->p < s->end && *s->p == '}') { s->p++; return v; }
        for (;;) {
            skip_ws(s);
            char *k = parse_string_raw(s);
            if (!k) { json_free(v); return NULL; }
            skip_ws(s);
            if (s->p >= s->end || *s->p != ':') { free(k); json_free(v); return NULL; }
            s->p++;
            jval *child = parse_val(s);
            if (!child) { free(k); json_free(v); return NULL; }
            jpush(v, k, child);
            skip_ws(s);
            if (s->p < s->end && *s->p == ',') { s->p++; continue; }
            if (s->p < s->end && *s->p == '}') { s->p++; return v; }
            json_free(v); return NULL;
        }
    }
    if (c == '[') {
        s->p++;
        jval *v = jnew(JARR);
        skip_ws(s);
        if (s->p < s->end && *s->p == ']') { s->p++; return v; }
        for (;;) {
            jval *child = parse_val(s);
            if (!child) { json_free(v); return NULL; }
            jpush(v, NULL, child);
            skip_ws(s);
            if (s->p < s->end && *s->p == ',') { s->p++; continue; }
            if (s->p < s->end && *s->p == ']') { s->p++; return v; }
            json_free(v); return NULL;
        }
    }
    if (c == '"') {
        char *str = parse_string_raw(s);
        if (!str) return NULL;
        jval *v = jnew(JSTR);
        v->str = str;
        return v;
    }
    if (!strncmp(s->p, "true", (size_t)(s->end - s->p) < 4 ? (size_t)(s->end - s->p) : 4) && s->end - s->p >= 4) {
        s->p += 4; jval *v = jnew(JBOOL); v->b = true; return v;
    }
    if (s->end - s->p >= 5 && !strncmp(s->p, "false", 5)) { s->p += 5; jval *v = jnew(JBOOL); v->b = false; return v; }
    if (s->end - s->p >= 4 && !strncmp(s->p, "null", 4)) { s->p += 4; return jnew(JNULL); }
    {
        const char *start = s->p;
        if (s->p < s->end && (*s->p == '-' || *s->p == '+')) s->p++;
        while (s->p < s->end && ((*s->p >= '0' && *s->p <= '9') || *s->p == '.' || *s->p == 'e' || *s->p == 'E' || *s->p == '-' || *s->p == '+')) s->p++;
        if (s->p == start) return NULL;
        jval *v = jnew(JNUM);
        size_t n = (size_t)(s->p - start);
        v->str = (char *)malloc(n + 1);
        memcpy(v->str, start, n);
        v->str[n] = 0;
        v->num = strtod(v->str, NULL);
        return v;
    }
}

jval *json_parse(const char *text, size_t len)
{
    jp s = { text, text + len };
    jval *v = parse_val(&s);
    if (!v) return NULL;
    skip_ws(&s);
    return v;
}

void json_free(jval *v)
{
    if (!v) return;
    for (size_t i = 0; i < v->n; i++) { free(v->keys[i]); json_free(v->items[i]); }
    free(v->items); free(v->keys); free(v->str); free(v);
}

jval *json_get(const jval *obj, const char *key)
{
    if (!obj || obj->type != JOBJ) return NULL;
    for (size_t i = 0; i < obj->n; i++)
        if (obj->keys[i] && !strcmp(obj->keys[i], key)) return obj->items[i];
    return NULL;
}

jval *json_at(const jval *arr, size_t i)
{
    if (!arr || i >= arr->n) return NULL;
    return arr->items[i];
}

const char *json_str(const jval *v)
{
    if (!v) return NULL;
    if (v->type == JSTR || v->type == JNUM) return v->str;
    return NULL;
}

void json_wr_raw(buf_t *o, const char *s) { buf_put(o, s, strlen(s)); }

void json_wr_str(buf_t *o, const char *s)
{
    buf_putc(o, '"');
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        switch (c) {
        case '"': json_wr_raw(o, "\\\""); break;
        case '\\': json_wr_raw(o, "\\\\"); break;
        case '\n': json_wr_raw(o, "\\n"); break;
        case '\r': json_wr_raw(o, "\\r"); break;
        case '\t': json_wr_raw(o, "\\t"); break;
        default:
            if (c < 0x20) { char tmp[8]; snprintf(tmp, sizeof tmp, "\\u%04x", c); json_wr_raw(o, tmp); }
            else buf_putc(o, c);
        }
    }
    buf_putc(o, '"');
}

void json_wr_key(buf_t *o, const char *k) { json_wr_str(o, k); buf_putc(o, ':'); }

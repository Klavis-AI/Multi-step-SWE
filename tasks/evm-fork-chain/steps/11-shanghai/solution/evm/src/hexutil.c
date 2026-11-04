#include "hexutil.h"
#include <stdio.h>
#include <stdlib.h>

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int hex_decode(const char *s, buf_t *out)
{
    if (!s) return -1;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    size_t n = strlen(s);
    if (n % 2) {                       /* odd nibble count: pad on the left */
        int v = hexval(s[0]);
        if (v < 0) return -1;
        buf_putc(out, (uint8_t)v);
        s++; n--;
    }
    for (size_t i = 0; i < n; i += 2) {
        int hi = hexval(s[i]), lo = hexval(s[i + 1]);
        if (hi < 0 || lo < 0) return -1;
        buf_putc(out, (uint8_t)((hi << 4) | lo));
    }
    return 0;
}

static int str_to_u256(const char *s, u256 *out)
{
    if (!s) return -1;
    while (*s == ' ') s++;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        buf_t b; buf_init(&b);
        if (hex_decode(s, &b) != 0) { buf_free(&b); return -1; }
        if (b.len > 32) { buf_free(&b); return -1; }
        u256_from_be(out, b.data, b.len);
        buf_free(&b);
        return 0;
    }
    /* decimal */
    u256 acc = U256_ZERO, ten = u256_from_u64(10);
    if (!*s) return -1;
    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '9') return -1;
        acc = u256_mul(acc, ten);
        acc = u256_add(acc, u256_from_u64((uint64_t)(*p - '0')));
    }
    *out = acc;
    return 0;
}

int jnum_u256(const jval *v, u256 *out)
{
    if (!v) return -1;
    if (v->type == JNUM) return str_to_u256(v->str, out);
    if (v->type == JSTR) return str_to_u256(v->str, out);
    return -1;
}

int jnum_u64(const jval *v, uint64_t *out)
{
    u256 t;
    if (jnum_u256(v, &t) != 0) return -1;
    if (!u256_fits_u64(t)) return -1;
    *out = t.v[0];
    return 0;
}

int jnum_i64(const jval *v, int64_t *out)
{
    if (!v) return -1;
    const char *s = json_str(v);
    if (!s) return -1;
    if (s[0] == '-') {
        u256 t;
        if (str_to_u256(s + 1, &t) != 0 || !u256_fits_u64(t)) return -1;
        *out = -(int64_t)t.v[0];
        return 0;
    }
    u256 t;
    if (str_to_u256(s, &t) != 0 || !u256_fits_u64(t)) return -1;
    *out = (int64_t)t.v[0];
    return 0;
}

int jaddr(const jval *v, address *out)
{
    const char *s = json_str(v);
    if (!s) return -1;
    buf_t b; buf_init(&b);
    if (hex_decode(s, &b) != 0) { buf_free(&b); return -1; }
    if (b.len > 20) { buf_free(&b); return -1; }
    memset(out->b, 0, 20);
    memcpy(out->b + (20 - b.len), b.data, b.len);
    buf_free(&b);
    return 0;
}

int jhash(const jval *v, hash256 *out)
{
    const char *s = json_str(v);
    if (!s) return -1;
    buf_t b; buf_init(&b);
    if (hex_decode(s, &b) != 0) { buf_free(&b); return -1; }
    if (b.len > 32) { buf_free(&b); return -1; }
    memset(out->b, 0, 32);
    memcpy(out->b + (32 - b.len), b.data, b.len);
    buf_free(&b);
    return 0;
}

int jbytes(const jval *v, buf_t *out)
{
    const char *s = json_str(v);
    if (!s) return -1;
    return hex_decode(s, out);
}

void wr_u64(buf_t *o, uint64_t v)
{
    char tmp[32];
    snprintf(tmp, sizeof tmp, "0x%llx", (unsigned long long)v);
    json_wr_raw(o, tmp);
}

void wr_u256(buf_t *o, u256 v)
{
    uint8_t be[32];
    char tmp[80];
    u256_to_be(&v, be);
    int i = 0;
    while (i < 31 && be[i] == 0) i++;
    char *p = tmp;
    *p++ = '0'; *p++ = 'x';
    bool first = true;
    for (; i < 32; i++) {
        static const char *hx = "0123456789abcdef";
        if (first) {
            if (be[i] >> 4) *p++ = hx[be[i] >> 4];
            *p++ = hx[be[i] & 15];
            first = false;
        } else {
            *p++ = hx[be[i] >> 4];
            *p++ = hx[be[i] & 15];
        }
    }
    *p = 0;
    json_wr_raw(o, tmp);
}

void wr_bytes(buf_t *o, const uint8_t *p, size_t n)
{
    static const char *hx = "0123456789abcdef";
    buf_putc(o, '0'); buf_putc(o, 'x');
    for (size_t i = 0; i < n; i++) { buf_putc(o, (uint8_t)hx[p[i] >> 4]); buf_putc(o, (uint8_t)hx[p[i] & 15]); }
}

void wr_addr(buf_t *o, address a) { wr_bytes(o, a.b, 20); }
void wr_hash(buf_t *o, hash256 h) { wr_bytes(o, h.b, 32); }

void wr_u64_q(buf_t *o, uint64_t v) { buf_putc(o, '"'); wr_u64(o, v); buf_putc(o, '"'); }
void wr_u256_q(buf_t *o, u256 v) { buf_putc(o, '"'); wr_u256(o, v); buf_putc(o, '"'); }
void wr_bytes_q(buf_t *o, const uint8_t *p, size_t n) { buf_putc(o, '"'); wr_bytes(o, p, n); buf_putc(o, '"'); }
void wr_addr_q(buf_t *o, address a) { buf_putc(o, '"'); wr_addr(o, a); buf_putc(o, '"'); }
void wr_hash_q(buf_t *o, hash256 h) { buf_putc(o, '"'); wr_hash(o, h); buf_putc(o, '"'); }

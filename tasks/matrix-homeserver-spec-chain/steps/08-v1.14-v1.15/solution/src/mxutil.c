#define _POSIX_C_SOURCE 200809L
#include "mxutil.h"

#include <ctype.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

/* ---------------------------------------------------------------- buffers */
void buf_free(buf *s) { free(s->b); s->b = NULL; s->n = s->cap = 0; }

static void buf_need(buf *s, size_t k) {
    if (s->n + k + 1 <= s->cap) return;
    size_t cap = s->cap ? s->cap : 128;
    while (cap < s->n + k + 1) cap *= 2;
    s->b = realloc(s->b, cap);
    if (!s->b) abort();
    s->cap = cap;
}

void buf_add(buf *s, const void *p, size_t n) {
    buf_need(s, n);
    memcpy(s->b + s->n, p, n);
    s->n += n;
    s->b[s->n] = 0;
}

void buf_puts(buf *s, const char *p) { if (p) buf_add(s, p, strlen(p)); }

void buf_printf(buf *s, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return; }
    buf_need(s, (size_t)n);
    vsnprintf(s->b + s->n, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    s->n += (size_t)n;
}

char *buf_take(buf *s, size_t *n) {
    buf_need(s, 0);
    s->b[s->n] = 0;
    if (n) *n = s->n;
    char *out = s->b;
    s->b = NULL; s->n = s->cap = 0;
    return out ? out : calloc(1, 1);
}

/* -------------------------------------------------------------- encodings */
static const char B64STD[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char B64URL[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

char *b64_encode(const void *data, size_t n, bool url, bool padded) {
    const unsigned char *p = data;
    const char *A = url ? B64URL : B64STD;
    size_t groups = (n + 2) / 3;
    char *out = malloc(groups * 4 + 1);
    if (!out) abort();
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = (unsigned)p[i] << 16;
        if (i + 1 < n) v |= (unsigned)p[i + 1] << 8;
        if (i + 2 < n) v |= p[i + 2];
        out[o++] = A[(v >> 18) & 63];
        out[o++] = A[(v >> 12) & 63];
        if (i + 1 < n) out[o++] = A[(v >> 6) & 63];
        else if (padded) out[o++] = '=';
        if (i + 2 < n) out[o++] = A[v & 63];
        else if (padded) out[o++] = '=';
    }
    out[o] = 0;
    return out;
}

static int b64val(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

unsigned char *b64_decode(const char *s, size_t *out_n) {
    size_t len = strlen(s);
    unsigned char *out = malloc(len * 3 / 4 + 4);
    if (!out) abort();
    size_t o = 0;
    unsigned acc = 0;
    int bits = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '=' || isspace((unsigned char)s[i])) continue;
        int v = b64val((unsigned char)s[i]);
        if (v < 0) { free(out); return NULL; }
        acc = (acc << 6) | (unsigned)v;
        bits += 6;
        if (bits >= 8) { bits -= 8; out[o++] = (unsigned char)((acc >> bits) & 0xFF); }
    }
    if (out_n) *out_n = o;
    return out;
}

char *hex_encode(const void *data, size_t n) {
    const unsigned char *p = data;
    char *out = malloc(n * 2 + 1);
    if (!out) abort();
    for (size_t i = 0; i < n; i++) sprintf(out + i * 2, "%02x", p[i]);
    out[n * 2] = 0;
    return out;
}

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

char *url_decode(const char *s, size_t n, size_t *out_n) {
    char *out = malloc(n + 1);
    if (!out) abort();
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '%' && i + 2 < n) {
            int a = hexval((unsigned char)s[i + 1]), b = hexval((unsigned char)s[i + 2]);
            if (a >= 0 && b >= 0) { out[o++] = (char)(a * 16 + b); i += 2; continue; }
        }
        out[o++] = s[i];
    }
    out[o] = 0;
    if (out_n) *out_n = o;
    return out;
}

char *url_encode(const char *s) {
    size_t n = strlen(s);
    char *out = malloc(n * 3 + 1);
    if (!out) abort();
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out[o++] = (char)c;
        else { sprintf(out + o, "%%%02X", c); o += 3; }
    }
    out[o] = 0;
    return out;
}

/* ---------------------------------------------------------------- hashing */
void sha256(const void *data, size_t n, unsigned char out[32]) {
    unsigned int len = 32;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, data, n);
    EVP_DigestFinal_ex(ctx, out, &len);
    EVP_MD_CTX_free(ctx);
}

void hmac_sha256(const void *key, size_t klen, const void *data, size_t n,
                 unsigned char out[32]) {
    unsigned int len = 32;
    HMAC(EVP_sha256(), key, (int)klen, data, n, out, &len);
}

/* ---------------------------------------------------------------- ed25519 */
bool ed25519_public_from_seed(const unsigned char seed[32], unsigned char pub[32]) {
    EVP_PKEY *k = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, NULL, seed, 32);
    if (!k) return false;
    size_t len = 32;
    bool ok = EVP_PKEY_get_raw_public_key(k, pub, &len) == 1 && len == 32;
    EVP_PKEY_free(k);
    return ok;
}

bool ed25519_sign(const unsigned char seed[32], const void *msg, size_t n,
                  unsigned char sig[64]) {
    EVP_PKEY *k = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, NULL, seed, 32);
    if (!k) return false;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    size_t siglen = 64;
    bool ok = ctx && EVP_DigestSignInit(ctx, NULL, NULL, NULL, k) == 1 &&
              EVP_DigestSign(ctx, sig, &siglen, msg, n) == 1 && siglen == 64;
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(k);
    return ok;
}

bool ed25519_verify(const unsigned char pub[32], const void *msg, size_t n,
                    const unsigned char sig[64]) {
    EVP_PKEY *k = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, NULL, pub, 32);
    if (!k) return false;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    bool ok = ctx && EVP_DigestVerifyInit(ctx, NULL, NULL, NULL, k) == 1 &&
              EVP_DigestVerify(ctx, sig, 64, msg, n) == 1;
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(k);
    return ok;
}

/* ------------------------------------------------------------------- misc */
int64_t now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void random_bytes(void *out, size_t n) {
    if (RAND_bytes(out, (int)n) != 1) {
        unsigned char *p = out;
        for (size_t i = 0; i < n; i++) p[i] = (unsigned char)(rand() & 0xFF);
    }
}

char *random_string(size_t nchars) {
    static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    char *out = malloc(nchars + 1);
    if (!out) abort();
    unsigned char *r = malloc(nchars);
    random_bytes(r, nchars);
    for (size_t i = 0; i < nchars; i++) out[i] = A[r[i] % (sizeof A - 1)];
    free(r);
    out[nchars] = 0;
    return out;
}

char *xstrdup_(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *p = malloc(n + 1);
    if (!p) abort();
    memcpy(p, s, n + 1);
    return p;
}

bool str_eq(const char *a, const char *b) {
    if (!a || !b) return a == b;
    return strcmp(a, b) == 0;
}

bool str_prefix(const char *s, const char *prefix) {
    if (!s || !prefix) return false;
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

char *str_fmt(const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return xstrdup_(""); }
    char *out = malloc((size_t)n + 1);
    if (!out) abort();
    vsnprintf(out, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return out;
}

/* User id grammar, Client-Server API "Users" appendix: the localpart is
 * a-z 0-9 . _ = - / + (historical ids may carry more, which is why reading is
 * lenient and minting is strict). */
bool valid_localpart(const char *s) {
    if (!s || !*s) return false;
    for (const char *p = s; *p; p++) {
        char c = *p;
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                  c == '.' || c == '_' || c == '=' || c == '-' || c == '/' || c == '+';
        if (!ok) return false;
    }
    return true;
}

bool valid_user_id(const char *s) {
    if (!s || s[0] != '@') return false;
    const char *colon = strchr(s + 1, ':');
    return colon && colon > s + 1 && colon[1];
}

const char *user_id_server(const char *user_id) {
    if (!user_id) return NULL;
    const char *colon = strchr(user_id, ':');
    return colon ? colon + 1 : NULL;
}

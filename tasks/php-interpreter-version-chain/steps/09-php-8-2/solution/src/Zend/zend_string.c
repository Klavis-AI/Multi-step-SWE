/* zend_string.c -- the interpreter's string type.
 *
 * A zend_string is immutable once handed out, binary safe (it may contain
 * embedded NUL bytes, ZSTR_LEN() is authoritative) and always has a NUL byte
 * one past the end so that the payload can also be passed straight to libc
 * functions when the caller knows it is text.
 *
 * The hash is computed lazily and cached in s->h; 0 means "not computed yet",
 * which is why the hash function can never return 0.
 */
#include "zend.h"
#include "zend_alloc.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Bytes needed to hold a zend_string with "len" payload bytes.  The struct ends
 * in char val[1], so offsetof() gives the exact header size and the +1 pays for
 * the terminating NUL rather than counting a byte twice. */
#define ZEND_STRING_SIZE(len) (offsetof(zend_string, val) + (len) + 1)

/* ------------------------------------------------------------ ASCII helpers */

static char zend_ascii_tolower(char c)
{
    unsigned char uc = (unsigned char) c;

    return (uc >= 'A' && uc <= 'Z') ? (char) (uc - 'A' + 'a') : c;
}

/* ------------------------------------------------------------- constructors */

zend_string *zend_string_alloc(size_t len)
{
    zend_string *s = emalloc(ZEND_STRING_SIZE(len));

    s->refcount = 1;
    s->len = len;
    s->h = 0;                   /* 0 == hash not computed yet */
    s->val[len] = '\0';
    return s;
}

zend_string *zend_string_init(const char *s, size_t len)
{
    zend_string *ret = zend_string_alloc(len);

    if (len != 0 && s != NULL) {
        memcpy(ret->val, s, len);
    }
    return ret;
}

zend_string *zend_string_initz(const char *s)
{
    return zend_string_init(s, s != NULL ? strlen(s) : 0);
}

zend_string *zend_string_concat(const zend_string *a, const zend_string *b)
{
    size_t alen = (a != NULL) ? a->len : 0;
    size_t blen = (b != NULL) ? b->len : 0;
    zend_string *ret = zend_string_alloc(alen + blen);

    if (alen != 0) {
        memcpy(ret->val, a->val, alen);
    }
    if (blen != 0) {
        memcpy(ret->val + alen, b->val, blen);
    }
    return ret;
}

zend_string *zend_strpprintf(const char *fmt, ...)
{
    va_list ap, ap2;
    zend_string *ret;
    int len;

    if (fmt == NULL) {
        return zend_string_init("", 0);
    }

    va_start(ap, fmt);
    va_copy(ap2, ap);
    len = vsnprintf(NULL, 0, fmt, ap);      /* ask for the required length */
    va_end(ap);

    if (len < 0) {                          /* encoding error: yield "" */
        va_end(ap2);
        return zend_string_init("", 0);
    }

    ret = zend_string_alloc((size_t) len);
    vsnprintf(ret->val, (size_t) len + 1, fmt, ap2);
    va_end(ap2);
    ret->val[len] = '\0';
    return ret;
}

/* -------------------------------------------------------------- comparisons */

int zend_string_equals(const zend_string *a, const zend_string *b)
{
    if (a == b) {
        return 1;               /* covers the two-NULLs case as well */
    }
    if (a == NULL || b == NULL || a->len != b->len) {
        return 0;
    }
    return a->len == 0 || memcmp(a->val, b->val, a->len) == 0;
}

/* ASCII case insensitive equality, the way PHP compares identifiers: only
 * A-Z/a-z fold, every other byte (including UTF-8 continuation bytes) has to
 * match exactly. */
int zend_string_equals_ci(const zend_string *a, const zend_string *b)
{
    size_t i;

    if (a == b) {
        return 1;
    }
    if (a == NULL || b == NULL || a->len != b->len) {
        return 0;
    }
    for (i = 0; i < a->len; i++) {
        if (zend_ascii_tolower(a->val[i]) != zend_ascii_tolower(b->val[i])) {
            return 0;
        }
    }
    return 1;
}

/* ----------------------------------------------------------------- case fold */

zend_string *zend_string_tolower(const zend_string *s)
{
    zend_string *ret;
    size_t i;

    if (s == NULL) {
        return zend_string_init("", 0);
    }
    ret = zend_string_alloc(s->len);
    for (i = 0; i < s->len; i++) {
        ret->val[i] = zend_ascii_tolower(s->val[i]);
    }
    return ret;
}

/* ----------------------------------------------------------------- hashing */

/* DJBX33A, as used by php-src: h = 5381, then h = h * 33 + c for every byte.
 * The top bit is forced on afterwards so a real hash is never 0 and 0 can keep
 * meaning "not computed yet" in the cache.  Bytes are read unsigned so the
 * result does not depend on whether plain char is signed. */
static zend_ulong zend_string_hash_bytes(const char *str, size_t len)
{
    zend_ulong h = (zend_ulong) 5381;
    size_t i;

    for (i = 0; i < len; i++) {
        h = ((h << 5) + h) + (zend_ulong) (unsigned char) str[i];
    }
    return h | (zend_ulong) 0x8000000000000000ULL;
}

zend_ulong zend_string_hash(zend_string *s)
{
    if (s == NULL) {
        return 0;
    }
    if (s->h == 0) {
        s->h = zend_string_hash_bytes(s->val, s->len);
    }
    return s->h;
}

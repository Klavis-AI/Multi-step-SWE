/* ext/standard/php_string.c -- string builtins.
 *
 * Behaviour target: PHP 5.6.40 (php-src ext/standard/string.c).  Every place
 * where a later release changes observable behaviour carries a
 * "VERSIONGATE:<version-id>" comment marker on the line above the deciding
 * `if`, naming which branch is the 5.6 one.  A later pass turns each of those into a runtime test on
 * EG(php_version); nothing post-5.6 is implemented here.  Functions that do
 * not exist in 5.6 carry the same marker above their definition and above
 * their registry row so both can be stripped together.
 */

#include "zend.h"
#include "zend_ast.h"
#include "zend_API.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ misc */

#define PS_MIN(a, b) ((a) < (b) ? (a) : (b))
#define PS_MAX(a, b) ((a) > (b) ? (a) : (b))

#define STR_PAD_LEFT    0
#define STR_PAD_RIGHT   1
#define STR_PAD_BOTH    2

/* htmlspecialchars()/html_entity_decode() flags */
#define PS_ENT_QUOTE_DOUBLE      2
#define PS_ENT_QUOTE_SINGLE      1
#define PS_ENT_IGNORE            4
#define PS_ENT_SUBSTITUTE        8
#define PS_ENT_DOC_TYPE_MASK     (16 | 32)
#define PS_ENT_DOC_HTML401       0
#define PS_ENT_DOC_XML1          16
#define PS_ENT_DOC_XHTML         32
#define PS_ENT_DOC_HTML5         (16 | 32)
#define PS_ENT_COMPAT            PS_ENT_QUOTE_DOUBLE

static void ps_ret_stringl(zval *ret, const char *s, size_t len)
{
    ZVAL_STR(ret, zend_string_init(s, len));
}

static void ps_ret_empty(zval *ret)
{
    ZVAL_STR(ret, zend_string_init("", 0));
}

static void ps_ret_false(zval *ret)
{
    ZVAL_BOOL(ret, 0);
}

static void ps_ret_zstr_copy(zval *ret, const zend_string *s)
{
    ZVAL_STR(ret, zend_string_init(ZSTR_VAL(s), ZSTR_LEN(s)));
}

/* By-reference parameters arrive as IS_REFERENCE; write through the slot. */
static zval *ps_ref(zval *arg)
{
    if (Z_TYPE_P(arg) == IS_REFERENCE) {
        return &Z_REF_P(arg)->val;
    }
    return arg;
}

/* ASCII case folding; php-src calls tolower()/toupper() which, under the "C"
 * locale the test harness runs in, touch only A-Z / a-z. */
static char ps_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
}

static char ps_upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char) (c - 'a' + 'A') : c;
}

static void ps_tolower_buf(char *s, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++) {
        s[i] = ps_lower(s[i]);
    }
}

/* php_memnstr(): find `needle` in [haystack, end). */
static const char *ps_memnstr(const char *haystack, const char *needle,
                              size_t needle_len, const char *end)
{
    const char *p, *last;

    if (needle_len == 0) {
        return haystack;
    }
    if ((size_t) (end - haystack) < needle_len) {
        return NULL;
    }
    if (needle_len == 1) {
        return (const char *) memchr(haystack, (unsigned char) *needle,
                                     (size_t) (end - haystack));
    }
    last = end - needle_len;
    p = haystack;
    while (p <= last) {
        const char *q = (const char *) memchr(p, (unsigned char) *needle,
                                              (size_t) (last - p) + 1);
        if (q == NULL) {
            return NULL;
        }
        if (memcmp(q, needle, needle_len) == 0) {
            return q;
        }
        p = q + 1;
    }
    return NULL;
}

static const char *ps_memrchr(const char *s, int c, size_t n)
{
    while (n > 0) {
        n--;
        if ((unsigned char) s[n] == (unsigned char) c) {
            return s + n;
        }
    }
    return NULL;
}

/* php_charmask(): builds a 256-entry membership table, supporting "a..z". */
static int ps_charmask(const unsigned char *input, size_t len, char *mask,
                       const char *fname)
{
    const unsigned char *end;
    unsigned char c;
    int result = 1;
    const unsigned char *start = input;

    memset(mask, 0, 256);
    for (end = input + len; input < end; input++) {
        c = *input;
        if ((input + 3 < end) && input[1] == '.' && input[2] == '.'
            && input[3] >= c) {
            memset(mask + c, 1, (size_t) (input[3] - c) + 1);
            input += 3;
        } else if ((input + 1 < end) && input[0] == '.' && input[1] == '.') {
            if (input == start) { /* no character to the left */
                zend_error(E_WARNING, "%s(): Invalid '..'-range, no character "
                           "to the left of '..'", fname);
                result = 0;
                continue;
            }
            if (input + 2 >= end) { /* no character to the right */
                zend_error(E_WARNING, "%s(): Invalid '..'-range, no character "
                           "to the right of '..'", fname);
                result = 0;
                continue;
            }
            if (input[-1] > input[2]) {
                zend_error(E_WARNING, "%s(): Invalid '..'-range, '..'-range "
                           "needs to be incrementing", fname);
                result = 0;
                continue;
            }
            zend_error(E_WARNING, "%s(): Invalid '..'-range", fname);
            result = 0;
            continue;
        } else {
            mask[c] = 1;
        }
    }
    return result;
}

/* zend_binary_strcmp()/zend_binary_strcasecmp() from 5.6: the raw byte
 * difference is returned, not a normalised -1/0/1.
 * VERSIONGATE:80000 the raw-difference result below is the 5.6 branch; 8.0
 * normalises every string comparison function to -1/0/1. */
static int ps_binary_strcmp(const char *a, size_t alen, const char *b, size_t blen)
{
    size_t n = PS_MIN(alen, blen), i;

    for (i = 0; i < n; i++) {
        int d = (int) (unsigned char) a[i] - (int) (unsigned char) b[i];
        if (d != 0) {
            return d;
        }
    }
    return (int) ((zend_long) alen - (zend_long) blen);
}

static int ps_binary_strcasecmp(const char *a, size_t alen, const char *b, size_t blen)
{
    size_t n = PS_MIN(alen, blen), i;

    for (i = 0; i < n; i++) {
        int c1 = (unsigned char) ps_lower(a[i]);
        int c2 = (unsigned char) ps_lower(b[i]);
        if (c1 != c2) {
            return c1 - c2;
        }
    }
    return (int) ((zend_long) alen - (zend_long) blen);
}

static int ps_binary_strncmp(const char *a, size_t alen, const char *b,
                             size_t blen, size_t length)
{
    size_t n = PS_MIN(length, PS_MIN(alen, blen)), i;

    for (i = 0; i < n; i++) {
        int d = (int) (unsigned char) a[i] - (int) (unsigned char) b[i];
        if (d != 0) {
            return d;
        }
    }
    return (int) ((zend_long) PS_MIN(length, alen) - (zend_long) PS_MIN(length, blen));
}

static int ps_binary_strncasecmp(const char *a, size_t alen, const char *b,
                                 size_t blen, size_t length)
{
    size_t n = PS_MIN(length, PS_MIN(alen, blen)), i;

    for (i = 0; i < n; i++) {
        int c1 = (unsigned char) ps_lower(a[i]);
        int c2 = (unsigned char) ps_lower(b[i]);
        if (c1 != c2) {
            return c1 - c2;
        }
    }
    return (int) ((zend_long) PS_MIN(length, alen) - (zend_long) PS_MIN(length, blen));
}

/* ------------------------------------------------- argument prologue ----
 * The evaluator rejects too *few* arguments and nothing else, and it converts
 * nothing, so each builtin runs php-src's zend_parse_parameters() checks
 * itself.  Every macro returns from the calling handler when a check fails,
 * leaving *ret at IS_NULL exactly as php-src leaves return_value.
 */
#define PS_ARGC(fname, mn, mx)                                                \
    do {                                                                      \
        if (argc < (uint32_t) (mn) || argc > (uint32_t) (mx)) {               \
            zend_wrong_param_count((fname), argc, (mn), (mx));                \
            return;                                                           \
        }                                                                     \
    } while (0)

/* The name zend_parse_parameters() gives the value it was handed.  5.6 uses
 * gettype()'s spelling with a lower case "null"; 7.0 renames double to float;
 * 7.3 renames integer/boolean to int/bool, which is also what 8.0's TypeError
 * uses.
 * VERSIONGATE:70300 else-branch is the 5.6/7.0-7.2 spelling */
static const char *ps_type_name(const zval *v)
{
    if (EG(php_version) >= 70300) {
        return zend_zval_type_name(v);
    }
    switch (Z_TYPE_P(v)) {
    case IS_UNDEF:
    case IS_NULL:   return "null";
    case IS_FALSE:
    case IS_TRUE:   return "boolean";
    case IS_LONG:   return "integer";
    /* VERSIONGATE:70000 else-branch is the 5.6 spelling */
    case IS_DOUBLE: return EG(php_version) >= 70000 ? "float" : "double";
    case IS_STRING: return "string";
    case IS_ARRAY:  return "array";
    case IS_OBJECT: return "object";
    default:        return "unknown";
    }
}

/* The name of the type a parameter wants, which moved the same way.
 * VERSIONGATE:70300 else-branch is the 5.6/7.0-7.2 spelling */
static const char *ps_want_long(void)
{
    if (EG(php_version) >= 70300) {
        return "int";
    }
    /* VERSIONGATE:70000 else-branch is the 5.6 spelling */
    return EG(php_version) >= 70000 ? "integer" : "long";
}

/* VERSIONGATE:70000 else-branch is the 5.6 spelling */
static const char *ps_want_double(void)
{
    return EG(php_version) >= 70000 ? "float" : "double";
}

/* VERSIONGATE:70300 else-branch is the 5.6/7.0-7.2 spelling */
static const char *ps_want_bool(void)
{
    return "boolean";
}

/* php-src 5.6 spells the scalar type names differently from 8.0, so these
 * wrap the engine's parsers rather than using them directly.
 * VERSIONGATE:80000 else-branch is the 5.6 spelling (long/double/boolean) */
static int ps_scalar_error(const zval *v, int argn, const char *fname,
                           const char *name56, const char *name80)
{
    if (EG(php_version) >= 80000) {
        zend_throw_error("TypeError", "%s(): Argument #%d must be of type %s, "
                         "%s given", fname, argn, name80, zend_zval_type_name(v));
    } else {
        zend_error(E_WARNING, "%s() expects parameter %d to be %s, %s given",
                   fname, argn, name56, ps_type_name(v));
    }
    return 0;
}

/* True when the string is numeric with no trailing characters. */
static int ps_numeric_is_clean(const zval *a)
{
    return is_numeric_string(ZSTR_VAL(Z_STR_P((zval *) a)),
                             ZSTR_LEN(Z_STR_P((zval *) a)), NULL, NULL, 0) != 0;
}

static int ps_arg_long(zval *arg, zend_long *dest, int argn, const char *fname)
{
    zval *a = zval_deref(arg);

    switch (Z_TYPE_P(a)) {
    case IS_UNDEF: case IS_NULL: case IS_FALSE: case IS_TRUE:
    case IS_LONG: case IS_DOUBLE:
        *dest = zval_get_long(a);
        return 1;
    case IS_STRING: {
        zend_long lval = 0;
        double dval = 0.0;
        int kind = is_numeric_string(ZSTR_VAL(Z_STR_P(a)), ZSTR_LEN(Z_STR_P(a)),
                                     &lval, &dval, 1);
        if (kind == 0) {
            return ps_scalar_error(a, argn, fname, ps_want_long(), "int");
        }
        if (!ps_numeric_is_clean(a)) {

            zend_error(E_NOTICE, "A non well formed numeric value encountered");
        }
        *dest = (kind == IS_LONG) ? lval : (zend_long) dval;
        return 1;
    }
    default:
        return ps_scalar_error(a, argn, fname, ps_want_long(), "int");
    }
}

static int ps_arg_double(zval *arg, double *dest, int argn, const char *fname)
{
    zval *a = zval_deref(arg);

    switch (Z_TYPE_P(a)) {
    case IS_UNDEF: case IS_NULL: case IS_FALSE: case IS_TRUE:
    case IS_LONG: case IS_DOUBLE:
        *dest = zval_get_double(a);
        return 1;
    case IS_STRING: {
        zend_long lval = 0;
        double dval = 0.0;
        int kind = is_numeric_string(ZSTR_VAL(Z_STR_P(a)), ZSTR_LEN(Z_STR_P(a)),
                                     &lval, &dval, 1);
        if (kind == IS_LONG) { *dest = (double) lval; return 1; }
        if (kind == IS_DOUBLE) { *dest = dval; return 1; }
        return ps_scalar_error(a, argn, fname, ps_want_double(), "float");
    }
    default:
        return ps_scalar_error(a, argn, fname, ps_want_double(), "float");
    }
}

static int ps_arg_bool(zval *arg, int *dest, int argn, const char *fname)
{
    zval *a = zval_deref(arg);

    switch (Z_TYPE_P(a)) {
    case IS_UNDEF: case IS_NULL: case IS_FALSE: case IS_TRUE:
    case IS_LONG: case IS_DOUBLE: case IS_STRING:
        *dest = zval_is_true(a);
        return 1;
    default:
        return ps_scalar_error(a, argn, fname, ps_want_bool(), "bool");
    }
}

/* php-src's WRONG_PARAM_COUNT: a few functions check their own argument count
 * instead of going through zend_parse_parameters().
 * VERSIONGATE:80000 else-branch is the 5.6 form: a warning, not a throw */
static void ps_wrong_count(const char *fname, uint32_t argc, uint32_t expect)
{
    if (EG(php_version) >= 80000) {
        zend_throw_error("ArgumentCountError",
                         "%s() expects exactly %u argument%s, %u given", fname,
                         (unsigned) expect, expect == 1 ? "" : "s",
                         (unsigned) argc);
    } else {
        zend_error(E_WARNING, "Wrong parameter count for %s()", fname);
    }
}

#define PS_LONG(var, idx, fname)                                              \
    do { if (!ps_arg_long(&argv[idx], &(var), (idx) + 1, (fname))) return; } while (0)
#define PS_OPT_LONG(var, idx, fname)                                          \
    do { if (argc > (uint32_t) (idx)                                          \
             && !ps_arg_long(&argv[idx], &(var), (idx) + 1, (fname))) return; \
    } while (0)
#define PS_OPT_BOOL(var, idx, fname)                                          \
    do { if (argc > (uint32_t) (idx)                                          \
             && !ps_arg_bool(&argv[idx], &(var), (idx) + 1, (fname))) return; \
    } while (0)

/* --------------------------------------------------------------- strlen */

static void zif_strlen(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("strlen", 1, 1);
    zend_string *s;
    if (!zend_parse_arg_string(&argv[0], &s, 1, "strlen")) return;
    (void) argc;
    ZVAL_LONG(ret, (zend_long) ZSTR_LEN(s));
}

static void zif_strtolower(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("strtolower", 1, 1);
    zend_string *s;
    if (!zend_parse_arg_string(&argv[0], &s, 1, "strtolower")) return;
    zend_string *out = zend_string_init(ZSTR_VAL(s), ZSTR_LEN(s));
    (void) argc;
    /* VERSIONGATE:80200 the ASCII-only fold below is the 5.6 branch (5.6 uses
     * tolower(), which is ASCII-only in the "C" locale the harness runs in);
     * 8.2 makes the fold locale-insensitive by definition. */
    ps_tolower_buf(ZSTR_VAL(out), ZSTR_LEN(out));
    ZVAL_STR(ret, out);
}

static void zif_strtoupper(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("strtoupper", 1, 1);
    zend_string *s;
    if (!zend_parse_arg_string(&argv[0], &s, 1, "strtoupper")) return;
    zend_string *out = zend_string_init(ZSTR_VAL(s), ZSTR_LEN(s));
    size_t i;
    (void) argc;
    for (i = 0; i < ZSTR_LEN(out); i++) {
        ZSTR_VAL(out)[i] = ps_upper(ZSTR_VAL(out)[i]);
    }
    ZVAL_STR(ret, out);
}

static void zif_ucfirst(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("ucfirst", 1, 1);
    zend_string *s;
    if (!zend_parse_arg_string(&argv[0], &s, 1, "ucfirst")) return;
    zend_string *out;
    (void) argc;
    if (ZSTR_LEN(s) == 0) {
        ps_ret_empty(ret);
        return;
    }
    out = zend_string_init(ZSTR_VAL(s), ZSTR_LEN(s));
    ZSTR_VAL(out)[0] = ps_upper(ZSTR_VAL(out)[0]);
    ZVAL_STR(ret, out);
}

static void zif_lcfirst(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("lcfirst", 1, 1);
    zend_string *s;
    if (!zend_parse_arg_string(&argv[0], &s, 1, "lcfirst")) return;
    zend_string *out;
    (void) argc;
    if (ZSTR_LEN(s) == 0) {
        ps_ret_empty(ret);
        return;
    }
    out = zend_string_init(ZSTR_VAL(s), ZSTR_LEN(s));
    ZSTR_VAL(out)[0] = ps_lower(ZSTR_VAL(out)[0]);
    ZVAL_STR(ret, out);
}

static void zif_ucwords(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("ucwords", 1, 2);
    zend_string *s;
    if (!zend_parse_arg_string(&argv[0], &s, 1, "ucwords")) return;
    const char *delims = " \t\r\n\f\v";
    size_t delims_len = 6;
    zend_string *delim_s = NULL;
    zend_string *out;
    char mask[256];
    size_t i;

    /* $separators exists since 5.4.32/5.5.16, i.e. already in 5.6. */
    if (argc > 1) {
        if (!zend_parse_arg_string(&argv[1], &delim_s, 2, "ucwords")) return;
        delims = ZSTR_VAL(delim_s);
        delims_len = ZSTR_LEN(delim_s);
    }
    if (ZSTR_LEN(s) == 0) {
        ps_ret_empty(ret);
        return;
    }
    ps_charmask((const unsigned char *) delims, delims_len, mask, "ucwords");

    out = zend_string_init(ZSTR_VAL(s), ZSTR_LEN(s));
    ZSTR_VAL(out)[0] = ps_upper(ZSTR_VAL(out)[0]);
    for (i = 0; i + 1 < ZSTR_LEN(out); i++) {
        if (mask[(unsigned char) ZSTR_VAL(out)[i]]) {
            ZSTR_VAL(out)[i + 1] = ps_upper(ZSTR_VAL(out)[i + 1]);
        }
    }
    ZVAL_STR(ret, out);
}

/* ------------------------------------------------------------------ trim */

/* mode: bit0 = trim left, bit1 = trim right */
static void ps_do_trim(zval *ret, uint32_t argc, zval *argv, int mode,
                       const char *fname)
{
    zend_string *s;
    if (!zend_parse_arg_string(&argv[0], &s, 1, "ucwords")) return;
    const char *what = " \n\r\t\v\0";
    size_t what_len = 6;
    zend_string *what_s = NULL;
    char mask[256];
    const char *c;
    size_t len;
    size_t i;

    if (argc > 1) {
        if (!zend_parse_arg_string(&argv[1], &what_s, 2, "ucwords")) return;
        what = ZSTR_VAL(what_s);
        what_len = ZSTR_LEN(what_s);
    }
    ps_charmask((const unsigned char *) what, what_len, mask, fname);

    c = ZSTR_VAL(s);
    len = ZSTR_LEN(s);

    if (mode & 1) {
        size_t trimmed = 0;
        for (i = 0; i < len; i++) {
            if (mask[(unsigned char) c[i]]) {
                trimmed++;
            } else {
                break;
            }
        }
        len -= trimmed;
        c += trimmed;
    }
    if (mode & 2) {
        while (len > 0 && mask[(unsigned char) c[len - 1]]) {
            len--;
        }
    }
    ps_ret_stringl(ret, c, len);
}

static void zif_trim(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("trim", 1, 2);
    ps_do_trim(ret, argc, argv, 3, "trim");
}

static void zif_ltrim(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("ltrim", 1, 2);
    ps_do_trim(ret, argc, argv, 1, "ltrim");
}

static void zif_rtrim(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("rtrim", 1, 2);
    ps_do_trim(ret, argc, argv, 2, "rtrim");
}

static void zif_chop(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("chop", 1, 2);
    ps_do_trim(ret, argc, argv, 2, "chop");
}

/* ------------------------------------------------------------- str_repeat */

static void zif_str_repeat(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("str_repeat", 2, 2);
    zend_string *s;
    if (!zend_parse_arg_string(&argv[0], &s, 1, "str_repeat")) return;
    zend_long mult;
    PS_LONG(mult, 1, "str_repeat");
    zend_string *out;
    size_t i, len;
    (void) argc;


    len = ZSTR_LEN(s);
    if (len == 0 || mult == 0) {
        ps_ret_empty(ret);
        return;
    }
    out = zend_string_alloc(len * (size_t) mult);
    for (i = 0; i < (size_t) mult; i++) {
        memcpy(ZSTR_VAL(out) + i * len, ZSTR_VAL(s), len);
    }
    ZSTR_VAL(out)[len * (size_t) mult] = '\0';
    ZVAL_STR(ret, out);
}

static void zif_strrev(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("strrev", 1, 1);
    zend_string *s;
    if (!zend_parse_arg_string(&argv[0], &s, 1, "strrev")) return;
    zend_string *out = zend_string_alloc(ZSTR_LEN(s));
    size_t i, n = ZSTR_LEN(s);
    (void) argc;
    for (i = 0; i < n; i++) {
        ZSTR_VAL(out)[i] = ZSTR_VAL(s)[n - 1 - i];
    }
    ZSTR_VAL(out)[n] = '\0';
    ZVAL_STR(ret, out);
}

/* ------------------------------------------------------- needle handling */

/* php_needle_char(): a non-string needle is used as a single byte (its integer
 * value).  VERSIONGATE:80000 this whole helper is the 5.6 branch; 7.3
 * deprecates the integer needle and 8.0 removes it (the needle is then always
 * converted to string). */
static int ps_needle_char(const zval *needle, char *target, const char *fname)
{
    switch (Z_TYPE_P(needle)) {
    case IS_LONG:
    case IS_FALSE:
    case IS_TRUE:
        *target = (char) zval_get_long(needle);
        return 1;
    case IS_NULL:
        *target = '\0';
        return 1;
    case IS_DOUBLE:
        *target = (char) (int) Z_DVAL_P(needle);
        return 1;
    case IS_OBJECT:
        *target = (char) zval_get_long(needle);
        return 1;
    default:
        zend_error(E_WARNING, "%s(): needle is not a string or an integer", fname);
        return 0;
    }
}

/* Resolve the needle into a flat buffer.  Returns 0 on failure (a warning has
 * already been emitted). */
static int ps_needle(const zval *needle, const char **out, size_t *out_len,
                     char *scratch, zend_string **hold, const char *fname)
{
    *hold = NULL;
    if (Z_TYPE_P(needle) == IS_STRING) {
        *hold = Z_STR_P((zval *) needle);
        *out = ZSTR_VAL(*hold);
        *out_len = ZSTR_LEN(*hold);
        return 1;
    }
    if (!ps_needle_char(needle, scratch, fname)) {
        return 0;
    }
    scratch[1] = '\0';
    *out = scratch;
    *out_len = 1;
    return 1;
}

/* -------------------------------------------------------- strpos family */

static void zif_strpos(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("strpos", 2, 3);
    zend_string *h;
    if (!zend_parse_arg_string(&argv[0], &h, 1, "strpos")) return;
    zend_long offset = 0;
    PS_OPT_LONG(offset, 2, "strpos");
    const char *needle;
    size_t needle_len;
    char scratch[2];
    zend_string *hold;
    const char *found;


    if (offset < 0 || (size_t) offset > ZSTR_LEN(h)) {

        zend_error(E_WARNING, "strpos(): Offset not contained in string");
        ps_ret_false(ret);
        return;
    }
    if (!ps_needle(&argv[1], &needle, &needle_len, scratch, &hold, "strpos")) {
        ps_ret_false(ret);
        return;
    }

    found = ps_memnstr(ZSTR_VAL(h) + offset, needle, needle_len,
                       ZSTR_VAL(h) + ZSTR_LEN(h));
    if (found) {
        ZVAL_LONG(ret, (zend_long) (found - ZSTR_VAL(h)));
    } else {
        ps_ret_false(ret);
    }
}

static void zif_stripos(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("stripos", 2, 3);
    zend_string *h;
    if (!zend_parse_arg_string(&argv[0], &h, 1, "stripos")) return;
    zend_long offset = 0;
    PS_OPT_LONG(offset, 2, "stripos");
    const char *needle;
    size_t needle_len;
    char scratch[2];
    zend_string *hold;
    zend_string *hdup, *ndup;
    const char *found;


    if (offset < 0 || (size_t) offset > ZSTR_LEN(h)) {

        zend_error(E_WARNING, "stripos(): Offset not contained in string");
        ps_ret_false(ret);
        return;
    }
    if (ZSTR_LEN(h) == 0) {
        ps_ret_false(ret);
        return;
    }
    if (!ps_needle(&argv[1], &needle, &needle_len, scratch, &hold, "stripos")) {
        ps_ret_false(ret);
        return;
    }
    if (needle_len == 0 || needle_len > ZSTR_LEN(h)) {
        ps_ret_false(ret);
        return;
    }
    hdup = zend_string_init(ZSTR_VAL(h), ZSTR_LEN(h));
    ps_tolower_buf(ZSTR_VAL(hdup), ZSTR_LEN(hdup));
    ndup = zend_string_init(needle, needle_len);
    ps_tolower_buf(ZSTR_VAL(ndup), ZSTR_LEN(ndup));

    found = ps_memnstr(ZSTR_VAL(hdup) + offset, ZSTR_VAL(ndup), needle_len,
                       ZSTR_VAL(hdup) + ZSTR_LEN(hdup));
    if (found) {
        ZVAL_LONG(ret, (zend_long) (found - ZSTR_VAL(hdup)));
    } else {
        ps_ret_false(ret);
    }
}

static void zif_strrpos(zval *ret, uint32_t argc, zval *argv)
{
    ps_ret_false(ret);   /* php-src returns false from this one on a parameter error */
    PS_ARGC("strrpos", 2, 3);
    zend_string *h;
    if (!zend_parse_arg_string(&argv[0], &h, 1, "strrpos")) return;
    zend_long offset = 0;
    PS_OPT_LONG(offset, 2, "strrpos");
    const char *needle;
    size_t needle_len;
    char scratch[2];
    zend_string *hold;
    const char *p, *e, *hv = ZSTR_VAL(h);
    size_t hlen = ZSTR_LEN(h);

    if (!ps_needle(&argv[1], &needle, &needle_len, scratch, &hold, "strrpos")) {
        ps_ret_false(ret);
        return;
    }
    if (hlen == 0 || needle_len == 0) {
        ps_ret_false(ret);
        return;
    }
    if (offset >= 0) {
        if ((size_t) offset > hlen) {
            zend_error(E_WARNING, "strrpos(): Offset is greater than the length "
                       "of haystack string");
            ps_ret_false(ret);
            return;
        }
        p = hv + offset;
        e = hv + hlen - needle_len;
    } else {
        if (offset < -INT_MAX || (size_t) (-offset) > hlen) {
            zend_error(E_WARNING, "strrpos(): Offset is greater than the length "
                       "of haystack string");
            ps_ret_false(ret);
            return;
        }
        p = hv;
        if ((zend_long) needle_len > -offset) {
            e = hv + hlen - needle_len;
        } else {
            e = hv + (zend_long) hlen + offset;
        }
    }
    while (e >= p) {
        if (memcmp(e, needle, needle_len) == 0) {
            ZVAL_LONG(ret, (zend_long) (e - p) + (offset > 0 ? offset : 0));
            return;
        }
        e--;
    }
    ps_ret_false(ret);
}

static void zif_strripos(zval *ret, uint32_t argc, zval *argv)
{
    ps_ret_false(ret);   /* php-src returns false from this one on a parameter error */
    PS_ARGC("strripos", 2, 3);
    zend_string *h;
    if (!zend_parse_arg_string(&argv[0], &h, 1, "strripos")) return;
    zend_long offset = 0;
    PS_OPT_LONG(offset, 2, "strripos");
    const char *needle;
    size_t needle_len;
    char scratch[2];
    zend_string *hold;
    zend_string *hdup, *ndup;
    const char *p, *e;
    size_t hlen = ZSTR_LEN(h);

    if (!ps_needle(&argv[1], &needle, &needle_len, scratch, &hold, "strripos")) {
        ps_ret_false(ret);
        return;
    }
    if (hlen == 0 || needle_len == 0) {
        ps_ret_false(ret);
        return;
    }
    if (offset >= 0) {
        if ((size_t) offset > hlen) {
            zend_error(E_WARNING, "strripos(): Offset is greater than the length "
                       "of haystack string");
            ps_ret_false(ret);
            return;
        }
    } else if (offset < -INT_MAX || (size_t) (-offset) > hlen) {
        zend_error(E_WARNING, "strripos(): Offset is greater than the length of "
                   "haystack string");
        ps_ret_false(ret);
        return;
    }

    hdup = zend_string_init(ZSTR_VAL(h), hlen);
    ps_tolower_buf(ZSTR_VAL(hdup), hlen);
    ndup = zend_string_init(needle, needle_len);
    ps_tolower_buf(ZSTR_VAL(ndup), needle_len);

    if (needle_len == 1) {
        /* php-src shortcut: the scan window ends at the last byte. */
        if (offset >= 0) {
            p = ZSTR_VAL(hdup) + offset;
            e = ZSTR_VAL(hdup) + hlen - 1;
        } else {
            p = ZSTR_VAL(hdup);
            e = ZSTR_VAL(hdup) + (zend_long) hlen + offset;
        }
    } else if (offset >= 0) {
        p = ZSTR_VAL(hdup) + offset;
        e = ZSTR_VAL(hdup) + hlen - needle_len;
    } else {
        p = ZSTR_VAL(hdup);
        if ((zend_long) needle_len > -offset) {
            e = ZSTR_VAL(hdup) + hlen - needle_len;
        } else {
            e = ZSTR_VAL(hdup) + (zend_long) hlen + offset;
        }
    }

    while (e >= p) {
        if (memcmp(e, ZSTR_VAL(ndup), needle_len) == 0) {
            ZVAL_LONG(ret, (zend_long) (e - p) + (offset > 0 ? offset : 0));
            return;
        }
        e--;
    }
    ps_ret_false(ret);
}

static void ps_strstr_common(zval *ret, uint32_t argc, zval *argv,
                             const char *fname)
{
    if (argc < 2 || argc > 3) {
        zend_wrong_param_count(fname, argc, 2, 3);
        return;
    }
    zend_string *h;
    if (!zend_parse_arg_string(&argv[0], &h, 1, fname)) return;
    int part = 0;
    PS_OPT_BOOL(part, 2, fname);
    const char *needle;
    size_t needle_len;
    char scratch[2];
    zend_string *hold;
    const char *found;

    if (!ps_needle(&argv[1], &needle, &needle_len, scratch, &hold, fname)) {
        ps_ret_false(ret);
        return;
    }

    found = ps_memnstr(ZSTR_VAL(h), needle, needle_len, ZSTR_VAL(h) + ZSTR_LEN(h));
    if (!found) {
        ps_ret_false(ret);
        return;
    }
    if (part) {
        ps_ret_stringl(ret, ZSTR_VAL(h), (size_t) (found - ZSTR_VAL(h)));
    } else {
        ps_ret_stringl(ret, found, ZSTR_LEN(h) - (size_t) (found - ZSTR_VAL(h)));
    }
}

static void zif_strstr(zval *ret, uint32_t argc, zval *argv)
{
    ps_strstr_common(ret, argc, argv, "strstr");
}

static void zif_strchr(zval *ret, uint32_t argc, zval *argv)
{
    ps_strstr_common(ret, argc, argv, "strchr");
}

static void zif_stristr(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("stristr", 2, 3);
    zend_string *h;
    if (!zend_parse_arg_string(&argv[0], &h, 1, "stristr")) return;
    int part = 0;
    PS_OPT_BOOL(part, 2, "stristr");
    const char *needle;
    size_t needle_len;
    char scratch[2];
    zend_string *hold;
    zend_string *hdup, *ndup;
    const char *found;
    size_t off;

    if (!ps_needle(&argv[1], &needle, &needle_len, scratch, &hold, "stristr")) {
        ps_ret_false(ret);
        return;
    }
    if (Z_TYPE(argv[1]) == IS_STRING && needle_len == 0) {
        zend_error(E_WARNING, "stristr(): Empty needle");
        ps_ret_false(ret);
        return;
    }
    hdup = zend_string_init(ZSTR_VAL(h), ZSTR_LEN(h));
    ps_tolower_buf(ZSTR_VAL(hdup), ZSTR_LEN(hdup));
    ndup = zend_string_init(needle, needle_len);
    ps_tolower_buf(ZSTR_VAL(ndup), needle_len);

    found = ps_memnstr(ZSTR_VAL(hdup), ZSTR_VAL(ndup), needle_len,
                       ZSTR_VAL(hdup) + ZSTR_LEN(hdup));
    if (!found) {
        ps_ret_false(ret);
        return;
    }
    off = (size_t) (found - ZSTR_VAL(hdup));
    if (part) {
        ps_ret_stringl(ret, ZSTR_VAL(h), off);
    } else {
        ps_ret_stringl(ret, ZSTR_VAL(h) + off, ZSTR_LEN(h) - off);
    }
}

static void zif_strrchr(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("strrchr", 2, 2);
    zend_string *h;
    if (!zend_parse_arg_string(&argv[0], &h, 1, "strrchr")) return;
    const char *found;
    (void) argc;

    /* Only the first byte of the needle is used. */
    if (Z_TYPE(argv[1]) == IS_STRING) {
        zend_string *n = Z_STR(argv[1]);
        if (ZSTR_LEN(n) == 0) {
            /* php-src reads *Z_STRVAL_P(needle), i.e. the terminator. */
            found = ps_memrchr(ZSTR_VAL(h), '\0', ZSTR_LEN(h));
        } else {
            found = ps_memrchr(ZSTR_VAL(h), (unsigned char) ZSTR_VAL(n)[0],
                               ZSTR_LEN(h));
        }
    } else {
        char c;
        if (!ps_needle_char(&argv[1], &c, "strrchr")) {
            ps_ret_false(ret);
            return;
        }
        found = ps_memrchr(ZSTR_VAL(h), (unsigned char) c, ZSTR_LEN(h));
    }
    if (found) {
        ps_ret_stringl(ret, found, ZSTR_LEN(h) - (size_t) (found - ZSTR_VAL(h)));
    } else {
        ps_ret_false(ret);
    }
}

/* ---------------------------------------------------------------- substr */

/* VERSIONGATE:80000 else-branch is the 5.6/7.x false result: every
 * out-of-range substr() returns "" from 8.0 on. */
static void ps_substr_oob(zval *ret)
{
    if (EG(php_version) >= 80000) {
        ps_ret_empty(ret);
    } else {
        ps_ret_false(ret);
    }
}

static void zif_substr(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("substr", 2, 3);
    zend_string *s;
    if (!zend_parse_arg_string(&argv[0], &s, 1, "substr")) return;
    zend_long f;
    PS_LONG(f, 1, "substr");
    zend_long l;
    zend_long str_len = (zend_long) ZSTR_LEN(s);


        l = str_len;


    if (f > str_len) {
        ps_substr_oob(ret);
        return;
    } else if (f < 0 && -f > str_len) {
        f = 0;
    }
    if (l < 0 && (l + str_len - f) < 0) {
        ps_substr_oob(ret);
        return;
    }
    if (f < 0) {
        f = str_len + f;
        if (f < 0) {
            f = 0;
        }
    }
    if (l < 0) {
        l = (str_len - f) + l;
        if (l < 0) {
            l = 0;
        }
    }
    if (f >= str_len) {
        ps_substr_oob(ret);
        return;
    }
    if ((f + l) > str_len) {
        l = str_len - f;
    }
    ps_ret_stringl(ret, ZSTR_VAL(s) + f, (size_t) l);
}

static void zif_substr_count(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("substr_count", 2, 4);
    zend_string *h;
    if (!zend_parse_arg_string(&argv[0], &h, 1, "substr_count")) return;
    zend_string *n;
    if (!zend_parse_arg_string(&argv[1], &n, 2, "substr_count")) return;
    zend_long offset = 0;
    PS_OPT_LONG(offset, 2, "substr_count");
    zend_long length = 0;
    PS_OPT_LONG(length, 3, "substr_count");
    const char *p, *endp;
    zend_long count = 0;


    p = ZSTR_VAL(h);
    endp = p + ZSTR_LEN(h);

if (offset < 0) {
        zend_error(E_WARNING, "substr_count(): Offset should be greater than or "
                   "equal to 0");
        ps_ret_false(ret);
        return;
    }
    if ((size_t) offset > ZSTR_LEN(h)) {
        zend_error(E_WARNING, "substr_count(): Offset value %lld exceeds "
                   "string length", (long long) offset);
        ps_ret_false(ret);
        return;
    }
    p += offset;


    if (argc >= 4) {

        if (length <= 0) {
            zend_error(E_WARNING, "substr_count(): Length should be greater than 0");
            ps_ret_false(ret);
            return;
        }
        if (length > (zend_long) ZSTR_LEN(h) - offset) {
            zend_error(E_WARNING, "substr_count(): Length value %lld exceeds "
                       "string length", (long long) length);
            ps_ret_false(ret);
            return;
        }
        endp = p + length;
    }

counted:
    while ((p = ps_memnstr(p, ZSTR_VAL(n), ZSTR_LEN(n), endp)) != NULL) {
        p += ZSTR_LEN(n);
        count++;
    }
    ZVAL_LONG(ret, count);
}

/* --------------------------------------------------------- string buffer */

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} ps_buf;

static void ps_buf_init(ps_buf *b)
{
    b->buf = NULL;
    b->len = 0;
    b->cap = 0;
}

static void ps_buf_need(ps_buf *b, size_t extra)
{
    if (b->len + extra + 1 > b->cap) {
        size_t nc = b->cap ? b->cap : 64;
        while (nc < b->len + extra + 1) {
            nc *= 2;
        }
        b->buf = (char *) realloc(b->buf, nc);
        b->cap = nc;
    }
}

static void ps_buf_append(ps_buf *b, const char *s, size_t n)
{
    if (n == 0) {
        return;
    }
    ps_buf_need(b, n);
    memcpy(b->buf + b->len, s, n);
    b->len += n;
}

static void ps_buf_char(ps_buf *b, char c)
{
    ps_buf_need(b, 1);
    b->buf[b->len++] = c;
}

static zend_string *ps_buf_finish(ps_buf *b)
{
    zend_string *s = zend_string_init(b->buf ? b->buf : "", b->len);
    free(b->buf);
    ps_buf_init(b);
    return s;
}

/* --------------------------------------------------------- substr_replace */

static zend_string *ps_substr_replace_one(const zend_string *str, const char *repl,
                                          size_t repl_len, zend_long f, zend_long l)
{
    zend_long slen = (zend_long) ZSTR_LEN(str);
    zend_string *out;
    size_t out_len;

    if (f < 0) {
        f = slen + f;
        if (f < 0) {
            f = 0;
        }
    } else if (f > slen) {
        f = slen;
    }
    if (l < 0) {
        l = (slen - f) + l;
        if (l < 0) {
            l = 0;
        }
    }
    if (l > slen) {
        l = slen;
    }
    if (f > slen - l) {
        l = slen - f;
    }

    out_len = (size_t) (slen - l) + repl_len;
    out = zend_string_alloc(out_len);
    memcpy(ZSTR_VAL(out), ZSTR_VAL(str), (size_t) f);
    if (repl_len) {
        memcpy(ZSTR_VAL(out) + f, repl, repl_len);
    }
    memcpy(ZSTR_VAL(out) + f + repl_len, ZSTR_VAL(str) + f + l,
           (size_t) (slen - f - l));
    ZSTR_VAL(out)[out_len] = '\0';
    return out;
}

static void zif_substr_replace(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("substr_replace", 3, 4);
    zval *zstr = &argv[0];
    zval *zrepl = &argv[1];
    zval *zfrom = &argv[2];
    zval *zlen = argc > 3 ? &argv[3] : NULL;

    if (Z_TYPE_P(zstr) != IS_ARRAY) {
        zend_string *str = zval_get_string(zstr);
        zend_long f, l;
        const char *repl = "";
        size_t repl_len = 0;
        zend_string *repl_s = NULL;

        if ((argc == 3 && Z_TYPE_P(zfrom) == IS_ARRAY)
            || (argc >= 4 && ((Z_TYPE_P(zfrom) == IS_ARRAY) != (Z_TYPE_P(zlen) == IS_ARRAY)))) {
            zend_error(E_WARNING, "substr_replace(): 'from' and 'len' should be "
                       "of same type - numerical or array ");
            ps_ret_zstr_copy(ret, str);
            return;
        }
        if (argc >= 4 && Z_TYPE_P(zfrom) == IS_ARRAY) {
            if (zend_hash_num_elements(Z_ARR_P(zfrom))
                != zend_hash_num_elements(Z_ARR_P(zlen))) {
                zend_error(E_WARNING, "substr_replace(): 'from' and 'len' should "
                           "have the same number of elements");
                ps_ret_zstr_copy(ret, str);
                return;
            }
        }
        if (Z_TYPE_P(zfrom) == IS_ARRAY) {
            zend_error(E_WARNING, "substr_replace(): Functionality of 'from' and "
                       "'len' as arrays is not implemented");
            ps_ret_zstr_copy(ret, str);
            return;
        }

        f = zval_get_long(zfrom);
        if (zlen && Z_TYPE_P(zlen) != IS_ARRAY) {
            l = zval_get_long(zlen);
        } else {
            l = (zend_long) ZSTR_LEN(str);
        }

        if (Z_TYPE_P(zrepl) == IS_ARRAY) {
            ZEND_HASH_FOREACH(Z_ARR_P(zrepl), b) {
                repl_s = zval_get_string(zval_deref(&b->val));
                break;
            } ZEND_HASH_FOREACH_END();
        } else {
            repl_s = zval_get_string(zrepl);
        }
        if (repl_s) {
            repl = ZSTR_VAL(repl_s);
            repl_len = ZSTR_LEN(repl_s);
        }
        ZVAL_STR(ret, ps_substr_replace_one(str, repl, repl_len, f, l));
        return;
    }

    /* str is an array of strings; from/len/repl may be arrays consumed in
     * parallel (positionally). */
    {
        HashTable *out = zend_hash_new();
        HashTable *hfrom = Z_TYPE_P(zfrom) == IS_ARRAY ? Z_ARR_P(zfrom) : NULL;
        HashTable *hlen = (zlen && Z_TYPE_P(zlen) == IS_ARRAY) ? Z_ARR_P(zlen) : NULL;
        HashTable *hrepl = Z_TYPE_P(zrepl) == IS_ARRAY ? Z_ARR_P(zrepl) : NULL;
        uint32_t ifrom = 0, ilen = 0, irepl = 0;
        zend_string *flat_repl = hrepl ? NULL : zval_get_string(zrepl);

        ZEND_HASH_FOREACH(Z_ARR_P(zstr), b) {
            zend_string *orig = zval_get_string(zval_deref(&b->val));
            zend_long f, l;
            const char *repl = "";
            size_t repl_len = 0;
            zend_string *repl_s = NULL;
            zend_string *res;
            zval tmp;

            if (hfrom) {
                zval *e = NULL;
                uint32_t seen = 0;
                ZEND_HASH_FOREACH(hfrom, fb) {
                    if (seen++ == ifrom) { e = &fb->val; break; }
                } ZEND_HASH_FOREACH_END();
                f = e ? zval_get_long(zval_deref(e)) : 0;
                ifrom++;
            } else {
                f = zval_get_long(zfrom);
            }
            if (f < 0) {
                f = (zend_long) ZSTR_LEN(orig) + f;
                if (f < 0) {
                    f = 0;
                }
            } else if (f > (zend_long) ZSTR_LEN(orig)) {
                f = (zend_long) ZSTR_LEN(orig);
            }

            if (hlen) {
                zval *e = NULL;
                uint32_t seen = 0;
                ZEND_HASH_FOREACH(hlen, lb) {
                    if (seen++ == ilen) { e = &lb->val; break; }
                } ZEND_HASH_FOREACH_END();
                l = e ? zval_get_long(zval_deref(e)) : (zend_long) ZSTR_LEN(orig);
                ilen++;
            } else if (zlen) {
                l = zval_get_long(zlen);
            } else {
                l = (zend_long) ZSTR_LEN(orig);
            }
            if (l < 0) {
                l = ((zend_long) ZSTR_LEN(orig) - f) + l;
                if (l < 0) {
                    l = 0;
                }
            }
            if (f + l > (zend_long) ZSTR_LEN(orig)) {
                l = (zend_long) ZSTR_LEN(orig) - f;
            }

            if (hrepl) {
                zval *e = NULL;
                uint32_t seen = 0;
                ZEND_HASH_FOREACH(hrepl, rb) {
                    if (seen++ == irepl) { e = &rb->val; break; }
                } ZEND_HASH_FOREACH_END();
                if (e) {
                    repl_s = zval_get_string(zval_deref(e));
                }
                irepl++;
            } else {
                repl_s = flat_repl;
            }
            if (repl_s) {
                repl = ZSTR_VAL(repl_s);
                repl_len = ZSTR_LEN(repl_s);
            }

            res = zend_string_alloc((size_t) ((zend_long) ZSTR_LEN(orig) - l) + repl_len);
            memcpy(ZSTR_VAL(res), ZSTR_VAL(orig), (size_t) f);
            memcpy(ZSTR_VAL(res) + f, repl, repl_len);
            memcpy(ZSTR_VAL(res) + f + repl_len, ZSTR_VAL(orig) + f + l,
                   (size_t) ((zend_long) ZSTR_LEN(orig) - f - l));
            ZSTR_VAL(res)[ZSTR_LEN(res)] = '\0';

            ZVAL_STR(&tmp, res);
            if (b->key) {
                zend_hash_update(out, zend_string_init(ZSTR_VAL(b->key),
                                                       ZSTR_LEN(b->key)), tmp);
            } else {
                zend_hash_index_update(out, (zend_long) b->h, tmp);
            }
        } ZEND_HASH_FOREACH_END();

        ZVAL_ARR(ret, out);
    }
}

/* ----------------------------------------------------------- str_replace */

/* One search/replace pass over a flat string. */
static zend_string *ps_replace_pass(const char *hay, size_t hlen,
                                    const char *needle, size_t nlen,
                                    const char *repl, size_t rlen,
                                    int case_sensitive, zend_long *count)
{
    ps_buf b;
    const char *scan_hay = hay;
    char *lc_hay = NULL, *lc_needle = NULL;
    size_t pos = 0;

    if (nlen == 0 || nlen > hlen) {
        return zend_string_init(hay, hlen);
    }
    if (!case_sensitive) {
        size_t i;
        lc_hay = (char *) malloc(hlen + 1);
        lc_needle = (char *) malloc(nlen + 1);
        for (i = 0; i < hlen; i++) {
            lc_hay[i] = ps_lower(hay[i]);
        }
        lc_hay[hlen] = '\0';
        for (i = 0; i < nlen; i++) {
            lc_needle[i] = ps_lower(needle[i]);
        }
        lc_needle[nlen] = '\0';
        scan_hay = lc_hay;
        needle = lc_needle;
    }

    ps_buf_init(&b);
    while (pos + nlen <= hlen) {
        const char *found = ps_memnstr(scan_hay + pos, needle, nlen,
                                       scan_hay + hlen);
        size_t off;
        if (!found) {
            break;
        }
        off = (size_t) (found - scan_hay);
        ps_buf_append(&b, hay + pos, off - pos);
        ps_buf_append(&b, repl, rlen);
        pos = off + nlen;
        if (count) {
            (*count)++;
        }
    }
    ps_buf_append(&b, hay + pos, hlen - pos);

    free(lc_hay);
    free(lc_needle);
    return ps_buf_finish(&b);
}

static zend_string *ps_replace_in_subject(zval *search, zval *replace,
                                          zval *subject, int case_sensitive,
                                          zend_long *count)
{
    zend_string *subj = zval_get_string(subject);
    zend_string *result;

    if (ZSTR_LEN(subj) == 0) {
        return zend_string_init("", 0);
    }
    if (Z_TYPE_P(search) != IS_ARRAY) {
        zend_string *s = zval_get_string(search);
        zend_string *r = zval_get_string(replace);
        if (ZSTR_LEN(s) == 0) {
            return zend_string_init(ZSTR_VAL(subj), ZSTR_LEN(subj));
        }
        return ps_replace_pass(ZSTR_VAL(subj), ZSTR_LEN(subj), ZSTR_VAL(s),
                               ZSTR_LEN(s), ZSTR_VAL(r), ZSTR_LEN(r),
                               case_sensitive, count);
    }

    result = zend_string_init(ZSTR_VAL(subj), ZSTR_LEN(subj));
    {
        HashTable *hrepl = Z_TYPE_P(replace) == IS_ARRAY ? Z_ARR_P(replace) : NULL;
        zend_string *flat_repl = hrepl ? NULL : zval_get_string(replace);
        uint32_t irepl = 0;

        ZEND_HASH_FOREACH(Z_ARR_P(search), sb) {
            zend_string *s = zval_get_string(zval_deref(&sb->val));
            const char *rv = "";
            size_t rl = 0;
            zend_string *r = NULL;

            if (hrepl) {
                zval *e = NULL;
                uint32_t seen = 0;
                ZEND_HASH_FOREACH(hrepl, rb) {
                    if (seen++ == irepl) { e = &rb->val; break; }
                } ZEND_HASH_FOREACH_END();
                irepl++;
                if (e) {
                    r = zval_get_string(zval_deref(e));
                }
            } else {
                r = flat_repl;
            }
            if (ZSTR_LEN(s) == 0) {
                continue;
            }
            if (r) {
                rv = ZSTR_VAL(r);
                rl = ZSTR_LEN(r);
            }
            result = ps_replace_pass(ZSTR_VAL(result), ZSTR_LEN(result),
                                     ZSTR_VAL(s), ZSTR_LEN(s), rv, rl,
                                     case_sensitive, count);
            if (ZSTR_LEN(result) == 0) {
                break;
            }
        } ZEND_HASH_FOREACH_END();
    }
    return result;
}

static void ps_str_replace_common(zval *ret, uint32_t argc, zval *argv,
                                  int case_sensitive)
{
    zval *search = &argv[0];
    zval *replace = &argv[1];
    zval *subject = &argv[2];
    zend_long count = 0;


        ZVAL_STR(ret, ps_replace_in_subject(search, replace, subject,
                                            case_sensitive,
                                            argc > 3 ? &count : NULL));


    if (argc > 3) {
        zval *slot = ps_ref(&argv[3]);
        ZVAL_LONG(slot, count);
    }
}

static void zif_str_replace(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("str_replace", 3, 4);
    ps_str_replace_common(ret, argc, argv, 1);
}

static void zif_str_ireplace(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("str_ireplace", 3, 4);
    ps_str_replace_common(ret, argc, argv, 0);
}

/* --------------------------------------------------------------- str_pad */

static void zif_str_pad(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("str_pad", 2, 4);
    zend_string *input;
    if (!zend_parse_arg_string(&argv[0], &input, 1, "str_pad")) return;
    zend_long pad_length;
    PS_LONG(pad_length, 1, "str_pad");
    const char *pad = " ";
    size_t pad_len = 1;
    zend_string *pad_s = NULL;
    zend_long pad_type = STR_PAD_RIGHT;
    size_t num_pad_chars, left_pad = 0, right_pad = 0, i, out_len = 0;
    zend_string *out;

    if (argc > 2) {
        if (!zend_parse_arg_string(&argv[2], &pad_s, 3, "str_pad")) return;
        pad = ZSTR_VAL(pad_s);
        pad_len = ZSTR_LEN(pad_s);
    }
    if (argc > 3) {
        PS_LONG(pad_type, 3, "str_pad");
    }

    if (pad_length <= 0 || (size_t) pad_length <= ZSTR_LEN(input)) {
        ps_ret_zstr_copy(ret, input);
        return;
    }



    num_pad_chars = (size_t) pad_length - ZSTR_LEN(input);
    switch (pad_type) {
    case STR_PAD_RIGHT:
        right_pad = num_pad_chars;
        break;
    case STR_PAD_LEFT:
        left_pad = num_pad_chars;
        break;
    case STR_PAD_BOTH:
        left_pad = num_pad_chars / 2;
        right_pad = num_pad_chars - left_pad;
        break;
    }

    out = zend_string_alloc(ZSTR_LEN(input) + num_pad_chars);
    for (i = 0; i < left_pad; i++) {
        ZSTR_VAL(out)[out_len++] = pad[i % pad_len];
    }
    memcpy(ZSTR_VAL(out) + out_len, ZSTR_VAL(input), ZSTR_LEN(input));
    out_len += ZSTR_LEN(input);
    for (i = 0; i < right_pad; i++) {
        ZSTR_VAL(out)[out_len++] = pad[i % pad_len];
    }
    ZSTR_VAL(out)[out_len] = '\0';
    ZVAL_STR(ret, out);
}

/* ------------------------------------------------------------- str_split */

static void zif_str_split(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("str_split", 1, 2);
    zend_string *s;
    if (!zend_parse_arg_string(&argv[0], &s, 1, "str_split")) return;
    zend_long split_length = 1;
    PS_OPT_LONG(split_length, 1, "str_split");
    HashTable *ht;
    size_t p = 0;
    zval tmp;


    ht = zend_hash_new();
    if ((size_t) split_length >= ZSTR_LEN(s)) {
        ZVAL_STR(&tmp, zend_string_init(ZSTR_VAL(s), ZSTR_LEN(s)));
        zend_hash_next_index_insert(ht, tmp);
        ZVAL_ARR(ret, ht);
        return;
    }
    while (p < ZSTR_LEN(s)) {
        size_t n = ZSTR_LEN(s) - p;
        if (n > (size_t) split_length) {
            n = (size_t) split_length;
        }
        ZVAL_STR(&tmp, zend_string_init(ZSTR_VAL(s) + p, n));
        zend_hash_next_index_insert(ht, tmp);
        p += n;
    }
    ZVAL_ARR(ret, ht);
}

/* --------------------------------------------------------------- explode */

static void zif_explode(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("explode", 2, 3);
    zend_string *delim;
    if (!zend_parse_arg_string(&argv[0], &delim, 1, "explode")) return;
    zend_string *str;
    if (!zend_parse_arg_string(&argv[1], &str, 2, "explode")) return;
    zend_long limit = ZEND_LONG_MAX;
    PS_OPT_LONG(limit, 2, "explode");
    HashTable *ht;
    zval tmp;


    ht = zend_hash_new();
    if (ZSTR_LEN(str) == 0) {
        if (limit >= 0) {
            ZVAL_STR(&tmp, zend_string_init("", 0));
            zend_hash_next_index_insert(ht, tmp);
        }
        ZVAL_ARR(ret, ht);
        return;
    }

    if (limit > 1) {
        const char *p1 = ZSTR_VAL(str);
        const char *endp = ZSTR_VAL(str) + ZSTR_LEN(str);
        const char *p2 = ps_memnstr(p1, ZSTR_VAL(delim), ZSTR_LEN(delim), endp);
        if (p2 == NULL) {
            ZVAL_STR(&tmp, zend_string_init(ZSTR_VAL(str), ZSTR_LEN(str)));
            zend_hash_next_index_insert(ht, tmp);
        } else {
            do {
                ZVAL_STR(&tmp, zend_string_init(p1, (size_t) (p2 - p1)));
                zend_hash_next_index_insert(ht, tmp);
                p1 = p2 + ZSTR_LEN(delim);
            } while ((p2 = ps_memnstr(p1, ZSTR_VAL(delim), ZSTR_LEN(delim), endp))
                     != NULL && --limit > 1);
            if (p1 <= endp) {
                ZVAL_STR(&tmp, zend_string_init(p1, (size_t) (endp - p1)));
                zend_hash_next_index_insert(ht, tmp);
            }
        }
    } else if (limit < 0) {
        const char *endp = ZSTR_VAL(str) + ZSTR_LEN(str);
        const char **positions;
        size_t allocated = 64, found = 0;
        const char *p1 = ZSTR_VAL(str);
        const char *p2 = ps_memnstr(p1, ZSTR_VAL(delim), ZSTR_LEN(delim), endp);

        if (p2 != NULL) {
            zend_long i, to_return;
            positions = (const char **) malloc(allocated * sizeof(char *));
            positions[found++] = p1;
            do {
                if (found >= allocated) {
                    allocated = found + 64;
                    positions = (const char **) realloc(positions,
                                                        allocated * sizeof(char *));
                }
                p1 = p2 + ZSTR_LEN(delim);
                positions[found++] = p1;
            } while ((p2 = ps_memnstr(p1, ZSTR_VAL(delim), ZSTR_LEN(delim), endp))
                     != NULL);

            to_return = limit + (zend_long) found;
            for (i = 0; i < to_return; i++) {
                size_t n = (size_t) ((positions[i + 1] - (zend_long) ZSTR_LEN(delim))
                                     - positions[i]);
                ZVAL_STR(&tmp, zend_string_init(positions[i], n));
                zend_hash_next_index_insert(ht, tmp);
            }
            free(positions);
        }
    } else {
        /* limit == 0 behaves like limit == 1 */
        ZVAL_STR(&tmp, zend_string_init(ZSTR_VAL(str), ZSTR_LEN(str)));
        zend_hash_index_update(ht, 0, tmp);
    }
    ZVAL_ARR(ret, ht);
}

/* --------------------------------------------------------------- implode */

static void ps_implode(zval *ret, zval *arr, const char *delim, size_t delim_len)
{
    ps_buf b;
    uint32_t n = zend_hash_num_elements(Z_ARR_P(arr));
    uint32_t i = 0;

    if (n == 0) {
        ps_ret_empty(ret);
        return;
    }
    ps_buf_init(&b);
    ZEND_HASH_FOREACH(Z_ARR_P(arr), bk) {
        zend_string *s = zval_get_string(zval_deref(&bk->val));
        ps_buf_append(&b, ZSTR_VAL(s), ZSTR_LEN(s));
        if (++i != n) {
            ps_buf_append(&b, delim, delim_len);
        }
    } ZEND_HASH_FOREACH_END();
    ZVAL_STR(ret, ps_buf_finish(&b));
}

static void ps_implode_common(zval *ret, uint32_t argc, zval *argv,
                              const char *fname)
{
    if (argc < 1 || argc > 2) {
        zend_wrong_param_count(fname, argc, 1, 2);
        return;
    }
    if (argc == 1) {

        ps_implode(ret, &argv[0], "", 0);
        return;
    }

    if (Z_TYPE(argv[1]) == IS_ARRAY) {
        zend_string *d = zval_get_string(&argv[0]);
        ps_implode(ret, &argv[1], ZSTR_VAL(d), ZSTR_LEN(d));
        return;
    }
    zend_error(E_WARNING, "%s(): Invalid arguments passed", fname);
}

static void zif_implode(zval *ret, uint32_t argc, zval *argv)
{
    ps_implode_common(ret, argc, argv, "implode");
}

static void zif_join(zval *ret, uint32_t argc, zval *argv)
{
    ps_implode_common(ret, argc, argv, "join");
}

/* ------------------------------------------------------- printf family --
 * The formatting engine itself lives in php_sprintf() (another module).
 */

/* main/php_printf.c owns the formatting engine; the *_named entry point is
 * declared here because this file may include only the frozen headers. */
extern zend_string *php_sprintf_named(const char *fname, const char *fmt,
                                      size_t fmtlen, uint32_t argc, zval *argv,
                                      int *err);

static void ps_format(zval *ret, const zval *fmt_zv, uint32_t argc, zval *argv,
                      int write_out, const char *fname)
{
    zend_string *fmt = zval_get_string(fmt_zv);
    int err = 0;
    zend_string *out;

    out = php_sprintf_named(fname, ZSTR_VAL(fmt), ZSTR_LEN(fmt), argc, argv, &err);
    if (err || out == NULL) {
        ps_ret_false(ret);
        return;
    }
    if (write_out) {
        php_write_str(out);
        ZVAL_LONG(ret, (zend_long) ZSTR_LEN(out));
    } else {
        ZVAL_STR(ret, out);
    }
}

/* Flatten an args array into a contiguous zval vector for php_sprintf(). */
static zval *ps_flatten_args(zval *arr, uint32_t *out_n)
{
    uint32_t n = zend_hash_num_elements(Z_ARR_P(arr));
    zval *v;
    uint32_t i = 0;

    *out_n = n;
    if (n == 0) {
        return NULL;
    }
    v = (zval *) calloc(n, sizeof(zval));
    ZEND_HASH_FOREACH(Z_ARR_P(arr), b) {
        v[i++] = *zval_deref(&b->val);
    } ZEND_HASH_FOREACH_END();
    return v;
}

static void zif_sprintf(zval *ret, uint32_t argc, zval *argv)
{
    ps_ret_false(ret);   /* php-src returns false from this one on a parameter error */
    PS_ARGC("sprintf", 1, UINT32_MAX);
    ps_format(ret, &argv[0], argc - 1, argv + 1, 0, "sprintf");
}

static void zif_printf(zval *ret, uint32_t argc, zval *argv)
{
    ps_ret_false(ret);   /* php-src returns false from this one on a parameter error */
    PS_ARGC("printf", 1, UINT32_MAX);
    ps_format(ret, &argv[0], argc - 1, argv + 1, 1, "printf");
}

/* php_formatted_print() runs convert_to_array() over $args, so a scalar becomes
 * a one-element list and null becomes the empty list. */
static zval *ps_args_as_vector(zval *arg, uint32_t *out_n)
{
    if (Z_TYPE_P(arg) == IS_ARRAY) {
        return ps_flatten_args(arg, out_n);
    }
    if (Z_TYPE_P(arg) == IS_NULL) {
        *out_n = 0;
        return NULL;
    }
    *out_n = 1;
    {
        zval *v = (zval *) calloc(1, sizeof(zval));
        v[0] = *arg;
        return v;
    }
}

static void zif_vsprintf(zval *ret, uint32_t argc, zval *argv)
{
    ps_ret_false(ret);   /* php-src returns false from this one on a parameter error */
    uint32_t n = 0;
    zval *v;

    if (argc == 0) {
        zend_wrong_param_count("vsprintf", argc, 1, UINT32_MAX);
        return;
    }
    if (argc != 2) {
        ps_wrong_count("vsprintf", argc, 2);
        return;
    }
    /* VERSIONGATE:80000 silently coercing $values to an array is the 5.6
     * branch; 8.0 raises a TypeError for a non-array $values. */
    v = ps_args_as_vector(&argv[1], &n);
    ps_format(ret, &argv[0], n, v, 0, "vsprintf");
    free(v);
}

static void zif_vprintf(zval *ret, uint32_t argc, zval *argv)
{
    ps_ret_false(ret);   /* php-src returns false from this one on a parameter error */
    uint32_t n = 0;
    zval *v;

    if (argc == 0) {
        zend_wrong_param_count("vprintf", argc, 1, UINT32_MAX);
        return;
    }
    if (argc != 2) {
        ps_wrong_count("vprintf", argc, 2);
        return;
    }
    /* VERSIONGATE:80000 silently coercing $values to an array is the 5.6
     * branch; 8.0 raises a TypeError for a non-array $values. */
    v = ps_args_as_vector(&argv[1], &n);
    ps_format(ret, &argv[0], n, v, 1, "vprintf");
    free(v);
}

/* --------------------------------------------------------- number_format */

static double ps_intpow10(int power)
{
    static const double powers[] = {
        1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,
        1e8,  1e9,  1e10, 1e11, 1e12, 1e13, 1e14, 1e15,
        1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22
    };
    if (power < 0 || power > 22) {
        return pow(10.0, (double) power);
    }
    return powers[power];
}

static int ps_intlog10abs(double value)
{
    int result;
    value = fabs(value);

    if (value < 1e-8 || value > 1e22) {
        result = (int) floor(log10(value));
    } else {
        static const double values[] = {
            1e-8, 1e-7, 1e-6, 1e-5, 1e-4, 1e-3, 1e-2, 1e-1,
            1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,
            1e8,  1e9,  1e10, 1e11, 1e12, 1e13, 1e14, 1e15,
            1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22
        };
        result = 15;
        result += (value < values[result]) ? -8 : 8;
        result += (value < values[result]) ? -4 : 4;
        result += (value < values[result]) ? -2 : 2;
        result += (value < values[result]) ? -1 : 1;
        if (value < values[result]) {
            result -= 1;
        }
        result -= 8;
    }
    return result;
}

/* PHP_ROUND_HALF_UP only -- that is all number_format() needs. */
static double ps_round_helper(double value)
{
    return value >= 0.0 ? floor(value + 0.5) : ceil(value - 0.5);
}

static double ps_math_round(double value, int places)
{
    double f1, f2, tmp_value;
    int precision_places;

    if (!isfinite(value)) {
        return value;
    }
    precision_places = 14 - ps_intlog10abs(value);
    f1 = ps_intpow10(abs(places));

    if (precision_places > places && precision_places - places < 15) {
        int use_precision = precision_places;
        f2 = ps_intpow10(abs(use_precision));
        tmp_value = (use_precision >= 0) ? value * f2 : value / f2;
        tmp_value = ps_round_helper(tmp_value);
        use_precision = places - precision_places;
        f2 = ps_intpow10(abs(use_precision));
        tmp_value = tmp_value / f2;
    } else {
        tmp_value = (places >= 0) ? value * f1 : value / f1;
        if (fabs(tmp_value) >= 1e15) {
            return value;
        }
    }

    tmp_value = ps_round_helper(tmp_value);

    if (abs(places) < 23) {
        tmp_value = (places > 0) ? tmp_value / f1 : tmp_value * f1;
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "%15fe%d", tmp_value, -places);
        tmp_value = strtod(buf, NULL);
        if (!isfinite(tmp_value)) {
            tmp_value = value;
        }
    }
    return tmp_value;
}

static zend_string *ps_number_format(double d, int dec, const char *dec_point,
                                     size_t dec_point_len,
                                     const char *thousand_sep,
                                     size_t thousand_sep_len)
{
    char *tmpbuf;
    int tmplen;
    char *resbuf;
    const char *dp;
    const char *s;
    char *t;
    size_t integral, reslen;
    int is_negative = 0, count = 0;
    zend_string *out;

    if (d < 0) {
        is_negative = 1;
        d = -d;
    }
    if (dec < 0) {
        dec = 0;
    }
    d = ps_math_round(d, dec);

    tmplen = snprintf(NULL, 0, "%.*f", dec, d);
    tmpbuf = (char *) malloc((size_t) tmplen + 1);
    snprintf(tmpbuf, (size_t) tmplen + 1, "%.*f", dec, d);

    if (!isdigit((unsigned char) tmpbuf[0])) {   /* inf / nan */
        out = zend_string_init(tmpbuf, (size_t) tmplen);
        free(tmpbuf);
        return out;
    }

    dp = dec ? strpbrk(tmpbuf, ".,") : NULL;
    integral = dp ? (size_t) (dp - tmpbuf) : (size_t) tmplen;
    if (thousand_sep_len) {
        integral += thousand_sep_len * ((integral - 1) / 3);
    }
    reslen = integral;
    if (dec) {
        reslen += (size_t) dec;
        reslen += dec_point_len;
    }
    if (is_negative) {
        reslen++;
    }

    resbuf = (char *) malloc(reslen + 1);
    s = tmpbuf + tmplen - 1;
    t = resbuf + reslen;
    *t-- = '\0';

    if (dec) {
        int declen = dp ? (int) (s - dp) : 0;
        int topad = dec > declen ? dec - declen : 0;

        while (topad--) {
            *t-- = '0';
        }
        if (dp) {
            s -= declen + 1;
            t -= declen;
            memcpy(t + 1, dp + 1, (size_t) declen);
        }
        if (dec_point_len) {
            t -= dec_point_len;
            memcpy(t + 1, dec_point, dec_point_len);
        }
    }

    while (s >= tmpbuf) {
        *t-- = *s--;
        if (thousand_sep_len && (++count % 3) == 0 && s >= tmpbuf) {
            t -= thousand_sep_len;
            memcpy(t + 1, thousand_sep, thousand_sep_len);
        }
    }
    if (is_negative) {
        *t-- = '-';
    }

    out = zend_string_init(resbuf, reslen);
    free(resbuf);
    free(tmpbuf);
    return out;
}

static void zif_number_format(zval *ret, uint32_t argc, zval *argv)
{
    double num = 0.0;
    zend_long dec = 0;
    const char *dec_point = ".";
    size_t dec_point_len = 1;
    const char *thousand_sep = ",";
    size_t thousand_sep_len = 1;
    zend_string *dp_s = NULL, *ts_s = NULL;

    PS_ARGC("number_format", 1, 4);
    if (!ps_arg_double(&argv[0], &num, 1, "number_format")) {
        return;
    }
    switch (argc) {
    case 1:
        break;
    case 2:
        PS_LONG(dec, 1, "number_format");
        break;
    case 4:
        PS_LONG(dec, 1, "number_format");
        if (Z_TYPE(argv[2]) != IS_NULL) {
            dp_s = zval_get_string(&argv[2]);
            dec_point = ZSTR_VAL(dp_s);
            dec_point_len = ZSTR_LEN(dp_s);
        }
        if (Z_TYPE(argv[3]) != IS_NULL) {
            ts_s = zval_get_string(&argv[3]);
            thousand_sep = ZSTR_VAL(ts_s);
            thousand_sep_len = ZSTR_LEN(ts_s);
        }
        break;
    /* VERSIONGATE:80000 rejecting a 3-argument call below is the 5.6 branch;
     * 8.0 accepts number_format($num, $dec, $dec_point). */
    default:
        ps_wrong_count("number_format", argc, 4);
        return;
    }
    ZVAL_STR(ret, ps_number_format(num, (int) dec, dec_point, dec_point_len,
                                   thousand_sep, thousand_sep_len));
}

/* ----------------------------------------------------------------- nl2br */

static void zif_nl2br(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("nl2br", 1, 2);
    zend_string *s;
    if (!zend_parse_arg_string(&argv[0], &s, 1, "nl2br")) return;
    int is_xhtml = 1;
    PS_OPT_BOOL(is_xhtml, 1, "nl2br");
    const char *p = ZSTR_VAL(s), *end = p + ZSTR_LEN(s);
    ps_buf b;
    int repl_cnt = 0;
    const char *tmp = p;

    while (tmp < end) {
        if (*tmp == '\r') {
            if (tmp + 1 < end && *(tmp + 1) == '\n') {
                tmp++;
            }
            repl_cnt++;
        } else if (*tmp == '\n') {
            if (tmp + 1 < end && *(tmp + 1) == '\r') {
                tmp++;
            }
            repl_cnt++;
        }
        tmp++;
    }
    if (repl_cnt == 0) {
        ps_ret_zstr_copy(ret, s);
        return;
    }

    ps_buf_init(&b);
    while (p < end) {
        if (*p == '\r' || *p == '\n') {
            ps_buf_append(&b, is_xhtml ? "<br />" : "<br>", is_xhtml ? 6 : 4);
            if (p + 1 < end
                && ((*p == '\r' && *(p + 1) == '\n')
                    || (*p == '\n' && *(p + 1) == '\r'))) {
                ps_buf_char(&b, *p++);
            }
        }
        ps_buf_char(&b, *p);
        p++;
    }
    ZVAL_STR(ret, ps_buf_finish(&b));
}

/* ------------------------------------------------------- addslashes etc. */

static void zif_addslashes(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("addslashes", 1, 1);
    zend_string *s;
    if (!zend_parse_arg_string(&argv[0], &s, 1, "addslashes")) return;
    ps_buf b;
    size_t i;
    (void) argc;

    if (ZSTR_LEN(s) == 0) {
        ps_ret_empty(ret);
        return;
    }
    ps_buf_init(&b);
    for (i = 0; i < ZSTR_LEN(s); i++) {
        char c = ZSTR_VAL(s)[i];
        switch (c) {
        case '\0':
            ps_buf_char(&b, '\\');
            ps_buf_char(&b, '0');
            break;
        case '\'':
        case '"':
        case '\\':
            ps_buf_char(&b, '\\');
            /* fall through */
        default:
            ps_buf_char(&b, c);
            break;
        }
    }
    ZVAL_STR(ret, ps_buf_finish(&b));
}

static void zif_stripslashes(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("stripslashes", 1, 1);
    zend_string *s;
    if (!zend_parse_arg_string(&argv[0], &s, 1, "stripslashes")) return;
    zend_string *out = zend_string_init(ZSTR_VAL(s), ZSTR_LEN(s));
    char *t = ZSTR_VAL(out);
    char *w = ZSTR_VAL(out);
    size_t l = ZSTR_LEN(out);
    size_t newlen = ZSTR_LEN(out);
    (void) argc;

    while (l > 0) {
        if (*t == '\\') {
            t++;
            newlen--;
            l--;
            if (l > 0) {
                if (*t == '0') {
                    *w++ = '\0';
                    t++;
                } else {
                    *w++ = *t++;
                }
                l--;
            }
        } else {
            *w++ = *t++;
            l--;
        }
    }
    ps_ret_stringl(ret, ZSTR_VAL(out), newlen);
}

/* ------------------------------------------------------------- chr / ord */

static void zif_chr(zval *ret, uint32_t argc, zval *argv)
{
    zend_long c;
    char t[2];

    if (argc != 1) {
        ps_wrong_count("chr", argc, 1);
        return;
    }
    /* VERSIONGATE:80000 coercing any argument to int (0 when it is not
     * numeric) is the 5.6 branch; 8.0 raises a TypeError instead.  The
     * truncating cast itself matches every release. */
    c = zval_get_long(&argv[0]);
    t[0] = (char) c;
    t[1] = '\0';
    ps_ret_stringl(ret, t, 1);
}

static void zif_ord(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("ord", 1, 1);
    zend_string *s;
    if (!zend_parse_arg_string(&argv[0], &s, 1, "ord")) return;
    (void) argc;
    /* php-src reads str[0] unconditionally; the NUL terminator gives 0. */
    ZVAL_LONG(ret, (zend_long) (unsigned char) ZSTR_VAL(s)[0]);
}

/* --------------------------------------------- str_contains family (8.0) */







/* -------------------------------------------------------------- wordwrap */

static void zif_wordwrap(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("wordwrap", 1, 4);
    zend_string *text;
    if (!zend_parse_arg_string(&argv[0], &text, 1, "wordwrap")) return;
    zend_long linelength = 75;
    PS_OPT_LONG(linelength, 1, "wordwrap");
    const char *breakchar = "\n";
    size_t breakcharlen = 1;
    zend_string *brk_s = NULL;
    int docut = 0;
    PS_OPT_BOOL(docut, 3, "wordwrap");
    size_t textlen = ZSTR_LEN(text);
    const char *t = ZSTR_VAL(text);

    if (argc > 2) {
        if (!zend_parse_arg_string(&argv[2], &brk_s, 3, "wordwrap")) return;
        breakchar = ZSTR_VAL(brk_s);
        breakcharlen = ZSTR_LEN(brk_s);
    }

    if (textlen == 0) {
        ps_ret_empty(ret);
        return;
    }

    if (linelength == 0 && docut) {
        zend_error(E_WARNING, "wordwrap(): Can't force cut when width is zero");
        ps_ret_false(ret);
        return;
    }
    if (linelength < 0) {
        linelength = 0; /* BC */
    }

    if (breakcharlen == 1 && !docut) {
        zend_string *out = zend_string_init(t, textlen);
        size_t laststart = 0, lastspace = 0, current;

        for (current = 0; current < textlen; current++) {
            if (t[current] == breakchar[0]) {
                laststart = lastspace = current + 1;
            } else if (t[current] == ' ') {
                if (current - laststart >= (size_t) linelength) {
                    ZSTR_VAL(out)[current] = breakchar[0];
                    laststart = current + 1;
                }
                lastspace = current;
            } else if (current - laststart >= (size_t) linelength
                       && laststart != lastspace) {
                ZSTR_VAL(out)[lastspace] = breakchar[0];
                laststart = lastspace + 1;
            }
        }
        ZVAL_STR(ret, out);
        return;
    }

    {
        ps_buf b;
        size_t laststart = 0, lastspace = 0, current;

        ps_buf_init(&b);
        for (current = 0; current < textlen; current++) {
            if (t[current] == breakchar[0]
                && current + breakcharlen < textlen
                && strncmp(t + current, breakchar, breakcharlen) == 0) {
                ps_buf_append(&b, t + laststart,
                              current - laststart + breakcharlen);
                current += breakcharlen - 1;
                laststart = lastspace = current + 1;
            } else if (t[current] == ' ') {
                if (current - laststart >= (size_t) linelength) {
                    ps_buf_append(&b, t + laststart, current - laststart);
                    ps_buf_append(&b, breakchar, breakcharlen);
                    laststart = current + 1;
                }
                lastspace = current;
            } else if (current - laststart >= (size_t) linelength
                       && docut && laststart >= lastspace) {
                ps_buf_append(&b, t + laststart, current - laststart);
                ps_buf_append(&b, breakchar, breakcharlen);
                laststart = lastspace = current;
            } else if (current - laststart >= (size_t) linelength
                       && laststart < lastspace) {
                ps_buf_append(&b, t + laststart, lastspace - laststart);
                ps_buf_append(&b, breakchar, breakcharlen);
                laststart = lastspace = lastspace + 1;
            }
        }
        if (laststart != current) {
            ps_buf_append(&b, t + laststart, current - laststart);
        }
        ZVAL_STR(ret, ps_buf_finish(&b));
    }
}

/* ------------------------------------------------------- comparison group */

static void zif_strcmp(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("strcmp", 2, 2);
    zend_string *a;
    if (!zend_parse_arg_string(&argv[0], &a, 1, "strcmp")) return;
    zend_string *b;
    if (!zend_parse_arg_string(&argv[1], &b, 2, "strcmp")) return;
    (void) argc;
    ZVAL_LONG(ret, ps_binary_strcmp(ZSTR_VAL(a), ZSTR_LEN(a),
                                    ZSTR_VAL(b), ZSTR_LEN(b)));
}

static void zif_strcasecmp(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("strcasecmp", 2, 2);
    zend_string *a;
    if (!zend_parse_arg_string(&argv[0], &a, 1, "strcasecmp")) return;
    zend_string *b;
    if (!zend_parse_arg_string(&argv[1], &b, 2, "strcasecmp")) return;
    (void) argc;
    ZVAL_LONG(ret, ps_binary_strcasecmp(ZSTR_VAL(a), ZSTR_LEN(a),
                                        ZSTR_VAL(b), ZSTR_LEN(b)));
}

static void zif_strncmp(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("strncmp", 3, 3);
    zend_string *a;
    if (!zend_parse_arg_string(&argv[0], &a, 1, "strncmp")) return;
    zend_string *b;
    if (!zend_parse_arg_string(&argv[1], &b, 2, "strncmp")) return;
    zend_long len;
    PS_LONG(len, 2, "strncmp");
    (void) argc;

    ZVAL_LONG(ret, ps_binary_strncmp(ZSTR_VAL(a), ZSTR_LEN(a),
                                     ZSTR_VAL(b), ZSTR_LEN(b), (size_t) len));
}

static void zif_strncasecmp(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("strncasecmp", 3, 3);
    zend_string *a;
    if (!zend_parse_arg_string(&argv[0], &a, 1, "strncasecmp")) return;
    zend_string *b;
    if (!zend_parse_arg_string(&argv[1], &b, 2, "strncasecmp")) return;
    zend_long len;
    PS_LONG(len, 2, "strncasecmp");
    (void) argc;
    if (len < 0) {
        zend_error(E_WARNING, "Length must be greater than or equal to 0");
        ps_ret_false(ret);
        return;
    }
    ZVAL_LONG(ret, ps_binary_strncasecmp(ZSTR_VAL(a), ZSTR_LEN(a),
                                         ZSTR_VAL(b), ZSTR_LEN(b), (size_t) len));
}

/* ------------------------------------------------------------- strnatcmp */

static int ps_compare_right(const char **a, const char *aend,
                            const char **b, const char *bend)
{
    int bias = 0;

    for (;; (*a)++, (*b)++) {
        int adig = (*a != aend) && isdigit((unsigned char) **a);
        int bdig = (*b != bend) && isdigit((unsigned char) **b);
        if (!adig && !bdig) {
            return bias;
        } else if (!adig) {
            return -1;
        } else if (!bdig) {
            return +1;
        } else if ((unsigned char) **a < (unsigned char) **b) {
            if (!bias) {
                bias = -1;
            }
        } else if ((unsigned char) **a > (unsigned char) **b) {
            if (!bias) {
                bias = +1;
            }
        }
    }
}

static int ps_compare_left(const char **a, const char *aend,
                           const char **b, const char *bend)
{
    for (;; (*a)++, (*b)++) {
        int adig = (*a != aend) && isdigit((unsigned char) **a);
        int bdig = (*b != bend) && isdigit((unsigned char) **b);
        if (!adig && !bdig) {
            return 0;
        } else if (!adig) {
            return -1;
        } else if (!bdig) {
            return +1;
        } else if ((unsigned char) **a < (unsigned char) **b) {
            return -1;
        } else if ((unsigned char) **a > (unsigned char) **b) {
            return +1;
        }
    }
}

static int ps_strnatcmp(const char *a, size_t a_len, const char *b, size_t b_len,
                        int fold_case)
{
    unsigned char ca, cb;
    const char *ap, *bp;
    const char *aend = a + a_len, *bend = b + b_len;
    int fractional, result;
    short leading = 1;

    if (a_len == 0 || b_len == 0) {
        return (int) ((zend_long) a_len - (zend_long) b_len);
    }
    ap = a;
    bp = b;
    for (;;) {
        ca = (unsigned char) *ap;
        cb = (unsigned char) *bp;

        while (leading && ca == '0' && (ap + 1 < aend)
               && isdigit((unsigned char) *(ap + 1))) {
            ca = (unsigned char) *++ap;
        }
        while (leading && cb == '0' && (bp + 1 < bend)
               && isdigit((unsigned char) *(bp + 1))) {
            cb = (unsigned char) *++bp;
        }
        leading = 0;

        while (isspace(ca)) {
            ca = (unsigned char) *++ap;
        }
        while (isspace(cb)) {
            cb = (unsigned char) *++bp;
        }

        if (isdigit(ca) && isdigit(cb)) {
            fractional = (ca == '0' || cb == '0');
            if (fractional) {
                result = ps_compare_left(&ap, aend, &bp, bend);
            } else {
                result = ps_compare_right(&ap, aend, &bp, bend);
            }
            if (result != 0) {
                return result;
            } else if (ap == aend && bp == bend) {
                return 0;
            } else {
                ca = (unsigned char) *ap;
                cb = (unsigned char) *bp;
            }
        }

        if (fold_case) {
            ca = (unsigned char) ps_upper((char) ca);
            cb = (unsigned char) ps_upper((char) cb);
        }
        if (ca < cb) {
            return -1;
        } else if (ca > cb) {
            return +1;
        }

        ++ap;
        ++bp;
        if (ap >= aend && bp >= bend) {
            return 0;
        } else if (ap >= aend) {
            return -1;
        } else if (bp >= bend) {
            return 1;
        }
    }
}

static void zif_strnatcmp(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("strnatcmp", 2, 2);
    zend_string *a;
    if (!zend_parse_arg_string(&argv[0], &a, 1, "strnatcmp")) return;
    zend_string *b;
    if (!zend_parse_arg_string(&argv[1], &b, 2, "strnatcmp")) return;
    (void) argc;
    ZVAL_LONG(ret, ps_strnatcmp(ZSTR_VAL(a), ZSTR_LEN(a),
                                ZSTR_VAL(b), ZSTR_LEN(b), 0));
}

static void zif_strnatcasecmp(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("strnatcasecmp", 2, 2);
    zend_string *a;
    if (!zend_parse_arg_string(&argv[0], &a, 1, "strnatcasecmp")) return;
    zend_string *b;
    if (!zend_parse_arg_string(&argv[1], &b, 2, "strnatcasecmp")) return;
    (void) argc;
    ZVAL_LONG(ret, ps_strnatcmp(ZSTR_VAL(a), ZSTR_LEN(a),
                                ZSTR_VAL(b), ZSTR_LEN(b), 1));
}

/* -------------------------------------------------------- str_word_count */

static void zif_str_word_count(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("str_word_count", 1, 3);
    zend_string *s;
    if (!zend_parse_arg_string(&argv[0], &s, 1, "str_word_count")) return;
    zend_long type = 0;
    PS_OPT_LONG(type, 1, "str_word_count");
    zend_string *cl = NULL;
    char ch[256];
    int have_charlist = 0;
    HashTable *ht = NULL;
    const char *p, *e, *start;
    zend_long word_count = 0;
    zval tmp;

    switch (type) {
    case 1:
    case 2:
        ht = zend_hash_new();
        if (ZSTR_LEN(s) == 0) {
            ZVAL_ARR(ret, ht);
            return;
        }
        break;
    case 0:
        if (ZSTR_LEN(s) == 0) {
            ZVAL_LONG(ret, 0);
            return;
        }
        break;
    /* VERSIONGATE:80000 the warning-and-false branch below is the 5.6 one; 8.0
     * throws ValueError. */
    default:
        zend_error(E_WARNING, "str_word_count(): Invalid format value %lld",
                   (long long) type);
        ps_ret_false(ret);
        return;
    }

    if (argc > 2) {
        if (!zend_parse_arg_string(&argv[2], &cl, 3, "str_word_count")) return;
        ps_charmask((const unsigned char *) ZSTR_VAL(cl), ZSTR_LEN(cl), ch,
                    "str_word_count");
        have_charlist = 1;
    }

    p = ZSTR_VAL(s);
    e = ZSTR_VAL(s) + ZSTR_LEN(s);

    if ((*p == '\'' && (!have_charlist || !ch['\'']))
        || (*p == '-' && (!have_charlist || !ch['-']))) {
        p++;
    }
    if (*(e - 1) == '-' && (!have_charlist || !ch['-'])) {
        e--;
    }

    while (p < e) {
        start = p;
        while (p < e && (isalpha((unsigned char) *p)
                         || (have_charlist && ch[(unsigned char) *p])
                         || *p == '\'' || *p == '-')) {
            p++;
        }
        if (p > start) {
            switch (type) {
            case 1:
                ZVAL_STR(&tmp, zend_string_init(start, (size_t) (p - start)));
                zend_hash_next_index_insert(ht, tmp);
                break;
            case 2:
                ZVAL_STR(&tmp, zend_string_init(start, (size_t) (p - start)));
                zend_hash_index_update(ht, (zend_long) (start - ZSTR_VAL(s)), tmp);
                break;
            default:
                word_count++;
                break;
            }
        }
        p++;
    }

    if (type == 0) {
        ZVAL_LONG(ret, word_count);
    } else {
        ZVAL_ARR(ret, ht);
    }
}

/* ----------------------------------------------------------- levenshtein */

#define PS_LEVENSHTEIN_MAX_LENGTH 255

static int ps_levdist(const char *s1, size_t l1, const char *s2, size_t l2,
                      int cost_ins, int cost_rep, int cost_del)
{
    int *p1, *p2, *tmp;
    size_t i1, i2;
    int c0, c1, c2;

    if (l1 == 0) {
        return (int) l2 * cost_ins;
    }
    if (l2 == 0) {
        return (int) l1 * cost_del;
    }
    if (l1 > PS_LEVENSHTEIN_MAX_LENGTH || l2 > PS_LEVENSHTEIN_MAX_LENGTH) {
        return -1;
    }
    p1 = (int *) malloc((l2 + 1) * sizeof(int));
    p2 = (int *) malloc((l2 + 1) * sizeof(int));

    for (i2 = 0; i2 <= l2; i2++) {
        p1[i2] = (int) i2 * cost_ins;
    }
    for (i1 = 0; i1 < l1; i1++) {
        p2[0] = p1[0] + cost_del;
        for (i2 = 0; i2 < l2; i2++) {
            c0 = p1[i2] + ((s1[i1] == s2[i2]) ? 0 : cost_rep);
            c1 = p1[i2 + 1] + cost_del;
            if (c1 < c0) {
                c0 = c1;
            }
            c2 = p2[i2] + cost_ins;
            if (c2 < c0) {
                c0 = c2;
            }
            p2[i2 + 1] = c0;
        }
        tmp = p1;
        p1 = p2;
        p2 = tmp;
    }
    c0 = p1[l2];
    free(p1);
    free(p2);
    return c0;
}

static void zif_levenshtein(zval *ret, uint32_t argc, zval *argv)
{
    zend_string *s1, *s2;
    zend_long ci = 1, cr = 1, cd = 1;
    int distance = -1;

    switch (argc) {
    case 2:
        if (!zend_parse_arg_string(&argv[0], &s1, 1, "levenshtein")) return;
        if (!zend_parse_arg_string(&argv[1], &s2, 2, "levenshtein")) return;
        distance = ps_levdist(ZSTR_VAL(s1), ZSTR_LEN(s1),
                              ZSTR_VAL(s2), ZSTR_LEN(s2), 1, 1, 1);
        break;
    case 5:
        if (!zend_parse_arg_string(&argv[0], &s1, 1, "levenshtein")) return;
        if (!zend_parse_arg_string(&argv[1], &s2, 2, "levenshtein")) return;
        PS_LONG(ci, 2, "levenshtein");
        PS_LONG(cr, 3, "levenshtein");
        PS_LONG(cd, 4, "levenshtein");
        distance = ps_levdist(ZSTR_VAL(s1), ZSTR_LEN(s1),
                              ZSTR_VAL(s2), ZSTR_LEN(s2),
                              (int) ci, (int) cr, (int) cd);
        break;
    /* VERSIONGATE:80000 accepting the never-implemented 3-argument form below
     * is the 5.6 branch; 8.0 removes it entirely. */
    case 3:
        zend_error(E_WARNING, "levenshtein(): The general Levenshtein support "
                   "is not there yet");
        ZVAL_LONG(ret, -1);
        return;
    default:
        ps_wrong_count("levenshtein", argc, 2);
        return;
    }


    ZVAL_LONG(ret, distance);
}

/* ---------------------------------------------------------- similar_text */

static void ps_similar_str(const char *txt1, size_t len1, const char *txt2,
                           size_t len2, size_t *pos1, size_t *pos2, size_t *max)
{
    const char *p, *q;
    const char *end1 = txt1 + len1;
    const char *end2 = txt2 + len2;
    size_t l;

    *max = 0;
    for (p = txt1; p < end1; p++) {
        for (q = txt2; q < end2; q++) {
            for (l = 0; (p + l < end1) && (q + l < end2) && (p[l] == q[l]); l++) {
                /* nothing */
            }
            if (l > *max) {
                *max = l;
                *pos1 = (size_t) (p - txt1);
                *pos2 = (size_t) (q - txt2);
            }
        }
    }
}

static size_t ps_similar_char(const char *txt1, size_t len1,
                              const char *txt2, size_t len2)
{
    size_t sum;
    size_t pos1 = 0, pos2 = 0, max;

    ps_similar_str(txt1, len1, txt2, len2, &pos1, &pos2, &max);
    if ((sum = max) != 0) {
        if (pos1 && pos2) {
            sum += ps_similar_char(txt1, pos1, txt2, pos2);
        }
        if ((pos1 + max < len1) && (pos2 + max < len2)) {
            sum += ps_similar_char(txt1 + pos1 + max, len1 - pos1 - max,
                                   txt2 + pos2 + max, len2 - pos2 - max);
        }
    }
    return sum;
}

static void zif_similar_text(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("similar_text", 2, 3);
    zend_string *t1;
    if (!zend_parse_arg_string(&argv[0], &t1, 1, "similar_text")) return;
    zend_string *t2;
    if (!zend_parse_arg_string(&argv[1], &t2, 2, "similar_text")) return;
    size_t sim;

    if (ZSTR_LEN(t1) + ZSTR_LEN(t2) == 0) {
        if (argc > 2) {
            ZVAL_DOUBLE(ps_ref(&argv[2]), 0.0);
        }
        ZVAL_LONG(ret, 0);
        return;
    }
    sim = ps_similar_char(ZSTR_VAL(t1), ZSTR_LEN(t1), ZSTR_VAL(t2), ZSTR_LEN(t2));
    if (argc > 2) {
        ZVAL_DOUBLE(ps_ref(&argv[2]),
                    (double) sim * 200.0 / (double) (ZSTR_LEN(t1) + ZSTR_LEN(t2)));
    }
    ZVAL_LONG(ret, (zend_long) sim);
}

/* --------------------------------------------------------------- soundex */

static void zif_soundex(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("soundex", 1, 1);
    static const char soundex_table[26] = {
        0,   '1', '2', '3', 0,   '1', '2', 0,   0,   '2', '2', '4', '5',
        '5', 0,   '1', '2', '6', '2', '3', 0,   '1', 0,   '2', 0,   '2'
    };
    zend_string *s;
    if (!zend_parse_arg_string(&argv[0], &s, 1, "soundex")) return;
    char out[5];
    int small = 0, code, last = -1;
    size_t i;
    (void) argc;


    for (i = 0; i < ZSTR_LEN(s) && small < 4; i++) {
        code = ps_upper(ZSTR_VAL(s)[i]);
        if (code >= 'A' && code <= 'Z') {
            if (small == 0) {
                out[small++] = (char) code;
                last = soundex_table[code - 'A'];
            } else {
                code = soundex_table[code - 'A'];
                if (code != last) {
                    if (code != 0) {
                        out[small++] = (char) code;
                    }
                    last = code;
                }
            }
        }
    }
    while (small < 4) {
        out[small++] = '0';
    }
    ps_ret_stringl(ret, out, 4);
}

/* ------------------------------------------------------------- metaphone */

#define PS_SH 'X'
#define PS_TH '0'

static const char ps_mp_codes[26] = {
    1, 16, 4, 16, 9, 2, 4, 16, 9, 2, 0, 2, 2, 2, 1, 4, 0, 2, 4, 4, 1, 0, 0, 0, 8, 0
};

static int ps_mp_encode(int c)
{
    c = (unsigned char) c;
    if (isalpha((unsigned char) c)) {
        return ps_mp_codes[ps_upper((char) c) - 'A'];
    }
    return 0;
}

#define PS_ISVOWEL(c)  (ps_mp_encode(c) & 1)
#define PS_NOCHANGE(c) (ps_mp_encode(c) & 2)
#define PS_AFFECTH(c)  (ps_mp_encode(c) & 4)
#define PS_MAKESOFT(c) (ps_mp_encode(c) & 8)
#define PS_NOGHTOF(c)  (ps_mp_encode(c) & 16)

static char ps_mp_lookahead(const char *word, int how_far)
{
    int idx;
    for (idx = 0; word[idx] != '\0' && idx < how_far; idx++) {
        /* advance */
    }
    return word[idx];
}

static int ps_metaphone(const char *word, size_t word_len, zend_long max_phonemes,
                        ps_buf *out)
{
    size_t w_idx = 0;
    zend_long p_idx = 0;

#define Curr_Letter          ((int) ps_upper(word[w_idx]))
#define Next_Letter          ((int) ps_upper(word[w_idx + 1]))
#define Look_Back_Letter(n)  (w_idx >= (size_t) (n) ? (int) ps_upper(word[w_idx - (n)]) : '\0')
#define Prev_Letter          Look_Back_Letter(1)
#define After_Next_Letter    (Next_Letter != '\0' ? (int) ps_upper(word[w_idx + 2]) : '\0')
#define Look_Ahead_Letter(n) ((int) ps_upper(ps_mp_lookahead(word + w_idx, (n))))
#define Phonize(c)           do { ps_buf_char(out, (char) (c)); p_idx++; } while (0)
#define Isbreak(c)           (!isalpha((unsigned char) (c)))

    (void) word_len;
    if (max_phonemes < 0) {
        return -1;
    }

    for (; !isalpha((unsigned char) Curr_Letter); w_idx++) {
        if (Curr_Letter == '\0') {
            return 0;
        }
    }

    switch (Curr_Letter) {
    case 'A':
        if (Next_Letter == 'E') {
            Phonize('E');
            w_idx += 2;
        } else {
            Phonize('A');
            w_idx++;
        }
        break;
    case 'G':
    case 'K':
    case 'P':
        if (Next_Letter == 'N') {
            Phonize('N');
            w_idx += 2;
        }
        break;
    case 'W':
        if (Next_Letter == 'R') {
            Phonize(Next_Letter);
            w_idx += 2;
        } else if (Next_Letter == 'H' || PS_ISVOWEL(Next_Letter)) {
            Phonize('W');
            w_idx += 2;
        }
        break;
    case 'X':
        Phonize('S');
        w_idx++;
        break;
    case 'E':
    case 'I':
    case 'O':
    case 'U':
        Phonize(Curr_Letter);
        w_idx++;
        break;
    default:
        break;
    }

    for (; Curr_Letter != '\0'
         && (max_phonemes == 0 || p_idx < max_phonemes); w_idx++) {
        unsigned short skip_letter = 0;

        if (!isalpha((unsigned char) Curr_Letter)) {
            continue;
        }
        if (Curr_Letter == Prev_Letter && Curr_Letter != 'C') {
            continue;
        }

        switch (Curr_Letter) {
        case 'B':
            if (Prev_Letter != 'M') {
                Phonize('B');
            }
            break;
        case 'C':
            if (PS_MAKESOFT(Next_Letter)) {
                if (After_Next_Letter == 'A' && Next_Letter == 'I') {
                    Phonize(PS_SH);
                } else if (Prev_Letter == 'S') {
                    /* dropped */
                } else {
                    Phonize('S');
                }
            } else if (Next_Letter == 'H') {
                Phonize(PS_SH);
                skip_letter++;
            } else {
                Phonize('K');
            }
            break;
        case 'D':
            if (Next_Letter == 'G' && PS_MAKESOFT(After_Next_Letter)) {
                Phonize('J');
                skip_letter++;
            } else {
                Phonize('T');
            }
            break;
        case 'G':
            if (Next_Letter == 'H') {
                if (!(PS_NOGHTOF(Look_Back_Letter(3))
                      || Look_Back_Letter(4) == 'H')) {
                    Phonize('F');
                    skip_letter++;
                }
            } else if (Next_Letter == 'N') {
                if (Isbreak(After_Next_Letter)
                    || (After_Next_Letter == 'E' && Look_Ahead_Letter(3) == 'D')) {
                    /* dropped */
                } else {
                    Phonize('K');
                }
            } else if (PS_MAKESOFT(Next_Letter) && Prev_Letter != 'G') {
                Phonize('J');
            } else {
                Phonize('K');
            }
            break;
        case 'H':
            if (PS_ISVOWEL(Next_Letter) && !PS_AFFECTH(Prev_Letter)) {
                Phonize('H');
            }
            break;
        case 'K':
            if (Prev_Letter != 'C') {
                Phonize('K');
            }
            break;
        case 'P':
            if (Next_Letter == 'H') {
                Phonize('F');
            } else {
                Phonize('P');
            }
            break;
        case 'Q':
            Phonize('K');
            break;
        case 'S':
            if (Next_Letter == 'I'
                && (After_Next_Letter == 'O' || After_Next_Letter == 'A')) {
                Phonize(PS_SH);
            } else if (Next_Letter == 'H') {
                Phonize(PS_SH);
                skip_letter++;
            } else {
                Phonize('S');
            }
            break;
        case 'T':
            if (Next_Letter == 'I'
                && (After_Next_Letter == 'O' || After_Next_Letter == 'A')) {
                Phonize(PS_SH);
            } else if (Next_Letter == 'H') {
                Phonize(PS_TH);
                skip_letter++;
            } else if (!(Next_Letter == 'C' && After_Next_Letter == 'H')) {
                Phonize('T');
            }
            break;
        case 'V':
            Phonize('F');
            break;
        case 'W':
            if (PS_ISVOWEL(Next_Letter)) {
                Phonize('W');
            }
            break;
        case 'X':
            Phonize('K');
            Phonize('S');
            break;
        case 'Y':
            if (PS_ISVOWEL(Next_Letter)) {
                Phonize('Y');
            }
            break;
        case 'Z':
            Phonize('S');
            break;
        case 'F':
        case 'J':
        case 'L':
        case 'M':
        case 'N':
        case 'R':
            Phonize(Curr_Letter);
            break;
        default:
            break;
        }
        w_idx += skip_letter;
    }
    return 0;

#undef Curr_Letter
#undef Next_Letter
#undef Look_Back_Letter
#undef Prev_Letter
#undef After_Next_Letter
#undef Look_Ahead_Letter
#undef Phonize
#undef Isbreak
}

static void zif_metaphone(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("metaphone", 1, 2);
    zend_string *s;
    if (!zend_parse_arg_string(&argv[0], &s, 1, "metaphone")) return;
    zend_long phones = 0;
    PS_OPT_LONG(phones, 1, "metaphone");
    ps_buf b;

    ps_buf_init(&b);
    if (ps_metaphone(ZSTR_VAL(s), ZSTR_LEN(s), phones, &b) == 0) {
        ZVAL_STR(ret, ps_buf_finish(&b));
    } else {
        free(b.buf);
        ps_buf_init(&b);
        ps_ret_false(ret);
    }
}

/* ------------------------------------------------------------- quotemeta */

static void zif_quotemeta(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("quotemeta", 1, 1);
    zend_string *s;
    if (!zend_parse_arg_string(&argv[0], &s, 1, "quotemeta")) return;
    ps_buf b;
    size_t i;
    (void) argc;


    ps_buf_init(&b);
    for (i = 0; i < ZSTR_LEN(s); i++) {
        char c = ZSTR_VAL(s)[i];
        switch (c) {
        case '.': case '\\': case '+': case '*': case '?':
        case '[': case '^': case ']': case '$': case '(': case ')':
            ps_buf_char(&b, '\\');
            /* fall through */
        default:
            ps_buf_char(&b, c);
            break;
        }
    }
    ZVAL_STR(ret, ps_buf_finish(&b));
}

/* ------------------------------------------------------------------ strtr */

typedef struct {
    char   *pat;
    size_t  pat_len;
    char   *repl;
    size_t  repl_len;
} ps_patrepl;

static void zif_strtr(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("strtr", 2, 3);
    zend_string *s;
    if (!zend_parse_arg_string(&argv[0], &s, 1, "strtr")) return;


    if (ZSTR_LEN(s) == 0) {
        ps_ret_empty(ret);
        return;
    }

    if (argc == 2) {
        HashTable *pats = Z_ARR(argv[1]);
        uint32_t npats = zend_hash_num_elements(pats);
        ps_patrepl *pr;
        uint32_t n = 0, i;
        size_t maxlen = 0, pos;
        ps_buf b;

        if (npats == 0) {
            ps_ret_zstr_copy(ret, s);
            return;
        }
        pr = (ps_patrepl *) calloc(npats, sizeof(ps_patrepl));
        ZEND_HASH_FOREACH(pats, bk) {
            zend_string *key;
            zend_string *rep;
            char numbuf[32];

            if (bk->key) {
                key = bk->key;
                if (ZSTR_LEN(key) == 0) {
                    free(pr);
                    ps_ret_false(ret);   /* empty pattern => false */
                    return;
                }
                if (ZSTR_LEN(key) > ZSTR_LEN(s)) {
                    continue;
                }
                pr[n].pat = ZSTR_VAL(key);
                pr[n].pat_len = ZSTR_LEN(key);
            } else {
                int ln = snprintf(numbuf, sizeof(numbuf), "%lld",
                                  (long long) (zend_long) bk->h);
                if ((size_t) ln > ZSTR_LEN(s)) {
                    continue;
                }
                pr[n].pat = (char *) malloc((size_t) ln + 1);
                memcpy(pr[n].pat, numbuf, (size_t) ln + 1);
                pr[n].pat_len = (size_t) ln;
            }
            rep = zval_get_string(zval_deref(&bk->val));
            pr[n].repl = ZSTR_VAL(rep);
            pr[n].repl_len = ZSTR_LEN(rep);
            if (pr[n].pat_len > maxlen) {
                maxlen = pr[n].pat_len;
            }
            n++;
        } ZEND_HASH_FOREACH_END();

        if (n == 0) {
            free(pr);
            ps_ret_zstr_copy(ret, s);
            return;
        }

        ps_buf_init(&b);
        pos = 0;
        while (pos < ZSTR_LEN(s)) {
            size_t best = 0;
            uint32_t best_i = 0;
            size_t avail = ZSTR_LEN(s) - pos;

            for (i = 0; i < n; i++) {
                if (pr[i].pat_len <= avail && pr[i].pat_len > best
                    && memcmp(ZSTR_VAL(s) + pos, pr[i].pat, pr[i].pat_len) == 0) {
                    best = pr[i].pat_len;
                    best_i = i;
                }
            }
            if (best) {
                ps_buf_append(&b, pr[best_i].repl, pr[best_i].repl_len);
                pos += best;
            } else {
                ps_buf_char(&b, ZSTR_VAL(s)[pos]);
                pos++;
            }
        }
        free(pr);
        ZVAL_STR(ret, ps_buf_finish(&b));
        return;
    }

    {
        zend_string *from;
        if (!zend_parse_arg_string(&argv[1], &from, 2, "strtr")) return;
        zend_string *to;
        if (!zend_parse_arg_string(&argv[2], &to, 3, "strtr")) return;
        size_t trlen = PS_MIN(ZSTR_LEN(from), ZSTR_LEN(to));
        zend_string *out = zend_string_init(ZSTR_VAL(s), ZSTR_LEN(s));
        unsigned char xlat[256];
        size_t i;

        if (trlen < 1) {
            ZVAL_STR(ret, out);
            return;
        }
        for (i = 0; i < 256; i++) {
            xlat[i] = (unsigned char) i;
        }
        for (i = 0; i < trlen; i++) {
            xlat[(unsigned char) ZSTR_VAL(from)[i]] =
                (unsigned char) ZSTR_VAL(to)[i];
        }
        for (i = 0; i < ZSTR_LEN(out); i++) {
            ZSTR_VAL(out)[i] = (char) xlat[(unsigned char) ZSTR_VAL(out)[i]];
        }
        ZVAL_STR(ret, out);
    }
}

/* ----------------------------------------------------------- str_shuffle */

/* The Mersenne Twister lives in ext/standard/php_rand.c, owned by another
 * module; this file may include only the three frozen headers, so the entry
 * point is declared here.  php_mt_rand_common() applies php-src's RAND_RANGE
 * scaling, matching the shape of 5.6's php_rand()+RAND_RANGE call; 5.6 drew the
 * raw number from libc random() rather than the Twister, so seeded sequences
 * differ from the oracle (the .phpt tests wildcard them). */
extern zend_long php_mt_rand_common(zend_long min, zend_long max);

static void zif_str_shuffle(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("str_shuffle", 1, 1);
    zend_string *s;
    if (!zend_parse_arg_string(&argv[0], &s, 1, "str_shuffle")) return;
    zend_string *out = zend_string_init(ZSTR_VAL(s), ZSTR_LEN(s));
    size_t n_left = ZSTR_LEN(out);
    (void) argc;

    if (n_left > 1) {
        while (--n_left) {
            size_t rnd_idx = (size_t) php_mt_rand_common(0, (zend_long) n_left);
            if (rnd_idx != n_left) {
                char t = ZSTR_VAL(out)[n_left];
                ZSTR_VAL(out)[n_left] = ZSTR_VAL(out)[rnd_idx];
                ZSTR_VAL(out)[rnd_idx] = t;
            }
        }
    }
    ZVAL_STR(ret, out);
}

/* ----------------------------------------------------------- chunk_split */

static void zif_chunk_split(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("chunk_split", 1, 3);
    zend_string *s;
    if (!zend_parse_arg_string(&argv[0], &s, 1, "chunk_split")) return;
    zend_long chunklen = 76;
    PS_OPT_LONG(chunklen, 1, "chunk_split");
    const char *end = "\r\n";
    size_t endlen = 2;
    zend_string *end_s = NULL;
    ps_buf b;
    size_t p, chunks, restlen;

    if (argc > 2) {
        if (!zend_parse_arg_string(&argv[2], &end_s, 3, "chunk_split")) return;
        end = ZSTR_VAL(end_s);
        endlen = ZSTR_LEN(end_s);
    }

    if ((size_t) chunklen > ZSTR_LEN(s)) {
        /* BC: original string + ending */
        ps_buf_init(&b);
        ps_buf_append(&b, ZSTR_VAL(s), ZSTR_LEN(s));
        ps_buf_append(&b, end, endlen);
        ZVAL_STR(ret, ps_buf_finish(&b));
        return;
    }
    if (ZSTR_LEN(s) == 0) {
        ps_ret_empty(ret);
        return;
    }

    chunks = ZSTR_LEN(s) / (size_t) chunklen;
    restlen = ZSTR_LEN(s) - chunks * (size_t) chunklen;

    /* VERSIONGATE:70000 else-branch is the 5.6 guard: the result length is an
     * int there, so an overflow makes chunk_split() return false rather than
     * try to build the string. */
    if (EG(php_version) < 70000) {
        size_t out_len;
        if (chunks > (size_t) INT_MAX - 1) {
            ps_ret_false(ret);
            return;
        }
        out_len = chunks + 1;
        if (endlen != 0 && out_len > (size_t) INT_MAX / endlen) {
            ps_ret_false(ret);
            return;
        }
        out_len *= endlen;
        if (out_len > (size_t) INT_MAX - ZSTR_LEN(s) - 1) {
            ps_ret_false(ret);
            return;
        }
    }

    ps_buf_init(&b);
    for (p = 0; p + (size_t) chunklen <= ZSTR_LEN(s); p += (size_t) chunklen) {
        ps_buf_append(&b, ZSTR_VAL(s) + p, (size_t) chunklen);
        ps_buf_append(&b, end, endlen);
    }
    if (restlen) {
        ps_buf_append(&b, ZSTR_VAL(s) + p, restlen);
        ps_buf_append(&b, end, endlen);
    }
    ZVAL_STR(ret, ps_buf_finish(&b));
}

/* ------------------------------------------------------- bin2hex/hex2bin */

static void zif_bin2hex(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("bin2hex", 1, 1);
    static const char hexconvtab[] = "0123456789abcdef";
    zend_string *s;
    if (!zend_parse_arg_string(&argv[0], &s, 1, "bin2hex")) return;
    zend_string *out = zend_string_alloc(ZSTR_LEN(s) * 2);
    size_t i, j;
    (void) argc;

    for (i = j = 0; i < ZSTR_LEN(s); i++) {
        unsigned char c = (unsigned char) ZSTR_VAL(s)[i];
        ZSTR_VAL(out)[j++] = hexconvtab[c >> 4];
        ZSTR_VAL(out)[j++] = hexconvtab[c & 15];
    }
    ZSTR_VAL(out)[j] = '\0';
    ZVAL_STR(ret, out);
}

static int ps_hexval(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static void zif_hex2bin(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("hex2bin", 1, 1);
    zend_string *s;
    if (!zend_parse_arg_string(&argv[0], &s, 1, "hex2bin")) return;
    size_t target_len, i;
    zend_string *out;
    (void) argc;


    target_len = ZSTR_LEN(s) >> 1;
    out = zend_string_alloc(target_len);
    for (i = 0; i < target_len; i++) {
        int hi = ps_hexval(ZSTR_VAL(s)[i * 2]);
        int lo = ps_hexval(ZSTR_VAL(s)[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            zend_error(E_WARNING, "hex2bin(): Input string must be hexadecimal "
                       "string");
            ps_ret_false(ret);
            return;
        }
        ZSTR_VAL(out)[i] = (char) ((hi << 4) | lo);
    }
    ZSTR_VAL(out)[target_len] = '\0';
    ZVAL_STR(ret, out);
}

/* --------------------------------------------------------------- strpbrk */

static void zif_strpbrk(zval *ret, uint32_t argc, zval *argv)
{
    ps_ret_false(ret);   /* php-src returns false from this one on a parameter error */
    PS_ARGC("strpbrk", 2, 2);
    zend_string *h;
    if (!zend_parse_arg_string(&argv[0], &h, 1, "strpbrk")) return;
    zend_string *cl;
    if (!zend_parse_arg_string(&argv[1], &cl, 2, "strpbrk")) return;
    size_t i, j;
    (void) argc;


    for (i = 0; i < ZSTR_LEN(h); i++) {
        for (j = 0; j < ZSTR_LEN(cl); j++) {
            if (ZSTR_VAL(cl)[j] == ZSTR_VAL(h)[i]) {
                ps_ret_stringl(ret, ZSTR_VAL(h) + i, ZSTR_LEN(h) - i);
                return;
            }
        }
    }
    ps_ret_false(ret);
}

/* -------------------------------------------------------- substr_compare */

static void zif_substr_compare(zval *ret, uint32_t argc, zval *argv)
{
    ps_ret_false(ret);   /* php-src returns false from this one on a parameter error */
    PS_ARGC("substr_compare", 3, 5);
    zend_string *s1;
    if (!zend_parse_arg_string(&argv[0], &s1, 1, "substr_compare")) return;
    zend_string *s2;
    if (!zend_parse_arg_string(&argv[1], &s2, 2, "substr_compare")) return;
    zend_long offset;
    PS_LONG(offset, 2, "substr_compare");
    zend_long len = 0;
    int ci = 0;
    size_t cmp_len;

    /* zend_parse_parameters() converts every argument before the function
     * looks at any of them, so all five are parsed first.
     * VERSIONGATE:80000 coercing a null $length to 0 here is the 5.6 branch
     * (which then short-circuits to a 0 result); 8.0 treats null as "compare
     * to the end". */
    PS_OPT_LONG(len, 3, "substr_compare");
    PS_OPT_BOOL(ci, 4, "substr_compare");

    if (argc >= 4) {

    }
    if (offset < 0) {
        offset = (zend_long) ZSTR_LEN(s1) + offset;
        offset = (offset < 0) ? 0 : offset;
    }

    cmp_len = (size_t) (len ? (size_t) len
                            : PS_MAX(ZSTR_LEN(s2), ZSTR_LEN(s1) - (size_t) offset));
    if (!ci) {
        ZVAL_LONG(ret, ps_binary_strncmp(ZSTR_VAL(s1) + offset,
                                         ZSTR_LEN(s1) - (size_t) offset,
                                         ZSTR_VAL(s2), ZSTR_LEN(s2), cmp_len));
    } else {
        ZVAL_LONG(ret, ps_binary_strncasecmp(ZSTR_VAL(s1) + offset,
                                             ZSTR_LEN(s1) - (size_t) offset,
                                             ZSTR_VAL(s2), ZSTR_LEN(s2), cmp_len));
    }
}

/* ---------------------------------------------------------- HTML entities */

typedef struct {
    const char *name;
    unsigned char name_len;
    unsigned int cp;
} ps_entity;

/* HTML 4.01 named entity table (generated from
 * get_html_translation_table(HTML_ENTITIES, ENT_QUOTES|ENT_HTML401, 'UTF-8')). */
static const ps_entity ps_html4_entities[] = {
    { "quot", 4, 0x0022 },
    { "amp", 3, 0x0026 },
    { "lt", 2, 0x003C },
    { "gt", 2, 0x003E },
    { "nbsp", 4, 0x00A0 },
    { "iexcl", 5, 0x00A1 },
    { "cent", 4, 0x00A2 },
    { "pound", 5, 0x00A3 },
    { "curren", 6, 0x00A4 },
    { "yen", 3, 0x00A5 },
    { "brvbar", 6, 0x00A6 },
    { "sect", 4, 0x00A7 },
    { "uml", 3, 0x00A8 },
    { "copy", 4, 0x00A9 },
    { "ordf", 4, 0x00AA },
    { "laquo", 5, 0x00AB },
    { "not", 3, 0x00AC },
    { "shy", 3, 0x00AD },
    { "reg", 3, 0x00AE },
    { "macr", 4, 0x00AF },
    { "deg", 3, 0x00B0 },
    { "plusmn", 6, 0x00B1 },
    { "sup2", 4, 0x00B2 },
    { "sup3", 4, 0x00B3 },
    { "acute", 5, 0x00B4 },
    { "micro", 5, 0x00B5 },
    { "para", 4, 0x00B6 },
    { "middot", 6, 0x00B7 },
    { "cedil", 5, 0x00B8 },
    { "sup1", 4, 0x00B9 },
    { "ordm", 4, 0x00BA },
    { "raquo", 5, 0x00BB },
    { "frac14", 6, 0x00BC },
    { "frac12", 6, 0x00BD },
    { "frac34", 6, 0x00BE },
    { "iquest", 6, 0x00BF },
    { "Agrave", 6, 0x00C0 },
    { "Aacute", 6, 0x00C1 },
    { "Acirc", 5, 0x00C2 },
    { "Atilde", 6, 0x00C3 },
    { "Auml", 4, 0x00C4 },
    { "Aring", 5, 0x00C5 },
    { "AElig", 5, 0x00C6 },
    { "Ccedil", 6, 0x00C7 },
    { "Egrave", 6, 0x00C8 },
    { "Eacute", 6, 0x00C9 },
    { "Ecirc", 5, 0x00CA },
    { "Euml", 4, 0x00CB },
    { "Igrave", 6, 0x00CC },
    { "Iacute", 6, 0x00CD },
    { "Icirc", 5, 0x00CE },
    { "Iuml", 4, 0x00CF },
    { "ETH", 3, 0x00D0 },
    { "Ntilde", 6, 0x00D1 },
    { "Ograve", 6, 0x00D2 },
    { "Oacute", 6, 0x00D3 },
    { "Ocirc", 5, 0x00D4 },
    { "Otilde", 6, 0x00D5 },
    { "Ouml", 4, 0x00D6 },
    { "times", 5, 0x00D7 },
    { "Oslash", 6, 0x00D8 },
    { "Ugrave", 6, 0x00D9 },
    { "Uacute", 6, 0x00DA },
    { "Ucirc", 5, 0x00DB },
    { "Uuml", 4, 0x00DC },
    { "Yacute", 6, 0x00DD },
    { "THORN", 5, 0x00DE },
    { "szlig", 5, 0x00DF },
    { "agrave", 6, 0x00E0 },
    { "aacute", 6, 0x00E1 },
    { "acirc", 5, 0x00E2 },
    { "atilde", 6, 0x00E3 },
    { "auml", 4, 0x00E4 },
    { "aring", 5, 0x00E5 },
    { "aelig", 5, 0x00E6 },
    { "ccedil", 6, 0x00E7 },
    { "egrave", 6, 0x00E8 },
    { "eacute", 6, 0x00E9 },
    { "ecirc", 5, 0x00EA },
    { "euml", 4, 0x00EB },
    { "igrave", 6, 0x00EC },
    { "iacute", 6, 0x00ED },
    { "icirc", 5, 0x00EE },
    { "iuml", 4, 0x00EF },
    { "eth", 3, 0x00F0 },
    { "ntilde", 6, 0x00F1 },
    { "ograve", 6, 0x00F2 },
    { "oacute", 6, 0x00F3 },
    { "ocirc", 5, 0x00F4 },
    { "otilde", 6, 0x00F5 },
    { "ouml", 4, 0x00F6 },
    { "divide", 6, 0x00F7 },
    { "oslash", 6, 0x00F8 },
    { "ugrave", 6, 0x00F9 },
    { "uacute", 6, 0x00FA },
    { "ucirc", 5, 0x00FB },
    { "uuml", 4, 0x00FC },
    { "yacute", 6, 0x00FD },
    { "thorn", 5, 0x00FE },
    { "yuml", 4, 0x00FF },
    { "OElig", 5, 0x0152 },
    { "oelig", 5, 0x0153 },
    { "Scaron", 6, 0x0160 },
    { "scaron", 6, 0x0161 },
    { "Yuml", 4, 0x0178 },
    { "fnof", 4, 0x0192 },
    { "circ", 4, 0x02C6 },
    { "tilde", 5, 0x02DC },
    { "Alpha", 5, 0x0391 },
    { "Beta", 4, 0x0392 },
    { "Gamma", 5, 0x0393 },
    { "Delta", 5, 0x0394 },
    { "Epsilon", 7, 0x0395 },
    { "Zeta", 4, 0x0396 },
    { "Eta", 3, 0x0397 },
    { "Theta", 5, 0x0398 },
    { "Iota", 4, 0x0399 },
    { "Kappa", 5, 0x039A },
    { "Lambda", 6, 0x039B },
    { "Mu", 2, 0x039C },
    { "Nu", 2, 0x039D },
    { "Xi", 2, 0x039E },
    { "Omicron", 7, 0x039F },
    { "Pi", 2, 0x03A0 },
    { "Rho", 3, 0x03A1 },
    { "Sigma", 5, 0x03A3 },
    { "Tau", 3, 0x03A4 },
    { "Upsilon", 7, 0x03A5 },
    { "Phi", 3, 0x03A6 },
    { "Chi", 3, 0x03A7 },
    { "Psi", 3, 0x03A8 },
    { "Omega", 5, 0x03A9 },
    { "alpha", 5, 0x03B1 },
    { "beta", 4, 0x03B2 },
    { "gamma", 5, 0x03B3 },
    { "delta", 5, 0x03B4 },
    { "epsilon", 7, 0x03B5 },
    { "zeta", 4, 0x03B6 },
    { "eta", 3, 0x03B7 },
    { "theta", 5, 0x03B8 },
    { "iota", 4, 0x03B9 },
    { "kappa", 5, 0x03BA },
    { "lambda", 6, 0x03BB },
    { "mu", 2, 0x03BC },
    { "nu", 2, 0x03BD },
    { "xi", 2, 0x03BE },
    { "omicron", 7, 0x03BF },
    { "pi", 2, 0x03C0 },
    { "rho", 3, 0x03C1 },
    { "sigmaf", 6, 0x03C2 },
    { "sigma", 5, 0x03C3 },
    { "tau", 3, 0x03C4 },
    { "upsilon", 7, 0x03C5 },
    { "phi", 3, 0x03C6 },
    { "chi", 3, 0x03C7 },
    { "psi", 3, 0x03C8 },
    { "omega", 5, 0x03C9 },
    { "thetasym", 8, 0x03D1 },
    { "upsih", 5, 0x03D2 },
    { "piv", 3, 0x03D6 },
    { "ensp", 4, 0x2002 },
    { "emsp", 4, 0x2003 },
    { "thinsp", 6, 0x2009 },
    { "zwnj", 4, 0x200C },
    { "zwj", 3, 0x200D },
    { "lrm", 3, 0x200E },
    { "rlm", 3, 0x200F },
    { "ndash", 5, 0x2013 },
    { "mdash", 5, 0x2014 },
    { "lsquo", 5, 0x2018 },
    { "rsquo", 5, 0x2019 },
    { "sbquo", 5, 0x201A },
    { "ldquo", 5, 0x201C },
    { "rdquo", 5, 0x201D },
    { "bdquo", 5, 0x201E },
    { "dagger", 6, 0x2020 },
    { "Dagger", 6, 0x2021 },
    { "bull", 4, 0x2022 },
    { "hellip", 6, 0x2026 },
    { "permil", 6, 0x2030 },
    { "prime", 5, 0x2032 },
    { "Prime", 5, 0x2033 },
    { "lsaquo", 6, 0x2039 },
    { "rsaquo", 6, 0x203A },
    { "oline", 5, 0x203E },
    { "frasl", 5, 0x2044 },
    { "euro", 4, 0x20AC },
    { "image", 5, 0x2111 },
    { "weierp", 6, 0x2118 },
    { "real", 4, 0x211C },
    { "trade", 5, 0x2122 },
    { "alefsym", 7, 0x2135 },
    { "larr", 4, 0x2190 },
    { "uarr", 4, 0x2191 },
    { "rarr", 4, 0x2192 },
    { "darr", 4, 0x2193 },
    { "harr", 4, 0x2194 },
    { "crarr", 5, 0x21B5 },
    { "lArr", 4, 0x21D0 },
    { "uArr", 4, 0x21D1 },
    { "rArr", 4, 0x21D2 },
    { "dArr", 4, 0x21D3 },
    { "hArr", 4, 0x21D4 },
    { "forall", 6, 0x2200 },
    { "part", 4, 0x2202 },
    { "exist", 5, 0x2203 },
    { "empty", 5, 0x2205 },
    { "nabla", 5, 0x2207 },
    { "isin", 4, 0x2208 },
    { "notin", 5, 0x2209 },
    { "ni", 2, 0x220B },
    { "prod", 4, 0x220F },
    { "sum", 3, 0x2211 },
    { "minus", 5, 0x2212 },
    { "lowast", 6, 0x2217 },
    { "radic", 5, 0x221A },
    { "prop", 4, 0x221D },
    { "infin", 5, 0x221E },
    { "ang", 3, 0x2220 },
    { "and", 3, 0x2227 },
    { "or", 2, 0x2228 },
    { "cap", 3, 0x2229 },
    { "cup", 3, 0x222A },
    { "int", 3, 0x222B },
    { "there4", 6, 0x2234 },
    { "sim", 3, 0x223C },
    { "cong", 4, 0x2245 },
    { "asymp", 5, 0x2248 },
    { "ne", 2, 0x2260 },
    { "equiv", 5, 0x2261 },
    { "le", 2, 0x2264 },
    { "ge", 2, 0x2265 },
    { "sub", 3, 0x2282 },
    { "sup", 3, 0x2283 },
    { "nsub", 4, 0x2284 },
    { "sube", 4, 0x2286 },
    { "supe", 4, 0x2287 },
    { "oplus", 5, 0x2295 },
    { "otimes", 6, 0x2297 },
    { "perp", 4, 0x22A5 },
    { "sdot", 4, 0x22C5 },
    { "lceil", 5, 0x2308 },
    { "rceil", 5, 0x2309 },
    { "lfloor", 6, 0x230A },
    { "rfloor", 6, 0x230B },
    { "lang", 4, 0x2329 },
    { "rang", 4, 0x232A },
    { "loz", 3, 0x25CA },
    { "spades", 6, 0x2660 },
    { "clubs", 5, 0x2663 },
    { "hearts", 6, 0x2665 },
    { "diams", 5, 0x2666 },
};

#define PS_HTML4_ENTITY_COUNT \
    ((int) (sizeof(ps_html4_entities) / sizeof(ps_html4_entities[0])))

/* The five "basic" entities used by htmlspecialchars()/htmlspecialchars_decode(). */
static const ps_entity ps_basic_entities[] = {
    { "quot", 4, 0x0022 },
    { "amp",  3, 0x0026 },
    { "lt",   2, 0x003C },
    { "gt",   2, 0x003E }
};

/* determine_charset(): php-src warns about a charset it does not know and
 * falls back to UTF-8. */
static int ps_charset_known(const zend_string *cs, const char *fname)
{
    static const char *known[] = {
        "utf-8", "utf8", "iso-8859-1", "iso8859-1", "iso-8859-5", "iso8859-5",
        "iso-8859-15", "iso8859-15", "cp1252", "windows-1252", "1252",
        "cp1251", "windows-1251", "win-1251", "cp866", "866", "ibm866",
        "koi8-r", "koi8-ru", "koi8r", "big5", "950", "big5-hkscs", "gb2312",
        "936", "shift_jis", "sjis", "932", "sjis-win", "cp932", "euc-jp",
        "eucjp", "eucjp-win", "macroman", "html-entities", ""
    };
    size_t i, j;
    char buf[64];

    if (cs == NULL) {
        return 1;
    }
    if (ZSTR_LEN(cs) < sizeof(buf)) {
        for (j = 0; j < ZSTR_LEN(cs); j++) {
            buf[j] = ps_lower(ZSTR_VAL(cs)[j]);
        }
        buf[ZSTR_LEN(cs)] = '\0';
        for (i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
            if (strcmp(buf, known[i]) == 0) {
                return 1;
            }
        }
    }
    zend_error(E_WARNING, "%s(): charset `%s' not supported, assuming utf-8",
               fname, ZSTR_VAL(cs));
    return 0;
}

static int ps_charset_is_utf8(const zend_string *cs)
{
    const char *p;
    size_t n;

    if (cs == NULL || ZSTR_LEN(cs) == 0) {
        return 1;   /* default_charset is empty in 5.6 => UTF-8 */
    }
    p = ZSTR_VAL(cs);
    n = ZSTR_LEN(cs);
    if (n == 5 && (p[0] == 'u' || p[0] == 'U') && (p[1] == 't' || p[1] == 'T')
        && (p[2] == 'f' || p[2] == 'F') && p[3] == '-' && p[4] == '8') {
        return 1;
    }
    if (n == 4 && (p[0] == 'u' || p[0] == 'U') && (p[1] == 't' || p[1] == 'T')
        && (p[2] == 'f' || p[2] == 'F') && p[3] == '8') {
        return 1;
    }
    return 0;
}

static int ps_utf8_trail(unsigned char c)
{
    return c >= 0x80 && c <= 0xBF;
}

static int ps_utf8_lead(unsigned char c)
{
    return c < 0x80 || (c >= 0xC2 && c <= 0xF4);
}

/* Returns the code point, advancing *cursor.  *status is 0 on an invalid
 * sequence (the cursor is still advanced past the offending bytes). */
static unsigned int ps_utf8_next(const unsigned char *str, size_t str_len,
                                 size_t *cursor, int *status)
{
    size_t pos = *cursor;
    unsigned int this_char = 0;
    unsigned char c;

    *status = 1;
    if (pos >= str_len) {
        *cursor = pos + 1;
        *status = 0;
        return 0;
    }
    c = str[pos];
    if (c < 0x80) {
        this_char = c;
        pos++;
    } else if (c < 0xc2) {
        *cursor = pos + 1;
        *status = 0;
        return 0;
    } else if (c < 0xe0) {
        if (pos + 2 > str_len) {
            *cursor = pos + 1; *status = 0; return 0;
        }
        if (!ps_utf8_trail(str[pos + 1])) {
            *cursor = pos + (ps_utf8_lead(str[pos + 1]) ? 1 : 2);
            *status = 0;
            return 0;
        }
        this_char = ((unsigned) (c & 0x1f) << 6) | (str[pos + 1] & 0x3f);
        if (this_char < 0x80) {
            *cursor = pos + 2; *status = 0; return 0;
        }
        pos += 2;
    } else if (c < 0xf0) {
        size_t avail = str_len - pos;
        if (avail < 3 || !ps_utf8_trail(str[pos + 1]) || !ps_utf8_trail(str[pos + 2])) {
            if (avail < 2 || ps_utf8_lead(str[pos + 1])) {
                *cursor = pos + 1;
            } else if (avail < 3 || ps_utf8_lead(str[pos + 2])) {
                *cursor = pos + 2;
            } else {
                *cursor = pos + 3;
            }
            *status = 0;
            return 0;
        }
        this_char = ((unsigned) (c & 0x0f) << 12)
                  | ((unsigned) (str[pos + 1] & 0x3f) << 6)
                  | (str[pos + 2] & 0x3f);
        if (this_char < 0x800 || (this_char >= 0xd800 && this_char <= 0xdfff)) {
            *cursor = pos + 3; *status = 0; return 0;
        }
        pos += 3;
    } else if (c < 0xf5) {
        size_t avail = str_len - pos;
        if (avail < 4 || !ps_utf8_trail(str[pos + 1]) || !ps_utf8_trail(str[pos + 2])
            || !ps_utf8_trail(str[pos + 3])) {
            if (avail < 2 || ps_utf8_lead(str[pos + 1])) {
                *cursor = pos + 1;
            } else if (avail < 3 || ps_utf8_lead(str[pos + 2])) {
                *cursor = pos + 2;
            } else if (avail < 4 || ps_utf8_lead(str[pos + 3])) {
                *cursor = pos + 3;
            } else {
                *cursor = pos + 4;
            }
            *status = 0;
            return 0;
        }
        this_char = ((unsigned) (c & 0x07) << 18)
                  | ((unsigned) (str[pos + 1] & 0x3f) << 12)
                  | ((unsigned) (str[pos + 2] & 0x3f) << 6)
                  | (str[pos + 3] & 0x3f);
        if (this_char < 0x10000 || this_char > 0x10FFFF) {
            *cursor = pos + 4; *status = 0; return 0;
        }
        pos += 4;
    } else {
        *cursor = pos + 1;
        *status = 0;
        return 0;
    }
    *cursor = pos;
    return this_char;
}

static size_t ps_utf32_utf8(unsigned char *buf, unsigned int k)
{
    if (k < 0x80) {
        buf[0] = (unsigned char) k;
        return 1;
    }
    if (k < 0x800) {
        buf[0] = (unsigned char) (0xc0 | (k >> 6));
        buf[1] = (unsigned char) (0x80 | (k & 0x3f));
        return 2;
    }
    if (k < 0x10000) {
        buf[0] = (unsigned char) (0xe0 | (k >> 12));
        buf[1] = (unsigned char) (0x80 | ((k >> 6) & 0x3f));
        buf[2] = (unsigned char) (0x80 | (k & 0x3f));
        return 3;
    }
    buf[0] = (unsigned char) (0xf0 | (k >> 18));
    buf[1] = (unsigned char) (0x80 | ((k >> 12) & 0x3f));
    buf[2] = (unsigned char) (0x80 | ((k >> 6) & 0x3f));
    buf[3] = (unsigned char) (0x80 | (k & 0x3f));
    return 4;
}

static int ps_unicode_cp_is_allowed(unsigned int cp, int doctype)
{
    switch (doctype) {
    case PS_ENT_DOC_HTML401:
        return (cp >= 0x20 && cp <= 0x7E)
            || (cp == 0x0A || cp == 0x09 || cp == 0x0D)
            || (cp >= 0xA0 && cp <= 0xD7FF)
            || (cp >= 0xE000 && cp <= 0x10FFFF);
    case PS_ENT_DOC_HTML5:
        return (cp >= 0x20 && cp <= 0x7E)
            || (cp >= 0x09 && cp <= 0x0D && cp != 0x0B)
            || (cp >= 0xA0 && cp <= 0xD7FF)
            || (cp >= 0xE000 && cp <= 0x10FFFF && ((cp & 0xFFFF) < 0xFFFE)
                && (cp < 0xFDD0 || cp > 0xFDEF));
    case PS_ENT_DOC_XHTML:
    case PS_ENT_DOC_XML1:
        return (cp >= 0x20 && cp <= 0xD7FF)
            || (cp == 0x0A || cp == 0x09 || cp == 0x0D)
            || (cp >= 0xE000 && cp <= 0x10FFFF && cp != 0xFFFE && cp != 0xFFFF);
    default:
        return 1;
    }
}

/* Scan a named entity body starting at `p` (just after '&'); returns its length
 * (excluding the ';') or 0 when it is not well formed. */
static size_t ps_scan_named_entity(const char *p, const char *lim)
{
    const char *q = p;
    while (q < lim && ((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z')
                       || (*q >= '0' && *q <= '9'))) {
        q++;
    }
    if (q >= lim || *q != ';' || q == p) {
        return 0;
    }
    return (size_t) (q - p);
}

static int ps_resolve_named_entity(const char *name, size_t len, int all,
                                   int doctype, unsigned int *cp)
{
    int i;

    if (all) {
        for (i = 0; i < PS_HTML4_ENTITY_COUNT; i++) {
            if (ps_html4_entities[i].name_len == len
                && memcmp(name, ps_html4_entities[i].name, len) == 0) {
                *cp = ps_html4_entities[i].cp;
                return 1;
            }
        }
    } else {
        for (i = 0; i < (int) (sizeof(ps_basic_entities) / sizeof(ps_basic_entities[0])); i++) {
            if (ps_basic_entities[i].name_len == len
                && memcmp(name, ps_basic_entities[i].name, len) == 0) {
                *cp = ps_basic_entities[i].cp;
                return 1;
            }
        }
        if (len == 4 && memcmp(name, "apos", 4) == 0
            && doctype != PS_ENT_DOC_HTML401) {
            *cp = '\'';
            return 1;
        }
        return 0;
    }
    /* html4 map has no &apos;; XHTML accepts it as a special case. */
    if (doctype == PS_ENT_DOC_XHTML && len == 4 && memcmp(name, "apos", 4) == 0) {
        *cp = '\'';
        return 1;
    }
    return 0;
}

/* Parse "#1234;" / "#x1A;" starting at `p` (just after '&'); on success returns
 * the number of bytes up to (not including) the ';'. */
static size_t ps_scan_numeric_entity(const char *p, const char *lim,
                                     unsigned int *cp)
{
    const char *q = p;
    int hex;
    long code = 0;
    int digits = 0;

    if (q >= lim || *q != '#') {
        return 0;
    }
    q++;
    hex = (q < lim && (*q == 'x' || *q == 'X'));
    if (hex) {
        q++;
    }
    while (q < lim) {
        int d;
        if (hex) {
            d = ps_hexval(*q);
            if (d < 0) {
                break;
            }
        } else {
            if (*q < '0' || *q > '9') {
                break;
            }
            d = *q - '0';
        }
        if (code <= 0x11000000L) {
            code = code * (hex ? 16 : 10) + d;
        }
        digits++;
        q++;
    }
    if (digits == 0 || q >= lim || *q != ';' || code > 0x10FFFFL) {
        return 0;
    }
    *cp = (unsigned int) code;
    return (size_t) (q - p);
}

/* php_unescape_html_entities() */
static zend_string *ps_unescape_entities(const zend_string *in, int all,
                                         zend_long flags, int utf8)
{
    ps_buf b;
    const char *p = ZSTR_VAL(in);
    const char *lim = p + ZSTR_LEN(in);
    int doctype = (int) (flags & PS_ENT_DOC_TYPE_MASK);

    ps_buf_init(&b);
    while (p < lim) {
        unsigned int code = 0;
        size_t body_len;

        /* php-src assumes there are no single-character entities. */
        if (p[0] != '&' || (p + 3 >= lim)) {
            ps_buf_char(&b, *p++);
            continue;
        }
        if (p[1] == '#') {
            body_len = ps_scan_numeric_entity(p + 1, lim, &code);
            if (body_len == 0) {
                ps_buf_char(&b, *p++);
                continue;
            }
            if (!all && !(code == '"' || code == '&' || code == '\''
                          || code == '<' || code == '>')) {
                ps_buf_char(&b, *p++);
                continue;
            }
            if (!ps_unicode_cp_is_allowed(code, doctype)
                || (doctype == PS_ENT_DOC_HTML5 && code == 0x0D)) {
                ps_buf_char(&b, *p++);
                continue;
            }
        } else {
            body_len = ps_scan_named_entity(p + 1, lim);
            if (body_len == 0
                || !ps_resolve_named_entity(p + 1, body_len, all, doctype, &code)) {
                ps_buf_char(&b, *p++);
                continue;
            }
        }
        if ((code == '\'' && !(flags & PS_ENT_QUOTE_SINGLE))
            || (code == '"' && !(flags & PS_ENT_QUOTE_DOUBLE))) {
            ps_buf_char(&b, *p++);
            continue;
        }
        if (!utf8) {
            if (code > 0xFF) {
                ps_buf_char(&b, *p++);
                continue;
            }
            ps_buf_char(&b, (char) code);
        } else {
            unsigned char tmp[4];
            size_t n = ps_utf32_utf8(tmp, code);
            ps_buf_append(&b, (const char *) tmp, n);
        }
        p += 1 + body_len + 1;     /* '&' + body + ';' */
    }
    return ps_buf_finish(&b);
}

/* php_escape_html_entities_ex(), restricted to all == 0 (htmlspecialchars). */
static zend_string *ps_escape_specialchars(const zend_string *in, zend_long flags,
                                           int utf8, int double_encode, int *failed)
{
    ps_buf b;
    const unsigned char *s = (const unsigned char *) ZSTR_VAL(in);
    size_t len = ZSTR_LEN(in);
    size_t cursor = 0;
    int doctype = (int) (flags & PS_ENT_DOC_TYPE_MASK);
    const char *replacement = utf8 ? "\xEF\xBF\xBD" : "&#xFFFD;";
    size_t replacement_len = utf8 ? 3 : 8;

    *failed = 0;
    ps_buf_init(&b);
    while (cursor < len) {
        size_t before = cursor;
        int status = 1;
        unsigned int this_char;

        if (utf8) {
            this_char = ps_utf8_next(s, len, &cursor, &status);
        } else {
            this_char = s[cursor];
            cursor++;
        }
        if (!status) {
            if (flags & PS_ENT_IGNORE) {
                continue;
            }
            if (flags & PS_ENT_SUBSTITUTE) {
                ps_buf_append(&b, replacement, replacement_len);
                continue;
            }
            free(b.buf);
            ps_buf_init(&b);
            *failed = 1;
            return zend_string_init("", 0);
        }

        if (this_char != '&') {
            const char *rep = NULL;

            if ((this_char == '\'' && !(flags & PS_ENT_QUOTE_SINGLE))
                || (this_char == '"' && !(flags & PS_ENT_QUOTE_DOUBLE))) {
                goto pass_through;
            }
            switch (this_char) {
            case '"':  rep = "&quot;"; break;
            case '\'': rep = "&#039;"; break;
            case '<':  rep = "&lt;"; break;
            case '>':  rep = "&gt;"; break;
            default:   rep = NULL; break;
            }
            if (rep) {
                ps_buf_append(&b, rep, strlen(rep));
                continue;
            }
            if (flags & 128 /* ENT_DISALLOWED */) {
                if (!ps_unicode_cp_is_allowed(this_char, doctype)) {
                    ps_buf_append(&b, replacement, replacement_len);
                    continue;
                }
            }
pass_through:
            ps_buf_append(&b, (const char *) s + before, cursor - before);
        } else {
            size_t ent_len = 0;
            unsigned int code = 0;

            if (!double_encode) {
                const char *p = (const char *) s + cursor;
                const char *lim = (const char *) s + len;
                if (p < lim && *p == '#') {
                    ent_len = ps_scan_numeric_entity(p, lim, &code);
                    if (ent_len && (flags & 128)
                        && !ps_unicode_cp_is_allowed(code, doctype)) {
                        ent_len = 0;
                    }
                } else {
                    ent_len = ps_scan_named_entity(p, lim);
                    if (ent_len
                        && !ps_resolve_named_entity(p, ent_len, 1, doctype, &code)) {
                        ent_len = 0;
                    }
                }
            }
            if (ent_len) {
                ps_buf_char(&b, '&');
                ps_buf_append(&b, (const char *) s + cursor, ent_len);
                ps_buf_char(&b, ';');
                cursor += ent_len + 1;
            } else {
                ps_buf_append(&b, "&amp;", 5);
            }
        }
    }
    return ps_buf_finish(&b);
}

static void zif_htmlspecialchars(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("htmlspecialchars", 1, 4);
    zend_string *s;
    if (!zend_parse_arg_string(&argv[0], &s, 1, "htmlspecialchars")) return;
    /* VERSIONGATE:80100 the ENT_COMPAT|ENT_HTML401 default below is the 5.6
     * branch; 8.1 defaults to ENT_QUOTES|ENT_SUBSTITUTE|ENT_HTML401. */
    zend_long flags = PS_ENT_COMPAT | PS_ENT_DOC_HTML401;
    PS_OPT_LONG(flags, 1, "htmlspecialchars");
    zend_string *charset = NULL;
    int double_encode = 1;
    PS_OPT_BOOL(double_encode, 3, "htmlspecialchars");
    int failed = 0;

    if (argc > 2 && Z_TYPE(argv[2]) != IS_NULL) {
        if (!zend_parse_arg_string(&argv[2], &charset, 3, "htmlspecialchars")) return;
        if (!ps_charset_known(charset, "htmlspecialchars")) {
            charset = NULL;
        }
    }
    ZVAL_STR(ret, ps_escape_specialchars(s, flags, ps_charset_is_utf8(charset),
                                         double_encode, &failed));
}

static void zif_htmlspecialchars_decode(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("htmlspecialchars_decode", 1, 2);
    zend_string *s;
    if (!zend_parse_arg_string(&argv[0], &s, 1, "htmlspecialchars_decode")) return;
    zend_long flags = PS_ENT_COMPAT | PS_ENT_DOC_HTML401;
    PS_OPT_LONG(flags, 1, "htmlspecialchars_decode");
    /* php-src decodes the basic set with a single-byte charset. */
    ZVAL_STR(ret, ps_unescape_entities(s, 0, flags, 0));
}

static void zif_html_entity_decode(zval *ret, uint32_t argc, zval *argv)
{
    PS_ARGC("html_entity_decode", 1, 3);
    zend_string *s;
    if (!zend_parse_arg_string(&argv[0], &s, 1, "html_entity_decode")) return;
    zend_long flags = PS_ENT_COMPAT | PS_ENT_DOC_HTML401;
    PS_OPT_LONG(flags, 1, "html_entity_decode");
    zend_string *charset = NULL;

    if (argc > 2 && Z_TYPE(argv[2]) != IS_NULL) {
        if (!zend_parse_arg_string(&argv[2], &charset, 3, "html_entity_decode")) return;
        if (!ps_charset_known(charset, "html_entity_decode")) {
            charset = NULL;
        }
    }
    ZVAL_STR(ret, ps_unescape_entities(s, 1, flags, ps_charset_is_utf8(charset)));
}

/* ------------------------------------------------------------- registry */

const php_function_entry php_functions_string[] = {
    { "strlen",                 zif_strlen, 0, 1, NULL,   0, 0 },
    { "strtolower",             zif_strtolower, 0, 1, NULL,   0, 0 },
    { "strtoupper",             zif_strtoupper, 0, 1, NULL,   0, 0 },
    { "ucfirst",                zif_ucfirst, 0, 1, NULL,   0, 0 },
    { "lcfirst",                zif_lcfirst, 0, 1, NULL,   0, 0 },
    { "ucwords",                zif_ucwords, 0, 2, NULL,   0, 0 },
    { "trim",                   zif_trim, 0, 2, NULL,   0, 0 },
    { "ltrim",                  zif_ltrim, 0, 2, NULL,   0, 0 },
    { "rtrim",                  zif_rtrim, 0, 2, NULL,   0, 0 },
    { "chop",                   zif_chop, 0, 2, NULL,   0, 0 },
    { "str_repeat",             zif_str_repeat, 0, 2, NULL,   0, 0 },
    { "strrev",                 zif_strrev, 0, 1, NULL,   0, 0 },
    { "strpos",                 zif_strpos, 0, 3, NULL,   0, 0 },
    { "stripos",                zif_stripos, 0, 3, NULL,   0, 0 },
    { "strrpos",                zif_strrpos, 0, 3, NULL,   0, 0 },
    { "strripos",               zif_strripos, 0, 3, NULL,   0, 0 },
    { "strstr",                 zif_strstr, 0, 3, NULL,   0, 0 },
    { "strchr",                 zif_strchr, 0, 3, NULL,   0, 0 },
    { "stristr",                zif_stristr, 0, 3, NULL,   0, 0 },
    { "strrchr",                zif_strrchr, 0, 2, NULL,   0, 0 },
    { "substr",                 zif_substr, 0, 3, NULL,   0, 0 },
    { "substr_count",           zif_substr_count, 0, 4, NULL,   0, 0 },
    { "substr_replace",         zif_substr_replace, 0, 4, NULL,   0, 0 },
    { "str_replace",            zif_str_replace, 0, 4, "0001", 0, 0 },
    { "str_ireplace",           zif_str_ireplace, 0, 4, "0001", 0, 0 },
    { "str_pad",                zif_str_pad, 0, 4, NULL,   0, 0 },
    { "str_split",              zif_str_split, 0, 2, NULL,   0, 0 },
    { "explode",                zif_explode, 0, 3, NULL,   0, 0 },
    { "implode",                zif_implode, 0, 2, NULL,   0, 0 },
    { "join",                   zif_join, 0, 2, NULL,   0, 0 },
    { "sprintf",                zif_sprintf, 0, UINT32_MAX, NULL, 0, 0 },
    { "vsprintf",               zif_vsprintf, 0, UINT32_MAX, NULL, 0, 0 },
    { "printf",                 zif_printf, 0, UINT32_MAX, NULL, 0, 0 },
    { "vprintf",                zif_vprintf, 0, UINT32_MAX, NULL, 0, 0 },
    { "number_format",          zif_number_format, 0, 4, NULL,   0, 0 },
    { "nl2br",                  zif_nl2br, 0, 2, NULL,   0, 0 },
    { "htmlspecialchars",       zif_htmlspecialchars, 0, 4, NULL,   0, 0 },
    { "htmlspecialchars_decode", zif_htmlspecialchars_decode, 0, 2, NULL,  0, 0 },
    { "html_entity_decode",     zif_html_entity_decode, 0, 3, NULL,   0, 0 },
    { "addslashes",             zif_addslashes, 0, 1, NULL,   0, 0 },
    { "stripslashes",           zif_stripslashes, 0, 1, NULL,   0, 0 },
    { "chr",                    zif_chr, 0, 1, NULL,   0, 0 },
    { "ord",                    zif_ord, 0, 1, NULL,   0, 0 },



    { "wordwrap",               zif_wordwrap, 0, 4, NULL,   0, 0 },
    { "strcmp",                 zif_strcmp, 0, 2, NULL,   0, 0 },
    { "strcasecmp",             zif_strcasecmp, 0, 2, NULL,   0, 0 },
    { "strncmp",                zif_strncmp, 0, 3, NULL,   0, 0 },
    { "strncasecmp",            zif_strncasecmp, 0, 3, NULL,   0, 0 },
    { "strnatcmp",              zif_strnatcmp, 0, 2, NULL,   0, 0 },
    { "strnatcasecmp",          zif_strnatcasecmp, 0, 2, NULL,   0, 0 },
    { "str_word_count",         zif_str_word_count, 0, 3, NULL,   0, 0 },
    { "levenshtein",            zif_levenshtein, 0, 5, NULL,   0, 0 },
    { "similar_text",           zif_similar_text, 0, 3, "001",  0, 0 },
    { "soundex",                zif_soundex, 0, 1, NULL,   0, 0 },
    { "metaphone",              zif_metaphone, 0, 2, NULL,   0, 0 },
    { "quotemeta",              zif_quotemeta, 0, 1, NULL,   0, 0 },
    { "strtr",                  zif_strtr, 0, 3, NULL,   0, 0 },
    { "str_shuffle",            zif_str_shuffle, 0, 1, NULL,   0, 0 },
    { "chunk_split",            zif_chunk_split, 0, 3, NULL,   0, 0 },
    { "bin2hex",                zif_bin2hex, 0, 1, NULL,   0, 0 },
    { "hex2bin",                zif_hex2bin, 0, 1, NULL,   0, 0 },
    { "strpbrk",                zif_strpbrk, 0, 2, NULL,   0, 0 },
    { "substr_compare",         zif_substr_compare, 0, 5, NULL,   0, 0 },
    { NULL, NULL, 0, 0, NULL, 0, 0 }
};

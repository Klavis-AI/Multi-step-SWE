/* ext/standard/php_array.c -- array builtins.
 *
 * Behaviour target: PHP 5.6.40 (php-src ext/standard/array.c).  Every place
 * where a later release changes observable behaviour carries a
 * "VERSIONGATE:<version-id>" comment marker on the line above the deciding
 * `if`, naming which branch is the 5.6 one.  A later pass turns each of those
 * into a runtime test on EG(php_version); nothing post-5.6 is implemented
 * here.  Functions that do not exist in 5.6 carry the same marker above their
 * definition and above their registry row so both can be stripped together.
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

#define PA_MIN(a, b) ((a) < (b) ? (a) : (b))
#define PA_MAX(a, b) ((a) > (b) ? (a) : (b))

/* sort flags */
#define PA_SORT_REGULAR        0
#define PA_SORT_NUMERIC        1
#define PA_SORT_STRING         2
#define PA_SORT_DESC           3
#define PA_SORT_ASC            4
#define PA_SORT_LOCALE_STRING  5
#define PA_SORT_NATURAL        6
#define PA_SORT_FLAG_CASE      8

#define PA_COUNT_NORMAL        0
#define PA_COUNT_RECURSIVE     1

#define PA_CASE_LOWER          0
#define PA_CASE_UPPER          1

#define PA_FILTER_USE_BOTH     1
#define PA_FILTER_USE_KEY      2

#define PA_EXTR_OVERWRITE        0
#define PA_EXTR_SKIP             1
#define PA_EXTR_PREFIX_SAME      2
#define PA_EXTR_PREFIX_ALL       3
#define PA_EXTR_PREFIX_INVALID   4
#define PA_EXTR_PREFIX_IF_EXISTS 5
#define PA_EXTR_IF_EXISTS        6
#define PA_EXTR_REFS             0x100

/* ---------------------------------------------------------------- basics */

static void pa_ret_false(zval *ret)
{
    ZVAL_BOOL(ret, 0);
}

static void pa_ret_true(zval *ret)
{
    ZVAL_BOOL(ret, 1);
}

static zend_long pa_opt_long(uint32_t argc, zval *argv, uint32_t i, zend_long def)
{
    return argc > i ? zval_get_long(&argv[i]) : def;
}

/* A by-reference parameter arrives as IS_REFERENCE. */
static zval *pa_ref(zval *arg)
{
    if (Z_TYPE_P(arg) == IS_REFERENCE) {
        return &Z_REF_P(arg)->val;
    }
    return arg;
}

static zend_string *pa_zs(const char *s)
{
    return zend_string_initz(s);
}

static zend_string *pa_dup_key(const zend_string *k)
{
    return zend_string_init(ZSTR_VAL(k), ZSTR_LEN(k));
}

/* array_key_normalize() is owned by the evaluator.  Its return convention is
 * not pinned down by the frozen header, so which flavour of key came back is
 * detected from the out-parameters instead: a string key sets *str. */
static int pa_key_is_long(const zval *key, zend_long *idx, zend_string **str)
{
    *str = NULL;
    *idx = 0;
    (void) array_key_normalize(key, idx, str);
    return *str == NULL;
}

static void pa_set_zval_key(HashTable *ht, const zval *key, zval val)
{
    zend_long idx;
    zend_string *str;

    if (pa_key_is_long(key, &idx, &str)) {
        zend_hash_index_update(ht, idx, val);
    } else {
        zend_hash_update(ht, str, val);
    }
}

static zval *pa_find_zval_key(HashTable *ht, const zval *key)
{
    zend_long idx;
    zend_string *str;

    if (pa_key_is_long(key, &idx, &str)) {
        return zend_hash_index_find(ht, idx);
    }
    return zend_hash_find(ht, str);
}

static zval pa_bucket_key(const Bucket *b)
{
    zval z;
    if (b->key) {
        ZVAL_STR(&z, pa_dup_key(b->key));
    } else {
        ZVAL_LONG(&z, (zend_long) b->h);
    }
    return z;
}

static void pa_put_bucket(HashTable *dst, const Bucket *b, zval val)
{
    if (b->key) {
        zend_hash_update(dst, pa_dup_key(b->key), val);
    } else {
        zend_hash_index_update(dst, (zend_long) b->h, val);
    }
}

/* Does `ht` contain a key equal to the one of `b`? */
static zval *pa_find_bucket_key(HashTable *ht, const Bucket *b)
{
    if (b->key) {
        return zend_hash_find(ht, b->key);
    }
    return zend_hash_index_find(ht, (zend_long) b->h);
}

/* ------------------------------------------------------------ comparison */

static int ps_cmp_norm(int r)
{
    return r < 0 ? -1 : (r > 0 ? 1 : 0);
}

static int pa_binary_strcmp(const char *a, size_t alen, const char *b, size_t blen)
{
    size_t n = PA_MIN(alen, blen), i;
    for (i = 0; i < n; i++) {
        int d = (int) (unsigned char) a[i] - (int) (unsigned char) b[i];
        if (d != 0) {
            return d;
        }
    }
    return (int) ((zend_long) alen - (zend_long) blen);
}

static char pa_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
}

static char pa_upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char) (c - 'a' + 'A') : c;
}

static int pa_binary_strcasecmp(const char *a, size_t alen, const char *b, size_t blen)
{
    size_t n = PA_MIN(alen, blen), i;
    for (i = 0; i < n; i++) {
        int c1 = (unsigned char) pa_lower(a[i]);
        int c2 = (unsigned char) pa_lower(b[i]);
        if (c1 != c2) {
            return c1 - c2;
        }
    }
    return (int) ((zend_long) alen - (zend_long) blen);
}

/* strnatcmp_ex(), duplicated here because the two extension files are
 * independent translation units. */
static int pa_cmp_right(const char **a, const char *aend,
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

static int pa_cmp_left(const char **a, const char *aend,
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

static int pa_strnatcmp(const char *a, size_t a_len, const char *b, size_t b_len,
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
            result = fractional ? pa_cmp_left(&ap, aend, &bp, bend)
                                : pa_cmp_right(&ap, aend, &bp, bend);
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
            ca = (unsigned char) pa_upper((char) ca);
            cb = (unsigned char) pa_upper((char) cb);
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

/* The comparison used by sort()/asort()/... for a given SORT_* flag. */
static int pa_compare_flag(const zval *a, const zval *b, zend_long sort_type)
{
    int ci = (sort_type & PA_SORT_FLAG_CASE) != 0;

    switch (sort_type & ~PA_SORT_FLAG_CASE) {
    case PA_SORT_NUMERIC: {
        double da = zval_get_double(a);
        double db = zval_get_double(b);
        return da < db ? -1 : (da > db ? 1 : 0);
    }
    case PA_SORT_STRING:
    case PA_SORT_LOCALE_STRING: {
        zend_string *sa = zval_get_string(a);
        zend_string *sb = zval_get_string(b);
        int r = ci ? pa_binary_strcasecmp(ZSTR_VAL(sa), ZSTR_LEN(sa),
                                          ZSTR_VAL(sb), ZSTR_LEN(sb))
                   : pa_binary_strcmp(ZSTR_VAL(sa), ZSTR_LEN(sa),
                                      ZSTR_VAL(sb), ZSTR_LEN(sb));
        return ps_cmp_norm(r);
    }
    case PA_SORT_NATURAL: {
        zend_string *sa = zval_get_string(a);
        zend_string *sb = zval_get_string(b);
        return ps_cmp_norm(pa_strnatcmp(ZSTR_VAL(sa), ZSTR_LEN(sa),
                                        ZSTR_VAL(sb), ZSTR_LEN(sb), ci));
    }
    case PA_SORT_REGULAR:
    default: {
        int r = 0;
        if (zend_compare(a, b, &r) != 0) {
            return 0;
        }
        return ps_cmp_norm(r);
    }
    }
}

/* array_diff()/array_intersect() compare values as strings. */
static int pa_string_equal(const zval *a, const zval *b)
{
    zend_string *sa = zval_get_string(a);
    zend_string *sb = zval_get_string(b);
    return ZSTR_LEN(sa) == ZSTR_LEN(sb)
        && memcmp(ZSTR_VAL(sa), ZSTR_VAL(sb), ZSTR_LEN(sa)) == 0;
}

/* ---------------------------------------------------- snapshot and sorting */

typedef struct {
    zend_string *key;      /* NULL for an integer key */
    zend_ulong   h;
    zval         val;
    uint32_t     order;    /* original position, used to keep sorts stable */
} pa_entry;

static pa_entry *pa_snapshot(HashTable *ht, uint32_t *out_n)
{
    uint32_t cnt = zend_hash_num_elements(ht);
    pa_entry *arr = cnt ? (pa_entry *) calloc(cnt, sizeof(pa_entry)) : NULL;
    uint32_t i = 0;

    if (cnt) {
        ZEND_HASH_FOREACH(ht, b) {
            if (i >= cnt) {
                break;
            }
            arr[i].key = b->key ? pa_dup_key(b->key) : NULL;
            arr[i].h = b->h;
            arr[i].val = zval_copy(b->val);
            arr[i].order = i;
            i++;
        } ZEND_HASH_FOREACH_END();
    }
    *out_n = i;
    return arr;
}

static void pa_rebuild(HashTable *ht, pa_entry *arr, uint32_t n, int renumber)
{
    uint32_t i;

    zend_hash_clean(ht);
    for (i = 0; i < n; i++) {
        if (renumber) {
            zend_hash_next_index_insert(ht, arr[i].val);
        } else if (arr[i].key) {
            zend_hash_update(ht, arr[i].key, arr[i].val);
        } else {
            zend_hash_index_update(ht, (zend_long) arr[i].h, arr[i].val);
        }
    }
    ht->nInternalPointer = 0;
    free(arr);
}

typedef int (*pa_cmp_fn)(const pa_entry *a, const pa_entry *b, void *ctx);

static void pa_merge(pa_entry *src, pa_entry *dst, uint32_t lo, uint32_t mid,
                     uint32_t hi, pa_cmp_fn cmp, void *ctx)
{
    uint32_t i = lo, j = mid, k = lo;

    while (i < mid && j < hi) {
        if (cmp(&src[j], &src[i], ctx) < 0) {
            dst[k++] = src[j++];
        } else {
            dst[k++] = src[i++];
        }
    }
    while (i < mid) {
        dst[k++] = src[i++];
    }
    while (j < hi) {
        dst[k++] = src[j++];
    }
}

static void pa_msort_rec(pa_entry *a, pa_entry *tmp, uint32_t lo, uint32_t hi,
                         pa_cmp_fn cmp, void *ctx)
{
    uint32_t mid;

    if (hi - lo < 2) {
        return;
    }
    mid = lo + (hi - lo) / 2;
    pa_msort_rec(a, tmp, lo, mid, cmp, ctx);
    pa_msort_rec(a, tmp, mid, hi, cmp, ctx);
    pa_merge(a, tmp, lo, mid, hi, cmp, ctx);
    memcpy(a + lo, tmp + lo, (size_t) (hi - lo) * sizeof(pa_entry));
}

/* Zend/zend_qsort.c as of 5.6: an unstable quicksort with a middle pivot.
 * php-src sorts an array of Bucket* with it, but only the sequence of
 * comparisons and swaps is observable, so the same walk over pa_entry gives
 * the same permutation.  php-src's byte arithmetic
 *     offset = (end - begin) >> 1;  begin + (offset - offset % siz)
 * is exactly "begin + (element count - 1) / 2" for any even element size. */
static void pa_swap_entries(pa_entry *a, pa_entry *b)
{
    pa_entry t = *a;
    *a = *b;
    *b = t;
}

static void pa_qsort(pa_entry *base, uint32_t nmemb, pa_cmp_fn cmp, void *ctx)
{
    long begin_stack[8 * sizeof(size_t)];
    long end_stack[8 * sizeof(size_t)];
    long begin, end, seg1, seg2, seg2p;
    int loop;

    if (nmemb < 2) {
        return;
    }
    begin_stack[0] = 0;
    end_stack[0] = (long) nmemb - 1;

    for (loop = 0; loop >= 0; --loop) {
        begin = begin_stack[loop];
        end = end_stack[loop];

        while (begin < end) {
            pa_swap_entries(&base[begin], &base[begin + (end - begin) / 2]);

            seg1 = begin + 1;
            seg2 = end;
            for (;;) {
                for (; seg1 < seg2 && cmp(&base[begin], &base[seg1], ctx) > 0; seg1++) {
                    /* advance */
                }
                for (; seg2 >= seg1 && cmp(&base[seg2], &base[begin], ctx) > 0; seg2--) {
                    /* retreat */
                }
                if (seg1 >= seg2) {
                    break;
                }
                pa_swap_entries(&base[seg1], &base[seg2]);
                seg1++;
                seg2--;
            }
            pa_swap_entries(&base[begin], &base[seg2]);
            seg2p = seg2;

            if ((seg2p - begin) <= (end - seg2p)) {
                if ((seg2p + 1) < end) {
                    begin_stack[loop] = seg2p + 1;
                    end_stack[loop++] = end;
                }
                end = seg2p - 1;
            } else {
                if ((seg2p - 1) > begin) {
                    begin_stack[loop] = begin;
                    end_stack[loop++] = seg2p - 1;
                }
                begin = seg2p + 1;
            }
        }
    }
}

static void pa_sort_entries(pa_entry *a, uint32_t n, pa_cmp_fn cmp, void *ctx)
{
    pa_entry *tmp;

    if (n < 2) {
        return;
    }
    /* VERSIONGATE:80000 else-branch is the 5.6/7.x behaviour: php-src's sort
     * only became stable in 8.0, and the tests pin the exact permutation an
     * unstable sort produces for elements that compare equal. */
    if (EG(php_version) >= 80000) {
        tmp = (pa_entry *) calloc(n, sizeof(pa_entry));
        pa_msort_rec(a, tmp, 0, n, cmp, ctx);
        free(tmp);
        return;
    }
    pa_qsort(a, n, cmp, ctx);
}

typedef struct {
    zend_long sort_type;
    int       reverse;
    zval     *callable;
    int       failed;
} pa_sort_ctx;

static int pa_cmp_data(const pa_entry *a, const pa_entry *b, void *ctxv)
{
    pa_sort_ctx *ctx = (pa_sort_ctx *) ctxv;
    int r = pa_compare_flag(&a->val, &b->val, ctx->sort_type);
    return ctx->reverse ? -r : r;
}

static int pa_cmp_key(const pa_entry *a, const pa_entry *b, void *ctxv)
{
    pa_sort_ctx *ctx = (pa_sort_ctx *) ctxv;
    zval ka, kb;
    int r;

    if (a->key) {
        ZVAL_STR(&ka, a->key);
    } else {
        ZVAL_LONG(&ka, (zend_long) a->h);
    }
    if (b->key) {
        ZVAL_STR(&kb, b->key);
    } else {
        ZVAL_LONG(&kb, (zend_long) b->h);
    }
    r = pa_compare_flag(&ka, &kb, ctx->sort_type);
    return ctx->reverse ? -r : r;
}

static int pa_cmp_natural(const pa_entry *a, const pa_entry *b, void *ctxv)
{
    pa_sort_ctx *ctx = (pa_sort_ctx *) ctxv;
    zend_string *sa = zval_get_string(&a->val);
    zend_string *sb = zval_get_string(&b->val);
    return pa_strnatcmp(ZSTR_VAL(sa), ZSTR_LEN(sa), ZSTR_VAL(sb), ZSTR_LEN(sb),
                        ctx->sort_type);
}

static int pa_call_cmp(zval *callable, zval *a, zval *b, int *failed)
{
    zval args[2];
    zval retval;
    zend_long r;

    if (*failed) {
        return 0;
    }
    args[0] = *a;
    args[1] = *b;
    ZVAL_NULL(&retval);
    if (zend_call_user_callable(callable, 2, args, &retval) != 0
        || EG(exception) != NULL) {
        *failed = 1;
        return 0;
    }
    r = zval_get_long(&retval);
    return r < 0 ? -1 : (r > 0 ? 1 : 0);
}

static int pa_cmp_user_data(const pa_entry *a, const pa_entry *b, void *ctxv)
{
    pa_sort_ctx *ctx = (pa_sort_ctx *) ctxv;
    return pa_call_cmp(ctx->callable, (zval *) &a->val, (zval *) &b->val,
                       &ctx->failed);
}

static int pa_cmp_user_key(const pa_entry *a, const pa_entry *b, void *ctxv)
{
    pa_sort_ctx *ctx = (pa_sort_ctx *) ctxv;
    zval ka, kb;

    if (a->key) {
        ZVAL_STR(&ka, a->key);
    } else {
        ZVAL_LONG(&ka, (zend_long) a->h);
    }
    if (b->key) {
        ZVAL_STR(&kb, b->key);
    } else {
        ZVAL_LONG(&kb, (zend_long) b->h);
    }
    return pa_call_cmp(ctx->callable, &ka, &kb, &ctx->failed);
}

/* The name zend_parse_parameters() gives the value it was handed.  5.6 uses
 * gettype()'s spelling with a lower case "null"; 7.0 renames double to float;
 * 7.3 renames integer/boolean to int/bool, which is also what 8.0's TypeError
 * uses.
 * VERSIONGATE:70300 else-branch is the 5.6/7.0-7.2 spelling */
static const char *pa_type_name(const zval *v)
{
    if (EG(php_version) >= 70300) {
        return zend_zval_type_name(v);
    }
    switch (Z_TYPE_P(v)) {
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
static const char *pa_want_long(void)
{
    if (EG(php_version) >= 70300) {
        return "int";
    }
    /* VERSIONGATE:70000 else-branch is the 5.6 spelling */
    return EG(php_version) >= 70000 ? "integer" : "long";
}

/* VERSIONGATE:70300 else-branch is the 5.6/7.0-7.2 spelling */
static const char *pa_want_bool(void)
{
    return EG(php_version) >= 70300 ? "bool" : "boolean";
}

/* ------------------------------------------------- argument prologue ----
 * The evaluator rejects too *few* arguments and nothing else, and it converts
 * nothing, so each builtin runs php-src's zend_parse_parameters() checks
 * itself.  Every macro returns from the calling handler when a check fails,
 * leaving *ret at IS_NULL exactly as php-src leaves return_value.
 */
#define PA_ARGC(fname, mn, mx)                                                \
    do {                                                                      \
        if (argc < (uint32_t) (mn) || argc > (uint32_t) (mx)) {               \
            zend_wrong_param_count((fname), argc, (mn), (mx));                \
            return;                                                           \
        }                                                                     \
    } while (0)

/* VERSIONGATE:80000 else-branch is the 5.6 form: a warning, not a throw */
static int pa_arg_error(const zval *v, int argn, const char *fname,
                        const char *name56, const char *name80)
{
    if (EG(php_version) >= 80000) {
        zend_throw_error("TypeError", "%s(): Argument #%d must be of type %s, "
                         "%s given", fname, argn, name80, zend_zval_type_name(v));
    } else {
        zend_error(E_WARNING, "%s() expects parameter %d to be %s, %s given",
                   fname, argn, name56, pa_type_name(v));
    }
    return 0;
}

static int pa_array_error(const zval *v, int argn, const char *fname)
{
    return pa_arg_error(v, argn, fname, "array", "array");
}

static int pa_arg_long(zval *arg, zend_long *dest, int argn, const char *fname)
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
            return pa_arg_error(a, argn, fname, pa_want_long(), "int");
        }
        if (is_numeric_string(ZSTR_VAL(Z_STR_P(a)), ZSTR_LEN(Z_STR_P(a)),
                              NULL, NULL, 0) == 0) {
            /* VERSIONGATE:80000 else-branch is the 5.6/7.x behaviour: a
             * trailing-garbage numeric string is accepted with a notice; 8.0
             * rejects it outright. */
            if (EG(php_version) >= 80000) {
                return pa_arg_error(a, argn, fname, pa_want_long(), "int");
            }
            zend_error(E_NOTICE, "A non well formed numeric value encountered");
        }
        *dest = (kind == IS_LONG) ? lval : (zend_long) dval;
        return 1;
    }
    default:
        return pa_arg_error(a, argn, fname, pa_want_long(), "int");
    }
}

static int pa_arg_bool(zval *arg, int *dest, int argn, const char *fname)
{
    zval *a = zval_deref(arg);

    switch (Z_TYPE_P(a)) {
    case IS_UNDEF: case IS_NULL: case IS_FALSE: case IS_TRUE:
    case IS_LONG: case IS_DOUBLE: case IS_STRING:
        *dest = zval_is_true(a);
        return 1;
    default:
        return pa_arg_error(a, argn, fname, pa_want_bool(), "bool");
    }
}

/* php-src spells the callable diagnostics with single quotes around the name,
 * so the engine's message (which uses double quotes) is re-rendered here. */
static int pa_arg_callable(zval *arg, int argn, const char *fname)
{
    zend_string *err = NULL;
    zval *c = zval_deref(arg);

    if (zend_is_callable_zval(arg, NULL, NULL, NULL, &err)) {
        return 1;
    }
    if (Z_TYPE_P(c) != IS_STRING && Z_TYPE_P(c) != IS_ARRAY
        && Z_TYPE_P(c) != IS_OBJECT) {
        /* VERSIONGATE:80000 else-branch is the 5.6 form: a warning, not a throw */
        if (EG(php_version) >= 80000) {
            zend_throw_error("TypeError", "%s(): Argument #%d must be a valid "
                             "callback, no array or string given", fname, argn);
        } else {
            zend_error(E_WARNING, "%s() expects parameter %d to be a valid "
                       "callback, no array or string given", fname, argn);
        }
        return 0;
    }
    {
        char buf[512];
        size_t i, n = 0;
        const char *e = err ? ZSTR_VAL(err) : "not callable";
        const char *mid;

        /* The engine reports the same failures with a different house style
         * (double quotes, "array callback"); php-src's exact wording is what
         * the tests pin, so re-render it here. */
        if (strncmp(e, "array callback ", 15) == 0
            || strncmp(e, "array callback has to contain", 29) == 0) {
            e = "array must have exactly two members";
        }
        for (i = 0; e[i] != '\0' && n < sizeof(buf) - 1; i++) {
            buf[n++] = (e[i] == '"') ? '\'' : e[i];
        }
        buf[n] = '\0';
        mid = strstr(buf, " does not have a method ");
        if (mid != NULL && strncmp(buf, "class ", 6) == 0) {
            char fixed[512];
            size_t clen = (size_t) (mid - (buf + 6));
            snprintf(fixed, sizeof(fixed), "class '%.*s'%s", (int) clen,
                     buf + 6, mid);
            memcpy(buf, fixed, sizeof(buf) < sizeof(fixed) ? sizeof(buf) : sizeof(fixed));
            buf[sizeof(buf) - 1] = '\0';
        }
        /* VERSIONGATE:80000 else-branch is the 5.6 form: a warning, not a throw */
        if (EG(php_version) >= 80000) {
            zend_throw_error("TypeError", "%s(): Argument #%d must be a valid "
                             "callback, %s", fname, argn, buf);
        } else {
            zend_error(E_WARNING, "%s() expects parameter %d to be a valid "
                       "callback, %s", fname, argn, buf);
        }
    }
    return 0;
}

#define PA_LONG(var, idx, fname)                                              \
    do { if (!pa_arg_long(&argv[idx], &(var), (idx) + 1, (fname))) return; } while (0)
#define PA_OPT_LONG(var, idx, fname)                                          \
    do { if (argc > (uint32_t) (idx)                                          \
             && !pa_arg_long(&argv[idx], &(var), (idx) + 1, (fname))) return; \
    } while (0)
#define PA_OPT_BOOL(var, idx, fname)                                          \
    do { if (argc > (uint32_t) (idx)                                          \
             && !pa_arg_bool(&argv[idx], &(var), (idx) + 1, (fname))) return; \
    } while (0)

/* Every sort takes its array by reference; refuse non-arrays the way
 * zend_parse_parameters "a" does. */
static HashTable *pa_sort_arg(zval *arg, const char *fname)
{
    zval *v = pa_ref(arg);

    /* VERSIONGATE:80000 the warning-and-false branch below is the 5.6 one; 8.0
     * raises a TypeError. */
    if (Z_TYPE_P(v) != IS_ARRAY) {
        pa_array_error(v, 1, fname);
        return NULL;
    }
    zval_separate_array(v);
    return Z_ARR_P(v);
}

/* ------------------------------------------------------------ sort group */

static void pa_do_sort(zval *ret, uint32_t argc, zval *argv, const char *fname,
                       int by_key, int reverse, int renumber)
{
    HashTable *ht = pa_sort_arg(&argv[0], fname);
    pa_sort_ctx ctx;
    pa_entry *arr;
    uint32_t n;

    if (!ht) {
        pa_ret_false(ret);
        return;
    }
    ctx.sort_type = pa_opt_long(argc, argv, 1, PA_SORT_REGULAR);
    ctx.reverse = reverse;
    ctx.callable = NULL;
    ctx.failed = 0;

    arr = pa_snapshot(ht, &n);
    pa_sort_entries(arr, n, by_key ? pa_cmp_key : pa_cmp_data, &ctx);
    pa_rebuild(ht, arr, n, renumber);
    pa_ret_true(ret);
}

static void zif_sort(zval *ret, uint32_t argc, zval *argv)
{
    pa_ret_false(ret);   /* php-src returns false from this one on a parameter error */
    PA_ARGC("sort", 1, 2);
    pa_do_sort(ret, argc, argv, "sort", 0, 0, 1);
}

static void zif_rsort(zval *ret, uint32_t argc, zval *argv)
{
    pa_ret_false(ret);   /* php-src returns false from this one on a parameter error */
    PA_ARGC("rsort", 1, 2);
    pa_do_sort(ret, argc, argv, "rsort", 0, 1, 1);
}

static void zif_asort(zval *ret, uint32_t argc, zval *argv)
{
    pa_ret_false(ret);   /* php-src returns false from this one on a parameter error */
    PA_ARGC("asort", 1, 2);
    pa_do_sort(ret, argc, argv, "asort", 0, 0, 0);
}

static void zif_arsort(zval *ret, uint32_t argc, zval *argv)
{
    pa_ret_false(ret);   /* php-src returns false from this one on a parameter error */
    PA_ARGC("arsort", 1, 2);
    pa_do_sort(ret, argc, argv, "arsort", 0, 1, 0);
}

static void zif_ksort(zval *ret, uint32_t argc, zval *argv)
{
    pa_ret_false(ret);   /* php-src returns false from this one on a parameter error */
    PA_ARGC("ksort", 1, 2);
    pa_do_sort(ret, argc, argv, "ksort", 1, 0, 0);
}

static void zif_krsort(zval *ret, uint32_t argc, zval *argv)
{
    pa_ret_false(ret);   /* php-src returns false from this one on a parameter error */
    PA_ARGC("krsort", 1, 2);
    pa_do_sort(ret, argc, argv, "krsort", 1, 1, 0);
}

/* natsort()/natcasesort(): ctx.sort_type carries the fold-case flag for
 * pa_cmp_natural(). */
static void pa_do_natsort(zval *ret, zval *argv, const char *fname, int fold_case)
{
    HashTable *ht = pa_sort_arg(&argv[0], fname);
    pa_sort_ctx ctx;
    pa_entry *arr;
    uint32_t n;

    if (!ht) {
        pa_ret_false(ret);
        return;
    }
    ctx.sort_type = fold_case;
    ctx.reverse = 0;
    ctx.callable = NULL;
    ctx.failed = 0;

    arr = pa_snapshot(ht, &n);
    pa_sort_entries(arr, n, pa_cmp_natural, &ctx);
    pa_rebuild(ht, arr, n, 0);
    pa_ret_true(ret);
}

static void zif_natsort(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("natsort", 1, 1);
    (void) argc;
    pa_do_natsort(ret, argv, "natsort", 0);
}

static void zif_natcasesort(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("natcasesort", 1, 1);
    (void) argc;
    pa_do_natsort(ret, argv, "natcasesort", 1);
}

static void pa_do_usort(zval *ret, zval *argv, const char *fname,
                        pa_cmp_fn cmp, int renumber)
{
    HashTable *ht = pa_sort_arg(&argv[0], fname);
    pa_sort_ctx ctx;
    pa_entry *arr;
    uint32_t n;

    if (!ht) {
        pa_ret_false(ret);
        return;
    }
    if (!pa_arg_callable(&argv[1], 2, fname)) {
        return;
    }
    ctx.sort_type = PA_SORT_REGULAR;
    ctx.reverse = 0;
    ctx.callable = &argv[1];
    ctx.failed = 0;

    arr = pa_snapshot(ht, &n);
    pa_sort_entries(arr, n, cmp, &ctx);
    if (ctx.failed || EG(exception)) {
        free(arr);
        return;   /* propagate the pending exception, leave the array alone */
    }
    pa_rebuild(ht, arr, n, renumber);
    pa_ret_true(ret);
}

static void zif_usort(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("usort", 2, 2);
    (void) argc;
    pa_do_usort(ret, argv, "usort", pa_cmp_user_data, 1);
}

static void zif_uasort(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("uasort", 2, 2);
    (void) argc;
    pa_do_usort(ret, argv, "uasort", pa_cmp_user_data, 0);
}

static void zif_uksort(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("uksort", 2, 2);
    (void) argc;
    pa_do_usort(ret, argv, "uksort", pa_cmp_user_key, 0);
}

/* ----------------------------------------------------------------- count */

static zend_long pa_count_recursive(zval *array, zend_long mode)
{
    zend_long cnt = 0;

    if (Z_TYPE_P(array) == IS_ARRAY) {
        HashTable *ht = Z_ARR_P(array);
        if (ht->protect > 1) {
            zend_error(E_WARNING, "count(): recursion detected");
            return 0;
        }
        cnt = (zend_long) zend_hash_num_elements(ht);
        if (mode == PA_COUNT_RECURSIVE) {
            ZEND_HASH_FOREACH(ht, b) {
                ht->protect++;
                cnt += pa_count_recursive(zval_deref(&b->val), PA_COUNT_RECURSIVE);
                ht->protect--;
            } ZEND_HASH_FOREACH_END();
        }
    }
    return cnt;
}

static void pa_count_common(zval *ret, uint32_t argc, zval *argv,
                            const char *fname)
{
    zval *v = zval_deref(&argv[0]);
    zend_long mode = PA_COUNT_NORMAL;

    if (argc < 1 || argc > 2) {
        zend_wrong_param_count(fname, argc, 1, 2);
        return;
    }
    PA_OPT_LONG(mode, 1, fname);

    /* VERSIONGATE:80000 the else-branch (no check at all) is the 5.6/7.x
     * behaviour: an out-of-range $mode is simply not COUNT_RECURSIVE. */
    if (EG(php_version) >= 80000
        && mode != PA_COUNT_NORMAL && mode != PA_COUNT_RECURSIVE) {
        zend_throw_error("ValueError", "%s(): Argument #2 ($mode) must be either "
                         "COUNT_NORMAL or COUNT_RECURSIVE", fname);
        return;
    }

    if (Z_TYPE_P(v) == IS_ARRAY) {
        ZVAL_LONG(ret, pa_count_recursive(v, mode));
        return;
    }
    if (Z_TYPE_P(v) == IS_OBJECT) {
        zend_class_entry *countable = zend_lookup_class(pa_zs("Countable"), 0);
        if (countable != NULL && zend_instanceof(Z_OBJ_P(v)->ce, countable)) {
            zend_function *fn = zend_lookup_method(Z_OBJ_P(v)->ce, pa_zs("count"));
            if (fn != NULL) {
                zval r;
                ZVAL_NULL(&r);
                if (zend_call_function_ex(fn, Z_OBJ_P(v), Z_OBJ_P(v)->ce, 0, NULL,
                                          NULL, &r, NULL) == 0) {
                    ZVAL_LONG(ret, zval_get_long(&r));
                }
                return;
            }
        }
    }

    /* Neither an array nor a Countable object.
     * VERSIONGATE:80000 the 8.0 branch throws; VERSIONGATE:70200 the 7.2
     * branch warns and still returns a count; below 7.2 it is silent.  All
     * three return 0 for null and 1 for anything else. */
    if (EG(php_version) >= 80000) {
        zend_throw_error("TypeError", "%s(): Argument #1 ($value) must be of "
                         "type Countable|array, %s given", fname,
                         Z_TYPE_P(v) == IS_OBJECT
                             ? ZSTR_VAL(Z_OBJ_P(v)->ce->name)
                             : zend_zval_type_name(v));
        return;
    }
    if (EG(php_version) >= 70200) {
        zend_error(E_WARNING, "%s(): Parameter must be an array or an object "
                   "that implements Countable", fname);
    }
    ZVAL_LONG(ret, Z_TYPE_P(v) == IS_NULL ? 0 : 1);
}

static void zif_count(zval *ret, uint32_t argc, zval *argv)
{
    pa_count_common(ret, argc, argv, "count");
}

static void zif_sizeof(zval *ret, uint32_t argc, zval *argv)
{
    pa_count_common(ret, argc, argv, "sizeof");
}

/* ------------------------------------------------- in_array/array_search */

static void pa_search_array(zval *ret, uint32_t argc, zval *argv, int want_key)
{
    zval *value = &argv[0];
    zval *array = &argv[1];
    int strict = 0;
    PA_OPT_BOOL(strict, 2, "count");

    if (Z_TYPE_P(array) != IS_ARRAY) {
        pa_array_error(array, 2, want_key ? "array_search" : "in_array");
        return;   /* NULL */
    }
    ZEND_HASH_FOREACH(Z_ARR_P(array), b) {
        zval *entry = zval_deref(&b->val);
        int eq = strict ? zend_is_identical(value, entry)
                        : zend_is_equal(value, entry);
        if (eq) {
            if (want_key) {
                *ret = pa_bucket_key(b);
            } else {
                pa_ret_true(ret);
            }
            return;
        }
    } ZEND_HASH_FOREACH_END();
    pa_ret_false(ret);
}

static void zif_in_array(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("in_array", 2, 3);
    pa_search_array(ret, argc, argv, 0);
}

static void zif_array_search(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("array_search", 2, 3);
    pa_search_array(ret, argc, argv, 1);
}

/* --------------------------------------------- array_keys/array_values */

static void zif_array_keys(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("array_keys", 1, 3);
    HashTable *out;
    int strict = 0;
    PA_OPT_BOOL(strict, 2, "array_keys");

    if (Z_TYPE(argv[0]) != IS_ARRAY) {
        pa_array_error(&argv[0], 1, "array_keys");
        return;
    }
    out = zend_hash_new();
    ZEND_HASH_FOREACH(Z_ARR(argv[0]), b) {
        int add = 1;
        if (argc > 1) {
            zval *entry = zval_deref(&b->val);
            add = strict ? zend_is_identical(&argv[1], entry)
                         : zend_is_equal(&argv[1], entry);
        }
        if (add) {
            zval k = pa_bucket_key(b);
            zend_hash_next_index_insert(out, k);
        }
    } ZEND_HASH_FOREACH_END();
    ZVAL_ARR(ret, out);
}

static void zif_array_values(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("array_values", 1, 1);
    HashTable *out;
    (void) argc;

    if (Z_TYPE(argv[0]) != IS_ARRAY) {
        pa_array_error(&argv[0], 1, "array_values");
        return;
    }
    out = zend_hash_new();
    ZEND_HASH_FOREACH(Z_ARR(argv[0]), b) {
        zend_hash_next_index_insert(out, zval_copy(b->val));
    } ZEND_HASH_FOREACH_END();
    ZVAL_ARR(ret, out);
}

/* ---------------------------------------------------------- array_merge */

static int pa_array_merge(HashTable *dest, HashTable *src, int recursive)
{
    ZEND_HASH_FOREACH(src, b) {
        if (b->key) {
            zval *dest_entry;
            if (recursive && (dest_entry = zend_hash_find(dest, b->key)) != NULL) {
                zval *src_entry = zval_deref(&b->val);
                HashTable *thash = Z_TYPE_P(dest_entry) == IS_ARRAY
                                 ? Z_ARR_P(dest_entry) : NULL;
                if ((thash && thash->protect > 1)
                    || (Z_TYPE(b->val) == IS_REFERENCE
                        && Z_TYPE_P(dest_entry) == IS_REFERENCE
                        && Z_REF(b->val) == Z_REF_P(dest_entry))
                    || (Z_TYPE_P(src_entry) == IS_ARRAY
                        && Z_TYPE_P(dest_entry) == IS_ARRAY
                        && Z_ARR_P(src_entry) == Z_ARR_P(dest_entry))) {
                    zend_error(E_WARNING, "array_merge_recursive(): recursion "
                               "detected");
                    return 0;
                }
                if (Z_TYPE_P(dest_entry) != IS_ARRAY) {
                    HashTable *wrap = zend_hash_new();
                    if (Z_TYPE_P(dest_entry) != IS_NULL) {
                        zend_hash_next_index_insert(wrap, zval_copy(*dest_entry));
                    } else {
                        zval nul;
                        ZVAL_NULL(&nul);
                        zend_hash_next_index_insert(wrap, nul);
                    }
                    ZVAL_ARR(dest_entry, wrap);
                    thash = wrap;
                } else {
                    zval_separate_array(dest_entry);
                    thash = Z_ARR_P(dest_entry);
                }
                if (Z_TYPE_P(src_entry) == IS_ARRAY) {
                    thash->protect++;
                    if (!pa_array_merge(thash, Z_ARR_P(src_entry), recursive)) {
                        thash->protect--;
                        return 0;
                    }
                    thash->protect--;
                } else {
                    zend_hash_next_index_insert(thash, zval_copy(*src_entry));
                }
            } else {
                zend_hash_update(dest, pa_dup_key(b->key),
                                 zval_copy(b->val));
            }
        } else {
            zend_hash_next_index_insert(dest, zval_copy(b->val));
        }
    } ZEND_HASH_FOREACH_END();
    return 1;
}

static void pa_merge_wrapper(zval *ret, uint32_t argc, zval *argv, int recursive)
{
    HashTable *out;
    uint32_t i;

    /* VERSIONGATE:80000 the warning-and-null branch below is the 5.6 one; 8.0
     * raises a TypeError naming the parameter.  VERSIONGATE:70400 5.6 also
     * requires at least one argument, which 7.4 relaxes to zero. */
    for (i = 0; i < argc; i++) {
        if (Z_TYPE(argv[i]) != IS_ARRAY) {
            zend_error(E_WARNING, "%s(): Argument #%u is not an array",
                       recursive ? "array_merge_recursive" : "array_merge",
                       (unsigned) (i + 1));
            return;   /* NULL */
        }
    }
    out = zend_hash_new();
    for (i = 0; i < argc; i++) {
        pa_array_merge(out, Z_ARR(argv[i]), recursive);
    }
    ZVAL_ARR(ret, out);
}

static void zif_array_merge(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("array_merge", 1, UINT32_MAX);
    pa_merge_wrapper(ret, argc, argv, 0);
}

static void zif_array_merge_recursive(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("array_merge_recursive", 1, UINT32_MAX);
    pa_merge_wrapper(ret, argc, argv, 1);
}

/* -------------------------------------- array_combine/array_flip/fill... */

static void zif_array_combine(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("array_combine", 2, 2);
    HashTable *keys, *values, *out;
    uint32_t nk, nv, i;
    pa_entry *ke, *ve;
    (void) argc;

    if (Z_TYPE(argv[0]) != IS_ARRAY || Z_TYPE(argv[1]) != IS_ARRAY) {
        pa_array_error(Z_TYPE(argv[0]) != IS_ARRAY ? &argv[0] : &argv[1],
                       Z_TYPE(argv[0]) != IS_ARRAY ? 1 : 2, "array_combine");
        return;
    }
    keys = Z_ARR(argv[0]);
    values = Z_ARR(argv[1]);
    nk = zend_hash_num_elements(keys);
    nv = zend_hash_num_elements(values);
    /* VERSIONGATE:80000 else-branch is the 5.6/7.x warning-and-false form */
    if (nk != nv) {
        if (EG(php_version) >= 80000) {
            zend_throw_error("ValueError", "array_combine(): Argument #1 ($keys) "
                             "and argument #2 ($values) must have the same "
                             "number of elements");
            return;
        }
        zend_error(E_WARNING, "array_combine(): Both parameters should have an "
                   "equal number of elements");
        pa_ret_false(ret);
        return;
    }
    out = zend_hash_new();
    if (nk == 0) {
        ZVAL_ARR(ret, out);
        return;
    }
    ke = pa_snapshot(keys, &nk);
    ve = pa_snapshot(values, &nv);
    for (i = 0; i < nk && i < nv; i++) {
        pa_set_zval_key(out, &ke[i].val, ve[i].val);
    }
    free(ke);
    free(ve);
    ZVAL_ARR(ret, out);
}

static void zif_array_flip(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("array_flip", 1, 1);
    HashTable *out;
    (void) argc;

    if (Z_TYPE(argv[0]) != IS_ARRAY) {
        pa_array_error(&argv[0], 1, "array_flip");
        return;
    }
    out = zend_hash_new();
    ZEND_HASH_FOREACH(Z_ARR(argv[0]), b) {
        zval *entry = zval_deref(&b->val);
        zval key = pa_bucket_key(b);
        if (Z_TYPE_P(entry) == IS_LONG) {
            zend_hash_index_update(out, Z_LVAL_P(entry), key);
        } else if (Z_TYPE_P(entry) == IS_STRING) {
            pa_set_zval_key(out, entry, key);
        /* VERSIONGATE:80000 else-branch is the 5.6/7.x wording; 8.0 keeps the
         * warning but rewords it. */
        } else if (EG(php_version) >= 80000) {
            zend_error(E_WARNING, "array_flip(): Can only flip string and "
                       "integer values, entry skipped");
        } else {
            zend_error(E_WARNING, "array_flip(): Can only flip STRING and "
                       "INTEGER values!");
        }
    } ZEND_HASH_FOREACH_END();
    ZVAL_ARR(ret, out);
}

static void zif_array_fill(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("array_fill", 3, 3);
    zend_long start_key;
    zend_long num;
    HashTable *out;
    zend_long i;

    PA_LONG(start_key, 0, "array_fill");
    PA_LONG(num, 1, "array_fill");

    /* VERSIONGATE:80000 else-branch is the 5.6/7.x warning-and-false form */
    if (num < 0) {
        if (EG(php_version) >= 80000) {
            zend_throw_error("ValueError", "array_fill(): Argument #2 ($count) "
                             "must be greater than or equal to 0");
            return;
        }
        zend_error(E_WARNING, "array_fill(): Number of elements can't be negative");
        pa_ret_false(ret);
        return;
    }
    out = zend_hash_new();
    if (num == 0) {
        ZVAL_ARR(ret, out);
        return;
    }
    zend_hash_index_update(out, start_key, zval_copy(argv[2]));
    for (i = 1; i < num; i++) {
        zend_hash_next_index_insert(out, zval_copy(argv[2]));
    }
    ZVAL_ARR(ret, out);
}

static void zif_array_fill_keys(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("array_fill_keys", 2, 2);
    HashTable *out;
    (void) argc;

    if (Z_TYPE(argv[0]) != IS_ARRAY) {
        pa_array_error(&argv[0], 1, "array_fill_keys");
        return;
    }
    out = zend_hash_new();
    ZEND_HASH_FOREACH(Z_ARR(argv[0]), b) {
        zval *entry = zval_deref(&b->val);
        if (Z_TYPE_P(entry) == IS_LONG) {
            zend_hash_index_update(out, Z_LVAL_P(entry), zval_copy(argv[1]));
        } else {
            zend_string *k = zval_get_string(entry);
            zval kz;
            ZVAL_STR(&kz, k);
            pa_set_zval_key(out, &kz, zval_copy(argv[1]));
        }
    } ZEND_HASH_FOREACH_END();
    ZVAL_ARR(ret, out);
}

/* ------------------------------------------------------ slice and splice */

static void zif_array_slice(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("array_slice", 2, 4);
    HashTable *in, *out;
    zend_long offset, length, num_in;
    int preserve_keys = 0;
    PA_OPT_BOOL(preserve_keys, 3, "array_slice");
    zend_long pos = 0;

    if (Z_TYPE(argv[0]) != IS_ARRAY) {
        pa_array_error(&argv[0], 1, "array_slice");
        return;
    }
    in = Z_ARR(argv[0]);
    num_in = (zend_long) zend_hash_num_elements(in);
    PA_LONG(offset, 1, "array_slice");

    if (argc < 3 || Z_TYPE(argv[2]) == IS_NULL) {
        length = num_in;
    } else {
        /* php-src takes $length as a raw zval here and converts it silently,
         * so a non-numeric string is not a parameter error. */
        length = zval_get_long(&argv[2]);
    }

    out = zend_hash_new();
    if (offset > num_in) {
        ZVAL_ARR(ret, out);
        return;
    } else if (offset < 0 && (offset = num_in + offset) < 0) {
        offset = 0;
    }
    if (length < 0) {
        length = num_in - offset + length;
    } else if (offset + length > num_in) {
        length = num_in - offset;
    }
    if (length <= 0) {
        ZVAL_ARR(ret, out);
        return;
    }

    ZEND_HASH_FOREACH(in, b) {
        if (pos >= offset + length) {
            break;
        }
        if (pos++ < offset) {
            continue;
        }
        if (b->key) {
            zend_hash_update(out, pa_dup_key(b->key),
                             zval_copy(b->val));
        } else if (preserve_keys) {
            zend_hash_index_update(out, (zend_long) b->h,
                                   zval_copy(b->val));
        } else {
            zend_hash_next_index_insert(out, zval_copy(b->val));
        }
    } ZEND_HASH_FOREACH_END();
    ZVAL_ARR(ret, out);
}

/* php_splice(): string keys survive, integer keys are renumbered from 0. */
static void pa_append_entry(HashTable *dst, pa_entry *e)
{
    if (e->key) {
        zend_hash_update(dst, e->key, e->val);
    } else {
        zend_hash_next_index_insert(dst, e->val);
    }
}

static void pa_splice(HashTable *ht, zend_long offset, zend_long length,
                      zval *repl, uint32_t nrepl, HashTable *removed)
{
    pa_entry *arr;
    uint32_t n, i;

    arr = pa_snapshot(ht, &n);
    zend_hash_clean(ht);

    for (i = 0; i < (uint32_t) offset && i < n; i++) {
        pa_append_entry(ht, &arr[i]);
    }
    for (i = 0; i < nrepl; i++) {
        zend_hash_next_index_insert(ht, zval_copy(repl[i]));
    }
    for (i = (uint32_t) offset; i < (uint32_t) (offset + length) && i < n; i++) {
        if (removed) {
            pa_append_entry(removed, &arr[i]);
        }
    }
    for (i = (uint32_t) (offset + length); i < n; i++) {
        pa_append_entry(ht, &arr[i]);
    }
    ht->nInternalPointer = 0;
    free(arr);
}

static void zif_array_splice(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("array_splice", 2, 4);
    zval *arg = pa_ref(&argv[0]);
    HashTable *ht, *removed;
    zend_long offset, length, num_in;
    zval *repl = NULL;
    uint32_t nrepl = 0;

    if (Z_TYPE_P(arg) != IS_ARRAY) {
        pa_array_error(arg, 1, "array_splice");
        return;
    }
    zval_separate_array(arg);
    ht = Z_ARR_P(arg);
    num_in = (zend_long) zend_hash_num_elements(ht);
    PA_LONG(offset, 1, "array_splice");
    length = num_in;
    if (argc >= 3 && Z_TYPE(argv[2]) != IS_NULL) {
        PA_LONG(length, 2, "array_splice");
    }

    if (argc > 3) {
        zval *r = zval_deref(&argv[3]);
        if (Z_TYPE_P(r) == IS_ARRAY) {
            uint32_t i = 0;
            nrepl = zend_hash_num_elements(Z_ARR_P(r));
            if (nrepl) {
                repl = (zval *) calloc(nrepl, sizeof(zval));
                ZEND_HASH_FOREACH(Z_ARR_P(r), b) {
                    repl[i++] = *zval_deref(&b->val);
                } ZEND_HASH_FOREACH_END();
            }
        } else if (Z_TYPE_P(r) != IS_NULL) {
            nrepl = 1;
            repl = (zval *) calloc(1, sizeof(zval));
            repl[0] = *r;
        }
    }

    if (offset < 0 && (offset = num_in + offset) < 0) {
        offset = 0;
    } else if (offset > num_in) {
        offset = num_in;
    }
    if (length < 0 && (length = num_in - offset + length) < 0) {
        length = 0;
    } else if (offset + length > num_in) {
        length = num_in - offset;
    }

    removed = zend_hash_new();
    pa_splice(ht, offset, length, repl, nrepl, removed);
    free(repl);
    ZVAL_ARR(ret, removed);
}

/* ------------------------------------------------- map/filter/reduce/walk */

static void zif_array_map(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("array_map", 2, UINT32_MAX);
    uint32_t n_arrays = argc - 1;
    uint32_t i, k, maxlen = 0;
    pa_entry **snaps;
    uint32_t *lens;
    HashTable *out;
    int have_cb = Z_TYPE(argv[0]) != IS_NULL;

    if (have_cb && !pa_arg_callable(&argv[0], 1, "array_map")) {
        return;
    }
    for (i = 0; i < n_arrays; i++) {
        if (Z_TYPE(argv[i + 1]) != IS_ARRAY) {
            zend_error(E_WARNING, "array_map(): Argument #%u should be an array",
                       (unsigned) (i + 2));
            return;   /* NULL */
        }
    }
    snaps = (pa_entry **) calloc(n_arrays, sizeof(pa_entry *));
    lens = (uint32_t *) calloc(n_arrays, sizeof(uint32_t));
    for (i = 0; i < n_arrays; i++) {
        snaps[i] = pa_snapshot(Z_ARR(argv[i + 1]), &lens[i]);
        if (lens[i] > maxlen) {
            maxlen = lens[i];
        }
    }

    if (!have_cb && n_arrays == 1) {
        *ret = zval_copy(argv[1]);
        goto done;
    }

    out = zend_hash_new();
    for (k = 0; k < maxlen; k++) {
        zval result;
        zval *params = (zval *) calloc(n_arrays ? n_arrays : 1, sizeof(zval));

        for (i = 0; i < n_arrays; i++) {
            if (k < lens[i]) {
                params[i] = snaps[i][k].val;
            } else {
                ZVAL_NULL(&params[i]);
            }
        }
        if (have_cb) {
            ZVAL_NULL(&result);
            if (zend_call_user_callable(&argv[0], n_arrays, params, &result) != 0
                || EG(exception)) {
                free(params);
                /* VERSIONGATE:70000 else-branch is the 5.6 behaviour: the
                 * failed call is reported before the exception surfaces. */
                if (EG(php_version) < 70000) {
                    zend_error(E_WARNING, "array_map(): An error occurred while "
                               "invoking the map callback");
                }
                goto done;
            }
        } else {
            HashTable *row = zend_hash_new();
            for (i = 0; i < n_arrays; i++) {
                zend_hash_next_index_insert(row, zval_copy(params[i]));
            }
            ZVAL_ARR(&result, row);
        }
        free(params);

        if (n_arrays > 1) {
            zend_hash_next_index_insert(out, result);
        } else if (snaps[0][k].key) {
            zend_hash_update(out, pa_dup_key(snaps[0][k].key), result);
        } else {
            zend_hash_index_update(out, (zend_long) snaps[0][k].h, result);
        }
    }
    ZVAL_ARR(ret, out);

done:
    for (i = 0; i < n_arrays; i++) {
        free(snaps[i]);
    }
    free(snaps);
    free(lens);
}

static void zif_array_filter(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("array_filter", 1, 3);
    HashTable *out;
    int have_cb = argc > 1;
    zend_long use_type = 0;
    PA_OPT_LONG(use_type, 2, "array_filter");
    pa_entry *snap;
    uint32_t n, i;

    if (Z_TYPE(argv[0]) != IS_ARRAY) {
        pa_array_error(&argv[0], 1, "array_filter");
        return;
    }
    /* VERSIONGATE:80000 rejecting a null $callback below is the 5.6 branch; 8.0
     * accepts null and falls back to the truthiness test. */
    if (have_cb && Z_TYPE(argv[1]) == IS_NULL) {
        zend_error(E_WARNING, "array_filter() expects parameter 2 to be a valid "
                   "callback, no array or string given");
        return;   /* NULL */
    }
    if (have_cb && !pa_arg_callable(&argv[1], 2, "array_filter")) {
        return;
    }
    out = zend_hash_new();
    snap = pa_snapshot(Z_ARR(argv[0]), &n);
    for (i = 0; i < n; i++) {
        int keep;
        if (have_cb) {
            zval args[2];
            zval key;
            zval result;
            uint32_t nargs;

            if (snap[i].key) {
                ZVAL_STR(&key, snap[i].key);
            } else {
                ZVAL_LONG(&key, (zend_long) snap[i].h);
            }
            if (use_type == PA_FILTER_USE_BOTH) {
                args[0] = snap[i].val;
                args[1] = key;
                nargs = 2;
            } else if (use_type == PA_FILTER_USE_KEY) {
                args[0] = key;
                nargs = 1;
            } else {
                args[0] = snap[i].val;
                nargs = 1;
            }
            ZVAL_NULL(&result);
            if (zend_call_user_callable(&argv[1], nargs, args, &result) != 0
                || EG(exception)) {
                free(snap);
                /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
                if (EG(php_version) < 70000) {
                    zend_error(E_WARNING, "array_filter(): An error occurred "
                               "while invoking the filter callback");
                }
                return;
            }
            keep = zval_is_true(&result);
        } else {
            keep = zval_is_true(&snap[i].val);
        }
        if (!keep) {
            continue;
        }
        if (snap[i].key) {
            zend_hash_update(out, pa_dup_key(snap[i].key), snap[i].val);
        } else {
            zend_hash_index_update(out, (zend_long) snap[i].h, snap[i].val);
        }
    }
    free(snap);
    ZVAL_ARR(ret, out);
}

static void zif_array_reduce(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("array_reduce", 2, 3);
    zval result;
    pa_entry *snap;
    uint32_t n, i;

    if (Z_TYPE(argv[0]) != IS_ARRAY) {
        pa_array_error(&argv[0], 1, "array_reduce");
        return;
    }
    if (!pa_arg_callable(&argv[1], 2, "array_reduce")) {
        return;
    }
    if (argc > 2) {
        result = zval_copy(argv[2]);
    } else {
        ZVAL_NULL(&result);
    }
    snap = pa_snapshot(Z_ARR(argv[0]), &n);
    for (i = 0; i < n; i++) {
        zval args[2];
        zval r;
        args[0] = result;
        args[1] = snap[i].val;
        ZVAL_NULL(&r);
        if (zend_call_user_callable(&argv[1], 2, args, &r) != 0 || EG(exception)) {
            free(snap);
            /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
            if (EG(php_version) < 70000) {
                zend_error(E_WARNING, "array_reduce(): An error occurred while "
                           "invoking the reduction callback");
            }
            return;
        }
        result = r;
    }
    free(snap);
    *ret = result;
}

static void zif_array_walk(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("array_walk", 2, 3);
    zval *arg = pa_ref(&argv[0]);
    HashTable *ht;
    uint32_t i;

    if (Z_TYPE_P(arg) != IS_ARRAY) {
        pa_array_error(arg, 1, "array_walk");
        return;   /* NULL */
    }
    if (!pa_arg_callable(&argv[1], 2, "array_walk")) {
        return;
    }
    zval_separate_array(arg);
    ht = Z_ARR_P(arg);

    /* php-src hands the same $userdata zval to every call, so a callback that
     * takes it by reference accumulates across the walk. */
    zend_reference udata;
    udata.refcount = 2;
    if (argc > 2) {
        udata.val = *zval_deref(&argv[2]);
    } else {
        ZVAL_NULL(&udata.val);
    }

    i = 0;
    for (;;) {
        Bucket *b;
        zval args[3];
        zval retval;
        zend_reference tmpref;
        int made_ref = 0;
        uint32_t nargs = argc > 2 ? 3 : 2;
        uint32_t pos;

        while (i < ht->nNumUsed && Z_TYPE(ht->arData[i].val) == IS_UNDEF) {
            i++;
        }
        if (i >= ht->nNumUsed) {
            break;
        }
        b = &ht->arData[i];
        if (Z_TYPE(b->val) == IS_REFERENCE) {
            args[0] = b->val;
        } else {
            tmpref.refcount = 2;
            tmpref.val = b->val;
            args[0].u1 = IS_REFERENCE;
            args[0].value.ref = &tmpref;
            made_ref = 1;
        }
        args[1] = pa_bucket_key(b);
        if (nargs == 3) {
            args[2].u1 = IS_REFERENCE;
            args[2].value.ref = &udata;
        }
        ZVAL_NULL(&retval);
        if (zend_call_user_callable(&argv[1], nargs, args, &retval) != 0
            || EG(exception)) {
            return;
        }
        /* the callback may have reallocated arData or deleted entries */
        if (made_ref && i < ht->nNumUsed
            && Z_TYPE(ht->arData[i].val) != IS_UNDEF) {
            ht->arData[i].val = tmpref.val;
        }
        /* Deleting the current element leaves php-src's internal pointer on
         * the element that followed it; the loop's move-forward then steps
         * past that one as well. */
        pos = i;
        if (i < ht->nNumUsed && Z_TYPE(ht->arData[i].val) == IS_UNDEF) {
            pos = i + 1;
            while (pos < ht->nNumUsed && Z_TYPE(ht->arData[pos].val) == IS_UNDEF) {
                pos++;
            }
        }
        i = pos + 1;
    }
    pa_ret_true(ret);
}

/* ---------------------------------------------------------- array_reverse */

static void zif_array_reverse(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("array_reverse", 1, 2);
    HashTable *out;
    int preserve_keys = 0;
    PA_OPT_BOOL(preserve_keys, 1, "array_reverse");
    pa_entry *snap;
    uint32_t n, i;

    if (Z_TYPE(argv[0]) != IS_ARRAY) {
        pa_array_error(&argv[0], 1, "array_reverse");
        return;
    }
    out = zend_hash_new();
    snap = pa_snapshot(Z_ARR(argv[0]), &n);
    for (i = n; i > 0; i--) {
        pa_entry *e = &snap[i - 1];
        if (e->key) {
            zend_hash_update(out, e->key, e->val);
        } else if (preserve_keys) {
            zend_hash_index_update(out, (zend_long) e->h, e->val);
        } else {
            zend_hash_next_index_insert(out, e->val);
        }
    }
    free(snap);
    ZVAL_ARR(ret, out);
}

/* ----------------------------------------------------------- array_unique */

static void zif_array_unique(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("array_unique", 1, 2);
    HashTable *out;
    zend_long sort_type = PA_SORT_STRING;
    PA_OPT_LONG(sort_type, 1, "array_unique");
    pa_entry *snap;
    uint32_t n, i, j, nkept = 0;
    uint32_t *kept;

    if (Z_TYPE(argv[0]) != IS_ARRAY) {
        pa_array_error(&argv[0], 1, "array_unique");
        return;
    }
    out = zend_hash_new();
    snap = pa_snapshot(Z_ARR(argv[0]), &n);
    kept = n ? (uint32_t *) calloc(n, sizeof(uint32_t)) : NULL;

    for (i = 0; i < n; i++) {
        int dup = 0;
        for (j = 0; j < nkept; j++) {
            if (pa_compare_flag(&snap[i].val, &snap[kept[j]].val, sort_type) == 0) {
                dup = 1;
                break;
            }
        }
        if (dup) {
            continue;
        }
        kept[nkept++] = i;
        if (snap[i].key) {
            zend_hash_update(out, snap[i].key, snap[i].val);
        } else {
            zend_hash_index_update(out, (zend_long) snap[i].h, snap[i].val);
        }
    }
    free(kept);
    free(snap);
    ZVAL_ARR(ret, out);
}

/* --------------------------------------------------- diff/intersect group */

/* mode: bit0 = compare keys, bit1 = compare data.  php-src compares both keys
 * and values as strings for this whole family (php_set_compare_func with
 * PHP_SORT_STRING). */
#define PA_CMP_KEY   1
#define PA_CMP_DATA  2

static void pa_diff_intersect(zval *ret, uint32_t argc, zval *argv, int mode,
                              int intersect, const char *fname)
{
    HashTable *out;
    uint32_t i;

    /* VERSIONGATE:80000 else-branch is the 5.6 form: a warning, not a throw */
    if (argc < 2) {
        if (EG(php_version) >= 80000) {
            zend_throw_error("ArgumentCountError", "%s() expects at least 2 "
                             "arguments, %u given", fname, (unsigned) argc);
        } else {
            zend_error(E_WARNING, "%s(): at least 2 parameters are required, "
                       "%u given", fname, (unsigned) argc);
        }
        return;   /* NULL */
    }
    for (i = 0; i < argc; i++) {
        if (Z_TYPE(argv[i]) != IS_ARRAY) {
            zend_error(E_WARNING, "%s(): Argument #%u is not an array", fname,
                       (unsigned) (i + 1));
            return;   /* NULL */
        }
    }
    out = zend_hash_new();
    ZEND_HASH_FOREACH(Z_ARR(argv[0]), b) {
        zval *val = zval_deref(&b->val);
        int found_in_all = 1;
        int found_in_any = 0;

        for (i = 1; i < argc; i++) {
            int found = 0;
            if (mode & PA_CMP_KEY) {
                zval *other = pa_find_bucket_key(Z_ARR(argv[i]), b);
                if (other) {
                    found = !(mode & PA_CMP_DATA)
                          || pa_string_equal(val, zval_deref(other));
                }
            } else {
                ZEND_HASH_FOREACH(Z_ARR(argv[i]), ob) {
                    if (pa_string_equal(val, zval_deref(&ob->val))) {
                        found = 1;
                        break;
                    }
                } ZEND_HASH_FOREACH_END();
            }
            if (found) {
                found_in_any = 1;
            } else {
                found_in_all = 0;
            }
            if (!intersect && found_in_any) {
                break;
            }
            if (intersect && !found_in_all) {
                break;
            }
        }
        if (intersect ? found_in_all : !found_in_any) {
            pa_put_bucket(out, b, zval_copy(b->val));
        }
    } ZEND_HASH_FOREACH_END();
    ZVAL_ARR(ret, out);
}

static void zif_array_diff(zval *ret, uint32_t argc, zval *argv)
{
    pa_diff_intersect(ret, argc, argv, PA_CMP_DATA, 0, "array_diff");
}

static void zif_array_diff_key(zval *ret, uint32_t argc, zval *argv)
{
    pa_diff_intersect(ret, argc, argv, PA_CMP_KEY, 0, "array_diff_key");
}

static void zif_array_diff_assoc(zval *ret, uint32_t argc, zval *argv)
{
    pa_diff_intersect(ret, argc, argv, PA_CMP_KEY | PA_CMP_DATA, 0,
                      "array_diff_assoc");
}

static void zif_array_intersect(zval *ret, uint32_t argc, zval *argv)
{
    pa_diff_intersect(ret, argc, argv, PA_CMP_DATA, 1, "array_intersect");
}

static void zif_array_intersect_key(zval *ret, uint32_t argc, zval *argv)
{
    pa_diff_intersect(ret, argc, argv, PA_CMP_KEY, 1, "array_intersect_key");
}

static void zif_array_intersect_assoc(zval *ret, uint32_t argc, zval *argv)
{
    pa_diff_intersect(ret, argc, argv, PA_CMP_KEY | PA_CMP_DATA, 1,
                      "array_intersect_assoc");
}

/* -------------------------------------------------------- array_key_exists */

static void pa_key_exists_common(zval *ret, uint32_t argc, zval *argv,
                                 const char *fname)
{
    zval *key = &argv[0];
    zval *arr = &argv[1];
    HashTable *ht;

    if (argc != 2) {
        zend_wrong_param_count(fname, argc, 2, 2);
        return;
    }

    if (Z_TYPE_P(arr) == IS_ARRAY) {
        ht = Z_ARR_P(arr);
    /* VERSIONGATE:80000 accepting an object below is the 5.6 branch; 8.0 only
     * accepts arrays and raises a TypeError otherwise. */
    } else if (Z_TYPE_P(arr) == IS_OBJECT) {
        ht = Z_OBJ_P(arr)->properties;
        if (!ht) {
            pa_ret_false(ret);
            return;
        }
    } else {
        pa_array_error(arr, 2, fname);
        return;
    }

    switch (Z_TYPE_P(key)) {
    case IS_STRING:
    case IS_LONG:
        ZVAL_BOOL(ret, pa_find_zval_key(ht, key) != NULL);
        return;
    case IS_NULL:
        ZVAL_BOOL(ret, zend_hash_str_find(ht, "", 0) != NULL);
        return;
    default:
        /* VERSIONGATE:80000 the warning-and-false branch below is the 5.6 one;
         * 8.0 raises a TypeError for a non string/int key. */
        zend_error(E_WARNING, "%s(): The first argument should be either a "
                   "string or an integer", fname);
        pa_ret_false(ret);
        return;
    }
}

static void zif_array_key_exists(zval *ret, uint32_t argc, zval *argv)
{
    pa_key_exists_common(ret, argc, argv, "array_key_exists");
}

static void zif_key_exists(zval *ret, uint32_t argc, zval *argv)
{
    pa_key_exists_common(ret, argc, argv, "key_exists");
}

/* ---------------------------------------------- push/pop/shift/unshift */

static void zif_array_push(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("array_push", 2, UINT32_MAX);
    zval *arg = pa_ref(&argv[0]);
    HashTable *ht;
    uint32_t i;

    if (Z_TYPE_P(arg) != IS_ARRAY) {
        pa_array_error(arg, 1, "array_push");
        return;
    }
    zval_separate_array(arg);
    ht = Z_ARR_P(arg);
    for (i = 1; i < argc; i++) {
        zend_hash_next_index_insert(ht, zval_copy(*zval_deref(&argv[i])));
    }
    ZVAL_LONG(ret, (zend_long) zend_hash_num_elements(ht));
}

static void pa_pop(zval *ret, zval *argv, int off_the_end, const char *fname)
{
    zval *arg = pa_ref(&argv[0]);
    HashTable *ht;
    Bucket *target = NULL;
    uint32_t i;

    if (Z_TYPE_P(arg) != IS_ARRAY) {
        pa_array_error(arg, 1, fname);
        return;
    }
    zval_separate_array(arg);
    ht = Z_ARR_P(arg);
    if (zend_hash_num_elements(ht) == 0) {
        return;   /* NULL */
    }

    if (off_the_end) {
        for (i = ht->nNumUsed; i > 0; i--) {
            if (Z_TYPE(ht->arData[i - 1].val) != IS_UNDEF) {
                target = &ht->arData[i - 1];
                break;
            }
        }
    } else {
        for (i = 0; i < ht->nNumUsed; i++) {
            if (Z_TYPE(ht->arData[i].val) != IS_UNDEF) {
                target = &ht->arData[i];
                break;
            }
        }
    }
    if (!target) {
        return;
    }
    *ret = zval_copy(*zval_deref(&target->val));

    if (target->key) {
        zend_hash_del(ht, target->key);
    } else {
        zend_long idx = (zend_long) target->h;
        zend_hash_index_del(ht, idx);
        if (off_the_end && ht->nNextFreeElement > 0
            && idx >= ht->nNextFreeElement - 1) {
            ht->nNextFreeElement = ht->nNextFreeElement - 1;
        }
    }

    if (!off_the_end) {
        /* array_shift() renumbers the integer keys, string keys survive. */
        pa_entry *arr;
        uint32_t n;
        arr = pa_snapshot(ht, &n);
        zend_hash_clean(ht);
        for (i = 0; i < n; i++) {
            pa_append_entry(ht, &arr[i]);
        }
        free(arr);
    }
    ht->nInternalPointer = 0;
}

static void zif_array_pop(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("array_pop", 1, 1);
    (void) argc;
    pa_pop(ret, argv, 1, "array_pop");
}

static void zif_array_shift(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("array_shift", 1, 1);
    (void) argc;
    pa_pop(ret, argv, 0, "array_shift");
}

static void zif_array_unshift(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("array_unshift", 2, UINT32_MAX);
    zval *arg = pa_ref(&argv[0]);
    HashTable *ht;
    zval *repl;
    uint32_t i;

    if (Z_TYPE_P(arg) != IS_ARRAY) {
        pa_array_error(arg, 1, "array_unshift");
        return;
    }
    zval_separate_array(arg);
    ht = Z_ARR_P(arg);
    repl = (zval *) calloc(argc > 1 ? argc - 1 : 1, sizeof(zval));
    for (i = 1; i < argc; i++) {
        repl[i - 1] = *zval_deref(&argv[i]);
    }
    pa_splice(ht, 0, 0, repl, argc - 1, NULL);
    free(repl);
    ZVAL_LONG(ret, (zend_long) zend_hash_num_elements(ht));
}

/* ------------------------------------------------------------- array_pad */

static void zif_array_pad(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("array_pad", 3, 3);
    HashTable *in, *out;
    zend_long pad_size, pad_size_abs;
    zend_long input_size, num_pads, i;
    zval *pads;
    (void) argc;

    if (Z_TYPE(argv[0]) != IS_ARRAY) {
        pa_array_error(&argv[0], 1, "array_pad");
        return;
    }
    in = Z_ARR(argv[0]);
    PA_LONG(pad_size, 1, "array_pad");
    pad_size_abs = pad_size < 0 ? -pad_size : pad_size;
    input_size = (zend_long) zend_hash_num_elements(in);

    if (input_size >= pad_size_abs) {
        *ret = zval_copy(argv[0]);
        return;
    }
    num_pads = pad_size_abs - input_size;
    /* VERSIONGATE:80000 else-branch is the 5.6/7.x warning-and-false form */
    if (num_pads > 1048576) {
        if (EG(php_version) >= 80000) {
            zend_throw_error("ValueError", "array_pad(): Argument #2 ($length) "
                             "must be less than or equal to 1048576");
            return;
        }
        zend_error(E_WARNING, "array_pad(): You may only pad up to 1048576 "
                   "elements at a time");
        pa_ret_false(ret);
        return;
    }

    out = zend_hash_new();
    ZEND_HASH_FOREACH(in, b) {
        pa_put_bucket(out, b, zval_copy(b->val));
    } ZEND_HASH_FOREACH_END();

    pads = (zval *) calloc((size_t) num_pads, sizeof(zval));
    for (i = 0; i < num_pads; i++) {
        pads[i] = argv[2];
    }
    if (pad_size > 0) {
        pa_splice(out, input_size, 0, pads, (uint32_t) num_pads, NULL);
    } else {
        pa_splice(out, 0, 0, pads, (uint32_t) num_pads, NULL);
    }
    free(pads);
    ZVAL_ARR(ret, out);
}

/* ------------------------------------------------- array_sum/array_product */

/* convert_scalar_to_number(): non-numeric strings become 0, trailing garbage is
 * ignored. */
static void pa_scalar_to_number(const zval *v, zval *out)
{
    switch (Z_TYPE_P(v)) {
    case IS_NULL:
    case IS_FALSE:
        ZVAL_LONG(out, 0);
        break;
    case IS_TRUE:
        ZVAL_LONG(out, 1);
        break;
    case IS_LONG:
    case IS_DOUBLE:
        *out = *v;
        break;
    case IS_STRING: {
        zend_long l = 0;
        double d = 0.0;
        int t = is_numeric_string(ZSTR_VAL(Z_STR_P((zval *) v)),
                                  ZSTR_LEN(Z_STR_P((zval *) v)), &l, &d, 1);
        if (t == IS_LONG) {
            ZVAL_LONG(out, l);
        } else if (t == IS_DOUBLE) {
            ZVAL_DOUBLE(out, d);
        } else {
            ZVAL_LONG(out, 0);
        }
        break;
    }
    default:
        ZVAL_LONG(out, 0);
        break;
    }
}

static void pa_add_into(zval *acc, const zval *n)
{
    if (Z_TYPE_P(acc) == IS_LONG && Z_TYPE_P(n) == IS_LONG) {
        zend_long a = Z_LVAL_P(acc), b = Z_LVAL_P(n);
        if ((b > 0 && a > ZEND_LONG_MAX - b) || (b < 0 && a < ZEND_LONG_MIN - b)) {
            ZVAL_DOUBLE(acc, (double) a + (double) b);
        } else {
            zend_long sum = a + b;
            ZVAL_LONG(acc, sum);
        }
        return;
    }
    {
        /* Compute before writing: ZVAL_DOUBLE() stores the type tag first, so
         * the operands must not be read out of *acc inside the macro. */
        double sum = zval_get_double(acc) + zval_get_double(n);
        ZVAL_DOUBLE(acc, sum);
    }
}

static void zif_array_sum(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("array_sum", 1, 1);
    (void) argc;
    if (Z_TYPE(argv[0]) != IS_ARRAY) {
        pa_array_error(&argv[0], 1, "array_sum");
        return;
    }
    ZVAL_LONG(ret, 0);
    ZEND_HASH_FOREACH(Z_ARR(argv[0]), b) {
        zval *entry = zval_deref(&b->val);
        zval num;
        if (Z_TYPE_P(entry) == IS_ARRAY || Z_TYPE_P(entry) == IS_OBJECT) {
            continue;
        }
        pa_scalar_to_number(entry, &num);
        pa_add_into(ret, &num);
    } ZEND_HASH_FOREACH_END();
}

static void zif_array_product(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("array_product", 1, 1);
    (void) argc;
    if (Z_TYPE(argv[0]) != IS_ARRAY) {
        pa_array_error(&argv[0], 1, "array_product");
        return;
    }
    ZVAL_LONG(ret, 1);
    ZEND_HASH_FOREACH(Z_ARR(argv[0]), b) {
        zval *entry = zval_deref(&b->val);
        zval num;
        if (Z_TYPE_P(entry) == IS_ARRAY || Z_TYPE_P(entry) == IS_OBJECT) {
            continue;
        }
        pa_scalar_to_number(entry, &num);
        if (Z_TYPE(num) == IS_LONG && Z_TYPE_P(ret) == IS_LONG) {
            double d = (double) Z_LVAL_P(ret) * (double) Z_LVAL(num);
            if ((double) ZEND_LONG_MIN <= d && d <= (double) ZEND_LONG_MAX) {
                zend_long prod = Z_LVAL_P(ret) * Z_LVAL(num);
                ZVAL_LONG(ret, prod);
                continue;
            }
        }
        {
            double prod = zval_get_double(ret) * zval_get_double(&num);
            ZVAL_DOUBLE(ret, prod);
        }
    } ZEND_HASH_FOREACH_END();
}

/* ---------------------------------------------------------- array_column */

static int pa_column_key_ok(zval *param, const char *name)
{
    switch (Z_TYPE_P(param)) {
    case IS_DOUBLE: {
        zend_long l = zval_get_long(param);
        ZVAL_LONG(param, l);
        return 1;
    }
    case IS_LONG:
    case IS_STRING:
        return 1;
    case IS_OBJECT: {
        zend_string *s = zval_get_string(param);
        ZVAL_STR(param, s);
        return 1;
    }
    default:
        /* VERSIONGATE:80000 the warning-and-false branch below is the 5.6 one;
         * 8.0 raises a TypeError. */
        zend_error(E_WARNING, "array_column(): The %s key should be either a "
                   "string or an integer", name);
        return 0;
    }
}

static void zif_array_column(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("array_column", 2, 3);
    HashTable *in, *out;
    zval *zcolumn = Z_TYPE(argv[1]) == IS_NULL ? NULL : &argv[1];
    zval *zkey = (argc > 2 && Z_TYPE(argv[2]) != IS_NULL) ? &argv[2] : NULL;

    if (Z_TYPE(argv[0]) != IS_ARRAY) {
        pa_array_error(&argv[0], 1, "array_column");
        return;
    }
    if ((zcolumn && !pa_column_key_ok(zcolumn, "column"))
        || (zkey && !pa_column_key_ok(zkey, "index"))) {
        pa_ret_false(ret);
        return;
    }
    in = Z_ARR(argv[0]);
    out = zend_hash_new();

    ZEND_HASH_FOREACH(in, b) {
        zval *data = zval_deref(&b->val);
        HashTable *row;
        zval *colval, *keyval = NULL;

        if (Z_TYPE_P(data) != IS_ARRAY) {
            continue;
        }
        row = Z_ARR_P(data);
        if (!zcolumn) {
            colval = data;
        } else {
            colval = pa_find_zval_key(row, zcolumn);
            if (!colval) {
                continue;
            }
        }
        if (zkey) {
            keyval = pa_find_zval_key(row, zkey);
        }
        if (keyval) {
            keyval = zval_deref(keyval);
        }
        if (keyval && (Z_TYPE_P(keyval) == IS_STRING || Z_TYPE_P(keyval) == IS_LONG
                       || Z_TYPE_P(keyval) == IS_OBJECT)) {
            if (Z_TYPE_P(keyval) == IS_OBJECT) {
                zval k;
                ZVAL_STR(&k, zval_get_string(keyval));
                pa_set_zval_key(out, &k, zval_copy(*colval));
            } else {
                pa_set_zval_key(out, keyval, zval_copy(*colval));
            }
        } else {
            zend_hash_next_index_insert(out, zval_copy(*colval));
        }
    } ZEND_HASH_FOREACH_END();
    ZVAL_ARR(ret, out);
}

/* ----------------------------------------------------------- array_chunk */

static void zif_array_chunk(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("array_chunk", 2, 3);
    HashTable *in, *out, *chunk = NULL;
    zend_long size, num_in, current = 0;
    int preserve_keys = 0;
    PA_OPT_BOOL(preserve_keys, 2, "array_chunk");

    if (Z_TYPE(argv[0]) != IS_ARRAY) {
        pa_array_error(&argv[0], 1, "array_chunk");
        return;
    }
    in = Z_ARR(argv[0]);
    PA_LONG(size, 1, "array_chunk");
    /* VERSIONGATE:80000 else-branch is the 5.6/7.x warning-and-null form */
    if (size < 1) {
        if (EG(php_version) >= 80000) {
            zend_throw_error("ValueError", "array_chunk(): Argument #2 ($length) "
                             "must be greater than 0");
            return;
        }
        zend_error(E_WARNING, "array_chunk(): Size parameter expected to be "
                   "greater than 0");
        return;   /* NULL */
    }
    num_in = (zend_long) zend_hash_num_elements(in);
    if (size > num_in) {
        size = num_in > 0 ? num_in : 1;
    }
    out = zend_hash_new();

    ZEND_HASH_FOREACH(in, b) {
        zval z;
        if (!chunk) {
            chunk = zend_hash_new();
        }
        if (preserve_keys) {
            pa_put_bucket(chunk, b, zval_copy(b->val));
        } else {
            zend_hash_next_index_insert(chunk, zval_copy(b->val));
        }
        if (!(++current % size)) {
            ZVAL_ARR(&z, chunk);
            zend_hash_next_index_insert(out, z);
            chunk = NULL;
        }
    } ZEND_HASH_FOREACH_END();

    if (chunk) {
        zval z;
        ZVAL_ARR(&z, chunk);
        zend_hash_next_index_insert(out, z);
    }
    ZVAL_ARR(ret, out);
}

/* ------------------------------------- array_key_first/array_key_last (7.3) */

/* VERSIONGATE:70300 */
static void zif_array_key_first(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("array_key_first", 1, 1);
    (void) argc;
    if (Z_TYPE(argv[0]) != IS_ARRAY) {
        pa_array_error(&argv[0], 1, "array_key_first");
        return;
    }
    ZEND_HASH_FOREACH(Z_ARR(argv[0]), b) {
        *ret = pa_bucket_key(b);
        return;
    } ZEND_HASH_FOREACH_END();
    ZVAL_NULL(ret);
}

/* VERSIONGATE:70300 */
static void zif_array_key_last(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("array_key_last", 1, 1);
    HashTable *ht;
    uint32_t i;
    (void) argc;

    if (Z_TYPE(argv[0]) != IS_ARRAY) {
        pa_array_error(&argv[0], 1, "array_key_last");
        return;
    }
    ht = Z_ARR(argv[0]);
    for (i = ht->nNumUsed; i > 0; i--) {
        if (Z_TYPE(ht->arData[i - 1].val) != IS_UNDEF) {
            *ret = pa_bucket_key(&ht->arData[i - 1]);
            return;
        }
    }
    ZVAL_NULL(ret);
}

/* ----------------------------------------------------------------- range */

#define PA_DOUBLE_DRIFT_FIX 0.000000000000001

static void zif_range(zval *ret, uint32_t argc, zval *argv)
{
    pa_ret_false(ret);   /* php-src returns false from this one on a parameter error */
    PA_ARGC("range", 2, 3);
    zval zlow = argv[0];
    zval zhigh = argv[1];
    HashTable *ht;
    double step = 1.0;
    int is_step_double = 0;
    int err = 0;

    if (argc > 2) {
        zval *zstep = &argv[2];
        if (Z_TYPE_P(zstep) == IS_DOUBLE) {
            is_step_double = 1;
        } else if (Z_TYPE_P(zstep) == IS_STRING) {
            zend_long l = 0;
            double d = 0.0;
            if (is_numeric_string(ZSTR_VAL(Z_STR_P(zstep)), ZSTR_LEN(Z_STR_P(zstep)),
                                  &l, &d, 0) == IS_DOUBLE) {
                is_step_double = 1;
            }
        }
        step = zval_get_double(zstep);
        if (step < 0.0) {
            step = -step;
        }
    }

    ht = zend_hash_new();

    if (Z_TYPE(zlow) == IS_STRING && Z_TYPE(zhigh) == IS_STRING
        && ZSTR_LEN(Z_STR(zlow)) >= 1 && ZSTR_LEN(Z_STR(zhigh)) >= 1) {
        zend_long dummy_l = 0;
        double dummy_d = 0.0;
        int type1 = is_numeric_string(ZSTR_VAL(Z_STR(zlow)), ZSTR_LEN(Z_STR(zlow)),
                                      &dummy_l, &dummy_d, 0);
        int type2 = is_numeric_string(ZSTR_VAL(Z_STR(zhigh)), ZSTR_LEN(Z_STR(zhigh)),
                                      &dummy_l, &dummy_d, 0);

        if (type1 == IS_DOUBLE || type2 == IS_DOUBLE || is_step_double) {
            goto double_str;
        } else if (type1 == IS_LONG || type2 == IS_LONG) {
            goto long_str;
        } else {
            unsigned char low = (unsigned char) ZSTR_VAL(Z_STR(zlow))[0];
            unsigned char high = (unsigned char) ZSTR_VAL(Z_STR(zhigh))[0];
            zend_long lstep = (zend_long) step;
            zval z;
            char c;

            if (low > high) {
                if (lstep <= 0) {
                    err = 1;
                    goto finish;
                }
                for (c = (char) low; (unsigned char) c >= high;
                     c = (char) ((unsigned char) c - (unsigned int) lstep)) {
                    ZVAL_STR(&z, zend_string_init(&c, 1));
                    zend_hash_next_index_insert(ht, z);
                    if (((int) (unsigned char) c - (int) lstep) < 0) {
                        break;
                    }
                }
            } else if (high > low) {
                if (lstep <= 0) {
                    err = 1;
                    goto finish;
                }
                for (c = (char) low; (unsigned char) c <= high;
                     c = (char) ((unsigned char) c + (unsigned int) lstep)) {
                    ZVAL_STR(&z, zend_string_init(&c, 1));
                    zend_hash_next_index_insert(ht, z);
                    if (((int) (unsigned char) c + (int) lstep) > 255) {
                        break;
                    }
                }
            } else {
                c = (char) low;
                ZVAL_STR(&z, zend_string_init(&c, 1));
                zend_hash_next_index_insert(ht, z);
            }
        }
    } else if (Z_TYPE(zlow) == IS_DOUBLE || Z_TYPE(zhigh) == IS_DOUBLE
               || is_step_double) {
        double low, high, value;
        zend_long i;
        zval z;
double_str:
        low = zval_get_double(&zlow);
        high = zval_get_double(&zhigh);
        i = 0;
        if (low > high) {
            if (low - high < step || step <= 0) {
                err = 1;
                goto finish;
            }
            for (value = low; value >= (high - PA_DOUBLE_DRIFT_FIX);
                 value = low - (++i * step)) {
                ZVAL_DOUBLE(&z, value);
                zend_hash_next_index_insert(ht, z);
            }
        } else if (high > low) {
            if (high - low < step || step <= 0) {
                err = 1;
                goto finish;
            }
            for (value = low; value <= (high + PA_DOUBLE_DRIFT_FIX);
                 value = low + (++i * step)) {
                ZVAL_DOUBLE(&z, value);
                zend_hash_next_index_insert(ht, z);
            }
        } else {
            ZVAL_DOUBLE(&z, low);
            zend_hash_next_index_insert(ht, z);
        }
    } else {
        double low, high;
        zend_long lstep;
        zval z;
long_str:
        low = zval_get_double(&zlow);
        high = zval_get_double(&zhigh);
        lstep = (zend_long) step;

        if (low > high) {
            if (low - high < (double) lstep || lstep <= 0) {
                err = 1;
                goto finish;
            }
            for (; low >= high; low -= (double) lstep) {
                ZVAL_LONG(&z, (zend_long) low);
                zend_hash_next_index_insert(ht, z);
            }
        } else if (high > low) {
            if (high - low < (double) lstep || lstep <= 0) {
                err = 1;
                goto finish;
            }
            for (; low <= high; low += (double) lstep) {
                ZVAL_LONG(&z, (zend_long) low);
                zend_hash_next_index_insert(ht, z);
            }
        } else {
            ZVAL_LONG(&z, (zend_long) low);
            zend_hash_next_index_insert(ht, z);
        }
    }

finish:
    /* VERSIONGATE:80000 else-branch is the 5.6/7.x warning-and-false form */
    if (err) {
        zend_hash_destroy(ht);
        if (EG(php_version) >= 80000) {
            zend_throw_error("ValueError", "range(): Argument #3 ($step) must not "
                             "exceed the specified range");
            return;
        }
        zend_error(E_WARNING, "range(): step exceeds the specified range");
        pa_ret_false(ret);
        return;
    }
    ZVAL_ARR(ret, ht);
}

/* -------------------------------------------------------- compact/extract */

static HashTable *pa_symbol_table(void)
{
    if (EG(current) && EG(current)->symbols) {
        return EG(current)->symbols;
    }
    return EG(symbol_table);
}

static int pa_valid_var_name(const char *name, size_t len)
{
    size_t i;
    int ch;

    if (!name || len == 0) {
        return 0;
    }
    ch = (unsigned char) name[0];
    if (ch != '_' && (ch < 'A' || ch > 'Z') && (ch < 'a' || ch > 'z')
        && ch < 127) {
        return 0;
    }
    for (i = 1; i < len; i++) {
        ch = (unsigned char) name[i];
        if (ch != '_' && (ch < '0' || ch > '9') && (ch < 'A' || ch > 'Z')
            && (ch < 'a' || ch > 'z') && ch < 127) {
            return 0;
        }
    }
    return 1;
}

static void pa_compact_var(HashTable *symbols, HashTable *out, zval *entry)
{
    if (Z_TYPE_P(entry) == IS_STRING) {
        zval *v = zend_hash_find(symbols, Z_STR_P(entry));
        if (v) {
            zend_hash_update(out, pa_dup_key(Z_STR_P(entry)),
                             zval_copy(*zval_deref(v)));
        }
    } else if (Z_TYPE_P(entry) == IS_ARRAY) {
        HashTable *ht = Z_ARR_P(entry);
        if (ht->protect > 1) {
            zend_error(E_WARNING, "compact(): recursion detected");
            return;
        }
        ht->protect++;
        ZEND_HASH_FOREACH(ht, b) {
            pa_compact_var(symbols, out, zval_deref(&b->val));
        } ZEND_HASH_FOREACH_END();
        ht->protect--;
    }
}

static void zif_compact(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("compact", 1, UINT32_MAX);
    HashTable *symbols = pa_symbol_table();
    HashTable *out = zend_hash_new();
    uint32_t i;

    if (!symbols) {
        ZVAL_ARR(ret, out);
        return;
    }
    /* VERSIONGATE:70300 silently ignoring an unknown name below is the 5.6
     * branch; 7.3 adds the notice "compact(): Undefined variable: %s". */
    for (i = 0; i < argc; i++) {
        pa_compact_var(symbols, out, zval_deref(&argv[i]));
    }
    ZVAL_ARR(ret, out);
}

static zend_string *pa_prefix_varname(const zend_string *prefix,
                                      const char *name, size_t name_len)
{
    size_t plen = prefix ? ZSTR_LEN(prefix) : 0;
    zend_string *out = zend_string_alloc(plen + 1 + name_len);

    if (plen) {
        memcpy(ZSTR_VAL(out), ZSTR_VAL(prefix), plen);
    }
    ZSTR_VAL(out)[plen] = '_';
    memcpy(ZSTR_VAL(out) + plen + 1, name, name_len);
    ZSTR_VAL(out)[plen + 1 + name_len] = '\0';
    return out;
}

static void zif_extract(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("extract", 1, 3);
    zval *var_array = zval_deref(&argv[0]);
    zend_long extract_type = PA_EXTR_OVERWRITE;
    PA_OPT_LONG(extract_type, 1, "extract");
    zend_string *prefix = NULL;
    HashTable *symbols;
    zend_long count = 0;
    pa_entry *snap;
    uint32_t n, i;
    int extract_refs;

    if (Z_TYPE_P(var_array) != IS_ARRAY) {
        pa_array_error(var_array, 1, "extract");
        return;
    }
    extract_refs = (extract_type & PA_EXTR_REFS) != 0;
    extract_type &= 0xff;

    if (extract_type < PA_EXTR_OVERWRITE || extract_type > PA_EXTR_IF_EXISTS) {
        /* VERSIONGATE:80000 the warning-and-null branch below is the 5.6 one;
         * 8.0 throws ValueError. */
        zend_error(E_WARNING, "extract(): Invalid extract type");
        return;
    }
    if (extract_type > PA_EXTR_SKIP && extract_type <= PA_EXTR_PREFIX_IF_EXISTS
        && argc < 3) {
        zend_error(E_WARNING, "extract(): specified extract type requires the "
                   "prefix parameter");
        return;
    }
    if (argc > 2) {
        prefix = zval_get_string(&argv[2]);
        if (ZSTR_LEN(prefix)
            && !pa_valid_var_name(ZSTR_VAL(prefix), ZSTR_LEN(prefix))) {
            zend_error(E_WARNING, "extract(): prefix is not a valid identifier");
            return;
        }
    }
    symbols = pa_symbol_table();
    if (!symbols) {
        ZVAL_LONG(ret, 0);
        return;
    }

    /* EXTR_REFS is not implemented: values are always imported by value. */
    (void) extract_refs;

    snap = pa_snapshot(Z_ARR_P(var_array), &n);
    for (i = 0; i < n; i++) {
        zend_string *final_name = NULL;
        const char *var_name = NULL;
        size_t var_name_len = 0;
        int var_exists = 0;
        char numbuf[32];

        if (snap[i].key) {
            var_name = ZSTR_VAL(snap[i].key);
            var_name_len = ZSTR_LEN(snap[i].key);
            var_exists = zend_hash_find(symbols, snap[i].key) != NULL;
        } else if (extract_type == PA_EXTR_PREFIX_ALL
                   || extract_type == PA_EXTR_PREFIX_INVALID) {
            int ln = snprintf(numbuf, sizeof(numbuf), "%lld",
                              (long long) (zend_long) snap[i].h);
            final_name = pa_prefix_varname(prefix, numbuf, (size_t) ln);
        } else {
            continue;
        }

        if (!final_name) {
            switch (extract_type) {
            case PA_EXTR_IF_EXISTS:
                if (!var_exists) {
                    break;
                }
                /* fall through */
            case PA_EXTR_OVERWRITE:
                if (var_exists && var_name_len == 7
                    && memcmp(var_name, "GLOBALS", 7) == 0) {
                    break;
                }
                if (var_exists && var_name_len == 4
                    && memcmp(var_name, "this", 4) == 0) {
                    break;
                }
                final_name = zend_string_init(var_name, var_name_len);
                break;
            case PA_EXTR_PREFIX_IF_EXISTS:
                if (var_exists) {
                    final_name = pa_prefix_varname(prefix, var_name, var_name_len);
                }
                break;
            case PA_EXTR_PREFIX_SAME:
                if (!var_exists && var_name_len != 0) {
                    final_name = zend_string_init(var_name, var_name_len);
                    break;
                }
                /* fall through */
            case PA_EXTR_PREFIX_ALL:
                if (var_name_len != 0) {
                    final_name = pa_prefix_varname(prefix, var_name, var_name_len);
                }
                break;
            case PA_EXTR_PREFIX_INVALID:
                if (!pa_valid_var_name(var_name, var_name_len)) {
                    final_name = pa_prefix_varname(prefix, var_name, var_name_len);
                } else {
                    final_name = zend_string_init(var_name, var_name_len);
                }
                break;
            default:   /* EXTR_SKIP */
                if (!var_exists) {
                    final_name = zend_string_init(var_name, var_name_len);
                }
                break;
            }
        }

        if (final_name
            && pa_valid_var_name(ZSTR_VAL(final_name), ZSTR_LEN(final_name))) {
            zend_hash_update(symbols, final_name, snap[i].val);
            count++;
        }
    }
    free(snap);
    ZVAL_LONG(ret, count);
}

/* ------------------------------------------------- internal array pointer */

static HashTable *pa_pointer_arg(zval *arg, const char *fname)
{
    zval *v = pa_ref(arg);

    if (Z_TYPE_P(v) == IS_ARRAY) {
        return Z_ARR_P(v);
    }
    if (Z_TYPE_P(v) == IS_OBJECT && Z_OBJ_P(v)->properties) {
        return Z_OBJ_P(v)->properties;
    }
    pa_array_error(v, 1, fname);
    return NULL;
}

static Bucket *pa_current_bucket(HashTable *ht)
{
    uint32_t i;

    for (i = ht->nInternalPointer; i < ht->nNumUsed; i++) {
        if (Z_TYPE(ht->arData[i].val) != IS_UNDEF) {
            ht->nInternalPointer = i;
            return &ht->arData[i];
        }
    }
    ht->nInternalPointer = ht->nNumUsed;
    return NULL;
}

static void pa_move_forward(HashTable *ht)
{
    uint32_t i;

    for (i = ht->nInternalPointer + 1; i < ht->nNumUsed; i++) {
        if (Z_TYPE(ht->arData[i].val) != IS_UNDEF) {
            ht->nInternalPointer = i;
            return;
        }
    }
    ht->nInternalPointer = ht->nNumUsed;
}

static void pa_move_backwards(HashTable *ht)
{
    uint32_t i;

    for (i = ht->nInternalPointer; i > 0; i--) {
        if (Z_TYPE(ht->arData[i - 1].val) != IS_UNDEF) {
            ht->nInternalPointer = i - 1;
            return;
        }
    }
    ht->nInternalPointer = ht->nNumUsed;   /* before the start = invalid */
}

static void pa_pointer_reset(HashTable *ht)
{
    ht->nInternalPointer = 0;
    (void) pa_current_bucket(ht);
}

static void pa_pointer_end(HashTable *ht)
{
    uint32_t i;

    for (i = ht->nNumUsed; i > 0; i--) {
        if (Z_TYPE(ht->arData[i - 1].val) != IS_UNDEF) {
            ht->nInternalPointer = i - 1;
            return;
        }
    }
    ht->nInternalPointer = ht->nNumUsed;
}

static void pa_return_current(zval *ret, HashTable *ht)
{
    Bucket *b = pa_current_bucket(ht);

    if (!b) {
        pa_ret_false(ret);
        return;
    }
    *ret = zval_copy(*zval_deref(&b->val));
}

static void pa_current_common(zval *ret, uint32_t argc, zval *argv,
                              const char *fname)
{
    HashTable *ht;

    if (argc != 1) {
        zend_wrong_param_count(fname, argc, 1, 1);
        return;
    }
    ht = pa_pointer_arg(&argv[0], fname);
    if (!ht) {
        return;
    }
    pa_return_current(ret, ht);
}

static void zif_current(zval *ret, uint32_t argc, zval *argv)
{
    pa_current_common(ret, argc, argv, "current");
}

static void zif_pos(zval *ret, uint32_t argc, zval *argv)
{
    pa_current_common(ret, argc, argv, "pos");
}

static void zif_key(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("key", 1, 1);
    HashTable *ht = pa_pointer_arg(&argv[0], "key");
    Bucket *b;
    (void) argc;

    if (!ht) {
        return;
    }
    b = pa_current_bucket(ht);
    if (!b) {
        ZVAL_NULL(ret);
        return;
    }
    *ret = pa_bucket_key(b);
}

static void zif_next(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("next", 1, 1);
    HashTable *ht = pa_pointer_arg(&argv[0], "next");
    (void) argc;
    if (!ht) {
        return;
    }
    (void) pa_current_bucket(ht);
    pa_move_forward(ht);
    pa_return_current(ret, ht);
}

static void zif_prev(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("prev", 1, 1);
    HashTable *ht = pa_pointer_arg(&argv[0], "prev");
    (void) argc;
    if (!ht) {
        return;
    }
    pa_move_backwards(ht);
    pa_return_current(ret, ht);
}

static void zif_reset(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("reset", 1, 1);
    HashTable *ht = pa_pointer_arg(&argv[0], "reset");
    (void) argc;
    if (!ht) {
        return;
    }
    pa_pointer_reset(ht);
    pa_return_current(ret, ht);
}

static void zif_end(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("end", 1, 1);
    HashTable *ht = pa_pointer_arg(&argv[0], "end");
    (void) argc;
    if (!ht) {
        return;
    }
    pa_pointer_end(ht);
    pa_return_current(ret, ht);
}

/* each() is removed in 8.0; the registry row carries `until` for that. */
static void zif_each(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("each", 1, 1);
    zval *v = pa_ref(&argv[0]);
    HashTable *ht;
    Bucket *b;
    HashTable *out;
    zval key, val;
    (void) argc;

    if (Z_TYPE_P(v) == IS_ARRAY) {
        ht = Z_ARR_P(v);
    } else if (Z_TYPE_P(v) == IS_OBJECT && Z_OBJ_P(v)->properties) {
        ht = Z_OBJ_P(v)->properties;
    } else {
        zend_error(E_WARNING, "Variable passed to each() is not an array or object");
        return;
    }
    b = pa_current_bucket(ht);
    if (!b) {
        pa_ret_false(ret);
        return;
    }
    key = pa_bucket_key(b);
    val = zval_copy(*zval_deref(&b->val));

    out = zend_hash_new();
    zend_hash_index_update(out, 1, val);
    zend_hash_update(out, pa_zs("value"), zval_copy(val));
    zend_hash_index_update(out, 0, key);
    zend_hash_update(out, pa_zs("key"), zval_copy(key));

    pa_move_forward(ht);
    ZVAL_ARR(ret, out);
}

/* ------------------------------------------------------- shuffle/array_rand */

/* The Mersenne Twister lives in ext/standard/php_rand.c, owned by another
 * module; this file may include only the three frozen headers, so the entry
 * point is declared here.  php_mt_rand_common() applies php-src's RAND_RANGE
 * scaling, matching the shape of 5.6's php_rand()+RAND_RANGE call; 5.6 drew the
 * raw number from libc random() rather than the Twister, so seeded sequences
 * differ from the oracle (the .phpt tests wildcard them). */
extern zend_long php_mt_rand_common(zend_long min, zend_long max);

static zend_long pa_rand_range(zend_long max_inclusive)
{
    if (max_inclusive <= 0) {
        return 0;
    }
    return php_mt_rand_common(0, max_inclusive);
}

static void zif_shuffle(zval *ret, uint32_t argc, zval *argv)
{
    pa_ret_false(ret);   /* php-src returns false from this one on a parameter error */
    PA_ARGC("shuffle", 1, 1);
    zval *arg = pa_ref(&argv[0]);
    HashTable *ht;
    pa_entry *snap;
    uint32_t n, n_left;
    (void) argc;

    if (Z_TYPE_P(arg) != IS_ARRAY) {
        pa_array_error(arg, 1, "shuffle");
        pa_ret_false(ret);
        return;
    }
    zval_separate_array(arg);
    ht = Z_ARR_P(arg);
    snap = pa_snapshot(ht, &n);

    n_left = n;
    while (n_left > 1) {
        zend_long rnd = pa_rand_range((zend_long) n_left - 1);
        pa_entry t;
        n_left--;
        if ((uint32_t) rnd != n_left) {
            t = snap[n_left];
            snap[n_left] = snap[(uint32_t) rnd];
            snap[(uint32_t) rnd] = t;
        }
    }
    pa_rebuild(ht, snap, n, 1);
    pa_ret_true(ret);
}

static void zif_array_rand(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("array_rand", 1, 2);
    HashTable *ht;
    zend_long num_req = 1;
    PA_OPT_LONG(num_req, 1, "array_rand");
    zend_long num_avail, i;
    pa_entry *snap;
    uint32_t n;

    if (Z_TYPE(argv[0]) != IS_ARRAY) {
        pa_array_error(&argv[0], 1, "array_rand");
        return;
    }
    ht = Z_ARR(argv[0]);
    num_avail = (zend_long) zend_hash_num_elements(ht);

    /* VERSIONGATE:80000 else-branch is the 5.6/7.x warning-and-null form */
    if (num_req <= 0 || num_req > num_avail) {
        if (EG(php_version) >= 80000) {
            zend_throw_error("ValueError", "array_rand(): Argument #2 ($num) must "
                             "be between 1 and the number of elements in "
                             "argument #1 ($array)");
            return;
        }
        zend_error(E_WARNING, "array_rand(): Second argument has to be between "
                   "1 and the number of elements in the array");
        return;
    }
    snap = pa_snapshot(ht, &n);
    if (num_req == 1) {
        zend_long pick = pa_rand_range(num_avail - 1);
        if (pick >= (zend_long) n) {
            pick = (zend_long) n - 1;
        }
        if (snap[pick].key) {
            ZVAL_STR(ret, snap[pick].key);
        } else {
            ZVAL_LONG(ret, (zend_long) snap[pick].h);
        }
    } else {
        HashTable *out = zend_hash_new();
        zend_long left = num_req;
        for (i = 0; i < (zend_long) n && left > 0; i++) {
            zend_long remaining = (zend_long) n - i;
            if (pa_rand_range(remaining - 1) < left) {
                zval k;
                if (snap[i].key) {
                    ZVAL_STR(&k, snap[i].key);
                } else {
                    ZVAL_LONG(&k, (zend_long) snap[i].h);
                }
                zend_hash_next_index_insert(out, k);
                left--;
            }
        }
        ZVAL_ARR(ret, out);
    }
    free(snap);
}

/* --------------------------------------------------------------- min/max */

static void pa_minmax(zval *ret, uint32_t argc, zval *argv, int want_max,
                      const char *fname)
{
    uint32_t i;
    zval *best;

    if (argc == 1) {
        if (Z_TYPE(argv[0]) != IS_ARRAY) {
            /* VERSIONGATE:80000 the warning-and-null branch below is the 5.6
             * one; 8.0 raises a TypeError. */
            zend_error(E_WARNING, "%s(): When only one parameter is given, it "
                       "must be an array", fname);
            ZVAL_NULL(ret);
            return;
        }
        if (zend_hash_num_elements(Z_ARR(argv[0])) == 0) {
            /* VERSIONGATE:80000 the warning-and-false branch below is the 5.6
             * one; 8.0 throws ValueError. */
            zend_error(E_WARNING, "%s(): Array must contain at least one element",
                       fname);
            pa_ret_false(ret);
            return;
        }
        best = NULL;
        ZEND_HASH_FOREACH(Z_ARR(argv[0]), b) {
            zval *entry = zval_deref(&b->val);
            if (best == NULL) {
                best = entry;
                continue;
            }
            {
                int r = 0;
                if (zend_compare(best, entry, &r) != 0) {
                    return;
                }
                if (want_max ? (r < 0) : (r > 0)) {
                    best = entry;
                }
            }
        } ZEND_HASH_FOREACH_END();
        if (best) {
            *ret = zval_copy(*best);
        }
        return;
    }

    best = zval_deref(&argv[0]);
    for (i = 1; i < argc; i++) {
        zval *cur = zval_deref(&argv[i]);
        int r = 0;
        if (zend_compare(cur, best, &r) != 0) {
            return;
        }
        if (want_max ? (r > 0) : (r < 0)) {
            best = cur;
        }
    }
    *ret = zval_copy(*best);
}

static void zif_min(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("min", 1, UINT32_MAX);
    pa_minmax(ret, argc, argv, 0, "min");
}

static void zif_max(zval *ret, uint32_t argc, zval *argv)
{
    PA_ARGC("max", 1, UINT32_MAX);
    pa_minmax(ret, argc, argv, 1, "max");
}

/* ------------------------------------------------------ array_is_list (8.1) */



/* --------------------------------- array_find/any/all/find_key (8.4) */

/* The callback receives ($value, $key). */











/* ------------------------------------------------------------- registry */

const php_function_entry php_functions_array[] = {
    { "count",                  zif_count, 0, 2, NULL,  0, 0 },
    { "sizeof",                 zif_sizeof, 0, 2, NULL,  0, 0 },
    { "in_array",               zif_in_array, 0, 3, NULL,  0, 0 },
    { "array_search",           zif_array_search, 0, 3, NULL,  0, 0 },
    { "array_keys",             zif_array_keys, 0, 3, NULL,  0, 0 },
    { "array_values",           zif_array_values, 0, 1, NULL,  0, 0 },
    { "array_merge",            zif_array_merge, 0, UINT32_MAX, NULL, 0, 0 },
    { "array_merge_recursive",  zif_array_merge_recursive, 0, UINT32_MAX, NULL, 0, 0 },
    { "array_combine",          zif_array_combine, 0, 2, NULL,  0, 0 },
    { "array_flip",             zif_array_flip, 0, 1, NULL,  0, 0 },
    { "array_fill",             zif_array_fill, 0, 3, NULL,  0, 0 },
    { "array_fill_keys",        zif_array_fill_keys, 0, 2, NULL,  0, 0 },
    { "array_slice",            zif_array_slice, 0, 4, NULL,  0, 0 },
    { "array_splice",           zif_array_splice, 0, 4, "1",   0, 0 },
    { "array_map",              zif_array_map, 0, UINT32_MAX, NULL, 0, 0 },
    { "array_filter",           zif_array_filter, 0, 3, NULL,  0, 0 },
    { "array_reduce",           zif_array_reduce, 0, 3, NULL,  0, 0 },
    { "array_walk",             zif_array_walk, 0, 3, "1",   0, 0 },
    { "array_reverse",          zif_array_reverse, 0, 2, NULL,  0, 0 },
    { "array_unique",           zif_array_unique, 0, 2, NULL,  0, 0 },
    /* the diff/intersect family reports its own count diagnostic */
    { "array_diff",             zif_array_diff, 0, UINT32_MAX, NULL, 0, 0 },
    { "array_diff_key",         zif_array_diff_key, 0, UINT32_MAX, NULL, 0, 0 },
    { "array_diff_assoc",       zif_array_diff_assoc, 0, UINT32_MAX, NULL, 0, 0 },
    { "array_intersect",        zif_array_intersect, 0, UINT32_MAX, NULL, 0, 0 },
    { "array_intersect_key",    zif_array_intersect_key, 0, UINT32_MAX, NULL, 0, 0 },
    { "array_intersect_assoc",  zif_array_intersect_assoc, 0, UINT32_MAX, NULL, 0, 0 },
    { "array_key_exists",       zif_array_key_exists, 0, 2, NULL,  0, 0 },
    { "key_exists",             zif_key_exists, 0, 2, NULL,  0, 0 },
    { "array_push",             zif_array_push, 0, UINT32_MAX, "10", 0, 0 },
    { "array_pop",              zif_array_pop, 0, 1, "1",   0, 0 },
    { "array_shift",            zif_array_shift, 0, 1, "1",   0, 0 },
    { "array_unshift",          zif_array_unshift, 0, UINT32_MAX, "10", 0, 0 },
    { "array_pad",              zif_array_pad, 0, 3, NULL,  0, 0 },
    { "array_sum",              zif_array_sum, 0, 1, NULL,  0, 0 },
    { "array_product",          zif_array_product, 0, 1, NULL,  0, 0 },
    { "array_column",           zif_array_column, 0, 3, NULL,  0, 0 },
    { "array_chunk",            zif_array_chunk, 0, 3, NULL,  0, 0 },
    /* VERSIONGATE:70300 */
    { "array_key_first",        zif_array_key_first, 0, 1, NULL,  70300, 0 },
    /* VERSIONGATE:70300 */
    { "array_key_last",         zif_array_key_last, 0, 1, NULL,  70300, 0 },
    { "range",                  zif_range, 0, 3, NULL,  0, 0 },
    { "compact",                zif_compact, 0, UINT32_MAX, NULL, 0, 0 },
    { "extract",                zif_extract, 0, 3, NULL,  0, 0 },
    { "sort",                   zif_sort, 0, 2, "1",   0, 0 },
    { "rsort",                  zif_rsort, 0, 2, "1",   0, 0 },
    { "usort",                  zif_usort, 0, 2, "1",   0, 0 },
    { "uasort",                 zif_uasort, 0, 2, "1",   0, 0 },
    { "uksort",                 zif_uksort, 0, 2, "1",   0, 0 },
    { "ksort",                  zif_ksort, 0, 2, "1",   0, 0 },
    { "krsort",                 zif_krsort, 0, 2, "1",   0, 0 },
    { "asort",                  zif_asort, 0, 2, "1",   0, 0 },
    { "arsort",                 zif_arsort, 0, 2, "1",   0, 0 },
    { "natsort",                zif_natsort, 0, 1, "1",   0, 0 },
    { "natcasesort",            zif_natcasesort, 0, 1, "1",   0, 0 },
    { "shuffle",                zif_shuffle, 0, 1, "1",   0, 0 },
    { "current",                zif_current, 0, 1, NULL,  0, 0 },
    { "pos",                    zif_pos, 0, 1, NULL,  0, 0 },
    { "key",                    zif_key, 0, 1, NULL,  0, 0 },
    { "next",                   zif_next, 0, 1, "1",   0, 0 },
    { "prev",                   zif_prev, 0, 1, "1",   0, 0 },
    { "reset",                  zif_reset, 0, 1, "1",   0, 0 },
    { "end",                    zif_end, 0, 1, "1",   0, 0 },
    { "each",                   zif_each, 0, 1, "1",   0, 80000 },
    { "array_rand",             zif_array_rand, 0, 2, NULL,  0, 0 },
    { "min",                    zif_min, 0, UINT32_MAX, NULL, 0, 0 },
    { "max",                    zif_max, 0, UINT32_MAX, NULL, 0, 0 },





    { NULL, NULL, 0, 0, NULL, 0, 0 }
};

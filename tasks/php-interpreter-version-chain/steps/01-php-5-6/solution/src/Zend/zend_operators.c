/* zend_operators.c — conversions, comparison and arithmetic.
 *
 * Semantics follow php-src's Zend/zend_operators.c.  Places where a release
 * changed behaviour carry a VERSION: comment.
 */
#include "zend.h"
#include "zend_ast.h"
#include "zend_API.h"
#include "zend_alloc.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>
#include <ctype.h>

zend_globals EG_;

/* ------------------------------------------------------------ zval basics */
zval zval_null(void)              { zval z; z.u1 = IS_NULL; z.value.lval = 0; return z; }
zval zval_bool(int b)             { zval z; z.u1 = b ? IS_TRUE : IS_FALSE; z.value.lval = 0; return z; }
zval zval_long(zend_long l)       { zval z; z.u1 = IS_LONG; z.value.lval = l; return z; }
zval zval_double(double d)        { zval z; z.u1 = IS_DOUBLE; z.value.dval = d; return z; }
zval zval_zstr(zend_string *s)    { zval z; z.u1 = IS_STRING; z.value.str = s; return z; }
zval zval_string(const char *s)   { return zval_zstr(zend_string_initz(s)); }
zval zval_stringl(const char *s, size_t l) { return zval_zstr(zend_string_init(s, l)); }
zval zval_array(HashTable *ht)    { zval z; z.u1 = IS_ARRAY; z.value.arr = ht; return z; }
zval zval_object(zend_object *o)  { zval z; z.u1 = IS_OBJECT; z.value.obj = o; return z; }

zval *zval_deref(zval *v)
{
    while (v && v->u1 == IS_REFERENCE) v = &v->value.ref->val;
    return v;
}

zval zval_copy(zval v)
{
    /* Value semantics.  Arrays are shared until written to (copy on write);
     * objects are shared for real; strings are immutable and shared. */
    if (v.u1 == IS_ARRAY) {
        v.value.arr->refcount++;
    } else if (v.u1 == IS_OBJECT) {
        v.value.obj->refcount++;
    } else if (v.u1 == IS_STRING) {
        v.value.str->refcount++;
    } else if (v.u1 == IS_REFERENCE) {
        v.value.ref->refcount++;
    }
    return v;
}

void zval_separate_array(zval *v)
{
    if (v->u1 == IS_ARRAY && v->value.arr->refcount > 1) {
        HashTable *n = zend_hash_dup(v->value.arr);
        v->value.arr->refcount--;
        v->value.arr = n;
    }
}

zval zval_clone_deep(zval v)
{
    if (v.u1 != IS_ARRAY) return zval_copy(v);
    HashTable *n = zend_hash_dup(v.value.arr);
    ZEND_HASH_FOREACH(n, b) {
        if (Z_TYPE(b->val) == IS_ARRAY) {
            zval tmp = zval_clone_deep(b->val);
            b->val = tmp;
        }
    } ZEND_HASH_FOREACH_END();
    return zval_array(n);
}

/* --------------------------------------------------------- numeric strings */
/* php-src's _is_numeric_string_ex.  Returns IS_LONG, IS_DOUBLE, or 0.
 * allow_errors: 0 = strict (trailing data rejected), 1 = trailing data allowed
 * (the behaviour of the arithmetic operators before PHP 8). */
int is_numeric_string(const char *s, size_t len, zend_long *lval, double *dval, int allow_errors)
{
    const char *p = s, *end = s + len;
    int type = IS_LONG;
    const char *digits;

    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' ||
                       *p == '\v' || *p == '\f')) p++;
    if (p == end) return 0;
    if (*p == '+' || *p == '-') p++;
    digits = p;
    while (p < end && *p >= '0' && *p <= '9') p++;
    if (p < end && *p == '.') {
        const char *dot = p;
        p++;
        while (p < end && *p >= '0' && *p <= '9') p++;
        if (p == dot + 1 && dot == digits) return 0;   /* just "." */
        type = IS_DOUBLE;
    }
    if (p == digits) return 0;                          /* no digits at all */
    if (p < end && (*p == 'e' || *p == 'E')) {
        const char *ex = p;
        p++;
        if (p < end && (*p == '+' || *p == '-')) p++;
        if (p < end && *p >= '0' && *p <= '9') {
            while (p < end && *p >= '0' && *p <= '9') p++;
            type = IS_DOUBLE;
        } else {
            p = ex;
        }
    }
    /* trailing whitespace is allowed from PHP 8.0; before that only leading.
     * VERSION: php 8.0 accepts trailing whitespace in numeric strings. */
    {
        const char *q = p;

        if (q != end) {
            if (!allow_errors) return 0;
            /* leading-numeric string: value is taken from the prefix */
        }
    }

    {
        char buf[512];
        size_t n = (size_t)(p - s);
        if (n >= sizeof(buf)) n = sizeof(buf) - 1;
        memcpy(buf, s, n);
        buf[n] = '\0';
        if (type == IS_LONG) {
            errno = 0;
            char *endp = NULL;
            long long v = strtoll(buf, &endp, 10);
            if (errno == ERANGE) {
                if (dval) *dval = strtod(buf, NULL);
                return IS_DOUBLE;
            }
            if (lval) *lval = (zend_long)v;
            return IS_LONG;
        }
        if (dval) *dval = strtod(buf, NULL);
        return IS_DOUBLE;
    }
}

/* ------------------------------------------------------------ conversions */
const char *zend_get_type_name(const zval *v)
{
    switch (v->u1) {
    case IS_UNDEF:
    case IS_NULL:    return "NULL";
    case IS_FALSE:
    case IS_TRUE:    return "boolean";
    case IS_LONG:    return "integer";
    case IS_DOUBLE:  return "double";
    case IS_STRING:  return "string";
    case IS_ARRAY:   return "array";
    case IS_OBJECT:  return "object";
    default:         return "unknown type";
    }
}

const char *zend_zval_type_name(const zval *v)
{
    switch (v->u1) {
    case IS_UNDEF:
    case IS_NULL:    return "null";
    case IS_FALSE:
    case IS_TRUE:    return "bool";
    case IS_LONG:    return "int";
    case IS_DOUBLE:  return "float";
    case IS_STRING:  return "string";
    case IS_ARRAY:   return "array";
    case IS_OBJECT:  return "object";
    default:         return "mixed";
    }
}

int zval_is_true(const zval *v)
{
    const zval *z = zval_deref((zval *)v);
    switch (z->u1) {
    case IS_UNDEF:
    case IS_NULL:   return 0;
    case IS_FALSE:  return 0;
    case IS_TRUE:   return 1;
    case IS_LONG:   return z->value.lval != 0;
    case IS_DOUBLE: return z->value.dval != 0.0;
    case IS_STRING:
        if (z->value.str->len == 0) return 0;
        if (z->value.str->len == 1 && z->value.str->val[0] == '0') return 0;
        return 1;
    case IS_ARRAY:  return zend_hash_num_elements(z->value.arr) != 0;
    case IS_OBJECT: return 1;
    default:        return 0;
    }
}

zend_long zval_get_long(const zval *v)
{
    const zval *z = zval_deref((zval *)v);
    switch (z->u1) {
    case IS_UNDEF:
    case IS_NULL:   return 0;
    case IS_FALSE:  return 0;
    case IS_TRUE:   return 1;
    case IS_LONG:   return z->value.lval;
    case IS_DOUBLE: {
        double d = z->value.dval;
        if (!isfinite(d) || fabs(d) >= 9.2233720368547758e18) {
            /* php-src: out-of-range doubles are undefined; zend_dval_to_lval
             * wraps modulo 2^64 since PHP 7. */
            if (!isfinite(d)) return 0;
            double two64 = 18446744073709551616.0;
            double dmod = fmod(d, two64);
            if (dmod < 0) dmod += two64;
            if (dmod >= 9223372036854775808.0) dmod -= two64;
            return (zend_long)dmod;
        }
        return (zend_long)d;
    }
    case IS_STRING: {
        zend_long l = 0; double d = 0;
        int t = is_numeric_string(z->value.str->val, z->value.str->len, &l, &d, 1);
        if (t == IS_LONG) return l;
        if (t == IS_DOUBLE) { zval tmp = zval_double(d); return zval_get_long(&tmp); }
        return 0;
    }
    case IS_ARRAY:  return zend_hash_num_elements(z->value.arr) ? 1 : 0;
    case IS_OBJECT:
        zend_error(E_NOTICE, "Object of class %s could not be converted to int",
                   z->value.obj->ce->name->val);
        return 1;
    default: return 0;
    }
}

double zval_get_double(const zval *v)
{
    const zval *z = zval_deref((zval *)v);
    switch (z->u1) {
    case IS_UNDEF:
    case IS_NULL:   return 0.0;
    case IS_FALSE:  return 0.0;
    case IS_TRUE:   return 1.0;
    case IS_LONG:   return (double)z->value.lval;
    case IS_DOUBLE: return z->value.dval;
    case IS_STRING: {
        zend_long l = 0; double d = 0;
        int t = is_numeric_string(z->value.str->val, z->value.str->len, &l, &d, 1);
        if (t == IS_LONG) return (double)l;
        if (t == IS_DOUBLE) return d;
        return 0.0;
    }
    case IS_ARRAY:  return zend_hash_num_elements(z->value.arr) ? 1.0 : 0.0;
    case IS_OBJECT:
        zend_error(E_NOTICE, "Object of class %s could not be converted to float",
                   z->value.obj->ce->name->val);
        return 1.0;
    default: return 0.0;
    }
}

zend_string *zval_get_string(const zval *v)
{
    const zval *z = zval_deref((zval *)v);
    switch (z->u1) {
    case IS_UNDEF:
    case IS_NULL:   return zend_string_init("", 0);
    case IS_FALSE:  return zend_string_init("", 0);
    case IS_TRUE:   return zend_string_init("1", 1);
    case IS_LONG:   return php_long_to_string(z->value.lval);
    case IS_DOUBLE: return php_double_to_string(z->value.dval, 14, 0);
    case IS_STRING: return z->value.str;
    case IS_ARRAY:
        zend_error(E_NOTICE, "Array to string conversion");
        return zend_string_init("Array", 5);
    case IS_OBJECT: {
        zend_string *out = NULL;
        if (zend_object_to_string(z->value.obj, &out) == 0) return out;
        return zend_string_init("", 0);
    }
    default: return zend_string_init("", 0);
    }
}

void convert_to_string(zval *v)  { zend_string *s = zval_get_string(v); v->u1 = IS_STRING; v->value.str = s; }
void convert_to_long(zval *v)    { zend_long l = zval_get_long(v); v->u1 = IS_LONG; v->value.lval = l; }
void convert_to_double(zval *v)  { double d = zval_get_double(v); v->u1 = IS_DOUBLE; v->value.dval = d; }
void convert_to_boolean(zval *v) { int b = zval_is_true(v); v->u1 = b ? IS_TRUE : IS_FALSE; v->value.lval = 0; }

void convert_to_array(zval *v)
{
    zval *z = zval_deref(v);
    switch (z->u1) {
    case IS_ARRAY: *v = *z; return;
    case IS_UNDEF:
    case IS_NULL: { HashTable *ht = zend_hash_new(); *v = zval_array(ht); return; }
    case IS_OBJECT: {
        HashTable *ht = zend_hash_new();
        zend_object *o = z->value.obj;
        if (o->properties) {
            ZEND_HASH_FOREACH(o->properties, b) {
                if (b->key) zend_hash_update(ht, b->key, zval_copy(b->val));
                else zend_hash_index_update(ht, (zend_long)b->h, zval_copy(b->val));
            } ZEND_HASH_FOREACH_END();
        }
        *v = zval_array(ht);
        return;
    }
    default: {
        HashTable *ht = zend_hash_new();
        zend_hash_index_update(ht, 0, zval_copy(*z));
        *v = zval_array(ht);
        return;
    }
    }
}

/* ------------------------------------------------------------- comparison */
static int compare_arrays(HashTable *a, HashTable *b, int *result)
{
    uint32_t na = zend_hash_num_elements(a), nb = zend_hash_num_elements(b);
    if (na != nb) { *result = na < nb ? -1 : 1; return 0; }
    ZEND_HASH_FOREACH(a, ba) {
        zval *other;
        if (ba->key) other = zend_hash_find(b, ba->key);
        else other = zend_hash_index_find(b, (zend_long)ba->h);
        if (!other) { *result = 1; return 0; }
        int r = 0;
        if (zend_compare(&ba->val, other, &r)) return 1;
        if (r != 0) { *result = r; return 0; }
    } ZEND_HASH_FOREACH_END();
    *result = 0;
    return 0;
}

static int compare_strings(zend_string *a, zend_string *b)
{
    size_t n = a->len < b->len ? a->len : b->len;
    int r = n ? memcmp(a->val, b->val, n) : 0;
    if (r) return r > 0 ? 1 : -1;
    if (a->len == b->len) return 0;
    return a->len < b->len ? -1 : 1;
}

/* smart string comparison: numeric when both sides look numeric */
static int compare_strings_smart(zend_string *a, zend_string *b, int *res)
{
    zend_long la = 0, lb = 0; double da = 0, db = 0;
    int ta = is_numeric_string(a->val, a->len, &la, &da, 0);
    int tb = is_numeric_string(b->val, b->len, &lb, &db, 0);
    if (ta && tb) {
        if (ta == IS_LONG && tb == IS_LONG) {
            *res = la == lb ? 0 : (la < lb ? -1 : 1);
        } else {
            double x = ta == IS_LONG ? (double)la : da;
            double y = tb == IS_LONG ? (double)lb : db;
            if (x == y) *res = 0; else *res = x < y ? -1 : 1;
        }
        return 0;
    }
    *res = compare_strings(a, b);
    return 0;
}

/* Compare a number with a string.  Before PHP 8 the string was cast to a
 * number; from PHP 8 a non-numeric string turns the comparison into a string
 * comparison instead.
 * VERSION: PHP 8.0 "Saner string to number comparisons". */
static int compare_num_string(const zval *num, zend_string *s)
{
    zend_long l = 0; double d = 0;
    int t = is_numeric_string(s->val, s->len, &l, &d, 0);

    if (num->u1 == IS_LONG && t == IS_LONG) {
        zend_long a = num->value.lval;
        return a == l ? 0 : (a < l ? -1 : 1);
    }
    {
        double x = zval_get_double(num);
        double y;
        if (t == IS_LONG) y = (double)l;
        else if (t == IS_DOUBLE) y = d;
        else {
            /* pre-8 fallback: leading-numeric or non-numeric string -> number */
            zend_long l2 = 0; double d2 = 0;
            int t2 = is_numeric_string(s->val, s->len, &l2, &d2, 1);
            y = t2 == IS_LONG ? (double)l2 : (t2 == IS_DOUBLE ? d2 : 0.0);
        }
        if (x == y) return 0;
        return x < y ? -1 : 1;
    }
}

static int compare_objects(zend_object *a, zend_object *b, int *result)
{
    if (a == b) { *result = 0; return 0; }
    if (a->ce != b->ce) { *result = 1; return 0; }
    if (!a->properties || !b->properties) { *result = a == b ? 0 : 1; return 0; }
    return compare_arrays(a->properties, b->properties, result);
}

int zend_compare(const zval *a0, const zval *b0, int *result)
{
    zval *a = zval_deref((zval *)a0);
    zval *b = zval_deref((zval *)b0);
    uint8_t ta = a->u1 == IS_UNDEF ? IS_NULL : a->u1;
    uint8_t tb = b->u1 == IS_UNDEF ? IS_NULL : b->u1;

    if (ta == IS_LONG && tb == IS_LONG) {
        *result = a->value.lval == b->value.lval ? 0 : (a->value.lval < b->value.lval ? -1 : 1);
        return 0;
    }
    if ((ta == IS_LONG || ta == IS_DOUBLE) && (tb == IS_LONG || tb == IS_DOUBLE)) {
        double x = ta == IS_LONG ? (double)a->value.lval : a->value.dval;
        double y = tb == IS_LONG ? (double)b->value.lval : b->value.dval;
        if (x == y) *result = 0;
        else if (x < y) *result = -1;
        else if (x > y) *result = 1;
        else *result = 1;                 /* NaN: php-src yields 1 */
        return 0;
    }
    if (ta == IS_ARRAY && tb == IS_ARRAY) return compare_arrays(a->value.arr, b->value.arr, result);
    if (ta == IS_STRING && tb == IS_STRING) return compare_strings_smart(a->value.str, b->value.str, result);

    if (ta == IS_NULL && tb == IS_STRING) { *result = b->value.str->len == 0 ? 0 : -1; return 0; }
    if (ta == IS_STRING && tb == IS_NULL) { *result = a->value.str->len == 0 ? 0 : 1; return 0; }

    if ((ta == IS_LONG || ta == IS_DOUBLE) && tb == IS_STRING) {
        *result = compare_num_string(a, b->value.str);
        return 0;
    }
    if (ta == IS_STRING && (tb == IS_LONG || tb == IS_DOUBLE)) {
        *result = -compare_num_string(b, a->value.str);
        return 0;
    }

    if (ta == IS_OBJECT && tb == IS_OBJECT) return compare_objects(a->value.obj, b->value.obj, result);
    if (ta == IS_OBJECT && tb == IS_STRING && a->value.obj->ce->__tostring) {
        zend_string *s = NULL;
        if (zend_object_to_string(a->value.obj, &s)) return 1;
        *result = compare_strings_smart(s, b->value.str, result) ? 1 : *result;
        return 0;
    }
    if (ta == IS_STRING && tb == IS_OBJECT && b->value.obj->ce->__tostring) {
        zend_string *s = NULL;
        if (zend_object_to_string(b->value.obj, &s)) return 1;
        compare_strings_smart(a->value.str, s, result);
        return 0;
    }

    /* null, false and true compare as booleans against everything else */
    if (ta == IS_NULL || ta == IS_FALSE || ta == IS_TRUE ||
        tb == IS_NULL || tb == IS_FALSE || tb == IS_TRUE) {
        int x = zval_is_true(a), y = zval_is_true(b);
        *result = x == y ? 0 : (x < y ? -1 : 1);
        return 0;
    }
    if (ta == IS_ARRAY) { *result = 1; return 0; }
    if (tb == IS_ARRAY) { *result = -1; return 0; }
    if (ta == IS_OBJECT) { *result = 1; return 0; }
    if (tb == IS_OBJECT) { *result = -1; return 0; }
    *result = 0;
    return 0;
}

int zend_is_equal(const zval *a, const zval *b)
{
    int r = 0;
    if (zend_compare(a, b, &r)) return 0;
    return r == 0;
}

int zend_is_identical(const zval *a0, const zval *b0)
{
    zval *a = zval_deref((zval *)a0);
    zval *b = zval_deref((zval *)b0);
    uint8_t ta = a->u1 == IS_UNDEF ? IS_NULL : a->u1;
    uint8_t tb = b->u1 == IS_UNDEF ? IS_NULL : b->u1;
    if (ta != tb) return 0;
    switch (ta) {
    case IS_NULL: case IS_FALSE: case IS_TRUE: return 1;
    case IS_LONG:   return a->value.lval == b->value.lval;
    case IS_DOUBLE: return a->value.dval == b->value.dval;
    case IS_STRING: return zend_string_equals(a->value.str, b->value.str);
    case IS_OBJECT: return a->value.obj == b->value.obj;
    case IS_ARRAY: {
        HashTable *x = a->value.arr, *y = b->value.arr;
        if (x == y) return 1;
        if (zend_hash_num_elements(x) != zend_hash_num_elements(y)) return 0;
        uint32_t i = 0, j = 0;
        while (1) {
            while (i < x->nNumUsed && Z_TYPE(x->arData[i].val) == IS_UNDEF) i++;
            while (j < y->nNumUsed && Z_TYPE(y->arData[j].val) == IS_UNDEF) j++;
            if (i >= x->nNumUsed || j >= y->nNumUsed) break;
            Bucket *bx = &x->arData[i], *by = &y->arData[j];
            if ((bx->key == NULL) != (by->key == NULL)) return 0;
            if (bx->key) { if (!zend_string_equals(bx->key, by->key)) return 0; }
            else if (bx->h != by->h) return 0;
            if (!zend_is_identical(&bx->val, &by->val)) return 0;
            i++; j++;
        }
        return 1;
    }
    default: return 0;
    }
}

/* ------------------------------------------------------------- arithmetic */
static void add_function_array(HashTable *a, HashTable *b, zval *result)
{
    HashTable *out = zend_hash_dup(a);
    ZEND_HASH_FOREACH(b, bb) {
        if (bb->key) {
            if (!zend_hash_find(out, bb->key)) zend_hash_update(out, bb->key, zval_copy(bb->val));
        } else {
            if (!zend_hash_index_find(out, (zend_long)bb->h))
                zend_hash_index_update(out, (zend_long)bb->h, zval_copy(bb->val));
        }
    } ZEND_HASH_FOREACH_END();
    *result = zval_array(out);
}

static int arith_operand(const zval *v, zval *out, zend_binop op, int argn)
{
    const zval *z = zval_deref((zval *)v);
    switch (z->u1) {
    case IS_UNDEF: case IS_NULL: *out = zval_long(0); return 0;
    case IS_FALSE: *out = zval_long(0); return 0;
    case IS_TRUE:  *out = zval_long(1); return 0;
    case IS_LONG: case IS_DOUBLE: *out = *z; return 0;
    case IS_STRING: {
        zend_long l = 0; double d = 0;
        int t = is_numeric_string(z->value.str->val, z->value.str->len, &l, &d, 0);
        if (t == IS_LONG) { *out = zval_long(l); return 0; }
        if (t == IS_DOUBLE) { *out = zval_double(d); return 0; }
        t = is_numeric_string(z->value.str->val, z->value.str->len, &l, &d, 1);
        if (t) {
            /* leading-numeric string: a notice before PHP 8, a warning after.
             * VERSION: 7.x "A non well formed numeric value encountered" ->
             * 8.0 "A non-numeric value" TypeError for fully non-numeric. */
            /* VERSIONGATE:80000 */
            zend_error(E_NOTICE,
                       /* VERSIONGATE:80000 */
                       "A non well formed numeric value encountered");
            *out = t == IS_LONG ? zval_long(l) : zval_double(d);
            return 0;
        }

        zend_error(E_WARNING, "A non-numeric value encountered");
        *out = zval_long(0);
        return 0;
    }
    default:
        (void)argn;
        return 2;    /* caller reports "Unsupported operand types" */
    }
}

static void long_add(zend_long a, zend_long b, zval *r)
{
    zend_long res;
    if (__builtin_add_overflow(a, b, &res)) *r = zval_double((double)a + (double)b);
    else *r = zval_long(res);
}
static void long_sub(zend_long a, zend_long b, zval *r)
{
    zend_long res;
    if (__builtin_sub_overflow(a, b, &res)) *r = zval_double((double)a - (double)b);
    else *r = zval_long(res);
}
static void long_mul(zend_long a, zend_long b, zval *r)
{
    zend_long res;
    if (__builtin_mul_overflow(a, b, &res)) *r = zval_double((double)a * (double)b);
    else *r = zval_long(res);
}

static int pow_op(const zval *a, const zval *b, zval *r)
{
    if (a->u1 == IS_LONG && b->u1 == IS_LONG && b->value.lval >= 0) {
        zend_long base = a->value.lval, e = b->value.lval, acc = 1;
        int overflow = 0;
        while (e) {
            if (e & 1) { if (__builtin_mul_overflow(acc, base, &acc)) { overflow = 1; break; } }
            e >>= 1;
            if (e) { if (__builtin_mul_overflow(base, base, &base)) { overflow = 1; break; } }
        }
        if (!overflow) { *r = zval_long(acc); return 0; }
        *r = zval_double(pow((double)a->value.lval, (double)b->value.lval));
        return 0;
    }
    *r = zval_double(pow(zval_get_double(a), zval_get_double(b)));
    return 0;
}

int zend_binary_op(zend_binop op, const zval *a0, const zval *b0, zval *result)
{
    zval *a = zval_deref((zval *)a0);
    zval *b = zval_deref((zval *)b0);

    switch (op) {
    case BIN_CONCAT: {
        zend_string *x = zval_get_string(a);
        if (EG(exception)) return 1;
        zend_string *y = zval_get_string(b);
        if (EG(exception)) return 1;
        *result = zval_zstr(zend_string_concat(x, y));
        return 0;
    }
    case BIN_IDENTICAL:     *result = zval_bool(zend_is_identical(a, b)); return 0;
    case BIN_NOT_IDENTICAL: *result = zval_bool(!zend_is_identical(a, b)); return 0;
    case BIN_EQUAL: case BIN_NOT_EQUAL: case BIN_SMALLER: case BIN_SMALLER_OR_EQUAL:
    case BIN_GREATER: case BIN_GREATER_OR_EQUAL: case BIN_SPACESHIP: {
        int r = 0;
        if (zend_compare(a, b, &r)) return 1;
        switch (op) {
        case BIN_EQUAL:             *result = zval_bool(r == 0); break;
        case BIN_NOT_EQUAL:         *result = zval_bool(r != 0); break;
        case BIN_SMALLER:           *result = zval_bool(r < 0); break;
        case BIN_SMALLER_OR_EQUAL:  *result = zval_bool(r <= 0); break;
        case BIN_GREATER:           *result = zval_bool(r > 0); break;
        case BIN_GREATER_OR_EQUAL:  *result = zval_bool(r >= 0); break;
        default:                    *result = zval_long(r < 0 ? -1 : (r > 0 ? 1 : 0)); break;
        }
        return 0;
    }
    case BIN_BOOL_XOR: *result = zval_bool(zval_is_true(a) != zval_is_true(b)); return 0;
    default: break;
    }

    if (op == BIN_ADD && a->u1 == IS_ARRAY && b->u1 == IS_ARRAY) {
        add_function_array(a->value.arr, b->value.arr, result);
        return 0;
    }

    if (op == BIN_SL || op == BIN_SR || op == BIN_BOR || op == BIN_BAND || op == BIN_BXOR) {
        if (a->u1 == IS_STRING && b->u1 == IS_STRING &&
            (op == BIN_BOR || op == BIN_BAND || op == BIN_BXOR)) {
            zend_string *x = a->value.str, *y = b->value.str;
            size_t n = op == BIN_BOR ? (x->len > y->len ? x->len : y->len)
                                     : (x->len < y->len ? x->len : y->len);
            zend_string *o = zend_string_alloc(n);
            for (size_t i = 0; i < n; i++) {
                unsigned char cx = i < x->len ? (unsigned char)x->val[i] : 0;
                unsigned char cy = i < y->len ? (unsigned char)y->val[i] : 0;
                o->val[i] = (char)(op == BIN_BOR ? (cx | cy) : op == BIN_BAND ? (cx & cy) : (cx ^ cy));
            }
            *result = zval_zstr(o);
            return 0;
        }
        zend_long x = zval_get_long(a), y = zval_get_long(b);
        if (EG(exception)) return 1;
        switch (op) {
        case BIN_SL:
            if (y < 0) { zend_throw_error("ArithmeticError", "Bit shift by negative number"); return 1; }
            *result = zval_long(y >= 64 ? 0 : (zend_long)((zend_ulong)x << y));
            break;
        case BIN_SR:
            if (y < 0) { zend_throw_error("ArithmeticError", "Bit shift by negative number"); return 1; }
            *result = zval_long(y >= 64 ? (x < 0 ? -1 : 0) : (x >> y));
            break;
        case BIN_BOR:  *result = zval_long(x | y); break;
        case BIN_BAND: *result = zval_long(x & y); break;
        default:       *result = zval_long(x ^ y); break;
        }
        return 0;
    }

    zval na, nb;
    int ra = arith_operand(a, &na, op, 1);
    if (ra == 1) return 1;
    int rb = ra == 2 ? 2 : arith_operand(b, &nb, op, 2);
    if (rb == 1) return 1;
    if (ra == 2 || rb == 2) {
        const char *sym = op == BIN_ADD ? "+" : op == BIN_SUB ? "-" : op == BIN_MUL ? "*"
                        : op == BIN_DIV ? "/" : op == BIN_MOD ? "%" : "**";

        zend_fatal("Unsupported operand types");
        return 1;
    }

    switch (op) {
    case BIN_ADD:
        if (na.u1 == IS_LONG && nb.u1 == IS_LONG) long_add(na.value.lval, nb.value.lval, result);
        else *result = zval_double(zval_get_double(&na) + zval_get_double(&nb));
        return 0;
    case BIN_SUB:
        if (na.u1 == IS_LONG && nb.u1 == IS_LONG) long_sub(na.value.lval, nb.value.lval, result);
        else *result = zval_double(zval_get_double(&na) - zval_get_double(&nb));
        return 0;
    case BIN_MUL:
        if (na.u1 == IS_LONG && nb.u1 == IS_LONG) long_mul(na.value.lval, nb.value.lval, result);
        else *result = zval_double(zval_get_double(&na) * zval_get_double(&nb));
        return 0;
    case BIN_DIV: {
        if (na.u1 == IS_LONG && nb.u1 == IS_LONG) {
            if (nb.value.lval == 0) {

                zend_error(E_WARNING, "Division by zero");
                *result = zval_bool(0);
                return 0;
            }
            if (na.value.lval % nb.value.lval == 0) {
                if (na.value.lval == ZEND_LONG_MIN && nb.value.lval == -1)
                    *result = zval_double(-(double)ZEND_LONG_MIN);
                else
                    *result = zval_long(na.value.lval / nb.value.lval);
            } else {
                *result = zval_double((double)na.value.lval / (double)nb.value.lval);
            }
            return 0;
        }
        double y = zval_get_double(&nb);
        if (y == 0.0) {

            zend_error(E_WARNING, "Division by zero");
            *result = zval_bool(0);
            return 0;
        }
        *result = zval_double(zval_get_double(&na) / y);
        return 0;
    }
    case BIN_MOD: {
        zend_long x = zval_get_long(&na), y = zval_get_long(&nb);
        if (y == 0) {


            zend_error(E_WARNING, "Division by zero");
            *result = zval_bool(0);
            return 0;
        }
        if (y == -1) { *result = zval_long(0); return 0; }
        *result = zval_long(x % y);
        return 0;
    }
    case BIN_POW: return pow_op(&na, &nb, result);
    default: *result = zval_null(); return 0;
    }
}

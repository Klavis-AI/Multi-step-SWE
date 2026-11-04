/* ext/standard/php_type.c -- type inspection and conversion builtins.
 *
 * Mirrors php-src's ext/standard/type.c.  Behaviour that changes after 5.6 is
 * selected at run time from EG(php_version) and marked with a VERSIONGATE
 * comment so a 5.6 snapshot can be produced mechanically.
 */

#include "zend.h"
#include "zend_ast.h"
#include "zend_API.h"

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ utils */

static int type_ci_equals(const zend_string *s, const char *lit)
{
    size_t i, n = strlen(lit);

    if (ZSTR_LEN(s) != n) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (tolower((unsigned char) ZSTR_VAL(s)[i]) != tolower((unsigned char) lit[i])) {
            return 0;
        }
    }
    return 1;
}

/* php-src performs the arity check inside the function (in ZPP), so a wrong
 * count is a warning plus a NULL return rather than an aborted call. */
static int type_arity(uint32_t argc, uint32_t min, uint32_t max, const char *fname)
{
    if (argc < min || (max != (uint32_t) -1 && argc > max)) {
        zend_wrong_param_count(fname, argc, min, max);
        return 0;
    }
    return 1;
}

/* The gettype()-style spelling php-src names an offending value with. */
static const char *type_word_given(const zval *v)
{

    switch (Z_TYPE_P(v)) {
    case IS_UNDEF: case IS_NULL:  return "null";
    case IS_FALSE: case IS_TRUE:  return "boolean";
    case IS_LONG:                 return "integer";
    case IS_DOUBLE:               return "double";
    case IS_STRING:               return "string";
    case IS_ARRAY:                return "array";
    case IS_OBJECT:               return "object";
    default:                      return "unknown";
    }
}

/* php-src 5.6's is_numeric_string_ex() also accepted a hexadecimal spelling;
 * PHP 7.0 removed it (RFC "Remove hex support in numeric strings"), which is
 * why is_numeric("0x1A") is true on 5.6 and false from 7.0.  The engine's
 * shared is_numeric_string() in Zend/zend_operators.c carries no such branch,
 * so the 5.6 ZPP path recognises the spelling here.
 *
 * Grammar, exactly as 5.6 had it: optional leading whitespace, "0x" or "0X",
 * one or more hex digits, and no sign ("-0x10" is not numeric).  Returns 0,
 * IS_LONG or IS_DOUBLE; *trailing is set when the string did not end there. */
static int type_hex_numeric_string(const zend_string *s, zend_long *lv,
                                    double *dv, int *trailing)
{
    const char *p = ZSTR_VAL(s);
    const char *end = p + ZSTR_LEN(s);
    const char *digits;
    unsigned long long acc = 0;
    double dacc = 0.0;
    int overflow = 0;

    /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
    if (EG(php_version) >= 70000) {
        return 0;
    }
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' ||
                       *p == '\r' || *p == '\v' || *p == '\f')) {
        p++;
    }
    if (end - p < 3 || p[0] != '0' || (p[1] != 'x' && p[1] != 'X')) {
        return 0;
    }
    p += 2;
    digits = p;
    while (p < end) {
        int d;

        if (*p >= '0' && *p <= '9')      d = *p - '0';
        else if (*p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
        else break;
        if (!overflow) {
            if (acc > (0xFFFFFFFFFFFFFFFFULL - (unsigned long long) d) / 16ULL) {
                overflow = 1;
                dacc = (double) acc;
            } else {
                acc = acc * 16ULL + (unsigned long long) d;
            }
        }
        if (overflow) {
            dacc = dacc * 16.0 + (double) d;
        }
        p++;
    }
    if (p == digits) {
        return 0;                       /* "0x" with no digits */
    }
    if (trailing) {
        *trailing = (p != end);
    }
    if (overflow || acc > (unsigned long long) ZEND_LONG_MAX) {
        if (dv) *dv = overflow ? dacc : (double) acc;
        return IS_DOUBLE;
    }
    if (lv) *lv = (zend_long) acc;
    return IS_LONG;
}

/* php-src ZPP "s": scalars convert, an object needs __toString, an array is
 * rejected with a warning (a TypeError from 8.0). */
static int type_arg_string(zval *in, zend_string **out, int argnum,
                           const char *fname, const char *pname)
{
    zval *v = zval_deref(in);

    switch (Z_TYPE_P(v)) {
    case IS_STRING: case IS_LONG: case IS_DOUBLE:
    case IS_UNDEF: case IS_NULL: case IS_FALSE: case IS_TRUE:
        *out = zval_get_string(v);
        return 1;
    case IS_OBJECT:
        if (Z_OBJ_P(v)->ce != NULL && Z_OBJ_P(v)->ce->__tostring != NULL) {
            *out = zval_get_string(v);
            return 1;
        }
        break;
    default:
        break;
    }

        zend_error(E_WARNING, "%s() expects parameter %d to be string, %s given",
                   fname, argnum, type_word_given(v));

    return 0;
}

static zend_class_entry *type_lookup_ce(const char *name, int autoload)
{
    zend_string *n = zend_string_initz(name);

    return zend_lookup_class(n, autoload);
}

/* ------------------------------------------------------------ gettype etc. */

static void zif_gettype(zval *ret, uint32_t argc, zval *argv)
{
    if (!type_arity(argc, 1, 1, "gettype")) return;
    /* zend_get_type_name() is documented as the gettype() spelling, i.e.
     * "boolean"/"integer"/"double"/"NULL"/... not the 8.0 short names. */
    ZVAL_STR(ret, zend_string_initz(zend_get_type_name(zval_deref(&argv[0]))));
}



/* php-src convert_to_object(): stdClass wrapping. */
static void type_convert_to_object(zval *v)
{
    zend_class_entry *ce;
    zend_object *obj;

    if (Z_TYPE_P(v) == IS_OBJECT) {
        return;
    }
    ce = type_lookup_ce("stdClass", 0);
    if (!ce) {
        return;
    }
    obj = zend_object_new(ce);
    if (!obj) {
        return;
    }
    if (Z_TYPE_P(v) == IS_ARRAY) {
        HashTable *src = Z_ARR_P(v);

        ZEND_HASH_FOREACH(src, _b) {
            if (_b->key) {
                zend_hash_update(obj->properties, _b->key, zval_copy(_b->val));
            } else {
                zend_string *k = php_long_to_string((zend_long) _b->h);

                zend_hash_update(obj->properties, k, zval_copy(_b->val));
            }
        } ZEND_HASH_FOREACH_END();
    } else if (Z_TYPE_P(v) != IS_NULL) {
        zend_hash_str_update(obj->properties, "scalar", 6, zval_copy(*v));
    }
    ZVAL_OBJ(v, obj);
}

static void zif_settype(zval *ret, uint32_t argc, zval *argv)
{
    zval *var;
    zend_string *type;

    if (!type_arity(argc, 2, 2, "settype")) return;
    if (!type_arg_string(&argv[1], &type, 2, "settype", "type")) return;
    if (Z_TYPE_P(&argv[0]) != IS_REFERENCE) {
        /* the engine passes by-reference parameters as IS_REFERENCE */
        ZVAL_BOOL(ret, 0);
        return;
    }
    var = &Z_REF_P(&argv[0])->val;

    if (type_ci_equals(type, "integer") || type_ci_equals(type, "int")) {
        convert_to_long(var);
    } else if (type_ci_equals(type, "float") || type_ci_equals(type, "double")) {
        convert_to_double(var);
    } else if (type_ci_equals(type, "string")) {
        convert_to_string(var);
    } else if (type_ci_equals(type, "array")) {
        convert_to_array(var);
    } else if (type_ci_equals(type, "object")) {
        type_convert_to_object(var);
    } else if (type_ci_equals(type, "bool") || type_ci_equals(type, "boolean")) {
        convert_to_boolean(var);
    } else if (type_ci_equals(type, "null")) {
        ZVAL_NULL(var);
    } else if (type_ci_equals(type, "resource")) {
        /* 8.0 turned both failure modes into ValueErrors. */

            zend_error(E_WARNING, "settype(): Cannot convert to resource type");
            ZVAL_BOOL(ret, 0);

        return;
    } else {

            zend_error(E_WARNING, "settype(): Invalid type");
            ZVAL_BOOL(ret, 0);

        return;
    }
    ZVAL_BOOL(ret, 1);
}

/* ------------------------------------------------------------- conversions */

/* php-src's ZEND_STRTOL-based intval($v, $base) path. */
static zend_long type_strtol_base(const zend_string *s, int base)
{
    char buf[128];
    char *heap = NULL;
    char *p;
    zend_long r;

    if (ZSTR_LEN(s) < sizeof(buf)) {
        memcpy(buf, ZSTR_VAL(s), ZSTR_LEN(s));
        buf[ZSTR_LEN(s)] = '\0';
        p = buf;
    } else {
        heap = malloc(ZSTR_LEN(s) + 1);
        if (!heap) {
            return 0;
        }
        memcpy(heap, ZSTR_VAL(s), ZSTR_LEN(s));
        heap[ZSTR_LEN(s)] = '\0';
        p = heap;
    }
    r = (zend_long) strtoll(p, NULL, base);
    free(heap);
    return r;
}

static void zif_intval(zval *ret, uint32_t argc, zval *argv)
{
    zval *num;
    zend_long base = 10;

    if (!type_arity(argc, 1, 2, "intval")) return;
    num = zval_deref(&argv[0]);
    if (argc >= 2) {
        base = zval_get_long(zval_deref(&argv[1]));
    }

    if (Z_TYPE_P(num) != IS_STRING || base == 10) {
        ZVAL_LONG(ret, zval_get_long(num));
        return;
    }

    /* 7.1 taught intval() to understand a "0b" prefix for bases 0 and 2; 5.6
     * and 7.0 hand the string straight to strtol(), so intval("0b101", 0) is 0
     * (verified against oracle/php7.0 and oracle/php7.1). */
    /* VERSIONGATE:70100 else-branch is the 5.6 behaviour */
    if (EG(php_version) >= 70100) {
        if (base == 0 || base == 2) {
            const char *strval = ZSTR_VAL(Z_STR_P(num));
            size_t len = ZSTR_LEN(Z_STR_P(num));

            while (len && isspace((unsigned char) *strval)) {
                strval++;
                len--;
            }
            /* a length of 3+ covers "0b#" and "-0b" (which results in 0) */
            if (len > 2) {
                size_t offset = 0;

                if (strval[0] == '-' || strval[0] == '+') {
                    offset = 1;
                }
                if (strval[offset] == '0' &&
                    (strval[offset + 1] == 'b' || strval[offset + 1] == 'B')) {
                    zend_string *tmp;
                    char *dst;

                    len -= 2;                 /* removing "0b" */
                    tmp = zend_string_alloc(len);
                    dst = ZSTR_VAL(tmp);
                    if (offset) {
                        dst[0] = strval[0];
                    }
                    memcpy(dst + offset, strval + offset + 2, len - offset);
                    dst[len] = '\0';
                    ZVAL_LONG(ret, type_strtol_base(tmp, 2));
                    return;
                }
            }
        }
    } else {
        /* 5.6/7.0: convert_to_long_base() is a bare strtol(), no prefix logic */
    }

    ZVAL_LONG(ret, type_strtol_base(Z_STR_P(num), (int) base));
}

static void zif_floatval(zval *ret, uint32_t argc, zval *argv)
{
    /* floatval() and doubleval() report under their own names */
    if (!type_arity(argc, 1, 1, "floatval")) return;
    ZVAL_DOUBLE(ret, zval_get_double(zval_deref(&argv[0])));
}

static void zif_doubleval(zval *ret, uint32_t argc, zval *argv)
{
    if (!type_arity(argc, 1, 1, "doubleval")) return;
    ZVAL_DOUBLE(ret, zval_get_double(zval_deref(&argv[0])));
}

static void zif_strval(zval *ret, uint32_t argc, zval *argv)
{
    if (!type_arity(argc, 1, 1, "strval")) return;
    ZVAL_STR(ret, zval_get_string(zval_deref(&argv[0])));
}

static void zif_boolval(zval *ret, uint32_t argc, zval *argv)
{
    if (!type_arity(argc, 1, 1, "boolval")) return;
    ZVAL_BOOL(ret, zval_is_true(zval_deref(&argv[0])));
}

/* --------------------------------------------------------------- is_*() */

#define TYPE_IS_FN(cname, phpname, test)                                     \
    static void zif_##cname(zval *ret, uint32_t argc, zval *argv)            \
    {                                                                        \
        zval *v;                                                             \
        if (!type_arity(argc, 1, 1, phpname)) return;                        \
        v = zval_deref(&argv[0]);                                            \
        ZVAL_BOOL(ret, (test));                                              \
    }

/* php-src registers is_int/is_integer/is_long as separate entries, each
 * reporting under its own name. */
TYPE_IS_FN(is_int,     "is_int",     Z_TYPE_P(v) == IS_LONG)
TYPE_IS_FN(is_integer, "is_integer", Z_TYPE_P(v) == IS_LONG)
TYPE_IS_FN(is_long,    "is_long",    Z_TYPE_P(v) == IS_LONG)
TYPE_IS_FN(is_float,   "is_float",   Z_TYPE_P(v) == IS_DOUBLE)
TYPE_IS_FN(is_double,  "is_double",  Z_TYPE_P(v) == IS_DOUBLE)
TYPE_IS_FN(is_string,  "is_string",  Z_TYPE_P(v) == IS_STRING)
TYPE_IS_FN(is_bool,    "is_bool",    Z_TYPE_P(v) == IS_TRUE || Z_TYPE_P(v) == IS_FALSE)
TYPE_IS_FN(is_array,   "is_array",   Z_TYPE_P(v) == IS_ARRAY)
TYPE_IS_FN(is_object,  "is_object",  Z_TYPE_P(v) == IS_OBJECT)
TYPE_IS_FN(is_null,    "is_null",    Z_TYPE_P(v) == IS_NULL)
TYPE_IS_FN(is_scalar,  "is_scalar",  Z_TYPE_P(v) == IS_FALSE || Z_TYPE_P(v) == IS_TRUE ||
                       Z_TYPE_P(v) == IS_LONG  || Z_TYPE_P(v) == IS_DOUBLE ||
                       Z_TYPE_P(v) == IS_STRING)

static void zif_is_numeric(zval *ret, uint32_t argc, zval *argv)
{
    zval *v;

    if (!type_arity(argc, 1, 1, "is_numeric")) return;
    v = zval_deref(&argv[0]);
    switch (Z_TYPE_P(v)) {
    case IS_LONG:
    case IS_DOUBLE:
        ZVAL_BOOL(ret, 1);
        break;
    case IS_STRING:
        /* The engine's is_numeric_string() owns the version differences here:
         * hex strings stop being numeric in 7.0 and trailing whitespace starts
         * being allowed in 8.0. */
        {
            int t = is_numeric_string(ZSTR_VAL(Z_STR_P(v)), ZSTR_LEN(Z_STR_P(v)),
                                      NULL, NULL, 0);
            int trailing = 0;

            if (t == 0 &&
                type_hex_numeric_string(Z_STR_P(v), NULL, NULL, &trailing) != 0 &&
                !trailing) {
                t = 1;
            }
            ZVAL_BOOL(ret, t != 0);
        }
        break;
    default:
        ZVAL_BOOL(ret, 0);
        break;
    }
}

/* VERSIONGATE:70100 */
static void zif_is_iterable(zval *ret, uint32_t argc, zval *argv)
{
    zval *v;

    if (!type_arity(argc, 1, 1, "is_iterable")) return;
    v = zval_deref(&argv[0]);
    if (Z_TYPE_P(v) == IS_ARRAY) {
        ZVAL_BOOL(ret, 1);
        return;
    }
    if (Z_TYPE_P(v) == IS_OBJECT) {
        zend_class_entry *tr = type_lookup_ce("Traversable", 0);

        ZVAL_BOOL(ret, tr && Z_OBJ_P(v)->ce && zend_instanceof(Z_OBJ_P(v)->ce, tr));
        return;
    }
    ZVAL_BOOL(ret, 0);
}



/* The class scope the caller is executing in (php-src zend_get_executed_scope);
 * an internal function runs in its caller's frame. */
static zend_class_entry *type_calling_scope(void)
{
    if (EG(current) && EG(current)->func) {
        return EG(current)->func->scope;
    }
    return NULL;
}

/* php-src zend_check_private()/zend_check_protected() for a resolved method. */
static int type_method_reachable(const zend_function *fn, zend_class_entry *caller)
{
    if (fn->flags & ZEND_ACC_PUBLIC) {
        return 1;
    }
    if (fn->flags & ZEND_ACC_PRIVATE) {
        return caller != NULL && caller == fn->scope;
    }
    if (fn->flags & ZEND_ACC_PROTECTED) {
        return caller != NULL && fn->scope != NULL &&
               (zend_instanceof(caller, fn->scope) || zend_instanceof(fn->scope, caller));
    }
    return 1;
}

static void zif_is_callable(zval *ret, uint32_t argc, zval *argv)
{
    zval *v;
    int syntax_only = 0;
    zend_function *fn = NULL;
    zend_object *obj = NULL;
    zend_class_entry *scope = NULL;
    zend_string *err = NULL;
    int callable;

    if (!type_arity(argc, 1, 3, "is_callable")) return;
    v = zval_deref(&argv[0]);
    if (argc >= 2) {
        syntax_only = zval_is_true(zval_deref(&argv[1]));
    }

    if (syntax_only) {
        /* shape check only, exactly like php-src's IS_CALLABLE_CHECK_SYNTAX_ONLY */
        switch (Z_TYPE_P(v)) {
        case IS_STRING:
            callable = 1;
            break;
        case IS_OBJECT:
            callable = 1;
            break;
        case IS_ARRAY: {
            HashTable *ht = Z_ARR_P(v);
            zval *o, *m;

            callable = 0;
            if (zend_hash_num_elements(ht) == 2 &&
                (o = zend_hash_index_find(ht, 0)) != NULL &&
                (m = zend_hash_index_find(ht, 1)) != NULL) {
                o = zval_deref(o);
                m = zval_deref(m);
                callable = (Z_TYPE_P(o) == IS_OBJECT || Z_TYPE_P(o) == IS_STRING) &&
                           Z_TYPE_P(m) == IS_STRING;
            }
            break;
        }
        default:
            callable = 0;
            break;
        }
    } else {
        /* zend_is_callable_zval() follows php-src's zend_is_callable():
         * non-zero means callable. */
        callable = zend_is_callable_zval(v, &fn, &obj, &scope, &err) != 0;

        /* A non-static method reached through a class-name callable has no
         * $this.  php-src let a userland one through (ZEND_ACC_ALLOW_STATIC)
         * but never an internal one, and 8.0 stopped allowing either:
         *   is_callable(array('foo','bar'))            true  < 8.0
         *   is_callable(array('Exception','getMessage')) false always */
        if (callable && fn != NULL && obj == NULL && fn->scope != NULL &&
            !(fn->flags & ZEND_ACC_STATIC)) {

                callable = !fn->is_internal;

        }

        /* php-src additionally checks that the resolved method is reachable
         * from the calling scope; a private/protected method is only callable
         * through __call() (object form) or __callStatic() (static form). */
        if (callable && fn != NULL && !(fn->flags & ZEND_ACC_PUBLIC) &&
            fn->scope != NULL) {
            zend_class_entry *caller = type_calling_scope();

            if (!type_method_reachable(fn, caller)) {
                zend_class_entry *ce = obj ? obj->ce : (scope ? scope : fn->scope);

                callable = ce != NULL &&
                           (obj != NULL ? ce->__call != NULL : ce->__callstatic != NULL);
            }
        }
    }

    /* optional by-reference $callable_name */
    if (argc >= 3 && Z_TYPE_P(&argv[2]) == IS_REFERENCE) {
        zval *slot = &Z_REF_P(&argv[2])->val;
        zend_string *name = NULL;

        if (Z_TYPE_P(v) == IS_STRING) {
            name = zend_string_init(ZSTR_VAL(Z_STR_P(v)), ZSTR_LEN(Z_STR_P(v)));
        } else if (Z_TYPE_P(v) == IS_ARRAY) {
            zval *o = zend_hash_index_find(Z_ARR_P(v), 0);
            zval *m = zend_hash_index_find(Z_ARR_P(v), 1);

            if (o && m) {
                zend_string *cn = NULL;
                zend_string *mn;

                o = zval_deref(o);
                m = zval_deref(m);
                if (Z_TYPE_P(o) == IS_OBJECT && Z_OBJ_P(o)->ce) {
                    cn = Z_OBJ_P(o)->ce->name;
                } else if (Z_TYPE_P(o) == IS_STRING) {
                    cn = Z_STR_P(o);
                }
                mn = zval_get_string(m);
                if (cn && mn) {
                    zend_string *sep = zend_string_initz("::");
                    zend_string *a = zend_string_concat(cn, sep);

                    name = zend_string_concat(a, mn);
                }
            }
        } else if (Z_TYPE_P(v) == IS_OBJECT && Z_OBJ_P(v)->ce) {
            zend_string *sep = zend_string_initz("::__invoke");

            name = zend_string_concat(Z_OBJ_P(v)->ce->name, sep);
        }
        if (!name) {
            name = zend_string_initz("");
        }
        ZVAL_STR(slot, name);
    }

    ZVAL_BOOL(ret, callable);
}

/* ==================================================================== */

/* `min_args` is deliberately 0 on every row: like php-src, this module does
 * its own arity check inside each handler (type_arity()) so that a wrong
 * argument count yields the 5.6 warning *and* the NULL return value.  See the
 * matching note in php_math.c. */
const php_function_entry php_functions_type[] = {
    /* name             handler             min max ref_mask since  until */
    {"gettype",         zif_gettype,        0, 1, NULL,  0,     0},
    {"settype",         zif_settype,        0, 2, "10",  0,     0},
    {"intval",          zif_intval,         0, 2, NULL,  0,     0},
    {"floatval",        zif_floatval,       0, 1, NULL,  0,     0},
    {"doubleval",       zif_doubleval,      0, 1, NULL,  0,     0},
    {"strval",          zif_strval,         0, 1, NULL,  0,     0},
    {"boolval",         zif_boolval,        0, 1, NULL,  0,     0},
    {"is_int",          zif_is_int,         0, 1, NULL,  0,     0},
    {"is_integer",      zif_is_integer,     0, 1, NULL,  0,     0},
    {"is_long",         zif_is_long,        0, 1, NULL,  0,     0},
    {"is_float",        zif_is_float,       0, 1, NULL,  0,     0},
    {"is_double",       zif_is_double,      0, 1, NULL,  0,     0},
    {"is_string",       zif_is_string,      0, 1, NULL,  0,     0},
    {"is_bool",         zif_is_bool,        0, 1, NULL,  0,     0},
    {"is_array",        zif_is_array,       0, 1, NULL,  0,     0},
    {"is_object",       zif_is_object,      0, 1, NULL,  0,     0},
    {"is_null",         zif_is_null,        0, 1, NULL,  0,     0},
    {"is_numeric",      zif_is_numeric,     0, 1, NULL,  0,     0},
    {"is_scalar",       zif_is_scalar,      0, 1, NULL,  0,     0},
    {"is_callable",     zif_is_callable,    0, 3, "001", 0,     0},
    /* VERSIONGATE:70100 */
    {"is_iterable",     zif_is_iterable,    0, 1, NULL,  70100, 0},


    {NULL, NULL, 0, 0, NULL, 0, 0}
};

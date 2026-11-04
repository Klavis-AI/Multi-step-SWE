/* ext/standard/php_var.c -- var_dump() / print_r() / var_export().
 *
 * These three output formats are the single most heavily asserted thing in
 * php-src's .phpt suite, so this file follows ext/standard/var.c and
 * Zend/zend.c (print_hash / zend_print_zval_r_to_buf) statement for statement.
 * Every observable difference between 5.6 and 8.4 carries a VERSION marker and
 * is selected at run time from EG(php_version).
 *
 * Formats verified byte for byte against oracle/php{5.6,7.0,7.1,7.4,8.0,8.1,
 * 8.2,8.4} -- see the module report for the exact comparisons.
 */

#include "zend.h"
#include "zend_ast.h"
#include "zend_API.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* php-src's PRINT_ZVAL_INDENT */
#define PRINT_ZVAL_INDENT 4

/* ------------------------------------------------------------------ buffer */

typedef struct {
    char   *b;
    size_t  len;
    size_t  cap;
} vbuf;

static void vb_init(vbuf *v)
{
    v->b = NULL;
    v->len = 0;
    v->cap = 0;
}

static void vb_free(vbuf *v)
{
    free(v->b);
    v->b = NULL;
    v->len = v->cap = 0;
}

static void vb_need(vbuf *v, size_t extra)
{
    size_t want = v->len + extra + 1;
    char *nb;

    if (want <= v->cap) {
        return;
    }
    if (v->cap == 0) {
        v->cap = 256;
    }
    while (v->cap < want) {
        v->cap *= 2;
    }
    nb = realloc(v->b, v->cap);
    if (!nb) {
        /* out of memory: keep the buffer consistent and drop the append */
        return;
    }
    v->b = nb;
}

static void vb_append(vbuf *v, const char *s, size_t len)
{
    if (len == 0) {
        return;
    }
    vb_need(v, len);
    if (!v->b) {
        return;
    }
    memcpy(v->b + v->len, s, len);
    v->len += len;
    v->b[v->len] = '\0';
}

static void vb_puts(vbuf *v, const char *s)
{
    vb_append(v, s, strlen(s));
}

static void vb_putc(vbuf *v, char c)
{
    vb_append(v, &c, 1);
}

static void vb_zstr(vbuf *v, const zend_string *s)
{
    vb_append(v, ZSTR_VAL(s), ZSTR_LEN(s));
}

static void vb_long(vbuf *v, zend_long l)
{
    char tmp[32];

    snprintf(tmp, sizeof(tmp), "%lld", (long long) l);
    vb_puts(v, tmp);
}

static void vb_ulong(vbuf *v, zend_ulong l)
{
    char tmp[32];

    snprintf(tmp, sizeof(tmp), "%llu", (unsigned long long) l);
    vb_puts(v, tmp);
}

/* php-src indents with php_printf("%*c", n, ' '), which emits max(n, 1)
 * characters -- n-1 pad spaces plus the space itself. */
static void vb_spaces(vbuf *v, int n)
{
    int i;

    if (n < 1) {
        n = 1;
    }
    for (i = 0; i < n; i++) {
        vb_putc(v, ' ');
    }
}

static void vb_flush(vbuf *v)
{
    if (v->b && v->len) {
        php_write(v->b, v->len);
    }
    vb_free(v);
}

static zend_string *vb_take(vbuf *v)
{
    zend_string *s = zend_string_init(v->b ? v->b : "", v->len);

    vb_free(v);
    return s;
}

/* php-src performs the arity check inside the function (in ZPP), so a wrong
 * count is a warning plus a NULL return rather than an aborted call. */
static int var_arity(uint32_t argc, uint32_t min, uint32_t max, const char *fname)
{
    if (argc < min || (max != (uint32_t) -1 && argc > max)) {
        zend_wrong_param_count(fname, argc, min, max);
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------- ini */

static zend_long var_ini_long(const char *name, zend_long dflt)
{
    zval *v;

    if (!EG(ini)) {
        return dflt;
    }
    v = zend_hash_str_find(EG(ini), name, strlen(name));
    if (!v) {
        return dflt;
    }
    if (Z_TYPE_P(v) == IS_LONG) {
        return Z_LVAL_P(v);
    }
    if (Z_TYPE_P(v) == IS_STRING) {
        return (zend_long) strtoll(ZSTR_VAL(Z_STR_P(v)), NULL, 10);
    }
    return dflt;
}

static int var_precision(void)
{
    return (int) var_ini_long("precision", 14);
}

static int var_serialize_precision(void)
{
    zend_long dflt;

    /* The compiled-in default for serialize_precision is 17 up to 7.0 and -1
     * (shortest round-trip repr) from 7.1, which is why var_export(0.1) is
     * 0.10000000000000001 on 5.6/7.0 and 0.1 from 7.1. */
    /* VERSIONGATE:70100 else-branch is the 5.6 behaviour */
    if (EG(php_version) >= 70100) {
        dflt = -1;
    } else {
        dflt = 17;
    }
    return (int) var_ini_long("serialize_precision", dflt);
}

/* Format a double the way php-src's "%.*G" / "%.*H" does.
 *
 * ASSUMPTION: php_double_to_string()'s third parameter is the printf
 * conversion character, as in php-src's snprintf.c ('G' -> php_gcvt with
 * exp_char 'E').  Under the LC_ALL=C that run-tests forces, '%G' and '%H'
 * are identical, so 'G' covers both. */
static zend_string *var_double_repr(double d, int precision)
{
    if (precision == -1) {
        return smart_double_repr(d);
    }
    return php_double_to_string(d, precision, 'G');
}

/* php-src's ZEND_HANDLE_NUMERIC_STR: a canonical decimal integer spelling is
 * stored as a numeric key, never as a string one.  This engine keeps such keys
 * in an object's property table as strings, so the printers recognise them
 * here and render them the way php-src does -- (object) array(0 => 1) dumps as
 * [0]=>, not ["0"]=>.  "01", "1.0", " 1" and "-0" are not canonical and stay
 * strings, exactly as in php-src. */
static int var_key_is_numeric(const zend_string *key, zend_long *idx)
{
    const char *p = ZSTR_VAL(key);
    size_t len = ZSTR_LEN(key);
    size_t i = 0;
    int neg = 0;
    unsigned long long acc = 0;

    if (len == 0 || len > 20) {
        return 0;
    }
    if (p[0] == '-') {
        neg = 1;
        i = 1;
        if (len == 1) return 0;
    }
    if (p[i] == '0' && len - i > 1) {
        return 0;                       /* leading zero */
    }
    for (; i < len; i++) {
        if (p[i] < '0' || p[i] > '9') {
            return 0;
        }
        acc = acc * 10ULL + (unsigned long long) (p[i] - '0');
        if (acc > (unsigned long long) ZEND_LONG_MAX + (neg ? 1ULL : 0ULL)) {
            return 0;                   /* out of range */
        }
    }
    if (neg && acc == 0) {
        return 0;                       /* "-0" stays a string */
    }
    if (idx) {
        *idx = neg ? -(zend_long) acc : (zend_long) acc;
    }
    return 1;
}

/* ---------------------------------------------------------- name mangling */

/* php-src stores protected properties as "\0*\0name" and private ones as
 * "\0ClassName\0name"; zend_mangle_property_name() in this tree follows the
 * same convention.  Returns 1 when the key was mangled. */
static int var_unmangle(const zend_string *key, const char **class_name,
                        size_t *class_len, const char **prop, size_t *prop_len)
{
    const char *v = ZSTR_VAL(key);
    size_t len = ZSTR_LEN(key);
    size_t i;

    *class_name = NULL;
    *class_len = 0;
    *prop = v;
    *prop_len = len;

    if (len == 0 || v[0] != '\0') {
        return 0;
    }
    for (i = 1; i < len; i++) {
        if (v[i] == '\0') {
            *class_name = v + 1;
            *class_len = i - 1;
            *prop = v + i + 1;
            *prop_len = len - i - 1;
            return 1;
        }
    }
    return 0;
}

/* ---------------------------------------------------------------- objects */

/* php-src's zend_get_properties_for(.., ZEND_PROP_PURPOSE_DEBUG): honours
 * __debugInfo().  *is_temp is set when the caller owns the table. */
static HashTable *var_object_debug_props(zend_object *obj, int *is_temp, zval *tmp)
{
    *is_temp = 0;
    ZVAL_NULL(tmp);

    if (obj->ce && obj->ce->__debuginfo) {
        if (zend_call_function_ex(obj->ce->__debuginfo, obj, obj->ce,
                                  0, NULL, NULL, tmp, NULL) == 0 &&
            Z_TYPE_P(tmp) == IS_ARRAY) {
            *is_temp = 1;
            return Z_ARR_P(tmp);
        }
        ZVAL_NULL(tmp);
    }
    return obj->properties;
}

static zend_string *var_enum_case_name(zend_object *obj)
{
    zval *v;

    if (!obj->properties) {
        return NULL;
    }
    v = zend_hash_str_find(obj->properties, "name", 4);
    if (v && Z_TYPE_P(v) == IS_STRING) {
        return Z_STR_P(v);
    }
    return NULL;
}

/* Recursion guards.  Arrays use HashTable.protect the way php-src used
 * nApplyCount: a depth counter, not a flag.  Objects piggyback on their
 * property table.
 *
 * php-src's own rule is "report on the second entry".  The one wrinkle is the
 * by-value argument itself: up to 7.4 an is_ref array handed to an internal
 * function is separated first, so the formatter walks a *copy* of it and meets
 * the cyclic table one level deeper.  8.0 stopped separating.
 *
 *   $a = [1]; $a[0] = &$a; var_dump($a);
 *     5.6 .. 7.4  array(1) { [0]=> &array(1) { [0]=> *RECURSION* } }
 *     8.0+        array(1) { [0]=> *RECURSION* }
 *
 * Everything reached *through* the argument uses the plain rule, which is what
 * Zend/tests/bug35163.phpt pins down:
 *
 *   $a = array(array(1)); $a[0][] =& $a[0]; $a[0][] =& $a[0];
 *     &array(3) { [0]=> int(2) [1]=> *RECURSION* [2]=> *RECURSION* }
 *
 * Objects are never separated, so their property tables always use allowance 1
 * ($o->self = $o prints *RECURSION* at the first nesting in every release). */
static const HashTable *var_top_ht;   /* the argument's own table */

static unsigned var_ht_allowance(const HashTable *ht, int is_object)
{
    if (is_object || ht == NULL || ht != var_top_ht) {
        return 1;
    }

    return 2;
}

/* 1 = descend, 0 = report *RECURSION* */
static int var_ht_enter(HashTable *ht, int is_object)
{
    if (!ht) {
        return 1;
    }
    if ((unsigned) ht->protect >= var_ht_allowance(ht, is_object)) {
        return 0;
    }
    ht->protect++;
    return 1;
}

static void var_ht_leave(HashTable *ht)
{
    if (ht && ht->protect) {
        ht->protect--;
    }
}

/* Remember which table the by-value argument itself holds. */
static const HashTable *var_note_top(zval *v)
{
    const HashTable *prev = var_top_ht;
    zval *d = zval_deref(v);

    if (Z_TYPE_P(d) == IS_ARRAY) {
        var_top_ht = Z_ARR_P(d);
    } else if (Z_TYPE_P(d) == IS_OBJECT) {
        var_top_ht = Z_OBJ_P(d)->properties;
    } else {
        var_top_ht = NULL;
    }
    return prev;
}


/* ==================================================================== */
/*                             var_dump()                               */
/* ==================================================================== */

static void var_dump_worker(vbuf *b, zval *struc, int level);

static void var_dump_array_element(vbuf *b, zval *zv, zend_ulong index,
                                   const zend_string *key, int level)
{
    if (key == NULL) {                      /* numeric key */
        vb_spaces(b, level + 1);
        vb_putc(b, '[');
        vb_long(b, (zend_long) index);
        vb_puts(b, "]=>\n");
    } else {                                /* string key */
        vb_spaces(b, level + 1);
        vb_puts(b, "[\"");
        vb_zstr(b, key);
        vb_puts(b, "\"]=>\n");
    }
    var_dump_worker(b, zv, level + 2);
}

static void var_dump_property(vbuf *b, zval *zv, zend_ulong index,
                              const zend_string *key, int level)
{
    zend_long nidx;

    if (key != NULL && var_key_is_numeric(key, &nidx)) {
        key = NULL;
        index = (zend_ulong) nidx;
    }
    if (key == NULL) {
        vb_spaces(b, level + 1);
        vb_putc(b, '[');
        vb_long(b, (zend_long) index);
        vb_puts(b, "]=>\n");
    } else {
        const char *class_name, *prop;
        size_t class_len, prop_len;
        int mangled = var_unmangle(key, &class_name, &class_len, &prop, &prop_len);

        vb_spaces(b, level + 1);
        vb_putc(b, '[');
        if (mangled && class_name) {
            if (class_name[0] == '*') {
                vb_putc(b, '"');
                vb_append(b, prop, prop_len);
                vb_puts(b, "\":protected");
            } else {
                vb_putc(b, '"');
                vb_append(b, prop, prop_len);
                vb_puts(b, "\":\"");
                vb_append(b, class_name, class_len);
                vb_puts(b, "\":private");
            }
        } else {
            vb_putc(b, '"');
            vb_zstr(b, key);
            vb_putc(b, '"');
        }
        vb_puts(b, "]=>\n");
    }
    var_dump_worker(b, zv, level + 2);
}

static void var_dump_worker(vbuf *b, zval *struc, int level)
{
    int is_ref = 0;
    HashTable *myht;

    if (level > 1) {
        vb_spaces(b, level - 1);
    }

again:
    switch (Z_TYPE_P(struc)) {
    case IS_FALSE:
        if (is_ref) vb_putc(b, '&');
        vb_puts(b, "bool(false)\n");
        break;

    case IS_TRUE:
        if (is_ref) vb_putc(b, '&');
        vb_puts(b, "bool(true)\n");
        break;

    case IS_NULL:
        if (is_ref) vb_putc(b, '&');
        vb_puts(b, "NULL\n");
        break;

    case IS_LONG:
        if (is_ref) vb_putc(b, '&');
        vb_puts(b, "int(");
        vb_long(b, Z_LVAL_P(struc));
        vb_puts(b, ")\n");
        break;

    case IS_DOUBLE: {
        int prec;
        zend_string *s;

        /* var_dump() formats with EG(precision) ("%.*G", 14 significant
         * digits) up to 7.4 and with PG(serialize_precision) ("%.*H", -1 =
         * shortest round-trip) from 8.0 -- var_dump(0.1+0.2) is float(0.3)
         * through 7.4 and float(0.30000000000000004) from 8.0. */

            prec = var_precision();

        s = var_double_repr(Z_DVAL_P(struc), prec);

        if (is_ref) vb_putc(b, '&');
        vb_puts(b, "float(");
        if (s) vb_zstr(b, s);
        vb_puts(b, ")\n");
        break;
    }

    case IS_STRING:
        if (is_ref) vb_putc(b, '&');
        vb_puts(b, "string(");
        vb_ulong(b, (zend_ulong) ZSTR_LEN(Z_STR_P(struc)));
        vb_puts(b, ") \"");
        vb_zstr(b, Z_STR_P(struc));
        vb_puts(b, "\"\n");
        break;

    case IS_ARRAY:
        myht = Z_ARR_P(struc);
        if (!var_ht_enter(myht, 0)) {
            vb_puts(b, "*RECURSION*\n");
            return;
        }
        if (is_ref) vb_putc(b, '&');
        vb_puts(b, "array(");
        vb_long(b, (zend_long) zend_hash_num_elements(myht));
        vb_puts(b, ") {\n");
        ZEND_HASH_FOREACH(myht, _b) {
            var_dump_array_element(b, &_b->val, _b->h, _b->key, level);
        } ZEND_HASH_FOREACH_END();
        var_ht_leave(myht);
        if (level > 1) {
            vb_spaces(b, level - 1);
        }
        vb_puts(b, "}\n");
        break;

    case IS_OBJECT: {
        zend_object *obj = Z_OBJ_P(struc);
        int is_temp = 0;
        zval tmp;

        /* 8.1 dumps enum instances as enum(Name::Case). */

            /* 5.6: there are no enums, every object dumps the same way */


        myht = var_object_debug_props(obj, &is_temp, &tmp);
        if (!var_ht_enter(myht, 1)) {
            vb_puts(b, "*RECURSION*\n");
            return;
        }

        if (is_ref) vb_putc(b, '&');
        vb_puts(b, "object(");
        if (obj->ce) {
            vb_zstr(b, obj->ce->name);
        } else {
            vb_puts(b, "unknown class");
        }
        vb_puts(b, ")#");
        vb_long(b, (zend_long) obj->handle);
        vb_puts(b, " (");
        vb_long(b, (zend_long) (myht ? zend_hash_num_elements(myht) : 0));
        vb_puts(b, ") {\n");

        if (myht) {
            ZEND_HASH_FOREACH(myht, _b) {
                var_dump_property(b, &_b->val, _b->h, _b->key, level);
            } ZEND_HASH_FOREACH_END();
        }
        var_ht_leave(myht);
        if (level > 1) {
            vb_spaces(b, level - 1);
        }
        vb_puts(b, "}\n");
        break;
    }

    case IS_REFERENCE:
        /* php-src only marks a reference with "&" when it is actually shared
         * (Z_REFCOUNT_P(struc) > 1), so function f(&$x) { var_dump($x); }
         * prints no "&". */
        if (Z_REF_P(struc)->refcount > 1) {
            is_ref = 1;
        }
        struc = &Z_REF_P(struc)->val;
        goto again;

    default:
        if (is_ref) vb_putc(b, '&');
        vb_puts(b, "UNKNOWN:0\n");
        break;
    }
}

void php_var_dump(zval *v, int level)
{
    vbuf b;
    const HashTable *prev = var_note_top(v);

    vb_init(&b);
    var_dump_worker(&b, v, level);
    vb_flush(&b);
    var_top_ht = prev;
}

static void zif_var_dump(zval *ret, uint32_t argc, zval *argv)
{
    uint32_t i;

    (void) ret;
    if (!var_arity(argc, 1, (uint32_t) -1, "var_dump")) return;
    for (i = 0; i < argc; i++) {
        php_var_dump(&argv[i], 1);
    }
}

/* ==================================================================== */
/*                              print_r()                               */
/* ==================================================================== */

static void var_print_r_worker(vbuf *b, zval *expr, int indent);

static void var_print_hash(vbuf *b, HashTable *ht, int indent, int is_object)
{
    int i;

    for (i = 0; i < indent; i++) {
        vb_putc(b, ' ');
    }
    vb_puts(b, "(\n");
    indent += PRINT_ZVAL_INDENT;

    if (ht) {
        ZEND_HASH_FOREACH(ht, _b) {
            int j;

            for (j = 0; j < indent; j++) {
                vb_putc(b, ' ');
            }
            vb_putc(b, '[');
            if (_b->key) {
                if (is_object) {
                    const char *class_name, *prop;
                    size_t class_len, prop_len;
                    int mangled = var_unmangle(_b->key, &class_name, &class_len,
                                               &prop, &prop_len);

                    vb_append(b, prop, prop_len);
                    if (mangled && class_name) {
                        if (class_name[0] == '*') {
                            vb_puts(b, ":protected");
                        } else {
                            vb_putc(b, ':');
                            vb_append(b, class_name, class_len);
                            vb_puts(b, ":private");
                        }
                    }
                } else {
                    vb_zstr(b, _b->key);
                }
            } else {
                vb_long(b, (zend_long) _b->h);
            }
            vb_puts(b, "] => ");
            var_print_r_worker(b, &_b->val, indent + PRINT_ZVAL_INDENT);
            vb_puts(b, "\n");
        } ZEND_HASH_FOREACH_END();
    }

    indent -= PRINT_ZVAL_INDENT;
    for (i = 0; i < indent; i++) {
        vb_putc(b, ' ');
    }
    vb_puts(b, ")\n");
}

static void var_print_r_worker(vbuf *b, zval *expr, int indent)
{
    switch (Z_TYPE_P(expr)) {
    case IS_ARRAY: {
        HashTable *ht = Z_ARR_P(expr);

        vb_puts(b, "Array\n");
        if (!var_ht_enter(ht, 0)) {
            vb_puts(b, " *RECURSION*");
            return;
        }
        var_print_hash(b, ht, indent, 0);
        var_ht_leave(ht);
        break;
    }

    case IS_OBJECT: {
        zend_object *obj = Z_OBJ_P(expr);
        HashTable *myht;
        int is_temp = 0;
        zval tmp;

        if (obj->ce) {
            vb_zstr(b, obj->ce->name);
        } else {
            vb_puts(b, "Unknown Class");
        }
        /* 8.1 prints enum instances as "Name Enum[:backing]". */

            vb_puts(b, " Object\n");


        myht = var_object_debug_props(obj, &is_temp, &tmp);
        if (!var_ht_enter(myht, 1)) {
            vb_puts(b, " *RECURSION*");
            return;
        }
        if (!myht) {
            var_print_hash(b, NULL, indent, 1);
            break;
        }
        var_print_hash(b, myht, indent, 1);
        var_ht_leave(myht);
        break;
    }

    case IS_LONG:
        vb_long(b, Z_LVAL_P(expr));
        break;

    case IS_REFERENCE:
        var_print_r_worker(b, &Z_REF_P(expr)->val, indent);
        break;

    case IS_STRING:
        vb_zstr(b, Z_STR_P(expr));
        break;

    default: {
        /* NULL -> "", false -> "", true -> "1", float via EG(precision) */
        zend_string *s = zval_get_string(expr);

        if (s) {
            vb_zstr(b, s);
        }
        break;
    }
    }
}

/* `in_property` is accepted for the frozen prototype; php-src's
 * zend_print_zval_r() takes only an indent, and the property/array split is
 * decided inside print_hash(). */
void php_print_r(zval *v, int level, int in_property)
{
    vbuf b;

    const HashTable *prev = var_note_top(v);

    (void) in_property;
    vb_init(&b);
    var_print_r_worker(&b, v, level);
    vb_flush(&b);
    var_top_ht = prev;
}

static void zif_print_r(zval *ret, uint32_t argc, zval *argv)
{
    int do_return = 0;

    if (!var_arity(argc, 1, 2, "print_r")) return;
    if (argc >= 2) {
        do_return = zval_is_true(zval_deref(&argv[1]));
    }
    if (do_return) {
        vbuf b;
        const HashTable *prev = var_note_top(&argv[0]);

        vb_init(&b);
        var_print_r_worker(&b, zval_deref(&argv[0]), 0);
        ZVAL_STR(ret, vb_take(&b));
        var_top_ht = prev;
        return;
    }
    php_print_r(zval_deref(&argv[0]), 0, 0);
    ZVAL_BOOL(ret, 1);
}

/* ==================================================================== */
/*                            var_export()                              */
/* ==================================================================== */

static void var_export_worker(vbuf *b, zval *struc, int level);

/* php_addcslashes(s, "'\\", 2) followed by the NUL -> `' . "\0" . '` rewrite,
 * fused into one pass. */
static void var_export_string(vbuf *b, const char *s, size_t len)
{
    size_t i;

    vb_putc(b, '\'');
    for (i = 0; i < len; i++) {
        char c = s[i];

        if (c == '\'' || c == '\\') {
            vb_putc(b, '\\');
            vb_putc(b, c);
        } else if (c == '\0') {
            vb_puts(b, "' . \"\\0\" . '");
        } else {
            vb_putc(b, c);
        }
    }
    vb_putc(b, '\'');
}

static void var_export_array_element(vbuf *b, zval *zv, zend_ulong index,
                                     const zend_string *key, int level)
{
    vb_spaces(b, level + 1);
    if (key == NULL) {
        vb_long(b, (zend_long) index);
        vb_puts(b, " => ");
    } else {
        var_export_string(b, ZSTR_VAL(key), ZSTR_LEN(key));
        vb_puts(b, " => ");
    }
    var_export_worker(b, zv, level + 2);
    vb_puts(b, ",\n");
}

static void var_export_object_element(vbuf *b, zval *zv, zend_ulong index,
                                      const zend_string *key, int level)
{
    zend_long nidx;

    vb_spaces(b, level + 2);
    if (key != NULL && var_key_is_numeric(key, &nidx)) {
        vb_long(b, nidx);
    } else if (key != NULL) {
        const char *class_name, *prop;
        size_t class_len, prop_len;

        var_unmangle(key, &class_name, &class_len, &prop, &prop_len);
        var_export_string(b, prop, prop_len);
    } else {
        vb_long(b, (zend_long) index);
    }
    vb_puts(b, " => ");
    var_export_worker(b, zv, level + 2);
    vb_puts(b, ",\n");
}

static int var_ce_is_stdclass(const zend_class_entry *ce)
{
    if (!ce) {
        return 0;
    }
    if (ce->lcname) {
        return ZSTR_LEN(ce->lcname) == 8 &&
               memcmp(ZSTR_VAL(ce->lcname), "stdclass", 8) == 0;
    }
    return ce->name && ZSTR_LEN(ce->name) == 8 &&
           memcmp(ZSTR_VAL(ce->name), "stdClass", 8) == 0;
}

static void var_export_worker(vbuf *b, zval *struc, int level)
{
    HashTable *myht;

again:
    switch (Z_TYPE_P(struc)) {
    case IS_FALSE:
        vb_puts(b, "false");
        break;

    case IS_TRUE:
        vb_puts(b, "true");
        break;

    case IS_NULL:
        vb_puts(b, "NULL");
        break;

    case IS_LONG:
        /* 7.2 started emitting PHP_INT_MIN as "-9223372036854775807-1" because
         * the bare literal would otherwise be parsed as a float. */

            /* 5.6: PHP_INT_MIN is printed as a plain literal */

        vb_long(b, Z_LVAL_P(struc));
        break;

    case IS_DOUBLE: {
        /* var_export() always uses PG(serialize_precision); the 17 vs -1
         * default split is handled in var_serialize_precision(). */
        zend_string *s = var_double_repr(Z_DVAL_P(struc), var_serialize_precision());

        if (s) {
            vb_zstr(b, s);
            /* 7.0 appends ".0" so the literal stays a float; 5.6 printed
             * var_export(1.0) as "1". */
            /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
            if (EG(php_version) >= 70000) {
                if (isfinite(Z_DVAL_P(struc)) &&
                    memchr(ZSTR_VAL(s), '.', ZSTR_LEN(s)) == NULL) {
                    vb_puts(b, ".0");
                }
            } else {
                /* 5.6: no ".0" suffix */
            }
        }
        break;
    }

    case IS_STRING:
        var_export_string(b, ZSTR_VAL(Z_STR_P(struc)), ZSTR_LEN(Z_STR_P(struc)));
        break;

    case IS_ARRAY:
        myht = Z_ARR_P(struc);
        if (!var_ht_enter(myht, 0)) {
            vb_puts(b, "NULL");
            zend_error(E_WARNING, "var_export does not handle circular references");
            return;
        }
        if (level > 1) {
            vb_putc(b, '\n');
            vb_spaces(b, level - 1);
        }
        vb_puts(b, "array (\n");
        ZEND_HASH_FOREACH(myht, _b) {
            var_export_array_element(b, &_b->val, _b->h, _b->key, level);
        } ZEND_HASH_FOREACH_END();
        var_ht_leave(myht);
        if (level > 1) {
            vb_spaces(b, level - 1);
        }
        vb_putc(b, ')');
        break;

    case IS_OBJECT: {
        zend_object *obj = Z_OBJ_P(struc);
        int is_enum;
        int std_object_syntax;

        /* 8.1 exports enum instances as \Name::Case instead of
         * Name::__set_state(array(...)). */

            is_enum = 0;                   /* 5.6: there are no enums */


        /* 7.3 exports stdClass as "(object) array(" instead of
         * "stdClass::__set_state(array(". */

            std_object_syntax = 0;


        myht = obj->properties;   /* var_export ignores __debugInfo() */
        if (!var_ht_enter(myht, 1)) {
            vb_puts(b, "NULL");
            zend_error(E_WARNING, "var_export does not handle circular references");
            return;
        }

        if (level > 1) {
            vb_putc(b, '\n');
            vb_spaces(b, level - 1);
        }

        if (std_object_syntax) {
            vb_puts(b, "(object) array(\n");
        } else {
            /* 8.2 prefixes the class name with a namespace-root backslash:
             * \Foo::__set_state(array( */

                /* 5.6 .. 8.1: the bare class name */

            if (obj->ce) {
                vb_zstr(b, obj->ce->name);
            }
            if (is_enum) {
                zend_string *cn = var_enum_case_name(obj);

                vb_puts(b, "::");
                if (cn) vb_zstr(b, cn);
            } else {
                vb_puts(b, "::__set_state(array(\n");
            }
        }

        if (myht && !is_enum) {
            ZEND_HASH_FOREACH(myht, _b) {
                var_export_object_element(b, &_b->val, _b->h, _b->key, level);
            } ZEND_HASH_FOREACH_END();
        }
        var_ht_leave(myht);

        if (level > 1 && !is_enum) {
            vb_spaces(b, level - 1);
        }
        if (std_object_syntax) {
            vb_putc(b, ')');
        } else if (!is_enum) {
            vb_puts(b, "))");
        }
        break;
    }

    case IS_REFERENCE:
        struc = &Z_REF_P(struc)->val;
        goto again;

    default:
        vb_puts(b, "NULL");
        break;
    }
}

void php_var_export(zval *v, int level)
{
    vbuf b;
    const HashTable *prev = var_note_top(v);

    vb_init(&b);
    var_export_worker(&b, v, level);
    vb_flush(&b);
    var_top_ht = prev;
}

static void zif_var_export(zval *ret, uint32_t argc, zval *argv)
{
    int do_return = 0;

    if (!var_arity(argc, 1, 2, "var_export")) return;
    if (argc >= 2) {
        do_return = zval_is_true(zval_deref(&argv[1]));
    }
    if (do_return) {
        vbuf b;
        const HashTable *prev = var_note_top(&argv[0]);

        vb_init(&b);
        var_export_worker(&b, zval_deref(&argv[0]), 1);
        ZVAL_STR(ret, vb_take(&b));
        var_top_ht = prev;
        return;
    }
    php_var_export(zval_deref(&argv[0]), 1);
    ZVAL_NULL(ret);
}

/* ==================================================================== */

/* `min_args` is deliberately 0: the handlers do their own arity check so that
 * a wrong count yields the 5.6 warning *and* the NULL return value. */
const php_function_entry php_functions_var[] = {
    /* name        handler        min max ref_mask since until */
    {"var_dump",   zif_var_dump,   0, UINT32_MAX, NULL, 0, 0},
    {"print_r",    zif_print_r,    0, 2,          NULL, 0, 0},
    {"var_export", zif_var_export, 0, 2,          NULL, 0, 0},
    {NULL, NULL, 0, 0, NULL, 0, 0}
};

/* ext/standard/php_misc.c -- constants, class/function reflection, callables,
 * ini and error-handling builtins.
 *
 * Mirrors php-src's ext/standard/basic_functions.c, Zend/zend_builtin_functions.c
 * and ext/spl/php_spl.c.  Behaviour that changes after 5.6 is selected at run
 * time from EG(php_version) and marked with a VERSIONGATE comment.
 */

#include "zend.h"
#include "zend_ast.h"
#include "zend_API.h"
#include "php_rand.h"
#include "php_misc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ utils */

/* The class/function/property tables map names onto raw C pointers.  Every
 * pointer member of the zval union aliases the same storage, so reading one
 * back through .value.ref is independent of which member the engine wrote. */
static void *misc_ptr(const zval *v)
{
    return (void *) v->value.ref;
}

static zend_string *misc_lc(const zend_string *s)
{
    return zend_string_tolower((zend_string *) s);
}

static zend_class_entry *misc_ce_by_name(const char *name, int autoload)
{
    zend_string *n = zend_string_initz(name);

    return zend_lookup_class(n, autoload);
}

/* php-src performs the arity check inside the function (in ZPP), so a wrong
 * count is a warning plus a NULL return rather than an aborted call. */
static int misc_arity(uint32_t argc, uint32_t min, uint32_t max, const char *fname)
{
    if (argc < min || (max != (uint32_t) -1 && argc > max)) {
        zend_wrong_param_count(fname, argc, min, max);
        return 0;
    }
    return 1;
}

/* The gettype()-style spelling php-src names an offending value with. */
static const char *misc_word_given(const zval *v)
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

/* php-src ZPP "s": scalars convert, an object needs __toString, an array is
 * rejected with a warning (a TypeError from 8.0). */
static int misc_arg_string(zval *in, zend_string **out, int argnum,
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
                   fname, argnum, misc_word_given(v));

    return 0;
}

/* The class scope the caller is executing in (php-src zend_get_executed_scope). */
static zend_class_entry *misc_scope(void)
{
    if (EG(current) && EG(current)->func) {
        return EG(current)->func->scope;
    }
    return NULL;
}

/* php-src zend_check_protected(): related classes may see each other's
 * protected members. */
static int misc_check_protected(zend_class_entry *declaring, zend_class_entry *scope)
{
    if (!scope || !declaring) {
        return 0;
    }
    return zend_instanceof(scope, declaring) || zend_instanceof(declaring, scope);
}

static int misc_member_visible(uint32_t flags, zend_class_entry *declaring,
                               zend_class_entry *scope)
{
    if (flags & ZEND_ACC_PUBLIC) {
        return 1;
    }
    if (flags & ZEND_ACC_PROTECTED) {
        return misc_check_protected(declaring, scope);
    }
    if (flags & ZEND_ACC_PRIVATE) {
        return scope != NULL && scope == declaring;
    }
    return 1;   /* no explicit visibility: treated as public */
}

/* php-src's ZEND_HANDLE_NUMERIC_STR: a canonical decimal integer spelling is
 * always stored as a numeric key.  This engine keeps such object property
 * names as strings, so recognise them here (same rule as php_var.c). */
static int misc_key_is_numeric(const zend_string *key, zend_long *idx)
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
        return 0;
    }
    for (; i < len; i++) {
        if (p[i] < '0' || p[i] > '9') {
            return 0;
        }
        acc = acc * 10ULL + (unsigned long long) (p[i] - '0');
        if (acc > (unsigned long long) ZEND_LONG_MAX + (neg ? 1ULL : 0ULL)) {
            return 0;
        }
    }
    if (neg && acc == 0) {
        return 0;
    }
    if (idx) {
        *idx = neg ? -(zend_long) acc : (zend_long) acc;
    }
    return 1;
}

/* Split "\0Class\0prop" the way php-src's mangling stores it. */
static int misc_unmangle(const zend_string *key, const char **cls, size_t *cls_len,
                         const char **prop, size_t *prop_len)
{
    const char *v = ZSTR_VAL(key);
    size_t len = ZSTR_LEN(key);
    size_t i;

    *cls = NULL;
    *cls_len = 0;
    *prop = v;
    *prop_len = len;

    if (len == 0 || v[0] != '\0') {
        return 0;
    }
    for (i = 1; i < len; i++) {
        if (v[i] == '\0') {
            *cls = v + 1;
            *cls_len = i - 1;
            *prop = v + i + 1;
            *prop_len = len - i - 1;
            return 1;
        }
    }
    return 0;
}

/* The class named by an object-or-string argument. */
static zend_class_entry *misc_arg_ce(zval *v, int autoload)
{
    v = zval_deref(v);
    if (Z_TYPE_P(v) == IS_OBJECT) {
        return Z_OBJ_P(v)->ce;
    }
    if (Z_TYPE_P(v) == IS_STRING) {
        return zend_lookup_class(Z_STR_P(v), autoload);
    }
    return NULL;
}

/* ==================================================================== */
/*                              constants                               */
/* ==================================================================== */

/* Resolve "Foo::BAR"; returns 0 when the name has no "::". */
static int misc_class_constant(const zend_string *name, zval *out)
{
    const char *v = ZSTR_VAL(name);
    size_t len = ZSTR_LEN(name);
    size_t i;
    zend_string *cls, *cname;
    zend_class_entry *ce;
    zval *slot;

    for (i = 0; i + 1 < len; i++) {
        if (v[i] == ':' && v[i + 1] == ':') {
            break;
        }
    }
    if (i + 1 >= len) {
        return 0;
    }
    cls = zend_string_init(v, i);
    cname = zend_string_init(v + i + 2, len - i - 2);

    ce = zend_lookup_class(cls, 1);
    if (!ce) {
        ZVAL_UNDEF(out);
        return 1;
    }

    /* 8.1: an enum case is fetched as a class constant. */

        /* 5.6: no enums */


    while (ce) {
        if (ce->constants_table) {
            slot = zend_hash_find(ce->constants_table, cname);
            if (slot) {
                zend_class_constant *cc = misc_ptr(slot);

                if (cc) {
                    if (!cc->evaluated && cc->ast) {
                        zval tmp;

                        if (zend_eval_expr(cc->ast, &tmp) == 0) {
                            cc->value = tmp;
                            cc->evaluated = 1;
                        }
                    }
                    *out = zval_copy(cc->value);
                    return 1;
                }
            }
        }
        ce = ce->parent;
    }
    ZVAL_UNDEF(out);
    return 1;
}

static void zif_defined(zval *ret, uint32_t argc, zval *argv)
{
    zend_string *name;
    zval tmp;

    if (!misc_arity(argc, 1, 1, "defined")) return;
    if (!misc_arg_string(&argv[0], &name, 1, "defined", "constant_name")) return;
    ZVAL_UNDEF(&tmp);
    if (misc_class_constant(name, &tmp)) {
        ZVAL_BOOL(ret, Z_TYPE(tmp) != IS_UNDEF);
        return;
    }
    ZVAL_BOOL(ret, EG(constants) && zend_hash_find(EG(constants), name) != NULL);
}

static void zif_constant(zval *ret, uint32_t argc, zval *argv)
{
    zend_string *name;
    zval tmp;
    zval *slot;

    if (!misc_arity(argc, 1, 1, "constant")) return;
    if (!misc_arg_string(&argv[0], &name, 1, "constant", "name")) return;
    ZVAL_UNDEF(&tmp);
    if (misc_class_constant(name, &tmp)) {
        if (Z_TYPE(tmp) != IS_UNDEF) {
            *ret = tmp;
            return;
        }
    } else if (EG(constants) && (slot = zend_hash_find(EG(constants), name)) != NULL) {
        *ret = zval_copy(*slot);
        return;
    }

    /* 8.0 turned the "couldn't find constant" warning into an Error. */

        zend_error(E_WARNING, "constant(): Couldn't find constant %s", ZSTR_VAL(name));

}

static void zif_define(zval *ret, uint32_t argc, zval *argv)
{
    zend_string *name;
    zval *value;
    int case_insensitive = 0;

    if (!misc_arity(argc, 2, 3, "define")) return;
    if (!misc_arg_string(&argv[0], &name, 1, "define", "constant_name")) return;
    value = zval_deref(&argv[1]);
    if (argc >= 3) {
        case_insensitive = zval_is_true(zval_deref(&argv[2]));
    }

    if (!EG(constants)) {
        ZVAL_BOOL(ret, 0);
        return;
    }
    if (zend_hash_find(EG(constants), name) != NULL) {
        /* 8.0 raised "Constant %s already defined" from a notice to a
         * warning. */

            zend_error(E_NOTICE, "Constant %s already defined", ZSTR_VAL(name));

        ZVAL_BOOL(ret, 0);
        return;
    }

    zend_hash_update(EG(constants), name, zval_copy(*value));

    /* Case-insensitive constants were deprecated in 7.3 and removed in 8.0;
     * before that define($n, $v, true) also registered the lowercase name. */

        if (case_insensitive) {
            zend_string *lc = misc_lc(name);

            if (!zend_hash_find(EG(constants), lc)) {
                zend_hash_update(EG(constants), lc, zval_copy(*value));
            }
        }

    ZVAL_BOOL(ret, 1);
}

/* ==================================================================== */
/*                     existence / reflection helpers                   */
/* ==================================================================== */

static void zif_function_exists(zval *ret, uint32_t argc, zval *argv)
{
    zend_string *name;
    zend_string *lc;

    if (!misc_arity(argc, 1, 1, "function_exists")) return;
    if (!misc_arg_string(&argv[0], &name, 1, "function_exists", "function")) return;
    /* php-src strips one leading namespace separator */
    if (ZSTR_LEN(name) && ZSTR_VAL(name)[0] == '\\') {
        name = zend_string_init(ZSTR_VAL(name) + 1, ZSTR_LEN(name) - 1);
    }
    lc = misc_lc(name);
    ZVAL_BOOL(ret, zend_lookup_function(lc) != NULL);
}

static int misc_class_kind_matches(const zend_class_entry *ce, uint32_t want)
{
    uint32_t kind = ce->flags & (ZEND_ACC_INTERFACE | ZEND_ACC_TRAIT);

    if (want == 0) {
        /* class_exists(): interfaces and traits do not count */
        return kind == 0;
    }
    return (ce->flags & want) != 0;
}

static void misc_exists(zval *ret, uint32_t argc, zval *argv, uint32_t want,
                        int enum_only, const char *fname)
{
    zend_string *name;
    int autoload = 1;
    zend_class_entry *ce;

    if (!misc_arity(argc, 1, 2, fname)) return;
    if (!misc_arg_string(&argv[0], &name, 1, fname, "class")) return;
    if (argc >= 2) {
        autoload = zval_is_true(zval_deref(&argv[1]));
    }
    if (ZSTR_LEN(name) && ZSTR_VAL(name)[0] == '\\') {
        name = zend_string_init(ZSTR_VAL(name) + 1, ZSTR_LEN(name) - 1);
    }
    ce = zend_lookup_class(name, autoload);
    if (!ce) {
        ZVAL_BOOL(ret, 0);
        return;
    }
    if (enum_only) {
        ZVAL_BOOL(ret, ce->is_enum != 0);
        return;
    }
    ZVAL_BOOL(ret, misc_class_kind_matches(ce, want));
}

static void zif_class_exists(zval *ret, uint32_t argc, zval *argv)
{
    misc_exists(ret, argc, argv, 0, 0, "class_exists");
}

static void zif_interface_exists(zval *ret, uint32_t argc, zval *argv)
{
    misc_exists(ret, argc, argv, ZEND_ACC_INTERFACE, 0, "interface_exists");
}

static void zif_trait_exists(zval *ret, uint32_t argc, zval *argv)
{
    misc_exists(ret, argc, argv, ZEND_ACC_TRAIT, 0, "trait_exists");
}



static void zif_method_exists(zval *ret, uint32_t argc, zval *argv)
{
    zend_class_entry *ce;
    zend_string *method, *lc;

    if (!misc_arity(argc, 2, 2, "method_exists")) return;
    if (!misc_arg_string(&argv[1], &method, 2, "method_exists", "method")) return;
    ce = misc_arg_ce(&argv[0], 1);
    if (!ce) {
        ZVAL_BOOL(ret, 0);
        return;
    }
    lc = misc_lc(method);
    ZVAL_BOOL(ret, zend_lookup_method(ce, lc) != NULL);
}

static void zif_property_exists(zval *ret, uint32_t argc, zval *argv)
{
    zval *subject;
    zend_class_entry *ce, *walk;
    zend_string *prop;

    if (!misc_arity(argc, 2, 2, "property_exists")) return;
    if (!misc_arg_string(&argv[1], &prop, 2, "property_exists", "property")) return;
    subject = zval_deref(&argv[0]);
    ce = misc_arg_ce(&argv[0], 1);
    if (!ce) {
        ZVAL_BOOL(ret, 0);
        return;
    }

    /* A declared property lives in the declaring class's tables; this engine
     * does not copy inherited entries into the subclass, so walk up. */
    for (walk = ce; walk != NULL; walk = walk->parent) {
        if (walk->properties_info && zend_hash_find(walk->properties_info, prop)) {
            ZVAL_BOOL(ret, 1);
            return;
        }
        if (walk->static_members && zend_hash_find(walk->static_members, prop)) {
            ZVAL_BOOL(ret, 1);
            return;
        }
    }
    if (Z_TYPE_P(subject) == IS_OBJECT && Z_OBJ_P(subject)->properties &&
        zend_hash_find(Z_OBJ_P(subject)->properties, prop)) {
        ZVAL_BOOL(ret, 1);
        return;
    }
    ZVAL_BOOL(ret, 0);
}

/* ==================================================================== */
/*                         class introspection                          */
/* ==================================================================== */

static void zif_get_class(zval *ret, uint32_t argc, zval *argv)
{
    if (!misc_arity(argc, 0, 1, "get_class")) return;
    if (argc >= 1 && Z_TYPE_P(zval_deref(&argv[0])) == IS_OBJECT) {
        zend_class_entry *ce = Z_OBJ_P(zval_deref(&argv[0]))->ce;

        if (ce) {
            ZVAL_STR(ret, zend_string_init(ZSTR_VAL(ce->name), ZSTR_LEN(ce->name)));
            return;
        }
        ZVAL_BOOL(ret, 0);
        return;
    }

    if (argc >= 1) {

            zend_error(E_WARNING, "get_class() expects parameter 1 to be object, %s given",
                       zend_zval_type_name(zval_deref(&argv[0])));
            ZVAL_BOOL(ret, 0);

        return;
    }

    /* 8.3 deprecated the no-argument form. */

        /* 5.6 .. 8.2: silent */

    {
        zend_class_entry *scope = misc_scope();

        if (!scope) {
            zend_error(E_WARNING, "get_class() called without object from outside a class");
            ZVAL_BOOL(ret, 0);
            return;
        }
        ZVAL_STR(ret, zend_string_init(ZSTR_VAL(scope->name), ZSTR_LEN(scope->name)));
    }
}

static void zif_get_parent_class(zval *ret, uint32_t argc, zval *argv)
{
    zend_class_entry *ce = NULL;

    if (!misc_arity(argc, 0, 1, "get_parent_class")) return;
    if (argc >= 1) {
        ce = misc_arg_ce(&argv[0], 1);
    } else {
        ce = misc_scope();
    }
    if (ce && ce->parent) {
        ZVAL_STR(ret, zend_string_init(ZSTR_VAL(ce->parent->name),
                                       ZSTR_LEN(ce->parent->name)));
        return;
    }
    ZVAL_BOOL(ret, 0);
}

static void zif_get_called_class(zval *ret, uint32_t argc, zval *argv)
{
    zend_class_entry *ce = NULL;

    (void) argv;
    if (!misc_arity(argc, 0, 0, "get_called_class")) return;
    if (EG(current)) {
        ce = EG(current)->called_scope;
        if (!ce && EG(current)->This) {
            ce = EG(current)->This->ce;
        }
    }
    if (!ce) {
        ce = misc_scope();
    }
    if (!ce) {

            zend_error(E_WARNING, "get_called_class() called from outside a class");
            ZVAL_BOOL(ret, 0);

        return;
    }
    ZVAL_STR(ret, zend_string_init(ZSTR_VAL(ce->name), ZSTR_LEN(ce->name)));
}

static void zif_get_object_vars(zval *ret, uint32_t argc, zval *argv)
{
    zval *v;
    zend_class_entry *scope = misc_scope();
    zend_object *obj;
    HashTable *out;

    if (!misc_arity(argc, 1, 1, "get_object_vars")) return;
    v = zval_deref(&argv[0]);
    if (Z_TYPE_P(v) != IS_OBJECT) {

            zend_error(E_WARNING, "get_object_vars() expects parameter 1 to be object, %s given",
                       zend_zval_type_name(v));
            ZVAL_BOOL(ret, 0);

        return;
    }
    obj = Z_OBJ_P(v);
    out = zend_hash_new();

    if (obj->properties) {
        ZEND_HASH_FOREACH(obj->properties, _b) {
            const char *cls, *prop;
            size_t cls_len, prop_len;
            zend_long nidx;
            int mangled;

            if (!_b->key) {
                /* php-src 5.6's get_object_vars() only walks string keys, so a
                 * numerically keyed property is invisible; 7.0 added them. */
                /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
                if (EG(php_version) >= 70000) {
                    zend_hash_index_update(out, (zend_long) _b->h, zval_copy(_b->val));
                }
                continue;
            }
            if (misc_key_is_numeric(_b->key, &nidx)) {
                /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
                if (EG(php_version) >= 70000) {
                    zend_hash_index_update(out, nidx, zval_copy(_b->val));
                }
                continue;
            }
            mangled = misc_unmangle(_b->key, &cls, &cls_len, &prop, &prop_len);
            if (!mangled) {
                zend_hash_update(out, _b->key, zval_copy(_b->val));
                continue;
            }
            if (cls_len == 1 && cls[0] == '*') {
                /* protected */
                if (!misc_check_protected(obj->ce, scope)) {
                    continue;
                }
            } else {
                /* private to `cls` */
                if (!scope || !scope->name ||
                    ZSTR_LEN(scope->name) != cls_len ||
                    memcmp(ZSTR_VAL(scope->name), cls, cls_len) != 0) {
                    continue;
                }
            }
            zend_hash_str_update(out, prop, prop_len, zval_copy(_b->val));
        } ZEND_HASH_FOREACH_END();
    }
    ZVAL_ARR(ret, out);
}

static void zif_get_class_methods(zval *ret, uint32_t argc, zval *argv)
{
    zend_class_entry *ce;
    zend_class_entry *scope = misc_scope();
    HashTable *out;

    if (!misc_arity(argc, 1, 1, "get_class_methods")) return;
    ce = misc_arg_ce(&argv[0], 1);
    if (!ce) {
        ZVAL_BOOL(ret, 0);
        return;
    }
    out = zend_hash_new();
    if (ce->function_table) {
        ZEND_HASH_FOREACH(ce->function_table, _b) {
            zend_function *fn = misc_ptr(&_b->val);
            zval name;

            if (!fn || !fn->name) {
                continue;
            }
            if (!misc_member_visible(fn->flags, fn->scope, scope)) {
                continue;
            }
            ZVAL_STR(&name, zend_string_init(ZSTR_VAL(fn->name), ZSTR_LEN(fn->name)));
            zend_hash_next_index_insert(out, name);
        } ZEND_HASH_FOREACH_END();
    }
    ZVAL_ARR(ret, out);
}

/* One add_class_vars() pass: `statics` selects which half of the property set
 * is collected.  php-src runs the non-static pass first and then the static
 * one, which is what fixes the ordering. */
static void misc_add_class_vars(zend_class_entry *asked, zend_class_entry *scope,
                                int statics, HashTable *out)
{
    zend_class_entry *walk;

    for (walk = asked; walk != NULL; walk = walk->parent) {
        if (!walk->properties_info) {
            continue;
        }
        ZEND_HASH_FOREACH(walk->properties_info, _b) {
            zend_property_info *pi = misc_ptr(&_b->val);
            zend_class_entry *decl;
            zend_string *key;
            zval *slot;
            int is_static;

            if (!pi || !pi->name) {
                continue;
            }
            is_static = (pi->flags & ZEND_ACC_STATIC) != 0;
            if (is_static != (statics != 0)) {
                continue;
            }
            decl = pi->ce ? pi->ce : walk;
            if (!misc_member_visible(pi->flags, decl, scope)) {
                continue;
            }
            if (zend_hash_find(out, pi->name)) {
                continue;               /* a nearer declaration already won */
            }
            key = pi->mangled ? pi->mangled : pi->name;
            slot = NULL;
            if (is_static) {
                if (asked->static_members) slot = zend_hash_find(asked->static_members, key);
                if (!slot && walk->static_members) slot = zend_hash_find(walk->static_members, key);
                if (!slot && asked->static_members) slot = zend_hash_find(asked->static_members, pi->name);
                if (!slot && walk->static_members) slot = zend_hash_find(walk->static_members, pi->name);
            } else {
                if (asked->default_properties) slot = zend_hash_find(asked->default_properties, key);
                if (!slot && walk->default_properties) slot = zend_hash_find(walk->default_properties, key);
                if (!slot && asked->default_properties) slot = zend_hash_find(asked->default_properties, pi->name);
                if (!slot && walk->default_properties) slot = zend_hash_find(walk->default_properties, pi->name);
            }
            if (slot) {
                zend_hash_update(out, pi->name, zval_copy(*slot));
            }
        } ZEND_HASH_FOREACH_END();
    }
}

static void zif_get_class_vars(zval *ret, uint32_t argc, zval *argv)
{
    zend_string *name;
    zend_class_entry *ce;
    zend_class_entry *scope = misc_scope();
    HashTable *out;

    if (!misc_arity(argc, 1, 1, "get_class_vars")) return;
    if (!misc_arg_string(&argv[0], &name, 1, "get_class_vars", "class")) return;
    ce = zend_lookup_class(name, 1);
    if (!ce) {
        ZVAL_BOOL(ret, 0);
        return;
    }
    out = zend_hash_new();
    misc_add_class_vars(ce, scope, 0, out);
    misc_add_class_vars(ce, scope, 1, out);
    ZVAL_ARR(ret, out);
}

static void misc_is_a(zval *ret, uint32_t argc, zval *argv, int only_subclass,
                      int default_allow_string)
{
    zval *obj;
    zend_class_entry *ce, *target;
    zend_string *tname;
    int allow_string = default_allow_string;
    const char *fname = only_subclass ? "is_subclass_of" : "is_a";

    if (!misc_arity(argc, 2, 3, fname)) return;
    if (!misc_arg_string(&argv[1], &tname, 2, fname, "class")) return;
    obj = zval_deref(&argv[0]);
    if (argc >= 3) {
        allow_string = zval_is_true(zval_deref(&argv[2]));
    }
    if (Z_TYPE_P(obj) == IS_STRING && !allow_string) {
        ZVAL_BOOL(ret, 0);
        return;
    }
    /* php-src autoloads the *subject* when it is given as a string but looks
     * the target class up without autoloading. */
    ce = misc_arg_ce(&argv[0], 1);
    if (!ce) {
        ZVAL_BOOL(ret, 0);
        return;
    }
    target = zend_lookup_class(tname, 0);
    if (!target) {
        ZVAL_BOOL(ret, 0);
        return;
    }
    if (only_subclass && ce == target) {
        ZVAL_BOOL(ret, 0);
        return;
    }
    ZVAL_BOOL(ret, zend_instanceof(ce, target) != 0);
}

static void zif_is_a(zval *ret, uint32_t argc, zval *argv)
{
    misc_is_a(ret, argc, argv, 0, 0);
}

static void zif_is_subclass_of(zval *ret, uint32_t argc, zval *argv)
{
    misc_is_a(ret, argc, argv, 1, 1);
}

/* ==================================================================== */
/*                                 SPL                                  */
/* ==================================================================== */

static int  spl_hash_mask_init;
static uintptr_t spl_hash_mask_handle;
static uintptr_t spl_hash_mask_handlers;

static void zif_spl_object_hash(zval *ret, uint32_t argc, zval *argv)
{
    zval *v;
    char buf[40];

    if (!misc_arity(argc, 1, 1, "spl_object_hash")) return;
    v = zval_deref(&argv[0]);
    if (Z_TYPE_P(v) != IS_OBJECT) {
        ZVAL_BOOL(ret, 0);
        return;
    }

    /* 8.1 dropped the random mask and prints the bare handle padded to 32
     * hex digits; before that the handle and handler pointer were XORed with
     * two mt_rand() draws taken on first use. */

        if (!spl_hash_mask_init) {
            spl_hash_mask_handle   = (uintptr_t) (php_mt_rand() >> 1);
            spl_hash_mask_handlers = (uintptr_t) (php_mt_rand() >> 1);
            spl_hash_mask_init = 1;
        }
        snprintf(buf, sizeof(buf), "%016llx%016llx",
                 (unsigned long long) (spl_hash_mask_handle ^ (uintptr_t) Z_OBJ_P(v)->handle),
                 (unsigned long long) spl_hash_mask_handlers);

    ZVAL_STR(ret, zend_string_initz(buf));
}

/* VERSIONGATE:70200 */
static void zif_spl_object_id(zval *ret, uint32_t argc, zval *argv)
{
    zval *v;

    if (!misc_arity(argc, 1, 1, "spl_object_id")) return;
    v = zval_deref(&argv[0]);
    if (Z_TYPE_P(v) != IS_OBJECT) {
        ZVAL_BOOL(ret, 0);
        return;
    }
    ZVAL_LONG(ret, (zend_long) Z_OBJ_P(v)->handle);
}

static void zif_spl_autoload_register(zval *ret, uint32_t argc, zval *argv)
{
    zval *cb;

    if (!misc_arity(argc, 0, 3, "spl_autoload_register")) return;
    if (argc < 1) {
        ZVAL_BOOL(ret, 1);
        return;
    }
    cb = zval_deref(&argv[0]);
    if (Z_TYPE_P(cb) == IS_NULL) {
        ZVAL_BOOL(ret, 1);
        return;
    }
    if (!EG(autoloaders)) {
        EG(autoloaders) = zend_hash_new();
    }
    zend_hash_next_index_insert(EG(autoloaders), zval_copy(*cb));
    ZVAL_BOOL(ret, 1);
}

/* ==================================================================== */
/*                          iterator_to_array()                         */
/* ==================================================================== */

static int misc_call_method(zend_object *obj, const char *name, zval *retval)
{
    zend_string *lc = zend_string_initz(name);
    zend_function *fn;

    ZVAL_NULL(retval);
    if (!obj->ce) {
        return 1;
    }
    fn = zend_lookup_method(obj->ce, lc);
    if (!fn) {
        return 1;
    }
    return zend_call_function_ex(fn, obj, obj->ce, 0, NULL, NULL, retval, NULL);
}

static void zif_iterator_to_array(zval *ret, uint32_t argc, zval *argv)
{
    zval *src;
    int preserve_keys = 1;
    HashTable *out;
    zend_class_entry *trav, *agg;
    zend_object *it;
    int guard = 0;

    if (!misc_arity(argc, 1, 2, "iterator_to_array")) return;
    src = zval_deref(&argv[0]);
    if (argc >= 2) {
        preserve_keys = zval_is_true(zval_deref(&argv[1]));
    }

    if (Z_TYPE_P(src) == IS_ARRAY) {
        /* 8.2 let iterator_to_array() accept a plain array. */

            zend_throw_error("TypeError",
                             "iterator_to_array(): Argument #1 ($iterator) must be of "
                             "type Traversable, array given");

        return;
    }

    if (Z_TYPE_P(src) != IS_OBJECT) {
        zend_throw_error("TypeError",
                         "iterator_to_array(): Argument #1 ($iterator) must be of "
                         "type Traversable, %s given", zend_zval_type_name(src));
        return;
    }

    trav = misc_ce_by_name("Traversable", 0);
    agg  = misc_ce_by_name("IteratorAggregate", 0);
    it = Z_OBJ_P(src);

    if (trav && it->ce && !zend_instanceof(it->ce, trav)) {
        zend_throw_error("TypeError",
                         "iterator_to_array(): Argument #1 ($iterator) must be of "
                         "type Traversable, %s given", zend_zval_type_name(src));
        return;
    }

    /* unwrap IteratorAggregate::getIterator() (bounded, to avoid cycles) */
    while (agg && it->ce && zend_instanceof(it->ce, agg) && guard++ < 16) {
        zval inner;

        if (misc_call_method(it, "getiterator", &inner) != 0 ||
            Z_TYPE(inner) != IS_OBJECT) {
            break;
        }
        it = Z_OBJ(inner);
    }

    out = zend_hash_new();
    {
        zval tmp;

        if (misc_call_method(it, "rewind", &tmp) != 0) {
            ZVAL_ARR(ret, out);
            return;
        }
        for (;;) {
            zval valid, item, key;

            if (misc_call_method(it, "valid", &valid) != 0 || !zval_is_true(&valid)) {
                break;
            }
            if (misc_call_method(it, "current", &item) != 0) {
                break;
            }
            if (!preserve_keys) {
                zend_hash_next_index_insert(out, item);
            } else {
                if (misc_call_method(it, "key", &key) != 0) {
                    ZVAL_NULL(&key);
                }
                if (Z_TYPE(key) == IS_LONG) {
                    zend_hash_index_update(out, Z_LVAL(key), item);
                } else {
                    zend_hash_update(out, zval_get_string(&key), item);
                }
            }
            if (misc_call_method(it, "next", &tmp) != 0 || EG_HAS_EXCEPTION()) {
                break;
            }
        }
    }
    ZVAL_ARR(ret, out);
}

/* ==================================================================== */
/*                         arguments and callables                      */
/* ==================================================================== */

static void zif_func_num_args(zval *ret, uint32_t argc, zval *argv)
{
    (void) argv;
    if (!misc_arity(argc, 0, 0, "func_num_args")) return;
    if (!EG(current)) {
        zend_error(E_WARNING, "func_num_args():  Called from the global scope - "
                              "no function context");
        ZVAL_LONG(ret, -1);
        return;
    }
    ZVAL_LONG(ret, (zend_long) EG(current)->num_args);
}

static void zif_func_get_args(zval *ret, uint32_t argc, zval *argv)
{
    HashTable *out;
    uint32_t i;

    (void) argv;
    if (!misc_arity(argc, 0, 0, "func_get_args")) return;
    if (!EG(current)) {
        zend_error(E_WARNING, "func_get_args():  Called from the global scope - "
                              "no function context");
        ZVAL_BOOL(ret, 0);
        return;
    }
    out = zend_hash_new();
    for (i = 0; i < EG(current)->num_args; i++) {
        zval *a = EG(current)->args ? zval_deref(&EG(current)->args[i]) : NULL;

        zend_hash_next_index_insert(out, a ? zval_copy(*a) : zval_null());
    }
    ZVAL_ARR(ret, out);
}

static void zif_func_get_arg(zval *ret, uint32_t argc, zval *argv)
{
    zend_long n;

    if (!misc_arity(argc, 1, 1, "func_get_arg")) return;
    n = zval_get_long(zval_deref(&argv[0]));
    if (!EG(current)) {
        zend_error(E_WARNING, "func_get_arg():  Called from the global scope - "
                              "no function context");
        ZVAL_BOOL(ret, 0);
        return;
    }
    if (n < 0) {

            zend_error(E_WARNING, "func_get_arg():  The argument number should be >= 0");
            ZVAL_BOOL(ret, 0);

        return;
    }
    if ((uint32_t) n >= EG(current)->num_args) {

            zend_error(E_WARNING, "func_get_arg():  Argument %lld not passed to function",
                       (long long) n);
            ZVAL_BOOL(ret, 0);

        return;
    }
    *ret = zval_copy(*zval_deref(&EG(current)->args[n]));
}

/* php-src 5.6 quotes identifiers in a callback diagnostic with apostrophes
 * ("class 'Foo' not found"); the engine's zend_is_callable_zval() uses double
 * quotes.  Rewrite just those quotes so the ZPP message reads as 5.6 spells
 * it, leaving the rest of the reason untouched. */
static zend_string *misc_callable_reason(const zend_string *err)
{
    zend_string *out;
    size_t i;

    if (err == NULL) {
        return zend_string_initz("no array or string given");
    }

    out = zend_string_init(ZSTR_VAL(err), ZSTR_LEN(err));
    for (i = 0; i < ZSTR_LEN(out); i++) {
        if (ZSTR_VAL(out)[i] == '"') {
            ZSTR_VAL(out)[i] = '\'';
        }
    }
    return out;
}

/* A non-static method reached through a class-name callable has no $this.
 * php-src let a *userland* one through (the call itself then emits "Non-static
 * method ... should not be called statically") but never an internal one,
 * because ZEND_ACC_ALLOW_STATIC was only set on userland methods; 8.0 stopped
 * allowing either.  Returns 1 when the callable stands. */
static int misc_static_call_ok(const zend_function *fn, const zend_object *obj)
{
    if (fn == NULL || obj != NULL || fn->scope == NULL) {
        return 1;                       /* plain function, or $this is bound */
    }
    if (fn->flags & ZEND_ACC_STATIC) {
        return 1;
    }

    return !fn->is_internal;
}

/* php-src refuses to bind a plain value to a by-reference parameter when the
 * call is made through call_user_func*(): 5.6 warns and returns NULL. */
static int misc_check_byref_args(zval *cb, uint32_t argc, zval *argv,
                                 const char *fname)
{
    zend_function *fn = NULL;
    zend_object *obj = NULL;
    zend_class_entry *scope = NULL;
    uint32_t i;

    (void) fname;
    if (zend_is_callable_zval(zval_deref(cb), &fn, &obj, &scope, NULL) == 0) {
        return 1;
    }
    if (fn == NULL || fn->args == NULL || fn->num_args == 0) {
        return 1;
    }
    for (i = 0; i < argc; i++) {
        const zend_arg_info *ai;

        if (i < fn->num_args) {
            ai = &fn->args[i];
        } else if (fn->flags & ZEND_ACC_VARIADIC) {
            ai = &fn->args[fn->num_args - 1];
        } else {
            break;
        }
        if (!ai->by_ref || Z_TYPE_P(&argv[i]) == IS_REFERENCE) {
            continue;
        }
        if (fn->scope != NULL && fn->scope->name != NULL && fn->name != NULL) {
            zend_error(E_WARNING, "Parameter %u to %s::%s() expected to be a "
                       "reference, value given", (unsigned) (i + 1),
                       ZSTR_VAL(fn->scope->name), ZSTR_VAL(fn->name));
        } else {
            zend_error(E_WARNING, "Parameter %u to %s() expected to be a "
                       "reference, value given", (unsigned) (i + 1),
                       fn->name ? ZSTR_VAL(fn->name) : "?");
        }
        return 0;
    }
    return 1;
}

/* php-src's ZPP "f" spec: report an invalid callback under the *calling*
 * function's name with the reason zend_is_callable_ex() produced. */
static int misc_arg_callable(zval *in, int argnum, const char *fname)
{
    zend_string *err = NULL;
    zend_string *reason;
    zend_function *fn = NULL;
    zend_object *obj = NULL;

    if (zend_is_callable_zval(zval_deref(in), &fn, &obj, NULL, &err) != 0) {
        if (misc_static_call_ok(fn, obj)) {
            return 1;
        }
        err = zend_strpprintf("non-static method %s::%s() cannot be called statically",
                              (fn && fn->scope && fn->scope->name) ? ZSTR_VAL(fn->scope->name) : "?",
                              (fn && fn->name) ? ZSTR_VAL(fn->name) : "?");
    }
    reason = misc_callable_reason(err);

        zend_error(E_WARNING, "%s() expects parameter %d to be a valid callback, %s",
                   fname, argnum, ZSTR_VAL(reason));

    return 0;
}

static void zif_call_user_func(zval *ret, uint32_t argc, zval *argv)
{
    zval *cb;

    if (!misc_arity(argc, 1, (uint32_t) -1, "call_user_func")) return;
    if (!misc_arg_callable(&argv[0], 1, "call_user_func")) return;
    if (argc > 1 && !misc_check_byref_args(&argv[0], argc - 1, &argv[1],
                                           "call_user_func")) {
        return;
    }
    cb = zval_deref(&argv[0]);
    if (zend_call_user_callable(cb, argc > 0 ? argc - 1 : 0,
                                argc > 1 ? &argv[1] : NULL, ret) != 0) {
        ZVAL_NULL(ret);
    }
}

static void zif_call_user_func_array(zval *ret, uint32_t argc, zval *argv)
{
    zval *cb;
    zval *args;
    uint32_t n = 0, i = 0;
    zval *vec = NULL;

    if (!misc_arity(argc, 2, 2, "call_user_func_array")) return;
    if (!misc_arg_callable(&argv[0], 1, "call_user_func_array")) return;
    cb = zval_deref(&argv[0]);
    args = zval_deref(&argv[1]);
    if (Z_TYPE_P(args) != IS_ARRAY) {

            zend_error(E_WARNING, "call_user_func_array() expects parameter 2 to be array, %s given",
                       zend_zval_type_name(args));
            ZVAL_BOOL(ret, 0);

        return;
    }
    n = zend_hash_num_elements(Z_ARR_P(args));
    if (n) {
        vec = malloc(sizeof(zval) * n);
        if (!vec) {
            ZVAL_NULL(ret);
            return;
        }
        ZEND_HASH_FOREACH(Z_ARR_P(args), _b) {
            if (i < n) {
                vec[i++] = _b->val;
            }
        } ZEND_HASH_FOREACH_END();
    }
    if (!misc_check_byref_args(&argv[0], i, vec, "call_user_func_array")) {
        free(vec);
        return;
    }
    if (zend_call_user_callable(cb, i, vec, ret) != 0) {
        ZVAL_NULL(ret);
    }
    free(vec);
}

/* ==================================================================== */
/*                        ini and error handling                        */
/* ==================================================================== */

static void zif_ini_get(zval *ret, uint32_t argc, zval *argv)
{
    zend_string *name;
    zval *slot;

    if (!misc_arity(argc, 1, 1, "ini_get")) return;
    if (!misc_arg_string(&argv[0], &name, 1, "ini_get", "option")) return;
    if (!EG(ini) || (slot = zend_hash_find(EG(ini), name)) == NULL) {
        ZVAL_BOOL(ret, 0);
        return;
    }
    ZVAL_STR(ret, zval_get_string(slot));
}

static void zif_ini_set(zval *ret, uint32_t argc, zval *argv)
{
    zend_string *name;
    zend_string *val;
    zval *slot;
    zval nv;

    if (!misc_arity(argc, 2, 2, "ini_set")) return;
    if (!misc_arg_string(&argv[0], &name, 1, "ini_set", "option")) return;
    if (!misc_arg_string(&argv[1], &val, 2, "ini_set", "value")) return;
    if (!EG(ini)) {
        EG(ini) = zend_hash_new();
    }
    slot = zend_hash_find(EG(ini), name);
    if (slot) {
        ZVAL_STR(ret, zval_get_string(slot));
    } else {
        ZVAL_BOOL(ret, 0);
    }
    ZVAL_STR(&nv, val);
    zend_hash_update(EG(ini), name, nv);
}

static void zif_error_reporting(zval *ret, uint32_t argc, zval *argv)
{
    if (!misc_arity(argc, 0, 1, "error_reporting")) return;
    ZVAL_LONG(ret, (zend_long) EG(error_reporting));
    if (argc >= 1) {
        zval *v = zval_deref(&argv[0]);

        if (Z_TYPE_P(v) != IS_NULL) {
            EG(error_reporting) = (int) zval_get_long(v);
        }
    }
}

/* php-src keeps the user error handler in EG(user_error_handler) plus a stack.
 * The frozen zend_globals has no slot for it, so the stack lives here; the
 * engine's zend_error() has to consult it through this module. */
#define MISC_EH_MAX 64
static zval misc_error_handlers[MISC_EH_MAX];
static int  misc_error_handler_levels[MISC_EH_MAX];
static int  misc_error_handler_top;      /* number of entries in use */
static int  misc_error_handler_running;  /* php-src does not re-enter */

int php_call_user_error_handler(int type, const char *msg,
                                const char *file, uint32_t line)
{
    zval handler, rv;
    zval args[5];
    uint32_t nargs;
    int consumed;

    if (misc_error_handler_top <= 0 || misc_error_handler_running) {
        return 0;
    }
    /* php-src never routes these to userland. */
    switch (type) {
    case E_ERROR: case E_PARSE: case E_CORE_ERROR: case E_CORE_WARNING:
    case E_COMPILE_ERROR: case E_COMPILE_WARNING:
        return 0;
    default:
        break;
    }
    if ((misc_error_handler_levels[misc_error_handler_top - 1] & type) == 0) {
        return 0;
    }

    handler = misc_error_handlers[misc_error_handler_top - 1];
    args[0] = zval_long((zend_long) type);
    args[1] = zval_string(msg ? msg : "");
    args[2] = zval_string(file ? file : "");
    args[3] = zval_long((zend_long) line);

    /* $errcontext was dropped in 8.0. */

        args[4] = zval_array(zend_hash_new());
        nargs = 5;


    ZVAL_NULL(&rv);
    misc_error_handler_running = 1;
    if (zend_call_user_callable(&handler, nargs, args, &rv) != 0) {
        misc_error_handler_running = 0;
        /* the handler threw: the exception replaces the diagnostic */
        return 1;
    }
    misc_error_handler_running = 0;

    /* Returning exactly false means "let the standard handler run too". */
    consumed = (Z_TYPE(rv) != IS_FALSE);
    return consumed;
}

static void zif_set_error_handler(zval *ret, uint32_t argc, zval *argv)
{
    zval *cb;
    zend_long levels = E_ALL;

    if (!misc_arity(argc, 1, 2, "set_error_handler")) return;
    cb = zval_deref(&argv[0]);
    if (argc >= 2) {
        levels = zval_get_long(zval_deref(&argv[1]));
    }

    if (misc_error_handler_top > 0) {
        *ret = zval_copy(misc_error_handlers[misc_error_handler_top - 1]);
    } else {
        ZVAL_NULL(ret);
    }

    if (Z_TYPE_P(cb) == IS_NULL) {
        if (misc_error_handler_top > 0) {
            misc_error_handler_top--;
        }
        return;
    }
    if (!misc_arg_callable(&argv[0], 1, "set_error_handler")) {
        return;
    }
    if (misc_error_handler_top < MISC_EH_MAX) {
        misc_error_handlers[misc_error_handler_top] = zval_copy(*cb);
        misc_error_handler_levels[misc_error_handler_top] = (int) levels;
        misc_error_handler_top++;
    }
}

static void zif_restore_error_handler(zval *ret, uint32_t argc, zval *argv)
{
    (void) argv;
    if (!misc_arity(argc, 0, 0, "restore_error_handler")) return;
    if (misc_error_handler_top > 0) {
        misc_error_handler_top--;
    }
    ZVAL_BOOL(ret, 1);
}

static void zif_trigger_error(zval *ret, uint32_t argc, zval *argv)
{
    zend_string *msg;
    zend_long level = E_USER_NOTICE;

    if (!misc_arity(argc, 1, 2, "trigger_error")) return;
    if (!misc_arg_string(&argv[0], &msg, 1, "trigger_error", "message")) return;
    if (argc >= 2) {
        level = zval_get_long(zval_deref(&argv[1]));
    }
    switch (level) {
    case E_USER_ERROR:
    case E_USER_WARNING:
    case E_USER_NOTICE:
    case E_USER_DEPRECATED:
        break;
    default:
        /* 8.0 turned the "invalid error type" warning into a ValueError. */

            zend_error(E_WARNING, "Invalid error type specified");
            ZVAL_BOOL(ret, 0);

        return;
    }
    zend_error((int) level, "%s", ZSTR_VAL(msg));
    ZVAL_BOOL(ret, 1);
}

/* ==================================================================== */

/* `min_args` is deliberately 0 on every row: like php-src, this module does
 * its own arity check inside each handler (misc_arity()) so that a wrong
 * argument count yields the 5.6 warning *and* the NULL return value.  See the
 * matching note in php_math.c. */
const php_function_entry php_functions_misc[] = {
    /* name                    handler                     min max ref_mask since  until */
    {"define",                 zif_define,                 0, 3, NULL,  0,     0},
    {"defined",                zif_defined,                0, 1, NULL,  0,     0},
    {"constant",               zif_constant,               0, 1, NULL,  0,     0},
    {"function_exists",        zif_function_exists,        0, 1, NULL,  0,     0},
    {"method_exists",          zif_method_exists,          0, 2, NULL,  0,     0},
    {"property_exists",        zif_property_exists,        0, 2, NULL,  0,     0},
    {"class_exists",           zif_class_exists,           0, 2, NULL,  0,     0},
    {"interface_exists",       zif_interface_exists,       0, 2, NULL,  0,     0},
    {"trait_exists",           zif_trait_exists,           0, 2, NULL,  0,     0},

    {"get_class",              zif_get_class,              0, 1, NULL,  0,     0},
    {"get_parent_class",       zif_get_parent_class,       0, 1, NULL,  0,     0},
    {"get_called_class",       zif_get_called_class,       0, 0, NULL,  0,     0},
    {"get_object_vars",        zif_get_object_vars,        0, 1, NULL,  0,     0},
    {"get_class_methods",      zif_get_class_methods,      0, 1, NULL,  0,     0},
    {"get_class_vars",         zif_get_class_vars,         0, 1, NULL,  0,     0},
    {"is_a",                   zif_is_a,                   0, 3, NULL,  0,     0},
    {"is_subclass_of",         zif_is_subclass_of,         0, 3, NULL,  0,     0},
    {"spl_object_hash",        zif_spl_object_hash,        0, 1, NULL,  0,     0},
    /* VERSIONGATE:70200 */
    {"spl_object_id",          zif_spl_object_id,          0, 1, NULL,  70200, 0},
    {"spl_autoload_register",  zif_spl_autoload_register,  0, 3, NULL,  0,     0},
    {"iterator_to_array",      zif_iterator_to_array,      0, 2, NULL,  0,     0},
    {"func_get_args",          zif_func_get_args,          0, 0, NULL,  0,     0},
    {"func_num_args",          zif_func_num_args,          0, 0, NULL,  0,     0},
    {"func_get_arg",           zif_func_get_arg,           0, 1, NULL,  0,     0},
    {"call_user_func",         zif_call_user_func,         0, UINT32_MAX, NULL, 0, 0},
    {"call_user_func_array",   zif_call_user_func_array,   0, 2, NULL,  0,     0},
    {"error_reporting",        zif_error_reporting,        0, 1, NULL,  0,     0},
    {"ini_get",                zif_ini_get,                0, 1, NULL,  0,     0},
    {"ini_set",                zif_ini_set,                0, 2, NULL,  0,     0},
    {"set_error_handler",      zif_set_error_handler,      0, 2, NULL,  0,     0},
    {"restore_error_handler",  zif_restore_error_handler,  0, 0, NULL,  0,     0},
    {"trigger_error",          zif_trigger_error,          0, 2, NULL,  0,     0},
    {"user_error",             zif_trigger_error,          0, 2, NULL,  0,     0},
    {NULL, NULL, 0, 0, NULL, 0, 0}
};

/* zend_execute.c — the AST evaluator. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1        /* realpath(), strcasecmp(), ucontext */
#endif
#include "zend.h"
#include "zend_ast.h"
#include "zend_API.h"
#include "zend_alloc.h"
#include "zend_object.h"
#include "zend_parse.h"
#include "zend_lex.h"
#include "zend_exceptions.h"
#include "zend_execute.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>

/* ------------------------------------------------------------- utilities */
#define CHECK_EXC() do { if (EG(exception) || EG(exiting)) return 1; } while (0)

/* An expression helper returns non-zero for "stop evaluating this expression".
 * That is only a statement-level abort when something is actually pending: a
 * plain warning (5.6's "Cannot use a scalar value as an array", say) lets the
 * script carry on with the next statement, exactly as php-src does. */
#define EXPR_FAIL() (EG(exiting) ? EX_EXIT : (EG(exception) ? EX_EXCEPTION : EX_NORMAL))

static zend_string *S(const char *s) { return zend_string_initz(s); }

zend_class_entry *zend_get_current_scope(void) {
    zend_execute_data *ex = EG(current);
    while (ex) {
        if (ex->func && ex->func->scope) return ex->func->scope;
        if (ex->This) return ex->This->ce;
        ex = ex->prev;
    }
    return NULL;
}

static zend_class_entry *called_scope(void) {
    zend_execute_data *ex = EG(current);
    while (ex) {
        if (ex->called_scope) return ex->called_scope;
        if (ex->func && ex->func->scope) return ex->func->scope;
        ex = ex->prev;
    }
    return NULL;
}

static HashTable *cur_symbols(void) {
    return EG(current) ? EG(current)->symbols : EG(symbol_table);
}

void zend_set_line(zend_ast *ast) { if (ast && ast->lineno) EG(lineno) = ast->lineno; }

/* --------------------------------------------------------------- keys */
int array_key_normalize(const zval *key0, zend_long *idx, zend_string **str)
{
    const zval *key = zval_deref((zval *)key0);
    *str = NULL;
    switch (key->u1) {
    case IS_UNDEF: case IS_NULL: *str = zend_string_init("", 0); return 0;
    case IS_FALSE: *idx = 0; return 1;
    case IS_TRUE:  *idx = 1; return 1;
    case IS_LONG:  *idx = key->value.lval; return 1;
    case IS_DOUBLE: {
        double d = key->value.dval;
        /* VERSIONGATE:80100 else-branch is the 5.6 behaviour */
        if (EG(php_version) >= 80100 && (d != (double)(zend_long)d))
            zend_error(E_DEPRECATED, "Implicit conversion from float %s to int loses precision",
                       php_double_to_string(d, 14, 0)->val);
        zval t = zval_double(d);
        *idx = zval_get_long(&t);
        return 1;
    }
    case IS_STRING: {
        zend_string *s = key->value.str;
        /* canonical decimal integers become integer keys */
        const char *p = s->val;
        size_t n = s->len;
        if (n == 0 || n > 20) { *str = s; return 0; }
        size_t i = 0; int neg = 0;
        if (p[0] == '-') { neg = 1; i = 1; if (n == 1) { *str = s; return 0; } }
        if (p[i] == '0' && n - i > 1) { *str = s; return 0; }   /* "05" stays a string */
        for (size_t j = i; j < n; j++) if (p[j] < '0' || p[j] > '9') { *str = s; return 0; }
        {
            char buf[24]; memcpy(buf, p, n); buf[n] = 0;
            errno = 0;
            char *end = NULL;
            long long v = strtoll(buf, &end, 10);
            if (errno || *end) { *str = s; return 0; }
            /* "9223372036854775808" does not fit and stays a string */
            char back[32];
            snprintf(back, sizeof(back), "%lld", v);
            if (strcmp(back, buf) != 0) { *str = s; return 0; }
            (void)neg;
            *idx = (zend_long)v;
            return 1;
        }
    }
    case IS_ARRAY:
        zend_throw_error("TypeError", "Illegal offset type");
        return -1;
    case IS_OBJECT:
        /* VERSIONGATE:80000 else-branch is the 5.6 behaviour */
        if (EG(php_version) >= 80000) { zend_throw_error("TypeError", "Illegal offset type"); return -1; }
        zend_error(E_WARNING, "Illegal offset type");
        return -1;
    default: *str = zend_string_init("", 0); return 0;
    }
}

zval *array_get(HashTable *ht, const zval *key)
{
    zend_long idx = 0; zend_string *str = NULL;
    int r = array_key_normalize(key, &idx, &str);
    if (r < 0) return NULL;
    return r ? zend_hash_index_find(ht, idx) : zend_hash_find(ht, str);
}

zval *array_set(HashTable *ht, const zval *key, zval value)
{
    zend_long idx = 0; zend_string *str = NULL;
    int r = array_key_normalize(key, &idx, &str);
    if (r < 0) return NULL;
    return r ? zend_hash_index_update(ht, idx, value) : zend_hash_update(ht, str, value);
}

static zend_string *ast_name(zend_ast *ast)
{
    if (!ast) return NULL;
    if (ast->kind == AST_ZVAL && ast->val.u1 == IS_STRING) return ast->val.value.str;
    return NULL;
}

/* php-src refuses anything but a string as a dynamic method name.  A method
 * call says "Method name", a static call says "Function name". */
static int eval_method_name_ex(zend_ast *ast, zend_string **out, const char *what)
{
    if (ast && ast->kind == AST_ZVAL && ast->val.u1 == IS_STRING) { *out = ast->val.value.str; return 0; }
    zval v = zval_null();
    if (zend_eval_expr(ast, &v)) return 1;
    zval *d = zval_deref(&v);
    if (d->u1 != IS_STRING) {
        /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
        if (EG(php_version) >= 70000)
            zend_throw_error("Error", "%s name must be a string", what);
        else
            zend_fatal("%s name must be a string", what);
        return 1;
    }
    *out = d->value.str;
    return 0;
}

static int eval_method_name(zend_ast *ast, zend_string **out)
{
    return eval_method_name_ex(ast, out, "Method");
}

static int eval_name(zend_ast *ast, zend_string **out)
{
    zend_string *n = ast_name(ast);
    if (n) { *out = n; return 0; }
    zval v = zval_null();
    if (zend_eval_expr(ast, &v)) return 1;
    *out = zval_get_string(&v);
    return 0;
}

/* --------------------------------------------------------- class resolve */
zend_class_entry *zend_resolve_class_ast(zend_ast *ast, int *err)
{
    *err = 0;
    zend_string *name = NULL;
    if (ast->kind == AST_ZVAL && ast->val.u1 == IS_STRING) name = ast->val.value.str;
    else if (ast->kind == AST_CONST) name = ast->val.value.str;
    else {
        zval v = zval_null();
        if (zend_eval_expr(ast, &v)) { *err = 1; return NULL; }
        zval *d = zval_deref(&v);
        if (d->u1 == IS_OBJECT) return d->value.obj->ce;
        name = zval_get_string(d);
    }
    if (zend_string_equals_ci(name, S("self"))) {
        zend_class_entry *ce = zend_get_current_scope();
        if (!ce) { zend_throw_error("Error", "Cannot use \"self\" when no class scope is active"); *err = 1; }
        return ce;
    }
    if (zend_string_equals_ci(name, S("parent"))) {
        zend_class_entry *ce = zend_get_current_scope();
        if (!ce || !ce->parent) { zend_throw_error("Error", "Cannot use \"parent\" when current class scope has no parent"); *err = 1; return NULL; }
        return ce->parent;
    }
    if (zend_string_equals_ci(name, S("static"))) {
        zend_class_entry *ce = called_scope();
        if (!ce) { zend_throw_error("Error", "Cannot use \"static\" when no class scope is active"); *err = 1; }
        return ce;
    }
    zend_class_entry *ce = zend_lookup_class(name, 1);
    if (!ce) {
        /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
        if (EG(php_version) >= 70000)
            zend_throw_error("Error", "Class '%s' not found", name->val);
        else
            zend_fatal("Class '%s' not found", name->val);
        *err = 1;
    }
    return ce;
}

/* ======================================================================== */
/*  module state                                                            */
/* ======================================================================== */

int zend_break_levels = 0;
zend_string *zend_goto_target = NULL;
uint32_t zend_goto_lineno = 0;
int zend_call_depth = 0;

/* Access modes, mirroring php-src's BP_VAR_*. */
#define FETCH_R     0
#define FETCH_W     1
#define FETCH_RW    2
#define FETCH_IS    3
#define FETCH_UNSET 4

#define IS_WRITE(t) ((t) == FETCH_W || (t) == FETCH_RW)
#define IS_QUIET(t) ((t) == FETCH_IS || (t) == FETCH_UNSET)

static int  do_assign(zend_ast *target, zval value, zval *result);
static int  eval_call_node(zend_ast *ast, zval *result);
static zval *fetch_lval_ex(zend_ast *ast, int type, int *err);
static int  unset_ast(zend_ast *ast);
static ex_status exec_stmt(zend_ast *ast, zval *retval);
static int  is_var_ast(zend_ast *a);
static int  fn_returns_ref(zend_function *fn);
static zend_string *fn_declaration(zend_function *fn);
static void report_bad_goto(zend_ast *root);
/* VERSIONGATE:80100 enum case objects are immutable */
static int  enum_reject_write(zend_object *obj, zend_string *name, int unsetting);
static int  bind_class_constants(zend_class_entry *ce);
static int  class_constant_value(zend_class_entry *ce, zend_string *name, zval *out);

/* ======================================================================== */
/*  value lifetime                                                          */
/* ======================================================================== */

static void zval_ptr_dtor_impl(zval *v, int depth);

/* Release one reference to an object, running __destruct when the last one
 * goes away.  php-src destroys deterministically and the tests depend on
 * exactly when the message appears. */
static void object_release(zend_object *obj)
{
    if (!obj) return;
    if (obj->refcount > 1) { obj->refcount--; return; }
    if (zend_ce_closure && obj->ce == zend_ce_closure && obj->internal &&
        ((zend_closure *)obj->internal)->running) {
        zend_fatal("Cannot destroy active lambda function");
        return;
    }
    obj->refcount = 0;
    if (obj->destructed || obj->in_destruct) return;
    obj->in_destruct = 1;
    if (obj->ce && obj->ce->destructor && !EG(exiting)) {
        zend_object *saved = EG(exception);
        EG(exception) = NULL;
        zval ret = zval_null();
        obj->refcount = 1;                 /* $this is alive inside __destruct */
        zend_call_function_ex(obj->ce->destructor, obj, obj->ce, 0, NULL, NULL, &ret, NULL);
        obj->refcount = 0;
        if (saved) { EG(exception) = saved; }
    }
    obj->destructed = 1;
    obj->in_destruct = 0;
}

static void zval_ptr_dtor_impl(zval *v, int depth)
{
    if (!v || depth > 64) return;
    switch (v->u1) {
    case IS_ARRAY: {
        HashTable *ht = v->value.arr;
        if (!ht) return;
        if (ht->refcount > 1) { ht->refcount--; return; }
        ht->refcount = 0;
        ZEND_HASH_FOREACH(ht, b) {
            zval_ptr_dtor_impl(&b->val, depth + 1);
        } ZEND_HASH_FOREACH_END();
        return;
    }
    case IS_OBJECT:
        object_release(v->value.obj);
        return;
    case IS_REFERENCE: {
        zend_reference *r = v->value.ref;
        if (!r) return;
        if (r->refcount > 1) { r->refcount--; return; }
        r->refcount = 0;
        zval_ptr_dtor_impl(&r->val, depth + 1);
        return;
    }
    default:
        return;
    }
}

static void zval_ptr_dtor(zval *v) { zval_ptr_dtor_impl(v, 0); }

/* Overwrite a storage slot with a new value, releasing whatever was there. */
static void slot_set(zval *slot, zval value)
{
    zval old = *slot;
    *slot = value;
    if (!(old.u1 == value.u1 &&
          (old.u1 == IS_ARRAY ? old.value.arr == value.value.arr
           : old.u1 == IS_OBJECT ? old.value.obj == value.value.obj : 0)))
        zval_ptr_dtor(&old);
}

/* ======================================================================== */
/*  references and copy on write                                            */
/* ======================================================================== */

zend_reference *zend_make_reference(zval *slot)
{
    if (slot->u1 == IS_REFERENCE) return slot->value.ref;
    zend_reference *r = ecalloc(1, sizeof(zend_reference));
    r->refcount = 1;
    r->val = *slot;
    slot->u1 = IS_REFERENCE;
    slot->value.ref = r;
    return r;
}

void zend_separate_slot(zval *slot)
{
    zval *v = zval_deref(slot);
    if (v->u1 == IS_ARRAY && v->value.arr->refcount > 1) zval_separate_array(v);
}

/* ======================================================================== */
/*  frames and symbol tables                                                */
/* ======================================================================== */

zend_class_entry *zend_get_called_scope(void) { return called_scope(); }


/* ======================================================================== */
/*  diagnostics                                                             */
/* ======================================================================== */

/* VERSION: undefined variable is a notice until 8.0 where it becomes a
 * warning, and the message drops its colon. */
static void undef_var_error(zend_string *name)
{
    /* VERSIONGATE:80000 else-branch is the 5.6 behaviour */
    if (EG(php_version) >= 80000)
        zend_error(E_WARNING, "Undefined variable $%s", name->val);
    else
        zend_error(E_NOTICE, "Undefined variable: %s", name->val);
}

/* VERSION: "Undefined index: k" / "Undefined offset: 3" (notice, <8.0) became
 * "Undefined array key \"k\"" / "Undefined array key 3" (warning, 8.0+). */
static void undef_index_error(const zval *key)
{
    zend_long idx = 0; zend_string *str = NULL;
    int r = array_key_normalize(key, &idx, &str);
    if (r < 0) return;
    /* VERSIONGATE:80000 else-branch is the 5.6 behaviour */
    if (EG(php_version) >= 80000) {
        if (r) zend_error(E_WARNING, "Undefined array key %lld", (long long)idx);
        else   zend_error(E_WARNING, "Undefined array key \"%s\"", str->val);
    } else {
        if (r) zend_error(E_NOTICE, "Undefined offset: %lld", (long long)idx);
        else   zend_error(E_NOTICE, "Undefined index: %s", str->val);
    }
}

/* VERSION: reading an offset of a scalar is silent before 7.4, a notice in
 * 7.4, a warning from 8.0, and 8.3 dropped the "value of type" wording. */
static void scalar_offset_notice(const zval *v)
{
    /* VERSIONGATE:70400 the guarded branch is the 5.6 behaviour */
    if (EG(php_version) < 70400) return;
    const char *t;

    switch (v->u1) {
    case IS_UNDEF: case IS_NULL: t = "null"; break;
    case IS_FALSE: case IS_TRUE: t = "bool"; break;
    case IS_LONG:  t = "int"; break;
    case IS_DOUBLE:t = "float"; break;
    default:       t = zend_zval_type_name(v); break;
    }
    /* VERSIONGATE:80000 else-branch is the 5.6 behaviour */
    zend_error(EG(php_version) >= 80000 ? E_WARNING : E_NOTICE,
               "Trying to access array offset on value of type %s", t);
}

/* ======================================================================== */
/*  object helpers                                                          */
/* ======================================================================== */

/* Call obj->name(args).  Returns -1 when the class has no such method, 0 on
 * success and 1 when an exception is pending. */
static int call_method_name(zend_object *obj, const char *name, uint32_t argc,
                            zval *argv, zval *ret)
{
    zend_function *fn = zend_lookup_method(obj->ce, S(name));
    if (!fn) return -1;
    return zend_call_function_ex(fn, obj, obj->ce, argc, argv, NULL, ret, NULL);
}

static int class_has_method(zend_class_entry *ce, const char *name)
{
    return ce && zend_lookup_method(ce, S(name)) != NULL;
}

static int object_is_arrayaccess(zend_object *obj)
{
    return obj && class_has_method(obj->ce, "offsetget");
}


/* ======================================================================== */
/*  magic property guards                                                   */
/* ======================================================================== */
/* php-src keeps a per-object guard so that a __get/__set/__isset/__unset that
 * touches the very property it was called for falls back to the plain access
 * instead of recursing for ever. */

static HashTable *magic_guards(void)
{
    static HashTable *t = NULL;
    if (!t) t = zend_hash_new();
    return t;
}

static zend_string *guard_key(zend_object *o, const char *kind, zend_string *name)
{
    return zend_strpprintf("%u:%s:%s", o->handle, kind, name->val);
}

static int magic_guard_enter(zend_object *o, const char *kind, zend_string *name)
{
    zend_string *k = guard_key(o, kind, name);
    if (zend_hash_find(magic_guards(), k)) return 0;
    zend_hash_update(magic_guards(), k, zval_bool(1));
    return 1;
}

static void magic_guard_leave(zend_object *o, const char *kind, zend_string *name)
{
    zend_hash_del(magic_guards(), guard_key(o, kind, name));
}

/* zend_read_property() in Zend/zend_object.c has no guard, so the evaluator
 * does its own guarded read. */
static zval *read_property_guarded(zend_object *obj, zend_string *name, int silent, zval *tmp)
{
    int inaccessible = 0;
    zval *slot = zend_object_get_prop_slot(obj, name, &inaccessible);
    if (slot && slot->u1 != IS_UNDEF) return slot;

    if (obj->ce && obj->ce->__get && magic_guard_enter(obj, "get", name)) {
        zval arg = zval_zstr(name), ret = zval_null();
        int rc = zend_call_function_ex(obj->ce->__get, obj, obj->ce, 1, &arg, NULL, &ret, NULL);
        magic_guard_leave(obj, "get", name);
        if (rc) return NULL;
        if (tmp) { *tmp = ret; return tmp; }
        return NULL;
    }
    if (inaccessible) {
        zend_property_info *info = zend_find_property_info(obj->ce, name);
        const char *vis = (info && (info->flags & ZEND_ACC_PRIVATE)) ? "private" : "protected";
        /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
        if (EG(php_version) >= 70000)
            zend_throw_error("Error", "Cannot access %s property %s::$%s", vis,
                             obj->ce->name->val, name->val);
        else
            zend_fatal("Cannot access %s property %s::$%s", vis, obj->ce->name->val, name->val);
        return NULL;
    }
    if (!silent) {
        /* VERSIONGATE:80000 else-branch is the 5.6 severity */
        zend_error(EG(php_version) >= 80000 ? E_WARNING : E_NOTICE,
                   "Undefined property: %s::$%s", obj->ce->name->val, name->val);
    }
    return NULL;
}

/* ======================================================================== */
/*  string offsets                                                          */
/* ======================================================================== */

/* Normalise a string offset.  Returns 0 on success, 1 when the offset is not
 * usable (a diagnostic has been emitted). */
static int string_offset_value(const zval *key, zend_long *out, int for_write)
{
    const zval *k = zval_deref((zval *)key);
    if (k->u1 == IS_STRING) {
        zend_long l = 0; double d = 0;
        int t = is_numeric_string(k->value.str->val, k->value.str->len, &l, &d, 0);
        if (t == IS_LONG) { *out = l; return 0; }
        /* VERSION: 8.0 turned the illegal string offset warning into a
         * TypeError; before that the offset was coerced to 0. */
        /* VERSIONGATE:80000 else-branch is the 5.6 behaviour */
        if (EG(php_version) >= 80000) {
            zend_throw_error(for_write ? "Error" : "TypeError",
                             "Cannot access offset of type string on string");
            return 1;
        }
        zend_error(E_WARNING, "Illegal string offset '%s'", k->value.str->val);
        *out = zval_get_long(k);
        return 0;
    }
    switch (k->u1) {
    case IS_LONG: *out = k->value.lval; return 0;
    case IS_UNDEF: case IS_NULL: case IS_FALSE: *out = 0; return 0;
    case IS_TRUE: *out = 1; return 0;
    case IS_DOUBLE: *out = (zend_long)k->value.dval; return 0;
    default:
        /* VERSIONGATE:80000 else-branch is the 5.6 behaviour */
        if (EG(php_version) >= 80000) {
            zend_throw_error("TypeError", "Cannot access offset of type %s on string",
                             zend_zval_type_name(k));
            return 1;
        }
        zend_error(E_WARNING, "Illegal offset type");
        return 1;
    }
}

/* ======================================================================== */
/*  reading a dimension                                                     */
/* ======================================================================== */

static int read_dim(zval *cont0, zval *key, int have_key, int type, zval *out)
{
    zval *cont = zval_deref(cont0);
    *out = zval_null();
    if (!have_key) {
        zend_fatal("Cannot use [] for reading");
        return 1;
    }
    switch (cont->u1) {
    case IS_ARRAY: {
        zval *v = array_get(cont->value.arr, key);
        if (EG(exception)) return 1;
        if (!v || v->u1 == IS_UNDEF) {
            if (!IS_QUIET(type)) undef_index_error(key);
            return 0;
        }
        *out = zval_copy(*zval_deref(v));
        return 0;
    }
    case IS_STRING: {
        zend_string *s = cont->value.str;
        zend_long off;
        if (string_offset_value(key, &off, 0)) return EG(exception) ? 1 : 0;
        /* VERSION: negative string offsets read from the end from 7.1 on. */
        zend_long real = off;
        if (real < 0) {
            /* VERSIONGATE:70100 else-branch is the 5.6 behaviour */
            if (EG(php_version) >= 70100) real += (zend_long)s->len;
        }
        if (real < 0 || (size_t)real >= s->len) {
            if (!IS_QUIET(type)) {
                /* VERSIONGATE:80000 else-branch is the 5.6 behaviour */
                if (EG(php_version) >= 80000)
                    zend_error(E_WARNING, "Uninitialized string offset %lld", (long long)off);
                else
                    zend_error(E_NOTICE, "Uninitialized string offset: %lld", (long long)off);
            }
            *out = zval_stringl("", 0);
            return 0;
        }
        *out = zval_stringl(s->val + real, 1);
        return 0;
    }
    case IS_OBJECT: {
        zend_object *obj = cont->value.obj;
        if (object_is_arrayaccess(obj)) {
            if (type == FETCH_IS) {
                zval a = zval_copy(*zval_deref(key)), r = zval_null();
                if (call_method_name(obj, "offsetexists", 1, &a, &r) == 1) return 1;
                if (!zval_is_true(&r)) { *out = zval_null(); return 0; }
            }
            zval a = zval_copy(*zval_deref(key)), r = zval_null();
            int rc = call_method_name(obj, "offsetget", 1, &a, &r);
            if (rc == 1) return 1;
            *out = *zval_deref(&r);
            return 0;
        }
        /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
        if (EG(php_version) >= 70000) {
            zend_throw_error("Error", "Cannot use object of type %s as array", obj->ce->name->val);
            return 1;
        }
        zend_fatal("Cannot use object of type %s as array", obj->ce->name->val);
        return 1;
    }
    case IS_UNDEF: case IS_NULL:
    case IS_FALSE: case IS_TRUE:
    case IS_LONG: case IS_DOUBLE:
    default:
        if (!IS_QUIET(type)) scalar_offset_notice(cont);
        return 0;
    }
}

/* ======================================================================== */
/*  fetching a writable dimension slot                                      */
/* ======================================================================== */

static zval *fetch_dim_slot(zval *contslot, zval *key, int have_key, int type, int *err)
{
    zval *c = zval_deref(contslot);

    /* VERSIONGATE:70100 the guarded branch is the 5.6 behaviour */
    if (c->u1 == IS_UNDEF || c->u1 == IS_NULL || c->u1 == IS_FALSE ||
        (c->u1 == IS_STRING && c->value.str->len == 0 && EG(php_version) < 70100)) {
        /* VERSION: writing through an empty string made an array before 7.1;
         * 8.1 deprecated auto-vivifying false, 9.0 will drop it. */
        /* VERSIONGATE:80100 else-branch is the 5.6 behaviour */
        if (c->u1 == IS_FALSE && EG(php_version) >= 80100)
            zend_error(E_DEPRECATED, "Automatic conversion of false to array is deprecated");
        *c = zval_array(zend_hash_new());
    }

    if (c->u1 == IS_ARRAY) {
        zval_separate_array(c);
        HashTable *ht = c->value.arr;
        if (!have_key) {
            zval *n = zend_hash_next_index_insert(ht, zval_null());
            if (!n) {
                zend_error(E_WARNING, "Cannot add element to the array as the next element is already occupied");
                *err = 1;
                return NULL;
            }
            return n;
        }
        zend_long idx = 0; zend_string *str = NULL;
        int r = array_key_normalize(key, &idx, &str);
        if (r < 0) { *err = 1; return NULL; }
        zval *v = r ? zend_hash_index_find(ht, idx) : zend_hash_find(ht, str);
        if (!v || v->u1 == IS_UNDEF) {
            if (type != FETCH_W && !IS_QUIET(type)) undef_index_error(key);
            v = r ? zend_hash_index_update(ht, idx, zval_null())
                  : zend_hash_update(ht, str, zval_null());
        }
        return v;
    }

    if (c->u1 == IS_OBJECT) {
        zend_object *obj = c->value.obj;
        zend_function *og = zend_lookup_method(obj->ce, S("offsetget"));
        if (og && fn_returns_ref(og)) {
            /* "&offsetGet()" hands back a real reference, so the write lands */
            zval a = have_key ? zval_copy(*zval_deref(key)) : zval_null();
            zval r = zval_null();
            if (zend_call_function_ex(og, obj, obj->ce, 1, &a, NULL, &r, NULL)) { *err = 1; return NULL; }
            if (r.u1 == IS_REFERENCE) return &r.value.ref->val;
        }
        if (object_is_arrayaccess(obj)) {
            /* php-src cannot hand out a slot inside an ArrayAccess object. */
            zend_error(E_NOTICE, "Indirect modification of overloaded element of %s has no effect",
                       obj->ce->name->val);
            *err = 1;
            return NULL;
        }
        /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
        if (EG(php_version) >= 70000)
            zend_throw_error("Error", "Cannot use object of type %s as array", obj->ce->name->val);
        else
            zend_fatal("Cannot use object of type %s as array", obj->ce->name->val);
        *err = 1;
        return NULL;
    }

    if (c->u1 == IS_STRING) {
        /* the caller (assignment) deals with string offsets */
        *err = 2;
        return NULL;
    }

    /* VERSION: a scalar container is a warning before 8.0 and an Error after. */
    /* VERSIONGATE:80000 else-branch is the 5.6 behaviour */
    if (EG(php_version) >= 80000)
        zend_throw_error("Error", "Cannot use a scalar value as an array");
    else
        zend_error(E_WARNING, "Cannot use a scalar value as an array");
    *err = 1;
    return NULL;
}

/* ======================================================================== */
/*  properties                                                              */
/* ======================================================================== */

static int prop_object_from(zval *v, zend_ast *ast, zend_object **out, int type)
{
    zval *o = zval_deref(v);
    if (o->u1 == IS_OBJECT) { *out = o->value.obj; return 0; }
    if (IS_QUIET(type)) { *out = NULL; return 0; }
    (void)ast;
    if (IS_WRITE(type)) {
        /* VERSION: 5.6/7.x auto-vivify an stdClass from null/false/"";
         * 8.0 made every case an Error. */
        if (o->u1 == IS_UNDEF || o->u1 == IS_NULL || o->u1 == IS_FALSE ||
            (o->u1 == IS_STRING && o->value.str->len == 0)) {
            /* VERSIONGATE:80000 else-branch is the 5.6 behaviour */
            if (EG(php_version) >= 80000) {
                zend_throw_error("Error", "Attempt to assign property on %s",
                                 zend_zval_type_name(o));
                return 1;
            }
            /* VERSIONGATE:70400 else-branch is the 5.6 behaviour */
            if (EG(php_version) >= 70400)
                zend_error(E_WARNING, "Creating default object from empty value");
            else
                zend_error(E_WARNING, "Creating default object from empty value");
            zend_class_entry *std = zend_lookup_class(S("stdClass"), 0);
            zend_object *no = zend_object_new(std);
            *o = zval_object(no);
            *out = no;
            return 0;
        }
        /* VERSIONGATE:80000 else-branch is the 5.6 behaviour */
        if (EG(php_version) >= 80000) {
            zend_throw_error("Error", "Attempt to assign property on %s", zend_zval_type_name(o));
            return 1;
        }
        zend_error(E_WARNING, "Attempt to assign property of non-object");
        *out = NULL;
        return 1;
    }
    /* read of a property of a non-object */
    /* VERSIONGATE:80000 else-branch is the 5.6 behaviour */
    if (EG(php_version) >= 80000) {
        zend_error(E_WARNING, "Attempt to read property \"%s\" on %s", "?",
                   zend_zval_type_name(o));
    /* VERSIONGATE:70400 else-branch is the 5.6 behaviour */
    } else if (EG(php_version) >= 70400) {
        zend_error(E_NOTICE, "Trying to get property of non-object");
    } else {
        zend_error(E_NOTICE, "Trying to get property of non-object");
    }
    *out = NULL;
    return 1;
}

/* Storage slot of $obj->name for writing; NULL when __set has to be used
 * (*magic is then set) or when access is refused (*err set). */
static zval *fetch_prop_slot(zend_object *obj, zend_string *name, int type,
                             int *magic, int *err)
{
    int inaccessible = 0;
    *magic = 0;
    zval *slot = zend_object_get_prop_slot(obj, name, &inaccessible);
    if (slot && slot->u1 != IS_UNDEF) return slot;

    if (obj->ce && ((IS_WRITE(type) && obj->ce->__set) || (!IS_WRITE(type) && obj->ce->__get))) {
        *magic = 1;
        return NULL;
    }
    if (inaccessible) {
        zend_property_info *info = zend_find_property_info(obj->ce, name);
        const char *vis = (info && (info->flags & ZEND_ACC_PRIVATE)) ? "private" : "protected";
        /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
        if (EG(php_version) >= 70000)
            zend_throw_error("Error", "Cannot access %s property %s::$%s", vis,
                             obj->ce->name->val, name->val);
        else
            zend_fatal("Cannot access %s property %s::$%s", vis, obj->ce->name->val, name->val);
        *err = 1;
        return NULL;
    }
    if (IS_WRITE(type)) {
        zend_property_info *info = zend_find_property_info(obj->ce, name);
        zend_string *key = (info && !(info->flags & ZEND_ACC_STATIC)) ? info->mangled : name;
        return zend_hash_update(obj->properties, key, zval_null());
    }
    return NULL;
}

/* ======================================================================== */
/*  static properties and class constants                                   */
/* ======================================================================== */

static zval *fetch_static_prop(zend_class_entry *ce, zend_string *name, int quiet, int *err)
{
    for (zend_class_entry *c = ce; c; c = c->parent) {
        zval *pv = zend_hash_find(c->properties_info, name);
        if (!pv) continue;
        zend_property_info *info = (zend_property_info *)ZEND_ZVAL_PTR_P(pv);
        if (!(info->flags & ZEND_ACC_STATIC)) continue;
        if (!zend_check_property_access(info, zend_get_current_scope())) {
            const char *vis = (info->flags & ZEND_ACC_PRIVATE) ? "private" : "protected";
            /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
            if (EG(php_version) >= 70000)
                zend_throw_error("Error", "Cannot access %s property %s::$%s", vis,
                                 ce->name->val, name->val);
            else
                zend_fatal("Cannot access %s property %s::$%s", vis, ce->name->val, name->val);
            *err = 1;
            return NULL;
        }
        zend_class_init_statics(info->ce);
        zval *slot = zend_hash_find(info->ce->static_members, name);
        if (!slot) slot = zend_hash_update(info->ce->static_members, name, zval_null());
        return slot;
    }
    if (!quiet) {
        /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
        if (EG(php_version) >= 70000)
            zend_throw_error("Error", "Access to undeclared static property %s::$%s",
                             ce->name->val, name->val);
        else
            zend_fatal("Access to undeclared static property: %s::$%s",
                       ce->name->val, name->val);
        *err = 1;
    }
    return NULL;
}

/* ======================================================================== */
/*  delayed dimension keys                                                  */
/* ======================================================================== */
/* php-src compiles the *base* of an assignment target into a delayed op
 * sequence but the dimension expression eagerly, so "$b[$j++] = $j" evaluates
 * $j++ before the right hand side while "$arr[$i] = ++$i" reads the plain CV
 * $i afterwards.  We reproduce that by pre-evaluating every dimension key that
 * is not a simple variable before the value expression runs. */

typedef struct { zend_ast *node; zval val; } prekey_t;
static prekey_t prekeys[64];
static int nprekeys = 0;

static int ast_is_cv(zend_ast *a)
{
    return a && a->kind == AST_VAR && a->nchild == 1 && ast_name(a->child[0]) != NULL;
}

static int prekey_collect(zend_ast *target)
{
    if (!target) return 0;
    switch (target->kind) {
    case AST_DIM:
        if (prekey_collect(target->child[0])) return 1;
        if (target->nchild > 1 && target->child[1] &&
            !ast_is_cv(target->child[1]) && target->child[1]->kind != AST_ZVAL) {
            if (nprekeys >= (int)(sizeof(prekeys) / sizeof(prekeys[0]))) return 0;
            zval v = zval_null();
            if (zend_eval_expr(target->child[1], &v)) return 1;
            prekeys[nprekeys].node = target->child[1];
            prekeys[nprekeys].val = v;
            nprekeys++;
        }
        return 0;
    case AST_PROP: case AST_NULLSAFE_PROP: case AST_STATIC_PROP:
        return prekey_collect(target->child[0]);
    default:
        return 0;
    }
}

static int prekey_lookup(zend_ast *node, int base, zval *out)
{
    for (int i = nprekeys - 1; i >= base; i--)
        if (prekeys[i].node == node) { *out = prekeys[i].val; return 1; }
    return 0;
}

static int prekey_base = 0;

/* Evaluate a dimension key, honouring anything pre-evaluated for this node. */
static int eval_dim_key(zend_ast *node, zval *out)
{
    if (prekey_lookup(node, prekey_base, out)) return 0;
    return zend_eval_expr(node, out);
}

/* ======================================================================== */
/*  l-value fetching                                                        */
/* ======================================================================== */

/* The auto-globals are visible in every scope without a "global" statement. */
static int is_superglobal(zend_string *n)
{
    static const char *sg[] = { "_SERVER", "_GET", "_POST", "_COOKIE", "_FILES",
                                "_ENV", "_REQUEST", "_SESSION", NULL };
    for (int i = 0; sg[i]; i++)
        if (n->len == strlen(sg[i]) && !memcmp(n->val, sg[i], n->len)) return 1;
    return 0;
}

static HashTable *table_for_var(zend_string *name)
{
    HashTable *tbl = cur_symbols();
    if (tbl != EG(symbol_table) && is_superglobal(name)) return EG(symbol_table);
    return tbl;
}

static int is_globals_var(zend_ast *ast)
{
    if (!ast || ast->kind != AST_VAR || ast->nchild < 1) return 0;
    zend_string *n = ast_name(ast->child[0]);
    return n && zend_string_equals(n, S("GLOBALS"));
}

static zval *fetch_var_slot(zend_ast *ast, int type, int *err)
{
    zend_string *name;
    if (eval_name(ast->child[0], &name)) { *err = 1; return NULL; }
    if (zend_string_equals(name, S("GLOBALS"))) {
        /* $GLOBALS is the global symbol table itself.  Handing it out with a
         * refcount above one means any write through the copy separates
         * instead of corrupting the real table; writes that must land in the
         * table go through the $GLOBALS['x'] special case. */
        static zval globals_zv;
        if (EG(symbol_table)->refcount < 2) EG(symbol_table)->refcount = 2;
        globals_zv = zval_array(EG(symbol_table));
        return &globals_zv;
    }
    if (zend_string_equals(name, S("this"))) {
        HashTable *tbl = cur_symbols();
        zval *v = zend_hash_find(tbl, name);
        if (v) return v;
        if (IS_QUIET(type)) return NULL;
        if (IS_WRITE(type)) { zend_fatal("Cannot re-assign $this"); *err = 1; return NULL; }
        /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
        if (EG(php_version) >= 70000) {
            zend_throw_error("Error", "Using $this when not in object context");
            *err = 1;
            return NULL;
        }
        zend_fatal("Using $this when not in object context");
        *err = 1;
        return NULL;
    }
    HashTable *tbl = table_for_var(name);
    zval *v = zend_hash_find(tbl, name);
    if (!v || v->u1 == IS_UNDEF) {
        if (IS_QUIET(type)) return NULL;
        if (type != FETCH_W) undef_var_error(name);
        if (type == FETCH_R) return NULL;
        v = zend_hash_update(tbl, name, zval_null());
    }
    return v;
}

static zval *fetch_lval_ex(zend_ast *ast, int type, int *err)
{
    if (!ast) { *err = 1; return NULL; }
    zend_set_line(ast);
    switch (ast->kind) {
    case AST_VAR:
        return fetch_var_slot(ast, type, err);

    case AST_DIM: {
        zval key = zval_null();
        int have_key = 0;
        if (ast->nchild > 1 && ast->child[1]) {
            if (eval_dim_key(ast->child[1], &key)) { *err = 1; return NULL; }
            have_key = 1;
        } else if (!IS_WRITE(type)) {
            zend_fatal("Cannot use [] for reading");
            *err = 1;
            return NULL;
        }
        /* $GLOBALS['x'] is the global symbol table itself */
        if (is_globals_var(ast->child[0])) {
            if (!have_key) { *err = 1; return NULL; }
            zend_string *k = zval_get_string(&key);
            zval *v = zend_hash_find(EG(symbol_table), k);
            if ((!v || v->u1 == IS_UNDEF) && IS_WRITE(type))
                v = zend_hash_update(EG(symbol_table), k, zval_null());
            return v;
        }
        zval *cont = fetch_lval_ex(ast->child[0], type == FETCH_RW ? FETCH_W : type, err);
        if (!cont) return NULL;
        if (IS_QUIET(type)) {
            zval *c = zval_deref(cont);
            if (type == FETCH_UNSET && c->u1 == IS_STRING) {
                /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
                if (EG(php_version) >= 70000)
                    zend_throw_error("Error", "Cannot unset string offsets");
                else
                    zend_fatal("Cannot unset string offsets");
                *err = 1;
                return NULL;
            }
            if (c->u1 == IS_ARRAY) {
                if (!have_key) return NULL;
                zval *v = array_get(c->value.arr, &key);
                return (v && v->u1 != IS_UNDEF) ? v : NULL;
            }
            return NULL;
        }
        /* *err == 2 means "the container is a string"; the caller picks the
         * wording, because php-src words it differently for a write, a
         * compound write, ++/-- and a reference. */
        return fetch_dim_slot(cont, &key, have_key, type, err);
    }

    case AST_PROP:
    case AST_NULLSAFE_PROP: {
        zval obj = zval_null();
        if (zend_eval_expr(ast->child[0], &obj)) { *err = 1; return NULL; }
        zval *o = zval_deref(&obj);
        if (o->u1 != IS_OBJECT) {
            if (IS_QUIET(type)) return NULL;
            zend_object *no = NULL;
            if (prop_object_from(&obj, ast, &no, type)) { *err = 1; return NULL; }
            if (!no) { *err = 1; return NULL; }
            /* the vivified object is not reachable from the original slot: we
             * have to write it back through the base l-value -- but only when
             * the base actually is one, never by re-running an expression */
            if (is_var_ast(ast->child[0])) {
                zval *bslot = fetch_lval_ex(ast->child[0], FETCH_W, err);
                if (bslot) slot_set(zval_deref(bslot), zval_object(no));
            }
            o = &obj;
            o->u1 = IS_OBJECT;
            o->value.obj = no;
        }
        zend_string *name;
        if (eval_name(ast->child[1], &name)) { *err = 1; return NULL; }
        zend_object *po = o->value.obj;
        /* VERSIONGATE:80100 BEGIN - an enum case is immutable */
        if (IS_WRITE(type) && enum_reject_write(po, name, 0)) { *err = 1; return NULL; }
        /* VERSIONGATE:80100 END */
        if (IS_WRITE(type) && po->ce && (po->ce->__get || po->ce->__set)) {
            /* php-src cannot hand out a slot for an overloaded property, so a
             * reference or an indirect write reports and does nothing */
            int inacc = 0;
            zval *have = zend_object_get_prop_slot(po, name, &inacc);
            if ((!have || have->u1 == IS_UNDEF) && !inacc) {
                zend_error(E_NOTICE, "Indirect modification of overloaded property %s::$%s has no effect",
                           po->ce->name->val, name->val);
                *err = 1;
                return NULL;
            }
        }
        int magic = 0;
        zval *slot = fetch_prop_slot(po, name, type, &magic, err);
        if (!slot && magic && !IS_QUIET(type)) {
            zend_error(E_NOTICE, "Indirect modification of overloaded property %s::$%s has no effect",
                       po->ce->name->val, name->val);
            *err = 1;
        }
        return slot;
    }

    case AST_STATIC_PROP: {
        zend_class_entry *ce = zend_resolve_class_ast(ast->child[0], err);
        if (!ce) { *err = 1; return NULL; }
        zend_string *name;
        if (eval_name(ast->child[1], &name)) { *err = 1; return NULL; }
        return fetch_static_prop(ce, name, IS_QUIET(type), err);
    }

    case AST_LIST:
        if (ast->nchild == 1) return fetch_lval_ex(ast->child[0], type, err);
        break;
    default:
        break;
    }
    if (IS_WRITE(type)) {
        /* php-src allows a property or dimension write through a temporary
         * ("foo()->a = 1"): the write happens, it just goes nowhere. */
        zval *tmp = ecalloc(1, sizeof(zval));
        *tmp = zval_null();
        if (zend_eval_expr(ast, tmp)) { *err = 1; return NULL; }
        return tmp;
    }
    if (!IS_QUIET(type)) {
        zend_fatal("Cannot use a temporary expression in write context");
        *err = 1;
    }
    return NULL;
}

zval *zend_get_lval(zend_ast *ast, int create)
{
    int err = 0;
    return fetch_lval_ex(ast, create ? FETCH_W : FETCH_R, &err);
}

/* ======================================================================== */
/*  string offset assignment                                                */
/* ======================================================================== */

static int assign_string_offset(zval *strslot, zval *key, zval value, zval *result)
{
    zend_long off = 0;
    if (string_offset_value(key, &off, 1)) return EG(exception) ? 1 : 0;
    zend_string *s = strslot->value.str;
    zend_long orig = off;
    if (off < 0) {
        /* VERSION: negative offsets became legal for writing in 7.1. */
        /* VERSIONGATE:70100 else-branch is the 5.6 behaviour */
        if (EG(php_version) >= 70100) {
            off += (zend_long)s->len;
            if (off < 0) {
                /* VERSIONGATE:80000 else-branch is the 5.6 behaviour */
                if (EG(php_version) >= 80000) {
                    zend_throw_error("Error", "Illegal string offset %lld", (long long)orig);
                    return 1;
                }
                zend_error(E_WARNING, "Illegal string offset:  %lld", (long long)orig);
                return 0;
            }
        } else {
            zend_error(E_WARNING, "Illegal string offset:  %lld", (long long)orig);
            return 0;
        }
    }
    zend_string *vs = zval_get_string(&value);
    if (EG(exception)) return 1;
    if (vs->len == 0) {
        /* VERSION: 7.1 started rejecting the empty string; 8.0 made it fatal. */
        /* VERSIONGATE:80000 else-branch is the 5.6 behaviour */
        if (EG(php_version) >= 80000) {
            zend_throw_error("Error", "Cannot assign an empty string to a string offset");
            return 1;
        }
        /* VERSIONGATE:70100 else-branch is the 5.6 behaviour */
        if (EG(php_version) >= 70100) {
            zend_error(E_WARNING, "Cannot assign an empty string to a string offset");
            return 0;
        }
        vs = zend_string_init("\0", 1);
    /* VERSIONGATE:80000 else-branch is the 5.6 behaviour */
    } else if (vs->len > 1 && EG(php_version) >= 80000) {
        zend_error(E_WARNING, "Only the first byte will be assigned to the string offset");
    }
    size_t newlen = (size_t)off >= s->len ? (size_t)off + 1 : s->len;
    zend_string *ns = zend_string_alloc(newlen);
    memcpy(ns->val, s->val, s->len);
    if (newlen > s->len) memset(ns->val + s->len, ' ', newlen - s->len);
    ns->val[off] = vs->val[0];
    ns->h = 0;
    *strslot = zval_zstr(ns);
    if (result) *result = zval_stringl(vs->val, 1);
    return 0;
}

/* ======================================================================== */
/*  assignment                                                              */
/* ======================================================================== */

static int list_assign(zend_ast *target, zval value, zval *result);

static int assign_into_slot(zval *slot, zval value, zval *result)
{
    zval *d = slot;
    if (d->u1 == IS_REFERENCE) d = &d->value.ref->val;
    slot_set(d, value);
    if (result) *result = zval_copy(*d);
    return 0;
}

/* Is this AST_PROP / AST_DIM node rooted in a string offset ("$s[0]->x")? */
static int string_offset_base(zend_ast *node)
{
    zend_ast *b = node ? node->child[0] : NULL;
    if (!b || b->kind != AST_DIM || b->nchild < 1) return 0;
    int e = 0;
    zval *bs = fetch_lval_ex(b->child[0], FETCH_IS, &e);
    if (!bs) return 0;
    zval *d = zval_deref(bs);
    return d->u1 == IS_STRING && d->value.str->len > 0;
}

static int do_assign(zend_ast *target, zval value, zval *result)
{
    int err = 0;
    switch (target->kind) {
    case AST_VAR: {
        zval *slot = fetch_var_slot(target, FETCH_W, &err);
        if (!slot) return 1;
        return assign_into_slot(slot, value, result);
    }
    case AST_DIM: {
        zval key = zval_null();
        int have_key = 0;
        if (target->nchild > 1 && target->child[1]) {
            if (eval_dim_key(target->child[1], &key)) return 1;
            have_key = 1;
        }
        if (is_globals_var(target->child[0])) {
            if (!have_key) return 1;
            zend_string *k = zval_get_string(&key);
            zval *v = zend_hash_find(EG(symbol_table), k);
            if (!v) v = zend_hash_update(EG(symbol_table), k, zval_null());
            return assign_into_slot(v, value, result);
        }
        zval *cont = fetch_lval_ex(target->child[0], FETCH_W, &err);
        if (!cont) return 1;
        zval *c = zval_deref(cont);
        /* VERSIONGATE:70100 the guarded branch is the 5.6 behaviour */
        if (c->u1 == IS_STRING && !(c->value.str->len == 0 && EG(php_version) < 70100)) {
            if (!have_key) {
                /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
                if (EG(php_version) >= 70000) {
                    zend_throw_error("Error", "[] operator not supported for strings");
                    return 1;
                }
                zend_fatal("[] operator not supported for strings");
                return 1;
            }
            return assign_string_offset(c, &key, value, result);
        }
        if (c->u1 == IS_OBJECT && object_is_arrayaccess(c->value.obj)) {
            zval args[2];
            args[0] = have_key ? zval_copy(*zval_deref(&key)) : zval_null();
            args[1] = value;
            zval r = zval_null();
            if (call_method_name(c->value.obj, "offsetset", 2, args, &r) == 1) return 1;
            if (result) *result = zval_copy(value);
            return 0;
        }
        zval *slot = fetch_dim_slot(cont, &key, have_key, FETCH_W, &err);
        if (!slot) return 1;
        return assign_into_slot(slot, value, result);
    }
    case AST_PROP:
    case AST_NULLSAFE_PROP: {
        if (string_offset_base(target)) {
            zend_fatal("Cannot use string offset as an array");
            return 1;
        }
        zval obj = zval_null();
        if (zend_eval_expr(target->child[0], &obj)) return 1;
        zval *o = zval_deref(&obj);
        if (o->u1 != IS_OBJECT) {
            zend_object *no = NULL;
            if (prop_object_from(&obj, target, &no, FETCH_W)) return EG(exception) ? 1 : 0;
            if (!no) return 0;
            if (is_var_ast(target->child[0])) {
                zval *bslot = fetch_lval_ex(target->child[0], FETCH_W, &err);
                if (bslot) slot_set(zval_deref(bslot), zval_object(no));
            }
            o = &obj;
            o->u1 = IS_OBJECT;
            o->value.obj = no;
        }
        zend_string *name;
        if (eval_name(target->child[1], &name)) return 1;
        if (zend_ce_closure && o->value.obj->ce == zend_ce_closure) {
            zend_error(E_RECOVERABLE_ERROR, "Closure object cannot have properties");
            return 0;
        }
        /* VERSIONGATE:80100 BEGIN - an enum case is immutable */
        if (enum_reject_write(o->value.obj, name, 0)) return 1;
        /* VERSIONGATE:80100 END */
        int magic = 0;
        zval *slot = fetch_prop_slot(o->value.obj, name, FETCH_W, &magic, &err);
        if (slot) return assign_into_slot(slot, value, result);
        if (err) return 1;
        if (magic) {
            zend_object *ob = o->value.obj;
            if (!magic_guard_enter(ob, "set", name)) {
                /* __set is already running for this property: php-src falls
                 * back to the plain write, which does nothing at all when the
                 * declared property is not accessible from here */
                int inacc = 0;
                zend_object_get_prop_slot(ob, name, &inacc);
                if (!inacc) zend_hash_update(ob->properties, name, value);
                if (result) *result = zval_copy(value);
                return 0;
            }
            zval args[2]; args[0] = zval_zstr(name); args[1] = value;
            zval r = zval_null();
            int rc = zend_call_function_ex(ob->ce->__set, ob, ob->ce, 2, args, NULL, &r, NULL);
            magic_guard_leave(ob, "set", name);
            if (rc) return 1;
            if (result) *result = zval_copy(value);
            return 0;
        }
        return 1;
    }
    case AST_STATIC_PROP: {
        zval *slot = fetch_lval_ex(target, FETCH_W, &err);
        if (!slot) return 1;
        return assign_into_slot(slot, value, result);
    }
    case AST_LIST_ASSIGN:
    case AST_ARRAY:
        return list_assign(target, value, result);
    default:
        /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
        if (EG(php_version) >= 70000)
            zend_throw_error("Error", "Cannot assign to this expression");
        else
            zend_fatal("Cannot re-assign this expression");
        return 1;
    }
}

/* ======================================================================== */
/*  list() destructuring                                                    */
/* ======================================================================== */

static int bind_reference(zend_ast *target, zval *srcslot, zval *result);

static int list_assign(zend_ast *target, zval value, zval *result)
{
    zval *src = zval_deref(&value);
    uint32_t n = target->nchild;

    if (src->u1 == IS_STRING) {
        /* VERSION: 5.6 filled the targets from the string, 7.0 dropped it. */
        /* VERSIONGATE:70000 the guarded branch is the 5.6 behaviour */
        if (EG(php_version) < 70000) {
            for (uint32_t i = 0; i < n; i++) {
                zend_ast *e = target->child[i];
                if (!e || !e->child[0]) continue;
                zval ch;
                if ((size_t)i < src->value.str->len) ch = zval_stringl(src->value.str->val + i, 1);
                else ch = zval_stringl("", 0);
                if (do_assign(e->child[0], ch, NULL)) return 1;
            }
            if (result) *result = zval_copy(value);
            return 0;
        }
        for (uint32_t i = 0; i < n; i++) {
            zend_ast *e = target->child[i];
            if (!e || !e->child[0]) continue;
            if (do_assign(e->child[0], zval_null(), NULL)) return 1;
        }
        if (result) *result = zval_copy(value);
        return 0;
    }

    int is_obj_aa = (src->u1 == IS_OBJECT && object_is_arrayaccess(src->value.obj));
    if (src->u1 != IS_ARRAY && !is_obj_aa) {
        for (uint32_t i = 0; i < n; i++) {
            zend_ast *e = target->child[i];
            if (!e || !e->child[0]) continue;
            if (do_assign(e->child[0], zval_null(), NULL)) return 1;
        }
        if (result) *result = zval_copy(value);
        return 0;
    }

    /* VERSION: 5.6 assigns the targets right to left, 7.0 left to right. */
    /* VERSIONGATE:70000 the guarded branch is the 5.6 behaviour */
    int reverse = EG(php_version) < 70000;
    zend_long autoidx = 0;
    /* pre-compute the automatic indices so the reverse walk agrees with them */
    zend_long *idxs = ecalloc(n ? n : 1, sizeof(zend_long));
    for (uint32_t i = 0; i < n; i++) {
        zend_ast *e = target->child[i];
        idxs[i] = (e && e->nchild > 1 && e->child[1]) ? -1 : autoidx++;
    }

    for (uint32_t step = 0; step < n; step++) {
        uint32_t i = reverse ? (n - 1 - step) : step;
        zend_ast *e = target->child[i];
        if (!e || !e->child[0]) continue;
        zval key;
        if (idxs[i] >= 0) key = zval_long(idxs[i]);
        else if (zend_eval_expr(e->child[1], &key)) { efree(idxs); return 1; }

        zval elem = zval_null();
        if (is_obj_aa) {
            zval a = zval_copy(*zval_deref(&key)), r = zval_null();
            if (call_method_name(src->value.obj, "offsetget", 1, &a, &r) == 1) { efree(idxs); return 1; }
            elem = *zval_deref(&r);
        }

        if (e->attr & ZEND_ARRAY_ELEM_BY_REF) {
            /* VERSION: list() by reference is 7.3+ */
            if (src->u1 != IS_ARRAY) { efree(idxs); return 1; }
            int err = 0;
            zval *cslot = &value;
            zval_separate_array(zval_deref(cslot));
            zval *eslot = fetch_dim_slot(cslot, &key, 1, FETCH_W, &err);
            if (!eslot) { efree(idxs); return 1; }
            if (bind_reference(e->child[0], eslot, NULL)) { efree(idxs); return 1; }
            continue;
        }

        if (!is_obj_aa) {
            zval *v = array_get(src->value.arr, &key);
            if (EG(exception)) { efree(idxs); return 1; }
            if (!v || v->u1 == IS_UNDEF) {
                undef_index_error(&key);
                elem = zval_null();
            } else {
                elem = zval_copy(*zval_deref(v));
            }
        }

        zend_ast *sub = e->child[0];
        if (sub->kind == AST_LIST_ASSIGN || sub->kind == AST_ARRAY) {
            if (list_assign(sub, elem, NULL)) { efree(idxs); return 1; }
        } else if (do_assign(sub, elem, NULL)) { efree(idxs); return 1; }
    }
    efree(idxs);
    if (result) *result = zval_copy(value);
    return 0;
}

/* ======================================================================== */
/*  reference binding                                                       */
/* ======================================================================== */

/* Make *target share the reference box that srcslot holds (creating it). */
static int bind_reference(zend_ast *target, zval *srcslot, zval *result)
{
    zend_reference *r = zend_make_reference(srcslot);
    int err = 0;
    zval rv; rv.u1 = IS_REFERENCE; rv.value.ref = r;

    switch (target->kind) {
    case AST_VAR: {
        zend_string *name;
        if (eval_name(target->child[0], &name)) return 1;
        if (zend_string_equals(name, S("this"))) { zend_fatal("Cannot re-assign $this"); return 1; }
        HashTable *tbl = cur_symbols();
        zval *slot = zend_hash_find(tbl, name);
        if (slot) { zval old = *slot; *slot = rv; r->refcount++; zval_ptr_dtor(&old); }
        else { r->refcount++; zend_hash_update(tbl, name, rv); }
        break;
    }
    case AST_DIM: {
        zval key = zval_null();
        int have_key = 0;
        if (target->nchild > 1 && target->child[1]) {
            if (eval_dim_key(target->child[1], &key)) return 1;
            have_key = 1;
        }
        zval *cont = fetch_lval_ex(target->child[0], FETCH_W, &err);
        if (!cont) return 1;
        if (zval_deref(cont)->u1 == IS_STRING) {
            zend_fatal("Cannot create references to/from string offsets");
            return 1;
        }
        zval *slot = fetch_dim_slot(cont, &key, have_key, FETCH_W, &err);
        if (!slot) return 1;
        zval old = *slot; *slot = rv; r->refcount++; zval_ptr_dtor(&old);
        break;
    }
    case AST_PROP: case AST_NULLSAFE_PROP: case AST_STATIC_PROP: {
        zval *slot = fetch_lval_ex(target, FETCH_W, &err);
        if (!slot) return 1;
        zval old = *slot; *slot = rv; r->refcount++; zval_ptr_dtor(&old);
        break;
    }
    default:
        /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
        if (EG(php_version) >= 70000)
            zend_throw_error("Error", "Cannot assign by reference to this expression");
        else
            zend_fatal("Cannot assign by reference");
        return 1;
    }
    if (result) *result = zval_copy(r->val);
    return 0;
}

/* ======================================================================== */
/*  increment / decrement                                                   */
/* ======================================================================== */

static zend_string *string_increment(zend_string *s)
{
    enum { L_NONE, L_NUM, L_UPPER, L_LOWER };
    size_t len = s->len;
    if (len == 0) return zend_string_initz("1");
    zend_string *t = zend_string_init(s->val, len);
    char *v = t->val;
    int carry = 0, last = L_NONE;
    size_t pos = len - 1;
    while (1) {
        char ch = v[pos];
        if (ch >= 'a' && ch <= 'z') {
            if (ch == 'z') { v[pos] = 'a'; carry = 1; } else { v[pos]++; carry = 0; }
            last = L_LOWER;
        } else if (ch >= 'A' && ch <= 'Z') {
            if (ch == 'Z') { v[pos] = 'A'; carry = 1; } else { v[pos]++; carry = 0; }
            last = L_UPPER;
        } else if (ch >= '0' && ch <= '9') {
            if (ch == '9') { v[pos] = '0'; carry = 1; } else { v[pos]++; carry = 0; }
            last = L_NUM;
        } else {
            carry = 0;
            break;
        }
        if (!carry || pos == 0) break;
        pos--;
    }
    if (carry) {
        zend_string *n = zend_string_alloc(len + 1);
        memcpy(n->val + 1, t->val, len);
        n->val[0] = last == L_NUM ? '1' : last == L_UPPER ? 'A' : 'a';
        n->val[len + 1] = '\0';
        return n;
    }
    t->h = 0;
    return t;
}

static int string_is_alnum_incrementable(zend_string *s)
{
    for (size_t i = 0; i < s->len; i++) {
        char c = s->val[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
            return 0;
    }
    return 1;
}

static void increment_zval(zval *v)
{
    switch (v->u1) {
    case IS_LONG:
        if (v->value.lval == ZEND_LONG_MAX) *v = zval_double((double)ZEND_LONG_MAX + 1);
        else v->value.lval++;
        return;
    case IS_DOUBLE: v->value.dval += 1; return;
    case IS_UNDEF: case IS_NULL: *v = zval_long(1); return;
    case IS_STRING: {
        zend_string *s = v->value.str;
        if (s->len == 0) { *v = zval_string("1"); return; }
        zend_long l = 0; double d = 0;
        switch (is_numeric_string(s->val, s->len, &l, &d, 0)) {
        case IS_LONG:
            if (l == ZEND_LONG_MAX) *v = zval_double((double)ZEND_LONG_MAX + 1);
            else *v = zval_long(l + 1);
            return;
        case IS_DOUBLE: *v = zval_double(d + 1); return;
        default:
            /* VERSION: 8.3 deprecated incrementing a non-alphanumeric string */

            *v = zval_zstr(string_increment(s));
            return;
        }
    }
    case IS_FALSE: case IS_TRUE:
        /* VERSION: 8.3 warns that ++ on a bool does nothing */

        return;
    default:
        return;
    }
}

static void decrement_zval(zval *v)
{
    switch (v->u1) {
    case IS_LONG:
        if (v->value.lval == ZEND_LONG_MIN) *v = zval_double((double)ZEND_LONG_MIN - 1);
        else v->value.lval--;
        return;
    case IS_DOUBLE: v->value.dval -= 1; return;
    case IS_STRING: {
        zend_string *s = v->value.str;
        if (s->len == 0) { *v = zval_long(-1); return; }
        zend_long l = 0; double d = 0;
        switch (is_numeric_string(s->val, s->len, &l, &d, 0)) {
        case IS_LONG:
            if (l == ZEND_LONG_MIN) *v = zval_double((double)ZEND_LONG_MIN - 1);
            else *v = zval_long(l - 1);
            return;
        case IS_DOUBLE: *v = zval_double(d - 1); return;
        default:
            /* VERSION: decrementing a non-numeric string is a no-op; 8.3 made
             * it a deprecation and 9.0 will make it work like ++. */

            return;
        }
    }
    case IS_UNDEF: case IS_NULL:
        /* VERSION: 8.3 warns that -- on null does nothing */

        return;
    case IS_FALSE: case IS_TRUE:

        return;
    default:
        return;
    }
}

/* ======================================================================== */
/*  isset / empty / unset                                                   */
/* ======================================================================== */

static int isset_prop(zend_object *obj, zend_string *name, int want_not_empty, int *res)
{
    int inaccessible = 0;
    zval *slot = zend_object_get_prop_slot(obj, name, &inaccessible);
    if (slot && slot->u1 != IS_UNDEF) {
        zval *d = zval_deref(slot);
        *res = want_not_empty ? zval_is_true(d) : (d->u1 > IS_NULL);
        return 0;
    }
    if (obj->ce && obj->ce->__isset && magic_guard_enter(obj, "isset", name)) {
        zval a = zval_zstr(name), r = zval_null();
        int rc = zend_call_function_ex(obj->ce->__isset, obj, obj->ce, 1, &a, NULL, &r, NULL);
        magic_guard_leave(obj, "isset", name);
        if (rc) return 1;
        int ok = zval_is_true(&r);
        if (ok && want_not_empty && obj->ce->__get) {
            zval a2 = zval_zstr(name), r2 = zval_null();
            if (zend_call_function_ex(obj->ce->__get, obj, obj->ce, 1, &a2, NULL, &r2, NULL)) return 1;
            *res = zval_is_true(zval_deref(&r2));
            return 0;
        }
        *res = ok;
        return 0;
    }
    *res = 0;
    return 0;
}

static int isset_expr(zend_ast *ast, int want_not_empty, int *res)
{
    int err = 0;
    *res = 0;
    if (!ast) return 0;
    zend_set_line(ast);
    switch (ast->kind) {
    case AST_VAR: {
        zval *slot = fetch_var_slot(ast, FETCH_IS, &err);
        if (err) return 1;
        if (!slot) { *res = 0; return 0; }
        zval *d = zval_deref(slot);
        *res = want_not_empty ? zval_is_true(d) : (d->u1 > IS_NULL);
        return 0;
    }
    case AST_DIM: {
        zval cont = zval_null();
        /* a missing base must stay silent */
        if (ast->child[0]->kind == AST_VAR || ast->child[0]->kind == AST_DIM ||
            ast->child[0]->kind == AST_PROP || ast->child[0]->kind == AST_STATIC_PROP ||
            ast->child[0]->kind == AST_NULLSAFE_PROP) {
            zval *cs = fetch_lval_ex(ast->child[0], FETCH_IS, &err);
            if (err) return 1;
            if (!cs) { *res = 0; return 0; }
            cont = *zval_deref(cs);
        } else if (zend_eval_expr(ast->child[0], &cont)) return 1;

        if (!(ast->nchild > 1 && ast->child[1])) { *res = 0; return 0; }
        zval key = zval_null();
        if (zend_eval_expr(ast->child[1], &key)) return 1;
        zval *c = zval_deref(&cont);
        if (c->u1 == IS_ARRAY) {
            zval *v = array_get(c->value.arr, &key);
            if (EG(exception)) { EG(exception) = NULL; *res = 0; return 0; }
            if (!v || v->u1 == IS_UNDEF) { *res = 0; return 0; }
            zval *d = zval_deref(v);
            *res = want_not_empty ? zval_is_true(d) : (d->u1 > IS_NULL);
            return 0;
        }
        if (c->u1 == IS_STRING) {
            zval *k = zval_deref(&key);
            zend_long off;
            if (k->u1 == IS_STRING) {
                zend_long l = 0; double d2 = 0;
                if (is_numeric_string(k->value.str->val, k->value.str->len, &l, &d2, 0) != IS_LONG) {
                    *res = 0;
                    return 0;
                }
                off = l;
            } else off = zval_get_long(k);
            /* VERSIONGATE:70100 else-branch is the 5.6 behaviour */
            if (off < 0 && EG(php_version) >= 70100) off += (zend_long)c->value.str->len;
            if (off < 0 || (size_t)off >= c->value.str->len) { *res = 0; return 0; }
            *res = want_not_empty ? (c->value.str->val[off] != '0') : 1;
            return 0;
        }
        if (c->u1 == IS_OBJECT && object_is_arrayaccess(c->value.obj)) {
            zval a = zval_copy(*zval_deref(&key)), r = zval_null();
            if (call_method_name(c->value.obj, "offsetexists", 1, &a, &r) == 1) return 1;
            if (!zval_is_true(&r)) { *res = 0; return 0; }
            if (want_not_empty) {
                zval a2 = zval_copy(*zval_deref(&key)), r2 = zval_null();
                if (call_method_name(c->value.obj, "offsetget", 1, &a2, &r2) == 1) return 1;
                *res = zval_is_true(zval_deref(&r2));
            } else *res = 1;
            return 0;
        }
        *res = 0;
        return 0;
    }
    case AST_PROP: case AST_NULLSAFE_PROP: {
        zval obj = zval_null();
        if (ast->child[0]->kind == AST_VAR || ast->child[0]->kind == AST_DIM ||
            ast->child[0]->kind == AST_PROP || ast->child[0]->kind == AST_NULLSAFE_PROP) {
            zval *cs = fetch_lval_ex(ast->child[0], FETCH_IS, &err);
            if (err) return 1;
            if (!cs) { *res = 0; return 0; }
            obj = *zval_deref(cs);
        } else if (zend_eval_expr(ast->child[0], &obj)) return 1;
        zval *o = zval_deref(&obj);
        if (o->u1 != IS_OBJECT) { *res = 0; return 0; }
        zend_string *name;
        if (eval_name(ast->child[1], &name)) return 1;
        return isset_prop(o->value.obj, name, want_not_empty, res);
    }
    case AST_STATIC_PROP: {
        zend_class_entry *ce = zend_resolve_class_ast(ast->child[0], &err);
        if (err || !ce) { if (EG(exception)) EG(exception) = NULL; *res = 0; return 0; }
        zend_string *name;
        if (eval_name(ast->child[1], &name)) return 1;
        zval *slot = fetch_static_prop(ce, name, 1, &err);
        if (!slot) { *res = 0; return 0; }
        zval *d = zval_deref(slot);
        *res = want_not_empty ? zval_is_true(d) : (d->u1 > IS_NULL);
        return 0;
    }
    default: {
        zval v = zval_null();
        if (zend_eval_expr(ast, &v)) return 1;
        zval *d = zval_deref(&v);
        *res = want_not_empty ? zval_is_true(d) : (d->u1 > IS_NULL);
        return 0;
    }
    }
}

static int unset_ast(zend_ast *ast)
{
    int err = 0;
    zend_set_line(ast);
    switch (ast->kind) {
    case AST_VAR: {
        zend_string *name;
        if (eval_name(ast->child[0], &name)) return 1;
        HashTable *tbl = table_for_var(name);
        zval *slot = zend_hash_find(tbl, name);
        if (slot) { zval old = *slot; zend_hash_del(tbl, name); zval_ptr_dtor(&old); }
        return 0;
    }
    case AST_DIM: {
        if (!(ast->nchild > 1 && ast->child[1])) { zend_fatal("Cannot use [] for unsetting"); return 1; }
        zval key = zval_null();
        if (zend_eval_expr(ast->child[1], &key)) return 1;
        if (is_globals_var(ast->child[0])) {
            zend_string *k = zval_get_string(&key);
            zend_hash_del(EG(symbol_table), k);
            return 0;
        }
        zval *cont = fetch_lval_ex(ast->child[0], FETCH_UNSET, &err);
        if (err) return 1;
        if (!cont) return 0;
        zval *c = zval_deref(cont);
        if (c->u1 == IS_ARRAY) {
            zval_separate_array(c);
            zend_long idx = 0; zend_string *str = NULL;
            int r = array_key_normalize(&key, &idx, &str);
            if (r < 0) return EG(exception) ? 1 : 0;
            zval *old = r ? zend_hash_index_find(c->value.arr, idx) : zend_hash_find(c->value.arr, str);
            zval saved = old ? *old : zval_null();
            if (r) zend_hash_index_del(c->value.arr, idx);
            else zend_hash_del(c->value.arr, str);
            if (old) zval_ptr_dtor(&saved);
            return 0;
        }
        if (c->u1 == IS_OBJECT && object_is_arrayaccess(c->value.obj)) {
            zval a = zval_copy(*zval_deref(&key)), r = zval_null();
            if (call_method_name(c->value.obj, "offsetunset", 1, &a, &r) == 1) return 1;
            return 0;
        }
        if (c->u1 == IS_STRING) {
            /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
            if (EG(php_version) >= 70000)
                zend_throw_error("Error", "Cannot unset string offsets");
            else
                zend_fatal("Cannot unset string offsets");
            return 1;
        }
        return 0;
    }
    case AST_PROP: case AST_NULLSAFE_PROP: {
        zval obj = zval_null();
        if (zend_eval_expr(ast->child[0], &obj)) return 1;
        zval *o = zval_deref(&obj);
        if (o->u1 != IS_OBJECT) return 0;
        zend_string *name;
        if (eval_name(ast->child[1], &name)) return 1;
        zend_object *ob = o->value.obj;
        /* VERSIONGATE:80100 BEGIN - an enum case is immutable */
        if (enum_reject_write(ob, name, 1)) return 1;
        /* VERSIONGATE:80100 END */
        int inaccessible = 0;
        zval *slot = zend_object_get_prop_slot(ob, name, &inaccessible);
        if (slot && slot->u1 != IS_UNDEF) {
            zend_property_info *info = zend_find_property_info(ob->ce, name);
            zend_string *key = (info && !(info->flags & ZEND_ACC_STATIC)) ? info->mangled : name;
            zval old = *slot;
            zend_hash_del(ob->properties, key);
            zval_ptr_dtor(&old);
            return 0;
        }
        if (ob->ce && ob->ce->__unset && magic_guard_enter(ob, "unset", name)) {
            zval a = zval_zstr(name), r = zval_null();
            int rc = zend_call_function_ex(ob->ce->__unset, ob, ob->ce, 1, &a, NULL, &r, NULL);
            magic_guard_leave(ob, "unset", name);
            return rc;
        }
        return 0;
    }
    case AST_STATIC_PROP:
        /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
        if (EG(php_version) >= 70000)
            zend_throw_error("Error", "Attempt to unset static property");
        else
            zend_fatal("Attempt to unset static property");
        return 1;
    default:
        zend_fatal("Cannot unset this expression");
        return 1;
    }
}

/* ======================================================================== */
/*  constants                                                               */
/* ======================================================================== */

static int eval_const_name(zend_string *name, uint32_t attr, zval *result)
{
    zval *v = zend_hash_find(EG(constants), name);
    if (v) { *result = zval_copy(*v); return 0; }

    /* An unqualified name inside a namespace falls back to the global one. */
    zend_string *tail = name;
    if (attr != ZEND_NAME_FQ) {
        const char *bs = NULL;
        for (size_t i = name->len; i > 0; i--)
            if (name->val[i - 1] == '\\') { bs = name->val + i; break; }
        if (bs) {
            tail = zend_string_initz(bs);
            v = zend_hash_find(EG(constants), tail);
            if (v) { *result = zval_copy(*v); return 0; }
        }
    }
    if (zend_string_equals_ci(tail, S("true")))  { *result = zval_bool(1); return 0; }
    if (zend_string_equals_ci(tail, S("false"))) { *result = zval_bool(0); return 0; }
    if (zend_string_equals_ci(tail, S("null")))  { *result = zval_null(); return 0; }

    /* VERSION: notice < 7.2, warning 7.2-7.4, Error from 8.0. */
    /* VERSIONGATE:80000 else-branch is the 5.6 behaviour */
    if (EG(php_version) >= 80000) {
        zend_throw_error("Error", "Undefined constant \"%s\"", name->val);
        return 1;
    }
    /* Only a bare, unqualified name falls back to its own text; a qualified or
     * fully qualified one is fatal in php-src. */
    if (attr == ZEND_NAME_FQ || memchr(name->val, '\\', name->len) != NULL) {
        zend_fatal("Undefined constant '%s'", name->val);
        return 1;
    }
    /* VERSIONGATE:70200 else-branch is the 5.6 behaviour */
    if (EG(php_version) >= 70200)
        zend_error(E_WARNING, "Use of undefined constant %s - assumed '%s' (this will throw an Error in a future version of PHP)",
                   name->val, name->val);
    else
        zend_error(E_NOTICE, "Use of undefined constant %s - assumed '%s'", name->val, name->val);
    *result = zval_zstr(name);
    return 0;
}

static zend_string *dir_name_of(zend_string *file)
{
    if (!file || file->len == 0) return zend_string_initz("");
    size_t i = file->len;
    while (i > 0 && file->val[i - 1] != '/') i--;
    if (i <= 1) return zend_string_initz(i == 1 ? "/" : ".");
    return zend_string_init(file->val, i - 1);
}

static int eval_magic_const(zend_ast *ast, zval *result)
{
    zend_execute_data *ex = EG(current);
    zend_function *fn = ex ? ex->func : NULL;
    zend_class_entry *scope = zend_get_current_scope();
    switch (ast->attr) {
    case T_LINE:    *result = zval_long((zend_long)ast->lineno); return 0;
    case T_FILE:    *result = zval_zstr(ex && ex->filename ? ex->filename : EG(filename)); return 0;
    case T_DIR:     *result = zval_zstr(dir_name_of(ex && ex->filename ? ex->filename : EG(filename))); return 0;
    case T_FUNC_C:  *result = zval_zstr(fn && fn->name ? fn->name : zend_string_initz("")); return 0;
    case T_CLASS_C: *result = zval_zstr(scope ? scope->name : zend_string_initz("")); return 0;
    case T_TRAIT_C: {
        zend_class_entry *tr = fn ? zend_trait_of_function(fn) : NULL;
        if (!tr && scope && (scope->flags & ZEND_ACC_TRAIT)) tr = scope;
        *result = zval_zstr(tr ? tr->name : zend_string_initz(""));
        return 0;
    }
    case T_METHOD_C:
        if (fn && fn->name) {
            if (fn->scope)
                *result = zval_zstr(zend_strpprintf("%s::%s", fn->scope->name->val, fn->name->val));
            else
                *result = zval_zstr(fn->name);
        } else *result = zval_zstr(zend_string_initz(""));
        return 0;
    case T_NS_C:
    default:
        *result = zval_zstr(zend_string_initz(""));
        return 0;
    }
}

/* ======================================================================== */
/*  array literals and string interpolation                                 */
/* ======================================================================== */

static int append_spread_ex(HashTable *ht, zval *v, int keep_string_keys);
#define append_spread(ht, v) append_spread_ex((ht), (v), 0)

static int eval_array_literal(zend_ast *ast, zval *result)
{
    HashTable *ht = zend_hash_new();
    for (uint32_t i = 0; i < ast->nchild; i++) {
        zend_ast *e = ast->child[i];
        if (!e || !e->child[0]) continue;
        if (e->attr & ZEND_ARRAY_ELEM_SPREAD) {
            zval v = zval_null();
            if (zend_eval_expr(e->child[0], &v)) return 1;
            if (append_spread(ht, &v)) return 1;
            continue;
        }
        zval key = zval_null();
        int have_key = 0;
        if (e->nchild > 1 && e->child[1]) {
            if (zend_eval_expr(e->child[1], &key)) return 1;
            have_key = 1;
        }
        zval val;
        if (e->attr & ZEND_ARRAY_ELEM_BY_REF) {
            int err = 0;
            zval *slot = fetch_lval_ex(e->child[0], FETCH_W, &err);
            if (!slot) {
                if (err == 2) zend_fatal("Cannot create references to/from string offsets");
                return 1;
            }
            zend_reference *r = zend_make_reference(slot);
            r->refcount++;
            val.u1 = IS_REFERENCE;
            val.value.ref = r;
        } else {
            val = zval_null();
            if (zend_eval_expr(e->child[0], &val)) return 1;
        }
        if (have_key) {
            if (!array_set(ht, &key, val)) { if (EG(exception)) return 1; }
        } else {
            zend_hash_next_index_insert(ht, val);
        }
    }
    *result = zval_array(ht);
    return 0;
}

static int append_spread_ex(HashTable *ht, zval *v0, int keep_string_keys)
{
    zval *v = zval_deref(v0);
    if (v->u1 == IS_ARRAY) {
        ZEND_HASH_FOREACH(v->value.arr, b) {
            if (b->key) {
                /* VERSION: string keys in an array spread are 8.1+; in an
                 * argument list they are 8.0 named arguments. */
                /* VERSIONGATE:80100 the guarded branch is the 5.6 behaviour */
                if (!keep_string_keys && EG(php_version) < 80100) {
                    zend_throw_error("Error", "Cannot unpack array with string keys");
                    return 1;
                }
                zend_hash_update(ht, b->key, zval_copy(b->val));
            } else {
                zend_hash_next_index_insert(ht, zval_copy(b->val));
            }
        } ZEND_HASH_FOREACH_END();
        return 0;
    }
    if (v->u1 == IS_OBJECT) {
        /* Traversable: drain it through the iterator protocol. */
        zend_object *obj = v->value.obj;
        zend_generator *g = zend_generator_from_object(obj);
        if (g) {
            if (zend_generator_ensure_started(g)) return 1;
            while (zend_generator_valid(g)) {
                zval val = zend_generator_current(g);
                zend_hash_next_index_insert(ht, zval_copy(val));
                if (zend_generator_next(g)) return 1;
            }
            return 0;
        }
        zval r = zval_null();
        if (call_method_name(obj, "getiterator", 0, NULL, &r) == 0) {
            zval rr = *zval_deref(&r);
            return append_spread_ex(ht, &rr, keep_string_keys);
        }
        if (class_has_method(obj->ce, "rewind")) {
            zval t = zval_null();
            if (call_method_name(obj, "rewind", 0, NULL, &t) == 1) return 1;
            while (1) {
                zval valid = zval_null();
                if (call_method_name(obj, "valid", 0, NULL, &valid) == 1) return 1;
                if (!zval_is_true(&valid)) break;
                zval cur = zval_null();
                if (call_method_name(obj, "current", 0, NULL, &cur) == 1) return 1;
                zend_hash_next_index_insert(ht, zval_copy(*zval_deref(&cur)));
                zval nx = zval_null();
                if (call_method_name(obj, "next", 0, NULL, &nx) == 1) return 1;
            }
            return 0;
        }
    }
    /* VERSIONGATE:80000 else-branch is the 5.6 behaviour (a warning, and the
     * operand is simply skipped) */
    if (EG(php_version) >= 80000) {
        zend_throw_error("Error", "Only arrays and Traversables can be unpacked");
        return 1;
    }
    zend_error(E_WARNING, "Only arrays and Traversables can be unpacked");
    return 0;
}

static int eval_encaps(zend_ast *ast, zval *result)
{
    zend_string *acc = zend_string_initz("");
    for (uint32_t i = 0; i < ast->nchild; i++) {
        if (!ast->child[i]) continue;
        zval v = zval_null();
        if (zend_eval_expr(ast->child[i], &v)) return 1;
        zend_string *s = zval_get_string(&v);
        if (EG(exception)) return 1;
        acc = zend_string_concat(acc, s);
    }
    *result = zval_zstr(acc);
    return 0;
}

/* ======================================================================== */
/*  casts                                                                   */
/* ======================================================================== */

static int eval_cast(zend_ast *ast, zval *result)
{
    zval v = zval_null();
    if (zend_eval_expr(ast->child[0], &v)) return 1;
    zval *d = zval_deref(&v);
    switch (ast->attr) {
    case IS_LONG:   *result = zval_long(zval_get_long(d)); return 0;
    case IS_DOUBLE: *result = zval_double(zval_get_double(d)); return 0;
    case IS_STRING: {
        zend_string *s = zval_get_string(d);
        if (EG(exception)) return 1;
        *result = zval_zstr(s);
        return 0;
    }
    case IS_TRUE: case IS_FALSE: *result = zval_bool(zval_is_true(d)); return 0;
    case IS_NULL: *result = zval_null(); return 0;
    case IS_ARRAY: {
        if (d->u1 == IS_ARRAY) { *result = zval_copy(*d); return 0; }
        if (d->u1 == IS_OBJECT) {
            /* (array) of an object keeps php-src's mangled private/protected
             * keys exactly as they are stored. */
            HashTable *ht = zend_hash_dup(d->value.obj->properties);
            *result = zval_array(ht);
            return 0;
        }
        HashTable *ht = zend_hash_new();
        if (d->u1 != IS_NULL && d->u1 != IS_UNDEF)
            zend_hash_next_index_insert(ht, zval_copy(*d));
        *result = zval_array(ht);
        return 0;
    }
    case IS_OBJECT: {
        if (d->u1 == IS_OBJECT) { *result = zval_copy(*d); return 0; }
        zend_class_entry *std = zend_lookup_class(S("stdClass"), 0);
        zend_object *o = zend_object_new(std);
        if (d->u1 == IS_ARRAY) {
            ZEND_HASH_FOREACH(d->value.arr, b) {
                zend_string *k = b->key ? b->key : zend_strpprintf("%lld", (long long)b->h);
                zend_hash_update(o->properties, k, zval_copy(b->val));
            } ZEND_HASH_FOREACH_END();
        } else if (d->u1 != IS_NULL && d->u1 != IS_UNDEF) {
            zend_hash_update(o->properties, S("scalar"), zval_copy(*d));
        }
        *result = zval_object(o);
        return 0;
    }
    default:
        *result = zval_copy(*d);
        return 0;
    }
}

/* ======================================================================== */
/*  clone                                                                   */
/* ======================================================================== */

static int eval_clone(zval *src, zval *result)
{
    zval *d = zval_deref(src);
    if (d->u1 != IS_OBJECT) {
        /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
        if (EG(php_version) >= 70000) {
            zend_throw_error("Error", "__clone method called on non-object");
            return 1;
        }
        zend_fatal("__clone method called on non-object");
        return 1;
    }
    zend_object *o = d->value.obj;
    if (zend_ce_generator && o->ce == zend_ce_generator) {
        zend_fatal("Trying to clone an uncloneable object of class %s", o->ce->name->val);
        return 1;
    }
    /* VERSIONGATE:80100 BEGIN - enum cases are singletons */
    if (o->ce && o->ce->is_enum) {
        zend_throw_error("Error", "Trying to clone an uncloneable object of class %s",
                         o->ce->name->val);
        return 1;
    }
    /* VERSIONGATE:80100 END */
    zend_object *n = ecalloc(1, sizeof(zend_object));
    n->handle = ++EG(next_obj_handle);
    n->refcount = 1;
    n->ce = o->ce;
    n->internal = o->internal;
    n->properties = zend_hash_dup(o->properties);
    /* references inside an object are shared by a clone, like php-src */
    *result = zval_object(n);
    if (o->ce && o->ce->clone) {
        zval r = zval_null();
        if (zend_call_function_ex(o->ce->clone, n, n->ce, 0, NULL, NULL, &r, NULL)) return 1;
    }
    return 0;
}

/* ======================================================================== */
/*  ?? operand fetch (never emits a diagnostic)                             */
/* ======================================================================== */

static int coalesce_fetch(zend_ast *ast, zval *out, int *found)
{
    int err = 0;
    *found = 0;
    switch (ast->kind) {
    case AST_VAR: case AST_DIM: case AST_STATIC_PROP: {
        zval *slot = fetch_lval_ex(ast, FETCH_IS, &err);
        if (err) return 1;
        if (!slot) {
            /* ArrayAccess and string offsets still have to be consulted */
            if (ast->kind == AST_DIM) {
                int r = 0;
                if (isset_expr(ast, 0, &r)) return 1;
                if (!r) return 0;
                zval cont = zval_null();
                if (zend_eval_expr(ast->child[0], &cont)) return 1;
                zval key = zval_null();
                if (ast->nchild > 1 && ast->child[1] && zend_eval_expr(ast->child[1], &key)) return 1;
                if (read_dim(&cont, &key, 1, FETCH_IS, out)) return 1;
                *found = zval_deref(out)->u1 > IS_NULL;
                return 0;
            }
            return 0;
        }
        zval *d = zval_deref(slot);
        if (d->u1 <= IS_NULL) return 0;
        *out = zval_copy(*d);
        *found = 1;
        return 0;
    }
    case AST_PROP: case AST_NULLSAFE_PROP: {
        int r = 0;
        if (isset_expr(ast, 0, &r)) return 1;
        if (!r) return 0;
        if (zend_eval_expr(ast, out)) return 1;
        *found = zval_deref(out)->u1 > IS_NULL;
        return 0;
    }
    default:
        if (zend_eval_expr(ast, out)) return 1;
        *found = zval_deref(out)->u1 > IS_NULL;
        return 0;
    }
}

/* ======================================================================== */
/*  compound assignment                                                     */
/* ======================================================================== */

static int do_compound_assign(zend_ast *t, zend_binop op, zval rhs, zval *result)
{
    int err = 0;

    if (t->kind == AST_PROP || t->kind == AST_NULLSAFE_PROP) {
        if (string_offset_base(t)) {
            zend_fatal("Cannot use string offset as an object");
            return 1;
        }
        zval obj = zval_null();
        if (zend_eval_expr(t->child[0], &obj)) return 1;
        zval *o = zval_deref(&obj);
        if (o->u1 != IS_OBJECT) {
            zend_object *no = NULL;
            if (prop_object_from(&obj, t, &no, FETCH_W)) return EG(exception) ? 1 : 0;
            if (!no) return 0;
            if (is_var_ast(t->child[0])) {
                zval *bslot = fetch_lval_ex(t->child[0], FETCH_W, &err);
                if (bslot) slot_set(zval_deref(bslot), zval_object(no));
            }
            obj = zval_object(no);
            o = &obj;
        }
        zend_string *name;
        if (eval_name(t->child[1], &name)) return 1;
        int magic = 0;
        zval *slot = fetch_prop_slot(o->value.obj, name, FETCH_RW, &magic, &err);
        if (slot) {
            zval *d = zval_deref(slot);
            zval out;
            if (zend_binary_op(op, d, &rhs, &out)) return 1;
            slot_set(d, out);
            if (result) *result = zval_copy(*d);
            return 0;
        }
        if (err) return 1;
        zval tmp = zval_null();
        zval *cur = read_property_guarded(o->value.obj, name, 0, &tmp);
        if (EG(exception)) return 1;
        zval base = cur ? *zval_deref(cur) : zval_null();
        zval out;
        if (zend_binary_op(op, &base, &rhs, &out)) return 1;
        if (zend_write_property(o->value.obj, name, out)) return 1;
        if (result) *result = zval_copy(out);
        return 0;
    }

    if (t->kind == AST_DIM) {
        zval key = zval_null();
        int have_key = 0;
        if (t->nchild > 1 && t->child[1]) {
            if (eval_dim_key(t->child[1], &key)) return 1;
            have_key = 1;
        }
        zval *cont = fetch_lval_ex(t->child[0], FETCH_W, &err);
        if (!cont) return 1;
        zval *c = zval_deref(cont);
        if ((c->u1 == IS_STRING && c->value.str->len) ||
            (c->u1 == IS_OBJECT && object_is_arrayaccess(c->value.obj))) {
            zval cur = zval_null();
            if (read_dim(c, &key, have_key, FETCH_R, &cur)) return 1;
            zval out;
            if (zend_binary_op(op, &cur, &rhs, &out)) return 1;
            if (c->u1 == IS_STRING) return assign_string_offset(c, &key, out, result);
            zval args[2];
            args[0] = have_key ? zval_copy(*zval_deref(&key)) : zval_null();
            args[1] = out;
            zval r = zval_null();
            if (call_method_name(c->value.obj, "offsetset", 2, args, &r) == 1) return 1;
            if (result) *result = zval_copy(out);
            return 0;
        }
        zval *slot = fetch_dim_slot(cont, &key, have_key, FETCH_RW, &err);
        if (!slot) return 1;
        zval *d = zval_deref(slot);
        zval out;
        if (zend_binary_op(op, d, &rhs, &out)) return 1;
        slot_set(d, out);
        if (result) *result = zval_copy(*d);
        return 0;
    }

    zval *slot = fetch_lval_ex(t, FETCH_RW, &err);
    if (!slot) return 1;
    zval *d = zval_deref(slot);
    zval out;
    if (zend_binary_op(op, d, &rhs, &out)) return 1;
    slot_set(d, out);
    if (result) *result = zval_copy(*d);
    return 0;
}

static int do_incdec(zend_ast *ast, zval *result)
{
    int dec  = (ast->attr & ZEND_INCDEC_DEC)  != 0;
    int post = (ast->attr & ZEND_INCDEC_POST) != 0;
    zend_ast *t = ast->child[0];
    int err = 0;

    if (t->kind == AST_PROP || t->kind == AST_NULLSAFE_PROP) {
        if (string_offset_base(t)) {
            zend_fatal("Cannot increment/decrement overloaded objects nor string offsets");
            return 1;
        }
        zval obj = zval_null();
        if (zend_eval_expr(t->child[0], &obj)) return 1;
        zval *o = zval_deref(&obj);
        if (o->u1 != IS_OBJECT) {
            zend_object *no = NULL;
            if (prop_object_from(&obj, t, &no, FETCH_W)) return EG(exception) ? 1 : 0;
            if (!no) return 0;
            if (is_var_ast(t->child[0])) {
                zval *bslot = fetch_lval_ex(t->child[0], FETCH_W, &err);
                if (bslot) slot_set(zval_deref(bslot), zval_object(no));
            }
            obj = zval_object(no);
            o = &obj;
        }
        zend_string *name;
        if (eval_name(t->child[1], &name)) return 1;
        int magic = 0;
        zval *slot = fetch_prop_slot(o->value.obj, name, FETCH_RW, &magic, &err);
        if (slot) {
            zval *d = zval_deref(slot);
            zval old = *d;
            if (dec) decrement_zval(d); else increment_zval(d);
            if (result) *result = post ? zval_copy(old) : zval_copy(*d);
            return 0;
        }
        if (err) return 1;
        zval tmp = zval_null();
        zval *cur = read_property_guarded(o->value.obj, name, 0, &tmp);
        if (EG(exception)) return 1;
        zval v = cur ? *zval_deref(cur) : zval_null();
        zval old = v;
        if (dec) decrement_zval(&v); else increment_zval(&v);
        if (zend_write_property(o->value.obj, name, v)) return 1;
        if (result) *result = post ? zval_copy(old) : zval_copy(v);
        return 0;
    }

    zval *slot = fetch_lval_ex(t, FETCH_RW, &err);
    if (!slot) return 1;
    zval *d = zval_deref(slot);
    zval old = *d;
    if (dec) decrement_zval(d); else increment_zval(d);
    if (result) *result = post ? zval_copy(old) : zval_copy(*d);
    return 0;
}

/* ======================================================================== */
/*  class constants                                                         */
/* ======================================================================== */

/* A throw-away frame that only exists so that self/static resolve inside a
 * constant or property default expression. */
typedef struct { zend_execute_data ex; zend_function fn; } scope_frame;

static void push_scope_frame(scope_frame *f, zend_class_entry *ce)
{
    memset(f, 0, sizeof(*f));
    f->fn.scope = ce;
    f->ex.prev = EG(current);
    f->ex.symbols = EG(current) ? EG(current)->symbols : EG(symbol_table);
    f->ex.func = &f->fn;
    f->ex.called_scope = ce;
    f->ex.filename = ce && ce->filename ? ce->filename : EG(filename);
    EG(current) = &f->ex;
}

static void pop_scope_frame(scope_frame *f) { EG(current) = f->ex.prev; }

/* 0 = found, -1 = no such constant, 1 = exception. */
int class_constant_value(zend_class_entry *ce, zend_string *name, zval *out)
{
    for (zend_class_entry *c = ce; c; c = c->parent) {
        zval *v = c->constants_table ? zend_hash_find(c->constants_table, name) : NULL;
        if (v) {
            zend_class_constant *cc = (zend_class_constant *)ZEND_ZVAL_PTR_P(v);
            if (cc->evaluated == 2) return 2;       /* recursive definition */
            if (!cc->evaluated) {
                if (cc->ast) {
                    cc->evaluated = 2;
                    scope_frame f;
                    push_scope_frame(&f, cc->ce ? cc->ce : c);
                    zval val = zval_null();
                    int rc = zend_eval_expr(cc->ast, &val);
                    pop_scope_frame(&f);
                    if (rc) { cc->evaluated = 0; return rc; }
                    cc->value = val;
                }
                cc->evaluated = 1;
            }
            *out = zval_copy(cc->value);
            return 0;
        }
        for (uint32_t i = 0; i < c->num_interfaces; i++) {
            int r = class_constant_value(c->interfaces[i], name, out);
            if (r != -1) return r;
        }
    }
    return -1;
}

/* ======================================================================== */
/*  the expression evaluator                                                */
/* ======================================================================== */

static int eval_closure_node(zend_ast *ast, zval *result);
static int eval_include(zend_ast *ast, zval *result);
static int eval_yield(zend_ast *ast, zval *result);
/* VERSIONGATE:70000 "yield from" is 7.0 */
static int eval_yield_from(zend_ast *ast, zval *result);

int zend_eval_expr(zend_ast *ast, zval *result)
{
    if (!ast) { *result = zval_null(); return 0; }
    if (EG(exception) || EG(exiting)) return 1;
    zend_set_line(ast);
    *result = zval_null();

    switch (ast->kind) {
    case AST_ZVAL:
        *result = zval_copy(ast->val);
        return 0;

    case AST_VAR: {
        int err = 0;
        zval *slot = fetch_var_slot(ast, FETCH_R, &err);
        if (err) return 1;
        if (!slot) { *result = zval_null(); return 0; }
        *result = zval_copy(*zval_deref(slot));
        return 0;
    }

    case AST_CONST:
        return eval_const_name(ast->val.value.str, ast->attr, result);

    case AST_MAGIC_CONST:
        return eval_magic_const(ast, result);

    case AST_BINARY: {
        zval a = zval_null(), b = zval_null();
        if (zend_eval_expr(ast->child[0], &a)) return 1;
        if (zend_eval_expr(ast->child[1], &b)) return 1;
        zend_set_line(ast);
        return zend_binary_op((zend_binop)ast->attr, &a, &b, result);
    }

    case AST_UNARY: {
        zval a = zval_null();
        if (zend_eval_expr(ast->child[0], &a)) return 1;
        zend_set_line(ast);
        zval *d = zval_deref(&a);
        switch (ast->attr) {
        case UN_MINUS: {
            zval zero = zval_long(0);
            return zend_binary_op(BIN_SUB, &zero, d, result);
        }
        case UN_PLUS: {
            zval zero = zval_long(0);
            return zend_binary_op(BIN_ADD, &zero, d, result);
        }
        case UN_NOT: *result = zval_bool(!zval_is_true(d)); return 0;
        case UN_BNOT:
            if (d->u1 == IS_STRING) {
                zend_string *s = d->value.str;
                zend_string *o = zend_string_alloc(s->len);
                for (size_t i = 0; i < s->len; i++) o->val[i] = (char)(~(unsigned char)s->val[i]);
                *result = zval_zstr(o);
                return 0;
            }
            if (d->u1 == IS_DOUBLE || d->u1 == IS_LONG) {
                *result = zval_long(~zval_get_long(d));
                return 0;
            }
            /* VERSIONGATE:80000 else-branch is the 5.6 behaviour */
            if (EG(php_version) >= 80000) {
                zend_throw_error("TypeError", "Cannot perform bitwise not on %s",
                                 zend_zval_type_name(d));
                return 1;
            }
            zend_fatal("Unsupported operand types");
            return 1;
        default: *result = zval_null(); return 0;
        }
    }

    case AST_ASSIGN: {
        zend_ast *t = ast->child[0];
        if (t->kind == AST_LIST_ASSIGN || t->kind == AST_ARRAY) {
            zval v = zval_null();
            if (zend_eval_expr(ast->child[1], &v)) return 1;
            zend_set_line(ast);
            return list_assign(t, v, result);
        }
        int base = nprekeys, saved = prekey_base;
        if (prekey_collect(t)) { nprekeys = base; prekey_base = saved; return 1; }
        zval v = zval_null();
        if (zend_eval_expr(ast->child[1], &v)) { nprekeys = base; prekey_base = saved; return 1; }
        zend_set_line(ast);
        prekey_base = base;
        int rc = do_assign(t, v, result);
        nprekeys = base; prekey_base = saved;
        return rc;
    }

    case AST_ASSIGN_REF: {
        zend_ast *src = ast->child[1];
        int err = 0;
        if (src->kind == AST_CALL || src->kind == AST_METHOD_CALL ||
            src->kind == AST_STATIC_CALL || src->kind == AST_NEW ||
            src->kind == AST_CLONE) {
            zval v = zval_null();
            if (zend_eval_expr(src, &v)) return 1;
            zend_reference *r;
            if (v.u1 == IS_REFERENCE) r = v.value.ref;
            else { r = ecalloc(1, sizeof(zend_reference)); r->refcount = 1; r->val = v; }
            zval box; box.u1 = IS_REFERENCE; box.value.ref = r;
            zval holder = box;
            return bind_reference(ast->child[0], &holder, result);
        }
        zval *slot = fetch_lval_ex(src, FETCH_W, &err);
        if (!slot) {
            if (err == 2) zend_fatal("Cannot create references to/from string offsets");
            return 1;
        }
        return bind_reference(ast->child[0], slot, result);
    }

    case AST_ASSIGN_OP: {
        zend_ast *t = ast->child[0];
        int base = nprekeys, saved = prekey_base;
        if (prekey_collect(t)) { nprekeys = base; prekey_base = saved; return 1; }
        zval rhs = zval_null();
        if (zend_eval_expr(ast->child[1], &rhs)) { nprekeys = base; prekey_base = saved; return 1; }
        zend_set_line(ast);
        prekey_base = base;
        int rc = do_compound_assign(t, (zend_binop)ast->attr, rhs, result);
        nprekeys = base; prekey_base = saved;
        return rc;
    }

    case AST_ASSIGN_COALESCE: {
        zend_ast *t = ast->child[0];
        zval cur = zval_null();
        int found = 0;
        if (coalesce_fetch(t, &cur, &found)) return 1;
        if (found) { if (result) *result = cur; return 0; }
        zval v = zval_null();
        if (zend_eval_expr(ast->child[1], &v)) return 1;
        return do_assign(t, v, result);
    }

    case AST_INC_DEC:
        return do_incdec(ast, result);

    case AST_CAST:
        return eval_cast(ast, result);

    case AST_ISSET: {
        int all = 1;
        for (uint32_t i = 0; i < ast->nchild; i++) {
            int r = 0;
            if (isset_expr(ast->child[i], 0, &r)) return 1;
            if (!r) { all = 0; break; }
        }
        *result = zval_bool(all);
        return 0;
    }

    case AST_EMPTY: {
        int r = 0;
        if (isset_expr(ast->child[0], 1, &r)) return 1;
        *result = zval_bool(!r);
        return 0;
    }

    case AST_UNSET:
        for (uint32_t i = 0; i < ast->nchild; i++)
            if (unset_ast(ast->child[i])) return 1;
        *result = zval_null();
        return 0;

    case AST_TERNARY: {
        zval c = zval_null();
        if (zend_eval_expr(ast->child[0], &c)) return 1;
        int t = zval_is_true(zval_deref(&c));
        if (!ast->child[1]) {                      /* $a ?: $b */
            if (t) { *result = zval_copy(*zval_deref(&c)); return 0; }
            return zend_eval_expr(ast->child[2], result);
        }
        return zend_eval_expr(t ? ast->child[1] : ast->child[2], result);
    }

    case AST_COALESCE: {
        zval v = zval_null();
        int found = 0;
        if (coalesce_fetch(ast->child[0], &v, &found)) return 1;
        if (found) { *result = v; return 0; }
        return zend_eval_expr(ast->child[1], result);
    }

    case AST_AND: {
        zval a = zval_null();
        if (zend_eval_expr(ast->child[0], &a)) return 1;
        if (!zval_is_true(zval_deref(&a))) { *result = zval_bool(0); return 0; }
        zval b = zval_null();
        if (zend_eval_expr(ast->child[1], &b)) return 1;
        *result = zval_bool(zval_is_true(zval_deref(&b)));
        return 0;
    }

    case AST_OR: {
        zval a = zval_null();
        if (zend_eval_expr(ast->child[0], &a)) return 1;
        if (zval_is_true(zval_deref(&a))) { *result = zval_bool(1); return 0; }
        zval b = zval_null();
        if (zend_eval_expr(ast->child[1], &b)) return 1;
        *result = zval_bool(zval_is_true(zval_deref(&b)));
        return 0;
    }

    case AST_ARRAY:
        return eval_array_literal(ast, result);

    case AST_DIM: {
        zval cont = zval_null();
        if (zend_eval_expr(ast->child[0], &cont)) return 1;
        zval key = zval_null();
        int have_key = 0;
        if (ast->nchild > 1 && ast->child[1]) {
            if (zend_eval_expr(ast->child[1], &key)) return 1;
            have_key = 1;
        }
        zend_set_line(ast);
        return read_dim(&cont, &key, have_key, FETCH_R, result);
    }

    case AST_PROP:
    case AST_NULLSAFE_PROP: {
        zval obj = zval_null();
        if (zend_eval_expr(ast->child[0], &obj)) return 1;
        zval *o = zval_deref(&obj);
        if (o->u1 != IS_OBJECT) {
            if (ast->kind == AST_NULLSAFE_PROP && o->u1 <= IS_NULL) { *result = zval_null(); return 0; }
            zend_string *pn = NULL;
            if (eval_name(ast->child[1], &pn)) return 1;
            zend_set_line(ast);
            /* VERSION: 8.0 names the property and the type in the warning. */
            /* VERSIONGATE:80000 else-branch is the 5.6 behaviour */
            if (EG(php_version) >= 80000)
                zend_error(E_WARNING, "Attempt to read property \"%s\" on %s",
                           pn->val, zend_zval_type_name(o));
            else
                zend_error(E_NOTICE, "Trying to get property of non-object");
            *result = zval_null();
            return 0;
        }
        zend_string *name;
        if (eval_name(ast->child[1], &name)) return 1;
        zend_set_line(ast);
        zval tmp = zval_null();
        zval *v = read_property_guarded(o->value.obj, name, 0, &tmp);
        if (EG(exception) || EG(exiting)) return 1;
        *result = v ? zval_copy(*zval_deref(v)) : zval_null();
        return 0;
    }

    case AST_STATIC_PROP: {
        int err = 0;
        zval *slot = fetch_lval_ex(ast, FETCH_R, &err);
        if (!slot) return err ? 1 : 0;
        *result = zval_copy(*zval_deref(slot));
        return 0;
    }

    case AST_CLASS_CONST:
    case AST_CONST_FETCH_DYNAMIC: {
        zend_string *name;
        if (eval_name(ast->child[1], &name)) return 1;
        if (zend_string_equals_ci(name, S("class"))) {
            zend_ast *ca = ast->child[0];
            zend_string *cn = (ca->kind == AST_ZVAL || ca->kind == AST_CONST)
                            ? ca->val.value.str : NULL;
            if (cn && !zend_string_equals_ci(cn, S("self")) &&
                      !zend_string_equals_ci(cn, S("parent")) &&
                      !zend_string_equals_ci(cn, S("static"))) {
                *result = zval_zstr(cn);
                return 0;
            }
            int err = 0;
            zend_class_entry *ce = zend_resolve_class_ast(ca, &err);
            if (!ce) return 1;
            *result = zval_zstr(ce->name);
            return 0;
        }
        int err = 0;
        zend_class_entry *ce = zend_resolve_class_ast(ast->child[0], &err);
        if (!ce) return 1;
        if (bind_class_constants(ce)) return 1;
        int r = class_constant_value(ce, name, result);
        if (r == 2) {
            zend_ast *ca2 = ast->child[0];
            zend_string *cn2 = (ca2->kind == AST_ZVAL || ca2->kind == AST_CONST)
                             ? ca2->val.value.str : ce->name;
            zend_fatal("Cannot declare self-referencing constant '%s::%s'", cn2->val, name->val);
            return 1;
        }
        if (r == 1) return 1;
        if (r == -1) {
            /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
            if (EG(php_version) >= 70000)
                zend_throw_error("Error", "Undefined class constant '%s'", name->val);
            else
                zend_fatal("Undefined class constant '%s'", name->val);
            return 1;
        }
        return 0;
    }

    case AST_CALL: case AST_METHOD_CALL: case AST_NULLSAFE_METHOD_CALL:
    case AST_STATIC_CALL: case AST_NEW:
        return eval_call_node(ast, result);

    case AST_CLONE: {
        zval v = zval_null();
        if (zend_eval_expr(ast->child[0], &v)) return 1;
        zend_set_line(ast);
        return eval_clone(&v, result);
    }

    case AST_PRINT: {
        zval v = zval_null();
        if (zend_eval_expr(ast->child[0], &v)) return 1;
        zend_string *s = zval_get_string(&v);
        if (EG(exception)) return 1;
        php_write_str(s);
        *result = zval_long(1);
        return 0;
    }

    case AST_EXIT: {
        zval v = zval_null();
        if (ast->nchild && ast->child[0] && zend_eval_expr(ast->child[0], &v)) return 1;
        zval *d = zval_deref(&v);
        if (d->u1 == IS_STRING) { php_write_str(d->value.str); EG(exit_status) = 0; }
        else if (d->u1 == IS_LONG) EG(exit_status) = (int)d->value.lval;
        else EG(exit_status) = 0;
        EG(exiting) = 1;
        return 1;
    }

    case AST_SILENCE: {
        EG(silence)++;
        int rc = zend_eval_expr(ast->child[0], result);
        EG(silence)--;
        return rc;
    }

    case AST_INSTANCEOF: {
        zval v = zval_null();
        if (zend_eval_expr(ast->child[0], &v)) return 1;
        zval *d = zval_deref(&v);
        zend_class_entry *ce = NULL;
        zend_ast *ca = ast->child[1];
        zend_string *nm = (ca->kind == AST_ZVAL || ca->kind == AST_CONST)
                        ? ca->val.value.str : NULL;
        if (nm) {
            if (zend_string_equals_ci(nm, S("self"))) ce = zend_get_current_scope();
            else if (zend_string_equals_ci(nm, S("parent"))) {
                zend_class_entry *s = zend_get_current_scope();
                ce = s ? s->parent : NULL;
            } else if (zend_string_equals_ci(nm, S("static"))) ce = called_scope();
            else ce = zend_lookup_class(nm, 0);     /* instanceof never autoloads */
        } else {
            zval c = zval_null();
            if (zend_eval_expr(ca, &c)) return 1;
            zval *cd = zval_deref(&c);
            if (cd->u1 == IS_OBJECT) ce = cd->value.obj->ce;
            else ce = zend_lookup_class(zval_get_string(cd), 0);
        }
        *result = zval_bool(d->u1 == IS_OBJECT && ce && zend_instanceof(d->value.obj->ce, ce));
        return 0;
    }

    case AST_CLOSURE:
    case AST_ARROW_FN:
        return eval_closure_node(ast, result);

    case AST_YIELD:      return eval_yield(ast, result);
    /* VERSIONGATE:70000 "yield from" is 7.0; 5.6 has no such node */
    case AST_YIELD_FROM: return eval_yield_from(ast, result);

    case AST_THROW_EXPR: {
        zval v = zval_null();
        if (zend_eval_expr(ast->child[0], &v)) return 1;
        zval *d = zval_deref(&v);
        if (d->u1 != IS_OBJECT) {
            zend_throw_error("Error", "Can only throw objects");
            return 1;
        }
        zend_throw_exception_object(d->value.obj);
        return 1;
    }

    case AST_MATCH: {
        zval subj = zval_null();
        if (zend_eval_expr(ast->child[0], &subj)) return 1;
        zend_ast *arms = ast->child[1];
        zend_ast *dflt = NULL;
        for (uint32_t i = 0; arms && i < arms->nchild; i++) {
            zend_ast *arm = arms->child[i];
            if (!arm) continue;
            if (!arm->child[0]) { dflt = arm; continue; }
            zend_ast *conds = arm->child[0];
            for (uint32_t j = 0; j < conds->nchild; j++) {
                zval c = zval_null();
                if (zend_eval_expr(conds->child[j], &c)) return 1;
                if (zend_is_identical(&subj, &c))
                    return zend_eval_expr(arm->child[1], result);
            }
        }
        if (dflt) return zend_eval_expr(dflt->child[1], result);
        {
            zval *d = zval_deref(&subj);
            zend_string *shown;
            if (d->u1 == IS_STRING) shown = zend_strpprintf("'%s'", d->value.str->val);
            else if (d->u1 == IS_OBJECT) shown = zend_strpprintf("of type %s", d->value.obj->ce->name->val);
            else if (d->u1 == IS_ARRAY) shown = zend_string_initz("of type array");
            else if (d->u1 == IS_NULL || d->u1 == IS_UNDEF) shown = zend_string_initz("of type null");
            else if (d->u1 == IS_TRUE) shown = zend_string_initz("of type bool");
            else if (d->u1 == IS_FALSE) shown = zend_string_initz("of type bool");
            else shown = zval_get_string(d);
            zend_throw_error("UnhandledMatchError", "Unhandled match case %s", shown->val);
        }
        return 1;
    }

    case AST_LIST_ASSIGN:
        zend_fatal("Cannot use list() outside of an assignment");
        return 1;

    case AST_ENCAPS:
        return eval_encaps(ast, result);

    case AST_SHELL_EXEC: {
        zval v = zval_null();
        if (zend_eval_expr(ast->child[0], &v)) return 1;
        zend_function *fn = zend_lookup_function(S("shell_exec"));
        if (!fn) { *result = zval_null(); return 0; }
        zval arg = zval_zstr(zval_get_string(&v));
        return zend_call_function_ex(fn, NULL, NULL, 1, &arg, NULL, result, NULL);
    }

    case AST_INCLUDE:
        return eval_include(ast, result);

    case AST_NAMED_ARG:
    case AST_SPREAD:
        return zend_eval_expr(ast->child[0], result);

    case AST_ANON_CLASS:
        return eval_call_node(ast, result);

    case AST_LIST:
        if (ast->nchild == 1) return zend_eval_expr(ast->child[0], result);
        for (uint32_t i = 0; i < ast->nchild; i++)
            if (zend_eval_expr(ast->child[i], result)) return 1;
        return 0;

    case AST_NOP:
        *result = zval_null();
        return 0;

    default:
        /* statements reached through an expression context */
        return exec_stmt(ast, result) == EX_EXCEPTION ? 1 : 0;
    }
}

/* ======================================================================== */
/*  type declarations                                                       */
/* ======================================================================== */

/* php-src's name for a value in a type error.  7.4 renamed the long forms
 * ("integer" -> "int" and friends) and 8.0 started naming true/false. */
static const char *type_error_name(const zval *v)
{
    /* VERSIONGATE:70400 else-branch is the 5.6 behaviour */
    if (EG(php_version) >= 70400) return zend_zval_type_name(v);
    switch (v->u1) {
    case IS_UNDEF: case IS_NULL: return "null";
    case IS_FALSE: case IS_TRUE: return "boolean";
    case IS_LONG:   return "integer";
    case IS_DOUBLE: return "double";
    case IS_STRING: return "string";
    case IS_ARRAY:  return "array";
    case IS_OBJECT: return "object";
    default:        return "unknown";
    }
}

static zend_string *given_description(const zval *v)
{
    if (v && v->u1 == IS_OBJECT)
        /* VERSIONGATE:80000 else-branch is the 5.6 behaviour */
        return EG(php_version) >= 80000
             ? zend_string_init(v->value.obj->ce->name->val, v->value.obj->ce->name->len)
             : zend_strpprintf("instance of %s", v->value.obj->ce->name->val);
    return zend_string_initz(type_error_name(v));
}

static int type_name_is_class(const char *n)
{
    static const char *scalars[] = {"int","float","string","bool","array","callable",
                                    "iterable","object","mixed","void","null","false",
                                    "true","never","static","parent","self",NULL};
    for (int i = 0; scalars[i]; i++) if (strcasecmp(n, scalars[i]) == 0) return 0;
    return 1;
}

/* Human readable form of a declared type, the way php-src prints it. */
static zend_string *type_hint_string(zend_type_hint *t)
{
    if (!t) return zend_string_initz("mixed");
    zend_string *s = zend_string_initz("");
    for (uint32_t i = 0; i < t->n; i++) {
        if (i) s = zend_string_concat(s, S("|"));
        s = zend_string_concat(s, t->names[i]);
    }
    if (t->nullable && t->n == 1) {
        zend_string *q = zend_string_initz("?");
        s = zend_string_concat(q, s);
    }
    return s;
}

/* Does `v` already satisfy one alternative of `t`?  No coercion. */
static int type_matches_exact(zend_type_hint *t, zval *v, zend_class_entry *scope)
{
    zval *d = zval_deref(v);
    if (t->nullable && d->u1 <= IS_NULL) return 1;
    for (uint32_t i = 0; i < t->n; i++) {
        const char *n = t->names[i]->val;
        if (*n == '?') { if (d->u1 <= IS_NULL) return 1; n++; }
        if (!strcasecmp(n, "mixed")) return 1;
        if (!strcasecmp(n, "null"))  { if (d->u1 <= IS_NULL) return 1; continue; }
        if (!strcasecmp(n, "false")) { if (d->u1 == IS_FALSE) return 1; continue; }
        if (!strcasecmp(n, "true"))  { if (d->u1 == IS_TRUE) return 1; continue; }
        if (!strcasecmp(n, "int"))   { if (d->u1 == IS_LONG) return 1; continue; }
        if (!strcasecmp(n, "float")) { if (d->u1 == IS_DOUBLE || d->u1 == IS_LONG) return 1; continue; }
        if (!strcasecmp(n, "string")) {
            if (d->u1 == IS_STRING) return 1;
            if (d->u1 == IS_OBJECT && d->value.obj->ce && d->value.obj->ce->__tostring) return 1;
            continue;
        }
        if (!strcasecmp(n, "bool"))  { if (d->u1 == IS_FALSE || d->u1 == IS_TRUE) return 1; continue; }
        if (!strcasecmp(n, "array")) { if (d->u1 == IS_ARRAY) return 1; continue; }
        if (!strcasecmp(n, "object")) { if (d->u1 == IS_OBJECT) return 1; continue; }
        if (!strcasecmp(n, "callable")) {
            zend_function *f = NULL; zend_object *o = NULL; zend_class_entry *s = NULL;
            zend_string *e = NULL;
            if (zend_is_callable_zval(d, &f, &o, &s, &e)) return 1;
            continue;
        }
        if (!strcasecmp(n, "iterable")) {
            if (d->u1 == IS_ARRAY) return 1;
            if (d->u1 == IS_OBJECT) {
                zend_class_entry *tr = zend_lookup_class(S("Traversable"), 0);
                if (tr && zend_instanceof(d->value.obj->ce, tr)) return 1;
            }
            continue;
        }
        if (d->u1 != IS_OBJECT) continue;
        zend_class_entry *ce;
        if (!strcasecmp(n, "self") || !strcasecmp(n, "static")) ce = scope;
        else if (!strcasecmp(n, "parent")) ce = scope ? scope->parent : NULL;
        else ce = zend_lookup_class(zend_string_initz(n), 1);
        if (ce && zend_instanceof(d->value.obj->ce, ce)) return 1;
    }
    return 0;
}

/* Weak-mode coercion (PHP 7's default).  Returns 1 when *v was coerced. */
/* VERSIONGATE:70000 scalar type declarations do not exist in 5.6 */
static int type_coerce_weak(zend_type_hint *t, zval *v)
{
    zval *d = zval_deref(v);
    for (uint32_t i = 0; i < t->n; i++) {
        const char *n = t->names[i]->val;
        if (*n == '?') n++;
        if (!strcasecmp(n, "int")) {
            if (d->u1 == IS_DOUBLE) {
                double dv = d->value.dval;
                if (dv != (double)(zend_long)dv) {
                    /* VERSIONGATE:80100 else-branch is the 5.6 behaviour */
                    if (EG(php_version) >= 80100) {
                        zend_error(E_DEPRECATED, "Implicit conversion from float %s to int loses precision",
                                   php_double_to_string(dv, 14, 0)->val);
                    }
                }
                *d = zval_long((zend_long)dv);
                return 1;
            }
            if (d->u1 == IS_STRING) {
                zend_long l = 0; double dd = 0;
                int k = is_numeric_string(d->value.str->val, d->value.str->len, &l, &dd, 0);
                if (k == IS_LONG) { *d = zval_long(l); return 1; }
                if (k == IS_DOUBLE) { *d = zval_long((zend_long)dd); return 1; }
                continue;
            }
            if (d->u1 == IS_FALSE || d->u1 == IS_TRUE) { *d = zval_long(d->u1 == IS_TRUE); return 1; }
            continue;
        }
        if (!strcasecmp(n, "float")) {
            if (d->u1 == IS_LONG) { *d = zval_double((double)d->value.lval); return 1; }
            if (d->u1 == IS_STRING) {
                zend_long l = 0; double dd = 0;
                int k = is_numeric_string(d->value.str->val, d->value.str->len, &l, &dd, 0);
                if (k == IS_LONG) { *d = zval_double((double)l); return 1; }
                if (k == IS_DOUBLE) { *d = zval_double(dd); return 1; }
                continue;
            }
            if (d->u1 == IS_FALSE || d->u1 == IS_TRUE) { *d = zval_double(d->u1 == IS_TRUE); return 1; }
            continue;
        }
        if (!strcasecmp(n, "string")) {
            if (d->u1 == IS_LONG || d->u1 == IS_DOUBLE ||
                d->u1 == IS_FALSE || d->u1 == IS_TRUE ||
                (d->u1 == IS_OBJECT && d->value.obj->ce && d->value.obj->ce->__tostring)) {
                zend_string *s = zval_get_string(d);
                if (EG(exception)) return 0;
                *d = zval_zstr(s);
                return 1;
            }
            continue;
        }
        if (!strcasecmp(n, "bool")) {
            if (d->u1 == IS_LONG || d->u1 == IS_DOUBLE || d->u1 == IS_STRING) {
                *d = zval_bool(zval_is_true(d));
                return 1;
            }
            continue;
        }
    }
    return 0;
}

static void arg_type_error(zend_function *fn, uint32_t argn, zend_type_hint *t,
                           zval *given, int none_given)
{
    zend_string *need = type_hint_string(t);
    zend_string *got = none_given ? zend_string_initz("none") : given_description(given);
    zend_string *cfile = EG(filename) ? EG(filename) : zend_string_initz("");
    uint32_t cline = EG(lineno);
    const char *cls = fn->scope ? fn->scope->name->val : "";
    const char *sep = fn->scope ? "::" : "";
    const char *nm = fn->name ? fn->name->val : "{closure}";

    /* VERSIONGATE:80000 else-branch is the 5.6/7.x wording */
    if (EG(php_version) >= 80000) {
        const char *pname = (argn - 1 < fn->num_args && fn->args)
                          ? fn->args[argn - 1].name->val : "";
        zend_throw_error("TypeError",
                         "%s%s%s(): Argument #%u ($%s) must be of type %s, %s given, called in %s on line %u",
                         cls, sep, nm, argn, pname, need->val, got->val, cfile->val, cline);
        return;
    }

    /* php-src words class hints differently from scalar and array hints. */
    const char *first = t && t->n ? t->names[0]->val : "mixed";
    zend_string *needmsg;
    if (t && t->n == 1 && type_name_is_class(first)) {
        zend_class_entry *ce = zend_lookup_class(t->names[0], 0);
        needmsg = (ce && (ce->flags & ZEND_ACC_INTERFACE))
                ? zend_strpprintf("implement interface %s", ce->name->val)
                : zend_strpprintf("be an instance of %s", ce ? ce->name->val : first);
    } else if (t && t->n == 1 && !strcasecmp(first, "callable")) {
        needmsg = zend_string_initz("be callable");
    } else {
        needmsg = zend_strpprintf("be of the type %s", need->val);
    }

    /* VERSIONGATE:70000 else-branch is the 5.6 behaviour (catchable fatal) */
    if (EG(php_version) >= 70000) {
        zend_throw_error("TypeError",
                         "Argument %u passed to %s%s%s() must %s, %s given, called in %s on line %u",
                         argn, cls, sep, nm, needmsg->val, got->val, cfile->val, cline);
        return;
    }
    zend_error_at(E_RECOVERABLE_ERROR, fn->filename ? fn->filename : EG(filename), fn->lineno,
                  "Argument %u passed to %s%s%s() must %s, %s given, called in %s on line %u and defined",
                  argn, cls, sep, nm, needmsg->val, got->val, cfile->val, cline);
}

/* 0 = ok, 1 = a diagnostic was raised. */
static int arg_default_is_null(zend_arg_info *ai)
{
    zend_ast *d = ai ? ai->default_ast : NULL;
    if (!d) return 0;
    if (d->kind == AST_ZVAL) return d->val.u1 <= IS_NULL;
    if (d->kind == AST_CONST && d->val.u1 == IS_STRING)
        return zend_string_equals_ci(d->val.value.str, S("null"));
    return 0;
}

static int verify_arg_type(zend_function *fn, uint32_t argn, zend_type_hint *t, zval *slot)
{
    if (!t || t->n == 0) return 0;
    if (argn - 1 < fn->num_args && fn->args && arg_default_is_null(&fn->args[argn - 1]) &&
        zval_deref(slot)->u1 <= IS_NULL)
        return 0;
    zend_class_entry *scope = fn->scope;
    zval *d = zval_deref(slot);
    if (type_matches_exact(t, d, scope)) {
        /* int accepted where float is declared is widened by php-src */
        /* VERSIONGATE:70000 scalar widening only exists from 7.0 */
        if (EG(php_version) >= 70000 && d->u1 == IS_LONG) {
            for (uint32_t i = 0; i < t->n; i++) {
                const char *n = t->names[i]->val;
                if (*n == '?') n++;
                if (!strcasecmp(n, "int")) return 0;
            }
            for (uint32_t i = 0; i < t->n; i++) {
                const char *n = t->names[i]->val;
                if (*n == '?') n++;
                if (!strcasecmp(n, "float")) { *d = zval_double((double)d->value.lval); return 0; }
            }
        }
        return 0;
    }
    /* VERSIONGATE:70000 weak-mode scalar coercion arrived with 7.0 */
    if (EG(php_version) >= 70000 && !EG(strict_types) && d->u1 != IS_ARRAY && d->u1 != IS_OBJECT) {
        if (type_coerce_weak(t, d)) return 0;
        if (EG(exception)) return 1;
    }
    /* VERSIONGATE:70000 strict mode lets int satisfy float even so */
    if (EG(php_version) >= 70000 && EG(strict_types) && d->u1 == IS_LONG) {
        for (uint32_t i = 0; i < t->n; i++) {
            const char *n = t->names[i]->val;
            if (*n == '?') n++;
            if (!strcasecmp(n, "float")) { *d = zval_double((double)d->value.lval); return 0; }
        }
    }
    arg_type_error(fn, argn, t, d, 0);
    return 1;
}

static int verify_return_type(zend_function *fn, zval *v)
{
    zend_type_hint *t = fn->return_type;
    if (!t || t->n == 0) return 0;
    if (t->n == 1 && (!strcasecmp(t->names[0]->val, "void") ||
                      !strcasecmp(t->names[0]->val, "never"))) return 0;
    zval *d = zval_deref(v);
    if (type_matches_exact(t, d, fn->scope)) {
        if (d->u1 == IS_LONG) {
            int wants_int = 0, wants_float = 0;
            for (uint32_t i = 0; i < t->n; i++) {
                const char *n = t->names[i]->val;
                if (*n == '?') n++;
                if (!strcasecmp(n, "int")) wants_int = 1;
                if (!strcasecmp(n, "float")) wants_float = 1;
            }
            if (!wants_int && wants_float) *d = zval_double((double)d->value.lval);
        }
        return 0;
    }
    /* VERSIONGATE:70000 return types and weak coercion are 7.0 only */
    if (EG(php_version) >= 70000 && !EG(strict_types) && d->u1 != IS_ARRAY && d->u1 != IS_OBJECT) {
        if (type_coerce_weak(t, d)) return 0;
        if (EG(exception)) return 1;
    }
    /* VERSIONGATE:70000 strict mode still widens int to float */
    if (EG(php_version) >= 70000 && EG(strict_types) && d->u1 == IS_LONG) {
        for (uint32_t i = 0; i < t->n; i++) {
            const char *n = t->names[i]->val;
            if (*n == '?') n++;
            if (!strcasecmp(n, "float")) { *d = zval_double((double)d->value.lval); return 0; }
        }
    }
    zend_string *need = type_hint_string(t);
    zend_string *got = given_description(d);
    const char *cls = fn->scope ? fn->scope->name->val : "";
    const char *sep = fn->scope ? "::" : "";
    const char *nm = fn->name ? fn->name->val : "{closure}";
    /* VERSIONGATE:80000 else-branch is the 7.x wording */
    if (EG(php_version) >= 80000)
        zend_throw_error("TypeError", "%s%s%s(): Return value must be of type %s, %s returned",
                         cls, sep, nm, need->val, got->val);
    else
        zend_throw_error("TypeError", "Return value of %s%s%s() must be of the type %s, %s returned",
                         cls, sep, nm, need->val, got->val);
    return 1;
}

/* ======================================================================== */
/*  frame construction and parameter binding                                */
/* ======================================================================== */

static void too_few_arguments(zend_function *fn, uint32_t argc, uint32_t argn)
{
    const char *cls = fn->scope ? fn->scope->name->val : "";
    const char *sep = fn->scope ? "::" : "";
    const char *nm  = fn->name ? fn->name->val : "{closure}";
    /* VERSIONGATE:70100 else-branch is the 5.6 behaviour (a warning, and the
     * parameter is simply left undefined) */
    if (EG(php_version) >= 70100) {
        uint32_t required = 0;
        for (uint32_t i = 0; i < fn->num_args; i++)
            if (!fn->args[i].default_ast && !fn->args[i].variadic) required = i + 1;
        int exact = (required == fn->num_args) && !(fn->flags & ZEND_ACC_VARIADIC);
        zend_throw_error("ArgumentCountError",
                         "Too few arguments to function %s%s%s(), %u passed in %s on line %u and %s %u expected",
                         cls, sep, nm, argc, EG(filename) ? EG(filename)->val : "", EG(lineno),
                         exact ? "exactly" : "at least", required);
        return;
    }
    zend_error_at(E_WARNING, fn->filename ? fn->filename : EG(filename), fn->lineno,
                  "Missing argument %u for %s%s%s(), called in %s on line %u and defined",
                  argn, cls, sep, nm, EG(filename) ? EG(filename)->val : "", EG(lineno));
}

zend_execute_data *zend_build_frame(zend_function *fn, zend_object *This,
                                    zend_class_entry *called_scope,
                                    uint32_t argc, zval *argv,
                                    zend_string **argnames, zval **arg_slots,
                                    int *failed)
{
    *failed = 0;
    zend_execute_data *ex = ecalloc(1, sizeof(zend_execute_data));
    ex->prev = EG(current);
    ex->symbols = zend_hash_new();
    ex->func = fn;
    ex->called_scope = called_scope ? called_scope : (This ? This->ce : fn->scope);
    ex->This = This;
    ex->filename = fn->filename ? fn->filename : EG(filename);
    ex->lineno = EG(lineno);
    ex->num_args = argc;
    ex->arg_names = argnames;
    if (argc) {
        ex->args = ecalloc(argc, sizeof(zval));
        for (uint32_t i = 0; i < argc; i++) ex->args[i] = zval_copy(*zval_deref(&argv[i]));
    }

    uint32_t nparams = fn->num_args;
    uint32_t npos = 0;
    while (npos < argc && !(argnames && argnames[npos])) npos++;

    uint32_t maxp = nparams ? nparams : 1;
    zval **bound = ecalloc(maxp, sizeof(zval *));
    zval **bslot = ecalloc(maxp, sizeof(zval *));
    unsigned char *bnamed = ecalloc(maxp, 1);
    for (uint32_t i = 0; i < npos && i < nparams; i++) {
        bound[i] = &argv[i];
        bslot[i] = arg_slots ? arg_slots[i] : NULL;
    }

    HashTable *extra_named = NULL;
    /* VERSIONGATE:80000 named arguments do not exist before 8.0 */
    for (uint32_t i = npos; i < argc; i++) {
        if (!argnames || !argnames[i]) continue;
        int found = -1;
        for (uint32_t p = 0; p < nparams; p++)
            if (fn->args && fn->args[p].name && !fn->args[p].variadic &&
                zend_string_equals(fn->args[p].name, argnames[i])) { found = (int)p; break; }
        if (found < 0) {
            if (fn->flags & ZEND_ACC_VARIADIC) {
                if (!extra_named) extra_named = zend_hash_new();
                zend_hash_update(extra_named, argnames[i], zval_copy(*zval_deref(&argv[i])));
                continue;
            }
            zend_throw_error("Error", "Unknown named parameter $%s", argnames[i]->val);
            *failed = 1;
            return ex;
        }
        if (bound[found]) {
            zend_throw_error("Error", "Named parameter $%s overwrites previous argument",
                             argnames[i]->val);
            *failed = 1;
            return ex;
        }
        bound[found] = &argv[i];
        bslot[found] = arg_slots ? arg_slots[i] : NULL;
        bnamed[found] = 1;
    }

    /* VERSIONGATE:80000 BEGIN - a named argument that is not a variable can
     * never satisfy a by-reference parameter, and php-src says so before it
     * complains about anything missing */
    for (uint32_t p = 0; p < nparams; p++) {
        if (!bnamed[p] || !bound[p] || !fn->args[p].by_ref) continue;
        if (bound[p]->u1 == IS_REFERENCE || bslot[p]) continue;
        zend_throw_error("Error", "%s%s%s(): Argument #%u ($%s) cannot be passed by reference",
                         fn->scope ? fn->scope->name->val : "", fn->scope ? "::" : "",
                         fn->name ? fn->name->val : "{closure}", p + 1,
                         fn->args[p].name ? fn->args[p].name->val : "");
        *failed = 1;
        return ex;
    }
    /* VERSIONGATE:80000 END */

    for (uint32_t p = 0; p < nparams; p++) {
        zend_arg_info *ai = &fn->args[p];
        if (ai->variadic) {
            HashTable *rest = zend_hash_new();
            for (uint32_t i = p; i < npos; i++) {
                if (ai->by_ref) {
                    zval *src = &argv[i];
                    zend_reference *r;
                    if (src->u1 == IS_REFERENCE) r = src->value.ref;
                    else if (arg_slots && arg_slots[i]) r = zend_make_reference(arg_slots[i]);
                    else { r = ecalloc(1, sizeof(zend_reference)); r->refcount = 1; r->val = zval_copy(*src); }
                    r->refcount++;
                    zval box; box.u1 = IS_REFERENCE; box.value.ref = r;
                    zend_hash_next_index_insert(rest, box);
                } else {
                    zend_hash_next_index_insert(rest, zval_copy(*zval_deref(&argv[i])));
                }
            }
            if (extra_named) {
                ZEND_HASH_FOREACH(extra_named, b) {
                    zend_hash_update(rest, b->key, zval_copy(b->val));
                } ZEND_HASH_FOREACH_END();
            }
            if (ai->name) zend_hash_update(ex->symbols, ai->name, zval_array(rest));
            break;
        }
        zval *src = bound[p];
        if (src) {
            if (ai->by_ref) {
                zend_reference *r;
                if (src->u1 == IS_REFERENCE) r = src->value.ref;
                else if (bslot[p]) r = zend_make_reference(bslot[p]);
                else { r = ecalloc(1, sizeof(zend_reference)); r->refcount = 1; r->val = zval_copy(*src); }
                r->refcount++;
                zval box; box.u1 = IS_REFERENCE; box.value.ref = r;
                zend_hash_update(ex->symbols, ai->name, box);
                if (ai->type && verify_arg_type(fn, p + 1, ai->type, &r->val)) { *failed = 1; return ex; }
            } else {
                zval v = zval_copy(*zval_deref(src));
                zval *slot = zend_hash_update(ex->symbols, ai->name, v);
                if (ai->type && verify_arg_type(fn, p + 1, ai->type, slot)) { *failed = 1; return ex; }
            }
            continue;
        }
        if (ai->default_ast) {
            scope_frame f;
            push_scope_frame(&f, fn->scope);
            zval dv = zval_null();
            int rc = zend_eval_expr(ai->default_ast, &dv);
            pop_scope_frame(&f);
            if (rc) { *failed = 1; return ex; }
            zend_hash_update(ex->symbols, ai->name, dv);
            continue;
        }
        /* a class-typed parameter with no argument still reports the type */
        /* VERSIONGATE:70100 else-branch is the 5.6 behaviour */
        if (EG(php_version) < 70100 && ai->type && ai->type->n) {
            arg_type_error(fn, p + 1, ai->type, NULL, 1);
            *failed = 1;
            return ex;
        }
        too_few_arguments(fn, argc, p + 1);
        if (EG(exception)) { *failed = 1; return ex; }
        /* 5.6/7.0 leave the parameter undefined on purpose */
    }

    if (This) zend_hash_update(ex->symbols, S("this"), zval_object(This));

    /* VERSIONGATE:80000 BEGIN - with named arguments func_get_args() reports
     * the parameters as they ended up bound, defaults included */
    if (argnames) {
        uint32_t top = 0;
        for (uint32_t p = 0; p < nparams; p++) if (bound[p]) top = p + 1;
        if (top) {
            ex->args = ecalloc(top, sizeof(zval));
            ex->num_args = top;
            for (uint32_t p = 0; p < top; p++) {
                zval *v = fn->args[p].name ? zend_hash_find(ex->symbols, fn->args[p].name) : NULL;
                ex->args[p] = v ? zval_copy(*zval_deref(v)) : zval_null();
            }
        }
    }
    /* VERSIONGATE:80000 END */
    efree(bound);
    efree(bslot);
    efree(bnamed);
    return ex;
}

/* ======================================================================== */
/*  calling                                                                 */
/* ======================================================================== */

ex_status zend_execute_function_body(zend_function *fn, zval *retval)
{
    *retval = zval_null();
    if (!fn->body) return EX_NORMAL;
    zend_hoist_declarations(fn->body);
    ex_status st = zend_execute_ast(fn->body, retval);
    if (st == EX_GOTO) {
        report_bad_goto(fn->body);
        return EX_EXCEPTION;
    }
    if (st == EX_BREAK || st == EX_CONTINUE) {
        zend_fatal("Cannot break/continue %d level", zend_break_levels);
        return EX_EXCEPTION;
    }
    return st;
}

int zend_call_function_ex(zend_function *fn, zend_object *thisptr,
                          zend_class_entry *cscope,
                          uint32_t argc, zval *argv, zend_string **argnames,
                          zval *retval, zval **arg_slots)
{
    *retval = zval_null();
    if (!fn) { zend_throw_error("Error", "Call to undefined function"); return 1; }
    if (EG(exception) || EG(exiting)) return 1;

    if (fn->is_internal) {
        if (argc < fn->min_args) {
            zend_wrong_param_count(fn->name ? fn->name->val : "?", argc, fn->min_args, fn->num_args);
            return 1;
        }
        if (!thisptr && !fn->scope) {
            /* A plain internal function runs in the caller's frame on purpose:
             * func_get_args(), compact() and get_called_class() all read
             * EG(current) and must see the *user* function that called them.
             * An internal *method* does get a frame, so that $this and the
             * late-static-binding scope are reachable from the handler. */
            fn->handler(retval, argc, argv);
            return (EG(exception) || EG(exiting)) ? 1 : 0;
        }
        /* heap allocated: a generator created inside the method keeps a
         * pointer to this frame long after the call returns */
        zend_execute_data *iex = ecalloc(1, sizeof(zend_execute_data));
        iex->prev = EG(current);
        iex->symbols = EG(current) ? EG(current)->symbols : EG(symbol_table);
        iex->func = fn;
        iex->called_scope = cscope ? cscope : (thisptr ? thisptr->ce : fn->scope);
        iex->This = thisptr;
        iex->filename = EG(filename);
        iex->lineno = EG(lineno);
        iex->args = argv;
        iex->num_args = argc;
        EG(current) = iex;
        fn->handler(retval, argc, argv);
        EG(current) = iex->prev;
        return (EG(exception) || EG(exiting)) ? 1 : 0;
    }

    if (zend_call_depth >= ZEND_MAX_CALL_DEPTH) {
        zend_fatal("Allowed memory size of %d bytes exhausted (tried to allocate %d bytes)",
                   134217728, 262144);
        return 1;
    }

    int failed = 0;
    zend_execute_data *ex = zend_build_frame(fn, thisptr, cscope, argc, argv,
                                             argnames, arg_slots, &failed);
    if (failed) return 1;

    if (zend_function_is_generator(fn)) {
        zend_object *g = zend_generator_create(ex);
        if (!g) return 1;
        *retval = zval_object(g);
        return 0;
    }

    zend_string *sfile = EG(filename);
    uint32_t sline = EG(lineno);
    EG(current) = ex;
    EG(filename) = ex->filename;
    zend_call_depth++;
    zval rv = zval_null();
    ex_status st = zend_execute_function_body(fn, &rv);
    zend_call_depth--;
    EG(current) = ex->prev;
    EG(filename) = sfile;
    EG(lineno) = sline;

    if (st == EX_EXCEPTION || st == EX_EXIT || EG(exception) || EG(exiting)) return 1;
    if (fn->return_type && verify_return_type(fn, &rv)) return 1;
    *retval = rv;
    return 0;
}

/* ======================================================================== */
/*  callables                                                               */
/* ======================================================================== */

#define ZEND_ACC_CALL_TRAMPOLINE (1u << 20)
static zend_function *make_call_trampoline(zend_function *magic, zend_class_entry *ce,
                                           zend_string *name);

/* "self", "parent" and "static" are legal class names in a callable. */
static zend_class_entry *resolve_callable_class(zend_string *n)
{
    if (zend_string_equals_ci(n, S("self")))   return zend_get_current_scope();
    if (zend_string_equals_ci(n, S("parent"))) {
        zend_class_entry *c = zend_get_current_scope();
        return c ? c->parent : NULL;
    }
    if (zend_string_equals_ci(n, S("static"))) return called_scope();
    return zend_lookup_class(n, 1);
}

int zend_is_callable_zval(zval *callable, zend_function **fnp, zend_object **objp,
                          zend_class_entry **scopep, zend_string **err)
{
    if (fnp) *fnp = NULL;
    if (objp) *objp = NULL;
    if (scopep) *scopep = NULL;
    if (err) *err = NULL;
    zval *c = zval_deref(callable);

    if (c->u1 == IS_STRING) {
        zend_string *s = c->value.str;
        const char *cc = NULL;
        for (size_t i = 0; i + 1 < s->len; i++)
            if (s->val[i] == ':' && s->val[i + 1] == ':') { cc = s->val + i; break; }
        if (cc) {
            zend_string *cn = zend_string_init(s->val, (size_t)(cc - s->val));
            zend_string *mn = zend_string_initz(cc + 2);
            zend_class_entry *ce = resolve_callable_class(cn);
            if (!ce) {
                if (err) *err = zend_strpprintf("class \"%s\" not found", cn->val);
                return 0;
            }
            zend_function *m = zend_lookup_method(ce, zend_string_tolower(mn));
            if (!m) {
                zend_object *cur = EG(current) ? EG(current)->This : NULL;
                if (cur && zend_instanceof(cur->ce, ce) && ce->__call) {
                    if (fnp) *fnp = make_call_trampoline(ce->__call, ce, mn);
                    if (objp) *objp = cur;
                    if (scopep) *scopep = ce;
                    return 1;
                }
                if (ce->__callstatic || ce->__call) {
                    if (fnp) *fnp = make_call_trampoline(ce->__callstatic ? ce->__callstatic : ce->__call, ce, mn);
                    if (scopep) *scopep = ce;
                    return 1;
                }
                if (err) *err = zend_strpprintf("class %s does not have a method \"%s\"",
                                                ce->name->val, mn->val);
                return 0;
            }
            if (fnp) *fnp = m;
            if (scopep) *scopep = ce;
            return 1;
        }
        zend_function *f = zend_lookup_function(zend_string_tolower(s));
        if (!f) {
            if (err) *err = zend_strpprintf("function \"%s\" not found or invalid function name", s->val);
            return 0;
        }
        if (fnp) *fnp = f;
        return 1;
    }

    if (c->u1 == IS_ARRAY) {
        HashTable *ht = c->value.arr;
        if (zend_hash_num_elements(ht) != 2) {
            if (err) *err = zend_string_initz("array callback must have exactly two members");
            return 0;
        }
        zval *o = zend_hash_index_find(ht, 0);
        zval *m = zend_hash_index_find(ht, 1);
        if (!o || !m) { if (err) *err = zend_string_initz("array callback has to contain indices 0 and 1"); return 0; }
        o = zval_deref(o); m = zval_deref(m);
        if (m->u1 != IS_STRING) {
            if (err) *err = zend_string_initz("second array member is not a valid method");
            return 0;
        }
        zend_class_entry *ce = NULL;
        zend_object *obj = NULL;
        if (o->u1 == IS_OBJECT) { obj = o->value.obj; ce = obj->ce; }
        else if (o->u1 == IS_STRING) {
            ce = resolve_callable_class(o->value.str);
            if (!ce) { if (err) *err = zend_strpprintf("class \"%s\" not found", o->value.str->val); return 0; }
        } else {
            if (err) *err = zend_string_initz("first array member is not a valid class name or object");
            return 0;
        }
        zend_string *mn = m->value.str;
        const char *cc = NULL;
        for (size_t i = 0; i + 1 < mn->len; i++)
            if (mn->val[i] == ':' && mn->val[i + 1] == ':') { cc = mn->val + i; break; }
        if (cc) {
            zend_string *pn = zend_string_init(mn->val, (size_t)(cc - mn->val));
            zend_class_entry *pce = resolve_callable_class(pn);
            if (pce) ce = pce;
            mn = zend_string_initz(cc + 2);
        }
        zend_function *f = zend_lookup_method(ce, zend_string_tolower(mn));
        if (!f) {
            zend_object *cur = EG(current) ? EG(current)->This : NULL;
            zend_function *magic = NULL;
            if (obj) magic = ce->__call;
            else if (cur && zend_instanceof(cur->ce, ce) && ce->__call) { magic = ce->__call; obj = cur; }
            else magic = ce->__callstatic ? ce->__callstatic : ce->__call;
            if (magic) {
                if (fnp) *fnp = make_call_trampoline(magic, ce, mn);
                if (objp) *objp = obj;
                if (scopep) *scopep = ce;
                return 1;
            }
            if (err) *err = zend_strpprintf("class %s does not have a method \"%s\"", ce->name->val, mn->val);
            return 0;
        }
        if (fnp) *fnp = f;
        if (objp) *objp = obj;
        if (scopep) *scopep = ce;
        return 1;
    }

    if (c->u1 == IS_OBJECT) {
        zend_object *obj = c->value.obj;
        zend_closure *cl = (zend_ce_closure && obj->ce == zend_ce_closure) ? (zend_closure *)obj->internal : NULL;
        if (cl) {
            if (fnp) *fnp = cl->fn;
            if (objp) *objp = cl->This;
            if (scopep) *scopep = cl->called_scope ? cl->called_scope : cl->scope;
            return 1;
        }
        if (obj->ce && obj->ce->__invoke) {
            if (fnp) *fnp = obj->ce->__invoke;
            if (objp) *objp = obj;
            if (scopep) *scopep = obj->ce;
            return 1;
        }
        if (err) *err = zend_string_initz("object of that class is not callable");
        return 0;
    }

    if (err) *err = zend_string_initz("no array or string given");
    return 0;
}

static int call_resolved(zend_function *fn, zend_object *obj, zend_class_entry *scope,
                         zval *callable, uint32_t argc, zval *argv, zend_string **names,
                         zval *retval, zval **slots)
{
    /* a Closure keeps its own bound scope and use() variables */
    if (callable) {
        zval *c = zval_deref(callable);
        if (c->u1 == IS_OBJECT && (zend_ce_closure && c->value.obj->ce == zend_ce_closure))
            return zend_closure_call(c->value.obj, argc, argv, names, retval, slots);
    }
    if (fn && (fn->flags & ZEND_ACC_CALL_TRAMPOLINE)) {
        HashTable *ht = zend_hash_new();
        for (uint32_t i = 0; i < argc; i++)
            zend_hash_next_index_insert(ht, zval_copy(*zval_deref(&argv[i])));
        zval margs[2];
        margs[0] = zval_zstr(fn->name);
        margs[1] = zval_array(ht);
        return zend_call_function_ex(fn->prototype, obj, scope, 2, margs, NULL, retval, NULL);
    }
    return zend_call_function_ex(fn, obj, scope, argc, argv, names, retval, slots);
}

int zend_call_user_callable(zval *callable, uint32_t argc, zval *argv, zval *retval)
{
    zend_function *fn = NULL;
    zend_object *obj = NULL;
    zend_class_entry *scope = NULL;
    zend_string *err = NULL;
    *retval = zval_null();
    if (!zend_is_callable_zval(callable, &fn, &obj, &scope, &err)) {
        zend_string *s = zval_get_string(zval_deref(callable));
        /* VERSIONGATE:80000 else-branch is the 5.6 behaviour */
        if (EG(php_version) >= 80000)
            zend_throw_error("TypeError", "Invalid callback %s, %s", s->val,
                             err ? err->val : "not callable");
        else
            zend_error(E_WARNING, "Invalid callback %s, %s", s->val, err ? err->val : "not callable");
        return 1;
    }
    return call_resolved(fn, obj, scope, callable, argc, argv, NULL, retval, NULL);
}

/* ======================================================================== */
/*  Closure                                                                 */
/* ======================================================================== */

static unsigned char closure_methods_bound = 0;

static zend_closure *closure_of(zend_object *o)
{
    return (o && zend_ce_closure && o->ce == zend_ce_closure) ? (zend_closure *)o->internal : NULL;
}

static void zif_closure_bindto(zval *ret, uint32_t argc, zval *argv);
static void zif_closure_bind(zval *ret, uint32_t argc, zval *argv);
/* VERSIONGATE:70000 Closure::call() arrived in 7.0 */
static void zif_closure_call(zval *ret, uint32_t argc, zval *argv);
/* VERSIONGATE:70100 Closure::fromCallable() arrived in 7.1 */
static void zif_closure_fromcallable(zval *ret, uint32_t argc, zval *argv);
static void zif_closure_invoke(zval *ret, uint32_t argc, zval *argv);

/* Closure's class entry is registered by Zend/zend_exceptions.c; the method
 * handlers belong here, so they are attached the first time we need them. */
static zend_class_entry *closure_ce(void)
{
    if (!zend_ce_closure) return NULL;
    if (!closure_methods_bound) {
        closure_methods_bound = 1;
        zend_class_add_method(zend_ce_closure, "bindTo", zif_closure_bindto, 1, 2,
                              ZEND_ACC_PUBLIC);
        zend_class_add_method(zend_ce_closure, "bind", zif_closure_bind, 2, 3,
                              ZEND_ACC_PUBLIC | ZEND_ACC_STATIC);
        /* VERSIONGATE:70000 Closure::call() arrived in 7.0 */
        zend_class_add_method(zend_ce_closure, "call", zif_closure_call, 1, 16,
                              ZEND_ACC_PUBLIC);
        /* VERSIONGATE:70100 Closure::fromCallable() arrived in 7.1 */
        zend_class_add_method(zend_ce_closure, "fromCallable", zif_closure_fromcallable, 1, 1,
                              ZEND_ACC_PUBLIC | ZEND_ACC_STATIC);
        zend_class_add_method(zend_ce_closure, "__invoke", zif_closure_invoke, 0, 16,
                              ZEND_ACC_PUBLIC);
    }
    return zend_ce_closure;
}

zend_object *zend_create_closure(zend_function *fn, zend_class_entry *scope,
                                 zend_class_entry *called_scope, zend_object *This,
                                 HashTable *bound, int is_static, int is_arrow)
{
    zend_object *o = zend_object_new(closure_ce());
    zend_closure *cl = ecalloc(1, sizeof(zend_closure));
    cl->fn = fn;
    cl->scope = scope;
    cl->called_scope = called_scope;
    cl->This = is_static ? NULL : This;
    cl->bound = bound;
    cl->is_static = (unsigned char)is_static;
    cl->is_arrow = (unsigned char)is_arrow;
    o->internal = cl;
    return o;
}

int zend_closure_call(zend_object *co, uint32_t argc, zval *argv,
                      zend_string **names, zval *retval, zval **slots)
{
    zend_closure *cl = closure_of(co);
    *retval = zval_null();
    if (!cl) { zend_throw_error("Error", "Not a closure"); return 1; }
    if (EG(exception) || EG(exiting)) return 1;
    if (zend_call_depth >= ZEND_MAX_CALL_DEPTH) {
        zend_fatal("Allowed memory size of %d bytes exhausted (tried to allocate %d bytes)",
                   134217728, 262144);
        return 1;
    }

    zend_function *fn = cl->fn;
    zend_function *use_fn = fn;
    if (cl->scope != fn->scope) {
        /* heap allocated: a generator built from this call keeps the pointer */
        zend_function *scoped = ecalloc(1, sizeof(zend_function));
        *scoped = *fn;
        scoped->scope = cl->scope;
        use_fn = scoped;
    }

    int failed = 0;
    zend_execute_data *ex = zend_build_frame(use_fn, cl->This,
                                             cl->called_scope ? cl->called_scope : cl->scope,
                                             argc, argv, names, slots, &failed);
    if (failed) return 1;
    ex->is_arrow_fn = cl->is_arrow;
    if (cl->bound) {
        ZEND_HASH_FOREACH(cl->bound, b) {
            if (!zend_hash_find(ex->symbols, b->key))
                zend_hash_update(ex->symbols, b->key, zval_copy(b->val));
        } ZEND_HASH_FOREACH_END();
    }
    if (cl->This) zend_hash_update(ex->symbols, S("this"), zval_object(cl->This));

    if (zend_function_is_generator(fn)) {
        zend_object *g = zend_generator_create(ex);
        if (!g) return 1;
        *retval = zval_object(g);
        return 0;
    }

    zend_string *sfile = EG(filename);
    uint32_t sline = EG(lineno);
    EG(current) = ex;
    EG(filename) = ex->filename;
    zend_call_depth++;
    cl->running++;
    zval rv = zval_null();
    ex_status st = zend_execute_function_body(fn, &rv);
    cl->running--;
    zend_call_depth--;
    EG(current) = ex->prev;
    EG(filename) = sfile;
    EG(lineno) = sline;
    if (st == EX_EXCEPTION || st == EX_EXIT || EG(exception) || EG(exiting)) return 1;
    if (fn->return_type && verify_return_type(fn, &rv)) return 1;
    *retval = rv;
    return 0;
}

zend_object *zend_closure_bind(zend_object *co, zval *newthis, zval *scope_arg, zend_string **err)
{
    zend_closure *cl = closure_of(co);
    if (err) *err = NULL;
    if (!cl) return NULL;
    zend_object *This = NULL;
    if (newthis) {
        zval *d = zval_deref(newthis);
        if (d->u1 == IS_OBJECT) This = d->value.obj;
    }
    if (This && cl->is_static) {
        if (err) *err = zend_string_initz("Cannot bind an instance to a static closure");
        return NULL;
    }
    zend_class_entry *scope = cl->scope;
    if (scope_arg) {
        zval *d = zval_deref(scope_arg);
        if (d->u1 == IS_OBJECT) scope = d->value.obj->ce;
        else if (d->u1 == IS_STRING) {
            if (zend_string_equals_ci(d->value.str, S("static"))) scope = cl->scope;
            else {
                scope = zend_lookup_class(d->value.str, 1);
                if (!scope) {
                    if (err) *err = zend_strpprintf("Class '%s' not found", d->value.str->val);
                    return NULL;
                }
            }
        } else if (d->u1 <= IS_NULL) scope = NULL;
    } else if (This) {
        scope = This->ce;
    }
    HashTable *bound = cl->bound ? zend_hash_dup(cl->bound) : NULL;
    return zend_create_closure(cl->fn, scope, This ? This->ce : scope, This, bound,
                               cl->is_static, cl->is_arrow);
}

static void zif_closure_bindto(zval *ret, uint32_t argc, zval *argv)
{
    zend_object *self = zend_get_this();
    zend_string *err = NULL;
    *ret = zval_null();
    if (!self) return;
    zend_object *n = zend_closure_bind(self, argc > 0 ? &argv[0] : NULL,
                                       argc > 1 ? &argv[1] : NULL, &err);
    if (!n) { if (err) zend_error(E_WARNING, "%s", err->val); return; }
    *ret = zval_object(n);
}

static void zif_closure_bind(zval *ret, uint32_t argc, zval *argv)
{
    *ret = zval_null();
    if (argc < 1) return;
    zval *c = zval_deref(&argv[0]);
    if (c->u1 != IS_OBJECT) return;
    zend_string *err = NULL;
    zend_object *n = zend_closure_bind(c->value.obj, argc > 1 ? &argv[1] : NULL,
                                       argc > 2 ? &argv[2] : NULL, &err);
    if (!n) { if (err) zend_error(E_WARNING, "%s", err->val); return; }
    *ret = zval_object(n);
}

/* VERSIONGATE:70000 Closure::call() arrived in 7.0 */
static void zif_closure_call(zval *ret, uint32_t argc, zval *argv)
{
    zend_object *self = zend_get_this();
    *ret = zval_null();
    if (!self || argc < 1) return;
    zval *nt = zval_deref(&argv[0]);
    if (nt->u1 != IS_OBJECT) {
        zend_error(E_WARNING, "Cannot bind closure to non-object");
        return;
    }
    zend_string *err = NULL;
    zend_object *n = zend_closure_bind(self, nt, nt, &err);
    if (!n) { if (err) zend_error(E_WARNING, "%s", err->val); return; }
    zend_closure_call(n, argc - 1, argv + 1, NULL, ret, NULL);
}

/* VERSIONGATE:70100 Closure::fromCallable() arrived in 7.1 */
static void zif_closure_fromcallable(zval *ret, uint32_t argc, zval *argv)
{
    *ret = zval_null();
    if (argc < 1) return;
    zval *c = zval_deref(&argv[0]);
    if (c->u1 == IS_OBJECT && (zend_ce_closure && c->value.obj->ce == zend_ce_closure)) { *ret = zval_copy(*c); return; }
    zend_function *fn = NULL; zend_object *obj = NULL; zend_class_entry *sc = NULL;
    zend_string *err = NULL;
    if (!zend_is_callable_zval(c, &fn, &obj, &sc, &err)) {
        zend_throw_error("TypeError", "Failed to create closure from callable: %s",
                         err ? err->val : "not callable");
        return;
    }
    *ret = zval_object(zend_create_closure(fn, sc ? sc : fn->scope, sc, obj, NULL,
                                           (fn->flags & ZEND_ACC_STATIC) != 0, 0));
}

static void zif_closure_invoke(zval *ret, uint32_t argc, zval *argv)
{
    zend_object *self = zend_get_this();
    *ret = zval_null();
    if (!self) return;
    zend_closure_call(self, argc, argv, NULL, ret, NULL);
}

/* ======================================================================== */
/*  closure literals                                                        */
/* ======================================================================== */

static int eval_closure_node(zend_ast *ast, zval *result)
{
    zend_function *fn = (zend_function *)ZEND_ZVAL_PTR(ast->val);
    if (!fn) { *result = zval_null(); return 0; }
    int is_static = (fn->flags & ZEND_ACC_STATIC) != 0;
    zend_class_entry *scope = zend_get_current_scope();
    zend_object *This = (EG(current) && !is_static) ? EG(current)->This : NULL;
    HashTable *bound = NULL;

    /* VERSIONGATE:70400 arrow functions capture the whole parent scope by value */
    if (ast->kind == AST_ARROW_FN) {
        bound = zend_hash_new();
        HashTable *src = cur_symbols();
        if (src) {
            ZEND_HASH_FOREACH(src, b) {
                if (b->key && !zend_string_equals(b->key, S("this")))
                    zend_hash_update(bound, b->key, zval_copy(b->val));
            } ZEND_HASH_FOREACH_END();
        }
    } else if (ast->nchild > 0 && ast->child[0]) {
        zend_ast *uses = ast->child[0];
        bound = zend_hash_new();
        for (uint32_t i = 0; i < uses->nchild; i++) {
            zend_ast *u = uses->child[i];
            if (!u) continue;
            zend_string *nm = ast_name(u);
            if (!nm && u->kind == AST_ZVAL && u->val.u1 == IS_STRING) nm = u->val.value.str;
            if (!nm) continue;
            HashTable *src = cur_symbols();
            if (u->attr & ZEND_ARRAY_ELEM_BY_REF) {
                zval *slot = zend_hash_find(src, nm);
                if (!slot) slot = zend_hash_update(src, nm, zval_null());
                zend_reference *r = zend_make_reference(slot);
                r->refcount++;
                zval box; box.u1 = IS_REFERENCE; box.value.ref = r;
                zend_hash_update(bound, nm, box);
            } else {
                zval *slot = zend_hash_find(src, nm);
                if (!slot || slot->u1 == IS_UNDEF) {
                    undef_var_error(nm);
                    zend_hash_update(bound, nm, zval_null());
                } else {
                    zend_hash_update(bound, nm, zval_copy(*zval_deref(slot)));
                }
            }
        }
    }
    zend_object *o = zend_create_closure(fn, scope, called_scope(), This, bound,
                                         is_static, ast->kind == AST_ARROW_FN);
    *result = zval_object(o);
    return 0;
}

/* ======================================================================== */
/*  argument lists                                                          */
/* ======================================================================== */

/* A __call/__callStatic trampoline: a fake zend_function carrying the name the
 * caller used, with the magic method itself in ->prototype. */
static zend_function *make_call_trampoline(zend_function *magic, zend_class_entry *ce,
                                           zend_string *name)
{
    zend_function *t = ecalloc(1, sizeof(zend_function));
    t->name = name;
    t->lcname = zend_string_tolower(name);
    t->flags = ZEND_ACC_PUBLIC | ZEND_ACC_CALL_TRAMPOLINE | (magic->flags & ZEND_ACC_STATIC);
    t->scope = ce;
    t->prototype = magic;
    t->filename = magic->filename;
    t->lineno = magic->lineno;
    return t;
}

typedef struct {
    zval *argv;
    zend_string **names;
    zval **slots;
    uint32_t n, cap;
} arglist_t;

static void arg_push(arglist_t *a, zval v, zend_string *name, zval *slot)
{
    if (a->n == a->cap) {
        uint32_t nc = a->cap ? a->cap * 2 : 8;
        a->argv  = erealloc(a->argv,  nc * sizeof(zval));
        a->names = erealloc(a->names, nc * sizeof(zend_string *));
        a->slots = erealloc(a->slots, nc * sizeof(zval *));
        a->cap = nc;
    }
    a->argv[a->n] = v;
    a->names[a->n] = name;
    a->slots[a->n] = slot;
    a->n++;
}

static int param_by_ref(zend_function *fn, uint32_t i)
{
    if (!fn || !fn->args) return 0;
    if (i < fn->num_args) return fn->args[i].by_ref;
    if ((fn->flags & ZEND_ACC_VARIADIC) && fn->num_args)
        return fn->args[fn->num_args - 1].by_ref;
    return 0;
}

static int is_var_ast(zend_ast *a)
{
    if (!a) return 0;
    switch (a->kind) {
    case AST_VAR: case AST_DIM: case AST_PROP: case AST_NULLSAFE_PROP:
    case AST_STATIC_PROP:
        return 1;
    default:
        return 0;
    }
}

static int eval_args_for(zend_function *fn, zend_ast *args, arglist_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!args) return 0;
    for (uint32_t i = 0; i < args->nchild; i++) {
        zend_ast *c = args->child[i];
        if (!c) continue;
        if (c->kind == AST_SPREAD ||
            (c->kind == AST_ARRAY_ELEM && (c->attr & ZEND_ARRAY_ELEM_SPREAD))) {
            /* VERSIONGATE:70000 argument unpacking is 5.6, string keys are 8.1 */
            zval v = zval_null();
            if (zend_eval_expr(c->child[0], &v)) return 1;
            HashTable *tmp = zend_hash_new();
            /* VERSIONGATE:80000 BEGIN - a string key unpacks as a named argument */
            int keep_keys = EG(php_version) >= 80000;
            if (append_spread_ex(tmp, &v, keep_keys)) return 1;
            /* VERSIONGATE:80000 END */
            ZEND_HASH_FOREACH(tmp, b) {
                arg_push(out, zval_copy(b->val), b->key, NULL);
            } ZEND_HASH_FOREACH_END();
            continue;
        }
        if (c->kind == AST_NAMED_ARG) {
            /* VERSIONGATE:80000 BEGIN - named arguments are 8.0 */
            zend_string *nm = c->val.u1 == IS_STRING ? c->val.value.str : NULL;
            int nref = 0;
            if (nm && fn && fn->args) {
                for (uint32_t k = 0; k < fn->num_args; k++)
                    if (fn->args[k].name && zend_string_equals(fn->args[k].name, nm))
                        { nref = fn->args[k].by_ref; break; }
            }
            if (nref && is_var_ast(c->child[0])) {
                int err = 0;
                zval *slot = fetch_lval_ex(c->child[0], FETCH_W, &err);
                if (!slot) return 1;
                zend_reference *r = zend_make_reference(slot);
                r->refcount++;
                zval box; box.u1 = IS_REFERENCE; box.value.ref = r;
                arg_push(out, box, nm, slot);
                continue;
            }
            zval v = zval_null();
            if (zend_eval_expr(c->child[0], &v)) return 1;
            arg_push(out, v, nm, NULL);
            /* VERSIONGATE:80000 END */
            continue;
        }
        uint32_t idx = out->n;
        if (param_by_ref(fn, idx)) {
            if (is_var_ast(c)) {
                int err = 0;
                zval *slot = fetch_lval_ex(c, FETCH_W, &err);
                if (!slot) {
                    if (err == 2) zend_fatal("Cannot create references to/from string offsets");
                    return 1;
                }
                zend_reference *r = zend_make_reference(slot);
                r->refcount++;
                zval box; box.u1 = IS_REFERENCE; box.value.ref = r;
                arg_push(out, box, NULL, slot);
                continue;
            }
            int is_call = (c->kind == AST_CALL || c->kind == AST_METHOD_CALL ||
                           c->kind == AST_NULLSAFE_METHOD_CALL ||
                           c->kind == AST_STATIC_CALL || c->kind == AST_NEW ||
                           c->kind == AST_CLONE);
            if (!is_call) {
                /* php-src rejects a literal or a computed temporary outright */
                zend_fatal("Only variables can be passed by reference");
                return 1;
            }
            /* VERSIONGATE:70000 else-branch is the 5.6 behaviour (E_STRICT) */
            if (EG(php_version) >= 70000)
                zend_error(E_NOTICE, "Only variables should be passed by reference");
            else
                zend_error(E_STRICT, "Only variables should be passed by reference");
        }
        zval v = zval_null();
        if (zend_eval_expr(c, &v)) return 1;
        arg_push(out, v, NULL, NULL);
    }
    return 0;
}

/* ======================================================================== */
/*  calling a resolved target                                               */
/* ======================================================================== */

static int invoke(zend_function *fn, zend_object *obj, zend_class_entry *scope,
                  arglist_t *a, zval *result)
{
    int has_named = 0;
    for (uint32_t i = 0; i < a->n; i++) if (a->names[i]) { has_named = 1; break; }
    if (fn && (fn->flags & ZEND_ACC_CALL_TRAMPOLINE)) {
        HashTable *ht = zend_hash_new();
        for (uint32_t i = 0; i < a->n; i++) {
            if (a->names[i]) zend_hash_update(ht, a->names[i], zval_copy(*zval_deref(&a->argv[i])));
            else zend_hash_next_index_insert(ht, zval_copy(*zval_deref(&a->argv[i])));
        }
        zval margs[2];
        margs[0] = zval_zstr(fn->name);
        margs[1] = zval_array(ht);
        return zend_call_function_ex(fn->prototype, obj, scope, 2, margs, NULL, result, NULL);
    }
    return zend_call_function_ex(fn, obj, scope, a->n, a->argv,
                                 has_named ? a->names : NULL, result, a->slots);
}

/* ======================================================================== */
/*  call expressions                                                        */
/* ======================================================================== */

/* VERSIONGATE:80000 BEGIN - named argument list rules */
/* php-src rejects these two shapes while compiling, before it ever looks the
 * callee up, so the diagnostic comes out even for an undefined function. */
static int validate_arg_list(zend_ast *args)
{
    if (EG(php_version) < 80000 || !args) return 0;
    int seen_named = 0, seen_unpack = 0;
    for (uint32_t i = 0; i < args->nchild; i++) {
        zend_ast *c = args->child[i];
        if (!c) continue;
        int is_unpack = (c->kind == AST_SPREAD ||
                         (c->kind == AST_ARRAY_ELEM && (c->attr & ZEND_ARRAY_ELEM_SPREAD)));
        if (c->kind == AST_NAMED_ARG) {
            if (seen_unpack) { zend_fatal("Cannot combine named arguments and argument unpacking"); return 1; }
            seen_named = 1;
            continue;
        }
        if (is_unpack) {
            if (seen_named) { zend_fatal("Cannot combine named arguments and argument unpacking"); return 1; }
            seen_unpack = 1;
            continue;
        }
        if (seen_named) { zend_fatal("Cannot use positional argument after named argument"); return 1; }
    }
    return 0;
}
/* VERSIONGATE:80000 END */

static int is_fcc(zend_ast *args)
{
    if (!args) return 0;
    if (args->kind == AST_FIRST_CLASS_CALLABLE) return 1;
    return args->nchild == 1 && args->child[0] &&
           args->child[0]->kind == AST_FIRST_CLASS_CALLABLE;
}

static int make_fcc(zend_function *fn, zend_object *obj, zend_class_entry *scope, zval *result)
{
    /* VERSIONGATE:80100 first class callable syntax is 8.1 */
    *result = zval_object(zend_create_closure(fn, scope ? scope : fn->scope, scope, obj, NULL,
                                              (fn->flags & ZEND_ACC_STATIC) != 0, 0));
    return 0;
}

static zend_function *lookup_function_name(zend_string *name, uint32_t attr)
{
    zend_function *fn = zend_lookup_function(zend_string_tolower(name));
    if (fn) return fn;
    if (attr != ZEND_NAME_FQ) {
        const char *bs = NULL;
        for (size_t i = name->len; i > 0; i--)
            if (name->val[i - 1] == '\\') { bs = name->val + i; break; }
        if (bs) fn = zend_lookup_function(zend_string_tolower(zend_string_initz(bs)));
    }
    if (!fn && name->len && name->val[0] == '\\')
        fn = zend_lookup_function(zend_string_tolower(zend_string_initz(name->val + 1)));
    return fn;
}

static void undefined_function(const char *name)
{
    /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
    if (EG(php_version) >= 70000)
        zend_throw_error("Error", "Call to undefined function %s()", name);
    else
        zend_fatal("Call to undefined function %s()", name);
}

static void undefined_method(zend_class_entry *ce, const char *name)
{
    /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
    if (EG(php_version) >= 70000)
        zend_throw_error("Error", "Call to undefined method %s::%s()", ce->name->val, name);
    else
        zend_fatal("Call to undefined method %s::%s()", ce->name->val, name);
}

static void inaccessible_method(zend_function *m, zend_class_entry *ce, const char *name)
{
    const char *vis = (m->flags & ZEND_ACC_PRIVATE) ? "private" : "protected";
    zend_class_entry *scope = zend_get_current_scope();
    if (m->scope) ce = m->scope;      /* php-src names the declaring class */
    /* VERSIONGATE:80000 else-branch is the 5.6/7.x wording */
    if (EG(php_version) >= 80000)
        zend_throw_error("Error", "Call to %s method %s::%s() from %s%s%s", vis,
                         ce->name->val, name,
                         scope ? "scope " : "global scope",
                         scope ? scope->name->val : "", "");
    else if (EG(php_version) >= 70000)
        zend_throw_error("Error", "Call to %s method %s::%s() from context '%s'", vis,
                         ce->name->val, name, scope ? scope->name->val : "");
    else
        zend_fatal("Call to %s method %s::%s() from context '%s'", vis,
                   ce->name->val, name, scope ? scope->name->val : "");
}

static int eval_new(zend_ast *ast, zval *result)
{
    zend_class_entry *ce = NULL;
    zend_ast *cls = ast->child[0];
    int err = 0;
    if (cls->kind == AST_ANON_CLASS) {
        /* VERSIONGATE:70000 anonymous classes are 7.0 */
        zend_class_entry *decl = (zend_class_entry *)ZEND_ZVAL_PTR(cls->val);
        ce = zend_declare_class(decl);
        if (!ce) return 1;
    } else {
        ce = zend_resolve_class_ast(cls, &err);
        if (!ce) return 1;
    }
    if (ce->flags & ZEND_ACC_ABSTRACT) {
        /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
        if (EG(php_version) >= 70000)
            zend_throw_error("Error", "Cannot instantiate abstract class %s", ce->name->val);
        else
            zend_fatal("Cannot instantiate abstract class %s", ce->name->val);
        return 1;
    }
    if (ce->flags & ZEND_ACC_INTERFACE) {
        /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
        if (EG(php_version) >= 70000)
            zend_throw_error("Error", "Cannot instantiate interface %s", ce->name->val);
        else
            zend_fatal("Cannot instantiate interface %s", ce->name->val);
        return 1;
    }
    if (ce->flags & ZEND_ACC_TRAIT) {
        /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
        if (EG(php_version) >= 70000)
            zend_throw_error("Error", "Cannot instantiate trait %s", ce->name->val);
        else
            zend_fatal("Cannot instantiate trait %s", ce->name->val);
        return 1;
    }
    /* VERSIONGATE:80100 BEGIN - an enum has no constructor to call */
    if (ce->is_enum) {
        zend_throw_error("Error", "Cannot instantiate enum %s", ce->name->val);
        return 1;
    }
    /* VERSIONGATE:80100 END */
    if (zend_ce_generator && ce == zend_ce_generator) {
        zend_error(E_RECOVERABLE_ERROR,
                   "The \"Generator\" class is reserved for internal use and cannot be manually instantiated");
        return 1;
    }
    /* VERSIONGATE:80000 named argument list rules */
    if (validate_arg_list(ast->nchild > 1 ? ast->child[1] : NULL)) return 1;
    if (bind_class_constants(ce)) return 1;
    zend_object *obj = zend_object_new(ce);
    arglist_t a;
    if (eval_args_for(ce->constructor, ast->nchild > 1 ? ast->child[1] : NULL, &a)) return 1;
    int has_named = 0;
    for (uint32_t i = 0; i < a.n; i++) if (a.names[i]) { has_named = 1; break; }
    /* VERSIONGATE:80000 BEGIN - a class without a constructor takes no names */
    if (has_named && !ce->constructor) {
        for (uint32_t i = 0; i < a.n; i++) {
            if (!a.names[i]) continue;
            zend_throw_error("Error", "Unknown named parameter $%s", a.names[i]->val);
            return 1;
        }
    }
    /* VERSIONGATE:80000 END */
    if (ce->constructor) {
        if (!zend_check_method_access(ce->constructor, zend_get_current_scope())) {
            inaccessible_method(ce->constructor, ce, "__construct");
            return 1;
        }
        zval rv = zval_null();
        if (zend_call_function_ex(ce->constructor, obj, ce, a.n, a.argv,
                                  has_named ? a.names : NULL, &rv, a.slots))
            return 1;
    }
    *result = zval_object(obj);
    return 0;
}

int eval_call_node(zend_ast *ast, zval *result)
{
    *result = zval_null();
    zend_set_line(ast);

    if (ast->kind == AST_NEW || ast->kind == AST_ANON_CLASS)
        return eval_new(ast, result);

    if (ast->kind == AST_CALL) {
        zend_ast *callee = ast->child[0];
        zend_ast *args = ast->nchild > 1 ? ast->child[1] : NULL;
        /* VERSIONGATE:80000 named argument list rules */
        if (validate_arg_list(args)) return 1;
        zend_function *fn = NULL;
        zend_object *obj = NULL;
        zend_class_entry *scope = NULL;
        zval holder = zval_null();
        int have_holder = 0;

        zend_string *fname = (callee->kind == AST_ZVAL && callee->val.u1 == IS_STRING)
                           ? callee->val.value.str
                           : (callee->kind == AST_CONST ? callee->val.value.str : NULL);
        if (fname) {
            fn = lookup_function_name(fname, callee->attr);
            if (!fn) { undefined_function(fname->val); return 1; }
        } else {
            if (is_var_ast(callee)) {
                int cerr = 0;
                zval *cs = fetch_lval_ex(callee, FETCH_R, &cerr);
                if (cerr) return 1;
                holder = cs ? *zval_deref(cs) : zval_null();
            } else if (zend_eval_expr(callee, &holder)) return 1;
            have_holder = 1;
            zval *c = zval_deref(&holder);
            zend_string *err = NULL;
            if (!zend_is_callable_zval(c, &fn, &obj, &scope, &err)) {
                if (c->u1 == IS_STRING) { undefined_function(c->value.str->val); return 1; }
                if (c->u1 == IS_ARRAY) {
                    HashTable *ht = c->value.arr;
                    zval *o0 = zend_hash_index_find(ht, 0);
                    zval *m0 = zend_hash_index_find(ht, 1);
                    if (!o0 || !m0) { zend_fatal("Array callback has to contain indices 0 and 1"); return 1; }
                    o0 = zval_deref(o0); m0 = zval_deref(m0);
                    if (m0->u1 != IS_STRING) { zend_fatal("Function name must be a string"); return 1; }
                    zend_class_entry *kce = NULL;
                    if (o0->u1 == IS_OBJECT) kce = o0->value.obj->ce;
                    else if (o0->u1 == IS_STRING) {
                        kce = zend_lookup_class(o0->value.str, 1);
                        if (!kce) { zend_fatal("Class '%s' not found", o0->value.str->val); return 1; }
                    } else { zend_fatal("Function name must be a string"); return 1; }
                    zend_fatal("Call to undefined method %s::%s()", kce->name->val, m0->value.str->val);
                    return 1;
                }
                /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
                if (EG(php_version) >= 70000)
                    zend_throw_error("Error", "Value of type %s is not callable",
                                     zend_zval_type_name(c));
                else
                    zend_fatal("Function name must be a string");
                return 1;
            }
        }
        if (is_fcc(args)) return make_fcc(fn, obj, scope, result);
        arglist_t a;
        if (eval_args_for(fn, args, &a)) return 1;
        zend_set_line(ast);
        if (have_holder) {
            zval *c = zval_deref(&holder);
            if (c->u1 == IS_OBJECT && (zend_ce_closure && c->value.obj->ce == zend_ce_closure)) {
                int has_named = 0;
                for (uint32_t i = 0; i < a.n; i++) if (a.names[i]) { has_named = 1; break; }
                return zend_closure_call(c->value.obj, a.n, a.argv,
                                         has_named ? a.names : NULL, result, a.slots);
            }
        }
        return invoke(fn, obj, scope, &a, result);
    }

    if (ast->kind == AST_METHOD_CALL || ast->kind == AST_NULLSAFE_METHOD_CALL) {
        /* VERSIONGATE:80000 named argument list rules */
        if (validate_arg_list(ast->nchild > 2 ? ast->child[2] : NULL)) return 1;
        zval ov = zval_null();
        if (zend_eval_expr(ast->child[0], &ov)) return 1;
        zval *o = zval_deref(&ov);
        zend_string *name;
        if (eval_method_name(ast->child[1], &name)) return 1;
        zend_set_line(ast);
        if (o->u1 != IS_OBJECT) {
            /* VERSIONGATE:80000 nullsafe method calls are 8.0 */
            if (ast->kind == AST_NULLSAFE_METHOD_CALL && o->u1 <= IS_NULL) {
                *result = zval_null();
                return 0;
            }
            /* VERSIONGATE:70000 else-branch is the 5.6 wording */
            if (EG(php_version) >= 70000)
                zend_throw_error("Error", "Call to a member function %s() on %s",
                                 name->val, zend_zval_type_name(o));
            else
                zend_fatal("Call to a member function %s() on a non-object", name->val);
            return 1;
        }
        zend_object *obj = o->value.obj;
        zend_class_entry *ce = obj->ce;
        zend_function *m = zend_lookup_method(ce, zend_string_tolower(name));
        if (m && !zend_check_method_access(m, zend_get_current_scope())) {
            if (ce->__call) m = make_call_trampoline(ce->__call, ce, name);
            else { inaccessible_method(m, ce, name->val); return 1; }
        }
        if (!m) {
            if (ce->__call) m = make_call_trampoline(ce->__call, ce, name);
            else { undefined_method(ce, name->val); return 1; }
        }
        zend_ast *args = ast->nchild > 2 ? ast->child[2] : NULL;
        if (is_fcc(args)) return make_fcc(m, obj, ce, result);
        /* a Closure called through ->__invoke() keeps its bindings */
        if (ce == closure_ce() && zend_string_equals_ci(name, S("__invoke"))) {
            arglist_t a;
            if (eval_args_for(closure_of(obj) ? closure_of(obj)->fn : NULL, args, &a)) return 1;
            int has_named = 0;
            for (uint32_t i = 0; i < a.n; i++) if (a.names[i]) { has_named = 1; break; }
            return zend_closure_call(obj, a.n, a.argv, has_named ? a.names : NULL, result, a.slots);
        }
        arglist_t a;
        if (eval_args_for((m->flags & ZEND_ACC_CALL_TRAMPOLINE) ? NULL : m, args, &a)) return 1;
        zend_set_line(ast);
        return invoke(m, obj, ce, &a, result);
    }

    if (ast->kind == AST_STATIC_CALL) {
        /* VERSIONGATE:80000 named argument list rules */
        if (validate_arg_list(ast->nchild > 2 ? ast->child[2] : NULL)) return 1;
        int err = 0;
        zend_ast *cls = ast->child[0];
        zend_class_entry *ce = zend_resolve_class_ast(cls, &err);
        if (!ce) return 1;
        zend_string *name;
        if (eval_method_name_ex(ast->child[1], &name, "Function")) return 1;
        zend_set_line(ast);
        zend_function *m = zend_lookup_method(ce, zend_string_tolower(name));
        zend_object *This = NULL;
        zend_class_entry *cur_called = called_scope();
        zend_string *cn = (cls->kind == AST_ZVAL || cls->kind == AST_CONST) ? cls->val.value.str : NULL;
        int keep_lsb = cn && (zend_string_equals_ci(cn, S("self")) ||
                              zend_string_equals_ci(cn, S("parent")) ||
                              zend_string_equals_ci(cn, S("static")));

        /* php-src prefers __call when $this is an instance of the class */
        zend_object *cur_this = EG(current) ? EG(current)->This : NULL;
        int this_fits = cur_this && zend_instanceof(cur_this->ce, ce);
        if (m && !zend_check_method_access(m, zend_get_current_scope())) {
            if (this_fits && ce->__call) { m = make_call_trampoline(ce->__call, ce, name); This = cur_this; }
            else if (ce->__callstatic) m = make_call_trampoline(ce->__callstatic, ce, name);
            else { inaccessible_method(m, ce, name->val); return 1; }
        }
        if (!m) {
            if (this_fits && ce->__call) { m = make_call_trampoline(ce->__call, ce, name); This = cur_this; }
            else if (ce->__callstatic) m = make_call_trampoline(ce->__callstatic, ce, name);
            else { undefined_method(ce, name->val); return 1; }
        }
        if (!(m->flags & ZEND_ACC_STATIC) && !(m->flags & ZEND_ACC_CALL_TRAMPOLINE)) {
            zend_object *cur = EG(current) ? EG(current)->This : NULL;
            if (cur && zend_instanceof(cur->ce, ce)) This = cur;
            else {
                /* VERSIONGATE:80000 else-branch is the 5.6/7.x behaviour */
                if (EG(php_version) >= 80000) {
                    zend_throw_error("Error", "Non-static method %s::%s() cannot be called statically",
                                     ce->name->val, m->name->val);
                    return 1;
                }
                if (cur) {
                    /* 5.6 still passes the caller's $this, from a foreign class */
                    zend_error(E_DEPRECATED,
                               "Non-static method %s::%s() should not be called statically, assuming $this from incompatible context",
                               ce->name->val, m->name->val);
                    This = cur;
                } else {
                    /* VERSIONGATE:70000 else-branch is the 5.6 behaviour (E_STRICT) */
                    if (EG(php_version) >= 70000)
                        zend_error(E_DEPRECATED, "Non-static method %s::%s() should not be called statically",
                                   ce->name->val, m->name->val);
                    else
                        zend_error(E_STRICT, "Non-static method %s::%s() should not be called statically",
                                   ce->name->val, m->name->val);
                }
            }
        }
        zend_ast *args = ast->nchild > 2 ? ast->child[2] : NULL;
        if (is_fcc(args)) return make_fcc(m, This, keep_lsb && cur_called ? cur_called : ce, result);
        arglist_t a;
        if (eval_args_for((m->flags & ZEND_ACC_CALL_TRAMPOLINE) ? NULL : m, args, &a)) return 1;
        zend_set_line(ast);
        return invoke(m, This, keep_lsb && cur_called ? cur_called : ce, &a, result);
    }

    *result = zval_null();
    return 0;
}

/* ======================================================================== */
/*  iteration                                                               */
/* ======================================================================== */

/* Assign one foreach value (a plain variable or a list() pattern). */
static int foreach_assign_value(zend_ast *target, zval v)
{
    if (!target) return 0;
    if (target->kind == AST_LIST_ASSIGN || target->kind == AST_ARRAY)
        return list_assign(target, v, NULL);
    return do_assign(target, v, NULL);
}

static ex_status loop_status(ex_status st, int *stop)
{
    *stop = 0;
    switch (st) {
    case EX_BREAK:
        if (--zend_break_levels > 0) { *stop = 1; return EX_BREAK; }
        *stop = 1;
        return EX_NORMAL;
    case EX_CONTINUE:
        if (--zend_break_levels > 0) { *stop = 1; return EX_CONTINUE; }
        return EX_NORMAL;
    case EX_NORMAL:
        return EX_NORMAL;
    default:
        *stop = 1;
        return st;
    }
}

static ex_status foreach_over_object(zend_ast *ast, zend_object *obj, zval *retval);

static ex_status exec_foreach(zend_ast *ast, zval *retval)
{
    zend_ast *vt = ast->child[1], *kt = ast->child[2], *body = ast->child[3];
    int by_ref = (ast->attr & ZEND_FOREACH_BY_REF) != 0;

    if (by_ref) {
        int err = 0;
        zval *slot = fetch_lval_ex(ast->child[0], FETCH_W, &err);
        if (!slot) return EXPR_FAIL();
        zval *c = zval_deref(slot);
        if (c->u1 == IS_OBJECT) return foreach_over_object(ast, c->value.obj, retval);
        if (c->u1 != IS_ARRAY) {
            zend_error(E_WARNING, "Invalid argument supplied for foreach()");
            return EX_NORMAL;
        }
        zval_separate_array(c);
        HashTable *ht = c->value.arr;
        for (uint32_t i = 0; i < ht->nNumUsed; i++) {
            Bucket *b = &ht->arData[i];
            if (b->val.u1 == IS_UNDEF) continue;
            if (kt) {
                zval k = b->key ? zval_zstr(b->key) : zval_long((zend_long)b->h);
                if (do_assign(kt, k, NULL)) return EXPR_FAIL();
            }
            if (vt && bind_reference(vt, &ht->arData[i].val, NULL)) return EXPR_FAIL();
            ex_status st = exec_stmt(body, retval);
            int stop = 0;
            st = loop_status(st, &stop);
            if (stop) return st;
        }
        return EX_NORMAL;
    }

    zval coll = zval_null();
    int live = 0;
    if (is_var_ast(ast->child[0])) {
        /* php-src cannot snapshot an array held in a reference, so a foreach
         * over one walks the live array and sees what the body appends. */
        int ferr = 0;
        zval *slot = fetch_lval_ex(ast->child[0], FETCH_R, &ferr);
        if (ferr) return EXPR_FAIL();
        if (slot) {
            if (slot->u1 == IS_REFERENCE && zval_deref(slot)->u1 == IS_ARRAY) {
                coll = *zval_deref(slot);
                live = 1;
            } else {
                coll = zval_copy(*zval_deref(slot));
            }
        }
    } else if (zend_eval_expr(ast->child[0], &coll)) return EXPR_FAIL();
    zval *c = zval_deref(&coll);
    if (c->u1 == IS_OBJECT) return foreach_over_object(ast, c->value.obj, retval);
    if (c->u1 != IS_ARRAY) {
        /* VERSIONGATE:80000 else-branch is the 5.6 wording */
        if (EG(php_version) >= 80000)
            zend_error(E_WARNING, "foreach() argument must be of type array|object, %s given",
                       zend_zval_type_name(c));
        else
            zend_error(E_WARNING, "Invalid argument supplied for foreach()");
        return EX_NORMAL;
    }
    HashTable *ht = c->value.arr;
    if (!live) ht->refcount++;      /* php-src iterates a snapshot */
    for (uint32_t i = 0; i < ht->nNumUsed; i++) {
        Bucket *b = &ht->arData[i];
        if (b->val.u1 == IS_UNDEF) continue;
        zend_string *bk = b->key;
        zend_ulong bh = b->h;
        zval item = zval_copy(*zval_deref(&b->val));
        if (kt) {
            zval k = bk ? zval_zstr(bk) : zval_long((zend_long)bh);
            if (do_assign(kt, k, NULL)) { if (!live) ht->refcount--; return EXPR_FAIL(); }
        }
        if (foreach_assign_value(vt, item)) { if (!live) ht->refcount--; return EXPR_FAIL(); }
        ex_status st = exec_stmt(body, retval);
        int stop = 0;
        st = loop_status(st, &stop);
        if (stop) { if (!live) ht->refcount--; return st; }
    }
    if (!live) ht->refcount--;
    return EX_NORMAL;
}

static ex_status foreach_over_object(zend_ast *ast, zend_object *obj, zval *retval)
{
    zend_ast *vt = ast->child[1], *kt = ast->child[2], *body = ast->child[3];
    int by_ref = (ast->attr & ZEND_FOREACH_BY_REF) != 0;

    /* a Generator drives its own protocol */
    zend_generator *g = zend_generator_from_object(obj);
    if (g) {
        zend_function *gfn = zend_generator_function(g);
        if (by_ref && gfn && !fn_returns_ref(gfn)) {
            zend_throw_error("Exception",
                             "You can only iterate a generator by-reference if it declared that it yields by-reference");
            return EX_EXCEPTION;
        }
        if (zend_generator_ensure_traversable(g)) return EX_EXCEPTION;
        while (zend_generator_valid(g)) {
            if (kt && do_assign(kt, zend_generator_key(g), NULL)) return EXPR_FAIL();
            zval cur = zend_generator_current(g);
            if (by_ref && cur.u1 == IS_REFERENCE) {
                if (vt && bind_reference(vt, &cur, NULL)) return EXPR_FAIL();
            } else if (foreach_assign_value(vt, zval_copy(cur))) return EXPR_FAIL();
            ex_status st = exec_stmt(body, retval);
            int stop = 0;
            st = loop_status(st, &stop);
            if (stop) return st;
            if (zend_generator_next(g)) return EX_EXCEPTION;
        }
        return EX_NORMAL;
    }

    /* IteratorAggregate */
    if (zend_ce_aggregate && zend_instanceof(obj->ce, zend_ce_aggregate)) {
        zval it = zval_null();
        if (call_method_name(obj, "getiterator", 0, NULL, &it) == 1) return EX_EXCEPTION;
        zval *d = zval_deref(&it);
        if (d->u1 == IS_OBJECT) return foreach_over_object(ast, d->value.obj, retval);
        if (d->u1 == IS_ARRAY) {
            HashTable *ht = d->value.arr;
            ht->refcount++;
            for (uint32_t i = 0; i < ht->nNumUsed; i++) {
                Bucket *b = &ht->arData[i];
                if (b->val.u1 == IS_UNDEF) continue;
                if (kt) {
                    zval k = b->key ? zval_zstr(b->key) : zval_long((zend_long)b->h);
                    if (do_assign(kt, k, NULL)) { ht->refcount--; return EXPR_FAIL(); }
                }
                if (foreach_assign_value(vt, zval_copy(*zval_deref(&b->val)))) { ht->refcount--; return EXPR_FAIL(); }
                ex_status st = exec_stmt(body, retval);
                int stop = 0;
                st = loop_status(st, &stop);
                if (stop) { ht->refcount--; return st; }
            }
            ht->refcount--;
            return EX_NORMAL;
        }
        zend_throw_error("Exception", "Objects returned by %s::getIterator() must be traversable or implement interface Iterator",
                         obj->ce->name->val);
        return EX_EXCEPTION;
    }

    /* Iterator */
    if (class_has_method(obj->ce, "rewind") && class_has_method(obj->ce, "valid")) {
        zval t = zval_null();
        if (call_method_name(obj, "rewind", 0, NULL, &t) == 1) return EX_EXCEPTION;
        while (1) {
            zval valid = zval_null();
            if (call_method_name(obj, "valid", 0, NULL, &valid) == 1) return EX_EXCEPTION;
            if (!zval_is_true(zval_deref(&valid))) break;
            if (kt) {
                zval k = zval_null();
                if (call_method_name(obj, "key", 0, NULL, &k) == 1) return EX_EXCEPTION;
                if (do_assign(kt, zval_copy(*zval_deref(&k)), NULL)) return EX_EXCEPTION;
            }
            zval cur = zval_null();
            if (call_method_name(obj, "current", 0, NULL, &cur) == 1) return EX_EXCEPTION;
            if (foreach_assign_value(vt, zval_copy(*zval_deref(&cur)))) return EX_EXCEPTION;
            ex_status st = exec_stmt(body, retval);
            int stop = 0;
            st = loop_status(st, &stop);
            if (stop) return st;
            zval nx = zval_null();
            if (call_method_name(obj, "next", 0, NULL, &nx) == 1) return EX_EXCEPTION;
        }
        return EX_NORMAL;
    }

    /* a plain object: iterate the properties visible from the current scope */
    HashTable *props = obj->properties;
    zend_class_entry *scope = zend_get_current_scope();
    props->refcount++;
    for (uint32_t i = 0; i < props->nNumUsed; i++) {
        Bucket *b = &props->arData[i];
        if (b->val.u1 == IS_UNDEF || !b->key) continue;
        zend_class_entry *decl = NULL;
        zend_string *plain = zend_unmangle_property_name(b->key, &decl);
        zend_property_info *info = zend_find_property_info(obj->ce, plain);
        if (info && info->mangled && !zend_string_equals(info->mangled, b->key))
            continue;                       /* shadowed by a redeclaration */
        if (info && !zend_check_property_access(info, scope)) continue;
        if (kt && do_assign(kt, zval_zstr(plain), NULL)) { props->refcount--; return EXPR_FAIL(); }
        if (by_ref) {
            if (vt && bind_reference(vt, &props->arData[i].val, NULL)) { props->refcount--; return EXPR_FAIL(); }
        } else if (foreach_assign_value(vt, zval_copy(*zval_deref(&b->val)))) {
            props->refcount--;
            return EXPR_FAIL();
        }
        ex_status st = exec_stmt(body, retval);
        int stop = 0;
        st = loop_status(st, &stop);
        if (stop) { props->refcount--; return st; }
    }
    props->refcount--;
    return EX_NORMAL;
}

/* ======================================================================== */
/*  declarations                                                            */
/* ======================================================================== */

/* VERSIONGATE:70000 the guarded branch is the 5.6 behaviour: a generator with
 * "return <expr>" is rejected at compile time until 7.0 gave it a meaning. */
static zend_ast *find_return_with_value(zend_ast *a)
{
    if (!a) return NULL;
    if (a->kind == AST_RETURN && a->nchild && a->child[0]) return a;
    if (a->kind == AST_FUNC_DECL || a->kind == AST_CLOSURE || a->kind == AST_ARROW_FN ||
        a->kind == AST_CLASS_DECL || a->kind == AST_ANON_CLASS)
        return NULL;
    for (uint32_t i = 0; i < a->nchild; i++) {
        zend_ast *r = find_return_with_value(a->child[i]);
        if (r) return r;
    }
    return NULL;
}

static void check_generator_return(zend_function *fn)
{
    /* VERSIONGATE:70000 the guarded branch skips the 5.6-only check */
    if (EG(php_version) >= 70000) return;
    if (!zend_function_is_generator(fn)) return;
    zend_ast *r = find_return_with_value(fn->body);
    if (r)
        zend_error_at(E_ERROR, fn->filename ? fn->filename : EG(filename), r->lineno,
                      "Generators cannot return values using \"return\"");
}

void zend_declare_function(zend_function *fn)
{
    if (!fn || !fn->lcname) return;
    check_generator_return(fn);
    if (EG(exiting)) return;
    zval *old = zend_hash_find(EG(function_table), fn->lcname);
    if (old) {
        zend_function *of = (zend_function *)ZEND_ZVAL_PTR_P(old);
        if (of == fn) return;
        zend_fatal("Cannot redeclare %s()", fn->name->val);
        return;
    }
    if (!fn->static_vars) fn->static_vars = zend_hash_new();
    zend_hash_update(EG(function_table), fn->lcname, ZEND_PTR_ZVAL(fn));
}

/* A declaration is "unconditional" when it sits directly in the top-level
 * statement list and does not depend on a class that is not bound yet. */
static int class_decl_ready(zend_class_entry *decl)
{
    zend_class_decl_extra *ex = zend_class_decl_get_extra(decl);
    if (!ex) return 1;
    /* php-src's zend_do_early_binding() bails out as soon as the declaration
     * needs ZEND_ADD_INTERFACE or ZEND_BIND_TRAITS, so such a class is bound
     * where the statement stands and its diagnostics appear in that order. */
    if (ex->num_ifaces || ex->num_traits) return 0;
    if (ex->parent_name && !zend_lookup_class(ex->parent_name, 0)) return 0;
    return 1;
}

void zend_hoist_declarations(zend_ast *stmts)
{
    if (!stmts) return;
    if (stmts->kind != AST_STMT_LIST && stmts->kind != AST_LIST) return;
    for (uint32_t i = 0; i < stmts->nchild; i++) {
        zend_ast *s = stmts->child[i];
        if (s && s->kind == AST_FUNC_DECL) {
            zend_function *fn = (zend_function *)ZEND_ZVAL_PTR(s->val);
            if (fn && fn->lcname && !zend_hash_find(EG(function_table), fn->lcname))
                zend_declare_function(fn);
        }
    }
    /* classes may need several passes: a child declared before its parent */
    int progress = 1;
    while (progress) {
        progress = 0;
        for (uint32_t i = 0; i < stmts->nchild; i++) {
            zend_ast *s = stmts->child[i];
            if (!s || s->kind != AST_CLASS_DECL) continue;
            zend_class_entry *decl = (zend_class_entry *)ZEND_ZVAL_PTR(s->val);
            if (!decl || !decl->lcname) continue;
            if (zend_hash_find(EG(class_table), decl->lcname)) continue;
            if (!class_decl_ready(decl)) continue;
            if (zend_declare_class(decl)) progress = 1;
            if (EG(exception)) return;
        }
    }
}

/* ======================================================================== */
/*  include / require                                                       */
/* ======================================================================== */

static int file_exists_readable(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static zend_string *join_path(zend_string *dir, zend_string *name)
{
    if (!dir || dir->len == 0) return name;
    zend_string *s = zend_string_alloc(dir->len + 1 + name->len);
    memcpy(s->val, dir->val, dir->len);
    s->val[dir->len] = '/';
    memcpy(s->val + dir->len + 1, name->val, name->len);
    s->val[dir->len + 1 + name->len] = '\0';
    return s;
}

static zend_string *resolve_include(zend_string *name)
{
    if (name->len == 0) return NULL;
    if (name->val[0] == '/') return file_exists_readable(name->val) ? name : NULL;
    if (name->len > 1 && name->val[0] == '.' &&
        (name->val[1] == '/' || (name->val[1] == '.' && name->len > 2 && name->val[2] == '/')))
        return file_exists_readable(name->val) ? name : NULL;

    zend_string *ipath = NULL;
    if (EG(ini)) {
        zval *v = zend_hash_find(EG(ini), S("include_path"));
        if (v && v->u1 == IS_STRING) ipath = v->value.str;
    }
    if (ipath && ipath->len) {
        const char *p = ipath->val;
        while (*p) {
            const char *q = strchr(p, ':');
            size_t n = q ? (size_t)(q - p) : strlen(p);
            if (n) {
                zend_string *cand = join_path(zend_string_init(p, n), name);
                if (file_exists_readable(cand->val)) return cand;
            }
            if (!q) break;
            p = q + 1;
        }
    }
    /* the including file's directory, then the working directory */
    zend_string *here = EG(current) && EG(current)->filename ? EG(current)->filename : EG(filename);
    if (here) {
        zend_string *cand = join_path(dir_name_of(here), name);
        if (file_exists_readable(cand->val)) return cand;
    }
    if (file_exists_readable(name->val)) return name;
    return NULL;
}

static zend_string *absolute_key(zend_string *path)
{
    char buf[4096];
    if (realpath(path->val, buf)) return zend_string_initz(buf);
    return path;
}

static int eval_include(zend_ast *ast, zval *result)
{
    zval v = zval_null();
    if (zend_eval_expr(ast->child[0], &v)) return 1;
    zend_string *name = zval_get_string(zval_deref(&v));
    if (EG(exception)) return 1;
    uint32_t mode = ast->attr;
    int required = (mode == ZEND_REQUIRE_PLAIN || mode == ZEND_REQUIRE_ONCE_);
    int once = (mode == ZEND_INCLUDE_ONCE_ || mode == ZEND_REQUIRE_ONCE_);
    const char *what = mode == ZEND_INCLUDE_PLAIN ? "include"
                     : mode == ZEND_INCLUDE_ONCE_ ? "include_once"
                     : mode == ZEND_REQUIRE_PLAIN ? "require" : "require_once";

    if (mode == ZEND_INCLUDE_EVAL) {
        zend_string *err = NULL;
        zend_string *fname = zend_strpprintf("%s(%u) : eval()'d code",
                                             EG(filename) ? EG(filename)->val : "", EG(lineno));
        zend_ast *code = php_parse_string(name->val, name->len, fname, &err);
        if (!code) {
            /* VERSIONGATE:80000 else-branch is the 5.6 behaviour */
            if (EG(php_version) >= 80000) {
                zend_throw_error("ParseError", "%s", err ? err->val : "syntax error");
                return 1;
            }
            zend_error(E_PARSE, "%s", err ? err->val : "syntax error");
            *result = zval_bool(0);
            return 1;
        }
        zend_hoist_declarations(code);
        zval rv = zval_null();
        ex_status st = zend_execute_ast(code, &rv);
        if (st == EX_EXCEPTION || st == EX_EXIT) return 1;
        *result = st == EX_RETURN ? rv : zval_null();
        return 0;
    }

    zend_string *path = resolve_include(name);
    if (!path) {
        /* VERSIONGATE:80000 else-branch is the 5.6 wording ("failed to open") */
        const char *fos = EG(php_version) >= 80000 ? "Failed to open stream" : "failed to open stream";
        if (required) {
            zend_error(E_WARNING, "%s(%s): %s: No such file or directory", what, name->val, fos);
            zend_fatal("%s(): Failed opening required '%s' (include_path='%s')",
                       what, name->val, ".:");
            return 1;
        }
        zend_error(E_WARNING, "%s(%s): %s: No such file or directory", what, name->val, fos);
        zend_error(E_WARNING, "%s(): Failed opening '%s' for inclusion (include_path='%s')",
                   what, name->val, ".:");
        *result = zval_bool(0);
        return 0;
    }

    zend_string *key = absolute_key(path);
    if (once) {
        if (!EG(included_files)) EG(included_files) = zend_hash_new();
        if (zend_hash_find(EG(included_files), key)) { *result = zval_bool(1); return 0; }
        zend_hash_update(EG(included_files), key, zval_bool(1));
    } else {
        if (!EG(included_files)) EG(included_files) = zend_hash_new();
        if (!zend_hash_find(EG(included_files), key))
            zend_hash_update(EG(included_files), key, zval_bool(1));
    }

    zend_string *err = NULL;
    zend_ast *code = php_parse_file(path->val, &err);
    if (!code) {
        zend_error(E_PARSE, "%s", err ? err->val : "syntax error");
        *result = zval_bool(0);
        return 1;
    }

    /* the included file shares the caller's variable scope */
    zend_execute_data *ex = ecalloc(1, sizeof(zend_execute_data));
    ex->prev = EG(current);
    ex->symbols = cur_symbols();
    ex->func = EG(current) ? EG(current)->func : NULL;
    ex->called_scope = called_scope();
    ex->This = EG(current) ? EG(current)->This : NULL;
    ex->filename = key;
    ex->lineno = EG(lineno);

    zend_string *sfile = EG(filename);
    uint32_t sline = EG(lineno);
    int sstrict = EG(strict_types);
    EG(current) = ex;
    EG(filename) = key;
    zend_validate_jumps(code);
    zend_hoist_declarations(code);
    zval rv = zval_null();
    ex_status st = zend_execute_ast(code, &rv);
    EG(current) = ex->prev;
    EG(filename) = sfile;
    EG(lineno) = sline;
    EG(strict_types) = sstrict;
    if (st == EX_EXCEPTION || st == EX_EXIT) return 1;
    *result = (st == EX_RETURN) ? rv : zval_long(1);
    return 0;
}


/* ======================================================================== */
/*  jump validation                                                         */
/* ======================================================================== */
/* php-src resolves every goto, break and continue while compiling, so the
 * diagnostics appear even when the function is never called.  This pass does
 * the same walk over the AST once, right after a file is parsed. */

#define GV_MAX_SITES 512
#define GV_MAX_DEPTH 32

typedef struct {
    zend_ast *node;
    zend_ast *scopes[GV_MAX_DEPTH];
    unsigned char kinds[GV_MAX_DEPTH];   /* 1 = loop or switch, 2 = finally */
    int depth;
} gv_site;

typedef struct {
    gv_site gotos[GV_MAX_SITES];   int ngotos;
    gv_site labels[GV_MAX_SITES];  int nlabels;
    gv_site jumps[GV_MAX_SITES];   int njumps;   /* break / continue */
} gv_ctx;

static void gv_validate_ast(zend_ast *root);

static zend_string *label_name_of(zend_ast *a)
{
    if (a->val.u1 == IS_STRING) return a->val.value.str;
    return a->nchild ? ast_name(a->child[0]) : NULL;
}

static void gv_record(gv_site *dst, zend_ast *a, zend_ast **sc, unsigned char *kd, int depth)
{
    dst->node = a;
    dst->depth = depth;
    for (int i = 0; i < depth && i < GV_MAX_DEPTH; i++) { dst->scopes[i] = sc[i]; dst->kinds[i] = kd[i]; }
}

static void gv_walk(zend_ast *a, gv_ctx *c, zend_ast **sc, unsigned char *kd, int depth)
{
    if (!a) return;
    switch (a->kind) {
    case AST_GOTO:
        if (c->ngotos < GV_MAX_SITES) gv_record(&c->gotos[c->ngotos++], a, sc, kd, depth);
        return;
    case AST_LABEL:
        if (c->nlabels < GV_MAX_SITES) gv_record(&c->labels[c->nlabels++], a, sc, kd, depth);
        return;
    case AST_BREAK: case AST_CONTINUE:
        if (c->njumps < GV_MAX_SITES) gv_record(&c->jumps[c->njumps++], a, sc, kd, depth);
        return;
    case AST_FUNC_DECL: case AST_CLOSURE: case AST_ARROW_FN: {
        zend_function *fn = (zend_function *)ZEND_ZVAL_PTR(a->val);
        if (fn && fn->body) gv_validate_ast(fn->body);
        return;
    }
    case AST_CLASS_DECL: case AST_ANON_CLASS: {
        zend_class_entry *ce = (zend_class_entry *)ZEND_ZVAL_PTR(a->val);
        if (ce && ce->function_table) {
            ZEND_HASH_FOREACH(ce->function_table, b) {
                zend_function *m = (zend_function *)ZEND_ZVAL_PTR(b->val);
                if (m && m->body) gv_validate_ast(m->body);
            } ZEND_HASH_FOREACH_END();
        }
        return;
    }
    default: break;
    }
    int body_index = -1;
    switch (a->kind) {
    case AST_WHILE:    body_index = 1; break;
    case AST_DO_WHILE: body_index = 0; break;
    case AST_FOR:      body_index = 3; break;
    case AST_FOREACH:  body_index = 3; break;
    case AST_SWITCH:   body_index = 1; break;
    default: break;
    }
    for (uint32_t i = 0; i < a->nchild; i++) {
        int d = depth;
        if (d < GV_MAX_DEPTH &&
            ((body_index >= 0 && (int)i == body_index) || (a->kind == AST_TRY && i == 2))) {
            sc[d] = a;
            kd[d] = (a->kind == AST_TRY) ? 2 : 1;
            d++;
        }
        gv_walk(a->child[i], c, sc, kd, d);
    }
}

static void gv_validate_ast(zend_ast *root)
{
    if (!root) return;
    gv_ctx *c = ecalloc(1, sizeof(gv_ctx));
    zend_ast *sc[GV_MAX_DEPTH];
    unsigned char kd[GV_MAX_DEPTH];
    gv_walk(root, c, sc, kd, 0);

    for (int i = 0; i < c->ngotos && !EG(exiting); i++) {
        gv_site *g = &c->gotos[i];
        zend_string *nm = label_name_of(g->node);
        if (!nm) continue;
        gv_site *lab = NULL;
        for (int j = 0; j < c->nlabels; j++) {
            zend_string *ln = label_name_of(c->labels[j].node);
            if (ln && zend_string_equals(ln, nm)) { lab = &c->labels[j]; break; }
        }
        if (!lab) {
            zend_error_at(E_ERROR, EG(filename), g->node->lineno,
                          "'goto' to undefined label '%s'", nm->val);
            return;
        }
        /* a label nested in a scope the goto is not in cannot be reached */
        for (int j = 0; j < lab->depth; j++) {
            if (j < g->depth && lab->scopes[j] == g->scopes[j]) continue;
            zend_error_at(E_ERROR, EG(filename), g->node->lineno, "%s",
                          lab->kinds[j] == 2 ? "jump into a finally block is disallowed"
                                             : "'goto' into loop or switch statement is disallowed");
            return;
        }
        /* leaving a finally block is not allowed either */
        for (int j = 0; j < g->depth; j++) {
            if (g->kinds[j] != 2) continue;
            if (j < lab->depth && lab->scopes[j] == g->scopes[j]) continue;
            zend_error_at(E_ERROR, EG(filename), g->node->lineno,
                          "jump out of a finally block is disallowed");
            return;
        }
    }

    for (int i = 0; i < c->njumps && !EG(exiting); i++) {
        gv_site *b = &c->jumps[i];
        zend_ast *lv = b->node->nchild ? b->node->child[0] : NULL;
        zend_long level = 1;
        if (lv && lv->kind == AST_ZVAL && lv->val.u1 == IS_LONG) level = lv->val.value.lval;
        else if (lv) continue;                 /* a computed level is a run-time affair */
        if (level < 1) level = 1;
        int seen = 0, target = -1;
        for (int j = b->depth - 1; j >= 0; j--) {
            if (b->kinds[j] != 1) continue;
            if (++seen == level) { target = j; break; }
        }
        if (target < 0) {
            zend_error_at(E_ERROR, EG(filename), b->node->lineno,
                          "Cannot break/continue %lld level%s", (long long)level,
                          level == 1 ? "" : "s");
            return;
        }
        for (int j = target + 1; j < b->depth; j++) {
            if (b->kinds[j] == 2) {
                zend_error_at(E_ERROR, EG(filename), b->node->lineno,
                              "jump out of a finally block is disallowed");
                return;
            }
        }
    }
    efree(c);
}

void zend_validate_jumps(zend_ast *root) { gv_validate_ast(root); }

/* ======================================================================== */
/*  statements                                                              */
/* ======================================================================== */


/* Where does a label with this name live?  0 = nowhere, 1 = inside a loop or a
 * switch, 2 = inside a finally block.  php-src rejects both at compile time. */
static int find_label_context(zend_ast *a, zend_string *name, int inloop, int infinally)
{
    if (!a) return 0;
    if (a->kind == AST_LABEL) {
        zend_string *nm = a->val.u1 == IS_STRING ? a->val.value.str
                        : (a->nchild ? ast_name(a->child[0]) : NULL);
        if (nm && zend_string_equals(nm, name)) return infinally ? 2 : (inloop ? 1 : 3);
        return 0;
    }
    if (a->kind == AST_FUNC_DECL || a->kind == AST_CLOSURE || a->kind == AST_ARROW_FN ||
        a->kind == AST_CLASS_DECL || a->kind == AST_ANON_CLASS)
        return 0;
    int loop_body = -1;
    switch (a->kind) {
    case AST_WHILE: case AST_FOREACH: loop_body = (a->kind == AST_WHILE) ? 1 : 3; break;
    case AST_DO_WHILE: loop_body = 0; break;
    case AST_FOR: loop_body = 3; break;
    case AST_SWITCH: loop_body = 1; break;
    default: break;
    }
    for (uint32_t i = 0; i < a->nchild; i++) {
        int il = inloop || (loop_body >= 0 && (int)i == loop_body);
        int fin = infinally || (a->kind == AST_TRY && i == 2);
        int r = find_label_context(a->child[i], name, il, fin);
        if (r) return r;
    }
    return 0;
}

static void report_bad_goto(zend_ast *root)
{
    if (!zend_goto_target) return;
    int c = find_label_context(root, zend_goto_target, 0, 0);
    uint32_t line = zend_goto_lineno ? zend_goto_lineno : EG(lineno);
    if (c == 1)
        zend_error_at(E_ERROR, EG(filename), line,
                      "'goto' into loop or switch statement is disallowed");
    else if (c == 2)
        zend_error_at(E_ERROR, EG(filename), line, "jump into a finally block is disallowed");
    else
        zend_error_at(E_ERROR, EG(filename), line, "'goto' to undefined label '%s'",
                      zend_goto_target->val);
}

static ex_status exec_stmt_list(zend_ast *list, zval *retval)
{
    uint32_t i = 0;
    while (i < list->nchild) {
        ex_status st = exec_stmt(list->child[i], retval);
        if (st == EX_GOTO && zend_goto_target) {
            int found = -1;
            for (uint32_t j = 0; j < list->nchild; j++) {
                zend_ast *s = list->child[j];
                if (!s || s->kind != AST_LABEL) continue;
                zend_string *nm = s->val.u1 == IS_STRING ? s->val.value.str
                                : (s->nchild ? ast_name(s->child[0]) : NULL);
                if (nm && zend_string_equals(nm, zend_goto_target)) { found = (int)j; break; }
            }
            if (found >= 0) { zend_goto_target = NULL; i = (uint32_t)found + 1; continue; }
            return st;
        }
        if (st != EX_NORMAL) return st;
        i++;
    }
    return EX_NORMAL;
}

static ex_status exec_try(zend_ast *ast, zval *retval)
{
    zend_ast *body = ast->child[0];
    zend_ast *catches = ast->nchild > 1 ? ast->child[1] : NULL;
    zend_ast *finally = ast->nchild > 2 ? ast->child[2] : NULL;

    ex_status st = exec_stmt(body, retval);
    if (EG(exiting)) return EX_EXIT;

    if (EG(exception) && catches) {
        zend_object *exc = EG(exception);
        for (uint32_t i = 0; i < catches->nchild; i++) {
            zend_ast *c = catches->child[i];
            if (!c) continue;
            zend_ast *classes = c->child[0];
            int match = 0;
            for (uint32_t j = 0; classes && j < classes->nchild; j++) {
                zend_string *cn = ast_name(classes->child[j]);
                if (!cn && classes->child[j] && classes->child[j]->kind == AST_CONST)
                    cn = classes->child[j]->val.value.str;
                if (!cn) continue;
                zend_class_entry *ce = zend_lookup_class(cn, 1);
                if (ce && zend_instanceof(exc->ce, ce)) { match = 1; break; }
            }
            if (!match) continue;
            EG(exception) = NULL;
            if (c->nchild > 1 && c->child[1]) {
                /* VERSIONGATE:80000 a catch without a variable is 8.0 */
                if (do_assign(c->child[1], zval_object(exc), NULL)) { st = EX_EXCEPTION; break; }
            }
            st = exec_stmt(c->child[2], retval);
            break;
        }
    }
    if (EG(exception)) st = EX_EXCEPTION;

    if (finally) {
        zend_object *pending = EG(exception);
        int saved_levels = zend_break_levels;
        zend_string *saved_goto = zend_goto_target;
        zval saved_ret = *retval;
        EG(exception) = NULL;
        zend_break_levels = 0;
        zend_goto_target = NULL;
        zval fret = zval_null();
        ex_status fst = exec_stmt(finally, &fret);
        if (fst == EX_RETURN) { *retval = fret; return EX_RETURN; }
        if (fst == EX_EXCEPTION || EG(exception)) return EX_EXCEPTION;
        if (fst == EX_EXIT || EG(exiting)) return EX_EXIT;
        if (fst == EX_BREAK || fst == EX_CONTINUE) return fst;
        if (fst == EX_GOTO) {
            zend_error_at(E_ERROR, EG(filename),
                          zend_goto_lineno ? zend_goto_lineno : EG(lineno),
                          "jump out of a finally block is disallowed");
            return EX_EXCEPTION;
        }
        zend_break_levels = saved_levels;
        zend_goto_target = saved_goto;
        *retval = saved_ret;
        EG(exception) = pending;
        if (pending) return EX_EXCEPTION;
    }
    return st;
}

static ex_status exec_switch(zend_ast *ast, zval *retval)
{
    zval subj = zval_null();
    if (zend_eval_expr(ast->child[0], &subj)) return EXPR_FAIL();
    zend_ast *cases = ast->child[1];
    if (!cases) return EX_NORMAL;
    int start = -1, dflt = -1;
    for (uint32_t i = 0; i < cases->nchild; i++) {
        zend_ast *c = cases->child[i];
        if (!c) continue;
        if (!c->child[0]) { if (dflt < 0) dflt = (int)i; continue; }
        zval cv = zval_null();
        if (zend_eval_expr(c->child[0], &cv)) return EXPR_FAIL();
        if (zend_is_equal(&subj, &cv)) { start = (int)i; break; }
        if (EG(exception)) return EXPR_FAIL();
    }
    if (start < 0) start = dflt;
    if (start < 0) return EX_NORMAL;
    for (uint32_t i = (uint32_t)start; i < cases->nchild; i++) {
        zend_ast *c = cases->child[i];
        if (!c || !c->child[1]) continue;
        ex_status st = exec_stmt(c->child[1], retval);
        /* a switch counts as a loop for break/continue levels, and "continue"
         * inside one behaves exactly like "break" */
        if (st == EX_BREAK || st == EX_CONTINUE) {
            if (--zend_break_levels > 0) return st;
            return EX_NORMAL;
        }
        if (st != EX_NORMAL) return st;
    }
    return EX_NORMAL;
}

static int level_of(zend_ast *ast)
{
    if (!ast->nchild || !ast->child[0]) return 1;
    zval v = zval_null();
    if (zend_eval_expr(ast->child[0], &v)) return 1;
    zend_long l = zval_get_long(zval_deref(&v));
    return l < 1 ? 1 : (int)l;
}

static void exec_global(zend_ast *ast)
{
    for (uint32_t i = 0; i < ast->nchild; i++) {
        zend_ast *v = ast->child[i];
        if (!v) continue;
        zend_string *name = NULL;
        if (v->kind == AST_VAR) { if (eval_name(v->child[0], &name)) return; }
        else name = ast_name(v);
        if (!name) continue;
        if (!zend_hash_find(EG(symbol_table), name))
            zend_hash_update(EG(symbol_table), name, zval_null());
        zval *g = zend_hash_find(EG(symbol_table), name);
        zend_reference *r = zend_make_reference(g);
        r->refcount++;
        zval box; box.u1 = IS_REFERENCE; box.value.ref = r;
        HashTable *tbl = cur_symbols();
        if (tbl != EG(symbol_table)) {
            zval *old = zend_hash_find(tbl, name);
            zval prev = old ? *old : zval_null();
            zend_hash_update(tbl, name, box);
            if (old) zval_ptr_dtor(&prev);
        }
    }
}

static HashTable *file_static_vars(void)
{
    static HashTable *tbl = NULL;
    if (!tbl) tbl = zend_hash_new();
    return tbl;
}

static int exec_static_var(zend_ast *ast)
{
    zend_ast *decls[1];
    zend_ast **list;
    uint32_t n;
    if (ast->nchild && ast->child[0] && ast->child[0]->kind == AST_STATIC_VAR) {
        list = ast->child; n = ast->nchild;
    } else {
        decls[0] = ast; list = decls; n = 1;
    }
    for (uint32_t i = 0; i < n; i++) {
        zend_ast *d = list[i];
        if (!d) continue;
        zend_string *name = NULL;
        zend_ast *nameast = d->nchild ? d->child[0] : NULL;
        if (nameast && nameast->kind == AST_VAR) { if (eval_name(nameast->child[0], &name)) return 1; }
        else name = ast_name(nameast);
        if (!name && d->val.u1 == IS_STRING) name = d->val.value.str;
        if (!name) continue;
        zend_function *fn = EG(current) ? EG(current)->func : NULL;
        HashTable *sv;
        if (fn) { if (!fn->static_vars) fn->static_vars = zend_hash_new(); sv = fn->static_vars; }
        else sv = file_static_vars();
        if (!zend_hash_find(sv, name)) {
            zval init = zval_null();
            if (d->nchild > 1 && d->child[1] && zend_eval_expr(d->child[1], &init)) return 1;
            zend_hash_update(sv, name, init);
        }
        zval *slot = zend_hash_find(sv, name);
        zend_reference *r = zend_make_reference(slot);
        r->refcount++;
        zval box; box.u1 = IS_REFERENCE; box.value.ref = r;
        HashTable *tbl = cur_symbols();
        zval *old = zend_hash_find(tbl, name);
        zval prev = old ? *old : zval_null();
        zend_hash_update(tbl, name, box);
        if (old) zval_ptr_dtor(&prev);
    }
    return 0;
}

static int exec_const_decl(zend_ast *ast)
{
    for (uint32_t i = 0; i < ast->nchild; i++) {
        zend_ast *e = ast->child[i];
        if (!e) continue;
        zend_string *name = ast_name(e->nchild ? e->child[0] : NULL);
        if (!name && e->val.u1 == IS_STRING) name = e->val.value.str;
        if (!name) continue;
        zval v = zval_null();
        if (e->nchild > 1 && e->child[1] && zend_eval_expr(e->child[1], &v)) return 1;
        if (zend_hash_find(EG(constants), name)) {
            zend_error(E_WARNING, "Constant %s already defined", name->val);
            continue;
        }
        zend_hash_update(EG(constants), name, v);
    }
    return 0;
}

static void exec_declare(zend_ast *ast)
{
    zend_ast *dirs = ast->child[0];
    for (uint32_t i = 0; dirs && i < dirs->nchild; i++) {
        zend_ast *d = dirs->child[i];
        if (!d) continue;
        zend_string *name = ast_name(d->nchild ? d->child[0] : NULL);
        if (!name && d->val.u1 == IS_STRING) name = d->val.value.str;
        if (!name) continue;
        zval v = zval_null();
        if (d->nchild > 1 && d->child[1] && zend_eval_expr(d->child[1], &v)) return;
        /* VERSIONGATE:70000 strict_types arrived in 7.0 */
        if (zend_string_equals_ci(name, S("strict_types")))
            EG(strict_types) = EG(php_version) >= 70000 ? (int)zval_get_long(&v) : 0;
    }
}

ex_status exec_stmt(zend_ast *ast, zval *retval)
{
    if (!ast) return EX_NORMAL;
    if (EG(exiting)) return EX_EXIT;
    if (EG(exception)) return EXPR_FAIL();
    zend_set_line(ast);

    switch (ast->kind) {
    case AST_STMT_LIST:
    case AST_LIST:
        return exec_stmt_list(ast, retval);

    case AST_NOP:
    case AST_USE:
    case AST_LABEL:
        return EX_NORMAL;

    case AST_HALT_COMPILER:
        return EX_RETURN;

    case AST_INLINE_HTML:
        if (ast->val.u1 == IS_STRING) php_write_str(ast->val.value.str);
        return EX_NORMAL;

    case AST_ECHO: {
        if (ast->nchild == 0 && ast->val.u1 == IS_STRING) { php_write_str(ast->val.value.str); return EX_NORMAL; }
        for (uint32_t i = 0; i < ast->nchild; i++) {
            zval v = zval_null();
            if (zend_eval_expr(ast->child[i], &v)) return EXPR_FAIL();
            zend_string *s = zval_get_string(zval_deref(&v));
            if (EG(exception)) return EXPR_FAIL();
            php_write_str(s);
        }
        return EX_NORMAL;
    }

    case AST_EXPR_STMT: {
        zval v = zval_null();
        if (zend_eval_expr(ast->child[0], &v)) return EXPR_FAIL();
        zval_ptr_dtor(&v);
        return EX_NORMAL;
    }

    case AST_IF: {
        for (uint32_t i = 0; i < ast->nchild; i++) {
            zend_ast *e = ast->child[i];
            if (!e) continue;
            if (!e->child[0]) return exec_stmt(e->child[1], retval);
            zval c = zval_null();
            if (zend_eval_expr(e->child[0], &c)) return EXPR_FAIL();
            if (zval_is_true(zval_deref(&c))) return exec_stmt(e->child[1], retval);
        }
        return EX_NORMAL;
    }

    case AST_WHILE: {
        while (1) {
            zval c = zval_null();
            if (zend_eval_expr(ast->child[0], &c)) return EXPR_FAIL();
            if (!zval_is_true(zval_deref(&c))) break;
            ex_status st = exec_stmt(ast->child[1], retval);
            int stop = 0;
            st = loop_status(st, &stop);
            if (stop) return st;
        }
        return EX_NORMAL;
    }

    case AST_DO_WHILE: {
        while (1) {
            ex_status st = exec_stmt(ast->child[0], retval);
            int stop = 0;
            st = loop_status(st, &stop);
            if (stop) return st;
            zval c = zval_null();
            if (zend_eval_expr(ast->child[1], &c)) return EXPR_FAIL();
            if (!zval_is_true(zval_deref(&c))) break;
        }
        return EX_NORMAL;
    }

    case AST_FOR: {
        zval t = zval_null();
        if (ast->child[0] && zend_eval_expr(ast->child[0], &t)) return EXPR_FAIL();
        while (1) {
            if (ast->child[1]) {
                zval c = zval_null();
                if (zend_eval_expr(ast->child[1], &c)) return EXPR_FAIL();
                if (!zval_is_true(zval_deref(&c))) break;
            }
            ex_status st = exec_stmt(ast->child[3], retval);
            int stop = 0;
            st = loop_status(st, &stop);
            if (stop) return st;
            if (ast->child[2]) {
                zval s = zval_null();
                if (zend_eval_expr(ast->child[2], &s)) return EXPR_FAIL();
            }
        }
        return EX_NORMAL;
    }

    case AST_FOREACH:
        return exec_foreach(ast, retval);

    case AST_SWITCH:
        return exec_switch(ast, retval);

    case AST_BREAK:
        zend_break_levels = level_of(ast);
        return EX_BREAK;

    case AST_CONTINUE:
        zend_break_levels = level_of(ast);
        return EX_CONTINUE;

    case AST_RETURN: {
        zval v = zval_null();
        zend_function *fn = EG(current) ? EG(current)->func : NULL;
        if (ast->nchild && ast->child[0] && fn && fn_returns_ref(fn) &&
            is_var_ast(ast->child[0])) {
            /* "return $x" from a &function() is a write fetch in php-src, so it
             * neither reports an undefined variable nor copies the value */
            int err = 0;
            zval *slot = fetch_lval_ex(ast->child[0], FETCH_W, &err);
            if (!slot) return EXPR_FAIL();
            zend_reference *r = zend_make_reference(slot);
            r->refcount++;
            v.u1 = IS_REFERENCE;
            v.value.ref = r;
            *retval = v;
            return EX_RETURN;
        }
        if (ast->nchild && ast->child[0] && zend_eval_expr(ast->child[0], &v))
            return EXPR_FAIL();
        *retval = v;
        return EX_RETURN;
    }

    case AST_GLOBAL:
        exec_global(ast);
        return EG(exception) ? EX_EXCEPTION : EX_NORMAL;

    case AST_STATIC_VAR:
        return exec_static_var(ast) ? EXPR_FAIL() : EX_NORMAL;

    case AST_TRY:
        return exec_try(ast, retval);

    case AST_THROW: {
        zval v = zval_null();
        if (zend_eval_expr(ast->child[0], &v)) return EXPR_FAIL();
        zval *d = zval_deref(&v);
        if (d->u1 != IS_OBJECT) {
            /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
            if (EG(php_version) >= 70000)
                zend_throw_error("Error", "Can only throw objects");
            else
                zend_fatal("Can only throw objects");
            return EX_EXCEPTION;
        }
        zend_throw_exception_object(d->value.obj);
        return EX_EXCEPTION;
    }

    case AST_GOTO: {
        zend_string *nm = ast->val.u1 == IS_STRING ? ast->val.value.str
                        : (ast->nchild ? ast_name(ast->child[0]) : NULL);
        zend_goto_target = nm;
        zend_goto_lineno = ast->lineno;
        return EX_GOTO;
    }

    case AST_FUNC_DECL: {
        zend_function *fn = (zend_function *)ZEND_ZVAL_PTR(ast->val);
        if (fn) zend_declare_function(fn);
        return EG(exception) ? EX_EXCEPTION : EX_NORMAL;
    }

    case AST_CLASS_DECL: {
        zend_class_entry *decl = (zend_class_entry *)ZEND_ZVAL_PTR(ast->val);
        if (decl) zend_declare_class(decl);
        return EG(exception) ? EX_EXCEPTION : EX_NORMAL;
    }

    case AST_CONST_DECL:
        return exec_const_decl(ast) ? EXPR_FAIL() : EX_NORMAL;

    case AST_DECLARE: {
        exec_declare(ast);
        if (EG(exception)) return EXPR_FAIL();
        if (ast->nchild > 1 && ast->child[1]) return exec_stmt(ast->child[1], retval);
        return EX_NORMAL;
    }

    case AST_UNSET: {
        for (uint32_t i = 0; i < ast->nchild; i++)
            if (unset_ast(ast->child[i])) return EXPR_FAIL();
        return EX_NORMAL;
    }

    default: {
        zval v = zval_null();
        if (zend_eval_expr(ast, &v)) return EXPR_FAIL();
        zval_ptr_dtor(&v);
        return EX_NORMAL;
    }
    }
}

ex_status zend_execute_ast(zend_ast *ast, zval *retval)
{
    if (retval) *retval = zval_null();
    zval dummy = zval_null();
    return exec_stmt(ast, retval ? retval : &dummy);
}

void zend_execute_script(zend_ast *ast)
{
    if (!ast) return;
    zend_validate_jumps(ast);
    if (EG(exiting) || EG(exception)) return;
    zend_hoist_declarations(ast);
    if (EG(exception)) return;
    zval ret = zval_null();
    ex_status st = zend_execute_ast(ast, &ret);
    if (st == EX_GOTO && zend_goto_target) report_bad_goto(ast);
    else if (st == EX_BREAK || st == EX_CONTINUE)
        zend_fatal("Cannot break/continue %d level", zend_break_levels);
}

/* ======================================================================== */
/*  class declaration                                                       */
/* ======================================================================== */

static uint32_t fn_required_args(zend_function *fn)
{
    uint32_t r = 0;
    if (!fn->args) return 0;
    for (uint32_t i = 0; i < fn->num_args; i++) {
        if (fn->args[i].default_ast || fn->args[i].variadic) break;
        r = i + 1;
    }
    return r;
}

static int fn_returns_ref(zend_function *fn)
{
    return fn->returns_ref || (fn->flags & ZEND_ACC_RETURN_REF) != 0;
}

/* --------------------------------------------------- printing a signature */

/* php-src renders a default value with a handful of special cases; anything it
 * cannot spell out becomes "<expression>". */
static zend_string *default_ast_text(zend_ast *a)
{
    if (!a) return zend_string_initz("NULL");
    if (a->kind == AST_CONST && a->val.u1 == IS_STRING) {
        zend_string *n = a->val.value.str;
        if (zend_string_equals_ci(n, S("true")))  return zend_string_initz("true");
        if (zend_string_equals_ci(n, S("false"))) return zend_string_initz("false");
        if (zend_string_equals_ci(n, S("null")))  return zend_string_initz("NULL");
        return n;
    }
    if (a->kind == AST_CLASS_CONST) {
        zend_string *c = ast_name(a->child[0]);
        zend_string *m = ast_name(a->child[1]);
        if (c && m) return zend_strpprintf("%s::%s", c->val, m->val);
        return zend_string_initz("<expression>");
    }
    if (a->kind == AST_ARRAY) return zend_string_initz("Array");
    if (a->kind != AST_ZVAL) return zend_string_initz("<expression>");
    zval *v = &a->val;
    switch (v->u1) {
    case IS_UNDEF: case IS_NULL: return zend_string_initz("NULL");
    case IS_TRUE:  return zend_string_initz("true");
    case IS_FALSE: return zend_string_initz("false");
    case IS_ARRAY: return zend_string_initz("Array");
    case IS_STRING: {
        zend_string *s = v->value.str;
        size_t n = s->len < 10 ? s->len : 10;
        zend_string *out = zend_string_initz("'");
        out = zend_string_concat(out, zend_string_init(s->val, n));
        if (s->len > 10) out = zend_string_concat(out, S("..."));
        return zend_string_concat(out, S("'"));
    }
    default:
        return zval_get_string(v);
    }
}

/* php-src's zend_get_function_declaration(), byte for byte. */
static zend_string *fn_declaration(zend_function *fn)
{
    zend_string *s = zend_string_initz("");
    if (fn_returns_ref(fn)) s = zend_string_concat(s, S("& "));
    zend_class_entry *owner = zend_trait_of_function(fn);
    if (!owner) owner = fn->scope;
    if (owner) {
        s = zend_string_concat(s, owner->name);
        s = zend_string_concat(s, S("::"));
    }
    s = zend_string_concat(s, fn->name ? fn->name : S("{closure}"));
    s = zend_string_concat(s, S("("));
    uint32_t required = fn_required_args(fn);
    for (uint32_t i = 0; i < fn->num_args; i++) {
        zend_arg_info *ai = &fn->args[i];
        if (ai->type && ai->type->n) {
            const char *tn = ai->type->names[0]->val;
            if (!strcasecmp(tn, "self") && fn->scope) s = zend_string_concat(s, fn->scope->name);
            else if (!strcasecmp(tn, "parent") && fn->scope && fn->scope->parent)
                s = zend_string_concat(s, fn->scope->parent->name);
            else s = zend_string_concat(s, ai->type->names[0]);
            s = zend_string_concat(s, S(" "));
        }
        if (ai->by_ref)   s = zend_string_concat(s, S("&"));
        if (ai->variadic) s = zend_string_concat(s, S("..."));
        s = zend_string_concat(s, S("$"));
        s = zend_string_concat(s, ai->name ? ai->name : zend_strpprintf("param%u", i));
        if (i >= required && !ai->variadic) {
            s = zend_string_concat(s, S(" = "));
            s = zend_string_concat(s, default_ast_text(ai->default_ast));
        }
        if (i + 1 < fn->num_args) s = zend_string_concat(s, S(", "));
    }
    return zend_string_concat(s, S(")"));
}

/* --------------------------------------------- signature compatibility */

static zend_string *arg_class_name(zend_arg_info *ai)
{
    if (!ai->type || ai->type->n != 1) return NULL;
    return type_name_is_class(ai->type->names[0]->val) ? ai->type->names[0] : NULL;
}

static const char *arg_scalar_hint(zend_arg_info *ai)
{
    if (!ai->type || ai->type->n != 1) return NULL;
    const char *n = ai->type->names[0]->val;
    return type_name_is_class(n) ? NULL : n;
}

/* php-src's zend_do_perform_implementation_check(). */
static int impl_check(zend_function *fe, zend_function *proto)
{
    if (!proto || !proto->args) {
        if (!proto) return 1;
        if (proto->num_args == 0 && fn_required_args(proto) == 0 && proto->is_internal) return 1;
    }
    if ((fe->flags & ZEND_ACC_CTOR) &&
        !(proto->scope && (proto->scope->flags & ZEND_ACC_INTERFACE)) &&
        !(proto->flags & ZEND_ACC_ABSTRACT) && !proto->is_abstract)
        return 1;
    if (proto->flags & ZEND_ACC_PRIVATE) return 1;

    if (fn_required_args(proto) < fn_required_args(fe)) return 0;
    if (proto->num_args > fe->num_args) return 0;
    if (fn_returns_ref(proto) && !fn_returns_ref(fe)) return 0;
    if ((proto->flags & ZEND_ACC_VARIADIC) && !(fe->flags & ZEND_ACC_VARIADIC)) return 0;

    uint32_t num_args = proto->num_args;
    if ((proto->flags & ZEND_ACC_VARIADIC) && fe->num_args > proto->num_args)
        num_args = fe->num_args;

    for (uint32_t i = 0; i < num_args; i++) {
        if (!fe->args || i >= fe->num_args) return 0;
        zend_arg_info *fa = &fe->args[i];
        zend_arg_info *pa = (i < proto->num_args) ? &proto->args[i]
                          : &proto->args[proto->num_args - 1];
        zend_string *fc = arg_class_name(fa), *pc = arg_class_name(pa);
        if ((fc != NULL) != (pc != NULL)) return 0;
        if (fc) {
            zend_string *fn_name = fc, *pn_name = pc;
            if (zend_string_equals_ci(fc, S("parent")) && proto->scope) fn_name = proto->scope->name;
            else if (zend_string_equals_ci(fc, S("self")) && fe->scope) fn_name = fe->scope->name;
            if (zend_string_equals_ci(pc, S("parent")) && proto->scope && proto->scope->parent)
                pn_name = proto->scope->parent->name;
            else if (zend_string_equals_ci(pc, S("self")) && proto->scope) pn_name = proto->scope->name;
            if (!zend_string_equals_ci(fn_name, pn_name)) {
                zend_class_entry *a = zend_lookup_class(fn_name, 0);
                zend_class_entry *b = zend_lookup_class(pn_name, 0);
                if (!a || !b || a != b) return 0;
            }
        }
        const char *fh = arg_scalar_hint(fa), *ph = arg_scalar_hint(pa);
        if ((fh != NULL) != (ph != NULL)) return 0;
        if (fh && strcasecmp(fh, ph) != 0) return 0;
        if (fa->by_ref != pa->by_ref) return 0;
    }
    return 1;
}

/* --------------------------------------------------- magic method checks */

/* The magic-method visibility rules are 5.6 behaviour and have no version
 * branch here (8.0 turns these warnings into Errors; not implemented). */
static void check_magic_method(zend_class_entry *ce, zend_function *fn)
{
    static const char *nonstatic[] = { "__call", "__get", "__set", "__unset", "__isset",
                                       "__tostring", "__invoke", "__debuginfo", NULL };
    const char *lc = fn->lcname->val;
    uint32_t vis = fn->flags & ZEND_ACC_VISIBILITY;
    int is_static = (fn->flags & ZEND_ACC_STATIC) != 0;
    int bad_vis = vis && !(vis & ZEND_ACC_PUBLIC);
    (void)ce;
    if (!strcmp(lc, "__callstatic")) {
        if (bad_vis || !is_static)
            zend_error(E_WARNING, "The magic method __callStatic() must have public visibility and be static");
        return;
    }
    for (int i = 0; nonstatic[i]; i++) {
        if (strcmp(lc, nonstatic[i])) continue;
        if (bad_vis || is_static) {
            const char *shown = !strcmp(lc, "__tostring") ? "__toString"
                              : !strcmp(lc, "__debuginfo") ? "__debugInfo" : fn->name->val;
            zend_error(E_WARNING, "The magic method %s() must have public visibility and cannot be static", shown);
        }
        return;
    }
}

/* php-src's zend_check_magic_method_implementation(), 5.6 wording. */
static void check_magic_signature(zend_class_entry *ce, zend_function *fn)
{
    const char *lc = fn->lcname->val;
    uint32_t n = fn->num_args;
    int any_ref = 0;
    for (uint32_t i = 0; i < n && fn->args; i++) if (fn->args[i].by_ref) any_ref = 1;
    const char *cn = ce->name->val;

    /* VERSIONGATE:70000 the guarded branch is the 5.6 behaviour */
    if (EG(php_version) < 70000 && ((fn->flags & ZEND_ACC_ABSTRACT) || fn->is_abstract) &&
        (fn->flags & ZEND_ACC_STATIC) && !(ce->flags & ZEND_ACC_INTERFACE))
        zend_error(E_STRICT, "Static function %s::%s() should not be abstract", cn, fn->name->val);
    if (!strcmp(lc, "__destruct")) {
        if (fn->flags & ZEND_ACC_STATIC)
            zend_fatal("Destructor %s::__destruct() cannot be static", cn);
        else if (n != 0) zend_fatal("Destructor %s::__destruct() cannot take arguments", cn);
        return;
    }
    if (!strcmp(lc, "__construct")) {
        if (fn->flags & ZEND_ACC_STATIC)
            zend_fatal("Constructor %s::%s() cannot be static", cn, fn->name->val);
        return;
    }
    if (!strcmp(lc, "__clone")) {
        if (fn->flags & ZEND_ACC_STATIC)
            zend_fatal("Constructor %s::__clone() cannot be static", cn);
        else if (n != 0) zend_fatal("Method %s::__clone() cannot take arguments", cn);
        return;
    }
    struct { const char *lc; const char *shown; uint32_t want; } one[] = {
        { "__get",    "__get",    1 }, { "__unset", "__unset", 1 },
        { "__isset",  "__isset",  1 }, { "__set",   "__set",   2 },
        { "__call",   "__call",   2 }, { "__callstatic", "__callStatic", 2 },
    };
    for (size_t i = 0; i < sizeof(one) / sizeof(one[0]); i++) {
        if (strcmp(lc, one[i].lc)) continue;
        if (n != one[i].want)
            zend_fatal("Method %s::%s() must take exactly %u argument%s", cn, one[i].shown,
                       one[i].want, one[i].want == 1 ? "" : "s");
        else if (any_ref)
            zend_fatal("Method %s::%s() cannot take arguments by reference", cn, one[i].shown);
        return;
    }
    if (!strcmp(lc, "__tostring")) {
        if (n != 0) zend_fatal("Method %s::__toString() cannot take arguments", cn);
        return;
    }
    if (!strcmp(lc, "__debuginfo")) {
        if (n != 0) zend_fatal("Method %s::__debugInfo() cannot take arguments", cn);
        return;
    }
}

static void ce_record_magic_ex(zend_class_entry *ce, zend_function *fn, int quiet)
{
    const char *lc = fn->lcname->val;
    if (!strcmp(lc, "__construct")) {
        /* VERSIONGATE:80000 the guarded branch is the 5.6 behaviour: a PHP 4
         * style constructor already claimed the slot */
        if (!quiet && EG(php_version) < 80000 && ce->constructor && ce->constructor != fn)
            zend_error(E_STRICT, "Redefining already defined constructor for class %s",
                       ce->name->val);
        if (quiet && ce->constructor) return;   /* the class's own wins */
        ce->constructor = fn;
        fn->flags |= ZEND_ACC_CTOR;
    }
    else if (!strcmp(lc, "__destruct"))   ce->destructor = fn;
    else if (!strcmp(lc, "__clone"))      ce->clone = fn;
    else if (!strcmp(lc, "__get"))        ce->__get = fn;
    else if (!strcmp(lc, "__set"))        ce->__set = fn;
    else if (!strcmp(lc, "__isset"))      ce->__isset = fn;
    else if (!strcmp(lc, "__unset"))      ce->__unset = fn;
    else if (!strcmp(lc, "__call"))       ce->__call = fn;
    else if (!strcmp(lc, "__callstatic")) ce->__callstatic = fn;
    else if (!strcmp(lc, "__tostring"))   ce->__tostring = fn;
    else if (!strcmp(lc, "__invoke"))     ce->__invoke = fn;
    else if (!strcmp(lc, "__debuginfo"))  ce->__debuginfo = fn;
    else if (!strcmp(lc, "__set_state"))  ce->__set_state = fn;
    else if (!strcmp(lc, "__serialize"))  ce->__serialize = fn;
    /* VERSIONGATE:80000 the guarded branch is the 5.6 behaviour: a method named
     * like its class is still a constructor */
    else if (EG(php_version) < 80000 && !ce->constructor && !(ce->flags & ZEND_ACC_TRAIT) &&
             zend_string_equals_ci(fn->name, ce->name)) {
        ce->constructor = fn;
        fn->flags |= ZEND_ACC_CTOR;
    }
}

static void ce_record_magic(zend_class_entry *ce, zend_function *fn)
{
    ce_record_magic_ex(ce, fn, 0);
}

/* --------------------------------------------------------- inheritance */

static const char *visibility_string(uint32_t flags)
{
    if (flags & ZEND_ACC_PRIVATE) return "private";
    if (flags & ZEND_ACC_PROTECTED) return "protected";
    return "public";
}

static void do_inheritance_check_on_method(zend_function *child, zend_function *parent)
{
    uint32_t pf = parent->flags, cf = child->flags;
    const char *cscope = child->scope ? child->scope->name->val : "";
    const char *pscope = parent->scope ? parent->scope->name->val : "";

    if (pf & ZEND_ACC_FINAL) {
        zend_fatal("Cannot override final method %s::%s()", pscope, child->name->val);
        return;
    }
    if ((cf & ZEND_ACC_STATIC) != (pf & ZEND_ACC_STATIC)) {
        if (cf & ZEND_ACC_STATIC)
            zend_fatal("Cannot make non static method %s::%s() static in class %s",
                       pscope, child->name->val, cscope);
        else
            zend_fatal("Cannot make static method %s::%s() non static in class %s",
                       pscope, child->name->val, cscope);
        return;
    }
    if ((cf & ZEND_ACC_ABSTRACT) && !(pf & ZEND_ACC_ABSTRACT)) {
        zend_fatal("Cannot make non abstract method %s::%s() abstract in class %s",
                   pscope, child->name->val, cscope);
        return;
    }
    /* a derived class may not narrow the visibility */
    uint32_t cv = cf & ZEND_ACC_VISIBILITY, pv = pf & ZEND_ACC_VISIBILITY;
    if (!cv) cv = ZEND_ACC_PUBLIC;
    if (!pv) pv = ZEND_ACC_PUBLIC;
    if (cv > pv) {
        zend_fatal("Access level to %s::%s() must be %s (as in class %s)%s",
                   cscope, child->name->val, visibility_string(pf), pscope,
                   (pv & ZEND_ACC_PUBLIC) ? "" : " or weaker");
        return;
    }

    if (pf & ZEND_ACC_PRIVATE) { child->prototype = NULL; return; }
    if (pf & ZEND_ACC_ABSTRACT) child->prototype = parent;
    else if (!(pf & ZEND_ACC_CTOR) ||
             (parent->prototype && parent->prototype->scope &&
              (parent->prototype->scope->flags & ZEND_ACC_INTERFACE)))
        child->prototype = parent->prototype ? parent->prototype : parent;

    if (child->prototype &&
        ((child->prototype->flags & ZEND_ACC_ABSTRACT) || child->prototype->is_abstract)) {
        if (!impl_check(child, child->prototype))
            zend_fatal("Declaration of %s::%s() must be compatible with %s",
                       cscope, child->name->val, fn_declaration(child->prototype)->val);
    } else if (!impl_check(child, parent)) {
        /* VERSIONGATE:80000 else-branch is the 5.6 severity (E_STRICT) */
        zend_error(EG(php_version) >= 80000 ? E_WARNING : E_STRICT,
                   "Declaration of %s::%s() should be compatible with %s",
                   cscope, child->name->val, fn_declaration(parent)->val);
    }
}

static void inherit_method(zend_class_entry *ce, zend_function *pf)
{
    zval *own = zend_hash_find(ce->function_table, pf->lcname);
    if (!own) {
        if (pf->flags & ZEND_ACC_PRIVATE) {
            /* a private parent method is not visible in the child, but it must
             * still be reachable through the parent chain */
            return;
        }
        zend_hash_update(ce->function_table, pf->lcname, ZEND_PTR_ZVAL(pf));
        return;
    }
    zend_function *cf = (zend_function *)ZEND_ZVAL_PTR_P(own);
    if (cf == pf) return;
    do_inheritance_check_on_method(cf, pf);
}

static void inherit_from(zend_class_entry *ce, zend_class_entry *parent)
{
    if (!parent) return;
    ZEND_HASH_FOREACH(parent->function_table, b) {
        inherit_method(ce, (zend_function *)ZEND_ZVAL_PTR(b->val));
        if (EG(exiting)) return;
    } ZEND_HASH_FOREACH_END();
    ZEND_HASH_FOREACH(parent->properties_info, b) {
        zend_property_info *pi = (zend_property_info *)ZEND_ZVAL_PTR(b->val);
        if (!pi || (pi->flags & ZEND_ACC_PRIVATE)) continue;   /* private stays home */
        if (!zend_hash_find(ce->properties_info, b->key))
            zend_hash_update(ce->properties_info, b->key, b->val);
    } ZEND_HASH_FOREACH_END();
    if (!ce->constructor)  ce->constructor  = parent->constructor;
    if (!ce->destructor)   ce->destructor   = parent->destructor;
    if (!ce->clone)        ce->clone        = parent->clone;
    if (!ce->__get)        ce->__get        = parent->__get;
    if (!ce->__set)        ce->__set        = parent->__set;
    if (!ce->__isset)      ce->__isset      = parent->__isset;
    if (!ce->__unset)      ce->__unset      = parent->__unset;
    if (!ce->__call)       ce->__call       = parent->__call;
    if (!ce->__callstatic) ce->__callstatic = parent->__callstatic;
    if (!ce->__tostring)   ce->__tostring   = parent->__tostring;
    if (!ce->__invoke)     ce->__invoke     = parent->__invoke;
    if (!ce->__debuginfo)  ce->__debuginfo  = parent->__debuginfo;
    if (!ce->__set_state)  ce->__set_state  = parent->__set_state;
    if (!ce->__serialize)  ce->__serialize  = parent->__serialize;
}

/* Interface constants may not be redefined by the implementing class or by a
 * second interface that carries the same name. */
static void inherit_interface_constants(zend_class_entry *ce, zend_class_entry *iface)
{
    ZEND_HASH_FOREACH(iface->constants_table, b) {
        for (zend_class_entry *c = ce; c; c = c->parent) {
            if (zend_hash_find(c->constants_table, b->key)) {
                zend_fatal("Cannot inherit previously-inherited or override constant %s from interface %s",
                           b->key->val, iface->name->val);
                return;
            }
        }
        for (uint32_t i = 0; i < ce->num_interfaces; i++) {
            if (ce->interfaces[i] == iface || !ce->interfaces[i]) continue;
            if (zend_hash_find(ce->interfaces[i]->constants_table, b->key)) {
                zend_fatal("Cannot inherit previously-inherited or override constant %s from interface %s",
                           b->key->val, iface->name->val);
                return;
            }
        }
    } ZEND_HASH_FOREACH_END();
}

/* ------------------------------------------------------------- traits */

typedef struct {
    zend_class_entry **traits;
    uint32_t n;
    HashTable *origin;         /* lcname => the trait a method came from  */
    HashTable *own;            /* lcnames declared in the class body      */
    HashTable *shadow;         /* trait methods the class's own overrode  */
    HashTable *prop_origin;    /* property name => the trait that added it */
} trait_ctx;

static zend_class_entry *trait_by_name(trait_ctx *tc, zend_string *name, zend_class_entry *ce)
{
    zend_class_entry *found = zend_lookup_class(name, 1);
    if (found && !(found->flags & ZEND_ACC_TRAIT)) {
        zend_fatal("Class %s is not a trait, Only traits may be used in 'as' and 'insteadof' statements",
                   found->name->val);
        return NULL;
    }
    for (uint32_t i = 0; i < tc->n; i++)
        if (tc->traits[i] == found) return found;
    zend_fatal("Required Trait %s wasn't added to %s",
               found ? found->name->val : name->val, ce->name->val);
    return NULL;
}

static HashTable *trait_of_method(void)
{
    static HashTable *t = NULL;
    if (!t) t = zend_hash_new();
    return t;
}

zend_class_entry *zend_trait_of_function(zend_function *fn)
{
    if (!fn) return NULL;
    zval *v = zend_hash_find(trait_of_method(), zend_strpprintf("%p", (void *)fn));
    return v ? (zend_class_entry *)ZEND_ZVAL_PTR_P(v) : NULL;
}

static void use_trait_method(zend_class_entry *ce, trait_ctx *tc, zend_function *tf,
                             zend_class_entry *trait, zend_string *alias, uint32_t newvis)
{
    zend_function *c = ecalloc(1, sizeof(zend_function));
    *c = *tf;
    if (alias) { c->name = alias; c->lcname = zend_string_tolower(alias); }
    c->scope = ce;
    c->prototype = NULL;
    if (newvis) c->flags = (c->flags & ~ZEND_ACC_VISIBILITY) | newvis;
    if (!c->static_vars) c->static_vars = zend_hash_new();
    zend_hash_update(ce->function_table, c->lcname, ZEND_PTR_ZVAL(c));
    zval tv; tv.u1 = IS_LONG; tv.value.lval = (zend_long)(intptr_t)trait;
    zend_hash_update(tc->origin, c->lcname, tv);
    zend_class_entry *root = zend_trait_of_function(tf);
    zval rv2; rv2.u1 = IS_LONG; rv2.value.lval = (zend_long)(intptr_t)(root ? root : trait);
    zend_hash_update(trait_of_method(), zend_strpprintf("%p", (void *)c), rv2);
    ce_record_magic_ex(ce, c, 1);
    check_magic_method(ce, c);
}

static void merge_trait_properties(zend_class_entry *ce, trait_ctx *tc, zend_class_entry *trait)
{
    ZEND_HASH_FOREACH(trait->properties_info, b) {
        zend_property_info *pi = (zend_property_info *)ZEND_ZVAL_PTR(b->val);
        if (!pi) continue;
        zval *ex = zend_hash_find(ce->properties_info, pi->name);
        if (ex) {
            zend_property_info *cp = (zend_property_info *)ZEND_ZVAL_PTR_P(ex);
            zval *ov = zend_hash_find(tc->prop_origin, pi->name);
            zend_class_entry *first = ov ? (zend_class_entry *)ZEND_ZVAL_PTR_P(ov)
                                         : (cp->ce ? cp->ce : ce);
            int same_flags = (cp->flags & (ZEND_ACC_VISIBILITY | ZEND_ACC_STATIC)) ==
                             (pi->flags & (ZEND_ACC_VISIBILITY | ZEND_ACC_STATIC));
            int incompatible = !same_flags;
            if (same_flags) {
                zval a = zval_null(), bb = zval_null();
                if (cp->default_ast && zend_eval_expr(cp->default_ast, &a)) return;
                if (pi->default_ast && zend_eval_expr(pi->default_ast, &bb)) return;
                int r = 0;
                if (zend_compare(&a, &bb, &r)) return;
                incompatible = (r != 0);
            }
            if (incompatible) {
                zend_fatal("%s and %s define the same property ($%s) in the composition of %s. However, the definition differs and is considered incompatible. Class was composed",
                           first->name->val, trait->name->val, pi->name->val, ce->name->val);
                return;
            }
            zend_error(E_STRICT,
                       "%s and %s define the same property ($%s) in the composition of %s. This might be incompatible, to improve maintainability consider using accessor methods in traits instead. Class was composed",
                       first->name->val, trait->name->val, pi->name->val, ce->name->val);
            continue;
        }
        /* VERSIONGATE:80100 BEGIN - enums take no properties, not even from a trait */
        if (ce->is_enum) {
            zend_fatal("Enum \"%s\" may not include properties", ce->name->val);
            return;
        }
        /* VERSIONGATE:80100 END */
        zend_property_info *np = ecalloc(1, sizeof(zend_property_info));
        *np = *pi;
        np->ce = ce;
        np->mangled = zend_mangle_property_name(ce, np->name, np->flags);
        zend_hash_update(ce->properties_info, np->name, ZEND_PTR_ZVAL(np));
        zval tv; tv.u1 = IS_LONG; tv.value.lval = (zend_long)(intptr_t)trait;
        zend_hash_update(tc->prop_origin, np->name, tv);
    } ZEND_HASH_FOREACH_END();
}

/* A trait constructor standing next to a PHP 4 style one in the using class is
 * a collision.  PHP 4 style constructors are gone in 8.0, so the rule is. */
/* VERSIONGATE:80000 the guarded branch is the 5.6 behaviour */
static int trait_ctor_collision(zend_class_entry *ce)
{
    if (EG(php_version) >= 80000) return 0;
    zval *tc_ctor = zend_hash_find(ce->function_table, S("__construct"));
    zval *old_ctor = zend_hash_find(ce->function_table, zend_string_tolower(ce->name));
    if (tc_ctor && old_ctor &&
        zend_trait_of_function((zend_function *)ZEND_ZVAL_PTR_P(tc_ctor)) &&
        !zend_trait_of_function((zend_function *)ZEND_ZVAL_PTR_P(old_ctor))) {
        zend_fatal("%s has colliding constructor definitions coming from traits",
                   ce->name->val);
        return 1;
    }
    return 0;
}

/* Traits are PHP 5.4 machinery: nothing here is version gated. */
static void apply_traits(zend_class_entry *ce, zend_class_decl_extra *extra)
{
    if (!extra || !extra->num_traits) return;
    if (ce->flags & ZEND_ACC_INTERFACE) {
        zend_fatal("Cannot use traits inside of interfaces. %s is used in %s",
                   extra->trait_names[0]->val, ce->name->val);
        return;
    }
    trait_ctx tc;
    tc.n = extra->num_traits;
    tc.traits = ecalloc(tc.n, sizeof(zend_class_entry *));
    tc.origin = zend_hash_new();
    tc.prop_origin = zend_hash_new();
    tc.own = zend_hash_new();
    tc.shadow = zend_hash_new();
    ZEND_HASH_FOREACH(ce->function_table, b) {
        zend_function *f = (zend_function *)ZEND_ZVAL_PTR(b->val);
        if (f && f->scope == ce) zend_hash_update(tc.own, b->key, zval_bool(1));
    } ZEND_HASH_FOREACH_END();
    for (uint32_t i = 0; i < tc.n; i++) {
        tc.traits[i] = zend_lookup_class(extra->trait_names[i], 1);
        if (!tc.traits[i]) { zend_fatal("Trait '%s' not found", extra->trait_names[i]->val); return; }
        if (!(tc.traits[i]->flags & ZEND_ACC_TRAIT)) {
            zend_fatal("%s cannot use %s - it is not a trait", ce->name->val, tc.traits[i]->name->val);
            return;
        }
    }

    zend_ast *ad = extra->trait_adaptations;

    /* "insteadof": validate and collect the exclusions */
    HashTable *excluded = zend_hash_new();
    for (uint32_t i = 0; ad && i < ad->nchild; i++) {
        zend_ast *a = ad->child[i];
        if (!a || a->attr != 1) continue;
        zend_string *tn = a->child[0] ? ast_name(a->child[0]) : NULL;
        zend_string *mname = ast_name(a->child[1]);
        if (!mname) continue;
        zend_class_entry *src = NULL;
        if (tn) {
            src = trait_by_name(&tc, tn, ce);
            if (!src) return;
            if (!zend_hash_find(src->function_table, zend_string_tolower(mname))) {
                zend_fatal("A precedence rule was defined for %s::%s but this method does not exist",
                           src->name->val, mname->val);
                return;
            }
        }
        for (uint32_t j = 0; a->nchild > 2 && a->child[2] && j < a->child[2]->nchild; j++) {
            zend_string *xn = ast_name(a->child[2]->child[j]);
            if (!xn) continue;
            zend_class_entry *x = trait_by_name(&tc, xn, ce);
            if (!x) return;
            zend_string *key = zend_strpprintf("%s::%s", x->lcname->val,
                                               zend_string_tolower(mname)->val);
            zend_hash_update(excluded, key, zval_bool(1));
        }
    }

    /* merge the trait methods, trait by trait, in declaration order */
    for (uint32_t i = 0; i < tc.n; i++) {
        zend_class_entry *t = tc.traits[i];
        ZEND_HASH_FOREACH(t->function_table, b) {
            zend_function *tf = (zend_function *)ZEND_ZVAL_PTR(b->val);
            zend_string *key = zend_strpprintf("%s::%s", t->lcname->val, tf->lcname->val);
            if (zend_hash_find(excluded, key)) continue;
            zval *prev = zend_hash_find(ce->function_table, tf->lcname);
            if (prev) {
                zend_function *pf = (zend_function *)ZEND_ZVAL_PTR_P(prev);
                if (zend_hash_find(tc.own, tf->lcname)) {
                    /* the class's own method wins, but php-src still compares
                     * the trait declarations with each other */
                    zval *sh = zend_hash_find(tc.shadow, tf->lcname);
                    if (!sh) { zend_hash_update(tc.shadow, tf->lcname, ZEND_PTR_ZVAL(tf)); continue; }
                    zend_function *sf = (zend_function *)ZEND_ZVAL_PTR_P(sh);
                    int sa = (sf->flags & ZEND_ACC_ABSTRACT) || sf->is_abstract;
                    int ta = (tf->flags & ZEND_ACC_ABSTRACT) || tf->is_abstract;
                    if ((sa || ta) && !(sa ? impl_check(tf, sf) : impl_check(sf, tf))) {
                        zend_fatal("Declaration of %s must be compatible with %s",
                                   fn_declaration(tf)->val, fn_declaration(sf)->val);
                        return;
                    }
                    continue;
                }
                int prev_abstract = (pf->flags & ZEND_ACC_ABSTRACT) || pf->is_abstract;
                int cur_abstract  = (tf->flags & ZEND_ACC_ABSTRACT) || tf->is_abstract;
                if (prev_abstract || cur_abstract) {
                    int ok = prev_abstract ? impl_check(tf, pf) : impl_check(pf, tf);
                    if (!ok) {
                        zend_fatal("Declaration of %s must be compatible with %s",
                                   fn_declaration(tf)->val, fn_declaration(pf)->val);
                        return;
                    }
                    if (prev_abstract) use_trait_method(ce, &tc, tf, t, NULL, 0);
                    continue;
                }
                if (zend_hash_find(tc.origin, tf->lcname)) {
                    zend_fatal("Trait method %s has not been applied, because there are collisions with other trait methods on %s",
                               tf->name->val, ce->name->val);
                    return;
                }
                /* an inherited method is replaced by the trait's, after the
                 * usual inheritance check */
                zend_function *copy = ecalloc(1, sizeof(zend_function));
                *copy = *tf;
                copy->scope = ce;
                copy->prototype = NULL;
                do_inheritance_check_on_method(copy, pf);
                if (EG(exiting)) return;
                use_trait_method(ce, &tc, tf, t, NULL, 0);
                continue;
            }
            use_trait_method(ce, &tc, tf, t, NULL, 0);
        } ZEND_HASH_FOREACH_END();
        if (EG(exiting)) return;
        merge_trait_properties(ce, &tc, t);
        if (EG(exiting)) return;
    }

    if (trait_ctor_collision(ce)) return;

    /* "as": aliases and visibility changes */
    for (uint32_t i = 0; ad && i < ad->nchild; i++) {
        zend_ast *a = ad->child[i];
        if (!a || a->attr != 2) continue;
        zend_string *tn = a->child[0] ? ast_name(a->child[0]) : NULL;
        zend_string *mn = ast_name(a->child[1]);
        zend_string *nn = (a->nchild > 2 && a->child[2]) ? ast_name(a->child[2]) : NULL;
        uint32_t vis = 0;
        if (a->nchild > 3 && a->child[3] && a->child[3]->val.u1 == IS_LONG)
            vis = (uint32_t)a->child[3]->val.value.lval;
        if (!mn) continue;
        zend_function *src = NULL;
        zend_class_entry *srct = NULL;
        if (tn) {
            srct = trait_by_name(&tc, tn, ce);
            if (!srct) return;
            zval *v = zend_hash_find(srct->function_table, zend_string_tolower(mn));
            if (!v) {
                zend_fatal("An alias was defined for %s::%s but this method does not exist",
                           srct->name->val, mn->val);
                return;
            }
            src = (zend_function *)ZEND_ZVAL_PTR_P(v);
        } else {
            for (uint32_t k = 0; k < tc.n && !src; k++) {
                zval *v = zend_hash_find(tc.traits[k]->function_table, zend_string_tolower(mn));
                if (v) { src = (zend_function *)ZEND_ZVAL_PTR_P(v); srct = tc.traits[k]; }
            }
            if (!src) {
                zend_fatal("An alias (%s) was defined for method %s(), but this method does not exist",
                           nn ? nn->val : mn->val, mn->val);
                return;
            }
        }
        if (nn) use_trait_method(ce, &tc, src, srct, nn, vis);
        else {
            zval *own = zend_hash_find(ce->function_table, zend_string_tolower(mn));
            if (own) {
                zend_function *f = (zend_function *)ZEND_ZVAL_PTR_P(own);
                if (vis) f->flags = (f->flags & ~ZEND_ACC_VISIBILITY) | vis;
            } else use_trait_method(ce, &tc, src, srct, NULL, vis);
        }
    }
}

/* ------------------------------------------------------------- abstract */

static void verify_abstract_methods(zend_class_entry *ce)
{
    if (ce->flags & (ZEND_ACC_ABSTRACT | ZEND_ACC_INTERFACE | ZEND_ACC_TRAIT)) return;
    uint32_t missing = 0;
    zend_string *list = NULL;
    ZEND_HASH_FOREACH(ce->function_table, b) {
        zend_function *fn = (zend_function *)ZEND_ZVAL_PTR(b->val);
        if (!((fn->flags & ZEND_ACC_ABSTRACT) || fn->is_abstract)) continue;
        missing++;
        if (missing <= 3) {
            zend_string *one = zend_strpprintf("%s::%s",
                fn->scope ? fn->scope->name->val : ce->name->val, fn->name->val);
            list = list ? zend_string_concat(zend_string_concat(list, S(", ")), one) : one;
        }
    } ZEND_HASH_FOREACH_END();
    if (!missing) return;
    zend_fatal("Class %s contains %u abstract method%s and must therefore be declared abstract or implement the remaining methods (%s%s)",
               ce->name->val, missing, missing == 1 ? "" : "s",
               list ? list->val : "", missing > 3 ? ", ..." : "");
}

/* ------------------------------------------------------ default values */

int bind_class_constants(zend_class_entry *ce)
{
    if (!ce || ce->statics_initialized == 2) return 0;
    ce->statics_initialized = 2;
    if (ce->parent && bind_class_constants(ce->parent)) return 1;

    scope_frame f;
    push_scope_frame(&f, ce);
    int rc = 0;
    ZEND_HASH_FOREACH(ce->properties_info, b) {
        zend_property_info *pi = (zend_property_info *)ZEND_ZVAL_PTR(b->val);
        if (!pi) continue;
        if (pi->flags & ZEND_ACC_STATIC) {
            if (zend_hash_find(ce->static_members, pi->name)) continue;
            zval v = zval_null();
            if (pi->default_ast && zend_eval_expr(pi->default_ast, &v)) { rc = 1; break; }
            zend_hash_update(ce->static_members, pi->name, v);
        } else {
            if (!pi->mangled) pi->mangled = zend_mangle_property_name(pi->ce ? pi->ce : ce,
                                                                     pi->name, pi->flags);
            if (zend_hash_find(ce->default_properties, pi->mangled)) continue;
            zval v = zval_null();
            if (pi->default_ast && zend_eval_expr(pi->default_ast, &v)) { rc = 1; break; }
            zend_hash_update(ce->default_properties, pi->mangled, v);
        }
    } ZEND_HASH_FOREACH_END();
    pop_scope_frame(&f);
    return rc;
}

/* ------------------------------------------------------------- binding */

/* VERSIONGATE:80100 BEGIN - enums */
/* ======================================================================== */
/*  enums (8.1)                                                             */
/* ======================================================================== */
/* An enum is a final class whose "cases" are singleton instances of itself,
 * created once when the declaration binds.  Each case object carries a public
 * `name` property and, for a backed enum, a public `value`; both are immutable.
 * The three static methods cases()/from()/tryFrom() are internal handlers that
 * read the called scope out of the current frame. */

/* Only these node kinds may appear in an enum case value. */
static int ast_is_const_expr(zend_ast *a)
{
    if (!a) return 1;
    switch (a->kind) {
    case AST_ZVAL: case AST_CONST: case AST_MAGIC_CONST: case AST_CLASS_CONST:
    case AST_BINARY: case AST_UNARY: case AST_ARRAY: case AST_ARRAY_ELEM:
    case AST_ENCAPS: case AST_TERNARY: case AST_COALESCE: case AST_DIM:
        break;
    default:
        return 0;
    }
    for (uint32_t i = 0; i < a->nchild; i++)
        if (!ast_is_const_expr(a->child[i])) return 0;
    return 1;
}

static zend_class_entry *called_enum_ce(void)
{
    zend_execute_data *ex = EG(current);
    if (ex && ex->called_scope) return ex->called_scope;
    if (ex && ex->func && ex->func->scope) return ex->func->scope;
    return zend_get_current_scope();
}

static zval *enum_case_by_value(zend_class_entry *ce, zval *needle)
{
    if (!ce->enum_cases) return NULL;
    ZEND_HASH_FOREACH(ce->enum_cases, b) {
        zend_object *o = Z_OBJ(b->val);
        zval *v = zend_hash_find(o->properties, S("value"));
        if (v && zend_is_identical(zval_deref(v), needle)) return &b->val;
    } ZEND_HASH_FOREACH_END();
    return NULL;
}

static void zim_enum_cases(zval *ret, uint32_t argc, zval *argv)
{
    zend_class_entry *ce = called_enum_ce();
    HashTable *out = zend_hash_new();
    if (ce && ce->enum_cases) {
        ZEND_HASH_FOREACH(ce->enum_cases, b) {
            zend_hash_next_index_insert(out, zval_copy(b->val));
        } ZEND_HASH_FOREACH_END();
    }
    *ret = zval_array(out);
}

/* Shared body of from() and tryFrom(); `soft` returns NULL instead of throwing. */
static void enum_from_impl(zval *ret, uint32_t argc, zval *argv, int soft)
{
    zend_class_entry *ce = called_enum_ce();
    *ret = zval_null();
    if (!ce || argc < 1) return;
    zval *in = zval_deref(&argv[0]);
    const char *want = "int";
    if (ce->enum_backing_type && ce->enum_backing_type->n == 1)
        want = ce->enum_backing_type->names[0]->val;
    int want_str = !strcasecmp(want, "string");

    zval key = *in;
    if (want_str) {
        if (in->u1 == IS_LONG && !EG(strict_types)) key = zval_zstr(zval_get_string(in));
        else if (in->u1 != IS_STRING) {
            zend_throw_error("TypeError", "%s::%s(): Argument #1 ($value) must be of type %s, %s given",
                             ce->name->val, soft ? "tryFrom" : "from", want, zend_zval_type_name(in));
            return;
        }
    } else {
        if (in->u1 == IS_STRING && !EG(strict_types)) {
            zend_long l = 0; double d = 0;
            if (is_numeric_string(in->value.str->val, in->value.str->len, &l, &d, 0) != IS_LONG) {
                zend_throw_error("TypeError", "%s::%s(): Argument #1 ($value) must be of type %s, %s given",
                                 ce->name->val, soft ? "tryFrom" : "from", want, zend_zval_type_name(in));
                return;
            }
            key = zval_long(l);
        } else if (in->u1 != IS_LONG) {
            zend_throw_error("TypeError", "%s::%s(): Argument #1 ($value) must be of type %s, %s given",
                             ce->name->val, soft ? "tryFrom" : "from", want, zend_zval_type_name(in));
            return;
        }
    }

    zval *found = enum_case_by_value(ce, &key);
    if (found) { *ret = zval_copy(*found); return; }
    if (soft) { *ret = zval_null(); return; }
    if (key.u1 == IS_STRING)
        zend_throw_error("ValueError", "\"%s\" is not a valid backing value for enum \"%s\"",
                         key.value.str->val, ce->name->val);
    else
        zend_throw_error("ValueError", "%lld is not a valid backing value for enum \"%s\"",
                         (long long)key.value.lval, ce->name->val);
}

static void zim_enum_from(zval *ret, uint32_t argc, zval *argv)    { enum_from_impl(ret, argc, argv, 0); }
static void zim_enum_tryfrom(zval *ret, uint32_t argc, zval *argv) { enum_from_impl(ret, argc, argv, 1); }

/* A concrete (non-abstract, user written) method of this name, or NULL.  The
 * abstract stubs inherited from UnitEnum/BackedEnum do not count. */
static zend_function *enum_concrete_method(zend_class_entry *ce, const char *lcname)
{
    zval *v = zend_hash_find(ce->function_table, S(lcname));
    if (!v) return NULL;
    zend_function *f = (zend_function *)ZEND_ZVAL_PTR_P(v);
    if (!f || (f->flags & ZEND_ACC_ABSTRACT) || f->is_abstract) return NULL;
    return f;
}

/* php-src refuses these member kinds inside an enum. */
static void enum_validate(zend_class_entry *ce)
{
    if (ce->enum_backing_type) {
        zend_type_hint *t = ce->enum_backing_type;
        const char *n = (t->n == 1) ? t->names[0]->val : NULL;
        if (!n || (strcasecmp(n, "int") && strcasecmp(n, "string"))) {
            zend_fatal("Enum backing type must be int or string, %s given",
                       type_hint_string(t)->val);
            return;
        }
    }
    if (zend_hash_num_elements(ce->properties_info)) {
        zend_fatal("Enums may not include properties");
        return;
    }
    static const char *banned[] = { "__construct", "__destruct", "__clone", "__get",
                                    "__set", "__unset", "__isset", "__sleep",
                                    "__wakeup", "__set_state", "__serialize",
                                    "__unserialize", NULL };
    for (int i = 0; banned[i]; i++) {
        if (enum_concrete_method(ce, banned[i])) {
            zend_fatal("Enum may not include %s", banned[i]);
            return;
        }
    }
    static const char *reserved[] = { "cases", "from", "tryfrom", NULL };
    static const char *shown[]    = { "cases", "from", "tryFrom", NULL };
    for (int i = 0; reserved[i]; i++) {
        zend_function *f = enum_concrete_method(ce, reserved[i]);
        if (!f || zend_trait_of_function(f)) continue;   /* a trait's is replaced */
        if (i > 0 && !ce->is_backed_enum) continue;    /* from/tryFrom only exist when backed */
        zend_fatal("Cannot redeclare %s::%s()", ce->name->val, shown[i]);
        return;
    }
}

static void enum_add_methods(zend_class_entry *ce)
{
    /* these replace the abstract stubs inherited from UnitEnum / BackedEnum and
     * anything a trait tried to contribute */
    zend_function *f;
    f = enum_concrete_method(ce, "cases");
    if (!f || zend_trait_of_function(f))
        zend_class_add_method(ce, "cases", zim_enum_cases, 0, 0, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC);
    if (!ce->is_backed_enum) return;
    f = enum_concrete_method(ce, "from");
    if (!f || zend_trait_of_function(f))
        zend_class_add_method(ce, "from", zim_enum_from, 1, 1, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC);
    f = enum_concrete_method(ce, "tryfrom");
    if (!f || zend_trait_of_function(f))
        zend_class_add_method(ce, "tryFrom", zim_enum_tryfrom, 1, 1, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC);
}

/* Build the singleton case objects and publish them as class constants. */
static void enum_bind_cases(zend_class_entry *ce, zend_class_decl_extra *extra)
{
    zend_ast *list = extra ? extra->cases : NULL;
    if (!ce->enum_cases) ce->enum_cases = zend_hash_new();
    if (!list) return;

    const char *want = "int";
    if (ce->enum_backing_type && ce->enum_backing_type->n == 1)
        want = ce->enum_backing_type->names[0]->val;
    int want_str = !strcasecmp(want, "string");

    scope_frame f;
    push_scope_frame(&f, ce);
    for (uint32_t i = 0; i < list->nchild; i++) {
        zend_ast *e = list->child[i];
        if (!e) continue;
        zend_string *name = ast_name(e->child[0]);
        if (!name) continue;
        if (zend_hash_find(ce->enum_cases, name)) {
            pop_scope_frame(&f);
            zend_fatal("Cannot redefine class constant %s::%s", ce->name->val, name->val);
            return;
        }
        zval val = zval_null();
        int have_val = (e->nchild > 1 && e->child[1]) ? 1 : 0;
        if (have_val && !ast_is_const_expr(e->child[1])) {
            pop_scope_frame(&f);
            zend_fatal("Enum case value must be compile-time evaluatable");
            return;
        }
        if (have_val && zend_eval_expr(e->child[1], &val)) { pop_scope_frame(&f); return; }

        if (ce->is_backed_enum) {
            if (!have_val) {
                pop_scope_frame(&f);
                zend_fatal("Case %s of backed enum %s must have a value", name->val, ce->name->val);
                return;
            }
            zval *d = zval_deref(&val);
            int is_str = d->u1 == IS_STRING;
            if (want_str != is_str) {
                pop_scope_frame(&f);
                zend_fatal("Enum case type %s does not match enum backing type %s",
                           is_str ? "string" : zend_zval_type_name(d), want);
                return;
            }
            zval *dup = enum_case_by_value(ce, d);
            if (dup) {
                zval *pn = zend_hash_find(Z_OBJ_P(dup)->properties, S("name"));
                pop_scope_frame(&f);
                zend_fatal("Duplicate value in enum %s for cases %s and %s", ce->name->val,
                           pn ? Z_STR_P(zval_deref(pn))->val : "?", name->val);
                return;
            }
        } else if (have_val) {
            pop_scope_frame(&f);
            zend_fatal("Case %s of non-backed enum %s must not have a value, try adding \": %s\" to the enum declaration",
                       name->val, ce->name->val,
                       zval_deref(&val)->u1 == IS_STRING ? "string" : "int");
            return;
        }

        zend_object *obj = ecalloc(1, sizeof(zend_object));
        obj->handle = ++EG(next_obj_handle);
        obj->refcount = 1;
        obj->ce = ce;
        obj->properties = zend_hash_new();
        zend_hash_update(obj->properties, S("name"), zval_zstr(name));
        if (ce->is_backed_enum) zend_hash_update(obj->properties, S("value"), zval_copy(val));
        zval zo = zval_object(obj);
        zend_hash_update(ce->enum_cases, name, zo);

        zend_class_constant *cc = ecalloc(1, sizeof(zend_class_constant));
        cc->value = zval_copy(zo);
        cc->flags = ZEND_ACC_PUBLIC;
        cc->ce = ce;
        cc->evaluated = 1;
        zend_hash_update(ce->constants_table, name, ZEND_PTR_ZVAL(cc));
    }
    pop_scope_frame(&f);
}

/* A case object never changes: php-src reports its properties as readonly. */
static int enum_reject_write(zend_object *obj, zend_string *name, int unsetting)
{
    if (EG(php_version) < 80100 || !obj->ce || !obj->ce->is_enum) return 0;
    if (zend_hash_find(obj->properties, name))
        zend_throw_error("Error", "Cannot %s readonly property %s::$%s",
                         unsetting ? "unset" : "modify", obj->ce->name->val, name->val);
    else if (unsetting)
        zend_throw_error("Error", "Cannot unset readonly property %s::$%s",
                         obj->ce->name->val, name->val);
    else
        zend_throw_error("Error", "Cannot create dynamic property %s::$%s",
                         obj->ce->name->val, name->val);
    return 1;
}
/* VERSIONGATE:80100 END */

zend_class_entry *zend_declare_class(zend_class_entry *decl)
{
    if (!decl) return NULL;
    if (!decl->lcname && decl->name) decl->lcname = zend_string_tolower(decl->name);
    zval *have = zend_hash_find(EG(class_table), decl->lcname);
    if (have) {
        zend_class_entry *old = (zend_class_entry *)ZEND_ZVAL_PTR_P(have);
        if (old == decl) return decl;
        zend_fatal("Cannot redeclare class %s", decl->name->val);
        return NULL;
    }

    zend_class_decl_extra *extra = zend_class_decl_get_extra(decl);

    if (!decl->function_table)     decl->function_table = zend_hash_new();
    if (!decl->constants_table)    decl->constants_table = zend_hash_new();
    if (!decl->properties_info)    decl->properties_info = zend_hash_new();
    if (!decl->default_properties) decl->default_properties = zend_hash_new();
    if (!decl->static_members)     decl->static_members = zend_hash_new();

    if ((decl->flags & ZEND_ACC_TRAIT) && zend_hash_num_elements(decl->constants_table)) {
        zend_fatal("Traits cannot have constants");
        return NULL;
    }

    if (extra && extra->parent_name) {
        zend_class_entry *p = zend_lookup_class(extra->parent_name, 1);
        if (!p) {
            /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
            if (EG(php_version) >= 70000)
                zend_throw_error("Error", "Class '%s' not found", extra->parent_name->val);
            else
                zend_fatal("Class '%s' not found", extra->parent_name->val);
            return NULL;
        }
        if (decl->flags & ZEND_ACC_INTERFACE) {
            if (!(p->flags & ZEND_ACC_INTERFACE)) {
                zend_fatal("%s cannot implement %s - it is not an interface",
                           decl->name->val, p->name->val);
                return NULL;
            }
        } else {
            if (p->flags & ZEND_ACC_FINAL) {
                /* VERSIONGATE:80000 else-branch is the 5.6 wording */
                if (EG(php_version) >= 80000)
                    zend_fatal("Class %s cannot extend final class %s",
                               decl->name->val, p->name->val);
                else
                    zend_fatal("Class %s may not inherit from final class (%s)",
                               decl->name->val, p->name->val);
                return NULL;
            }
            if (p->flags & ZEND_ACC_INTERFACE) {
                zend_fatal("Class %s cannot extend from interface %s", decl->name->val, p->name->val);
                return NULL;
            }
            if (p->flags & ZEND_ACC_TRAIT) {
                zend_fatal("Class %s cannot extend from trait %s", decl->name->val, p->name->val);
                return NULL;
            }
        }
        decl->parent = p;
    }

    ZEND_HASH_FOREACH(decl->function_table, b) {
        zend_function *fn = (zend_function *)ZEND_ZVAL_PTR(b->val);
        if (!fn) continue;
        if (!fn->scope) fn->scope = decl;
        if (!fn->static_vars) fn->static_vars = zend_hash_new();
        ce_record_magic(decl, fn);
        check_magic_method(decl, fn);
        check_magic_signature(decl, fn);
        if (EG(exiting)) return NULL;
    } ZEND_HASH_FOREACH_END();

    uint32_t iface_cap = (extra ? extra->num_ifaces : 0) + 2;
    if ((extra && extra->num_ifaces) || decl->is_enum) {
        decl->interfaces = ecalloc(iface_cap, sizeof(zend_class_entry *));
        decl->num_interfaces = 0;
        /* VERSIONGATE:80100 BEGIN - an enum implicitly implements UnitEnum */
        if (decl->is_enum) decl->is_backed_enum = decl->enum_backing_type != NULL;
        if (decl->is_enum && zend_ce_unit_enum) {
            decl->interfaces[decl->num_interfaces++] = zend_ce_unit_enum;
            if (decl->is_backed_enum && zend_ce_backed_enum)
                decl->interfaces[decl->num_interfaces++] = zend_ce_backed_enum;
        }
        /* VERSIONGATE:80100 END */
        for (uint32_t i = 0; extra && i < extra->num_ifaces; i++) {
            zend_class_entry *ie = zend_lookup_class(extra->iface_names[i], 1);
            if (!ie) {
                /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
                if (EG(php_version) >= 70000)
                    zend_throw_error("Error", "Interface '%s' not found", extra->iface_names[i]->val);
                else
                    zend_fatal("Interface '%s' not found", extra->iface_names[i]->val);
                return NULL;
            }
            if (decl->flags & ZEND_ACC_TRAIT) {
                zend_fatal("Cannot use '%s' as interface on '%s' since it is a Trait",
                           ie->name->val, decl->name->val);
                return NULL;
            }
            if (!(ie->flags & ZEND_ACC_INTERFACE)) {
                zend_fatal("%s cannot implement %s - it is not an interface",
                           decl->name->val, ie->name->val);
                return NULL;
            }
            /* VERSIONGATE:80100 BEGIN - Serializable is deprecated in 8.1 */
            if (EG(php_version) >= 80100 && zend_string_equals_ci(ie->name, S("Serializable")))
                zend_error(E_DEPRECATED,
                           "%s implements the Serializable interface, which is deprecated. Implement __serialize() and __unserialize() instead (or in addition, if support for old PHP versions is necessary)",
                           decl->name->val);
            /* VERSIONGATE:80100 END */
            /* VERSIONGATE:80100 BEGIN - who may implement the enum interfaces */
            if (!decl->is_enum &&
                ((zend_ce_unit_enum && ie == zend_ce_unit_enum) ||
                 (zend_ce_backed_enum && ie == zend_ce_backed_enum))) {
                zend_fatal("Non-enum class %s cannot implement interface %s",
                           decl->name->val, ie->name->val);
                return NULL;
            }
            if (decl->is_enum && !decl->is_backed_enum &&
                zend_ce_backed_enum && ie == zend_ce_backed_enum) {
                zend_fatal("Non-backed enum %s cannot implement interface BackedEnum",
                           decl->name->val);
                return NULL;
            }
            if (decl->is_enum && zend_string_equals_ci(ie->name, S("Serializable"))) {
                zend_fatal("Enums may not implement the Serializable interface");
                return NULL;
            }
            /* VERSIONGATE:80100 END */
            for (uint32_t j = 0; j < decl->num_interfaces; j++) {
                if (decl->interfaces[j] == ie) {
                    zend_fatal("Class %s cannot implement previously implemented interface %s",
                               decl->name->val, ie->name->val);
                    return NULL;
                }
            }
            if (zend_ce_iterator && zend_ce_aggregate) {
                zend_class_entry *other = (ie == zend_ce_iterator) ? zend_ce_aggregate
                                        : (ie == zend_ce_aggregate) ? zend_ce_iterator : NULL;
                if (other) {
                    for (uint32_t j = 0; j < decl->num_interfaces; j++) {
                        if (zend_instanceof(decl->interfaces[j], other)) {
                            zend_fatal("Class %s cannot implement both %s and %s at the same time",
                                       decl->name->val, ie->name->val, other->name->val);
                            return NULL;
                        }
                    }
                }
            }
            inherit_interface_constants(decl, ie);
            if (EG(exiting)) return NULL;
            decl->interfaces[decl->num_interfaces++] = ie;
        }
    }

    inherit_from(decl, decl->parent);
    if (EG(exiting)) return NULL;
    for (uint32_t i = 0; i < decl->num_interfaces; i++) {
        inherit_from(decl, decl->interfaces[i]);
        if (EG(exiting)) return NULL;
    }

    apply_traits(decl, extra);
    if (EG(exiting) || EG(exception)) return NULL;

    /* VERSIONGATE:80100 BEGIN - enum member rules and the built-in methods */
    if (decl->is_enum) {
        /* the parser flags is_backed_enum for a case that carries a value too;
         * only the ": int" / ": string" clause really makes an enum backed */
        decl->is_backed_enum = decl->enum_backing_type != NULL;
        enum_validate(decl);
        if (EG(exiting) || EG(exception)) return NULL;
        enum_add_methods(decl);
    }
    /* VERSIONGATE:80100 END */

    ZEND_HASH_FOREACH(decl->properties_info, b) {
        zend_property_info *pi = (zend_property_info *)ZEND_ZVAL_PTR(b->val);
        if (!pi) continue;
        if (!pi->ce) pi->ce = decl;
        if (!pi->mangled) pi->mangled = zend_mangle_property_name(pi->ce, pi->name, pi->flags);
    } ZEND_HASH_FOREACH_END();

    if (decl->parent) {
        HashTable *merged = zend_hash_new();
        if (bind_class_constants(decl->parent)) return NULL;
        ZEND_HASH_FOREACH(decl->parent->default_properties, b) {
            zend_hash_update(merged, b->key, zval_copy(b->val));
        } ZEND_HASH_FOREACH_END();
        ZEND_HASH_FOREACH(decl->default_properties, b) {
            zend_hash_update(merged, b->key, zval_copy(b->val));
        } ZEND_HASH_FOREACH_END();
        decl->default_properties = merged;
    }

    ZEND_HASH_FOREACH(decl->constants_table, b) {
        zend_class_constant *cc = (zend_class_constant *)ZEND_ZVAL_PTR(b->val);
        if (cc && !cc->ce) cc->ce = decl;
    } ZEND_HASH_FOREACH_END();

    verify_abstract_methods(decl);
    if (EG(exception) || EG(exiting)) return NULL;

    zend_register_class(decl);
    /* VERSIONGATE:80100 BEGIN - the case singletons need the class registered */
    if (decl->is_enum) {
        enum_bind_cases(decl, extra);
        if (EG(exiting) || EG(exception)) return NULL;
    }
    /* VERSIONGATE:80100 END */
    if (bind_class_constants(decl)) return NULL;
    return decl;
}

/* ======================================================================== */
/*  yield                                                                   */
/* ======================================================================== */

static zend_generator *current_generator(void)
{
    return EG(current) ? (zend_generator *)EG(current)->generator : NULL;
}

static int eval_yield(zend_ast *ast, zval *result)
{
    zend_generator *g = current_generator();
    if (!g) {
        zend_fatal("The \"yield\" expression can only be used inside a function");
        return 1;
    }
    zval val = zval_null(), key = zval_null();
    int have_key = 0;
    zend_function *gfn = EG(current) ? EG(current)->func : NULL;
    if (ast->nchild > 0 && ast->child[0]) {
        if (gfn && fn_returns_ref(gfn)) {
            /* "function &gen()" yields references, so a plain value is only a
             * notice and the caller still sees the value */
            if (is_var_ast(ast->child[0])) {
                int err = 0;
                zval *slot = fetch_lval_ex(ast->child[0], FETCH_W, &err);
                if (!slot) return 1;
                zend_reference *r = zend_make_reference(slot);
                r->refcount++;
                val.u1 = IS_REFERENCE;
                val.value.ref = r;
            } else {
                zend_error(E_NOTICE, "Only variable references should be yielded by reference");
                if (zend_eval_expr(ast->child[0], &val)) return 1;
            }
        } else if (zend_eval_expr(ast->child[0], &val)) return 1;
    }
    if (ast->nchild > 1 && ast->child[1]) {
        if (zend_eval_expr(ast->child[1], &key)) return 1;
        have_key = 1;
    }
    return zend_generator_do_yield(g, key, have_key, val, result);
}

/* VERSIONGATE:70000 "yield from" is 7.0; 5.6 rejects it at compile time */
static int eval_yield_from(zend_ast *ast, zval *result)
{
    zend_generator *g = current_generator();
    if (!g) {
        zend_fatal("The \"yield\" expression can only be used inside a function");
        return 1;
    }
    zval src = zval_null();
    if (zend_eval_expr(ast->child[0], &src)) return 1;
    return zend_generator_do_yield_from(g, src, result);
}

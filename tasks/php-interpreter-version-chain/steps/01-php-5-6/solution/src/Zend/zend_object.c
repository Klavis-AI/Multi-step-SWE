/* zend_object.c — classes, objects, property access and magic methods. */
#include "zend.h"
#include "zend_ast.h"
#include "zend_API.h"
#include "zend_alloc.h"
#include "zend_parse.h"
#include "zend_object.h"
#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------ class table */
zend_class_entry *zend_lookup_class(const zend_string *name, int autoload)
{
    zend_string *lc = zend_string_tolower((zend_string *)name);
    const char *p = lc->val;
    size_t n = lc->len;
    if (n && p[0] == '\\') { lc = zend_string_init(p + 1, n - 1); }   /* \Foo == Foo */
    zval *v = zend_hash_find(EG(class_table), lc);
    if (v) return (zend_class_entry *)(intptr_t)v->value.lval;
    if (autoload && EG(autoloaders) && zend_hash_num_elements(EG(autoloaders))) {
        zend_string *orig = zend_string_init(name->val, name->len);
        if (orig->len && orig->val[0] == '\\') orig = zend_string_init(name->val + 1, name->len - 1);
        ZEND_HASH_FOREACH(EG(autoloaders), b) {
            zval arg = zval_zstr(orig), ret = zval_null();
            zval cb = b->val;
            zend_call_user_callable(&cb, 1, &arg, &ret);
            if (EG(exception)) return NULL;
            v = zend_hash_find(EG(class_table), lc);
            if (v) return (zend_class_entry *)(intptr_t)v->value.lval;
        } ZEND_HASH_FOREACH_END();
    }
    return NULL;
}

void zend_register_class(zend_class_entry *ce)
{
    zval v; v.u1 = IS_LONG; v.value.lval = (zend_long)(intptr_t)ce;
    zend_hash_update(EG(class_table), ce->lcname, v);
}

int zend_instanceof(zend_class_entry *ce, zend_class_entry *target)
{
    if (!ce || !target) return 0;
    for (zend_class_entry *c = ce; c; c = c->parent) {
        if (c == target) return 1;
        for (uint32_t i = 0; i < c->num_interfaces; i++) {
            if (c->interfaces[i] == target) return 1;
            if (zend_instanceof(c->interfaces[i], target)) return 1;
        }
    }
    return 0;
}

zend_function *zend_lookup_method(zend_class_entry *ce, const zend_string *lcname)
{
    for (zend_class_entry *c = ce; c; c = c->parent) {
        zval *v = zend_hash_find(c->function_table, lcname);
        if (v) return (zend_function *)(intptr_t)v->value.lval;
    }
    return NULL;
}

/* --------------------------------------------------------- property names */
zend_string *zend_mangle_property_name(zend_class_entry *ce, zend_string *name, uint32_t flags)
{
    if (flags & ZEND_ACC_PRIVATE) {
        size_t n = 1 + ce->name->len + 1 + name->len;
        zend_string *s = zend_string_alloc(n);
        s->val[0] = '\0';
        memcpy(s->val + 1, ce->name->val, ce->name->len);
        s->val[1 + ce->name->len] = '\0';
        memcpy(s->val + 2 + ce->name->len, name->val, name->len);
        s->val[n] = '\0';
        return s;
    }
    if (flags & ZEND_ACC_PROTECTED) {
        size_t n = 3 + name->len;
        zend_string *s = zend_string_alloc(n);
        s->val[0] = '\0'; s->val[1] = '*'; s->val[2] = '\0';
        memcpy(s->val + 3, name->val, name->len);
        s->val[n] = '\0';
        return s;
    }
    return name;
}

zend_string *zend_unmangle_property_name(zend_string *mangled, zend_class_entry **ce_out)
{
    if (ce_out) *ce_out = NULL;
    if (mangled->len == 0 || mangled->val[0] != '\0') return mangled;
    const char *p = mangled->val + 1;
    const char *end = mangled->val + mangled->len;
    const char *q = memchr(p, '\0', (size_t)(end - p));
    if (!q) return mangled;
    if (ce_out && !(q - p == 1 && p[0] == '*')) {
        zend_string *cls = zend_string_init(p, (size_t)(q - p));
        *ce_out = zend_lookup_class(cls, 0);
    }
    return zend_string_init(q + 1, (size_t)(end - (q + 1)));
}

zend_property_info *zend_find_property_info(zend_class_entry *ce, zend_string *name)
{
    for (zend_class_entry *c = ce; c; c = c->parent) {
        zval *v = zend_hash_find(c->properties_info, name);
        if (v) return (zend_property_info *)(intptr_t)v->value.lval;
    }
    return NULL;
}

/* Which class is "the current scope" for visibility checks. */
static zend_class_entry *current_scope(void)
{
    zend_execute_data *ex = EG(current);
    while (ex) {
        if (ex->func && ex->func->scope) return ex->func->scope;
        if (ex->This) return ex->This->ce;
        ex = ex->prev;
    }
    return NULL;
}

int zend_check_property_access(zend_property_info *info, zend_class_entry *scope)
{
    if (!info) return 1;
    if (info->flags & ZEND_ACC_PUBLIC) return 1;
    if (info->flags & ZEND_ACC_PROTECTED)
        return scope && (zend_instanceof(scope, info->ce) || zend_instanceof(info->ce, scope));
    return scope == info->ce;
}

/* ---------------------------------------------------------------- objects */
zend_object *zend_object_new(zend_class_entry *ce)
{
    zend_object *o = ecalloc(1, sizeof(zend_object));
    o->handle = ++EG(next_obj_handle);
    o->refcount = 1;
    o->ce = ce;
    o->properties = zend_hash_new();
    if (ce) {
        zend_class_init_statics(ce);
        for (zend_class_entry *chain[64], *c = ce; c; c = c->parent) { (void)chain; break; }
        /* defaults are already flattened into ce->default_properties */
        if (ce->default_properties) {
            ZEND_HASH_FOREACH(ce->default_properties, b) {
                zend_hash_update(o->properties, b->key, zval_clone_deep(b->val));
            } ZEND_HASH_FOREACH_END();
        }
    }
    return o;
}

int zend_object_construct(zend_object *obj, uint32_t argc, zval *argv, zend_string **names)
{
    zend_function *ctor = obj->ce ? obj->ce->constructor : NULL;
    if (!ctor) return 0;
    if (!zend_check_method_access(ctor, current_scope())) {
        zend_throw_error("Error", "Call to %s %s::__construct() from %s%s",
                         (ctor->flags & ZEND_ACC_PRIVATE) ? "private" : "protected",
                         obj->ce->name->val,
                         current_scope() ? "scope " : "global scope",
                         current_scope() ? current_scope()->name->val : "");
        return 1;
    }
    zval ret = zval_null();
    return zend_call_function_ex(ctor, obj, obj->ce, argc, argv, names, &ret, NULL);
}

int zend_check_method_access(zend_function *fn, zend_class_entry *scope)
{
    if (!fn) return 1;
    if (fn->flags & ZEND_ACC_PUBLIC) return 1;
    if (fn->flags & ZEND_ACC_PROTECTED)
        return scope && (zend_instanceof(scope, fn->scope) || zend_instanceof(fn->scope, scope));
    if (fn->flags & ZEND_ACC_PRIVATE) return scope == fn->scope;
    return 1;
}

/* Find the storage slot for a property, honouring visibility. */
zval *zend_object_get_prop_slot(zend_object *obj, zend_string *name, int *inaccessible)
{
    zend_class_entry *scope = current_scope();
    zend_property_info *info = zend_find_property_info(obj->ce, name);
    if (inaccessible) *inaccessible = 0;
    if (info && !(info->flags & ZEND_ACC_STATIC)) {
        if (!zend_check_property_access(info, scope)) {
            /* a private property of a subclass may still be visible when the
             * scope declares its own private with the same name */
            if (scope) {
                zval *pv = zend_hash_find(scope->properties_info, name);
                if (pv) {
                    zend_property_info *si = (zend_property_info *)(intptr_t)pv->value.lval;
                    if ((si->flags & ZEND_ACC_PRIVATE) && si->ce == scope)
                        return zend_hash_find(obj->properties, si->mangled);
                }
            }
            if (inaccessible) *inaccessible = 1;
            return NULL;
        }
        return zend_hash_find(obj->properties, info->mangled);
    }
    /* dynamic property, or a private property declared in the current scope */
    if (scope) {
        zval *pv = zend_hash_find(scope->properties_info, name);
        if (pv) {
            zend_property_info *si = (zend_property_info *)(intptr_t)pv->value.lval;
            if (si->flags & ZEND_ACC_PRIVATE)
                return zend_hash_find(obj->properties, si->mangled);
        }
    }
    return zend_hash_find(obj->properties, name);
}

zval *zend_read_property(zend_object *obj, zend_string *name, int silent, zval *tmp)
{
    int inaccessible = 0;
    zval *slot = zend_object_get_prop_slot(obj, name, &inaccessible);
    if (slot && Z_TYPE_P(slot) != IS_UNDEF) return slot;

    if (obj->ce && obj->ce->__get) {
        zval arg = zval_zstr(name), ret = zval_null();
        if (zend_call_function_ex(obj->ce->__get, obj, obj->ce, 1, &arg, NULL, &ret, NULL))
            return NULL;
        if (tmp) { *tmp = ret; return tmp; }
        return NULL;
    }
    if (inaccessible) {
        zend_throw_error("Error", "Cannot access %s property %s::$%s",
                         "private", obj->ce->name->val, name->val);
        return NULL;
    }
    if (!silent) {
zend_error(E_NOTICE, "Undefined property: %s::$%s", obj->ce->name->val, name->val);

    }
    return NULL;
}

int zend_write_property(zend_object *obj, zend_string *name, zval value)
{
    int inaccessible = 0;
    zval *slot = zend_object_get_prop_slot(obj, name, &inaccessible);
    if (slot) {
        if (Z_TYPE_P(slot) == IS_REFERENCE) { Z_REF_P(slot)->val = value; return 0; }
        *slot = value;
        return 0;
    }
    if (obj->ce && obj->ce->__set) {
        zval args[2]; args[0] = zval_zstr(name); args[1] = value;
        zval ret = zval_null();
        return zend_call_function_ex(obj->ce->__set, obj, obj->ce, 2, args, NULL, &ret, NULL);
    }
    if (inaccessible) {
        zend_throw_error("Error", "Cannot access private property %s::$%s",
                         obj->ce->name->val, name->val);
        return 1;
    }
    zend_property_info *info = zend_find_property_info(obj->ce, name);
    zend_string *key = info && !(info->flags & ZEND_ACC_STATIC) ? info->mangled : name;
    zend_hash_update(obj->properties, key, value);
    return 0;
}

int zend_object_to_string(zend_object *obj, zend_string **out)
{
    if (!obj->ce || !obj->ce->__tostring) {

        zend_fatal("Object of class %s could not be converted to string",
                   obj->ce ? obj->ce->name->val : "stdClass");
        return 1;
    }
    zval ret = zval_null();
    if (zend_call_function_ex(obj->ce->__tostring, obj, obj->ce, 0, NULL, NULL, &ret, NULL))
        return 1;
    zval *r = zval_deref(&ret);
    if (Z_TYPE_P(r) != IS_STRING) {
        zend_throw_error("Error", "Method %s::__toString() must return a string value",
                         obj->ce->name->val);
        return 1;
    }
    *out = Z_STR_P(r);
    return 0;
}

/* ---------------------------------------------------------------- statics */
void zend_class_init_statics(zend_class_entry *ce)
{
    if (!ce || ce->statics_initialized) return;
    ce->statics_initialized = 1;
    if (ce->parent) zend_class_init_statics(ce->parent);
}

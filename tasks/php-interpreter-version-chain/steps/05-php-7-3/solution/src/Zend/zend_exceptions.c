/* zend_exceptions.c -- the built-in class hierarchy.
 *
 * Every engine class is a real zend_class_entry with real internal methods,
 * built here at start-up.  Two things make the shape of this file:
 *
 *  - The hierarchy grew one release at a time.  Throwable and the whole Error
 *    branch arrived in 7.0, ArgumentCountError in 7.1, JsonException in 7.3,
 *    WeakReference in 7.4, ValueError / UnhandledMatchError / WeakMap /
 *    Stringable in 8.0, the enum interfaces and Fiber in 8.1.  Each of those
 *    is built by its own function and registered behind a runtime test on
 *    EG(php_version), both marked VERSIONGATE:<id> so a build that only wants
 *    an older release can drop the pair.
 *
 *  - Exception's text formats are load-bearing.  __toString() and the
 *    uncaught-exception report are compared byte for byte by the .phpt suite,
 *    so they follow php-src's zend_exceptions.c literally, including the
 *    5.6 -> 7.0 wording change and the way a `previous` chain is spelled out
 *    innermost-first with "\n\nNext " between links.
 *
 * Properties are read and written through the mangled storage key directly
 * rather than through zend_read_property(), because an internal method has no
 * reliable scope of its own and `string`, `trace` and `previous` are private.
 */

#include "zend.h"
#include "zend_ast.h"
#include "zend_API.h"
#include "zend_alloc.h"
#include "zend_object.h"
#include "zend_exceptions.h"
#include "php_main.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ==========================================================================
 *  Published class entries
 * ========================================================================== */

zend_class_entry *zend_ce_traversable;
zend_class_entry *zend_ce_iterator;
zend_class_entry *zend_ce_aggregate;
zend_class_entry *zend_ce_arrayaccess;
zend_class_entry *zend_ce_countable;
zend_class_entry *zend_ce_serializable;
zend_class_entry *zend_ce_jsonserializable;
zend_class_entry *zend_ce_throwable;
zend_class_entry *zend_ce_stringable;
zend_class_entry *zend_ce_unit_enum;
zend_class_entry *zend_ce_backed_enum;

zend_class_entry *zend_ce_exception;
zend_class_entry *zend_ce_error_exception;
zend_class_entry *zend_ce_error;
zend_class_entry *zend_ce_type_error;
zend_class_entry *zend_ce_value_error;
zend_class_entry *zend_ce_argument_count_error;
zend_class_entry *zend_ce_arithmetic_error;
zend_class_entry *zend_ce_division_by_zero_error;
zend_class_entry *zend_ce_assertion_error;
zend_class_entry *zend_ce_unhandled_match_error;
zend_class_entry *zend_ce_json_exception;
zend_class_entry *zend_ce_runtime_exception;
zend_class_entry *zend_ce_logic_exception;

zend_class_entry *zend_standard_class_def;
zend_class_entry *zend_ce_closure;
zend_class_entry *zend_ce_generator;
zend_class_entry *zend_ce_weakref;
zend_class_entry *zend_ce_weakmap;

/* ==========================================================================
 *  Small helpers
 * ========================================================================== */

#define ACC_PUB      ZEND_ACC_PUBLIC
#define ACC_PROT     ZEND_ACC_PROTECTED
#define ACC_PRIV     ZEND_ACC_PRIVATE
#define ACC_FINAL_PUB (ZEND_ACC_PUBLIC | ZEND_ACC_FINAL)
#define ACC_ABSTRACT_PUB (ZEND_ACC_PUBLIC | ZEND_ACC_ABSTRACT)
#define ACC_STATIC_PUB (ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)

static zend_string *S(const char *s)
{
    return zend_string_initz(s);
}

static zval ptr_zval(void *p)
{
    zval v;

    v.u1 = IS_LONG;
    v.value.lval = (zend_long) (intptr_t) p;
    return v;
}

zend_object *zend_get_this(void)
{
    zend_execute_data *ex = EG(current);

    return (ex != NULL) ? ex->This : NULL;
}

/* ==========================================================================
 *  Building an internal class
 * ========================================================================== */

/* php-src flattens the inherited default properties into every class, and
 * Zend/zend_object.c's zend_object_new() relies on that: it copies
 * ce->default_properties without walking the parent chain.  The magic-method
 * pointers are copied down for the same reason -- zend_object_construct()
 * reads obj->ce->constructor directly. */
static void class_inherit(zend_class_entry *ce, zend_class_entry *parent)
{
    ce->parent = parent;
    if (parent == NULL) {
        return;
    }

    if (parent->default_properties != NULL) {
        ZEND_HASH_FOREACH(parent->default_properties, b) {
            zend_hash_update(ce->default_properties, b->key, zval_copy(b->val));
        } ZEND_HASH_FOREACH_END();
    }

    ce->constructor  = parent->constructor;
    ce->destructor   = parent->destructor;
    ce->clone        = parent->clone;
    ce->__get        = parent->__get;
    ce->__set        = parent->__set;
    ce->__isset      = parent->__isset;
    ce->__unset      = parent->__unset;
    ce->__call       = parent->__call;
    ce->__callstatic = parent->__callstatic;
    ce->__tostring   = parent->__tostring;
    ce->__invoke     = parent->__invoke;
    ce->__debuginfo  = parent->__debuginfo;
    ce->__set_state  = parent->__set_state;
    ce->__serialize  = parent->__serialize;
}

zend_class_entry *zend_register_internal_class(const char *name,
                                               zend_class_entry *parent,
                                               uint32_t flags)
{
    zend_class_entry *ce = ecalloc(1, sizeof(zend_class_entry));

    ce->name               = S(name);
    ce->lcname             = zend_string_tolower(ce->name);
    ce->flags              = flags;
    ce->function_table     = zend_hash_new();
    ce->constants_table    = zend_hash_new();
    ce->properties_info    = zend_hash_new();
    ce->default_properties = zend_hash_new();
    ce->static_members     = zend_hash_new();
    ce->attributes         = zend_hash_new();
    ce->statics_initialized = 1;
    ce->allow_dynamic_props = 1;
    ce->filename           = NULL;
    ce->lineno             = 0;

    class_inherit(ce, parent);
    zend_register_class(ce);
    return ce;
}

zend_class_entry *zend_register_internal_interface(const char *name)
{
    return zend_register_internal_class(name, NULL, ZEND_ACC_INTERFACE);
}

void zend_class_add_interface(zend_class_entry *ce, zend_class_entry *iface)
{
    zend_class_entry **grown;

    if (ce == NULL || iface == NULL) {
        return;
    }
    grown = ecalloc(ce->num_interfaces + 1, sizeof(zend_class_entry *));
    if (ce->num_interfaces != 0) {
        memcpy(grown, ce->interfaces, ce->num_interfaces * sizeof(zend_class_entry *));
    }
    grown[ce->num_interfaces] = iface;
    ce->interfaces = grown;
    ce->num_interfaces++;
}

zend_function *zend_class_add_method(zend_class_entry *ce, const char *name,
                                     void (*handler)(zval *ret, uint32_t argc, zval *argv),
                                     uint32_t min_args, uint32_t num_args,
                                     uint32_t flags)
{
    zend_function *fn = ecalloc(1, sizeof(zend_function));
    const char *lc;

    fn->name        = S(name);
    fn->lcname      = zend_string_tolower(fn->name);
    fn->scope       = ce;
    fn->handler     = handler;
    /* (uint32_t) -1 is the "takes any number of arguments" sentinel.  Store it
     * as the variadic flag over a real slot count instead: fn->num_args is the
     * bound of every loop over fn->args, so leaving the sentinel in place both
     * asks for a 160GB argument array here and walks off the end of it later. */
    if (num_args == (uint32_t) -1) {
        flags |= ZEND_ACC_VARIADIC;
        num_args = (min_args > 0) ? min_args : 1;   /* the variadic slot itself */
    }

    fn->min_args    = min_args;
    fn->num_args    = num_args;
    fn->flags       = flags;
    fn->is_internal = 1;
    fn->is_abstract = (flags & ZEND_ACC_ABSTRACT) ? 1 : 0;
    fn->args        = (num_args != 0) ? ecalloc(num_args, sizeof(zend_arg_info)) : NULL;
    if (fn->args != NULL && (flags & ZEND_ACC_VARIADIC) != 0) {
        fn->args[num_args - 1].variadic = 1;
    }
    fn->static_vars = zend_hash_new();

    zend_hash_update(ce->function_table, fn->lcname, ptr_zval(fn));

    lc = ZSTR_VAL(fn->lcname);
    if (strcmp(lc, "__construct") == 0) {
        fn->flags |= ZEND_ACC_CTOR;
        ce->constructor = fn;
    } else if (strcmp(lc, "__destruct") == 0) {
        ce->destructor = fn;
    } else if (strcmp(lc, "__clone") == 0) {
        ce->clone = fn;
    } else if (strcmp(lc, "__tostring") == 0) {
        ce->__tostring = fn;
    } else if (strcmp(lc, "__get") == 0) {
        ce->__get = fn;
    } else if (strcmp(lc, "__set") == 0) {
        ce->__set = fn;
    } else if (strcmp(lc, "__isset") == 0) {
        ce->__isset = fn;
    } else if (strcmp(lc, "__unset") == 0) {
        ce->__unset = fn;
    } else if (strcmp(lc, "__call") == 0) {
        ce->__call = fn;
    } else if (strcmp(lc, "__callstatic") == 0) {
        ce->__callstatic = fn;
    } else if (strcmp(lc, "__invoke") == 0) {
        ce->__invoke = fn;
    } else if (strcmp(lc, "__debuginfo") == 0) {
        ce->__debuginfo = fn;
    } else if (strcmp(lc, "__set_state") == 0) {
        ce->__set_state = fn;
    } else if (strcmp(lc, "__serialize") == 0) {
        ce->__serialize = fn;
    }
    return fn;
}

void zend_function_set_arg_names(zend_function *fn, const char *names)
{
    const char *p = names;
    uint32_t i = 0;

    if (fn == NULL || fn->args == NULL || names == NULL) {
        return;
    }
    while (*p != '\0' && i < fn->num_args) {
        const char *comma = strchr(p, ',');
        size_t len = (comma != NULL) ? (size_t) (comma - p) : strlen(p);

        fn->args[i].name = zend_string_init(p, len);
        i++;
        if (comma == NULL) {
            break;
        }
        p = comma + 1;
    }
}

void zend_class_add_property(zend_class_entry *ce, const char *name,
                             uint32_t flags, zval default_value)
{
    zend_property_info *info = ecalloc(1, sizeof(zend_property_info));

    info->name    = S(name);
    info->flags   = flags;
    info->ce      = ce;
    info->mangled = zend_mangle_property_name(ce, info->name, flags);

    zend_hash_update(ce->properties_info, info->name, ptr_zval(info));
    if (!(flags & ZEND_ACC_STATIC)) {
        zend_hash_update(ce->default_properties, info->mangled, default_value);
    } else {
        zend_hash_update(ce->static_members, info->mangled, default_value);
    }
}

void zend_class_add_constant(zend_class_entry *ce, const char *name,
                             zval value, uint32_t flags)
{
    zend_class_constant *c = ecalloc(1, sizeof(zend_class_constant));

    c->value     = value;
    c->flags     = flags;
    c->ce        = ce;
    c->evaluated = 1;
    zend_hash_update(ce->constants_table, S(name), ptr_zval(c));
}

/* ==========================================================================
 *  Exception property access
 * ==========================================================================
 * `message`, `code`, `file` and `line` are protected and `string`, `trace`
 * and `previous` are private -- private to Exception in the Exception branch
 * and private to Error in the Error branch, which is why the storage key has
 * to come from the declaring class rather than be spelled out here.
 */

static zval *ex_slot(zend_object *obj, const char *name)
{
    zend_string *n;
    zend_property_info *info;

    if (obj == NULL || obj->properties == NULL) {
        return NULL;
    }
    n = S(name);
    info = zend_find_property_info(obj->ce, n);
    return zend_hash_find(obj->properties, info != NULL ? info->mangled : n);
}

static void ex_set(zend_object *obj, const char *name, zval value)
{
    zend_string *n;
    zend_property_info *info;

    if (obj == NULL || obj->properties == NULL) {
        return;
    }
    n = S(name);
    info = zend_find_property_info(obj->ce, n);
    zend_hash_update(obj->properties, info != NULL ? info->mangled : n, value);
}

zend_string *zend_exception_get_message(zend_object *ex)
{
    zval *v = ex_slot(ex, "message");

    return (v != NULL) ? zval_get_string(v) : zend_string_init("", 0);
}

zend_string *zend_exception_get_file(zend_object *ex)
{
    zval *v = ex_slot(ex, "file");

    return (v != NULL) ? zval_get_string(v) : zend_string_init("", 0);
}

zend_long zend_exception_get_line(zend_object *ex)
{
    zval *v = ex_slot(ex, "line");

    return (v != NULL) ? zval_get_long(v) : 0;
}

zend_long zend_exception_get_code(zend_object *ex)
{
    zval *v = ex_slot(ex, "code");

    return (v != NULL) ? zval_get_long(v) : 0;
}

zend_object *zend_exception_get_previous(zend_object *ex)
{
    zval *v = ex_slot(ex, "previous");

    if (v == NULL) {
        return NULL;
    }
    v = zval_deref(v);
    return (Z_TYPE_P(v) == IS_OBJECT) ? Z_OBJ_P(v) : NULL;
}

/* ==========================================================================
 *  A growable byte buffer
 * ========================================================================== */

typedef struct {
    char  *s;
    size_t len;
    size_t cap;
} sbuf;

static void sbuf_need(sbuf *b, size_t extra)
{
    if (b->len + extra + 1 > b->cap) {
        size_t want = (b->cap != 0) ? b->cap : 128;

        while (b->len + extra + 1 > want) {
            want *= 2;
        }
        b->s = (b->s != NULL) ? erealloc(b->s, want) : emalloc(want);
        b->cap = want;
    }
}

static void sbuf_addl(sbuf *b, const char *s, size_t l)
{
    if (l == 0) {
        return;
    }
    sbuf_need(b, l);
    memcpy(b->s + b->len, s, l);
    b->len += l;
    b->s[b->len] = '\0';
}

static void sbuf_adds(sbuf *b, const char *s)
{
    if (s != NULL) {
        sbuf_addl(b, s, strlen(s));
    }
}

static void sbuf_addc(sbuf *b, char c)
{
    sbuf_addl(b, &c, 1);
}

static void sbuf_addlong(sbuf *b, zend_long v)
{
    char tmp[32];

    snprintf(tmp, sizeof(tmp), "%lld", (long long) v);
    sbuf_adds(b, tmp);
}

static zend_string *sbuf_to_string(sbuf *b)
{
    zend_string *r = zend_string_init(b->s != NULL ? b->s : "", b->len);

    return r;
}

/* ==========================================================================
 *  Trace formatting
 * ==========================================================================
 * php-src's Zend/zend_smart_str.c smart_str_append_escaped(): anything below
 * a space, a backslash, or anything above '~' is escaped; the six control
 * characters with a letter spelling get it, the rest become \xHH with
 * upper-case hex digits.  A single quote is deliberately left alone, which is
 * why a trace line can contain an unbalanced one.
 */

static void trace_append_escaped(sbuf *b, const char *s, size_t l)
{
    size_t i;

    for (i = 0; i < l; i++) {
        unsigned char c = (unsigned char) s[i];

        if (c < 32 || c == '\\' || c > 126) {
            sbuf_addc(b, '\\');
            switch (c) {
                case '\n': sbuf_addc(b, 'n'); break;
                case '\r': sbuf_addc(b, 'r'); break;
                case '\t': sbuf_addc(b, 't'); break;
                case '\f': sbuf_addc(b, 'f'); break;
                case '\v': sbuf_addc(b, 'v'); break;
                case '\\': sbuf_addc(b, '\\'); break;
                case 27:   sbuf_addc(b, 'e'); break;
                default: {
                    static const char hex[] = "0123456789ABCDEF";

                    sbuf_addc(b, 'x');
                    sbuf_addc(b, hex[(c >> 4) & 0xf]);
                    sbuf_addc(b, hex[c & 0xf]);
                    break;
                }
            }
        } else {
            sbuf_addc(b, (char) c);
        }
    }
}

static size_t trace_string_param_max_len(void)
{

    return 15;
}

static void trace_append_arg(sbuf *b, zval *arg)
{
    zval *a = zval_deref(arg);

    switch (Z_TYPE_P(a)) {
        case IS_UNDEF:
        case IS_NULL:
            sbuf_adds(b, "NULL, ");
            break;
        case IS_FALSE:
            sbuf_adds(b, "false, ");
            break;
        case IS_TRUE:
            sbuf_adds(b, "true, ");
            break;
        case IS_LONG:
        case IS_DOUBLE: {
            /* php-src prints a double through the same path as a string cast,
             * i.e. at the `precision` INI; zval_get_string() is that path. */
            zend_string *s = zval_get_string(a);

            sbuf_addl(b, ZSTR_VAL(s), ZSTR_LEN(s));
            sbuf_adds(b, ", ");
            break;
        }
        case IS_STRING: {
            size_t max = trace_string_param_max_len();
            zend_string *s = Z_STR_P(a);
            size_t take = (ZSTR_LEN(s) < max) ? ZSTR_LEN(s) : max;

            sbuf_addc(b, '\'');
            trace_append_escaped(b, ZSTR_VAL(s), take);
            if (ZSTR_LEN(s) > max) {
                sbuf_adds(b, "...', ");
            } else {
                sbuf_adds(b, "', ");
            }
            break;
        }
        case IS_ARRAY:
            sbuf_adds(b, "Array, ");
            break;
        case IS_OBJECT:
            sbuf_adds(b, "Object(");
            if (Z_OBJ_P(a)->ce != NULL) {
                sbuf_addl(b, ZSTR_VAL(Z_OBJ_P(a)->ce->name), ZSTR_LEN(Z_OBJ_P(a)->ce->name));
            }
            sbuf_adds(b, "), ");
            break;
        default:
            break;
    }
}

static void trace_append_str_key(sbuf *b, HashTable *ht, const char *key)
{
    zval *v = zend_hash_str_find(ht, key, strlen(key));

    if (v != NULL && Z_TYPE_P(v) == IS_STRING) {
        sbuf_addl(b, ZSTR_VAL(Z_STR_P(v)), ZSTR_LEN(Z_STR_P(v)));
    }
}

/* One "#N file(line): Class->method(args)\n" line. */
static void trace_append_frame(sbuf *b, HashTable *ht, uint32_t num)
{
    zval *file, *tmp;

    sbuf_addc(b, '#');
    sbuf_addlong(b, (zend_long) num);
    sbuf_addc(b, ' ');

    file = zend_hash_str_find(ht, "file", 4);
    if (file != NULL) {
        if (Z_TYPE_P(file) != IS_STRING) {
            sbuf_adds(b, "[unknown function]");
        } else {
            zend_long line = 0;

            tmp = zend_hash_str_find(ht, "line", 4);
            if (tmp != NULL && Z_TYPE_P(tmp) == IS_LONG) {
                line = Z_LVAL_P(tmp);
            }
            sbuf_addl(b, ZSTR_VAL(Z_STR_P(file)), ZSTR_LEN(Z_STR_P(file)));
            sbuf_addc(b, '(');
            sbuf_addlong(b, line);
            sbuf_adds(b, "): ");
        }
    } else {
        sbuf_adds(b, "[internal function]: ");
    }

    trace_append_str_key(b, ht, "class");
    trace_append_str_key(b, ht, "type");
    trace_append_str_key(b, ht, "function");

    sbuf_addc(b, '(');
    tmp = zend_hash_str_find(ht, "args", 4);
    if (tmp != NULL && Z_TYPE_P(tmp) == IS_ARRAY) {
        size_t before = b->len;

        ZEND_HASH_FOREACH(Z_ARR_P(tmp), bucket) {
            if (bucket->key != NULL) {
                sbuf_addl(b, ZSTR_VAL(bucket->key), ZSTR_LEN(bucket->key));
                sbuf_adds(b, ": ");
            }
            trace_append_arg(b, &bucket->val);
        } ZEND_HASH_FOREACH_END();

        if (b->len != before) {
            b->len -= 2;            /* drop the trailing ", " */
            b->s[b->len] = '\0';
        }
    }
    sbuf_adds(b, ")\n");
}

/* php-src's zend_trace_to_string(): every frame, then "#N {main}" with no
 * trailing newline -- getTraceAsString() of a top-level throw is the nine
 * bytes "#0 {main}". */
static zend_string *trace_to_string(HashTable *trace, int include_main)
{
    sbuf b = { NULL, 0, 0 };
    uint32_t num = 0;

    if (trace != NULL) {
        ZEND_HASH_FOREACH(trace, bucket) {
            if (Z_TYPE(bucket->val) == IS_ARRAY) {
                trace_append_frame(&b, Z_ARR(bucket->val), num++);
            }
        } ZEND_HASH_FOREACH_END();
    }
    if (include_main) {
        sbuf_addc(&b, '#');
        sbuf_addlong(&b, (zend_long) num);
        sbuf_adds(&b, " {main}");
    }
    return sbuf_to_string(&b);
}

zend_string *zend_exception_trace_as_string(zend_object *ex)
{
    zval *trace = ex_slot(ex, "trace");

    if (trace == NULL || Z_TYPE_P(trace) != IS_ARRAY) {
        return trace_to_string(NULL, 1);
    }
    return trace_to_string(Z_ARR_P(trace), 1);
}

/* ==========================================================================
 *  Capturing the stack
 * ==========================================================================
 * php-src snapshots the stack in the object-creation handler, before any
 * constructor body runs, so neither Exception::__construct nor a user
 * subclass's constructor appears in the trace -- and the exception's own
 * file/line are the `new` site, not wherever parent::__construct was called
 * from.  Reproducing that here means walking past the leading Throwable
 * constructor frames; the outermost of them is the `new` site, because an
 * execute_data's filename/lineno are its *call site*.
 */

static int class_is_throwable(zend_class_entry *ce)
{
    if (ce == NULL) {
        return 0;
    }
    /* VERSIONGATE:70000 else-branch is the 5.6 rule: Exception is the root */
    if (EG(php_version) >= 70000 && zend_ce_throwable != NULL) {
        return zend_instanceof(ce, zend_ce_throwable);
    }
    return zend_instanceof(ce, zend_ce_exception);
}

static int frame_is_throwable_ctor(zend_execute_data *frame)
{
    if (frame == NULL || frame->func == NULL || frame->func->scope == NULL) {
        return 0;
    }
    if (frame->func->lcname == NULL ||
        strcmp(ZSTR_VAL(frame->func->lcname), "__construct") != 0) {
        return 0;
    }
    return class_is_throwable(frame->func->scope);
}

/* Skip the Throwable constructor chain.  `origin` comes back as the outermost
 * constructor frame, whose call site is where `new` was written. */
static zend_execute_data *skip_ctor_frames(zend_execute_data **origin)
{
    zend_execute_data *frame = EG(current);

    *origin = NULL;
    while (frame_is_throwable_ctor(frame)) {
        *origin = frame;
        frame = frame->prev;
    }
    return frame;
}

static int trace_ignores_args(void)
{

    return 0;
}

void zend_exception_capture_trace(zend_object *ex)
{
    HashTable *trace = zend_hash_new();
    zend_execute_data *origin = NULL;
    zend_execute_data *frame = skip_ctor_frames(&origin);
    int ignore_args = trace_ignores_args();

    for (; frame != NULL; frame = frame->prev) {
        HashTable *entry;

        if (frame->func == NULL) {
            continue;               /* the file scope is not a trace frame */
        }
        entry = zend_hash_new();

        /* php-src's key order: file, line, function, class, type, args. */
        if (frame->filename != NULL) {
            zend_hash_str_update(entry, "file", 4, zval_zstr(frame->filename));
        }
        zend_hash_str_update(entry, "line", 4, zval_long((zend_long) frame->lineno));
        if (frame->func->name != NULL) {
            zend_hash_str_update(entry, "function", 8, zval_zstr(frame->func->name));
        }
        if (frame->func->scope != NULL) {
            zend_hash_str_update(entry, "class", 5, zval_zstr(frame->func->scope->name));
            zend_hash_str_update(entry, "type", 4,
                                 zval_string(frame->This != NULL ? "->" : "::"));
        }
        if (!ignore_args) {
            HashTable *args = zend_hash_new();
            uint32_t i;

            for (i = 0; i < frame->num_args; i++) {
                zval v = (frame->args != NULL) ? zval_copy(frame->args[i]) : zval_null();

                if (frame->arg_names != NULL && frame->arg_names[i] != NULL) {
                    zend_hash_update(args, frame->arg_names[i], v);
                } else {
                    zend_hash_next_index_insert(args, v);
                }
            }
            zend_hash_str_update(entry, "args", 4, zval_array(args));
        }
        zend_hash_next_index_insert(trace, zval_array(entry));
    }

    ex_set(ex, "trace", zval_array(trace));

    /* file/line: the `new` site, which is the outermost constructor frame's
     * call site.  With no frame to read (an engine-thrown exception), the
     * current execution position is the same thing. */
    if (origin != NULL && origin->filename != NULL) {
        ex_set(ex, "file", zval_zstr(origin->filename));
        ex_set(ex, "line", zval_long((zend_long) origin->lineno));
    } else {
        ex_set(ex, "file", zval_zstr(EG(filename) != NULL ? EG(filename)
                                                          : zend_string_init("", 0)));
        ex_set(ex, "line", zval_long((zend_long) EG(lineno)));
    }
}

void zend_exception_init(zend_object *ex, zend_string *message, zend_long code,
                         zend_object *previous)
{
    if (ex == NULL) {
        return;
    }
    ex_set(ex, "message", zval_zstr(message != NULL ? message : zend_string_init("", 0)));
    ex_set(ex, "code", zval_long(code));
    if (previous != NULL) {
        ex_set(ex, "previous", zval_object(previous));
    }
    zend_exception_capture_trace(ex);
}

/* ==========================================================================
 *  __toString()
 * ==========================================================================
 * php-src walks outward along `previous`, prepending each link:
 *
 *   5.6   exception 'Cls' with message 'msg' in FILE:LINE\nStack trace:\n<t>
 *         and, with an empty message,
 *         exception 'Cls' in FILE:LINE\nStack trace:\n<t>
 *
 *   7.0+  Cls: msg in FILE:LINE\nStack trace:\n<t>
 *         Cls in FILE:LINE\nStack trace:\n<t>
 *
 * with "\n\nNext " joining a link to everything built so far, so the text
 * reads innermost cause first.  There is no trailing newline.
 */

static zend_string *exception_build_string(zend_object *ex)
{
    zend_string *acc = zend_string_init("", 0);
    zend_object *cur = ex;
    int depth = 0;

    while (cur != NULL && class_is_throwable(cur->ce) && depth < 64) {
        zend_string *message = zend_exception_get_message(cur);
        zend_string *file    = zend_exception_get_file(cur);
        zend_long    line    = zend_exception_get_line(cur);
        zend_string *trace   = zend_exception_trace_as_string(cur);
        const char  *cname   = (cur->ce != NULL) ? ZSTR_VAL(cur->ce->name) : "Exception";
        const char  *joiner  = (ZSTR_LEN(acc) != 0) ? "\n\nNext " : "";
        sbuf b = { NULL, 0, 0 };

        /* VERSIONGATE:70000 else-branch is the 5.6 format */
        if (EG(php_version) >= 70000) {
            sbuf_adds(&b, cname);
            if (ZSTR_LEN(message) > 0) {
                sbuf_adds(&b, ": ");
                sbuf_addl(&b, ZSTR_VAL(message), ZSTR_LEN(message));
            }
        } else {
            sbuf_adds(&b, "exception '");
            sbuf_adds(&b, cname);
            sbuf_addc(&b, '\'');
            if (ZSTR_LEN(message) > 0) {
                sbuf_adds(&b, " with message '");
                sbuf_addl(&b, ZSTR_VAL(message), ZSTR_LEN(message));
                sbuf_addc(&b, '\'');
            }
        }
        sbuf_adds(&b, " in ");
        sbuf_addl(&b, ZSTR_VAL(file), ZSTR_LEN(file));
        sbuf_addc(&b, ':');
        sbuf_addlong(&b, line);
        sbuf_adds(&b, "\nStack trace:\n");
        if (ZSTR_LEN(trace) != 0) {
            sbuf_addl(&b, ZSTR_VAL(trace), ZSTR_LEN(trace));
        } else {
            sbuf_adds(&b, "#0 {main}\n");
        }
        sbuf_adds(&b, joiner);
        sbuf_addl(&b, ZSTR_VAL(acc), ZSTR_LEN(acc));

        acc = sbuf_to_string(&b);
        cur = zend_exception_get_previous(cur);
        depth++;
    }
    return acc;
}

zend_string *zend_exception_to_string(zend_object *ex)
{
    zend_function *ts;
    zend_string *result;

    if (ex == NULL) {
        return zend_string_init("", 0);
    }

    /* A user subclass that overrides __toString() wins, exactly as
     * zend_exception_error() lets it. */
    ts = (ex->ce != NULL) ? ex->ce->__tostring : NULL;
    if (ts != NULL && !ts->is_internal) {
        zval ret = zval_null();

        if (zend_call_function_ex(ts, ex, ex->ce, 0, NULL, NULL, &ret, NULL) == 0) {
            zval *r = zval_deref(&ret);

            if (Z_TYPE_P(r) == IS_STRING) {
                return Z_STR_P(r);
            }
        }
        EG(exception) = NULL;
    }

    result = exception_build_string(ex);
    /* php-src caches the text in the private `string` slot. */
    ex_set(ex, "string", zval_zstr(result));
    return result;
}

/* ==========================================================================
 *  Exception's internal methods
 * ========================================================================== */

static void zim_exception_construct(zval *ret, uint32_t argc, zval *argv)
{
    zend_object *self = zend_get_this();
    zend_string *message = NULL;
    zend_long code = 0;
    zend_object *previous = NULL;

    ZVAL_NULL(ret);
    if (self == NULL) {
        return;
    }
    if (argc > 0) {
        message = zval_get_string(zval_deref(&argv[0]));
    }
    if (argc > 1) {
        code = zval_get_long(zval_deref(&argv[1]));
    }
    if (argc > 2) {
        zval *p = zval_deref(&argv[2]);

        if (Z_TYPE_P(p) == IS_OBJECT && class_is_throwable(Z_OBJ_P(p)->ce)) {
            previous = Z_OBJ_P(p);
        }
    }
    zend_exception_init(self, message, code, previous);
}

static void zim_exception_wakeup(zval *ret, uint32_t argc, zval *argv)
{
    ZVAL_NULL(ret);
}

static void zim_exception_clone(zval *ret, uint32_t argc, zval *argv)
{
    ZVAL_NULL(ret);
}

static void zim_exception_get_message(zval *ret, uint32_t argc, zval *argv)
{
    ZVAL_STR(ret, zend_exception_get_message(zend_get_this()));
}

static void zim_exception_get_code(zval *ret, uint32_t argc, zval *argv)
{
    zend_object *self = zend_get_this();
    zval *v = (self != NULL) ? ex_slot(self, "code") : NULL;

    /* getCode() hands back whatever is in the slot: php allows a string code
     * for PDOException and friends. */
    *ret = (v != NULL) ? zval_copy(*zval_deref(v)) : zval_long(0);
}

static void zim_exception_get_file(zval *ret, uint32_t argc, zval *argv)
{
    ZVAL_STR(ret, zend_exception_get_file(zend_get_this()));
}

static void zim_exception_get_line(zval *ret, uint32_t argc, zval *argv)
{
    ZVAL_LONG(ret, zend_exception_get_line(zend_get_this()));
}

static void zim_exception_get_trace(zval *ret, uint32_t argc, zval *argv)
{
    zend_object *self = zend_get_this();
    zval *v = (self != NULL) ? ex_slot(self, "trace") : NULL;

    if (v != NULL && Z_TYPE_P(v) == IS_ARRAY) {
        *ret = zval_copy(*v);
    } else {
        ZVAL_ARR(ret, zend_hash_new());
    }
}

static void zim_exception_get_previous(zval *ret, uint32_t argc, zval *argv)
{
    zend_object *prev = zend_exception_get_previous(zend_get_this());

    if (prev != NULL) {
        prev->refcount++;
        ZVAL_OBJ(ret, prev);
    } else {
        ZVAL_NULL(ret);
    }
}

static void zim_exception_get_trace_as_string(zval *ret, uint32_t argc, zval *argv)
{
    ZVAL_STR(ret, zend_exception_trace_as_string(zend_get_this()));
}

static void zim_exception_to_string(zval *ret, uint32_t argc, zval *argv)
{
    zend_object *self = zend_get_this();
    zend_string *str;

    if (self == NULL) {
        ZVAL_STR(ret, zend_string_init("", 0));
        return;
    }
    str = exception_build_string(self);
    ex_set(self, "string", zval_zstr(str));
    ZVAL_STR(ret, str);
}

static void zim_errorexception_construct(zval *ret, uint32_t argc, zval *argv)
{
    zend_object *self = zend_get_this();
    zend_string *message = NULL;
    zend_long code = 0;
    zend_long severity = E_ERROR;
    zend_object *previous = NULL;

    ZVAL_NULL(ret);
    if (self == NULL) {
        return;
    }
    if (argc > 0) {
        message = zval_get_string(zval_deref(&argv[0]));
    }
    if (argc > 1) {
        code = zval_get_long(zval_deref(&argv[1]));
    }
    if (argc > 2) {
        severity = zval_get_long(zval_deref(&argv[2]));
    }
    if (argc > 5) {
        zval *p = zval_deref(&argv[5]);

        if (Z_TYPE_P(p) == IS_OBJECT && class_is_throwable(Z_OBJ_P(p)->ce)) {
            previous = Z_OBJ_P(p);
        }
    }
    zend_exception_init(self, message, code, previous);
    ex_set(self, "severity", zval_long(severity));

    /* ErrorException lets the caller name the origin explicitly. */
    if (argc > 3) {
        zval *p = zval_deref(&argv[3]);

        if (Z_TYPE_P(p) != IS_NULL && Z_TYPE_P(p) != IS_UNDEF) {
            ex_set(self, "file", zval_zstr(zval_get_string(p)));
        }
    }
    if (argc > 4) {
        zval *p = zval_deref(&argv[4]);

        if (Z_TYPE_P(p) != IS_NULL && Z_TYPE_P(p) != IS_UNDEF) {
            ex_set(self, "line", zval_long(zval_get_long(p)));
        }
    }
}

static void zim_errorexception_get_severity(zval *ret, uint32_t argc, zval *argv)
{
    zend_object *self = zend_get_this();
    zval *v = (self != NULL) ? ex_slot(self, "severity") : NULL;

    ZVAL_LONG(ret, (v != NULL) ? zval_get_long(v) : E_ERROR);
}

static void zim_noop(zval *ret, uint32_t argc, zval *argv)
{
    ZVAL_NULL(ret);
}

/* ==========================================================================
 *  The Throwable property set and method set
 * ========================================================================== */

/* Exception and Error carry the same seven slots in the same order; only the
 * owner of the three private ones differs, which is what makes a var_dump of
 * a TypeError say ["string":"Error":private]. */
static void add_throwable_properties(zend_class_entry *ce)
{
    zend_class_add_property(ce, "message",  ACC_PROT, zval_string(""));
    zend_class_add_property(ce, "string",   ACC_PRIV, zval_string(""));
    zend_class_add_property(ce, "code",     ACC_PROT, zval_long(0));
    zend_class_add_property(ce, "file",     ACC_PROT, zval_string(""));
    zend_class_add_property(ce, "line",     ACC_PROT, zval_long(0));
    zend_class_add_property(ce, "trace",    ACC_PRIV, zval_array(zend_hash_new()));
    zend_class_add_property(ce, "previous", ACC_PRIV, zval_null());
}

static void add_throwable_methods(zend_class_entry *ce)
{
    zend_class_add_method(ce, "__clone", zim_exception_clone, 0, 0, ACC_PRIV);
    zend_function_set_arg_names(
        zend_class_add_method(ce, "__construct", zim_exception_construct, 0, 3, ACC_PUB),
        "message,code,previous");
    zend_class_add_method(ce, "__wakeup", zim_exception_wakeup, 0, 0, ACC_PUB);
    zend_class_add_method(ce, "getMessage", zim_exception_get_message, 0, 0, ACC_FINAL_PUB);
    zend_class_add_method(ce, "getCode", zim_exception_get_code, 0, 0, ACC_FINAL_PUB);
    zend_class_add_method(ce, "getFile", zim_exception_get_file, 0, 0, ACC_FINAL_PUB);
    zend_class_add_method(ce, "getLine", zim_exception_get_line, 0, 0, ACC_FINAL_PUB);
    zend_class_add_method(ce, "getTrace", zim_exception_get_trace, 0, 0, ACC_FINAL_PUB);
    zend_class_add_method(ce, "getPrevious", zim_exception_get_previous, 0, 0, ACC_FINAL_PUB);
    zend_class_add_method(ce, "getTraceAsString", zim_exception_get_trace_as_string,
                          0, 0, ACC_FINAL_PUB);
    zend_class_add_method(ce, "__toString", zim_exception_to_string, 0, 0, ACC_PUB);
}

/* ==========================================================================
 *  Class construction, one release at a time
 * ========================================================================== */

static void register_class_stdclass(void)
{
    zend_standard_class_def = zend_register_internal_class("stdClass", NULL, 0);
}

static void register_interfaces_5_6(void)
{
    zend_ce_traversable = zend_register_internal_interface("Traversable");

    zend_ce_iterator = zend_register_internal_interface("Iterator");
    zend_class_add_interface(zend_ce_iterator, zend_ce_traversable);
    zend_class_add_method(zend_ce_iterator, "current", NULL, 0, 0, ACC_ABSTRACT_PUB);
    zend_class_add_method(zend_ce_iterator, "next", NULL, 0, 0, ACC_ABSTRACT_PUB);
    zend_class_add_method(zend_ce_iterator, "key", NULL, 0, 0, ACC_ABSTRACT_PUB);
    zend_class_add_method(zend_ce_iterator, "valid", NULL, 0, 0, ACC_ABSTRACT_PUB);
    zend_class_add_method(zend_ce_iterator, "rewind", NULL, 0, 0, ACC_ABSTRACT_PUB);

    zend_ce_aggregate = zend_register_internal_interface("IteratorAggregate");
    zend_class_add_interface(zend_ce_aggregate, zend_ce_traversable);
    zend_class_add_method(zend_ce_aggregate, "getIterator", NULL, 0, 0, ACC_ABSTRACT_PUB);

    zend_ce_arrayaccess = zend_register_internal_interface("ArrayAccess");
    zend_class_add_method(zend_ce_arrayaccess, "offsetExists", NULL, 1, 1, ACC_ABSTRACT_PUB);
    zend_class_add_method(zend_ce_arrayaccess, "offsetGet", NULL, 1, 1, ACC_ABSTRACT_PUB);
    zend_class_add_method(zend_ce_arrayaccess, "offsetSet", NULL, 2, 2, ACC_ABSTRACT_PUB);
    zend_class_add_method(zend_ce_arrayaccess, "offsetUnset", NULL, 1, 1, ACC_ABSTRACT_PUB);

    zend_ce_countable = zend_register_internal_interface("Countable");
    zend_class_add_method(zend_ce_countable, "count", NULL, 0, 0, ACC_ABSTRACT_PUB);

    zend_ce_serializable = zend_register_internal_interface("Serializable");
    zend_class_add_method(zend_ce_serializable, "serialize", NULL, 0, 0, ACC_ABSTRACT_PUB);
    zend_class_add_method(zend_ce_serializable, "unserialize", NULL, 1, 1, ACC_ABSTRACT_PUB);

    zend_ce_jsonserializable = zend_register_internal_interface("JsonSerializable");
    zend_class_add_method(zend_ce_jsonserializable, "jsonSerialize", NULL, 0, 0, ACC_ABSTRACT_PUB);
}



/* VERSIONGATE:70000 */
static void register_class_throwable(void)
{
    zend_ce_throwable = zend_register_internal_interface("Throwable");

    zend_class_add_method(zend_ce_throwable, "getMessage", NULL, 0, 0, ACC_ABSTRACT_PUB);
    zend_class_add_method(zend_ce_throwable, "getCode", NULL, 0, 0, ACC_ABSTRACT_PUB);
    zend_class_add_method(zend_ce_throwable, "getFile", NULL, 0, 0, ACC_ABSTRACT_PUB);
    zend_class_add_method(zend_ce_throwable, "getLine", NULL, 0, 0, ACC_ABSTRACT_PUB);
    zend_class_add_method(zend_ce_throwable, "getTrace", NULL, 0, 0, ACC_ABSTRACT_PUB);
    zend_class_add_method(zend_ce_throwable, "getPrevious", NULL, 0, 0, ACC_ABSTRACT_PUB);
    zend_class_add_method(zend_ce_throwable, "getTraceAsString", NULL, 0, 0, ACC_ABSTRACT_PUB);
}



static void register_class_exception(void)
{
    zend_ce_exception = zend_register_internal_class("Exception", NULL, 0);
    /* VERSIONGATE:70000 Exception implements Throwable from 7.0 */
    if (EG(php_version) >= 70000 && zend_ce_throwable != NULL) {
        zend_class_add_interface(zend_ce_exception, zend_ce_throwable);
    }
    add_throwable_properties(zend_ce_exception);
    add_throwable_methods(zend_ce_exception);
}

/* VERSIONGATE:70000 */
static void register_class_error(void)
{
    zend_ce_error = zend_register_internal_class("Error", NULL, 0);
    if (zend_ce_throwable != NULL) {
        zend_class_add_interface(zend_ce_error, zend_ce_throwable);
    }
    add_throwable_properties(zend_ce_error);
    add_throwable_methods(zend_ce_error);
}

static void register_class_errorexception(void)
{
    zend_ce_error_exception = zend_register_internal_class("ErrorException",
                                                           zend_ce_exception, 0);
    zend_class_add_property(zend_ce_error_exception, "severity", ACC_PROT,
                            zval_long(E_ERROR));
    zend_function_set_arg_names(
        zend_class_add_method(zend_ce_error_exception, "__construct",
                              zim_errorexception_construct, 0, 6, ACC_PUB),
        "message,code,severity,filename,line,previous");
    zend_class_add_method(zend_ce_error_exception, "getSeverity",
                          zim_errorexception_get_severity, 0, 0, ACC_FINAL_PUB);
}

/* The SPL exception family; present in every release this interpreter
 * emulates, so none of it is gated. */
static void register_classes_spl_exceptions(void)
{
    zend_class_entry *bad_function_call;

    zend_ce_logic_exception   = zend_register_internal_class("LogicException",
                                                             zend_ce_exception, 0);
    zend_ce_runtime_exception = zend_register_internal_class("RuntimeException",
                                                             zend_ce_exception, 0);

    bad_function_call = zend_register_internal_class("BadFunctionCallException",
                                                     zend_ce_logic_exception, 0);
    zend_register_internal_class("BadMethodCallException", bad_function_call, 0);
    zend_register_internal_class("DomainException", zend_ce_logic_exception, 0);
    zend_register_internal_class("InvalidArgumentException", zend_ce_logic_exception, 0);
    zend_register_internal_class("LengthException", zend_ce_logic_exception, 0);
    zend_register_internal_class("OutOfRangeException", zend_ce_logic_exception, 0);

    zend_register_internal_class("OutOfBoundsException", zend_ce_runtime_exception, 0);
    zend_register_internal_class("OverflowException", zend_ce_runtime_exception, 0);
    zend_register_internal_class("RangeException", zend_ce_runtime_exception, 0);
    zend_register_internal_class("UnderflowException", zend_ce_runtime_exception, 0);
    zend_register_internal_class("UnexpectedValueException", zend_ce_runtime_exception, 0);
}

/* VERSIONGATE:70000 */
static void register_class_type_error(void)
{
    zend_ce_type_error = zend_register_internal_class("TypeError", zend_ce_error, 0);
}

/* VERSIONGATE:70100 */
static void register_class_argument_count_error(void)
{
    zend_ce_argument_count_error =
        zend_register_internal_class("ArgumentCountError", zend_ce_type_error, 0);
}

/* VERSIONGATE:70000 */
static void register_class_arithmetic_error(void)
{
    zend_ce_arithmetic_error =
        zend_register_internal_class("ArithmeticError", zend_ce_error, 0);
}

/* VERSIONGATE:70000 */
static void register_class_division_by_zero_error(void)
{
    zend_ce_division_by_zero_error =
        zend_register_internal_class("DivisionByZeroError", zend_ce_arithmetic_error, 0);
}

/* VERSIONGATE:70000 */
static void register_class_assertion_error(void)
{
    zend_ce_assertion_error =
        zend_register_internal_class("AssertionError", zend_ce_error, 0);
}





/* VERSIONGATE:70300 */
static void register_class_json_exception(void)
{
    zend_ce_json_exception =
        zend_register_internal_class("JsonException", zend_ce_exception, 0);
}

/* ==========================================================================
 *  Shells owned by the evaluator
 * ==========================================================================
 * Closure and Generator exist in every release, but their mechanics belong to
 * the evaluator: it attaches the method handlers with
 * zend_class_add_method() once it can service them.  Registering the class
 * entries here keeps class_exists(), instanceof and the class table honest
 * from start-up.
 */

static void register_class_closure(void)
{
    zend_ce_closure = zend_register_internal_class("Closure", NULL, ZEND_ACC_FINAL);
}

static void register_class_generator(void)
{
    zend_ce_generator = zend_register_internal_class("Generator", NULL, ZEND_ACC_FINAL);
    zend_class_add_interface(zend_ce_generator, zend_ce_iterator);
}
































/* ==========================================================================
 *  Registration
 * ========================================================================== */

void zend_register_default_classes(void)
{
    register_class_stdclass();
    register_interfaces_5_6();


    /* VERSIONGATE:70000 */
    if (EG(php_version) >= 70000) {
        register_class_throwable();
    }


    register_class_exception();
    /* VERSIONGATE:70000 */
    if (EG(php_version) >= 70000) {
        register_class_error();
    }
    register_class_errorexception();
    register_classes_spl_exceptions();

    /* VERSIONGATE:70000 */
    if (EG(php_version) >= 70000) {
        register_class_type_error();
    }
    /* VERSIONGATE:70100 */
    if (EG(php_version) >= 70100) {
        register_class_argument_count_error();
    }
    /* VERSIONGATE:70000 */
    if (EG(php_version) >= 70000) {
        register_class_arithmetic_error();
    }
    /* VERSIONGATE:70000 */
    if (EG(php_version) >= 70000) {
        register_class_division_by_zero_error();
    }
    /* VERSIONGATE:70000 */
    if (EG(php_version) >= 70000) {
        register_class_assertion_error();
    }


    /* VERSIONGATE:70300 */
    if (EG(php_version) >= 70300) {
        register_class_json_exception();
    }

    register_class_closure();
    register_class_generator();





}

/* ==========================================================================
 *  Throwing
 * ========================================================================== */

/* php-src's zend_exception_set_previous().  An exception thrown while another
 * one is still travelling does not replace it: the pending exception is hung
 * off the end of the new one's `previous` chain, which is what makes two
 * destructors throwing during shutdown report as
 *
 *     Uncaught exception 'Exception' with message 'First' ...
 *     Next exception 'Exception' with message 'Second' ...
 *
 * The walk stops at a link that is already the exception being added, so a
 * re-throw of the same object cannot build a cycle. */
static void exception_set_previous(zend_object *ex, zend_object *previous)
{
    zend_object *tail = ex;
    int depth = 0;

    if (ex == NULL || previous == NULL || ex == previous) {
        return;
    }
    if (!class_is_throwable(previous->ce)) {
        return;
    }
    for (;;) {
        zend_object *next = zend_exception_get_previous(tail);

        if (next == NULL) {
            break;
        }
        if (next == previous) {
            return;                 /* already on the chain */
        }
        tail = next;
        if (++depth > 64) {
            return;
        }
    }
    ex_set(tail, "previous", zval_object(previous));
}

void zend_throw_exception_object(zend_object *obj)
{
    if (obj == NULL) {
        return;
    }
    exception_set_previous(obj, EG(exception));
    EG(exception) = obj;
}

/* The engine's own throw.  `code` lands in Exception::$code, which matters for
 * the modules that report a numeric status that way -- ext/json puts its
 * JSON_ERROR_* value there under JSON_THROW_ON_ERROR. */
static zend_object *zend_throw_error_va(const char *class_name, zend_long code,
                                        const char *fmt, va_list ap)
{
    zend_class_entry *ce;
    zend_object *obj;
    zend_string *message;
    zend_string *lcname;
    va_list ap2;
    char stackbuf[512];
    char *buf = stackbuf;
    int len;

    va_copy(ap2, ap);
    len = vsnprintf(stackbuf, sizeof(stackbuf), fmt, ap);
    if (len < 0) {
        len = 0;
        stackbuf[0] = '\0';
    } else if ((size_t) len >= sizeof(stackbuf)) {
        buf = emalloc((size_t) len + 1);
        vsnprintf(buf, (size_t) len + 1, fmt, ap2);
    }
    va_end(ap2);
    message = zend_string_init(buf, (size_t) len);

    lcname = zend_string_tolower(S(class_name != NULL ? class_name : "Exception"));
    ce = zend_lookup_class(lcname, 0);

    /* Before 7.0 the Error branch does not exist, so an engine condition that
     * a later release turns into a Throwable is simply fatal. */
    /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
    if (ce == NULL) {
        if (EG(php_version) < 70000 || zend_ce_error == NULL) {
            zend_fatal("%s", ZSTR_VAL(message));
            return NULL;
        }
        ce = zend_ce_error;
    }

    obj = zend_object_new(ce);
    zend_exception_init(obj, message, code, NULL);
    zend_throw_exception_object(obj);
    return obj;
}

zend_object *zend_throw_error(const char *class_name, const char *fmt, ...)
{
    zend_object *obj;
    va_list ap;

    va_start(ap, fmt);
    obj = zend_throw_error_va(class_name, 0, fmt, ap);
    va_end(ap);
    return obj;
}

zend_object *zend_throw_error_code(const char *class_name, zend_long code,
                                   const char *fmt, ...)
{
    zend_object *obj;
    va_list ap;

    va_start(ap, fmt);
    obj = zend_throw_error_va(class_name, code, fmt, ap);
    va_end(ap);
    return obj;
}

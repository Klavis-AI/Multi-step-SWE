/* zend_generator.c -- generators (and 8.1's Fiber) as real coroutines.
 *
 * A generator function does not run when it is called: zend_call_function_ex()
 * builds the frame, hands it to zend_generator_create() and returns a
 * Generator object.  The body then runs on a stack of its own, switched into
 * with makecontext()/swapcontext(), so a "yield" can suspend in the middle of
 * an arbitrarily deep C recursion of the AST evaluator -- which is exactly
 * what an AST walker needs and what php-src gets for free from its VM loop.
 *
 * Everything the engine considers "current" (EG(current), EG(filename),
 * EG(lineno), EG(exception), the call depth and the pending break level) is
 * saved on one side of every switch and restored on the other, so the
 * generator and its caller never observe each other's bookkeeping.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1        /* realpath(), strcasecmp(), ucontext */
#endif
#include "zend.h"
#include "zend_ast.h"
#include "zend_API.h"
#include "zend_alloc.h"
#include "zend_object.h"
#include "zend_parse.h"
#include "zend_exceptions.h"
#include "zend_execute.h"

#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <ucontext.h>

#define GEN_STACK_SIZE (512 * 1024)

enum { GEN_INIT = 0, GEN_SUSPENDED, GEN_RUNNING, GEN_DONE };

struct _zend_generator {
    zend_object       *obj;
    zend_execute_data *frame;

    ucontext_t  ctx;            /* the generator's own context   */
    ucontext_t  ret_ctx;        /* where to go when it suspends  */
    char       *stack;

    unsigned char state;
    unsigned char started;
    unsigned char finished;
    unsigned char has_current;
    unsigned char running_fiber;    /* this coroutine is a Fiber, not a generator */
    unsigned char advanced;         /* resumed at least once past the first yield */

    zval  current_key, current_val;
    zval  send_value;
    zval  retval;
    zend_long auto_key;

    zend_object *throw_in;          /* exception to raise at the resume point */
    zend_object *pending_exception; /* exception escaping the body            */

    /* engine state parked while the generator is suspended */
    zend_execute_data *gen_current;
    zend_string       *gen_filename;
    uint32_t           gen_lineno;
    int                gen_depth;

    /* Fiber payload */
    zval fiber_callable;
    zval fiber_transfer;
    unsigned char fiber_status;
};

static zend_generator *g_entering = NULL;
static zend_generator *g_current_fiber = NULL;

/* ------------------------------------------------------------------ class */

static unsigned char generator_methods_bound = 0;
static void zim_gen_current(zval *r, uint32_t c, zval *v);
static void zim_gen_key(zval *r, uint32_t c, zval *v);
static void zim_gen_next(zval *r, uint32_t c, zval *v);
static void zim_gen_send(zval *r, uint32_t c, zval *v);
static void zim_gen_valid(zval *r, uint32_t c, zval *v);
static void zim_gen_rewind(zval *r, uint32_t c, zval *v);
static void zim_gen_getreturn(zval *r, uint32_t c, zval *v);
static void zim_gen_throw(zval *r, uint32_t c, zval *v);

static zend_class_entry *generator_ce(void)
{
    if (!zend_ce_generator) return NULL;
    if (!generator_methods_bound) {
        generator_methods_bound = 1;
        zend_class_add_method(zend_ce_generator, "current",   zim_gen_current,   0, 0, ZEND_ACC_PUBLIC);
        zend_class_add_method(zend_ce_generator, "key",       zim_gen_key,       0, 0, ZEND_ACC_PUBLIC);
        zend_class_add_method(zend_ce_generator, "next",      zim_gen_next,      0, 0, ZEND_ACC_PUBLIC);
        zend_class_add_method(zend_ce_generator, "send",      zim_gen_send,      1, 1, ZEND_ACC_PUBLIC);
        zend_class_add_method(zend_ce_generator, "valid",     zim_gen_valid,     0, 0, ZEND_ACC_PUBLIC);
        zend_class_add_method(zend_ce_generator, "rewind",    zim_gen_rewind,    0, 0, ZEND_ACC_PUBLIC);
        /* VERSIONGATE:70000 Generator::getReturn() arrived with 7.0 */
        zend_class_add_method(zend_ce_generator, "getReturn", zim_gen_getreturn, 0, 0, ZEND_ACC_PUBLIC);
        zend_class_add_method(zend_ce_generator, "throw",     zim_gen_throw,     1, 1, ZEND_ACC_PUBLIC);
    }
    return zend_ce_generator;
}

zend_function *zend_generator_function(zend_generator *g)
{
    return (g && g->frame) ? g->frame->func : NULL;
}

zend_generator *zend_generator_from_object(zend_object *obj)
{
    if (!obj || !zend_ce_generator || obj->ce != zend_ce_generator) return NULL;
    return (zend_generator *)obj->internal;
}

/* ------------------------------------------------- detecting a generator */

/* Bit 31 of zend_function.flags records that the body has been scanned, so a
 * function is only walked once.  ZEND_ACC_GENERATOR is set by the parser when
 * it already knows; this is the fallback. */
#define ZEND_ACC_GEN_CHECKED (1u << 31)

static int ast_has_yield(zend_ast *a)
{
    if (!a) return 0;
    if (a->kind == AST_YIELD || a->kind == AST_YIELD_FROM) return 1;
    /* a nested function, closure or class body has its own yields */
    if (a->kind == AST_FUNC_DECL || a->kind == AST_CLOSURE || a->kind == AST_ARROW_FN ||
        a->kind == AST_CLASS_DECL || a->kind == AST_ANON_CLASS)
        return 0;
    for (uint32_t i = 0; i < a->nchild; i++)
        if (ast_has_yield(a->child[i])) return 1;
    return 0;
}

int zend_function_is_generator(zend_function *fn)
{
    if (!fn || fn->is_internal) return 0;
    if (fn->flags & ZEND_ACC_GENERATOR) return 1;
    if (fn->flags & ZEND_ACC_GEN_CHECKED) return 0;
    fn->flags |= ZEND_ACC_GEN_CHECKED;
    if (ast_has_yield(fn->body)) { fn->flags |= ZEND_ACC_GENERATOR; return 1; }
    return 0;
}

/* -------------------------------------------------------- context switching */

static void generator_trampoline(void)
{
    zend_generator *g = g_entering;
    zval rv = zval_null();

    if (g->running_fiber) {
        zval ret = zval_null();
        zval cb = g->fiber_callable;
        zval args[1];
        uint32_t argc = 0;
        if (g->send_value.u1 != IS_UNDEF && g->send_value.u1 != IS_NULL) {
            args[0] = g->send_value;
            argc = 1;
        }
        if (zend_call_user_callable(&cb, argc, argc ? args : NULL, &ret)) {
            if (EG(exception)) { g->pending_exception = EG(exception); EG(exception) = NULL; }
        } else {
            g->retval = ret;
        }
    } else {
        ex_status st = zend_execute_function_body(g->frame->func, &rv);
        if (st == EX_RETURN) g->retval = rv;
        if (EG(exception)) { g->pending_exception = EG(exception); EG(exception) = NULL; }
    }

    g->finished = 1;
    g->has_current = 0;
    g->state = GEN_DONE;
    g->fiber_status = ZEND_FIBER_TERMINATED;
    swapcontext(&g->ctx, &g->ret_ctx);
}

/* Run the coroutine until it suspends or finishes.  1 = an exception escaped. */
static int coroutine_enter(zend_generator *g)
{
    zend_execute_data *saved_cur  = EG(current);
    zend_string       *saved_file = EG(filename);
    uint32_t           saved_line = EG(lineno);
    zend_object       *saved_exc  = EG(exception);
    int                saved_depth = zend_call_depth;
    int                saved_break = zend_break_levels;
    zend_string       *saved_goto = zend_goto_target;
    zend_generator    *saved_fiber = g_current_fiber;

    EG(exception) = NULL;
    zend_break_levels = 0;
    zend_goto_target = NULL;

    if (!g->started) {
        g->started = 1;
        g->stack = emalloc(GEN_STACK_SIZE);
        getcontext(&g->ctx);
        g->ctx.uc_stack.ss_sp = g->stack;
        g->ctx.uc_stack.ss_size = GEN_STACK_SIZE;
        g->ctx.uc_link = &g->ret_ctx;
        g_entering = g;
        EG(current) = g->frame;
        EG(filename) = g->frame ? g->frame->filename : saved_file;
        zend_call_depth = 0;
        makecontext(&g->ctx, generator_trampoline, 0);
    } else {
        EG(current) = g->gen_current;
        EG(filename) = g->gen_filename;
        EG(lineno) = g->gen_lineno;
        zend_call_depth = g->gen_depth;
    }

    g->state = GEN_RUNNING;
    if (g->running_fiber) { g->fiber_status = ZEND_FIBER_STARTED | ZEND_FIBER_RUNNING; g_current_fiber = g; }
    swapcontext(&g->ret_ctx, &g->ctx);

    EG(current)  = saved_cur;
    EG(filename) = saved_file;
    EG(lineno)   = saved_line;
    zend_call_depth = saved_depth;
    zend_break_levels = saved_break;
    zend_goto_target = saved_goto;
    g_current_fiber = saved_fiber;

    if (g->pending_exception) {
        zend_object *e = g->pending_exception;
        g->pending_exception = NULL;
        EG(exception) = e;
        return 1;
    }
    EG(exception) = saved_exc;
    return EG(exiting) ? 1 : 0;
}

/* Suspend the running coroutine and hand control back to its caller. */
static void coroutine_suspend(zend_generator *g)
{
    g->gen_current  = EG(current);
    g->gen_filename = EG(filename);
    g->gen_lineno   = EG(lineno);
    g->gen_depth    = zend_call_depth;
    g->state = GEN_SUSPENDED;
    if (g->running_fiber) g->fiber_status = ZEND_FIBER_STARTED | ZEND_FIBER_SUSPENDED;
    swapcontext(&g->ctx, &g->ret_ctx);
    /* resumed */
    EG(current)  = g->gen_current;
    EG(filename) = g->gen_filename;
    EG(lineno)   = g->gen_lineno;
    zend_call_depth = g->gen_depth;
    g->state = GEN_RUNNING;
    if (g->running_fiber) g->fiber_status = ZEND_FIBER_STARTED | ZEND_FIBER_RUNNING;
}

/* ------------------------------------------------------------- lifecycle */

zend_object *zend_generator_create(zend_execute_data *frame)
{
    zend_class_entry *ce = generator_ce();
    if (!ce) { zend_throw_error("Error", "Generator class is not registered"); return NULL; }
    zend_object *obj = zend_object_new(ce);
    zend_generator *g = ecalloc(1, sizeof(zend_generator));
    g->obj = obj;
    g->frame = frame;
    g->state = GEN_INIT;
    g->current_key = zval_null();
    g->current_val = zval_null();
    g->send_value = zval_null();
    g->retval = zval_null();
    g->fiber_callable = zval_null();
    g->fiber_transfer = zval_null();
    if (frame) frame->generator = g;
    obj->internal = g;
    return obj;
}

int zend_generator_ensure_started(zend_generator *g)
{
    if (!g) return 0;
    if (g->started || g->finished) return 0;
    return coroutine_enter(g);
}

int zend_generator_valid(zend_generator *g)
{
    return g && !g->finished && g->has_current;
}

zval zend_generator_current(zend_generator *g)
{
    return (g && g->has_current) ? g->current_val : zval_null();
}

zval zend_generator_key(zend_generator *g)
{
    return (g && g->has_current) ? g->current_key : zval_null();
}

static int generator_closed_error(zend_generator *g)
{
    if (g->state == GEN_RUNNING) {
        zend_throw_error("Exception", "Cannot resume an already running generator");
        return 1;
    }
    return 0;
}

int zend_generator_next(zend_generator *g)
{
    if (!g) return 0;
    if (zend_generator_ensure_started(g)) return 1;
    if (g->finished) return 0;
    if (generator_closed_error(g)) return 1;
    g->send_value = zval_null();
    g->advanced = 1;
    return coroutine_enter(g);
}

int zend_generator_send(zend_generator *g, zval value, zval *retval)
{
    if (retval) *retval = zval_null();
    if (!g) return 0;
    if (!g->started) { if (coroutine_enter(g)) return 1; }
    if (g->finished) return 0;
    if (generator_closed_error(g)) return 1;
    g->send_value = value;
    g->advanced = 1;
    if (coroutine_enter(g)) return 1;
    if (retval) *retval = zend_generator_current(g);
    return 0;
}

int zend_generator_throw(zend_generator *g, zend_object *ex, zval *retval)
{
    if (retval) *retval = zval_null();
    if (!g) return 0;
    if (!g->started) {
        /* an unstarted generator is primed first, exactly as php-src does */
        if (coroutine_enter(g)) return 1;
    }
    if (g->finished) {
        zend_throw_exception_object(ex);
        return 1;
    }
    if (generator_closed_error(g)) return 1;
    g->throw_in = ex;
    g->advanced = 1;
    if (coroutine_enter(g)) return 1;
    if (retval) *retval = zend_generator_current(g);
    return 0;
}

int zend_generator_rewind(zend_generator *g)
{
    if (!g) return 0;
    if (!g->started) return coroutine_enter(g);
    if (g->advanced || g->finished) {
        zend_throw_error("Exception", "Cannot rewind a generator that was already run");
        return 1;
    }
    return 0;
}

/* foreach and "yield from" refuse a generator that has already run out. */
int zend_generator_ensure_traversable(zend_generator *g)
{
    if (!g) return 0;
    if (!g->started) return coroutine_enter(g);
    if (g->finished) {
        zend_throw_error("Exception", "Cannot traverse an already closed generator");
        return 1;
    }
    return 0;
}

int zend_generator_get_return(zend_generator *g, zval *retval)
{
    *retval = zval_null();
    if (!g) return 0;
    if (!g->finished) {
        zend_throw_error("Exception",
                         "Cannot get return value of a generator that hasn't returned");
        return 1;
    }
    *retval = g->retval;
    return 0;
}

/* ------------------------------------------------------------- yielding */

int zend_generator_do_yield(zend_generator *g, zval key, int have_key, zval value, zval *sent)
{
    if (sent) *sent = zval_null();
    if (!g) return 1;
    if (have_key) {
        g->current_key = key;
        zval *k = zval_deref(&key);
        /* a delegated key (have_key == 2) must not move the automatic counter */
        if (have_key == 1 && k->u1 == IS_LONG && k->value.lval >= g->auto_key)
            g->auto_key = k->value.lval + 1;
    } else {
        g->current_key = zval_long(g->auto_key++);
    }
    g->current_val = value;
    g->has_current = 1;

    coroutine_suspend(g);

    if (g->throw_in) {
        zend_object *e = g->throw_in;
        g->throw_in = NULL;
        zend_throw_exception_object(e);
        return 1;
    }
    if (sent) *sent = g->send_value;
    g->send_value = zval_null();
    return 0;
}

/* VERSIONGATE:70000 "yield from" is a 7.0 feature; its only caller,
 * eval_yield_from() in zend_execute.c, carries the same marker. */
int zend_generator_do_yield_from(zend_generator *g, zval src, zval *result)
{
    *result = zval_null();
    zval *s = zval_deref(&src);

    if (s->u1 == IS_ARRAY) {
        HashTable *ht = s->value.arr;
        ht->refcount++;
        for (uint32_t i = 0; i < ht->nNumUsed; i++) {
            Bucket *b = &ht->arData[i];
            if (Z_TYPE(b->val) == IS_UNDEF) continue;
            zval k = b->key ? zval_zstr(b->key) : zval_long((zend_long)b->h);
            zval sent = zval_null();
            if (zend_generator_do_yield(g, k, 2, zval_copy(*zval_deref(&b->val)), &sent)) {
                ht->refcount--;
                return 1;
            }
        }
        ht->refcount--;
        return 0;
    }

    if (s->u1 == IS_OBJECT) {
        zend_generator *inner = zend_generator_from_object(s->value.obj);
        if (inner) {
            if (zend_generator_ensure_traversable(inner)) return 1;
            while (zend_generator_valid(inner)) {
                zval sent = zval_null();
                if (zend_generator_do_yield(g, zend_generator_key(inner), 2,
                                            zend_generator_current(inner), &sent))
                    return 1;
                zval dummy = zval_null();
                if (zend_generator_send(inner, sent, &dummy)) return 1;
            }
            *result = inner->retval;
            return 0;
        }
        /* any other Traversable */
        zend_object *obj = s->value.obj;
        if (zend_ce_aggregate && zend_instanceof(obj->ce, zend_ce_aggregate)) {
            zend_function *gi = zend_lookup_method(obj->ce, zend_string_initz("getiterator"));
            if (gi) {
                zval it = zval_null();
                if (zend_call_function_ex(gi, obj, obj->ce, 0, NULL, NULL, &it, NULL)) return 1;
                return zend_generator_do_yield_from(g, it, result);
            }
        }
        zend_function *rew = zend_lookup_method(obj->ce, zend_string_initz("rewind"));
        if (rew) {
            zval t = zval_null();
            if (zend_call_function_ex(rew, obj, obj->ce, 0, NULL, NULL, &t, NULL)) return 1;
            while (1) {
                zend_function *vf = zend_lookup_method(obj->ce, zend_string_initz("valid"));
                zval ok = zval_null();
                if (!vf || zend_call_function_ex(vf, obj, obj->ce, 0, NULL, NULL, &ok, NULL)) return 1;
                if (!zval_is_true(zval_deref(&ok))) break;
                zend_function *cf = zend_lookup_method(obj->ce, zend_string_initz("current"));
                zend_function *kf = zend_lookup_method(obj->ce, zend_string_initz("key"));
                zval cur = zval_null(), k = zval_null();
                if (cf && zend_call_function_ex(cf, obj, obj->ce, 0, NULL, NULL, &cur, NULL)) return 1;
                if (kf && zend_call_function_ex(kf, obj, obj->ce, 0, NULL, NULL, &k, NULL)) return 1;
                zval sent = zval_null();
                if (zend_generator_do_yield(g, *zval_deref(&k), 2, *zval_deref(&cur), &sent)) return 1;
                zend_function *nf = zend_lookup_method(obj->ce, zend_string_initz("next"));
                zval t2 = zval_null();
                if (nf && zend_call_function_ex(nf, obj, obj->ce, 0, NULL, NULL, &t2, NULL)) return 1;
            }
            return 0;
        }
    }

    zend_throw_error("Error", "Can use \"yield from\" only with arrays and Traversables");
    return 1;
}

/* ------------------------------------------------------------ the methods */

static zend_generator *this_generator(void)
{
    zend_object *o = zend_get_this();
    return zend_generator_from_object(o);
}

static void zim_gen_current(zval *r, uint32_t c, zval *v)
{
    (void)c; (void)v;
    zend_generator *g = this_generator();
    *r = zval_null();
    if (!g) return;
    if (zend_generator_ensure_started(g)) return;
    *r = zval_copy(zend_generator_current(g));
}

static void zim_gen_key(zval *r, uint32_t c, zval *v)
{
    (void)c; (void)v;
    zend_generator *g = this_generator();
    *r = zval_null();
    if (!g) return;
    if (zend_generator_ensure_started(g)) return;
    *r = zval_copy(zend_generator_key(g));
}

static void zim_gen_next(zval *r, uint32_t c, zval *v)
{
    (void)c; (void)v;
    zend_generator *g = this_generator();
    *r = zval_null();
    if (g) zend_generator_next(g);
}

static void zim_gen_send(zval *r, uint32_t c, zval *v)
{
    zend_generator *g = this_generator();
    *r = zval_null();
    if (!g || c < 1) return;
    zend_generator_send(g, zval_copy(*zval_deref(&v[0])), r);
}

static void zim_gen_valid(zval *r, uint32_t c, zval *v)
{
    (void)c; (void)v;
    zend_generator *g = this_generator();
    *r = zval_bool(0);
    if (!g) return;
    if (zend_generator_ensure_started(g)) return;
    *r = zval_bool(zend_generator_valid(g));
}

static void zim_gen_rewind(zval *r, uint32_t c, zval *v)
{
    (void)c; (void)v;
    zend_generator *g = this_generator();
    *r = zval_null();
    if (g) zend_generator_rewind(g);
}

static void zim_gen_getreturn(zval *r, uint32_t c, zval *v)
{
    (void)c; (void)v;
    zend_generator *g = this_generator();
    *r = zval_null();
    if (g) zend_generator_get_return(g, r);
}

static void zim_gen_throw(zval *r, uint32_t c, zval *v)
{
    zend_generator *g = this_generator();
    *r = zval_null();
    if (!g || c < 1) return;
    zval *e = zval_deref(&v[0]);
    /* VERSIONGATE:70000 else-branch is the 5.6 wording (Exception, not Throwable) */
    int ok = e->u1 == IS_OBJECT &&
             (EG(php_version) >= 70000
                ? (zend_ce_throwable && zend_instanceof(e->value.obj->ce, zend_ce_throwable))
                : (zend_ce_exception && zend_instanceof(e->value.obj->ce, zend_ce_exception)));
    if (!ok) {
        if (EG(php_version) >= 70000)
            zend_throw_error("Error", "Can only throw objects");
        else
            zend_fatal("Exceptions must be valid objects derived from the Exception base class");
        return;
    }
    zend_generator_throw(g, e->value.obj, r);
}


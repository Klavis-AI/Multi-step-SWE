/* zend_exceptions.h -- the built-in class hierarchy.
 *
 * Zend/zend_exceptions.c builds every engine class as a real
 * zend_class_entry with internal methods, and publishes the class entries
 * here so the evaluator and the extensions can reach them (instanceof checks,
 * throwing, Closure/Generator/Fiber payloads, ...).
 *
 * zend_throw_error() and zend_throw_exception_object() live in the frozen
 * Zend/zend_API.h; everything else an outside module might need is below.
 */
#ifndef ZEND_EXCEPTIONS_H
#define ZEND_EXCEPTIONS_H

#include "zend.h"
#include "zend_ast.h"
#include "zend_API.h"

/* ------------------------------------------------------------- interfaces */
extern zend_class_entry *zend_ce_traversable;
extern zend_class_entry *zend_ce_iterator;
extern zend_class_entry *zend_ce_aggregate;         /* IteratorAggregate      */
extern zend_class_entry *zend_ce_arrayaccess;
extern zend_class_entry *zend_ce_countable;
extern zend_class_entry *zend_ce_serializable;
extern zend_class_entry *zend_ce_jsonserializable;
extern zend_class_entry *zend_ce_throwable;         /* 7.0+                   */
extern zend_class_entry *zend_ce_stringable;        /* 8.0+                   */
extern zend_class_entry *zend_ce_unit_enum;         /* 8.1+                   */
extern zend_class_entry *zend_ce_backed_enum;       /* 8.1+                   */

/* ------------------------------------------------------- the Throwable tree */
extern zend_class_entry *zend_ce_exception;
extern zend_class_entry *zend_ce_error_exception;
extern zend_class_entry *zend_ce_error;              /* 7.0+ */
extern zend_class_entry *zend_ce_type_error;         /* 7.0+ */
extern zend_class_entry *zend_ce_value_error;        /* 8.0+ */
extern zend_class_entry *zend_ce_argument_count_error;   /* 7.1+ */
extern zend_class_entry *zend_ce_arithmetic_error;   /* 7.0+ */
extern zend_class_entry *zend_ce_division_by_zero_error; /* 7.0+ */
extern zend_class_entry *zend_ce_assertion_error;    /* 7.0+ */
extern zend_class_entry *zend_ce_unhandled_match_error;  /* 8.0+ */
extern zend_class_entry *zend_ce_json_exception;     /* 7.3+ */
extern zend_class_entry *zend_ce_runtime_exception;
extern zend_class_entry *zend_ce_logic_exception;

/* ------------------------------------------------------------ other shells */
extern zend_class_entry *zend_standard_class_def;    /* stdClass */
extern zend_class_entry *zend_ce_closure;
extern zend_class_entry *zend_ce_generator;
extern zend_class_entry *zend_ce_weakref;            /* 7.4+ */
extern zend_class_entry *zend_ce_weakmap;            /* 8.0+ */
extern zend_class_entry *zend_ce_fiber;              /* 8.1+ */
extern zend_class_entry *zend_ce_fiber_error;        /* 8.1+ */

/* ------------------------------------------------- building engine classes
 *
 * These are the same helpers zend_exceptions.c uses for itself; an extension
 * that wants to publish a class of its own should use them so that the class
 * ends up shaped exactly like the built-in ones (flattened default
 * properties, magic-method pointers filled in, registered under its lowercased
 * name).  All of them are safe to call before the first script runs and only
 * then. */
zend_class_entry *zend_register_internal_class(const char *name,
                                               zend_class_entry *parent,
                                               uint32_t flags);
zend_class_entry *zend_register_internal_interface(const char *name);

zend_function *zend_class_add_method(zend_class_entry *ce, const char *name,
                                     void (*handler)(zval *ret, uint32_t argc, zval *argv),
                                     uint32_t min_args, uint32_t num_args,
                                     uint32_t flags);
/* Name an internal function's parameters from a comma separated list
 * ("message,code,previous").  Named arguments, Reflection and the inheritance
 * signature check all read these, so a method that a script can call by name
 * should have them. */
void zend_function_set_arg_names(zend_function *fn, const char *names);

void zend_class_add_property(zend_class_entry *ce, const char *name,
                             uint32_t flags, zval default_value);
void zend_class_add_constant(zend_class_entry *ce, const char *name,
                             zval value, uint32_t flags);
void zend_class_add_interface(zend_class_entry *ce, zend_class_entry *iface);

/* Build and register every engine class.  main/php_registry.c's
 * zend_register_classes() is a thin wrapper around this. */
void zend_register_default_classes(void);

/* The object an internal method was called on, i.e. $this.  Reads the top of
 * the zend_execute_data chain; NULL in a static or free function. */
zend_object *zend_get_this(void);

/* -------------------------------------------------- reading an exception
 *
 * Bypasses the visibility layer on purpose: these read the declared slot no
 * matter which class in the chain declared it. */
zend_string *zend_exception_get_message(zend_object *ex);
zend_string *zend_exception_get_file(zend_object *ex);
zend_long    zend_exception_get_line(zend_object *ex);
zend_long    zend_exception_get_code(zend_object *ex);
zend_object *zend_exception_get_previous(zend_object *ex);

/* php-src's Exception::getTraceAsString() text, without a trailing newline. */
zend_string *zend_exception_trace_as_string(zend_object *ex);

/* php-src's Exception::__toString() text.  Calls the object's own __toString
 * when the class overrides it, so a user subclass wins, exactly as
 * zend_exception_error() does.  Never returns NULL. */
zend_string *zend_exception_to_string(zend_object *ex);

/* Snapshot the call stack into the exception's private `trace` slot.  Called
 * by Exception::__construct; an engine-thrown exception that never runs a
 * constructor should call it too.  Leading Throwable constructor frames are
 * skipped so the trace starts where php-src's does. */
void zend_exception_capture_trace(zend_object *ex);

/* zend_throw_error() with a code: identical in every other way, but the value
 * also lands in Exception::$code.  ext/json uses it so that an exception
 * raised under JSON_THROW_ON_ERROR carries its JSON_ERROR_* status. */
zend_object *zend_throw_error_code(const char *class_name, zend_long code,
                                   const char *fmt, ...);

/* Fill in message / code / previous / file / line the way
 * Exception::__construct does.  `message` and `previous` may be NULL. */
void zend_exception_init(zend_object *ex, zend_string *message, zend_long code,
                         zend_object *previous);

/* ------------------------------------------------------------------ Fiber
 *
 * 8.1's Fiber is a class shell here: the coroutine mechanics belong to the
 * evaluator, which implements these. */
int          zend_fiber_create(zend_object *fiber, zval *callable);
int          zend_fiber_start(zend_object *fiber, uint32_t argc, zval *argv, zval *retval);
int          zend_fiber_resume(zend_object *fiber, zval *value, zval *retval);
int          zend_fiber_throw(zend_object *fiber, zval *exception, zval *retval);
int          zend_fiber_get_return(zend_object *fiber, zval *retval);
int          zend_fiber_suspend(zval *value, zval *retval);
zend_object *zend_fiber_get_current(void);

/* Fiber state, as the four is*() methods report it. */
#define ZEND_FIBER_STARTED     (1u << 0)
#define ZEND_FIBER_SUSPENDED   (1u << 1)
#define ZEND_FIBER_RUNNING     (1u << 2)
#define ZEND_FIBER_TERMINATED  (1u << 3)
uint32_t zend_fiber_status(zend_object *fiber);

#endif /* ZEND_EXCEPTIONS_H */

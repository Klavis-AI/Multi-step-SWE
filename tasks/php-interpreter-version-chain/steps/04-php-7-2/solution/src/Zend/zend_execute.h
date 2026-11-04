/* zend_execute.h -- what the AST evaluator publishes to the rest of the tree.
 *
 * The evaluation entry points themselves (zend_eval_expr, zend_get_lval,
 * zend_execute_ast, zend_execute_script, zend_call_function_ex,
 * zend_call_user_callable, zend_is_callable_zval) are declared in the frozen
 * Zend/zend_API.h; this header only adds the evaluator-private services that
 * the generator module, the object module and ext/ need to reach.
 */
#ifndef ZEND_EXECUTE_H
#define ZEND_EXECUTE_H

#include "zend.h"
#include "zend_ast.h"
#include "zend_API.h"

/* ------------------------------------------------------------ module state */

/* How many enclosing loops a pending break/continue still has to unwind.
 * Set when zend_execute_ast() returns EX_BREAK or EX_CONTINUE and decremented
 * by every loop that the status passes through. */
extern int zend_break_levels;

/* Label a pending EX_GOTO is looking for; NULL when no goto is in flight. */
extern zend_string *zend_goto_target;
extern uint32_t zend_goto_lineno;

/* Nesting depth of user function calls; the evaluator refuses to go deeper
 * than ZEND_MAX_CALL_DEPTH so that runaway recursion reports instead of
 * smashing the C stack. */
extern int zend_call_depth;
#define ZEND_MAX_CALL_DEPTH 3000

/* ------------------------------------------------------------- small utils */

/* Keep EG(lineno) in step with the node about to be evaluated. */
void zend_set_line(zend_ast *ast);

/* Resolve a class expression ("Foo", self, parent, static, an expression that
 * yields a name or an object).  *err is set when a diagnostic was raised. */
zend_class_entry *zend_resolve_class_ast(zend_ast *ast, int *err);

/* The class whose body is currently executing (self), and the late static
 * binding class (static).  zend_get_current_scope() is declared in
 * zend_object.h as well; the evaluator owns the definition. */
zend_class_entry *zend_get_current_scope(void);
zend_class_entry *zend_get_called_scope(void);

/* Turn a storage slot into a reference box, returning it.  Used by "global",
 * "static", =& and by-reference argument binding. */
zend_reference *zend_make_reference(zval *slot);

/* Copy-on-write split of an array held in *slot (follows a reference box). */
void zend_separate_slot(zval *slot);

/* ------------------------------------------------------------ declarations */

/* Bind a parsed function/class declaration into the run-time tables. */
void zend_declare_function(zend_function *fn);
/* zend_declare_class() is declared in zend_object.h. */

/* Hoist the unconditional top-level function and class declarations of a
 * statement list, the way php-src's compiler does at the end of compilation. */
void zend_hoist_declarations(zend_ast *stmts);

/* Resolve every goto/break/continue in a freshly parsed tree, the way php-src's
 * compiler does, so the diagnostics appear even for functions never called. */
void zend_validate_jumps(zend_ast *root);

/* Which trait a method was copied from, for __TRAIT__. */
zend_class_entry *zend_trait_of_function(zend_function *fn);

/* ----------------------------------------------------------------- closures */

typedef struct _zend_closure {
    zend_function    *fn;
    zend_class_entry *scope;         /* the "self" the body sees             */
    zend_class_entry *called_scope;  /* late static binding                  */
    zend_object      *This;          /* bound $this, or NULL                 */
    HashTable        *bound;         /* use()/arrow captures, name => zval   */
    unsigned char     is_static;
    unsigned char     is_arrow;
    uint32_t          running;   /* active invocations, for the "active lambda" check */
} zend_closure;

zend_object      *zend_create_closure(zend_function *fn, zend_class_entry *scope,
                                      zend_class_entry *called_scope,
                                      zend_object *This, HashTable *bound,
                                      int is_static, int is_arrow);
/* Closure::bind / bindTo / call / fromCallable, and calling one. */
zend_object *zend_closure_bind(zend_object *closure, zval *newthis,
                               zval *scope_arg, zend_string **err);
int  zend_closure_call(zend_object *closure, uint32_t argc, zval *argv,
                       zend_string **names, zval *retval, zval **arg_slots);

/* --------------------------------------------------------------- generators */

typedef struct _zend_generator zend_generator;

/* Create the Generator object for a call to a generator function; the frame is
 * fully built (parameters bound) but the body has not run. */
zend_object *zend_generator_create(zend_execute_data *frame);
/* Drive a generator far enough that current()/key() are valid. */
int  zend_generator_ensure_started(zend_generator *g);
/* Same, but refuses a generator that has already run to completion, which is
 * what foreach and yield from have to do. */
int  zend_generator_ensure_traversable(zend_generator *g);
int  zend_generator_next(zend_generator *g);
int  zend_generator_send(zend_generator *g, zval value, zval *retval);
int  zend_generator_throw(zend_generator *g, zend_object *ex, zval *retval);
int  zend_generator_rewind(zend_generator *g);
int  zend_generator_valid(zend_generator *g);
zval zend_generator_current(zend_generator *g);
zval zend_generator_key(zend_generator *g);
int  zend_generator_get_return(zend_generator *g, zval *retval);
zend_generator *zend_generator_from_object(zend_object *obj);
/* The user function a generator is running. */
zend_function *zend_generator_function(zend_generator *g);
/* Called from the evaluator when it meets AST_YIELD / AST_YIELD_FROM. */
int  zend_generator_do_yield(zend_generator *g, zval key, int have_key,
                             zval value, zval *sent);
int  zend_generator_do_yield_from(zend_generator *g, zval src, zval *result);
/* Does this function body contain a yield?  Cached in fn->flags. */
int  zend_function_is_generator(zend_function *fn);
/* The evaluator's function-body runner, used by the generator trampoline. */
ex_status zend_execute_function_body(zend_function *fn, zval *retval);

/* --------------------------------------------------------------- arg binding */
/* Bind evaluated arguments into a fresh frame; shared with the generator
 * module so that a generator call builds the same frame a plain call does. */
zend_execute_data *zend_build_frame(zend_function *fn, zend_object *This,
                                    zend_class_entry *called_scope,
                                    uint32_t argc, zval *argv,
                                    zend_string **argnames, zval **arg_slots,
                                    int *failed);

#endif /* ZEND_EXECUTE_H */

/* zend_parse.h -- parser side contract.
 *
 * Owned by the parser module (Zend/zend_parse.c, Zend/zend_ast.c).
 * The evaluator includes this to reach the bits of a declaration that do not
 * fit into the frozen zend_function / zend_class_entry structures.
 *
 * The entry points themselves (php_parse_file / php_parse_string) are declared
 * in zend_API.h; this header only adds what the parser hands over on the side.
 */
#ifndef ZEND_PARSE_H
#define ZEND_PARSE_H

#include "zend.h"
#include "zend_ast.h"

/* Line of the token that made the last parse fail (php-src reports the line of
 * the offending token, not of the enclosing statement). */
extern uint32_t php_parse_error_lineno;

/* eval()'d code is not a file: php-src's zend_compile_string() starts the
 * scanner *inside* PHP, so eval("echo 1;") is code and not inline HTML.
 * php_parse_string() keeps the file behaviour (it waits for "<?php"), which is
 * what the CLI needs for stdin and for the "<?php " it prepends to -r.
 * eval() must call this one instead. */
zend_ast *php_parse_eval_string(const char *code, size_t len, zend_string *filename,
                                zend_string **err);

/* ====================================================================
 *  Pointers inside a zval
 * ====================================================================
 * There is no pointer zval type, so wherever the AST or a HashTable has to
 * carry a C pointer the agreed convention is an IS_LONG zval holding the
 * pointer value.  These two macros are the only blessed way to do it.
 *
 *   AST_FUNC_DECL / AST_CLOSURE / AST_ARROW_FN  ->  val holds zend_function*
 *   AST_CLASS_DECL / AST_ANON_CLASS             ->  val holds zend_class_entry*
 *   ce->function_table     lcname => zend_function*
 *   ce->constants_table    name   => zend_class_constant*
 *   ce->properties_info    name   => zend_property_info*
 *   fn->attributes / ce->attributes   name => zend_ast* (AST_LIST of args)
 */
#define ZEND_PTR_ZVAL(p)   zend_parse_ptr_zval((void *)(p))
#define ZEND_ZVAL_PTR(z)   ((void *)(intptr_t)(z).value.lval)
#define ZEND_ZVAL_PTR_P(z) ((void *)(intptr_t)(z)->value.lval)

static inline zval zend_parse_ptr_zval(void *p)
{
    zval v;
    v.u1 = IS_LONG;
    v.value.lval = (zend_long)(intptr_t)p;
    return v;
}

/* ====================================================================
 *  Side data for a class declaration
 * ====================================================================
 * The class table does not exist at parse time, so parent/interface/trait
 * links are kept as names and resolved by the evaluator when it runs the
 * AST_CLASS_DECL statement.
 *
 * `cases`    : NULL, or an AST_LIST whose children are AST_CONST_ELEM nodes
 *              (child0 = AST_ZVAL case name, child1 = backing value or NULL,
 *               child2 = AST_LIST of attributes or NULL).
 * `trait_adaptations` :
 *              NULL, or an AST_LIST of AST_LIST nodes, distinguished by attr:
 *                attr == 1  "insteadof":
 *                    child0 = trait name (AST_ZVAL) or NULL
 *                    child1 = method name (AST_ZVAL)
 *                    child2 = AST_LIST of excluded trait names (AST_ZVAL)
 *                attr == 2  "as" (alias and/or visibility change):
 *                    child0 = trait name (AST_ZVAL) or NULL
 *                    child1 = method name (AST_ZVAL)
 *                    child2 = new name (AST_ZVAL) or NULL
 *                    child3 = AST_ZVAL long with the new ZEND_ACC_* visibility
 *                             (0 when only the name changes)
 */
typedef struct {
    zend_string  *parent_name;
    zend_string **iface_names;
    uint32_t      num_ifaces;
    zend_string **trait_names;
    uint32_t      num_traits;
    zend_ast     *trait_adaptations;
    zend_ast     *cases;
} zend_class_decl_extra;

zend_class_decl_extra *zend_class_decl_get_extra(zend_class_entry *ce);
void zend_class_decl_set_extra(zend_class_entry *ce, zend_class_decl_extra *extra);

/* ====================================================================
 *  Node conventions the frozen zend_ast.h could not spell out
 * ==================================================================== */

/* AST_CAST attr: the zend_type_t the expression is cast to.  zend.h has no
 * _IS_BOOL, so (bool) uses IS_TRUE and (unset) uses IS_NULL. */
#define ZEND_CAST_BOOL     IS_TRUE
#define ZEND_CAST_UNSET    IS_NULL

/* AST_INCLUDE attr */
#define ZEND_INCLUDE_PLAIN        0
#define ZEND_INCLUDE_ONCE_        1
#define ZEND_REQUIRE_PLAIN        2
#define ZEND_REQUIRE_ONCE_        3
#define ZEND_INCLUDE_EVAL         4   /* eval() rides the same node */

/* Name resolution hints, stored in the attr of the AST_ZVAL / AST_CONST node
 * that carries a name.  A leading "\" is stripped from the stored string. */
#define ZEND_NAME_NOT_FQ    0
#define ZEND_NAME_FQ        1   /* source had a leading "\"                  */
#define ZEND_NAME_RELATIVE  2   /* source started with "namespace\"          */

/* AST_USE attr: what kind of import (or namespace declaration) this is. */
#define ZEND_USE_CLASS      0
#define ZEND_USE_FUNCTION   1
#define ZEND_USE_CONST      2
#define ZEND_USE_NAMESPACE  3   /* a namespace declaration, not an import    */
/* AST_USE layout: val = prefix/namespace name (IS_STRING, may be IS_UNDEF),
 * children = AST_CONST_ELEM(child0 = name, child1 = alias or NULL,
 *                           child2 = AST_ZVAL long ZEND_USE_* for group use) */

/* AST_ARRAY_ELEM attr bits (also used for AST_LIST_ASSIGN children). */
#define ZEND_ARRAY_ELEM_BY_REF  (1u << 0)
#define ZEND_ARRAY_ELEM_SPREAD  (1u << 1)

/* AST_NEW attr bit0: the expression was written "(new X)".  A parenthesised
 * "new" has always been dereferenceable; the bare "new X()->y" spelling is the
 * 8.4 addition. */
#define ZEND_NEW_PARENTHESIZED  (1u << 0)

/* AST_INC_DEC attr bits. */
#define ZEND_INCDEC_DEC         (1u << 0)
#define ZEND_INCDEC_POST        (1u << 1)

/* AST_FOREACH attr bits. */
#define ZEND_FOREACH_BY_REF     (1u << 0)

/* AST_LIST_ASSIGN attr: which spelling the source used. */
#define ZEND_LIST_SYNTAX_SHORT  0   /* [$a, $b] = ...     */
#define ZEND_LIST_SYNTAX_LIST   1   /* list($a, $b) = ... */

/* A first-class callable, "f(...)": the call node (AST_CALL /
 * AST_METHOD_CALL / AST_STATIC_CALL) gets an AST_FIRST_CLASS_CALLABLE node in
 * place of its argument list. */

/* Closures: AST_CLOSURE child0 is NULL, or an AST_LIST of AST_ZVAL strings
 * naming the "use (...)" variables; attr bit0 of each element is set when the
 * variable is bound by reference.  AST_ARROW_FN has no such list. */

/* AST_MAGIC_CONST attr is the lexer's token type (T_LINE, T_FILE, ...) so the
 * evaluator can switch on the same enum the lexer publishes. */

#endif /* ZEND_PARSE_H */

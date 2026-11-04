/* zend_ast.h — the AST contract between the parser and the evaluator. */
#ifndef ZEND_AST_H
#define ZEND_AST_H
#include "zend.h"

typedef enum {
    /* leaves */
    AST_ZVAL,        /* val = literal                                        */
    AST_VAR,         /* child0 = name expr (AST_ZVAL string or expr)         */
    AST_CONST,       /* val = constant name                                  */
    AST_MAGIC_CONST, /* attr = T_LINE/T_FILE/T_DIR/T_FUNCTION/T_CLASS/...    */
    AST_LIST,        /* generic list of children                             */

    /* expressions */
    AST_BINARY,      /* attr = BIN_*, child0, child1                         */
    AST_UNARY,       /* attr = UN_*, child0                                  */
    AST_ASSIGN,      /* child0 = target, child1 = value                      */
    AST_ASSIGN_REF,  /* child0 = target, child1 = value                      */
    AST_ASSIGN_OP,   /* attr = BIN_*, child0, child1                         */
    AST_ASSIGN_COALESCE, /* ??= (7.4)                                        */
    AST_INC_DEC,     /* attr: bit0 dec, bit1 post                            */
    AST_CAST,        /* attr = IS_* target type                              */
    AST_ISSET,       /* child0..n = vars                                     */
    AST_EMPTY,       /* child0                                               */
    AST_UNSET,       /* children                                             */
    AST_TERNARY,     /* child0, child1 (may be NULL => ?:), child2           */
    AST_COALESCE,    /* child0 ?? child1                                     */
    AST_AND, AST_OR, /* short-circuit                                        */
    AST_ARRAY,       /* children = AST_ARRAY_ELEM                            */
    AST_ARRAY_ELEM,  /* child0 = value (NULL for skipped), child1 = key,
                        attr bit0 = by-ref, bit1 = spread                    */
    AST_DIM,         /* child0 = container, child1 = offset (NULL = append)  */
    AST_PROP,        /* child0 = object, child1 = name expr                  */
    AST_NULLSAFE_PROP,
    AST_STATIC_PROP, /* child0 = class expr, child1 = name expr              */
    AST_CLASS_CONST, /* child0 = class expr, child1 = name expr              */
    AST_CALL,        /* child0 = callee expr, child1 = args list             */
    AST_METHOD_CALL, /* child0 = obj, child1 = name, child2 = args           */
    AST_NULLSAFE_METHOD_CALL,
    AST_STATIC_CALL, /* child0 = class, child1 = name, child2 = args         */
    AST_NEW,         /* child0 = class expr or AST_CLASS, child1 = args      */
    AST_CLONE,
    AST_PRINT,
    AST_EXIT,
    AST_SILENCE,     /* @expr                                                */
    AST_INSTANCEOF,  /* child0 = expr, child1 = class expr                   */
    AST_CLOSURE,     /* val = decl pointer (zend_function*)                  */
    AST_ARROW_FN,
    AST_YIELD,       /* child0 = value, child1 = key                         */
    AST_YIELD_FROM,
    AST_THROW_EXPR,  /* throw as expression (8.0)                            */
    AST_MATCH,       /* child0 = subject, child1 = arms                      */
    AST_MATCH_ARM,   /* child0 = conditions list (NULL = default), child1    */
    AST_LIST_ASSIGN, /* list() target: children = AST_ARRAY_ELEM             */
    AST_ENCAPS,      /* interpolated string: children are parts              */
    AST_SHELL_EXEC,
    AST_ANON_CLASS,  /* val = class decl                                     */
    AST_FIRST_CLASS_CALLABLE, /* f(...)                                      */
    AST_NAMED_ARG,   /* val = name, child0 = value                           */
    AST_SPREAD,      /* ...expr in an argument list                          */
    AST_CONST_FETCH_DYNAMIC, /* C::{$x} (8.3)                                */
    AST_PROP_HOOK_PARENT,    /* parent::$prop::get() style access (8.4)      */

    /* statements */
    AST_STMT_LIST,
    AST_ECHO,
    AST_IF,          /* children = AST_IF_ELEM                               */
    AST_IF_ELEM,     /* child0 = cond (NULL for else), child1 = body         */
    AST_WHILE,
    AST_DO_WHILE,
    AST_FOR,         /* child0 = init, 1 = cond, 2 = step, 3 = body          */
    AST_FOREACH,     /* child0 = expr, 1 = value, 2 = key, 3 = body,
                        attr bit0 = by-ref                                   */
    AST_SWITCH,      /* child0 = subject, child1 = cases                     */
    AST_SWITCH_CASE, /* child0 = cond (NULL = default), child1 = body        */
    AST_BREAK,
    AST_CONTINUE,
    AST_RETURN,
    AST_GLOBAL,
    AST_STATIC_VAR,  /* child0 = name, child1 = default                      */
    AST_TRY,         /* child0 = body, child1 = catches, child2 = finally    */
    AST_CATCH,       /* child0 = class list, child1 = var (may be NULL),
                        child2 = body                                        */
    AST_THROW,
    AST_GOTO,
    AST_LABEL,
    AST_FUNC_DECL,   /* val = function pointer                               */
    AST_CLASS_DECL,  /* val = class decl pointer                             */
    AST_CONST_DECL,  /* children = AST_CONST_ELEM                            */
    AST_CONST_ELEM,
    AST_USE,         /* namespace import (parsed, no-op)                     */
    AST_DECLARE,     /* child0 = directives, child1 = body                   */
    AST_INLINE_HTML,
    AST_NOP,
    AST_INCLUDE,     /* attr = 0 include,1 include_once,2 require,3 require_once */
    AST_EXPR_STMT,
    AST_HALT_COMPILER
} zend_ast_kind;

/* binary operators */
typedef enum {
    BIN_ADD, BIN_SUB, BIN_MUL, BIN_DIV, BIN_MOD, BIN_POW,
    BIN_CONCAT,
    BIN_SL, BIN_SR, BIN_BOR, BIN_BAND, BIN_BXOR,
    BIN_IDENTICAL, BIN_NOT_IDENTICAL, BIN_EQUAL, BIN_NOT_EQUAL,
    BIN_SMALLER, BIN_SMALLER_OR_EQUAL, BIN_GREATER, BIN_GREATER_OR_EQUAL,
    BIN_SPACESHIP, BIN_BOOL_XOR
} zend_binop;

/* unary operators */
typedef enum { UN_MINUS, UN_PLUS, UN_NOT, UN_BNOT } zend_unop;

struct _zend_ast {
    uint16_t   kind;
    uint16_t   attr;
    uint32_t   lineno;
    uint32_t   nchild;
    zend_ast **child;
    zval       val;
};

zend_ast *zend_ast_create(zend_ast_kind kind, uint32_t lineno, int n, ...);
zend_ast *zend_ast_create_zval(zval v, uint32_t lineno);
zend_ast *zend_ast_list_add(zend_ast *list, zend_ast *node);
zend_ast *zend_ast_create_list(uint32_t lineno);

/* ------------------------------------------------------------- functions */
typedef struct _zend_type_hint {
    uint32_t n;                 /* number of alternatives in a union         */
    zend_string **names;        /* "int", "Foo", "?Bar" already split        */
    unsigned char nullable;
    unsigned char is_intersection;
    struct _zend_type_hint **groups; /* DNF: intersection groups (8.2)       */
    uint32_t ngroups;
} zend_type_hint;

typedef struct _zend_arg_info {
    zend_string *name;
    zend_type_hint *type;
    zend_ast *default_ast;
    unsigned char by_ref;
    unsigned char variadic;
    uint32_t promote_flags;     /* 8.0 constructor promotion (0 = none)      */
    uint32_t promote_set_flags; /* 8.4 asymmetric visibility on promotion    */
    unsigned char promote_readonly;
} zend_arg_info;

typedef struct _zend_prop_hook {
    zend_ast *get_body;         /* statement list or expression              */
    zend_ast *set_body;
    zend_string *set_param;     /* parameter name of set{}                   */
    zend_type_hint *set_type;
    unsigned char get_is_expr, set_is_expr;
    unsigned char get_by_ref;
    unsigned char get_is_final, set_is_final;
} zend_prop_hook;

struct _zend_function {
    zend_string *name;
    zend_string *lcname;
    uint32_t     flags;
    uint32_t     num_args;
    zend_arg_info *args;
    zend_type_hint *return_type;
    zend_ast    *body;
    zend_class_entry *scope;
    zend_string *filename;
    uint32_t     lineno;
    uint32_t     end_lineno;
    HashTable   *static_vars;
    HashTable   *attributes;      /* name => args (8.0)                      */
    /* internal functions */
    void (*handler)(zval *ret, uint32_t argc, zval *argv);
    uint32_t min_args;            /* required argument count (internal)      */
    unsigned char is_internal;
    unsigned char returns_ref;
    unsigned char is_abstract;
    zend_string *doc_comment;
    struct _zend_function *prototype;
};

struct _zend_class_entry {
    zend_string *name;
    zend_string *lcname;
    uint32_t flags;
    zend_class_entry *parent;
    zend_class_entry **interfaces;
    uint32_t num_interfaces;
    HashTable *function_table;    /* lcname => zend_function*                */
    HashTable *constants_table;   /* name => struct zend_class_constant*     */
    HashTable *properties_info;   /* name => zend_property_info*             */
    HashTable *default_properties;/* mangled name => zval (evaluated)        */
    HashTable *static_members;    /* mangled name => zval                    */
    zend_ast **prop_default_asts;
    zend_function *constructor, *destructor, *clone;
    zend_function *__get, *__set, *__isset, *__unset, *__call, *__callstatic;
    zend_function *__tostring, *__invoke, *__debuginfo, *__set_state, *__serialize;
    HashTable *attributes;
    HashTable *enum_cases;        /* 8.1: name => case object               */
    zend_type_hint *enum_backing_type;
    unsigned char is_enum, is_backed_enum, is_readonly, allow_dynamic_props;
    unsigned char statics_initialized;
    zend_string *filename;
    uint32_t lineno;
    zend_string *doc_comment;
};

typedef struct _zend_class_constant {
    zval value;
    zend_ast *ast;
    uint32_t flags;
    zend_class_entry *ce;
    zend_type_hint *type;   /* 8.3 typed constants */
    unsigned char evaluated;
    zend_string *doc;
} zend_class_constant;

#endif

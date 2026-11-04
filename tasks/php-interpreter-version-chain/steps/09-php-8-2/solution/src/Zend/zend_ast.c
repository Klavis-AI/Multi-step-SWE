/* zend_ast.c -- AST node construction for the from-scratch PHP parser.
 *
 * Owns: zend_ast_create / zend_ast_create_zval / zend_ast_create_list /
 *       zend_ast_list_add, as declared (frozen) in Zend/zend_ast.h.
 *
 * Every node records the line of the *first token of the construct*, which is
 * what php-src reports in diagnostics.
 */

#include "zend_ast.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------- allocator */
/* Zend/zend_alloc.h is owned by another module.  Include it when it exists so
 * that we pick up the real declarations (or macros); otherwise fall back to
 * plain extern declarations that match the php-src signatures, so this file
 * keeps compiling either way. */
#if defined(__has_include)
#  if __has_include("zend_alloc.h")
#    include "zend_alloc.h"
#    define ZEND_AST_HAVE_ALLOC_H 1
#  endif
#endif

#ifndef ZEND_AST_HAVE_ALLOC_H
void *emalloc(size_t size);
void *erealloc(void *ptr, size_t size);
void  efree(void *ptr);
#endif

/* ------------------------------------------------------------- internals */

/* child[] capacity rule: 0 children -> no allocation, otherwise the smallest
 * power of two >= 4 that holds n.  Because the rule is a pure function of
 * nchild we do not need to store a capacity field in zend_ast (the struct is
 * frozen), yet growth stays geometric: 4, 8, 16, 32, ... */
static uint32_t ast_cap_for(uint32_t n)
{
    uint32_t c;

    if (n == 0) {
        return 0;
    }
    c = 4;
    while (c < n) {
        c <<= 1;
    }
    return c;
}

static zend_ast *ast_alloc(zend_ast_kind kind, uint32_t lineno, uint32_t nchild)
{
    zend_ast *ast = (zend_ast *) emalloc(sizeof(zend_ast));

    if (!ast) {
        return NULL;
    }
    ast->kind = (uint16_t) kind;
    ast->attr = 0;
    ast->lineno = lineno;
    ast->nchild = nchild;
    ast->child = NULL;
    ast->val.u1 = IS_UNDEF;
    ast->val.value.lval = 0;

    if (nchild) {
        uint32_t cap = ast_cap_for(nchild);
        ast->child = (zend_ast **) emalloc(cap * sizeof(zend_ast *));
        if (!ast->child) {
            ast->nchild = 0;
            return ast;
        }
        memset(ast->child, 0, cap * sizeof(zend_ast *));
    }
    return ast;
}

/* ---------------------------------------------------------- public API */

zend_ast *zend_ast_create(zend_ast_kind kind, uint32_t lineno, int n, ...)
{
    zend_ast *ast;
    va_list ap;
    int i;

    if (n < 0) {
        n = 0;
    }
    ast = ast_alloc(kind, lineno, (uint32_t) n);
    if (!ast) {
        return NULL;
    }
    if (n > 0 && ast->child) {
        va_start(ap, n);
        for (i = 0; i < n; i++) {
            ast->child[i] = va_arg(ap, zend_ast *);
        }
        va_end(ap);
    }
    return ast;
}

zend_ast *zend_ast_create_zval(zval v, uint32_t lineno)
{
    zend_ast *ast = ast_alloc(AST_ZVAL, lineno, 0);

    if (ast) {
        ast->val = v;
    }
    return ast;
}

zend_ast *zend_ast_create_list(uint32_t lineno)
{
    return ast_alloc(AST_LIST, lineno, 0);
}

/* Append to any node's child[] (not just AST_LIST: the parser builds
 * AST_STMT_LIST, AST_ARRAY, AST_IF, ... the same way). */
zend_ast *zend_ast_list_add(zend_ast *list, zend_ast *node)
{
    uint32_t cap;

    if (!list) {
        return NULL;
    }
    cap = ast_cap_for(list->nchild);
    if (list->nchild + 1u > cap) {
        uint32_t ncap = cap ? cap * 2u : 4u;
        zend_ast **nc;

        if (list->child) {
            nc = (zend_ast **) erealloc(list->child, ncap * sizeof(zend_ast *));
        } else {
            nc = (zend_ast **) emalloc(ncap * sizeof(zend_ast *));
        }
        if (!nc) {
            return list;
        }
        list->child = nc;
    }
    list->child[list->nchild++] = node;
    return list;
}

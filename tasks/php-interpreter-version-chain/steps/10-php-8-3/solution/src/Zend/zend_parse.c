/* zend_parse.c -- hand written recursive descent parser for PHP.
 *
 * Targets the grammar of php-src's Zend/zend_language_parser.y for the *union*
 * of PHP 5.6 ... 8.4, so that later versions only change evaluation and never
 * the shape of the syntax tree.  Expressions use precedence climbing with
 * php-src's own precedence table (see the %left/%right/%precedence block at the
 * top of zend_language_parser.y); everything else is straight recursive
 * descent.
 *
 * The whole token stream is materialised up front (php_lexer_next until
 * T_EOF).  That buys unbounded lookahead, which the grammar genuinely needs in
 * a few places -- telling a type apart from an expression, "&" as an
 * intersection type versus a by-reference marker, and DNF types such as
 * "(A&B)|null".
 *
 * Parse errors unwind with longjmp to the entry point, which turns them into
 * the php-src style message the caller prepends "PHP Parse error:  " to.
 */

#include "zend.h"
#include "zend_ast.h"
#include "zend_API.h"
#include "zend_alloc.h"
#include "zend_lex.h"
#include "zend_parse.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Which PHP release this parser is allowed to accept.  The build passes
 * -DPHP_VERSION_ID; a plain build with no define is the PHP 5.6 parser, so
 * every production added after 5.6 sits behind a VERSIONGATE guard and can be
 * stripped mechanically to produce the 5.6 snapshot. */
#ifndef PHP_TARGET_ID
# ifdef PHP_VERSION_ID
#  define PHP_TARGET_ID PHP_VERSION_ID
# else
#  define PHP_TARGET_ID 50640
# endif
#endif

/* Line of the token that made the last parse fail. */
uint32_t php_parse_error_lineno = 0;

/* ===================================================================== */
/*  token buffer                                                          */
/* ===================================================================== */

typedef struct {
    php_token    tok;
    zend_string *doc;          /* doc comment in effect at this token       */
} ptok;

typedef struct {
    ptok        *t;
    uint32_t     n, cap, pos;
    zend_string *filename;

    /* a scanner failure is queued as a T_BAD_CHARACTER sentinel so that an
     * earlier syntax error is still the one that gets reported */
    char         lex_err[256];

    char         err[512];
    uint32_t     err_line;
    jmp_buf      jb;

    size_t       halt_offset;  /* __COMPILER_HALT_OFFSET__                  */
    int          v8;           /* php-src >= 8.0 spelling of error messages */

    /* per function bookkeeping, saved/restored around nested declarations */
    int          saw_yield;
    int          in_initializer;  /* parsing a constant expression          */

    /* Name resolution.  php-src does this in the compiler; this parser is the
     * only pass there is, so declared names are prefixed and referenced names
     * are resolved here, exactly as zend_compile.c would. */
    zend_string *ns;              /* current namespace, NULL at global scope */
    HashTable   *imp_class;       /* lowercased alias => full name           */
    HashTable   *imp_func;
    HashTable   *imp_const;       /* case sensitive alias => full name       */
} parser;

static void toks_push(parser *p, const php_token *tok, zend_string *doc)
{
    if (p->n == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 256;
        p->t = (ptok *) erealloc(p->t, p->cap * sizeof(ptok));
    }
    p->t[p->n].tok = *tok;
    p->t[p->n].doc = doc;
    p->n++;
}

/* ===================================================================== */
/*  error reporting                                                       */
/* ===================================================================== */

/* php-src reworded parse errors in 8.0:
 *   < 8.0   syntax error, unexpected ';', expecting ')'
 *   >= 8.0  syntax error, unexpected token ";", expecting ")"
 * EG(php_version) is set before the script is parsed; 0 means "not set", in
 * which case we use the older, more widely quoted spelling. */
static const char *tok_fixed_text(php_token_type t)
{
    switch (t) {
    case T_SEMICOLON: return ";";
    case T_COMMA:     return ",";
    case T_COLON:     return ":";
    case T_QUESTION:  return "?";
    case T_LPAREN:    return "(";
    case T_RPAREN:    return ")";
    case T_LBRACKET:  return "[";
    case T_RBRACKET:  return "]";
    case T_LBRACE:    return "{";
    case T_RBRACE:    return "}";
    case T_PLUS:      return "+";
    case T_MINUS:     return "-";
    case T_MUL:       return "*";
    case T_DIV:       return "/";
    case T_MOD:       return "%";
    case T_DOT:       return ".";
    case T_ASSIGN:    return "=";
    case T_BANG:      return "!";
    case T_AMP:       return "&";
    case T_PIPE:      return "|";
    case T_CARET:     return "^";
    case T_TILDE:     return "~";
    case T_LT:        return "<";
    case T_GT:        return ">";
    case T_AT:        return "@";
    case T_DOLLAR:    return "$";
    case T_BACKQUOTE: return "`";
    case T_IS_IDENTICAL:        return "===";
    case T_IS_NOT_IDENTICAL:    return "!==";
    case T_IS_EQUAL:            return "==";
    case T_IS_NOT_EQUAL:        return "!=";
    case T_SPACESHIP:           return "<=>";
    case T_IS_SMALLER_OR_EQUAL: return "<=";
    case T_IS_GREATER_OR_EQUAL: return ">=";
    case T_INC:                 return "++";
    case T_DEC:                 return "--";
    case T_POW:                 return "**";
    case T_PLUS_EQUAL:          return "+=";
    case T_MINUS_EQUAL:         return "-=";
    case T_MUL_EQUAL:           return "*=";
    case T_DIV_EQUAL:           return "/=";
    case T_CONCAT_EQUAL:        return ".=";
    case T_MOD_EQUAL:           return "%=";
    case T_AND_EQUAL:           return "&=";
    case T_OR_EQUAL:            return "|=";
    case T_XOR_EQUAL:           return "^=";
    case T_SL_EQUAL:            return "<<=";
    case T_SR_EQUAL:            return ">>=";
    case T_POW_EQUAL:           return "**=";
    case T_COALESCE_EQUAL:      return "?\?=";
    case T_COALESCE:            return "??";
    case T_NULLSAFE_OBJECT_OPERATOR: return "?->";
    case T_OBJECT_OPERATOR:     return "->";
    case T_DOUBLE_ARROW:        return "=>";
    case T_PAAMAYIM_NEKUDOTAYIM: return "::";
    case T_ELLIPSIS:            return "...";
    case T_BOOLEAN_AND:         return "&&";
    case T_BOOLEAN_OR:          return "||";
    case T_SL:                  return "<<";
    case T_SR:                  return ">>";
    case T_ATTRIBUTE:           return "#[";
    case T_NS_SEPARATOR:        return "\\";
    case T_INT_CAST:            return "(int)";
    case T_DOUBLE_CAST:         return "(double)";
    case T_STRING_CAST:         return "(string)";
    case T_ARRAY_CAST:          return "(array)";
    case T_OBJECT_CAST:         return "(object)";
    case T_BOOL_CAST:           return "(bool)";
    case T_UNSET_CAST:          return "(unset)";
    case T_CURLY_OPEN:          return "{$";
    case T_DOLLAR_OPEN_CURLY_BRACES: return "${";
    case T_DQ_START: case T_DQ_END:  return "\"";
    default: return NULL;
    }
}

/* how php-src names a token it did not expect */
static void tok_describe(char *buf, size_t size, const php_token *tok, int v8)
{
    const char *fixed = tok_fixed_text(tok->type);
    const char *text = (tok->text && tok->text->val) ? tok->text->val : "";
    char oneline[128];

    /* php-src truncates the token text at the first newline so that the
     * diagnostic stays on one line. */
    {
        size_t i = 0;
        while (i + 1 < sizeof(oneline) && text[i] && text[i] != '\n' && text[i] != '\r') {
            oneline[i] = text[i];
            i++;
        }
        oneline[i] = '\0';
        text = oneline;
    }

    if (tok->type == T_EOF) {
        snprintf(buf, size, "end of file");
        return;
    }
    if (fixed) {
        if (v8) {
            snprintf(buf, size, "token \"%s\"", fixed);
        } else {
            snprintf(buf, size, "'%s'", fixed);
        }
        return;
    }
    switch (tok->type) {
    case T_STRING:
        if (v8) snprintf(buf, size, "identifier \"%.40s\"", text);
        else    snprintf(buf, size, "'%.40s' (T_STRING)", text);
        return;
    case T_VARIABLE:
        if (v8) snprintf(buf, size, "variable \"$%.40s\"", text);
        else    snprintf(buf, size, "'$%.40s' (T_VARIABLE)", text);
        return;
    case T_LNUMBER:
        if (v8) snprintf(buf, size, "integer \"%.40s\"", text);
        else    snprintf(buf, size, "'%.40s' (T_LNUMBER)", text);
        return;
    case T_DNUMBER:
        if (v8) snprintf(buf, size, "floating-point number \"%.40s\"", text);
        else    snprintf(buf, size, "'%.40s' (T_DNUMBER)", text);
        return;
    case T_CONSTANT_ENCAPSED_STRING:
        if (v8) snprintf(buf, size, "quoted string \"%.30s\"", text);
        else    snprintf(buf, size, "'%.30s' (T_CONSTANT_ENCAPSED_STRING)", text);
        return;
    case T_ENCAPSED_AND_WHITESPACE:
        if (v8) snprintf(buf, size, "string content \"%.30s\"", text);
        else    snprintf(buf, size, "'%.30s' (T_ENCAPSED_AND_WHITESPACE)", text);
        return;
    case T_INLINE_HTML:
        snprintf(buf, size, v8 ? "token \"?>\"" : "'?>'");
        return;
    default:
        break;
    }
    if (tok->text && tok->text->len) {
        if (v8) snprintf(buf, size, "token \"%.40s\"", text);
        else    snprintf(buf, size, "'%.40s' (%s)", text, php_token_name(tok->type));
    } else {
        const char *nm = php_token_name(tok->type);
        if (v8) snprintf(buf, size, "token \"%s\"", nm ? nm : "?");
        else    snprintf(buf, size, "'%s'", nm ? nm : "?");
    }
}

/* how php-src names a token it *was* expecting */
static void tok_expected(char *buf, size_t size, php_token_type t, int v8)
{
    const char *fixed = tok_fixed_text(t);

    if (fixed) {
        snprintf(buf, size, v8 ? "\"%s\"" : "'%s'", fixed);
        return;
    }
    switch (t) {
    case T_STRING:   snprintf(buf, size, v8 ? "identifier" : "identifier (T_STRING)"); return;
    case T_VARIABLE: snprintf(buf, size, v8 ? "variable" : "variable (T_VARIABLE)"); return;
    case T_LNUMBER:  snprintf(buf, size, v8 ? "integer" : "integer (T_LNUMBER)"); return;
    case T_EOF:      snprintf(buf, size, "end of file"); return;
    default: {
        const char *nm = php_token_name(t);
        snprintf(buf, size, v8 ? "\"%s\"" : "'%s'", nm ? nm : "?");
        return;
    }
    }
}

static _Noreturn void parse_fail(parser *p, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(p->err, sizeof(p->err), fmt, ap);
    va_end(ap);
    longjmp(p->jb, 1);
}

static php_token *cur_tok(parser *p)      { return &p->t[p->pos].tok; }
static php_token_type cur_type(parser *p) { return p->t[p->pos].tok.type; }
static uint32_t cur_line(parser *p)       { return p->t[p->pos].tok.lineno; }

static php_token_type peek_type(parser *p, uint32_t k)
{
    uint32_t i = p->pos + k;
    if (i >= p->n) {
        i = p->n - 1;
    }
    return p->t[i].tok.type;
}

static _Noreturn void unexpected(parser *p, php_token_type expecting)
{
    char got[160], want[80];
    php_token *tok = cur_tok(p);

    p->err_line = tok->lineno;
    php_parse_error_lineno = tok->lineno;

    /* a queued scanner failure surfaces here, in source order */
    if (tok->type == T_BAD_CHARACTER) {
        parse_fail(p, "%s", p->lex_err[0] ? p->lex_err : "syntax error");
    }

    tok_describe(got, sizeof(got), tok, p->v8);
    if (expecting != T_LAST_TOKEN) {
        tok_expected(want, sizeof(want), expecting, p->v8);
        parse_fail(p, "syntax error, unexpected %s, expecting %s", got, want);
    }
    parse_fail(p, "syntax error, unexpected %s", got);
}

/* Refuse a construct the targeted release does not have yet.  Not static: a
 * build in which every VERSIONGATE region has been stripped away must still
 * compile without an "unused function" diagnostic. */
_Noreturn void version_gate(parser *p, const char *msg);

_Noreturn void version_gate(parser *p, const char *msg)
{
    php_token *tok = cur_tok(p);

    p->err_line = tok->lineno;
    php_parse_error_lineno = tok->lineno;
    parse_fail(p, "%s", msg);
}

static void advance(parser *p)
{
    if (p->pos + 1 < p->n) {
        p->pos++;
    }
}

static int accept(parser *p, php_token_type t)
{
    if (cur_type(p) == t) {
        advance(p);
        return 1;
    }
    return 0;
}

static void expect(parser *p, php_token_type t)
{
    if (cur_type(p) != t) {
        unexpected(p, t);
    }
    advance(p);
}

/* "?>" behaves exactly like ";" (php-src's scanner returns ';' for it), and the
 * tokeniser already rewrote it, so this is just the plain terminator plus the
 * end of file, which php-src also tolerates after a trailing statement. */
static _Noreturn void unexpected_expecting(parser *p, const char *want)
{
    char got[160];
    php_token *tok = cur_tok(p);

    p->err_line = tok->lineno;
    php_parse_error_lineno = tok->lineno;
    if (tok->type == T_BAD_CHARACTER) {
        parse_fail(p, "%s", p->lex_err[0] ? p->lex_err : "syntax error");
    }
    tok_describe(got, sizeof(got), tok, p->v8);
    parse_fail(p, "syntax error, unexpected %s, expecting %s", got, want);
}

/* After a complete expression almost any operator could follow, which is why
 * php-src does not list the alternatives here. */
static void expect_semi_after_expr(parser *p)
{
    if (cur_type(p) == T_SEMICOLON || cur_type(p) == T_CLOSE_TAG) {
        advance(p);
        return;
    }
    unexpected(p, T_LAST_TOKEN);
}

static void expect_comma_or_semi(parser *p)
{
    if (cur_type(p) == T_SEMICOLON || cur_type(p) == T_CLOSE_TAG) {
        advance(p);
        return;
    }
    unexpected_expecting(p, p->v8 ? "\",\" or \";\"" : "',' or ';'");
}

static void expect_semi(parser *p)
{
    if (cur_type(p) == T_SEMICOLON || cur_type(p) == T_CLOSE_TAG) {
        advance(p);
        return;
    }
    unexpected(p, T_SEMICOLON);
}

/* ===================================================================== */
/*  small AST helpers                                                     */
/* ===================================================================== */

static zend_ast *ast0(zend_ast_kind k, uint32_t line)
{
    return zend_ast_create(k, line, 0);
}
static zend_ast *ast1(zend_ast_kind k, uint32_t line, zend_ast *a)
{
    return zend_ast_create(k, line, 1, a);
}
static zend_ast *ast2(zend_ast_kind k, uint32_t line, zend_ast *a, zend_ast *b)
{
    return zend_ast_create(k, line, 2, a, b);
}
static zend_ast *ast3(zend_ast_kind k, uint32_t line, zend_ast *a, zend_ast *b, zend_ast *c)
{
    return zend_ast_create(k, line, 3, a, b, c);
}
static zend_ast *ast4(zend_ast_kind k, uint32_t line, zend_ast *a, zend_ast *b,
                      zend_ast *c, zend_ast *d)
{
    return zend_ast_create(k, line, 4, a, b, c, d);
}

static zend_ast *ast_zstr(zend_string *s, uint32_t line)
{
    return zend_ast_create_zval(zval_zstr(s), line);
}
static zend_ast *ast_cstr(const char *s, uint32_t line)
{
    return zend_ast_create_zval(zval_zstr(zend_string_initz(s)), line);
}
static zend_ast *ast_long(zend_long v, uint32_t line)
{
    return zend_ast_create_zval(zval_long(v), line);
}

/* An empty AST_LIST of a specific kind (AST_STMT_LIST, AST_ARRAY, ...). */
static zend_ast *ast_list(zend_ast_kind k, uint32_t line)
{
    return zend_ast_create(k, line, 0);
}

static zend_string *lc(zend_string *s)
{
    return zend_string_tolower(s);
}

/* ===================================================================== */
/*  identifiers and names                                                 */
/* ===================================================================== */

/* php-src's `semi_reserved`: T_STRING plus every keyword and magic constant.
 * The lexer groups both runs contiguously, which is what these ranges rely on. */
static int tok_is_ident(php_token_type t)
{
    if (t == T_STRING) {
        return 1;
    }
    if (t >= T_ABSTRACT && t <= T_YIELD_FROM) {
        return 1;
    }
    if (t >= T_LINE && t <= T_PROPERTY_C) {
        return 1;
    }
    return 0;
}

static int tok_is_name(php_token_type t)
{
    return t == T_STRING || t == T_NAME_QUALIFIED ||
           t == T_NAME_FULLY_QUALIFIED || t == T_NAME_RELATIVE;
}

static zend_string *tok_text_of(parser *p, php_token *tok)
{
    if (tok->text) {
        return tok->text;
    }
    /* keywords always carry their spelling; this is belt and braces */
    {
        const char *fixed = tok_fixed_text(tok->type);
        const char *nm = fixed ? fixed : php_token_name(tok->type);
        (void) p;
        return zend_string_initz(nm ? nm : "");
    }
}

/* consume an `identifier` (T_STRING | semi_reserved) and return its spelling */
static zend_string *parse_identifier(parser *p)
{
    php_token *tok = cur_tok(p);
    zend_string *s;

    if (!tok_is_ident(tok->type)) {
        unexpected(p, T_STRING);
    }
    s = tok_text_of(p, tok);
    advance(p);
    return s;
}

static int zstr_is(const zend_string *s, const char *lit)
{
    size_t n = strlen(lit);
    size_t i;

    if (!s || s->len != n) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        char a = s->val[i], b = lit[i];
        if (a >= 'A' && a <= 'Z') a = (char) (a - 'A' + 'a');
        if (a != b) return 0;
    }
    return 1;
}

/* Type names that only became types in a later release.  Each is a two-way
 * choice: with the newer branch gone the name is refused outright. */
static void gate_type_name(parser *p, zend_string *s)
{
    /* "int", "float", "string" and "bool" are not gated: PHP 5.6 parses them
     * as ordinary class type hints and only complains when the argument is
     * bound, so this is a semantic difference the evaluator owns, not a
     * syntactic one. */
    /* VERSIONGATE:70100 else-branch is the older behaviour: no void/iterable */
    if (PHP_TARGET_ID >= 70100) {
        /* void and iterable are declarable types */
    } else {
        if (zstr_is(s, "void") || zstr_is(s, "iterable")) {
            version_gate(p, "syntax error, unexpected type name");
        }
    }
    /* VERSIONGATE:70200 else-branch is the older behaviour: no object type */
    if (PHP_TARGET_ID >= 70200) {
        /* object is a declarable type */
    } else {
        if (zstr_is(s, "object")) {
            version_gate(p, "syntax error, unexpected type name");
        }
    }
    /* VERSIONGATE:80000 else-branch is the older behaviour: no mixed type */
    if (PHP_TARGET_ID >= 80000) {
        /* mixed is a declarable type */
    } else {
        if (zstr_is(s, "mixed")) {
            version_gate(p, "syntax error, unexpected type name");
        }
    }
    /* VERSIONGATE:80100 else-branch is the older behaviour: no never type */
    if (PHP_TARGET_ID >= 80100) {
        /* never is a declarable type */
    } else {
        if (zstr_is(s, "never")) {
            version_gate(p, "syntax error, unexpected type name");
        }
    }
}

/* "A\B" from "A" and "B" */
static zend_string *ns_join(const char *a, size_t alen, const char *b, size_t blen)
{
    char *buf = (char *) emalloc(alen + blen + 2);
    zend_string *r;

    memcpy(buf, a, alen);
    buf[alen] = '\\';
    memcpy(buf + alen + 1, b, blen);
    buf[alen + 1 + blen] = '\0';
    r = zend_string_init(buf, alen + 1 + blen);
    efree(buf);
    return r;
}

enum { NS_CLASS = 0, NS_FUNCTION = 1, NS_CONST = 2 };

/* php-src's zend_resolve_class_name / zend_resolve_function_name.  An
 * unqualified function or constant keeps ZEND_NAME_NOT_FQ so that the
 * evaluator can still fall back to the global name at run time; a class name
 * has no such fallback and comes back fully qualified. */
static zend_string *resolve_name(parser *p, zend_string *name, uint32_t *attr, int kind)
{
    const char *bs;
    HashTable *imp;
    zval *v;

    if (*attr == ZEND_NAME_FQ) {
        return name;                              /* "\Foo\Bar" */
    }
    if (*attr == ZEND_NAME_RELATIVE) {            /* "namespace\Foo" */
        *attr = ZEND_NAME_FQ;
        return p->ns ? ns_join(p->ns->val, p->ns->len, name->val, name->len) : name;
    }

    bs = (const char *) memchr(name->val, '\\', name->len);
    if (bs) {
        /* qualified: only the first segment can be an imported alias, and the
         * lookup always uses the class import table */
        size_t flen = (size_t) (bs - name->val);
        v = p->imp_class ? zend_hash_find(p->imp_class,
                zend_string_tolower(zend_string_init(name->val, flen))) : NULL;
        *attr = ZEND_NAME_FQ;
        if (v) {
            return ns_join(v->value.str->val, v->value.str->len,
                           bs + 1, name->len - flen - 1);
        }
        return p->ns ? ns_join(p->ns->val, p->ns->len, name->val, name->len) : name;
    }

    imp = (kind == NS_CLASS) ? p->imp_class
        : (kind == NS_FUNCTION) ? p->imp_func : p->imp_const;
    if (imp) {
        v = (kind == NS_CONST) ? zend_hash_find(imp, name)
                               : zend_hash_find(imp, zend_string_tolower(name));
        if (v) {
            *attr = ZEND_NAME_FQ;
            return v->value.str;
        }
    }
    if (!p->ns) {
        return name;
    }
    if (kind == NS_CLASS) {
        *attr = ZEND_NAME_FQ;                     /* classes never fall back */
    }
    return ns_join(p->ns->val, p->ns->len, name->val, name->len);
}

/* rewrite an AST_ZVAL name node in place */
static zend_ast *resolve_name_ast(parser *p, zend_ast *ast, int kind)
{
    uint32_t attr = ast->attr;

    ast->val = zval_zstr(resolve_name(p, ast->val.value.str, &attr, kind));
    ast->attr = (uint16_t) attr;
    return ast;
}

/* last segment of "A\\B\\C" */
static zend_string *name_tail(zend_string *s)
{
    size_t i;

    for (i = s->len; i > 0; i--) {
        if (s->val[i - 1] == '\\') {
            return zend_string_init(s->val + i, s->len - i);
        }
    }
    return s;
}

static void add_import(HashTable **tbl, zend_string *alias, zend_string *full, int ci)
{
    if (!*tbl) {
        *tbl = zend_hash_new();
    }
    zend_hash_update(*tbl, ci ? zend_string_tolower(alias) : alias, zval_zstr(full));
}

/* prefix a declared name with the namespace it is declared in */
static zend_string *ns_declare(parser *p, zend_string *name)
{
    return p->ns ? ns_join(p->ns->val, p->ns->len, name->val, name->len) : name;
}

/* consume a `name` and return an AST_ZVAL leaf carrying the source spelling
 * (leading "\" already removed by the lexer); attr records how it was written */
static zend_ast *parse_name(parser *p)
{
    php_token *tok = cur_tok(p);
    zend_ast *ast;
    uint32_t attr = ZEND_NAME_NOT_FQ;

    /* the pre-8.0 spelling, where "namespace \ Foo" arrives as separate tokens */
    if (tok->type == T_NAMESPACE &&
        (peek_type(p, 1) == T_NS_SEPARATOR ||
         peek_type(p, 1) == T_NAME_FULLY_QUALIFIED)) {
        advance(p);
        accept(p, T_NS_SEPARATOR);
        ast = parse_name(p);
        ast->attr = ZEND_NAME_RELATIVE;
        return ast;
    }

    switch (tok->type) {
    case T_NAME_FULLY_QUALIFIED: attr = ZEND_NAME_FQ; break;
    case T_NAME_RELATIVE:        attr = ZEND_NAME_RELATIVE; break;
    case T_STRING:
    case T_NAME_QUALIFIED:       break;
    default:
        unexpected(p, T_STRING);
    }
    ast = ast_zstr(tok_text_of(p, tok), tok->lineno);
    ast->attr = (uint16_t) attr;
    advance(p);
    return ast;
}

/* `class_name`: a name, or the keyword `static` */
static zend_ast *parse_class_name(parser *p)
{
    zend_ast *ast;

    if (cur_type(p) == T_STATIC) {
        ast = ast_cstr("static", cur_line(p));
        advance(p);
        return ast;
    }
    ast = parse_name(p);
    if (ast->attr == ZEND_NAME_NOT_FQ) {
        zend_string *nm = ast->val.value.str;
        if (zstr_is(nm, "self") || zstr_is(nm, "parent") || zstr_is(nm, "static")) {
            return ast;
        }
    }
    return resolve_name_ast(p, ast, NS_CLASS);
}

static zend_string *ast_name_str(zend_ast *ast)
{
    if (ast && ast->kind == AST_ZVAL && ast->val.u1 == IS_STRING) {
        return ast->val.value.str;
    }
    return NULL;
}

/* ===================================================================== */
/*  per class side table (parent / interface / trait names, enum cases)    */
/* ===================================================================== */

typedef struct {
    zend_class_entry      *ce;
    zend_class_decl_extra *ex;
} ce_extra_slot;

static ce_extra_slot *ce_extra_tab;
static uint32_t       ce_extra_cap;
static uint32_t       ce_extra_used;

static uint32_t ce_extra_hash(const zend_class_entry *ce, uint32_t cap)
{
    uintptr_t v = (uintptr_t) ce;
    v ^= v >> 33;
    v *= (uintptr_t) 0xff51afd7ed558ccdULL;
    v ^= v >> 29;
    return (uint32_t) (v & (cap - 1));
}

static void ce_extra_grow(void)
{
    uint32_t ncap = ce_extra_cap ? ce_extra_cap * 2 : 64;
    ce_extra_slot *ntab = (ce_extra_slot *) ecalloc(ncap, sizeof(ce_extra_slot));
    uint32_t i;

    for (i = 0; i < ce_extra_cap; i++) {
        if (ce_extra_tab[i].ce) {
            uint32_t j = ce_extra_hash(ce_extra_tab[i].ce, ncap);
            while (ntab[j].ce) {
                j = (j + 1) & (ncap - 1);
            }
            ntab[j] = ce_extra_tab[i];
        }
    }
    ce_extra_tab = ntab;
    ce_extra_cap = ncap;
}

void zend_class_decl_set_extra(zend_class_entry *ce, zend_class_decl_extra *extra)
{
    uint32_t i;

    if (!ce) {
        return;
    }
    if (ce_extra_used * 10 >= ce_extra_cap * 7) {
        ce_extra_grow();
    }
    i = ce_extra_hash(ce, ce_extra_cap);
    while (ce_extra_tab[i].ce && ce_extra_tab[i].ce != ce) {
        i = (i + 1) & (ce_extra_cap - 1);
    }
    if (!ce_extra_tab[i].ce) {
        ce_extra_used++;
    }
    ce_extra_tab[i].ce = ce;
    ce_extra_tab[i].ex = extra;
}

zend_class_decl_extra *zend_class_decl_get_extra(zend_class_entry *ce)
{
    uint32_t i;

    if (!ce || !ce_extra_cap) {
        return NULL;
    }
    i = ce_extra_hash(ce, ce_extra_cap);
    while (ce_extra_tab[i].ce) {
        if (ce_extra_tab[i].ce == ce) {
            return ce_extra_tab[i].ex;
        }
        i = (i + 1) & (ce_extra_cap - 1);
    }
    return NULL;
}

/* ===================================================================== */
/*  forward declarations                                                  */
/* ===================================================================== */

static zend_ast *parse_expr(parser *p);
static zend_ast *parse_expr_prec(parser *p, int minprec);
static zend_ast *parse_statement(parser *p);
static zend_ast *parse_stmt_list(parser *p, const php_token_type *stops);
static zend_ast *parse_variable(parser *p);
static zend_ast *parse_args(parser *p);
static zend_ast *parse_array_literal(parser *p, php_token_type close, uint32_t line);
static zend_type_hint *parse_type_expr(parser *p);
/* VERSIONGATE:80000 BEGIN - attributes */
static HashTable *parse_attribute_groups(parser *p);
/* VERSIONGATE:80000 END */
static zend_ast *parse_class_decl(parser *p, uint32_t flags, uint32_t line,
                                  HashTable *attrs, int anonymous, zend_ast **anon_args);
static zend_ast *parse_encaps_list(parser *p, php_token_type endtok, uint32_t line);
static uint32_t add_class_modifier(parser *p, uint32_t flags, uint32_t add,
                                   uint32_t line);

/* ===================================================================== */
/*  type hints                                                            */
/* ===================================================================== */

static zend_type_hint *type_new(uint32_t n)
{
    zend_type_hint *t = (zend_type_hint *) ecalloc(1, sizeof(zend_type_hint));

    t->n = n;
    if (n) {
        t->names = (zend_string **) ecalloc(n, sizeof(zend_string *));
    }
    return t;
}

static int type_atom_here(parser *p)
{
    php_token_type t = cur_type(p);

    return t == T_ARRAY || t == T_CALLABLE || t == T_STATIC || tok_is_name(t);
}


/* Type words that are never class names, so a namespace must not prefix them.
 * 5.6 only reserved self/parent/static; the scalar words became reserved with
 * the release that made them declarable types. */
static int type_name_is_reserved(zend_string *s)
{
    if (zstr_is(s, "self") || zstr_is(s, "parent") || zstr_is(s, "static")) {
        return 1;
    }
    /* VERSIONGATE:70000 else-branch is the older behaviour: "int" is a class */
    if (PHP_TARGET_ID >= 70000) {
        if (zstr_is(s, "int") || zstr_is(s, "float") ||
            zstr_is(s, "string") || zstr_is(s, "bool")) {
            return 1;
        }
    } else {
        /* in 5.6 these resolve like any other class name */
    }
    /* VERSIONGATE:70100 else-branch is the older behaviour */
    if (PHP_TARGET_ID >= 70100) {
        if (zstr_is(s, "void") || zstr_is(s, "iterable")) return 1;
    } else {
        /* not type words yet */
    }
    /* VERSIONGATE:70200 else-branch is the older behaviour */
    if (PHP_TARGET_ID >= 70200) {
        if (zstr_is(s, "object")) return 1;
    } else {
        /* not a type word yet */
    }
    /* VERSIONGATE:80000 else-branch is the older behaviour */
    if (PHP_TARGET_ID >= 80000) {
        if (zstr_is(s, "mixed") || zstr_is(s, "false") ||
            zstr_is(s, "null") || zstr_is(s, "true")) {
            return 1;
        }
    } else {
        /* not type words yet */
    }
    /* VERSIONGATE:80100 else-branch is the older behaviour */
    if (PHP_TARGET_ID >= 80100) {
        if (zstr_is(s, "never")) return 1;
    } else {
        /* not a type word yet */
    }
    return 0;
}

/* a single type atom, kept in its source spelling */
static zend_string *parse_type_atom(parser *p)
{
    php_token *tok = cur_tok(p);
    zend_string *s;

    switch (tok->type) {
    case T_ARRAY: case T_CALLABLE: case T_STATIC:
    case T_STRING: case T_NAME_QUALIFIED:
    case T_NAME_FULLY_QUALIFIED: case T_NAME_RELATIVE:
        s = tok_text_of(p, tok);
        gate_type_name(p, s);
        if (!type_name_is_reserved(s)) {
            uint32_t attr = (tok->type == T_NAME_FULLY_QUALIFIED) ? ZEND_NAME_FQ
                          : (tok->type == T_NAME_RELATIVE) ? ZEND_NAME_RELATIVE
                          : ZEND_NAME_NOT_FQ;
            s = resolve_name(p, s, &attr, NS_CLASS);
        }
        advance(p);
        return s;
    default:
        unexpected(p, T_STRING);
    }
}

/* "&" is an intersection separator only when it is not the by-reference marker
 * of the parameter that follows (php-src splits the token in the lexer for the
 * same reason). */
static int amp_is_intersection(parser *p)
{
    php_token_type nxt;

    if (cur_type(p) != T_AMP) {
        return 0;
    }
    nxt = peek_type(p, 1);
    return !(nxt == T_VARIABLE || nxt == T_ELLIPSIS);
}

/* type_expr, including union, intersection and DNF "(A&B)|C" */
static zend_type_hint *parse_type_expr(parser *p)
{
    zend_type_hint *res;
    zend_string *names[64];
    zend_type_hint *groups[32];
    uint32_t nnames = 0, ngroups = 0;
    int nullable = 0;
    int intersection = 0;
    uint32_t i;

    /* VERSIONGATE:70100 BEGIN - nullable type "?T" */
    if (cur_type(p) == T_QUESTION) {
        if (PHP_TARGET_ID < 70100) version_gate(p, "syntax error, unexpected '?'");
        advance(p);
        res = type_new(1);
        res->names[0] = parse_type_atom(p);
        res->nullable = 1;
        return res;
    }
    /* VERSIONGATE:70100 END */

    for (;;) {
        /* VERSIONGATE:80200 BEGIN - DNF types "(A&B)|C" */
        if (cur_type(p) == T_LPAREN) {
            /* a parenthesised intersection group, only legal inside a union */
            zend_type_hint *g;
            zend_string *gnames[32];
            uint32_t gn = 0;

            if (PHP_TARGET_ID < 80200) version_gate(p, "syntax error, unexpected '('");
            advance(p);
            gnames[gn++] = parse_type_atom(p);
            while (cur_type(p) == T_AMP && gn < 32) {
                advance(p);
                gnames[gn++] = parse_type_atom(p);
            }
            expect(p, T_RPAREN);
            g = type_new(gn);
            for (i = 0; i < gn; i++) {
                g->names[i] = gnames[i];
            }
            g->is_intersection = 1;
            if (ngroups < 32) {
                groups[ngroups++] = g;
            }
        } else
        /* VERSIONGATE:80200 END */
        {
            zend_string *a = parse_type_atom(p);
            if (zstr_is(a, "null")) {
                nullable = 1;
            }
            if (nnames < 64) {
                names[nnames++] = a;
            }
        }

        /* VERSIONGATE:80000 BEGIN - union types "A|B" */
        if (cur_type(p) == T_PIPE) {
            if (PHP_TARGET_ID < 80000) version_gate(p, "syntax error, unexpected '|'");
            advance(p);
            continue;
        }
        /* VERSIONGATE:80000 END */

        /* VERSIONGATE:80100 BEGIN - intersection types "A&B" */
        if (!intersection && ngroups == 0 && nnames == 1 && amp_is_intersection(p)) {
            if (PHP_TARGET_ID < 80100) version_gate(p, "syntax error, unexpected '&'");
            intersection = 1;
        }
        if (intersection && cur_type(p) == T_AMP) {
            advance(p);
            continue;
        }
        /* VERSIONGATE:80100 END */
        break;
    }

    /* VERSIONGATE:80200 else-branch is the older behaviour: no standalone
       true/false/null type */
    if (PHP_TARGET_ID >= 80200) {
        /* "true", "false" and "null" stand on their own */
    } else {
        if (nnames == 1 && ngroups == 0 && !intersection &&
            (zstr_is(names[0], "true") || zstr_is(names[0], "false") ||
             zstr_is(names[0], "null"))) {
            version_gate(p, "syntax error, unexpected type name");
        }
    }

    res = type_new(nnames);
    for (i = 0; i < nnames; i++) {
        res->names[i] = names[i];
    }
    res->nullable = (unsigned char) nullable;
    res->is_intersection = (unsigned char) intersection;
    /* VERSIONGATE:80200 BEGIN - DNF groups on the result */
    if (ngroups) {
        res->groups = (zend_type_hint **) ecalloc(ngroups, sizeof(zend_type_hint *));
        for (i = 0; i < ngroups; i++) {
            res->groups[i] = groups[i];
        }
        res->ngroups = ngroups;
    }
    /* VERSIONGATE:80200 END */
    return res;
}


/* ===================================================================== */
/*  expression precedence (php-src's table, lowest first)                 */
/* ===================================================================== */

enum {
    PREC_NONE = 0,
    PREC_THROW,        /* throw                                     */
    PREC_INCLUDE,      /* include require ...                       */
    PREC_OR,           /* or                                        */
    PREC_XOR,          /* xor                                       */
    PREC_AND,          /* and                                       */
    PREC_PRINT,        /* print                                     */
    PREC_YIELD,        /* yield                                     */
    PREC_ARROW,        /* =>                                        */
    PREC_YIELD_FROM,   /* yield from                                */
    PREC_ASSIGN,       /* = += ... ??=                              */
    PREC_TERNARY,      /* ?:                                        */
    PREC_COALESCE,     /* ??   (right)                              */
    PREC_BOOL_OR,      /* ||                                        */
    PREC_BOOL_AND,     /* &&                                        */
    PREC_BOR,          /* |                                         */
    PREC_BXOR,         /* ^                                         */
    PREC_BAND,         /* &                                         */
    PREC_EQ,           /* == != === !== <=>                         */
    PREC_CMP,          /* < <= > >=                                 */
    PREC_CONCAT,       /* .    (below +- since 8.0)                 */
    PREC_SHIFT,        /* << >>                                     */
    PREC_ADD,          /* + -                                       */
    PREC_MUL,          /* * / %                                     */
    PREC_NOT,          /* !                                         */
    PREC_INSTANCEOF,   /* instanceof                                */
    PREC_CAST,         /* ~ (int) @ unary+ unary-                   */
    PREC_POW,          /* **   (right)                              */
    PREC_CLONE         /* clone                                     */
};

/* binary operator table: precedence, right associativity, zend_binop */
static int binop_of(php_token_type t, int *prec, int *rightassoc, zend_binop *op)
{
    *rightassoc = 0;
    switch (t) {
    case T_LOGICAL_OR:  *prec = PREC_OR;  return 1;   /* handled as AST_OR  */
    case T_LOGICAL_XOR: *prec = PREC_XOR; *op = BIN_BOOL_XOR; return 2;
    case T_LOGICAL_AND: *prec = PREC_AND; return 1;   /* handled as AST_AND */
    case T_BOOLEAN_OR:  *prec = PREC_BOOL_OR;  return 1;
    case T_BOOLEAN_AND: *prec = PREC_BOOL_AND; return 1;
    case T_PIPE:  *prec = PREC_BOR;  *op = BIN_BOR;  return 2;
    case T_CARET: *prec = PREC_BXOR; *op = BIN_BXOR; return 2;
    case T_AMP:   *prec = PREC_BAND; *op = BIN_BAND; return 2;
    case T_IS_EQUAL:         *prec = PREC_EQ; *op = BIN_EQUAL; return 2;
    case T_IS_NOT_EQUAL:     *prec = PREC_EQ; *op = BIN_NOT_EQUAL; return 2;
    case T_IS_IDENTICAL:     *prec = PREC_EQ; *op = BIN_IDENTICAL; return 2;
    case T_IS_NOT_IDENTICAL: *prec = PREC_EQ; *op = BIN_NOT_IDENTICAL; return 2;
    case T_LT: *prec = PREC_CMP; *op = BIN_SMALLER; return 2;
    case T_GT: *prec = PREC_CMP; *op = BIN_GREATER; return 2;
    case T_IS_SMALLER_OR_EQUAL: *prec = PREC_CMP; *op = BIN_SMALLER_OR_EQUAL; return 2;
    case T_IS_GREATER_OR_EQUAL: *prec = PREC_CMP; *op = BIN_GREATER_OR_EQUAL; return 2;
    case T_DOT:   *prec = PREC_CONCAT; *op = BIN_CONCAT; return 2;
    case T_SL:    *prec = PREC_SHIFT; *op = BIN_SL; return 2;
    case T_SR:    *prec = PREC_SHIFT; *op = BIN_SR; return 2;
    case T_PLUS:  *prec = PREC_ADD; *op = BIN_ADD; return 2;
    case T_MINUS: *prec = PREC_ADD; *op = BIN_SUB; return 2;
    case T_MUL:   *prec = PREC_MUL; *op = BIN_MUL; return 2;
    case T_DIV:   *prec = PREC_MUL; *op = BIN_DIV; return 2;
    case T_MOD:   *prec = PREC_MUL; *op = BIN_MOD; return 2;
    case T_POW:   *prec = PREC_POW; *op = BIN_POW; *rightassoc = 1; return 2;
    default: return 0;
    }
}

/* compound assignment operators */
static int assignop_of(php_token_type t, zend_binop *op)
{
    switch (t) {
    case T_PLUS_EQUAL:   *op = BIN_ADD; return 1;
    case T_MINUS_EQUAL:  *op = BIN_SUB; return 1;
    case T_MUL_EQUAL:    *op = BIN_MUL; return 1;
    case T_DIV_EQUAL:    *op = BIN_DIV; return 1;
    case T_MOD_EQUAL:    *op = BIN_MOD; return 1;
    case T_POW_EQUAL:    *op = BIN_POW; return 1;
    case T_CONCAT_EQUAL: *op = BIN_CONCAT; return 1;
    case T_AND_EQUAL:    *op = BIN_BAND; return 1;
    case T_OR_EQUAL:     *op = BIN_BOR; return 1;
    case T_XOR_EQUAL:    *op = BIN_BXOR; return 1;
    case T_SL_EQUAL:     *op = BIN_SL; return 1;
    case T_SR_EQUAL:     *op = BIN_SR; return 1;
    default: return 0;
    }
}

static int cast_target(php_token_type t, uint16_t *out)
{
    switch (t) {
    case T_INT_CAST:    *out = IS_LONG; return 1;
    case T_DOUBLE_CAST: *out = IS_DOUBLE; return 1;
    case T_STRING_CAST: *out = IS_STRING; return 1;
    case T_ARRAY_CAST:  *out = IS_ARRAY; return 1;
    case T_OBJECT_CAST: *out = IS_OBJECT; return 1;
    case T_BOOL_CAST:   *out = ZEND_CAST_BOOL; return 1;
    case T_UNSET_CAST:  *out = ZEND_CAST_UNSET; return 1;
    default: return 0;
    }
}

static int is_assignable(zend_ast *a)
{
    if (!a) {
        return 0;
    }
    switch (a->kind) {
    case AST_VAR: case AST_DIM: case AST_PROP: case AST_NULLSAFE_PROP:
    case AST_STATIC_PROP: case AST_LIST_ASSIGN:
    case AST_CALL: case AST_METHOD_CALL: case AST_NULLSAFE_METHOD_CALL:
    case AST_STATIC_CALL:
        return 1;
    default:
        return 0;
    }
}

/* An array literal used as an assignment target is a destructuring pattern;
 * nested arrays inside it are patterns too. */
static zend_ast *to_list_assign(parser *p, zend_ast *a, uint16_t syntax)
{
    uint32_t i;

    if (!a) {
        return NULL;
    }
    if (a->kind == AST_ARRAY) {
        a->kind = AST_LIST_ASSIGN;
        a->attr = syntax;
    } else if (a->kind != AST_LIST_ASSIGN) {
        return a;
    }
    for (i = 0; i < a->nchild; i++) {
        zend_ast *el = a->child[i];
        if (!el || el->kind != AST_ARRAY_ELEM) {
            continue;
        }
        /* VERSIONGATE:70100 else-branch is the older behaviour: no keys in list() */
        if (PHP_TARGET_ID >= 70100) {
            /* list("a" => $x) is allowed */
        } else {
            if (el->child[1]) {
                version_gate(p, "syntax error, unexpected '=>'");
            }
        }
        if (el->attr & ZEND_ARRAY_ELEM_SPREAD) {
            zend_error_at(E_COMPILE_ERROR, p->filename, el->lineno,
                          "Spread operator is not supported in assignments");
        }
        /* VERSIONGATE:70300 else-branch is the older behaviour: no by-ref in list() */
        if (PHP_TARGET_ID >= 70300) {
            /* list(&$x) is allowed */
        } else {
            if (el->attr & ZEND_ARRAY_ELEM_BY_REF) {
                version_gate(p, "syntax error, unexpected '&'");
            }
        }
        if (el->child[0]) {
            el->child[0] = to_list_assign(p, el->child[0], syntax);
        }
    }
    return a;
}

/* ===================================================================== */
/*  interpolated strings                                                  */
/* ===================================================================== */

/* "$a[key]" -- a bare identifier offset is a string literal, not a constant */
static zend_ast *parse_encaps_offset(parser *p)
{
    php_token *tok = cur_tok(p);
    zend_ast *r;

    switch (tok->type) {
    case T_STRING:
        r = ast_zstr(tok_text_of(p, tok), tok->lineno);
        advance(p);
        return r;
    case T_LNUMBER:
        r = ast_long(tok->lval, tok->lineno);
        advance(p);
        return r;
    case T_MINUS:                      /* defensive: "-" T_LNUMBER */
        advance(p);
        tok = cur_tok(p);
        if (tok->type != T_LNUMBER) {
            unexpected(p, T_LNUMBER);
        }
        r = ast_long(-tok->lval, tok->lineno);
        advance(p);
        return r;
    case T_VARIABLE:
        r = ast1(AST_VAR, tok->lineno, ast_zstr(tok_text_of(p, tok), tok->lineno));
        advance(p);
        return r;
    default:
        return parse_expr(p);
    }
}

static zend_ast *parse_encaps_var(parser *p)
{
    php_token *tok = cur_tok(p);
    uint32_t line = tok->lineno;
    zend_ast *var;

    switch (tok->type) {
    case T_VARIABLE:
        var = ast1(AST_VAR, line, ast_zstr(tok_text_of(p, tok), line));
        advance(p);
        if (cur_type(p) == T_LBRACKET) {
            zend_ast *off;
            advance(p);
            off = parse_encaps_offset(p);
            expect(p, T_RBRACKET);
            return ast2(AST_DIM, line, var, off);
        }
        if (cur_type(p) == T_OBJECT_OPERATOR && tok_is_ident(peek_type(p, 1))) {
            zend_string *nm;
            advance(p);
            nm = parse_identifier(p);
            return ast2(AST_PROP, line, var, ast_zstr(nm, line));
        }
        /* VERSIONGATE:80000 BEGIN - nullsafe property in an interpolated string */
        if (cur_type(p) == T_NULLSAFE_OBJECT_OPERATOR && tok_is_ident(peek_type(p, 1))) {
            zend_string *nm;
            if (PHP_TARGET_ID < 80000) version_gate(p, "syntax error, unexpected '?->'");
            advance(p);
            nm = parse_identifier(p);
            return ast2(AST_NULLSAFE_PROP, line, var, ast_zstr(nm, line));
        }
        /* VERSIONGATE:80000 END */
        return var;

    case T_DOLLAR_OPEN_CURLY_BRACES:
        advance(p);
        if (cur_type(p) == T_STRING_VARNAME) {
            zend_ast *nm = ast_zstr(tok_text_of(p, cur_tok(p)), line);
            advance(p);
            var = ast1(AST_VAR, line, nm);
            if (cur_type(p) == T_LBRACKET) {
                zend_ast *off;
                advance(p);
                off = parse_expr(p);
                expect(p, T_RBRACKET);
                var = ast2(AST_DIM, line, var, off);
            }
        } else {
            /* "${expr}" -- a variable variable */
            var = ast1(AST_VAR, line, parse_expr(p));
        }
        expect(p, T_RBRACE);
        return var;

    case T_CURLY_OPEN:
        advance(p);
        var = parse_expr(p);
        expect(p, T_RBRACE);
        return var;

    default:
        unexpected(p, T_LAST_TOKEN);
    }
}

/* the parts between the framing tokens of an interpolated string, a heredoc or
 * a backtick expression */
static zend_ast *parse_encaps_list(parser *p, php_token_type endtok, uint32_t line)
{
    zend_ast *list = ast_list(AST_ENCAPS, line);

    while (cur_type(p) != endtok) {
        if (cur_type(p) == T_EOF || cur_type(p) == T_BAD_CHARACTER) {
            unexpected(p, endtok);
        }
        if (cur_type(p) == T_ENCAPSED_AND_WHITESPACE) {
            php_token *tok = cur_tok(p);
            zend_ast_list_add(list, ast_zstr(tok_text_of(p, tok), tok->lineno));
            advance(p);
        } else {
            zend_ast_list_add(list, parse_encaps_var(p));
        }
    }
    advance(p);                                   /* the closing token */

    if (list->nchild == 0) {
        return ast_zstr(zend_string_init("", 0), line);
    }
    if (list->nchild == 1 && list->child[0]->kind == AST_ZVAL &&
        list->child[0]->val.u1 == IS_STRING) {
        return list->child[0];                    /* a heredoc with no parts */
    }
    return list;
}

/* ===================================================================== */
/*  variables, arguments, arrays                                          */
/* ===================================================================== */

/* php-src `simple_variable`: T_VARIABLE | '$' '{' expr '}' | '$' simple_variable */
static zend_ast *parse_simple_variable(parser *p)
{
    php_token *tok = cur_tok(p);
    uint32_t line = tok->lineno;

    if (tok->type == T_VARIABLE) {
        advance(p);
        return ast1(AST_VAR, line, ast_zstr(tok_text_of(p, tok), line));
    }
    if (tok->type == T_DOLLAR) {
        advance(p);
        if (accept(p, T_LBRACE)) {
            zend_ast *e = parse_expr(p);
            expect(p, T_RBRACE);
            return ast1(AST_VAR, line, e);
        }
        return ast1(AST_VAR, line, parse_simple_variable(p));
    }
    unexpected(p, T_VARIABLE);
}

/* property_name after "->" / "?->" */
static zend_ast *parse_property_name(parser *p)
{
    uint32_t line = cur_line(p);

    if (accept(p, T_LBRACE)) {
        zend_ast *e = parse_expr(p);
        expect(p, T_RBRACE);
        return e;
    }
    if (cur_type(p) == T_VARIABLE || cur_type(p) == T_DOLLAR) {
        return parse_simple_variable(p);
    }
    return ast_zstr(parse_identifier(p), line);
}

static zend_ast *parse_args(parser *p)
{
    uint32_t line = cur_line(p);
    zend_ast *list;

    expect(p, T_LPAREN);

    /* VERSIONGATE:80100 BEGIN - first class callable syntax "f(...)" */
    if (cur_type(p) == T_ELLIPSIS && peek_type(p, 1) == T_RPAREN) {
        if (PHP_TARGET_ID < 80100) version_gate(p, "syntax error, unexpected ')'");
        advance(p);
        advance(p);
        return ast0(AST_FIRST_CLASS_CALLABLE, line);
    }
    /* VERSIONGATE:80100 END */

    list = ast_list(AST_LIST, line);
    while (cur_type(p) != T_RPAREN) {
        uint32_t aline = cur_line(p);
        zend_ast *arg;

        if (accept(p, T_ELLIPSIS)) {
            arg = ast1(AST_SPREAD, aline, parse_expr(p));
        } else
        /* VERSIONGATE:80000 BEGIN - named arguments "f(x: 1)" */
        if (tok_is_ident(cur_type(p)) && peek_type(p, 1) == T_COLON &&
            peek_type(p, 2) != T_COLON) {
            zend_string *nm;
            if (PHP_TARGET_ID < 80000) version_gate(p, "syntax error, unexpected ':'");
            nm = parse_identifier(p);
            advance(p);                            /* ":" */
            arg = ast1(AST_NAMED_ARG, aline, parse_expr(p));
            arg->val = zval_zstr(nm);
        } else
        /* VERSIONGATE:80000 END */
        {
            arg = parse_expr(p);
        }
        zend_ast_list_add(list, arg);
        if (!accept(p, T_COMMA)) {
            break;
        }
        /* VERSIONGATE:70300 else-branch is the older behaviour: no trailing comma */
        if (PHP_TARGET_ID >= 70300) {
            if (cur_type(p) == T_RPAREN) break;
        } else {
            if (cur_type(p) == T_RPAREN) version_gate(p, "syntax error, unexpected ')'");
        }
    }
    expect(p, T_RPAREN);
    return list;
}

/* array_pair_list, shared by array literals, list() and destructuring */
static zend_ast *parse_array_literal(parser *p, php_token_type close, uint32_t line)
{
    zend_ast *list = ast_list(AST_ARRAY, line);
    int trailing_empty = 0;

    while (cur_type(p) != close) {
        uint32_t eline = cur_line(p);
        zend_ast *el;

        if (cur_type(p) == T_COMMA) {              /* a skipped slot: list(, $b) */
            zend_ast_list_add(list, ast2(AST_ARRAY_ELEM, eline, NULL, NULL));
            advance(p);
            trailing_empty = 1;
            continue;
        }
        /* VERSIONGATE:70400 BEGIN - array unpacking "[...$a]" */
        if (cur_type(p) == T_ELLIPSIS) {
            if (PHP_TARGET_ID < 70400) version_gate(p, "syntax error, unexpected '...'");
            advance(p);
            el = ast2(AST_ARRAY_ELEM, eline, parse_expr(p), NULL);
            el->attr = ZEND_ARRAY_ELEM_SPREAD;
        } else
        /* VERSIONGATE:70400 END */
        if (cur_type(p) == T_AMP) {
            advance(p);
            el = ast2(AST_ARRAY_ELEM, eline, parse_variable(p), NULL);
            el->attr = ZEND_ARRAY_ELEM_BY_REF;
        } else {
            zend_ast *first = parse_expr(p);
            if (accept(p, T_DOUBLE_ARROW)) {
                if (cur_type(p) == T_AMP) {
                    advance(p);
                    el = ast2(AST_ARRAY_ELEM, eline, parse_variable(p), first);
                    el->attr = ZEND_ARRAY_ELEM_BY_REF;
                } else {
                    el = ast2(AST_ARRAY_ELEM, eline, parse_expr(p), first);
                }
            } else {
                el = ast2(AST_ARRAY_ELEM, eline, first, NULL);
            }
        }
        zend_ast_list_add(list, el);
        trailing_empty = 0;
        if (!accept(p, T_COMMA)) {
            break;
        }
    }
    expect(p, close);

    /* a single trailing comma leaves one empty slot behind; drop it */
    if (trailing_empty && list->nchild) {
        list->nchild--;
    }
    return list;
}

/* ===================================================================== */
/*  primaries, postfix chains, unary and binary operators                 */
/* ===================================================================== */

static zend_ast *parse_closure(parser *p, uint32_t line, uint32_t flags, HashTable *attrs);
/* VERSIONGATE:70400 BEGIN - arrow functions */
static zend_ast *parse_arrow_fn(parser *p, uint32_t line, uint32_t flags, HashTable *attrs);
/* VERSIONGATE:70400 END */
static zend_ast *parse_class_name_reference(parser *p);
static zend_ast *parse_postfix(parser *p, zend_ast *node);

/* index just past the token that closes a bracket run starting at `i` */
static uint32_t skip_balanced(parser *p, uint32_t i, php_token_type open, php_token_type close)
{
    int depth = 0;

    for (; i < p->n; i++) {
        php_token_type t = p->t[i].tok.type;
        if (t == open) {
            depth++;
        } else if (t == close) {
            if (--depth == 0) {
                return i + 1;
            }
        } else if (t == T_EOF) {
            break;
        }
    }
    return i;
}

/* "$a{0}" was a string/array offset until PHP 8.  A "{" after a variable is a
 * block everywhere else, so only treat it as an offset when the braces hold a
 * single expression (no ";" before the matching "}"). */
static int looks_like_curly_offset(parser *p, zend_ast *node)
{
    uint32_t i;
    int depth = 0;

    if (!node) {
        return 0;
    }
    switch (node->kind) {
    case AST_VAR: case AST_DIM: case AST_PROP:
    case AST_NULLSAFE_PROP: case AST_STATIC_PROP:
        break;
    default:
        return 0;
    }
    for (i = p->pos; i < p->n; i++) {
        php_token_type t = p->t[i].tok.type;
        if (t == T_LBRACE || t == T_CURLY_OPEN || t == T_DOLLAR_OPEN_CURLY_BRACES) {
            depth++;
        } else if (t == T_RBRACE) {
            if (--depth == 0) {
                return i > p->pos + 1;          /* reject the empty "{}" */
            }
        } else if (depth == 1 && t == T_SEMICOLON) {
            return 0;                            /* a statement: this is a block */
        } else if (t == T_EOF) {
            break;
        }
    }
    return 0;
}

/* php-src `new_variable`: the dereference chain allowed in a class name
 * reference.  It never contains a call. */
static zend_ast *parse_new_variable_chain(parser *p, zend_ast *node)
{
    for (;;) {
        uint32_t line = cur_line(p);

        if (cur_type(p) == T_LBRACKET) {
            zend_ast *off = NULL;
            advance(p);
            if (cur_type(p) != T_RBRACKET) {
                off = parse_expr(p);
            }
            expect(p, T_RBRACKET);
            node = ast2(AST_DIM, line, node, off);
        } else if (cur_type(p) == T_OBJECT_OPERATOR) {
            advance(p);
            node = ast2(AST_PROP, line, node, parse_property_name(p));
        }
        /* VERSIONGATE:80000 BEGIN - nullsafe operator in a class name reference */
        else if (cur_type(p) == T_NULLSAFE_OBJECT_OPERATOR) {
            if (PHP_TARGET_ID < 80000) version_gate(p, "syntax error, unexpected '?->'");
            advance(p);
            node = ast2(AST_NULLSAFE_PROP, line, node, parse_property_name(p));
        }
        /* VERSIONGATE:80000 END */
        else if (cur_type(p) == T_PAAMAYIM_NEKUDOTAYIM &&
                   (peek_type(p, 1) == T_VARIABLE || peek_type(p, 1) == T_DOLLAR)) {
            zend_ast *sv;
            advance(p);
            sv = parse_simple_variable(p);
            node = ast2(AST_STATIC_PROP, line, node, sv->child[0]);
        } else {
            return node;
        }
    }
}

static zend_ast *parse_class_name_reference(parser *p)
{
    zend_ast *node;

    if (cur_type(p) == T_LPAREN) {
        advance(p);
        node = parse_expr(p);
        expect(p, T_RPAREN);
        return node;
    }
    if (cur_type(p) == T_VARIABLE || cur_type(p) == T_DOLLAR) {
        return parse_new_variable_chain(p, parse_simple_variable(p));
    }
    node = parse_class_name(p);
    if (cur_type(p) == T_PAAMAYIM_NEKUDOTAYIM &&
        (peek_type(p, 1) == T_VARIABLE || peek_type(p, 1) == T_DOLLAR)) {
        zend_ast *sv;
        uint32_t line = cur_line(p);
        advance(p);
        sv = parse_simple_variable(p);
        node = ast2(AST_STATIC_PROP, line, node, sv->child[0]);
        return parse_new_variable_chain(p, node);
    }
    return node;
}

static int is_class_modifier_tok(php_token_type t)
{
    return t == T_ABSTRACT || t == T_FINAL || t == T_READONLY;
}

static uint32_t class_modifier_flag(php_token_type t)
{
    switch (t) {
    case T_ABSTRACT: return ZEND_ACC_ABSTRACT;
    case T_FINAL:    return ZEND_ACC_FINAL;
    case T_READONLY: return ZEND_ACC_READONLY;
    default:         return 0;
    }
}

static zend_ast *parse_new(parser *p)
{
    uint32_t line = cur_line(p);
    HashTable *attrs = NULL;
    zend_ast *cls, *args;

    advance(p);                                    /* "new" */

    /* VERSIONGATE:80000 BEGIN - attributes on an anonymous class */
    if (cur_type(p) == T_ATTRIBUTE) {
        if (PHP_TARGET_ID < 80000) version_gate(p, "syntax error, unexpected '#['");
        attrs = parse_attribute_groups(p);
    }
    /* VERSIONGATE:80000 END */

    /* VERSIONGATE:80100 else-branch is the older behaviour: no "new" in a
       constant expression */
    if (PHP_TARGET_ID >= 80100) {
        /* "new" may appear in an initialiser */
    } else {
        if (p->in_initializer) {
            version_gate(p, "syntax error, unexpected 'new'");
        }
    }

    /* VERSIONGATE:70000 BEGIN - anonymous classes */
    {
        uint32_t i = p->pos, flags = 0;
        while (i < p->n && is_class_modifier_tok(p->t[i].tok.type)) {
            i++;
        }
        if (i < p->n && p->t[i].tok.type == T_CLASS) {
            zend_ast *anon_args = NULL;
            zend_ast *decl;
            if (PHP_TARGET_ID < 70000) version_gate(p, "syntax error, unexpected 'class'");
            while (is_class_modifier_tok(cur_type(p))) {
                flags = add_class_modifier(p, flags, class_modifier_flag(cur_type(p)),
                                           cur_line(p));
                advance(p);
            }
            decl = parse_class_decl(p, flags, line, attrs, 1, &anon_args);
            return ast2(AST_NEW, line, decl, anon_args);
        }
    }
    /* VERSIONGATE:70000 END */

    cls = parse_class_name_reference(p);
    if (cur_type(p) == T_LPAREN) {
        args = parse_args(p);
    } else {
        args = ast_list(AST_LIST, line);
    }
    return ast2(AST_NEW, line, cls, args);
}

/* VERSIONGATE:80000 BEGIN - match expressions */
static zend_ast *parse_match(parser *p)
{
    if (PHP_TARGET_ID < 80000) version_gate(p, "syntax error, unexpected 'match'");
    {
    uint32_t line = cur_line(p);
    zend_ast *subject, *arms;

    advance(p);                                    /* "match" */
    expect(p, T_LPAREN);
    subject = parse_expr(p);
    expect(p, T_RPAREN);
    expect(p, T_LBRACE);

    arms = ast_list(AST_LIST, line);
    while (cur_type(p) != T_RBRACE) {
        uint32_t aline = cur_line(p);
        zend_ast *conds = NULL;

        if (cur_type(p) == T_DEFAULT &&
            (peek_type(p, 1) == T_DOUBLE_ARROW ||
             (peek_type(p, 1) == T_COMMA && peek_type(p, 2) == T_DOUBLE_ARROW))) {
            advance(p);
            accept(p, T_COMMA);
        } else {
            conds = ast_list(AST_LIST, aline);
            for (;;) {
                zend_ast_list_add(conds, parse_expr(p));
                if (!accept(p, T_COMMA)) {
                    break;
                }
                if (cur_type(p) == T_DOUBLE_ARROW) {
                    break;                          /* trailing comma */
                }
            }
        }
        expect(p, T_DOUBLE_ARROW);
        zend_ast_list_add(arms, ast2(AST_MATCH_ARM, aline, conds, parse_expr(p)));
        if (!accept(p, T_COMMA)) {
            break;
        }
    }
    expect(p, T_RBRACE);
    return ast2(AST_MATCH, line, subject, arms);
    }
}
/* VERSIONGATE:80000 END */

/* "match", "fn" and "readonly" are only keywords in context; when they are
 * followed by something that cannot continue their own syntax they are plain
 * identifiers (a function call, a constant, ...). */
/* VERSIONGATE:80000 BEGIN - match expressions */
static int match_is_expression(parser *p)
{
    uint32_t j;

    if (peek_type(p, 1) != T_LPAREN) {
        return 0;
    }
    j = skip_balanced(p, p->pos + 1, T_LPAREN, T_RPAREN);
    return j < p->n && p->t[j].tok.type == T_LBRACE;
}
/* VERSIONGATE:80000 END */

/* VERSIONGATE:70400 BEGIN - arrow functions */
static int fn_is_arrow_function(parser *p)
{
    uint32_t i = p->pos + 1, j;

    if (i < p->n && p->t[i].tok.type == T_AMP) {
        i++;
    }
    if (i >= p->n || p->t[i].tok.type != T_LPAREN) {
        return 0;
    }
    j = skip_balanced(p, i, T_LPAREN, T_RPAREN);
    if (j >= p->n) {
        return 0;
    }
    return p->t[j].tok.type == T_DOUBLE_ARROW || p->t[j].tok.type == T_COLON;
}
/* VERSIONGATE:70400 END */

/* turn a contextual keyword back into a plain name node */
static zend_ast *ident_as_name(parser *p)
{
    php_token *tok = cur_tok(p);
    zend_ast *ast = ast_zstr(tok_text_of(p, tok), tok->lineno);

    advance(p);
    return ast;
}

static zend_ast *parse_primary(parser *p)
{
    php_token *tok = cur_tok(p);
    uint32_t line = tok->lineno;
    zend_ast *node;
    uint16_t ctype;

    switch (tok->type) {
    case T_VARIABLE:
    case T_DOLLAR:
        return parse_simple_variable(p);

    case T_LNUMBER:
        advance(p);
        return zend_ast_create_zval(zval_long(tok->lval), line);

    case T_DNUMBER:
        advance(p);
        return zend_ast_create_zval(zval_double(tok->dval), line);

    case T_CONSTANT_ENCAPSED_STRING:
        advance(p);
        return ast_zstr(tok_text_of(p, tok), line);

    case T_DQ_START:
        advance(p);
        return parse_encaps_list(p, T_DQ_END, line);

    case T_START_HEREDOC:
        advance(p);
        return parse_encaps_list(p, T_END_HEREDOC, line);

    case T_BACKQUOTE:
        advance(p);
        return ast1(AST_SHELL_EXEC, line, parse_encaps_list(p, T_BACKQUOTE, line));

    case T_LBRACKET:
        advance(p);
        return parse_array_literal(p, T_RBRACKET, line);

    case T_ARRAY:
        if (peek_type(p, 1) == T_LPAREN) {
            advance(p);
            advance(p);
            return parse_array_literal(p, T_RPAREN, line);
        }
        return ast_cstr("array", line);            /* used as a bare name */

    case T_LIST: {
        zend_ast *lst;
        advance(p);
        expect(p, T_LPAREN);
        lst = parse_array_literal(p, T_RPAREN, line);
        return to_list_assign(p, lst, ZEND_LIST_SYNTAX_LIST);
    }

    case T_LPAREN:
        advance(p);
        node = parse_expr(p);
        expect(p, T_RPAREN);
        if (node->kind == AST_NEW) {
            node->attr |= ZEND_NEW_PARENTHESIZED;
        }
        /* VERSIONGATE:70000 else-branch is the older behaviour: only "(new X)"
           may be dereferenced */
        if (PHP_TARGET_ID >= 70000) {
            /* uniform variable syntax: any "(expr)" is dereferenceable */
        } else {
            switch (cur_type(p)) {
            case T_OBJECT_OPERATOR: case T_NULLSAFE_OBJECT_OPERATOR:
            case T_LBRACKET: case T_PAAMAYIM_NEKUDOTAYIM: case T_LPAREN:
                if (node->kind != AST_NEW) {
                    version_gate(p, "syntax error, unexpected dereference");
                }
                break;
            default:
                break;
            }
        }
        return node;

    case T_NEW:
        return parse_new(p);

    case T_EXIT: {
        zend_ast *arg = NULL;
        advance(p);
        if (cur_type(p) == T_LPAREN) {
            zend_ast *args = parse_args(p);
            if (args->kind != AST_FIRST_CLASS_CALLABLE && args->nchild) {
                arg = args->child[0];
            }
        }
        return ast1(AST_EXIT, line, arg);
    }

    case T_ISSET: {
        zend_ast *lst;
        advance(p);
        expect(p, T_LPAREN);
        lst = ast_list(AST_ISSET, line);
        while (cur_type(p) != T_RPAREN) {
            zend_ast_list_add(lst, parse_expr(p));
            if (!accept(p, T_COMMA)) {
                break;
            }
        }
        expect(p, T_RPAREN);
        return lst;
    }

    case T_EMPTY:
        advance(p);
        expect(p, T_LPAREN);
        node = ast1(AST_EMPTY, line, parse_expr(p));
        expect(p, T_RPAREN);
        return node;

    case T_EVAL:
        advance(p);
        expect(p, T_LPAREN);
        node = ast1(AST_INCLUDE, line, parse_expr(p));
        node->attr = ZEND_INCLUDE_EVAL;
        expect(p, T_RPAREN);
        return node;

    case T_MATCH:
        /* VERSIONGATE:80000 BEGIN - match expressions */
        if (match_is_expression(p)) {
            return parse_match(p);
        }
        /* VERSIONGATE:80000 END */
        node = ident_as_name(p);
        break;

    case T_FN:
        /* VERSIONGATE:70400 BEGIN - arrow functions */
        if (fn_is_arrow_function(p)) {
            advance(p);
            return parse_arrow_fn(p, line, 0, NULL);
        }
        /* VERSIONGATE:70400 END */
        node = ident_as_name(p);
        break;

    case T_FUNCTION:
        advance(p);
        return parse_closure(p, line, 0, NULL);

    case T_STATIC:
        if (peek_type(p, 1) == T_FUNCTION) {
            advance(p);
            advance(p);
            return parse_closure(p, line, ZEND_ACC_STATIC, NULL);
        }
        /* VERSIONGATE:70400 BEGIN - static arrow functions */
        if (peek_type(p, 1) == T_FN) {
            if (PHP_TARGET_ID < 70400) version_gate(p, "syntax error, unexpected 'fn'");
            advance(p);
            advance(p);
            return parse_arrow_fn(p, line, ZEND_ACC_STATIC, NULL);
        }
        /* VERSIONGATE:70400 END */
        advance(p);
        return ast_cstr("static", line);           /* static:: */

    /* VERSIONGATE:80000 BEGIN - attributes before a closure */
    case T_ATTRIBUTE: {
        HashTable *attrs;
        uint32_t flags = 0;
        if (PHP_TARGET_ID < 80000) version_gate(p, "syntax error, unexpected '#['");
        attrs = parse_attribute_groups(p);
        if (accept(p, T_STATIC)) {
            flags = ZEND_ACC_STATIC;
        }
        line = cur_line(p);
        if (cur_type(p) == T_FUNCTION) {
            advance(p);
            return parse_closure(p, line, flags, attrs);
        }
        if (cur_type(p) == T_FN) {
            advance(p);
            return parse_arrow_fn(p, line, flags, attrs);
        }
        unexpected(p, T_FUNCTION);
    }
    /* VERSIONGATE:80000 END */

    case T_NS_C:
        /* php-src folds __NAMESPACE__ at compile time */
        advance(p);
        return ast_zstr(p->ns ? p->ns : zend_string_init("", 0), line);

    case T_LINE: case T_FILE: case T_DIR: case T_FUNC_C: case T_CLASS_C:
    case T_METHOD_C: case T_TRAIT_C: case T_PROPERTY_C:
        advance(p);
        node = ast0(AST_MAGIC_CONST, line);
        node->attr = (uint16_t) tok->type;
        return node;

    case T_READONLY:
        node = ident_as_name(p);
        break;

    case T_STRING:
    case T_NAME_QUALIFIED:
    case T_NAME_FULLY_QUALIFIED:
    case T_NAME_RELATIVE:
        node = parse_name(p);
        break;

    default:
        if (cast_target(tok->type, &ctype)) {
            unexpected(p, T_LAST_TOKEN);           /* handled in parse_unary */
        }
        unexpected(p, T_LAST_TOKEN);
    }

    /* `node` is a bare name: a call, a class name, or a constant */
    if (cur_type(p) == T_LPAREN) {
        return ast2(AST_CALL, line, resolve_name_ast(p, node, NS_FUNCTION),
                    parse_args(p));
    }
    if (cur_type(p) == T_PAAMAYIM_NEKUDOTAYIM) {
        zend_string *nm = node->val.value.str;
        if (node->attr != ZEND_NAME_NOT_FQ ||
            !(zstr_is(nm, "self") || zstr_is(nm, "parent") || zstr_is(nm, "static"))) {
            resolve_name_ast(p, node, NS_CLASS);
        }
        return node;                               /* the postfix loop takes it */
    }
    {
        zend_ast *c = ast0(AST_CONST, line);
        resolve_name_ast(p, node, NS_CONST);
        c->val = node->val;
        c->attr = node->attr;
        return c;
    }
}

static zend_ast *parse_postfix(parser *p, zend_ast *node)
{
    for (;;) {
        uint32_t line = cur_line(p);


            if (node->kind == AST_NEW && !(node->attr & ZEND_NEW_PARENTHESIZED)) {
                return node;
            }


        switch (cur_type(p)) {
        case T_LBRACKET: {
            zend_ast *off = NULL;
            advance(p);
            if (cur_type(p) != T_RBRACKET) {
                off = parse_expr(p);
            }
            expect(p, T_RBRACKET);
            node = ast2(AST_DIM, line, node, off);
            break;
        }
        case T_LBRACE: {
            zend_ast *off;
            /* VERSIONGATE:80000 else-branch is the older behaviour: "$a{0}" was
               a string/array offset until 8.0 removed the syntax */
            if (PHP_TARGET_ID >= 80000) {
                return node;
            } else {
                if (!looks_like_curly_offset(p, node)) {
                    return node;
                }
            }
            advance(p);
            off = parse_expr(p);
            expect(p, T_RBRACE);
            node = ast2(AST_DIM, line, node, off);
            break;
        }
        case T_OBJECT_OPERATOR: {
            zend_ast *name;
            advance(p);
            name = parse_property_name(p);
            if (cur_type(p) == T_LPAREN) {
                node = ast3(AST_METHOD_CALL, line, node, name, parse_args(p));
            } else {
                node = ast2(AST_PROP, line, node, name);
            }
            break;
        }
        /* VERSIONGATE:80000 BEGIN - nullsafe object operator "?->" */
        case T_NULLSAFE_OBJECT_OPERATOR: {
            zend_ast *name;
            if (PHP_TARGET_ID < 80000) version_gate(p, "syntax error, unexpected '?->'");
            advance(p);
            name = parse_property_name(p);
            if (cur_type(p) == T_LPAREN) {
                node = ast3(AST_NULLSAFE_METHOD_CALL, line, node, name, parse_args(p));
            } else {
                node = ast2(AST_NULLSAFE_PROP, line, node, name);
            }
            break;
        }
        /* VERSIONGATE:80000 END */
        case T_PAAMAYIM_NEKUDOTAYIM: {
            advance(p);
            if (cur_type(p) == T_VARIABLE || cur_type(p) == T_DOLLAR) {
                zend_ast *sv = parse_simple_variable(p);
                if (cur_type(p) == T_LPAREN) {
                    /* Foo::$m() calls the method named by $m */
                    node = ast3(AST_STATIC_CALL, line, node, sv, parse_args(p));
                } else {
                    node = ast2(AST_STATIC_PROP, line, node, sv->child[0]);
                }
            }
            else if (cur_type(p) == T_LBRACE) {
                zend_ast *e;
                advance(p);
                e = parse_expr(p);
                expect(p, T_RBRACE);
                if (cur_type(p) == T_LPAREN) {
                    node = ast3(AST_STATIC_CALL, line, node, e, parse_args(p));
                } else {
                    /* VERSIONGATE:80300 else-branch is the older behaviour:
                       "C::{$x}" may only introduce a call */
                    if (PHP_TARGET_ID >= 80300) {
                        node = ast2(AST_CONST_FETCH_DYNAMIC, line, node, e);
                    } else {
                        version_gate(p, "syntax error, unexpected '{'");
                    }
                }
            }
            else {
                zend_ast *name = ast_zstr(parse_identifier(p), line);
                if (cur_type(p) == T_LPAREN) {
                    {
                        node = ast3(AST_STATIC_CALL, line, node, name, parse_args(p));
                    }
                } else {
                    /* VERSIONGATE:80000 else-branch is the older behaviour:
                       "::class" only applies to a class name */
                    if (PHP_TARGET_ID >= 80000) {
                        /* $obj::class is allowed */
                    } else {
                        if (node->kind != AST_ZVAL && zstr_is(name->val.value.str, "class")) {
                            version_gate(p, "syntax error, unexpected 'class'");
                        }
                    }
                    /* php-src resolves "::class" at compile time inside a
                     * constant expression, which "static" (and, before 7.4,
                     * "parent") cannot support. */
                    if (p->in_initializer && node->kind == AST_ZVAL &&
                        node->val.u1 == IS_STRING &&
                        zstr_is(name->val.value.str, "class")) {
                        zend_string *cn = node->val.value.str;
                        if (zstr_is(cn, "static")) {
                            zend_error_at(E_COMPILE_ERROR, p->filename, line,
                                          "static::class cannot be used for "
                                          "compile-time class name resolution");
                        }
                        /* VERSIONGATE:70400 else-branch is the older behaviour */
                        if (PHP_TARGET_ID >= 70400) {
                            /* parent::class is resolved at run time */
                        } else {
                            if (zstr_is(cn, "parent")) {
                                zend_error_at(E_COMPILE_ERROR, p->filename, line,
                                              "parent::class cannot be used for "
                                              "compile-time class name resolution");
                            }
                        }
                    }
                    node = ast2(AST_CLASS_CONST, line, node, name);
                }
            }
            break;
        }
        case T_LPAREN:
            /* VERSIONGATE:70000 else-branch is the older behaviour: the result of
               a call is not itself callable */
            if (PHP_TARGET_ID >= 70000) {
                /* uniform variable syntax: f()() */
            } else {
                if (node->kind == AST_CALL || node->kind == AST_METHOD_CALL ||
                    node->kind == AST_STATIC_CALL) {
                    version_gate(p, "syntax error, unexpected '('");
                }
            }
            node = ast2(AST_CALL, line, node, parse_args(p));
            break;
        default:
            return node;
        }
    }
}

static zend_ast *parse_variable(parser *p)
{
    return parse_postfix(p, parse_primary(p));
}

static zend_ast *parse_unary(parser *p)
{
    php_token_type t = cur_type(p);
    uint32_t line = cur_line(p);
    zend_ast *node;
    uint16_t ctype;

    switch (t) {
    case T_BANG:
        advance(p);
        node = ast1(AST_UNARY, line, parse_expr_prec(p, PREC_NOT));
        node->attr = UN_NOT;
        return node;
    case T_TILDE:
        advance(p);
        node = ast1(AST_UNARY, line, parse_expr_prec(p, PREC_CAST));
        node->attr = UN_BNOT;
        return node;
    case T_PLUS:
        advance(p);
        node = ast1(AST_UNARY, line, parse_expr_prec(p, PREC_CAST));
        node->attr = UN_PLUS;
        return node;
    case T_MINUS:
        advance(p);
        node = ast1(AST_UNARY, line, parse_expr_prec(p, PREC_CAST));
        node->attr = UN_MINUS;
        return node;
    case T_AT:
        advance(p);
        return ast1(AST_SILENCE, line, parse_expr_prec(p, PREC_CAST));
    case T_INC: case T_DEC:
        advance(p);
        node = ast1(AST_INC_DEC, line, parse_variable(p));
        node->attr = (t == T_DEC) ? ZEND_INCDEC_DEC : 0;
        return node;
    case T_PRINT:
        advance(p);
        return ast1(AST_PRINT, line, parse_expr_prec(p, PREC_PRINT));
    case T_CLONE:
        advance(p);
        return ast1(AST_CLONE, line, parse_expr_prec(p, PREC_CLONE));
    /* VERSIONGATE:80000 BEGIN - throw as an expression */
    case T_THROW:
        if (PHP_TARGET_ID < 80000) version_gate(p, "syntax error, unexpected 'throw'");
        advance(p);
        return ast1(AST_THROW_EXPR, line, parse_expr_prec(p, PREC_OR));
    /* VERSIONGATE:80000 END */
    /* VERSIONGATE:70000 BEGIN - "yield from" */
    case T_YIELD_FROM:
        if (PHP_TARGET_ID < 70000) version_gate(p, "syntax error, unexpected 'yield'");
        advance(p);
        p->saw_yield = 1;
        return ast1(AST_YIELD_FROM, line, parse_expr_prec(p, PREC_YIELD_FROM));
    /* VERSIONGATE:70000 END */
    case T_YIELD: {
        zend_ast *val = NULL, *key = NULL;
        advance(p);
        p->saw_yield = 1;
        switch (cur_type(p)) {
        case T_SEMICOLON: case T_RPAREN: case T_RBRACKET: case T_RBRACE:
        case T_COMMA: case T_EOF: case T_COLON:
            break;                                  /* a bare "yield" */
        default:
            val = parse_expr_prec(p, PREC_ARROW + 1);
            if (accept(p, T_DOUBLE_ARROW)) {
                key = val;
                val = parse_expr_prec(p, PREC_ARROW + 1);
            }
        }
        return ast2(AST_YIELD, line, val, key);
    }
    case T_INCLUDE: case T_INCLUDE_ONCE: case T_REQUIRE: case T_REQUIRE_ONCE: {
        uint16_t kind = (t == T_INCLUDE)      ? ZEND_INCLUDE_PLAIN :
                        (t == T_INCLUDE_ONCE) ? ZEND_INCLUDE_ONCE_ :
                        (t == T_REQUIRE)      ? ZEND_REQUIRE_PLAIN : ZEND_REQUIRE_ONCE_;
        advance(p);
        node = ast1(AST_INCLUDE, line, parse_expr_prec(p, PREC_OR));
        node->attr = kind;
        return node;
    }
    default:
        break;
    }

    if (cast_target(t, &ctype)) {
        advance(p);
        node = ast1(AST_CAST, line, parse_expr_prec(p, PREC_CAST));
        node->attr = ctype;
        return node;
    }

    node = parse_variable(p);

    if ((cur_type(p) == T_INC || cur_type(p) == T_DEC) && is_assignable(node)) {
        uint16_t attr = ZEND_INCDEC_POST |
                        (cur_type(p) == T_DEC ? ZEND_INCDEC_DEC : 0);
        zend_ast *r = ast1(AST_INC_DEC, node->lineno, node);
        r->attr = attr;
        advance(p);
        return r;
    }
    return node;
}

static zend_ast *parse_expr_prec(parser *p, int minprec)
{
    zend_ast *left = parse_unary(p);

    for (;;) {
        php_token_type t = cur_type(p);
        uint32_t line = cur_line(p);
        zend_binop op = BIN_ADD;
        int prec = 0, ra = 0, kind;

        /* VERSIONGATE:70100 BEGIN - short array destructuring "[$a, $b] = ..." */
        if (t == T_ASSIGN && left->kind == AST_ARRAY) {
            if (PHP_TARGET_ID < 70100) version_gate(p, "syntax error, unexpected '='");
            advance(p);
            left = ast2(AST_ASSIGN, line,
                        to_list_assign(p, left, ZEND_LIST_SYNTAX_SHORT),
                        parse_expr_prec(p, PREC_ASSIGN));
            continue;
        }
        /* VERSIONGATE:70100 END */

        /* --- assignment ------------------------------------------------ */
        if (is_assignable(left)) {
            if (t == T_ASSIGN) {
                advance(p);
                if (cur_type(p) == T_AMP) {
                    advance(p);
                    /* PHP 5 also allowed "$a = &new Foo" */
                    left = ast2(AST_ASSIGN_REF, line, left,
                                cur_type(p) == T_NEW ? parse_new(p) : parse_variable(p));
                } else {
                    left = ast2(AST_ASSIGN, line, left, parse_expr_prec(p, PREC_ASSIGN));
                }
                continue;
            }
            /* VERSIONGATE:70400 BEGIN - null coalescing assignment "??=" */
            if (t == T_COALESCE_EQUAL) {
                if (PHP_TARGET_ID < 70400) version_gate(p, "syntax error, unexpected '?\?='");
                advance(p);
                left = ast2(AST_ASSIGN_COALESCE, line, left,
                            parse_expr_prec(p, PREC_ASSIGN));
                continue;
            }
            /* VERSIONGATE:70400 END */
            if (assignop_of(t, &op)) {
                advance(p);
                left = ast2(AST_ASSIGN_OP, line, left, parse_expr_prec(p, PREC_ASSIGN));
                left->attr = (uint16_t) op;
                continue;
            }
        }

        /* --- ternary ---------------------------------------------------- */
        if (t == T_QUESTION && PREC_TERNARY >= minprec) {
            zend_ast *mid = NULL, *els;
            advance(p);
            if (!accept(p, T_COLON)) {
                mid = parse_expr_prec(p, PREC_OR);
                expect(p, T_COLON);
            }
            els = parse_expr_prec(p, PREC_TERNARY + 1);
            left = ast3(AST_TERNARY, line, left, mid, els);
            continue;
        }

        /* VERSIONGATE:70000 BEGIN - null coalescing operator "??" */
        if (t == T_COALESCE && PREC_COALESCE >= minprec) {
            if (PHP_TARGET_ID < 70000) version_gate(p, "syntax error, unexpected '?\?'");
            advance(p);
            left = ast2(AST_COALESCE, line, left, parse_expr_prec(p, PREC_COALESCE));
            continue;
        }
        /* VERSIONGATE:70000 END */

        /* VERSIONGATE:70000 BEGIN - spaceship operator "<=>" */
        if (t == T_SPACESHIP && PREC_EQ >= minprec) {
            if (PHP_TARGET_ID < 70000) version_gate(p, "syntax error, unexpected '<=>'");
            advance(p);
            left = ast2(AST_BINARY, line, left, parse_expr_prec(p, PREC_EQ + 1));
            left->attr = BIN_SPACESHIP;
            continue;
        }
        /* VERSIONGATE:70000 END */

        /* --- instanceof -------------------------------------------------- */
        if (t == T_INSTANCEOF && PREC_INSTANCEOF >= minprec) {
            advance(p);
            left = ast2(AST_INSTANCEOF, line, left, parse_class_name_reference(p));
            continue;
        }

        /* --- short circuit boolean --------------------------------------- */
        if (t == T_BOOLEAN_OR || t == T_LOGICAL_OR ||
            t == T_BOOLEAN_AND || t == T_LOGICAL_AND) {
            int is_or = (t == T_BOOLEAN_OR || t == T_LOGICAL_OR);
            prec = (t == T_BOOLEAN_OR)  ? PREC_BOOL_OR :
                   (t == T_BOOLEAN_AND) ? PREC_BOOL_AND :
                   (t == T_LOGICAL_OR)  ? PREC_OR : PREC_AND;
            if (prec < minprec) {
                break;
            }
            advance(p);
            left = ast2(is_or ? AST_OR : AST_AND, line, left,
                        parse_expr_prec(p, prec + 1));
            continue;
        }

        /* --- the rest of the binary operators ---------------------------- */
        kind = binop_of(t, &prec, &ra, &op);
        if (kind != 2 || prec < minprec) {
            break;
        }
        advance(p);
        left = ast2(AST_BINARY, line, left, parse_expr_prec(p, ra ? prec : prec + 1));
        left->attr = (uint16_t) op;
    }
    return left;
}

static zend_ast *parse_expr(parser *p)
{
    return parse_expr_prec(p, PREC_THROW);
}

/* A constant expression: property and parameter defaults, class constants and
 * enum case values.  The flag lets parse_new() refuse "new" there before 8.1. */
static zend_ast *parse_const_expr(parser *p)
{
    zend_ast *e;
    int outer = p->in_initializer;

    p->in_initializer = 1;
    e = parse_expr(p);
    p->in_initializer = outer;
    return e;
}

/* ===================================================================== */
/*  attributes, parameters, functions                                     */
/* ===================================================================== */

static const php_token_type STOP_RBRACE[]     = { T_RBRACE, T_LAST_TOKEN };
static const php_token_type STOP_ENDIF[]      = { T_ENDIF, T_ELSE, T_ELSEIF, T_LAST_TOKEN };
static const php_token_type STOP_ENDWHILE[]   = { T_ENDWHILE, T_LAST_TOKEN };
static const php_token_type STOP_ENDFOR[]     = { T_ENDFOR, T_LAST_TOKEN };
static const php_token_type STOP_ENDFOREACH[] = { T_ENDFOREACH, T_LAST_TOKEN };
static const php_token_type STOP_ENDDECLARE[] = { T_ENDDECLARE, T_LAST_TOKEN };
static const php_token_type STOP_CASE[]       = { T_CASE, T_DEFAULT, T_ENDSWITCH,
                                                  T_RBRACE, T_LAST_TOKEN };

/* #[Attr(1, name: 2)] ... -- a table of name => AST_LIST of arguments */
/* VERSIONGATE:80000 BEGIN - attributes */
static HashTable *parse_attribute_groups(parser *p)
{
    HashTable *ht = NULL;

    if (PHP_TARGET_ID < 80000) version_gate(p, "syntax error, unexpected '#['");
    while (cur_type(p) == T_ATTRIBUTE) {
        advance(p);
        while (cur_type(p) != T_RBRACKET) {
            uint32_t line = cur_line(p);
            zend_ast *cls = parse_class_name(p);
            zend_ast *args = (cur_type(p) == T_LPAREN) ? parse_args(p)
                                                       : ast_list(AST_LIST, line);
            zend_string *nm = ast_name_str(cls);
            if (!ht) {
                ht = zend_hash_new();
            }
            if (nm) {
                zend_hash_update(ht, nm, ZEND_PTR_ZVAL(args));
            }
            if (!accept(p, T_COMMA)) {
                break;
            }
        }
        expect(p, T_RBRACKET);
    }
    return ht;
}
/* VERSIONGATE:80000 END */

static uint32_t member_modifier_flag(php_token_type t)
{
    switch (t) {
    case T_PUBLIC:    return ZEND_ACC_PUBLIC;
    case T_PROTECTED: return ZEND_ACC_PROTECTED;
    case T_PRIVATE:   return ZEND_ACC_PRIVATE;
    case T_STATIC:    return ZEND_ACC_STATIC;
    case T_ABSTRACT:  return ZEND_ACC_ABSTRACT;
    case T_FINAL:     return ZEND_ACC_FINAL;
    case T_READONLY:  return ZEND_ACC_READONLY;
    default:          return 0;
    }
}

static uint32_t set_modifier_flag(php_token_type t)
{
    switch (t) {
    case T_PUBLIC_SET:    return ZEND_ACC_PUBLIC;
    case T_PROTECTED_SET: return ZEND_ACC_PROTECTED;
    case T_PRIVATE_SET:   return ZEND_ACC_PRIVATE;
    default:              return 0;
    }
}

static int is_member_modifier(php_token_type t)
{
    return member_modifier_flag(t) != 0 || set_modifier_flag(t) != 0;
}

/* php-src rejects a repeated or contradictory modifier while it is still
 * building the member, with a compile error rather than a syntax error. */
static uint32_t add_member_modifier(parser *p, uint32_t flags, uint32_t add,
                                    uint32_t line)
{
    uint32_t both = flags | add;

    if ((flags & ZEND_ACC_VISIBILITY) && (add & ZEND_ACC_VISIBILITY)) {
        zend_error_at(E_COMPILE_ERROR, p->filename, line,
                      "Multiple access type modifiers are not allowed");
    } else if ((flags & ZEND_ACC_ABSTRACT) && (add & ZEND_ACC_ABSTRACT)) {
        zend_error_at(E_COMPILE_ERROR, p->filename, line,
                      "Multiple abstract modifiers are not allowed");
    } else if ((flags & ZEND_ACC_STATIC) && (add & ZEND_ACC_STATIC)) {
        zend_error_at(E_COMPILE_ERROR, p->filename, line,
                      "Multiple static modifiers are not allowed");
    } else if ((flags & ZEND_ACC_FINAL) && (add & ZEND_ACC_FINAL)) {
        zend_error_at(E_COMPILE_ERROR, p->filename, line,
                      "Multiple final modifiers are not allowed");
    } else if ((both & ZEND_ACC_ABSTRACT) && (both & ZEND_ACC_FINAL)) {
        zend_error_at(E_COMPILE_ERROR, p->filename, line,
                      "Cannot use the final modifier on an abstract class member");
    }
    return both;
}

static uint32_t add_class_modifier(parser *p, uint32_t flags, uint32_t add,
                                   uint32_t line)
{
    uint32_t both = flags | add;

    if ((flags & ZEND_ACC_ABSTRACT) && (add & ZEND_ACC_ABSTRACT)) {
        zend_error_at(E_COMPILE_ERROR, p->filename, line,
                      "Multiple abstract modifiers are not allowed");
    } else if ((flags & ZEND_ACC_FINAL) && (add & ZEND_ACC_FINAL)) {
        zend_error_at(E_COMPILE_ERROR, p->filename, line,
                      "Multiple final modifiers are not allowed");
    } else if ((both & ZEND_ACC_ABSTRACT) && (both & ZEND_ACC_FINAL)) {
        zend_error_at(E_COMPILE_ERROR, p->filename, line,
                      "Cannot use the final modifier on an abstract class");
    }
    return both;
}


/* one parameter of a function, method, closure or arrow function */
static void parse_parameter(parser *p, zend_arg_info *arg)
{
    uint32_t promote = 0, promote_set = 0;
    int readonly = 0;

    memset(arg, 0, sizeof(*arg));

    /* VERSIONGATE:80000 BEGIN - attributes on a parameter */
    if (cur_type(p) == T_ATTRIBUTE) {
        parse_attribute_groups(p);                 /* parameter attributes */
    }
    /* VERSIONGATE:80000 END */
    /* VERSIONGATE:80000 BEGIN - constructor property promotion */
    while (is_member_modifier(cur_type(p))) {
        uint32_t f = member_modifier_flag(cur_type(p));
        uint32_t s = set_modifier_flag(cur_type(p));
        if (PHP_TARGET_ID < 80000) version_gate(p, "syntax error, unexpected modifier");
        /* VERSIONGATE:80100 else-branch is the older behaviour: no readonly */
        if (PHP_TARGET_ID >= 80100) {
            /* readonly promotion is allowed */
        } else {
            if (cur_type(p) == T_READONLY) {
                version_gate(p, "syntax error, unexpected 'readonly'");
            }
        }

            if (s) {
                version_gate(p, "syntax error, unexpected set visibility modifier");
            }

        if (f == ZEND_ACC_READONLY) {
            readonly = 1;
            promote |= ZEND_ACC_READONLY;
        } else if (f) {
            promote = add_member_modifier(p, promote, f, cur_line(p));
        } else {
            promote_set |= s;
        }
        advance(p);
    }
    /* VERSIONGATE:80000 END */

    /* a type is present unless the next token already starts the variable */
    if (cur_type(p) != T_AMP && cur_type(p) != T_ELLIPSIS &&
        cur_type(p) != T_VARIABLE) {
        arg->type = parse_type_expr(p);
    }
    if (accept(p, T_AMP)) {
        arg->by_ref = 1;
    }
    if (accept(p, T_ELLIPSIS)) {
        arg->variadic = 1;
    }
    if (cur_type(p) != T_VARIABLE) {
        unexpected(p, T_VARIABLE);
    }
    arg->name = tok_text_of(p, cur_tok(p));
    advance(p);

    if (accept(p, T_ASSIGN)) {
        arg->default_ast = parse_const_expr(p);
    }

    if (promote || promote_set) {
        if (!(promote & ZEND_ACC_VISIBILITY)) {
            promote |= ZEND_ACC_PUBLIC;
        }
        arg->promote_flags = promote;
        arg->promote_set_flags = promote_set;
        arg->promote_readonly = (unsigned char) readonly;
    }
}

static void parse_param_list(parser *p, zend_arg_info **out, uint32_t *nout,
                             uint32_t *flags, uint32_t *min_args)
{
    zend_arg_info *args = NULL;
    uint32_t n = 0, cap = 0, req = 0;
    int seen_optional = 0;

    expect(p, T_LPAREN);
    while (cur_type(p) != T_RPAREN) {
        if (n == cap) {
            cap = cap ? cap * 2 : 8;
            args = (zend_arg_info *) erealloc(args, cap * sizeof(zend_arg_info));
        }
        parse_parameter(p, &args[n]);
        if (args[n].variadic) {
            *flags |= ZEND_ACC_VARIADIC;
            seen_optional = 1;
        } else if (args[n].default_ast) {
            seen_optional = 1;
        } else if (!seen_optional) {
            req = n + 1;
        }
        n++;
        if (!accept(p, T_COMMA)) {
            break;
        }
        /* VERSIONGATE:80000 else-branch is the older behaviour: no trailing comma */
        if (PHP_TARGET_ID >= 80000) {
            if (cur_type(p) == T_RPAREN) break;
        } else {
            if (cur_type(p) == T_RPAREN) version_gate(p, "syntax error, unexpected ')'");
        }
    }
    expect(p, T_RPAREN);
    *out = args;
    *nout = n;
    *min_args = req;
}

static zend_function *fn_new(parser *p, zend_string *name, uint32_t line)
{
    zend_function *fn = (zend_function *) ecalloc(1, sizeof(zend_function));

    fn->name = name;
    fn->lcname = name ? lc(name) : NULL;
    fn->filename = p->filename;
    fn->lineno = line;
    fn->end_lineno = line;
    return fn;
}

static zend_ast *parse_body_block(parser *p, zend_function *fn)
{
    zend_ast *body;
    int outer = p->saw_yield;

    p->saw_yield = 0;
    expect(p, T_LBRACE);
    body = parse_stmt_list(p, STOP_RBRACE);
    fn->end_lineno = cur_line(p);
    expect(p, T_RBRACE);
    if (p->saw_yield) {
        fn->flags |= ZEND_ACC_GENERATOR;
    }
    p->saw_yield = outer;
    return body;
}

/* "function" has already been consumed */
static zend_function *parse_named_function(parser *p, uint32_t line, uint32_t flags,
                                           HashTable *attrs, zend_string *doc,
                                           int allow_abstract)
{
    zend_function *fn;
    zend_string *name;

    if (accept(p, T_AMP)) {
        flags |= ZEND_ACC_RETURN_REF;
    }
    name = parse_identifier(p);
    fn = fn_new(p, name, line);
    fn->flags = flags;
    fn->attributes = attrs;
    fn->doc_comment = doc;
    fn->returns_ref = (flags & ZEND_ACC_RETURN_REF) ? 1 : 0;

    parse_param_list(p, &fn->args, &fn->num_args, &fn->flags, &fn->min_args);
    /* VERSIONGATE:70000 BEGIN - return type declarations */
    if (cur_type(p) == T_COLON) {
        if (PHP_TARGET_ID < 70000) version_gate(p, "syntax error, unexpected ':'");
        advance(p);
        fn->return_type = parse_type_expr(p);
    }
    /* VERSIONGATE:70000 END */
    if (cur_type(p) == T_SEMICOLON && allow_abstract) {
        fn->end_lineno = cur_line(p);
        advance(p);
        fn->is_abstract = 1;
        fn->flags |= ZEND_ACC_ABSTRACT;
        fn->body = NULL;
    } else {
        fn->body = parse_body_block(p, fn);
    }
    if (fn->lcname && zstr_is(fn->lcname, "__construct")) {
        fn->flags |= ZEND_ACC_CTOR;
    }
    return fn;
}

/* "function" has already been consumed; builds AST_CLOSURE */
static zend_ast *parse_closure(parser *p, uint32_t line, uint32_t flags, HashTable *attrs)
{
    zend_function *fn;
    zend_ast *uses = NULL;
    zend_ast *node;

    if (accept(p, T_AMP)) {
        flags |= ZEND_ACC_RETURN_REF;
    }
    fn = fn_new(p, zend_string_initz("{closure}"), line);
    fn->flags = flags;
    fn->attributes = attrs;
    fn->returns_ref = (flags & ZEND_ACC_RETURN_REF) ? 1 : 0;

    parse_param_list(p, &fn->args, &fn->num_args, &fn->flags, &fn->min_args);

    if (accept(p, T_USE)) {
        uses = ast_list(AST_LIST, cur_line(p));
        expect(p, T_LPAREN);
        while (cur_type(p) != T_RPAREN) {
            uint32_t uline = cur_line(p);
            int by_ref = accept(p, T_AMP);
            zend_ast *u;
            if (cur_type(p) != T_VARIABLE) {
                unexpected(p, T_VARIABLE);
            }
            u = ast_zstr(tok_text_of(p, cur_tok(p)), uline);
            u->attr = by_ref ? 1 : 0;
            advance(p);
            zend_ast_list_add(uses, u);
            if (!accept(p, T_COMMA)) {
                break;
            }
        }
        expect(p, T_RPAREN);
    }
    /* VERSIONGATE:70000 BEGIN - return type declarations on a closure */
    if (cur_type(p) == T_COLON) {
        if (PHP_TARGET_ID < 70000) version_gate(p, "syntax error, unexpected ':'");
        advance(p);
        fn->return_type = parse_type_expr(p);
    }
    /* VERSIONGATE:70000 END */
    fn->body = parse_body_block(p, fn);

    node = ast1(AST_CLOSURE, line, uses);
    node->val = ZEND_PTR_ZVAL(fn);
    return node;
}

/* "fn" has already been consumed; builds AST_ARROW_FN */
/* VERSIONGATE:70400 BEGIN - arrow functions */
static zend_ast *parse_arrow_fn(parser *p, uint32_t line, uint32_t flags, HashTable *attrs)
{
    zend_function *fn;
    zend_ast *node, *body;
    int outer = p->saw_yield;

    if (PHP_TARGET_ID < 70400) version_gate(p, "syntax error, unexpected 'fn'");
    if (accept(p, T_AMP)) {
        flags |= ZEND_ACC_RETURN_REF;
    }
    fn = fn_new(p, zend_string_initz("{closure}"), line);
    fn->flags = flags;
    fn->attributes = attrs;
    fn->returns_ref = (flags & ZEND_ACC_RETURN_REF) ? 1 : 0;

    parse_param_list(p, &fn->args, &fn->num_args, &fn->flags, &fn->min_args);
    if (accept(p, T_COLON)) {
        fn->return_type = parse_type_expr(p);
    }
    expect(p, T_DOUBLE_ARROW);

    p->saw_yield = 0;
    body = parse_expr(p);
    /* php-src wraps the expression in a return during compilation; do it here
     * so the evaluator sees an ordinary function body. */
    fn->body = zend_ast_list_add(ast_list(AST_STMT_LIST, line),
                                 ast1(AST_RETURN, body->lineno, body));
    fn->end_lineno = cur_line(p);
    if (p->saw_yield) {
        fn->flags |= ZEND_ACC_GENERATOR;
    }
    p->saw_yield = outer;

    node = ast1(AST_ARROW_FN, line, NULL);
    node->val = ZEND_PTR_ZVAL(fn);
    return node;
}
/* VERSIONGATE:70400 END */


/* ===================================================================== */
/*  classes, interfaces, traits and enums                                 */
/* ===================================================================== */

typedef struct {
    zend_string **v;
    uint32_t      n, cap;
} name_vec;

static void name_vec_add(name_vec *nv, zend_string *s)
{
    if (nv->n == nv->cap) {
        nv->cap = nv->cap ? nv->cap * 2 : 4;
        nv->v = (zend_string **) erealloc(nv->v, nv->cap * sizeof(zend_string *));
    }
    nv->v[nv->n++] = s;
}

/* php-src's zend_generate_anon_class_name(): the parent (or first interface)
 * name, then "@anonymous", a NUL, and the location.  Everything after the NUL
 * is what makes two anonymous classes in one script distinct. */
static zend_string *anon_class_name(parser *p, zend_string *prefix, uint32_t line)
{
    static uint32_t rtd_counter = 0;
    const char *pre = prefix ? prefix->val : "class";
    size_t prelen = prefix ? prefix->len : 5;
    const char *fn = (p->filename && p->filename->len) ? p->filename->val : "";
    size_t flen = strlen(fn);
    const char *mid = "@anonymous";
    size_t midlen = 10;
    char tail[64];
    size_t taillen;
    zend_string *s;

    taillen = (size_t) snprintf(tail, sizeof(tail), ":%u$%x",
                                (unsigned) line, (unsigned) ++rtd_counter);
    s = zend_string_alloc(prelen + midlen + 1 + flen + taillen);
    memcpy(s->val, pre, prelen);
    memcpy(s->val + prelen, mid, midlen);
    s->val[prelen + midlen] = '\0';
    memcpy(s->val + prelen + midlen + 1, fn, flen);
    memcpy(s->val + prelen + midlen + 1 + flen, tail, taillen);
    return s;
}

static zend_class_entry *ce_new(parser *p, zend_string *name, uint32_t line)
{
    zend_class_entry *ce = (zend_class_entry *) ecalloc(1, sizeof(zend_class_entry));

    ce->name = name;
    ce->lcname = name ? lc(name) : NULL;
    ce->function_table = zend_hash_new();
    ce->constants_table = zend_hash_new();
    ce->properties_info = zend_hash_new();
    ce->filename = p->filename;
    ce->lineno = line;
    ce->allow_dynamic_props = 1;
    return ce;
}

/* record the locally declared magic methods; inheritance is the evaluator's job */
static void ce_add_method(parser *p, zend_class_entry *ce, zend_function *fn)
{
    const char *n;

    fn->scope = ce;
    zend_hash_update(ce->function_table, fn->lcname, ZEND_PTR_ZVAL(fn));

    n = fn->lcname ? fn->lcname->val : "";
    if (!strcmp(n, "__construct")) {
        /* VERSIONGATE:70000 else-branch is the 5.6 diagnostic */
        if (PHP_TARGET_ID >= 70000) {
            /* the old style constructor is deprecated, not re-defined */
        } else {
            if (ce->constructor && ce->constructor != fn) {
                zend_error_at(E_STRICT, p->filename, fn->lineno,
                              "Redefining already defined constructor for class %s",
                              ce->name->val);
            }
        }
        ce->constructor = fn;
    }
    if (!strcmp(n, "__destruct"))    ce->destructor = fn;
    else if (!strcmp(n, "__clone"))       ce->clone = fn;
    else if (!strcmp(n, "__get"))         ce->__get = fn;
    else if (!strcmp(n, "__set"))         ce->__set = fn;
    else if (!strcmp(n, "__isset"))       ce->__isset = fn;
    else if (!strcmp(n, "__unset"))       ce->__unset = fn;
    else if (!strcmp(n, "__call"))        ce->__call = fn;
    else if (!strcmp(n, "__callstatic"))  ce->__callstatic = fn;
    else if (!strcmp(n, "__tostring"))    ce->__tostring = fn;
    else if (!strcmp(n, "__invoke"))      ce->__invoke = fn;
    else if (!strcmp(n, "__debuginfo"))   ce->__debuginfo = fn;
    else if (!strcmp(n, "__set_state"))   ce->__set_state = fn;
    else if (!strcmp(n, "__serialize"))   ce->__serialize = fn;

    /* VERSIONGATE:80000 else-branch is the older behaviour: a method named
       like its class is the constructor */
    if (PHP_TARGET_ID >= 80000) {
        /* PHP 4 style constructors were removed */
    } else {
        if (!ce->constructor && ce->lcname && fn->lcname &&
            !(fn->flags & ZEND_ACC_STATIC) &&
            zend_string_equals(ce->lcname, fn->lcname)) {
            ce->constructor = fn;
            fn->flags |= ZEND_ACC_CTOR;
        }
    }
}

/* class_name_list, used by extends/implements/use and insteadof */
static void parse_class_name_list(parser *p, name_vec *nv)
{
    for (;;) {
        zend_ast *n = parse_class_name(p);
        zend_string *s = ast_name_str(n);
        if (s) {
            name_vec_add(nv, s);
        }
        if (!accept(p, T_COMMA)) {
            return;
        }
    }
}

/* identifier | class_name "::" identifier */
static void parse_trait_method_ref(parser *p, zend_ast **trait, zend_ast **method)
{
    uint32_t line = cur_line(p);

    *trait = NULL;
    if ((tok_is_name(cur_type(p)) || cur_type(p) == T_STATIC) &&
        peek_type(p, 1) == T_PAAMAYIM_NEKUDOTAYIM) {
        *trait = parse_class_name(p);
        advance(p);                                /* "::" */
    }
    *method = ast_zstr(parse_identifier(p), line);
}

static zend_ast *parse_trait_adaptations(parser *p)
{
    zend_ast *list;

    if (accept(p, T_SEMICOLON)) {
        return NULL;
    }
    expect(p, T_LBRACE);
    list = ast_list(AST_LIST, cur_line(p));
    while (cur_type(p) != T_RBRACE) {
        uint32_t line = cur_line(p);
        zend_ast *trait = NULL, *method = NULL, *node;

        parse_trait_method_ref(p, &trait, &method);
        if (accept(p, T_INSTEADOF)) {
            name_vec nv;
            zend_ast *names = ast_list(AST_LIST, line);
            uint32_t i;
            memset(&nv, 0, sizeof(nv));
            parse_class_name_list(p, &nv);
            for (i = 0; i < nv.n; i++) {
                zend_ast_list_add(names, ast_zstr(nv.v[i], line));
            }
            node = ast3(AST_LIST, line, trait, method, names);
            node->attr = 1;
        } else {
            uint32_t flags = 0;
            zend_ast *newname = NULL;
            expect(p, T_AS);
            if (is_member_modifier(cur_type(p))) {
                uint32_t f = member_modifier_flag(cur_type(p));
                if (f) {
                    flags = f;
                    advance(p);
                }
            }
            if (tok_is_ident(cur_type(p))) {
                newname = ast_zstr(parse_identifier(p), line);
            }
            node = ast4(AST_LIST, line, trait, method, newname, ast_long(flags, line));
            node->attr = 2;
        }
        expect_semi(p);
        zend_ast_list_add(list, node);
    }
    expect(p, T_RBRACE);
    return list->nchild ? list : NULL;
}

static void parse_class_body(parser *p, zend_class_entry *ce, zend_class_decl_extra *ex)
{
    name_vec traits;
    zend_ast *adaptations = NULL;

    memset(&traits, 0, sizeof(traits));
    expect(p, T_LBRACE);

    while (cur_type(p) != T_RBRACE) {
        uint32_t line = cur_line(p);
        zend_string *doc = p->t[p->pos].doc;
        HashTable *attrs = NULL;
        uint32_t flags = 0, set_flags = 0;
        int had_modifier = 0;

        if (cur_type(p) == T_EOF || cur_type(p) == T_BAD_CHARACTER) {
            unexpected(p, T_RBRACE);
        }
        /* VERSIONGATE:80000 BEGIN - attributes on a class member */
        if (cur_type(p) == T_ATTRIBUTE) {
            attrs = parse_attribute_groups(p);
            line = cur_line(p);
            doc = p->t[p->pos].doc;
        }
        /* VERSIONGATE:80000 END */

        /* use of traits */
        if (cur_type(p) == T_USE) {
            zend_ast *ad;
            advance(p);
            parse_class_name_list(p, &traits);
            ad = parse_trait_adaptations(p);
            if (ad) {
                if (!adaptations) {
                    adaptations = ast_list(AST_LIST, line);
                }
                {
                    uint32_t i;
                    for (i = 0; i < ad->nchild; i++) {
                        zend_ast_list_add(adaptations, ad->child[i]);
                    }
                }
            }
            continue;
        }

        if (cur_type(p) == T_VAR) {
            advance(p);
            flags |= ZEND_ACC_PUBLIC;
            had_modifier = 1;
        } else {
            while (is_member_modifier(cur_type(p))) {
                uint32_t f = member_modifier_flag(cur_type(p));
                uint32_t s = set_modifier_flag(cur_type(p));
                /* VERSIONGATE:80100 else-branch is the older behaviour: no readonly */
                if (PHP_TARGET_ID >= 80100) {
                    /* readonly is a member modifier */
                } else {
                    if (cur_type(p) == T_READONLY) {
                        version_gate(p, "syntax error, unexpected 'readonly'");
                    }
                }

                    if (s) {
                        version_gate(p, "syntax error, unexpected set visibility modifier");
                    }

                if (f) {
                    flags = add_member_modifier(p, flags, f, cur_line(p));
                } else {
                    set_flags |= s;
                }
                had_modifier = 1;
                advance(p);
            }
        }

        /* ------------------------------------------------ enum case ---- */
        /* VERSIONGATE:80100 BEGIN - enum cases */
        if (cur_type(p) == T_CASE && !had_modifier) {
            zend_ast *nm, *val = NULL, *node;
            if (PHP_TARGET_ID < 80100) version_gate(p, "syntax error, unexpected 'case'");
            advance(p);
            nm = ast_zstr(parse_identifier(p), line);
            if (accept(p, T_ASSIGN)) {
                val = parse_const_expr(p);
                ce->is_backed_enum = 1;
            }
            expect_semi(p);
            node = ast3(AST_CONST_ELEM, line, nm, val, NULL);
            if (!ex->cases) {
                ex->cases = ast_list(AST_LIST, line);
            }
            zend_ast_list_add(ex->cases, node);
            continue;
        }
        /* VERSIONGATE:80100 END */

        /* ---------------------------------------------- class const ---- */
        if (cur_type(p) == T_CONST) {
            zend_type_hint *ctype = NULL;
            /* VERSIONGATE:70100 else-branch is the older behaviour: no visibility
               on a class constant */
            if (PHP_TARGET_ID >= 70100) {
                /* public/protected/private const X = 1; */
            } else {
                if (had_modifier) {
                    version_gate(p, "syntax error, unexpected 'const'");
                }
            }
            advance(p);
            /* VERSIONGATE:80300 BEGIN - typed class constants */
            if (!(tok_is_ident(cur_type(p)) && peek_type(p, 1) == T_ASSIGN)) {
                if (PHP_TARGET_ID < 80300) version_gate(p, "syntax error, unexpected type");
                ctype = parse_type_expr(p);
            }
            /* VERSIONGATE:80300 END */
            if (!(flags & ZEND_ACC_VISIBILITY)) {
                flags |= ZEND_ACC_PUBLIC;
            }
            for (;;) {
                zend_string *nm = parse_identifier(p);
                zend_class_constant *cc =
                    (zend_class_constant *) ecalloc(1, sizeof(zend_class_constant));
                expect(p, T_ASSIGN);
                cc->value.u1 = IS_UNDEF;
                cc->ast = parse_const_expr(p);
                cc->flags = flags;
                cc->type = ctype;
                cc->doc = doc;
                zend_hash_update(ce->constants_table, nm, ZEND_PTR_ZVAL(cc));
                if (!accept(p, T_COMMA)) {
                    break;
                }
            }
            expect_comma_or_semi(p);
            continue;
        }

        /* -------------------------------------------------- method ----- */
        if (cur_type(p) == T_FUNCTION) {
            zend_function *fn;
            advance(p);
            if (!(flags & ZEND_ACC_VISIBILITY)) {
                flags |= ZEND_ACC_PUBLIC;
            }
            fn = parse_named_function(p, line, flags, attrs, doc, 1);
            ce_add_method(p, ce, fn);
            continue;
        }

        /* ------------------------------------------------ property ----- */
        {
            zend_type_hint *ptype = NULL;

            if (!had_modifier && cur_type(p) != T_VARIABLE &&
                !type_atom_here(p) && cur_type(p) != T_QUESTION &&
                cur_type(p) != T_LPAREN) {
                unexpected(p, T_LAST_TOKEN);
            }
            /* VERSIONGATE:70400 BEGIN - typed properties */
            if (cur_type(p) != T_VARIABLE) {
                if (PHP_TARGET_ID < 70400) version_gate(p, "syntax error, unexpected type");
                ptype = parse_type_expr(p);
            }
            /* VERSIONGATE:70400 END */
            if (!(flags & ZEND_ACC_VISIBILITY)) {
                flags |= ZEND_ACC_PUBLIC;
            }
            for (;;) {
                zend_property_info *pi;
                zend_string *nm;

                if (cur_type(p) != T_VARIABLE) {
                    unexpected(p, T_VARIABLE);
                }
                nm = tok_text_of(p, cur_tok(p));
                advance(p);

                pi = (zend_property_info *) ecalloc(1, sizeof(zend_property_info));
                pi->name = nm;
                pi->flags = flags;
                pi->set_flags = set_flags;
                pi->type = ptype;
                pi->doc = doc;
                if (accept(p, T_ASSIGN)) {
                    pi->default_ast = parse_const_expr(p);
                }
                zend_hash_update(ce->properties_info, nm, ZEND_PTR_ZVAL(pi));
                if (!accept(p, T_COMMA)) {
                    break;
                }
            }
            expect_comma_or_semi(p);
prop_done:
            continue;
        }
    }
    expect(p, T_RBRACE);

    if (traits.n) {
        ex->trait_names = traits.v;
        ex->num_traits = traits.n;
    }
    ex->trait_adaptations = adaptations;
}

static zend_ast *parse_class_decl(parser *p, uint32_t flags, uint32_t line,
                                  HashTable *attrs, int anonymous, zend_ast **anon_args)
{
    php_token_type kw = cur_type(p);
    zend_class_entry *ce;
    zend_class_decl_extra *ex;
    zend_string *name = NULL;
    zend_string *doc = p->t[p->pos].doc;
    name_vec ifaces;
    zend_ast *node;

    memset(&ifaces, 0, sizeof(ifaces));

    switch (kw) {
    case T_CLASS:                                             break;
    case T_INTERFACE: flags |= ZEND_ACC_INTERFACE;            break;
    case T_TRAIT:     flags |= ZEND_ACC_TRAIT;                break;
    /* VERSIONGATE:80100 BEGIN - enum declarations */
    case T_ENUM:
        if (PHP_TARGET_ID < 80100) version_gate(p, "syntax error, unexpected 'enum'");
        flags |= ZEND_ACC_ENUM | ZEND_ACC_FINAL;
        break;
    /* VERSIONGATE:80100 END */
    default: unexpected(p, T_CLASS);
    }
    advance(p);

    /* VERSIONGATE:80200 else-branch is the older behaviour: no readonly classes */
    if (PHP_TARGET_ID >= 80200) {
        /* readonly class C {} */
    } else {
        if (flags & ZEND_ACC_READONLY) {
            version_gate(p, "syntax error, unexpected 'readonly'");
        }
    }

    if (anonymous) {
        name = zend_string_initz("class@anonymous");
        if (cur_type(p) == T_LPAREN) {
            *anon_args = parse_args(p);
        } else {
            *anon_args = ast_list(AST_LIST, line);
        }
    } else {
        name = ns_declare(p, parse_identifier(p));
    }

    ce = ce_new(p, name, line);
    ce->flags = flags;
    ce->attributes = attrs;
    ce->doc_comment = doc;
    ce->is_enum = (kw == T_ENUM);
    ce->is_readonly = (flags & ZEND_ACC_READONLY) ? 1 : 0;
    if (ce->is_readonly) {
        ce->allow_dynamic_props = 0;
    }
    ex = (zend_class_decl_extra *) ecalloc(1, sizeof(zend_class_decl_extra));

    /* VERSIONGATE:80100 BEGIN - enum backing type */
    if (kw == T_ENUM && accept(p, T_COLON)) {
        ce->enum_backing_type = parse_type_expr(p);
        ce->is_backed_enum = 1;
    }
    /* VERSIONGATE:80100 END */

    if (accept(p, T_EXTENDS)) {
        if (kw == T_INTERFACE) {
            parse_class_name_list(p, &ifaces);
        } else {
            zend_ast *pn = parse_class_name(p);
            ex->parent_name = ast_name_str(pn);
        }
    }
    if (accept(p, T_IMPLEMENTS)) {
        parse_class_name_list(p, &ifaces);
    }
    if (ifaces.n) {
        ex->iface_names = ifaces.v;
        ex->num_ifaces = ifaces.n;
    }
    if (anonymous) {
        ce->name = anon_class_name(p, ex->parent_name ? ex->parent_name
                                    : (ex->num_ifaces ? ex->iface_names[0] : NULL),
                                   line);
        ce->lcname = lc(ce->name);
    }

    parse_class_body(p, ce, ex);
    zend_class_decl_set_extra(ce, ex);

    node = ast0(anonymous ? AST_ANON_CLASS : AST_CLASS_DECL, line);
    node->val = ZEND_PTR_ZVAL(ce);
    return node;
}

/* ===================================================================== */
/*  statements                                                            */
/* ===================================================================== */

static zend_ast *parse_stmt_list(parser *p, const php_token_type *stops)
{
    zend_ast *list = ast_list(AST_STMT_LIST, cur_line(p));

    for (;;) {
        php_token_type t = cur_type(p);
        uint32_t i;

        if (t == T_EOF) {
            break;
        }
        if (stops) {
            int stop = 0;
            for (i = 0; stops[i] != T_LAST_TOKEN; i++) {
                if (t == stops[i]) {
                    stop = 1;
                    break;
                }
            }
            if (stop) {
                break;
            }
        }
        zend_ast_list_add(list, parse_statement(p));
    }
    return list;
}

static zend_ast *parse_block(parser *p)
{
    zend_ast *body;

    expect(p, T_LBRACE);
    body = parse_stmt_list(p, STOP_RBRACE);
    expect(p, T_RBRACE);
    return body;
}

static zend_ast *parse_if(parser *p)
{
    uint32_t line = cur_line(p);
    zend_ast *list = ast_list(AST_IF, line);
    zend_ast *cond;
    int alt;

    advance(p);
    expect(p, T_LPAREN);
    cond = parse_expr(p);
    expect(p, T_RPAREN);
    alt = (cur_type(p) == T_COLON);
    if (alt) {
        advance(p);
        zend_ast_list_add(list, ast2(AST_IF_ELEM, line, cond,
                                     parse_stmt_list(p, STOP_ENDIF)));
    } else {
        zend_ast_list_add(list, ast2(AST_IF_ELEM, line, cond, parse_statement(p)));
    }

    while (cur_type(p) == T_ELSEIF) {
        uint32_t eline = cur_line(p);
        advance(p);
        expect(p, T_LPAREN);
        cond = parse_expr(p);
        expect(p, T_RPAREN);
        if (alt) {
            expect(p, T_COLON);
            zend_ast_list_add(list, ast2(AST_IF_ELEM, eline, cond,
                                         parse_stmt_list(p, STOP_ENDIF)));
        } else {
            zend_ast_list_add(list, ast2(AST_IF_ELEM, eline, cond, parse_statement(p)));
        }
    }
    if (cur_type(p) == T_ELSE) {
        uint32_t eline = cur_line(p);
        advance(p);
        if (alt) {
            expect(p, T_COLON);
            zend_ast_list_add(list, ast2(AST_IF_ELEM, eline, NULL,
                                         parse_stmt_list(p, STOP_ENDIF)));
        } else {
            zend_ast_list_add(list, ast2(AST_IF_ELEM, eline, NULL, parse_statement(p)));
        }
    }
    if (alt) {
        expect(p, T_ENDIF);
        expect_semi(p);
    }
    return list;
}

static zend_ast *parse_while(parser *p)
{
    uint32_t line = cur_line(p);
    zend_ast *cond, *body;

    advance(p);
    expect(p, T_LPAREN);
    cond = parse_expr(p);
    expect(p, T_RPAREN);
    if (accept(p, T_COLON)) {
        body = parse_stmt_list(p, STOP_ENDWHILE);
        expect(p, T_ENDWHILE);
        expect_semi(p);
    } else {
        body = parse_statement(p);
    }
    return ast2(AST_WHILE, line, cond, body);
}

static zend_ast *parse_do_while(parser *p)
{
    uint32_t line = cur_line(p);
    zend_ast *body, *cond;

    advance(p);
    body = parse_statement(p);
    expect(p, T_WHILE);
    expect(p, T_LPAREN);
    cond = parse_expr(p);
    expect(p, T_RPAREN);
    expect_semi(p);
    return ast2(AST_DO_WHILE, line, body, cond);
}

/* comma separated expression list, empty yields NULL */
static zend_ast *parse_for_exprs(parser *p, php_token_type stop)
{
    zend_ast *list;

    if (cur_type(p) == stop) {
        return NULL;
    }
    list = ast_list(AST_LIST, cur_line(p));
    for (;;) {
        zend_ast_list_add(list, parse_expr(p));
        if (!accept(p, T_COMMA)) {
            break;
        }
    }
    return list;
}

static zend_ast *parse_for(parser *p)
{
    uint32_t line = cur_line(p);
    zend_ast *init, *cond, *step, *body;

    advance(p);
    expect(p, T_LPAREN);
    init = parse_for_exprs(p, T_SEMICOLON);
    expect(p, T_SEMICOLON);
    cond = parse_for_exprs(p, T_SEMICOLON);
    expect(p, T_SEMICOLON);
    step = parse_for_exprs(p, T_RPAREN);
    expect(p, T_RPAREN);
    if (accept(p, T_COLON)) {
        body = parse_stmt_list(p, STOP_ENDFOR);
        expect(p, T_ENDFOR);
        expect_semi(p);
    } else {
        body = parse_statement(p);
    }
    return ast4(AST_FOR, line, init, cond, step, body);
}

static zend_ast *parse_foreach_variable(parser *p, int *by_ref)
{
    uint32_t line = cur_line(p);
    zend_ast *v;

    *by_ref = 0;
    if (cur_type(p) == T_AMP) {
        advance(p);
        *by_ref = 1;
        return parse_variable(p);
    }
    if (cur_type(p) == T_LIST) {
        zend_ast *lst;
        advance(p);
        expect(p, T_LPAREN);
        lst = parse_array_literal(p, T_RPAREN, line);
        return to_list_assign(p, lst, ZEND_LIST_SYNTAX_LIST);
    }
    /* VERSIONGATE:70100 BEGIN - short list destructuring in foreach */
    if (cur_type(p) == T_LBRACKET) {
        zend_ast *lst;
        if (PHP_TARGET_ID < 70100) version_gate(p, "syntax error, unexpected '['");
        advance(p);
        lst = parse_array_literal(p, T_RBRACKET, line);
        return to_list_assign(p, lst, ZEND_LIST_SYNTAX_SHORT);
    }
    /* VERSIONGATE:70100 END */
    v = parse_variable(p);
    if (v->kind == AST_ARRAY) {
        unexpected(p, T_VARIABLE);
    }
    return v;
}

static zend_ast *parse_foreach(parser *p)
{
    uint32_t line = cur_line(p);
    zend_ast *subject, *value, *key = NULL, *body, *node;
    int by_ref = 0, dummy = 0;

    advance(p);
    expect(p, T_LPAREN);
    subject = parse_expr(p);
    expect(p, T_AS);
    value = parse_foreach_variable(p, &by_ref);
    if (accept(p, T_DOUBLE_ARROW)) {
        key = value;
        value = parse_foreach_variable(p, &by_ref);
        (void) dummy;
    }
    expect(p, T_RPAREN);
    if (accept(p, T_COLON)) {
        body = parse_stmt_list(p, STOP_ENDFOREACH);
        expect(p, T_ENDFOREACH);
        expect_semi(p);
    } else {
        body = parse_statement(p);
    }
    node = ast4(AST_FOREACH, line, subject, value, key, body);
    node->attr = by_ref ? ZEND_FOREACH_BY_REF : 0;
    return node;
}

static zend_ast *parse_switch(parser *p)
{
    uint32_t line = cur_line(p);
    zend_ast *subject, *cases;
    int alt;

    advance(p);
    expect(p, T_LPAREN);
    subject = parse_expr(p);
    expect(p, T_RPAREN);
    if (accept(p, T_COLON)) {
        alt = 1;
    } else {
        alt = 0;
        expect(p, T_LBRACE);
    }
    accept(p, T_SEMICOLON);                        /* "switch (x) { ; case ..." */

    cases = ast_list(AST_LIST, line);
    for (;;) {
        uint32_t cline = cur_line(p);
        zend_ast *cond = NULL;

        if (cur_type(p) == T_CASE) {
            advance(p);
            cond = parse_expr(p);
        } else if (cur_type(p) == T_DEFAULT) {
            advance(p);
        } else {
            break;
        }
        if (!accept(p, T_COLON)) {
            expect(p, T_SEMICOLON);
        }
        zend_ast_list_add(cases, ast2(AST_SWITCH_CASE, cline, cond,
                                      parse_stmt_list(p, STOP_CASE)));
    }
    if (alt) {
        expect(p, T_ENDSWITCH);
        expect_semi(p);
    } else {
        expect(p, T_RBRACE);
    }
    return ast2(AST_SWITCH, line, subject, cases);
}

static zend_ast *parse_try(parser *p)
{
    uint32_t line = cur_line(p);
    zend_ast *body, *catches, *finally = NULL;

    advance(p);
    body = parse_block(p);
    catches = ast_list(AST_LIST, line);

    while (cur_type(p) == T_CATCH) {
        uint32_t cline = cur_line(p);
        zend_ast *names, *var = NULL, *cbody;

        advance(p);
        expect(p, T_LPAREN);
        names = ast_list(AST_LIST, cline);
        zend_ast_list_add(names, parse_class_name(p));
        /* VERSIONGATE:70100 BEGIN - multi catch "catch (A | B $e)" */
        while (cur_type(p) == T_PIPE) {
            if (PHP_TARGET_ID < 70100) version_gate(p, "syntax error, unexpected '|'");
            advance(p);
            zend_ast_list_add(names, parse_class_name(p));
        }
        /* VERSIONGATE:70100 END */
        /* VERSIONGATE:80000 BEGIN - catch without a variable */
        if (cur_type(p) != T_VARIABLE) {
            if (PHP_TARGET_ID < 80000) version_gate(p, "syntax error, unexpected ')'");
        } else
        /* VERSIONGATE:80000 END */
        {
            if (cur_type(p) != T_VARIABLE) {
                unexpected(p, T_VARIABLE);
            }
            var = ast1(AST_VAR, cur_line(p),
                       ast_zstr(tok_text_of(p, cur_tok(p)), cur_line(p)));
            advance(p);
        }
        expect(p, T_RPAREN);
        cbody = parse_block(p);
        zend_ast_list_add(catches, ast3(AST_CATCH, cline, names, var, cbody));
    }
    if (accept(p, T_FINALLY)) {
        finally = parse_block(p);
    }
    if (catches->nchild == 0 && !finally) {
        unexpected(p, T_CATCH);
    }
    return ast3(AST_TRY, line, body, catches, finally);
}

static zend_ast *parse_declare(parser *p)
{
    uint32_t line = cur_line(p);
    zend_ast *dirs, *body = NULL;

    advance(p);
    expect(p, T_LPAREN);
    dirs = ast_list(AST_LIST, line);
    for (;;) {
        uint32_t dline = cur_line(p);
        zend_ast *nm = ast_zstr(parse_identifier(p), dline);
        zend_ast *v;
        expect(p, T_ASSIGN);
        v = parse_const_expr(p);
        zend_ast_list_add(dirs, ast3(AST_CONST_ELEM, dline, nm, v, NULL));
        if (!accept(p, T_COMMA)) {
            break;
        }
    }
    expect(p, T_RPAREN);
    if (accept(p, T_COLON)) {
        body = parse_stmt_list(p, STOP_ENDDECLARE);
        expect(p, T_ENDDECLARE);
        expect_semi(p);
    } else if (cur_type(p) == T_SEMICOLON) {
        advance(p);
    } else {
        body = parse_statement(p);
    }
    return ast2(AST_DECLARE, line, dirs, body);
}

/* "use A\B as C;", "use function ...", "use A\{B, C}" */
static zend_ast *parse_use_stmt(parser *p)
{
    uint32_t line = cur_line(p);
    uint16_t kind = ZEND_USE_CLASS;
    zend_ast *first, *node, *elems;

    advance(p);
    if (cur_type(p) == T_FUNCTION) {
        kind = ZEND_USE_FUNCTION;
        advance(p);
    } else if (cur_type(p) == T_CONST) {
        kind = ZEND_USE_CONST;
        advance(p);
    }
    first = parse_name(p);

    /* VERSIONGATE:70000 BEGIN - group use declarations */
    if (cur_type(p) == T_NS_SEPARATOR && peek_type(p, 1) == T_LBRACE) {
        if (PHP_TARGET_ID < 70000) version_gate(p, "syntax error, unexpected '{'");
        advance(p);
        advance(p);
        elems = ast_list(AST_LIST, line);
        while (cur_type(p) != T_RBRACE) {
            uint32_t eline = cur_line(p);
            uint16_t ekind = kind;
            zend_ast *nm, *alias = NULL;

            if (cur_type(p) == T_FUNCTION) {
                ekind = ZEND_USE_FUNCTION;
                advance(p);
            } else if (cur_type(p) == T_CONST) {
                ekind = ZEND_USE_CONST;
                advance(p);
            }
            nm = parse_name(p);
            {
                zend_string *full = ns_join(first->val.value.str->val,
                                            first->val.value.str->len,
                                            nm->val.value.str->val,
                                            nm->val.value.str->len);
                zend_string *al;
                if (accept(p, T_AS)) {
                    alias = ast_zstr(parse_identifier(p), eline);
                    al = alias->val.value.str;
                } else {
                    al = name_tail(full);
                }
                add_import(ekind == ZEND_USE_FUNCTION ? &p->imp_func :
                           ekind == ZEND_USE_CONST    ? &p->imp_const : &p->imp_class,
                           al, full, ekind != ZEND_USE_CONST);
            }
            zend_ast_list_add(elems, ast3(AST_CONST_ELEM, eline, nm, alias,
                                          ast_long(ekind, eline)));
            if (!accept(p, T_COMMA)) {
                break;
            }
            /* VERSIONGATE:70200 else-branch is the older behaviour: no trailing comma */
            if (PHP_TARGET_ID >= 70200) {
                if (cur_type(p) == T_RBRACE) break;
            } else {
                if (cur_type(p) == T_RBRACE) version_gate(p, "syntax error, unexpected '}'");
            }
        }
        expect(p, T_RBRACE);
        expect_semi(p);
        node = ast1(AST_USE, line, elems);
        node->attr = kind;
        node->val = first->val;
        return node;
    }
    /* VERSIONGATE:70000 END */

    elems = ast_list(AST_LIST, line);
    for (;;) {
        uint32_t eline = cur_line(p);
        zend_ast *alias = NULL;
        zend_string *full = first->val.value.str;
        zend_string *al;

        if (accept(p, T_AS)) {
            alias = ast_zstr(parse_identifier(p), eline);
            al = alias->val.value.str;
        } else {
            al = name_tail(full);
        }
        add_import(kind == ZEND_USE_FUNCTION ? &p->imp_func :
                   kind == ZEND_USE_CONST    ? &p->imp_const : &p->imp_class,
                   al, full, kind != ZEND_USE_CONST);
        zend_ast_list_add(elems, ast3(AST_CONST_ELEM, eline, first, alias,
                                      ast_long(kind, eline)));
        if (!accept(p, T_COMMA)) {
            break;
        }
        first = parse_name(p);
    }
    expect_semi(p);
    node = ast1(AST_USE, line, elems);
    node->attr = kind;
    return node;
}

static zend_ast *parse_namespace(parser *p)
{
    uint32_t line = cur_line(p);
    zend_ast *node, *nameast = NULL;

    advance(p);
    if (cur_type(p) != T_LBRACE) {
        if (tok_is_name(cur_type(p))) {
            nameast = parse_name(p);
        } else {
            nameast = ast_zstr(parse_identifier(p), cur_line(p));
        }
    }
    node = ast0(AST_USE, line);
    node->attr = ZEND_USE_NAMESPACE;
    if (nameast) {
        node->val = nameast->val;
    }

    /* a namespace declaration starts a fresh set of imports */
    p->ns = nameast ? nameast->val.value.str : NULL;
    p->imp_class = NULL;
    p->imp_func = NULL;
    p->imp_const = NULL;

    if (cur_type(p) == T_LBRACE) {
        zend_ast *body, *out;
        uint32_t i;
        advance(p);
        body = parse_stmt_list(p, STOP_RBRACE);
        expect(p, T_RBRACE);
        p->ns = NULL;
        p->imp_class = NULL;
        p->imp_func = NULL;
        p->imp_const = NULL;
        out = ast_list(AST_STMT_LIST, line);
        zend_ast_list_add(out, node);
        for (i = 0; i < body->nchild; i++) {
            zend_ast_list_add(out, body->child[i]);
        }
        return out;
    }
    expect_semi(p);
    return node;
}

static zend_ast *parse_const_decl(parser *p)
{
    uint32_t line = cur_line(p);
    zend_ast *list = ast_list(AST_CONST_DECL, line);

    advance(p);
    for (;;) {
        uint32_t cline = cur_line(p);
        zend_ast *nm;
        zend_ast *v;
        if (cur_type(p) != T_STRING) {
            unexpected(p, T_STRING);
        }
        nm = ast_zstr(ns_declare(p, parse_identifier(p)), cline);
        expect(p, T_ASSIGN);
        v = parse_const_expr(p);
        zend_ast_list_add(list, ast3(AST_CONST_ELEM, cline, nm, v, NULL));
        if (!accept(p, T_COMMA)) {
            break;
        }
    }
    expect_comma_or_semi(p);
    return list;
}

/* does a run of class modifiers lead to "class"? */
static int stmt_is_class_decl(parser *p)
{
    uint32_t i = p->pos;

    while (i < p->n && is_class_modifier_tok(p->t[i].tok.type)) {
        i++;
    }
    return i < p->n && i > p->pos && p->t[i].tok.type == T_CLASS;
}

/* php-src's `function_name` is T_STRING or T_READONLY, not any semi reserved
 * word, so "function exit()" is a syntax error and not a declaration. */
static int is_function_name_tok(php_token_type t)
{
    return t == T_STRING || t == T_READONLY;
}

static int stmt_is_function_decl(parser *p)
{
    if (cur_type(p) != T_FUNCTION) {
        return 0;
    }
    if (is_function_name_tok(peek_type(p, 1))) {
        return 1;
    }
    return peek_type(p, 1) == T_AMP && is_function_name_tok(peek_type(p, 2));
}

static zend_ast *parse_decl_statement(parser *p, HashTable *attrs)
{
    uint32_t line = cur_line(p);
    zend_string *doc = p->t[p->pos].doc;
    php_token_type t = cur_type(p);

    if (t == T_CLASS || t == T_INTERFACE || t == T_TRAIT || t == T_ENUM ||
        stmt_is_class_decl(p)) {
        uint32_t flags = 0;
        while (is_class_modifier_tok(cur_type(p))) {
            flags = add_class_modifier(p, flags, class_modifier_flag(cur_type(p)),
                                       cur_line(p));
            advance(p);
        }
        return parse_class_decl(p, flags, line, attrs, 0, NULL);
    }
    if (stmt_is_function_decl(p)) {
        zend_function *fn;
        zend_ast *node;
        advance(p);
        fn = parse_named_function(p, line, 0, attrs, doc, 0);
        fn->name = ns_declare(p, fn->name);
        fn->lcname = lc(fn->name);
        node = ast0(AST_FUNC_DECL, line);
        node->val = ZEND_PTR_ZVAL(fn);
        return node;
    }
    return NULL;
}

static zend_ast *parse_statement(parser *p)
{
    php_token *tok = cur_tok(p);
    php_token_type t = tok->type;
    uint32_t line = tok->lineno;
    zend_ast *node;

    switch (t) {
    case T_SEMICOLON:
        advance(p);
        return ast0(AST_NOP, line);

    case T_INLINE_HTML: {
        zval v = zval_zstr(tok_text_of(p, tok));
        advance(p);
        node = ast1(AST_INLINE_HTML, line, zend_ast_create_zval(v, line));
        node->val = v;
        return node;
    }

    case T_LBRACE:
        return parse_block(p);

    case T_IF:      return parse_if(p);
    case T_WHILE:   return parse_while(p);
    case T_DO:      return parse_do_while(p);
    case T_FOR:     return parse_for(p);
    case T_FOREACH: return parse_foreach(p);
    case T_TRY:     return parse_try(p);
    case T_DECLARE: return parse_declare(p);
    case T_USE:     return parse_use_stmt(p);
    case T_NAMESPACE: return parse_namespace(p);
    case T_CONST:   return parse_const_decl(p);

    case T_SWITCH:
        if (peek_type(p, 1) == T_LPAREN) {
            return parse_switch(p);
        }
        break;

    case T_ECHO: {
        zend_ast *list;
        advance(p);
        node = ast1(AST_ECHO, line, parse_expr(p));
        if (cur_type(p) != T_COMMA) {
            expect_comma_or_semi(p);
            return node;
        }
        list = ast_list(AST_STMT_LIST, line);
        zend_ast_list_add(list, node);
        while (accept(p, T_COMMA)) {
            uint32_t eline = cur_line(p);
            zend_ast_list_add(list, ast1(AST_ECHO, eline, parse_expr(p)));
        }
        expect_comma_or_semi(p);
        return list;
    }

    case T_BREAK:
    case T_CONTINUE: {
        zend_ast *lvl = NULL;
        advance(p);
        if (cur_type(p) != T_SEMICOLON && cur_type(p) != T_EOF) {
            lvl = parse_expr(p);
        }
        expect_semi_after_expr(p);
        return ast1(t == T_BREAK ? AST_BREAK : AST_CONTINUE, line, lvl);
    }

    case T_RETURN: {
        zend_ast *v = NULL;
        advance(p);
        if (cur_type(p) != T_SEMICOLON && cur_type(p) != T_EOF) {
            v = parse_expr(p);
        }
        expect_semi_after_expr(p);
        return ast1(AST_RETURN, line, v);
    }

    case T_THROW:
        advance(p);
        node = ast1(AST_THROW, line, parse_expr(p));
        expect_semi_after_expr(p);
        return node;

    case T_GOTO: {
        zval v;
        advance(p);
        v = zval_zstr(parse_identifier(p));
        expect_semi(p);
        node = ast1(AST_GOTO, line, zend_ast_create_zval(v, line));
        node->val = v;
        return node;
    }

    case T_GLOBAL: {
        zend_ast *list = ast_list(AST_STMT_LIST, line);
        advance(p);
        for (;;) {
            uint32_t gline = cur_line(p);
            zend_ast_list_add(list, ast1(AST_GLOBAL, gline, parse_simple_variable(p)));
            if (!accept(p, T_COMMA)) {
                break;
            }
        }
        expect_semi(p);
        return list;
    }

    case T_UNSET: {
        zend_ast *list;
        advance(p);
        expect(p, T_LPAREN);
        list = ast_list(AST_UNSET, line);
        while (cur_type(p) != T_RPAREN) {
            zend_ast_list_add(list, parse_variable(p));
            if (!accept(p, T_COMMA)) {
                break;
            }
        }
        expect(p, T_RPAREN);
        expect_semi(p);
        return list;
    }

    case T_STATIC:
        if (peek_type(p, 1) == T_VARIABLE) {
            zend_ast *list = ast_list(AST_STMT_LIST, line);
            advance(p);
            for (;;) {
                uint32_t sline = cur_line(p);
                zend_ast *nm, *def = NULL;
                if (cur_type(p) != T_VARIABLE) {
                    unexpected(p, T_VARIABLE);
                }
                nm = ast_zstr(tok_text_of(p, cur_tok(p)), sline);
                advance(p);
                if (accept(p, T_ASSIGN)) {
                    def = parse_const_expr(p);
                }
                zend_ast_list_add(list, ast2(AST_STATIC_VAR, sline, nm, def));
                if (!accept(p, T_COMMA)) {
                    break;
                }
            }
            expect_semi(p);
            return list;
        }
        break;

    case T_HALT_COMPILER: {
        advance(p);
        expect(p, T_LPAREN);
        expect(p, T_RPAREN);
        expect_semi(p);
        node = ast0(AST_HALT_COMPILER, line);
        node->val = zval_long((zend_long) p->halt_offset);
        return node;
    }

    /* VERSIONGATE:80000 BEGIN - attributes on a declaration */
    case T_ATTRIBUTE: {
        HashTable *attrs;
        if (PHP_TARGET_ID < 80000) version_gate(p, "syntax error, unexpected '#['");
        attrs = parse_attribute_groups(p);
        node = parse_decl_statement(p, attrs);
        if (node) {
            return node;
        }
        unexpected(p, T_CLASS);
    }
    /* VERSIONGATE:80000 END */

    default:
        break;
    }

    /* a label: "foo:" (but not "Foo::bar") */
    if (t == T_STRING && peek_type(p, 1) == T_COLON) {
        zval v = zval_zstr(tok_text_of(p, tok));
        advance(p);
        advance(p);
        node = ast1(AST_LABEL, line, zend_ast_create_zval(v, line));
        node->val = v;
        return node;
    }

    node = parse_decl_statement(p, NULL);
    if (node) {
        return node;
    }

    node = ast1(AST_EXPR_STMT, line, parse_expr(p));
    expect_semi_after_expr(p);
    return node;
}

/* ===================================================================== */
/*  entry points                                                          */
/* ===================================================================== */

static void tokenize(parser *p, const char *code, size_t len, int in_scripting)
{
    php_lexer *lex = php_lexer_new_ex(code, len, p->filename, in_scripting);
    php_token tok;

    for (;;) {
        memset(&tok, 0, sizeof(tok));
        if (php_lexer_next(lex, &tok) != 0) {
            const char *msg = php_lexer_error(lex);
            snprintf(p->lex_err, sizeof(p->lex_err), "%s",
                     (msg && *msg) ? msg : "syntax error");
            memset(&tok, 0, sizeof(tok));
            tok.type = T_BAD_CHARACTER;
            tok.lineno = php_lexer_lineno(lex);
            toks_push(p, &tok, NULL);
            break;
        }
        if (tok.type == T_OPEN_TAG) {
            continue;                              /* php-src drops it too */
        }
        if (tok.type == T_CLOSE_TAG) {
            tok.type = T_SEMICOLON;                /* "?>" terminates a statement */
        } else if (tok.type == T_OPEN_TAG_WITH_ECHO) {
            tok.type = T_ECHO;                     /* "<?=" is an echo */
        }
        toks_push(p, &tok, php_lexer_doc_comment(lex));
        if (tok.type == T_SEMICOLON || tok.type == T_LBRACE || tok.type == T_RBRACE) {
            php_lexer_clear_doc_comment(lex);
        }
        if (tok.type == T_EOF) {
            break;
        }

        /* "__halt_compiler();" stops the scanner: the rest of the file is data */
        if (tok.type == T_HALT_COMPILER) {
            php_token t2;
            int i, ok = 1;
            for (i = 0; i < 3 && ok; i++) {
                memset(&t2, 0, sizeof(t2));
                if (php_lexer_next(lex, &t2) != 0) {
                    ok = 0;
                    break;
                }
                if (t2.type == T_CLOSE_TAG) {
                    t2.type = T_SEMICOLON;
                }
                if ((i == 0 && t2.type != T_LPAREN) ||
                    (i == 1 && t2.type != T_RPAREN) ||
                    (i == 2 && t2.type != T_SEMICOLON)) {
                    ok = 0;
                }
                toks_push(p, &t2, NULL);
            }
            if (ok) {
                p->halt_offset = php_lexer_offset(lex);
                php_lexer_halt(lex);
                memset(&t2, 0, sizeof(t2));
                t2.type = T_EOF;
                t2.lineno = php_lexer_lineno(lex);
                toks_push(p, &t2, NULL);
                break;
            }
        }
    }

    if (!p->n || p->t[p->n - 1].tok.type != T_EOF) {
        memset(&tok, 0, sizeof(tok));
        tok.type = T_EOF;
        tok.lineno = p->n ? p->t[p->n - 1].tok.lineno : 1;
        toks_push(p, &tok, NULL);
    }
    php_lexer_free(lex);
}

static zend_ast *parse_source(const char *code, size_t len, zend_string *filename,
                              zend_string **err, int in_scripting)
{
    parser *p = (parser *) ecalloc(1, sizeof(parser));
    zend_ast *list;

    if (err) {
        *err = NULL;
    }
    p->filename = filename ? filename : zend_string_initz("");
    p->v8 = (EG(php_version) >= 80000);
    php_parse_error_lineno = 0;

    tokenize(p, code, len, in_scripting);

    if (setjmp(p->jb)) {
        php_parse_error_lineno = p->err_line ? p->err_line : cur_line(p);
        if (err) {
            *err = zend_string_initz(p->err);
        }
        return NULL;
    }

    list = ast_list(AST_STMT_LIST, 1);
    while (cur_type(p) != T_EOF) {
        zend_ast_list_add(list, parse_statement(p));
    }
    return list;
}

/* A whole script or a stream of inline HTML: the scanner starts outside PHP
 * and waits for an opening tag. */
zend_ast *php_parse_string(const char *code, size_t len, zend_string *filename,
                           zend_string **err)
{
    return parse_source(code, len, filename, err, 0);
}

/* eval()'d code: php-src's zend_compile_string() starts the scanner inside PHP
 * rather than requiring an opening tag, so "echo 1;" is code and not HTML. */
zend_ast *php_parse_eval_string(const char *code, size_t len, zend_string *filename,
                                zend_string **err)
{
    return parse_source(code, len, filename, err, 1);
}

zend_ast *php_parse_file(const char *filename, zend_string **err)
{
    FILE *f;
    char *buf = NULL;
    size_t cap = 0, len = 0;
    zend_ast *ast;

    if (err) {
        *err = NULL;
    }
    f = fopen(filename, "rb");
    if (!f) {
        if (err) {
            *err = zend_strpprintf("Failed opening %s", filename);
        }
        return NULL;
    }
    cap = 65536;
    buf = (char *) emalloc(cap);
    for (;;) {
        size_t got;
        if (len + 4096 >= cap) {
            cap *= 2;
            buf = (char *) erealloc(buf, cap);
        }
        got = fread(buf + len, 1, cap - len - 1, f);   /* keep room for the NUL */
        if (got == 0) {
            break;
        }
        len += got;
    }
    fclose(f);
    buf[len] = '\0';

    ast = parse_source(buf, len, zend_string_initz(filename), err, 0);
    return ast;
}

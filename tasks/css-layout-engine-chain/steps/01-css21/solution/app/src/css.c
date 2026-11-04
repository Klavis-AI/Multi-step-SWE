/* css.c -- CSS Syntax Level 3 tokenizer and parser.
 *
 * The whole input is tokenized into an array first; the parser then works on
 * that array, which makes the "is this a declaration or a nested rule?"
 * lookahead trivial and keeps block matching exact.
 */

#include "css.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------- token kinds */

enum {
    T_EOF = 0, T_WS, T_IDENT, T_FUNCTION, T_AT, T_HASH, T_STRING,
    T_BADSTRING, T_URL, T_BADURL, T_DELIM, T_NUMBER, T_PERCENT,
    T_DIMENSION, T_CDO, T_CDC, T_COLON, T_SEMI, T_COMMA,
    T_LBRACK, T_RBRACK, T_LPAREN, T_RPAREN, T_LBRACE, T_RBRACE
};

typedef struct {
    int         t;
    const char *s;     /* raw slice of the input, used for serialization */
    size_t      n;
} ctok;

typedef struct {
    ctok   *v;
    size_t  len, cap;
    arena  *a;
} toklist;

static void tl_push(toklist *tl, int t, const char *s, size_t n)
{
    if (tl->len == tl->cap) {
        size_t ncap = (tl->cap != 0) ? tl->cap * 2 : 256;
        ctok *nv = (ctok *)arena_alloc(tl->a, ncap * sizeof(ctok));
        if (tl->len != 0)
            memcpy(nv, tl->v, tl->len * sizeof(ctok));
        tl->v = nv;
        tl->cap = ncap;
    }
    tl->v[tl->len].t = t;
    tl->v[tl->len].s = s;
    tl->v[tl->len].n = n;
    tl->len++;
}

/* --------------------------------------------------------------- ascii bits */

static int c_is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static int c_is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static int c_is_hex(char c)
{
    return c_is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static char c_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static int c_is_name_start(char c)
{
    unsigned char u = (unsigned char)c;
    return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') ||
           c == '_' || u >= 0x80;
}

static int c_is_name(char c)
{
    return c_is_name_start(c) || c_is_digit(c) || c == '-';
}

static int slice_casecmp(const char *s, size_t n, const char *lit)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (lit[i] == '\0')
            return 1;
        if (c_lower(s[i]) != c_lower(lit[i]))
            return 1;
    }
    return lit[n] == '\0' ? 0 : 1;
}

/* -------------------------------------------------------------- tokenizer */

typedef struct { const char *s; size_t n, i; } lexer;

static int valid_escape(lexer *L, size_t i)
{
    return i < L->n && L->s[i] == '\\' && i + 1 < L->n && L->s[i + 1] != '\n';
}

static void consume_escape(lexer *L)
{
    /* L->s[L->i] == '\\' */
    L->i++;
    if (L->i < L->n && c_is_hex(L->s[L->i])) {
        int k = 0;
        while (L->i < L->n && k < 6 && c_is_hex(L->s[L->i])) {
            L->i++;
            k++;
        }
        if (L->i < L->n && c_is_ws(L->s[L->i]))
            L->i++;
        return;
    }
    if (L->i < L->n)
        L->i++;
}

static void consume_name(lexer *L)
{
    while (L->i < L->n) {
        if (c_is_name(L->s[L->i]))
            L->i++;
        else if (valid_escape(L, L->i))
            consume_escape(L);
        else
            break;
    }
}

static int starts_ident(lexer *L, size_t i)
{
    if (i >= L->n)
        return 0;
    if (c_is_name_start(L->s[i]))
        return 1;
    if (L->s[i] == '-') {
        if (i + 1 < L->n && (c_is_name_start(L->s[i + 1]) || L->s[i + 1] == '-'))
            return 1;
        if (valid_escape(L, i + 1))
            return 1;
        return 0;
    }
    return valid_escape(L, i);
}

static int starts_number(lexer *L, size_t i)
{
    if (i >= L->n)
        return 0;
    if (c_is_digit(L->s[i]))
        return 1;
    if (L->s[i] == '.')
        return i + 1 < L->n && c_is_digit(L->s[i + 1]);
    if (L->s[i] == '+' || L->s[i] == '-') {
        if (i + 1 < L->n && c_is_digit(L->s[i + 1]))
            return 1;
        return i + 2 < L->n && L->s[i + 1] == '.' && c_is_digit(L->s[i + 2]);
    }
    return 0;
}

static void consume_number(lexer *L)
{
    if (L->i < L->n && (L->s[L->i] == '+' || L->s[L->i] == '-'))
        L->i++;
    while (L->i < L->n && c_is_digit(L->s[L->i]))
        L->i++;
    if (L->i + 1 < L->n && L->s[L->i] == '.' && c_is_digit(L->s[L->i + 1])) {
        L->i++;
        while (L->i < L->n && c_is_digit(L->s[L->i]))
            L->i++;
    }
    if (L->i < L->n && (L->s[L->i] == 'e' || L->s[L->i] == 'E')) {
        size_t j = L->i + 1;
        if (j < L->n && (L->s[j] == '+' || L->s[j] == '-'))
            j++;
        if (j < L->n && c_is_digit(L->s[j])) {
            L->i = j;
            while (L->i < L->n && c_is_digit(L->s[L->i]))
                L->i++;
        }
    }
}

/* A string token.  An unescaped newline makes it a bad-string (and the
 * newline is left in the stream); EOF simply ends it. */
static int consume_string(lexer *L, char quote)
{
    L->i++;                       /* opening quote */
    while (L->i < L->n) {
        char c = L->s[L->i];
        if (c == quote) {
            L->i++;
            return T_STRING;
        }
        if (c == '\n')
            return T_BADSTRING;
        if (c == '\\') {
            if (L->i + 1 < L->n && L->s[L->i + 1] == '\n') {
                L->i += 2;        /* escaped newline continues the string */
                continue;
            }
            if (valid_escape(L, L->i)) {
                consume_escape(L);
                continue;
            }
            L->i++;
            continue;
        }
        L->i++;
    }
    return T_STRING;              /* unterminated: closes at EOF */
}

/* Unquoted url(...) contents; L->i sits just after '('. */
static int consume_url(lexer *L)
{
    while (L->i < L->n && c_is_ws(L->s[L->i]))
        L->i++;
    while (L->i < L->n) {
        char c = L->s[L->i];
        if (c == ')') {
            L->i++;
            return T_URL;
        }
        if (c_is_ws(c)) {
            while (L->i < L->n && c_is_ws(L->s[L->i]))
                L->i++;
            if (L->i < L->n && L->s[L->i] == ')') {
                L->i++;
                return T_URL;
            }
            if (L->i >= L->n)
                return T_URL;
            /* fall through to the bad-url remnant scan */
        } else if (c == '"' || c == '\'' || c == '(') {
            /* bad url */
        } else if (c == '\\') {
            if (valid_escape(L, L->i)) {
                consume_escape(L);
                continue;
            }
            /* bad url */
        } else {
            L->i++;
            continue;
        }
        while (L->i < L->n) {     /* consume the remnants of a bad url */
            if (L->s[L->i] == ')') {
                L->i++;
                break;
            }
            if (valid_escape(L, L->i))
                consume_escape(L);
            else
                L->i++;
        }
        return T_BADURL;
    }
    return T_URL;                 /* unterminated: closes at EOF */
}

static void tokenize(toklist *tl, const char *text)
{
    lexer L;

    L.s = text;
    L.n = strlen(text);
    L.i = 0;

    while (L.i < L.n) {
        size_t start = L.i;
        char c = L.s[L.i];

        /* comments produce no token at all */
        if (c == '/' && L.i + 1 < L.n && L.s[L.i + 1] == '*') {
            L.i += 2;
            while (L.i + 1 < L.n && !(L.s[L.i] == '*' && L.s[L.i + 1] == '/'))
                L.i++;
            L.i = (L.i + 1 < L.n) ? L.i + 2 : L.n;   /* unterminated: EOF */
            continue;
        }
        if (c_is_ws(c)) {
            while (L.i < L.n && c_is_ws(L.s[L.i]))
                L.i++;
            tl_push(tl, T_WS, L.s + start, L.i - start);
            continue;
        }
        if (c == '"' || c == '\'') {
            int t = consume_string(&L, c);
            tl_push(tl, t, L.s + start, L.i - start);
            continue;
        }
        if (c == '#') {
            if (L.i + 1 < L.n && (c_is_name(L.s[L.i + 1]) ||
                                  valid_escape(&L, L.i + 1))) {
                L.i++;
                consume_name(&L);
                tl_push(tl, T_HASH, L.s + start, L.i - start);
            } else {
                L.i++;
                tl_push(tl, T_DELIM, L.s + start, 1);
            }
            continue;
        }
        if (c == '(') { L.i++; tl_push(tl, T_LPAREN, L.s + start, 1); continue; }
        if (c == ')') { L.i++; tl_push(tl, T_RPAREN, L.s + start, 1); continue; }
        if (c == '[') { L.i++; tl_push(tl, T_LBRACK, L.s + start, 1); continue; }
        if (c == ']') { L.i++; tl_push(tl, T_RBRACK, L.s + start, 1); continue; }
        if (c == '{') { L.i++; tl_push(tl, T_LBRACE, L.s + start, 1); continue; }
        if (c == '}') { L.i++; tl_push(tl, T_RBRACE, L.s + start, 1); continue; }
        if (c == ',') { L.i++; tl_push(tl, T_COMMA,  L.s + start, 1); continue; }
        if (c == ':') { L.i++; tl_push(tl, T_COLON,  L.s + start, 1); continue; }
        if (c == ';') { L.i++; tl_push(tl, T_SEMI,   L.s + start, 1); continue; }

        if (starts_number(&L, L.i)) {
            int t = T_NUMBER;
            consume_number(&L);
            if (starts_ident(&L, L.i)) {
                consume_name(&L);
                t = T_DIMENSION;
            } else if (L.i < L.n && L.s[L.i] == '%') {
                L.i++;
                t = T_PERCENT;
            }
            tl_push(tl, t, L.s + start, L.i - start);
            continue;
        }
        if (c == '+' || c == '.') {
            L.i++;
            tl_push(tl, T_DELIM, L.s + start, 1);
            continue;
        }
        if (c == '-') {
            if (L.n - L.i >= 3 && memcmp(L.s + L.i, "-->", 3) == 0) {
                L.i += 3;
                tl_push(tl, T_CDC, L.s + start, 3);
                continue;
            }
            if (starts_ident(&L, L.i)) {
                consume_name(&L);
                if (L.i < L.n && L.s[L.i] == '(') {
                    L.i++;
                    tl_push(tl, T_FUNCTION, L.s + start, L.i - start);
                } else {
                    tl_push(tl, T_IDENT, L.s + start, L.i - start);
                }
                continue;
            }
            L.i++;
            tl_push(tl, T_DELIM, L.s + start, 1);
            continue;
        }
        if (c == '<') {
            if (L.n - L.i >= 4 && memcmp(L.s + L.i, "<!--", 4) == 0) {
                L.i += 4;
                tl_push(tl, T_CDO, L.s + start, 4);
                continue;
            }
            L.i++;
            tl_push(tl, T_DELIM, L.s + start, 1);
            continue;
        }
        if (c == '@') {
            if (starts_ident(&L, L.i + 1)) {
                L.i++;
                consume_name(&L);
                tl_push(tl, T_AT, L.s + start, L.i - start);
            } else {
                L.i++;
                tl_push(tl, T_DELIM, L.s + start, 1);
            }
            continue;
        }
        if (c == '\\') {
            if (valid_escape(&L, L.i)) {
                consume_name(&L);
                tl_push(tl, T_IDENT, L.s + start, L.i - start);
            } else {
                L.i++;
                tl_push(tl, T_DELIM, L.s + start, 1);
            }
            continue;
        }
        if (c_is_name_start(c)) {
            size_t name_end;
            consume_name(&L);
            name_end = L.i;
            if (L.i < L.n && L.s[L.i] == '(') {
                int is_url = (slice_casecmp(L.s + start, name_end - start,
                                            "url") == 0);
                L.i++;
                if (is_url) {
                    size_t j = L.i;
                    while (j < L.n && c_is_ws(L.s[j]))
                        j++;
                    if (j < L.n && (L.s[j] == '"' || L.s[j] == '\'')) {
                        /* url("...") behaves as a normal function */
                        tl_push(tl, T_FUNCTION, L.s + start, L.i - start);
                    } else {
                        int t = consume_url(&L);
                        tl_push(tl, t, L.s + start, L.i - start);
                    }
                } else {
                    tl_push(tl, T_FUNCTION, L.s + start, L.i - start);
                }
            } else {
                tl_push(tl, T_IDENT, L.s + start, L.i - start);
            }
            continue;
        }
        L.i++;
        tl_push(tl, T_DELIM, L.s + start, 1);
    }
    tl_push(tl, T_EOF, text + L.n, 0);
}

/* ------------------------------------------------------- serialization */

static int suppress_space_before(int t)
{
    return t == T_RPAREN || t == T_RBRACK || t == T_RBRACE || t == T_COMMA;
}

static int suppress_space_after(int t)
{
    return t == T_LPAREN || t == T_LBRACK || t == T_LBRACE || t == T_FUNCTION;
}

/* Join tokens [from, to): whitespace runs collapse to one space, and spaces
 * hugging the inside of a bracket or preceding a comma are dropped, so
 * "calc( 1px  +  2px )" serializes as "calc(1px + 2px)". */
static char *toks_to_text(arena *a, const ctok *v, size_t from, size_t to)
{
    strbuf sb;
    size_t i;
    int have = 0, pending = 0, prev = T_EOF;

    sb_init(&sb, a);
    for (i = from; i < to; i++) {
        int t = v[i].t;
        if (t == T_EOF)
            break;
        if (t == T_WS) {
            if (have)
                pending = 1;
            continue;
        }
        if (pending) {
            if (!suppress_space_before(t) && !suppress_space_after(prev))
                sb_putc(&sb, ' ');
            pending = 0;
        }
        sb_putn(&sb, v[i].s, v[i].n);
        have = 1;
        prev = t;
    }
    return sb_done(&sb);
}

static char *slice_lower(arena *a, const char *s, size_t n)
{
    strbuf sb;
    size_t i;

    sb_init(&sb, a);
    for (i = 0; i < n; i++)
        sb_putc(&sb, c_lower(s[i]));
    return sb_done(&sb);
}

/* ------------------------------------------------------------- the parser */

#define CSS_MAX_DEPTH 64

typedef struct {
    toklist tl;
    size_t  i;
    arena  *a;
    int     order;
    int     depth;
} cssp;

static int tk(cssp *p, size_t i)
{
    return (i < p->tl.len) ? p->tl.v[i].t : T_EOF;
}

static int cur_t(cssp *p)
{
    return tk(p, p->i);
}

static int depth_delta(int t)
{
    if (t == T_LPAREN || t == T_FUNCTION || t == T_LBRACK || t == T_LBRACE)
        return 1;
    if (t == T_RPAREN || t == T_RBRACK || t == T_RBRACE)
        return -1;
    return 0;
}

static void skip_ws(cssp *p)
{
    while (cur_t(p) == T_WS)
        p->i++;
}

enum { STOP_EOF = 0, STOP_BLOCK, STOP_SEMI, STOP_RBRACE };

/* Consume component values up to a top-level '{', ';' or '}'. */
static int collect_prelude(cssp *p, size_t *start, size_t *end)
{
    int depth = 0;

    *start = p->i;
    while (cur_t(p) != T_EOF) {
        int t = cur_t(p);
        if (depth == 0) {
            if (t == T_LBRACE) { *end = p->i; return STOP_BLOCK; }
            if (t == T_SEMI)   { *end = p->i; return STOP_SEMI; }
            if (t == T_RBRACE) { *end = p->i; return STOP_RBRACE; }
        }
        depth += depth_delta(t);
        if (depth < 0)
            depth = 0;
        p->i++;
    }
    *end = p->i;
    return STOP_EOF;
}

/* p->i sits just after the '{'; consume through the matching '}'. */
static void skip_block(cssp *p)
{
    int depth = 1;

    while (cur_t(p) != T_EOF) {
        int t = cur_t(p);
        p->i++;
        if (t == T_LBRACE)
            depth++;
        else if (t == T_RBRACE) {
            depth--;
            if (depth == 0)
                return;
        }
    }
}

static css_rule *new_rule(cssp *p, int kind)
{
    css_rule *r = (css_rule *)arena_alloc(p->a, sizeof *r);
    r->kind = kind;
    r->order = p->order++;
    return r;
}

static void split_selectors(cssp *p, css_rule *r, size_t from, size_t to)
{
    size_t i, seg = from, count = 0, k = 0;
    int depth = 0;
    char **out;

    /* Pass 1: count the non-empty segments. */
    for (i = from; i <= to; i++) {
        int t = (i < to) ? p->tl.v[i].t : T_EOF;
        if ((i == to) || (depth == 0 && t == T_COMMA)) {
            char *s = toks_to_text(p->a, p->tl.v, seg, i);
            if (s[0] != '\0')
                count++;
            seg = i + 1;
            if (i == to)
                break;
            continue;
        }
        depth += depth_delta(t);
        if (depth < 0)
            depth = 0;
    }
    if (count == 0) {
        r->selectors = NULL;
        r->n_selectors = 0;
        return;
    }
    out = (char **)arena_alloc(p->a, count * sizeof(char *));
    seg = from;
    depth = 0;
    for (i = from; i <= to; i++) {
        int t = (i < to) ? p->tl.v[i].t : T_EOF;
        if ((i == to) || (depth == 0 && t == T_COMMA)) {
            char *s = toks_to_text(p->a, p->tl.v, seg, i);
            if (s[0] != '\0' && k < count)
                out[k++] = s;
            seg = i + 1;
            if (i == to)
                break;
            continue;
        }
        depth += depth_delta(t);
        if (depth < 0)
            depth = 0;
    }
    r->selectors = out;
    r->n_selectors = (int)k;
}

static void parse_block_contents(cssp *p, css_rule *r);

/* The current token is an at-keyword. */
static css_rule *parse_at_rule(cssp *p)
{
    css_rule *r;
    size_t s, e;
    int stop;
    const ctok *at = &p->tl.v[p->i];

    r = new_rule(p, CSS_AT_RULE);
    r->at_name = slice_lower(p->a, at->s + 1, (at->n > 0) ? at->n - 1 : 0);
    p->i++;

    stop = collect_prelude(p, &s, &e);
    r->prelude = toks_to_text(p->a, p->tl.v, s, e);
    if (stop == STOP_BLOCK) {
        p->i++;                       /* '{' */
        parse_block_contents(p, r);
    } else if (stop == STOP_SEMI) {
        p->i++;                       /* statement at-rule */
    }
    /* STOP_RBRACE / STOP_EOF: leave the token for the caller. */
    return r;
}

static css_rule *parse_qualified_rule(cssp *p)
{
    size_t s, e;
    int stop = collect_prelude(p, &s, &e);
    css_rule *r;

    if (stop != STOP_BLOCK) {
        if (stop == STOP_SEMI)
            p->i++;                   /* malformed: drop it and move on */
        return NULL;
    }
    r = new_rule(p, CSS_STYLE_RULE);
    r->prelude = toks_to_text(p->a, p->tl.v, s, e);
    split_selectors(p, r, s, e);
    p->i++;                           /* '{' */
    parse_block_contents(p, r);
    return r;
}

/* Does the run starting at p->i parse as a declaration (as opposed to a
 * nested style rule)?  "a:hover {" hits a top-level '{' first, so it is a
 * rule; "color: red;" hits ';' or '}' first, so it is a declaration. */
static int looks_like_decl(cssp *p)
{
    size_t j = p->i;
    int depth = 0;

    if (tk(p, j) != T_IDENT)
        return 0;
    j++;
    while (tk(p, j) == T_WS)
        j++;
    if (tk(p, j) != T_COLON)
        return 0;

    for (j = p->i; tk(p, j) != T_EOF; j++) {
        int t = tk(p, j);
        if (depth == 0) {
            if (t == T_SEMI || t == T_RBRACE)
                return 1;
            if (t == T_LBRACE)
                return 0;
        }
        depth += depth_delta(t);
        if (depth < 0)
            depth = 0;
    }
    return 1;                         /* EOF closes the declaration */
}

/* Consume to the end of a declaration; the ';' is eaten, a '}' is not. */
static void collect_decl_value(cssp *p, size_t *start, size_t *end)
{
    int depth = 0;

    *start = p->i;
    while (cur_t(p) != T_EOF) {
        int t = cur_t(p);
        if (depth == 0 && (t == T_SEMI || t == T_RBRACE))
            break;
        depth += depth_delta(t);
        if (depth < 0)
            depth = 0;
        p->i++;
    }
    *end = p->i;
    if (cur_t(p) == T_SEMI)
        p->i++;
}

static css_decl *parse_decl(cssp *p)
{
    const ctok *name;
    css_decl *d;
    size_t s, e;
    int important = 0;
    int custom;

    name = &p->tl.v[p->i];
    p->i++;
    skip_ws(p);
    if (cur_t(p) != T_COLON) {        /* no colon: drop the declaration */
        collect_decl_value(p, &s, &e);
        return NULL;
    }
    p->i++;                           /* ':' */

    collect_decl_value(p, &s, &e);

    /* Strip a trailing "!important" (whitespace-tolerant, case-insensitive). */
    {
        size_t j = e;
        while (j > s && p->tl.v[j - 1].t == T_WS)
            j--;
        if (j > s && p->tl.v[j - 1].t == T_IDENT &&
            slice_casecmp(p->tl.v[j - 1].s, p->tl.v[j - 1].n, "important") == 0) {
            size_t k = j - 1;
            while (k > s && p->tl.v[k - 1].t == T_WS)
                k--;
            if (k > s && p->tl.v[k - 1].t == T_DELIM &&
                p->tl.v[k - 1].n == 1 && p->tl.v[k - 1].s[0] == '!') {
                important = 1;
                e = k - 1;
            }
        }
    }

    custom = (name->n >= 2 && name->s[0] == '-' && name->s[1] == '-');

    d = (css_decl *)arena_alloc(p->a, sizeof *d);
    d->prop = custom ? arena_strndup(p->a, name->s, name->n)
                     : slice_lower(p->a, name->s, name->n);
    d->value = toks_to_text(p->a, p->tl.v, s, e);
    d->important = important;
    if (!custom && d->value[0] == '\0')
        return NULL;                  /* "color: ;" is invalid; drop it */
    return d;
}

/* p->i sits just after the '{' of `r`'s block. */
static void parse_block_contents(cssp *p, css_rule *r)
{
    css_decl *dtail = NULL;
    css_rule *rtail = NULL;

    p->depth++;
    if (p->depth > CSS_MAX_DEPTH) {   /* pathological nesting: skip the block */
        skip_block(p);
        p->depth--;
        return;
    }

    for (;;) {
        int t;
        while ((t = cur_t(p)) == T_WS || t == T_SEMI)
            p->i++;
        if (t == T_EOF)
            break;                    /* unterminated block: closes at EOF */
        if (t == T_RBRACE) {
            p->i++;
            break;
        }
        if (t == T_AT) {
            css_rule *c = parse_at_rule(p);
            if (c != NULL) {
                if (rtail != NULL)
                    rtail->next = c;
                else
                    r->children = c;
                rtail = c;
            }
            continue;
        }
        if (looks_like_decl(p)) {
            css_decl *d = parse_decl(p);
            if (d != NULL) {
                if (dtail != NULL)
                    dtail->next = d;
                else
                    r->decls = d;
                dtail = d;
            }
            continue;
        }
        {
            size_t before = p->i;
            css_rule *c = parse_qualified_rule(p);
            if (c != NULL) {
                if (rtail != NULL)
                    rtail->next = c;
                else
                    r->children = c;
                rtail = c;
            }
            if (p->i == before)       /* belt and braces: always advance */
                p->i++;
        }
    }
    p->depth--;
}

css_sheet *css_parse_sheet(arena *a, const char *text)
{
    cssp p;
    css_sheet *sheet;
    css_rule *tail = NULL;

    sheet = (css_sheet *)arena_alloc(a, sizeof *sheet);
    if (text == NULL)
        return sheet;

    p.tl.v = NULL;
    p.tl.len = 0;
    p.tl.cap = 0;
    p.tl.a = a;
    p.i = 0;
    p.a = a;
    p.order = 0;
    p.depth = 0;
    tokenize(&p.tl, text);

    for (;;) {
        int t;
        while ((t = cur_t(&p)) == T_WS || t == T_CDO || t == T_CDC ||
               t == T_SEMI || t == T_RBRACE)
            p.i++;                    /* stray junk between top-level rules */
        if (t == T_EOF)
            break;
        {
            css_rule *r;
            size_t before = p.i;
            if (t == T_AT)
                r = parse_at_rule(&p);
            else
                r = parse_qualified_rule(&p);
            if (r != NULL) {
                if (tail != NULL)
                    tail->next = r;
                else
                    sheet->rules = r;
                tail = r;
            }
            if (p.i == before)
                p.i++;
        }
    }
    return sheet;
}

css_decl *css_parse_decls(arena *a, const char *text)
{
    cssp p;
    css_decl *head = NULL, *tail = NULL;

    if (text == NULL)
        return NULL;

    p.tl.v = NULL;
    p.tl.len = 0;
    p.tl.cap = 0;
    p.tl.a = a;
    p.i = 0;
    p.a = a;
    p.order = 0;
    p.depth = 0;
    tokenize(&p.tl, text);

    for (;;) {
        int t;
        while ((t = cur_t(&p)) == T_WS || t == T_SEMI || t == T_RBRACE ||
               t == T_LBRACE)
            p.i++;
        if (t == T_EOF)
            break;
        if (t == T_IDENT) {
            size_t before = p.i;
            css_decl *d = parse_decl(&p);
            if (d != NULL) {
                if (tail != NULL)
                    tail->next = d;
                else
                    head = d;
                tail = d;
            }
            if (p.i == before)
                p.i++;
            continue;
        }
        {   /* not a declaration: skip to the next ';' at depth zero */
            int depth = 0;
            while (cur_t(&p) != T_EOF) {
                int u = cur_t(&p);
                if (depth == 0 && u == T_SEMI) {
                    p.i++;
                    break;
                }
                depth += depth_delta(u);
                if (depth < 0)
                    depth = 0;
                p.i++;
            }
        }
    }
    return head;
}

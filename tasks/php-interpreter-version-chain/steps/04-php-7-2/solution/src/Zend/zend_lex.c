/* zend_lex.c -- hand written PHP scanner (php-src Zend/zend_language_scanner.l).
 *
 * The scanner is a straightforward forward-only reader over the whole source
 * buffer.  It mirrors php-src's start conditions:
 *
 *   ST_INITIAL              outside <?php ... ?>
 *   ST_IN_SCRIPTING         normal PHP code
 *   ST_DOUBLE_QUOTES        inside "..."      (interpolated)
 *   ST_BACKQUOTE            inside `...`      (interpolated)
 *   ST_HEREDOC              inside a heredoc body
 *   ST_NOWDOC               inside a nowdoc body
 *   ST_END_HEREDOC          sitting on the closing label
 *   ST_LOOKING_FOR_PROPERTY just after "->" / "?->"
 *   ST_LOOKING_FOR_VARNAME  just after "${"
 *   ST_VAR_OFFSET           inside the "[...]" of the simple "$a[x]" syntax
 *
 * ST_IN_SCRIPTING is pushed on the state stack by "{" and by the interpolation
 * openers, and popped by "}", which is what makes "{$a[$b]}" nest correctly.
 */

#include "zend_lex.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ memory */
#if defined(__has_include)
#  if __has_include("zend_alloc.h")
#    define PHP_LEX_HAVE_ZEND_ALLOC 1
#  endif
#endif

#ifdef PHP_LEX_HAVE_ZEND_ALLOC
#  include "zend_alloc.h"
#else
/* Zend/zend_alloc.h (owned by another component) provides these; they are
 * declared here so that the scanner still compiles before that file lands. */
void *emalloc(size_t size);
void  efree(void *ptr);
char *estrdup(const char *s);
#endif

/* ---------------------------------------------------------------- version */
/* The release this scanner emulates.  The build passes -DPHP_TARGET_ID; a
 * plain build with no define is the PHP 5.6 scanner, so every behaviour that
 * changed after 5.6 sits behind a VERSIONGATE marker and can be stripped
 * mechanically to produce the 5.6 snapshot. */
#ifndef PHP_TARGET_ID
# ifdef PHP_VERSION_ID
#  define PHP_TARGET_ID PHP_VERSION_ID
# else
#  define PHP_TARGET_ID 50640
# endif
#endif

/* ------------------------------------------------------------------- types */

#define LEX_ERRBUF 512

enum lex_start_condition {
    ST_INITIAL = 0,
    ST_IN_SCRIPTING,
    ST_DOUBLE_QUOTES,
    ST_BACKQUOTE,
    ST_HEREDOC,
    ST_NOWDOC,
    ST_END_HEREDOC,
    ST_LOOKING_FOR_PROPERTY,
    ST_LOOKING_FOR_VARNAME,
    ST_VAR_OFFSET
};

typedef struct {
    size_t label_off;        /* label position inside src                    */
    size_t label_len;
    size_t indentation;      /* indentation of the closing marker (7.3+)     */
    size_t body_start;
    size_t body_end;         /* one past the last body byte                  */
    size_t term_line_start;  /* offset of the line holding the closing label */
    int    uses_spaces;      /* the closing marker is indented with spaces   */
    int    is_nowdoc;
} heredoc_frame;

struct php_lexer {
    const char  *src;
    size_t       len;
    size_t       p;

    zend_string *filename;

    uint32_t     lineno;     /* line number of offset `counted`              */
    size_t       counted;    /* newlines have been counted up to here        */

    int          state;
    int         *stack;
    size_t       sp;
    size_t       stack_cap;

    heredoc_frame *hd;
    size_t         hd_sp;
    size_t         hd_cap;

    zend_string *doc_comment;

    int  halted;
    int  errored;
    char error[LEX_ERRBUF];
};

#define SCAN_OK       0
#define SCAN_RESTART  1
#define SCAN_ERROR  (-1)

/* ---------------------------------------------------------- character sets */

static int lex_is_label_start(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80;
}

static int lex_is_digit(unsigned char c)
{
    return c >= '0' && c <= '9';
}

static int lex_is_label_cont(unsigned char c)
{
    return lex_is_label_start(c) || lex_is_digit(c);
}

static int lex_is_hex(unsigned char c)
{
    return lex_is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int lex_is_oct(unsigned char c)
{
    return c >= '0' && c <= '7';
}

static int lex_is_bin(unsigned char c)
{
    return c == '0' || c == '1';
}

static int lex_hexval(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

static char lex_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
}

/* Case insensitive compare of src[off..off+n) against a lowercase literal. */
static int lex_ci_eq(const char *s, size_t n, const char *lit)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (lit[i] == '\0' || lex_lower(s[i]) != lit[i]) return 0;
    }
    return lit[n] == '\0';
}

/* Byte at `off`, or 0 past the end -- php-src relies on a NUL sentinel the same
 * way, and a NUL byte is never valid PHP source anyway. */
static unsigned char lex_at(const php_lexer *lex, size_t off)
{
    return off < lex->len ? (unsigned char) lex->src[off] : (unsigned char) 0;
}

static int lex_at_line_start(const php_lexer *lex, size_t off)
{
    return off > 0 && (lex->src[off - 1] == '\n' || lex->src[off - 1] == '\r');
}

/* ------------------------------------------------------------ line numbers */

/* Advance the newline counter up to `off` and return the line number there.
 * The scanner only ever moves forward, so this stays O(n) overall. */
static uint32_t lex_line(php_lexer *lex, size_t off)
{
    if (off > lex->len) off = lex->len;
    while (lex->counted < off) {
        char c = lex->src[lex->counted];
        if (c == '\n') {
            lex->lineno++;
        } else if (c == '\r' &&
                   !(lex->counted + 1 < lex->len && lex->src[lex->counted + 1] == '\n')) {
            lex->lineno++;
        }
        lex->counted++;
    }
    return lex->lineno;
}

static int lex_fail(php_lexer *lex, size_t off, const char *fmt, ...)
{
    va_list ap;

    lex_line(lex, off);
    lex->errored = 1;
    va_start(ap, fmt);
    vsnprintf(lex->error, sizeof(lex->error), fmt, ap);
    va_end(ap);
    return SCAN_ERROR;
}

/* ------------------------------------------------------------ state stacks */

static void lex_push_state(php_lexer *lex, int st)
{
    if (lex->sp == lex->stack_cap) {
        size_t ncap = lex->stack_cap ? lex->stack_cap * 2 : 16;
        int *n = (int *) emalloc(ncap * sizeof(int));
        if (lex->stack) {
            memcpy(n, lex->stack, lex->sp * sizeof(int));
            efree(lex->stack);
        }
        lex->stack = n;
        lex->stack_cap = ncap;
    }
    lex->stack[lex->sp++] = lex->state;
    lex->state = st;
}

static void lex_pop_state(php_lexer *lex)
{
    lex->state = (lex->sp > 0) ? lex->stack[--lex->sp] : ST_IN_SCRIPTING;
}

static heredoc_frame *lex_push_heredoc(php_lexer *lex)
{
    if (lex->hd_sp == lex->hd_cap) {
        size_t ncap = lex->hd_cap ? lex->hd_cap * 2 : 4;
        heredoc_frame *n = (heredoc_frame *) emalloc(ncap * sizeof(heredoc_frame));
        if (lex->hd) {
            memcpy(n, lex->hd, lex->hd_sp * sizeof(heredoc_frame));
            efree(lex->hd);
        }
        lex->hd = n;
        lex->hd_cap = ncap;
    }
    memset(&lex->hd[lex->hd_sp], 0, sizeof(heredoc_frame));
    return &lex->hd[lex->hd_sp++];
}

/* --------------------------------------------------------- token emission */

static void lex_tok(php_token *out, php_token_type type, uint32_t line)
{
    out->type   = type;
    out->text   = NULL;
    out->lval   = 0;
    out->dval   = 0.0;
    out->lineno = line;
}

static void lex_tok_text(php_token *out, php_token_type type, uint32_t line,
                         const char *s, size_t n)
{
    lex_tok(out, type, line);
    out->text = zend_string_init(s, n);
}

/* ------------------------------------------------------- escape sequences */

/* Decode the escapes of a double quoted / heredoc / backtick chunk.
 * `quote` is '"', '`' or 0 (heredoc): only the active quote character can be
 * escaped, everything else keeps its backslash, exactly like php-src.
 * `dst` must have room for `n` bytes (decoding never grows the text). */
static int lex_escapes(php_lexer *lex, const char *s, size_t n, char quote,
                       char *dst, size_t *dstlen, size_t err_off)
{
    const char *end = s + n;
    char *t = dst;

    while (s < end) {
        if (*s != '\\') {
            *t++ = *s++;
            continue;
        }
        s++;
        if (s >= end) {                 /* trailing backslash */
            *t++ = '\\';
            break;
        }
        switch (*s) {
        case 'n':  *t++ = '\n'; s++; break;
        case 't':  *t++ = '\t'; s++; break;
        case 'r':  *t++ = '\r'; s++; break;
        case 'v':  *t++ = '\v'; s++; break;
        case 'f':  *t++ = '\f'; s++; break;
        case 'e':  *t++ = '\033'; s++; break;
        case '\\':
        case '$':  *t++ = *s++; break;
        case '"':
        case '`':
            if (*s != quote) {
                *t++ = '\\';
            }
            *t++ = *s++;
            break;
        case 'x':
        case 'X':
            if (s + 1 < end && lex_is_hex((unsigned char) s[1])) {
                int v = lex_hexval((unsigned char) s[1]);
                s += 2;
                if (s < end && lex_is_hex((unsigned char) *s)) {
                    v = v * 16 + lex_hexval((unsigned char) *s);
                    s++;
                }
                *t++ = (char) v;
            } else {
                *t++ = '\\';
                *t++ = *s++;
            }
            break;
        case 'u': {
            const char *q = s + 1;
            const char *hstart;
            unsigned long cp = 0;
            int too_big = 0;

            if (q >= end || *q != '{') {
                /* Not \u{...}: left alone so that JSON in string literals
                 * ("‮") keeps working. */
                *t++ = '\\';
                *t++ = *s++;
                break;
            }
            q++;
            hstart = q;
            while (q < end && lex_is_hex((unsigned char) *q)) {
                cp = cp * 16 + (unsigned long) lex_hexval((unsigned char) *q);
                if (cp > 0x10FFFFUL) too_big = 1;
                q++;
            }
            if (q >= end || *q != '}' || q == hstart) {
                return lex_fail(lex, err_off,
                                "Invalid UTF-8 codepoint escape sequence");
            }
            if (too_big) {
                return lex_fail(lex, err_off,
                                "Invalid UTF-8 codepoint escape sequence: Codepoint too large");
            }
            if (cp < 0x80) {
                *t++ = (char) cp;
            } else if (cp <= 0x7FF) {
                *t++ = (char) ((cp >> 6) + 0xC0);
                *t++ = (char) ((cp & 0x3F) + 0x80);
            } else if (cp <= 0xFFFF) {
                *t++ = (char) ((cp >> 12) + 0xE0);
                *t++ = (char) (((cp >> 6) & 0x3F) + 0x80);
                *t++ = (char) ((cp & 0x3F) + 0x80);
            } else {
                *t++ = (char) ((cp >> 18) + 0xF0);
                *t++ = (char) (((cp >> 12) & 0x3F) + 0x80);
                *t++ = (char) (((cp >> 6) & 0x3F) + 0x80);
                *t++ = (char) ((cp & 0x3F) + 0x80);
            }
            s = q + 1;
            break;
        }
        default:
            if (lex_is_oct((unsigned char) *s)) {
                int v = *s - '0';
                s++;
                if (s < end && lex_is_oct((unsigned char) *s)) {
                    v = v * 8 + (*s - '0');
                    s++;
                    if (s < end && lex_is_oct((unsigned char) *s)) {
                        v = v * 8 + (*s - '0');
                        s++;
                    }
                }
                *t++ = (char) v;
            } else {
                *t++ = '\\';
                *t++ = *s++;
            }
            break;
        }
    }
    *dstlen = (size_t) (t - dst);
    return SCAN_OK;
}

/* Escapes of a single quoted string: only \\ and \' mean anything. */
static void lex_sq_escapes(const char *s, size_t n, char *dst, size_t *dstlen)
{
    const char *end = s + n;
    char *t = dst;

    while (s < end) {
        if (*s == '\\' && s + 1 < end && (s[1] == '\\' || s[1] == '\'')) {
            *t++ = s[1];
            s += 2;
        } else {
            *t++ = *s++;
        }
    }
    *dstlen = (size_t) (t - dst);
}

/* ---------------------------------------------- heredoc body de-indentation */

static const char *lex_next_newline(const char *s, const char *end, size_t *nl_len)
{
    for (; s < end; s++) {
        if (*s == '\r') {
            *nl_len = (s + 1 < end && s[1] == '\n') ? 2 : 1;
            return s;
        }
        if (*s == '\n') {
            *nl_len = 1;
            return s;
        }
    }
    *nl_len = 0;
    return NULL;
}

/* Remove `indentation` leading whitespace characters from every line of a
 * heredoc/nowdoc body chunk (PHP 7.3 flexible closing markers).  A port of
 * php-src's strip_multiline_string_indentation(). */
static int lex_strip_indent(php_lexer *lex, char *buf, size_t *len_io,
                            size_t indentation, int using_spaces,
                            int newline_at_start, int newline_at_end,
                            size_t err_off)
{
    char *str = buf;
    char *cp  = buf;
    char *end = buf + *len_io;
    const char *nl;
    size_t nl_len = 0;

    if (indentation == 0) return SCAN_OK;

    if (!newline_at_start) {
        nl = lex_next_newline(str, end, &nl_len);
        if (!nl) return SCAN_OK;             /* single line, nothing to strip */
        str = (char *) nl + nl_len;
        cp  = str;
    } else {
        nl = str;
    }

    while (str <= end && nl) {
        size_t skip, seg;

        nl = lex_next_newline(str, end, &nl_len);
        if (!nl && newline_at_end) {
            nl = end;
            nl_len = 0;
        }

        for (skip = 0; skip < indentation; skip++, str++) {
            if (str == nl) break;            /* short whitespace-only line is ok */
            if (str == end || (*str != ' ' && *str != '\t')) {
                return lex_fail(lex, err_off,
                    "Invalid body indentation level (expecting an indentation level of at least %u)",
                    (unsigned) indentation);
            }
            if ((!using_spaces && *str == ' ') || (using_spaces && *str == '\t')) {
                return lex_fail(lex, err_off,
                    "Invalid indentation - tabs and spaces cannot be mixed");
            }
        }
        if (str == end) break;

        seg = nl ? (size_t) (nl - str) + nl_len : (size_t) (end - str);
        memmove(cp, str, seg);
        str += seg;
        cp  += seg;
    }

    *len_io = (size_t) (cp - buf);
    return SCAN_OK;
}

/* -------------------------------------------------------- numeric literals */

static void lex_strip_underscores(char *b, size_t *n)
{
    size_t i, o = 0;
    for (i = 0; i < *n; i++) {
        if (b[i] != '_') b[o++] = b[i];
    }
    b[o] = '\0';
    *n = o;
}

static double lex_digits_to_double(const char *d, size_t n, int base)
{
    double v = 0.0;
    size_t i;
    for (i = 0; i < n; i++) {
        v = v * base + lex_hexval((unsigned char) d[i]);
    }
    return v;
}

/* [digits](_[digits])* -- assumes the first character already matches. */
static size_t lex_scan_digits(php_lexer *lex, size_t i, int (*pred)(unsigned char))
{
    for (;;) {
        while (pred(lex_at(lex, i))) i++;

            /* "1_0" is the number 1 followed by the identifier _0 */

        break;
    }
    return i;
}

/* Scan one numeric literal starting at lex->p.  Handles decimal, 0x, 0b, 0o,
 * leading-zero octal, floats, exponents and `_` digit separators.  A decimal
 * integer that does not fit in a zend_long becomes a T_DNUMBER. */
static int lex_scan_number(php_lexer *lex, php_token *out, uint32_t line)
{
    size_t start = lex->p;
    size_t i = start;
    size_t rawn, n;
    char *nb;
    int is_float = 0, has_int = 0, is_octal = 0;
    int base = 10;
    php_token_type type = T_LNUMBER;
    zend_long lval = 0;
    double dval = 0.0;

    if (lex_at(lex, i) == '0' &&
        (lex_at(lex, i + 1) == 'x' || lex_at(lex, i + 1) == 'X') &&
        lex_is_hex(lex_at(lex, i + 2))) {
        base = 16;
        i = lex_scan_digits(lex, i + 2, lex_is_hex);
    } else if (lex_at(lex, i) == '0' &&
               (lex_at(lex, i + 1) == 'b' || lex_at(lex, i + 1) == 'B') &&
               lex_is_bin(lex_at(lex, i + 2))) {
        base = 2;
        i = lex_scan_digits(lex, i + 2, lex_is_bin);
    }
    else {
        if (lex_is_digit(lex_at(lex, i))) {
            i = lex_scan_digits(lex, i, lex_is_digit);
            has_int = 1;
        }
        if (lex_at(lex, i) == '.') {
            if (lex_is_digit(lex_at(lex, i + 1))) {
                i = lex_scan_digits(lex, i + 1, lex_is_digit);
                is_float = 1;
            } else if (has_int) {
                i++;                          /* "1." */
                is_float = 1;
            }
        }
        if ((has_int || is_float) &&
            (lex_at(lex, i) == 'e' || lex_at(lex, i) == 'E')) {
            size_t k = i + 1;
            if (lex_at(lex, k) == '+' || lex_at(lex, k) == '-') k++;
            if (lex_is_digit(lex_at(lex, k))) {
                i = lex_scan_digits(lex, k, lex_is_digit);
                is_float = 1;
            }
        }
        if (!is_float && has_int && lex_at(lex, start) == '0' && i - start > 1) {
            is_octal = 1;
            base = 8;
        }
    }

    rawn = i - start;
    nb = (char *) emalloc(rawn + 1);
    memcpy(nb, lex->src + start, rawn);
    nb[rawn] = '\0';
    n = rawn;
    lex_strip_underscores(nb, &n);

    if (is_float) {
        type = T_DNUMBER;
        dval = strtod(nb, NULL);
    } else if (base == 10 || is_octal) {
        char *endp;
        size_t j;

        if (is_octal) {
            /* VERSIONGATE:70000 else-branch is the older behaviour: strtol(.., 8)
               simply stops at the first digit that is not octal, so "090" is 0
               and "078" is 7 */
            if (PHP_TARGET_ID >= 70000) {
                for (j = 0; j < n; j++) {
                    if (nb[j] == '8' || nb[j] == '9') {
                        efree(nb);
                        return lex_fail(lex, start, "Invalid numeric literal");
                    }
                }
            } else {
                for (j = 0; j < n; j++) {
                    if (nb[j] == '8' || nb[j] == '9') {
                        nb[j] = '\0';
                        n = j;
                        break;
                    }
                }
            }
        }
        errno = 0;
        lval = (zend_long) strtoll(nb, &endp, is_octal ? 8 : 10);
        if (errno == ERANGE) {
            type = T_DNUMBER;
            dval = is_octal ? lex_digits_to_double(nb, n, 8) : strtod(nb, NULL);
        }
    } else {
        /* 0x / 0b / 0o: skip the prefix and any leading zeroes */
        char *d = nb + 2;
        size_t dn = n - 2;
        int fits;

        while (dn > 0 && *d == '0') { d++; dn--; }
        if (base == 16) {
            fits = (dn < 16) || (dn == 16 && *d <= '7');
        } else if (base == 2) {
            fits = (dn < 64);
        } else {
            fits = (dn < 21);
        }
        if (fits && dn == 0) {
            lval = 0;
        } else if (fits) {
            char *endp;
            errno = 0;
            lval = (zend_long) strtoll(d, &endp, base);
            if (errno == ERANGE) fits = 0;
        }
        if (!fits) {
            type = T_DNUMBER;
            dval = lex_digits_to_double(d, dn, base);
        }
    }
    efree(nb);

    lex->p = i;
    lex_tok_text(out, type, line, lex->src + start, rawn);
    out->lval = lval;
    out->dval = dval;
    return SCAN_OK;
}

/* --------------------------------------------------------------- keywords */

typedef struct {
    const char    *name;      /* lowercase */
    unsigned char  len;
    php_token_type tok;
} lex_keyword;

static const lex_keyword lex_keywords[] = {
    { "abstract",         8, T_ABSTRACT },
    { "and",              3, T_LOGICAL_AND },
    { "array",            5, T_ARRAY },
    { "as",               2, T_AS },
    { "break",            5, T_BREAK },
    { "callable",         8, T_CALLABLE },
    { "case",             4, T_CASE },
    { "catch",            5, T_CATCH },
    { "class",            5, T_CLASS },
    { "clone",            5, T_CLONE },
    { "const",            5, T_CONST },
    { "continue",         8, T_CONTINUE },
    { "declare",          7, T_DECLARE },
    { "default",          7, T_DEFAULT },
    { "die",              3, T_EXIT },
    { "do",               2, T_DO },
    { "echo",             4, T_ECHO },
    { "else",             4, T_ELSE },
    { "elseif",           6, T_ELSEIF },
    { "empty",            5, T_EMPTY },
    { "enddeclare",      10, T_ENDDECLARE },
    { "endfor",           6, T_ENDFOR },
    { "endforeach",      10, T_ENDFOREACH },
    { "endif",            5, T_ENDIF },
    { "endswitch",        9, T_ENDSWITCH },
    { "endwhile",         8, T_ENDWHILE },
    { "eval",             4, T_EVAL },
    { "exit",             4, T_EXIT },
    { "extends",          7, T_EXTENDS },
    { "final",            5, T_FINAL },
    { "finally",          7, T_FINALLY },
    { "fn",               2, T_FN },
    { "for",              3, T_FOR },
    { "foreach",          7, T_FOREACH },
    { "function",         8, T_FUNCTION },
    { "global",           6, T_GLOBAL },
    { "goto",             4, T_GOTO },
    { "if",               2, T_IF },
    { "implements",      10, T_IMPLEMENTS },
    { "include",          7, T_INCLUDE },
    { "include_once",    12, T_INCLUDE_ONCE },
    { "instanceof",      10, T_INSTANCEOF },
    { "insteadof",        9, T_INSTEADOF },
    { "interface",        9, T_INTERFACE },
    { "isset",            5, T_ISSET },
    { "list",             4, T_LIST },
    { "match",            5, T_MATCH },
    { "namespace",        9, T_NAMESPACE },
    { "new",              3, T_NEW },
    { "or",               2, T_LOGICAL_OR },
    { "print",            5, T_PRINT },
    { "private",          7, T_PRIVATE },
    { "protected",        9, T_PROTECTED },
    { "public",           6, T_PUBLIC },
    { "readonly",         8, T_READONLY },
    { "require",          7, T_REQUIRE },
    { "require_once",    12, T_REQUIRE_ONCE },
    { "return",           6, T_RETURN },
    { "static",           6, T_STATIC },
    { "switch",           6, T_SWITCH },
    { "throw",            5, T_THROW },
    { "trait",            5, T_TRAIT },
    { "try",              3, T_TRY },
    { "unset",            5, T_UNSET },
    { "use",              3, T_USE },
    { "var",              3, T_VAR },
    { "while",            5, T_WHILE },
    { "xor",              3, T_LOGICAL_XOR },
    { "__class__",        9, T_CLASS_C },
    { "__dir__",          7, T_DIR },
    { "__file__",         8, T_FILE },
    { "__function__",    12, T_FUNC_C },
    { "__halt_compiler", 15, T_HALT_COMPILER },
    { "__line__",         8, T_LINE },
    { "__method__",      10, T_METHOD_C },
    { "__namespace__",   13, T_NS_C },
    { "__property__",    12, T_PROPERTY_C },
    { "__trait__",        9, T_TRAIT_C },
    /* "enum" and "yield" need lookahead and are handled separately. */
};

static php_token_type lex_lookup_keyword(const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < sizeof(lex_keywords) / sizeof(lex_keywords[0]); i++) {
        if (lex_keywords[i].len == n && lex_ci_eq(s, n, lex_keywords[i].name)) {
            return lex_keywords[i].tok;
        }
    }
    return T_STRING;
}

/* --------------------------------------------- whitespace and comments */

/* Skip whitespace and all three comment forms.  Records doc comments. */
static int lex_skip_ws_comments(php_lexer *lex)
{
    for (;;) {
        unsigned char c = lex_at(lex, lex->p);

        if (lex->p >= lex->len) return SCAN_OK;

        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            lex->p++;
            continue;
        }
        if ((c == '/' && lex_at(lex, lex->p + 1) == '/') ||
            (c == '#' && lex_at(lex, lex->p + 1) != '[')) {
            size_t i = lex->p + (c == '#' ? 1 : 2);
            while (i < lex->len) {
                char ch = lex->src[i];
                if (ch == '\n' || ch == '\r') break;
                if (ch == '?' && lex_at(lex, i + 1) == '>') break;
                i++;
            }
            lex->p = i;
            continue;
        }
        if (c == '/' && lex_at(lex, lex->p + 1) == '*') {
            size_t start = lex->p;
            size_t i = lex->p + 2;
            int is_doc = (lex_at(lex, i) == '*' &&
                          (lex_at(lex, i + 1) == ' '  || lex_at(lex, i + 1) == '\t' ||
                           lex_at(lex, i + 1) == '\n' || lex_at(lex, i + 1) == '\r'));
            while (i + 1 < lex->len && !(lex->src[i] == '*' && lex->src[i + 1] == '/')) i++;
            if (i + 1 >= lex->len) {
                return lex_fail(lex, start, "Unterminated comment starting line %u",
                                (unsigned) lex_line(lex, start));
            }
            i += 2;
            if (is_doc) {
                lex->doc_comment = zend_string_init(lex->src + start, i - start);
            }
            lex->p = i;
            continue;
        }
        return SCAN_OK;
    }
}

/* Skip whitespace and comments without consuming them permanently; used by the
 * "yield from" and "enum <name>" lookaheads. */
static size_t lex_skip_ws_ahead(php_lexer *lex, size_t i)
{
    for (;;) {
        unsigned char c = lex_at(lex, i);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { i++; continue; }
        if ((c == '/' && lex_at(lex, i + 1) == '/') ||
            (c == '#' && lex_at(lex, i + 1) != '[')) {
            i += (c == '#' ? 1 : 2);
            while (i < lex->len && lex->src[i] != '\n' && lex->src[i] != '\r') {
                if (lex->src[i] == '?' && lex_at(lex, i + 1) == '>') break;
                i++;
            }
            continue;
        }
        if (c == '/' && lex_at(lex, i + 1) == '*') {
            i += 2;
            while (i + 1 < lex->len && !(lex->src[i] == '*' && lex->src[i + 1] == '/')) i++;
            i = (i + 1 < lex->len) ? i + 2 : lex->len;
            continue;
        }
        return i;
    }
}

/* ------------------------------------------------------------ identifiers */

static int lex_scan_identifier(php_lexer *lex, php_token *out, uint32_t line)
{
    size_t start = lex->p;
    size_t e = start;
    size_t n;

    while (lex_is_label_cont(lex_at(lex, e))) e++;
    n = e - start;

    /* Foo\Bar is a single name token since PHP 8.0. */
    if (lex_at(lex, e) == '\\' && lex_is_label_start(lex_at(lex, e + 1))) {
        size_t q = e;
        while (lex_at(lex, q) == '\\' && lex_is_label_start(lex_at(lex, q + 1))) {
            q++;
            while (lex_is_label_cont(lex_at(lex, q))) q++;
        }
        lex->p = q;
        if (n == 9 && lex_ci_eq(lex->src + start, 9, "namespace")) {
            lex_tok_text(out, T_NAME_RELATIVE, line,
                         lex->src + start + 10, q - start - 10);
        } else {
            lex_tok_text(out, T_NAME_QUALIFIED, line, lex->src + start, q - start);
        }
        return SCAN_OK;
    }

    /* "yield from" is one token, comments and newlines may sit in between. */
    if (n == 5 && lex_ci_eq(lex->src + start, 5, "yield")) {
        size_t k = lex_skip_ws_ahead(lex, e);
        if (k > e && k + 4 <= lex->len && lex_ci_eq(lex->src + k, 4, "from") &&
            !lex_is_label_cont(lex_at(lex, k + 4))) {
            lex->p = k + 4;
            lex_tok_text(out, T_YIELD_FROM, line, lex->src + start, lex->p - start);
            return SCAN_OK;
        }
        lex->p = e;
        lex_tok_text(out, T_YIELD, line, lex->src + start, n);
        return SCAN_OK;
    }

    /* "enum" is only a keyword directly in front of a class name. */
    if (n == 4 && lex_ci_eq(lex->src + start, 4, "enum")) {
        size_t k = lex_skip_ws_ahead(lex, e);
        php_token_type t = T_STRING;
        if (k > e && lex_is_label_start(lex_at(lex, k))) {
            size_t w = k;
            while (lex_is_label_cont(lex_at(lex, w))) w++;
            if (!((w - k == 7  && lex_ci_eq(lex->src + k, 7,  "extends")) ||
                  (w - k == 10 && lex_ci_eq(lex->src + k, 10, "implements")))) {
                t = T_ENUM;
            }
        }
        lex->p = e;
        lex_tok_text(out, t, line, lex->src + start, n);
        return SCAN_OK;
    }


    lex->p = e;
    lex_tok_text(out, lex_lookup_keyword(lex->src + start, n), line,
                 lex->src + start, n);
    return SCAN_OK;
}

/* ----------------------------------------------------------------- casts */

/* Returns the cast token for "( word )" at `start`, or T_LAST_TOKEN when the
 * parenthesis is not a cast.  Only spaces and tabs may sit inside. */
static php_token_type lex_cast_at(php_lexer *lex, size_t start, size_t *endp)
{
    size_t i = start + 1;
    size_t ws, n;
    const char *w;

    while (lex_at(lex, i) == ' ' || lex_at(lex, i) == '\t') i++;
    ws = i;
    while (lex_is_label_cont(lex_at(lex, i))) i++;
    n = i - ws;
    while (lex_at(lex, i) == ' ' || lex_at(lex, i) == '\t') i++;
    if (lex_at(lex, i) != ')' || n == 0) return T_LAST_TOKEN;

    *endp = i + 1;
    w = lex->src + ws;
    if (lex_ci_eq(w, n, "int")     || lex_ci_eq(w, n, "integer")) return T_INT_CAST;
    if (lex_ci_eq(w, n, "bool")    || lex_ci_eq(w, n, "boolean")) return T_BOOL_CAST;
    if (lex_ci_eq(w, n, "float")   || lex_ci_eq(w, n, "double")  ||
        lex_ci_eq(w, n, "real"))                                 return T_DOUBLE_CAST;
    if (lex_ci_eq(w, n, "string")  || lex_ci_eq(w, n, "binary"))  return T_STRING_CAST;
    if (lex_ci_eq(w, n, "array"))                                 return T_ARRAY_CAST;
    if (lex_ci_eq(w, n, "object"))                                return T_OBJECT_CAST;
    if (lex_ci_eq(w, n, "unset"))                                 return T_UNSET_CAST;
    return T_LAST_TOKEN;
}

/* --------------------------------------------------------------- strings */

static int lex_scan_sq(php_lexer *lex, php_token *out, uint32_t line, size_t qpos)
{
    size_t i = qpos + 1;
    size_t bodys = i;
    char *buf;
    size_t blen = 0;

    while (i < lex->len) {
        char ch = lex->src[i];
        if (ch == '\\') { i += (i + 1 < lex->len) ? 2 : 1; continue; }
        if (ch == '\'') break;
        i++;
    }
    if (i >= lex->len) {
        return lex_fail(lex, qpos, "Unterminated single-quoted string starting on line %u",
                        (unsigned) lex_line(lex, qpos));
    }
    buf = (char *) emalloc(i - bodys + 1);
    lex_sq_escapes(lex->src + bodys, i - bodys, buf, &blen);
    lex->p = i + 1;
    lex_tok(out, T_CONSTANT_ENCAPSED_STRING, line);
    out->text = zend_string_init(buf, blen);
    efree(buf);
    return SCAN_OK;
}

/* Does the string starting at `from` interpolate?  When it does not, *close is
 * the offset of the terminating quote (== lex->len when it is missing). */
static int lex_dq_has_interp(php_lexer *lex, size_t from, char quote, size_t *close)
{
    size_t i = from;

    while (i < lex->len) {
        unsigned char ch = (unsigned char) lex->src[i];
        if (ch == (unsigned char) quote) { *close = i; return 0; }
        if (ch == '\\') { i += (i + 1 < lex->len) ? 2 : 1; continue; }
        if (ch == '$' && (lex_is_label_start(lex_at(lex, i + 1)) ||
                          lex_at(lex, i + 1) == '{')) return 1;
        if (ch == '{' && lex_at(lex, i + 1) == '$') return 1;
        i++;
    }
    *close = lex->len;
    return 0;
}

static int lex_scan_dq_open(php_lexer *lex, php_token *out, uint32_t line, size_t qpos)
{
    size_t close = 0;
    char *buf;
    size_t n, blen = 0;

    if (lex_dq_has_interp(lex, qpos + 1, '"', &close)) {
        lex->p = qpos + 1;
        lex->state = ST_DOUBLE_QUOTES;
        lex_tok(out, T_DQ_START, line);
        return SCAN_OK;
    }
    if (close >= lex->len) {
        return lex_fail(lex, qpos, "Unterminated double-quoted string starting on line %u",
                        (unsigned) lex_line(lex, qpos));
    }
    n = close - (qpos + 1);
    buf = (char *) emalloc(n + 1);
    if (lex_escapes(lex, lex->src + qpos + 1, n, '"', buf, &blen, qpos) != SCAN_OK) {
        efree(buf);
        return SCAN_ERROR;
    }
    lex->p = close + 1;
    lex_tok(out, T_CONSTANT_ENCAPSED_STRING, line);
    out->text = zend_string_init(buf, blen);
    efree(buf);
    return SCAN_OK;
}

/* --------------------------------------------------------------- heredocs */

static size_t lex_next_line_start(php_lexer *lex, size_t i)
{
    while (i < lex->len) {
        char ch = lex->src[i];
        if (ch == '\n') return i + 1;
        if (ch == '\r') return (i + 1 < lex->len && lex->src[i + 1] == '\n') ? i + 2 : i + 1;
        i++;
    }
    return lex->len + 1;                 /* sentinel: there is no next line */
}

#define SCAN_NOT_HEREDOC 2

/* "<<<LABEL", "<<<\"LABEL\"" or "<<<'LABEL'" followed by a newline. */
static int lex_scan_heredoc_start(php_lexer *lex, php_token *out, uint32_t line,
                                  size_t start)
{
    size_t i = start + 3;
    size_t ls, ll, body_start, s;
    int nowdoc = 0, quoted = 0;
    heredoc_frame *hf;
    int found = 0;
    size_t term = 0, indentation = 0;
    int uses_spaces = 0;

    while (lex_at(lex, i) == ' ' || lex_at(lex, i) == '\t') i++;
    if (lex_at(lex, i) == '\'')      { nowdoc = 1; quoted = 1; i++; }
    else if (lex_at(lex, i) == '"')  { quoted = 1; i++; }

    if (!lex_is_label_start(lex_at(lex, i))) return SCAN_NOT_HEREDOC;
    ls = i;
    while (lex_is_label_cont(lex_at(lex, i))) i++;
    ll = i - ls;

    if (quoted) {
        if (lex_at(lex, i) != (unsigned char) (nowdoc ? '\'' : '"')) return SCAN_NOT_HEREDOC;
        i++;
    }
    if (lex_at(lex, i) == '\r')      { i++; if (lex_at(lex, i) == '\n') i++; }
    else if (lex_at(lex, i) == '\n') { i++; }
    else                             { return SCAN_NOT_HEREDOC; }
    body_start = i;

    /* Locate the closing marker so that the body extent and the indentation to
     * strip are known up front. */
    s = body_start;
    while (s <= lex->len) {
        size_t j = s;
        int spacing = 0;


            if (j + ll < lex->len && memcmp(lex->src + j, lex->src + ls, ll) == 0) {
                size_t e = j + ll;
                if (lex_at(lex, e) == ';') e++;
                if (lex_at(lex, e) == '\n' || lex_at(lex, e) == '\r') {
                    found = 1;
                    term = s;
                    indentation = 0;
                    uses_spaces = 0;
                }
            }

        if (found) break;
        s = lex_next_line_start(lex, s);
        if (s > lex->len) break;
    }
    if (!found) {
        return lex_fail(lex, start, "Unterminated %s starting on line %u",
                        nowdoc ? "nowdoc" : "heredoc", (unsigned) lex_line(lex, start));
    }

    hf = lex_push_heredoc(lex);
    hf->label_off  = ls;
    hf->label_len  = ll;
    hf->indentation = indentation;
    hf->uses_spaces = uses_spaces;
    hf->is_nowdoc   = nowdoc;
    hf->body_start  = body_start;
    hf->term_line_start = term;
    if (term == body_start) {
        hf->body_end = body_start;
    } else {
        size_t e = term - 1;             /* the newline in front of the marker */
        if (lex->src[e] == '\n' && e > body_start && lex->src[e - 1] == '\r') e--;
        hf->body_end = e;
    }

    lex->p = body_start;
    lex->state = nowdoc ? ST_NOWDOC : ST_HEREDOC;
    lex_tok_text(out, T_START_HEREDOC, line, lex->src + ls, ll);
    return SCAN_OK;
}

/* --------------------------------------------------------- ST_INITIAL */

/* Length of the PHP open tag at `i`, 0 when there is none. */
static size_t lex_open_tag_len(php_lexer *lex, size_t i, int *with_echo)
{
    *with_echo = 0;
    if (lex_at(lex, i) != '<' || lex_at(lex, i + 1) != '?') return 0;
    if (lex_at(lex, i + 2) == '=') { *with_echo = 1; return 3; }
    if (i + 5 <= lex->len && lex_ci_eq(lex->src + i + 2, 3, "php")) {
        unsigned char c = lex_at(lex, i + 5);
        if (i + 5 == lex->len) return 5;            /* "<?php" at end of file */
        if (c == ' ' || c == '\t' || c == '\n') return 6;
        if (c == '\r') return (lex_at(lex, i + 6) == '\n') ? 7 : 6;
    }
    return 0;                                        /* short tags stay off */
}

static int lex_scan_initial(php_lexer *lex, php_token *out)
{
    size_t start = lex->p;
    uint32_t line = lex_line(lex, start);
    int echo = 0;
    size_t tl, i;

    if (start >= lex->len) {
        lex_tok(out, T_EOF, line);
        return SCAN_OK;
    }
    tl = lex_open_tag_len(lex, start, &echo);
    if (tl) {
        lex->p = start + tl;
        lex->state = ST_IN_SCRIPTING;
        lex_tok(out, echo ? T_OPEN_TAG_WITH_ECHO : T_OPEN_TAG, line);
        return SCAN_OK;
    }
    i = start + 1;
    for (;;) {
        const char *hit;
        if (i >= lex->len) { i = lex->len; break; }
        hit = (const char *) memchr(lex->src + i, '<', lex->len - i);
        if (!hit) { i = lex->len; break; }
        i = (size_t) (hit - lex->src);
        if (lex_open_tag_len(lex, i, &echo)) break;
        i++;
    }
    lex->p = i;
    lex_tok_text(out, T_INLINE_HTML, line, lex->src + start, i - start);
    return SCAN_OK;
}

/* ----------------------------------------------------- ST_IN_SCRIPTING */

#define LEX_OP(nchars, token)                       \
    do {                                            \
        lex->p = start + (nchars);                  \
        lex_tok(out, (token), line);                \
        return SCAN_OK;                             \
    } while (0)

static int lex_scan_scripting(php_lexer *lex, php_token *out)
{
    size_t start;
    uint32_t line;
    unsigned char c, c1, c2;

    if (lex_skip_ws_comments(lex) != SCAN_OK) return SCAN_ERROR;

    start = lex->p;
    line  = lex_line(lex, start);
    if (start >= lex->len) {
        lex_tok(out, T_EOF, line);
        return SCAN_OK;
    }

    c  = lex_at(lex, start);
    c1 = lex_at(lex, start + 1);
    c2 = lex_at(lex, start + 2);

    /* b"..." / b'...' / b<<<EOT binary string prefix */
    if (c == 'b' || c == 'B') {
        if (c1 == '\'') return lex_scan_sq(lex, out, line, start + 1);
        if (c1 == '"')  return lex_scan_dq_open(lex, out, line, start + 1);
        if (c1 == '<' && c2 == '<' && lex_at(lex, start + 3) == '<') {
            int r = lex_scan_heredoc_start(lex, out, line, start + 1);
            if (r != SCAN_NOT_HEREDOC) return r;
        }
    }

    switch (c) {
    case '$':
        if (lex_is_label_start(c1)) {
            size_t e = start + 2;
            while (lex_is_label_cont(lex_at(lex, e))) e++;
            lex->p = e;
            lex_tok_text(out, T_VARIABLE, line, lex->src + start + 1, e - start - 1);
            return SCAN_OK;
        }
        LEX_OP(1, T_DOLLAR);

    case '\'':
        return lex_scan_sq(lex, out, line, start);
    case '"':
        return lex_scan_dq_open(lex, out, line, start);

    case '`':
        lex->p = start + 1;
        lex->state = ST_BACKQUOTE;
        lex_tok(out, T_BACKQUOTE, line);
        return SCAN_OK;

    case '\\':
        if (lex_is_label_start(c1)) {
            size_t q = start;
            while (lex_at(lex, q) == '\\' && lex_is_label_start(lex_at(lex, q + 1))) {
                q++;
                while (lex_is_label_cont(lex_at(lex, q))) q++;
            }
            lex->p = q;
            lex_tok_text(out, T_NAME_FULLY_QUALIFIED, line,
                         lex->src + start + 1, q - start - 1);
            return SCAN_OK;
        }
        LEX_OP(1, T_NS_SEPARATOR);

    case '(': {
        size_t e = 0;
        php_token_type t = lex_cast_at(lex, start, &e);
        if (t != T_LAST_TOKEN) {
            lex->p = e;
            lex_tok(out, t, line);
            return SCAN_OK;
        }
        LEX_OP(1, T_LPAREN);
    }
    case ')': LEX_OP(1, T_RPAREN);
    case '[': LEX_OP(1, T_LBRACKET);
    case ']': LEX_OP(1, T_RBRACKET);

    case '{':
        lex->p = start + 1;
        lex_push_state(lex, ST_IN_SCRIPTING);
        lex_tok(out, T_LBRACE, line);
        return SCAN_OK;

    case '}':
        lex->p = start + 1;
        if (lex->sp > 0) lex_pop_state(lex);
        lex->doc_comment = NULL;
        lex_tok(out, T_RBRACE, line);
        return SCAN_OK;

    case ';': LEX_OP(1, T_SEMICOLON);
    case ',': LEX_OP(1, T_COMMA);
    case '@': LEX_OP(1, T_AT);
    case '~': LEX_OP(1, T_TILDE);

    case ':':
        if (c1 == ':') LEX_OP(2, T_PAAMAYIM_NEKUDOTAYIM);
        LEX_OP(1, T_COLON);

    case '#':
        /* comments were skipped above, so this can only be "#[" */
        LEX_OP(2, T_ATTRIBUTE);

    case '?':
        if (c1 == '-' && c2 == '>') {
            lex->p = start + 3;
            lex_push_state(lex, ST_LOOKING_FOR_PROPERTY);
            lex_tok(out, T_NULLSAFE_OBJECT_OPERATOR, line);
            return SCAN_OK;
        }
        if (c1 == '?' && c2 == '=') LEX_OP(3, T_COALESCE_EQUAL);
        if (c1 == '?')              LEX_OP(2, T_COALESCE);
        if (c1 == '>') {
            size_t e = start + 2;
            if (lex_at(lex, e) == '\r')      { e++; if (lex_at(lex, e) == '\n') e++; }
            else if (lex_at(lex, e) == '\n') { e++; }
            lex->p = e;
            lex->state = ST_INITIAL;
            lex_tok(out, T_CLOSE_TAG, line);
            return SCAN_OK;
        }
        LEX_OP(1, T_QUESTION);

    case '=':
        if (c1 == '=' && c2 == '=') LEX_OP(3, T_IS_IDENTICAL);
        if (c1 == '=')              LEX_OP(2, T_IS_EQUAL);
        if (c1 == '>')              LEX_OP(2, T_DOUBLE_ARROW);
        LEX_OP(1, T_ASSIGN);

    case '!':
        if (c1 == '=' && c2 == '=') LEX_OP(3, T_IS_NOT_IDENTICAL);
        if (c1 == '=')              LEX_OP(2, T_IS_NOT_EQUAL);
        LEX_OP(1, T_BANG);

    case '+':
        if (c1 == '=') LEX_OP(2, T_PLUS_EQUAL);
        if (c1 == '+') LEX_OP(2, T_INC);
        LEX_OP(1, T_PLUS);

    case '-':
        if (c1 == '=') LEX_OP(2, T_MINUS_EQUAL);
        if (c1 == '-') LEX_OP(2, T_DEC);
        if (c1 == '>') {
            lex->p = start + 2;
            lex_push_state(lex, ST_LOOKING_FOR_PROPERTY);
            lex_tok(out, T_OBJECT_OPERATOR, line);
            return SCAN_OK;
        }
        LEX_OP(1, T_MINUS);

    case '*':
        if (c1 == '*' && c2 == '=') LEX_OP(3, T_POW_EQUAL);
        if (c1 == '*')              LEX_OP(2, T_POW);
        if (c1 == '=')              LEX_OP(2, T_MUL_EQUAL);
        LEX_OP(1, T_MUL);

    case '/':
        if (c1 == '=') LEX_OP(2, T_DIV_EQUAL);
        LEX_OP(1, T_DIV);

    case '%':
        if (c1 == '=') LEX_OP(2, T_MOD_EQUAL);
        LEX_OP(1, T_MOD);

    case '.':
        if (c1 == '.' && c2 == '.') LEX_OP(3, T_ELLIPSIS);
        if (c1 == '=')              LEX_OP(2, T_CONCAT_EQUAL);
        if (lex_is_digit(c1))       return lex_scan_number(lex, out, line);
        LEX_OP(1, T_DOT);

    case '&':
        if (c1 == '&') LEX_OP(2, T_BOOLEAN_AND);
        if (c1 == '=') LEX_OP(2, T_AND_EQUAL);
        LEX_OP(1, T_AMP);

    case '|':
        if (c1 == '|') LEX_OP(2, T_BOOLEAN_OR);
        if (c1 == '=') LEX_OP(2, T_OR_EQUAL);
        LEX_OP(1, T_PIPE);

    case '^':
        if (c1 == '=') LEX_OP(2, T_XOR_EQUAL);
        LEX_OP(1, T_CARET);

    case '<':
        if (c1 == '<' && c2 == '<') {
            int r = lex_scan_heredoc_start(lex, out, line, start);
            if (r != SCAN_NOT_HEREDOC) return r;
        }
        if (c1 == '<' && c2 == '=') LEX_OP(3, T_SL_EQUAL);
        if (c1 == '<')              LEX_OP(2, T_SL);
        if (c1 == '=' && c2 == '>') LEX_OP(3, T_SPACESHIP);
        if (c1 == '=')              LEX_OP(2, T_IS_SMALLER_OR_EQUAL);
        if (c1 == '>')              LEX_OP(2, T_IS_NOT_EQUAL);
        LEX_OP(1, T_LT);

    case '>':
        if (c1 == '>' && c2 == '=') LEX_OP(3, T_SR_EQUAL);
        if (c1 == '>')              LEX_OP(2, T_SR);
        if (c1 == '=')              LEX_OP(2, T_IS_GREATER_OR_EQUAL);
        LEX_OP(1, T_GT);

    default:
        if (lex_is_digit(c))       return lex_scan_number(lex, out, line);
        if (lex_is_label_start(c)) return lex_scan_identifier(lex, out, line);
        return lex_fail(lex, start, "Unexpected character in input: '%c' (ASCII=%d)",
                        (c >= 32 && c < 127) ? (char) c : '?', (int) c);
    }
}

/* ------------------------------- ST_DOUBLE_QUOTES / ST_BACKQUOTE / ST_HEREDOC */

static int lex_scan_string_body(php_lexer *lex, php_token *out)
{
    int st = lex->state;
    char quote = (st == ST_DOUBLE_QUOTES) ? '"' : (st == ST_BACKQUOTE) ? '`' : '\0';
    heredoc_frame *hf = (st == ST_HEREDOC) ? &lex->hd[lex->hd_sp - 1] : NULL;
    size_t limit = hf ? hf->body_end : lex->len;
    size_t start = lex->p;
    uint32_t line = lex_line(lex, start);
    unsigned char c, c1;
    size_t i, rawn, blen, outn = 0;
    char *buf, *dst;

    if (hf) {
        if (start >= limit) {
            lex->p = hf->term_line_start;
            lex->state = ST_END_HEREDOC;
            return SCAN_RESTART;
        }
    } else {
        if (start >= lex->len) {
            return lex_fail(lex, start, "Unterminated %s string",
                            quote == '"' ? "double-quoted" : "shell-exec");
        }
        if (lex->src[start] == quote) {
            lex->p = start + 1;
            lex->state = ST_IN_SCRIPTING;
            lex_tok(out, (quote == '"') ? T_DQ_END : T_BACKQUOTE, line);
            return SCAN_OK;
        }
    }

    c  = lex_at(lex, start);
    c1 = lex_at(lex, start + 1);

    /* An interpolation sitting at the start of a line cannot be de-indented. */
    if (hf && hf->indentation && lex_at_line_start(lex, start) &&
        ((c == '$' && (lex_is_label_start(c1) || c1 == '{')) ||
         (c == '{' && c1 == '$'))) {
        return lex_fail(lex, start,
            "Invalid body indentation level (expecting an indentation level of at least %u)",
            (unsigned) hf->indentation);
    }

    if (c == '$' && lex_is_label_start(c1)) {          /* simple syntax */
        size_t e = start + 2;
        while (lex_is_label_cont(lex_at(lex, e))) e++;
        if (lex_at(lex, e) == '-' && lex_at(lex, e + 1) == '>' &&
            lex_is_label_start(lex_at(lex, e + 2))) {
            lex_push_state(lex, ST_LOOKING_FOR_PROPERTY);
        } else if (lex_at(lex, e) == '?' && lex_at(lex, e + 1) == '-' &&
                   lex_at(lex, e + 2) == '>' && lex_is_label_start(lex_at(lex, e + 3))) {
            lex_push_state(lex, ST_LOOKING_FOR_PROPERTY);
        } else if (lex_at(lex, e) == '[') {
            lex_push_state(lex, ST_VAR_OFFSET);
        }
        lex->p = e;
        lex_tok_text(out, T_VARIABLE, line, lex->src + start + 1, e - start - 1);
        return SCAN_OK;
    }
    if (c == '$' && c1 == '{') {                       /* ${name} / ${expr} */
        lex->p = start + 2;
        lex_push_state(lex, ST_LOOKING_FOR_VARNAME);
        lex_tok(out, T_DOLLAR_OPEN_CURLY_BRACES, line);
        return SCAN_OK;
    }
    if (c == '{' && c1 == '$') {                       /* {$expr} */
        lex->p = start + 1;                            /* '$' re-scanned as code */
        lex_push_state(lex, ST_IN_SCRIPTING);
        lex_tok(out, T_CURLY_OPEN, line);
        return SCAN_OK;
    }

    /* literal chunk */
    i = start;
    while (i < limit) {
        unsigned char ch = (unsigned char) lex->src[i];
        if (!hf && ch == (unsigned char) quote) break;
        if (ch == '\\') {
            /* inside a heredoc a backslash never escapes the newline */
            if (i + 1 < limit &&
                !(hf && (lex->src[i + 1] == '\n' || lex->src[i + 1] == '\r'))) {
                i += 2;
            } else {
                i += 1;
            }
            continue;
        }
        if (ch == '$' && (lex_is_label_start(lex_at(lex, i + 1)) ||
                          lex_at(lex, i + 1) == '{')) break;
        if (ch == '{' && lex_at(lex, i + 1) == '$') break;
        i++;
    }
    if (!hf && i >= lex->len) {
        return lex_fail(lex, start, "Unterminated %s string",
                        quote == '"' ? "double-quoted" : "shell-exec");
    }

    rawn = i - start;
    buf  = (char *) emalloc(rawn + 1);
    memcpy(buf, lex->src + start, rawn);
    buf[rawn] = '\0';
    blen = rawn;
    if (hf && hf->indentation) {
        if (lex_strip_indent(lex, buf, &blen, hf->indentation, hf->uses_spaces,
                             lex_at_line_start(lex, start), i == limit,
                             start) != SCAN_OK) {
            efree(buf);
            return SCAN_ERROR;
        }
    }
    dst = (char *) emalloc(blen + 1);
    if (lex_escapes(lex, buf, blen, quote, dst, &outn, start) != SCAN_OK) {
        efree(buf);
        efree(dst);
        return SCAN_ERROR;
    }
    efree(buf);
    lex->p = i;
    lex_tok(out, T_ENCAPSED_AND_WHITESPACE, line);
    out->text = zend_string_init(dst, outn);
    efree(dst);
    return SCAN_OK;
}

/* ------------------------------------------------------------- ST_NOWDOC */

static int lex_scan_nowdoc(php_lexer *lex, php_token *out)
{
    heredoc_frame *hf = &lex->hd[lex->hd_sp - 1];
    size_t start = lex->p;
    uint32_t line = lex_line(lex, start);
    size_t rawn, blen;
    char *buf;

    if (start >= hf->body_end) {
        lex->p = hf->term_line_start;
        lex->state = ST_END_HEREDOC;
        return SCAN_RESTART;
    }
    rawn = hf->body_end - start;
    buf = (char *) emalloc(rawn + 1);
    memcpy(buf, lex->src + start, rawn);
    buf[rawn] = '\0';
    blen = rawn;
    if (hf->indentation) {
        if (lex_strip_indent(lex, buf, &blen, hf->indentation, hf->uses_spaces,
                             lex_at_line_start(lex, start), 1, start) != SCAN_OK) {
            efree(buf);
            return SCAN_ERROR;
        }
    }
    lex->p = hf->body_end;
    lex_tok(out, T_ENCAPSED_AND_WHITESPACE, line);
    out->text = zend_string_init(buf, blen);
    efree(buf);
    return SCAN_OK;
}

/* --------------------------------------------------------- ST_END_HEREDOC */

static int lex_scan_end_heredoc(php_lexer *lex, php_token *out)
{
    heredoc_frame *hf = &lex->hd[lex->hd_sp - 1];
    size_t start = hf->term_line_start;
    uint32_t line = lex_line(lex, start);

    lex->p = start + hf->indentation + hf->label_len;
    lex_tok_text(out, T_END_HEREDOC, line, lex->src + hf->label_off, hf->label_len);
    lex->state = ST_IN_SCRIPTING;
    lex->hd_sp--;
    return SCAN_OK;
}

/* -------------------------------------------------- ST_LOOKING_FOR_PROPERTY */

static int lex_scan_property(php_lexer *lex, php_token *out)
{
    size_t start;
    uint32_t line;

    if (lex_skip_ws_comments(lex) != SCAN_OK) return SCAN_ERROR;
    start = lex->p;
    line  = lex_line(lex, start);

    if (lex_at(lex, start) == '-' && lex_at(lex, start + 1) == '>') {
        lex->p = start + 2;
        lex_tok(out, T_OBJECT_OPERATOR, line);
        return SCAN_OK;
    }
    if (lex_at(lex, start) == '?' && lex_at(lex, start + 1) == '-' &&
        lex_at(lex, start + 2) == '>') {
        lex->p = start + 3;
        lex_tok(out, T_NULLSAFE_OBJECT_OPERATOR, line);
        return SCAN_OK;
    }
    if (lex_is_label_start(lex_at(lex, start))) {
        size_t e = start + 1;
        while (lex_is_label_cont(lex_at(lex, e))) e++;
        lex->p = e;
        lex_pop_state(lex);
        lex_tok_text(out, T_STRING, line, lex->src + start, e - start);
        return SCAN_OK;
    }
    lex_pop_state(lex);
    return SCAN_RESTART;
}

/* --------------------------------------------------- ST_LOOKING_FOR_VARNAME */

static int lex_scan_varname(php_lexer *lex, php_token *out)
{
    size_t start = lex->p;
    uint32_t line = lex_line(lex, start);

    if (lex_is_label_start(lex_at(lex, start))) {
        size_t e = start + 1;
        while (lex_is_label_cont(lex_at(lex, e))) e++;
        if (lex_at(lex, e) == '[' || lex_at(lex, e) == '}') {
            lex->p = e;
            lex->state = ST_IN_SCRIPTING;   /* the string state stays on the stack */
            lex_tok_text(out, T_STRING_VARNAME, line, lex->src + start, e - start);
            return SCAN_OK;
        }
    }
    lex->state = ST_IN_SCRIPTING;
    return SCAN_RESTART;
}

/* --------------------------------------------------------- ST_VAR_OFFSET */

static int lex_scan_var_offset(php_lexer *lex, php_token *out)
{
    size_t start = lex->p;
    uint32_t line = lex_line(lex, start);
    unsigned char c = lex_at(lex, start);
    size_t i, ds, dn;
    int neg = 0;

    if (c == '[') { lex->p = start + 1; lex_tok(out, T_LBRACKET, line); return SCAN_OK; }
    if (c == ']') {
        lex->p = start + 1;
        lex_pop_state(lex);
        lex_tok(out, T_RBRACKET, line);
        return SCAN_OK;
    }
    if (c == '$' && lex_is_label_start(lex_at(lex, start + 1))) {
        size_t e = start + 2;
        while (lex_is_label_cont(lex_at(lex, e))) e++;
        lex->p = e;
        lex_tok_text(out, T_VARIABLE, line, lex->src + start + 1, e - start - 1);
        return SCAN_OK;
    }

    i = start;
    if (c == '-' && lex_is_digit(lex_at(lex, start + 1))) { neg = 1; i++; }
    if (lex_is_digit(lex_at(lex, i))) {
        ds = i;
        while (lex_is_digit(lex_at(lex, i))) i++;
        dn = i - ds;
        if (!lex_is_label_cont(lex_at(lex, i)) && dn <= 20) {
            char b[24];
            long long v;
            memcpy(b, lex->src + ds, dn);
            b[dn] = '\0';
            errno = 0;
            v = strtoll(b, NULL, 10);
            if (errno != ERANGE) {
                lex->p = i;
                lex_tok_text(out, T_LNUMBER, line, lex->src + start, i - start);
                out->lval = neg ? -(zend_long) v : (zend_long) v;
                return SCAN_OK;
            }
        }
        if (neg) {                          /* let the parser choke on the '-' */
            lex->p = start + 1;
            lex_tok(out, T_MINUS, line);
            return SCAN_OK;
        }
        while (lex_is_label_cont(lex_at(lex, i))) i++;
        lex->p = i;
        lex_tok_text(out, T_STRING, line, lex->src + start, i - start);
        return SCAN_OK;
    }
    if (lex_is_label_start(c)) {
        size_t e = start + 1;
        while (lex_is_label_cont(lex_at(lex, e))) e++;
        lex->p = e;
        lex_tok_text(out, T_STRING, line, lex->src + start, e - start);
        return SCAN_OK;
    }
    return lex_fail(lex, start,
                    "Unexpected character in string offset: '%c' (ASCII=%d)",
                    (c >= 32 && c < 127) ? (char) c : '?', (int) c);
}

/* ------------------------------------------------------------ token names */

static const char *const lex_token_names[] = {
    "T_EOF", "T_INLINE_HTML", "T_OPEN_TAG", "T_OPEN_TAG_WITH_ECHO",
    "T_CLOSE_TAG", "T_VARIABLE", "T_STRING", "T_NAME_QUALIFIED",
    "T_NAME_FULLY_QUALIFIED", "T_NAME_RELATIVE", "T_NS_SEPARATOR", "T_LNUMBER",
    "T_DNUMBER", "T_CONSTANT_ENCAPSED_STRING", "T_ENCAPSED_AND_WHITESPACE", "T_STRING_VARNAME",
    "T_NUM_STRING", "T_DQ_START", "T_DQ_END", "T_START_HEREDOC",
    "T_END_HEREDOC", "T_CURLY_OPEN", "T_DOLLAR_OPEN_CURLY_BRACES", "T_ABSTRACT",
    "T_ARRAY", "T_AS", "T_BREAK", "T_CALLABLE",
    "T_CASE", "T_CATCH", "T_CLASS", "T_CLONE",
    "T_CONST", "T_CONTINUE", "T_DECLARE", "T_DEFAULT",
    "T_DO", "T_ECHO", "T_ELSE", "T_ELSEIF",
    "T_EMPTY", "T_ENDDECLARE", "T_ENDFOR", "T_ENDFOREACH",
    "T_ENDIF", "T_ENDSWITCH", "T_ENDWHILE", "T_ENUM",
    "T_EVAL", "T_EXIT", "T_EXTENDS", "T_FINAL",
    "T_FINALLY", "T_FN", "T_FOR", "T_FOREACH",
    "T_FUNCTION", "T_GLOBAL", "T_GOTO", "T_HALT_COMPILER",
    "T_IF", "T_IMPLEMENTS", "T_INCLUDE", "T_INCLUDE_ONCE",
    "T_INSTANCEOF", "T_INSTEADOF", "T_INTERFACE", "T_ISSET",
    "T_LIST", "T_LOGICAL_AND", "T_LOGICAL_OR", "T_LOGICAL_XOR",
    "T_MATCH", "T_NAMESPACE", "T_NEW", "T_PRINT",
    "T_PRIVATE", "T_PRIVATE_SET", "T_PROTECTED", "T_PROTECTED_SET",
    "T_PUBLIC", "T_PUBLIC_SET", "T_READONLY", "T_REQUIRE",
    "T_REQUIRE_ONCE", "T_RETURN", "T_STATIC", "T_SWITCH",
    "T_THROW", "T_TRAIT", "T_TRY", "T_UNSET",
    "T_USE", "T_VAR", "T_WHILE", "T_YIELD",
    "T_YIELD_FROM", "T_LINE", "T_FILE", "T_DIR",
    "T_FUNC_C", "T_CLASS_C", "T_METHOD_C", "T_NS_C",
    "T_TRAIT_C", "T_PROPERTY_C", "T_INT_CAST", "T_DOUBLE_CAST",
    "T_STRING_CAST", "T_ARRAY_CAST", "T_OBJECT_CAST", "T_BOOL_CAST",
    "T_UNSET_CAST", "T_IS_IDENTICAL", "T_IS_NOT_IDENTICAL", "T_IS_EQUAL",
    "T_IS_NOT_EQUAL", "T_SPACESHIP", "T_IS_SMALLER_OR_EQUAL", "T_IS_GREATER_OR_EQUAL",
    "T_INC", "T_DEC", "T_POW", "T_PLUS_EQUAL",
    "T_MINUS_EQUAL", "T_MUL_EQUAL", "T_DIV_EQUAL", "T_CONCAT_EQUAL",
    "T_MOD_EQUAL", "T_AND_EQUAL", "T_OR_EQUAL", "T_XOR_EQUAL",
    "T_SL_EQUAL", "T_SR_EQUAL", "T_POW_EQUAL", "T_COALESCE_EQUAL",
    "T_COALESCE", "T_NULLSAFE_OBJECT_OPERATOR", "T_OBJECT_OPERATOR", "T_DOUBLE_ARROW",
    "T_PAAMAYIM_NEKUDOTAYIM", "T_ELLIPSIS", "T_BOOLEAN_AND", "T_BOOLEAN_OR",
    "T_SL", "T_SR", "T_ATTRIBUTE", "T_SEMICOLON",
    "T_COMMA", "T_COLON", "T_QUESTION", "T_LPAREN",
    "T_RPAREN", "T_LBRACKET", "T_RBRACKET", "T_LBRACE",
    "T_RBRACE", "T_PLUS", "T_MINUS", "T_MUL",
    "T_DIV", "T_MOD", "T_DOT", "T_ASSIGN",
    "T_BANG", "T_AMP", "T_PIPE", "T_CARET",
    "T_TILDE", "T_LT", "T_GT", "T_AT",
    "T_DOLLAR", "T_BACKQUOTE", "T_COMMENT", "T_DOC_COMMENT",
    "T_WHITESPACE", "T_BAD_CHARACTER",
};

_Static_assert(sizeof(lex_token_names) / sizeof(lex_token_names[0]) ==
               (size_t) T_LAST_TOKEN,
               "php_token_name table is out of sync with php_token_type");

const char *php_token_name(php_token_type type)
{
    if ((int) type < 0 || (int) type >= (int) T_LAST_TOKEN) return "T_UNKNOWN";
    return lex_token_names[type];
}

/* ------------------------------------------------------------- public API */

php_lexer *php_lexer_new_ex(const char *src, size_t len, zend_string *filename,
                            int in_scripting)
{
    php_lexer *lex = (php_lexer *) emalloc(sizeof(php_lexer));

    memset(lex, 0, sizeof(*lex));
    lex->src      = src ? src : "";
    lex->len      = src ? len : 0;
    lex->filename = filename;
    lex->lineno   = 1;
    lex->state    = in_scripting ? ST_IN_SCRIPTING : ST_INITIAL;
    lex->error[0] = '\0';

    /* A "#!" first line is skipped, the way the CLI does for a script. */
    if (!in_scripting && lex->len >= 2 && lex->src[0] == '#' && lex->src[1] == '!') {
        size_t i = lex_next_line_start(lex, 0);
        lex->p = (i > lex->len) ? lex->len : i;
    }
    return lex;
}

php_lexer *php_lexer_new(const char *src, size_t len, zend_string *filename)
{
    return php_lexer_new_ex(src, len, filename, 0);
}

void php_lexer_free(php_lexer *lex)
{
    if (!lex) return;
    if (lex->stack) efree(lex->stack);
    if (lex->hd)    efree(lex->hd);
    efree(lex);
}

int php_lexer_next(php_lexer *lex, php_token *out)
{
    lex_tok(out, T_EOF, lex->lineno);
    if (lex->errored) return -1;
    if (lex->halted) {
        lex_tok(out, T_EOF, lex_line(lex, lex->p));
        return 0;
    }

    for (;;) {
        int r;

        switch (lex->state) {
        case ST_INITIAL:              r = lex_scan_initial(lex, out);     break;
        case ST_IN_SCRIPTING:         r = lex_scan_scripting(lex, out);   break;
        case ST_DOUBLE_QUOTES:
        case ST_BACKQUOTE:
        case ST_HEREDOC:              r = lex_scan_string_body(lex, out); break;
        case ST_NOWDOC:               r = lex_scan_nowdoc(lex, out);      break;
        case ST_END_HEREDOC:          r = lex_scan_end_heredoc(lex, out); break;
        case ST_LOOKING_FOR_PROPERTY: r = lex_scan_property(lex, out);    break;
        case ST_LOOKING_FOR_VARNAME:  r = lex_scan_varname(lex, out);     break;
        case ST_VAR_OFFSET:           r = lex_scan_var_offset(lex, out);  break;
        default:
            r = lex_fail(lex, lex->p, "Internal scanner state error");
            break;
        }
        if (r == SCAN_RESTART) continue;
        return (r == SCAN_OK) ? 0 : -1;
    }
}

const char *php_lexer_error(php_lexer *lex)
{
    return lex ? lex->error : "";
}

uint32_t php_lexer_lineno(php_lexer *lex)
{
    return lex_line(lex, lex->p);
}

size_t php_lexer_offset(php_lexer *lex)
{
    return lex->p;
}

void php_lexer_halt(php_lexer *lex)
{
    lex->halted = 1;
}

zend_string *php_lexer_doc_comment(php_lexer *lex)
{
    return lex->doc_comment;
}

void php_lexer_clear_doc_comment(php_lexer *lex)
{
    lex->doc_comment = NULL;
}

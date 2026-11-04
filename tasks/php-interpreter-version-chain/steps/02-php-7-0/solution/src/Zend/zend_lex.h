/* zend_lex.h -- the PHP token scanner.
 *
 * This is the hand written equivalent of php-src's Zend/zend_language_scanner.l.
 * The token set is the *union* of PHP 5.6 ... 8.4 so that the parser never has to
 * change its token set: tokens that only exist in newer versions (T_FN, T_MATCH,
 * T_ENUM, T_READONLY, T_ATTRIBUTE, T_NULLSAFE_OBJECT_OPERATOR, T_PROPERTY_C,
 * T_PUBLIC_SET ...) are always produced, and it is the compiler's job to reject
 * them when the emulated version is too old.
 *
 * Conventions for php_token
 * -------------------------
 *   type    the token id below
 *   text    for tokens that carry text, otherwise NULL.  See the per-token notes:
 *             T_VARIABLE                  variable name *without* the leading '$'
 *             T_STRING, keywords          the source spelling, case preserved
 *             T_NAME_QUALIFIED            "Foo\Bar"
 *             T_NAME_FULLY_QUALIFIED      "Foo\Bar"   (leading '\' stripped)
 *             T_NAME_RELATIVE             "Foo\Bar"   ("namespace\" stripped)
 *             T_STRING_VARNAME            name inside "${name}"
 *             T_LNUMBER / T_DNUMBER       the raw literal, value in lval / dval
 *             T_CONSTANT_ENCAPSED_STRING  the *decoded* string value
 *             T_ENCAPSED_AND_WHITESPACE   the *decoded* literal chunk of an
 *                                         interpolated string
 *             T_INLINE_HTML               the raw HTML run
 *             T_START_HEREDOC/T_END_HEREDOC  the heredoc label
 *           Operators, punctuation, casts and tags carry text == NULL.
 *   lval    valid for T_LNUMBER
 *   dval    valid for T_DNUMBER
 *   lineno  1-based line on which the token *starts*
 *
 * Interpolated strings
 * --------------------
 * A double quoted string that contains no interpolation at all is returned as a
 * single T_CONSTANT_ENCAPSED_STRING with escapes already processed.  Otherwise the
 * lexer emits a token stream the parser can assemble directly:
 *
 *   "a $x b {$y->z} c ${n} d"
 *     T_DQ_START
 *     T_ENCAPSED_AND_WHITESPACE("a ")
 *     T_VARIABLE("x")
 *     T_ENCAPSED_AND_WHITESPACE(" b ")
 *     T_CURLY_OPEN  T_VARIABLE("y") T_OBJECT_OPERATOR T_STRING("z") T_RBRACE
 *     T_ENCAPSED_AND_WHITESPACE(" c ")
 *     T_DOLLAR_OPEN_CURLY_BRACES T_STRING_VARNAME("n") T_RBRACE
 *     T_ENCAPSED_AND_WHITESPACE(" d")
 *     T_DQ_END
 *
 * Simple offset / property syntax inside a string:
 *     "$a[key]"   -> T_VARIABLE T_LBRACKET T_STRING   T_RBRACKET
 *     "$a[3]"     -> T_VARIABLE T_LBRACKET T_LNUMBER  T_RBRACKET
 *     "$a[-3]"    -> T_VARIABLE T_LBRACKET T_LNUMBER  T_RBRACKET   (lval == -3)
 *     "$a[$k]"    -> T_VARIABLE T_LBRACKET T_VARIABLE T_RBRACKET
 *     "$a->b"     -> T_VARIABLE T_OBJECT_OPERATOR T_STRING
 *
 * Heredoc/nowdoc use T_START_HEREDOC ... T_END_HEREDOC around the same part
 * stream (a nowdoc body is always a single T_ENCAPSED_AND_WHITESPACE; an empty
 * body produces no part at all).  Shell exec strings use T_BACKQUOTE as both the
 * opening and the closing token.
 */
#ifndef ZEND_LEX_H
#define ZEND_LEX_H

#include "zend.h"

typedef enum {
    T_EOF = 0,

    /* ------------------------------------------- inline HTML and PHP tags */
    T_INLINE_HTML,
    T_OPEN_TAG,                 /* <?php                                    */
    T_OPEN_TAG_WITH_ECHO,       /* <?=   (behaves like "echo")              */
    T_CLOSE_TAG,                /* ?>    (also terminates a statement)      */

    /* ------------------------------------------------ names and literals */
    T_VARIABLE,
    T_STRING,
    T_NAME_QUALIFIED,           /* Foo\Bar            (8.0 lexing)          */
    T_NAME_FULLY_QUALIFIED,     /* \Foo  /  \Foo\Bar                        */
    T_NAME_RELATIVE,            /* namespace\Foo\Bar                        */
    T_NS_SEPARATOR,             /* a lone '\'                               */
    T_LNUMBER,
    T_DNUMBER,
    T_CONSTANT_ENCAPSED_STRING,
    T_ENCAPSED_AND_WHITESPACE,
    T_STRING_VARNAME,           /* the name in "${name}"                    */
    T_NUM_STRING,               /* (reserved; offsets come back as T_LNUMBER) */

    /* -------------------------------------------- interpolation framing */
    T_DQ_START,                 /* opening " of an interpolated string      */
    T_DQ_END,                   /* closing " of an interpolated string      */
    T_START_HEREDOC,            /* <<<LABEL / <<<"LABEL" / <<<'LABEL'       */
    T_END_HEREDOC,
    T_CURLY_OPEN,               /* "{$"  (the '$' is re-scanned as code)    */
    T_DOLLAR_OPEN_CURLY_BRACES, /* "${"                                     */

    /* -------------------------------------------------------- keywords */
    T_ABSTRACT,
    T_ARRAY,
    T_AS,
    T_BREAK,
    T_CALLABLE,
    T_CASE,
    T_CATCH,
    T_CLASS,
    T_CLONE,
    T_CONST,
    T_CONTINUE,
    T_DECLARE,
    T_DEFAULT,
    T_DO,
    T_ECHO,
    T_ELSE,
    T_ELSEIF,
    T_EMPTY,
    T_ENDDECLARE,
    T_ENDFOR,
    T_ENDFOREACH,
    T_ENDIF,
    T_ENDSWITCH,
    T_ENDWHILE,
    T_ENUM,                     /* 8.1, only before an identifier          */
    T_EVAL,
    T_EXIT,                     /* exit / die                              */
    T_EXTENDS,
    T_FINAL,
    T_FINALLY,
    T_FN,                       /* 7.4                                     */
    T_FOR,
    T_FOREACH,
    T_FUNCTION,
    T_GLOBAL,
    T_GOTO,
    T_HALT_COMPILER,            /* __halt_compiler                         */
    T_IF,
    T_IMPLEMENTS,
    T_INCLUDE,
    T_INCLUDE_ONCE,
    T_INSTANCEOF,
    T_INSTEADOF,
    T_INTERFACE,
    T_ISSET,
    T_LIST,
    T_LOGICAL_AND,              /* and                                     */
    T_LOGICAL_OR,               /* or                                      */
    T_LOGICAL_XOR,              /* xor                                     */
    T_MATCH,                    /* 8.0                                     */
    T_NAMESPACE,
    T_NEW,
    T_PRINT,
    T_PRIVATE,
    T_PRIVATE_SET,              /* 8.4  private(set)                       */
    T_PROTECTED,
    T_PROTECTED_SET,            /* 8.4  protected(set)                     */
    T_PUBLIC,
    T_PUBLIC_SET,               /* 8.4  public(set)                        */
    T_READONLY,                 /* 8.1                                     */
    T_REQUIRE,
    T_REQUIRE_ONCE,
    T_RETURN,
    T_STATIC,
    T_SWITCH,
    T_THROW,
    T_TRAIT,
    T_TRY,
    T_UNSET,
    T_USE,
    T_VAR,
    T_WHILE,
    T_YIELD,
    T_YIELD_FROM,               /* "yield from", whitespace/comments inside */

    /* ------------------------------------------------- magic constants */
    T_LINE,                     /* __LINE__                                */
    T_FILE,                     /* __FILE__                                */
    T_DIR,                      /* __DIR__                                 */
    T_FUNC_C,                   /* __FUNCTION__                            */
    T_CLASS_C,                  /* __CLASS__                               */
    T_METHOD_C,                 /* __METHOD__                              */
    T_NS_C,                     /* __NAMESPACE__                           */
    T_TRAIT_C,                  /* __TRAIT__                               */
    T_PROPERTY_C,               /* __PROPERTY__  (8.4)                     */

    /* ------------------------------------------------------------ casts */
    T_INT_CAST,                 /* (int) (integer)                         */
    T_DOUBLE_CAST,              /* (float) (double) (real)                 */
    T_STRING_CAST,              /* (string) (binary)                       */
    T_ARRAY_CAST,               /* (array)                                 */
    T_OBJECT_CAST,              /* (object)                                */
    T_BOOL_CAST,                /* (bool) (boolean)                        */
    T_UNSET_CAST,               /* (unset)                                 */

    /* -------------------------------------------------------- operators */
    T_IS_IDENTICAL,             /* ===                                     */
    T_IS_NOT_IDENTICAL,         /* !==                                     */
    T_IS_EQUAL,                 /* ==                                      */
    T_IS_NOT_EQUAL,             /* !=  <>                                  */
    T_SPACESHIP,                /* <=>                                     */
    T_IS_SMALLER_OR_EQUAL,      /* <=                                      */
    T_IS_GREATER_OR_EQUAL,      /* >=                                      */
    T_INC,                      /* ++                                      */
    T_DEC,                      /* --                                      */
    T_POW,                      /* **                                      */
    T_PLUS_EQUAL,               /* +=                                      */
    T_MINUS_EQUAL,              /* -=                                      */
    T_MUL_EQUAL,                /* *=                                      */
    T_DIV_EQUAL,                /* /=                                      */
    T_CONCAT_EQUAL,             /* .=                                      */
    T_MOD_EQUAL,                /* %=                                      */
    T_AND_EQUAL,                /* &=                                      */
    T_OR_EQUAL,                 /* |=                                      */
    T_XOR_EQUAL,                /* ^=                                      */
    T_SL_EQUAL,                 /* <<=                                     */
    T_SR_EQUAL,                 /* >>=                                     */
    T_POW_EQUAL,                /* **=                                     */
    T_COALESCE_EQUAL,           /* ??=                                     */
    T_COALESCE,                 /* ??                                      */
    T_NULLSAFE_OBJECT_OPERATOR, /* ?->                                     */
    T_OBJECT_OPERATOR,          /* ->                                      */
    T_DOUBLE_ARROW,             /* =>                                      */
    T_PAAMAYIM_NEKUDOTAYIM,     /* ::                                      */
    T_ELLIPSIS,                 /* ...                                     */
    T_BOOLEAN_AND,              /* &&                                      */
    T_BOOLEAN_OR,               /* ||                                      */
    T_SL,                       /* <<                                      */
    T_SR,                       /* >>                                      */
    T_ATTRIBUTE,                /* #[   (8.0)                              */

    /* ---------------------------------------------- one character tokens */
    T_SEMICOLON,                /* ;                                       */
    T_COMMA,                    /* ,                                       */
    T_COLON,                    /* :                                       */
    T_QUESTION,                 /* ?                                       */
    T_LPAREN,                   /* (                                       */
    T_RPAREN,                   /* )                                       */
    T_LBRACKET,                 /* [                                       */
    T_RBRACKET,                 /* ]                                       */
    T_LBRACE,                   /* {                                       */
    T_RBRACE,                   /* }                                       */
    T_PLUS,                     /* +                                       */
    T_MINUS,                    /* -                                       */
    T_MUL,                      /* *                                       */
    T_DIV,                      /* /                                       */
    T_MOD,                      /* %                                       */
    T_DOT,                      /* .                                       */
    T_ASSIGN,                   /* =                                       */
    T_BANG,                     /* !                                       */
    T_AMP,                      /* &                                       */
    T_PIPE,                     /* |                                       */
    T_CARET,                    /* ^                                       */
    T_TILDE,                    /* ~                                       */
    T_LT,                       /* <                                       */
    T_GT,                       /* >                                       */
    T_AT,                       /* @                                       */
    T_DOLLAR,                   /* $  (variable variables: $$x, ${expr})   */
    T_BACKQUOTE,                /* `  (opens and closes a shell exec)      */

    /* Present so that the enum is a superset of PHP's token list; the lexer
     * never returns these: whitespace and comments are skipped, and a bad
     * character makes php_lexer_next() fail. */
    T_COMMENT,
    T_DOC_COMMENT,
    T_WHITESPACE,
    T_BAD_CHARACTER,

    T_LAST_TOKEN
} php_token_type;

/* php-src spells "::" T_PAAMAYIM_NEKUDOTAYIM; this alias is easier to read. */
#define T_DOUBLE_COLON T_PAAMAYIM_NEKUDOTAYIM

typedef struct {
    php_token_type type;
    zend_string   *text;        /* see the table at the top, may be NULL   */
    zend_long      lval;        /* T_LNUMBER                               */
    double         dval;        /* T_DNUMBER                               */
    uint32_t       lineno;      /* line the token starts on, 1 based       */
} php_token;

typedef struct php_lexer php_lexer;

/* `src` must stay alive for the lifetime of the lexer. */
php_lexer  *php_lexer_new(const char *src, size_t len, zend_string *filename);

/* Same, but `in_scripting` starts the scanner inside PHP code instead of in
 * inline HTML.  eval()'d code needs this: php-src's zend_compile_string() sets
 * ST_IN_SCRIPTING directly rather than requiring a "<?php" tag. */
php_lexer  *php_lexer_new_ex(const char *src, size_t len, zend_string *filename,
                             int in_scripting);
void        php_lexer_free(php_lexer *lex);

/* 0 = a token was produced (T_EOF at the end of input), -1 = scanner error. */
int         php_lexer_next(php_lexer *lex, php_token *out);

/* Description of the last error, "" when there was none. */
const char *php_lexer_error(php_lexer *lex);

/* Line the scanner is currently sitting on (1 based). */
uint32_t    php_lexer_lineno(php_lexer *lex);

/* --------------------------------------------------------------- extras */

/* Printable name of a token, e.g. "T_ECHO" -- for parse error messages. */
const char *php_token_name(php_token_type type);

/* Most recent doc comment block (a "slash star star" comment) seen in the token
 * stream, or NULL.  Like php-src's CG(doc_comment) it is cleared by '}'; the
 * parser clears it itself with php_lexer_clear_doc_comment() once it has been
 * attached to a declaration.  The text includes the comment delimiters. */
zend_string *php_lexer_doc_comment(php_lexer *lex);
void         php_lexer_clear_doc_comment(php_lexer *lex);

/* Byte offset of the next character to be scanned; __halt_compiler() uses it
 * for __COMPILER_HALT_OFFSET__. */
size_t       php_lexer_offset(php_lexer *lex);

/* Stop scanning: every further php_lexer_next() returns T_EOF.  Used by the
 * parser once it has consumed "__halt_compiler ( ) ;". */
void         php_lexer_halt(php_lexer *lex);

#endif /* ZEND_LEX_H */

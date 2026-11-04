/*
 * ext/json/php_json.c -- the JSON extension.
 *
 * Port of php-src's ext/json: json_encoder.c (php_json_encode_zval /
 * php_json_escape_string / php_json_encode_array), json_scanner.re +
 * json_parser.y (rewritten as a recursive descent parser that accepts
 * exactly the same language and raises the same error codes), and json.c
 * (the userland functions and the error message table).
 *
 * Exports
 *   php_json_encode(), php_json_decode()      -- declared in zend_API.h
 *   php_functions_json[]                      -- the builtin table
 *   php_json_register_constants()             -- the JSON_* constants
 *
 * Everything below was checked against
 * oracle/php{5.6,7.0,7.1,7.2,7.4,8.0,8.3,8.4}.
 */

#include "zend.h"
#include "zend_API.h"
#include "zend_ast.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Provided by the engine's constant-registration layer.  It does not exist
 * yet; declaring it here keeps this module self contained until it does.
 */
extern void zend_register_long_constant(const char *name, zend_long val);

/*
 * Zend/zend_exceptions.h -- zend_throw_error() plus an exception code.  It is
 * declared here rather than included so this module keeps to zend.h,
 * zend_API.h and zend_ast.h.
 */
extern zend_object *zend_throw_error_code(const char *class_name,
                                          zend_long code, const char *fmt, ...);

/* main/php_number.c -- php-src's php_gcvt(), writing into a caller buffer. */
extern size_t php_gcvt_into(double value, int ndigit, char dec_point,
                            char exponent, char *buf);

/* ------------------------------------------------------------------ */
/* constants (php-src: ext/json/php_json.h)                             */
/* ------------------------------------------------------------------ */
#define PHP_JSON_ERROR_NONE                  0
#define PHP_JSON_ERROR_DEPTH                 1
#define PHP_JSON_ERROR_STATE_MISMATCH        2
#define PHP_JSON_ERROR_CTRL_CHAR             3
#define PHP_JSON_ERROR_SYNTAX                4
#define PHP_JSON_ERROR_UTF8                  5
#define PHP_JSON_ERROR_RECURSION             6
#define PHP_JSON_ERROR_INF_OR_NAN            7
#define PHP_JSON_ERROR_UNSUPPORTED_TYPE      8
#define PHP_JSON_ERROR_INVALID_PROPERTY_NAME 9
#define PHP_JSON_ERROR_UTF16                 10
#define PHP_JSON_ERROR_NON_BACKED_ENUM       11

/* json_decode() options */
#define PHP_JSON_OBJECT_AS_ARRAY             (1 << 0)
#define PHP_JSON_BIGINT_AS_STRING            (1 << 1)

/* json_encode() options */
#define PHP_JSON_HEX_TAG                     (1 << 0)
#define PHP_JSON_HEX_AMP                     (1 << 1)
#define PHP_JSON_HEX_APOS                    (1 << 2)
#define PHP_JSON_HEX_QUOT                    (1 << 3)
#define PHP_JSON_FORCE_OBJECT                (1 << 4)
#define PHP_JSON_NUMERIC_CHECK               (1 << 5)
#define PHP_JSON_UNESCAPED_SLASHES           (1 << 6)
#define PHP_JSON_PRETTY_PRINT                (1 << 7)
#define PHP_JSON_UNESCAPED_UNICODE           (1 << 8)
#define PHP_JSON_PARTIAL_OUTPUT_ON_ERROR     (1 << 9)
#define PHP_JSON_PRESERVE_ZERO_FRACTION      (1 << 10)
#define PHP_JSON_UNESCAPED_LINE_TERMINATORS  (1 << 11)

/* shared between encode and decode */
#define PHP_JSON_INVALID_UTF8_IGNORE         (1 << 20)
#define PHP_JSON_INVALID_UTF8_SUBSTITUTE     (1 << 21)
#define PHP_JSON_THROW_ON_ERROR              (1 << 22)

#define PHP_JSON_PARSER_DEFAULT_DEPTH        512

#define PHP_JSON_OUTPUT_ARRAY  0
#define PHP_JSON_OUTPUT_OBJECT 1

/* Zend/zend_long.h */
#define PHP_JSON_INT_MAX_LENGTH 19
static const char php_json_long_min_digits[] = "9223372036854775808";

/*
 * The one piece of module state php-src also keeps (JSON_G(error_code)):
 * json_last_error()/json_last_error_msg() report the code left behind by the
 * most recent json_encode()/json_decode() call.
 */
static int php_json_last_error_code = PHP_JSON_ERROR_NONE;

static long php_json_version(void)
{
    return EG(php_version) ? EG(php_version) : 80400;
}

static const char *php_json_get_error_msg(int code)
{
    switch (code) {
        case PHP_JSON_ERROR_NONE:            return "No error";
        case PHP_JSON_ERROR_DEPTH:           return "Maximum stack depth exceeded";
        case PHP_JSON_ERROR_STATE_MISMATCH:  return "State mismatch (invalid or malformed JSON)";
        case PHP_JSON_ERROR_CTRL_CHAR:       return "Control character error, possibly incorrectly encoded";
        case PHP_JSON_ERROR_SYNTAX:          return "Syntax error";
        case PHP_JSON_ERROR_UTF8:            return "Malformed UTF-8 characters, possibly incorrectly encoded";
        case PHP_JSON_ERROR_RECURSION:       return "Recursion detected";
        case PHP_JSON_ERROR_INF_OR_NAN:      return "Inf and NaN cannot be JSON encoded";
        case PHP_JSON_ERROR_UNSUPPORTED_TYPE: return "Type is not supported";
        case PHP_JSON_ERROR_INVALID_PROPERTY_NAME: return "The decoded property name is invalid";
        case PHP_JSON_ERROR_UTF16:           return "Single unpaired UTF-16 surrogate in unicode escape";
        case PHP_JSON_ERROR_NON_BACKED_ENUM: return "Non-backed enums have no default serialization";
        default:                             return "Unknown error";
    }
}

/*
 * VERSION: JsonException and JSON_THROW_ON_ERROR arrived in PHP 7.3.  On
 * older releases the flag is simply an unknown bit and the error is reported
 * through json_last_error() as usual.  Oracle:
 *   php7.4 -r 'try{json_decode("{",false,512,JSON_THROW_ON_ERROR);}
 *              catch(Throwable $e){echo get_class($e),":",$e->getMessage(),
 *              ":",$e->getCode();}'
 *       => JsonException:Syntax error:4
 */
/* VERSIONGATE:70300 else-branch is the 5.6 behaviour */
static int php_json_should_throw(zend_long options)
{
    if (php_json_version() >= 70300) {
        return (options & PHP_JSON_THROW_ON_ERROR) != 0;
    }
    /* PHP 5.6/7.0-7.2: the bit is unknown, errors only reach
     * json_last_error(). */
    return 0;
}

static void php_json_report(int code, zend_long options)
{
    if (php_json_should_throw(options)) {
        /*
         * php-src passes the JSON_ERROR_* status as the exception code, so
         * JsonException::getCode() reports it.  Oracle (php7.4):
         *   try { json_decode("{", false, 512, JSON_THROW_ON_ERROR); }
         *   catch (Throwable $e) { echo $e->getCode(); }   => 4
         */
        zend_throw_error_code("JsonException", (zend_long) code, "%s",
                              php_json_get_error_msg(code));
    } else {
        php_json_last_error_code = code;
    }
}

/* ------------------------------------------------------------------ */
/* ini access                                                           */
/* ------------------------------------------------------------------ */
static int php_json_ini_int(const char *name, size_t namelen, int dflt)
{
    zval *v;

    if (!EG(ini)) {
        return dflt;
    }
    v = zend_hash_str_find(EG(ini), name, namelen);
    if (!v) {
        return dflt;
    }
    if (Z_TYPE_P(v) == IS_STRING && Z_STR_P(v)) {
        return (int) strtol(ZSTR_VAL(Z_STR_P(v)), NULL, 10);
    }
    if (Z_TYPE_P(v) == IS_LONG) {
        return (int) Z_LVAL_P(v);
    }
    return dflt;
}

/*
 * VERSION: json_encode() rendered doubles with `precision` up to PHP 7.0 and
 * with `serialize_precision` from 7.1 on.  Oracle, -d precision=14 (default
 * serialize_precision: 17 on 5.6/7.0, -1 from 7.1):
 *   php7.0 -r 'echo json_encode(1/3);' => 0.33333333333333
 *   php7.1 -r 'echo json_encode(1/3);' => 0.3333333333333333
 *   php7.0 -r 'echo json_encode(1e14);' => 1.0e+14
 *   php7.1 -r 'echo json_encode(1e14);' => 100000000000000
 */
/* VERSIONGATE:70100 else-branch is the 5.6 behaviour */
static int php_json_double_precision(void)
{
    if (php_json_version() >= 70100) {
        /* serialize_precision; -1 selects smart_double_repr()'s digits */
        return php_json_ini_int("serialize_precision",
                                sizeof("serialize_precision") - 1, -1);
    }
    /* PHP 5.6/7.0: json_encode() rendered doubles with `precision`. */
    return php_json_ini_int("precision", sizeof("precision") - 1, 14);
}

/* ------------------------------------------------------------------ */
/* growable byte buffer                                                 */
/* ------------------------------------------------------------------ */
typedef struct {
    char   *s;
    size_t  len;
    size_t  cap;
    int     oom;
} php_json_buf;

static void jb_reserve(php_json_buf *b, size_t extra)
{
    size_t need;
    char *n;

    if (b->oom) {
        return;
    }
    need = b->len + extra + 1;
    if (need <= b->cap) {
        return;
    }
    while (b->cap < need) {
        b->cap = b->cap ? b->cap << 1 : 256;
    }
    n = (char *) realloc(b->s, b->cap);
    if (!n) {
        b->oom = 1;
        return;
    }
    b->s = n;
}

static void jb_char(php_json_buf *b, char c)
{
    jb_reserve(b, 1);
    if (!b->oom) {
        b->s[b->len++] = c;
    }
}

static void jb_mem(php_json_buf *b, const char *s, size_t n)
{
    if (!n) {
        return;
    }
    jb_reserve(b, n);
    if (!b->oom) {
        memcpy(b->s + b->len, s, n);
        b->len += n;
    }
}

static void jb_long(php_json_buf *b, zend_long l)
{
    zend_string *z = php_long_to_string(l);
    jb_mem(b, ZSTR_VAL(z), ZSTR_LEN(z));
}

static void jb_free(php_json_buf *b)
{
    free(b->s);
    b->s = NULL;
    b->len = b->cap = 0;
}

/* ------------------------------------------------------------------ */
/* UTF-8                                                                */
/* ------------------------------------------------------------------ */
/*
 * Strict decoder matching php_next_utf8_char(): rejects overlong forms,
 * UTF-16 surrogates and anything above U+10FFFF.  Returns the code point and
 * stores the byte length in *used, or -1 (with *used == 1) on a bad byte.
 */
static int php_json_utf8_decode(const unsigned char *s, size_t len, size_t *used)
{
    unsigned int c = s[0];

    *used = 1;
    if (c < 0x80) {
        return (int) c;
    }
    if (c < 0xC2) {
        return -1;                              /* continuation or overlong */
    }
    if (c < 0xE0) {
        if (len < 2 || (s[1] & 0xC0) != 0x80) {
            return -1;
        }
        *used = 2;
        return (int) (((c & 0x1F) << 6) | (s[1] & 0x3F));
    }
    if (c < 0xF0) {
        int cp;
        if (len < 3 || (s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) {
            return -1;
        }
        cp = (int) (((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F));
        if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) {
            return -1;
        }
        *used = 3;
        return cp;
    }
    if (c < 0xF5) {
        int cp;
        if (len < 4 || (s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 ||
                (s[3] & 0xC0) != 0x80) {
            return -1;
        }
        cp = (int) (((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) |
                    ((s[2] & 0x3F) << 6) | (s[3] & 0x3F));
        if (cp < 0x10000 || cp > 0x10FFFF) {
            return -1;
        }
        *used = 4;
        return cp;
    }
    return -1;
}

/*
 * php_next_utf8_char()'s stepper, which the *encoder* uses.  On a malformed
 * sequence it reports how many bytes belong to that one bad character, per
 * UTR #36 strategy 2: "do not include any non-initial byte that encodes a
 * valid character or is a leading byte for a valid sequence".  That is why
 * encoding "a\xf0\x80\x80A" with JSON_INVALID_UTF8_SUBSTITUTE yields a
 * single replacement character and not three.
 *
 * The *decoder* deliberately does not use this: php-src's re2c scanner
 * matches a single byte with its catch-all rule, so json_decode() of the
 * same input substitutes three replacement characters.  Oracle (php8.4):
 *   json_encode("a\xf0\x80\x80A", JSON_INVALID_UTF8_SUBSTITUTE)
 *       => "a\ufffdA"
 *   bin2hex(json_decode("\"a\xf0\x80\x80A\"", true, 512,
 *                       JSON_INVALID_UTF8_SUBSTITUTE))
 *       => 61efbfbdefbfbdefbfbd41
 */
#define PHP_JSON_UTF8_TRAIL(c) ((c) >= 0x80 && (c) <= 0xBF)
#define PHP_JSON_UTF8_LEAD(c)  ((c) < 0x80 || ((c) >= 0xC2 && (c) <= 0xF4))

static int php_json_utf8_next(const unsigned char *s, size_t len, size_t *used)
{
    unsigned int c = s[0];

    if (c < 0x80) {
        *used = 1;
        return (int) c;
    }
    if (c < 0xC2) {
        *used = 1;
        return -1;
    }
    if (c < 0xE0) {
        if (len < 2) {
            *used = 1;
            return -1;
        }
        if (!PHP_JSON_UTF8_TRAIL(s[1])) {
            *used = PHP_JSON_UTF8_LEAD(s[1]) ? 1 : 2;
            return -1;
        }
        *used = 2;
        return (int) (((c & 0x1F) << 6) | (s[1] & 0x3F));
    }
    if (c < 0xF0) {
        int cp;
        if (len < 3 || !PHP_JSON_UTF8_TRAIL(s[1]) || !PHP_JSON_UTF8_TRAIL(s[2])) {
            if (len < 2 || PHP_JSON_UTF8_LEAD(s[1]))      { *used = 1; }
            else if (len < 3 || PHP_JSON_UTF8_LEAD(s[2])) { *used = 2; }
            else                                          { *used = 3; }
            return -1;
        }
        cp = (int) (((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F));
        *used = 3;
        if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) {
            return -1;
        }
        return cp;
    }
    if (c < 0xF5) {
        int cp;
        if (len < 4 || !PHP_JSON_UTF8_TRAIL(s[1]) ||
                !PHP_JSON_UTF8_TRAIL(s[2]) || !PHP_JSON_UTF8_TRAIL(s[3])) {
            if (len < 2 || PHP_JSON_UTF8_LEAD(s[1]))      { *used = 1; }
            else if (len < 3 || PHP_JSON_UTF8_LEAD(s[2])) { *used = 2; }
            else if (len < 4 || PHP_JSON_UTF8_LEAD(s[3])) { *used = 3; }
            else                                          { *used = 4; }
            return -1;
        }
        cp = (int) (((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) |
                    ((s[2] & 0x3F) << 6) | (s[3] & 0x3F));
        *used = 4;
        if (cp < 0x10000 || cp > 0x10FFFF) {
            return -1;
        }
        return cp;
    }
    *used = 1;
    return -1;
}

static void php_json_utf8_encode(php_json_buf *b, unsigned int cp)
{
    if (cp < 0x80) {
        jb_char(b, (char) cp);
    } else if (cp < 0x800) {
        jb_char(b, (char) (0xC0 | (cp >> 6)));
        jb_char(b, (char) (0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        jb_char(b, (char) (0xE0 | (cp >> 12)));
        jb_char(b, (char) (0x80 | ((cp >> 6) & 0x3F)));
        jb_char(b, (char) (0x80 | (cp & 0x3F)));
    } else {
        jb_char(b, (char) (0xF0 | (cp >> 18)));
        jb_char(b, (char) (0x80 | ((cp >> 12) & 0x3F)));
        jb_char(b, (char) (0x80 | ((cp >> 6) & 0x3F)));
        jb_char(b, (char) (0x80 | (cp & 0x3F)));
    }
}

/* ------------------------------------------------------------------ */
/* encoder                                                              */
/* ------------------------------------------------------------------ */
typedef struct {
    int depth;
    int max_depth;
    int error_code;
} php_json_encoder;

static int php_json_encode_zval(php_json_buf *buf, zval *val, zend_long options,
                                php_json_encoder *enc);
static int php_json_encode_array(php_json_buf *buf, zval *val, zend_long options,
                                 php_json_encoder *enc);

/*
 * The hash table php-src guards against recursion for a given value: the
 * array itself, or an object's property table.
 */
static HashTable *php_json_container_ht(zval *v)
{
    v = zval_deref(v);
    if (Z_TYPE_P(v) == IS_ARRAY) {
        return Z_ARR_P(v);
    }
    if (Z_TYPE_P(v) == IS_OBJECT) {
        return Z_OBJ_P(v)->properties;
    }
    return NULL;
}

/*
 * Recursion guarding changed shape in PHP 7.2.  Up to 7.1 php-src bumped
 * HashTable.nApplyCount on the *child* before descending and flagged
 * recursion at `nApplyCount > 1`, so a self-referencing value was printed
 * twice before collapsing to null.  From 7.2 the guard is the single
 * GC_PROTECT_RECURSION flag set on the container itself, which collapses one
 * level earlier.  Oracle:
 *   php7.1 -r '$o=new stdClass;$o->prop=$o;
 *              echo json_encode($o,JSON_PARTIAL_OUTPUT_ON_ERROR);'
 *       => {"prop":{"prop":null}}
 *   php7.2 same => {"prop":null}
 * (and ext/json/tests/003.phpt expects "[[null]]" through 7.1, "[null]" from
 * 7.2 on.)
 */
/* VERSIONGATE:70200 else-branch is the 5.6 behaviour */
static int php_json_single_level_recursion(void)
{
    return php_json_version() >= 70200;
}

static const char php_json_digits[] = "0123456789abcdef";

static void php_json_pretty_char(php_json_buf *b, zend_long options, char c)
{
    if (options & PHP_JSON_PRETTY_PRINT) {
        jb_char(b, c);
    }
}

static void php_json_pretty_indent(php_json_buf *b, zend_long options,
                                   const php_json_encoder *enc)
{
    int i;

    if (options & PHP_JSON_PRETTY_PRINT) {
        for (i = 0; i < enc->depth; ++i) {
            jb_mem(b, "    ", 4);           /* php-src: exactly four spaces */
        }
    }
}

static int php_json_is_valid_double(double d)
{
    return !isinf(d) && !isnan(d);
}

static void php_json_encode_double(php_json_buf *buf, double d, zend_long options)
{
    char num[160];
    size_t len;

    /* php-src uses a lowercase 'e' here; serialize()/var_export() use 'E'. */
    len = php_gcvt_into(d, php_json_double_precision(), '.', 'e', num);

    /*
     * VERSION: JSON_PRESERVE_ZERO_FRACTION landed in PHP 5.6.6.  php-src only
     * appends ".0" when the rendering has no '.' at all, which is why
     * 1.0e+17 is left alone while 100 becomes 100.0.  Oracle:
     *   php7.4 -r 'echo json_encode(100.0, JSON_PRESERVE_ZERO_FRACTION);'
     *       => 100.0
     *   php7.4 -r 'echo json_encode(1e17, JSON_PRESERVE_ZERO_FRACTION);'
     *       => 1.0e+17
     */
    if ((options & PHP_JSON_PRESERVE_ZERO_FRACTION) &&
            memchr(num, '.', len) == NULL && len < sizeof num - 2) {
        num[len++] = '.';
        num[len++] = '0';
    }
    jb_mem(buf, num, len);
}

/*
 * php-src's "packed list" test: a PHP array encodes as a JSON array exactly
 * when its keys are the integers 0..n-1 in that order.
 */
static int php_json_determine_array_type(const HashTable *ht)
{
    zend_ulong idx = 0;

    if (!ht || zend_hash_num_elements((HashTable *) ht) == 0) {
        return PHP_JSON_OUTPUT_ARRAY;
    }

    ZEND_HASH_FOREACH((HashTable *) ht, b) {
        if (b->key) {
            return PHP_JSON_OUTPUT_OBJECT;
        }
        if (b->h != idx) {
            return PHP_JSON_OUTPUT_OBJECT;
        }
        idx++;
    } ZEND_HASH_FOREACH_END();

    return PHP_JSON_OUTPUT_ARRAY;
}

static int php_json_escape_string(php_json_buf *buf, const char *s, size_t len,
                                  zend_long options, php_json_encoder *enc)
{
    /*
     * Bitmap of the bytes that need attention, straight out of php-src:
     * every control character, " & ' / < > \ and every byte >= 0x80.
     */
    static const uint32_t charmap[8] = {
        0xffffffffu, 0x500080c4u, 0x10000000u, 0x00000000u,
        0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu
    };
    size_t checkpoint;
    size_t pos = 0;

    if (len == 0) {
        jb_mem(buf, "\"\"", 2);
        return 0;
    }

    if (options & PHP_JSON_NUMERIC_CHECK) {
        double d;
        zend_long p;
        int type = is_numeric_string(s, len, &p, &d, 0);

        if (type == IS_LONG) {
            jb_long(buf, p);
            return 0;
        }
        if (type == IS_DOUBLE && php_json_is_valid_double(d)) {
            php_json_encode_double(buf, d, options);
            return 0;
        }
    }

    checkpoint = buf->len;
    jb_char(buf, '"');

    while (len) {
        unsigned int us = (unsigned char) s[pos];

        if (!(charmap[us >> 5] & (1u << (us & 31)))) {
            pos++;
            len--;
            if (len == 0) {
                jb_mem(buf, s, pos);
                break;
            }
            continue;
        }

        if (pos) {
            jb_mem(buf, s, pos);
            s += pos;
            pos = 0;
        }
        us = (unsigned char) s[0];

        if (us >= 0x80) {
            size_t used;
            int cp = php_json_utf8_next((const unsigned char *) s, len, &used);

            int utf8_ignore = 0, utf8_substitute = 0, pass_through;

            /*
             * JSON_INVALID_UTF8_IGNORE / _SUBSTITUTE were added in PHP 7.2;
             * on older releases the bits are unknown and a bad byte is always
             * an error.  Oracle:
             *   php7.4 -r 'echo json_encode("\xC3(", JSON_INVALID_UTF8_IGNORE);'
             *       => "("
             *   php7.1 -r 'echo JSON_INVALID_UTF8_IGNORE;'
             *       => Notice: Use of undefined constant
             */


            /*
             * JSON_UNESCAPED_LINE_TERMINATORS is 7.1+; before it,
             * JSON_UNESCAPED_UNICODE always passed U+2028 / U+2029 through
             * unescaped.  Oracle, json_encode of a lone U+2028 with
             * JSON_UNESCAPED_UNICODE:
             *   php7.0 => the raw three bytes
             *   php7.1 => the six character escape for U+2028
             */
            /* VERSIONGATE:70100 else-branch is the 5.6 behaviour */
            if (php_json_version() >= 70100) {
                pass_through = (options & PHP_JSON_UNESCAPED_UNICODE) &&
                               ((options & PHP_JSON_UNESCAPED_LINE_TERMINATORS) ||
                                cp < 0x2028 || cp > 0x2029);
            } else {
                pass_through = (options & PHP_JSON_UNESCAPED_UNICODE) != 0;
            }

            if (cp < 0) {
                if (utf8_ignore) {
                    /* drop the byte */
                } else if (utf8_substitute) {
                    if (options & PHP_JSON_UNESCAPED_UNICODE) {
                        jb_mem(buf, "\xef\xbf\xbd", 3);
                    } else {
                        jb_mem(buf, "\\ufffd", 6);
                    }
                } else {
                    buf->len = checkpoint;
                    enc->error_code = PHP_JSON_ERROR_UTF8;
                    if (options & PHP_JSON_PARTIAL_OUTPUT_ON_ERROR) {
                        jb_mem(buf, "null", 4);
                    }
                    return -1;
                }
            } else if (pass_through) {
                jb_mem(buf, s, used);
            } else {
                unsigned int u = (unsigned int) cp;
                char esc[6];

                if (u >= 0x10000) {
                    unsigned int next;
                    u -= 0x10000;
                    next = (unsigned short) ((u & 0x3ff) | 0xdc00);
                    u = (unsigned short) ((u >> 10) | 0xd800);
                    esc[0] = '\\'; esc[1] = 'u';
                    esc[2] = php_json_digits[(u >> 12) & 0xf];
                    esc[3] = php_json_digits[(u >> 8) & 0xf];
                    esc[4] = php_json_digits[(u >> 4) & 0xf];
                    esc[5] = php_json_digits[u & 0xf];
                    jb_mem(buf, esc, 6);
                    u = next;
                }
                esc[0] = '\\'; esc[1] = 'u';
                esc[2] = php_json_digits[(u >> 12) & 0xf];
                esc[3] = php_json_digits[(u >> 8) & 0xf];
                esc[4] = php_json_digits[(u >> 4) & 0xf];
                esc[5] = php_json_digits[u & 0xf];
                jb_mem(buf, esc, 6);
            }
            s += used;
            len -= used;
            continue;
        }

        s++;
        switch (us) {
            case '"':
                jb_mem(buf, (options & PHP_JSON_HEX_QUOT) ? "\\u0022" : "\\\"",
                       (options & PHP_JSON_HEX_QUOT) ? 6 : 2);
                break;
            case '\\': jb_mem(buf, "\\\\", 2); break;
            case '/':
                if (options & PHP_JSON_UNESCAPED_SLASHES) {
                    jb_char(buf, '/');
                } else {
                    jb_mem(buf, "\\/", 2);
                }
                break;
            case '\b': jb_mem(buf, "\\b", 2); break;
            case '\f': jb_mem(buf, "\\f", 2); break;
            case '\n': jb_mem(buf, "\\n", 2); break;
            case '\r': jb_mem(buf, "\\r", 2); break;
            case '\t': jb_mem(buf, "\\t", 2); break;
            case '<':
                /* note the uppercase hex digits, exactly as php-src spells them */
                if (options & PHP_JSON_HEX_TAG) { jb_mem(buf, "\\u003C", 6); }
                else { jb_char(buf, '<'); }
                break;
            case '>':
                if (options & PHP_JSON_HEX_TAG) { jb_mem(buf, "\\u003E", 6); }
                else { jb_char(buf, '>'); }
                break;
            case '&':
                if (options & PHP_JSON_HEX_AMP) { jb_mem(buf, "\\u0026", 6); }
                else { jb_char(buf, '&'); }
                break;
            case '\'':
                if (options & PHP_JSON_HEX_APOS) { jb_mem(buf, "\\u0027", 6); }
                else { jb_char(buf, '\''); }
                break;
            default: {
                char esc[6];
                esc[0] = '\\'; esc[1] = 'u'; esc[2] = '0'; esc[3] = '0';
                esc[4] = php_json_digits[(us >> 4) & 0xf];
                esc[5] = php_json_digits[us & 0xf];
                jb_mem(buf, esc, 6);
                break;
            }
        }
        len--;
    }

    jb_char(buf, '"');
    return 0;
}

static int php_json_encode_array(php_json_buf *buf, zval *val, zend_long options,
                                 php_json_encoder *enc)
{
    HashTable *myht;
    int r, need_comma = 0;
    int is_object = (Z_TYPE_P(val) == IS_OBJECT);

    if (is_object) {
        myht = Z_OBJ_P(val)->properties;
        r = PHP_JSON_OUTPUT_OBJECT;
    } else {
        myht = Z_ARR_P(val);
        r = (options & PHP_JSON_FORCE_OBJECT)
            ? PHP_JSON_OUTPUT_OBJECT
            : php_json_determine_array_type(myht);
    }

    if (myht && myht->protect > (php_json_single_level_recursion() ? 0 : 1)) {
        enc->error_code = PHP_JSON_ERROR_RECURSION;
        jb_mem(buf, "null", 4);
        return -1;
    }
    if (myht && php_json_single_level_recursion()) {
        myht->protect = 1;
    }

    jb_char(buf, (r == PHP_JSON_OUTPUT_ARRAY) ? '[' : '{');
    ++enc->depth;

    if (myht && zend_hash_num_elements(myht) > 0) {
        ZEND_HASH_FOREACH(myht, bkt) {
            zval *data = zval_deref(&bkt->val);

            if (r == PHP_JSON_OUTPUT_ARRAY) {
                if (need_comma) { jb_char(buf, ','); } else { need_comma = 1; }
                php_json_pretty_char(buf, options, '\n');
                php_json_pretty_indent(buf, options, enc);
            } else {
                if (bkt->key) {
                    /* skip mangled protected/private members */
                    if (is_object && ZSTR_LEN(bkt->key) > 0 &&
                            ZSTR_VAL(bkt->key)[0] == '\0') {
                        continue;
                    }
                    if (need_comma) { jb_char(buf, ','); } else { need_comma = 1; }
                    php_json_pretty_char(buf, options, '\n');
                    php_json_pretty_indent(buf, options, enc);

                    if (php_json_escape_string(buf, ZSTR_VAL(bkt->key),
                                               ZSTR_LEN(bkt->key),
                                               options & ~PHP_JSON_NUMERIC_CHECK,
                                               enc) < 0 &&
                            (options & PHP_JSON_PARTIAL_OUTPUT_ON_ERROR)) {
                        buf->len -= 4;              /* drop the "null" */
                        jb_mem(buf, "\"\"", 2);
                    }
                } else {
                    if (need_comma) { jb_char(buf, ','); } else { need_comma = 1; }
                    php_json_pretty_char(buf, options, '\n');
                    php_json_pretty_indent(buf, options, enc);
                    jb_char(buf, '"');
                    jb_long(buf, (zend_long) bkt->h);
                    jb_char(buf, '"');
                }
                jb_char(buf, ':');
                php_json_pretty_char(buf, options, ' ');
            }

            {
                HashTable *child = php_json_single_level_recursion()
                                 ? NULL : php_json_container_ht(data);
                int rc;

                if (child) {
                    child->protect++;
                }
                rc = php_json_encode_zval(buf, data, options, enc);
                if (child) {
                    child->protect--;
                }
                if (rc < 0 && !(options & PHP_JSON_PARTIAL_OUTPUT_ON_ERROR)) {
                    if (php_json_single_level_recursion()) {
                        myht->protect = 0;
                    }
                    return -1;
                }
            }
        } ZEND_HASH_FOREACH_END();
    }

    if (myht && php_json_single_level_recursion()) {
        myht->protect = 0;
    }

    if (enc->depth > enc->max_depth) {
        enc->error_code = PHP_JSON_ERROR_DEPTH;
        if (!(options & PHP_JSON_PARTIAL_OUTPUT_ON_ERROR)) {
            return -1;
        }
    }
    --enc->depth;

    /* php-src keeps the closing bracket on the same line only when empty */
    if (need_comma) {
        php_json_pretty_char(buf, options, '\n');
        php_json_pretty_indent(buf, options, enc);
    }

    jb_char(buf, (r == PHP_JSON_OUTPUT_ARRAY) ? ']' : '}');
    return 0;
}

/*
 * An object implementing JsonSerializable is encoded from whatever its
 * jsonSerialize() method returns.  php-src special-cases `return $this;` by
 * going straight to the array encoder, so the object does not recurse
 * through this function forever.
 */
static int php_json_encode_serializable(php_json_buf *buf, zval *val,
                                        zend_long options,
                                        php_json_encoder *enc)
{
    zend_object *obj = Z_OBJ_P(val);
    HashTable *myht = obj->properties;
    zend_function *fn;
    zval retval;
    int rc;

    if (myht && myht->protect > (php_json_single_level_recursion() ? 0 : 1)) {
        enc->error_code = PHP_JSON_ERROR_RECURSION;
        jb_mem(buf, "null", 4);
        return -1;
    }

    fn = zend_lookup_method(obj->ce, zend_string_initz("jsonserialize"));
    if (!fn) {
        enc->error_code = PHP_JSON_ERROR_UNSUPPORTED_TYPE;
        jb_char(buf, '0');
        return -1;
    }

    ZVAL_NULL(&retval);
    if (zend_call_function_ex(fn, obj, obj->ce, 0, NULL, NULL, &retval, NULL)
            || EG_HAS_EXCEPTION()) {
        jb_char(buf, '0');
        return -1;
    }

    if (Z_TYPE(retval) == IS_OBJECT && Z_OBJ(retval) == obj) {
        /* jsonSerialize() returned $this */
        rc = php_json_encode_array(buf, &retval, options, enc);
    } else {
        rc = php_json_encode_zval(buf, &retval, options, enc);
    }
    return rc;
}

static int php_json_is_serializable(zend_class_entry *ce)
{
    static const char iname[] = "JsonSerializable";
    zend_class_entry *ifce;

    if (!ce) {
        return 0;
    }
    ifce = zend_lookup_class(zend_string_init(iname, sizeof(iname) - 1), 0);
    return ifce && zend_instanceof(ce, ifce);
}

static int php_json_encode_zval(php_json_buf *buf, zval *val, zend_long options,
                                php_json_encoder *enc)
{
    val = zval_deref(val);

    switch (Z_TYPE_P(val)) {
        case IS_UNDEF:
        case IS_NULL:
            jb_mem(buf, "null", 4);
            return 0;

        case IS_TRUE:
            jb_mem(buf, "true", 4);
            return 0;

        case IS_FALSE:
            jb_mem(buf, "false", 5);
            return 0;

        case IS_LONG:
            jb_long(buf, Z_LVAL_P(val));
            return 0;

        case IS_DOUBLE:
            if (php_json_is_valid_double(Z_DVAL_P(val))) {
                php_json_encode_double(buf, Z_DVAL_P(val), options);
                return 0;
            }
            enc->error_code = PHP_JSON_ERROR_INF_OR_NAN;
            jb_char(buf, '0');
            return -1;

        case IS_STRING:
            return php_json_escape_string(buf, ZSTR_VAL(Z_STR_P(val)),
                                          ZSTR_LEN(Z_STR_P(val)), options, enc);

        case IS_OBJECT:
            if (php_json_is_serializable(Z_OBJ_P(val)->ce)) {
                return php_json_encode_serializable(buf, val, options, enc);
            }
            /* fall through -- a plain object encodes its public properties */
        case IS_ARRAY:
            return php_json_encode_array(buf, val, options, enc);

        default:
            enc->error_code = PHP_JSON_ERROR_UNSUPPORTED_TYPE;
            if (options & PHP_JSON_PARTIAL_OUTPUT_ON_ERROR) {
                jb_mem(buf, "null", 4);
            }
            return -1;
    }
}

/*
 * Public encoder.  Returns NULL on failure with *err set to the JSON_ERROR_*
 * code; json_last_error() is updated unless JSON_THROW_ON_ERROR is set, in
 * which case a JsonException is thrown instead.
 */
static zend_string *php_json_encode_ex(zval *v, zend_long options,
                                       zend_long depth, int *err)
{
    php_json_buf buf = { NULL, 0, 0, 0 };
    php_json_encoder enc;
    zend_string *out;
    int rc;

    enc.depth = 0;
    enc.max_depth = (int) depth;
    enc.error_code = PHP_JSON_ERROR_NONE;

    rc = php_json_encode_zval(&buf, v, options, &enc);
    (void) rc;

    if (err) {
        *err = enc.error_code;
    }

    if (enc.error_code != PHP_JSON_ERROR_NONE &&
            !(options & PHP_JSON_PARTIAL_OUTPUT_ON_ERROR)) {
        jb_free(&buf);
        php_json_report(enc.error_code, options);
        return NULL;
    }
    if (buf.oom) {
        jb_free(&buf);
        php_json_report(PHP_JSON_ERROR_STATE_MISMATCH, options);
        return NULL;
    }

    php_json_last_error_code = enc.error_code;
    out = zend_string_init(buf.s ? buf.s : "", buf.len);
    jb_free(&buf);
    return out;
}

zend_string *php_json_encode(zval *v, zend_long options, int *err)
{
    return php_json_encode_ex(v, options, PHP_JSON_PARSER_DEFAULT_DEPTH, err);
}

/* ------------------------------------------------------------------ */
/* decoder                                                              */
/* ------------------------------------------------------------------ */
typedef struct {
    const char *p;
    const char *end;
    zend_long   options;
    int         depth;
    int         max_depth;
    int         error;
} php_json_parser;

static int php_json_parse_value(php_json_parser *ps, zval *out);

static void php_json_skip_ws(php_json_parser *ps)
{
    while (ps->p < ps->end) {
        char c = *ps->p;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ps->p++;
        } else {
            break;
        }
    }
}

static int php_json_hex4(const char *p, unsigned int *out)
{
    unsigned int v = 0;
    int i;

    for (i = 0; i < 4; i++) {
        char c = p[i];
        v <<= 4;
        if (c >= '0' && c <= '9')      { v |= (unsigned) (c - '0'); }
        else if (c >= 'a' && c <= 'f') { v |= (unsigned) (c - 'a' + 10); }
        else if (c >= 'A' && c <= 'F') { v |= (unsigned) (c - 'A' + 10); }
        else                           { return -1; }
    }
    *out = v;
    return 0;
}

/* Parses a JSON string literal; ps->p must sit on the opening quote. */
static int php_json_parse_string(php_json_parser *ps, zend_string **out)
{
    php_json_buf b = { NULL, 0, 0, 0 };

    ps->p++;                                    /* skip the opening quote */

    for (;;) {
        unsigned char c;

        if (ps->p >= ps->end) {
            ps->error = PHP_JSON_ERROR_SYNTAX;
            jb_free(&b);
            return -1;
        }
        c = (unsigned char) *ps->p;

        if (c == '"') {
            ps->p++;
            *out = zend_string_init(b.s ? b.s : "", b.len);
            jb_free(&b);
            return 0;
        }
        if (c < 0x20) {
            ps->error = PHP_JSON_ERROR_CTRL_CHAR;
            jb_free(&b);
            return -1;
        }
        if (c == '\\') {
            if (ps->p + 1 >= ps->end) {
                ps->error = PHP_JSON_ERROR_SYNTAX;
                jb_free(&b);
                return -1;
            }
            switch (ps->p[1]) {
                case '"':  jb_char(&b, '"');  ps->p += 2; continue;
                case '\\': jb_char(&b, '\\'); ps->p += 2; continue;
                case '/':  jb_char(&b, '/');  ps->p += 2; continue;
                case 'b':  jb_char(&b, '\b'); ps->p += 2; continue;
                case 'f':  jb_char(&b, '\f'); ps->p += 2; continue;
                case 'n':  jb_char(&b, '\n'); ps->p += 2; continue;
                case 'r':  jb_char(&b, '\r'); ps->p += 2; continue;
                case 't':  jb_char(&b, '\t'); ps->p += 2; continue;
                case 'u': {
                    unsigned int u, lo;

                    if (ps->p + 6 > ps->end || php_json_hex4(ps->p + 2, &u) < 0) {
                        ps->error = PHP_JSON_ERROR_SYNTAX;
                        jb_free(&b);
                        return -1;
                    }
                    ps->p += 6;
                    if (u >= 0xD800 && u <= 0xDBFF) {
                        if (ps->p + 6 > ps->end || ps->p[0] != '\\' ||
                                ps->p[1] != 'u' ||
                                php_json_hex4(ps->p + 2, &lo) < 0 ||
                                lo < 0xDC00 || lo > 0xDFFF) {
                            ps->error = PHP_JSON_ERROR_UTF16;
                            jb_free(&b);
                            return -1;
                        }
                        ps->p += 6;
                        u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                    } else if (u >= 0xDC00 && u <= 0xDFFF) {
                        ps->error = PHP_JSON_ERROR_UTF16;
                        jb_free(&b);
                        return -1;
                    }
                    php_json_utf8_encode(&b, u);
                    continue;
                }
                default:
                    ps->error = PHP_JSON_ERROR_SYNTAX;
                    jb_free(&b);
                    return -1;
            }
        }
        if (c < 0x80) {
            jb_char(&b, (char) c);
            ps->p++;
            continue;
        }
        {
            size_t used;
            int cp = php_json_utf8_decode((const unsigned char *) ps->p,
                                          (size_t) (ps->end - ps->p), &used);
            if (cp < 0) {

                ps->error = PHP_JSON_ERROR_UTF8;
                jb_free(&b);
                return -1;
            }
            jb_mem(&b, ps->p, used);
            ps->p += used;
        }
    }
}

static int php_json_parse_number(php_json_parser *ps, zval *out)
{
    const char *start = ps->p;
    const char *p = ps->p;
    int negative = 0, is_double = 0;
    size_t digits;

    if (p < ps->end && *p == '-') {
        negative = 1;
        p++;
    }
    if (p >= ps->end) {
        ps->error = PHP_JSON_ERROR_SYNTAX;
        return -1;
    }
    if (*p == '0') {
        p++;
    } else if (*p >= '1' && *p <= '9') {
        while (p < ps->end && *p >= '0' && *p <= '9') {
            p++;
        }
    } else {
        ps->error = PHP_JSON_ERROR_SYNTAX;
        return -1;
    }
    digits = (size_t) (p - start - negative);

    if (p + 1 < ps->end && *p == '.' && p[1] >= '0' && p[1] <= '9') {
        is_double = 1;
        p++;
        while (p < ps->end && *p >= '0' && *p <= '9') {
            p++;
        }
    }
    if (p < ps->end && (*p == 'e' || *p == 'E')) {
        const char *q = p + 1;
        if (q < ps->end && (*q == '+' || *q == '-')) {
            q++;
        }
        if (q < ps->end && *q >= '0' && *q <= '9') {
            is_double = 1;
            while (q < ps->end && *q >= '0' && *q <= '9') {
                q++;
            }
            p = q;
        }
    }

    if (!is_double) {
        int bigint = 0;

        if (digits >= PHP_JSON_INT_MAX_LENGTH) {
            if (digits == PHP_JSON_INT_MAX_LENGTH) {
                int cmp = strncmp(start + negative, php_json_long_min_digits,
                                  PHP_JSON_INT_MAX_LENGTH);
                if (!(cmp < 0 || (cmp == 0 && negative))) {
                    bigint = 1;
                }
            } else {
                bigint = 1;
            }
        }
        if (!bigint) {
            ZVAL_LONG(out, (zend_long) strtoll(start, NULL, 10));
            ps->p = p;
            return 0;
        }
        if (ps->options & PHP_JSON_BIGINT_AS_STRING) {
            ZVAL_STR(out, zend_string_init(start, (size_t) (p - start)));
            ps->p = p;
            return 0;
        }
    }

    ZVAL_DOUBLE(out, strtod(start, NULL));
    ps->p = p;
    return 0;
}

static int php_json_object_set(php_json_parser *ps, zval *obj,
                               zend_string *key, zval value)
{
    if (Z_TYPE_P(obj) == IS_ARRAY) {
        zval kz;
        ZVAL_STR(&kz, key);
        array_set(Z_ARR_P(obj), &kz, value);
        return 0;
    }

    /*
     * JSON_ERROR_INVALID_PROPERTY_NAME is new in PHP 7.0.  PHP 5.6 kept
     * property names as C strings, so an embedded NUL truncated the name and
     * a leading NUL was a fatal error.  Oracle:
     *   php7.0 -r 'var_dump(json_decode("{\"\\u0000ab\":1}"),
     *              json_last_error());'   => NULL int(9)
     *   php5.6 same input  => Fatal error: Cannot access property started
     *                         with '\0'
     *   php5.6 -r 'var_dump(json_decode("{\"a\\u0000b\":1}"));'
     *       => object(stdClass)#1 (1) { ["a"]=> int(1) }
     */
    /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
    if (php_json_version() >= 70000) {
        if (ZSTR_LEN(key) > 0 && ZSTR_VAL(key)[0] == '\0') {
            ps->error = PHP_JSON_ERROR_INVALID_PROPERTY_NAME;
            return -1;
        }
    } else {
        const char *nul;
        if (ZSTR_LEN(key) > 0 && ZSTR_VAL(key)[0] == '\0') {
            zend_fatal("Cannot access property started with '\\0'");
            ps->error = PHP_JSON_ERROR_INVALID_PROPERTY_NAME;
            return -1;
        }
        nul = (const char *) memchr(ZSTR_VAL(key), '\0', ZSTR_LEN(key));
        if (nul) {
            key = zend_string_init(ZSTR_VAL(key),
                                   (size_t) (nul - ZSTR_VAL(key)));
        }
    }

    /*
     * An empty property name was rewritten to "_empty_" up to PHP 7.0 and is
     * kept verbatim from 7.1 on.  Oracle:
     *   php7.0 -r 'var_dump(json_decode("{\"\":1}"));'
     *       => object(stdClass)#1 (1) { ["_empty_"]=> int(1) }
     *   php7.1 -r 'var_dump(json_decode("{\"\":1}"));'
     *       => object(stdClass)#1 (1) { [""]=> int(1) }
     */
    /* VERSIONGATE:70100 else-branch is the 5.6 behaviour */
    if (php_json_version() >= 70100) {
        /* the empty name is a legal property name */
    } else if (ZSTR_LEN(key) == 0) {
        key = zend_string_initz("_empty_");
    }

    zend_write_property(Z_OBJ_P(obj), key, value);
    return 0;
}

static int php_json_parse_object(php_json_parser *ps, zval *out)
{
    int as_array = (ps->options & PHP_JSON_OBJECT_AS_ARRAY) ? 1 : 0;

    if (ps->max_depth && ps->depth >= ps->max_depth) {
        ps->error = PHP_JSON_ERROR_DEPTH;
        return -1;
    }
    ++ps->depth;
    ps->p++;                                    /* '{' */

    if (as_array) {
        ZVAL_ARR(out, zend_hash_new());
    } else {
        zend_class_entry *ce = zend_lookup_class(zend_string_initz("stdClass"), 0);
        if (!ce) {
            /* Defensive: without stdClass there is nothing to build. */
            ps->error = PHP_JSON_ERROR_STATE_MISMATCH;
            return -1;
        }
        ZVAL_OBJ(out, zend_object_new(ce));
    }

    php_json_skip_ws(ps);
    if (ps->p < ps->end && *ps->p == '}') {
        ps->p++;
        --ps->depth;
        return 0;
    }
    if (ps->p < ps->end && *ps->p == ']') {
        ps->error = PHP_JSON_ERROR_STATE_MISMATCH;
        return -1;
    }

    for (;;) {
        zend_string *key;
        zval value;

        php_json_skip_ws(ps);
        if (ps->p >= ps->end || *ps->p != '"') {
            ps->error = PHP_JSON_ERROR_SYNTAX;
            return -1;
        }
        if (php_json_parse_string(ps, &key) < 0) {
            return -1;
        }
        php_json_skip_ws(ps);
        if (ps->p >= ps->end || *ps->p != ':') {
            ps->error = PHP_JSON_ERROR_SYNTAX;
            return -1;
        }
        ps->p++;
        php_json_skip_ws(ps);
        if (php_json_parse_value(ps, &value) < 0) {
            return -1;
        }
        if (php_json_object_set(ps, out, key, value) < 0) {
            return -1;
        }

        php_json_skip_ws(ps);
        if (ps->p < ps->end && *ps->p == ',') {
            ps->p++;
            continue;
        }
        if (ps->p < ps->end && *ps->p == '}') {
            ps->p++;
            --ps->depth;
            return 0;
        }
        if (ps->p < ps->end && *ps->p == ']') {
            ps->error = PHP_JSON_ERROR_STATE_MISMATCH;
            return -1;
        }
        ps->error = PHP_JSON_ERROR_SYNTAX;
        return -1;
    }
}

static int php_json_parse_array(php_json_parser *ps, zval *out)
{
    HashTable *ht;

    if (ps->max_depth && ps->depth >= ps->max_depth) {
        ps->error = PHP_JSON_ERROR_DEPTH;
        return -1;
    }
    ++ps->depth;
    ps->p++;                                    /* '[' */

    ht = zend_hash_new();
    ZVAL_ARR(out, ht);

    php_json_skip_ws(ps);
    if (ps->p < ps->end && *ps->p == ']') {
        ps->p++;
        --ps->depth;
        return 0;
    }
    if (ps->p < ps->end && *ps->p == '}') {
        ps->error = PHP_JSON_ERROR_STATE_MISMATCH;
        return -1;
    }

    for (;;) {
        zval value;

        php_json_skip_ws(ps);
        if (php_json_parse_value(ps, &value) < 0) {
            return -1;
        }
        zend_hash_next_index_insert(ht, value);

        php_json_skip_ws(ps);
        if (ps->p < ps->end && *ps->p == ',') {
            ps->p++;
            continue;
        }
        if (ps->p < ps->end && *ps->p == ']') {
            ps->p++;
            --ps->depth;
            return 0;
        }
        if (ps->p < ps->end && *ps->p == '}') {
            ps->error = PHP_JSON_ERROR_STATE_MISMATCH;
            return -1;
        }
        ps->error = PHP_JSON_ERROR_SYNTAX;
        return -1;
    }
}

static int php_json_parse_value(php_json_parser *ps, zval *out)
{
    unsigned char c;

    php_json_skip_ws(ps);
    if (ps->p >= ps->end) {
        ps->error = PHP_JSON_ERROR_SYNTAX;
        return -1;
    }

    c = (unsigned char) *ps->p;
    switch (c) {
        case '{': return php_json_parse_object(ps, out);
        case '[': return php_json_parse_array(ps, out);
        case '"': {
            zend_string *s;
            if (php_json_parse_string(ps, &s) < 0) {
                return -1;
            }
            ZVAL_STR(out, s);
            return 0;
        }
        case 't':
            if ((size_t) (ps->end - ps->p) >= 4 && !memcmp(ps->p, "true", 4)) {
                ps->p += 4;
                ZVAL_BOOL(out, 1);
                return 0;
            }
            break;
        case 'f':
            if ((size_t) (ps->end - ps->p) >= 5 && !memcmp(ps->p, "false", 5)) {
                ps->p += 5;
                ZVAL_BOOL(out, 0);
                return 0;
            }
            break;
        case 'n':
            if ((size_t) (ps->end - ps->p) >= 4 && !memcmp(ps->p, "null", 4)) {
                ps->p += 4;
                ZVAL_NULL(out);
                return 0;
            }
            break;
        default:
            if (c == '-' || (c >= '0' && c <= '9')) {
                return php_json_parse_number(ps, out);
            }
            break;
    }

    /*
     * php-src's scanner distinguishes three failures here: a control
     * character, a well formed but unexpected UTF-8 character, and a byte
     * that is not valid UTF-8 at all.
     */
    if (c < 0x20) {
        ps->error = PHP_JSON_ERROR_CTRL_CHAR;
    } else if (c >= 0x80) {
        size_t used;
        ps->error = (php_json_utf8_decode((const unsigned char *) ps->p,
                                          (size_t) (ps->end - ps->p), &used) < 0)
                    ? PHP_JSON_ERROR_UTF8 : PHP_JSON_ERROR_SYNTAX;
    } else {
        ps->error = PHP_JSON_ERROR_SYNTAX;
    }
    return -1;
}

/*
 * Public decoder.  Returns 0 on success and 1 on failure; on failure *ret is
 * set to NULL and the reason is available through json_last_error() (or a
 * thrown JsonException when JSON_THROW_ON_ERROR is set).
 *
 * `depth` counts nesting levels and, as in php-src, the check is
 * `depth >= max_depth` with the counter starting at 1, so decoding
 * "[[[1]]]" needs a depth of at least 4.  Oracle:
 *   php7.4 -r 'var_dump(json_decode("[[[1]]]", true, 3), json_last_error());'
 *       => NULL int(1)
 *   php7.4 -r 'var_dump(json_decode("[[[1]]]", true, 4), json_last_error());'
 *       => array(...) int(0)
 */
int php_json_decode(const char *str, size_t len, int assoc, zend_long depth,
                    zend_long options, zval *ret)
{
    php_json_parser ps;
    zval tmp;

    ZVAL_NULL(ret);

    if (assoc) {
        options |= PHP_JSON_OBJECT_AS_ARRAY;
    }
    if (depth <= 0) {
        depth = PHP_JSON_PARSER_DEFAULT_DEPTH;
    }

    if (!(options & PHP_JSON_THROW_ON_ERROR)) {
        php_json_last_error_code = PHP_JSON_ERROR_NONE;
    }

    if (len == 0) {
        /*
         * PHP 5.6 returned NULL for an empty string and left json_last_error()
         * at JSON_ERROR_NONE; from PHP 7.0 it reports a syntax error.
         * Oracle: php5.6 -r 'json_decode(""); echo json_last_error();' => 0
         *         php7.0 same => 4
         * (ext/json/tests/bug54484.phpt pins the 5.6 side.)
         */
        /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
        if (php_json_version() >= 70000) {
            php_json_report(PHP_JSON_ERROR_SYNTAX, options);
        }
        return 1;
    }

    ps.p = str;
    ps.end = str + len;
    ps.options = options;
    ps.depth = 1;
    ps.max_depth = (int) depth;
    ps.error = PHP_JSON_ERROR_NONE;

    ZVAL_NULL(&tmp);
    if (php_json_parse_value(&ps, &tmp) < 0) {
        php_json_report(ps.error ? ps.error : PHP_JSON_ERROR_SYNTAX, options);
        return 1;
    }

    php_json_skip_ws(&ps);
    if (ps.p != ps.end) {
        int code = PHP_JSON_ERROR_SYNTAX;
        if ((unsigned char) *ps.p < 0x20) {
            code = PHP_JSON_ERROR_CTRL_CHAR;
        }
        php_json_report(code, options);
        return 1;
    }

    php_json_last_error_code = PHP_JSON_ERROR_NONE;
    *ret = tmp;
    return 0;
}

/* ------------------------------------------------------------------ */
/* builtins                                                             */
/* ------------------------------------------------------------------ */
static zend_long php_json_arg_long(uint32_t argc, zval *argv, uint32_t idx,
                                   zend_long dflt)
{
    zval *z;

    if (idx >= argc) {
        return dflt;
    }
    z = zval_deref(&argv[idx]);
    if (Z_TYPE_P(z) == IS_NULL || Z_TYPE_P(z) == IS_UNDEF) {
        return dflt;
    }
    return zval_get_long(z);
}

static void zif_json_encode(zval *ret, uint32_t argc, zval *argv)
{
    zend_long options = php_json_arg_long(argc, argv, 1, 0);
    zend_long depth = php_json_arg_long(argc, argv, 2,
                                        PHP_JSON_PARSER_DEFAULT_DEPTH);
    zend_string *out;
    int err = PHP_JSON_ERROR_NONE;

    /*
     * The depth is used verbatim, including 0: php-src reports
     * JSON_ERROR_DEPTH rather than validating the argument.  Oracle (php8.4):
     *   var_export(json_encode(new stdClass, 0, 0)); => false
     *   var_export(json_encode(new stdClass, 0, 1)); => '{}'
     */
    out = php_json_encode_ex(zval_deref(&argv[0]), options, depth, &err);
    if (!out) {
        ZVAL_BOOL(ret, 0);
        return;
    }
    ZVAL_STR(ret, out);
}

static void zif_json_decode(zval *ret, uint32_t argc, zval *argv)
{
    zend_string *str;
    zend_long depth, options;
    int assoc = 0;

    ZVAL_NULL(ret);
    str = zval_get_string(zval_deref(&argv[0]));
    depth = php_json_arg_long(argc, argv, 2, PHP_JSON_PARSER_DEFAULT_DEPTH);
    options = php_json_arg_long(argc, argv, 3, 0);

    /*
     * For BC the bool $assoc overrides the JSON_OBJECT_AS_ARRAY bit, but only
     * when it was actually supplied (null means "not given").
     */
    if (argc > 1) {
        zval *a = zval_deref(&argv[1]);
        if (Z_TYPE_P(a) != IS_NULL && Z_TYPE_P(a) != IS_UNDEF) {
            if (zval_is_true(a)) {
                options |= PHP_JSON_OBJECT_AS_ARRAY;
            } else {
                options &= ~PHP_JSON_OBJECT_AS_ARRAY;
            }
        }
    }
    assoc = (options & PHP_JSON_OBJECT_AS_ARRAY) ? 1 : 0;

    if (ZSTR_LEN(str) == 0) {
        php_json_decode(ZSTR_VAL(str), ZSTR_LEN(str), assoc, depth, options, ret);
        return;
    }

    /*
     * php-src validates the depth only once the string is known to be
     * non-empty.  Oracle:
     *   php7.4 -r 'var_dump(json_decode("[]", false, 0x100000000));'
     *       => Warning: json_decode(): Depth must be lower than 2147483647
     *          NULL
     */
    if (depth <= 0) {

            zend_error(E_WARNING,
                       "json_decode(): Depth must be greater than zero");

        ZVAL_NULL(ret);
        return;
    }
    if (depth > 2147483647L) {

            zend_error(E_WARNING, "json_decode(): Depth must be lower than %d",
                       2147483647);

        ZVAL_NULL(ret);
        return;
    }

    php_json_decode(ZSTR_VAL(str), ZSTR_LEN(str), assoc, depth, options, ret);
}



static void zif_json_last_error(zval *ret, uint32_t argc, zval *argv)
{
    ZVAL_LONG(ret, php_json_last_error_code);
}

static void zif_json_last_error_msg(zval *ret, uint32_t argc, zval *argv)
{
    ZVAL_STR(ret, zend_string_initz(
                 php_json_get_error_msg(php_json_last_error_code)));
}

const php_function_entry php_functions_json[] = {
    { "json_encode",         zif_json_encode,         1, 3,  NULL, 0,     0 },
    { "json_decode",         zif_json_decode,         1, 4,  NULL, 0,     0 },
    { "json_last_error",     zif_json_last_error,     0, 0,  NULL, 0,     0 },
    { "json_last_error_msg", zif_json_last_error_msg, 0, 0,  NULL, 0,     0 },

    { NULL, NULL, 0, 0, NULL, 0, 0 }
};

/* ------------------------------------------------------------------ */
/* constants                                                            */
/* ------------------------------------------------------------------ */
void php_json_register_constants(void)
{
    long ver = php_json_version();

#define JSON_CONST(n, v) zend_register_long_constant((n), (zend_long) (v))

    JSON_CONST("JSON_ERROR_NONE",            PHP_JSON_ERROR_NONE);
    JSON_CONST("JSON_ERROR_DEPTH",           PHP_JSON_ERROR_DEPTH);
    JSON_CONST("JSON_ERROR_STATE_MISMATCH",  PHP_JSON_ERROR_STATE_MISMATCH);
    JSON_CONST("JSON_ERROR_CTRL_CHAR",       PHP_JSON_ERROR_CTRL_CHAR);
    JSON_CONST("JSON_ERROR_SYNTAX",          PHP_JSON_ERROR_SYNTAX);
    JSON_CONST("JSON_ERROR_UTF8",            PHP_JSON_ERROR_UTF8);
    JSON_CONST("JSON_ERROR_RECURSION",       PHP_JSON_ERROR_RECURSION);
    JSON_CONST("JSON_ERROR_INF_OR_NAN",      PHP_JSON_ERROR_INF_OR_NAN);
    JSON_CONST("JSON_ERROR_UNSUPPORTED_TYPE", PHP_JSON_ERROR_UNSUPPORTED_TYPE);

    JSON_CONST("JSON_HEX_TAG",               PHP_JSON_HEX_TAG);
    JSON_CONST("JSON_HEX_AMP",               PHP_JSON_HEX_AMP);
    JSON_CONST("JSON_HEX_APOS",              PHP_JSON_HEX_APOS);
    JSON_CONST("JSON_HEX_QUOT",              PHP_JSON_HEX_QUOT);
    JSON_CONST("JSON_FORCE_OBJECT",          PHP_JSON_FORCE_OBJECT);
    JSON_CONST("JSON_NUMERIC_CHECK",         PHP_JSON_NUMERIC_CHECK);
    JSON_CONST("JSON_UNESCAPED_SLASHES",     PHP_JSON_UNESCAPED_SLASHES);
    JSON_CONST("JSON_PRETTY_PRINT",          PHP_JSON_PRETTY_PRINT);
    JSON_CONST("JSON_UNESCAPED_UNICODE",     PHP_JSON_UNESCAPED_UNICODE);
    JSON_CONST("JSON_PARTIAL_OUTPUT_ON_ERROR", PHP_JSON_PARTIAL_OUTPUT_ON_ERROR);
    JSON_CONST("JSON_BIGINT_AS_STRING",      PHP_JSON_BIGINT_AS_STRING);
    JSON_CONST("JSON_OBJECT_AS_ARRAY",       PHP_JSON_OBJECT_AS_ARRAY);

    /* VERSION: JSON_PRESERVE_ZERO_FRACTION is 5.6.6+ */
    JSON_CONST("JSON_PRESERVE_ZERO_FRACTION", PHP_JSON_PRESERVE_ZERO_FRACTION);

    /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
    if (ver >= 70000) {
        JSON_CONST("JSON_ERROR_INVALID_PROPERTY_NAME",
                   PHP_JSON_ERROR_INVALID_PROPERTY_NAME);
        JSON_CONST("JSON_ERROR_UTF16", PHP_JSON_ERROR_UTF16);
    }

    /* VERSIONGATE:70100 else-branch is the 5.6 behaviour */
    if (ver >= 70100) {
        JSON_CONST("JSON_UNESCAPED_LINE_TERMINATORS",
                   PHP_JSON_UNESCAPED_LINE_TERMINATORS);
    }







#undef JSON_CONST
}

/*
 * main/php_number.c -- numeric to string conversion.
 *
 * Reimplementation of php-src's main/snprintf.c:php_gcvt() and the
 * zend_dtoa() digit generator it sits on top of, plus the three entry
 * points the rest of the interpreter uses:
 *
 *   php_long_to_string()   -- (string) cast of an integer
 *   php_double_to_string() -- "%.*G" / "%.*H" style, driven by `precision`
 *   smart_double_repr()    -- the serialize_precision = -1 "shortest
 *                             round-tripping decimal" path
 *
 * php-src generates digits with David Gay's zend_dtoa().  We do not ship
 * dtoa here; instead the digit generator is built on the libc printf, which
 * on every platform we target is correctly rounded (round-half-to-even), so
 * it produces exactly the same digit strings:
 *
 *   dtoa mode 2 (ndigits significant digits, trailing zeros suppressed)
 *       == snprintf("%.*e", ndigits - 1, v) mantissa, trailing zeros stripped
 *   dtoa mode 0 (shortest string that reads back as v)
 *       == the first of the above for ndigits = 1..17 that strtod()s back
 *          to v.  (For a fixed digit count the correctly rounded decimal is
 *          the one most likely to round-trip, so the smallest count that
 *          works is the same one dtoa picks.)
 *
 * Verified against oracle/php{5.6,7.0,7.1,7.2,7.3,7.4,8.0,8.4}.
 */

#include "zend.h"
#include "zend_API.h"
#include "zend_ast.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Widest digit run we generate; php-src's NDIG is 320 but php_gcvt is only
 * ever reached with ndigit <= 53 (sprintf clamps) or 17 (the ini values). */
#define PHP_NUM_MAX_DIGITS   128
#define PHP_NUM_BUF_SIZE     (PHP_NUM_MAX_DIGITS + 64)

/* php-src: ZEND_DOUBLE_MAX_LENGTH */
#define PHP_DOUBLE_MAX_LENGTH (PHP_NUM_MAX_DIGITS + 32)

/* ------------------------------------------------------------------ */
/* php_long_to_string                                                   */
/* ------------------------------------------------------------------ */
zend_string *php_long_to_string(zend_long l)
{
    char buf[24];
    char *p = buf + sizeof(buf);
    zend_ulong magn;
    int neg = 0;

    if (l < 0) {
        neg = 1;
        /* -ZEND_LONG_MIN overflows, so negate in the unsigned domain. */
        magn = ((zend_ulong) -(l + 1)) + 1;
    } else {
        magn = (zend_ulong) l;
    }

    do {
        *--p = (char) ('0' + (magn % 10));
        magn /= 10;
    } while (magn);

    if (neg) {
        *--p = '-';
    }

    return zend_string_init(p, (size_t) (buf + sizeof(buf) - p));
}

/* ------------------------------------------------------------------ */
/* digit generator (stand-in for zend_dtoa)                             */
/* ------------------------------------------------------------------ */

/*
 * mode 0 -> shortest round-tripping digit string
 * mode 2 -> max(1, ndigit) significant digits, trailing zeros suppressed
 *
 * `digits` receives a NUL terminated run of decimal digits with no sign and
 * no decimal point; the value is 0.<digits> * 10^(*decpt).  Zero yields the
 * single digit "0" with *decpt == 1, exactly like zend_dtoa().
 *
 * Infinity and NaN set *decpt to 9999 and leave "Infinity"/"NaN" in digits,
 * which is the dtoa protocol php_gcvt() keys off.
 */
static void php_num_dtoa(double value, int mode, int ndigit,
                         int *decpt, int *sign, char *digits)
{
    char buf[PHP_NUM_BUF_SIZE];
    const char *p;
    int prec, n = 0, exp10 = 0;

    *sign = signbit(value) ? 1 : 0;

    if (isnan(value)) {
        *decpt = 9999;
        memcpy(digits, "NaN", 4);
        return;
    }
    if (isinf(value)) {
        *decpt = 9999;
        memcpy(digits, "Infinity", 9);
        return;
    }

    if (mode == 0) {
        /* Shortest decimal that strtod()s back to the same double. */
        for (prec = 1; prec < 17; prec++) {
            snprintf(buf, sizeof buf, "%.*e", prec - 1, value);
            if (strtod(buf, NULL) == value) {
                break;
            }
        }
    } else {
        prec = ndigit < 1 ? 1 : ndigit;
        if (prec > PHP_NUM_MAX_DIGITS) {
            prec = PHP_NUM_MAX_DIGITS;
        }
    }
    snprintf(buf, sizeof buf, "%.*e", prec - 1, value);

    p = buf;
    if (*p == '-' || *p == '+') {
        p++;
    }
    while (*p && *p != 'e' && *p != 'E') {
        if (*p >= '0' && *p <= '9') {
            digits[n++] = *p;
        }
        p++;
    }
    digits[n] = '\0';
    if (*p == 'e' || *p == 'E') {
        exp10 = (int) strtol(p + 1, NULL, 10);
    }

    /* dtoa modes 0 and 2 both suppress trailing zeros. */
    while (n > 1 && digits[n - 1] == '0') {
        digits[--n] = '\0';
    }

    if (n == 1 && digits[0] == '0') {
        *decpt = 1;                 /* zend_dtoa()'s convention for 0.0 */
    } else {
        *decpt = exp10 + 1;
    }
}

/* ------------------------------------------------------------------ */
/* php_gcvt -- port of main/snprintf.c                                  */
/* ------------------------------------------------------------------ */

/*
 * ndigit < 0 selects dtoa mode 0 (shortest round-trip) and, as in php-src,
 * still uses 17 as the "switch to E notation" threshold.
 * Returns the length written into buf (which must hold
 * PHP_DOUBLE_MAX_LENGTH bytes).
 */
static size_t php_num_gcvt(double value, int ndigit, char dec_point,
                           char exponent, char *buf)
{
    char digits[PHP_NUM_MAX_DIGITS + 16];
    char *dst = buf;
    const char *src;
    int i, decpt, sign;
    int mode = ndigit >= 0 ? 2 : 0;

    if (mode == 0) {
        ndigit = 17;
    }

    php_num_dtoa(value, mode, ndigit, &decpt, &sign, digits);

    if (decpt == 9999) {
        /*
         * Infinity or NaN.  php-src truncates through snprintf(buf,
         * ndigit + 1, ...) here, but every live call site either passes
         * ndigit >= 14 or tests isinf/isnan before calling, so the full
         * word is always what comes out.
         */
        const char *word = (digits[0] == 'I') ? "INF" : "NAN";
        size_t len = 0;
        if (sign && digits[0] == 'I') {
            buf[len++] = '-';
        }
        memcpy(buf + len, word, 3);
        len += 3;
        buf[len] = '\0';
        return len;
    }

    /*
     * VERSION: php_gcvt() keeps the sign of -0.0 on every release, so this
     * function does NOT special-case it.  The familiar
     *   php5.6 -r 'echo -0.0;'  => 0        php7.0 -r 'echo -0.0;' => -0
     * split is produced one layer up: PHP 5 compiled unary minus as
     * `0 - $x`, which turns -0.0 into +0.0 before it ever reaches here.
     * Feeding the same bit pattern in from a cast proves it:
     *   php5.6 -r '$x=(float)"-0.0"; echo $x, " ", serialize($x);'
     *       => -0 d:-0;
     * so the fix belongs in the unary-minus opcode, not in this module.
     */

    if (sign) {
        *dst++ = '-';
    }

    if ((decpt >= 0 && decpt > ndigit) || decpt < -3) {
        /* E-style, e.g. 1.2345E+13 */
        if (--decpt < 0) {
            sign = 1;
            decpt = -decpt;
        } else {
            sign = 0;
        }
        src = digits;
        *dst++ = *src++;
        *dst++ = dec_point;
        if (*src == '\0') {
            *dst++ = '0';
        } else {
            do {
                *dst++ = *src++;
            } while (*src != '\0');
        }
        *dst++ = exponent;
        *dst++ = sign ? '-' : '+';
        if (decpt < 10) {
            *dst++ = (char) ('0' + decpt);
            *dst = '\0';
        } else {
            for (sign = decpt, i = 0; (sign /= 10) != 0; i++) {
                /* count the extra exponent digits */
            }
            dst[i + 1] = '\0';
            while (decpt != 0) {
                dst[i--] = (char) ('0' + decpt % 10);
                decpt /= 10;
            }
            dst = strchr(dst, '\0');
        }
    } else if (decpt < 0) {
        /* 0.000ddd */
        *dst++ = '0';
        *dst++ = dec_point;
        do {
            *dst++ = '0';
        } while (++decpt < 0);
        src = digits;
        while (*src != '\0') {
            *dst++ = *src++;
        }
        *dst = '\0';
    } else {
        /* plain ddd.ddd */
        for (i = 0, src = digits; i < decpt; i++) {
            if (*src != '\0') {
                *dst++ = *src++;
            } else {
                *dst++ = '0';
            }
        }
        if (*src != '\0') {
            if (src == digits) {
                *dst++ = '0';       /* zero before the decimal point */
            }
            *dst++ = dec_point;
            for (i = decpt; digits[i] != '\0'; i++) {
                *dst++ = digits[i];
            }
        }
        *dst = '\0';
    }

    return (size_t) (dst - buf);
}

/*
 * Shared with the other modules in this package: php_printf.c uses it for
 * %g/%G and php_json.c for encoding doubles.  It is not declared in the
 * frozen headers, so those files pick it up with a matching extern
 * declaration.
 */
size_t php_gcvt_into(double value, int ndigit, char dec_point,
                     char exponent, char *buf)
{
    return php_num_gcvt(value, ndigit, dec_point, exponent, buf);
}

/* ------------------------------------------------------------------ */
/* php_double_to_string                                                 */
/* ------------------------------------------------------------------ */

/*
 * mode 0 -- 'G'/'H' style with an uppercase 'E' exponent.  This is what
 *           (string)$float, echo, serialize(), var_export() and var_dump()
 *           produce:  "0.1", "1.0E+25", "INF", "-INF", "NAN", and "1" for
 *           1.0 (php_gcvt never emits a trailing ".0" in plain notation).
 * mode 1 -- the same digits with a lowercase 'e' exponent, which is what
 *           json_encode() and sprintf("%g") use.
 *
 * `precision` is the ini setting: >= 1 selects that many significant digits,
 * and a negative value selects the shortest round-tripping form.
 *
 * Oracle (php7.4 -n -d precision=14):
 *   echo 1.0      => 1          echo 0.1       => 0.1
 *   echo 1e25     => 1.0E+25    echo 1e-5      => 1.0E-5
 *   echo 0.0001   => 0.0001     echo 1/3       => 0.33333333333333
 *   echo INF      => INF        echo -INF      => -INF     echo NAN => NAN
 */
zend_string *php_double_to_string(double d, int precision, int mode)
{
    char buf[PHP_DOUBLE_MAX_LENGTH];
    char exp_char = (mode == 1) ? 'e' : 'E';
    size_t len;

    /* php-src: smart_str_append_double() models snprintf and maps 0 -> 1. */
    if (precision == 0) {
        precision = 1;
    }

    len = php_num_gcvt(d, precision, '.', exp_char, buf);
    return zend_string_init(buf, len);
}

/* ------------------------------------------------------------------ */
/* smart_double_repr -- serialize_precision = -1                        */
/* ------------------------------------------------------------------ */

/*
 * The shortest decimal string that reads back as `d`, laid out the way
 * php_gcvt() would with a 17 digit E-notation threshold and an uppercase
 * exponent.  This is the serialize()/var_export()/var_dump() spelling;
 * json_encode() wants the same digits with a lowercase 'e', which is
 * php_double_to_string(d, -1, 1).
 *
 * Oracle (php7.4 -n -d precision=14):
 *   serialize(1.0)     d:1;                    -> "1"
 *   serialize(0.1)     d:0.1;                  -> "0.1"
 *   serialize(1/3)     d:0.3333333333333333;   -> "0.3333333333333333"
 *   serialize(1e100)   d:1.0E+100;             -> "1.0E+100"
 *   serialize(1e16)    d:10000000000000000;    -> "10000000000000000"
 *   serialize(1e17)    d:1.0E+17;              -> "1.0E+17"
 *   serialize(5e-324)  d:5.0E-324;             -> "5.0E-324"
 *   serialize(0.1+0.2) d:0.30000000000000004;  -> "0.30000000000000004"
 *
 * NOTE the missing ".0" for whole floats: callers that need var_export()'s
 * "1.0" append it themselves (php-src does the same in
 * php_var_export_ex()), and json_encode() only appends it under
 * JSON_PRESERVE_ZERO_FRACTION.
 *
 * VERSION: the *consumers* moved to serialize_precision at different times.
 * serialize()/var_export()/json_encode() switched in PHP 7.1; var_dump()
 * kept using `precision` until PHP 8.0.  Oracle, -d precision=14:
 *   php7.0 -r 'echo json_encode(1/3);'  => 0.33333333333333
 *   php7.1 -r 'echo json_encode(1/3);'  => 0.3333333333333333
 *   php7.4 -r 'var_dump(1/3);'          => float(0.33333333333333)
 *   php8.0 -r 'var_dump(1/3);'          => float(0.3333333333333333)
 *
 * This whole function is unreachable before PHP 7.1: serialize_precision
 * cannot be -1 on 5.6/7.0, so every caller takes the php_double_to_string()
 * path with a positive precision instead.  Callers must therefore guard the
 * call with EG(php_version) >= 70100 and carry the same marker.
 * Known call sites (none inside this package):
 *   ext/standard  php_var_export()  php_var_serialize()  php_var_dump() [8.0+]
 *   ext/json      handled here via php_json_double_precision()
 */
/* Not gated: this is a formatting helper, not a language feature; its
 * call sites are what the release selects. */
zend_string *smart_double_repr(double d)
{
    char buf[PHP_DOUBLE_MAX_LENGTH];
    size_t len = php_num_gcvt(d, -1, '.', 'E', buf);
    return zend_string_init(buf, len);
}

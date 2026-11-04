/*
 * main/php_printf.c -- the printf engine behind sprintf()/printf()/
 * vsprintf()/vprintf()/fprintf()/vfprintf().
 *
 * Port of php-src's ext/standard/formatted_print.c:php_formatted_print(),
 * including its helper layer (php_sprintf_appendstring / _appendint /
 * _appenduint / _append2n / _appenddouble) and main/snprintf.c:php_conv_fp().
 * Keeping the helper structure is not cosmetic: several observable quirks
 * fall straight out of it (see the notes on %x precision and on zero padding
 * in front of a sign).
 *
 * Grammar: %[argnum$][flags][width][.precision]specifier
 *   flags       '-'  left align
 *               '+'  always print a sign on numbers
 *               ' '  pad with spaces (the default)
 *               '0'  pad with zeros
 *               '\'c pad with the character c
 *   specifiers  b c d e E f F g G o s u x X %
 *
 * All behaviour below was checked against
 * oracle/php{5.6,7.0,7.4,8.0,8.4} -n -d precision=14.
 */

#include "zend.h"
#include "zend_API.h"
#include "zend_ast.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* php-src: ext/standard/formatted_print.c */
#define ALIGN_LEFT            0
#define ALIGN_RIGHT           1
#define ADJ_WIDTH             1
#define ADJ_PRECISION         2
#define NUM_BUF_SIZE          512
#define FLOAT_PRECISION       6
#define MAX_FLOAT_PRECISION   53
/* php-src: main/snprintf.c NDIG */
#define CONV_FP_NDIG          320

static const char hexchars[] = "0123456789abcdef";
static const char HEXCHARS[] = "0123456789ABCDEF";

/* php_number.c; not in the frozen headers, so declared here. */
extern size_t php_gcvt_into(double value, int ndigit, char dec_point,
                            char exponent, char *buf);

static long php_printf_version(void)
{
    return EG(php_version) ? EG(php_version) : 80400;
}

/* ------------------------------------------------------------------ */
/* output buffer                                                        */
/* ------------------------------------------------------------------ */
typedef struct {
    char   *data;
    size_t  len;
    size_t  cap;
    int     oom;
} php_pbuf;

static void pbuf_reserve(php_pbuf *b, size_t extra)
{
    size_t need;

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
    {
        char *n = (char *) realloc(b->data, b->cap);
        if (!n) {
            b->oom = 1;
            return;
        }
        b->data = n;
    }
}

static void pbuf_char(php_pbuf *b, char c)
{
    pbuf_reserve(b, 1);
    if (b->oom) {
        return;
    }
    b->data[b->len++] = c;
}

static void pbuf_mem(php_pbuf *b, const char *s, size_t n)
{
    if (!n) {
        return;
    }
    pbuf_reserve(b, n);
    if (b->oom) {
        return;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
}

static void pbuf_fill(php_pbuf *b, char c, size_t n)
{
    if (!n) {
        return;
    }
    pbuf_reserve(b, n);
    if (b->oom) {
        return;
    }
    memset(b->data + b->len, c, n);
    b->len += n;
}

/* ------------------------------------------------------------------ */
/* php_sprintf_appendstring                                             */
/* ------------------------------------------------------------------ */
/*
 * `expprec` means "a .precision was given"; when it is set, max_width caps
 * the number of characters copied.  %o/%x/%X/%b pass max_width = 0 with
 * expprec forwarded, which is why sprintf("%.2x", 255) yields "" in PHP.
 */
static void php_sprintf_appendstring(php_pbuf *b, const char *add,
                                     size_t min_width, size_t max_width,
                                     char padding, int alignment, size_t len,
                                     int neg, int expprec, int always_sign)
{
    size_t copy_len = expprec ? (max_width < len ? max_width : len) : len;
    size_t npad = (min_width < copy_len) ? 0 : min_width - copy_len;

    if (alignment == ALIGN_RIGHT) {
        if ((neg || always_sign) && padding == '0') {
            pbuf_char(b, neg ? '-' : '+');
            add++;
            if (copy_len) {
                copy_len--;
            }
        }
        pbuf_fill(b, padding, npad);
        pbuf_mem(b, add, copy_len);
    } else {
        pbuf_mem(b, add, copy_len);
        pbuf_fill(b, padding, npad);
    }
}

/* ------------------------------------------------------------------ */
/* php_sprintf_appendint / _appenduint / _append2n                      */
/* ------------------------------------------------------------------ */
static void php_sprintf_appendint(php_pbuf *b, zend_long number, size_t width,
                                  char padding, int alignment, int always_sign)
{
    char numbuf[NUM_BUF_SIZE];
    zend_ulong magn, nmagn;
    unsigned int i = NUM_BUF_SIZE - 1;
    int neg = 0;

    if (number < 0) {
        neg = 1;
        magn = ((zend_ulong) - (number + 1)) + 1;
    } else {
        magn = (zend_ulong) number;
    }

    /* Can't right-pad 0's on integers. */
    if (alignment == ALIGN_LEFT && padding == '0') {
        padding = ' ';
    }

    numbuf[i] = '\0';
    do {
        nmagn = magn / 10;
        numbuf[--i] = (char) ((magn - (nmagn * 10)) + '0');
        magn = nmagn;
    } while (magn > 0 && i > 1);

    if (neg) {
        numbuf[--i] = '-';
    } else if (always_sign) {
        numbuf[--i] = '+';
    }

    php_sprintf_appendstring(b, &numbuf[i], width, 0, padding, alignment,
                             (NUM_BUF_SIZE - 1) - i, neg, 0, always_sign);
}

static void php_sprintf_appenduint(php_pbuf *b, zend_ulong number, size_t width,
                                   char padding, int alignment)
{
    char numbuf[NUM_BUF_SIZE];
    zend_ulong magn = number, nmagn;
    unsigned int i = NUM_BUF_SIZE - 1;

    if (alignment == ALIGN_LEFT && padding == '0') {
        padding = ' ';
    }

    numbuf[i] = '\0';
    do {
        nmagn = magn / 10;
        numbuf[--i] = (char) ((magn - (nmagn * 10)) + '0');
        magn = nmagn;
    } while (magn > 0 && i > 0);

    php_sprintf_appendstring(b, &numbuf[i], width, 0, padding, alignment,
                             (NUM_BUF_SIZE - 1) - i, 0, 0, 0);
}

static void php_sprintf_append2n(php_pbuf *b, zend_long number, size_t width,
                                 char padding, int alignment, int n,
                                 const char *chartable, int expprec)
{
    char numbuf[NUM_BUF_SIZE];
    zend_ulong num = (zend_ulong) number;
    unsigned int i = NUM_BUF_SIZE - 1;
    int andbits = (1 << n) - 1;

    numbuf[i] = '\0';
    do {
        numbuf[--i] = chartable[num & (zend_ulong) andbits];
        num >>= n;
    } while (num > 0);

    php_sprintf_appendstring(b, &numbuf[i], width, 0, padding, alignment,
                             (NUM_BUF_SIZE - 1) - i, 0, expprec, 0);
}

/* ------------------------------------------------------------------ */
/* php_conv_fp -- main/snprintf.c                                       */
/* ------------------------------------------------------------------ */
/*
 * Renders `num` in %f ('F') or %e/%E style into buf and returns the length.
 * Two deliberate deviations from plain libc printf, both taken from
 * php_ecvt()/php_fcvt():
 *
 *   1. the exponent carries no leading zeros and at least one digit, so
 *      sprintf("%e", 10) is "1.000000e+1", not C's "1.000000e+01", and an
 *      exponent of zero renders as "+0";
 *   2. __cvt() special-cases value == 0.0 before consulting the sign bit, so
 *      -0.0 loses its sign here.  Oracle: printf("%f", -0.0) => "0.000000"
 *      (C prints "-0.000000").  This holds on every release 5.6 .. 8.4.
 */
static size_t php_conv_fp(char format, double num, int add_dp, int precision,
                          char dec_point, int *is_negative, char *buf)
{
    char tmp[NUM_BUF_SIZE];
    char *s = buf;
    const char *p;
    int n;

    if (precision >= CONV_FP_NDIG - 1) {
        precision = CONV_FP_NDIG - 2;
    }

    *is_negative = (num != 0.0 && signbit(num)) ? 1 : 0;
    if (num < 0.0 || (num == 0.0 && signbit(num))) {
        num = -num;
    }

    if (format == 'F') {
        n = snprintf(tmp, sizeof tmp, "%.*f", precision, num);
        if (n < 0 || (size_t) n >= sizeof tmp) {
            n = (int) sizeof tmp - 1;
        }
        p = tmp;
        while (*p) {
            *s++ = (*p == '.') ? dec_point : *p;
            p++;
        }
        if (add_dp && !memchr(tmp, '.', (size_t) n)) {
            *s++ = dec_point;
        }
        *s = '\0';
        return (size_t) (s - buf);
    }

    /* 'e' or 'E' */
    n = snprintf(tmp, sizeof tmp, "%.*e", precision, num);
    if (n < 0 || (size_t) n >= sizeof tmp) {
        n = (int) sizeof tmp - 1;
    }

    p = tmp;
    while (*p && *p != 'e' && *p != 'E') {
        *s++ = *p++;
    }
    if (add_dp && !memchr(buf, '.', (size_t) (s - buf))) {
        *s++ = '.';
    }
    *s++ = format;                      /* 'e' or 'E' */
    {
        long e = 0;
        if (*p) {
            e = strtol(p + 1, NULL, 10);
        }
        /*
         * The exponent carries no leading zeros and always at least one
         * digit, so printf("%e", 10) is "1.000000e+1", not C's
         * "1.000000e+01", and an exponent of zero renders as "+0".
         *
         * This is the documented "PHP 7.0 changed the %e exponent format"
         * site.  It is kept as an explicit runtime gate so the 5.6 snapshot
         * can be stripped down to the else-branch, but note that the change
         * is not observable: php_conv_fp() and ap_php_conv_10() are byte
         * identical in php-5.6.40 and php-7.0.33, and all 67 format/value
         * pairs of the self-test agree on 5.6, 7.0, 7.4, 8.0 and 8.4.
         * Oracle:
         *   php5.6 -r 'printf("%e|%.3e|%e", 10, 1234.5678, 0.0);'
         *       => 1.000000e+1|1.235e+3|0.000000e+0
         *   php7.0 -r 'printf("%e|%.3e|%e", 10, 1234.5678, 0.0);'
         *       => 1.000000e+1|1.235e+3|0.000000e+0
         */
        /* VERSIONGATE:70000 else-branch is the 5.6 behaviour */
        if (php_printf_version() >= 70000) {
            if (e == 0) {
                *s++ = '+';
                *s++ = '0';
            } else {
                char ebuf[24];
                int k, m = 0;
                long mag = e < 0 ? -e : e;
                *s++ = (e < 0) ? '-' : '+';
                do {
                    ebuf[m++] = (char) ('0' + (mag % 10));
                    mag /= 10;
                } while (mag);
                for (k = m - 1; k >= 0; k--) {
                    *s++ = ebuf[k];
                }
            }
        } else {
            /* PHP 5.6: ap_php_conv_10() with the same "at least one digit,
             * never zero padded" rule. */
            if (e == 0) {
                *s++ = '+';
                *s++ = '0';
            } else {
                char ebuf[24];
                int k, m = 0;
                long mag = e < 0 ? -e : e;
                *s++ = (e < 0) ? '-' : '+';
                do {
                    ebuf[m++] = (char) ('0' + (mag % 10));
                    mag /= 10;
                } while (mag);
                for (k = m - 1; k >= 0; k--) {
                    *s++ = ebuf[k];
                }
            }
        }
    }
    *s = '\0';
    return (size_t) (s - buf);
}

/* ------------------------------------------------------------------ */
/* php_sprintf_appenddouble                                             */
/* ------------------------------------------------------------------ */
static void php_sprintf_appenddouble(php_pbuf *b, double number, size_t width,
                                     char padding, int alignment, int precision,
                                     int adjust, char fmt, int always_sign,
                                     const char *fname)
{
    char num_buf[NUM_BUF_SIZE];
    char *s;
    size_t s_len = 0;
    int is_negative = 0;

    if ((adjust & ADJ_PRECISION) == 0) {
        precision = FLOAT_PRECISION;
    } else if (precision > MAX_FLOAT_PRECISION) {
        zend_error(E_NOTICE,
                   "%s(): Requested precision of %d digits was truncated "
                   "to PHP maximum of %d digits", fname, precision,
                   MAX_FLOAT_PRECISION);
        precision = MAX_FLOAT_PRECISION;
    }

    /*
     * NOTE the hardcoded min_width: php-src passes the literal string length
     * here, not `width`, so a requested field width is silently ignored for
     * INF/NaN.  Oracle: sprintf("%10f|", INF) => "INF|".
     */
    if (isnan(number)) {
        is_negative = (number < 0);
        php_sprintf_appendstring(b, "NaN", 3, 0, padding, alignment, 3,
                                 is_negative, 0, always_sign);
        return;
    }
    if (isinf(number)) {
        is_negative = (number < 0);
        /*
         * VERSION: up to PHP 8.4.1 the word was always "INF" and the sign was
         * only ever reached through the zero-padding branch of
         * php_sprintf_appendstring(), which produced the famous "-NF".
         * PHP 8.4.2 started passing "-INF" explicitly.  Oracle:
         *   php8.3 -r 'printf("[%f][%010f]", -INF, -INF);' => [INF][-NF]
         *   php8.4 -r 'printf("[%f][%010f]", -INF, -INF);' => [-INF][-INF]
         * (Not backported: php-8.3.25 still has the old code.)
         */
        /* VERSIONGATE:80402 else-branch is the 5.6 behaviour */
        if (is_negative && php_printf_version() >= 80402) {
            php_sprintf_appendstring(b, "-INF", 4, 0, padding, alignment, 4,
                                     is_negative, 0, always_sign);
        } else {
            php_sprintf_appendstring(b, "INF", 3, 0, padding, alignment, 3,
                                     is_negative, 0, always_sign);
        }
        return;
    }

    switch (fmt) {
        case 'e':
        case 'E':
        case 'f':
        case 'F':
            s_len = php_conv_fp((fmt == 'f') ? 'F' : fmt, number, 0, precision,
                                '.', &is_negative, &num_buf[1]);
            s = &num_buf[1];
            if (is_negative) {
                num_buf[0] = '-';
                s = num_buf;
                s_len++;
            } else if (always_sign) {
                num_buf[0] = '+';
                s = num_buf;
                s_len++;
            }
            break;

        case 'g':
        case 'G':
        case 'h':          /* 8.0+: like %g but never locale aware */
        case 'H':          /* 8.0+: like %G but never locale aware */
        default:
            if (precision == 0) {
                precision = 1;
            }
            php_gcvt_into(number, precision, '.',
                          (fmt == 'G' || fmt == 'H') ? 'E' : 'e',
                          &num_buf[1]);
            s = &num_buf[1];
            is_negative = 0;
            if (*s == '-') {
                is_negative = 1;
            } else if (always_sign) {
                num_buf[0] = '+';
                s = num_buf;
            }
            s_len = strlen(s);
            break;
    }

    php_sprintf_appendstring(b, s, width, 0, padding, alignment, s_len,
                             is_negative, 0, always_sign);
}

/* ------------------------------------------------------------------ */
/* number scanner for argnum / width / precision                        */
/* ------------------------------------------------------------------ */
static int php_sprintf_getnumber(const char *fmt, size_t fmtlen, size_t *pos)
{
    char tmp[32];
    size_t n = 0, copy;
    long num;

    while (*pos + n < fmtlen && fmt[*pos + n] >= '0' && fmt[*pos + n] <= '9') {
        n++;
    }
    copy = (n < sizeof tmp - 1) ? n : sizeof tmp - 1;
    memcpy(tmp, fmt + *pos, copy);
    tmp[copy] = '\0';
    *pos += n;

    errno = 0;
    num = strtol(tmp, NULL, 10);
    if (n >= sizeof tmp - 1 || errno == ERANGE || num >= INT_MAX || num < 0) {
        return -1;
    }
    return (int) num;
}

/* ------------------------------------------------------------------ */
/* php_sprintf                                                          */
/* ------------------------------------------------------------------ */

/*
 * `fmt`/`fmtlen` is the format string, `argv`/`argc` the *substituted*
 * arguments (the format itself is NOT argv[0]).  Returns NULL and sets
 * *err to 1 on a format error; the caller then returns false.
 *
 * VERSION: error reporting is version dependent.  This implementation
 * follows PHP 5.6/7.x -- an E_WARNING plus a `false` return -- and only
 * escalates to the 8.0 exceptions when EG(php_version) says so.  Oracle:
 *
 *   sprintf('%s %s','a')     5.6-7.4  Warning: sprintf(): Too few arguments
 *                                     -> false
 *                            8.0+     ArgumentCountError:
 *                                     "3 arguments are required, 2 given"
 *   sprintf('%0$s','a')      5.6-7.4  Warning: Argument number must be
 *                                     greater than zero -> false
 *                            8.0+     ValueError: "Argument number specifier
 *                                     must be greater than zero and less
 *                                     than 2147483647"
 *   sprintf('%2147483648d',1) 5.6-7.4 Warning: Width must be greater than
 *                                     zero and less than 2147483647
 *                            8.0-8.2  ValueError: same text
 *                            8.3+     ValueError: "Width must be between 0
 *                                     and 2147483647"
 *   sprintf('%y','a')        5.6-7.4  "" (unknown specifiers are skipped)
 *                            8.0+     ValueError: Unknown format specifier "y"
 *   sprintf("%'",'a')        5.6-7.4  ""
 *                            8.0+     ValueError: Missing padding character
 *
 * VERSION: contrary to a common belief, `%e` does NOT differ between 5.6 and
 * 7.0.  Both print printf("%e", 10) as "1.000000e+1"; the whole matrix of 67
 * format/value pairs in the self-test is byte identical on 5.6, 7.0, 7.4 and
 * 8.0.  The only genuine differences in this module are the error paths above.
 */
/* VERSIONGATE:80000 else-branch is the 5.6 behaviour */
static zend_string *php_sprintf_legacy(const char *fname, const char *fmt,
                                       size_t fmtlen, uint32_t argc,
                                       zval *argv, int *err)
{
#define FMT_CH(k) ((k) < fmtlen ? fmt[(size_t) (k)] : '\0')

    php_pbuf b = { NULL, 0, 0, 0 };
    zend_string *result;
    size_t pos = 0;
    int currarg = 0;
    long ver = php_printf_version();

    if (!fname || !*fname) {
        fname = "sprintf";
    }
    if (err) {
        *err = 0;
    }
    pbuf_reserve(&b, 240);

    while (pos < fmtlen) {
        int alignment, adjusting, always_sign, expprec;
        int argnum, width, precision;
        char padding;
        const char *temppos;
        zval *tmp;
        char spec;

        temppos = (const char *) memchr(fmt + pos, '%', fmtlen - pos);
        if (!temppos) {
            pbuf_mem(&b, fmt + pos, fmtlen - pos);
            break;
        }
        if (temppos != fmt + pos) {
            pbuf_mem(&b, fmt + pos, (size_t) (temppos - (fmt + pos)));
            pos = (size_t) (temppos - fmt);
        }
        pos++;                              /* skip the '%' */

        if (FMT_CH(pos) == '%') {
            pbuf_char(&b, '%');
            pos++;
            continue;
        }

        /* starting a new format specifier, reset variables */
        alignment = ALIGN_RIGHT;
        adjusting = 0;
        padding = ' ';
        always_sign = 0;
        expprec = 0;

        if (isalpha((unsigned char) FMT_CH(pos))) {
            width = precision = 0;
            argnum = currarg++;
        } else {
            size_t scan = pos;

            /* first look for argnum */
            while (isdigit((unsigned char) FMT_CH(scan))) {
                scan++;
            }
            if (FMT_CH(scan) == '$') {
                argnum = php_sprintf_getnumber(fmt, fmtlen, &pos);
                if (argnum <= 0) {
                    /* VERSIONGATE:80000 else-branch is the 5.6 behaviour */
                    if (ver >= 80000) {
                        zend_throw_error("ValueError",
                                         "Argument number specifier must be "
                                         "greater than zero and less than %d",
                                         INT_MAX);
                    } else {
                        zend_error(E_WARNING, "%s(): Argument number must be "
                                   "greater than zero", fname);
                    }
                    goto fail;
                }
                argnum--;
                pos++;                      /* skip the '$' */
            } else {
                argnum = currarg++;
            }

            /* after argnum come the modifiers */
            for (;; pos++) {
                char c = FMT_CH(pos);
                if (c == ' ' || c == '0') {
                    padding = c;
                } else if (c == '-') {
                    alignment = ALIGN_LEFT;
                } else if (c == '+') {
                    always_sign = 1;
                } else if (c == '\'') {
                    if (pos + 1 < fmtlen) {
                        pos++;
                        padding = fmt[pos];
                    /* VERSIONGATE:80000 else-branch is the 5.6 behaviour */
                    } else if (ver >= 80000) {
                        zend_throw_error("ValueError", "Missing padding character");
                        goto fail;
                    } else {
                        break;
                    }
                } else {
                    break;
                }
            }

            /* then width */
            if (isdigit((unsigned char) FMT_CH(pos))) {
                width = php_sprintf_getnumber(fmt, fmtlen, &pos);
                if (width < 0) {
                    /* VERSIONGATE:80300 else-branch is the 8.0 wording */
                    if (ver >= 80300) {
                        zend_throw_error("ValueError",
                                         "Width must be between 0 and %d",
                                         INT_MAX);
                    /* VERSIONGATE:80000 else-branch is the 5.6 behaviour */
                    } else if (ver >= 80000) {
                        zend_throw_error("ValueError", "Width must be greater "
                                         "than zero and less than %d", INT_MAX);
                    } else {
                        zend_error(E_WARNING, "%s(): Width must be greater "
                                   "than zero and less than %d", fname,
                                   INT_MAX);
                    }
                    goto fail;
                }
                adjusting |= ADJ_WIDTH;
            } else {
                width = 0;
            }

            /* then precision */
            if (FMT_CH(pos) == '.') {
                pos++;
                if (isdigit((unsigned char) FMT_CH(pos))) {
                    precision = php_sprintf_getnumber(fmt, fmtlen, &pos);
                    if (precision < 0) {
                        /* VERSIONGATE:80000 else-branch is the 5.6 behaviour */
                        if (ver >= 80000) {
                            zend_throw_error("ValueError",
                                             "Precision must be between -1 "
                                             "and %d", INT_MAX);
                        } else {
                            zend_error(E_WARNING, "%s(): Precision must be "
                                       "greater than zero and less than %d",
                                       fname, INT_MAX);
                        }
                        goto fail;
                    }
                    adjusting |= ADJ_PRECISION;
                    expprec = 1;
                } else {
                    precision = 0;
                }
            } else {
                precision = 0;
            }
        }

        if (argnum >= (int) argc) {
            /* VERSIONGATE:80000 else-branch is the 5.6 behaviour */
            if (ver >= 80000) {
                /* 8.0+: the reported count includes the format string. */
                zend_throw_error("ArgumentCountError",
                                 "%d arguments are required, %d given",
                                 argnum + 2, (int) argc + 1);
            } else {
                zend_error(E_WARNING, "%s(): Too few arguments", fname);
            }
            goto fail;
        }

        if (FMT_CH(pos) == 'l') {
            pos++;
        }

        tmp = &argv[argnum];
        spec = FMT_CH(pos);
        switch (spec) {
            case 's': {
                zend_string *str = zval_get_string(tmp);
                php_sprintf_appendstring(&b, ZSTR_VAL(str), (size_t) width,
                                         (size_t) precision, padding, alignment,
                                         ZSTR_LEN(str), 0, expprec, 0);
                break;
            }

            case 'd':
                php_sprintf_appendint(&b, zval_get_long(tmp), (size_t) width,
                                      padding, alignment, always_sign);
                break;

            case 'u':
                php_sprintf_appenduint(&b, (zend_ulong) zval_get_long(tmp),
                                       (size_t) width, padding, alignment);
                break;

            case 'g':
            case 'G':
            case 'e':
            case 'E':
            case 'f':
            case 'F':
                php_sprintf_appenddouble(&b, zval_get_double(tmp),
                                         (size_t) width, padding, alignment,
                                         precision, adjusting, spec,
                                         always_sign, fname);
                break;

            case 'c':
                /* width and padding are deliberately ignored, as in php-src */
                pbuf_char(&b, (char) zval_get_long(tmp));
                break;

            case 'o':
                php_sprintf_append2n(&b, zval_get_long(tmp), (size_t) width,
                                     padding, alignment, 3, hexchars, expprec);
                break;

            case 'x':
                php_sprintf_append2n(&b, zval_get_long(tmp), (size_t) width,
                                     padding, alignment, 4, hexchars, expprec);
                break;

            case 'X':
                php_sprintf_append2n(&b, zval_get_long(tmp), (size_t) width,
                                     padding, alignment, 4, HEXCHARS, expprec);
                break;

            case 'b':
                php_sprintf_append2n(&b, zval_get_long(tmp), (size_t) width,
                                     padding, alignment, 1, hexchars, expprec);
                break;

            case '%':
                pbuf_char(&b, '%');
                break;

            case '\0':
                if (pos >= fmtlen) {
                    /*
                     * VERSION: a format that ends in a bare '%' is silently
                     * dropped up to 7.4 and a ValueError from 8.0 on.
                     * Oracle: sprintf("abc%%", "x")
                     *   7.4 => "abc"
                     *   8.0 => ValueError: Missing format specifier at end
                     *          of string
                     */
                    /* VERSIONGATE:80000 else-branch is the 5.6 behaviour */
                    if (ver >= 80000) {
                        zend_throw_error("ValueError",
                                         "Missing format specifier at end of "
                                         "string");
                        goto fail;
                    }
                    goto done;
                }
                break;

            default:
                /* 5.6-7.4 silently skip unknown specifiers; 8.0+ throws
                 * ValueError: Unknown format specifier "y". */
                /* VERSIONGATE:80000 else-branch is the 5.6 behaviour */
                if (ver >= 80000) {
                    zend_throw_error("ValueError",
                                     "Unknown format specifier \"%c\"", spec);
                    goto fail;
                }
                break;
        }
        pos++;
    }

done:
    if (b.oom) {
        goto fail;
    }
    result = zend_string_init(b.data ? b.data : "", b.len);
    free(b.data);
    return result;

fail:
    free(b.data);
    if (err) {
        *err = 1;
    }
    return NULL;

#undef FMT_CH
}


/* ------------------------------------------------------------------ */
/* PHP 8.0 parser                                                       */
/* ------------------------------------------------------------------ */
/*
 * VERSIONGATE:80000
 * PHP 8.0 rewrote php_formatted_print().  Three things changed that the
 * pre-8.0 loop above cannot express, so the 8.0+ grammar gets its own
 * implementation and the 5.6 snapshot keeps only php_sprintf_legacy():
 *
 *   1. `%*d` / `%.*f` take the width / precision from an argument, and the
 *      argument-number prefix is parsed *before* them, so `%1$*2$d` works.
 *   2. a missing argument no longer aborts at the first offender; the parser
 *      records the highest missing argument number, keeps going, and reports
 *      it once at the end.  Oracle (php8.4):
 *        sprintf('%s %s %s','a')
 *            => ArgumentCountError: 4 arguments are required, 2 given
 *      whereas the pre-8.0 warning fires on the first one.
 *   3. `%h` / `%H` (locale-independent `%g` / `%G`) exist, and a precision of
 *      -1 is accepted for exactly those four.
 */
#define ARG_NUM_NEXT    (-1)
#define ARG_NUM_INVALID (-2)

static int php_sprintf_get_argnum(const char *fmt, size_t fmtlen, size_t *pos)
{
    size_t scan = *pos;
    int argnum;

    while (scan < fmtlen && fmt[scan] >= '0' && fmt[scan] <= '9') {
        scan++;
    }
    if (scan >= fmtlen || fmt[scan] != '$') {
        return ARG_NUM_NEXT;
    }
    argnum = php_sprintf_getnumber(fmt, fmtlen, pos);
    if (argnum <= 0 || argnum == INT_MAX) {
        zend_throw_error("ValueError", "Argument number specifier must be "
                         "greater than zero and less than %d", INT_MAX);
        return ARG_NUM_INVALID;
    }
    (*pos)++;                               /* skip the '$' */
    return argnum - 1;
}

static zend_string *php_sprintf_modern(const char *fname, const char *fmt,
                                       size_t fmtlen, uint32_t argc,
                                       zval *argv, int *err)
{
#define FMT_CH(k) ((k) < fmtlen ? fmt[(size_t) (k)] : '\0')

    php_pbuf b = { NULL, 0, 0, 0 };
    zend_string *result;
    size_t pos = 0;
    int currarg = 0;
    int max_missing_argnum = -1;
    long ver = php_printf_version();

    if (!fname || !*fname) {
        fname = "sprintf";
    }
    if (err) {
        *err = 0;
    }
    pbuf_reserve(&b, 240);

    while (pos < fmtlen) {
        int alignment, adjusting, always_sign, expprec;
        int argnum, width, precision;
        char padding;
        const char *temppos;
        zval *tmp;
        char spec;

        temppos = (const char *) memchr(fmt + pos, '%', fmtlen - pos);
        if (!temppos) {
            pbuf_mem(&b, fmt + pos, fmtlen - pos);
            break;
        }
        if (temppos != fmt + pos) {
            pbuf_mem(&b, fmt + pos, (size_t) (temppos - (fmt + pos)));
            pos = (size_t) (temppos - fmt);
        }
        pos++;                              /* skip the '%' */

        if (FMT_CH(pos) == '%') {
            pbuf_char(&b, '%');
            pos++;
            continue;
        }

        alignment = ALIGN_RIGHT;
        adjusting = 0;
        padding = ' ';
        always_sign = 0;
        expprec = 0;

        if (isalpha((unsigned char) FMT_CH(pos))) {
            width = precision = 0;
            argnum = ARG_NUM_NEXT;
        } else {
            argnum = php_sprintf_get_argnum(fmt, fmtlen, &pos);
            if (argnum == ARG_NUM_INVALID) {
                goto fail;
            }

            for (;; pos++) {
                char c = FMT_CH(pos);
                if (c == ' ' || c == '0') {
                    padding = c;
                } else if (c == '-') {
                    alignment = ALIGN_LEFT;
                } else if (c == '+') {
                    always_sign = 1;
                } else if (c == '\'') {
                    if (pos + 1 < fmtlen) {
                        pos++;
                        padding = fmt[pos];
                    } else {
                        zend_throw_error("ValueError",
                                         "Missing padding character");
                        goto fail;
                    }
                } else {
                    break;
                }
            }

            /* width, possibly taken from an argument */
            if (FMT_CH(pos) == '*') {
                int width_argnum;

                pos++;
                width_argnum = php_sprintf_get_argnum(fmt, fmtlen, &pos);
                if (width_argnum == ARG_NUM_INVALID) {
                    goto fail;
                }
                if (width_argnum == ARG_NUM_NEXT) {
                    width_argnum = currarg++;
                }
                if (width_argnum >= (int) argc) {
                    if (width_argnum > max_missing_argnum) {
                        max_missing_argnum = width_argnum;
                    }
                    continue;           /* deliberately no pos++ */
                }
                tmp = zval_deref(&argv[width_argnum]);
                if (Z_TYPE_P(tmp) != IS_LONG) {
                    zend_throw_error("ValueError", "Width must be an integer");
                    goto fail;
                }
                if (Z_LVAL_P(tmp) < 0 || Z_LVAL_P(tmp) > INT_MAX) {
                    zend_throw_error("ValueError", "Width must be greater than "
                                     "zero and less than %d", INT_MAX);
                    goto fail;
                }
                width = (int) Z_LVAL_P(tmp);
                adjusting |= ADJ_WIDTH;
            } else if (isdigit((unsigned char) FMT_CH(pos))) {
                width = php_sprintf_getnumber(fmt, fmtlen, &pos);
                if (width < 0) {
                    /* VERSIONGATE:80300 else-branch is the 8.0 wording */
                    if (ver >= 80300) {
                        zend_throw_error("ValueError",
                                         "Width must be between 0 and %d",
                                         INT_MAX);
                    } else {
                        zend_throw_error("ValueError", "Width must be greater "
                                         "than zero and less than %d", INT_MAX);
                    }
                    goto fail;
                }
                adjusting |= ADJ_WIDTH;
            } else {
                width = 0;
            }

            /* precision, possibly taken from an argument */
            if (FMT_CH(pos) == '.') {
                pos++;
                if (FMT_CH(pos) == '*') {
                    int prec_argnum;

                    pos++;
                    prec_argnum = php_sprintf_get_argnum(fmt, fmtlen, &pos);
                    if (prec_argnum == ARG_NUM_INVALID) {
                        goto fail;
                    }
                    if (prec_argnum == ARG_NUM_NEXT) {
                        prec_argnum = currarg++;
                    }
                    if (prec_argnum >= (int) argc) {
                        if (prec_argnum > max_missing_argnum) {
                            max_missing_argnum = prec_argnum;
                        }
                        continue;       /* deliberately no pos++ */
                    }
                    tmp = zval_deref(&argv[prec_argnum]);
                    if (Z_TYPE_P(tmp) != IS_LONG) {
                        zend_throw_error("ValueError",
                                         "Precision must be an integer");
                        goto fail;
                    }
                    if (Z_LVAL_P(tmp) < -1 || Z_LVAL_P(tmp) > INT_MAX) {
                        zend_throw_error("ValueError",
                                         "Precision must be between -1 and %d",
                                         INT_MAX);
                        goto fail;
                    }
                    precision = (int) Z_LVAL_P(tmp);
                    adjusting |= ADJ_PRECISION;
                    expprec = 1;
                } else if (isdigit((unsigned char) FMT_CH(pos))) {
                    precision = php_sprintf_getnumber(fmt, fmtlen, &pos);
                    if (precision < 0) {
                        zend_throw_error("ValueError",
                                         "Precision must be between -1 and %d",
                                         INT_MAX);
                        goto fail;
                    }
                    adjusting |= ADJ_PRECISION;
                    expprec = 1;
                } else {
                    precision = 0;
                }
            } else {
                precision = 0;
            }
        }

        if (FMT_CH(pos) == 'l') {
            pos++;
        }

        if (argnum == ARG_NUM_NEXT) {
            argnum = currarg++;
        }
        if (argnum >= (int) argc) {
            if (argnum > max_missing_argnum) {
                max_missing_argnum = argnum;
            }
            continue;                   /* deliberately no pos++ */
        }

        spec = FMT_CH(pos);
        if (expprec && precision == -1 && spec != 'g' && spec != 'G' &&
                spec != 'h' && spec != 'H') {
            zend_throw_error("ValueError", "Precision -1 is only supported for "
                             "%%g, %%G, %%h and %%H");
            goto fail;
        }

        tmp = &argv[argnum];
        switch (spec) {
            case 's': {
                zend_string *str = zval_get_string(tmp);
                php_sprintf_appendstring(&b, ZSTR_VAL(str), (size_t) width,
                                         (size_t) precision, padding, alignment,
                                         ZSTR_LEN(str), 0, expprec, 0);
                break;
            }

            case 'd':
                php_sprintf_appendint(&b, zval_get_long(tmp), (size_t) width,
                                      padding, alignment, always_sign);
                break;

            case 'u':
                php_sprintf_appenduint(&b, (zend_ulong) zval_get_long(tmp),
                                       (size_t) width, padding, alignment);
                break;

            case 'g':
            case 'G':
            case 'h':
            case 'H':
            case 'e':
            case 'E':
            case 'f':
            case 'F':
                php_sprintf_appenddouble(&b, zval_get_double(tmp),
                                         (size_t) width, padding, alignment,
                                         precision, adjusting, spec,
                                         always_sign, fname);
                break;

            case 'c':
                pbuf_char(&b, (char) zval_get_long(tmp));
                break;

            case 'o':
                php_sprintf_append2n(&b, zval_get_long(tmp), (size_t) width,
                                     padding, alignment, 3, hexchars, expprec);
                break;

            case 'x':
                php_sprintf_append2n(&b, zval_get_long(tmp), (size_t) width,
                                     padding, alignment, 4, hexchars, expprec);
                break;

            case 'X':
                php_sprintf_append2n(&b, zval_get_long(tmp), (size_t) width,
                                     padding, alignment, 4, HEXCHARS, expprec);
                break;

            case 'b':
                php_sprintf_append2n(&b, zval_get_long(tmp), (size_t) width,
                                     padding, alignment, 1, hexchars, expprec);
                break;

            case '%':
                pbuf_char(&b, '%');
                break;

            case '\0':
                if (pos >= fmtlen) {
                    zend_throw_error("ValueError",
                                     "Missing format specifier at end of "
                                     "string");
                    goto fail;
                }
                zend_throw_error("ValueError",
                                 "Unknown format specifier \"%c\"", spec);
                goto fail;

            default:
                zend_throw_error("ValueError",
                                 "Unknown format specifier \"%c\"", spec);
                goto fail;
        }
        pos++;
    }

    if (max_missing_argnum >= 0) {
        /* the reported counts include the format string itself */
        zend_throw_error("ArgumentCountError",
                         "%d arguments are required, %d given",
                         max_missing_argnum + 2, (int) argc + 1);
        goto fail;
    }

    if (b.oom) {
        goto fail;
    }
    result = zend_string_init(b.data ? b.data : "", b.len);
    free(b.data);
    return result;

fail:
    free(b.data);
    if (err) {
        *err = 1;
    }
    return NULL;

#undef FMT_CH
}

/*
 * The signature declared in zend_API.h.  php-src names the *calling* function
 * in every diagnostic ("printf(): Too few arguments", "vsprintf(): Too few
 * arguments", ...), which this entry point cannot know, so it defaults to
 * "sprintf".  Callers that know better -- ext/standard's printf family, which
 * already threads an `fname` through ps_format() -- should call
 * php_sprintf_named() instead.  Oracle:
 *   php5.6 -r 'printf("%s%s","a");'
 *       => Warning: printf(): Too few arguments
 *   php5.6 -r 'vsprintf("%s%s",array("a"));'
 *       => Warning: vsprintf(): Too few arguments
 */
zend_string *php_sprintf_named(const char *fname, const char *fmt,
                               size_t fmtlen, uint32_t argc, zval *argv,
                               int *err)
{
    /* VERSIONGATE:80000 else-branch is the 5.6 behaviour */
    if (php_printf_version() >= 80000) {
        return php_sprintf_modern(fname, fmt, fmtlen, argc, argv, err);
    }
    return php_sprintf_legacy(fname, fmt, fmtlen, argc, argv, err);
}

zend_string *php_sprintf(const char *fmt, size_t fmtlen, uint32_t argc,
                         zval *argv, int *err)
{
    return php_sprintf_named("sprintf", fmt, fmtlen, argc, argv, err);
}

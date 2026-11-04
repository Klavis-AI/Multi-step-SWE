/* ext/standard/php_math.c -- math builtins.
 *
 * Re-implementation of php-src's ext/standard/math.c, ext/standard/rand.c and
 * ext/standard/mt_rand.c (ext/random/engine_mt19937.c from 8.2) against the
 * frozen zend.h / zend_API.h / zend_ast.h contract.  Every behavioural fork is
 * keyed off EG(php_version) and carries a VERSION marker comment.
 */

/* random()/srandom() are POSIX; -std=c11 alone hides them from <stdlib.h>. */
#define _DEFAULT_SOURCE 1

#include "zend.h"
#include "zend_ast.h"
#include "zend_API.h"
#include "php_rand.h"

#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define PHP_ROUND_HALF_UP        0x01
#define PHP_ROUND_HALF_DOWN      0x02
#define PHP_ROUND_HALF_EVEN      0x03
#define PHP_ROUND_HALF_ODD       0x04
/* 8.4 RoundingMode additions */
#define PHP_ROUND_TOWARD_ZERO    0x05
#define PHP_ROUND_AWAY_FROM_ZERO 0x06
#define PHP_ROUND_CEILING        0x07
#define PHP_ROUND_FLOOR          0x08
/* ------------------------------------------------------------------ errors */

/* The gettype()-style spelling php-src names the offending value with. */
static const char *math_word_given(const zval *v)
{

    switch (Z_TYPE_P(v)) {
    case IS_UNDEF: case IS_NULL:  return "null";
    case IS_FALSE: case IS_TRUE:  return "boolean";
    case IS_LONG:                 return "integer";
    case IS_DOUBLE:               return "double";
    case IS_STRING:               return "string";
    case IS_ARRAY:                return "array";
    case IS_OBJECT:               return "object";
    default:                      return "unknown";
    }
}

/* ZPP's word for an int parameter: "long" (5.6), "integer" (7.0), "int" (7.3). */
static const char *math_word_int(void)
{


    return "long";
}

/* ZPP's word for a float parameter: "double" (5.6), "float" (7.0). */
static const char *math_word_float(void)
{

    return "double";
}

/* php-src raises a TypeError from 8.0 and a ZPP warning + NULL before that. */
static void math_type_error(const char *fname, int argnum, const char *pname,
                            const char *expect, const zval *v)
{

        zend_error(E_WARNING, "%s() expects parameter %d to be %s, %s given",
                   fname, argnum, expect, math_word_given(v));

}

/* base_convert()'s out-of-range base: a warning plus false below 8.0 and a
 * ValueError from 8.0.  Returns 1 when a throw happened (leave NULL). */
static int math_base_range_error(const char *fname, int argnum, const char *pname,
                                 const char *legacy_label, zend_long base)
{
    char buf[32];

    snprintf(buf, sizeof(buf), "%lld", (long long) base);

    zend_error(E_WARNING, "%s(): Invalid %s (%s)", fname, legacy_label, buf);
    return 0;
}

/* php-src's is_numeric_string(allow_errors = -1) reports the trailing data
 * before using the numeric prefix. */
static void math_non_well_formed(void)
{

        zend_error(E_NOTICE, "A non well formed numeric value encountered");

}

/* ------------------------------------------------------- argument fetching */
/* A re-implementation of the zend_parse_parameters() specs that
 * ext/standard/math.c and ext/standard/rand.c actually use: "d", "l", "Z" and
 * "z/".  php-src performs the arity check inside the function (in ZPP), which
 * is why a wrong count is a warning plus a NULL return rather than an aborted
 * call. */

typedef struct {
    int        is_double;
    zend_long  lval;
    double     dval;
} math_number;

/* php-src ZEND_NUM_ARGS() checking.  Returns 0 when the call must be aborted;
 * zend_wrong_param_count() already picked the 5.6 warning or the 8.0
 * ArgumentCountError. */
static int math_arity(uint32_t argc, uint32_t min, uint32_t max, const char *fname)
{
    if (argc < min || (max != (uint32_t) -1 && argc > max)) {
        zend_wrong_param_count(fname, argc, min, max);
        return 0;
    }
    return 1;
}

/* php-src 5.6's is_numeric_string_ex() also accepted a hexadecimal spelling;
 * PHP 7.0 removed it (RFC "Remove hex support in numeric strings"), which is
 * why is_numeric("0x1A") is true on 5.6 and false from 7.0.  The engine's
 * shared is_numeric_string() in Zend/zend_operators.c carries no such branch,
 * so the 5.6 ZPP path recognises the spelling here.
 *
 * Grammar, exactly as 5.6 had it: optional leading whitespace, "0x" or "0X",
 * one or more hex digits, and no sign ("-0x10" is not numeric).  Returns 0,
 * IS_LONG or IS_DOUBLE; *trailing is set when the string did not end there. */
static int math_hex_numeric_string(const zend_string *s, zend_long *lv,
                                    double *dv, int *trailing)
{
    const char *p = ZSTR_VAL(s);
    const char *end = p + ZSTR_LEN(s);
    const char *digits;
    unsigned long long acc = 0;
    double dacc = 0.0;
    int overflow = 0;


    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' ||
                       *p == '\r' || *p == '\v' || *p == '\f')) {
        p++;
    }
    if (end - p < 3 || p[0] != '0' || (p[1] != 'x' && p[1] != 'X')) {
        return 0;
    }
    p += 2;
    digits = p;
    while (p < end) {
        int d;

        if (*p >= '0' && *p <= '9')      d = *p - '0';
        else if (*p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
        else break;
        if (!overflow) {
            if (acc > (0xFFFFFFFFFFFFFFFFULL - (unsigned long long) d) / 16ULL) {
                overflow = 1;
                dacc = (double) acc;
            } else {
                acc = acc * 16ULL + (unsigned long long) d;
            }
        }
        if (overflow) {
            dacc = dacc * 16.0 + (double) d;
        }
        p++;
    }
    if (p == digits) {
        return 0;                       /* "0x" with no digits */
    }
    if (trailing) {
        *trailing = (p != end);
    }
    if (overflow || acc > (unsigned long long) ZEND_LONG_MAX) {
        if (dv) *dv = overflow ? dacc : (double) acc;
        return IS_DOUBLE;
    }
    if (lv) *lv = (zend_long) acc;
    return IS_LONG;
}

/* Shared string -> number step of the "d" and "l" specs. */
static int math_numeric_string(const zend_string *s, zend_long *lv, double *dv, int *kind)
{
    int strict = is_numeric_string(ZSTR_VAL(s), ZSTR_LEN(s), lv, dv, 0);
    int trailing = 0;
    int hex;

    if (strict) {
        *kind = strict;
        return 1;
    }
    hex = math_hex_numeric_string(s, lv, dv, &trailing);
    if (hex) {
        if (trailing) {
            math_non_well_formed();
        }
        *kind = hex;
        return 1;
    }
    strict = is_numeric_string(ZSTR_VAL(s), ZSTR_LEN(s), lv, dv, 1);
    if (strict) {
        math_non_well_formed();
        *kind = strict;
        return 1;
    }
    return 0;
}

/* php-src ZPP "d" */
static int math_zpp_double(zval *in, double *out, int argnum,
                           const char *fname, const char *pname)
{
    zval *v = zval_deref(in);

    switch (Z_TYPE_P(v)) {
    case IS_DOUBLE: *out = Z_DVAL_P(v);          return 1;
    case IS_LONG:   *out = (double) Z_LVAL_P(v); return 1;
    case IS_UNDEF: case IS_NULL: case IS_FALSE: *out = 0.0; return 1;
    case IS_TRUE:   *out = 1.0;                  return 1;
    case IS_STRING: {
        zend_long lv = 0;
        double dv = 0;
        int kind = 0;

        if (math_numeric_string(Z_STR_P(v), &lv, &dv, &kind)) {
            *out = (kind == IS_LONG) ? (double) lv : dv;
            return 1;
        }
        break;
    }
    default:
        break;
    }
    math_type_error(fname, argnum, pname, math_word_float(), v);
    return 0;
}

/* php-src ZPP "l" */
static int math_zpp_long(zval *in, zend_long *out, int argnum,
                         const char *fname, const char *pname)
{
    zval *v = zval_deref(in);

    switch (Z_TYPE_P(v)) {
    case IS_LONG:   *out = Z_LVAL_P(v);             return 1;
    case IS_DOUBLE: *out = (zend_long) Z_DVAL_P(v); return 1;
    case IS_UNDEF: case IS_NULL: case IS_FALSE: *out = 0; return 1;
    case IS_TRUE:   *out = 1;                       return 1;
    case IS_STRING: {
        zend_long lv = 0;
        double dv = 0;
        int kind = 0;

        if (math_numeric_string(Z_STR_P(v), &lv, &dv, &kind)) {
            *out = (kind == IS_LONG) ? lv : (zend_long) dv;
            return 1;
        }
        break;
    }
    default:
        break;
    }
    math_type_error(fname, argnum, pname, math_word_int(), v);
    return 0;
}

/* php-src convert_scalar_to_number() behind a "Z" parameter.  Returns 0 for an
 * array, which abs()/ceil()/floor()/round() report as false without a
 * diagnostic. */
static int math_zpp_number(zval *in, math_number *out)
{
    zval *v = zval_deref(in);

    switch (Z_TYPE_P(v)) {
    case IS_LONG:   out->is_double = 0; out->lval = Z_LVAL_P(v); return 1;
    case IS_DOUBLE: out->is_double = 1; out->dval = Z_DVAL_P(v); return 1;
    case IS_UNDEF: case IS_NULL: case IS_FALSE:
        out->is_double = 0; out->lval = 0; return 1;
    case IS_TRUE:
        out->is_double = 0; out->lval = 1; return 1;
    case IS_STRING: {
        zend_long lv = 0;
        double dv = 0;
        /* Strict first, then the 5.6 hex spelling, then allow_errors = 1:
         * a leading-numeric string is taken silently and a fully non-numeric
         * one becomes 0.  The hex probe has to precede the loose parse,
         * because "0x1A" also parses loosely as the long 0. */
        int t = is_numeric_string(ZSTR_VAL(Z_STR_P(v)), ZSTR_LEN(Z_STR_P(v)), &lv, &dv, 0);

        if (t == 0) {
            t = math_hex_numeric_string(Z_STR_P(v), &lv, &dv, NULL);
        }
        if (t == 0) {
            t = is_numeric_string(ZSTR_VAL(Z_STR_P(v)), ZSTR_LEN(Z_STR_P(v)), &lv, &dv, 1);
        }
        if (t == IS_LONG)   { out->is_double = 0; out->lval = lv; return 1; }
        if (t == IS_DOUBLE) { out->is_double = 1; out->dval = dv; return 1; }
        out->is_double = 0; out->lval = 0;
        return 1;
    }
    case IS_OBJECT:
        /* zval_get_long() emits "Object of class X could not be converted" */
        out->is_double = 0;
        out->lval = zval_get_long(v);
        return 1;
    default:
        return 0;
    }
}
/* ==================================================================== */
/*                          Mersenne Twister                            */
/* ==================================================================== */

#define MT_N  PHP_MT_N
#define MT_M  397

#define hiBit(u)      ((u) & 0x80000000U)
#define loBit(u)      ((u) & 0x00000001U)
#define loBits(u)     ((u) & 0x7FFFFFFFU)
#define mixBits(u, v) (hiBit(u) | loBits(v))

/* php-src <= 7.0 twisted on loBit(u) -- a transcription bug kept for BC as
 * MT_RAND_PHP; 7.1 fixed it to loBit(v) for MT_RAND_MT19937, which is why
 * mt_srand(42); mt_rand() is 1354439493 on 5.6/7.0 and 804318771 later.  The
 * run-time selection lives in php_mt_reload(). */
#define mt_twist(m, u, v) \
    ((m) ^ (mixBits(u, v) >> 1) ^ ((uint32_t) (-(int32_t) (loBit(v))) & 0x9908b0dfU))
#define mt_twist_php(m, u, v) \
    ((m) ^ (mixBits(u, v) >> 1) ^ ((uint32_t) (-(int32_t) (loBit(u))) & 0x9908b0dfU))

static uint32_t  mt_state[MT_N];
static uint32_t *mt_next;
static uint32_t  mt_left;
static int       mt_is_seeded;
static int       mt_mode = MT_RAND_MT19937;

/* php-src's system rand()/srand() are glibc random()/srandom() before 7.1; we
 * call straight through to libc so seeded sequences match the oracle. */
static int sys_rand_is_seeded;

static void php_mt_initialize(uint32_t seed, uint32_t *state)
{
    uint32_t *s = state;
    uint32_t *r = state;
    int i = 1;

    *s++ = seed & 0xffffffffU;
    for (; i < MT_N; ++i) {
        *s++ = (1812433253U * (*r ^ (*r >> 30)) + (uint32_t) i) & 0xffffffffU;
        r++;
    }
}

static void php_mt_reload(void)
{
    uint32_t *state = mt_state;
    uint32_t *p = state;
    int i;

    /* The MT19937-correct twist only exists from 7.1. */

        for (i = MT_N - MT_M; i--; ++p)
            *p = mt_twist_php(p[MT_M], p[0], p[1]);
        for (i = MT_M; --i; ++p)
            *p = mt_twist_php(p[MT_M - MT_N], p[0], p[1]);
        *p = mt_twist_php(p[MT_M - MT_N], p[0], state[0]);

    mt_left = MT_N;
    mt_next = state;
}

void php_mt_srand(uint32_t seed)
{
    php_mt_initialize(seed, mt_state);
    php_mt_reload();
    mt_is_seeded = 1;
}

void php_mt_set_mode(int mode)
{
    mt_mode = (mode == MT_RAND_PHP) ? MT_RAND_PHP : MT_RAND_MT19937;
}

int php_mt_get_mode(void)
{
    return mt_mode;
}

/* php-src's GENERATE_SEED(): no test can depend on the value, only that it
 * differs between runs. */
static uint32_t math_generate_seed(void)
{
    static uint32_t counter = 0;
    uintptr_t p = (uintptr_t) &counter;
    uint32_t v = 0;
    FILE *f;

    counter++;
    f = fopen("/dev/urandom", "rb");
    if (f) {
        size_t got = fread(&v, 1, sizeof(v), f);
        fclose(f);
        if (got == sizeof(v)) {
            return v ^ counter;
        }
    }
    return ((uint32_t) (p >> 3) ^ (uint32_t) p) ^ (counter * 2654435761U);
}

uint32_t php_mt_rand(void)
{
    uint32_t s1;

    if (!mt_is_seeded) {
        php_mt_srand(math_generate_seed());
    }
    if (mt_left == 0) {
        php_mt_reload();
    }
    --mt_left;

    s1 = *mt_next++;
    s1 ^= (s1 >> 11);
    s1 ^= (s1 << 7) & 0x9d2c5680U;
    s1 ^= (s1 << 15) & 0xefc60000U;
    return s1 ^ (s1 >> 18);
}

static uint32_t mt_range32(uint32_t umax)
{
    uint32_t result, limit;

    result = php_mt_rand();

    if (umax == UINT32_MAX) {
        return result;
    }
    umax++;
    if ((umax & (umax - 1)) == 0) {
        return result & (umax - 1);
    }
    limit = UINT32_MAX - (UINT32_MAX % umax) - 1;
    while (result > limit) {
        result = php_mt_rand();
    }
    return result % umax;
}

static uint64_t mt_draw64(void)
{
    uint64_t result;

    /* <= 8.1 built the 64-bit draw high word first; ext/random in 8.2 rebuilt
     * it low word first, which changes every seeded 64-bit range
     * (mt_srand(42); mt_rand(0, PHP_INT_MAX) differs between 8.1 and 8.2). */

        result = (uint64_t) php_mt_rand();
        result = (result << 32) | (uint64_t) php_mt_rand();

    return result;
}

static uint64_t mt_range64(uint64_t umax)
{
    uint64_t result, limit;

    result = mt_draw64();

    if (umax == UINT64_MAX) {
        return result;
    }
    umax++;
    if ((umax & (umax - 1)) == 0) {
        return result & (umax - 1);
    }
    limit = UINT64_MAX - (UINT64_MAX % umax) - 1;
    while (result > limit) {
        result = mt_draw64();
    }
    return result % umax;
}

zend_long php_mt_rand_range(zend_long min, zend_long max)
{
    zend_ulong umax = (zend_ulong) max - (zend_ulong) min;

    if (umax > UINT32_MAX) {
        return (zend_long) (mt_range64(umax) + (zend_ulong) min);
    }
    return (zend_long) ((zend_ulong) mt_range32((uint32_t) umax) + (zend_ulong) min);
}

/* php-src RAND_RANGE_BADSCALING */
static zend_long mt_bad_scaling(zend_long n, zend_long min, zend_long max,
                                zend_long tmax)
{
    return min + (zend_long) ((double) ((double) max - (double) min + 1.0) *
                              ((double) n / ((double) tmax + 1.0)));
}

zend_long php_mt_rand_common(zend_long min, zend_long max)
{
    zend_long n;

    if (mt_mode == MT_RAND_MT19937) {
        return php_mt_rand_range(min, max);
    }
    /* legacy mode, deliberately outside php_mt_rand_range() */
    n = (zend_long) (php_mt_rand() >> 1);
    return mt_bad_scaling(n, min, max, PHP_MT_RAND_MAX);
}

/* ==================================================================== */
/*                              round()                                 */
/* ==================================================================== */

/* floor(log10(fabs(val))) via php-src's 5-step binary search. */
static int php_intlog10abs(double value)
{
    int result;

    value = fabs(value);

    if (value < 1e-8 || value > 1e22) {
        result = (int) floor(log10(value));
    } else {
        static const double values[] = {
            1e-8, 1e-7, 1e-6, 1e-5, 1e-4, 1e-3, 1e-2, 1e-1,
            1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,
            1e8,  1e9,  1e10, 1e11, 1e12, 1e13, 1e14, 1e15,
            1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22 };

        result = 15;
        if (value < values[result]) result -= 8; else result += 8;
        if (value < values[result]) result -= 4; else result += 4;
        if (value < values[result]) result -= 2; else result += 2;
        if (value < values[result]) result -= 1; else result += 1;
        if (value < values[result]) result -= 1;
        result -= 8;
    }
    return result;
}

static double php_intpow10(int power)
{
    static const double powers[] = {
        1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,
        1e8,  1e9,  1e10, 1e11, 1e12, 1e13, 1e14, 1e15,
        1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22 };

    if (power < 0 || power > 22) {
        return pow(10.0, (double) power);
    }
    return powers[power];
}

/* php-src php_round_helper() as it stood from 4.3 through 8.3. */
static double php_round_helper_legacy(double value, int mode)
{
    double tmp_value;

    if (value >= 0.0) {
        tmp_value = floor(value + 0.5);
        if ((mode == PHP_ROUND_HALF_DOWN && value == (-0.5 + tmp_value)) ||
            (mode == PHP_ROUND_HALF_EVEN && value == (0.5 + 2 * floor(tmp_value / 2.0))) ||
            (mode == PHP_ROUND_HALF_ODD  && value == (0.5 + 2 * floor(tmp_value / 2.0) - 1.0))) {
            tmp_value = tmp_value - 1.0;
        }
    } else {
        tmp_value = ceil(value - 0.5);
        if ((mode == PHP_ROUND_HALF_DOWN && value == (0.5 + tmp_value)) ||
            (mode == PHP_ROUND_HALF_EVEN && value == (-0.5 + 2 * ceil(tmp_value / 2.0))) ||
            (mode == PHP_ROUND_HALF_ODD  && value == (-0.5 + 2 * ceil(tmp_value / 2.0) + 1.0))) {
            tmp_value = tmp_value + 1.0;
        }
    }
    return tmp_value;
}

/* The pre-rounding algorithm (http://wiki.php.net/rfc/rounding), 5.3 .. 8.3. */
static double php_math_round_legacy(double value, int places, int mode)
{
    double f1, f2;
    double tmp_value;
    int precision_places;
    int pre_round;

    if (!isfinite(value)) {
        return value;
    }
    /* The "value == 0.0" short circuit (which preserves -0.0) only exists from
     * 7.4; 5.6 .. 7.3 turn round(-0.0, 2) into +0.0. */

        /* 5.6: -0.0 falls through the algorithm and comes back as +0.0 */


    places = places < INT_MIN + 1 ? INT_MIN + 1 : places;
    precision_places = 14 - php_intlog10abs(value);

    f1 = php_intpow10(abs(places));

    /* If the decimal precision guaranteed by FP arithmetic is higher than the
     * requested places BUT is small enough to make sure a non-zero value is
     * returned, pre-round the result to that precision.
     *
     * 5.6 .. 7.3 wrote "precision_places - places < 15", which can overflow;
     * 7.4 rewrote it as "precision_places - 15 < places". */

        pre_round = precision_places > places && precision_places - places < 15;


    if (pre_round) {
        int64_t use_precision = precision_places < INT_MIN + 1 ? INT_MIN + 1 : precision_places;

        f2 = php_intpow10(abs((int) use_precision));
        if (use_precision >= 0) {
            tmp_value = value * f2;
        } else {
            tmp_value = value / f2;
        }
        /* pre-round: tmp_value is always something * 1e14, never above 1e15 */
        tmp_value = php_round_helper_legacy(tmp_value, mode);

        use_precision = (int64_t) places - (int64_t) precision_places;
        use_precision = use_precision < INT_MIN + 1 ? INT_MIN + 1 : use_precision;
        /* move the decimal point back; places < precision_places */
        f2 = php_intpow10(abs((int) use_precision));
        tmp_value = tmp_value / f2;
    } else {
        if (places >= 0) {
            tmp_value = value * f1;
        } else {
            tmp_value = value / f1;
        }
        /* beyond our precision: rounding it is pointless */
        if (fabs(tmp_value) >= 1e15) {
            return value;
        }
    }

    tmp_value = php_round_helper_legacy(tmp_value, mode);

    if (abs(places) < 23) {
        if (places > 0) {
            tmp_value = tmp_value / f1;
        } else {
            tmp_value = tmp_value * f1;
        }
    } else {
        /* simple division would be wrong here: round-trip through a string so
         * strtod() picks the nearest representable value */
        char buf[40];

        snprintf(buf, 39, "%15fe%d", tmp_value, -places);
        buf[39] = '\0';
        tmp_value = strtod(buf, NULL);
        if (!isfinite(tmp_value) || isnan(tmp_value)) {
            tmp_value = value;
        }
    }
    return tmp_value;
}

/* ---- 8.4's rewritten rounding (RFC "Change the edge case of round()") ---- */

static double round_basic_edge(double integral, double exponent, int places)
{
    return (places > 0)
        ? fabs((integral + copysign(0.5, integral)) / exponent)
        : fabs((integral + copysign(0.5, integral)) * exponent);
}

static double round_zero_edge(double integral, double exponent, int places)
{
    return (places > 0) ? fabs(integral / exponent) : fabs(integral * exponent);
}

static double php_round_helper_84(double integral, double value, double exponent,
                                  int places, int mode)
{
    double value_abs = fabs(value);
    double edge_case;

    switch (mode) {
    case PHP_ROUND_HALF_UP:
        edge_case = round_basic_edge(integral, exponent, places);
        if (value_abs >= edge_case) return integral + copysign(1.0, integral);
        return integral;

    case PHP_ROUND_HALF_DOWN:
        edge_case = round_basic_edge(integral, exponent, places);
        if (value_abs > edge_case) return integral + copysign(1.0, integral);
        return integral;

    case PHP_ROUND_CEILING:
        edge_case = round_zero_edge(integral, exponent, places);
        if (value > 0.0 && value_abs > edge_case) return integral + 1.0;
        return integral;

    case PHP_ROUND_FLOOR:
        edge_case = round_zero_edge(integral, exponent, places);
        if (value < 0.0 && value_abs > edge_case) return integral - 1.0;
        return integral;

    case PHP_ROUND_TOWARD_ZERO:
        return integral;

    case PHP_ROUND_AWAY_FROM_ZERO:
        edge_case = round_zero_edge(integral, exponent, places);
        if (value_abs > edge_case) return integral + copysign(1.0, integral);
        return integral;

    case PHP_ROUND_HALF_EVEN:
        edge_case = round_basic_edge(integral, exponent, places);
        if (value_abs > edge_case) {
            return integral + copysign(1.0, integral);
        } else if (value_abs == edge_case && fmod(integral, 2.0) != 0.0) {
            return integral + copysign(1.0, integral);
        }
        return integral;

    case PHP_ROUND_HALF_ODD:
        edge_case = round_basic_edge(integral, exponent, places);
        if (value_abs > edge_case) {
            return integral + copysign(1.0, integral);
        } else if (value_abs == edge_case && fmod(integral, 2.0) == 0.0) {
            return integral + copysign(1.0, integral);
        }
        return integral;

    default:
        return integral;
    }
}

static double php_math_round_84(double value, int places, int mode)
{
    double exponent, tmp_value, tmp_value2;

    if (!isfinite(value) || value == 0.0) {
        return value;
    }

    places = places < INT_MIN + 1 ? INT_MIN + 1 : places;
    exponent = php_intpow10(abs(places));

    /* Extracting the integral part can be one ULP short because of FP error
     * (0.285 * 1e10 == 2849999999.9999995); add one and check whether that
     * maps back onto the original value. */
    if (value >= 0.0) {
        tmp_value = floor(places > 0 ? value * exponent : value / exponent);
        tmp_value2 = tmp_value + 1.0;
    } else {
        tmp_value = ceil(places > 0 ? value * exponent : value / exponent);
        tmp_value2 = tmp_value - 1.0;
    }

    if ((places > 0 ? tmp_value2 / exponent : tmp_value2 * exponent) == value) {
        tmp_value = tmp_value2;
    }

    if (fabs(tmp_value) >= 1e16) {
        return value;
    }

    tmp_value = php_round_helper_84(tmp_value, value, exponent, places, mode);

    if (abs(places) < 23) {
        if (places > 0) {
            tmp_value = tmp_value / exponent;
        } else {
            tmp_value = tmp_value * exponent;
        }
    } else {
        char buf[40];

        snprintf(buf, 39, "%15fe%d", tmp_value, -places);
        buf[39] = '\0';
        tmp_value = strtod(buf, NULL);
        if (!isfinite(tmp_value) || isnan(tmp_value)) {
            tmp_value = value;
        }
    }
    return tmp_value;
}

static double php_math_round(double value, int places, int mode)
{
    /* 8.4 replaced the pre-rounding algorithm wholesale;
     * round(0.49999999999999994) is 1.0 up to 8.3 and 0.0 from 8.4. */

        return php_math_round_legacy(value, places, mode);

}

/* ==================================================================== */
/*                          base conversions                            */
/* ==================================================================== */

static const char math_digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";

/* php-src _php_math_basetozval(); returns 0 on failure. */
static int math_basetozval(const zend_string *str, int base, zval *ret)
{
    zend_long num = 0;
    double fnum = 0;
    int mode = 0;
    char c;
    const char *s, *e;
    zend_long cutoff;
    int cutlim;
    int invalidchars = 0;

    if (base < 2 || base > 36) {
        return 0;
    }

    s = ZSTR_VAL(str);
    e = s + ZSTR_LEN(str);

    /* Whitespace trimming, 0x/0o/0b prefix skipping and the "invalid
     * characters" deprecation were all added in 7.4. */

        /* 5.6: the whole string is scanned as-is, prefixes included */


    cutoff = ZEND_LONG_MAX / base;
    cutlim = (int) (ZEND_LONG_MAX % base);

    while (s < e) {
        c = *s++;

        if (c >= '0' && c <= '9')      c = (char) (c - '0');
        else if (c >= 'A' && c <= 'Z') c = (char) (c - ('A' - 10));
        else if (c >= 'a' && c <= 'z') c = (char) (c - ('a' - 10));
        else { invalidchars++; continue; }

        if (c >= base) { invalidchars++; continue; }

        switch (mode) {
        case 0: /* integer */
            if (num < cutoff || (num == cutoff && c <= cutlim)) {
                num = num * base + c;
                break;
            }
            fnum = (double) num;
            mode = 1;
            /* fall through */
        case 1: /* float */
            fnum = fnum * base + c;
        }
    }

    /* 7.4 added this deprecation (invisible before 8.0 because E_DEPRECATED is
     * not in the default error_reporting until then). */

        /* 5.6: invalid characters are silently skipped */


    if (mode == 1) {
        ZVAL_DOUBLE(ret, fnum);
    } else {
        ZVAL_LONG(ret, num);
    }
    return 1;
}

static zend_string *math_longtobase(zend_long value, int base)
{
    char buf[(sizeof(zend_ulong) << 3) + 1];
    char *ptr, *end;
    zend_ulong uvalue;

    if (base < 2 || base > 36) {
        return zend_string_init("", 0);
    }
    uvalue = (zend_ulong) value;

    end = ptr = buf + sizeof(buf) - 1;
    *ptr = '\0';
    do {
        *--ptr = math_digits[uvalue % (zend_ulong) base];
        uvalue /= (zend_ulong) base;
    } while (ptr > buf && uvalue);

    return zend_string_init(ptr, (size_t) (end - ptr));
}

static zend_string *math_zvaltobase(const zval *arg, int base)
{
    if ((Z_TYPE_P(arg) != IS_LONG && Z_TYPE_P(arg) != IS_DOUBLE) ||
        base < 2 || base > 36) {
        return zend_string_init("", 0);
    }

    if (Z_TYPE_P(arg) == IS_DOUBLE) {
        double fvalue = floor(Z_DVAL_P(arg));   /* floor it just in case */
        char buf[(sizeof(double) << 3) + 1];
        char *ptr, *end;

        if (fvalue == HUGE_VAL || fvalue == -HUGE_VAL || isnan(fvalue)) {
            zend_error(E_WARNING, "Number too large");
            return zend_string_init("", 0);
        }

        end = ptr = buf + sizeof(buf) - 1;
        *ptr = '\0';
        do {
            *--ptr = math_digits[(int) fmod(fvalue, (double) base)];
            fvalue /= base;
        } while (ptr > buf && fabs(fvalue) >= 1);

        return zend_string_init(ptr, (size_t) (end - ptr));
    }
    return math_longtobase(Z_LVAL_P(arg), base);
}
/* ==================================================================== */
/*                              builtins                                */
/* ==================================================================== */

/* abs()/ceil()/floor() take "Z" and run convert_scalar_to_number() on it. */
static void zif_abs(zval *ret, uint32_t argc, zval *argv)
{
    math_number n;

    if (!math_arity(argc, 1, 1, "abs")) return;
    if (!math_zpp_number(&argv[0], &n)) {
        ZVAL_BOOL(ret, 0);
        return;
    }
    if (n.is_double) {
        ZVAL_DOUBLE(ret, fabs(n.dval));
    } else if (n.lval == ZEND_LONG_MIN) {
        ZVAL_DOUBLE(ret, -(double) ZEND_LONG_MIN);
    } else {
        ZVAL_LONG(ret, n.lval < 0 ? -n.lval : n.lval);
    }
}

static void zif_ceil(zval *ret, uint32_t argc, zval *argv)
{
    math_number n;

    if (!math_arity(argc, 1, 1, "ceil")) return;
    if (!math_zpp_number(&argv[0], &n)) {
        ZVAL_BOOL(ret, 0);
        return;
    }
    ZVAL_DOUBLE(ret, n.is_double ? ceil(n.dval) : (double) n.lval);
}

static void zif_floor(zval *ret, uint32_t argc, zval *argv)
{
    math_number n;

    if (!math_arity(argc, 1, 1, "floor")) return;
    if (!math_zpp_number(&argv[0], &n)) {
        ZVAL_BOOL(ret, 0);
        return;
    }
    ZVAL_DOUBLE(ret, n.is_double ? floor(n.dval) : (double) n.lval);
}

static void zif_round(zval *ret, uint32_t argc, zval *argv)
{
    math_number n;
    zend_long precision = 0;
    zend_long mode = PHP_ROUND_HALF_UP;
    int places = 0;

    if (!math_arity(argc, 1, 3, "round")) return;
    if (argc >= 2 && !math_zpp_long(&argv[1], &precision, 2, "round", "precision")) return;
    if (argc >= 3 && !math_zpp_long(&argv[2], &mode, 3, "round", "mode")) return;
    if (!math_zpp_number(&argv[0], &n)) {
        ZVAL_BOOL(ret, 0);
        return;
    }

    if (argc >= 2) {
        if (precision >= 0) {
            places = precision > INT_MAX ? INT_MAX : (int) precision;
        } else {
            places = precision <= INT_MIN ? INT_MIN + 1 : (int) precision;
        }
    }

    /* 8.4 validates $mode; earlier releases silently fall back to
     * PHP_ROUND_HALF_UP for an unrecognised mode. */

        /* 5.6: an unknown mode silently behaves like PHP_ROUND_HALF_UP */


    if (!n.is_double && places >= 0) {
        /* simple case: an int that needs no rounding */
        ZVAL_DOUBLE(ret, (double) n.lval);
        return;
    }
    ZVAL_DOUBLE(ret, php_math_round(n.is_double ? n.dval : (double) n.lval,
                                    places, (int) mode));
}

static void zif_fmod(zval *ret, uint32_t argc, zval *argv)
{
    double a, b;

    if (!math_arity(argc, 2, 2, "fmod")) return;
    if (!math_zpp_double(&argv[0], &a, 1, "fmod", "num1")) return;
    if (!math_zpp_double(&argv[1], &b, 2, "fmod", "num2")) return;
    ZVAL_DOUBLE(ret, fmod(a, b));
}





static void zif_sqrt(zval *ret, uint32_t argc, zval *argv)
{
    double a;

    if (!math_arity(argc, 1, 1, "sqrt")) return;
    if (!math_zpp_double(&argv[0], &a, 1, "sqrt", "num")) return;
    ZVAL_DOUBLE(ret, a < 0.0 ? (double) NAN : sqrt(a));
}

static void zif_pow(zval *ret, uint32_t argc, zval *argv)
{
    /* php-src takes "z/z/" and implements pow() as the ** operator, including
     * the integer fast path with overflow promotion to double. */
    if (!math_arity(argc, 2, 2, "pow")) return;
    if (zend_binary_op(BIN_POW, zval_deref(&argv[0]), zval_deref(&argv[1]), ret) != 0) {
        ZVAL_NULL(ret);
    }
}

static void zif_exp(zval *ret, uint32_t argc, zval *argv)
{
    double a;

    if (!math_arity(argc, 1, 1, "exp")) return;
    if (!math_zpp_double(&argv[0], &a, 1, "exp", "num")) return;
    ZVAL_DOUBLE(ret, exp(a));
}

static void zif_log(zval *ret, uint32_t argc, zval *argv)
{
    double num, base;

    if (!math_arity(argc, 1, 2, "log")) return;
    if (!math_zpp_double(&argv[0], &num, 1, "log", "num")) return;

    if (argc < 2 || Z_TYPE_P(zval_deref(&argv[1])) == IS_NULL) {
        ZVAL_DOUBLE(ret, log(num));
        return;
    }
    if (!math_zpp_double(&argv[1], &base, 2, "log", "base")) return;

    if (base == 10.0) {
        ZVAL_DOUBLE(ret, log10(num));
        return;
    }
    /* The base-2 shortcut arrived in 7.0. */

        /* 5.6: base 2 goes through the generic log(num)/log(base) path */


    /* 8.0 turned the "base must be greater than 0" warning into a ValueError
     * and moved it ahead of the base == 1 test. */

        /* 5.6/7.x test base == 1 before the base <= 0 guard */
        if (base == 1.0) {
            ZVAL_DOUBLE(ret, (double) NAN);
            return;
        }
        if (base <= 0.0) {
            zend_error(E_WARNING, "log(): base must be greater than 0");
            ZVAL_BOOL(ret, 0);
            return;
        }

    ZVAL_DOUBLE(ret, log(num) / log(base));
}

#define MATH_UNARY_DOUBLE(cname, phpname, expr)                              \
    static void zif_##cname(zval *ret, uint32_t argc, zval *argv)            \
    {                                                                        \
        double num;                                                          \
        if (!math_arity(argc, 1, 1, phpname)) return;                        \
        if (!math_zpp_double(&argv[0], &num, 1, phpname, "num")) return;     \
        ZVAL_DOUBLE(ret, (expr));                                            \
    }

MATH_UNARY_DOUBLE(log10,   "log10",   log10(num))
MATH_UNARY_DOUBLE(log1p,   "log1p",   log1p(num))
MATH_UNARY_DOUBLE(expm1,   "expm1",   expm1(num))
MATH_UNARY_DOUBLE(sin,     "sin",     sin(num))
MATH_UNARY_DOUBLE(cos,     "cos",     cos(num))
MATH_UNARY_DOUBLE(tan,     "tan",     tan(num))
MATH_UNARY_DOUBLE(asin,    "asin",    asin(num))
MATH_UNARY_DOUBLE(acos,    "acos",    acos(num))
MATH_UNARY_DOUBLE(atan,    "atan",    atan(num))
MATH_UNARY_DOUBLE(sinh,    "sinh",    sinh(num))
MATH_UNARY_DOUBLE(cosh,    "cosh",    cosh(num))
MATH_UNARY_DOUBLE(tanh,    "tanh",    tanh(num))
MATH_UNARY_DOUBLE(asinh,   "asinh",   asinh(num))
MATH_UNARY_DOUBLE(acosh,   "acosh",   acosh(num))
MATH_UNARY_DOUBLE(atanh,   "atanh",   atanh(num))
MATH_UNARY_DOUBLE(deg2rad, "deg2rad", num / 180.0 * M_PI)
MATH_UNARY_DOUBLE(rad2deg, "rad2deg", num / M_PI * 180.0)

static void zif_atan2(zval *ret, uint32_t argc, zval *argv)
{
    double y, x;

    if (!math_arity(argc, 2, 2, "atan2")) return;
    if (!math_zpp_double(&argv[0], &y, 1, "atan2", "y")) return;
    if (!math_zpp_double(&argv[1], &x, 2, "atan2", "x")) return;
    ZVAL_DOUBLE(ret, atan2(y, x));
}

static void zif_hypot(zval *ret, uint32_t argc, zval *argv)
{
    double x, y;

    if (!math_arity(argc, 2, 2, "hypot")) return;
    if (!math_zpp_double(&argv[0], &x, 1, "hypot", "x")) return;
    if (!math_zpp_double(&argv[1], &y, 2, "hypot", "y")) return;
    ZVAL_DOUBLE(ret, hypot(x, y));
}

static void zif_pi(zval *ret, uint32_t argc, zval *argv)
{
    (void) argv;
    if (!math_arity(argc, 0, 0, "pi")) return;
    ZVAL_DOUBLE(ret, M_PI);
}

#define MATH_UNARY_PREDICATE(cname, phpname, expr)                           \
    static void zif_##cname(zval *ret, uint32_t argc, zval *argv)            \
    {                                                                        \
        double d;                                                            \
        if (!math_arity(argc, 1, 1, phpname)) return;                        \
        if (!math_zpp_double(&argv[0], &d, 1, phpname, "num")) return;       \
        ZVAL_BOOL(ret, (expr));                                              \
    }

MATH_UNARY_PREDICATE(is_nan,      "is_nan",      isnan(d))
MATH_UNARY_PREDICATE(is_finite,   "is_finite",   isfinite(d))
MATH_UNARY_PREDICATE(is_infinite, "is_infinite", isinf(d))

static void zif_base_convert(zval *ret, uint32_t argc, zval *argv)
{
    zend_string *num;
    zend_long from_base, to_base;
    zval tmp;

    if (!math_arity(argc, 3, 3, "base_convert")) return;
    if (!math_zpp_long(&argv[1], &from_base, 2, "base_convert", "from_base")) return;
    if (!math_zpp_long(&argv[2], &to_base, 3, "base_convert", "to_base")) return;

    /* php-src converts the value before validating the bases, so an array
     * argument reports "Array to string conversion" first. */
    num = zval_get_string(zval_deref(&argv[0]));

    if (from_base < 2 || from_base > 36) {
        if (!math_base_range_error("base_convert", 2, "from_base",
                                   "`from base'", from_base)) {
            ZVAL_BOOL(ret, 0);
        }
        return;
    }
    if (to_base < 2 || to_base > 36) {
        if (!math_base_range_error("base_convert", 3, "to_base",
                                   "`to base'", to_base)) {
            ZVAL_BOOL(ret, 0);
        }
        return;
    }

    ZVAL_NULL(&tmp);
    if (!math_basetozval(num, (int) from_base, &tmp)) {
        ZVAL_BOOL(ret, 0);
        return;
    }
    ZVAL_STR(ret, math_zvaltobase(&tmp, (int) to_base));
}

/* bindec()/hexdec()/octdec() take "Z" and convert_to_string_ex() it. */
#define MATH_BASETOZVAL(cname, phpname, base)                                \
    static void zif_##cname(zval *ret, uint32_t argc, zval *argv)            \
    {                                                                        \
        zend_string *s;                                                      \
        if (!math_arity(argc, 1, 1, phpname)) return;                        \
        s = zval_get_string(zval_deref(&argv[0]));                           \
        if (!math_basetozval(s, (base), ret)) { ZVAL_BOOL(ret, 0); }         \
    }

MATH_BASETOZVAL(bindec, "bindec", 2)
MATH_BASETOZVAL(hexdec, "hexdec", 16)
MATH_BASETOZVAL(octdec, "octdec", 8)

/* decbin()/decoct()/dechex() take "Z" and convert_to_long_ex() it. */
#define MATH_LONGTOBASE(cname, phpname, base)                                \
    static void zif_##cname(zval *ret, uint32_t argc, zval *argv)            \
    {                                                                        \
        if (!math_arity(argc, 1, 1, phpname)) return;                        \
        ZVAL_STR(ret, math_longtobase(zval_get_long(zval_deref(&argv[0])),   \
                                      (base)));                              \
    }

MATH_LONGTOBASE(decbin, "decbin",  2)
MATH_LONGTOBASE(decoct, "decoct",  8)
MATH_LONGTOBASE(dechex, "dechex", 16)

/* ------------------------------------------------------------------ random */

static void zif_mt_srand(zval *ret, uint32_t argc, zval *argv)
{
    zend_long seed = 0;
    zend_long mode = MT_RAND_MT19937;
    uint32_t max_args = 1;

    (void) ret;
    /* The $mode parameter only exists from 7.1. */

        max_args = 1;

    if (!math_arity(argc, 0, max_args, "mt_srand")) return;

    if (argc >= 1) {
        if (!math_zpp_long(&argv[0], &seed, 1, "mt_srand", "seed")) return;
    } else {
        seed = (zend_long) math_generate_seed();
    }
    if (argc >= 2) {
        if (!math_zpp_long(&argv[1], &mode, 2, "mt_srand", "mode")) return;
    }
    php_mt_set_mode(mode == MT_RAND_PHP ? MT_RAND_PHP : MT_RAND_MT19937);
    php_mt_srand((uint32_t) seed);
}

static void zif_mt_rand(zval *ret, uint32_t argc, zval *argv)
{
    zend_long min, max;

    if (argc == 0) {
        if (!mt_is_seeded) {
            php_mt_srand(math_generate_seed());
        }
        /* genrand_int31(): the raw draw shifted right by one */
        ZVAL_LONG(ret, (zend_long) (php_mt_rand() >> 1));
        return;
    }
    if (!math_arity(argc, 2, 2, "mt_rand")) return;
    if (!math_zpp_long(&argv[0], &min, 1, "mt_rand", "min")) return;
    if (!math_zpp_long(&argv[1], &max, 2, "mt_rand", "max")) return;

    if (max < min) {
        /* 8.0 turned the warning + false into a ValueError. */

            char bmin[32], bmax[32];

            snprintf(bmin, sizeof(bmin), "%lld", (long long) min);
            snprintf(bmax, sizeof(bmax), "%lld", (long long) max);
            zend_error(E_WARNING, "mt_rand(): max(%s) is smaller than min(%s)",
                       bmax, bmin);
            ZVAL_BOOL(ret, 0);

        return;
    }
    if (!mt_is_seeded) {
        php_mt_srand(math_generate_seed());
    }
    /* 5.6/7.0 always used the biased scaling; 7.1 introduced the
     * rejection-sampling range unless MT_RAND_PHP is selected. */

        ZVAL_LONG(ret, mt_bad_scaling((zend_long) (php_mt_rand() >> 1),
                                      min, max, PHP_MT_RAND_MAX));

}

static void zif_srand(zval *ret, uint32_t argc, zval *argv)
{
    zend_long seed = 0;
    uint32_t max_args = 1;

    (void) ret;

        max_args = 1;

    if (!math_arity(argc, 0, max_args, "srand")) return;

    if (argc >= 1) {
        if (!math_zpp_long(&argv[0], &seed, 1, "srand", "seed")) return;
    } else {
        seed = (zend_long) math_generate_seed();
    }

    /* srand()/rand() are the libc generator up to 7.0 and plain aliases of
     * mt_srand()/mt_rand() from 7.1. */

        srandom((unsigned int) seed);
        sys_rand_is_seeded = 1;

}

static void zif_rand(zval *ret, uint32_t argc, zval *argv)
{
    zend_long min, max;
    zend_long number;

    if (argc != 0 && !math_arity(argc, 2, 2, "rand")) return;

    /* 7.1 made rand() an alias of mt_rand(); before that it was libc random(). */

        if (!sys_rand_is_seeded) {
            srandom((unsigned int) math_generate_seed());
            sys_rand_is_seeded = 1;
        }
        number = (zend_long) random();
        if (argc >= 2) {
            if (!math_zpp_long(&argv[0], &min, 1, "rand", "min")) return;
            if (!math_zpp_long(&argv[1], &max, 2, "rand", "max")) return;
            number = mt_bad_scaling(number, min, max, PHP_RAND_MAX);
        }
        ZVAL_LONG(ret, number);

}

static void zif_getrandmax(zval *ret, uint32_t argc, zval *argv)
{
    (void) argv;
    if (!math_arity(argc, 0, 0, "getrandmax")) return;
    ZVAL_LONG(ret, PHP_RAND_MAX);
}

static void zif_mt_getrandmax(zval *ret, uint32_t argc, zval *argv)
{
    (void) argv;
    if (!math_arity(argc, 0, 0, "mt_getrandmax")) return;
    ZVAL_LONG(ret, PHP_MT_RAND_MAX);
}

/* CSPRNG behind random_int(); no test can depend on the values it returns. */
static int math_csprng(void *buf, size_t len)
{
    FILE *f = fopen("/dev/urandom", "rb");
    size_t got;

    if (!f) {
        return 0;
    }
    got = fread(buf, 1, len, f);
    fclose(f);
    return got == len;
}


/* ==================================================================== */

/* The `min_args` column is deliberately 0 for every row: like php-src, this
 * module performs its own arity check inside each handler (math_arity()), so
 * that a wrong argument count produces the 5.6 warning *and* the NULL return
 * value.  The engine's pre-emptive `argc < fn->min_args` test in
 * zend_call_function_ex() aborts the whole call expression instead, which
 * would swallow the NULL.  `max_args` stays accurate because the registry uses
 * it to size the arg_info/ref_mask table. */
const php_function_entry php_functions_math[] = {
    /* name           handler            min max ref_mask  since  until */
    {"abs",           zif_abs,           0, 1, NULL,     0,     0},
    {"ceil",          zif_ceil,          0, 1, NULL,     0,     0},
    {"floor",         zif_floor,         0, 1, NULL,     0,     0},
    {"round",         zif_round,         0, 3, NULL,     0,     0},
    {"fmod",          zif_fmod,          0, 2, NULL,     0,     0},


    {"sqrt",          zif_sqrt,          0, 1, NULL,     0,     0},
    {"pow",           zif_pow,           0, 2, NULL,     0,     0},
    {"exp",           zif_exp,           0, 1, NULL,     0,     0},
    {"log",           zif_log,           0, 2, NULL,     0,     0},
    {"log10",         zif_log10,         0, 1, NULL,     0,     0},
    {"log1p",         zif_log1p,         0, 1, NULL,     0,     0},
    {"expm1",         zif_expm1,         0, 1, NULL,     0,     0},
    {"sin",           zif_sin,           0, 1, NULL,     0,     0},
    {"cos",           zif_cos,           0, 1, NULL,     0,     0},
    {"tan",           zif_tan,           0, 1, NULL,     0,     0},
    {"asin",          zif_asin,          0, 1, NULL,     0,     0},
    {"acos",          zif_acos,          0, 1, NULL,     0,     0},
    {"atan",          zif_atan,          0, 1, NULL,     0,     0},
    {"atan2",         zif_atan2,         0, 2, NULL,     0,     0},
    {"sinh",          zif_sinh,          0, 1, NULL,     0,     0},
    {"cosh",          zif_cosh,          0, 1, NULL,     0,     0},
    {"tanh",          zif_tanh,          0, 1, NULL,     0,     0},
    {"asinh",         zif_asinh,         0, 1, NULL,     0,     0},
    {"acosh",         zif_acosh,         0, 1, NULL,     0,     0},
    {"atanh",         zif_atanh,         0, 1, NULL,     0,     0},
    {"pi",            zif_pi,            0, 0, NULL,     0,     0},
    {"hypot",         zif_hypot,         0, 2, NULL,     0,     0},
    {"deg2rad",       zif_deg2rad,       0, 1, NULL,     0,     0},
    {"rad2deg",       zif_rad2deg,       0, 1, NULL,     0,     0},
    {"base_convert",  zif_base_convert,  0, 3, NULL,     0,     0},
    {"bindec",        zif_bindec,        0, 1, NULL,     0,     0},
    {"decbin",        zif_decbin,        0, 1, NULL,     0,     0},
    {"dechex",        zif_dechex,        0, 1, NULL,     0,     0},
    {"hexdec",        zif_hexdec,        0, 1, NULL,     0,     0},
    {"decoct",        zif_decoct,        0, 1, NULL,     0,     0},
    {"octdec",        zif_octdec,        0, 1, NULL,     0,     0},
    {"mt_rand",       zif_mt_rand,       0, 2, NULL,     0,     0},
    {"mt_srand",      zif_mt_srand,      0, 2, NULL,     0,     0},
    {"mt_getrandmax", zif_mt_getrandmax, 0, 0, NULL,     0,     0},
    {"rand",          zif_rand,          0, 2, NULL,     0,     0},
    {"srand",         zif_srand,         0, 2, NULL,     0,     0},
    {"getrandmax",    zif_getrandmax,    0, 0, NULL,     0,     0},

    {"is_nan",        zif_is_nan,        0, 1, NULL,     0,     0},
    {"is_finite",     zif_is_finite,     0, 1, NULL,     0,     0},
    {"is_infinite",   zif_is_infinite,   0, 1, NULL,     0,     0},
    {NULL, NULL, 0, 0, NULL, 0, 0}
};

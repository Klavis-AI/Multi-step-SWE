/*
 * numeric.c - arbitrary-precision decimal arithmetic, PostgreSQL semantics.
 *
 * This implements the `Numeric` API declared in pgvc.h.  The goal is bug-for-
 * bug compatibility with PostgreSQL 18's `numeric` type: same display-scale
 * propagation, same result-scale selection for division / power / sqrt, same
 * half-away-from-zero rounding, same NaN / +-Infinity behaviour, and the same
 * text input/output syntax.
 *
 * Representation
 * --------------
 * A value is sign + an array of base-10000 "digits" + a weight + a display
 * scale, exactly like PostgreSQL's NumericVar:
 *
 *      value = +-  sum( digits[i] * 10000 ^ (weight - i) )   for i in [0,ndigits)
 *
 * `dscale` is the number of fractional decimal digits the value *prints* with.
 * It is carried along with the value and is what makes 1.00 + 2.000 print as
 * 3.000 rather than 3.  It is independent of how many digits are actually
 * stored: 0.10 stores one digit (1000, weight -1) but has dscale 2.
 *
 * digits[] always points one element into buf[] so that a carry out of the
 * most significant digit during rounding has somewhere to go.
 *
 * Version-sensitive behaviour is gated on PGVC_MAJOR:
 *      < 11   power(NaN, 0) and power(1, NaN) are NaN; from 11 they are 1,
 *             following POSIX pow(3)
 *      < 12   round() and trunc() clamp their scale argument to 2000
 *             rather than to the full 16383
 *      < 14   there are no numeric infinities at all, so 'Infinity'/'inf'
 *             are input syntax errors and num_from_double() cannot represent
 *             an infinite double (it returns NULL)
 *      < 15   a numeric typmod cannot carry a negative scale, so the scale
 *             field is read unsigned
 *      < 16   no '_' digit separators and no 0x/0o/0b integer literals; and
 *             power() with an integral exponent uses a flat 16 fractional
 *             digits instead of a scale based on the result's magnitude
 *
 * One deliberate omission: PostgreSQL 9.6-12 computed sqrt() by Newton
 * iteration and raised "division by zero" when the argument underflowed
 * that iteration (somewhere below 1e-2030; the exact boundary depended on
 * the discarded algorithm's initial guess).  PostgreSQL 13 replaced the
 * algorithm, and this module uses the newer exact integer square root at
 * every version, so for those extreme inputs it returns the correct zero
 * instead of failing.
 *
 * Everything else -- display-scale propagation, the division and sqrt result
 * scales, rounding, NaN ordering -- is identical in 9.6 through 18, which was
 * verified by replaying the whole test corpus against all ten servers.
 *
 * libc only; no GMP, no other external dependencies.
 */

#include "pgvc.h"

#include <limits.h>
#include <float.h>

/*
 * PGVC_MAJOR is spelled 96 for PostgreSQL 9.6 and 10..18 for the rest, so it
 * cannot be compared numerically.  PGVC_VERNUM is monotonic: 96 < 110 < 118.
 */
#if PGVC_MAJOR == 96
#define PGVC_VERNUM 96
#else
#define PGVC_VERNUM (PGVC_MAJOR + 100)
#endif

/* ------------------------------------------------------------------ tuning */

#define NBASE            10000          /* digit base */
#define HALF_NBASE       5000
#define DEC_DIGITS       4              /* decimal digits per NBASE digit */
#define MUL_GUARD_DIGITS 2              /* NBASE guard digits for mul_var  */
#define DIV_GUARD_DIGITS 4              /* NBASE guard digits for div paths */

/*
 * Scale/precision limits, matching PostgreSQL's numeric.h.
 */
#define NUM_MAX_PRECISION      1000
#define NUM_MIN_SIG_DIGITS     16       /* min significant digits for / ^ sqrt */
#define NUM_MAX_DISPLAY_SCALE  NUM_MAX_PRECISION
#define NUM_MIN_DISPLAY_SCALE  0
#define NUM_MAX_RESULT_SCALE   (NUM_MAX_PRECISION * 2)
#define NUM_DSCALE_MAX         0x3FFF   /* 16383 */
#define NUM_WEIGHT_MAX         32767

/*
 * Feature switches derived from the target server version.  Spelling them as
 * ordinary constants (rather than #if around each use) keeps the parser
 * readable and lets the compiler fold the dead branches away.
 */
#define HAVE_INFINITIES     (PGVC_VERNUM >= 114)
/*
 * round()/trunc() clamp their scale argument.  Before 12 the cap was
 * NUM_MAX_RESULT_SCALE; from 12 it is the full NUM_DSCALE_MAX.
 */
#define ROUND_SCALE_CAP     (PGVC_VERNUM >= 112 ? NUM_DSCALE_MAX \
                                                : NUM_MAX_RESULT_SCALE)
#define ALLOW_UNDERSCORES   (PGVC_VERNUM >= 116)
#define ALLOW_NONDECIMAL    (PGVC_VERNUM >= 116)
#define POW_POSIX_NAN       (PGVC_VERNUM >= 111)

/* sign / special-value codes */
#define NSIGN_POS   0
#define NSIGN_NEG   1
#define NSIGN_NAN   2
#define NSIGN_PINF  3
#define NSIGN_NINF  4

typedef int16 NumDigit;

struct Numeric
{
    int        ndigits;     /* # of digits in digits[] */
    int        weight;      /* weight of digits[0] */
    int        sign;        /* NSIGN_* */
    int        dscale;      /* display scale */
    NumDigit  *buf;         /* start of the allocation (buf[0] is spare) */
    NumDigit  *digits;      /* == buf + 1 initially; may drift on strip */
};

#define IS_SPECIAL(v) ((v)->sign > NSIGN_NEG)
#define IS_NAN(v)     ((v)->sign == NSIGN_NAN)
#define IS_PINF(v)    ((v)->sign == NSIGN_PINF)
#define IS_NINF(v)    ((v)->sign == NSIGN_NINF)
#define IS_INF(v)     ((v)->sign == NSIGN_PINF || (v)->sign == NSIGN_NINF)

#ifndef Max
#define Max(a, b)  ((a) > (b) ? (a) : (b))
#endif
#ifndef Min
#define Min(a, b)  ((a) < (b) ? (a) : (b))
#endif

/* ------------------------------------------------------------- allocation */

static void *
nm_alloc(size_t n)
{
    void *p = malloc(n ? n : 1);

    if (p == NULL)
    {
        fprintf(stderr, "numeric: out of memory\n");
        abort();
    }
    return p;
}

static void *
nm_calloc(size_t n, size_t sz)
{
    void *p = calloc(n ? n : 1, sz ? sz : 1);

    if (p == NULL)
    {
        fprintf(stderr, "numeric: out of memory\n");
        abort();
    }
    return p;
}

/* ---------------------------------------------------------- var lifecycle */

static void
nv_init(Numeric *v)
{
    v->ndigits = 0;
    v->weight = 0;
    v->sign = NSIGN_POS;
    v->dscale = 0;
    v->buf = NULL;
    v->digits = NULL;
}

static void
nv_free(Numeric *v)
{
    free(v->buf);
    v->buf = NULL;
    v->digits = NULL;
    v->ndigits = 0;
}

/*
 * Make room for ndigits digits.  buf[0] is reserved as a spare slot so that
 * round_var() can prepend a carry digit without reallocating.
 */
static void
nv_alloc(Numeric *v, int ndigits)
{
    NumDigit *nb = nm_alloc((size_t) (ndigits + 1) * sizeof(NumDigit));

    nb[0] = 0;
    free(v->buf);
    v->buf = nb;
    v->digits = nb + 1;
    v->ndigits = ndigits;
}

/* Set to zero, preserving dscale (PostgreSQL's zero_var). */
static void
nv_zero(Numeric *v)
{
    free(v->buf);
    v->buf = NULL;
    v->digits = NULL;
    v->ndigits = 0;
    v->weight = 0;
    v->sign = NSIGN_POS;
}

/* dst = src.  Safe when dst and src are distinct objects. */
static void
nv_set(Numeric *dst, const Numeric *src)
{
    NumDigit *nb = nm_alloc((size_t) (src->ndigits + 1) * sizeof(NumDigit));

    nb[0] = 0;
    if (src->ndigits > 0)
        memcpy(nb + 1, src->digits, (size_t) src->ndigits * sizeof(NumDigit));
    free(dst->buf);
    dst->buf = nb;
    dst->digits = nb + 1;
    dst->ndigits = src->ndigits;
    dst->weight = src->weight;
    dst->sign = src->sign;
    dst->dscale = src->dscale;
}

static void nv_strip(Numeric *v);

static Numeric *
nv_new(void)
{
    Numeric *v = nm_alloc(sizeof(Numeric));

    nv_init(v);
    return v;
}

static Numeric *
nv_new_special(int sign)
{
    Numeric *v = nv_new();

    v->sign = sign;
    return v;
}

/* Detach a stack var into a freshly malloc'd Numeric (stealing its buffer). */
static Numeric *
nv_export(Numeric *v)
{
    Numeric *r = nm_alloc(sizeof(Numeric));

    nv_strip(v);                /* results are always handed out normalised */
    *r = *v;
    nv_init(v);                 /* caller's var no longer owns the buffer */
    return r;
}

/* Strip leading and trailing zero digits; normalise the zero value. */
static void
nv_strip(Numeric *v)
{
    NumDigit *digits;
    int        ndigits;

    if (IS_SPECIAL(v))
        return;                 /* NaN and the infinities carry no digits */

    digits = v->digits;
    ndigits = v->ndigits;

    while (ndigits > 0 && *digits == 0)
    {
        digits++;
        v->weight--;
        ndigits--;
    }
    while (ndigits > 0 && digits[ndigits - 1] == 0)
        ndigits--;

    if (ndigits == 0)
    {
        v->sign = NSIGN_POS;    /* there is no negative zero */
        v->weight = 0;
    }
    v->digits = digits;
    v->ndigits = ndigits;
}

/* ------------------------------------------------------------- constants */

static const NumDigit const_one_data[1] = {1};
static const Numeric const_one =
{1, 0, NSIGN_POS, 0, NULL, (NumDigit *) const_one_data};

static const NumDigit const_two_data[1] = {2};
static const Numeric const_two =
{1, 0, NSIGN_POS, 0, NULL, (NumDigit *) const_two_data};

static const NumDigit const_minus_one_data[1] = {1};
static const Numeric const_minus_one =
{1, 0, NSIGN_NEG, 0, NULL, (NumDigit *) const_minus_one_data};

static const Numeric const_zero =
{0, 0, NSIGN_POS, 0, NULL, NULL};

/* 0.9 and 1.1 bound the range ln_var() reduces its argument into */
static const NumDigit const_pt9_data[1] = {9000};
static const Numeric const_pt9 =
{1, -1, NSIGN_POS, 1, NULL, (NumDigit *) const_pt9_data};

static const NumDigit const_1pt1_data[2] = {1, 1000};
static const Numeric const_1pt1 =
{2, 0, NSIGN_POS, 1, NULL, (NumDigit *) const_1pt1_data};

/* 10^di for di in 1..3, used when rounding inside an NBASE digit */
static const int round_powers[DEC_DIGITS] = {0, 1000, 100, 10};

/* ------------------------------------------------------ forward decls */

static void round_var(Numeric *var, int rscale);
static void trunc_var(Numeric *var, int rscale);
static int  cmp_var(const Numeric *a, const Numeric *b);
static void add_var(const Numeric *a, const Numeric *b, Numeric *res);
static void sub_var(const Numeric *a, const Numeric *b, Numeric *res);
static void mul_var(const Numeric *a, const Numeric *b, Numeric *res, int rscale);
static bool div_var(const Numeric *a, const Numeric *b, Numeric *res,
                    int rscale, bool round);
static void div_var_int(const Numeric *var, int ival, int ival_weight,
                        Numeric *res, int rscale, bool round);
static void sqrt_var(const Numeric *arg, Numeric *res, int rscale);
static void ln_var(const Numeric *arg, Numeric *res, int rscale);
static char *get_str_from_var(const Numeric *var);
static double var_to_double(const Numeric *var);

/* ------------------------------------------------------ absolute-value core */

/*
 * Compare the absolute values of two variables.  Returns -1, 0 or 1.
 */
static int
cmp_abs(const Numeric *a, const Numeric *b)
{
    int        i1 = 0,
               i2 = 0;
    int        w1 = a->weight,
               w2 = b->weight;

    /*
     * Skip over any high-order positions where only one operand has digits.
     * A nonzero digit there settles the comparison immediately.
     */
    while (w1 > w2 && i1 < a->ndigits)
    {
        if (a->digits[i1++] != 0)
            return 1;
        w1--;
    }
    while (w2 > w1 && i2 < b->ndigits)
    {
        if (b->digits[i2++] != 0)
            return -1;
        w2--;
    }

    /* Either the weights now agree, or one side ran out of digits. */
    if (w1 == w2)
    {
        while (i1 < a->ndigits && i2 < b->ndigits)
        {
            int        stat = a->digits[i1++] - b->digits[i2++];

            if (stat)
                return stat > 0 ? 1 : -1;
        }
    }

    /* One side is exhausted; any remaining nonzero digit makes that side bigger. */
    while (i1 < a->ndigits)
        if (a->digits[i1++] != 0)
            return 1;
    while (i2 < b->ndigits)
        if (b->digits[i2++] != 0)
            return -1;

    return 0;
}

/*
 * res = |a| + |b|.  The sign of res is left for the caller to set.
 */
static void
add_abs(const Numeric *a, const Numeric *b, Numeric *res)
{
    Numeric     out;
    int         res_weight = Max(a->weight, b->weight) + 1;
    int         res_rscale = Max(a->ndigits - a->weight, b->ndigits - b->weight) - 1;
    int         res_ndigits;
    int         i, i1, i2;
    int         carry = 0;
    NumDigit   *rd;

    res_ndigits = res_rscale + res_weight + 1;
    if (res_ndigits <= 0)
        res_ndigits = 1;

    nv_init(&out);
    nv_alloc(&out, res_ndigits);
    rd = out.digits;

    i1 = res_weight - a->weight;    /* offset of a's digits inside result */
    i2 = res_weight - b->weight;

    for (i = res_ndigits - 1; i >= 0; i--)
    {
        int        ai = i - i1;
        int        bi = i - i2;

        if (ai >= 0 && ai < a->ndigits)
            carry += a->digits[ai];
        if (bi >= 0 && bi < b->ndigits)
            carry += b->digits[bi];

        if (carry >= NBASE)
        {
            rd[i] = (NumDigit) (carry - NBASE);
            carry = 1;
        }
        else
        {
            rd[i] = (NumDigit) carry;
            carry = 0;
        }
    }
    /* carry must be zero: res_weight left room for it */

    out.weight = res_weight;
    out.dscale = Max(a->dscale, b->dscale);
    out.sign = NSIGN_POS;
    nv_strip(&out);

    free(res->buf);
    *res = out;
}

/*
 * res = |a| - |b|.  Caller must have checked |a| >= |b|.
 */
static void
sub_abs(const Numeric *a, const Numeric *b, Numeric *res)
{
    Numeric     out;
    int         res_weight = a->weight;
    int         res_rscale = Max(a->ndigits - a->weight, b->ndigits - b->weight) - 1;
    int         res_ndigits;
    int         i, i1, i2;
    int         borrow = 0;
    NumDigit   *rd;

    res_ndigits = res_rscale + res_weight + 1;
    if (res_ndigits <= 0)
        res_ndigits = 1;

    nv_init(&out);
    nv_alloc(&out, res_ndigits);
    rd = out.digits;

    i1 = res_weight - a->weight;
    i2 = res_weight - b->weight;

    for (i = res_ndigits - 1; i >= 0; i--)
    {
        int        ai = i - i1;
        int        bi = i - i2;

        if (ai >= 0 && ai < a->ndigits)
            borrow += a->digits[ai];
        if (bi >= 0 && bi < b->ndigits)
            borrow -= b->digits[bi];

        if (borrow < 0)
        {
            rd[i] = (NumDigit) (borrow + NBASE);
            borrow = -1;
        }
        else
        {
            rd[i] = (NumDigit) borrow;
            borrow = 0;
        }
    }

    out.weight = res_weight;
    out.dscale = Max(a->dscale, b->dscale);
    out.sign = NSIGN_POS;
    nv_strip(&out);

    free(res->buf);
    *res = out;
}

/* ------------------------------------------------------------ comparison */

static int
cmp_var(const Numeric *a, const Numeric *b)
{
    if (a->ndigits == 0)
    {
        if (b->ndigits == 0)
            return 0;
        return b->sign == NSIGN_NEG ? 1 : -1;
    }
    if (b->ndigits == 0)
        return a->sign == NSIGN_NEG ? -1 : 1;

    if (a->sign == NSIGN_POS)
    {
        if (b->sign == NSIGN_NEG)
            return 1;
        return cmp_abs(a, b);
    }
    if (b->sign == NSIGN_POS)
        return -1;
    return -cmp_abs(a, b);
}

/* ---------------------------------------------------------- add / subtract */

static void
add_var(const Numeric *a, const Numeric *b, Numeric *res)
{
    if (a->sign == NSIGN_POS)
    {
        if (b->sign == NSIGN_POS)
        {
            add_abs(a, b, res);
            res->sign = NSIGN_POS;
        }
        else
        {
            switch (cmp_abs(a, b))
            {
                case 0:
                {
                    int ds = Max(a->dscale, b->dscale);

                    nv_zero(res);
                    res->dscale = ds;
                    break;
                }
                case 1:
                    sub_abs(a, b, res);
                    res->sign = NSIGN_POS;
                    break;
                default:
                    sub_abs(b, a, res);
                    res->sign = NSIGN_NEG;
                    break;
            }
        }
    }
    else
    {
        if (b->sign == NSIGN_POS)
        {
            switch (cmp_abs(a, b))
            {
                case 0:
                {
                    int ds = Max(a->dscale, b->dscale);

                    nv_zero(res);
                    res->dscale = ds;
                    break;
                }
                case 1:
                    sub_abs(a, b, res);
                    res->sign = NSIGN_NEG;
                    break;
                default:
                    sub_abs(b, a, res);
                    res->sign = NSIGN_POS;
                    break;
            }
        }
        else
        {
            add_abs(a, b, res);
            res->sign = NSIGN_NEG;
        }
    }
}

static void
sub_var(const Numeric *a, const Numeric *b, Numeric *res)
{
    Numeric     negb = *b;

    /* Flip the sign of b without touching its storage, then add. */
    if (b->sign == NSIGN_POS)
        negb.sign = NSIGN_NEG;
    else
        negb.sign = NSIGN_POS;

    add_var(a, &negb, res);
}

/* ------------------------------------------------------- round / truncate */

/*
 * Round var at rscale fractional decimal digits, half away from zero.
 * rscale may be negative, meaning rounding before the decimal point.
 * Sets var->dscale = rscale.
 */
static void
round_var(Numeric *var, int rscale)
{
    NumDigit   *digits = var->digits;
    int         di;
    int         ndigits;
    int         carry;

    var->dscale = rscale;

    /* number of decimal digits we want to keep */
    di = (var->weight + 1) * DEC_DIGITS + rscale;

    if (di < 0)
    {
        /* Everything is rounded away (di == 0 could still round up to 1). */
        var->ndigits = 0;
        var->weight = 0;
        var->sign = NSIGN_POS;
        return;
    }

    ndigits = (di + DEC_DIGITS - 1) / DEC_DIGITS;   /* NBASE digits wanted */
    di %= DEC_DIGITS;                               /* decimals kept in last one */

    if (ndigits < var->ndigits || (ndigits == var->ndigits && di > 0))
    {
        var->ndigits = ndigits;

        if (di == 0)
            carry = (digits[ndigits] >= HALF_NBASE) ? 1 : 0;
        else
        {
            /* We must round inside the last retained NBASE digit. */
            int        extra,
                       pow10;

            pow10 = round_powers[di];
            extra = digits[--ndigits] % pow10;
            digits[ndigits] -= (NumDigit) extra;
            carry = 0;
            if (extra >= pow10 / 2)
            {
                pow10 += digits[ndigits];
                if (pow10 >= NBASE)
                {
                    pow10 -= NBASE;
                    carry = 1;
                }
                digits[ndigits] = (NumDigit) pow10;
            }
        }

        while (carry)
        {
            carry += digits[--ndigits];
            if (carry >= NBASE)
            {
                digits[ndigits] = (NumDigit) (carry - NBASE);
                carry = 1;
            }
            else
            {
                digits[ndigits] = (NumDigit) carry;
                carry = 0;
            }
        }

        if (ndigits < 0)
        {
            /* Carried out of the top digit: use the spare slot in buf[]. */
            var->digits--;
            var->ndigits++;
            var->weight++;
        }
    }
}

/*
 * Truncate var towards zero at rscale fractional decimal digits.
 * rscale may be negative.  Sets var->dscale = rscale.
 */
static void
trunc_var(Numeric *var, int rscale)
{
    int        di;
    int        ndigits;

    var->dscale = rscale;

    di = (var->weight + 1) * DEC_DIGITS + rscale;

    if (di <= 0)
    {
        var->ndigits = 0;
        var->weight = 0;
        var->sign = NSIGN_POS;
        return;
    }

    ndigits = (di + DEC_DIGITS - 1) / DEC_DIGITS;

    if (ndigits <= var->ndigits)
    {
        var->ndigits = ndigits;

        di %= DEC_DIGITS;
        if (di > 0)
        {
            NumDigit   *digits = var->digits;
            int         pow10 = round_powers[di];
            int         extra = digits[--ndigits] % pow10;

            digits[ndigits] -= (NumDigit) extra;
        }
    }
}

/* ------------------------------------------------------------- multiply */

/*
 * res = a * b, rounded to rscale fractional digits.
 *
 * The product is computed exactly except that input digits which could only
 * contribute below the rounding position (plus MUL_GUARD_DIGITS of slack) are
 * skipped, which is what keeps repeated squaring in power_var_int() cheap.
 */
static void
mul_var(const Numeric *a, const Numeric *b, Numeric *res, int rscale)
{
    int         n1 = a->ndigits;
    int         n2 = b->ndigits;
    int         res_sign;
    int         res_weight;
    int         res_ndigits;
    int         maxdigits;
    uint64     *acc;
    Numeric     out;
    int         i, j;
    uint64      carry;

    if (n1 == 0 || n2 == 0)
    {
        nv_zero(res);
        res->dscale = rscale;
        return;
    }

    res_sign = (a->sign == b->sign) ? NSIGN_POS : NSIGN_NEG;

    /*
     * Digit k of the result (k >= 1) collects the products a[i]*b[j] with
     * i + j == k - 1, so digit 0 is a pure carry slot and carries weight
     * a->weight + b->weight + 1.
     */
    res_weight = a->weight + b->weight + 1;
    res_ndigits = n1 + n2;

    /* Positions that can't survive the rounding need not be computed. */
    maxdigits = res_weight + 1 + (rscale + DEC_DIGITS - 1) / DEC_DIGITS +
        MUL_GUARD_DIGITS;
    if (maxdigits < 1)
    {
        nv_zero(res);
        res->dscale = rscale;
        return;
    }
    res_ndigits = Min(res_ndigits, maxdigits);

    acc = nm_calloc((size_t) res_ndigits, sizeof(uint64));

    /*
     * Accumulate without normalising.  Each cell takes at most
     * min(n1,n2) * (NBASE-1)^2 < 1e8 * n terms, far below UINT64_MAX for any
     * digit count we can actually allocate.
     */
    for (i = 0; i < n1; i++)
    {
        uint64      d1 = (uint64) a->digits[i];
        int         jlim;

        if (d1 == 0)
            continue;
        jlim = res_ndigits - 1 - i;     /* j such that i+j+1 < res_ndigits */
        if (jlim > n2)
            jlim = n2;
        if (jlim <= 0)
            break;
        for (j = 0; j < jlim; j++)
            acc[i + j + 1] += d1 * (uint64) b->digits[j];
    }

    nv_init(&out);
    nv_alloc(&out, res_ndigits);

    carry = 0;
    for (i = res_ndigits - 1; i >= 0; i--)
    {
        uint64      v = acc[i] + carry;

        carry = v / NBASE;
        out.digits[i] = (NumDigit) (v - carry * NBASE);
    }
    free(acc);

    out.weight = res_weight;
    out.sign = res_sign;
    round_var(&out, rscale);
    nv_strip(&out);

    free(res->buf);
    *res = out;
}

/* --------------------------------------------------------------- divide */

/*
 * Divide var by a (possibly large) integer ival whose weight is ival_weight,
 * i.e. compute var / (ival * NBASE^ival_weight).  Short division; used both
 * as the fast path of div_var() and directly by the Taylor series code.
 */
static void
div_var_int(const Numeric *var, int ival, int ival_weight,
            Numeric *res, int rscale, bool round)
{
    int         var_ndigits = var->ndigits;
    int         res_sign;
    int         res_weight;
    int         res_ndigits;
    uint64      divisor;
    uint64      carry = 0;
    Numeric     out;
    int         i;

    if (var_ndigits == 0)
    {
        nv_zero(res);
        res->dscale = rscale;
        return;
    }

    if (var->sign == NSIGN_POS)
        res_sign = ival > 0 ? NSIGN_POS : NSIGN_NEG;
    else
        res_sign = ival > 0 ? NSIGN_NEG : NSIGN_POS;

    res_weight = var->weight - ival_weight;
    res_ndigits = res_weight + 1 + (rscale + DEC_DIGITS - 1) / DEC_DIGITS;
    res_ndigits = Max(res_ndigits, 1);
    if (round)
        res_ndigits++;

    nv_init(&out);
    nv_alloc(&out, res_ndigits);

    divisor = (uint64) (ival < 0 ? -(int64) ival : (int64) ival);

    for (i = 0; i < res_ndigits; i++)
    {
        carry = carry * NBASE + (uint64) (i < var_ndigits ? var->digits[i] : 0);
        out.digits[i] = (NumDigit) (carry / divisor);
        carry = carry % divisor;
    }

    out.weight = res_weight;
    out.sign = res_sign;

    if (round)
        round_var(&out, rscale);
    else
        trunc_var(&out, rscale);
    nv_strip(&out);

    free(res->buf);
    *res = out;
}

/*
 * res = a / b, computed exactly to res_ndigits base-NBASE digits and then
 * rounded (or truncated) to rscale fractional digits.
 *
 * Returns false, leaving res untouched, if b is zero.
 */
static bool
div_var(const Numeric *a, const Numeric *b, Numeric *res,
        int rscale, bool round)
{
    int         n1 = a->ndigits;
    int         n2 = b->ndigits;
    int         res_sign;
    int         res_weight;
    int         res_ndigits;
    Numeric     out;
    int         L, m, i, j;
    int        *u;
    int        *v;
    int         d;
    int64       carry;

    if (n2 == 0 || b->digits[0] == 0)
        return false;           /* division by zero */

    /* One- and two-digit divisors are handled by fast short division. */
    if (n2 <= 2)
    {
        int        idiv = b->digits[0];
        int        idiv_weight = b->weight;

        if (n2 == 2)
        {
            idiv = idiv * NBASE + b->digits[1];
            idiv_weight--;
        }
        if (b->sign == NSIGN_NEG)
            idiv = -idiv;

        div_var_int(a, idiv, idiv_weight, res, rscale, round);
        return true;
    }

    if (n1 == 0)
    {
        nv_zero(res);
        res->dscale = rscale;
        return true;
    }

    res_sign = (a->sign == b->sign) ? NSIGN_POS : NSIGN_NEG;

    /*
     * Quotient digit k has weight res_weight - k.  As in PostgreSQL the
     * weight assumes no leading zero digit is emitted; nv_strip() fixes that
     * up afterwards.
     */
    res_weight = a->weight - b->weight + 1;
    res_ndigits = res_weight + 1 + (rscale + DEC_DIGITS - 1) / DEC_DIGITS;
    res_ndigits = Max(res_ndigits, 1);
    if (round)
        res_ndigits++;

    /*
     * Knuth's algorithm D over positions u[0..L], where L = res_ndigits +
     * n2 - 1.  u[0] is algorithm D's normalisation overflow slot and u[1] is
     * a leading zero, so u[i] carries weight a->weight - (i - 2).  That
     * alignment makes the first of the L - n2 + 1 == res_ndigits quotient
     * digits land on res_weight.
     */
    L = res_ndigits + n2 - 1;
    m = L - n2;                 /* == res_ndigits - 1 */

    u = nm_calloc((size_t) (L + 1), sizeof(int));
    v = nm_calloc((size_t) n2, sizeof(int));

    for (i = 2; i <= L; i++)
        u[i] = (i - 2 < n1) ? a->digits[i - 2] : 0;
    for (i = 0; i < n2; i++)
        v[i] = b->digits[i];

    /* D1: normalise so that the leading divisor digit is >= NBASE/2. */
    d = NBASE / (v[0] + 1);
    if (d > 1)
    {
        carry = 0;
        for (i = n2 - 1; i >= 0; i--)
        {
            int64 t = (int64) v[i] * d + carry;

            v[i] = (int) (t % NBASE);
            carry = t / NBASE;
        }
        carry = 0;
        for (i = L - 1; i >= 0; i--)
        {
            int64 t = (int64) u[i] * d + carry;

            u[i] = (int) (t % NBASE);
            carry = t / NBASE;
        }
        /* u[0] was zero and d <= NBASE/2, so nothing can carry out */
    }

    nv_init(&out);
    nv_alloc(&out, res_ndigits);

    for (j = 0; j <= m; j++)
    {
        int64       num = (int64) u[j] * NBASE + u[j + 1];
        int64       qhat = num / v[0];
        int64       rhat = num % v[0];
        int64       borrow;

        /* D3: refine the estimate so it is at most one too large. */
        while (qhat >= NBASE ||
               (n2 > 1 && qhat * v[1] > rhat * NBASE + u[j + 2]))
        {
            qhat--;
            rhat += v[0];
            if (rhat >= NBASE)
                break;
        }

        /* D4: multiply and subtract. */
        borrow = 0;
        carry = 0;
        for (i = n2 - 1; i >= 0; i--)
        {
            int64   p = qhat * v[i] + carry;
            int64   sub;

            carry = p / NBASE;
            sub = (int64) u[j + 1 + i] - (p - carry * NBASE) - borrow;
            if (sub < 0)
            {
                borrow = 1;
                sub += NBASE;
            }
            else
                borrow = 0;
            u[j + 1 + i] = (int) sub;
        }
        {
            int64 sub = (int64) u[j] - carry - borrow;

            if (sub < 0)
            {
                borrow = 1;
                sub += NBASE;
            }
            else
                borrow = 0;
            u[j] = (int) sub;
        }

        /* D5/D6: if we subtracted too much, add the divisor back. */
        if (borrow)
        {
            qhat--;
            carry = 0;
            for (i = n2 - 1; i >= 0; i--)
            {
                int64 t = (int64) u[j + 1 + i] + v[i] + carry;

                if (t >= NBASE)
                {
                    u[j + 1 + i] = (int) (t - NBASE);
                    carry = 1;
                }
                else
                {
                    u[j + 1 + i] = (int) t;
                    carry = 0;
                }
            }
            u[j] = (int) (((int64) u[j] + carry) % NBASE);
        }

        out.digits[j] = (NumDigit) qhat;
    }

    free(u);
    free(v);

    out.weight = res_weight;
    out.sign = res_sign;

    if (round)
        round_var(&out, rscale);
    else
        trunc_var(&out, rscale);
    nv_strip(&out);

    free(res->buf);
    *res = out;
    return true;
}

/*
 * Choose the result scale for the `/` operator, exactly as PostgreSQL's
 * select_div_scale() does: enough to give NUM_MIN_SIG_DIGITS significant
 * digits, but never fewer than either input's display scale.
 */
static int
select_div_scale(const Numeric *a, const Numeric *b)
{
    int         weight1 = 0,
                weight2 = 0;
    int         firstdigit1 = 0,
                firstdigit2 = 0;
    int         qweight;
    int         rscale;
    int         i;

    for (i = 0; i < a->ndigits; i++)
    {
        firstdigit1 = a->digits[i];
        if (firstdigit1 != 0)
        {
            weight1 = a->weight - i;
            break;
        }
    }
    for (i = 0; i < b->ndigits; i++)
    {
        firstdigit2 = b->digits[i];
        if (firstdigit2 != 0)
        {
            weight2 = b->weight - i;
            break;
        }
    }

    /* Estimated weight of the quotient; pessimistic when the leads tie. */
    qweight = weight1 - weight2;
    if (firstdigit1 <= firstdigit2)
        qweight--;

    rscale = NUM_MIN_SIG_DIGITS - qweight * DEC_DIGITS;
    rscale = Max(rscale, a->dscale);
    rscale = Max(rscale, b->dscale);
    rscale = Max(rscale, NUM_MIN_DISPLAY_SCALE);
    rscale = Min(rscale, NUM_MAX_DISPLAY_SCALE);

    return rscale;
}

/* res = a mod b, using mod(x,y) = x - trunc(x/y)*y (sign follows x). */
static bool
mod_var(const Numeric *a, const Numeric *b, Numeric *res)
{
    Numeric     tmp;

    nv_init(&tmp);
    if (!div_var(a, b, &tmp, 0, false))
    {
        nv_free(&tmp);
        return false;
    }
    mul_var(b, &tmp, &tmp, b->dscale);
    sub_var(a, &tmp, res);
    nv_free(&tmp);
    return true;
}

static void
ceil_var(const Numeric *var, Numeric *res)
{
    Numeric     tmp;

    nv_init(&tmp);
    nv_set(&tmp, var);
    trunc_var(&tmp, 0);
    if (var->sign == NSIGN_POS && cmp_var(var, &tmp) != 0)
        add_var(&tmp, &const_one, &tmp);
    nv_set(res, &tmp);
    nv_free(&tmp);
}

static void
floor_var(const Numeric *var, Numeric *res)
{
    Numeric     tmp;

    nv_init(&tmp);
    nv_set(&tmp, var);
    trunc_var(&tmp, 0);
    if (var->sign == NSIGN_NEG && cmp_var(var, &tmp) != 0)
        sub_var(&tmp, &const_one, &tmp);
    nv_set(res, &tmp);
    nv_free(&tmp);
}

/* ------------------------------------------------------------ square root */

/*
 * out = floor(sqrt(n)) for a nonnegative integer-valued n (n > 0).
 * Newton's method; the starting point is derived from the two leading digits
 * so it is always >= the true root, which makes the iteration monotone.
 */
static void
isqrt_var(const Numeric *n, Numeric *out)
{
    int         nd = n->weight + 1;     /* # of integer digit positions */
    int         R = (nd + 1) / 2;       /* # of digits in the root */
    int         t = nd - 2 * (R - 1);   /* 1 or 2 leading digits to sample */
    int64       T;
    int64       q;
    Numeric     x,
                y;

    T = n->digits[0];
    if (t == 2)
        T = T * NBASE + (n->ndigits > 1 ? n->digits[1] : 0);

    /* q = isqrt(T) + 1, so that q * NBASE^(R-1) >= sqrt(n) */
    q = (int64) sqrt((double) T);
    while (q * q > T)
        q--;
    while ((q + 1) * (q + 1) <= T)
        q++;
    q++;

    nv_init(&x);
    nv_init(&y);

    if (q >= NBASE)
    {
        nv_alloc(&x, 1);
        x.digits[0] = 1;
        x.weight = R;
    }
    else
    {
        nv_alloc(&x, 1);
        x.digits[0] = (NumDigit) q;
        x.weight = R - 1;
    }
    x.sign = NSIGN_POS;
    x.dscale = 0;

    for (;;)
    {
        div_var(n, &x, &y, 0, false);   /* y = floor(n / x) */
        add_var(&x, &y, &y);
        div_var_int(&y, 2, 0, &y, 0, false);
        if (cmp_var(&y, &x) >= 0)
            break;
        nv_set(&x, &y);
    }

    nv_free(&y);
    nv_set(out, &x);
    nv_free(&x);
}

/*
 * res = sqrt(arg), computed to rscale fractional digits.  rscale may be
 * negative (rounding before the decimal point), which ln_var() relies on.
 * The caller must have rejected negative arguments.
 */
static void
sqrt_var(const Numeric *arg, Numeric *res, int rscale)
{
    int         res_weight;
    int         res_ndigits;
    int         src_ndigits;
    Numeric     n,
                s;
    int         i;

    if (arg->ndigits == 0)
    {
        nv_zero(res);
        res->dscale = rscale;
        return;
    }

    /* result weight is floor(arg->weight / 2) */
    if (arg->weight >= 0)
        res_weight = arg->weight / 2;
    else
        res_weight = -((-arg->weight - 1) / 2 + 1);

    /*
     * Digits to compute: everything down to rscale, plus one extra decimal
     * digit so the final rounding is correct.  rscale may be negative.
     */
    if (rscale + 1 >= 0)
        res_ndigits = res_weight + 1 + (rscale + DEC_DIGITS) / DEC_DIGITS;
    else
        res_ndigits = res_weight + 1 - (-rscale - 1) / DEC_DIGITS;
    res_ndigits = Max(res_ndigits, 1);

    /* Two source digits are consumed per result digit after the point. */
    src_ndigits = arg->weight + 1 + (res_ndigits - res_weight - 1) * 2;
    src_ndigits = Max(src_ndigits, 1);

    nv_init(&n);
    nv_init(&s);

    nv_alloc(&n, src_ndigits);
    for (i = 0; i < src_ndigits; i++)
        n.digits[i] = (i < arg->ndigits) ? arg->digits[i] : 0;
    n.weight = src_ndigits - 1;
    n.sign = NSIGN_POS;
    n.dscale = 0;
    nv_strip(&n);

    isqrt_var(&n, &s);
    nv_free(&n);

    /*
     * s is the integer root; reinterpret it so that its most significant
     * digit sits at res_weight.
     */
    s.weight = res_weight + (s.weight - (res_ndigits - 1));
    s.sign = NSIGN_POS;
    round_var(&s, rscale);
    nv_strip(&s);

    free(res->buf);
    *res = s;
}

/* ------------------------------------------------- transcendental helpers */

static double
var_to_double(const Numeric *var)
{
    char       *tmp = get_str_from_var(var);
    double      val = strtod(tmp, NULL);

    free(tmp);
    return val;
}

/*
 * Estimate log10(abs(ln(var))) so callers can pick a working scale for ln().
 */
static int
estimate_ln_dweight(const Numeric *var)
{
    int         ln_dweight;

    if (var->sign != NSIGN_POS)
        return 0;

    if (cmp_var(var, &const_pt9) >= 0 && cmp_var(var, &const_1pt1) <= 0)
    {
        /* Near 1, use ln(1+x) ~= x to avoid catastrophic cancellation. */
        Numeric     x;

        nv_init(&x);
        sub_var(var, &const_one, &x);
        if (x.ndigits > 0)
            ln_dweight = x.weight * DEC_DIGITS + (int) log10((double) x.digits[0]);
        else
            ln_dweight = 0;         /* ln(1) == 0 exactly */
        nv_free(&x);
    }
    else if (var->ndigits > 0)
    {
        int         digits = var->digits[0];
        int         dweight = var->weight * DEC_DIGITS;
        double      lnv;

        if (var->ndigits > 1)
        {
            digits = digits * NBASE + var->digits[1];
            dweight -= DEC_DIGITS;
        }
        /* var ~= digits * 10^dweight, so ln(var) ~= ln(digits) + dweight*ln(10) */
        lnv = log((double) digits) + dweight * 2.302585092994046;
        ln_dweight = (int) log10(fabs(lnv));
    }
    else
        ln_dweight = 0;

    return ln_dweight;
}

/*
 * res = e^arg to rscale fractional digits.  Returns false on overflow.
 */
static bool
exp_var_err(const Numeric *arg, Numeric *res, int rscale, bool *overflow)
{
    Numeric     x,
                elem;
    int         ni;
    double      val;
    int         dweight;
    int         ndiv2;
    int         sig_digits;
    int         local_rscale;

    nv_init(&x);
    nv_init(&elem);
    nv_set(&x, arg);

    val = var_to_double(&x);

    if (fabs(val) >= NUM_MAX_RESULT_SCALE * 3)
    {
        nv_free(&x);
        nv_free(&elem);
        if (val > 0)
        {
            if (overflow)
                *overflow = true;
            return false;
        }
        nv_zero(res);
        res->dscale = rscale;
        return true;
    }

    /* decimal weight of the result = x * log10(e) */
    dweight = (int) (val * 0.434294481903252);

    /*
     * Reduce x into roughly [-0.01, 0.01] by halving it ndiv2 times; the
     * Taylor series then converges very quickly.
     */
    if (fabs(val) > 0.01)
    {
        ndiv2 = 1;
        val /= 2;
        while (fabs(val) > 0.01)
        {
            ndiv2++;
            val /= 2;
        }
        local_rscale = x.dscale + ndiv2;
        div_var_int(&x, 1 << ndiv2, 0, &x, local_rscale, true);
    }
    else
        ndiv2 = 0;

    /*
     * Working precision: the answer needs dweight + rscale + 1 significant
     * digits, plus log10(2^ndiv2) for the squarings, plus slack.
     */
    sig_digits = 1 + dweight + rscale + (int) (ndiv2 * 0.301029995663981);
    sig_digits = Max(sig_digits, 0) + 8;
    local_rscale = sig_digits - 1;

    /* exp(x) = 1 + x + x^2/2! + x^3/3! + ... */
    add_var(&const_one, &x, res);

    mul_var(&x, &x, &elem, local_rscale);
    ni = 2;
    div_var_int(&elem, ni, 0, &elem, local_rscale, true);

    while (elem.ndigits != 0)
    {
        add_var(res, &elem, res);
        mul_var(&elem, &x, &elem, local_rscale);
        ni++;
        div_var_int(&elem, ni, 0, &elem, local_rscale, true);
    }

    /* Undo the range reduction by repeated squaring. */
    while (ndiv2-- > 0)
    {
        local_rscale = sig_digits - res->weight * 2 * DEC_DIGITS;
        local_rscale = Max(local_rscale, NUM_MIN_DISPLAY_SCALE);
        mul_var(res, res, res, local_rscale);
    }

    round_var(res, rscale);

    nv_free(&x);
    nv_free(&elem);
    return true;
}

/*
 * res = ln(arg) to rscale fractional digits.  Caller must ensure arg > 0.
 */
static void
ln_var(const Numeric *arg, Numeric *res, int rscale)
{
    Numeric     x,
                xx,
                elem,
                fact;
    int         ni;
    int         nsqrt;
    int         local_rscale;

    nv_init(&x);
    nv_init(&xx);
    nv_init(&elem);
    nv_init(&fact);

    nv_set(&x, arg);
    nv_set(&fact, &const_two);

    /*
     * Reduce x into (0.9, 1.1) by repeated square roots, remembering the
     * factor 2^nsqrt we will have to multiply the series result by.
     */
    nsqrt = 0;
    while (cmp_var(&x, &const_pt9) <= 0)
    {
        local_rscale = rscale - x.weight * DEC_DIGITS / 2 + 8;
        sqrt_var(&x, &x, local_rscale);
        mul_var(&fact, &const_two, &fact, 0);
        nsqrt++;
    }
    while (cmp_var(&x, &const_1pt1) >= 0)
    {
        local_rscale = rscale - x.weight * DEC_DIGITS / 2 + 8;
        sqrt_var(&x, &x, local_rscale);
        mul_var(&fact, &const_two, &fact, 0);
        nsqrt++;
    }

    /*
     * 0.5 * ln((1+z)/(1-z)) = z + z^3/3 + z^5/5 + ...  with z = (x-1)/(x+1),
     * which the reduction above keeps small.
     */
    local_rscale = rscale + (int) ((nsqrt + 1) * 0.301029995663981) + 8;

    sub_var(&x, &const_one, res);
    add_var(&x, &const_one, &elem);
    div_var(res, &elem, res, local_rscale, true);
    nv_set(&xx, res);
    mul_var(res, res, &x, local_rscale);

    ni = 1;
    for (;;)
    {
        ni += 2;
        mul_var(&xx, &x, &xx, local_rscale);
        div_var_int(&xx, ni, 0, &elem, local_rscale, true);

        if (elem.ndigits == 0)
            break;

        add_var(res, &elem, res);

        if (elem.weight < (res->weight - local_rscale * 2 / DEC_DIGITS))
            break;
    }

    /* Undo the range reduction and round. */
    mul_var(res, &fact, res, rscale);

    nv_free(&x);
    nv_free(&xx);
    nv_free(&elem);
    nv_free(&fact);
}

/* ------------------------------------------------------------------ power */

#define PWR_OK          0
#define PWR_OVERFLOW    1
#define PWR_ZERO_NEG    2       /* 0 ^ negative */
#define PWR_NEG_FRAC    3       /* negative ^ non-integer */

/* res = base ^ exp for an integer exp.  Chooses the result dscale itself. */
static int
power_var_int(const Numeric *base, int exp, int exp_dscale, Numeric *res)
{
    double      f;
    int         p;
    int         i;
    int         rscale;
    int         sig_digits;
    unsigned int mask;
    bool        neg;
    Numeric     base_prod;
    int         local_rscale;

    /* Approximate log10 of the result, for scale selection and range checks. */
    if (base->ndigits != 0)
    {
        f = base->digits[0];
        p = base->weight * DEC_DIGITS;

        for (i = 1; i < base->ndigits && i * DEC_DIGITS < 16; i++)
        {
            f = f * NBASE + base->digits[i];
            p -= DEC_DIGITS;
        }
        f = exp * (log10(f) + p);
    }
    else
        f = 0;

    if (f > (NUM_WEIGHT_MAX + 1) * DEC_DIGITS)
        return PWR_OVERFLOW;

    /*
     * Choose the result scale.
     *
     * PostgreSQL 16 reworked this: before it, an integral exponent always got
     * a flat NUM_MIN_SIG_DIGITS fractional digits (raised only by the base's
     * own display scale), so 2^10 printed as 1024.0000000000000000 and 10^-20
     * rounded away to 0.0000000000000000.  From 16 on, the scale is measured
     * from the estimated decimal weight of the result instead, so 2^10 keeps
     * 13 fractional digits and 10^-20 keeps 36.  The exponent's display scale
     * only participates from 16 on as well.
     */
#if PGVC_VERNUM >= 116
    rscale = NUM_MIN_SIG_DIGITS - (int) f;
    rscale = Max(rscale, base->dscale);
    rscale = Max(rscale, exp_dscale);
#else
    rscale = NUM_MIN_SIG_DIGITS;
    rscale = Max(rscale, base->dscale);
#endif
    rscale = Max(rscale, NUM_MIN_DISPLAY_SCALE);
    rscale = Min(rscale, NUM_MAX_DISPLAY_SCALE);

    /*
     * If the result is far below the last digit we would keep, skip the work
     * and return a zero carrying the scale we settled on.
     */
    if (f + 1 < -NUM_MAX_DISPLAY_SCALE)
    {
        nv_zero(res);
#if PGVC_VERNUM >= 116
        res->dscale = NUM_MAX_DISPLAY_SCALE;
#else
        res->dscale = rscale;
#endif
        return PWR_OK;
    }

    switch (exp)
    {
        case 0:
            /* 0^0 is 1, as SQL:2003 requires */
            nv_set(res, &const_one);
            res->dscale = rscale;
            return PWR_OK;
        case 1:
            nv_set(res, base);
            round_var(res, rscale);
            return PWR_OK;
        case -1:
            if (!div_var(&const_one, base, res, rscale, true))
                return PWR_ZERO_NEG;
            return PWR_OK;
        case 2:
            mul_var(base, base, res, rscale);
            return PWR_OK;
        default:
            break;
    }

    if (base->ndigits == 0)
    {
        if (exp < 0)
            return PWR_ZERO_NEG;
        nv_zero(res);
        res->dscale = rscale;
        return PWR_OK;
    }

    /*
     * Binary exponentiation.  Each multiplication is done at a local scale
     * that keeps a constant number of significant digits.
     */
    sig_digits = 1 + rscale + (int) f;
    sig_digits += (int) log(fabs((double) exp)) + 8;

    neg = (exp < 0);
    mask = (unsigned int) (exp < 0 ? -(int64) exp : (int64) exp);

    nv_init(&base_prod);
    nv_set(&base_prod, base);

    if (mask & 1)
        nv_set(res, base);
    else
        nv_set(res, &const_one);

    while ((mask >>= 1) > 0)
    {
        local_rscale = sig_digits - 2 * base_prod.weight * DEC_DIGITS;
        local_rscale = Min(local_rscale, 2 * base_prod.dscale);
        local_rscale = Max(local_rscale, NUM_MIN_DISPLAY_SCALE);

        mul_var(&base_prod, &base_prod, &base_prod, local_rscale);

        if (mask & 1)
        {
            local_rscale = sig_digits -
                (base_prod.weight + res->weight) * DEC_DIGITS;
            local_rscale = Min(local_rscale, base_prod.dscale + res->dscale);
            local_rscale = Max(local_rscale, NUM_MIN_DISPLAY_SCALE);

            mul_var(&base_prod, res, res, local_rscale);
        }

        /* Once the weight can't fit in an int16 the answer must overflow. */
        if (base_prod.weight > NUM_WEIGHT_MAX || res->weight > NUM_WEIGHT_MAX)
        {
            if (!neg)
            {
                nv_free(&base_prod);
                return PWR_OVERFLOW;
            }
            nv_zero(res);
            neg = false;
            break;
        }
    }

    nv_free(&base_prod);

    if (neg)
    {
        if (!div_var(&const_one, res, res, rscale, true))
            return PWR_ZERO_NEG;
    }
    else
        round_var(res, rscale);

    return PWR_OK;
}

/* res = base ^ exp for arbitrary finite operands. */
static int
power_var(const Numeric *base, const Numeric *exp, Numeric *res)
{
    int         res_sign;
    Numeric     abs_base;
    Numeric     ln_base;
    Numeric     ln_num;
    int         ln_dweight;
    int         rscale;
    int         sig_digits;
    int         local_rscale;
    double      val;
    bool        overflow = false;
    int         rc = PWR_OK;

    /* An exact integer exponent that fits in int gets the exact algorithm. */
    if (exp->ndigits == 0 || exp->ndigits <= exp->weight + 1)
    {
        int64       expval64;
        bool        fits = true;
        int         i;

        if (exp->weight > 2)
            fits = false;   /* >= 10^12, cannot be an int32 */
        else
        {
            expval64 = 0;
            for (i = 0; i <= exp->weight; i++)
            {
                expval64 = expval64 * NBASE +
                    (i < exp->ndigits ? exp->digits[i] : 0);
            }
            if (exp->sign == NSIGN_NEG)
                expval64 = -expval64;
            if (expval64 < INT32_MIN || expval64 > INT32_MAX)
                fits = false;
        }

        if (fits)
            return power_var_int(base, (int) expval64, exp->dscale, res);
    }

    /* 0 ^ non-integer; power_var_int() has already handled 0 ^ integer. */
    if (cmp_var(base, &const_zero) == 0)
    {
        nv_set(res, &const_zero);
        res->dscale = NUM_MIN_SIG_DIGITS;
        return PWR_OK;
    }

    nv_init(&abs_base);
    nv_init(&ln_base);
    nv_init(&ln_num);

    if (base->sign == NSIGN_NEG)
    {
        /* A negative base demands an integer exponent. */
        if (exp->ndigits > 0 && exp->ndigits > exp->weight + 1)
        {
            nv_free(&abs_base);
            nv_free(&ln_base);
            nv_free(&ln_num);
            return PWR_NEG_FRAC;
        }
        if (exp->ndigits > 0 && exp->ndigits == exp->weight + 1 &&
            (exp->digits[exp->ndigits - 1] & 1))
            res_sign = NSIGN_NEG;
        else
            res_sign = NSIGN_POS;

        nv_set(&abs_base, base);
        abs_base.sign = NSIGN_POS;
        base = &abs_base;
    }
    else
        res_sign = NSIGN_POS;

    /*
     * Low-precision pass to estimate the weight of exp*ln(base), which sets
     * both the result scale and the working scale for the real pass.
     */
    ln_dweight = estimate_ln_dweight(base);

    local_rscale = 8 - ln_dweight;
    local_rscale = Max(local_rscale, NUM_MIN_DISPLAY_SCALE);

    ln_var(base, &ln_base, local_rscale);
    mul_var(&ln_base, exp, &ln_num, local_rscale);

    val = var_to_double(&ln_num);

    if (fabs(val) > NUM_MAX_RESULT_SCALE * 3.01)
    {
        nv_free(&abs_base);
        nv_free(&ln_base);
        nv_free(&ln_num);
        if (val > 0)
            return PWR_OVERFLOW;
        nv_zero(res);
        res->dscale = NUM_MAX_DISPLAY_SCALE;
        return PWR_OK;
    }

    val *= 0.434294481903252;       /* approximate decimal result weight */

    rscale = NUM_MIN_SIG_DIGITS - (int) val;
    rscale = Max(rscale, base->dscale);
    rscale = Max(rscale, exp->dscale);
    rscale = Max(rscale, NUM_MIN_DISPLAY_SCALE);
    rscale = Min(rscale, NUM_MAX_DISPLAY_SCALE);

    sig_digits = rscale + (int) val;
    sig_digits = Max(sig_digits, 0);

    local_rscale = sig_digits - ln_dweight + 8;
    local_rscale = Max(local_rscale, NUM_MIN_DISPLAY_SCALE);

    ln_var(base, &ln_base, local_rscale);
    mul_var(&ln_base, exp, &ln_num, local_rscale);

    if (!exp_var_err(&ln_num, res, rscale, &overflow))
        rc = PWR_OVERFLOW;
    else if (res_sign == NSIGN_NEG && res->ndigits > 0)
        res->sign = NSIGN_NEG;

    nv_free(&abs_base);
    nv_free(&ln_base);
    nv_free(&ln_num);
    return rc;
}

/* ----------------------------------------------------------- text input */

/*
 * Parse a plain decimal number (no sign handling beyond an optional leading
 * one, which the caller normally overrides).  *endptr is set past the number.
 */
static bool
set_var_from_str(const char *cp, const char **endptr, Numeric *dest)
{
    bool            have_dp = false;
    int             i;
    unsigned char  *decdigits;
    int             sign = NSIGN_POS;
    int             dweight = -1;   /* decimal weight of the first digit */
    int             ddigits;
    int             dscale = 0;
    int             weight;
    int             ndigits;
    int             offset;
    NumDigit       *digits;

    if (*cp == '+')
        cp++;
    else if (*cp == '-')
    {
        sign = NSIGN_NEG;
        cp++;
    }

    if (*cp == '.')
    {
        have_dp = true;
        cp++;
    }

    if (!isdigit((unsigned char) *cp))
        return false;

    /* Collect the decimal digits, with padding on both ends for alignment. */
    decdigits = nm_alloc(strlen(cp) + DEC_DIGITS * 2);
    memset(decdigits, 0, DEC_DIGITS);
    i = DEC_DIGITS;

    while (*cp)
    {
        if (isdigit((unsigned char) *cp))
        {
            decdigits[i++] = (unsigned char) (*cp++ - '0');
            if (!have_dp)
                dweight++;
            else
                dscale++;
        }
        else if (*cp == '.')
        {
            if (have_dp)
            {
                free(decdigits);
                return false;
            }
            have_dp = true;
            cp++;
            if (ALLOW_UNDERSCORES && *cp == '_')    /* "1._2" is not valid */
            {
                free(decdigits);
                return false;
            }
        }
        else if (ALLOW_UNDERSCORES && *cp == '_')
        {
            cp++;               /* digit separators must be surrounded by digits */
            if (!isdigit((unsigned char) *cp))
            {
                free(decdigits);
                return false;
            }
        }
        else
            break;
    }

    ddigits = i - DEC_DIGITS;
    memset(decdigits + i, 0, DEC_DIGITS - 1);

    /* Exponent, if any: it shifts the weight and reduces the display scale. */
    if (*cp == 'e' || *cp == 'E')
    {
        int64       exponent = 0;
        bool        neg = false;
        const char *ep = cp + 1;

        if (*ep == '+')
            ep++;
        else if (*ep == '-')
        {
            neg = true;
            ep++;
        }
        if (!isdigit((unsigned char) *ep))
        {
            free(decdigits);
            return false;
        }
        while (*ep)
        {
            if (isdigit((unsigned char) *ep))
            {
                exponent = exponent * 10 + (*ep++ - '0');
                if (exponent > INT32_MAX / 2)
                {
                    free(decdigits);
                    return false;   /* value overflows numeric format */
                }
            }
            else if (ALLOW_UNDERSCORES && *ep == '_')
            {
                ep++;
                if (!isdigit((unsigned char) *ep))
                {
                    free(decdigits);
                    return false;
                }
            }
            else
                break;
        }
        if (neg)
            exponent = -exponent;
        cp = ep;

        dweight += (int) exponent;
        dscale -= (int) exponent;
        if (dscale < 0)
            dscale = 0;
    }

    /* Convert the decimal digit string into base-NBASE digits. */
    if (dweight >= 0)
        weight = (dweight + 1 + DEC_DIGITS - 1) / DEC_DIGITS - 1;
    else
        weight = -((-dweight - 1) / DEC_DIGITS + 1);
    offset = (weight + 1) * DEC_DIGITS - (dweight + 1);
    ndigits = (ddigits + offset + DEC_DIGITS - 1) / DEC_DIGITS;

    nv_alloc(dest, ndigits);
    dest->sign = sign;
    dest->weight = weight;
    dest->dscale = dscale;

    i = DEC_DIGITS - offset;
    digits = dest->digits;
    while (ndigits-- > 0)
    {
        *digits++ = (NumDigit) (((decdigits[i] * 10 + decdigits[i + 1]) * 10 +
                                 decdigits[i + 2]) * 10 + decdigits[i + 3]);
        i += DEC_DIGITS;
    }
    free(decdigits);

    nv_strip(dest);
    *endptr = cp;
    return true;
}

static void
int64_to_var(int64 val, Numeric *var)
{
    uint64      uval;
    NumDigit    tmp[20 / DEC_DIGITS + 2];
    int         nd = 0;
    int         i;

    if (val < 0)
    {
        uval = (uint64) (-(val + 1)) + 1;   /* avoids INT64_MIN overflow */
    }
    else
        uval = (uint64) val;

    if (val == 0)
    {
        nv_zero(var);
        var->dscale = 0;
        return;
    }

    do
    {
        tmp[nd++] = (NumDigit) (uval % NBASE);
        uval /= NBASE;
    } while (uval);

    nv_alloc(var, nd);
    for (i = 0; i < nd; i++)
        var->digits[i] = tmp[nd - 1 - i];
    var->weight = nd - 1;
    var->sign = (val < 0) ? NSIGN_NEG : NSIGN_POS;
    var->dscale = 0;
}

static int
xdigit_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* Parse 0x / 0o / 0b integer syntax (base is 16, 8 or 2). */
static bool
set_var_from_nondecimal_str(const char *cp, int sign, int base,
                            Numeric *dest, const char **endptr)
{
    const char *firstdigit = cp;
    int64       tmp = 0;
    int64       mul = 1;
    Numeric     tv;

    nv_init(&tv);
    nv_zero(dest);
    dest->dscale = 0;

    for (;;)
    {
        int         dv = -1;

        if (base == 16)
            dv = xdigit_value(*cp);
        else if (*cp >= '0' && *cp < '0' + base)
            dv = *cp - '0';

        if (dv >= 0)
        {
            if (mul > INT64_MAX / base)
            {
                /* Flush the accumulated group: dest = dest * mul + tmp */
                int64_to_var(mul, &tv);
                mul_var(dest, &tv, dest, 0);
                int64_to_var(tmp, &tv);
                add_var(dest, &tv, dest);
                tmp = 0;
                mul = 1;
            }
            tmp = tmp * base + dv;
            mul *= base;
            cp++;
        }
        else if (*cp == '_')
        {
            cp++;
            if (base == 16 ? xdigit_value(*cp) < 0
                : !(*cp >= '0' && *cp < '0' + base))
            {
                nv_free(&tv);
                return false;
            }
        }
        else
            break;
    }

    if (cp == firstdigit)
    {
        nv_free(&tv);
        return false;
    }

    int64_to_var(mul, &tv);
    mul_var(dest, &tv, dest, 0);
    int64_to_var(tmp, &tv);
    add_var(dest, &tv, dest);
    nv_free(&tv);

    dest->sign = (dest->ndigits == 0) ? NSIGN_POS : sign;
    dest->dscale = 0;
    *endptr = cp;
    return true;
}

static int
strncasecmp_ascii(const char *a, const char *b, size_t n)
{
    size_t      i;

    for (i = 0; i < n; i++)
    {
        int ca = tolower((unsigned char) a[i]);
        int cb = tolower((unsigned char) b[i]);

        if (ca != cb)
            return ca - cb;
        if (ca == 0)
            return 0;
    }
    return 0;
}

/* ---------------------------------------------------------- text output */

static char *
get_str_from_var(const Numeric *var)
{
    int         dscale = var->dscale;
    char       *str;
    char       *cp;
    char       *endcp;
    int         i;
    int         d;
    NumDigit    dig;
    NumDigit    d1;

    i = (var->weight + 1) * DEC_DIGITS;
    if (i <= 0)
        i = 1;

    str = nm_alloc((size_t) i + (size_t) dscale + DEC_DIGITS + 8);
    cp = str;

    if (var->sign == NSIGN_NEG)
        *cp++ = '-';

    /* integer part */
    if (var->weight < 0)
    {
        d = var->weight + 1;
        *cp++ = '0';
    }
    else
    {
        for (d = 0; d <= var->weight; d++)
        {
            bool        putit = (d > 0);

            dig = (d < var->ndigits) ? var->digits[d] : 0;

            d1 = dig / 1000;
            dig -= d1 * 1000;
            putit |= (d1 > 0);
            if (putit)
                *cp++ = (char) (d1 + '0');
            d1 = dig / 100;
            dig -= d1 * 100;
            putit |= (d1 > 0);
            if (putit)
                *cp++ = (char) (d1 + '0');
            d1 = dig / 10;
            dig -= d1 * 10;
            putit |= (d1 > 0);
            if (putit)
                *cp++ = (char) (d1 + '0');
            *cp++ = (char) (dig + '0');
        }
    }

    /* fractional part: emit whole NBASE digits, then cut at dscale */
    if (dscale > 0)
    {
        *cp++ = '.';
        endcp = cp + dscale;
        for (i = 0; i < dscale; d++, i += DEC_DIGITS)
        {
            dig = (d >= 0 && d < var->ndigits) ? var->digits[d] : 0;
            d1 = dig / 1000;
            dig -= d1 * 1000;
            *cp++ = (char) (d1 + '0');
            d1 = dig / 100;
            dig -= d1 * 100;
            *cp++ = (char) (d1 + '0');
            d1 = dig / 10;
            dig -= d1 * 10;
            *cp++ = (char) (d1 + '0');
            *cp++ = (char) (dig + '0');
        }
        cp = endcp;
    }

    *cp = '\0';
    return str;
}

/* ============================================================== public API */

/* -1, 0 or 1 according to the sign of v (infinities included). */
static int
var_sign(const Numeric *v)
{
    if (IS_PINF(v))
        return 1;
    if (IS_NINF(v))
        return -1;
    if (v->ndigits == 0)
        return 0;
    return (v->sign == NSIGN_NEG) ? -1 : 1;
}

/* True if v has no fractional part.  Infinities count as integral; NaN does not. */
static bool
var_is_integral(const Numeric *v)
{
    if (IS_NAN(v))
        return false;
    if (IS_INF(v))
        return true;
    return (v->ndigits == 0 || v->ndigits <= v->weight + 1);
}

/* typmod is ((precision << 16) | scale) + VARHDRSZ; -1 means unconstrained. */
#define TYPMOD_VARHDRSZ 4

static bool
typmod_valid(int32 typmod)
{
    return typmod >= TYPMOD_VARHDRSZ;
}

static int
typmod_precision(int32 typmod)
{
    return ((typmod - TYPMOD_VARHDRSZ) >> 16) & 0xffff;
}

/*
 * From PostgreSQL 15 the scale field is 11 bits and may be negative, so it
 * has to be sign-extended; before that a typmod could only carry a scale in
 * [0, precision] and the field was read unsigned.
 */
static int
typmod_scale(int32 typmod)
{
#if PGVC_VERNUM >= 115
    return (((typmod - TYPMOD_VARHDRSZ) & 0x7ff) ^ 1024) - 1024;
#else
    return (typmod - TYPMOD_VARHDRSZ) & 0xffff;
#endif
}

Numeric *
num_from_str(const char *s, bool *ok)
{
    const char *cp;
    const char *numstart;
    int         sign = NSIGN_POS;
    int         base = 10;
    Numeric     v;

    if (ok)
        *ok = false;
    if (s == NULL)
        return NULL;

    cp = s;
    while (*cp && isspace((unsigned char) *cp))
        cp++;

    numstart = cp;
    if (*cp == '+')
        cp++;
    else if (*cp == '-')
    {
        sign = NSIGN_NEG;
        cp++;
    }

    /* Everything legal but NaN/Infinity starts with a digit or a point. */
    if (!isdigit((unsigned char) *cp) && *cp != '.')
    {
        Numeric    *r;

        if (strncasecmp_ascii(numstart, "NaN", 3) == 0)
        {
            r = nv_new_special(NSIGN_NAN);
            cp = numstart + 3;
        }
        else if (HAVE_INFINITIES &&
                 strncasecmp_ascii(cp, "Infinity", 8) == 0)
        {
            r = nv_new_special(sign == NSIGN_POS ? NSIGN_PINF : NSIGN_NINF);
            cp += 8;
        }
        else if (HAVE_INFINITIES && strncasecmp_ascii(cp, "inf", 3) == 0)
        {
            r = nv_new_special(sign == NSIGN_POS ? NSIGN_PINF : NSIGN_NINF);
            cp += 3;
        }
        else
            return NULL;        /* before 14 the infinities are syntax errors */

        while (*cp)
        {
            if (!isspace((unsigned char) *cp))
            {
                num_free(r);
                return NULL;
            }
            cp++;
        }
        if (ok)
            *ok = true;
        return r;
    }

    if (ALLOW_NONDECIMAL && cp[0] == '0')
    {
        switch (cp[1])
        {
            case 'x': case 'X': base = 16; break;
            case 'o': case 'O': base = 8;  break;
            case 'b': case 'B': base = 2;  break;
            default:            base = 10; break;
        }
    }

    nv_init(&v);
    if (base == 10)
    {
        if (!set_var_from_str(cp, &cp, &v))
        {
            nv_free(&v);
            return NULL;
        }
        v.sign = sign;
    }
    else if (!set_var_from_nondecimal_str(cp + 2, sign, base, &v, &cp))
    {
        nv_free(&v);
        return NULL;
    }

    while (*cp)
    {
        if (!isspace((unsigned char) *cp))
        {
            nv_free(&v);
            return NULL;
        }
        cp++;
    }

    /*
     * PostgreSQL stores weight in an int16 and dscale in 14 bits, and rejects
     * literals that do not fit with "value overflows numeric format".
     */
    if (v.dscale > NUM_DSCALE_MAX ||
        v.weight > NUM_WEIGHT_MAX || v.weight < -NUM_WEIGHT_MAX - 1)
    {
        nv_free(&v);
        return NULL;
    }

    if (ok)
        *ok = true;
    return nv_export(&v);
}

Numeric *
num_from_int64(int64 v)
{
    Numeric     t;

    nv_init(&t);
    int64_to_var(v, &t);
    return nv_export(&t);
}

/*
 * PostgreSQL converts float8 to numeric through the value's "%.15g" text
 * form, so (1::float8/3)::numeric is 0.333333333333333, not a 17-digit value.
 */
Numeric *
num_from_double(double d)
{
    char        buf[DBL_DIG + 100];
    const char *endptr;
    Numeric     t;

    if (isnan(d))
        return nv_new_special(NSIGN_NAN);
    if (isinf(d))
    {
        /*
         * Before 14 there is no numeric infinity to convert to; PostgreSQL
         * raises "cannot convert infinity to numeric" and this API has no
         * error channel, so report the failure as NULL.
         */
        if (!HAVE_INFINITIES)
            return NULL;
        return nv_new_special(d < 0 ? NSIGN_NINF : NSIGN_PINF);
    }

    snprintf(buf, sizeof(buf), "%.*g", DBL_DIG, d);

    nv_init(&t);
    if (!set_var_from_str(buf, &endptr, &t))
    {
        nv_free(&t);
        return nv_new_special(NSIGN_NAN);   /* cannot happen */
    }
    return nv_export(&t);
}

Numeric *
num_copy(const Numeric *a)
{
    Numeric     t;

    if (a == NULL)
        return NULL;
    nv_init(&t);
    nv_set(&t, a);
    return nv_export(&t);
}

void
num_free(Numeric *a)
{
    if (a == NULL)
        return;
    free(a->buf);
    free(a);
}

char *
num_to_str(const Numeric *a)
{
    if (a == NULL)
        return NULL;
    if (IS_NAN(a))
        return strdup("NaN");
    if (IS_PINF(a))
        return strdup("Infinity");
    if (IS_NINF(a))
        return strdup("-Infinity");
    return get_str_from_var(a);
}

double
num_to_double(const Numeric *a)
{
    char       *s;
    double      d;

    if (IS_NAN(a))
        return (double) NAN;
    if (IS_PINF(a))
        return (double) INFINITY;
    if (IS_NINF(a))
        return -(double) INFINITY;

    s = get_str_from_var(a);
    d = strtod(s, NULL);
    free(s);
    return d;
}

/*
 * Exact conversion to int64.  Unlike PostgreSQL's numeric->bigint cast this
 * does not round: a fractional value is rejected, as the header specifies.
 */
bool
num_to_int64(const Numeric *a, int64 *out)
{
    int64       val = 0;
    int         i;

    if (IS_SPECIAL(a))
        return false;
    if (!var_is_integral(a))
        return false;

    if (a->ndigits == 0)
    {
        *out = 0;
        return true;
    }

    /* Accumulate negatively so that INT64_MIN is representable. */
    val = -(int64) a->digits[0];
    for (i = 1; i <= a->weight; i++)
    {
        if (val < INT64_MIN / NBASE || val > INT64_MAX / NBASE)
            return false;
        val *= NBASE;
        if (i < a->ndigits)
        {
            int64   d = a->digits[i];

            if (val < INT64_MIN + d)
                return false;
            val -= d;
        }
    }

    if (a->sign != NSIGN_NEG)
    {
        if (val == INT64_MIN)
            return false;
        val = -val;
    }
    *out = val;
    return true;
}

/*
 * Total ordering.  NaN sorts above everything else (including Infinity),
 * which is PostgreSQL's arbitrary but consistent choice.
 */
int
num_cmp(const Numeric *a, const Numeric *b)
{
    if (IS_SPECIAL(a))
    {
        if (IS_NAN(a))
            return IS_NAN(b) ? 0 : 1;
        if (IS_PINF(a))
        {
            if (IS_NAN(b))
                return -1;
            return IS_PINF(b) ? 0 : 1;
        }
        /* a is -Infinity */
        return IS_NINF(b) ? 0 : -1;
    }
    if (IS_SPECIAL(b))
        return IS_NINF(b) ? 1 : -1;

    return cmp_var(a, b);
}

Numeric *
num_add(const Numeric *a, const Numeric *b)
{
    Numeric     r;

    if (IS_SPECIAL(a) || IS_SPECIAL(b))
    {
        if (IS_NAN(a) || IS_NAN(b))
            return nv_new_special(NSIGN_NAN);
        if (IS_PINF(a))
            return nv_new_special(IS_NINF(b) ? NSIGN_NAN : NSIGN_PINF);
        if (IS_NINF(a))
            return nv_new_special(IS_PINF(b) ? NSIGN_NAN : NSIGN_NINF);
        return nv_new_special(IS_PINF(b) ? NSIGN_PINF : NSIGN_NINF);
    }

    nv_init(&r);
    add_var(a, b, &r);
    return nv_export(&r);
}

Numeric *
num_sub(const Numeric *a, const Numeric *b)
{
    Numeric     r;

    if (IS_SPECIAL(a) || IS_SPECIAL(b))
    {
        if (IS_NAN(a) || IS_NAN(b))
            return nv_new_special(NSIGN_NAN);
        if (IS_PINF(a))
            return nv_new_special(IS_PINF(b) ? NSIGN_NAN : NSIGN_PINF);
        if (IS_NINF(a))
            return nv_new_special(IS_NINF(b) ? NSIGN_NAN : NSIGN_NINF);
        return nv_new_special(IS_PINF(b) ? NSIGN_NINF : NSIGN_PINF);
    }

    nv_init(&r);
    sub_var(a, b, &r);
    return nv_export(&r);
}

Numeric *
num_mul(const Numeric *a, const Numeric *b)
{
    Numeric     r;

    if (IS_SPECIAL(a) || IS_SPECIAL(b))
    {
        int     s;

        if (IS_NAN(a) || IS_NAN(b))
            return nv_new_special(NSIGN_NAN);
        if (IS_INF(a))
        {
            s = var_sign(b);
            if (s == 0)
                return nv_new_special(NSIGN_NAN);   /* Inf * 0 */
            if (IS_PINF(a))
                return nv_new_special(s > 0 ? NSIGN_PINF : NSIGN_NINF);
            return nv_new_special(s > 0 ? NSIGN_NINF : NSIGN_PINF);
        }
        s = var_sign(a);
        if (s == 0)
            return nv_new_special(NSIGN_NAN);
        if (IS_PINF(b))
            return nv_new_special(s > 0 ? NSIGN_PINF : NSIGN_NINF);
        return nv_new_special(s > 0 ? NSIGN_NINF : NSIGN_PINF);
    }

    /*
     * The `*` operator asks for the exact product: rscale is the sum of the
     * input display scales, so 2 * 3.50 is 7.00.
     */
    nv_init(&r);
    mul_var(a, b, &r, a->dscale + b->dscale);
    if (r.dscale > NUM_DSCALE_MAX)
        round_var(&r, NUM_DSCALE_MAX);
    return nv_export(&r);
}

/*
 * Shared special-value handling for the three division entry points.
 * Returns true when the result is fully determined by the specials.
 */
static bool
div_special(const Numeric *a, const Numeric *b, Numeric **out, bool *divzero)
{
    if (!IS_SPECIAL(a) && !IS_SPECIAL(b))
        return false;

    if (IS_NAN(a) || IS_NAN(b))
    {
        *out = nv_new_special(NSIGN_NAN);
        return true;
    }
    if (IS_INF(a))
    {
        int     s;

        if (IS_SPECIAL(b))
        {
            *out = nv_new_special(NSIGN_NAN);   /* Inf / [-]Inf */
            return true;
        }
        s = var_sign(b);
        if (s == 0)
        {
            if (divzero)
                *divzero = true;
            *out = NULL;
            return true;
        }
        if (IS_PINF(a))
            *out = nv_new_special(s > 0 ? NSIGN_PINF : NSIGN_NINF);
        else
            *out = nv_new_special(s > 0 ? NSIGN_NINF : NSIGN_PINF);
        return true;
    }

    /* finite / [-]Infinity: numeric has no underflow, so just return zero */
    *out = num_from_int64(0);
    return true;
}

Numeric *
num_div(const Numeric *a, const Numeric *b, bool *divzero)
{
    Numeric     r;
    Numeric    *sp;
    int         rscale;

    if (divzero)
        *divzero = false;
    if (div_special(a, b, &sp, divzero))
        return sp;

    rscale = select_div_scale(a, b);

    nv_init(&r);
    if (!div_var(a, b, &r, rscale, true))
    {
        nv_free(&r);
        if (divzero)
            *divzero = true;
        return NULL;
    }
    return nv_export(&r);
}

Numeric *
num_div_trunc(const Numeric *a, const Numeric *b, bool *divzero)
{
    Numeric     r;
    Numeric    *sp;

    if (divzero)
        *divzero = false;
    if (div_special(a, b, &sp, divzero))
        return sp;

    nv_init(&r);
    if (!div_var(a, b, &r, 0, false))
    {
        nv_free(&r);
        if (divzero)
            *divzero = true;
        return NULL;
    }
    return nv_export(&r);
}

Numeric *
num_mod(const Numeric *a, const Numeric *b, bool *divzero)
{
    Numeric     r;

    if (divzero)
        *divzero = false;

    if (IS_SPECIAL(a) || IS_SPECIAL(b))
    {
        if (IS_NAN(a) || IS_NAN(b))
            return nv_new_special(NSIGN_NAN);
        if (IS_INF(a))
        {
            if (var_sign(b) == 0)
            {
                if (divzero)
                    *divzero = true;
                return NULL;
            }
            return nv_new_special(NSIGN_NAN);   /* Inf % nonzero */
        }
        /* b is [-]Infinity: the remainder is a itself */
        return num_copy(a);
    }

    nv_init(&r);
    if (!mod_var(a, b, &r))
    {
        nv_free(&r);
        if (divzero)
            *divzero = true;
        return NULL;
    }
    return nv_export(&r);
}

Numeric *
num_neg(const Numeric *a)
{
    Numeric    *r = num_copy(a);

    if (IS_PINF(r))
        r->sign = NSIGN_NINF;
    else if (IS_NINF(r))
        r->sign = NSIGN_PINF;
    else if (!IS_NAN(r) && r->ndigits != 0)
        r->sign = (r->sign == NSIGN_NEG) ? NSIGN_POS : NSIGN_NEG;

    return r;
}

Numeric *
num_abs(const Numeric *a)
{
    Numeric    *r = num_copy(a);

    if (IS_NINF(r))
        r->sign = NSIGN_PINF;
    else if (!IS_SPECIAL(r))
        r->sign = NSIGN_POS;

    return r;
}

Numeric *
num_round(const Numeric *a, int scale)
{
    Numeric     t;

    if (IS_SPECIAL(a))
        return num_copy(a);

    scale = Max(scale, -(NUM_WEIGHT_MAX + 1) * DEC_DIGITS - 1);
    scale = Min(scale, ROUND_SCALE_CAP);

    nv_init(&t);
    nv_set(&t, a);
    round_var(&t, scale);
    if (scale < 0)
        t.dscale = 0;           /* a negative display scale is not a thing */
    return nv_export(&t);
}

Numeric *
num_trunc(const Numeric *a, int scale)
{
    Numeric     t;

    if (IS_SPECIAL(a))
        return num_copy(a);

    scale = Max(scale, -(NUM_WEIGHT_MAX + 1) * DEC_DIGITS);
    scale = Min(scale, ROUND_SCALE_CAP);

    nv_init(&t);
    nv_set(&t, a);
    trunc_var(&t, scale);
    if (scale < 0)
        t.dscale = 0;
    return nv_export(&t);
}

Numeric *
num_ceil(const Numeric *a)
{
    Numeric     t;

    if (IS_SPECIAL(a))
        return num_copy(a);

    nv_init(&t);
    ceil_var(a, &t);
    return nv_export(&t);
}

Numeric *
num_floor(const Numeric *a)
{
    Numeric     t;

    if (IS_SPECIAL(a))
        return num_copy(a);

    nv_init(&t);
    floor_var(a, &t);
    return nv_export(&t);
}

Numeric *
num_pow(const Numeric *a, const Numeric *b, bool *err)
{
    Numeric     r;
    int         sign1,
                sign2;
    int         rc;

    if (err)
        *err = false;

    if (IS_SPECIAL(a) || IS_SPECIAL(b))
    {
        /*
         * POSIX pow(3): NaN^0 == 1 and 1^NaN == 1; otherwise NaN.  PostgreSQL
         * adopted those two special cases in 11; before that any NaN operand
         * simply produced NaN.
         */
        if (IS_NAN(a))
        {
            if (POW_POSIX_NAN && !IS_SPECIAL(b) && cmp_var(b, &const_zero) == 0)
                return num_from_int64(1);
            return nv_new_special(NSIGN_NAN);
        }
        if (IS_NAN(b))
        {
            if (POW_POSIX_NAN && !IS_SPECIAL(a) && cmp_var(a, &const_one) == 0)
                return num_from_int64(1);
            return nv_new_special(NSIGN_NAN);
        }

        sign1 = var_sign(a);
        sign2 = var_sign(b);

        if (sign1 == 0 && sign2 < 0)
            goto pow_error;                     /* 0 ^ negative */
        if (sign1 < 0 && !var_is_integral(b))
            goto pow_error;                     /* negative ^ non-integer */

        if (!IS_SPECIAL(a) && cmp_var(a, &const_one) == 0)
            return num_from_int64(1);           /* 1 ^ anything */
        if (sign2 == 0)
            return num_from_int64(1);           /* anything ^ 0 */
        if (sign1 == 0 && sign2 > 0)
            return num_from_int64(0);

        if (IS_INF(b))
        {
            bool    abs_a_gt_one;

            if (IS_SPECIAL(a))
                abs_a_gt_one = true;
            else
            {
                Numeric absa = *a;

                if (cmp_var(a, &const_minus_one) == 0)
                    return num_from_int64(1);
                absa.sign = NSIGN_POS;
                abs_a_gt_one = (cmp_var(&absa, &const_one) > 0);
            }
            if (abs_a_gt_one == (sign2 > 0))
                return nv_new_special(NSIGN_PINF);
            return num_from_int64(0);
        }

        if (IS_PINF(a))
            return (sign2 > 0) ? nv_new_special(NSIGN_PINF) : num_from_int64(0);

        /* a is -Infinity */
        if (sign2 < 0)
            return num_from_int64(0);
        if (b->ndigits > 0 && b->ndigits == b->weight + 1 &&
            (b->digits[b->ndigits - 1] & 1))
            return nv_new_special(NSIGN_NINF);  /* odd positive exponent */
        return nv_new_special(NSIGN_PINF);
    }

    if (var_sign(a) == 0 && var_sign(b) < 0)
        goto pow_error;

    nv_init(&r);
    rc = power_var(a, b, &r);
    if (rc != PWR_OK)
    {
        nv_free(&r);
        goto pow_error;
    }
    return nv_export(&r);

pow_error:
    if (err)
        *err = true;
    return NULL;
}

Numeric *
num_sqrt(const Numeric *a, bool *err)
{
    Numeric     t;
    int         sweight;
    int         rscale;

    if (err)
        *err = false;

    if (IS_SPECIAL(a))
    {
        if (IS_NINF(a))
        {
            if (err)
                *err = true;
            return NULL;
        }
        return num_copy(a);     /* NaN and Infinity pass through */
    }
    if (a->sign == NSIGN_NEG && a->ndigits != 0)
    {
        if (err)
            *err = true;
        return NULL;
    }

    /*
     * The root has about sweight digits before the point; ask for enough
     * fractional digits to reach NUM_MIN_SIG_DIGITS significant digits.
     */
    sweight = a->weight * DEC_DIGITS / 2 + 1;

    rscale = NUM_MIN_SIG_DIGITS - sweight;
    rscale = Max(rscale, a->dscale);
    rscale = Max(rscale, NUM_MIN_DISPLAY_SCALE);
    rscale = Min(rscale, NUM_MAX_DISPLAY_SCALE);

    nv_init(&t);
    sqrt_var(a, &t, rscale);
    return nv_export(&t);
}

bool
num_is_nan(const Numeric *a)
{
    return a != NULL && IS_NAN(a);
}

Numeric *
num_apply_typmod(const Numeric *a, int32 typmod, bool *overflow)
{
    Numeric     v;
    int         precision;
    int         scale;
    int         maxdigits;
    int         ddigits;
    int         i;

    if (overflow)
        *overflow = false;

    if (IS_SPECIAL(a))
    {
        /* NaN is accepted by any typmod; an infinity fits in no finite one. */
        if (IS_NAN(a) || !typmod_valid(typmod))
            return num_copy(a);
        if (overflow)
            *overflow = true;
        return NULL;
    }

    if (!typmod_valid(typmod))
        return num_copy(a);

    precision = typmod_precision(typmod);
    scale = typmod_scale(typmod);
    maxdigits = precision - scale;

    nv_init(&v);
    nv_set(&v, a);

    round_var(&v, scale);
    if (v.dscale < 0)
        v.dscale = 0;

    /*
     * Overflow must be judged after rounding, since rounding can add a digit
     * before the point.  Leading zero digits do not count.
     */
    ddigits = (v.weight + 1) * DEC_DIGITS;
    if (ddigits > maxdigits)
    {
        for (i = 0; i < v.ndigits; i++)
        {
            int     dig = v.digits[i];

            if (dig)
            {
                if (dig < 10)
                    ddigits -= 3;
                else if (dig < 100)
                    ddigits -= 2;
                else if (dig < 1000)
                    ddigits -= 1;

                if (ddigits > maxdigits)
                {
                    nv_free(&v);
                    if (overflow)
                        *overflow = true;
                    return NULL;
                }
                break;
            }
            ddigits -= DEC_DIGITS;
        }
    }

    return nv_export(&v);
}

/*
 * The display scale.  PostgreSQL's scale() returns NULL for NaN and the
 * infinities; there is no NULL here, so we report 0 for those.
 */
bool
num_is_infinite(const Numeric *a)
{
    return a != NULL && IS_INF(a);
}

int
num_scale(const Numeric *a)
{
    if (a == NULL || IS_SPECIAL(a))
        return 0;
    return a->dscale;
}

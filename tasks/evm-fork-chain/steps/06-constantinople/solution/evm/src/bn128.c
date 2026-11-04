/*
 * alt_bn128 (BN254 / BN128) precompiles.
 *
 *   bn128_add     -- EIP-196 ECADD    (precompile 0x06)
 *   bn128_mul     -- EIP-196 ECMUL    (precompile 0x07)
 *   bn128_pairing -- EIP-197 ECPAIRING(precompile 0x08)
 *
 * Self-contained: only <stdint.h>/<stddef.h>/<string.h> are used.  Fp is a
 * fixed 4x64-bit limb Montgomery representation; on top of it sits the
 * tower Fp2 = Fp[u]/(u^2+1), Fp6 = Fp2[v]/(v^3-(9+u)), Fp12 = Fp6[w]/(w^2-v).
 * The pairing is the optimal ate pairing with BN parameter
 * x = 4965661367192848881 (Miller loop over the NAF of 6x+2).
 *
 * Not constant-time: this is a reference/testing implementation.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* types                                                              */
/* ------------------------------------------------------------------ */

typedef struct { uint64_t v[4]; } fp;       /* Montgomery form, < p      */
typedef struct { fp c0, c1; } fp2;          /* c0 + c1*u,   u^2 = -1     */
typedef struct { fp2 c0, c1, c2; } fp6;     /* c0 + c1*v + c2*v^2        */
typedef struct { fp6 c0, c1; } fp12;        /* c0 + c1*w,   w^2 = v      */

/* p */
#define FP_P0 0x3c208c16d87cfd47ULL
#define FP_P1 0x97816a916871ca8dULL
#define FP_P2 0xb85045b68181585dULL
#define FP_P3 0x30644e72e131a029ULL
static const uint64_t FP_P[4] = {FP_P0, FP_P1, FP_P2, FP_P3};
static const uint64_t FP_N0 = 0x87d20782e4866389ULL; /* -p^-1 mod 2^64 */
static const fp FP_ONE = {{0xd35d438dc58f0d9dULL,0x0a78eb28f5c70b3dULL,0x666ea36f7879462cULL,0x0e0a77c19a07df2fULL}};   /* R mod p */
static const fp FP_R2  = {{0xf32cfc5b538afa89ULL,0xb5e71911d44501fbULL,0x47ab1eff0a417ff6ULL,0x06d89f71cab8351fULL}};   /* R^2 mod p */
static const fp FP_B   = {{0x7a17caa950ad28d7ULL,0x1f6ac17ae15521b9ULL,0x334bea4e696bd284ULL,0x2a1f6744ce179d8eULL}};   /* 3 */
static const fp2 FP2_B = {{{0x3bf938e377b802a8ULL,0x020b1b273633535dULL,0x26b7edf049755260ULL,0x2514c6324384a86dULL}},{{0x38e7ecccd1dcff67ULL,0x65f0b37d93ce0d3eULL,0xd749d0dd22ac00aaULL,0x0141b9ce4a688d4dULL}}}; /* 3/(9+u) */


/* Frobenius: FQ6_C1[i] = xi^((p^i-1)/3), FQ6_C2[i] = xi^(2(p^i-1)/3), FQ12_C1[i] = xi^((p^i-1)/6) */
static const fp2 FQ6_C1[4] = {
    {{{0xd35d438dc58f0d9dULL,0x0a78eb28f5c70b3dULL,0x666ea36f7879462cULL,0x0e0a77c19a07df2fULL}},{{0x0000000000000000ULL,0x0000000000000000ULL,0x0000000000000000ULL,0x0000000000000000ULL}}},
    {{{0xb5773b104563ab30ULL,0x347f91c8a9aa6454ULL,0x7a007127242e0991ULL,0x1956bcd8118214ecULL}},{{0x6e849f1ea0aa4757ULL,0xaa1c7b6d89f89141ULL,0xb6e713cdfae0ca3aULL,0x26694fbb4e82ebc3ULL}}},
    {{{0x3350c88e13e80b9cULL,0x7dce557cdb5e56b9ULL,0x6001b4b8b615564aULL,0x2682e617020217e0ULL}},{{0x0000000000000000ULL,0x0000000000000000ULL,0x0000000000000000ULL,0x0000000000000000ULL}}},
    {{{0xc9af22f716ad6badULL,0xb311782a4aa662b2ULL,0x19eeaf64e248c7f4ULL,0x20273e77e3439f82ULL}},{{0xacc02860f7ce93acULL,0x3933d5817ba76b4cULL,0x69e6188b446c8467ULL,0x0a46036d4417cc55ULL}}},
};
static const fp2 FQ6_C2[4] = {
    {{{0xd35d438dc58f0d9dULL,0x0a78eb28f5c70b3dULL,0x666ea36f7879462cULL,0x0e0a77c19a07df2fULL}},{{0x0000000000000000ULL,0x0000000000000000ULL,0x0000000000000000ULL,0x0000000000000000ULL}}},
    {{{0x7361d77f843abe92ULL,0xa5bb2bd3273411fbULL,0x9c941f314b3e2399ULL,0x15df9cddbb9fd3ecULL}},{{0x5dddfd154bd8c949ULL,0x62cb29a5a4445b60ULL,0x37bc870a0c7dd2b9ULL,0x24830a9d3171f0fdULL}}},
    {{{0x71930c11d782e155ULL,0xa6bb947cffbe3323ULL,0xaa303344d4741444ULL,0x2c3b3f0d26594943ULL}},{{0x0000000000000000ULL,0x0000000000000000ULL,0x0000000000000000ULL,0x0000000000000000ULL}}},
    {{{0x448a93a57b6762dfULL,0xbfd62df528fdeadfULL,0xd858f5d00e9bd47aULL,0x06b03d4d3476ec58ULL}},{{0x2b19daf4bcc936d1ULL,0xa1a54e7a56f4299fULL,0xb533eee05adeaef1ULL,0x170c812b84dda0b2ULL}}},
};
static const fp2 FQ12_C1[4] = {
    {{{0xd35d438dc58f0d9dULL,0x0a78eb28f5c70b3dULL,0x666ea36f7879462cULL,0x0e0a77c19a07df2fULL}},{{0x0000000000000000ULL,0x0000000000000000ULL,0x0000000000000000ULL,0x0000000000000000ULL}}},
    {{{0xaf9ba69633144907ULL,0xca6b1d7387afb78aULL,0x11bded5ef08a2087ULL,0x02f34d751a1f3a7cULL}},{{0xa222ae234c492d72ULL,0xd00f02a4565de15bULL,0xdc2ff3a253dfc926ULL,0x10a75716b3899551ULL}}},
    {{{0xca8d800500fa1bf2ULL,0xf0c5d61468b39769ULL,0x0e201271ad0d4418ULL,0x04290f65bad856e6ULL}},{{0x0000000000000000ULL,0x0000000000000000ULL,0x0000000000000000ULL,0x0000000000000000ULL}}},
    {{{0x365316184e46d97dULL,0x0af7129ed4c96d9fULL,0x659da72fca1009b5ULL,0x08116d8983a20d23ULL}},{{0xb1df4af7c39c1939ULL,0x3d9f02878a73bf7fULL,0x9b2220928caf0ae0ULL,0x26684515eff054a6ULL}}},
};

/* G2 Frobenius coefficients */
static const fp2 G2_FX1 = {{{0xb5773b104563ab30ULL,0x347f91c8a9aa6454ULL,0x7a007127242e0991ULL,0x1956bcd8118214ecULL}},{{0x6e849f1ea0aa4757ULL,0xaa1c7b6d89f89141ULL,0xb6e713cdfae0ca3aULL,0x26694fbb4e82ebc3ULL}}}; /* xi^((p-1)/3)   */
static const fp2 G2_FY1 = {{{0xe4bbdd0c2936b629ULL,0xbb30f162e133bacbULL,0x31a9d1b6f9645366ULL,0x253570bea500f8ddULL}},{{0xa1d77ce45ffe77c7ULL,0x07affd117826d1dbULL,0x6d16bd27bb7edc6bULL,0x2c87200285defeccULL}}}; /* xi^((p-1)/2)   */
static const fp2 G2_FX2 = {{{0x3350c88e13e80b9cULL,0x7dce557cdb5e56b9ULL,0x6001b4b8b615564aULL,0x2682e617020217e0ULL}},{{0x0000000000000000ULL,0x0000000000000000ULL,0x0000000000000000ULL,0x0000000000000000ULL}}}; /* xi^((p^2-1)/3) */
static const fp2 G2_FY2N= {{{0xd35d438dc58f0d9dULL,0x0a78eb28f5c70b3dULL,0x666ea36f7879462cULL,0x0e0a77c19a07df2fULL}},{{0x0000000000000000ULL,0x0000000000000000ULL,0x0000000000000000ULL,0x0000000000000000ULL}}}; /* -xi^((p^2-1)/2)*/

/* NAF of 6x+2 = 29793968203157093288, most-significant digit first (leading 1 dropped by the loop). */
static const int8_t ATE_NAF[66] = {
     0, 0, 0, 1, 0, 1, 0,-1, 0, 0,-1, 0, 0, 0, 1, 0,
     0,-1, 0,-1, 0, 0, 0, 1, 0,-1, 0, 0, 0, 0,-1, 0,
     0, 1, 0,-1, 0, 0, 1, 0, 0, 0, 0, 0,-1, 0, 0,-1,
     0, 1, 0,-1, 0, 0, 0,-1, 0,-1, 0, 0, 0, 1, 0,-1,
     0, 1,
};
#define ATE_NAF_LEN 66

/* BN parameter x = 4965661367192848881 */
#define BN_X 0x44e992b44a6909f1ULL

static const fp FP_ZERO = {{0, 0, 0, 0}};
static const fp FP_RAW_ONE = {{1, 0, 0, 0}};    /* plain 1, not Montgomery */
/* p - 2, used as the exponent for inversion by Fermat's little theorem */
static const uint64_t FP_PM2[4] = {
    0x3c208c16d87cfd45ULL, 0x97816a916871ca8dULL,
    0xb85045b68181585dULL, 0x30644e72e131a029ULL
};

/* ------------------------------------------------------------------ */
/* Fp                                                                  */
/* ------------------------------------------------------------------ */

static inline uint64_t addc64(uint64_t a, uint64_t b, uint64_t cin, uint64_t *out)
{
    unsigned __int128 t = (unsigned __int128)a + b + cin;
    *out = (uint64_t)t;
    return (uint64_t)(t >> 64);
}

static inline uint64_t subb64(uint64_t a, uint64_t b, uint64_t bin, uint64_t *out)
{
    unsigned __int128 t = (unsigned __int128)a - b - bin;
    *out = (uint64_t)t;
    return (uint64_t)((t >> 64) & 1);
}

static inline int fp_is_zero(const fp *a)
{
    return (a->v[0] | a->v[1] | a->v[2] | a->v[3]) == 0;
}

static inline int fp_eq(const fp *a, const fp *b)
{
    return a->v[0] == b->v[0] && a->v[1] == b->v[1] &&
           a->v[2] == b->v[2] && a->v[3] == b->v[3];
}

static void fp_add(fp *r, const fp *a, const fp *b)
{
    uint64_t t0, t1, t2, t3, d0, d1, d2, d3, c, brw;
    c   = addc64(a->v[0], b->v[0], 0, &t0);
    c   = addc64(a->v[1], b->v[1], c, &t1);
    c   = addc64(a->v[2], b->v[2], c, &t2);
    c   = addc64(a->v[3], b->v[3], c, &t3);
    brw = subb64(t0, FP_P[0], 0,   &d0);
    brw = subb64(t1, FP_P[1], brw, &d1);
    brw = subb64(t2, FP_P[2], brw, &d2);
    brw = subb64(t3, FP_P[3], brw, &d3);
    if (c != 0 || brw == 0) { r->v[0]=d0; r->v[1]=d1; r->v[2]=d2; r->v[3]=d3; }
    else                    { r->v[0]=t0; r->v[1]=t1; r->v[2]=t2; r->v[3]=t3; }
}

static void fp_sub(fp *r, const fp *a, const fp *b)
{
    uint64_t t0, t1, t2, t3, c, brw;
    brw = subb64(a->v[0], b->v[0], 0,   &t0);
    brw = subb64(a->v[1], b->v[1], brw, &t1);
    brw = subb64(a->v[2], b->v[2], brw, &t2);
    brw = subb64(a->v[3], b->v[3], brw, &t3);
    if (brw) {
        c = addc64(t0, FP_P[0], 0, &t0);
        c = addc64(t1, FP_P[1], c, &t1);
        c = addc64(t2, FP_P[2], c, &t2);
        (void)addc64(t3, FP_P[3], c, &t3);
    }
    r->v[0]=t0; r->v[1]=t1; r->v[2]=t2; r->v[3]=t3;
}

static void fp_neg(fp *r, const fp *a)
{
    uint64_t brw;
    if (fp_is_zero(a)) { *r = FP_ZERO; return; }
    brw = subb64(FP_P[0], a->v[0], 0,   &r->v[0]);
    brw = subb64(FP_P[1], a->v[1], brw, &r->v[1]);
    brw = subb64(FP_P[2], a->v[2], brw, &r->v[2]);
    (void)subb64(FP_P[3], a->v[3], brw, &r->v[3]);
}

static void fp_dbl(fp *r, const fp *a) { fp_add(r, a, a); }

/* Montgomery multiplication (CIOS), fully unrolled.  p < 2^255 so the
 * accumulator needs only five words and one conditional subtraction. */
static void fp_mul(fp *r, const fp *a, const fp *b)
{
    const uint64_t a0 = a->v[0], a1 = a->v[1], a2 = a->v[2], a3 = a->v[3];
    uint64_t t0 = 0, t1 = 0, t2 = 0, t3 = 0, t4 = 0;
    uint64_t c, m, hi, d0, d1, d2, d3, brw;
    unsigned __int128 z;

#define CIOS(BI) do {                                                          \
    const uint64_t bi = (BI);                                                  \
    z = (unsigned __int128)a0 * bi + t0;     t0 = (uint64_t)z; c = (uint64_t)(z >> 64); \
    z = (unsigned __int128)a1 * bi + t1 + c; t1 = (uint64_t)z; c = (uint64_t)(z >> 64); \
    z = (unsigned __int128)a2 * bi + t2 + c; t2 = (uint64_t)z; c = (uint64_t)(z >> 64); \
    z = (unsigned __int128)a3 * bi + t3 + c; t3 = (uint64_t)z; c = (uint64_t)(z >> 64); \
    z = (unsigned __int128)t4 + c;           t4 = (uint64_t)z; hi = (uint64_t)(z >> 64); \
    m = t0 * FP_N0;                                                            \
    z = (unsigned __int128)m * FP_P0 + t0;                    c = (uint64_t)(z >> 64); \
    z = (unsigned __int128)m * FP_P1 + t1 + c; t0 = (uint64_t)z; c = (uint64_t)(z >> 64); \
    z = (unsigned __int128)m * FP_P2 + t2 + c; t1 = (uint64_t)z; c = (uint64_t)(z >> 64); \
    z = (unsigned __int128)m * FP_P3 + t3 + c; t2 = (uint64_t)z; c = (uint64_t)(z >> 64); \
    z = (unsigned __int128)t4 + c;             t3 = (uint64_t)z;               \
    t4 = hi + (uint64_t)(z >> 64);                                             \
} while (0)

    CIOS(b->v[0]);
    CIOS(b->v[1]);
    CIOS(b->v[2]);
    CIOS(b->v[3]);
#undef CIOS

    brw = subb64(t0, FP_P0, 0,   &d0);
    brw = subb64(t1, FP_P1, brw, &d1);
    brw = subb64(t2, FP_P2, brw, &d2);
    brw = subb64(t3, FP_P3, brw, &d3);
    if (t4 != 0 || brw == 0) { r->v[0]=d0; r->v[1]=d1; r->v[2]=d2; r->v[3]=d3; }
    else                     { r->v[0]=t0; r->v[1]=t1; r->v[2]=t2; r->v[3]=t3; }
}

static void fp_sqr(fp *r, const fp *a) { fp_mul(r, a, a); }

static void fp_from_mont(fp *r, const fp *a) { fp_mul(r, a, &FP_RAW_ONE); }

/* r = a^-1 (a != 0), by a^(p-2). */
static void fp_inv(fp *r, const fp *a)
{
    fp acc = FP_ONE, base = *a;
    int i, bit;
    for (i = 255; i >= 0; i--) {
        fp_sqr(&acc, &acc);
        bit = (int)((FP_PM2[i >> 6] >> (i & 63)) & 1);
        if (bit) fp_mul(&acc, &acc, &base);
    }
    *r = acc;
}

/* Decode 32 big-endian bytes; returns -1 if the value is >= p. */
static int fp_from_be(fp *r, const uint8_t b[32])
{
    uint64_t t[4], d[4], brw = 0;
    fp x;
    int i, j;
    for (i = 0; i < 4; i++) {
        uint64_t w = 0;
        for (j = 0; j < 8; j++) w = (w << 8) | b[i * 8 + j];
        t[3 - i] = w;
    }
    for (i = 0; i < 4; i++) brw = subb64(t[i], FP_P[i], brw, &d[i]);
    if (brw == 0) return -1;                 /* t >= p */
    memcpy(x.v, t, sizeof t);
    fp_mul(r, &x, &FP_R2);
    return 0;
}

static void fp_to_be(const fp *a, uint8_t b[32])
{
    fp t;
    int i, j;
    fp_from_mont(&t, a);
    for (i = 0; i < 4; i++) {
        uint64_t w = t.v[3 - i];
        for (j = 0; j < 8; j++) b[i * 8 + j] = (uint8_t)(w >> (56 - 8 * j));
    }
}

/* ------------------------------------------------------------------ */
/* Fp2                                                                 */
/* ------------------------------------------------------------------ */

static const fp2 FP2_ZERO = {{{0,0,0,0}}, {{0,0,0,0}}};

static inline int fp2_is_zero(const fp2 *a) { return fp_is_zero(&a->c0) && fp_is_zero(&a->c1); }
static inline int fp2_eq(const fp2 *a, const fp2 *b) { return fp_eq(&a->c0,&b->c0) && fp_eq(&a->c1,&b->c1); }

static void fp2_add(fp2 *r, const fp2 *a, const fp2 *b)
{ fp_add(&r->c0,&a->c0,&b->c0); fp_add(&r->c1,&a->c1,&b->c1); }

static void fp2_sub(fp2 *r, const fp2 *a, const fp2 *b)
{ fp_sub(&r->c0,&a->c0,&b->c0); fp_sub(&r->c1,&a->c1,&b->c1); }

static void fp2_neg(fp2 *r, const fp2 *a)
{ fp_neg(&r->c0,&a->c0); fp_neg(&r->c1,&a->c1); }

static void fp2_dbl(fp2 *r, const fp2 *a)
{ fp_dbl(&r->c0,&a->c0); fp_dbl(&r->c1,&a->c1); }

static void fp2_conj(fp2 *r, const fp2 *a)
{ r->c0 = a->c0; fp_neg(&r->c1,&a->c1); }

static void fp2_mul(fp2 *r, const fp2 *a, const fp2 *b)
{
    fp t0, t1, t2, t3;
    fp_mul(&t0, &a->c0, &b->c0);
    fp_mul(&t1, &a->c1, &b->c1);
    fp_add(&t2, &a->c0, &a->c1);
    fp_add(&t3, &b->c0, &b->c1);
    fp_mul(&t2, &t2, &t3);
    fp_sub(&r->c0, &t0, &t1);
    fp_sub(&t2, &t2, &t0);
    fp_sub(&r->c1, &t2, &t1);
}

static void fp2_sqr(fp2 *r, const fp2 *a)
{
    fp t0, t1, t2;
    fp_add(&t0, &a->c0, &a->c1);
    fp_sub(&t1, &a->c0, &a->c1);
    fp_mul(&t2, &a->c0, &a->c1);
    fp_mul(&r->c0, &t0, &t1);
    fp_add(&r->c1, &t2, &t2);
}

static void fp2_mul_fp(fp2 *r, const fp2 *a, const fp *k)
{ fp_mul(&r->c0,&a->c0,k); fp_mul(&r->c1,&a->c1,k); }

static void fp2_mul3(fp2 *r, const fp2 *a)
{ fp2 t; fp2_dbl(&t,a); fp2_add(r,&t,a); }

static void fp2_mul8(fp2 *r, const fp2 *a)
{ fp2_dbl(r,a); fp2_dbl(r,r); fp2_dbl(r,r); }

/* multiply by the Fp6 non-residue xi = 9 + u */
static void fp2_mul_xi(fp2 *r, const fp2 *a)
{
    fp a0 = a->c0, a1 = a->c1, n0, n1;
    fp_dbl(&n0,&a0); fp_dbl(&n0,&n0); fp_dbl(&n0,&n0); fp_add(&n0,&n0,&a0);
    fp_dbl(&n1,&a1); fp_dbl(&n1,&n1); fp_dbl(&n1,&n1); fp_add(&n1,&n1,&a1);
    fp_sub(&r->c0,&n0,&a1);
    fp_add(&r->c1,&n1,&a0);
}

static void fp2_inv(fp2 *r, const fp2 *a)
{
    fp t0, t1, x0 = a->c0, x1 = a->c1;
    fp_sqr(&t0,&x0); fp_sqr(&t1,&x1); fp_add(&t0,&t0,&t1);
    fp_inv(&t0,&t0);
    fp_mul(&r->c0,&x0,&t0);
    fp_mul(&t1,&x1,&t0);
    fp_neg(&r->c1,&t1);
}

/* ------------------------------------------------------------------ */
/* Fp6                                                                 */
/* ------------------------------------------------------------------ */

static void fp6_add(fp6 *r, const fp6 *a, const fp6 *b)
{ fp2_add(&r->c0,&a->c0,&b->c0); fp2_add(&r->c1,&a->c1,&b->c1); fp2_add(&r->c2,&a->c2,&b->c2); }

static void fp6_sub(fp6 *r, const fp6 *a, const fp6 *b)
{ fp2_sub(&r->c0,&a->c0,&b->c0); fp2_sub(&r->c1,&a->c1,&b->c1); fp2_sub(&r->c2,&a->c2,&b->c2); }

static void fp6_neg(fp6 *r, const fp6 *a)
{ fp2_neg(&r->c0,&a->c0); fp2_neg(&r->c1,&a->c1); fp2_neg(&r->c2,&a->c2); }

static void fp6_mul(fp6 *r, const fp6 *a, const fp6 *b)
{
    fp2 t0, t1, t2, u0, u1, s0, s1, s2;
    fp2_mul(&t0,&a->c0,&b->c0);
    fp2_mul(&t1,&a->c1,&b->c1);
    fp2_mul(&t2,&a->c2,&b->c2);

    fp2_add(&u0,&a->c1,&a->c2); fp2_add(&u1,&b->c1,&b->c2);
    fp2_mul(&s0,&u0,&u1); fp2_sub(&s0,&s0,&t1); fp2_sub(&s0,&s0,&t2);
    fp2_mul_xi(&s0,&s0); fp2_add(&s0,&s0,&t0);

    fp2_add(&u0,&a->c0,&a->c1); fp2_add(&u1,&b->c0,&b->c1);
    fp2_mul(&s1,&u0,&u1); fp2_sub(&s1,&s1,&t0); fp2_sub(&s1,&s1,&t1);
    fp2_mul_xi(&u0,&t2); fp2_add(&s1,&s1,&u0);

    fp2_add(&u0,&a->c0,&a->c2); fp2_add(&u1,&b->c0,&b->c2);
    fp2_mul(&s2,&u0,&u1); fp2_sub(&s2,&s2,&t0); fp2_sub(&s2,&s2,&t2);
    fp2_add(&s2,&s2,&t1);

    r->c0 = s0; r->c1 = s1; r->c2 = s2;
}

/* multiply by v (the Fp6 generator): (c0,c1,c2) -> (xi*c2, c0, c1) */
static void fp6_mul_v(fp6 *r, const fp6 *a)
{
    fp2 t0 = a->c0, t1 = a->c1, t2 = a->c2;
    fp2_mul_xi(&t2,&t2);
    r->c0 = t2; r->c1 = t0; r->c2 = t1;
}

/* sparse multiply: b = b0 + b1*v */
static void fp6_mul_01(fp6 *r, const fp6 *a, const fp2 *b0, const fp2 *b1)
{
    fp2 t0, t1, t2, u0, u1, s0, s1, s2;
    fp2_mul(&t0,&a->c0,b0);
    fp2_mul(&t1,&a->c1,b1);
    fp2_mul(&t2,&a->c2,b1);
    fp2_mul_xi(&s0,&t2); fp2_add(&s0,&s0,&t0);

    fp2_add(&u0,&a->c0,&a->c1); fp2_add(&u1,b0,b1);
    fp2_mul(&s1,&u0,&u1); fp2_sub(&s1,&s1,&t0); fp2_sub(&s1,&s1,&t1);

    fp2_add(&u0,&a->c0,&a->c2);
    fp2_mul(&s2,&u0,b0); fp2_sub(&s2,&s2,&t0); fp2_add(&s2,&s2,&t1);

    r->c0 = s0; r->c1 = s1; r->c2 = s2;
}

static void fp6_mul_fp2(fp6 *r, const fp6 *a, const fp2 *k)
{ fp2_mul(&r->c0,&a->c0,k); fp2_mul(&r->c1,&a->c1,k); fp2_mul(&r->c2,&a->c2,k); }

static void fp6_inv(fp6 *r, const fp6 *a)
{
    fp2 t0, t1, t2, t, u;
    fp2_sqr(&t0,&a->c0); fp2_mul(&u,&a->c1,&a->c2); fp2_mul_xi(&u,&u); fp2_sub(&t0,&t0,&u);
    fp2_sqr(&t1,&a->c2); fp2_mul_xi(&t1,&t1); fp2_mul(&u,&a->c0,&a->c1); fp2_sub(&t1,&t1,&u);
    fp2_sqr(&t2,&a->c1); fp2_mul(&u,&a->c0,&a->c2); fp2_sub(&t2,&t2,&u);

    fp2_mul(&t,&a->c2,&t1);
    fp2_mul(&u,&a->c1,&t2); fp2_add(&t,&t,&u);
    fp2_mul_xi(&t,&t);
    fp2_mul(&u,&a->c0,&t0); fp2_add(&t,&t,&u);
    fp2_inv(&t,&t);

    fp2_mul(&r->c0,&t0,&t);
    fp2_mul(&r->c1,&t1,&t);
    fp2_mul(&r->c2,&t2,&t);
}

static void fp6_frob(fp6 *r, const fp6 *a, int i)
{
    if (i & 1) { fp2_conj(&r->c0,&a->c0); fp2_conj(&r->c1,&a->c1); fp2_conj(&r->c2,&a->c2); }
    else       { *r = *a; }
    fp2_mul(&r->c1,&r->c1,&FQ6_C1[i]);
    fp2_mul(&r->c2,&r->c2,&FQ6_C2[i]);
}

/* ------------------------------------------------------------------ */
/* Fp12                                                                */
/* ------------------------------------------------------------------ */

static void fp12_one(fp12 *r)
{
    memset(r, 0, sizeof *r);
    r->c0.c0.c0 = FP_ONE;
}

static int fp12_is_one(const fp12 *a)
{
    static const fp12 z = {{{{{0,0,0,0}},{{0,0,0,0}}},{{{0,0,0,0}},{{0,0,0,0}}},{{{0,0,0,0}},{{0,0,0,0}}}},
                           {{{{0,0,0,0}},{{0,0,0,0}}},{{{0,0,0,0}},{{0,0,0,0}}},{{{0,0,0,0}},{{0,0,0,0}}}}};
    fp12 t = *a;
    fp_sub(&t.c0.c0.c0, &t.c0.c0.c0, &FP_ONE);
    return memcmp(&t, &z, sizeof t) == 0;
}

static void fp12_mul(fp12 *r, const fp12 *a, const fp12 *b)
{
    fp6 t0, t1, t2, t3;
    fp6_mul(&t0,&a->c0,&b->c0);
    fp6_mul(&t1,&a->c1,&b->c1);
    fp6_add(&t2,&a->c0,&a->c1);
    fp6_add(&t3,&b->c0,&b->c1);
    fp6_mul(&t2,&t2,&t3);
    fp6_sub(&t2,&t2,&t0); fp6_sub(&t2,&t2,&t1);
    fp6_mul_v(&t1,&t1);
    fp6_add(&r->c0,&t0,&t1);
    r->c1 = t2;
}

static void fp12_sqr(fp12 *r, const fp12 *a)
{
    fp6 t0, t1, t2;
    fp6_mul(&t0,&a->c0,&a->c1);        /* a0*a1               */
    fp6_add(&t1,&a->c0,&a->c1);        /* a0+a1               */
    fp6_mul_v(&t2,&a->c1);
    fp6_add(&t2,&t2,&a->c0);           /* a0 + v*a1           */
    fp6_mul(&t1,&t1,&t2);
    fp6_mul_v(&t2,&t0);
    fp6_sub(&t1,&t1,&t0);
    fp6_sub(&t1,&t1,&t2);
    fp6_add(&r->c1,&t0,&t0);
    r->c0 = t1;
}

static void fp12_conj(fp12 *r, const fp12 *a)
{ r->c0 = a->c0; fp6_neg(&r->c1,&a->c1); }

static void fp12_inv(fp12 *r, const fp12 *a)
{
    fp6 t0, t1;
    fp6_mul(&t0,&a->c0,&a->c0);
    fp6_mul(&t1,&a->c1,&a->c1);
    fp6_mul_v(&t1,&t1);
    fp6_sub(&t0,&t0,&t1);
    fp6_inv(&t0,&t0);
    fp6_mul(&t1,&a->c1,&t0);
    fp6_mul(&r->c0,&a->c0,&t0);
    fp6_neg(&r->c1,&t1);
}

static void fp12_frob(fp12 *r, const fp12 *a, int i)
{
    fp6_frob(&r->c0,&a->c0,i);
    fp6_frob(&r->c1,&a->c1,i);
    fp6_mul_fp2(&r->c1,&r->c1,&FQ12_C1[i]);
}

/* r = a * (c0 + c1*w + c3*w^3), i.e. the sparse element ((c0,0,0),(c1,c3,0)) */
static void fp12_mul_line(fp12 *r, const fp12 *a, const fp2 *c0, const fp2 *c1, const fp2 *c3)
{
    fp6 t0, t1, t2;
    fp2 d0;
    fp6_mul_fp2(&t0,&a->c0,c0);
    fp6_mul_01(&t1,&a->c1,c1,c3);
    fp2_add(&d0,c0,c1);
    fp6_add(&t2,&a->c0,&a->c1);
    fp6_mul_01(&t2,&t2,&d0,c3);
    fp6_sub(&t2,&t2,&t0); fp6_sub(&t2,&t2,&t1);
    fp6_mul_v(&t1,&t1);
    fp6_add(&r->c0,&t0,&t1);
    r->c1 = t2;
}

/* Granger-Scott squaring, valid for elements of the cyclotomic subgroup
 * G_Phi12(p) (everything that comes out of the "easy" part of the final
 * exponentiation).  Nine Fp2 multiplications instead of eighteen. */
static void fp12_cyc_sqr(fp12 *r, const fp12 *a)
{
    fp2 z0 = a->c0.c0, z4 = a->c0.c1, z3 = a->c0.c2;
    fp2 z2 = a->c1.c0, z1 = a->c1.c1, z5 = a->c1.c2;
    fp2 t0, t1, t2, t3, t4, t5, tmp, u;

#define CYC_SQ2(A, B, T0, T1) do {                     \
    fp2_mul(&tmp, &(A), &(B));                         \
    fp2_add(&(T0), &(A), &(B));                        \
    fp2_mul_xi(&u, &(B)); fp2_add(&u, &u, &(A));       \
    fp2_mul(&(T0), &(T0), &u);                         \
    fp2_sub(&(T0), &(T0), &tmp);                       \
    fp2_mul_xi(&u, &tmp); fp2_sub(&(T0), &(T0), &u);   \
    fp2_add(&(T1), &tmp, &tmp);                        \
} while (0)

    CYC_SQ2(z0, z1, t0, t1);
    CYC_SQ2(z2, z3, t2, t3);
    CYC_SQ2(z4, z5, t4, t5);
#undef CYC_SQ2

    fp2_sub(&z0,&t0,&z0); fp2_dbl(&z0,&z0); fp2_add(&z0,&z0,&t0);
    fp2_add(&z1,&t1,&z1); fp2_dbl(&z1,&z1); fp2_add(&z1,&z1,&t1);
    fp2_mul_xi(&tmp,&t5);
    fp2_add(&z2,&tmp,&z2); fp2_dbl(&z2,&z2); fp2_add(&z2,&z2,&tmp);
    fp2_sub(&z3,&t4,&z3); fp2_dbl(&z3,&z3); fp2_add(&z3,&z3,&t4);
    fp2_sub(&z4,&t2,&z4); fp2_dbl(&z4,&z4); fp2_add(&z4,&z4,&t2);
    fp2_add(&z5,&t3,&z5); fp2_dbl(&z5,&z5); fp2_add(&z5,&z5,&t3);

    r->c0.c0 = z0; r->c0.c1 = z4; r->c0.c2 = z3;
    r->c1.c0 = z2; r->c1.c1 = z1; r->c1.c2 = z5;
}

static void fp12_cyc_pow_u64(fp12 *r, const fp12 *a, uint64_t e)
{
    fp12 acc, base = *a;
    fp12_one(&acc);
    while (e) {
        if (e & 1) fp12_mul(&acc,&acc,&base);
        e >>= 1;
        if (e) fp12_cyc_sqr(&base,&base);
    }
    *r = acc;
}

/* ------------------------------------------------------------------ */
/* G1                                                                  */
/* ------------------------------------------------------------------ */

typedef struct { fp x, y; int inf; } g1;
typedef struct { fp x, y, z; } g1j;          /* Jacobian, z == 0 => infinity */

static int g1_on_curve(const g1 *p)
{
    fp l, r;
    if (p->inf) return 1;
    fp_sqr(&l,&p->y);
    fp_sqr(&r,&p->x); fp_mul(&r,&r,&p->x); fp_add(&r,&r,&FP_B);
    return fp_eq(&l,&r);
}

static void g1j_dbl(g1j *r, const g1j *a)
{
    fp A, B, C, D, E, F, t;
    if (fp_is_zero(&a->z) || fp_is_zero(&a->y)) {
        memset(r, 0, sizeof *r);
        return;
    }
    fp_sqr(&A,&a->x);                       /* X^2            */
    fp_sqr(&B,&a->y);                       /* Y^2            */
    fp_sqr(&C,&B);                          /* Y^4            */
    fp_add(&D,&a->x,&B); fp_sqr(&D,&D);
    fp_sub(&D,&D,&A); fp_sub(&D,&D,&C);
    fp_dbl(&D,&D);                          /* D = 2((X+B)^2-A-C) */
    fp_dbl(&E,&A); fp_add(&E,&E,&A);        /* E = 3A         */
    fp_sqr(&F,&E);
    fp_mul(&t,&a->y,&a->z); fp_dbl(&t,&t);  /* Z3 = 2YZ       */
    fp_sub(&F,&F,&D); fp_sub(&F,&F,&D);     /* X3 = F - 2D    */
    fp_sub(&D,&D,&F);
    fp_mul(&D,&D,&E);
    fp_dbl(&C,&C); fp_dbl(&C,&C); fp_dbl(&C,&C);  /* 8C       */
    fp_sub(&D,&D,&C);
    r->x = F; r->y = D; r->z = t;
}

/* r = a + b, with b affine and not the point at infinity */
static void g1j_add_mixed(g1j *r, const g1j *a, const g1 *b)
{
    fp Z1Z1, U2, S2, H, HH, I, J, rr, V, t;
    if (fp_is_zero(&a->z)) {
        r->x = b->x; r->y = b->y; r->z = FP_ONE;
        return;
    }
    fp_sqr(&Z1Z1,&a->z);
    fp_mul(&U2,&b->x,&Z1Z1);
    fp_mul(&S2,&b->y,&a->z); fp_mul(&S2,&S2,&Z1Z1);
    fp_sub(&H,&U2,&a->x);
    fp_sub(&rr,&S2,&a->y);
    if (fp_is_zero(&H)) {
        if (fp_is_zero(&rr)) { g1j_dbl(r, a); return; }
        memset(r, 0, sizeof *r);
        return;
    }
    fp_dbl(&rr,&rr);
    fp_sqr(&HH,&H);
    fp_dbl(&I,&HH); fp_dbl(&I,&I);
    fp_mul(&J,&H,&I);
    fp_mul(&V,&a->x,&I);
    fp_sqr(&t,&rr); fp_sub(&t,&t,&J); fp_sub(&t,&t,&V); fp_sub(&t,&t,&V);
    fp_sub(&V,&V,&t);
    fp_mul(&V,&V,&rr);
    fp_mul(&J,&J,&a->y); fp_dbl(&J,&J);
    fp_sub(&V,&V,&J);
    fp_add(&HH,&a->z,&H); fp_sqr(&HH,&HH);
    fp_sqr(&J,&H);
    fp_sub(&HH,&HH,&Z1Z1); fp_sub(&HH,&HH,&J);
    r->x = t; r->y = V; r->z = HH;
}

static void g1j_to_affine(g1 *r, const g1j *a)
{
    fp zi, zi2, zi3;
    if (fp_is_zero(&a->z)) { memset(r, 0, sizeof *r); r->inf = 1; return; }
    fp_inv(&zi,&a->z);
    fp_sqr(&zi2,&zi);
    fp_mul(&zi3,&zi2,&zi);
    fp_mul(&r->x,&a->x,&zi2);
    fp_mul(&r->y,&a->y,&zi3);
    r->inf = 0;
}

static void g1_scalar_mul(g1 *r, const g1 *p, const uint64_t k[4])
{
    g1j acc;
    int i;
    memset(&acc, 0, sizeof acc);
    if (p->inf) { memset(r, 0, sizeof *r); r->inf = 1; return; }
    for (i = 255; i >= 0; i--) {
        g1j_dbl(&acc,&acc);
        if ((k[i >> 6] >> (i & 63)) & 1) g1j_add_mixed(&acc,&acc,p);
    }
    g1j_to_affine(r,&acc);
}

/* affine addition, handling every special case (used by ECADD) */
static void g1_add_affine(g1 *r, const g1 *a, const g1 *b)
{
    fp lam, num, den, x3, y3;
    if (a->inf) { *r = *b; return; }
    if (b->inf) { *r = *a; return; }
    if (fp_eq(&a->x,&b->x)) {
        if (!fp_eq(&a->y,&b->y) || fp_is_zero(&a->y)) {
            memset(r, 0, sizeof *r); r->inf = 1; return;
        }
        fp_sqr(&num,&a->x); fp_dbl(&den,&num); fp_add(&num,&num,&den); /* 3x^2 */
        fp_dbl(&den,&a->y);
    } else {
        fp_sub(&num,&b->y,&a->y);
        fp_sub(&den,&b->x,&a->x);
    }
    fp_inv(&den,&den);
    fp_mul(&lam,&num,&den);
    fp_sqr(&x3,&lam); fp_sub(&x3,&x3,&a->x); fp_sub(&x3,&x3,&b->x);
    fp_sub(&y3,&a->x,&x3); fp_mul(&y3,&y3,&lam); fp_sub(&y3,&y3,&a->y);
    r->x = x3; r->y = y3; r->inf = 0;
}

/* ------------------------------------------------------------------ */
/* G2 (the sextic twist over Fp2)                                      */
/* ------------------------------------------------------------------ */

typedef struct { fp2 x, y; int inf; } g2;
typedef struct { fp2 x, y, z; } g2j;

static int g2_on_curve(const g2 *p)
{
    fp2 l, r;
    if (p->inf) return 1;
    fp2_sqr(&l,&p->y);
    fp2_sqr(&r,&p->x); fp2_mul(&r,&r,&p->x); fp2_add(&r,&r,&FP2_B);
    return fp2_eq(&l,&r);
}

static void g2j_dbl(g2j *r, const g2j *a)
{
    fp2 A, B, C, D, E, F, t;
    if (fp2_is_zero(&a->z) || fp2_is_zero(&a->y)) { memset(r, 0, sizeof *r); return; }
    fp2_sqr(&A,&a->x);
    fp2_sqr(&B,&a->y);
    fp2_sqr(&C,&B);
    fp2_add(&D,&a->x,&B); fp2_sqr(&D,&D);
    fp2_sub(&D,&D,&A); fp2_sub(&D,&D,&C);
    fp2_dbl(&D,&D);
    fp2_mul3(&E,&A);
    fp2_sqr(&F,&E);
    fp2_mul(&t,&a->y,&a->z); fp2_dbl(&t,&t);
    fp2_sub(&F,&F,&D); fp2_sub(&F,&F,&D);
    fp2_sub(&D,&D,&F);
    fp2_mul(&D,&D,&E);
    fp2_mul8(&C,&C);
    fp2_sub(&D,&D,&C);
    r->x = F; r->y = D; r->z = t;
}

static void g2j_add_mixed(g2j *r, const g2j *a, const g2 *b)
{
    fp2 Z1Z1, U2, S2, H, HH, I, J, rr, V, t;
    if (fp2_is_zero(&a->z)) { r->x = b->x; r->y = b->y; r->z.c0 = FP_ONE; r->z.c1 = FP_ZERO; return; }
    fp2_sqr(&Z1Z1,&a->z);
    fp2_mul(&U2,&b->x,&Z1Z1);
    fp2_mul(&S2,&b->y,&a->z); fp2_mul(&S2,&S2,&Z1Z1);
    fp2_sub(&H,&U2,&a->x);
    fp2_sub(&rr,&S2,&a->y);
    if (fp2_is_zero(&H)) {
        if (fp2_is_zero(&rr)) { g2j_dbl(r, a); return; }
        memset(r, 0, sizeof *r);
        return;
    }
    fp2_dbl(&rr,&rr);
    fp2_sqr(&HH,&H);
    fp2_dbl(&I,&HH); fp2_dbl(&I,&I);
    fp2_mul(&J,&H,&I);
    fp2_mul(&V,&a->x,&I);
    fp2_sqr(&t,&rr); fp2_sub(&t,&t,&J); fp2_sub(&t,&t,&V); fp2_sub(&t,&t,&V);
    fp2_sub(&V,&V,&t);
    fp2_mul(&V,&V,&rr);
    fp2_mul(&J,&J,&a->y); fp2_dbl(&J,&J);
    fp2_sub(&V,&V,&J);
    fp2_add(&HH,&a->z,&H); fp2_sqr(&HH,&HH);
    fp2_sqr(&J,&H);
    fp2_sub(&HH,&HH,&Z1Z1); fp2_sub(&HH,&HH,&J);
    r->x = t; r->y = V; r->z = HH;
}

static void g2j_add(g2j *r, const g2j *a, const g2j *b)
{
    fp2 Z1Z1, Z2Z2, U1, U2, S1, S2, H, I, J, rr, V, t;
    if (fp2_is_zero(&a->z)) { *r = *b; return; }
    if (fp2_is_zero(&b->z)) { *r = *a; return; }
    fp2_sqr(&Z1Z1,&a->z);
    fp2_sqr(&Z2Z2,&b->z);
    fp2_mul(&U1,&a->x,&Z2Z2);
    fp2_mul(&U2,&b->x,&Z1Z1);
    fp2_mul(&S1,&a->y,&b->z); fp2_mul(&S1,&S1,&Z2Z2);
    fp2_mul(&S2,&b->y,&a->z); fp2_mul(&S2,&S2,&Z1Z1);
    fp2_sub(&H,&U2,&U1);
    fp2_sub(&rr,&S2,&S1);
    if (fp2_is_zero(&H)) {
        if (fp2_is_zero(&rr)) { g2j_dbl(r, a); return; }
        memset(r, 0, sizeof *r);
        return;
    }
    fp2_dbl(&I,&H); fp2_sqr(&I,&I);
    fp2_mul(&J,&H,&I);
    fp2_dbl(&rr,&rr);
    fp2_mul(&V,&U1,&I);
    fp2_sqr(&t,&rr); fp2_sub(&t,&t,&J); fp2_sub(&t,&t,&V); fp2_sub(&t,&t,&V);
    fp2_sub(&V,&V,&t); fp2_mul(&V,&V,&rr);
    fp2_mul(&J,&J,&S1); fp2_dbl(&J,&J);
    fp2_sub(&V,&V,&J);
    fp2_add(&I,&a->z,&b->z); fp2_sqr(&I,&I);
    fp2_sub(&I,&I,&Z1Z1); fp2_sub(&I,&I,&Z2Z2);
    fp2_mul(&I,&I,&H);
    r->x = t; r->y = V; r->z = I;
}

static int g2j_eq(const g2j *a, const g2j *b)
{
    fp2 z1, z2, t1, t2;
    int ai = fp2_is_zero(&a->z), bi = fp2_is_zero(&b->z);
    if (ai || bi) return ai && bi;
    fp2_sqr(&z1,&a->z); fp2_sqr(&z2,&b->z);
    fp2_mul(&t1,&a->x,&z2); fp2_mul(&t2,&b->x,&z1);
    if (!fp2_eq(&t1,&t2)) return 0;
    fp2_mul(&z1,&z1,&a->z); fp2_mul(&z2,&z2,&b->z);
    fp2_mul(&t1,&a->y,&z2); fp2_mul(&t2,&b->y,&z1);
    return fp2_eq(&t1,&t2);
}

/* psi = untwist . Frobenius . twist, in Jacobian coordinates.  On the
 * r-order subgroup psi acts as multiplication by p. */
static void g2j_psi(g2j *r, const g2j *a)
{
    fp2_conj(&r->x,&a->x); fp2_mul(&r->x,&r->x,&G2_FX1);
    fp2_conj(&r->y,&a->y); fp2_mul(&r->y,&r->y,&G2_FY1);
    fp2_conj(&r->z,&a->z);
}

/* Subgroup test for the twist.  Instead of the 254-bit [r]P == O we use the
 * standard BN identity
 *     [x+1]P + psi([x]P) + psi^2([x]P) == psi^3([2x]P),
 * which needs only a 63-bit scalar multiplication. */
static int g2_in_subgroup(const g2 *p)
{
    g2j q, xq, a, c, t;
    int i;
    if (p->inf) return 1;
    q.x = p->x; q.y = p->y; q.z.c0 = FP_ONE; q.z.c1 = FP_ZERO;

    memset(&xq, 0, sizeof xq);
    for (i = 63; i >= 0; i--) {
        g2j_dbl(&xq,&xq);
        if ((BN_X >> i) & 1) g2j_add_mixed(&xq,&xq,p);
    }

    g2j_add(&a,&xq,&q);              /* [x+1]P                      */
    g2j_psi(&t,&xq);                 /* psi([x]P)                   */
    g2j_add(&a,&a,&t);
    g2j_psi(&t,&t);                  /* psi^2([x]P)                 */
    g2j_add(&a,&a,&t);

    g2j_dbl(&c,&xq);                 /* [2x]P                       */
    g2j_psi(&c,&c); g2j_psi(&c,&c); g2j_psi(&c,&c);

    return g2j_eq(&a,&c);
}

/* ------------------------------------------------------------------ */
/* optimal ate pairing                                                 */
/* ------------------------------------------------------------------ */

typedef struct { fp2 c0, c1, c3; } eline;

/* T = (X,Y,Z) homogeneous on the twist; the line through T and T evaluated
 * at the affine G1 point (px,py), scaled by 2*Y*Z^2 (an Fp2 factor, killed
 * by the final exponentiation). */
static void dbl_step(fp2 *X, fp2 *Y, fp2 *Z, const fp *px, const fp *py, eline *l)
{
    fp2 A, B, C, W, S, S2, Bx, H, t0, t1;

    fp2_sqr(&A, X);
    fp2_sqr(&B, Y);
    fp2_sqr(&C, Z);

    /* line coefficients, computed from the current T */
    fp2_mul(&t0,Y,&C); fp2_dbl(&t0,&t0);
    fp2_mul_fp(&l->c0,&t0,py);                    /*  2 Y Z^2 yp        */
    fp2_mul(&t0,&A,Z); fp2_mul3(&t0,&t0);
    fp2_mul_fp(&t1,&t0,px); fp2_neg(&l->c1,&t1);  /* -3 X^2 Z xp        */
    fp2_mul(&t0,&A,X); fp2_mul3(&t0,&t0);
    fp2_mul(&t1,&B,Z); fp2_dbl(&t1,&t1);
    fp2_sub(&l->c3,&t0,&t1);                      /*  3X^3 - 2Y^2 Z     */

    /* T <- 2T */
    fp2_mul3(&W,&A);
    fp2_mul(&S,Y,Z);
    fp2_sqr(&S2,&S);
    fp2_mul(&Bx,X,Y); fp2_mul(&Bx,&Bx,&S);
    fp2_sqr(&H,&W);
    fp2_mul8(&t0,&Bx); fp2_sub(&H,&H,&t0);        /* H = W^2 - 8B       */
    fp2_dbl(&t0,&Bx); fp2_dbl(&t0,&t0);           /* 4B                 */
    fp2_sub(&t0,&t0,&H);
    fp2_mul(&t0,&W,&t0);
    fp2_mul(&t1,&B,&S2); fp2_mul8(&t1,&t1);
    fp2_sub(&t1,&t0,&t1);                         /* Y3                 */
    fp2_mul(&t0,&H,&S); fp2_dbl(&t0,&t0);         /* X3 = 2HS           */
    fp2_mul(&S,&S,&S2); fp2_mul8(&S,&S);          /* Z3 = 8S^3          */
    *X = t0; *Y = t1; *Z = S;
}

/* T <- T + Q with Q = (qx,qy) affine on the twist; line scaled by v. */
static void add_step(fp2 *X, fp2 *Y, fp2 *Z, const fp2 *qx, const fp2 *qy,
                     const fp *px, const fp *py, eline *l)
{
    fp2 u, v, uu, vv, vvv, R, A, t0, t1;

    fp2_mul(&u,qy,Z); fp2_sub(&u,&u,Y);           /* u = qy*Z - Y       */
    fp2_mul(&v,qx,Z); fp2_sub(&v,&v,X);           /* v = qx*Z - X       */

    fp2_mul_fp(&l->c0,&v,py);                     /*  v yp              */
    fp2_mul_fp(&t0,&u,px); fp2_neg(&l->c1,&t0);   /* -u xp              */
    fp2_mul(&t0,&u,qx); fp2_mul(&t1,&v,qy);
    fp2_sub(&l->c3,&t0,&t1);                      /*  u qx - v qy       */

    fp2_sqr(&uu,&u);
    fp2_sqr(&vv,&v);
    fp2_mul(&vvv,&v,&vv);
    fp2_mul(&R,&vv,X);
    fp2_mul(&A,&uu,Z); fp2_sub(&A,&A,&vvv);
    fp2_dbl(&t0,&R); fp2_sub(&A,&A,&t0);
    fp2_mul(&t0,&v,&A);                           /* X3                 */
    fp2_sub(&t1,&R,&A); fp2_mul(&t1,&u,&t1);
    fp2_mul(&R,&vvv,Y); fp2_sub(&t1,&t1,&R);      /* Y3                 */
    fp2_mul(&vvv,&vvv,Z);                         /* Z3                 */
    *X = t0; *Y = t1; *Z = vvv;
}

#define MILLER_BATCH 8

/* Accumulate the Miller function of up to `n` pairs into `f`. */
static void miller_batch(fp12 *f, const g1 *P, const g2 *Q, size_t n)
{
    fp2 X[MILLER_BATCH], Y[MILLER_BATCH], Z[MILLER_BATCH], nqy[MILLER_BATCH];
    fp2 q1x, q1y, q2x, q2y;
    eline l;
    size_t j;
    int i;

    for (j = 0; j < n; j++) {
        X[j] = Q[j].x; Y[j] = Q[j].y;
        Z[j].c0 = FP_ONE; Z[j].c1 = FP_ZERO;
        fp2_neg(&nqy[j], &Q[j].y);
    }

    for (i = ATE_NAF_LEN - 2; i >= 0; i--) {
        fp12_sqr(f, f);
        for (j = 0; j < n; j++) {
            dbl_step(&X[j],&Y[j],&Z[j],&P[j].x,&P[j].y,&l);
            fp12_mul_line(f,f,&l.c0,&l.c1,&l.c3);
        }
        if (ATE_NAF[i] == 1) {
            for (j = 0; j < n; j++) {
                add_step(&X[j],&Y[j],&Z[j],&Q[j].x,&Q[j].y,&P[j].x,&P[j].y,&l);
                fp12_mul_line(f,f,&l.c0,&l.c1,&l.c3);
            }
        } else if (ATE_NAF[i] == -1) {
            for (j = 0; j < n; j++) {
                add_step(&X[j],&Y[j],&Z[j],&Q[j].x,&nqy[j],&P[j].x,&P[j].y,&l);
                fp12_mul_line(f,f,&l.c0,&l.c1,&l.c3);
            }
        }
    }

    /* the two Frobenius steps: T += pi(Q); T += -pi^2(Q) */
    for (j = 0; j < n; j++) {
        fp2_conj(&q1x,&Q[j].x); fp2_mul(&q1x,&q1x,&G2_FX1);
        fp2_conj(&q1y,&Q[j].y); fp2_mul(&q1y,&q1y,&G2_FY1);
        add_step(&X[j],&Y[j],&Z[j],&q1x,&q1y,&P[j].x,&P[j].y,&l);
        fp12_mul_line(f,f,&l.c0,&l.c1,&l.c3);

        fp2_mul(&q2x,&Q[j].x,&G2_FX2);
        fp2_mul(&q2y,&Q[j].y,&G2_FY2N);
        add_step(&X[j],&Y[j],&Z[j],&q2x,&q2y,&P[j].x,&P[j].y,&l);
        fp12_mul_line(f,f,&l.c0,&l.c1,&l.c3);
    }
}

/* f^((p^12-1)/r) */
static void final_exp(fp12 *r, const fp12 *f)
{
    fp12 t, u, a, fx, fx2, fx3, y0, y1, y2;

    /* easy part: f^(p^6-1) then ^(p^2+1) */
    fp12_conj(&a,f);
    fp12_inv(&t,f);
    fp12_mul(&t,&a,&t);
    fp12_frob(&a,&t,2);
    fp12_mul(&t,&a,&t);

    /* hard part, (p^4-p^2+1)/r = p^3 + (6x^2+1)p^2
     *                          + (-36x^3-18x^2-12x+1)p
     *                          + (-36x^3-30x^2-18x-2)                */
    fp12_cyc_pow_u64(&fx,&t,BN_X);
    fp12_cyc_pow_u64(&fx2,&fx,BN_X);
    fp12_cyc_pow_u64(&fx3,&fx2,BN_X);

    /* (f^{p^2})^{6x^2+1} */
    fp12_frob(&u,&fx2,2);
    fp12_cyc_pow_u64(&y0,&u,6);
    fp12_frob(&u,&t,2);
    fp12_mul(&y0,&y0,&u);

    /* (f^p)^{-36x^3-18x^2-12x+1} */
    fp12_frob(&u,&fx3,1); fp12_cyc_pow_u64(&y1,&u,36);
    fp12_frob(&u,&fx2,1); fp12_cyc_pow_u64(&a,&u,18);  fp12_mul(&y1,&y1,&a);
    fp12_frob(&u,&fx,1);  fp12_cyc_pow_u64(&a,&u,12);  fp12_mul(&y1,&y1,&a);
    fp12_conj(&y1,&y1);
    fp12_frob(&u,&t,1);
    fp12_mul(&y1,&y1,&u);

    /* f^{-36x^3-30x^2-18x-2} */
    fp12_cyc_pow_u64(&y2,&fx3,36);
    fp12_cyc_pow_u64(&a,&fx2,30); fp12_mul(&y2,&y2,&a);
    fp12_cyc_pow_u64(&a,&fx,18);  fp12_mul(&y2,&y2,&a);
    fp12_cyc_sqr(&a,&t);          fp12_mul(&y2,&y2,&a);
    fp12_conj(&y2,&y2);

    fp12_frob(&u,&t,3);
    fp12_mul(&u,&u,&y0);
    fp12_mul(&u,&u,&y1);
    fp12_mul(r,&u,&y2);
}

/* ------------------------------------------------------------------ */
/* decoding helpers                                                    */
/* ------------------------------------------------------------------ */

static int read_g1(g1 *p, const uint8_t b[64])
{
    if (fp_from_be(&p->x, b) != 0) return -1;
    if (fp_from_be(&p->y, b + 32) != 0) return -1;
    p->inf = fp_is_zero(&p->x) && fp_is_zero(&p->y);
    if (p->inf) { p->x = FP_ZERO; p->y = FP_ZERO; return 0; }
    return g1_on_curve(p) ? 0 : -1;
}

/* G2 encoding puts the u-coefficient first: x.c1, x.c0, y.c1, y.c0 */
static int read_g2(g2 *p, const uint8_t b[128])
{
    if (fp_from_be(&p->x.c1, b)       != 0) return -1;
    if (fp_from_be(&p->x.c0, b + 32)  != 0) return -1;
    if (fp_from_be(&p->y.c1, b + 64)  != 0) return -1;
    if (fp_from_be(&p->y.c0, b + 96)  != 0) return -1;
    p->inf = fp2_is_zero(&p->x) && fp2_is_zero(&p->y);
    if (p->inf) { p->x = FP2_ZERO; p->y = FP2_ZERO; return 0; }
    if (!g2_on_curve(p)) return -1;
    return g2_in_subgroup(p) ? 0 : -1;
}

static void write_g1(const g1 *p, uint8_t out[64])
{
    if (p->inf) { memset(out, 0, 64); return; }
    fp_to_be(&p->x, out);
    fp_to_be(&p->y, out + 32);
}

/* ------------------------------------------------------------------ */
/* public entry points                                                 */
/* ------------------------------------------------------------------ */

int bn128_add(const uint8_t in[128], uint8_t out[64]);
int bn128_mul(const uint8_t in[96], uint8_t out[64]);
int bn128_pairing(const uint8_t *in, size_t len, uint8_t out[32]);

int bn128_add(const uint8_t in[128], uint8_t out[64])
{
    g1 a, b, c;
    if (read_g1(&a, in) != 0) return -1;
    if (read_g1(&b, in + 64) != 0) return -1;
    g1_add_affine(&c, &a, &b);
    write_g1(&c, out);
    return 0;
}

int bn128_mul(const uint8_t in[96], uint8_t out[64])
{
    g1 a, c;
    uint64_t k[4];
    int i, j;
    if (read_g1(&a, in) != 0) return -1;
    for (i = 0; i < 4; i++) {
        uint64_t w = 0;
        for (j = 0; j < 8; j++) w = (w << 8) | in[64 + i * 8 + j];
        k[3 - i] = w;
    }
    if (a.inf || (k[0] | k[1] | k[2] | k[3]) == 0) { memset(out, 0, 64); return 0; }
    g1_scalar_mul(&c, &a, k);
    write_g1(&c, out);
    return 0;
}

int bn128_pairing(const uint8_t *in, size_t len, uint8_t out[32])
{
    size_t k = len / 192, i, n;
    fp12 f, e;
    g1 P[MILLER_BATCH];
    g2 Q[MILLER_BATCH];

    if (len % 192 != 0) return -1;

    /* Pass 1: validate every point before computing anything. */
    for (i = 0; i < k; i++) {
        g1 p;
        g2 q;
        if (read_g1(&p, in + i * 192) != 0) return -1;
        if (read_g2(&q, in + i * 192 + 64) != 0) return -1;
    }

    /* Pass 2: Miller loops, batched so that the Fp12 squarings are shared. */
    fp12_one(&f);
    i = 0;
    while (i < k) {
        n = 0;
        while (i < k && n < MILLER_BATCH) {
            g1 p;
            g2 q;
            (void)read_g1(&p, in + i * 192);
            (void)read_g2(&q, in + i * 192 + 64);
            i++;
            if (p.inf || q.inf) continue;     /* e(O,Q) = e(P,O) = 1 */
            P[n] = p; Q[n] = q; n++;
        }
        if (n) {
            fp12 fb;
            fp12_one(&fb);
            miller_batch(&fb, P, Q, n);
            fp12_mul(&f, &f, &fb);
        }
    }

    final_exp(&e, &f);
    memset(out, 0, 32);
    if (fp12_is_one(&e)) out[31] = 1;
    return 0;
}

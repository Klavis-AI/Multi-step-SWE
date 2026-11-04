/*
 * EIP-2537  BLS12-381 curve operations   (precompiles 0x0b .. 0x11)
 * EIP-4844  KZG point evaluation          (precompile  0x0a)
 * EIP-7951  secp256r1 / P256VERIFY        (precompile  0x100)
 *
 * BLS12-381 field/curve/pairing arithmetic and the subgroup checks come from
 * the vendored supranational/blst (impl/vendor/blst, Apache-2.0).
 * secp256r1 uses OpenSSL libcrypto.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "blst.h"
#include "crypto.h"                             /* sha256_hash() */

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/obj_mac.h>

/* ------------------------------------------------------------- constants */

/* BLS12-381 base field modulus p (EIP-2537 "Curve parameters"). */
static const uint8_t BLS12_381_P_BE[48] = {
    0x1a, 0x01, 0x11, 0xea, 0x39, 0x7f, 0xe6, 0x9a, 0x4b, 0x1b, 0xa7, 0xb6,
    0x43, 0x4b, 0xac, 0xd7, 0x64, 0x77, 0x4b, 0x84, 0xf3, 0x85, 0x12, 0xbf,
    0x67, 0x30, 0xd2, 0xa0, 0xf6, 0xb0, 0xf6, 0x24, 0x1e, 0xab, 0xff, 0xfe,
    0xb1, 0x53, 0xff, 0xff, 0xb9, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xaa, 0xab
};

/* EIP-4844 BLS_MODULUS == the BLS12-381 main subgroup order q. */
static const uint8_t BLS_MODULUS_BE[32] = {
    0x73, 0xed, 0xa7, 0x53, 0x29, 0x9d, 0x7d, 0x48, 0x33, 0x39, 0xd8, 0x08,
    0x09, 0xa1, 0xd8, 0x05, 0x53, 0xbd, 0xa4, 0x02, 0xff, 0xfe, 0x5b, 0xfe,
    0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x01
};

/*
 * KZG_SETUP_G2[1] == [s]_2, the second G2 point of the mainnet KZG ceremony
 * trusted setup.  A fixed public constant; taken verbatim from the
 * "g2_monomial" section of ethereum/c-kzg-4844 src/trusted_setup.txt (entry 1
 * of that section; entry 0 is the plain G2 generator).  Stored here in its
 * 96-byte Zcash-style compressed form, exactly as it appears there.
 */
static const uint8_t KZG_SETUP_G2_1[96] = {
    0xb5, 0xbf, 0xd7, 0xdd, 0x8c, 0xde, 0xb1, 0x28, 0x84, 0x3b, 0xc2, 0x87,
    0x23, 0x0a, 0xf3, 0x89, 0x26, 0x18, 0x70, 0x75, 0xcb, 0xfb, 0xef, 0xa8,
    0x10, 0x09, 0xa2, 0xce, 0x61, 0x5a, 0xc5, 0x3d, 0x29, 0x14, 0xe5, 0x87,
    0x0c, 0xb4, 0x52, 0xd2, 0xaf, 0xaa, 0xab, 0x24, 0xf3, 0x49, 0x9f, 0x72,
    0x18, 0x5c, 0xbf, 0xee, 0x53, 0x49, 0x27, 0x14, 0x73, 0x44, 0x29, 0xb7,
    0xb3, 0x86, 0x08, 0xe2, 0x39, 0x26, 0xc9, 0x11, 0xcc, 0xec, 0xea, 0xc9,
    0xa3, 0x68, 0x51, 0x47, 0x7b, 0xa4, 0xc6, 0x0b, 0x08, 0x70, 0x41, 0xde,
    0x62, 0x10, 0x00, 0xed, 0xc9, 0x8e, 0xda, 0xda, 0x20, 0xc1, 0xde, 0xf2
};

/* EIP-2537 G1 MSM discount table, k = 1..128; max_discount == entry 128. */
static const uint16_t MSM_DISCOUNT_G1[128] = {
    1000, 949, 848, 797, 764, 750, 738, 728, 719, 712, 705, 698, 692, 687,
    682, 677, 673, 669, 665, 661, 658, 654, 651, 648, 645, 642, 640, 637,
    635, 632, 630, 627, 625, 623, 621, 619, 617, 615, 613, 611, 609, 608,
    606, 604, 603, 601, 599, 598, 596, 595, 593, 592, 591, 589, 588, 586,
    585, 584, 582, 581, 580, 579, 577, 576, 575, 574, 573, 572, 570, 569,
    568, 567, 566, 565, 564, 563, 562, 561, 560, 559, 558, 557, 556, 555,
    554, 553, 552, 551, 550, 549, 548, 547, 547, 546, 545, 544, 543, 542,
    541, 540, 540, 539, 538, 537, 536, 536, 535, 534, 533, 532, 532, 531,
    530, 529, 528, 528, 527, 526, 525, 525, 524, 523, 522, 522, 521, 520,
    520, 519
};

/* EIP-2537 G2 MSM discount table, k = 1..128; max_discount == entry 128. */
static const uint16_t MSM_DISCOUNT_G2[128] = {
    1000, 1000, 923, 884, 855, 832, 812, 796, 782, 770, 759, 749, 740, 732,
    724, 717, 711, 704, 699, 693, 688, 683, 679, 674, 670, 666, 663, 659,
    655, 652, 649, 646, 643, 640, 637, 634, 632, 629, 627, 624, 622, 620,
    618, 615, 613, 611, 609, 607, 606, 604, 602, 600, 598, 597, 595, 593,
    592, 590, 589, 587, 586, 584, 583, 582, 580, 579, 578, 576, 575, 574,
    573, 571, 570, 569, 568, 567, 566, 565, 563, 562, 561, 560, 559, 558,
    557, 556, 555, 554, 553, 552, 552, 551, 550, 549, 548, 547, 546, 545,
    545, 544, 543, 542, 541, 541, 540, 539, 538, 537, 537, 536, 535, 535,
    534, 533, 532, 532, 531, 530, 530, 529, 528, 528, 527, 526, 526, 525,
    524, 524
};

#define BLS_G1ADD    0x0b
#define BLS_G1MSM    0x0c
#define BLS_G2ADD    0x0d
#define BLS_G2MSM    0x0e
#define BLS_PAIRING  0x0f
#define BLS_MAP_G1   0x10
#define BLS_MAP_G2   0x11

#define G1_ADD_GAS       375ULL
#define G2_ADD_GAS       600ULL
#define G1_MUL_GAS     12000ULL
#define G2_MUL_GAS     22500ULL
#define PAIRING_BASE   37700ULL
#define PAIRING_PAIR   32600ULL
#define MAP_G1_GAS      5500ULL
#define MAP_G2_GAS     23800ULL

#define G1_LEN      128          /* uncompressed G1 point */
#define G2_LEN      256          /* uncompressed G2 point */
#define FP_LEN       64          /* padded Fp element     */
#define SCALAR_LEN   32
#define G1_MSM_PAIR (G1_LEN + SCALAR_LEN)       /* 160 */
#define G2_MSM_PAIR (G2_LEN + SCALAR_LEN)       /* 288 */
#define PAIR_LEN    (G1_LEN + G2_LEN)           /* 384 */

/* Scalars are reduced mod q below, so 255 bits always suffice. */
#define SCALAR_BITS 255

/* ---------------------------------------------------------------- Fp codec */

/*
 * EIP-2537: an Fp element is 64 bytes big-endian; the top 16 bytes MUST be
 * zero and the 381-bit remainder MUST be strictly less than the modulus.
 */
static int fp_decode(blst_fp *out, const uint8_t in[FP_LEN])
{
    int i, lt = 0;

    for (i = 0; i < 16; i++)
        if (in[i] != 0) return -1;
    for (i = 0; i < 48; i++) {
        if (in[16 + i] != BLS12_381_P_BE[i]) {
            lt = in[16 + i] < BLS12_381_P_BE[i];
            break;
        }
    }
    if (!lt) return -1;                          /* >= p (equality included) */
    blst_fp_from_bendian(out, in + 16);
    return 0;
}

static void fp_encode(uint8_t out[FP_LEN], const blst_fp *a)
{
    memset(out, 0, 16);
    blst_bendian_from_fp(out + 16, a);
}

/* ------------------------------------------------------------ point codecs */

static int g1_decode(blst_p1_affine *p, const uint8_t in[G1_LEN])
{
    if (fp_decode(&p->x, in) != 0) return -1;
    if (fp_decode(&p->y, in + FP_LEN) != 0) return -1;
    /* blst treats the all-zero affine point as the point at infinity, which
     * is exactly the EIP-2537 encoding convention. */
    return blst_p1_affine_on_curve(p) ? 0 : -1;
}

static void g1_encode(uint8_t out[G1_LEN], const blst_p1 *p)
{
    blst_p1_affine a;

    if (blst_p1_is_inf(p)) { memset(out, 0, G1_LEN); return; }
    blst_p1_to_affine(&a, p);
    fp_encode(out, &a.x);
    fp_encode(out + FP_LEN, &a.y);
}

static int g2_decode(blst_p2_affine *p, const uint8_t in[G2_LEN])
{
    if (fp_decode(&p->x.fp[0], in) != 0) return -1;
    if (fp_decode(&p->x.fp[1], in + FP_LEN) != 0) return -1;
    if (fp_decode(&p->y.fp[0], in + 2 * FP_LEN) != 0) return -1;
    if (fp_decode(&p->y.fp[1], in + 3 * FP_LEN) != 0) return -1;
    return blst_p2_affine_on_curve(p) ? 0 : -1;
}

static void g2_encode(uint8_t out[G2_LEN], const blst_p2 *p)
{
    blst_p2_affine a;

    if (blst_p2_is_inf(p)) { memset(out, 0, G2_LEN); return; }
    blst_p2_to_affine(&a, p);
    fp_encode(out, &a.x.fp[0]);
    fp_encode(out + FP_LEN, &a.x.fp[1]);
    fp_encode(out + 2 * FP_LEN, &a.y.fp[0]);
    fp_encode(out + 3 * FP_LEN, &a.y.fp[1]);
}

/*
 * EIP-2537 scalars are arbitrary 256-bit integers, not necessarily reduced.
 * Every MSM input point is subgroup-checked, so it has order dividing q and
 * reducing the scalar mod q is exact.
 */
static void scalar_decode(blst_scalar *s, const uint8_t in[SCALAR_LEN])
{
    blst_fr fr;

    blst_scalar_from_bendian(s, in);
    blst_fr_from_scalar(&fr, s);                 /* reduces mod q */
    blst_scalar_from_fr(s, &fr);
}

static bool scalar_is_zero(const blst_scalar *s)
{
    int i;
    for (i = 0; i < SCALAR_LEN; i++)
        if (s->b[i]) return false;
    return true;
}

/* ---------------------------------------------------------------------- gas */

static uint64_t msm_gas(const uint16_t table[128], uint64_t mul_gas, size_t k)
{
    uint64_t discount = table[(k < 128 ? k : 128) - 1];
    return ((uint64_t)k * mul_gas * discount) / 1000;
}

uint64_t bls_gas(uint8_t which, size_t ilen)
{
    switch (which) {
    case BLS_G1ADD:
        return ilen == 2 * G1_LEN ? G1_ADD_GAS : 0;
    case BLS_G1MSM:
        if (ilen == 0 || ilen % G1_MSM_PAIR) return 0;
        return msm_gas(MSM_DISCOUNT_G1, G1_MUL_GAS, ilen / G1_MSM_PAIR);
    case BLS_G2ADD:
        return ilen == 2 * G2_LEN ? G2_ADD_GAS : 0;
    case BLS_G2MSM:
        if (ilen == 0 || ilen % G2_MSM_PAIR) return 0;
        return msm_gas(MSM_DISCOUNT_G2, G2_MUL_GAS, ilen / G2_MSM_PAIR);
    case BLS_PAIRING:
        if (ilen == 0 || ilen % PAIR_LEN) return 0;
        return PAIRING_PAIR * (uint64_t)(ilen / PAIR_LEN) + PAIRING_BASE;
    case BLS_MAP_G1:
        return ilen == FP_LEN ? MAP_G1_GAS : 0;
    case BLS_MAP_G2:
        return ilen == 2 * FP_LEN ? MAP_G2_GAS : 0;
    default:
        return 0;
    }
}

/* --------------------------------------------------------------- operations */

static int op_g1_add(const uint8_t *in, size_t len, uint8_t *out, size_t *olen)
{
    blst_p1_affine a, b;
    blst_p1 acc, res;

    if (len != 2 * G1_LEN) return -1;
    if (g1_decode(&a, in) != 0) return -1;
    if (g1_decode(&b, in + G1_LEN) != 0) return -1;
    /* No subgroup check here, as specified by EIP-2537. */
    blst_p1_from_affine(&acc, &a);
    blst_p1_add_or_double_affine(&res, &acc, &b);
    g1_encode(out, &res);
    *olen = G1_LEN;
    return 0;
}

static int op_g2_add(const uint8_t *in, size_t len, uint8_t *out, size_t *olen)
{
    blst_p2_affine a, b;
    blst_p2 acc, res;

    if (len != 2 * G2_LEN) return -1;
    if (g2_decode(&a, in) != 0) return -1;
    if (g2_decode(&b, in + G2_LEN) != 0) return -1;
    blst_p2_from_affine(&acc, &a);
    blst_p2_add_or_double_affine(&res, &acc, &b);
    g2_encode(out, &res);
    *olen = G2_LEN;
    return 0;
}

static int op_g1_msm(const uint8_t *in, size_t len, uint8_t *out, size_t *olen)
{
    blst_p1_affine *pts = NULL;
    blst_scalar *scl = NULL;
    limb_t *scratch = NULL;
    blst_p1 res;
    size_t k, i, n = 0;
    int rc = -1;

    if (len == 0 || len % G1_MSM_PAIR) return -1;
    k = len / G1_MSM_PAIR;
    pts = (blst_p1_affine *)malloc(k * sizeof *pts);
    scl = (blst_scalar *)malloc(k * sizeof *scl);
    if (!pts || !scl) goto done;

    for (i = 0; i < k; i++) {
        const uint8_t *rec = in + i * G1_MSM_PAIR;
        blst_p1_affine p;
        blst_scalar s;

        if (g1_decode(&p, rec) != 0) goto done;
        if (!blst_p1_affine_in_g1(&p)) goto done;       /* subgroup check */
        scalar_decode(&s, rec + G1_LEN);
        if (blst_p1_affine_is_inf(&p) || scalar_is_zero(&s)) continue;
        pts[n] = p;
        scl[n] = s;
        n++;
    }

    memset(&res, 0, sizeof res);                        /* point at infinity */
    if (n == 1) {
        blst_p1 j;
        blst_p1_from_affine(&j, &pts[0]);
        blst_p1_mult(&res, &j, scl[0].b, SCALAR_BITS);
    } else if (n > 1) {
        const blst_p1_affine *pp[2];
        const byte *sp[2];
        scratch = (limb_t *)malloc(blst_p1s_mult_pippenger_scratch_sizeof(n));
        if (!scratch) goto done;
        pp[0] = pts; pp[1] = NULL;                      /* contiguous arrays */
        sp[0] = scl[0].b; sp[1] = NULL;
        blst_p1s_mult_pippenger(&res, pp, n, sp, SCALAR_BITS, scratch);
    }
    g1_encode(out, &res);
    *olen = G1_LEN;
    rc = 0;
done:
    free(scratch);
    free(scl);
    free(pts);
    return rc;
}

static int op_g2_msm(const uint8_t *in, size_t len, uint8_t *out, size_t *olen)
{
    blst_p2_affine *pts = NULL;
    blst_scalar *scl = NULL;
    limb_t *scratch = NULL;
    blst_p2 res;
    size_t k, i, n = 0;
    int rc = -1;

    if (len == 0 || len % G2_MSM_PAIR) return -1;
    k = len / G2_MSM_PAIR;
    pts = (blst_p2_affine *)malloc(k * sizeof *pts);
    scl = (blst_scalar *)malloc(k * sizeof *scl);
    if (!pts || !scl) goto done;

    for (i = 0; i < k; i++) {
        const uint8_t *rec = in + i * G2_MSM_PAIR;
        blst_p2_affine p;
        blst_scalar s;

        if (g2_decode(&p, rec) != 0) goto done;
        if (!blst_p2_affine_in_g2(&p)) goto done;       /* subgroup check */
        scalar_decode(&s, rec + G2_LEN);
        if (blst_p2_affine_is_inf(&p) || scalar_is_zero(&s)) continue;
        pts[n] = p;
        scl[n] = s;
        n++;
    }

    memset(&res, 0, sizeof res);
    if (n == 1) {
        blst_p2 j;
        blst_p2_from_affine(&j, &pts[0]);
        blst_p2_mult(&res, &j, scl[0].b, SCALAR_BITS);
    } else if (n > 1) {
        const blst_p2_affine *pp[2];
        const byte *sp[2];
        scratch = (limb_t *)malloc(blst_p2s_mult_pippenger_scratch_sizeof(n));
        if (!scratch) goto done;
        pp[0] = pts; pp[1] = NULL;
        sp[0] = scl[0].b; sp[1] = NULL;
        blst_p2s_mult_pippenger(&res, pp, n, sp, SCALAR_BITS, scratch);
    }
    g2_encode(out, &res);
    *olen = G2_LEN;
    rc = 0;
done:
    free(scratch);
    free(scl);
    free(pts);
    return rc;
}

static int op_pairing(const uint8_t *in, size_t len, uint8_t *out, size_t *olen)
{
    blst_p1_affine *ps = NULL;
    blst_p2_affine *qs = NULL;
    size_t k, i, n = 0;
    int rc = -1, ok = 0;

    if (len == 0 || len % PAIR_LEN) return -1;
    k = len / PAIR_LEN;
    ps = (blst_p1_affine *)malloc(k * sizeof *ps);
    qs = (blst_p2_affine *)malloc(k * sizeof *qs);
    if (!ps || !qs) goto done;

    for (i = 0; i < k; i++) {
        const uint8_t *rec = in + i * PAIR_LEN;
        blst_p1_affine p;
        blst_p2_affine q;

        if (g1_decode(&p, rec) != 0) goto done;
        if (g2_decode(&q, rec + G1_LEN) != 0) goto done;
        if (!blst_p1_affine_in_g1(&p)) goto done;
        if (!blst_p2_affine_in_g2(&q)) goto done;
        /* e(P, O) = e(O, Q) = 1: drop the pair rather than feed a point at
         * infinity to the Miller loop. */
        if (blst_p1_affine_is_inf(&p) || blst_p2_affine_is_inf(&q)) continue;
        ps[n] = p;
        qs[n] = q;
        n++;
    }

    if (n == 0) {
        ok = 1;                                  /* empty product == 1 */
    } else {
        const blst_p1_affine *pp[2];
        const blst_p2_affine *qp[2];
        blst_fp12 f;
        pp[0] = ps; pp[1] = NULL;
        qp[0] = qs; qp[1] = NULL;
        blst_miller_loop_n(&f, qp, pp, n);
        blst_final_exp(&f, &f);
        ok = blst_fp12_is_one(&f);
    }
    memset(out, 0, 32);
    out[31] = (uint8_t)(ok ? 1 : 0);
    *olen = 32;
    rc = 0;
done:
    free(qs);
    free(ps);
    return rc;
}

static int op_map_g1(const uint8_t *in, size_t len, uint8_t *out, size_t *olen)
{
    blst_fp u;
    blst_p1 p;

    if (len != FP_LEN) return -1;
    if (fp_decode(&u, in) != 0) return -1;
    blst_map_to_g1(&p, &u, NULL);
    g1_encode(out, &p);
    *olen = G1_LEN;
    return 0;
}

static int op_map_g2(const uint8_t *in, size_t len, uint8_t *out, size_t *olen)
{
    blst_fp2 u;
    blst_p2 p;

    if (len != 2 * FP_LEN) return -1;
    if (fp_decode(&u.fp[0], in) != 0) return -1;
    if (fp_decode(&u.fp[1], in + FP_LEN) != 0) return -1;
    blst_map_to_g2(&p, &u, NULL);
    g2_encode(out, &p);
    *olen = G2_LEN;
    return 0;
}

int bls_precompile(uint8_t which, const uint8_t *in, size_t len,
                   uint8_t *out, size_t *olen)
{
    switch (which) {
    case BLS_G1ADD:   return op_g1_add(in, len, out, olen);
    case BLS_G1MSM:   return op_g1_msm(in, len, out, olen);
    case BLS_G2ADD:   return op_g2_add(in, len, out, olen);
    case BLS_G2MSM:   return op_g2_msm(in, len, out, olen);
    case BLS_PAIRING: return op_pairing(in, len, out, olen);
    case BLS_MAP_G1:  return op_map_g1(in, len, out, olen);
    case BLS_MAP_G2:  return op_map_g2(in, len, out, olen);
    default:          return -1;
    }
}

/* ---------------------------------------------------- EIP-4844 KZG proof */

/* "KeyValidate", but the identity point is allowed. */
static int kzg_g1_decode(blst_p1 *out, const uint8_t in[48])
{
    blst_p1_affine a;

    if (blst_p1_uncompress(&a, in) != BLST_SUCCESS) return -1;
    blst_p1_from_affine(out, &a);
    if (blst_p1_is_inf(out)) return 0;
    return blst_p1_in_g1(out) ? 0 : -1;
}

static const blst_p2_affine *kzg_setup_point(void)
{
    static blst_p2_affine s;
    static int state = 0;                        /* 0 unset, 1 ok, -1 bad */

    if (state == 0)
        state = blst_p2_uncompress(&s, KZG_SETUP_G2_1) == BLST_SUCCESS ? 1 : -1;
    return state == 1 ? &s : NULL;
}

int kzg_point_eval(const uint8_t in[192], uint8_t out[64])
{
    const blst_p2_affine *setup;
    uint8_t vhash[32];
    blst_scalar z, y;
    blst_p1 commitment, proof, tmp1, p_minus_y;
    blst_p2 tmp2, x_minus_z;
    blst_p1_affine a1, b1;
    blst_p2_affine a2, b2;
    blst_fp12 loop0, loop1, gt;

    /* versioned_hash == 0x01 || sha256(commitment)[1:] */
    sha256_hash(in + 96, 48, vhash);
    vhash[0] = 0x01;
    if (memcmp(vhash, in, 32) != 0) return -1;

    /* z and y must be canonical field elements (< BLS_MODULUS). */
    blst_scalar_from_bendian(&z, in + 32);
    blst_scalar_from_bendian(&y, in + 64);
    if (!blst_scalar_fr_check(&z)) return -1;
    if (!blst_scalar_fr_check(&y)) return -1;

    if (kzg_g1_decode(&commitment, in + 96) != 0) return -1;
    if (kzg_g1_decode(&proof, in + 144) != 0) return -1;

    setup = kzg_setup_point();
    if (!setup) return -1;

    /* X_minus_z = [s]_2 - [z]_2 */
    blst_p2_mult(&tmp2, blst_p2_generator(), z.b, SCALAR_BITS);
    blst_p2_cneg(&tmp2, true);
    blst_p2_from_affine(&x_minus_z, setup);
    blst_p2_add_or_double(&x_minus_z, &x_minus_z, &tmp2);

    /* P_minus_y = commitment - [y]_1 */
    blst_p1_mult(&tmp1, blst_p1_generator(), y.b, SCALAR_BITS);
    blst_p1_cneg(&tmp1, true);
    blst_p1_add_or_double(&p_minus_y, &commitment, &tmp1);

    /* e(P - [y], G2) == e(proof, [s]_2 - [z]_2), evaluated as a single
     * product by negating the first G1 argument. */
    blst_p1_cneg(&p_minus_y, true);
    blst_p1_to_affine(&a1, &p_minus_y);
    blst_p2_to_affine(&a2, blst_p2_generator());
    blst_p1_to_affine(&b1, &proof);
    blst_p2_to_affine(&b2, &x_minus_z);

    blst_miller_loop(&loop0, &a2, &a1);
    blst_miller_loop(&loop1, &b2, &b1);
    blst_fp12_mul(&gt, &loop0, &loop1);
    blst_final_exp(&gt, &gt);
    if (!blst_fp12_is_one(&gt)) return -1;

    /* FIELD_ELEMENTS_PER_BLOB (4096) || BLS_MODULUS */
    memset(out, 0, 64);
    out[30] = 0x10;
    memcpy(out + 32, BLS_MODULUS_BE, 32);
    return 0;
}

/* ----------------------------------------------- EIP-7951 secp256r1 verify */

static const EC_GROUP *p256_group(void)
{
    static EC_GROUP *g = NULL;

    if (!g) g = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);
    return g;
}

int p256_verify(const uint8_t in[160])
{
    const EC_GROUP *grp = p256_group();
    const BIGNUM *order;
    BIGNUM *h, *r, *s, *qx, *qy, *field, *sinv, *u1, *u2, *x;
    BN_CTX *ctx;
    EC_POINT *q = NULL, *rp = NULL;
    int rc = -1;

    if (!grp) return -1;
    ctx = BN_CTX_new();
    if (!ctx) return -1;
    BN_CTX_start(ctx);

    h     = BN_CTX_get(ctx);
    r     = BN_CTX_get(ctx);
    s     = BN_CTX_get(ctx);
    qx    = BN_CTX_get(ctx);
    qy    = BN_CTX_get(ctx);
    field = BN_CTX_get(ctx);
    sinv  = BN_CTX_get(ctx);
    u1    = BN_CTX_get(ctx);
    u2    = BN_CTX_get(ctx);
    x     = BN_CTX_get(ctx);
    if (!x) goto done;

    order = EC_GROUP_get0_order(grp);
    if (!order) goto done;
    if (!EC_GROUP_get_curve(grp, field, NULL, NULL, ctx)) goto done;

    if (!BN_bin2bn(in, 32, h)) goto done;
    if (!BN_bin2bn(in + 32, 32, r)) goto done;
    if (!BN_bin2bn(in + 64, 32, s)) goto done;
    if (!BN_bin2bn(in + 96, 32, qx)) goto done;
    if (!BN_bin2bn(in + 128, 32, qy)) goto done;

    /* 0 < r < n and 0 < s < n */
    if (BN_is_zero(r) || BN_cmp(r, order) >= 0) goto done;
    if (BN_is_zero(s) || BN_cmp(s, order) >= 0) goto done;
    /* 0 <= qx < p and 0 <= qy < p, and Q is not the point at infinity */
    if (BN_cmp(qx, field) >= 0 || BN_cmp(qy, field) >= 0) goto done;
    if (BN_is_zero(qx) && BN_is_zero(qy)) goto done;

    q  = EC_POINT_new(grp);
    rp = EC_POINT_new(grp);
    if (!q || !rp) goto done;
    /* Also rejects a public key that is not on the curve. */
    if (!EC_POINT_set_affine_coordinates(grp, q, qx, qy, ctx)) goto done;
    if (EC_POINT_is_on_curve(grp, q, ctx) != 1) goto done;

    /* u1 = h * s^-1 mod n, u2 = r * s^-1 mod n */
    if (!BN_mod_inverse(sinv, s, order, ctx)) goto done;
    if (!BN_nnmod(h, h, order, ctx)) goto done;
    if (!BN_mod_mul(u1, h, sinv, order, ctx)) goto done;
    if (!BN_mod_mul(u2, r, sinv, order, ctx)) goto done;

    /* R' = u1 * G + u2 * Q */
    if (!EC_POINT_mul(grp, rp, u1, q, u2, ctx)) goto done;
    if (EC_POINT_is_at_infinity(grp, rp)) goto done;
    if (!EC_POINT_get_affine_coordinates(grp, rp, x, NULL, ctx)) goto done;

    /* valid iff R'.x == r (mod n) */
    if (!BN_nnmod(x, x, order, ctx)) goto done;
    if (BN_cmp(x, r) == 0) rc = 0;

done:
    EC_POINT_free(rp);
    EC_POINT_free(q);
    BN_CTX_end(ctx);
    BN_CTX_free(ctx);
    ERR_clear_error();
    return rc;
}

#include "evm.h"
#include "crypto.h"
#include "bignum.h"
#include "keccak.h"
#include <stdlib.h>
#include <string.h>

/* implemented in bn128.c / bls.c / kzg.c */
int bn128_add(const uint8_t in[128], uint8_t out[64]);
int bn128_mul(const uint8_t in[96], uint8_t out[64]);
int bn128_pairing(const uint8_t *in, size_t len, uint8_t out[32]);
int bls_precompile(uint8_t which, const uint8_t *in, size_t len, uint8_t *out, size_t *olen);
int kzg_point_eval(const uint8_t in[192], uint8_t out[64]);
int p256_verify(const uint8_t in[160]);

static uint64_t last_prec_addr(address a)
{
    uint64_t v = 0;
    for (int i = 0; i < 20; i++) v = (v << 8) | a.b[i];
    return v;
}

bool is_precompile(fork_id f, address a)
{
    for (int i = 0; i < 18; i++) if (a.b[i]) return false;
    uint64_t n = ((uint64_t)a.b[18] << 8) | a.b[19];
    if (n >= 1 && n <= 4) return true;
    if (IS_BYZANTIUM(f) && n >= 5 && n <= 8) return true;
    if (IS_ISTANBUL(f) && n == 9) return true;
    if (IS_CANCUN(f) && n == 0x0a) return true;
    if (IS_PRAGUE(f) && n >= 0x0b && n <= 0x11) return true;
    if (IS_OSAKA(f) && n == 0x100) return true;
    return false;
}

static ex_result prec_fail(void)
{
    ex_result r; memset(&r, 0, sizeof r);
    r.status = EX_HALT; r.gas_left = 0;
    return r;
}

static ex_result prec_ok(uint64_t gas_left, const uint8_t *out, size_t n)
{
    ex_result r; memset(&r, 0, sizeof r);
    r.status = EX_OK; r.gas_left = gas_left;
    if (n) { r.ret = (uint8_t *)malloc(n); memcpy(r.ret, out, n); r.ret_len = n; }
    return r;
}

static uint64_t words(uint64_t n) { return (n + 31) / 32; }

static void read_in(const uint8_t *in, size_t ilen, size_t off, uint8_t *out, size_t n)
{
    for (size_t i = 0; i < n; i++) out[i] = (off + i < ilen) ? in[off + i] : 0;
}

/* ---------------------------------------------------------------- MODEXP */
/* The reference tool computes the modexp gas in arbitrary precision and returns "more gas
 * than exists" when it does not fit; doing it in 64 bits wraps a gigantic cost down to a
 * payable one, which is the difference between a precompile that runs and one that is out of
 * gas.  Saturate instead of wrapping. */
static uint64_t mul_sat(uint64_t a, uint64_t b)
{
    if (a == 0 || b == 0) return 0;
    if (a > UINT64_MAX / b) return UINT64_MAX;
    return a * b;
}

static uint64_t add_sat(uint64_t a, uint64_t b)
{
    return (a > UINT64_MAX - b) ? UINT64_MAX : a + b;
}

static uint64_t mult_complexity_byzantium(uint64_t x)
{
    if (x <= 64) return mul_sat(x, x);
    if (x <= 1024) return add_sat(mul_sat(x, x) / 4, mul_sat(96, x)) - 3072;
    return add_sat(mul_sat(x, x) / 16, mul_sat(480, x)) - 199680;
}

/* the adjusted exponent length of EIP-198 / EIP-2565 / EIP-7883: `multiplier` is the
 * per-byte weight applied to everything past the head 32 bytes (8, or 16 from Osaka). */
static uint64_t adjusted_exp_len_mult(const uint8_t *in, size_t ilen, size_t exp_off,
                                      uint64_t elen, uint64_t multiplier)
{
    uint64_t head_bits = 0;
    uint64_t n = elen < 32 ? elen : 32;
    for (uint64_t i = 0; i < n; i++) {
        uint8_t b = (exp_off + i < ilen) ? in[exp_off + i] : 0;
        if (b) {
            int bl = 0;
            while (b) { bl++; b >>= 1; }
            head_bits = (n - 1 - i) * 8 + (uint64_t)bl;
            break;
        }
    }
    uint64_t iters = (elen > 32) ? multiplier * (elen - 32) : 0;
    if (head_bits) iters += head_bits - 1;
    return iters ? iters : 1;
}

static ex_result run_modexp(fork_id fk, const uint8_t *in, size_t ilen, uint64_t gas)
{
    uint8_t hdr[96];
    read_in(in, ilen, 0, hdr, 96);
    u256 bl, el, ml;
    u256_from_be(&bl, hdr, 32);
    u256_from_be(&el, hdr + 32, 32);
    u256_from_be(&ml, hdr + 64, 32);
    /* EIP-7823 bounds the three lengths from Osaka; before that they are only bounded by
     * what the gas formula can charge for. */
    if (IS_OSAKA(fk)) {
        if (!u256_fits_u64(bl) || !u256_fits_u64(el) || !u256_fits_u64(ml)) return prec_fail();
        if (bl.v[0] > 1024 || el.v[0] > 1024 || ml.v[0] > 1024) return prec_fail();
    }
    /* With no base and no modulus there is nothing to compute and nothing to read, so the
     * exponent length is never looked at -- it may be 2^255 and the call still succeeds for
     * the floor price.  The reference tool short-circuits here, before the gas formula. */
    if (u256_is_zero(bl) && u256_is_zero(ml)) {
        uint64_t floor_cost = IS_OSAKA(fk) ? 500 : (IS_BERLIN(fk) ? 200 : 0);
        if (gas < floor_cost) return prec_fail();
        return prec_ok(gas - floor_cost, NULL, 0);
    }
    if (!u256_fits_u64(bl) || !u256_fits_u64(el) || !u256_fits_u64(ml)) return prec_fail();
    uint64_t blen = bl.v[0], elen = el.v[0], mlen = ml.v[0];
    if (blen > (1ULL << 32) || elen > (1ULL << 32) || mlen > (1ULL << 32)) return prec_fail();

    uint64_t maxlen = blen > mlen ? blen : mlen;
    uint64_t w = (maxlen + 7) / 8;
    uint64_t cost;
    if (IS_OSAKA(fk)) {                                   /* EIP-7883 */
        uint64_t mc = (maxlen <= 32) ? 16 : mul_sat(2, mul_sat(w, w));
        uint64_t iters = adjusted_exp_len_mult(in, ilen, 96 + blen, elen, 16);
        cost = mul_sat(mc, iters);
        if (cost < 500) cost = 500;
    } else if (IS_BERLIN(fk)) {                           /* EIP-2565 */
        uint64_t iters = adjusted_exp_len_mult(in, ilen, 96 + blen, elen, 8);
        cost = mul_sat(mul_sat(w, w), iters) / 3;
        if (cost < 200) cost = 200;
    } else {                                              /* EIP-198 */
        uint64_t iters = adjusted_exp_len_mult(in, ilen, 96 + blen, elen, 8);
        cost = mul_sat(mult_complexity_byzantium(maxlen), iters) / 20;
    }
    if (gas < cost) return prec_fail();
    gas -= cost;
    if (mlen == 0) return prec_ok(gas, NULL, 0);

    uint8_t *base = (uint8_t *)calloc(blen ? blen : 1, 1);
    uint8_t *e = (uint8_t *)calloc(elen ? elen : 1, 1);
    uint8_t *m = (uint8_t *)calloc(mlen, 1);
    uint8_t *out = (uint8_t *)calloc(mlen, 1);
    /* The declared lengths reach into the gigabytes; anything that large costs more gas than
     * exists, so a failed allocation here means the input was never payable in the first
     * place.  Fail the call rather than dereferencing a null pointer. */
    if (!base || !e || !m || !out) {
        free(base); free(e); free(m); free(out);
        return prec_fail();
    }
    read_in(in, ilen, 96, base, blen);
    read_in(in, ilen, 96 + blen, e, elen);
    read_in(in, ilen, 96 + blen + elen, m, mlen);
    if (bn_modexp(base, blen, e, elen, m, mlen, out) != 0) {
        free(base); free(e); free(m); free(out);
        return prec_fail();
    }
    ex_result r = prec_ok(gas, out, mlen);
    free(base); free(e); free(m); free(out);
    return r;
}

/* ---------------------------------------------------------------- BLAKE2F */
static ex_result run_blake2f(const uint8_t *in, size_t ilen, uint64_t gas)
{
    if (ilen != 213) return prec_fail();
    if (in[212] != 0 && in[212] != 1) return prec_fail();
    uint32_t rounds = ((uint32_t)in[0] << 24) | ((uint32_t)in[1] << 16) | ((uint32_t)in[2] << 8) | in[3];
    if (gas < rounds) return prec_fail();
    gas -= rounds;
    uint64_t h[8], m[16], t[2];
    for (int i = 0; i < 8; i++) {
        uint64_t v = 0;
        for (int j = 7; j >= 0; j--) v = (v << 8) | in[4 + i * 8 + j];
        h[i] = v;
    }
    for (int i = 0; i < 16; i++) {
        uint64_t v = 0;
        for (int j = 7; j >= 0; j--) v = (v << 8) | in[68 + i * 8 + j];
        m[i] = v;
    }
    for (int i = 0; i < 2; i++) {
        uint64_t v = 0;
        for (int j = 7; j >= 0; j--) v = (v << 8) | in[196 + i * 8 + j];
        t[i] = v;
    }
    blake2f(rounds, h, m, t, in[212] == 1);
    uint8_t out[64];
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++) out[i * 8 + j] = (uint8_t)(h[i] >> (8 * j));
    return prec_ok(gas, out, 64);
}

/* ------------------------------------------------------------------ entry */
ex_result run_precompile(vm_t *vm, address a, const uint8_t *in, size_t ilen, uint64_t gas)
{
    uint64_t n = last_prec_addr(a);
    fork_id fk = vm->fork;
    vm->precompile_calls++;

    switch (n) {
    case 1: {                                                 /* ECRECOVER */
        if (gas < 3000) return prec_fail();
        gas -= 3000;
        uint8_t buf[128];
        read_in(in, ilen, 0, buf, 128);
        u256 v, r, s;
        u256_from_be(&v, buf + 32, 32);
        u256_from_be(&r, buf + 64, 32);
        u256_from_be(&s, buf + 96, 32);
        if (!u256_fits_u64(v) || (v.v[0] != 27 && v.v[0] != 28)) return prec_ok(gas, NULL, 0);
        address out;
        if (!secp_recover(buf, &r, &s, (int)(v.v[0] - 27), false, &out)) return prec_ok(gas, NULL, 0);
        uint8_t res[32];
        memset(res, 0, 32);
        memcpy(res + 12, out.b, 20);
        return prec_ok(gas, res, 32);
    }
    case 2: {                                                 /* SHA256 */
        uint64_t cost = 60 + 12 * words(ilen);
        if (gas < cost) return prec_fail();
        gas -= cost;
        uint8_t out[32];
        sha256_hash(in, ilen, out);
        return prec_ok(gas, out, 32);
    }
    case 3: {                                                 /* RIPEMD160 */
        uint64_t cost = 600 + 120 * words(ilen);
        if (gas < cost) return prec_fail();
        gas -= cost;
        uint8_t d[20], out[32];
        ripemd160_hash(in, ilen, d);
        memset(out, 0, 32);
        memcpy(out + 12, d, 20);
        return prec_ok(gas, out, 32);
    }
    case 4: {                                                 /* IDENTITY */
        uint64_t cost = 15 + 3 * words(ilen);
        if (gas < cost) return prec_fail();
        gas -= cost;
        return prec_ok(gas, in, ilen);
    }
    case 5: return run_modexp(fk, in, ilen, gas);
    case 6: {                                                 /* BN_ADD */
        uint64_t cost = IS_ISTANBUL(fk) ? 150 : 500;
        if (gas < cost) return prec_fail();
        gas -= cost;
        uint8_t buf[128], out[64];
        read_in(in, ilen, 0, buf, 128);
        if (bn128_add(buf, out) != 0) return prec_fail();
        return prec_ok(gas, out, 64);
    }
    case 7: {                                                 /* BN_MUL */
        uint64_t cost = IS_ISTANBUL(fk) ? 6000 : 40000;
        if (gas < cost) return prec_fail();
        gas -= cost;
        uint8_t buf[96], out[64];
        read_in(in, ilen, 0, buf, 96);
        if (bn128_mul(buf, out) != 0) return prec_fail();
        return prec_ok(gas, out, 64);
    }
    case 8: {                                                 /* BN_PAIRING */
        if (ilen % 192 != 0) return prec_fail();
        uint64_t k = ilen / 192;
        uint64_t cost = IS_ISTANBUL(fk) ? (45000 + 34000 * k) : (100000 + 80000 * k);
        if (gas < cost) return prec_fail();
        gas -= cost;
        uint8_t out[32];
        if (bn128_pairing(in, ilen, out) != 0) return prec_fail();
        return prec_ok(gas, out, 32);
    }
    case 9: return run_blake2f(in, ilen, gas);
    case 0x0a: {                                              /* KZG point evaluation */
        if (gas < 50000) return prec_fail();
        gas -= 50000;
        if (ilen != 192) return prec_fail();
        uint8_t out[64];
        if (kzg_point_eval(in, out) != 0) return prec_fail();
        return prec_ok(gas, out, 64);
    }
    case 0x100: {                                             /* P256VERIFY, EIP-7951 */
        if (gas < 6900) return prec_fail();
        gas -= 6900;
        if (ilen != 160) return prec_ok(gas, NULL, 0);
        if (p256_verify(in) != 0) return prec_ok(gas, NULL, 0);
        uint8_t out[32];
        memset(out, 0, 32);
        out[31] = 1;
        return prec_ok(gas, out, 32);
    }
    default:
        if (n >= 0x0b && n <= 0x11) {
            uint8_t out[256];
            size_t olen = 0;
            uint64_t cost = 0;
            extern uint64_t bls_gas(uint8_t which, size_t ilen);
            cost = bls_gas((uint8_t)n, ilen);
            if (cost == 0 || gas < cost) return prec_fail();
            gas -= cost;
            if (bls_precompile((uint8_t)n, in, ilen, out, &olen) != 0) return prec_fail();
            return prec_ok(gas, out, olen);
        }
        return prec_fail();
    }
}

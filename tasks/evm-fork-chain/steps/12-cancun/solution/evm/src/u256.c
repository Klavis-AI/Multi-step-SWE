#include "u256.h"
#include <stdlib.h>

typedef unsigned __int128 u128;

void u256_from_be(u256 *r, const uint8_t *b, size_t len)
{
    uint8_t buf[32];
    memset(buf, 0, 32);
    if (len > 32) { b += len - 32; len = 32; }
    memcpy(buf + (32 - len), b, len);
    for (int i = 0; i < 4; i++) {
        uint64_t x = 0;
        for (int j = 0; j < 8; j++) x = (x << 8) | buf[(3 - i) * 8 + j];
        r->v[i] = x;
    }
}

void u256_to_be(const u256 *a, uint8_t out[32])
{
    for (int i = 0; i < 4; i++) {
        uint64_t x = a->v[3 - i];
        for (int j = 0; j < 8; j++) out[i * 8 + j] = (uint8_t)(x >> (56 - 8 * j));
    }
}

int u256_cmp(u256 a, u256 b)
{
    for (int i = 3; i >= 0; i--) {
        if (a.v[i] != b.v[i]) return a.v[i] < b.v[i] ? -1 : 1;
    }
    return 0;
}

static inline bool is_neg(u256 a) { return (a.v[3] >> 63) != 0; }

int u256_scmp(u256 a, u256 b)
{
    bool na = is_neg(a), nb = is_neg(b);
    if (na != nb) return na ? -1 : 1;
    return u256_cmp(a, b);
}

u256 u256_add(u256 a, u256 b)
{
    u256 r; u128 carry = 0;
    for (int i = 0; i < 4; i++) {
        u128 s = (u128)a.v[i] + b.v[i] + carry;
        r.v[i] = (uint64_t)s;
        carry = s >> 64;
    }
    return r;
}

u256 u256_sub(u256 a, u256 b)
{
    u256 r; u128 borrow = 0;
    for (int i = 0; i < 4; i++) {
        u128 s = (u128)a.v[i] - b.v[i] - borrow;
        r.v[i] = (uint64_t)s;
        borrow = (s >> 64) & 1;
    }
    return r;
}

u256 u256_mul(u256 a, u256 b)
{
    uint64_t p[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; i++) {
        uint64_t carry = 0;
        for (int j = 0; i + j < 4; j++) {
            u128 t = (u128)a.v[i] * b.v[j] + p[i + j] + carry;
            p[i + j] = (uint64_t)t;
            carry = (uint64_t)(t >> 64);
        }
    }
    u256 r; memcpy(r.v, p, 32); return r;
}

/* full 512-bit product, little-endian 8 limbs */
static void mul_full(const uint64_t a[4], const uint64_t b[4], uint64_t out[8])
{
    memset(out, 0, 8 * sizeof(uint64_t));
    for (int i = 0; i < 4; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < 4; j++) {
            u128 t = (u128)a[i] * b[j] + out[i + j] + carry;
            out[i + j] = (uint64_t)t;
            carry = (uint64_t)(t >> 64);
        }
        out[i + 4] += carry;
    }
}

static size_t mp_len(const uint64_t *x, size_t n)
{
    while (n > 0 && x[n - 1] == 0) n--;
    return n;
}

static int nlz64(uint64_t x)
{
    int n = 0;
    if (x == 0) return 64;
    while ((x & 0x8000000000000000ULL) == 0) { x <<= 1; n++; }
    return n;
}

/* Knuth algorithm D.  quot may be NULL, rem may be NULL.
 * quot needs nlen limbs, rem needs dlen limbs. */
void mp_divmod(const uint64_t *num, size_t nlen, const uint64_t *den, size_t dlen,
               uint64_t *quot, uint64_t *rem)
{
    size_t n = mp_len(den, dlen);
    size_t m = mp_len(num, nlen);
    if (quot) memset(quot, 0, nlen * sizeof(uint64_t));
    if (rem) memset(rem, 0, dlen * sizeof(uint64_t));
    if (n == 0) return;                    /* division by zero -> 0/0 */
    if (m < n) {
        if (rem) memcpy(rem, num, (m < dlen ? m : dlen) * sizeof(uint64_t));
        return;
    }
    if (n == 1) {
        uint64_t d = den[0], r = 0;
        for (size_t i = m; i-- > 0; ) {
            u128 cur = ((u128)r << 64) | num[i];
            uint64_t q = (uint64_t)(cur / d);
            r = (uint64_t)(cur % d);
            if (quot) quot[i] = q;
        }
        if (rem) rem[0] = r;
        return;
    }

    int s = nlz64(den[n - 1]);
    uint64_t *un = (uint64_t *)calloc(m + 1, sizeof(uint64_t));
    uint64_t *vn = (uint64_t *)calloc(n, sizeof(uint64_t));
    for (size_t i = n; i-- > 0; )
        vn[i] = (den[i] << s) | (i > 0 && s ? (den[i - 1] >> (64 - s)) : 0);
    un[m] = s ? (num[m - 1] >> (64 - s)) : 0;
    for (size_t i = m; i-- > 0; )
        un[i] = (num[i] << s) | (i > 0 && s ? (num[i - 1] >> (64 - s)) : 0);

    for (size_t j = m - n + 1; j-- > 0; ) {
        u128 cur = ((u128)un[j + n] << 64) | un[j + n - 1];
        u128 qhat = cur / vn[n - 1];
        u128 rhat = cur % vn[n - 1];
        while (qhat > (u128)UINT64_MAX ||
               (u128)(uint64_t)qhat * vn[n - 2] > (((u128)(uint64_t)rhat << 64) | un[j + n - 2])) {
            qhat--;
            rhat += vn[n - 1];
            if (rhat > (u128)UINT64_MAX) break;
        }
        uint64_t qh = (uint64_t)qhat;
        /* multiply and subtract */
        u128 borrow = 0, carry = 0;
        for (size_t i = 0; i < n; i++) {
            u128 p = (u128)qh * vn[i] + carry;
            carry = p >> 64;
            u128 t = (u128)un[i + j] - (uint64_t)p - borrow;
            un[i + j] = (uint64_t)t;
            borrow = (t >> 64) & 1;
        }
        u128 t = (u128)un[j + n] - carry - borrow;
        un[j + n] = (uint64_t)t;
        if ((t >> 64) & 1) {  /* add back */
            qh--;
            u128 c = 0;
            for (size_t i = 0; i < n; i++) {
                u128 sum = (u128)un[i + j] + vn[i] + c;
                un[i + j] = (uint64_t)sum;
                c = sum >> 64;
            }
            un[j + n] += (uint64_t)c;
        }
        if (quot && j < nlen) quot[j] = qh;
    }
    if (rem) {
        for (size_t i = 0; i < n && i < dlen; i++)
            rem[i] = (un[i] >> s) | (s && i + 1 < n ? (un[i + 1] << (64 - s)) : 0);
    }
    free(un);
    free(vn);
}

u256 u256_div(u256 a, u256 b)
{
    if (u256_is_zero(b)) return U256_ZERO;
    u256 q;
    mp_divmod(a.v, 4, b.v, 4, q.v, NULL);
    return q;
}

u256 u256_mod(u256 a, u256 b)
{
    if (u256_is_zero(b)) return U256_ZERO;
    u256 r;
    mp_divmod(a.v, 4, b.v, 4, NULL, r.v);
    return r;
}

static u256 u256_neg(u256 a) { return u256_add(u256_not(a), U256_ONE); }

u256 u256_sdiv(u256 a, u256 b)
{
    if (u256_is_zero(b)) return U256_ZERO;
    bool na = is_neg(a), nb = is_neg(b);
    u256 ua = na ? u256_neg(a) : a;
    u256 ub = nb ? u256_neg(b) : b;
    u256 q = u256_div(ua, ub);
    if (na != nb) q = u256_neg(q);
    return q;
}

u256 u256_smod(u256 a, u256 b)
{
    if (u256_is_zero(b)) return U256_ZERO;
    bool na = is_neg(a), nb = is_neg(b);
    u256 ua = na ? u256_neg(a) : a;
    u256 ub = nb ? u256_neg(b) : b;
    u256 r = u256_mod(ua, ub);
    if (na) r = u256_neg(r);
    (void)nb;
    return r;
}

u256 u256_addmod(u256 a, u256 b, u256 m)
{
    if (u256_is_zero(m)) return U256_ZERO;
    uint64_t sum[5] = {0, 0, 0, 0, 0};
    u128 carry = 0;
    for (int i = 0; i < 4; i++) {
        u128 s = (u128)a.v[i] + b.v[i] + carry;
        sum[i] = (uint64_t)s;
        carry = s >> 64;
    }
    sum[4] = (uint64_t)carry;
    u256 r;
    mp_divmod(sum, 5, m.v, 4, NULL, r.v);
    return r;
}

u256 u256_mulmod(u256 a, u256 b, u256 m)
{
    if (u256_is_zero(m)) return U256_ZERO;
    uint64_t prod[8];
    mul_full(a.v, b.v, prod);
    u256 r;
    mp_divmod(prod, 8, m.v, 4, NULL, r.v);
    return r;
}

u256 u256_exp(u256 base, u256 e)
{
    u256 result = U256_ONE;
    u256 b = base;
    for (int i = 0; i < 256; i++) {
        if ((e.v[i / 64] >> (i % 64)) & 1) result = u256_mul(result, b);
        if (i != 255) b = u256_mul(b, b);
    }
    return result;
}

u256 u256_and(u256 a, u256 b) { u256 r; for (int i = 0; i < 4; i++) r.v[i] = a.v[i] & b.v[i]; return r; }
u256 u256_or(u256 a, u256 b)  { u256 r; for (int i = 0; i < 4; i++) r.v[i] = a.v[i] | b.v[i]; return r; }
u256 u256_xor(u256 a, u256 b) { u256 r; for (int i = 0; i < 4; i++) r.v[i] = a.v[i] ^ b.v[i]; return r; }
u256 u256_not(u256 a)         { u256 r; for (int i = 0; i < 4; i++) r.v[i] = ~a.v[i]; return r; }

u256 u256_shl(u256 shift, u256 val)
{
    if (!u256_fits_u64(shift) || shift.v[0] >= 256) return U256_ZERO;
    unsigned n = (unsigned)shift.v[0];
    unsigned limb = n / 64, bit = n % 64;
    u256 r = U256_ZERO;
    for (int i = 3; i >= 0; i--) {
        int src = i - (int)limb;
        if (src < 0) continue;
        uint64_t x = val.v[src] << bit;
        if (bit && src > 0) x |= val.v[src - 1] >> (64 - bit);
        r.v[i] = x;
    }
    return r;
}

u256 u256_shr(u256 shift, u256 val)
{
    if (!u256_fits_u64(shift) || shift.v[0] >= 256) return U256_ZERO;
    unsigned n = (unsigned)shift.v[0];
    unsigned limb = n / 64, bit = n % 64;
    u256 r = U256_ZERO;
    for (int i = 0; i < 4; i++) {
        int src = i + (int)limb;
        if (src > 3) continue;
        uint64_t x = val.v[src] >> bit;
        if (bit && src < 3) x |= val.v[src + 1] << (64 - bit);
        r.v[i] = x;
    }
    return r;
}

u256 u256_sar(u256 shift, u256 val)
{
    bool neg = is_neg(val);
    if (!u256_fits_u64(shift) || shift.v[0] >= 256) {
        if (neg) { u256 r; memset(r.v, 0xff, 32); return r; }
        return U256_ZERO;
    }
    u256 r = u256_shr(shift, val);
    if (neg) {
        unsigned n = (unsigned)shift.v[0];
        if (n) {
            u256 mask;
            memset(mask.v, 0xff, 32);
            mask = u256_shl(u256_from_u64(256 - n), mask);
            r = u256_or(r, mask);
        }
    }
    return r;
}

u256 u256_byte(u256 idx, u256 val)
{
    if (!u256_fits_u64(idx) || idx.v[0] >= 32) return U256_ZERO;
    uint8_t b[32];
    u256_to_be(&val, b);
    return u256_from_u64(b[idx.v[0]]);
}

u256 u256_signextend(u256 k, u256 val)
{
    if (!u256_fits_u64(k) || k.v[0] >= 31) return val;
    unsigned byte_index = (unsigned)k.v[0];
    unsigned bit = byte_index * 8 + 7;
    bool sign = ((val.v[bit / 64] >> (bit % 64)) & 1) != 0;
    u256 mask;
    memset(mask.v, 0xff, 32);
    mask = u256_shl(u256_from_u64(bit + 1), mask);   /* high bits */
    if (sign) return u256_or(val, mask);
    return u256_and(val, u256_not(mask));
}

int u256_bitlen(u256 a)
{
    for (int i = 3; i >= 0; i--) {
        if (a.v[i]) return i * 64 + (64 - nlz64(a.v[i]));
    }
    return 0;
}

int u256_bytelen(u256 a)
{
    int b = u256_bitlen(a);
    return (b + 7) / 8;
}

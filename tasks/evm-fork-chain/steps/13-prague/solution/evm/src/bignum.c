#include "bignum.h"
#include "u256.h"
#include <stdlib.h>
#include <string.h>

typedef unsigned __int128 u128;

/* little-endian limb vectors */
static size_t to_limbs(const uint8_t *be, size_t len, uint64_t **out)
{
    size_t n = (len + 7) / 8;
    if (n == 0) n = 1;
    uint64_t *v = (uint64_t *)calloc(n, sizeof(uint64_t));
    if (!v) { *out = NULL; return 0; }
    for (size_t i = 0; i < len; i++) {
        size_t bytepos = len - 1 - i;
        v[i / 8] |= (uint64_t)be[bytepos] << (8 * (i % 8));
    }
    *out = v;
    return n;
}

static size_t trim(const uint64_t *v, size_t n) { while (n > 0 && v[n - 1] == 0) n--; return n; }

static void mul_mod(const uint64_t *a, const uint64_t *b, size_t n,
                    const uint64_t *m, size_t mn, uint64_t *out, uint64_t *scratch)
{
    memset(scratch, 0, 2 * n * sizeof(uint64_t));
    for (size_t i = 0; i < n; i++) {
        uint64_t carry = 0;
        for (size_t j = 0; j < n; j++) {
            u128 t = (u128)a[i] * b[j] + scratch[i + j] + carry;
            scratch[i + j] = (uint64_t)t;
            carry = (uint64_t)(t >> 64);
        }
        scratch[i + n] += carry;
    }
    mp_divmod(scratch, 2 * n, m, mn, NULL, out);
}

int bn_modexp(const uint8_t *base, size_t blen, const uint8_t *exp, size_t elen,
              const uint8_t *mod, size_t mlen, uint8_t *out)
{
    memset(out, 0, mlen);
    uint64_t *m = NULL;
    size_t mn = to_limbs(mod, mlen, &m);
    if (!m) return -1;
    size_t mtrim = trim(m, mn);
    if (mtrim == 0) { free(m); return 0; }          /* modulus 0 -> result 0 */

    uint64_t *b = NULL;
    size_t bn = to_limbs(base, blen, &b);
    if (!b) { free(m); return -1; }
    size_t n = mn;
    uint64_t *cur = (uint64_t *)calloc(n, sizeof(uint64_t));
    uint64_t *acc = (uint64_t *)calloc(n, sizeof(uint64_t));
    uint64_t *tmp = (uint64_t *)calloc(n, sizeof(uint64_t));
    uint64_t *scratch = (uint64_t *)calloc(2 * n + 2, sizeof(uint64_t));
    /* the declared lengths run to the gigabytes; an input that large is never payable, so a
     * failed allocation is a failed call rather than something to dereference */
    if (!cur || !acc || !tmp || !scratch) {
        free(m); free(b); free(cur); free(acc); free(tmp); free(scratch);
        return -1;
    }

    /* cur = base mod m */
    {
        size_t bnn = bn > n ? bn : n;
        uint64_t *bx = (uint64_t *)calloc(bnn, sizeof(uint64_t));
        if (!bx) { free(m); free(b); free(cur); free(acc); free(tmp); free(scratch); return -1; }
        memcpy(bx, b, bn * sizeof(uint64_t));
        mp_divmod(bx, bnn, m, mn, NULL, cur);
        free(bx);
    }
    /* acc = 1 mod m */
    {
        uint64_t *ox = (uint64_t *)calloc(n, sizeof(uint64_t));
        if (!ox) { free(m); free(b); free(cur); free(acc); free(tmp); free(scratch); return -1; }
        ox[0] = 1;
        mp_divmod(ox, n, m, mn, NULL, acc);
        free(ox);
    }

    /* scan exponent bits, most significant first */
    size_t ebits = elen * 8;
    size_t first = 0;
    while (first < ebits) {
        size_t byte = first / 8, bit = 7 - (first % 8);
        if ((exp[byte] >> bit) & 1) break;
        first++;
    }
    for (size_t i = first; i < ebits; i++) {
        mul_mod(acc, acc, n, m, mn, tmp, scratch);
        memcpy(acc, tmp, n * sizeof(uint64_t));
        size_t byte = i / 8, bit = 7 - (i % 8);
        if ((exp[byte] >> bit) & 1) {
            mul_mod(acc, cur, n, m, mn, tmp, scratch);
            memcpy(acc, tmp, n * sizeof(uint64_t));
        }
    }

    for (size_t i = 0; i < mlen; i++) {
        size_t li = i / 8;
        size_t sh = 8 * (i % 8);
        uint8_t byte = (li < n) ? (uint8_t)(acc[li] >> sh) : 0;
        out[mlen - 1 - i] = byte;
    }
    free(m); free(b); free(cur); free(acc); free(tmp); free(scratch);
    return 0;
}

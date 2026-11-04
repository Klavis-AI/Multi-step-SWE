/* 256-bit unsigned integers, little-endian limb order (v[0] least significant). */
#ifndef U256_H
#define U256_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

typedef struct { uint64_t v[4]; } u256;

static const u256 U256_ZERO = {{0, 0, 0, 0}};
static const u256 U256_ONE = {{1, 0, 0, 0}};

static inline u256 u256_from_u64(uint64_t x) { u256 r = {{x, 0, 0, 0}}; return r; }
static inline bool u256_is_zero(u256 a) { return (a.v[0] | a.v[1] | a.v[2] | a.v[3]) == 0; }
static inline bool u256_eq(u256 a, u256 b) { return memcmp(a.v, b.v, 32) == 0; }
static inline bool u256_fits_u64(u256 a) { return (a.v[1] | a.v[2] | a.v[3]) == 0; }

/* big-endian conversion */
void u256_from_be(u256 *r, const uint8_t *b, size_t len); /* len <= 32, right-aligned */
void u256_to_be(const u256 *a, uint8_t out[32]);

int u256_cmp(u256 a, u256 b);        /* unsigned  -1/0/1 */
int u256_scmp(u256 a, u256 b);       /* two's complement signed */

u256 u256_add(u256 a, u256 b);
u256 u256_sub(u256 a, u256 b);
u256 u256_mul(u256 a, u256 b);
u256 u256_div(u256 a, u256 b);       /* b==0 -> 0 */
u256 u256_mod(u256 a, u256 b);       /* b==0 -> 0 */
u256 u256_sdiv(u256 a, u256 b);
u256 u256_smod(u256 a, u256 b);
u256 u256_addmod(u256 a, u256 b, u256 m);
u256 u256_mulmod(u256 a, u256 b, u256 m);
u256 u256_exp(u256 base, u256 e);

u256 u256_and(u256 a, u256 b);
u256 u256_or(u256 a, u256 b);
u256 u256_xor(u256 a, u256 b);
u256 u256_not(u256 a);
u256 u256_shl(u256 shift, u256 val);
u256 u256_shr(u256 shift, u256 val);
u256 u256_sar(u256 shift, u256 val);
u256 u256_byte(u256 idx, u256 val);
u256 u256_signextend(u256 k, u256 val);

int u256_bitlen(u256 a);
int u256_bytelen(u256 a);

/* Saturating conversion: returns UINT64_MAX when the value does not fit. */
static inline uint64_t u256_to_u64_sat(u256 a)
{
    if (!u256_fits_u64(a)) return UINT64_MAX;
    return a.v[0];
}

/* Generic multi-precision helpers shared with the modexp precompile. */
void mp_divmod(const uint64_t *num, size_t nlen, const uint64_t *den, size_t dlen,
               uint64_t *quot, uint64_t *rem);

#endif

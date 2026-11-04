#ifndef CRYPTO_H
#define CRYPTO_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "types.h"

void sha256_hash(const uint8_t *in, size_t len, uint8_t out[32]);
void ripemd160_hash(const uint8_t *in, size_t len, uint8_t out[20]);
/* returns true and fills `out` (20-byte address) on success */
bool ecrecover_addr(const uint8_t hash[32], const uint8_t sig64[64], int recid, address *out);
/* EIP-152 BLAKE2b F compression; rounds may be large */
void blake2f(uint32_t rounds, uint64_t h[8], const uint64_t m[16], const uint64_t t[2], bool f);
/* secp256k1 signature -> sender address, with the fork's s-upper-bound rule */
bool secp_recover(const uint8_t msg32[32], const u256 *r, const u256 *s, int v, bool homestead, address *out);
#endif

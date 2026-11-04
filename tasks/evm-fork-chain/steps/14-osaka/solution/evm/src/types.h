#ifndef TYPES_H
#define TYPES_H
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "u256.h"

typedef struct { uint8_t b[20]; } address;
typedef struct { uint8_t b[32]; } hash256;

static inline bool addr_eq(address a, address b) { return memcmp(a.b, b.b, 20) == 0; }
static inline bool hash_eq(hash256 a, hash256 b) { return memcmp(a.b, b.b, 32) == 0; }
static inline address addr_zero(void) { address a; memset(a.b, 0, 20); return a; }
static inline hash256 hash_zero(void) { hash256 h; memset(h.b, 0, 32); return h; }
static inline address addr_from_u256(u256 v)
{
    uint8_t be[32]; address a;
    u256_to_be(&v, be);
    memcpy(a.b, be + 12, 20);
    return a;
}
static inline u256 u256_from_addr(address a)
{
    u256 r; u256_from_be(&r, a.b, 20); return r;
}
static inline u256 u256_from_hash(hash256 h) { u256 r; u256_from_be(&r, h.b, 32); return r; }
static inline hash256 hash_from_u256(u256 v) { hash256 h; u256_to_be(&v, h.b); return h; }
#endif

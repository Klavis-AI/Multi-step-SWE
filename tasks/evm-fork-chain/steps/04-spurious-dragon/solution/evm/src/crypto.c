#include "crypto.h"
#include "keccak.h"
#include <openssl/sha.h>
#include <openssl/ripemd.h>
#include <openssl/evp.h>
#include <secp256k1.h>
#include <secp256k1_recovery.h>
#include <string.h>
#include <stdlib.h>

void sha256_hash(const uint8_t *in, size_t len, uint8_t out[32])
{
    unsigned int olen = 32;
    EVP_MD_CTX *c = EVP_MD_CTX_new();
    EVP_DigestInit_ex(c, EVP_sha256(), NULL);
    EVP_DigestUpdate(c, in, len);
    EVP_DigestFinal_ex(c, out, &olen);
    EVP_MD_CTX_free(c);
}

void ripemd160_hash(const uint8_t *in, size_t len, uint8_t out[20])
{
    unsigned int olen = 20;
    EVP_MD_CTX *c = EVP_MD_CTX_new();
    const EVP_MD *md = EVP_ripemd160();
    EVP_DigestInit_ex(c, md, NULL);
    EVP_DigestUpdate(c, in, len);
    EVP_DigestFinal_ex(c, out, &olen);
    EVP_MD_CTX_free(c);
}

static secp256k1_context *ctx(void)
{
    static secp256k1_context *c = NULL;
    if (!c) c = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY);
    return c;
}

bool ecrecover_addr(const uint8_t hash[32], const uint8_t sig64[64], int recid, address *out)
{
    if (recid < 0 || recid > 3) return false;
    secp256k1_ecdsa_recoverable_signature sig;
    if (!secp256k1_ecdsa_recoverable_signature_parse_compact(ctx(), &sig, sig64, recid)) return false;
    secp256k1_pubkey pk;
    if (!secp256k1_ecdsa_recover(ctx(), &pk, &sig, hash)) return false;
    uint8_t ser[65];
    size_t slen = 65;
    secp256k1_ec_pubkey_serialize(ctx(), ser, &slen, &pk, SECP256K1_EC_UNCOMPRESSED);
    uint8_t h[32];
    keccak256(ser + 1, 64, h);
    memcpy(out->b, h + 12, 20);
    return true;
}

/* secp256k1 group order n and n/2 */
static const uint8_t SECP_N[32] = {
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xfe,
    0xba,0xae,0xdc,0xe6,0xaf,0x48,0xa0,0x3b,0xbf,0xd2,0x5e,0x8c,0xd0,0x36,0x41,0x41};
static const uint8_t SECP_HALF_N[32] = {
    0x7f,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0x5d,0x57,0x6e,0x73,0x57,0xa4,0x50,0x1d,0xdf,0xe9,0x2f,0x46,0x68,0x1b,0x20,0xa0};

bool secp_recover(const uint8_t msg32[32], const u256 *r, const u256 *s, int v, bool homestead, address *out)
{
    if (v != 0 && v != 1) return false;
    u256 n, half;
    u256_from_be(&n, SECP_N, 32);
    u256_from_be(&half, SECP_HALF_N, 32);
    if (u256_is_zero(*r) || u256_cmp(*r, n) >= 0) return false;
    if (u256_is_zero(*s) || u256_cmp(*s, n) >= 0) return false;
    if (homestead && u256_cmp(*s, half) > 0) return false;
    uint8_t sig[64];
    u256_to_be(r, sig);
    u256_to_be(s, sig + 32);
    return ecrecover_addr(msg32, sig, v, out);
}

/* ------------------------------------------------------------- BLAKE2b F */
static const uint8_t SIGMA[10][16] = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15},
    {14,10, 4, 8, 9,15,13, 6, 1,12, 0, 2,11, 7, 5, 3},
    {11, 8,12, 0, 5, 2,15,13,10,14, 3, 6, 7, 1, 9, 4},
    { 7, 9, 3, 1,13,12,11,14, 2, 6, 5,10, 4, 0,15, 8},
    { 9, 0, 5, 7, 2, 4,10,15,14, 1,11,12, 6, 8, 3,13},
    { 2,12, 6,10, 0,11, 8, 3, 4,13, 7, 5,15,14, 1, 9},
    {12, 5, 1,15,14,13, 4,10, 0, 7, 6, 3, 9, 2, 8,11},
    {13,11, 7,14,12, 1, 3, 9, 5, 0,15, 4, 8, 6, 2,10},
    { 6,15,14, 9,11, 3, 0, 8,12, 2,13, 7, 1, 4,10, 5},
    {10, 2, 8, 4, 7, 6, 1, 5,15,11, 9,14, 3,12,13, 0}
};
static const uint64_t IV[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
};
static inline uint64_t ror64(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }

void blake2f(uint32_t rounds, uint64_t h[8], const uint64_t m[16], const uint64_t t[2], bool f)
{
    uint64_t v[16];
    for (int i = 0; i < 8; i++) v[i] = h[i];
    for (int i = 0; i < 8; i++) v[8 + i] = IV[i];
    v[12] ^= t[0];
    v[13] ^= t[1];
    if (f) v[14] = ~v[14];
    for (uint32_t r = 0; r < rounds; r++) {
        const uint8_t *sg = SIGMA[r % 10];
#define G(a,b,c,d,x,y) do { \
    v[a] = v[a] + v[b] + (x); v[d] = ror64(v[d] ^ v[a], 32); \
    v[c] = v[c] + v[d];       v[b] = ror64(v[b] ^ v[c], 24); \
    v[a] = v[a] + v[b] + (y); v[d] = ror64(v[d] ^ v[a], 16); \
    v[c] = v[c] + v[d];       v[b] = ror64(v[b] ^ v[c], 63); } while (0)
        G(0, 4,  8, 12, m[sg[0]],  m[sg[1]]);
        G(1, 5,  9, 13, m[sg[2]],  m[sg[3]]);
        G(2, 6, 10, 14, m[sg[4]],  m[sg[5]]);
        G(3, 7, 11, 15, m[sg[6]],  m[sg[7]]);
        G(0, 5, 10, 15, m[sg[8]],  m[sg[9]]);
        G(1, 6, 11, 12, m[sg[10]], m[sg[11]]);
        G(2, 7,  8, 13, m[sg[12]], m[sg[13]]);
        G(3, 4,  9, 14, m[sg[14]], m[sg[15]]);
#undef G
    }
    for (int i = 0; i < 8; i++) h[i] ^= v[i] ^ v[i + 8];
}

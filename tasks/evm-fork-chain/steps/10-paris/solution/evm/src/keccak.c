/* Keccak-256 (original padding, not NIST SHA3). */
#include "keccak.h"

static const uint64_t RC[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL, 0x8000000080008000ULL,
    0x000000000000808bULL, 0x0000000080000001ULL, 0x8000000080008081ULL, 0x8000000000008009ULL,
    0x000000000000008aULL, 0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
    0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL, 0x8000000000008003ULL,
    0x8000000000008002ULL, 0x8000000000000080ULL, 0x000000000000800aULL, 0x800000008000000aULL,
    0x8000000080008081ULL, 0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL
};
static const int ROT[25] = {
     0,  1, 62, 28, 27,
    36, 44,  6, 55, 20,
     3, 10, 43, 25, 39,
    41, 45, 15, 21,  8,
    18,  2, 61, 56, 14
};

static inline uint64_t rol(uint64_t x, int n) { return n ? ((x << n) | (x >> (64 - n))) : x; }

static void keccakf(uint64_t st[25])
{
    for (int round = 0; round < 24; round++) {
        uint64_t bc[5], t, tmp[25];
        for (int i = 0; i < 5; i++)
            bc[i] = st[i] ^ st[i + 5] ^ st[i + 10] ^ st[i + 15] ^ st[i + 20];
        for (int i = 0; i < 5; i++) {
            t = bc[(i + 4) % 5] ^ rol(bc[(i + 1) % 5], 1);
            for (int j = 0; j < 25; j += 5) st[j + i] ^= t;
        }
        for (int i = 0; i < 25; i++) {
            int x = i % 5, y = i / 5;
            int nx = y, ny = (2 * x + 3 * y) % 5;
            tmp[ny * 5 + nx] = rol(st[i], ROT[i]);
        }
        memcpy(st, tmp, sizeof(tmp));
        for (int j = 0; j < 25; j += 5) {
            for (int i = 0; i < 5; i++) bc[i] = st[j + i];
            for (int i = 0; i < 5; i++)
                st[j + i] = bc[i] ^ ((~bc[(i + 1) % 5]) & bc[(i + 2) % 5]);
        }
        st[0] ^= RC[round];
    }
}

void keccak256(const uint8_t *in, size_t len, uint8_t out[32])
{
    uint64_t st[25];
    const size_t rate = 136;
    uint8_t block[136];
    memset(st, 0, sizeof(st));

    while (len >= rate) {
        for (size_t i = 0; i < rate / 8; i++) {
            uint64_t x = 0;
            for (int j = 7; j >= 0; j--) x = (x << 8) | in[i * 8 + j];
            st[i] ^= x;
        }
        keccakf(st);
        in += rate;
        len -= rate;
    }
    memset(block, 0, rate);
    if (len) memcpy(block, in, len);
    block[len] = 0x01;
    block[rate - 1] |= 0x80;
    for (size_t i = 0; i < rate / 8; i++) {
        uint64_t x = 0;
        for (int j = 7; j >= 0; j--) x = (x << 8) | block[i * 8 + j];
        st[i] ^= x;
    }
    keccakf(st);
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 8; j++) out[i * 8 + j] = (uint8_t)(st[i] >> (8 * j));
}

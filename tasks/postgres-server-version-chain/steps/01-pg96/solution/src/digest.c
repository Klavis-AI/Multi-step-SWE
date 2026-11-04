/* digest.c -- MD5, SHA-256, HMAC-SHA256, PBKDF2-HMAC-SHA256, base64, and
 * PostgreSQL password-verifier helpers (SCRAM-SHA-256 and md5).
 *
 * Implements the six functions declared in pgvc.h:
 *     md5_hex, sha256, hmac_sha256, pbkdf2_sha256, b64_encode, b64_decode
 * using only the C standard library -- no OpenSSL / libcrypto.
 *
 * These are used for (a) PostgreSQL's "md5" authentication method and its
 * md5() SQL function, and (b) SCRAM-SHA-256 authentication per RFC 5802
 * (SCRAM) / RFC 7677 (SCRAM-SHA-256), as PostgreSQL implements it.
 *
 * ---------------------------------------------------------------------
 * Additional SCRAM / password-verifier helpers
 *
 * These are declared in pgvc.h's "digests" section alongside the six
 * prototypes above; the declarations are reproduced here and the
 * definitions follow later in this file.
 *
 *     typedef struct {
 *         int            iterations;
 *         unsigned char *salt;        // malloc'd; free via scram_free_verifier()
 *         size_t         salt_len;
 *         unsigned char  stored_key[32];
 *         unsigned char  server_key[32];
 *     } ScramVerifier;
 *
 *     char *scram_build_verifier(const char *pass, size_t plen,
 *                                 const unsigned char *salt, size_t slen,
 *                                 int iters);
 *     bool  scram_parse_verifier(const char *verifier, ScramVerifier *out);
 *     void  scram_free_verifier(ScramVerifier *v);
 *
 *     void  scram_client_key(const unsigned char salted_password[32],
 *                             unsigned char out[32]);
 *     void  scram_stored_key(const unsigned char client_key[32],
 *                             unsigned char out[32]);
 *     void  scram_server_key(const unsigned char salted_password[32],
 *                             unsigned char out[32]);
 *     void  scram_client_signature(const unsigned char stored_key[32],
 *                                   const char *auth_message, size_t am_len,
 *                                   unsigned char out[32]);
 *     void  scram_client_proof(const unsigned char client_key[32],
 *                               const unsigned char client_signature[32],
 *                               unsigned char out[32]);
 *     void  scram_server_signature(const unsigned char server_key[32],
 *                                   const char *auth_message, size_t am_len,
 *                                   unsigned char out[32]);
 *
 *     char *md5_build_verifier(const char *pass, const char *user);
 *
 * LIMITATION -- SASLprep: RFC 5802 requires the password to be normalized
 * with SASLprep (RFC 4013) before it is fed into PBKDF2. This module
 * implements only the ASCII pass-through case: bytes are used exactly as
 * given, with no Unicode normalization (NFKC), no bidi checks, and no
 * prohibited-character screening. For ASCII-only passwords SASLprep is
 * the identity transform, so this is fully correct; it is also exactly
 * what real PostgreSQL does when SASLprep itself declines to touch an
 * input (see src/common/saslprep.c: on any error it falls back to the
 * original, unmodified password bytes). For passwords containing
 * non-ASCII characters that a conformant SASLprep would actually
 * normalize, this module's output will NOT match a fully RFC-4013
 * conformant implementation.
 * ---------------------------------------------------------------------
 */

#include "pgvc.h"

/* ================================================================== */
/* MD5 (RFC 1321)                                                      */
/* ================================================================== */

typedef struct {
    uint32_t      state[4];
    uint64_t      bitcount;      /* total message length, in bits */
    unsigned char buffer[64];
} Md5Ctx;

static const uint32_t MD5_K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
    0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
    0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
    0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
    0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
    0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
};

static const int MD5_S[64] = {
    7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,
    5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,
    4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,
    6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21
};

static uint32_t
md5_rotl(uint32_t x, int c)
{
    return (x << c) | (x >> (32 - c));
}

static void
md5_transform(uint32_t state[4], const unsigned char block[64])
{
    uint32_t M[16];
    uint32_t A = state[0], B = state[1], C = state[2], D = state[3];
    int i;

    for (i = 0; i < 16; i++)
        M[i] = (uint32_t) block[i * 4]
             | ((uint32_t) block[i * 4 + 1] << 8)
             | ((uint32_t) block[i * 4 + 2] << 16)
             | ((uint32_t) block[i * 4 + 3] << 24);

    for (i = 0; i < 64; i++) {
        uint32_t F;
        int      g;

        if (i < 16) {
            F = (B & C) | (~B & D);
            g = i;
        } else if (i < 32) {
            F = (D & B) | (~D & C);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            F = B ^ C ^ D;
            g = (3 * i + 5) % 16;
        } else {
            F = C ^ (B | ~D);
            g = (7 * i) % 16;
        }

        {
            uint32_t tmp = D;
            D = C;
            C = B;
            B = B + md5_rotl(A + F + MD5_K[i] + M[g], MD5_S[i]);
            A = tmp;
        }
    }

    state[0] += A;
    state[1] += B;
    state[2] += C;
    state[3] += D;
}

static void
md5_init(Md5Ctx *ctx)
{
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
    ctx->bitcount = 0;
}

static void
md5_update(Md5Ctx *ctx, const unsigned char *data, size_t len)
{
    size_t have = (size_t) ((ctx->bitcount >> 3) & 0x3F);
    size_t need;
    size_t i = 0;

    ctx->bitcount += (uint64_t) len << 3;
    need = 64 - have;

    if (len >= need) {
        memcpy(ctx->buffer + have, data, need);
        md5_transform(ctx->state, ctx->buffer);
        for (i = need; i + 64 <= len; i += 64)
            md5_transform(ctx->state, data + i);
        have = 0;
    }
    memcpy(ctx->buffer + have, data + i, len - i);
}

static void
md5_final(Md5Ctx *ctx, unsigned char digest[16])
{
    unsigned char pad[64] = { 0x80 };
    unsigned char lenbuf[8];
    uint64_t      bitcount = ctx->bitcount;
    size_t        have, padlen;
    int           i;

    for (i = 0; i < 8; i++)
        lenbuf[i] = (unsigned char) (bitcount >> (8 * i));   /* little-endian */

    have = (size_t) ((ctx->bitcount >> 3) & 0x3F);
    padlen = (have < 56) ? (56 - have) : (120 - have);
    md5_update(ctx, pad, padlen);
    md5_update(ctx, lenbuf, 8);

    for (i = 0; i < 4; i++) {
        digest[i * 4 + 0] = (unsigned char) (ctx->state[i]);
        digest[i * 4 + 1] = (unsigned char) (ctx->state[i] >> 8);
        digest[i * 4 + 2] = (unsigned char) (ctx->state[i] >> 16);
        digest[i * 4 + 3] = (unsigned char) (ctx->state[i] >> 24);
    }
}

void
md5_hex(const unsigned char *in, size_t n, char out[33])
{
    static const char hexd[] = "0123456789abcdef";
    Md5Ctx        ctx;
    unsigned char digest[16];
    int           i;

    md5_init(&ctx);
    md5_update(&ctx, in, n);
    md5_final(&ctx, digest);

    for (i = 0; i < 16; i++) {
        out[i * 2]     = hexd[(digest[i] >> 4) & 0xF];
        out[i * 2 + 1] = hexd[digest[i] & 0xF];
    }
    out[32] = '\0';
}

/* ================================================================== */
/* SHA-256 (FIPS 180-4)                                                */
/* ================================================================== */

typedef struct {
    uint32_t      state[8];
    uint64_t      bitcount;
    unsigned char buffer[64];
} Sha256Ctx;

static const uint32_t SHA256_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static uint32_t
sha256_rotr(uint32_t x, int n)
{
    return (x >> n) | (x << (32 - n));
}

static void
sha256_transform(uint32_t state[8], const unsigned char block[64])
{
    uint32_t w[64];
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    int      i;

    for (i = 0; i < 16; i++)
        w[i] = ((uint32_t) block[i * 4] << 24)
             | ((uint32_t) block[i * 4 + 1] << 16)
             | ((uint32_t) block[i * 4 + 2] << 8)
             | ((uint32_t) block[i * 4 + 3]);

    for (i = 16; i < 64; i++) {
        uint32_t s0 = sha256_rotr(w[i - 15], 7) ^ sha256_rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = sha256_rotr(w[i - 2], 17) ^ sha256_rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    for (i = 0; i < 64; i++) {
        uint32_t S1    = sha256_rotr(e, 6) ^ sha256_rotr(e, 11) ^ sha256_rotr(e, 25);
        uint32_t ch    = (e & f) ^ (~e & g);
        uint32_t temp1 = h + S1 + ch + SHA256_K[i] + w[i];
        uint32_t S0    = sha256_rotr(a, 2) ^ sha256_rotr(a, 13) ^ sha256_rotr(a, 22);
        uint32_t maj   = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;

        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

static void
sha256_init(Sha256Ctx *ctx)
{
    static const uint32_t H0[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    memcpy(ctx->state, H0, sizeof H0);
    ctx->bitcount = 0;
}

static void
sha256_update(Sha256Ctx *ctx, const unsigned char *data, size_t len)
{
    size_t have = (size_t) ((ctx->bitcount >> 3) & 0x3F);
    size_t need;
    size_t i = 0;

    ctx->bitcount += (uint64_t) len << 3;
    need = 64 - have;

    if (len >= need) {
        memcpy(ctx->buffer + have, data, need);
        sha256_transform(ctx->state, ctx->buffer);
        for (i = need; i + 64 <= len; i += 64)
            sha256_transform(ctx->state, data + i);
        have = 0;
    }
    memcpy(ctx->buffer + have, data + i, len - i);
}

static void
sha256_final(Sha256Ctx *ctx, unsigned char digest[32])
{
    unsigned char pad[64] = { 0x80 };
    unsigned char lenbuf[8];
    uint64_t      bitcount = ctx->bitcount;
    size_t        have, padlen;
    int           i;

    for (i = 0; i < 8; i++)
        lenbuf[7 - i] = (unsigned char) (bitcount >> (8 * i));   /* big-endian */

    have = (size_t) ((ctx->bitcount >> 3) & 0x3F);
    padlen = (have < 56) ? (56 - have) : (120 - have);
    sha256_update(ctx, pad, padlen);
    sha256_update(ctx, lenbuf, 8);

    for (i = 0; i < 8; i++) {
        digest[i * 4 + 0] = (unsigned char) (ctx->state[i] >> 24);
        digest[i * 4 + 1] = (unsigned char) (ctx->state[i] >> 16);
        digest[i * 4 + 2] = (unsigned char) (ctx->state[i] >> 8);
        digest[i * 4 + 3] = (unsigned char) (ctx->state[i]);
    }
}

void
sha256(const unsigned char *in, size_t n, unsigned char out[32])
{
    Sha256Ctx ctx;

    sha256_init(&ctx);
    sha256_update(&ctx, in, n);
    sha256_final(&ctx, out);
}

/* ================================================================== */
/* SHA-224 (FIPS 180-4): the SHA-256 core with a different initial     */
/* state, truncated to 28 bytes.                                       */
/* ================================================================== */
void
sha224(const unsigned char *in, size_t n, unsigned char out[28])
{
    static const uint32_t H0[8] = {
        0xc1059ed8, 0x367cd507, 0x3070dd17, 0xf70e5939,
        0xffc00b31, 0x68581511, 0x64f98fa7, 0xbefa4fa4
    };
    Sha256Ctx ctx;
    unsigned char full[32];

    sha256_init(&ctx);
    memcpy(ctx.state, H0, sizeof H0);
    sha256_update(&ctx, in, n);
    sha256_final(&ctx, full);
    memcpy(out, full, 28);
}

/* ================================================================== */
/* SHA-512 and SHA-384 (FIPS 180-4)                                    */
/* ================================================================== */
typedef struct {
    uint64_t      state[8];
    uint64_t      bitcount;          /* low 64 bits are enough here */
    unsigned char buffer[128];
} Sha512Ctx;

static const uint64_t SHA512_K[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};

static uint64_t
sha512_rotr(uint64_t x, int n)
{
    return (x >> n) | (x << (64 - n));
}

static void
sha512_transform(uint64_t state[8], const unsigned char block[128])
{
    uint64_t w[80];
    uint64_t a, b, c, d, e, f, g, h;
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = 0;
        for (int k = 0; k < 8; k++)
            w[i] = (w[i] << 8) | block[i * 8 + k];
    }
    for (i = 16; i < 80; i++) {
        uint64_t s0 = sha512_rotr(w[i-15], 1) ^ sha512_rotr(w[i-15], 8) ^ (w[i-15] >> 7);
        uint64_t s1 = sha512_rotr(w[i-2], 19) ^ sha512_rotr(w[i-2], 61) ^ (w[i-2] >> 6);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];
    for (i = 0; i < 80; i++) {
        uint64_t S1    = sha512_rotr(e, 14) ^ sha512_rotr(e, 18) ^ sha512_rotr(e, 41);
        uint64_t ch    = (e & f) ^ ((~e) & g);
        uint64_t temp1 = h + S1 + ch + SHA512_K[i] + w[i];
        uint64_t S0    = sha512_rotr(a, 28) ^ sha512_rotr(a, 34) ^ sha512_rotr(a, 39);
        uint64_t maj   = (a & b) ^ (a & c) ^ (b & c);
        uint64_t temp2 = S0 + maj;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

static void
sha512_run(const unsigned char *in, size_t n, const uint64_t H0[8], unsigned char out[64])
{
    Sha512Ctx ctx;
    size_t i;

    memcpy(ctx.state, H0, 8 * sizeof(uint64_t));
    ctx.bitcount = (uint64_t) n << 3;

    for (i = 0; i + 128 <= n; i += 128)
        sha512_transform(ctx.state, in + i);

    unsigned char tail[256];
    size_t rem = n - i;
    memset(tail, 0, sizeof tail);
    memcpy(tail, in + i, rem);
    tail[rem] = 0x80;
    size_t total = (rem < 112) ? 128 : 256;
    for (int k = 0; k < 8; k++)
        tail[total - 1 - k] = (unsigned char) (ctx.bitcount >> (8 * k));
    for (size_t off = 0; off < total; off += 128)
        sha512_transform(ctx.state, tail + off);

    for (int k = 0; k < 8; k++)
        for (int j = 0; j < 8; j++)
            out[k * 8 + j] = (unsigned char) (ctx.state[k] >> (56 - 8 * j));
}

void
sha512(const unsigned char *in, size_t n, unsigned char out[64])
{
    static const uint64_t H0[8] = {
        0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
        0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
    };
    sha512_run(in, n, H0, out);
}

void
sha384(const unsigned char *in, size_t n, unsigned char out[48])
{
    static const uint64_t H0[8] = {
        0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL, 0x9159015a3070dd17ULL, 0x152fecd8f70e5939ULL,
        0x67332667ffc00b31ULL, 0x8eb44a8768581511ULL, 0xdb0c2e0d64f98fa7ULL, 0x47b5481dbefa4fa4ULL
    };
    unsigned char full[64];
    sha512_run(in, n, H0, full);
    memcpy(out, full, 48);
}

/* ================================================================== */
/* HMAC-SHA256 (RFC 2104 / FIPS 198-1)                                 */
/* ================================================================== */

void
hmac_sha256(const unsigned char *key, size_t klen,
            const unsigned char *msg, size_t mlen, unsigned char out[32])
{
    unsigned char k0[64];
    unsigned char keydigest[32];
    unsigned char ipad[64], opad[64];
    unsigned char inner_hash[32];
    size_t        i;
    Sha256Ctx     ctx;

    if (klen > 64) {
        sha256(key, klen, keydigest);
        key = keydigest;
        klen = 32;
    }

    memset(k0, 0, sizeof k0);
    memcpy(k0, key, klen);

    for (i = 0; i < 64; i++) {
        ipad[i] = (unsigned char) (k0[i] ^ 0x36);
        opad[i] = (unsigned char) (k0[i] ^ 0x5c);
    }

    sha256_init(&ctx);
    sha256_update(&ctx, ipad, 64);
    sha256_update(&ctx, msg, mlen);
    sha256_final(&ctx, inner_hash);

    sha256_init(&ctx);
    sha256_update(&ctx, opad, 64);
    sha256_update(&ctx, inner_hash, 32);
    sha256_final(&ctx, out);
}

/* ================================================================== */
/* PBKDF2-HMAC-SHA256 (RFC 8018)                                       */
/* ================================================================== */

void
pbkdf2_sha256(const char *pass, size_t plen, const unsigned char *salt,
              size_t slen, int iters, unsigned char *out, size_t outlen)
{
    const size_t   hlen = 32;
    uint32_t       block_index = 1;
    unsigned char *saltbuf;
    size_t         produced = 0;

    if (iters < 1)
        iters = 1;

    saltbuf = (unsigned char *) malloc(slen + 4);
    if (saltbuf == NULL)
        return;                    /* nothing sane to do without memory */
    memcpy(saltbuf, salt, slen);

    while (produced < outlen) {
        unsigned char U[32];
        unsigned char T[32];
        int           j;
        size_t        take;

        saltbuf[slen + 0] = (unsigned char) (block_index >> 24);
        saltbuf[slen + 1] = (unsigned char) (block_index >> 16);
        saltbuf[slen + 2] = (unsigned char) (block_index >> 8);
        saltbuf[slen + 3] = (unsigned char) (block_index);

        hmac_sha256((const unsigned char *) pass, plen, saltbuf, slen + 4, U);
        memcpy(T, U, hlen);

        for (j = 1; j < iters; j++) {
            size_t k;

            hmac_sha256((const unsigned char *) pass, plen, U, hlen, U);
            for (k = 0; k < hlen; k++)
                T[k] ^= U[k];
        }

        take = outlen - produced;
        if (take > hlen)
            take = hlen;
        memcpy(out + produced, T, take);
        produced += take;
        block_index++;
    }

    free(saltbuf);
}

/* ================================================================== */
/* base64 (RFC 4648 sec. 4, standard alphabet, '=' padding)            */
/* ================================================================== */

static const char B64_ALPHABET[64] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t
b64_encode(const unsigned char *in, size_t n, char *out)
{
    size_t i, o = 0, rem;

    for (i = 0; i + 3 <= n; i += 3) {
        uint32_t v = ((uint32_t) in[i] << 16) | ((uint32_t) in[i + 1] << 8) | in[i + 2];

        out[o++] = B64_ALPHABET[(v >> 18) & 0x3F];
        out[o++] = B64_ALPHABET[(v >> 12) & 0x3F];
        out[o++] = B64_ALPHABET[(v >> 6) & 0x3F];
        out[o++] = B64_ALPHABET[v & 0x3F];
    }

    rem = n - i;
    if (rem == 1) {
        uint32_t v = (uint32_t) in[i] << 16;

        out[o++] = B64_ALPHABET[(v >> 18) & 0x3F];
        out[o++] = B64_ALPHABET[(v >> 12) & 0x3F];
        out[o++] = '=';
        out[o++] = '=';
    } else if (rem == 2) {
        uint32_t v = ((uint32_t) in[i] << 16) | ((uint32_t) in[i + 1] << 8);

        out[o++] = B64_ALPHABET[(v >> 18) & 0x3F];
        out[o++] = B64_ALPHABET[(v >> 12) & 0x3F];
        out[o++] = B64_ALPHABET[(v >> 6) & 0x3F];
        out[o++] = '=';
    }

    out[o] = '\0';
    return o;
}

/* -1 for any character outside the base64 alphabet (padding handled by caller) */
static int
b64_val(int c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

int
b64_decode(const char *in, size_t n, unsigned char *out)
{
    size_t i, o = 0;

    if (n % 4 != 0)
        return -1;

    for (i = 0; i < n; i += 4) {
        int c0 = (unsigned char) in[i];
        int c1 = (unsigned char) in[i + 1];
        int c2 = (unsigned char) in[i + 2];
        int c3 = (unsigned char) in[i + 3];
        int pad = 0;
        int v0, v1, v2, v3;
        uint32_t v;

        if (c2 == '=') {
            if (c3 != '=' || i + 4 != n)
                return -1;
            pad = 2;
        } else if (c3 == '=') {
            if (i + 4 != n)
                return -1;
            pad = 1;
        }

        v0 = b64_val(c0);
        v1 = b64_val(c1);
        v2 = (pad == 2) ? 0 : b64_val(c2);
        v3 = (pad >= 1) ? 0 : b64_val(c3);

        if (v0 < 0 || v1 < 0 || (pad < 2 && v2 < 0) || (pad < 1 && v3 < 0))
            return -1;

        v = ((uint32_t) v0 << 18) | ((uint32_t) v1 << 12) | ((uint32_t) v2 << 6) | (uint32_t) v3;

        out[o++] = (unsigned char) (v >> 16);
        if (pad < 2)
            out[o++] = (unsigned char) (v >> 8);
        if (pad < 1)
            out[o++] = (unsigned char) v;
    }

    return (int) o;
}

/* ================================================================== */
/* PostgreSQL SCRAM-SHA-256 verifier helpers (RFC 5802 / RFC 7677)     */
/* ================================================================== */

/* ScramVerifier is declared in pgvc.h */

static const char SCRAM_SHA256_PREFIX[] = "SCRAM-SHA-256$";

/* SASLprep (RFC 4013) pass-through -- see the LIMITATION note at the top
 * of this file. Correct for ASCII-only passwords. */
static void
saslprep_passthrough(const char *pass, size_t plen, const char **out, size_t *outlen)
{
    *out = pass;
    *outlen = plen;
}

/* Build "SCRAM-SHA-256$<iters>:<salt_b64>$<StoredKey_b64>:<ServerKey_b64>"
 * exactly as PostgreSQL's password_encryption='scram-sha-256' does.
 * Returns a malloc'd, NUL-terminated string, or NULL on error. */
char *
scram_build_verifier(const char *pass, size_t plen,
                      const unsigned char *salt, size_t slen, int iters)
{
    const char    *p;
    size_t         p_len;
    unsigned char  salted_password[32];
    unsigned char  client_key[32];
    unsigned char  stored_key[32];
    unsigned char  server_key[32];
    char          *salt_b64;
    size_t         salt_b64_cap;
    char           stored_b64[45];   /* b64 of 32 bytes = 44 chars + NUL */
    char           server_b64[45];
    int            needed;
    char          *verifier;

    if (iters < 1 || salt == NULL)
        return NULL;

    saslprep_passthrough(pass, plen, &p, &p_len);

    pbkdf2_sha256(p, p_len, salt, slen, iters, salted_password, sizeof salted_password);

    hmac_sha256(salted_password, sizeof salted_password,
                (const unsigned char *) "Client Key", strlen("Client Key"), client_key);
    sha256(client_key, sizeof client_key, stored_key);
    hmac_sha256(salted_password, sizeof salted_password,
                (const unsigned char *) "Server Key", strlen("Server Key"), server_key);

    salt_b64_cap = 4 * ((slen + 2) / 3) + 1;
    salt_b64 = (char *) malloc(salt_b64_cap);
    if (salt_b64 == NULL)
        return NULL;
    b64_encode(salt, slen, salt_b64);

    b64_encode(stored_key, sizeof stored_key, stored_b64);
    b64_encode(server_key, sizeof server_key, server_b64);

    needed = snprintf(NULL, 0, "%s%d:%s$%s:%s",
                       SCRAM_SHA256_PREFIX, iters, salt_b64, stored_b64, server_b64);
    if (needed < 0) {
        free(salt_b64);
        return NULL;
    }

    verifier = (char *) malloc((size_t) needed + 1);
    if (verifier == NULL) {
        free(salt_b64);
        return NULL;
    }
    snprintf(verifier, (size_t) needed + 1, "%s%d:%s$%s:%s",
             SCRAM_SHA256_PREFIX, iters, salt_b64, stored_b64, server_b64);

    free(salt_b64);
    return verifier;
}

/* Parse "SCRAM-SHA-256$<iters>:<salt_b64>$<StoredKey_b64>:<ServerKey_b64>".
 * On success *out is filled in (out->salt is malloc'd; free with
 * scram_free_verifier()) and true is returned; on any malformed input,
 * false is returned and *out is zeroed. */
bool
scram_parse_verifier(const char *verifier, ScramVerifier *out)
{
    size_t         prefix_len;
    const char    *p, *colon1, *dollar, *colon2;
    char           itersbuf[32];
    char          *endptr;
    long           iters;
    size_t         salt_b64_len, stored_b64_len, server_b64_len;
    unsigned char *salt_buf;
    unsigned char  stored_buf[32], server_buf[32];
    int            dec;

    if (out != NULL)
        memset(out, 0, sizeof *out);
    if (verifier == NULL || out == NULL)
        return false;

    prefix_len = strlen(SCRAM_SHA256_PREFIX);
    if (strncmp(verifier, SCRAM_SHA256_PREFIX, prefix_len) != 0)
        return false;
    p = verifier + prefix_len;

    colon1 = strchr(p, ':');
    if (colon1 == NULL || (size_t) (colon1 - p) >= sizeof itersbuf)
        return false;
    memcpy(itersbuf, p, (size_t) (colon1 - p));
    itersbuf[colon1 - p] = '\0';

    errno = 0;
    iters = strtol(itersbuf, &endptr, 10);
    if (itersbuf[0] == '\0' || *endptr != '\0' || errno != 0 || iters <= 0 || iters > INT32_MAX)
        return false;

    dollar = strchr(colon1 + 1, '$');
    if (dollar == NULL)
        return false;
    salt_b64_len = (size_t) (dollar - (colon1 + 1));

    colon2 = strchr(dollar + 1, ':');
    if (colon2 == NULL)
        return false;
    stored_b64_len = (size_t) (colon2 - (dollar + 1));
    server_b64_len = strlen(colon2 + 1);

    if (salt_b64_len == 0 || stored_b64_len != 44 || server_b64_len != 44)
        return false;

    salt_buf = (unsigned char *) malloc(salt_b64_len);   /* decoded <= encoded length */
    if (salt_buf == NULL)
        return false;

    dec = b64_decode(colon1 + 1, salt_b64_len, salt_buf);
    if (dec < 0) {
        free(salt_buf);
        return false;
    }
    if (b64_decode(dollar + 1, stored_b64_len, stored_buf) != 32) {
        free(salt_buf);
        return false;
    }
    if (b64_decode(colon2 + 1, server_b64_len, server_buf) != 32) {
        free(salt_buf);
        return false;
    }

    out->iterations = (int) iters;
    out->salt = salt_buf;
    out->salt_len = (size_t) dec;
    memcpy(out->stored_key, stored_buf, 32);
    memcpy(out->server_key, server_buf, 32);
    return true;
}

void
scram_free_verifier(ScramVerifier *v)
{
    if (v != NULL && v->salt != NULL) {
        free(v->salt);
        v->salt = NULL;
        v->salt_len = 0;
    }
}

/* ClientKey = HMAC(SaltedPassword, "Client Key") */
void
scram_client_key(const unsigned char salted_password[32], unsigned char out[32])
{
    hmac_sha256(salted_password, 32,
                (const unsigned char *) "Client Key", strlen("Client Key"), out);
}

/* StoredKey = H(ClientKey) */
void
scram_stored_key(const unsigned char client_key[32], unsigned char out[32])
{
    sha256(client_key, 32, out);
}

/* ServerKey = HMAC(SaltedPassword, "Server Key") */
void
scram_server_key(const unsigned char salted_password[32], unsigned char out[32])
{
    hmac_sha256(salted_password, 32,
                (const unsigned char *) "Server Key", strlen("Server Key"), out);
}

/* ClientSignature = HMAC(StoredKey, AuthMessage) */
void
scram_client_signature(const unsigned char stored_key[32],
                        const char *auth_message, size_t am_len, unsigned char out[32])
{
    hmac_sha256(stored_key, 32, (const unsigned char *) auth_message, am_len, out);
}

/* ClientProof = ClientKey XOR ClientSignature */
void
scram_client_proof(const unsigned char client_key[32],
                    const unsigned char client_signature[32], unsigned char out[32])
{
    int i;

    for (i = 0; i < 32; i++)
        out[i] = (unsigned char) (client_key[i] ^ client_signature[i]);
}

/* ServerSignature = HMAC(ServerKey, AuthMessage) */
void
scram_server_signature(const unsigned char server_key[32],
                        const char *auth_message, size_t am_len, unsigned char out[32])
{
    hmac_sha256(server_key, 32, (const unsigned char *) auth_message, am_len, out);
}

/* ================================================================== */
/* PostgreSQL md5-method password verifier                             */
/* ================================================================== */

/* Build "md5" || md5_hex(password || username), exactly as PostgreSQL's
 * password_encryption='md5' does (see CREATE/ALTER ROLE ... PASSWORD).
 * Returns a malloc'd, NUL-terminated string of length 35, or NULL on
 * allocation failure. */
char *
md5_build_verifier(const char *pass, const char *user)
{
    size_t         plen = strlen(pass);
    size_t         ulen = strlen(user);
    unsigned char *buf;
    char           hex[33];
    char          *out;

    buf = (unsigned char *) malloc(plen + ulen);
    if (buf == NULL && plen + ulen != 0)
        return NULL;
    if (plen > 0)
        memcpy(buf, pass, plen);
    if (ulen > 0)
        memcpy(buf + plen, user, ulen);

    md5_hex(buf, plen + ulen, hex);
    free(buf);

    out = (char *) malloc(3 + 32 + 1);
    if (out == NULL)
        return NULL;
    memcpy(out, "md5", 3);
    memcpy(out + 3, hex, 32);
    out[35] = '\0';
    return out;
}

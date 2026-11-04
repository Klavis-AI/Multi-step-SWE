/* crypt.c -- the /Encrypt dictionary and the standard security handler.
 */
#include "pdfint.h"

/* ---------------------------------------------------------------------------
 * PDF 1.6/1.7 -- the standard security handler, ISO 32000-1 clause 7.6.
 *
 * Implemented: /V 1, 2 and 4, /R 2, 3 and 4, RC4 and AES-128-CBC,
 * the file key (Algorithm 2), /O and /U (Algorithms 3 to 5), authentication
 * with either password (Algorithms 6 and 7), the per-object key (Algorithm 1)
 * and the /CF crypt filters that choose between the two ciphers.
 *
 * The three primitives -- MD5, RC4 and AES -- are written here from their
 * published specifications (RFC 1321 and FIPS-197) rather than taken from a
 * library, because the workspace may not link one.
 * ------------------------------------------------------------------------ */

#define CFM_NONE     0      /* /Identity, or no decryption at this layer */
#define CFM_RC4      1      /* /V2                                       */
#define CFM_AESV2    2      /* /AESV2: AES-128 in CBC mode               */
#define CFM_UNKNOWN  3      /* a /CFM this implementation does not know  */
#define CFM_AESV3    4      /* /AESV3: AES-256 in CBC mode (PDF 2.0)     */

/* 7.6.3.3, the padding string of Algorithm 2. */
static const unsigned char crypt_pad[32] = {
    0x28, 0xBF, 0x4E, 0x5E, 0x4E, 0x75, 0x8A, 0x41,
    0x64, 0x00, 0x4E, 0x56, 0xFF, 0xFA, 0x01, 0x08,
    0x2E, 0x2E, 0x00, 0xB6, 0xD0, 0x68, 0x3E, 0x80,
    0x2F, 0x0C, 0xA9, 0xFE, 0x64, 0x53, 0x69, 0x7A
};

/* ------------------------------------------------------------------- MD5 */
/* RFC 1321.  Only ever fed a few hundred bytes at a time here, so the
 * buffering is the simple one: absorb into a 64-byte block, transform when
 * full. */

typedef struct {
    unsigned int  h[4];
    unsigned long long bits;
    unsigned char buf[64];
    size_t        n;
} md5_ctx;

static unsigned int md5_rol(unsigned int x, int c)
{
    return (x << c) | (x >> (32 - c));
}

static const unsigned char md5_shift[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
};

/* K[i] = floor(2^32 * |sin(i+1)|), computed once rather than tabulated. */
/* K[i] = floor(2^32 * |sin(i+1)|).  Tabulated rather than computed, because
 * deriving them through sin() would make the digest depend on libm rounding. */
static unsigned int md5_k(int i)
{
    static const unsigned int t[64] = {
        0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu,
        0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
        0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu,
        0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
        0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau,
        0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
        0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu,
        0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
        0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu,
        0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
        0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u,
        0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
        0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u,
        0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
        0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,
        0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u
    };
    return t[i];
}

static void md5_block(md5_ctx *c, const unsigned char *p)
{
    unsigned int m[16], a, b, cc, d, f, tmp;
    int i, g;

    for (i = 0; i < 16; i++)
        m[i] = (unsigned int)p[i * 4] | ((unsigned int)p[i * 4 + 1] << 8)
             | ((unsigned int)p[i * 4 + 2] << 16)
             | ((unsigned int)p[i * 4 + 3] << 24);

    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3];
    for (i = 0; i < 64; i++) {
        if (i < 16)      { f = (b & cc) | (~b & d);      g = i; }
        else if (i < 32) { f = (d & b) | (~d & cc);      g = (5 * i + 1) & 15; }
        else if (i < 48) { f = b ^ cc ^ d;               g = (3 * i + 5) & 15; }
        else             { f = cc ^ (b | ~d);            g = (7 * i) & 15; }
        tmp = d;
        d = cc;
        cc = b;
        b = b + md5_rol(a + f + md5_k(i) + m[g], md5_shift[i]);
        a = tmp;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
}

static void md5_init(md5_ctx *c)
{
    c->h[0] = 0x67452301u; c->h[1] = 0xefcdab89u;
    c->h[2] = 0x98badcfeu; c->h[3] = 0x10325476u;
    c->bits = 0;
    c->n = 0;
}

static void md5_update(md5_ctx *c, const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;

    c->bits += (unsigned long long)len * 8u;
    while (len > 0) {
        size_t take = 64 - c->n;

        if (take > len) take = len;
        memcpy(c->buf + c->n, p, take);
        c->n += take;
        p += take;
        len -= take;
        if (c->n == 64) {
            md5_block(c, c->buf);
            c->n = 0;
        }
    }
}

static void md5_final(md5_ctx *c, unsigned char out[16])
{
    unsigned long long bits = c->bits;      /* before the padding is counted */
    unsigned char len[8];
    unsigned char pad = 0x80;
    int i;

    for (i = 0; i < 8; i++)
        len[i] = (unsigned char)((bits >> (8 * i)) & 0xff);
    md5_update(c, &pad, 1);
    while (c->n != 56) {
        unsigned char z = 0;
        md5_update(c, &z, 1);
    }
    md5_update(c, len, 8);
    for (i = 0; i < 4; i++) {
        out[i * 4]     = (unsigned char)(c->h[i] & 0xff);
        out[i * 4 + 1] = (unsigned char)((c->h[i] >> 8) & 0xff);
        out[i * 4 + 2] = (unsigned char)((c->h[i] >> 16) & 0xff);
        out[i * 4 + 3] = (unsigned char)((c->h[i] >> 24) & 0xff);
    }
}

static void md5_once(const unsigned char *p, size_t n, unsigned char out[16])
{
    md5_ctx c;

    md5_init(&c);
    md5_update(&c, p, n);
    md5_final(&c, out);
}

/* ------------------------------------------------------------------- RC4 */

/* In-place is not wanted anywhere here, and src == dst is safe regardless. */
static void rc4_crypt(const unsigned char *key, size_t keylen,
                      const unsigned char *src, size_t n, unsigned char *dst)
{
    unsigned char s[256];
    unsigned int i, j;
    size_t k;

    if (keylen == 0) {
        if (dst != src) memmove(dst, src, n);
        return;
    }
    for (i = 0; i < 256; i++)
        s[i] = (unsigned char)i;
    for (i = 0, j = 0; i < 256; i++) {
        unsigned char t;

        j = (j + s[i] + key[i % keylen]) & 0xff;
        t = s[i]; s[i] = s[j]; s[j] = t;
    }
    i = j = 0;
    for (k = 0; k < n; k++) {
        unsigned char t;

        i = (i + 1) & 0xff;
        j = (j + s[i]) & 0xff;
        t = s[i]; s[i] = s[j]; s[j] = t;
        dst[k] = (unsigned char)(src[k] ^ s[(s[i] + s[j]) & 0xff]);
    }
}

/* ------------------------------------------------------------------- AES */
/* FIPS-197, 128-bit keys only (that is all /AESV2 is).  Decryption only:
 * nothing this library does ever encrypts. */

static const unsigned char aes_sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const unsigned char aes_rcon[10] = {
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};

static unsigned char aes_inv_sbox_tab[256];
static int aes_inv_ready;

static void aes_make_inv(void)
{
    int i;

    if (aes_inv_ready) return;
    for (i = 0; i < 256; i++)
        aes_inv_sbox_tab[aes_sbox[i]] = (unsigned char)i;
    aes_inv_ready = 1;
}

/* Multiplication in GF(2^8) with the AES reduction polynomial. */
static unsigned char aes_mul(unsigned char a, unsigned char b)
{
    unsigned char r = 0;
    int i;

    for (i = 0; i < 8; i++) {
        if (b & 1) r = (unsigned char)(r ^ a);
        b = (unsigned char)(b >> 1);
        a = (unsigned char)((a & 0x80) ? ((a << 1) ^ 0x1b) : (a << 1));
    }
    return r;
}

/* 11 round keys of 16 bytes for a 128-bit key. */
static void aes_expand(const unsigned char key[16], unsigned char rk[176])
{
    int i;

    memcpy(rk, key, 16);
    for (i = 4; i < 44; i++) {
        unsigned char t[4];

        memcpy(t, rk + (i - 1) * 4, 4);
        if (i % 4 == 0) {
            unsigned char s = t[0];

            t[0] = (unsigned char)(aes_sbox[t[1]] ^ aes_rcon[i / 4 - 1]);
            t[1] = aes_sbox[t[2]];
            t[2] = aes_sbox[t[3]];
            t[3] = aes_sbox[s];
        }
        rk[i * 4]     = (unsigned char)(rk[(i - 4) * 4] ^ t[0]);
        rk[i * 4 + 1] = (unsigned char)(rk[(i - 4) * 4 + 1] ^ t[1]);
        rk[i * 4 + 2] = (unsigned char)(rk[(i - 4) * 4 + 2] ^ t[2]);
        rk[i * 4 + 3] = (unsigned char)(rk[(i - 4) * 4 + 3] ^ t[3]);
    }
}

static void aes_add_rk(unsigned char st[16], const unsigned char *rk)
{
    int i;

    for (i = 0; i < 16; i++)
        st[i] = (unsigned char)(st[i] ^ rk[i]);
}

/* State is column-major: st[r + 4*c]. */
static void aes_inv_shift_rows(unsigned char st[16])
{
    unsigned char t[16];
    int r, c;

    memcpy(t, st, 16);
    for (r = 1; r < 4; r++)
        for (c = 0; c < 4; c++)
            st[r + 4 * ((c + r) & 3)] = t[r + 4 * c];
}

static void aes_inv_sub_bytes(unsigned char st[16])
{
    int i;

    for (i = 0; i < 16; i++)
        st[i] = aes_inv_sbox_tab[st[i]];
}

static void aes_inv_mix_cols(unsigned char st[16])
{
    int c;

    for (c = 0; c < 4; c++) {
        unsigned char a0 = st[4 * c], a1 = st[4 * c + 1];
        unsigned char a2 = st[4 * c + 2], a3 = st[4 * c + 3];

        st[4 * c]     = (unsigned char)(aes_mul(a0, 14) ^ aes_mul(a1, 11)
                                      ^ aes_mul(a2, 13) ^ aes_mul(a3, 9));
        st[4 * c + 1] = (unsigned char)(aes_mul(a0, 9) ^ aes_mul(a1, 14)
                                      ^ aes_mul(a2, 11) ^ aes_mul(a3, 13));
        st[4 * c + 2] = (unsigned char)(aes_mul(a0, 13) ^ aes_mul(a1, 9)
                                      ^ aes_mul(a2, 14) ^ aes_mul(a3, 11));
        st[4 * c + 3] = (unsigned char)(aes_mul(a0, 11) ^ aes_mul(a1, 13)
                                      ^ aes_mul(a2, 9) ^ aes_mul(a3, 14));
    }
}

static void aes_decrypt_block(const unsigned char rk[176],
                              const unsigned char in[16], unsigned char out[16])
{
    unsigned char st[16];
    int round;

    aes_make_inv();
    memcpy(st, in, 16);
    aes_add_rk(st, rk + 10 * 16);
    for (round = 9; round >= 1; round--) {
        aes_inv_shift_rows(st);
        aes_inv_sub_bytes(st);
        aes_add_rk(st, rk + round * 16);
        aes_inv_mix_cols(st);
    }
    aes_inv_shift_rows(st);
    aes_inv_sub_bytes(st);
    aes_add_rk(st, rk);
    memcpy(out, st, 16);
}

/* 7.6.2: the initialization vector is the first 16 bytes of the data, and the
 * plaintext carries RFC 2898 padding that is removed here.  A payload that is
 * not a whole number of blocks, or has no room for an IV, yields nothing --
 * which is the honest answer for data that is not what it claims to be. */
static int aes_cbc_decrypt(const unsigned char *key, size_t keylen,
                           const unsigned char *src, size_t n, pdf_buf *out)
{
    unsigned char rk[176], prev[16], blk[16], plain[16];
    size_t i;
    unsigned int pad;

    if (keylen != 16 || n < 32 || (n % 16) != 0)
        return 0;                       /* nothing recoverable */
    aes_expand(key, rk);
    memcpy(prev, src, 16);
    for (i = 16; i + 16 <= n; i += 16) {
        int k;

        memcpy(blk, src + i, 16);
        aes_decrypt_block(rk, blk, plain);
        for (k = 0; k < 16; k++)
            plain[k] = (unsigned char)(plain[k] ^ prev[k]);
        memcpy(prev, blk, 16);
        if (pdf_buf_put(out, plain, 16) != 0)
            return PDF_E_NOMEM;
    }
    if (out->n == 0)
        return 0;
    pad = out->p[out->n - 1];
    if (pad >= 1 && pad <= 16 && (size_t)pad <= out->n)
        out->n -= pad;                  /* strip the RFC 2898 padding */
    return 0;
}
/* ---------------------------------------------------------------------------
 * PDF 2.0 -- /V 5 and /R 6, ISO 32000-2 clause 7.6.4.
 *
 * Nothing above is reused except the S-box and the field arithmetic.  The key
 * is 256 bits and is NOT run through Algorithm 1 per object; the password is
 * checked with Algorithm 2.B, which is built out of SHA-256, SHA-384, SHA-512
 * and AES-128-CBC ENCRYPTION rather than a single digest; and the file key is
 * unwrapped out of /UE or /OE rather than derived.  All of it is written here
 * from ISO 32000-2 and FIPS 180-4 / 197, for the same reason MD5 and RC4 were.
 * ------------------------------------------------------------------------ */

/* ---------------------------------------------------------------- SHA-2 */
/* FIPS 180-4.  Three digest lengths are needed, because Algorithm 2.B picks
 * between them per round from the ciphertext it has just produced; an
 * implementation carrying only SHA-256 cannot compute the hash at all. */

typedef struct {
    unsigned int  h[8];
    unsigned long long bits;
    unsigned char buf[64];
    size_t        n;
} sha256_ctx;

static unsigned int sha256_ror(unsigned int x, int c)
{
    return (x >> c) | (x << (32 - c));
}

static const unsigned int sha256_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static void sha256_block(sha256_ctx *c, const unsigned char *p)
{
    unsigned int w[64], a, b, cc, d, e, f, g, h;
    int i;

    for (i = 0; i < 16; i++)
        w[i] = ((unsigned int)p[i * 4] << 24) | ((unsigned int)p[i * 4 + 1] << 16)
             | ((unsigned int)p[i * 4 + 2] << 8) | (unsigned int)p[i * 4 + 3];
    for (i = 16; i < 64; i++) {
        unsigned int s0 = sha256_ror(w[i - 15], 7) ^ sha256_ror(w[i - 15], 18)
                        ^ (w[i - 15] >> 3);
        unsigned int s1 = sha256_ror(w[i - 2], 17) ^ sha256_ror(w[i - 2], 19)
                        ^ (w[i - 2] >> 10);

        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3];
    e = c->h[4]; f = c->h[5]; g = c->h[6]; h = c->h[7];
    for (i = 0; i < 64; i++) {
        unsigned int s1 = sha256_ror(e, 6) ^ sha256_ror(e, 11) ^ sha256_ror(e, 25);
        unsigned int ch = (e & f) ^ (~e & g);
        unsigned int t1 = h + s1 + ch + sha256_k[i] + w[i];
        unsigned int s0 = sha256_ror(a, 2) ^ sha256_ror(a, 13) ^ sha256_ror(a, 22);
        unsigned int mj = (a & b) ^ (a & cc) ^ (b & cc);
        unsigned int t2 = s0 + mj;

        h = g; g = f; f = e; e = d + t1;
        d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
    c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}

static void sha256_init(sha256_ctx *c)
{
    c->h[0] = 0x6a09e667u; c->h[1] = 0xbb67ae85u;
    c->h[2] = 0x3c6ef372u; c->h[3] = 0xa54ff53au;
    c->h[4] = 0x510e527fu; c->h[5] = 0x9b05688cu;
    c->h[6] = 0x1f83d9abu; c->h[7] = 0x5be0cd19u;
    c->bits = 0;
    c->n = 0;
}

static void sha256_update(sha256_ctx *c, const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;

    c->bits += (unsigned long long)len * 8u;
    while (len > 0) {
        size_t take = 64 - c->n;

        if (take > len) take = len;
        memcpy(c->buf + c->n, p, take);
        c->n += take;
        p += take;
        len -= take;
        if (c->n == 64) {
            sha256_block(c, c->buf);
            c->n = 0;
        }
    }
}

static void sha256_final(sha256_ctx *c, unsigned char out[32])
{
    unsigned long long bits = c->bits;      /* before the padding is counted */
    unsigned char len[8];
    unsigned char pad = 0x80;
    int i;

    for (i = 0; i < 8; i++)
        len[i] = (unsigned char)((bits >> (8 * (7 - i))) & 0xff);
    sha256_update(c, &pad, 1);
    while (c->n != 56) {
        unsigned char z = 0;
        sha256_update(c, &z, 1);
    }
    sha256_update(c, len, 8);
    for (i = 0; i < 8; i++) {
        out[i * 4]     = (unsigned char)((c->h[i] >> 24) & 0xff);
        out[i * 4 + 1] = (unsigned char)((c->h[i] >> 16) & 0xff);
        out[i * 4 + 2] = (unsigned char)((c->h[i] >> 8) & 0xff);
        out[i * 4 + 3] = (unsigned char)(c->h[i] & 0xff);
    }
}

static void sha256_once(const unsigned char *p, size_t n, unsigned char out[32])
{
    sha256_ctx c;

    sha256_init(&c);
    sha256_update(&c, p, n);
    sha256_final(&c, out);
}

/* SHA-384 and SHA-512 share a block function and differ only in the initial
 * state and in how much of it is published. */

typedef struct {
    unsigned long long h[8];
    unsigned long long bits;    /* the high half of the length is never needed */
    unsigned char buf[128];
    size_t        n;
} sha512_ctx;

static unsigned long long sha512_ror(unsigned long long x, int c)
{
    return (x >> c) | (x << (64 - c));
}

static const unsigned long long sha512_k[80] = {
    0x428a2f98d728ae22ull, 0x7137449123ef65cdull, 0xb5c0fbcfec4d3b2full,
    0xe9b5dba58189dbbcull, 0x3956c25bf348b538ull, 0x59f111f1b605d019ull,
    0x923f82a4af194f9bull, 0xab1c5ed5da6d8118ull, 0xd807aa98a3030242ull,
    0x12835b0145706fbeull, 0x243185be4ee4b28cull, 0x550c7dc3d5ffb4e2ull,
    0x72be5d74f27b896full, 0x80deb1fe3b1696b1ull, 0x9bdc06a725c71235ull,
    0xc19bf174cf692694ull, 0xe49b69c19ef14ad2ull, 0xefbe4786384f25e3ull,
    0x0fc19dc68b8cd5b5ull, 0x240ca1cc77ac9c65ull, 0x2de92c6f592b0275ull,
    0x4a7484aa6ea6e483ull, 0x5cb0a9dcbd41fbd4ull, 0x76f988da831153b5ull,
    0x983e5152ee66dfabull, 0xa831c66d2db43210ull, 0xb00327c898fb213full,
    0xbf597fc7beef0ee4ull, 0xc6e00bf33da88fc2ull, 0xd5a79147930aa725ull,
    0x06ca6351e003826full, 0x142929670a0e6e70ull, 0x27b70a8546d22ffcull,
    0x2e1b21385c26c926ull, 0x4d2c6dfc5ac42aedull, 0x53380d139d95b3dfull,
    0x650a73548baf63deull, 0x766a0abb3c77b2a8ull, 0x81c2c92e47edaee6ull,
    0x92722c851482353bull, 0xa2bfe8a14cf10364ull, 0xa81a664bbc423001ull,
    0xc24b8b70d0f89791ull, 0xc76c51a30654be30ull, 0xd192e819d6ef5218ull,
    0xd69906245565a910ull, 0xf40e35855771202aull, 0x106aa07032bbd1b8ull,
    0x19a4c116b8d2d0c8ull, 0x1e376c085141ab53ull, 0x2748774cdf8eeb99ull,
    0x34b0bcb5e19b48a8ull, 0x391c0cb3c5c95a63ull, 0x4ed8aa4ae3418acbull,
    0x5b9cca4f7763e373ull, 0x682e6ff3d6b2b8a3ull, 0x748f82ee5defb2fcull,
    0x78a5636f43172f60ull, 0x84c87814a1f0ab72ull, 0x8cc702081a6439ecull,
    0x90befffa23631e28ull, 0xa4506cebde82bde9ull, 0xbef9a3f7b2c67915ull,
    0xc67178f2e372532bull, 0xca273eceea26619cull, 0xd186b8c721c0c207ull,
    0xeada7dd6cde0eb1eull, 0xf57d4f7fee6ed178ull, 0x06f067aa72176fbaull,
    0x0a637dc5a2c898a6ull, 0x113f9804bef90daeull, 0x1b710b35131c471bull,
    0x28db77f523047d84ull, 0x32caab7b40c72493ull, 0x3c9ebe0a15c9bebcull,
    0x431d67c49c100d4cull, 0x4cc5d4becb3e42b6ull, 0x597f299cfc657e2aull,
    0x5fcb6fab3ad6faecull, 0x6c44198c4a475817ull
};

static void sha512_block(sha512_ctx *c, const unsigned char *p)
{
    unsigned long long w[80], a, b, cc, d, e, f, g, h;
    int i;

    for (i = 0; i < 16; i++) {
        int j;

        w[i] = 0;
        for (j = 0; j < 8; j++)
            w[i] = (w[i] << 8) | (unsigned long long)p[i * 8 + j];
    }
    for (i = 16; i < 80; i++) {
        unsigned long long s0 = sha512_ror(w[i - 15], 1) ^ sha512_ror(w[i - 15], 8)
                              ^ (w[i - 15] >> 7);
        unsigned long long s1 = sha512_ror(w[i - 2], 19) ^ sha512_ror(w[i - 2], 61)
                              ^ (w[i - 2] >> 6);

        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3];
    e = c->h[4]; f = c->h[5]; g = c->h[6]; h = c->h[7];
    for (i = 0; i < 80; i++) {
        unsigned long long s1 = sha512_ror(e, 14) ^ sha512_ror(e, 18)
                              ^ sha512_ror(e, 41);
        unsigned long long ch = (e & f) ^ (~e & g);
        unsigned long long t1 = h + s1 + ch + sha512_k[i] + w[i];
        unsigned long long s0 = sha512_ror(a, 28) ^ sha512_ror(a, 34)
                              ^ sha512_ror(a, 39);
        unsigned long long mj = (a & b) ^ (a & cc) ^ (b & cc);
        unsigned long long t2 = s0 + mj;

        h = g; g = f; f = e; e = d + t1;
        d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
    c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}

static void sha512_init(sha512_ctx *c, int is384)
{
    if (is384) {
        c->h[0] = 0xcbbb9d5dc1059ed8ull; c->h[1] = 0x629a292a367cd507ull;
        c->h[2] = 0x9159015a3070dd17ull; c->h[3] = 0x152fecd8f70e5939ull;
        c->h[4] = 0x67332667ffc00b31ull; c->h[5] = 0x8eb44a8768581511ull;
        c->h[6] = 0xdb0c2e0d64f98fa7ull; c->h[7] = 0x47b5481dbefa4fa4ull;
    } else {
        c->h[0] = 0x6a09e667f3bcc908ull; c->h[1] = 0xbb67ae8584caa73bull;
        c->h[2] = 0x3c6ef372fe94f82bull; c->h[3] = 0xa54ff53a5f1d36f1ull;
        c->h[4] = 0x510e527fade682d1ull; c->h[5] = 0x9b05688c2b3e6c1full;
        c->h[6] = 0x1f83d9abfb41bd6bull; c->h[7] = 0x5be0cd19137e2179ull;
    }
    c->bits = 0;
    c->n = 0;
}

static void sha512_update(sha512_ctx *c, const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;

    c->bits += (unsigned long long)len * 8u;
    while (len > 0) {
        size_t take = 128 - c->n;

        if (take > len) take = len;
        memcpy(c->buf + c->n, p, take);
        c->n += take;
        p += take;
        len -= take;
        if (c->n == 128) {
            sha512_block(c, c->buf);
            c->n = 0;
        }
    }
}

/* The length field is 128 bits wide.  Nothing hashed here is anywhere near
 * 2^64 bits long, so its high half is written as zeroes. */
static void sha512_final(sha512_ctx *c, unsigned char *out, int outlen)
{
    unsigned long long bits = c->bits;
    unsigned char len[16];
    unsigned char pad = 0x80;
    int i;

    memset(len, 0, 8);
    for (i = 0; i < 8; i++)
        len[8 + i] = (unsigned char)((bits >> (8 * (7 - i))) & 0xff);
    sha512_update(c, &pad, 1);
    while (c->n != 112) {
        unsigned char z = 0;
        sha512_update(c, &z, 1);
    }
    sha512_update(c, len, 16);
    for (i = 0; i < 8; i++) {
        int j;

        for (j = 0; j < 8; j++) {
            int k = i * 8 + j;

            if (k < outlen)
                out[k] = (unsigned char)((c->h[i] >> (8 * (7 - j))) & 0xff);
        }
    }
}

static void sha384_once(const unsigned char *p, size_t n, unsigned char out[48])
{
    sha512_ctx c;

    sha512_init(&c, 1);
    sha512_update(&c, p, n);
    sha512_final(&c, out, 48);
}

static void sha512_once(const unsigned char *p, size_t n, unsigned char out[64])
{
    sha512_ctx c;

    sha512_init(&c, 0);
    sha512_update(&c, p, n);
    sha512_final(&c, out, 64);
}

/* ----------------------------------------------------- AES, generalised */
/* The milestone above needed AES-128 and only ever decrypted.  /AESV3 needs a
 * 256-bit key -- Nk = 8, Nr = 14, a 240-byte schedule -- and Algorithm 2.B
 * needs the FORWARD direction, because the hash it defines is built out of
 * AES-128-CBC encryption.  The 128-bit decrypt path above is left exactly as
 * it was: these are separate entry points, not edits to it. */

#define AES_MAX_RK 240

/* FIPS-197 5.2, for Nk of 4 or 8.  The Nk = 8 schedule has an extra SubWord
 * every eighth word that the Nk = 4 schedule does not; leaving it out is the
 * classic way to end up with an AES-256 that passes no published vector. */
static int aes_expand_key(const unsigned char *key, int keylen,
                          unsigned char rk[AES_MAX_RK])
{
    int nk = keylen / 4;
    int nr = (nk == 8) ? 14 : 10;
    int total = (nr + 1) * 4;
    int i;

    memcpy(rk, key, (size_t)keylen);
    for (i = nk; i < total; i++) {
        unsigned char t[4];

        memcpy(t, rk + (i - 1) * 4, 4);
        if (i % nk == 0) {
            unsigned char s = t[0];

            t[0] = (unsigned char)(aes_sbox[t[1]] ^ aes_rcon[i / nk - 1]);
            t[1] = aes_sbox[t[2]];
            t[2] = aes_sbox[t[3]];
            t[3] = aes_sbox[s];
        } else if (nk == 8 && i % nk == 4) {
            t[0] = aes_sbox[t[0]];
            t[1] = aes_sbox[t[1]];
            t[2] = aes_sbox[t[2]];
            t[3] = aes_sbox[t[3]];
        }
        rk[i * 4]     = (unsigned char)(rk[(i - nk) * 4] ^ t[0]);
        rk[i * 4 + 1] = (unsigned char)(rk[(i - nk) * 4 + 1] ^ t[1]);
        rk[i * 4 + 2] = (unsigned char)(rk[(i - nk) * 4 + 2] ^ t[2]);
        rk[i * 4 + 3] = (unsigned char)(rk[(i - nk) * 4 + 3] ^ t[3]);
    }
    return nr;
}

static void aes_sub_bytes(unsigned char st[16])
{
    int i;

    for (i = 0; i < 16; i++)
        st[i] = aes_sbox[st[i]];
}

/* The exact inverse of aes_inv_shift_rows: row r rotates LEFT by r. */
static void aes_shift_rows(unsigned char st[16])
{
    unsigned char t[16];
    int r, c;

    memcpy(t, st, 16);
    for (r = 1; r < 4; r++)
        for (c = 0; c < 4; c++)
            st[r + 4 * c] = t[r + 4 * ((c + r) & 3)];
}

static void aes_mix_cols(unsigned char st[16])
{
    int c;

    for (c = 0; c < 4; c++) {
        unsigned char a0 = st[4 * c], a1 = st[4 * c + 1];
        unsigned char a2 = st[4 * c + 2], a3 = st[4 * c + 3];

        st[4 * c]     = (unsigned char)(aes_mul(a0, 2) ^ aes_mul(a1, 3) ^ a2 ^ a3);
        st[4 * c + 1] = (unsigned char)(a0 ^ aes_mul(a1, 2) ^ aes_mul(a2, 3) ^ a3);
        st[4 * c + 2] = (unsigned char)(a0 ^ a1 ^ aes_mul(a2, 2) ^ aes_mul(a3, 3));
        st[4 * c + 3] = (unsigned char)(aes_mul(a0, 3) ^ a1 ^ a2 ^ aes_mul(a3, 2));
    }
}

static void aes_encrypt_block(const unsigned char *rk, int nr,
                              const unsigned char in[16], unsigned char out[16])
{
    unsigned char st[16];
    int round;

    memcpy(st, in, 16);
    aes_add_rk(st, rk);
    for (round = 1; round < nr; round++) {
        aes_sub_bytes(st);
        aes_shift_rows(st);
        aes_mix_cols(st);
        aes_add_rk(st, rk + round * 16);
    }
    aes_sub_bytes(st);
    aes_shift_rows(st);
    aes_add_rk(st, rk + nr * 16);
    memcpy(out, st, 16);
}

/* The same shape as aes_decrypt_block, with the round count as an argument. */
static void aes_decrypt_block_n(const unsigned char *rk, int nr,
                                const unsigned char in[16], unsigned char out[16])
{
    unsigned char st[16];
    int round;

    aes_make_inv();
    memcpy(st, in, 16);
    aes_add_rk(st, rk + nr * 16);
    for (round = nr - 1; round >= 1; round--) {
        aes_inv_shift_rows(st);
        aes_inv_sub_bytes(st);
        aes_add_rk(st, rk + round * 16);
        aes_inv_mix_cols(st);
    }
    aes_inv_shift_rows(st);
    aes_inv_sub_bytes(st);
    aes_add_rk(st, rk);
    memcpy(out, st, 16);
}

/* CBC with an explicit IV, no padding added and none removed: n must already
 * be a whole number of blocks.  Algorithm 2.B is the only caller that ever
 * encrypts anything. */
static void aes_cbc_encrypt_raw(const unsigned char *key, int keylen,
                                const unsigned char iv[16],
                                const unsigned char *src, size_t n,
                                unsigned char *dst)
{
    unsigned char rk[AES_MAX_RK], prev[16], blk[16];
    size_t i;
    int nr = aes_expand_key(key, keylen, rk);

    memcpy(prev, iv, 16);
    for (i = 0; i + 16 <= n; i += 16) {
        int k;

        for (k = 0; k < 16; k++)
            blk[k] = (unsigned char)(src[i + k] ^ prev[k]);
        aes_encrypt_block(rk, nr, blk, dst + i);
        memcpy(prev, dst + i, 16);
    }
}

/* The mirror image: an explicit IV rather than one taken from the data, and
 * no padding removed.  Algorithm 2.A unwraps /UE and /OE with this. */
static void aes_cbc_decrypt_raw(const unsigned char *key, int keylen,
                                const unsigned char iv[16],
                                const unsigned char *src, size_t n,
                                unsigned char *dst)
{
    unsigned char rk[AES_MAX_RK], prev[16], blk[16], plain[16];
    size_t i;
    int nr = aes_expand_key(key, keylen, rk);

    memcpy(prev, iv, 16);
    for (i = 0; i + 16 <= n; i += 16) {
        int k;

        memcpy(blk, src + i, 16);
        aes_decrypt_block_n(rk, nr, blk, plain);
        for (k = 0; k < 16; k++)
            dst[i + k] = (unsigned char)(plain[k] ^ prev[k]);
        memcpy(prev, blk, 16);
    }
}

/* /Perms is a single ECB block: no chaining, no IV, no padding. */
static void aes_ecb_decrypt_one(const unsigned char *key, int keylen,
                                const unsigned char in[16],
                                unsigned char out[16])
{
    unsigned char rk[AES_MAX_RK];
    int nr = aes_expand_key(key, keylen, rk);

    aes_decrypt_block_n(rk, nr, in, out);
}

/* ----------------------------------------------- ISO 32000-2 Algorithm 2.B */
/* The hash /R 6 authenticates with.  It is not a plain digest: every round
 * encrypts a 64-fold repetition of (password, running hash, extra data) and
 * then chooses the next digest length out of the ciphertext, which is why all
 * three of SHA-256, SHA-384 and SHA-512 have to exist for it to run at all.
 * The loop always turns at least 64 times and then stops on a condition read
 * from the last byte of the ciphertext.
 *
 * `udata` is empty when a user password is being checked and the whole
 * 48-byte /U when an owner password is. */

#define CRYPT_PW_MAX   127      /* 7.6.4.3.3 truncates a password here */
#define CRYPT_2B_K1    (64 * (CRYPT_PW_MAX + 64 + 48))

static void crypt_hash_2b(int r, const unsigned char *pw, size_t pwlen,
                          const unsigned char *salt, size_t saltlen,
                          const unsigned char *udata, size_t udatalen,
                          unsigned char out[32])
{
    static unsigned char k1[CRYPT_2B_K1], e[CRYPT_2B_K1];
    unsigned char k[64];
    size_t klen, step, n;
    int round;
    sha256_ctx sc;

    sha256_init(&sc);
    sha256_update(&sc, pw, pwlen);
    sha256_update(&sc, salt, saltlen);
    sha256_update(&sc, udata, udatalen);
    sha256_final(&sc, k);
    klen = 32;

    /* Revision 5 -- Adobe extension level 3, deprecated -- stopped here.
     * Revision 6 is the one that iterates. */
    if (r < 6) {
        memcpy(out, k, 32);
        return;
    }

    for (round = 0; ; ) {
        unsigned int sum = 0;
        int i;

        step = pwlen + klen + udatalen;
        for (n = 0; n < 64; n++) {
            if (pwlen)    memcpy(k1 + n * step, pw, pwlen);
            memcpy(k1 + n * step + pwlen, k, klen);
            if (udatalen) memcpy(k1 + n * step + pwlen + klen, udata, udatalen);
        }
        n = step * 64;
        aes_cbc_encrypt_raw(k, 16, k + 16, k1, n, e);

        /* 256 is 1 modulo 3, so summing the bytes and reducing is the same
         * number as reducing the 16-byte big-endian integer they spell. */
        for (i = 0; i < 16; i++)
            sum += e[i];
        switch (sum % 3) {
        case 0:  sha256_once(e, n, k); klen = 32; break;
        case 1:  sha384_once(e, n, k); klen = 48; break;
        default: sha512_once(e, n, k); klen = 64; break;
        }

        round++;
        if (round >= 64 && (int)e[n - 1] <= round - 32)
            break;
    }
    memcpy(out, k, 32);
}

/* 7.6.2 with a 256-bit key.  The shape is exactly aes_cbc_decrypt's -- the
 * initialization vector is the first 16 bytes of the data and the plaintext
 * carries RFC 2898 padding -- so /AESV3 streams and strings are framed the way
 * /AESV2 ones are.  Only the key length and the round count differ. */
static int aes_cbc_decrypt_v3(const unsigned char *key, size_t keylen,
                              const unsigned char *src, size_t n, pdf_buf *out)
{
    unsigned char rk[AES_MAX_RK], prev[16], blk[16], plain[16];
    size_t i;
    unsigned int pad;
    int nr;

    if (keylen != 32 || n < 32 || (n % 16) != 0)
        return 0;                       /* nothing recoverable */
    nr = aes_expand_key(key, (int)keylen, rk);
    memcpy(prev, src, 16);
    for (i = 16; i + 16 <= n; i += 16) {
        int k;

        memcpy(blk, src + i, 16);
        aes_decrypt_block_n(rk, nr, blk, plain);
        for (k = 0; k < 16; k++)
            plain[k] = (unsigned char)(plain[k] ^ prev[k]);
        memcpy(prev, blk, 16);
        if (pdf_buf_put(out, plain, 16) != 0)
            return PDF_E_NOMEM;
    }
    if (out->n == 0)
        return 0;
    pad = out->p[out->n - 1];
    if (pad >= 1 && pad <= 16 && (size_t)pad <= out->n)
        out->n -= pad;                  /* strip the RFC 2898 padding */
    return 0;
}

struct pdf_crypt {
    int v, r;
    long long p;
    char method[16];    /* "RC4", "AES", "AES-256", or "None" */
    int length_bits;
    unsigned char key[16];      /* the file encryption key (Algorithm 2)  */
    int           keylen;       /* its length in bytes                    */
    int           stm_cfm;      /* CFM_* selected by /StmF                */
    int           str_cfm;      /* CFM_* selected by /StrF                */
    int           encrypt_metadata;
    int           encnum;       /* object number of /Encrypt, or -1       */
    unsigned char id0[128];
    size_t        id0len;
    unsigned char o[48], u[48];
    size_t        olen, ulen;
    int           owner_matched, user_matched;
    /* The user password, once something has proved what it is: either it was
     * the password supplied, or Algorithm 7 recovered it out of /O.  Empty
     * until then -- and empty is also the answer for the ordinary document
     * whose user password really is empty. */
    char          userpw[128];
    int           ready;        /* authenticated: the key above is usable */
    int           standard;     /* /Filter /Standard, the only one handled */
    /* /V 5.  The key is 256 bits, so it does not fit the field above, and the
     * three strings below exist at no earlier revision. */
    unsigned char key256[32];   /* the file encryption key (Algorithm 2.A)  */
    unsigned char ue[32], oe[32];   /* the wrapped copies of it             */
    size_t        uelen, oelen;
    unsigned char perms[16];    /* /Perms, the permission block             */
    size_t        permslen;
    int           perms_bad;    /* ... and it disagreed with /P             */
    int           v5_malformed; /* a /V 5 string is missing or the wrong size */
};

static void detect_method(pdf_doc *doc, struct pdf_crypt *c, pdf_obj *enc)
{
    pdf_obj *cf, *stmf, *entry, *cfm;
    const char *name;

    /* V 1 and 2 are RC4 with no crypt filters. */
    if (c->v <= 2) {
        snprintf(c->method, sizeof c->method, "RC4");
        return;
    }
    /* V 4 and 5 select an algorithm through /CF and the /StmF name (7.6.5). */
    snprintf(c->method, sizeof c->method, "RC4");
    cf = pdf_dict_get(doc, enc, "CF");
    stmf = pdf_dict_get(doc, enc, "StmF");
    name = pdf_is_name(stmf, NULL) ? pdf_to_name(stmf) : "StdCF";
    if (pdf_is_dict(cf)) {
        entry = pdf_dict_get(doc, cf, name);
        if (pdf_is_dict(entry)) {
            cfm = pdf_dict_get(doc, entry, "CFM");
            if (pdf_is_name(cfm, "AESV2"))
                snprintf(c->method, sizeof c->method, "AES");
            else if (pdf_is_name(cfm, "AESV3"))
                snprintf(c->method, sizeof c->method, "AES-256");
            else if (pdf_is_name(cfm, "None"))
                snprintf(c->method, sizeof c->method, "None");
        }
    }
    if (c->v == 5 && strcmp(c->method, "AES-256") != 0)
        snprintf(c->method, sizeof c->method, "AES-256");
}

/* -------------------------------------------------- the handler's own work */

/* 7.6.5 table 25.  /StmF and /StrF name an entry of /CF; the two standard
 * names need no entry at all.  A /CFM this implementation does not know is
 * recorded as such rather than guessed at, so a document using one is reported
 * as encrypted-but-unsupported instead of silently mis-decrypted. */
static int crypt_filter_method(pdf_doc *doc, pdf_obj *enc, const char *which)
{
    pdf_obj *sel = pdf_dict_get(doc, enc, which);
    pdf_obj *cf, *entry, *cfm;
    const char *name;

    if (pdf_is_name(sel, "Identity"))
        return CFM_NONE;
    name = pdf_is_name(sel, NULL) ? pdf_to_name(sel) : "Identity";
    if (strcmp(name, "Identity") == 0)
        return CFM_NONE;

    cf = pdf_dict_get(doc, enc, "CF");
    if (!pdf_is_dict(cf))
        return CFM_NONE;
    entry = pdf_dict_get(doc, cf, name);
    if (!pdf_is_dict(entry))
        return CFM_NONE;
    cfm = pdf_dict_get(doc, entry, "CFM");
    if (pdf_is_name(cfm, "V2"))     return CFM_RC4;
    if (pdf_is_name(cfm, "AESV2"))  return CFM_AESV2;
    if (pdf_is_name(cfm, "None"))   return CFM_NONE;
    if (pdf_is_name(cfm, "AESV3"))  return CFM_AESV3;
    if (pdf_is_name(cfm, NULL))     return CFM_UNKNOWN;
    return CFM_NONE;
}

/* 7.6.3.3: pad or truncate a password to exactly 32 bytes. */
static void crypt_pad_pw(const char *pw, unsigned char out[32])
{
    size_t n = pw ? strlen(pw) : 0;

    if (n > 32) n = 32;
    if (n) memcpy(out, pw, n);
    memcpy(out + n, crypt_pad, 32 - n);
}

/* Algorithm 2: the file encryption key from the user password. */
static void crypt_file_key(struct pdf_crypt *c, const char *userpw,
                           unsigned char out[16])
{
    unsigned char padded[32], digest[16];
    unsigned char pbytes[4];
    unsigned int pu = (unsigned int)(c->p & 0xffffffffu);
    md5_ctx m;
    int i;

    crypt_pad_pw(userpw, padded);
    pbytes[0] = (unsigned char)(pu & 0xff);
    pbytes[1] = (unsigned char)((pu >> 8) & 0xff);
    pbytes[2] = (unsigned char)((pu >> 16) & 0xff);
    pbytes[3] = (unsigned char)((pu >> 24) & 0xff);

    md5_init(&m);
    md5_update(&m, padded, 32);
    md5_update(&m, c->o, c->olen);
    md5_update(&m, pbytes, 4);
    md5_update(&m, c->id0, c->id0len);
    if (c->r >= 4 && !c->encrypt_metadata) {
        static const unsigned char ff[4] = { 0xff, 0xff, 0xff, 0xff };
        md5_update(&m, ff, 4);
    }
    md5_final(&m, digest);

    if (c->r >= 3)
        for (i = 0; i < 50; i++)
            md5_once(digest, (size_t)c->keylen, digest);

    memcpy(out, digest, 16);
}

/* Algorithm 4 (R2) and Algorithm 5 (R3+): the value /U should hold. */
static void crypt_expected_u(struct pdf_crypt *c, const unsigned char *key,
                             unsigned char out[32])
{
    unsigned char digest[16], tmp[32];
    md5_ctx m;
    int i, j;

    if (c->r == 2) {
        rc4_crypt(key, (size_t)c->keylen, crypt_pad, 32, out);
        return;
    }
    md5_init(&m);
    md5_update(&m, crypt_pad, 32);
    md5_update(&m, c->id0, c->id0len);
    md5_final(&m, digest);
    rc4_crypt(key, (size_t)c->keylen, digest, 16, tmp);
    for (i = 1; i <= 19; i++) {
        unsigned char k2[16];

        for (j = 0; j < c->keylen; j++)
            k2[j] = (unsigned char)(key[j] ^ i);
        rc4_crypt(k2, (size_t)c->keylen, tmp, 16, tmp);
    }
    memcpy(out, tmp, 16);
    memset(out + 16, 0, 16);        /* R3+ leaves these arbitrary */
}

/* Algorithm 6: does this password authenticate as the user password? */
static int crypt_try_user(struct pdf_crypt *c, const char *pw,
                          unsigned char keyout[16])
{
    unsigned char key[16], want[32];
    size_t cmp = (c->r >= 3) ? 16 : 32;

    if (c->ulen < cmp)
        return 0;
    crypt_file_key(c, pw, key);
    crypt_expected_u(c, key, want);
    if (memcmp(want, c->u, cmp) != 0)
        return 0;
    memcpy(keyout, key, 16);
    /* This password just proved itself against /U, so it is the user password
     * whatever route it arrived by: straight from the caller, or recovered
     * from /O by Algorithm 7, which finishes by calling this function. */
    if (pw != NULL) {
        size_t n = strlen(pw);

        if (n >= sizeof c->userpw) n = sizeof c->userpw - 1;
        memcpy(c->userpw, pw, n);
        c->userpw[n] = '\0';
    }
    return 1;
}

/* Algorithm 7: derive the RC4 key /O was built with, undo it, and treat what
 * falls out as a user password.  The 20 rounds run from 19 down to 0. */
static int crypt_try_owner(struct pdf_crypt *c, const char *pw,
                           unsigned char keyout[16])
{
    unsigned char padded[32], digest[16], key[16], guess[32];
    char userpw[33];
    size_t n;
    int i, j;

    if (c->olen < 32)
        return 0;
    crypt_pad_pw(pw, padded);
    md5_once(padded, 32, digest);
    if (c->r >= 3)
        for (i = 0; i < 50; i++)
            md5_once(digest, 16, digest);
    memcpy(key, digest, 16);

    memcpy(guess, c->o, 32);
    if (c->r == 2) {
        rc4_crypt(key, (size_t)c->keylen, guess, 32, guess);
    } else {
        for (i = 19; i >= 0; i--) {
            unsigned char k2[16];

            for (j = 0; j < c->keylen; j++)
                k2[j] = (unsigned char)(key[j] ^ i);
            rc4_crypt(k2, (size_t)c->keylen, guess, 32, guess);
        }
    }
    /* The result is a padded password; trim it at the padding string so it can
     * be handed to Algorithm 6 as text. */
    for (n = 0; n < 32; n++)
        if (guess[n] == crypt_pad[0] &&
            memcmp(guess + n, crypt_pad, 32 - n) == 0)
            break;
    memcpy(userpw, guess, n);
    userpw[n] = '\0';
    return crypt_try_user(c, userpw, keyout);
}

/* 7.6.1: /ID is not encrypted, and its first element feeds Algorithm 2. */
static void crypt_take_id(struct pdf_crypt *c, pdf_doc *doc, pdf_obj *id0)
{
    const char *s;
    size_t n = 0;

    c->id0len = 0;
    if (!pdf_is_string(id0)) {
        /* The caller may have handed us the array rather than its head. */
        if (pdf_is_array(id0))
            id0 = pdf_array_get(doc, id0, 0);
        if (!pdf_is_string(id0))
            return;
    }
    s = pdf_to_string(id0, &n);
    if (n > sizeof c->id0) n = sizeof c->id0;
    if (s && n) memcpy(c->id0, s, n);
    c->id0len = n;
}

static void crypt_take_str(unsigned char *dst, size_t cap, size_t *lenout,
                           pdf_obj *o)
{
    const char *s;
    size_t n = 0;

    *lenout = 0;
    if (!pdf_is_string(o)) return;
    s = pdf_to_string(o, &n);
    if (n > cap) n = cap;
    if (s && n) memcpy(dst, s, n);
    *lenout = n;
}

/* Which object is /Encrypt?  Its own strings are never encrypted (7.6.1), so
 * the walker has to be able to recognise it. */
static int crypt_encrypt_objnum(pdf_doc *doc)
{
    pdf_obj *e = pdf_dict_get_raw(doc->trailer, "Encrypt");

    return (e && e->kind == PDF_REF) ? e->u.ref.num : -1;
}

static int crypt_setup(pdf_doc *doc, struct pdf_crypt *c, pdf_obj *enc,
                       pdf_obj *id0)
{
    pdf_obj *o;

    c->encnum = crypt_encrypt_objnum(doc);
    c->standard = 1;
    o = pdf_dict_get(doc, enc, "Filter");
    if (pdf_is_name(o, NULL) && strcmp(pdf_to_name(o), "Standard") != 0)
        c->standard = 0;                /* /Adobe.PubSec and friends: 7.6.4 */

    /* 7.6.3.2: /Length is in bits and defaults to 40; n is 5 at /V 1. */
    c->keylen = (c->v <= 1) ? 5 : (c->length_bits / 8);
    if (c->keylen < 5)  c->keylen = 5;
    if (c->keylen > 16) c->keylen = 16;

    c->encrypt_metadata = 1;
    o = pdf_dict_get(doc, enc, "EncryptMetadata");
    if (o && o->kind == PDF_BOOL && !o->u.b)
        c->encrypt_metadata = 0;

    crypt_take_str(c->o, sizeof c->o, &c->olen, pdf_dict_get(doc, enc, "O"));
    crypt_take_str(c->u, sizeof c->u, &c->ulen, pdf_dict_get(doc, enc, "U"));
    crypt_take_id(c, doc, id0);
    /* 7.6.4.3.  At /V 5 the file key is 256 bits whatever /Length says, and
     * three strings that no earlier revision has carry the wrapped copies of
     * it and the permission block.  /O and /U are 48 bytes here rather than
     * 32, which the fixed-size fields above already allow for. */
    if (c->v == 5) {
        crypt_take_str(c->ue, sizeof c->ue, &c->uelen,
                       pdf_dict_get(doc, enc, "UE"));
        crypt_take_str(c->oe, sizeof c->oe, &c->oelen,
                       pdf_dict_get(doc, enc, "OE"));
        crypt_take_str(c->perms, sizeof c->perms, &c->permslen,
                       pdf_dict_get(doc, enc, "Perms"));
        /* 7.6.4.3 fixes all five widths, and a file that does not carry them is
         * not a /V 5 file this tool can reason about at all: the key is wrapped
         * in /UE and /OE and the permissions are cross-checked against /Perms,
         * so a missing or short one leaves nothing to authenticate against.
         * The reference tool refuses the whole document rather than reporting
         * from the parameters that did survive, and so does this. */
        if (c->ulen != 48 || c->olen != 48 || c->uelen != 32
            || c->oelen != 32 || c->permslen != 16)
            c->v5_malformed = 1;
    }

    if (c->v >= 4) {
        c->stm_cfm = crypt_filter_method(doc, enc, "StmF");
        c->str_cfm = crypt_filter_method(doc, enc, "StrF");
    } else {
        c->stm_cfm = CFM_RC4;
        c->str_cfm = CFM_RC4;
    }

    /* 7.6.3.1: a reader tries the empty user password before prompting. */
    (void)pdf_crypt_authenticate(doc, "");
    return 0;
}

/* ------------------------------------------------ /V 5: Algorithms 2.A and 13 */

/* 7.6.4.3.3.  The password is UTF-8 and is cut at 127 bytes.  SASLprep
 * normalisation is not applied: it changes nothing for an ASCII password, and
 * the empty password every reader tries first is ASCII. */
static size_t crypt_pw_utf8(const char *pw, unsigned char out[CRYPT_PW_MAX])
{
    size_t n = pw ? strlen(pw) : 0;

    if (n > CRYPT_PW_MAX) n = CRYPT_PW_MAX;
    if (n) memcpy(out, pw, n);
    return n;
}

/* Algorithm 2.A.  /U and /O are each hash || validation salt || key salt, 32 +
 * 8 + 8 bytes.  A password is right when 2.B over the VALIDATION salt
 * reproduces the stored hash; the file key is then unwrapped from /UE or /OE
 * with a second 2.B over the KEY salt.  The only difference between the user
 * half and the owner half is that the owner's hashes take the whole of /U as
 * extra input -- which is why the two are one function here.
 *
 * There is no recovery of one password from the other at this revision: /UE is
 * sealed under the user password and /OE under the owner's, and neither leads
 * back to the other. */
static int crypt_try_v5(struct pdf_crypt *c, const char *pw, int owner,
                        unsigned char keyout[32])
{
    static const unsigned char no_udata[1] = { 0 };
    unsigned char pwb[CRYPT_PW_MAX], hash[32], inter[32], zero[16];
    const unsigned char *blob    = owner ? c->o : c->u;
    const unsigned char *wrapped = owner ? c->oe : c->ue;
    size_t bloblen = owner ? c->olen : c->ulen;
    size_t wraplen = owner ? c->oelen : c->uelen;
    const unsigned char *udata = owner ? c->u : no_udata;
    size_t udatalen = owner ? c->ulen : 0;
    size_t pwlen;

    if (bloblen < 48 || wraplen < 32)
        return 0;                       /* too short to authenticate against */
    if (owner && udatalen < 48)
        return 0;                       /* the owner hash needs all of /U    */

    pwlen = crypt_pw_utf8(pw, pwb);
    crypt_hash_2b(c->r, pwb, pwlen, blob + 32, 8, udata, udatalen, hash);
    if (memcmp(hash, blob, 32) != 0)
        return 0;

    /* The KEY salt, not the validation salt.  The unwrap is CBC with a ZERO
     * initialization vector, no IV prefix in the data and no padding -- which
     * is what makes it unlike every other AES-CBC in this file. */
    crypt_hash_2b(c->r, pwb, pwlen, blob + 40, 8, udata, udatalen, inter);
    memset(zero, 0, sizeof zero);
    aes_cbc_decrypt_raw(inter, 32, zero, wrapped, 32, keyout);
    return 1;
}

/* Algorithm 13.  /Perms is a single ECB block -- no chaining, no IV, no
 * padding -- holding /P again in little-endian, the /EncryptMetadata flag as
 * 'T' or 'F', and the marker 'a' 'd' 'b'.  It exists so that a file cannot
 * claim one set of permissions in the clear and another under the key, and a
 * reader that accepted a disagreement silently would defeat the point of it.
 * Returns 1 when the block agrees with the dictionary around it. */
static int crypt_check_perms(struct pdf_crypt *c)
{
    unsigned char dec[16];
    unsigned int got, want;

    if (c->permslen < 16)
        return 1;                       /* absent: nothing contradicts /P */
    aes_ecb_decrypt_one(c->key256, 32, c->perms, dec);
    if (dec[9] != 'a' || dec[10] != 'd' || dec[11] != 'b')
        return 0;
    got = (unsigned int)dec[0] | ((unsigned int)dec[1] << 8)
        | ((unsigned int)dec[2] << 16) | ((unsigned int)dec[3] << 24);
    want = (unsigned int)(c->p & 0xffffffffu);
    if (got != want)
        return 0;
    if (dec[8] != (c->encrypt_metadata ? 'T' : 'F'))
        return 0;
    return 1;
}

static int crypt_auth_v5(pdf_doc *doc, struct pdf_crypt *c, const char *pw)
{
    unsigned char key[32];

    if (c->r != 5 && c->r != 6)
        return PDF_E_UNSUP;             /* a /V 5 revision we do not know */

    /* Try both and remember both answers, exactly as the revision below. */
    if (crypt_try_v5(c, pw, 0, key)) {
        size_t n = pw ? strlen(pw) : 0;

        memcpy(c->key256, key, 32);
        c->user_matched = 1;
        c->ready = 1;
        /* Knowing it because it was supplied, not by recovering it: at this
         * revision the owner password yields /OE and nothing else, so the
         * owner branch below deliberately leaves this alone. */
        if (n >= sizeof c->userpw) n = sizeof c->userpw - 1;
        if (n) memcpy(c->userpw, pw, n);
        c->userpw[n] = '\0';
    }
    if (crypt_try_v5(c, pw, 1, key)) {
        if (!c->ready) memcpy(c->key256, key, 32);
        c->owner_matched = 1;
        c->ready = 1;
    }
    if (!c->ready)
        return PDF_E_ENCRYPT;

    /* The key exists now, so the permission block can finally be read. */
    c->perms_bad = !crypt_check_perms(c);
    doc->unlocked = 1;
    return 0;
}

static int crypt_auth(pdf_doc *doc, struct pdf_crypt *c, const char *pw)
{
    unsigned char key[16];

    /* /V 5 is a different algorithm end to end -- SHA-2 rather than MD5, a
     * 256-bit key, no per-object derivation -- so it is answered on its own
     * path instead of inside the one below. */
    if (c->standard && c->v == 5)
        return crypt_auth_v5(doc, c, pw);
    if (!c->standard || c->v == 3 || c->v > 4)
        return PDF_E_UNSUP;             /* not a handler we implement */

    /* Try both, and remember both answers: qpdf reports each separately. */
    if (crypt_try_user(c, pw, key)) {
        memcpy(c->key, key, 16);
        c->user_matched = 1;
        c->ready = 1;
    }
    if (crypt_try_owner(c, pw, key)) {
        if (!c->ready) memcpy(c->key, key, 16);
        c->owner_matched = 1;
        c->ready = 1;
    }
    if (!c->ready)
        return PDF_E_ENCRYPT;
    doc->unlocked = 1;
    return 0;
}

/* Algorithm 1: the key for one object.  `aes` appends the four bytes 0x73 0x41
 * 0x6C 0x54 before the digest is taken. */
static size_t crypt_object_key(struct pdf_crypt *c, int num, int gen, int aes,
                               unsigned char out[16])
{
    unsigned char ext[5], digest[16];
    size_t n;
    md5_ctx m;

    ext[0] = (unsigned char)(num & 0xff);
    ext[1] = (unsigned char)((num >> 8) & 0xff);
    ext[2] = (unsigned char)((num >> 16) & 0xff);
    ext[3] = (unsigned char)(gen & 0xff);
    ext[4] = (unsigned char)((gen >> 8) & 0xff);

    md5_init(&m);
    md5_update(&m, c->key, (size_t)c->keylen);
    md5_update(&m, ext, 5);
    if (aes) {
        static const unsigned char salt[4] = { 0x73, 0x41, 0x6c, 0x54 };
        md5_update(&m, salt, 4);
    }
    md5_final(&m, digest);

    n = (size_t)c->keylen + 5;
    if (n > 16) n = 16;
    memcpy(out, digest, n);
    return n;
}

static int crypt_apply(pdf_doc *doc, int num, int gen, int cfm,
                       const unsigned char *src, size_t srclen, pdf_buf *out)
{
    unsigned char ok[16];
    size_t oklen;

    if (cfm == CFM_NONE || cfm == CFM_UNKNOWN)
        return pdf_buf_put(out, src, srclen);

    /* 7.6.4.2.  At /AESV3 the FILE key IS the object key: Algorithm 1 is not
     * applied at all.  Running the MD5-of-object-number step below over it
     * would produce sixteen unrelated bytes and a stream that decodes to
     * noise, which is the single easiest way to get this revision wrong.
     *
     * The second line is the malformed case -- /AESV3 named by a file that is
     * not /V 5, or /V 5 naming one of the older filters -- where no key of the
     * right length was ever derived.  Handing the bytes back untouched beats
     * deriving a key from a buffer that was never filled in. */
    if (cfm == CFM_AESV3 && doc->crypt->v == 5)
        return aes_cbc_decrypt_v3(doc->crypt->key256, 32, src, srclen, out);
    if (cfm == CFM_AESV3 || doc->crypt->v == 5)
        return pdf_buf_put(out, src, srclen);

    oklen = crypt_object_key(doc->crypt, num, gen, cfm == CFM_AESV2, ok);
    if (cfm == CFM_AESV2)
        return aes_cbc_decrypt(ok, oklen, src, srclen, out);

    if (pdf_buf_put(out, src, srclen) != 0)
        return PDF_E_NOMEM;
    if (srclen)
        rc4_crypt(ok, oklen, out->p + out->n - srclen, srclen,
                  out->p + out->n - srclen);
    return 0;
}

/* Is this stream one that 7.6.1 exempts?  A cross-reference stream is read
 * before any key exists and is never encrypted; /Metadata is exempt when
 * /EncryptMetadata is false. */
static int crypt_stream_exempt(pdf_doc *doc, pdf_obj *stm)
{
    pdf_obj *type = pdf_dict_get_raw(stm, "Type");

    if (pdf_is_name(type, "XRef"))
        return 1;
    if (!doc->crypt->encrypt_metadata && pdf_is_name(type, "Metadata"))
        return 1;
    return 0;
}

/* Decrypt every string reachable from one freshly parsed indirect object.
 * Containers are walked; the object's own number and generation are the key
 * material for all of them (Algorithm 1 step a).  Strings inside an object
 * stream are NOT reached from here, and must not be: the stream carrying them
 * was decrypted once, as a stream, and they are not encrypted again. */
static void crypt_strings(pdf_doc *doc, pdf_obj *o, int num, int gen, int depth)
{
    size_t i, n;

    if (!o || depth > 64)
        return;
    switch (o->kind) {
    case PDF_STRING: {
        pdf_buf b;

        if (o->u.str.n == 0)
            break;
        pdf_buf_init(&b);
        if (crypt_apply(doc, num, gen, doc->crypt->str_cfm,
                        (const unsigned char *)o->u.str.p, o->u.str.n, &b) == 0
            && b.n <= o->u.str.n) {
            /* Plaintext is never longer than ciphertext for either cipher, so
             * the existing allocation always has room. */
            if (b.n) memcpy(o->u.str.p, b.p, b.n);
            o->u.str.n = b.n;
        }
        pdf_buf_free(&b);
        break;
    }
    case PDF_ARRAY:
        for (i = 0, n = pdf_array_len(o); i < n; i++)
            crypt_strings(doc, pdf_array_get_raw(o, i), num, gen, depth + 1);
        break;
    case PDF_DICT:
        for (i = 0, n = pdf_dict_len(o); i < n; i++)
            crypt_strings(doc, pdf_dict_val_raw(o, i), num, gen, depth + 1);
        break;
    case PDF_STREAM:
        crypt_strings(doc, o->u.stm.dict, num, gen, depth + 1);
        break;
    default:
        break;
    }
}

void pdf_crypt_decrypt_obj(pdf_doc *doc, pdf_obj *o, int num, int gen)
{
    if (!doc || !doc->crypt || !doc->crypt->ready || !o)
        return;
    if (num == doc->crypt->encnum)
        return;                         /* 7.6.1: never its own strings */
    if (o->kind == PDF_STREAM && pdf_is_name(pdf_dict_get_raw(o, "Type"), "XRef"))
        return;
    crypt_strings(doc, o, num, gen, 0);
}

int pdf_crypt_stream(pdf_doc *doc, pdf_obj *stm, const unsigned char *src,
                     size_t srclen, pdf_buf *out)
{
    if (!doc || !doc->crypt || !doc->crypt->ready || !pdf_is_stream(stm))
        return 0;
    if (stm->u.stm.num <= 0 || stm->u.stm.num == doc->crypt->encnum)
        return 0;
    if (crypt_stream_exempt(doc, stm))
        return 0;
    if (doc->crypt->stm_cfm == CFM_NONE || doc->crypt->stm_cfm == CFM_UNKNOWN)
        return 0;
    if (crypt_apply(doc, stm->u.stm.num, stm->u.stm.gen, doc->crypt->stm_cfm,
                    src, srclen, out) != 0)
        return 0;
    return 1;
}

/* ------------------------------------------- what the reports need to know */

static const char *cfm_name(int cfm)
{
    switch (cfm) {
    case CFM_RC4:   return "RC4";
    case CFM_AESV2: return "AESv2";
    case CFM_AESV3: return "AESv3";
    case CFM_NONE:  return "none";
    default:        return "unknown";
    }
}

const char *pdf_crypt_stream_method(pdf_doc *doc)
{
    return (doc && doc->crypt) ? cfm_name(doc->crypt->stm_cfm) : "none";
}

const char *pdf_crypt_string_method(pdf_doc *doc)
{
    return (doc && doc->crypt) ? cfm_name(doc->crypt->str_cfm) : "none";
}

int pdf_crypt_owner_matched(pdf_doc *doc)
{
    return (doc && doc->crypt) ? doc->crypt->owner_matched : 0;
}

int pdf_crypt_user_matched(pdf_doc *doc)
{
    return (doc && doc->crypt) ? doc->crypt->user_matched : 0;
}

int pdf_crypt_encrypt_metadata(pdf_doc *doc)
{
    return (doc && doc->crypt) ? doc->crypt->encrypt_metadata : 1;
}

const char *pdf_crypt_user_password(pdf_doc *doc)
{
    return (doc && doc->crypt) ? doc->crypt->userpw : "";
}

int pdf_crypt_perms_bad(pdf_doc *doc)
{
    return (doc && doc->crypt) ? doc->crypt->perms_bad : 0;
}

int pdf_crypt_encrypt_objnum(pdf_doc *doc)
{
    return (doc && doc->crypt) ? doc->crypt->encnum : -1;
}

int pdf_crypt_v5_malformed(pdf_doc *doc)
{
    return (doc && doc->crypt) ? doc->crypt->v5_malformed : 0;
}

const unsigned char *pdf_crypt_key(pdf_doc *doc, size_t *len)
{
    if (len) *len = 0;
    if (!doc || !doc->crypt || !doc->crypt->ready)
        return NULL;            /* nothing has proved a key yet */
    /* Its width is the revision's, not a constant: five bytes at /V 1, the
     * /Length at /V 2 and 4, and thirty-two at /V 5 whatever /Length says. */
    if (doc->crypt->v == 5) {
        if (len) *len = sizeof doc->crypt->key256;
        return doc->crypt->key256;
    }
    if (len) *len = (size_t)doc->crypt->keylen;
    return doc->crypt->key;
}

int pdf_crypt_init(pdf_doc *doc, pdf_obj *encrypt, pdf_obj *id0)
{
    struct pdf_crypt *c;
    pdf_obj *o;

    (void)id0;
    if (!doc || !pdf_is_dict(encrypt)) return 0;

    c = pdf_calloc(doc, 1, sizeof *c);
    if (!c) return PDF_E_NOMEM;

    o = pdf_dict_get(doc, encrypt, "V");
    c->v = pdf_is_num(o) ? (int)pdf_to_int(o) : 0;
    o = pdf_dict_get(doc, encrypt, "R");
    c->r = pdf_is_num(o) ? (int)pdf_to_int(o) : 0;
    o = pdf_dict_get(doc, encrypt, "P");
    c->p = pdf_is_num(o) ? pdf_to_int(o) : 0;
    o = pdf_dict_get(doc, encrypt, "Length");
    c->length_bits = pdf_is_num(o) ? (int)pdf_to_int(o) : 40;
    detect_method(doc, c, encrypt);

    doc->crypt = c;
    doc->encrypted = 1;
    doc->unlocked = 0;
    return crypt_setup(doc, c, encrypt, id0);
    return PDF_E_UNSUP;
}

int pdf_crypt_authenticate(pdf_doc *doc, const char *pw)
{
    (void)pw;
    if (!doc || !doc->crypt) return 0;     /* nothing to unlock */
    return crypt_auth(doc, doc->crypt, pw ? pw : "");
    return PDF_E_UNSUP;
}

int pdf_crypt_data(pdf_doc *doc, int num, int gen, int is_string,
                   const unsigned char *src, size_t srclen, pdf_buf *out)
{
    (void)num; (void)gen; (void)is_string;
    if (!out) return PDF_E_RANGE;
    /* No handler, or an unencrypted document: the bytes are already plaintext. */
    if (!doc || !doc->crypt)
        return pdf_buf_put(out, src, srclen);
    if (doc->crypt->ready)
        return crypt_apply(doc, num, gen,
                           is_string ? doc->crypt->str_cfm : doc->crypt->stm_cfm,
                           src, srclen, out);
    return PDF_E_ENCRYPT;
}

int pdf_crypt_revision(pdf_doc *doc)
{
    return (doc && doc->crypt) ? doc->crypt->r : 0;
}

int pdf_crypt_version(pdf_doc *doc)
{
    return (doc && doc->crypt) ? doc->crypt->v : 0;
}

long long pdf_crypt_perms(pdf_doc *doc)
{
    return (doc && doc->crypt) ? doc->crypt->p : 0;
}

const char *pdf_crypt_method(pdf_doc *doc)
{
    return (doc && doc->crypt) ? doc->crypt->method : "None";
}

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

static int crypt_auth(pdf_doc *doc, struct pdf_crypt *c, const char *pw)
{
    unsigned char key[16];

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

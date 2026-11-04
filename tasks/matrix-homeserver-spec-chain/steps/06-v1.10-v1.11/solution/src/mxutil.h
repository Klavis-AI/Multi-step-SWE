/* Buffers, encodings, hashing and Ed25519 signing. */
#ifndef MXUTIL_H
#define MXUTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* growable byte buffer */
typedef struct { char *b; size_t n, cap; } buf;
void buf_free(buf *s);
void buf_add(buf *s, const void *p, size_t n);
void buf_puts(buf *s, const char *p);
void buf_printf(buf *s, const char *fmt, ...);
char *buf_take(buf *s, size_t *n);      /* NUL-terminated; caller frees */

/* encodings.  "unpadded" drops '='; "url" uses the RFC 4648 §5 alphabet. */
char *b64_encode(const void *data, size_t n, bool url, bool padded);
unsigned char *b64_decode(const char *s, size_t *out_n);   /* accepts both alphabets */
char *hex_encode(const void *data, size_t n);
char *url_decode(const char *s, size_t n, size_t *out_n);
char *url_encode(const char *s);

/* hashing */
void sha256(const void *data, size_t n, unsigned char out[32]);
void hmac_sha256(const void *key, size_t klen, const void *data, size_t n,
                 unsigned char out[32]);

/* Ed25519 (OpenSSL EVP).  Keys are the raw 32-byte seed / public key. */
bool ed25519_public_from_seed(const unsigned char seed[32], unsigned char pub[32]);
bool ed25519_sign(const unsigned char seed[32], const void *msg, size_t n,
                  unsigned char sig[64]);
bool ed25519_verify(const unsigned char pub[32], const void *msg, size_t n,
                    const unsigned char sig[64]);

/* misc */
int64_t now_ms(void);
void random_bytes(void *out, size_t n);
char *random_string(size_t nchars);        /* [A-Za-z] */
char *xstrdup_(const char *s);
bool str_eq(const char *a, const char *b);
bool str_prefix(const char *s, const char *prefix);
char *str_fmt(const char *fmt, ...);
/* Matrix identifier helpers */
bool valid_localpart(const char *s);       /* user id localpart grammar */
bool valid_user_id(const char *s);
const char *user_id_server(const char *user_id);  /* pointer after the ':' */

#endif

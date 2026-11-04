/* sha256.h -- SHA-256 and HMAC-SHA256, for the JWS signatures SMART tokens use. */
#ifndef FHIR_SHA256_H
#define FHIR_SHA256_H

#include <stddef.h>
#include <stdint.h>

void sha256(const void *data, size_t len, uint8_t out[32]);
void hmac_sha256(const void *key, size_t keylen, const void *data, size_t len,
                 uint8_t out[32]);

/* base64url without padding, as JOSE uses it.  `decode` returns the length or
   -1; `out` must have room for 3*(len/4)+3 bytes. */
size_t b64url_encode(const uint8_t *in, size_t len, char *out);
long   b64url_decode(const char *in, size_t len, uint8_t *out);

#endif

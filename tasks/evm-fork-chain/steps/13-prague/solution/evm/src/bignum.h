#ifndef BIGNUM_H
#define BIGNUM_H
#include <stdint.h>
#include <stddef.h>
/* big-endian byte-string modular exponentiation: out = base^exp mod mod.
 * out has `modlen` bytes.  Returns 0 on success. */
int bn_modexp(const uint8_t *base, size_t blen, const uint8_t *exp, size_t elen,
              const uint8_t *mod, size_t mlen, uint8_t *out);
#endif

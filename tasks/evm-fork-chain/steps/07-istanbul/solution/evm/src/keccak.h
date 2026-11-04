#ifndef KECCAK_H
#define KECCAK_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
void keccak256(const uint8_t *in, size_t len, uint8_t out[32]);
#endif

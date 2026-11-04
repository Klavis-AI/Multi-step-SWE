/* HPACK (RFC 7541) decoder used by both the gRPC client and server. */
#ifndef HPACK_H
#define HPACK_H
#include <stddef.h>

typedef struct { char *n, *v; size_t sz; } Ent;
typedef struct { Ent e[256]; int n; size_t size, cap; } Dyn;

/* Decodes one header block. Returns 0 on success, -1 malformed, -2 Huffman
 * (detected and refused rather than silently mis-decoded). Emits through the
 * callback set by hpack_on_header. */
int hpack_decode(const unsigned char *b, size_t n, Dyn *d, int *cnt);
void hpack_on_header(void (*cb)(const char *n, const char *v, void *u), void *u);
#endif

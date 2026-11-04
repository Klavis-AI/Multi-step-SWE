#ifndef HEXUTIL_H
#define HEXUTIL_H
#include "json.h"
#include "types.h"
#include "buf.h"

int hex_decode(const char *s, buf_t *out);          /* accepts optional 0x prefix */
int jnum_u64(const jval *v, uint64_t *out);         /* hex 0x.. or decimal */
int jnum_u256(const jval *v, u256 *out);
int jnum_i64(const jval *v, int64_t *out);
int jaddr(const jval *v, address *out);
int jhash(const jval *v, hash256 *out);
int jbytes(const jval *v, buf_t *out);

void wr_u64(buf_t *o, uint64_t v);                  /* "0x1f" minimal */
void wr_u256(buf_t *o, u256 v);
void wr_bytes(buf_t *o, const uint8_t *p, size_t n);/* "0x…" (empty -> "0x") */
void wr_addr(buf_t *o, address a);
void wr_hash(buf_t *o, hash256 h);
void wr_u64_q(buf_t *o, uint64_t v);                /* quoted */
void wr_u256_q(buf_t *o, u256 v);
void wr_bytes_q(buf_t *o, const uint8_t *p, size_t n);
void wr_addr_q(buf_t *o, address a);
void wr_hash_q(buf_t *o, hash256 h);
#endif

#ifndef RLP_H
#define RLP_H
#include "buf.h"
#include "u256.h"
#include <stdbool.h>

/* --- encoding --- */
void rlp_put_header(buf_t *out, size_t len, uint8_t short_off, uint8_t long_off);
void rlp_put_bytes(buf_t *out, const uint8_t *b, size_t len);
void rlp_put_u64(buf_t *out, uint64_t v);
void rlp_put_u256(buf_t *out, u256 v);
/* append `payload` as a list item to out */
void rlp_put_list(buf_t *out, const uint8_t *payload, size_t len);

/* --- decoding --- */
typedef struct {
    bool is_list;
    const uint8_t *payload;   /* content (for a string: the bytes; for a list: the items) */
    size_t payload_len;
    size_t total_len;         /* bytes consumed including the header */
} rlp_item;

/* returns 0 on success, -1 on malformed input (canonical encoding enforced) */
int rlp_decode(const uint8_t *p, size_t len, rlp_item *out);

/* iterate items of a list payload */
typedef struct { const uint8_t *p; size_t left; } rlp_iter;
static inline void rlp_iter_init(rlp_iter *it, const rlp_item *list) { it->p = list->payload; it->left = list->payload_len; }
static inline bool rlp_iter_done(const rlp_iter *it) { return it->left == 0; }
int rlp_iter_next(rlp_iter *it, rlp_item *out);

int rlp_as_u64(const rlp_item *it, uint64_t *out);
int rlp_as_u256(const rlp_item *it, u256 *out);
int rlp_count(const rlp_item *list);
#endif

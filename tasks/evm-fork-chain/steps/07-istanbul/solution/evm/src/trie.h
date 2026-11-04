#ifndef TRIE_H
#define TRIE_H
#include "types.h"
#include "buf.h"

typedef struct {
    uint8_t key[32];      /* fixed-width keys (keccak of address / storage slot / rlp(index)) */
    size_t klen;
    uint8_t *val;
    size_t vlen;
} trie_kv;

/* Merkle-Patricia root of the given key/value set.  Entries are sorted in place. */
void trie_root(trie_kv *kvs, size_t n, hash256 *out);

/* root of a list-indexed trie: key = rlp(i), value = v[i] (transactions, receipts, withdrawals) */
void trie_root_list(const buf_t *values, size_t n, hash256 *out);

extern const hash256 EMPTY_TRIE_ROOT;
extern const hash256 EMPTY_CODE_HASH;
#endif

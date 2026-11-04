#ifndef TX_H
#define TX_H
#include "types.h"
#include "buf.h"
#include "fork.h"
#include "json.h"
#include <stdbool.h>

typedef struct { address addr; hash256 *keys; size_t nkeys; } access_tuple;

typedef struct {
    u256 chain_id;
    address addr;
    uint64_t nonce;
    int y;
    u256 r, s;
} authorization;

typedef struct {
    uint8_t type;              /* 0,1,2,3,4 */
    u256 chain_id;             /* typed txs and EIP-155 legacy */
    bool has_chain_id;
    uint64_t nonce;
    u256 gas_price;            /* legacy / 2930 */
    u256 max_fee, max_prio;    /* 1559+ */
    uint64_t gas;
    bool has_to;
    address to;
    u256 value;
    uint8_t *data;
    size_t data_len;
    access_tuple *al;
    size_t nal;
    u256 max_fee_blob;
    hash256 *blob_hashes;
    size_t nblobs;
    authorization *auths;
    size_t nauths;
    u256 v, r, s;
    address sender;
    bool over256;               /* an integer field needed more than 32 bytes */
    bool have_sender;
    hash256 hash;
    uint8_t *raw;
    size_t raw_len;
} tx_t;

void tx_free(tx_t *t);
/* decode one transaction from its canonical network encoding */
int tx_decode(const uint8_t *p, size_t len, tx_t *out);
/* build the canonical encoding from an already-populated tx (signature included) */
void tx_encode(const tx_t *t, buf_t *out);
/* recover the sender; returns 0 on success */
int tx_recover(tx_t *t, fork_id fk, u256 chain_id);
/* signing hash for the given transaction */
void tx_signing_hash(const tx_t *t, u256 chain_id, bool eip155, hash256 *out);
/* sign an unsigned transaction with `key` (used for JSON input with secretKey) */
int tx_sign(tx_t *t, const uint8_t key[32], u256 chain_id, bool eip155);
uint64_t tx_intrinsic_gas(const tx_t *t, fork_id fk, uint64_t *floor_out);
void tx_compute_hash(tx_t *t);
#endif

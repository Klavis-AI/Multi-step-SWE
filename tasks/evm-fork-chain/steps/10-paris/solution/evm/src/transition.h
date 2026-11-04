#ifndef TRANSITION_H
#define TRANSITION_H
#include "types.h"
#include "state.h"
#include "evm.h"
#include "tx.h"

typedef struct { uint64_t delta; address addr; } ommer_t;
typedef struct { uint64_t index, validator_index; address recipient; u256 amount; } withdrawal_t;

typedef struct {
    address coinbase;
    uint64_t gas_limit, number, timestamp;
    u256 difficulty;      bool has_difficulty;
    hash256 random;       bool has_random;
    u256 base_fee;        bool has_base_fee;
    u256 parent_difficulty; bool has_parent_difficulty;
    uint64_t parent_timestamp; bool has_parent_timestamp;
    uint64_t parent_gas_used, parent_gas_limit;
    hash256 parent_uncle_hash; bool has_parent_uncle_hash;
    u256 parent_base_fee; bool has_parent_base_fee;
    uint64_t parent_excess_blob_gas; bool has_peb;
    uint64_t parent_blob_gas_used;   bool has_pbgu;
    uint64_t excess_blob_gas;        bool has_ebg;
    hash256 parent_beacon_block_root; bool has_pbbr;
    hash256 parent_hash;  bool has_parent_hash;
    blockhash_entry *bhashes; size_t nbhashes;
    ommer_t *ommers; size_t nommers;
    withdrawal_t *withdrawals; size_t nwithdrawals; bool has_withdrawals;
} env_t;

typedef struct {
    uint8_t type;
    bool has_status;
    uint8_t status;
    hash256 post_root;
    uint64_t cumulative_gas;
    uint8_t bloom[256];
    evm_log *logs;
    size_t nlogs;
    hash256 tx_hash;
    address contract_address;
    bool has_contract;
    uint64_t gas_used;
    uint64_t index;
} receipt_t;

typedef struct { size_t index; char *error; } rejected_t;

typedef struct {
    hash256 state_root, tx_root, receipts_root, logs_hash;
    uint8_t logs_bloom[256];
    receipt_t *receipts; size_t nreceipts;
    rejected_t *rejected; size_t nrejected;
    u256 difficulty; bool has_difficulty;
    uint64_t gas_used;
    u256 base_fee; bool has_base_fee;
    hash256 withdrawals_root; bool has_withdrawals_root;
    uint64_t blob_gas_used; bool has_blob_gas_used;
    uint64_t excess_blob_gas; bool has_excess_blob_gas;
    hash256 requests_hash; bool has_requests_hash;
    buf_t *requests; size_t nrequests;
    buf_t body;                       /* rlp list of included transactions */
} result_t;

int apply_block(state *st, env_t *env, tx_t *txs, size_t ntxs, fork_id fk,
                u256 chain_id, int64_t reward, result_t *out);
void bloom_add(uint8_t bloom[256], const uint8_t *data, size_t len);
#endif

#ifndef EVM_H
#define EVM_H
#include "types.h"
#include "state.h"
#include "fork.h"
#include "buf.h"

typedef struct { uint64_t num; hash256 h; } blockhash_entry;

typedef struct {
    fork_id fork;
    u256 chain_id;
    address coinbase;
    uint64_t number;
    uint64_t timestamp;
    uint64_t gas_limit;
    u256 difficulty;          /* pre-merge value returned by DIFFICULTY */
    hash256 prevrandao;       /* post-merge value returned by PREVRANDAO */
    bool post_merge;
    u256 base_fee;
    uint64_t excess_blob_gas;
    u256 blob_base_fee;
    blockhash_entry *bhashes;
    size_t nbhashes;
    bool bhash_missing;       /* set when BLOCKHASH needed an absent entry */
} block_env;

typedef struct {
    address origin;
    u256 gas_price;
    hash256 *blob_hashes;
    size_t nblobs;
} tx_env;

typedef struct {
    state *st;
    block_env *blk;
    tx_env *tx;
    fork_id fork;
    int depth;
    uint64_t precompile_calls;
} vm_t;

typedef enum { EX_OK = 0, EX_REVERT = 1, EX_HALT = 2 } ex_status;

typedef struct {
    ex_status status;
    uint64_t gas_left;
    uint8_t *ret;
    size_t ret_len;
    address created;          /* for creates */
    /* A code-deposit failure before Homestead keeps the account and returns the gas, and the
     * CREATE opcode then pushes the new address as though it had succeeded -- but the
     * transaction that contains it is still reported as failed.  The two need telling apart. */
    bool deposit_oog;
} ex_result;

typedef enum { CK_CALL = 0, CK_CALLCODE, CK_DELEGATE, CK_STATIC, CK_TX } call_kind;

typedef struct {
    call_kind kind;
    address caller;
    address self;
    address code_addr;
    u256 value;
    bool transfer_value;
    const uint8_t *input;
    size_t ilen;
    uint64_t gas;
    bool is_static;
} call_args;

ex_result evm_call(vm_t *vm, const call_args *a);
ex_result evm_create(vm_t *vm, address creator, u256 value, const uint8_t *init,
                     size_t ilen, uint64_t gas, bool create2, u256 salt);

/* precompiles: returns true when `addr` is a precompile at this fork */
bool is_precompile(fork_id f, address a);
ex_result run_precompile(vm_t *vm, address a, const uint8_t *in, size_t ilen, uint64_t gas);

/* helpers shared with the transaction layer */
address create_address(address sender, uint64_t nonce);
address create2_address(address sender, u256 salt, const uint8_t *init, size_t ilen);
uint64_t memory_gas(uint64_t words);

#define MAX_CODE_SIZE   24576
#define MAX_INITCODE    49152
#define STACK_LIMIT     1024
#define CALL_DEPTH_LIMIT 1024
#endif

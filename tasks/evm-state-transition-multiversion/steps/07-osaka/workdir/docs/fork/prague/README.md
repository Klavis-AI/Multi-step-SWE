# Prague

**Activation**: Timestamp-based (hypothetical mainnet timestamp, February 2025 deployment).

**EIP files in this directory**: 2537, 2935, 6110, 7002, 7251, 7623, 7685, 7702.

## State-transition consequences

**EIP-2537** (Precompile for BLS12-381 curve operations): Seven new precompiles at addresses `0x0b` through `0x11` implement BLS12-381 elliptic-curve operations (point addition, multi-scalar multiplication, pairing check, and map-to-curve for both the G1 and the G2 group); `eip-2537.md`'s own parameter table is the authoritative list of the seven. Transactions that call these precompiles pay different gas and produce results that affect `gasUsed` and execution state.

**EIP-2935** (Serve historical block hashes from state): A system call made at the start of every block, before any transaction, stores the parent block hash into a ring buffer held by the history contract at `0x0000F90827F1C53a10cb7A02335B175320002935`, at slot `(block.number - 1) % 8191`. A contract that calls that address with a block number in `[block.number - 8191, block.number - 1]` is served the hash from state, and is reverted on anything outside that window. The **`BLOCKHASH` opcode itself does not change**: same 256-block window, same cost, same return value. This EIP adds a contract; it does not re-point the opcode. The observable consequences at the `t8n` boundary are the ring-buffer slot in the post-state `alloc` and `stateRoot`, and what the history contract returns to a transaction that calls it.

**EIP-6110** (Supply validator deposits on chain): Deposit contract logs are parsed into a structured request list that is hashed into `result.requestsHash` alongside withdrawal and consolidation requests (see EIP-7685).

**EIP-7002** (Execution layer triggerable withdrawals): Withdrawal requests submitted through a system contract are aggregated and included in `result.requestsHash`, enabling validator exits triggered from the execution layer.

**EIP-7251** (Increase the MAX_EFFECTIVE_BALANCE): Consolidation requests submitted through a system contract are aggregated and included in `result.requestsHash`. This EIP enables higher validator effective balances and validator consolidation, observable through the requests hash.

**EIP-7623** (Increase calldata cost): A transaction's intrinsic gas becomes the **maximum** of the ordinary intrinsic cost and a floor derived from its calldata token count: `floor = 21000 + TOTAL_COST_FLOOR_PER_TOKEN * tokens_in_calldata`, where `tokens_in_calldata = zero_bytes + 4 * nonzero_bytes`, `STANDARD_TOKEN_COST` is 4 and `TOTAL_COST_FLOOR_PER_TOKEN` is 10. Transactions with large calldata relative to their execution cost pay more intrinsic gas, increasing `gasUsed` and potentially causing previously-valid transactions to run out of gas.

**EIP-7691** (Blob throughput increase — **no `eip-7691.md` in this directory**; it restates Cancun parameters rather than adding a document): Prague raises the EIP-4844 blob schedule. `MAX_BLOB_GAS_PER_BLOCK` becomes **1,179,648** (nine blobs) where Cancun's is 786,432, `TARGET_BLOB_GAS_PER_BLOCK` becomes **786,432** (six), and `BLOB_BASE_FEE_UPDATE_FRACTION` becomes **5,007,716**; `GAS_PER_BLOB` (131,072) and `MIN_BASE_FEE_PER_BLOB_GAS` are unchanged. The observable consequence at the `t8n` boundary is the block's blob-gas budget: the per-transaction exclusion rule that Cancun applies against the running total now admits up to **nine** blobs per block, so a blob transaction carrying nine versioned hashes is **included** at Prague where Cancun excludes it. Step 6's `instruction.md` carries the same three numbers under its own EIP-7691 bullet.

**EIP-7685** (General purpose execution layer requests): Deposit, withdrawal, and consolidation requests are concatenated and hashed to produce `result.requestsHash`, a new header field that commits to the request list.

**EIP-7702** (Set EOA code): Type-4 transactions (`0x04 || rlp([chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, to, value, data, accessList, authorizationList, ...])`) carry a list of signed authorizations. Each authorization allows the transaction to set the signer's account code to a delegation designator `0xef0100 || address`, so that a later call to that account executes the target's code in the account's own context. The designator is **persistent**: it is written to the authority's account, survives the transaction that wrote it, is not rolled back when that transaction reverts, and shows up in the post-state `alloc` and in `stateRoot` until another authorization replaces or clears it. This charges per-authorization gas, validates each authority's nonce, and affects execution outcomes and `stateRoot`.

## What is NOT in scope at this fork

- Consensus-layer validator duties, deposits, withdrawals, and consolidations queue management.
- Beacon chain state and its coordination with execution payloads.
- Engine API and communication with a consensus client.
- Peer-to-peer networking (devp2p, transaction and block propagation).
- JSON-RPC interfaces beyond the `t8n`/`t9n`/`b11r` CLI surface.

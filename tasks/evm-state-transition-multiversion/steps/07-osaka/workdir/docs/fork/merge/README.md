# Merge (Paris)

**Activation**: Terminal Total Difficulty (TTD) transition (mainnet September 2022). Post-merge blocks use proof-of-stake and omit difficulty-based validation.

**EIP files in this directory**: 3675, 4399.

## State-transition consequences

**EIP-3675** (Upgrade consensus to Proof-of-Stake): The block reward becomes zero — the tool no longer credits 2 ETH (or any fixed amount) to the coinbase for producing the block; the coinbase receives only transaction fees. The `--state.reward=-1` flag signals "no block reward accounting," which is the only mode the graded interface exercises. `env.currentDifficulty` may be absent or zero in post-merge blocks, and the tool must handle its absence without error.

**EIP-4399** (Supplant DIFFICULTY opcode with PREVRANDAO): The `DIFFICULTY` opcode (`0x44`) now reads `env.currentRandom` (the beacon chain's RANDAO value) instead of `env.currentDifficulty`. Contracts that execute `DIFFICULTY` see the new PREVRANDAO value on the stack, changing execution outcomes and `stateRoot` for contracts that depend on this value.

## What is NOT in scope at this fork

- Consensus-layer proof-of-stake (validator duties, attestations, slashing, finality gadget).
- Beacon chain state and its block production.
- Engine API methods (`engine_newPayloadV1`, `engine_forkchoiceUpdatedV1`) used to coordinate execution and consensus clients.
- Peer-to-peer networking (devp2p, transaction and block propagation).
- JSON-RPC interfaces beyond the `t8n`/`t9n`/`b11r` CLI surface.

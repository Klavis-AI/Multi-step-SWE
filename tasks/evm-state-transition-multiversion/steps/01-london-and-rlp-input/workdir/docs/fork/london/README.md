# London

**Activation**: Block-number based (mainnet block 12,965,000, August 2021).

**EIP files in this directory**: 1559, 3198, 3529, 3541.

## State-transition consequences

**EIP-1559** (Fee market change): The tool must accept `env.currentBaseFee` and process type-2 transactions (`0x02 || rlp([chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, ...])`) with a dynamic base fee. The effective gas price is `min(maxFeePerGas, baseFee + maxPriorityFeePerGas)`, the base-fee portion is burned (never credited to the coinbase), and only the priority fee goes to the miner. A type-2 transaction with `maxFeePerGas < baseFee` is rejected. This changes sender and coinbase balances, affecting `stateRoot` and `gasUsed`.

**EIP-3198** (BASEFEE opcode): The `BASEFEE` instruction (`0x48`) pushes `env.currentBaseFee` onto the stack, allowing contracts to read the current block's base fee and changing execution outcomes for contracts that use it.

**EIP-3529** (Reduction in refunds): Gas refunds are capped at `gasUsed / 5` (previously `gasUsed / 2`), `SSTORE` refunds are reduced, and `SELFDESTRUCT` no longer grants any refund. This increases net `gasUsed` for transactions that previously received large refunds and changes the sender's final balance.

**EIP-3541** (Reject new contract code starting with `0xEF`): A contract creation — a creation transaction, or a `CREATE`/`CREATE2` operation — whose *deployed* code would begin with byte `0xEF` fails with an **exceptional abort**: no contract is deployed and every unit of gas given to that creation is consumed. This is an execution failure, not a pre-execution validity failure, so it does **not** put anything in `result.rejected[]`. `rejected[]` carries only transactions the tool declines to execute at all (bad signature, wrong nonce, intrinsic gas above the gas limit, balance below the up-front cost). An EIP-3541 abort inside a `CREATE`/`CREATE2` leaves the calling frame running with zero pushed in place of the new address; an EIP-3541 abort in a creation *transaction* still yields an included transaction with a receipt whose `status` is `0x0`. In both shapes the change is visible in `stateRoot` — no new code account, and the gas is gone.

## What is NOT in scope at this fork

- Consensus-layer state (validator sets, beacon chain, finality).
- Peer-to-peer networking (devp2p, transaction and block propagation).
- Engine API and communication with a consensus client.
- Block-difficulty-bomb or ice-age delay EIPs (EIP-3554).
- JSON-RPC interfaces beyond the `t8n`/`t9n`/`b11r` CLI surface.

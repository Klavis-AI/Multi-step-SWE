# Shanghai

**Activation**: Timestamp-based (mainnet timestamp 1681338455, April 2023). This is the first fork to activate by timestamp rather than block number.

**EIP files in this directory**: 3651, 3855, 3860, 4895.

## State-transition consequences

**EIP-3651** (Warm COINBASE): The coinbase address (block producer, `env.currentCoinbase`) is placed in the warm access set at transaction start. The first `COINBASE`, `BALANCE(coinbase)`, or `CALL`-family operation targeting the coinbase costs warm-access gas instead of cold-access gas, reducing `gasUsed` for transactions that touch the coinbase early.

**EIP-3855** (PUSH0 instruction): The `PUSH0` opcode (`0x5f`) pushes a zero onto the stack at a cost of 2 gas, cheaper than `PUSH1 0x00` (3 gas). Contracts using `PUSH0` have different bytecode and lower execution cost, affecting `gasUsed` and enabling previously-invalid bytecode patterns.

**EIP-3860** (Limit and meter initcode): Contract creation (via transaction `to == null` or `CREATE`/`CREATE2`) now charges 2 gas per 32-byte word of initcode and enforces a hard limit of 49,152 bytes of initcode. Transactions or operations that exceed this limit are rejected, and those with large initcode pay more gas, both affecting `gasUsed` and `result.rejected[]`.

**EIP-4895** (Beacon chain push withdrawals): The tool must process `env.withdrawals[]`, an array of `{index, validatorIndex, address, amount}` records. Each withdrawal credits `amount` (in gwei) to the recipient `address` as a system-level balance increase that occurs outside any transaction. The tool computes `result.withdrawalsRoot`, the Merkle root of the withdrawals list, and the post-state `alloc` reflects the credited balances in `stateRoot`.

## What is NOT in scope at this fork

- Consensus-layer validator withdrawals queue and eligibility rules.
- Beacon chain state and its coordination with execution payloads.
- Engine API and communication with a consensus client.
- Peer-to-peer networking (devp2p, transaction and block propagation).
- JSON-RPC interfaces beyond the `t8n`/`t9n`/`b11r` CLI surface.

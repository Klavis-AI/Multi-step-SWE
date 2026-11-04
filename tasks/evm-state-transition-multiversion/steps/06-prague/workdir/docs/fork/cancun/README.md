# Cancun

**Activation**: Timestamp-based (mainnet timestamp 1710338135, March 2024).

**EIP files in this directory**: 1153, 4788, 4844, 5656, 6780, 7516.

## State-transition consequences

**EIP-1153** (Transient storage opcodes): The `TSTORE` (`0x5d`) and `TLOAD` (`0x5c`) opcodes provide transient storage that persists within a transaction but is cleared before the next transaction begins. Transient storage survives between calls in the same transaction and reverts with the call frame on revert, but does not persist to disk. This changes execution behavior and `gasUsed` for contracts that use transient state.

**EIP-4788** (Beacon block root in the EVM): Beginning at this fork every block carries a `parent_beacon_block_root`, and the execution layer makes a system call at the **start of the block, before any transaction**, to the beacon-roots contract at `BEACON_ROOTS_ADDRESS` = `0x000F3df6D732807Ef1319fB7B8bB8522d0Beac02` with the caller set to `SYSTEM_ADDRESS` = `0xfffffffffffffffffffffffffffffffffffffffe`, 30,000,000 gas, zero value, and the 32-byte root as calldata. Because the caller is `SYSTEM_ADDRESS` the contract takes its `set` branch and writes two slots of an 8191-entry ring buffer: `storage[timestamp % 8191] = timestamp` and `storage[timestamp % 8191 + 8191] = parent_beacon_block_root`. At the `t8n` boundary the root arrives as `env.parentBeaconBlockRoot` and the timestamp as `env.currentTimestamp`, so those two writes appear in the post-state `alloc` and in `stateRoot` for every block whose pre-state carries code at that address. As with every system call, no value is transferred, the gas is not charged to the block, and the call fails silently when the address holds no code. No scored group in this task is named for EIP-4788; it is observed only through the post-state of the blockchain-derived corpora, which from step 6 onward ship that contract with its runtime code.

**EIP-4844** (Shard Blob Transactions): Type-3 transactions (`0x03 || rlp([chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, to, value, data, accessList, maxFeePerBlobGas, blobVersionedHashes, ...])`) carry blob commitments. The tool must process `env.currentExcessBlobGas` to compute the blob base fee, validate that `maxFeePerBlobGas >= blobBaseFee`, charge `blobGasUsed = len(blobVersionedHashes) * 0x20000` blob gas, burn the blob fee (`blobGasUsed * blobBaseFee`) from the sender, emit `result.blobGasUsed` and `result.currentExcessBlobGas`, and support the `BLOBHASH` opcode (`0x49`) and the KZG point-evaluation precompile at address `0x0a`.

**EIP-5656** (MCOPY - Memory copying instruction): The `MCOPY` opcode (`0x5e`) copies a memory region to another memory region more efficiently than a loop of `MLOAD`/`MSTORE`. It reduces `gasUsed` for memory copies and enables previously-invalid bytecode.

**EIP-6780** (SELFDESTRUCT only in same transaction): `SELFDESTRUCT` now deletes the account only when the contract was created in the same transaction; otherwise it only sweeps the account's balance to the beneficiary and the account survives. This dramatically changes the post-state `alloc` and `stateRoot` for contracts that call `SELFDESTRUCT`.

**EIP-7516** (BLOBBASEFEE instruction): The `BLOBBASEFEE` opcode (`0x4a`) pushes the current blob base fee onto the stack, allowing contracts to read `blobBaseFee` and changing execution outcomes for contracts that use it.

## What is NOT in scope at this fork

- Blob data availability, blob pool, and peer-to-peer blob propagation.
- Consensus-layer beacon chain state and coordination.
- Engine API and communication with a consensus client.
- JSON-RPC interfaces beyond the `t8n`/`t9n`/`b11r` CLI surface.

# Osaka

**Activation**: Timestamp-based (hypothetical mainnet timestamp, August 2025 deployment).

**EIP files in this directory**: 7823, 7825, 7883, 7918, 7939, 7951.

## State-transition consequences

**EIP-7825** (Transaction Gas Limit Cap): Transactions with `gasLimit > 16,777,216` are rejected regardless of the block gas limit. This is a new per-transaction validity rule that appears in `result.rejected[]` for oversized transactions. Transactions that were valid under all previous forks may now be rejected.

**EIP-7883** (ModExp Gas Cost Increase): The `0x05` precompile (modular exponentiation) uses a revised gas-cost formula with a higher minimum cost and higher per-word costs. Transactions calling this precompile pay more gas than they did at Berlin or earlier, increasing `gasUsed`.

**EIP-7823** (Set upper bounds for MODEXP): Each of the three length fields at the head of the `0x05` precompile's input — `length_of_BASE`, `length_of_EXPONENT` and `length_of_MODULUS` — is bounded at 8192 bits (1024 bytes). An input whose declared length exceeds the bound makes the precompile stop, return an error and consume all gas, so the calling frame sees a failed `CALL` with empty return data rather than a truncated computation. The bound is on the declared lengths, not on the input's total size.

**EIP-7918** (Blob base fee bounded by execution cost): The blob base fee has a floor value tied to the execution-layer base fee, ensuring that blob gas is not underpriced relative to execution gas. This changes the blob base fee calculation, affecting blob transaction validity (when `maxFeePerBlobGas < blobBaseFee`) and the blob fee burned, both influencing `gasUsed` and sender balance.

**EIP-7939** (Count leading zeros (CLZ) opcode): The `CLZ` instruction (`0x1e`) counts the number of leading zero bits in a 256-bit word and pushes the result onto the stack. Contracts using this opcode have different bytecode and execution cost, enabling previously-invalid code patterns.

**EIP-7951** (Precompile for secp256r1 Curve Support): A new precompile at address `0x0100` implements signature verification for the secp256r1 (P-256) elliptic curve, widely used in passkeys and secure hardware. Transactions that call this precompile pay for the verification and receive a success/failure result, affecting `gasUsed` and execution state.

## What is NOT in scope at this fork

- **EIP-7594** (PeerDAS, Peer Data Availability Sampling): This is a consensus-layer and peer-to-peer networking change that does not affect the state-transition function exercised by `t8n`. It is explicitly **out of scope** for this task package.
- Consensus-layer beacon chain state and coordination.
- Engine API and communication with a consensus client.
- Peer-to-peer networking (devp2p, transaction and block propagation).
- JSON-RPC interfaces beyond the `t8n`/`t9n`/`b11r` CLI surface.

# Berlin

**Activation**: Block-number based (mainnet block 12,244,000, April 2021).

**EIP files in this directory**: 155, 2565, 2718, 2929, 2930.

## State-transition consequences

**EIP-155** (Simple replay attack protection): The tool must accept `--state.chainid` and transactions must encode the chain ID into their signature `v` value when `v >= 35`, affecting signature validation and sender recovery.

**EIP-2565** (ModExp gas cost): The `0x05` precompile (modular exponentiation) uses a new gas-cost formula that reduces cost for most inputs, changing `gasUsed` for transactions that call it.

**EIP-2718** (Typed transaction envelope): Transactions are now decoded as typed envelopes—legacy transactions remain `0xc0..0xfe` RLP, while type-1 and later begin with a type byte `0x01`, `0x02`, etc., requiring envelope-aware decoding and affecting `txRoot` and sender recovery.

**EIP-2929** (Gas cost increases for state access opcodes): `SLOAD`, `*CALL*`, `BALANCE`, `EXT*`, and `SELFDESTRUCT` have higher gas costs on first ("cold") access per transaction; subsequent ("warm") accesses cost less. This changes `gasUsed` and can cause transactions to run out of gas that previously succeeded.

**EIP-2930** (Optional access lists): Type-1 transactions (`0x01 || rlp([chainId, nonce, gasPrice, gasLimit, to, value, data, accessList, ...])`) carry an `accessList` that pre-warms addresses and storage slots, reducing their access cost and affecting final `gasUsed` and transaction validity (a transaction with insufficient gas for the access-list cost is rejected).

## What is NOT in scope at this fork

- Consensus-layer state (validator sets, beacon chain, finality).
- Peer-to-peer networking (devp2p, transaction and block propagation).
- Engine API and communication with a consensus client.
- Block-difficulty-bomb or ice-age delay EIPs.
- JSON-RPC interfaces beyond the `t8n`/`t9n`/`b11r` CLI surface.

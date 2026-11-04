# Step 5 — Cancun

## 1. Implement this milestone

`/app` is the workspace you delivered at step 4: go-ethereum with a
state-transition tool that is correct through **Shanghai**, carrying the `t9n`
and `b11r` subcommands beside `t8n`.

Carry it forward to **Cancun**. `--state.fork=Cancun` must be accepted and must
mean all five of the EIPs below. Cancun activates by timestamp, as Shanghai
does; that is a chain-configuration detail and nothing the in-scope interface asks
about — every case in scope names its fork through `--state.fork`.

**EIP-4788 is out of scope *at this step*.**
The beacon-root system contract is not exercised by this step's corpora: they
carry no code at `BEACON_ROOTS_ADDRESS`. `--input.env` documents in the in-scope
corpora do carry a `parentBeaconBlockRoot` field, so the tool must parse an env
that contains it without failing; nothing is asserted about its effect. It does
not stay that way — `/app/README.md` and `docs/fork/cancun/README.md` both say
where the EIP becomes observable, and a tool that accepts `parentBeaconBlockRoot`
without ever performing the system call will not match the post-state there.

- **EIP-1153, transient storage.** `TLOAD` (`0x5c`) and `TSTORE` (`0x5d`),
  addressed exactly as `SLOAD`/`SSTORE` are, backed by a store that is private
  to the owning contract and **discarded at the end of every transaction**.
  `TSTORE` costs a warm dirty-slot `SSTORE` (100 gas) and `TLOAD` costs a hot
  `SLOAD` (100 gas), and `TSTORE` carries none of EIP-2200's `gasleft > 2300`
  stipend condition. Ownership follows persistent storage: `DELEGATECALL` and
  `CALLCODE` keep the caller's transient store, `CALL` and `STATICCALL` use the
  callee's. A frame that reverts undoes every transient write made inside it,
  inner calls included. `TSTORE` inside a `STATICCALL` is an exception, not a
  silent no-op; `TLOAD` is permitted there. Both opcodes must remain
  **undefined at every earlier fork**.
- **EIP-5656, `MCOPY`.** A new instruction at `0x5e` taking `dst`, `src`,
  `length` from the top of the stack in that order and returning nothing. The
  copy behaves **as if an intermediate buffer were used**, so overlapping
  regions copy correctly in both directions. It costs
  `G_verylow(3) + 3 * ceil(length / 32)` plus memory expansion, and a
  zero-length `MCOPY` expands no memory however large its offsets. `0x5e` must
  remain invalid at every earlier fork.
- **EIP-6780, `SELFDESTRUCT` only in the same transaction.** If the contract
  executing `SELFDESTRUCT` was **not** created in this transaction, the frame
  halts and the entire balance moves to the target, but nothing is deleted —
  not the account, not its code, not its storage. If it **was** created in this
  transaction, the pre-Cancun behaviour stands and the account is deleted. A
  contract counts as created from the beginning of a creation transaction or
  from the moment a `CREATE`/`CREATE2` begins executing, and a pre-existing
  balance at that address does not change that. Self-targeting splits the same
  way: burnt in the same-transaction case, a no-op in the other, where Ether is
  no longer burnt. No refund in either case, and EIP-2929's warm/cold rules for
  `SELFDESTRUCT` are unchanged. This is checked through the post-state `alloc`
  and `stateRoot`, which is exactly where a surviving-versus-deleted account
  shows up.
- **EIP-7516, `BLOBBASEFEE`.** A new instruction at `0x4a`, no operands, 2 gas,
  pushing the block's blob base fee. EIP-7516 calls that function
  `get_blob_gasprice`; the vendored EIP-4844 text spells the same function
  `get_base_fee_per_blob_gas`, in its **Gas accounting** section. Its value is
  pinned directly, at several values of the block's excess blob gas, including
  the ones that make the exponential's argument zero and the one at
  `TARGET_BLOB_GAS_PER_BLOCK` — a tool that pushes a constant, or that pushes
  `currentBaseFee`, is visible there. `0x4a` must remain invalid at every
  earlier fork.
- **EIP-4844, blob transactions.** The largest change, and the only one that
  widens the tool's input and output documents. It is covered in two independent
  groups: one drives the transaction type through the fixture corpus, the other
  pins the header arithmetic, the fee burn, `BLOBHASH` and the precompile
  directly. All of it is specified in `eip-4844.md`.
  - **The transaction.** Type `0x03`, adding `maxFeePerBlobGas` and
    `blobVersionedHashes` to the type-2 field set. The tool must decode it from
    an RLP `--input.txs` payload **and** from the JSON form, where the two new
    fields are spelled exactly that way and a `secretKey` is signed as for
    every other type. Each versioned hash is a 32-byte value whose first byte
    is `VERSIONED_HASH_VERSION_KZG` (`0x01`).
  - **The second gas market, in `--input.env`.** The block's excess blob gas
    arrives either directly, as `currentExcessBlobGas`, or as the parent pair
    `parentExcessBlobGas` and `parentBlobGasUsed`, from which the tool computes
    it with `calc_excess_blob_gas`. Both forms appear in the cases in scope. The
    running total is **clamped at zero**: a parent whose
    `excess + blob_gas_used` is below `TARGET_BLOB_GAS_PER_BLOCK` yields 0, not
    a negative number and not an underflow.
  - **Two new `--output.result` fields**, `blobGasUsed` and
    `currentExcessBlobGas`, **present from Cancun and absent at every earlier
    fork**. `blobGasUsed` is `GAS_PER_BLOB * (total versioned hashes in the
    block's included transactions)` — `0x0` for a block with no blob
    transaction, and `0x0`, not a missing field. A transaction that is
    *rejected* contributes no blob gas.
  - **The blob fee is burned.** `get_base_fee_per_blob_gas` is
    `fake_exponential(MIN_BASE_FEE_PER_BLOB_GAS, excess_blob_gas,
    BLOB_BASE_FEE_UPDATE_FRACTION)`. The product of that and the transaction's
    blob gas is deducted from the sender **before execution and burned** — the
    coinbase receives none of it, and the sender's post-state balance is
    checked against exactly that deduction. Paying the blob fee to the coinbase
    and burning nothing produces the same `blobGasUsed` and the wrong `alloc`.
  - **`maxFeePerBlobGas` is a validity rule**, `tx.max_fee_per_blob_gas >=
    get_base_fee_per_blob_gas(block.header)`. It is probed at the exact
    boundary from both sides: equal is valid, one below is **invalid**. An
    invalid one is an entry in `rejected[]` with a zero exit status, not a
    tool-level error, and it spends no blob gas.
  - **The block's blob-gas cap is a validity rule too**, and it is applied
    **per transaction against the running total**, in the order `--input.txs`
    gives them: a blob transaction whose blob gas would take the block past
    `MAX_BLOB_GAS_PER_BLOCK` is **excluded** — an entry in `rejected[]` with a
    zero exit status, exactly like the fee-cap rule above, contributing no blob
    gas, no blob fee and nothing to `blobGasUsed`. A later transaction with
    room left is still included, so which transaction is excluded is decided by
    the running total, not by its own size. The vendored EIP-4844 text states
    the same limit as a block-level assertion
    (`assert blob_gas_used <= MAX_BLOB_GAS_PER_BLOCK`); the required behaviour is
    the per-transaction exclusion. **786,432 is Cancun's value of that
    parameter and not a constant of the tool**: Prague raises it to 1,179,648
    (nine blobs) under EIP-7691, and step 6's instruction states the Prague
    schedule when you get there. Nothing at this step asks for the Prague
    value, and nothing here requires it.
  - **`BLOBHASH` (`0x49`)** pops an index and pushes
    `tx.blob_versioned_hashes[index]` when the index is in range and a **zeroed
    bytes32** when it is not. Both halves are in scope.
  - **The point-evaluation precompile at `0x0a`**, costing
    `POINT_EVALUATION_PRECOMPILE_GAS` (50000). Input is exactly 192 bytes,
    `versioned_hash || z || y || commitment || proof`. On success it returns
    the 64 bytes `FIELD_ELEMENTS_PER_BLOB` and `BLS_MODULUS`, each as a padded
    32-byte big-endian value. It must **fail** — consuming its gas and
    returning nothing, so the caller sees a failed `CALL` rather than a
    reverted transaction — on each of the four paths the specification names: a
    length that is not 192, a `versioned_hash` that is not
    `kzg_to_versioned_hash(commitment)`, a `z` or `y` that is not a canonical
    field element (strictly less than `BLS_MODULUS`), and a proof that does not
    verify. All five outcomes are checked separately, and the probe contract
    writes a sentinel so that "the precompile correctly failed" is
    distinguishable from "the code never ran". Verifying a KZG proof needs a
    **trusted setup** beside the verifier itself. That setup is data, not code:
    if the library you pick embeds it there is nothing to do, but if it loads it
    from a file at run time, the file has to be inside the image and reachable
    with no network, because the target run has none. `/app/TEST_INTERFACE.md`
    says the same thing under **Build**.
- **Nothing below Cancun moves.** Two inherited groups re-run the earlier
  ladder, and they are not only state transitions. `inherited-shanghai-t8n`
  holds the Shanghai fixture corpus (RLP input, asserting `stateRoot`,
  `logsHash` and `rejected`), the EIP-4895 **withdrawal** cases from step 4 —
  an `--input.env` carrying a `withdrawals` array, credited in wei, with
  `withdrawalsRoot` in the result — and the **Merge** cases from step 3, at
  `--state.fork=Merge`, for `PREVRANDAO` and for a block that pays no block
  reward. `inherited-london-t8n` holds the London fixture corpus together with
  **`t9n`** cases (all four entry shapes, at `London` and at `Berlin`,
  including the 32000 creation surcharge) and **`b11r`** cases (plain
  assembly and a derived `sha3Uncles`) from step 2. The `-t8n` in the two names
  is historical: it names the fork tier, not the subcommand.

  Everything an earlier step introduced is therefore still observed here. A
  Cancun change that shifts an earlier fork's result is a regression and is
  treated as one: `0x5c`, `0x5d`, `0x5e` or `0x4a` becoming valid at Shanghai is
  checked by four dedicated probes in `inherited-shanghai-t8n`, which call a
  contract whose whole body is that one opcode and store the `CALL` success
  flag; `SELFDESTRUCT` keeping an account alive at London, and the blob result
  fields appearing at a fork that does not define them, are checked too.

  One case in `inherited-shanghai-t8n` is not an earlier step's work coming
  back around; it is an inherited behaviour that no step's documents
  introduced, and it is stated here because this step's gate includes it. The
  **identity precompile at `0x04` returns a copy of its input**, so a call
  whose return region overlaps its argument region is safe: with memory
  `[1,2,3,4]`, a `STATICCALL` to `0x04` with `args_offset=0, args_size=4,
  ret_offset=1, ret_size=4` must leave memory `[1,2,3,4,4]`, not
  `[1,1,2,3,4]`. A tree whose precompile hands back the argument slice itself
  and writes that slice into memory without copying the bytes first produces
  the second image; the case asserts the first through `stateRoot` and the
  post-state `alloc`, and one failing case in a gate group means the step is not
  satisfied.

## 2. Read these local references

**Normative semantics.** `/app/docs/fork/cancun/README.md` first, for what the
fork changes at the `t8n` boundary and for what is explicitly out of scope,
then the five EIP files beside it:

- `/app/docs/fork/cancun/eip-1153.md` — the `TLOAD`/`TSTORE` opcode bytes, the
  two gas costs, the ownership rule under `DELEGATECALL`/`CALLCODE` versus
  `CALL`/`STATICCALL`, the revert rule, the `STATICCALL` exception and the
  discard-at-end-of-transaction rule. Its reference implementation section
  spells out the same semantics a second time.
- `/app/docs/fork/cancun/eip-4844.md` — everything in scope about blobs. The
  **Parameters** table is the source of `GAS_PER_BLOB`,
  `TARGET_BLOB_GAS_PER_BLOCK`, `MIN_BASE_FEE_PER_BLOB_GAS`,
  `BLOB_BASE_FEE_UPDATE_FRACTION`, `FIELD_ELEMENTS_PER_BLOB`, `BLS_MODULUS`,
  `POINT_EVALUATION_PRECOMPILE_ADDRESS` and
  `POINT_EVALUATION_PRECOMPILE_GAS`; **Helpers** gives
  `kzg_to_versioned_hash`; **Header extension** gives `calc_excess_blob_gas`;
  **Gas accounting** gives `fake_exponential`, `get_base_fee_per_blob_gas`
  (EIP-7516's `get_blob_gasprice`) and `get_total_blob_gas`; **Opcode to get
  versioned hashes** gives `BLOBHASH`;
  **Point evaluation precompile** gives the 192-byte layout, the assertions and
  the 64-byte return; **Execution layer validation** gives the
  `max_fee_per_blob_gas` check and the sentence that the blob fee is deducted
  before execution and burned.
- `/app/docs/fork/cancun/eip-5656.md` — `MCOPY`'s opcode byte, its stack order,
  its gas formula and the intermediate-buffer semantics. Its **Test Cases**
  section works a copy overlapping in each direction, with the gas each costs;
  the `length > 0` condition in **Semantics** is what makes a zero-length
  `MCOPY` expand no memory.
- `/app/docs/fork/cancun/eip-6780.md` — the two `SELFDESTRUCT` branches in
  full, the definition of "created in the same transaction", and the
  self-target note for each branch.
- `/app/docs/fork/cancun/eip-7516.md` — `BLOBBASEFEE`'s opcode byte, its 2-gas
  cost and its forwarding to EIP-4844's `get_blob_gasprice`.

`/app/docs/fork/shanghai/` and `/app/docs/fork/london/` define the behaviour the
two inherited groups assert and that you must not disturb;
`/app/docs/fork/berlin/` and `/app/docs/fork/merge/` are the rest of what you
arrived with.

**The interface.** `/app/TEST_INTERFACE.md` is the command surface and the
result schema. It states where `blobGasUsed` and `currentExcessBlobGas` sit in
the `--output.result` field set ("present from the fork that defines them":
Cancun), the `--state.reward=-1` convention every case in scope uses, the fork
vocabulary — `Cancun` spelled exactly that way — and the exit-status rule that
separates a rejected transaction (zero exit, an entry in `rejected[]`) from a
malformed input document (non-zero exit, no partial output) — both sides of
that rule are checked. It also states that `t8n --help` must exit 0 before any
group runs, gives the shape of the `--output.alloc` post-state document and the
canonicalisation the checker applies to it, and carries the trusted-setup note
above. The blob-related `--input.env` field names are given in section 1 above,
since the interface file lists only result fields.

**Additional files.** `/app/WORKSPACE_HANDOFF.md` — what this workspace is,
what the next step receives, how the step's requirement groups are organised,
and why the
`github.com/fjl/memsize` replace directive in `go.mod` has to stay.
`/app/README.md` indexes the documentation tree.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Continue cumulatively in `/app`; preserve inherited source, manifests,
  documents, and handoff files. `/app/build.sh` must keep working and must keep
  producing `/app/build/t8n`.
- This environment has no network access, and everything the build needs is already installed. The upstream project's source, its releases, mirrors and package registries are unreachable, so a copy cannot be fetched by any route. Do not search for, download, copy, wrap, or
  invoke an online implementation or task-specific answer — including a later
  go-ethereum release, its commits, or its diffs; do not inspect `/tests`,
  later-step directories, or solution material.
- Do not edit the supplied documents (`TEST_INTERFACE.md`, `docs/`) or weaken, filter, rename, or disable tests.
  Run the references' local checks and leave a reproducible handoff.

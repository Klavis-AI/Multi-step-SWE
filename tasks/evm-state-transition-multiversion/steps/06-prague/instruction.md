# Step 6 — Prague

## 1. Implement this milestone

`/app` is the workspace you delivered at step 5: go-ethereum with a
state-transition tool that is correct through **Cancun**, carrying the `t9n` and
`b11r` subcommands beside `t8n`.

Carry it forward to **Prague**. `--state.fork=Prague` must be accepted and must
mean all nine of the EIPs below. Be warned that accepting the name is not the
milestone on its own: a tool that takes `Prague` and then
executes Cancun rules passes almost none of the cases in scope, because every
requirement group here asserts Prague *behaviour*.

- **EIP-2537, the BLS12-381 precompiles.** Seven precompiles at the addresses
  `0x0b` `BLS12_G1ADD`, `0x0c` `BLS12_G1MSM`, `0x0d` `BLS12_G2ADD`, `0x0e`
  `BLS12_G2MSM`, `0x0f` `BLS12_PAIRING_CHECK`, `0x10` `BLS12_MAP_FP_TO_G1` and
  `0x11` `BLS12_MAP_FP2_TO_G2`. Each has a fixed input encoding, a fixed output
  encoding and a gas rule — flat for the two additions and the two maps, input-
  length dependent for the two MSMs and for the pairing check. Getting the
  arithmetic right is not enough on its own: the validation rules are required
  just as strictly as the happy path. A field element that is not reduced modulo
  the base field modulus, a padding byte that is not zero, an input whose
  length is not a multiple of the operation's slice size, and an empty input to
  a variable-length operation must each make the precompile **fail** — consuming
  the gas the caller forwarded and returning nothing, so the caller observes a
  failed `CALL` — rather than returning a wrong answer. The subgroup check is
  the one rule that does **not** apply to all six operations, and `eip-2537.md`
  is explicit about the split: "There is no subgroup check for the G1 addition
  precompile" (nor the G2 one), while "MSMs and pairings MUST perform a subgroup
  check" and "MUST return an error" on an input that fails it. So a point on the
  curve but outside the correct subgroup is **accepted** by G1ADD and G2ADD and
  **rejected** by the two MSMs and by the pairing check. The cases in scope reach the
  precompiles through `CALL`, `CALLCODE`, `DELEGATECALL` and `STATICCALL`, so
  they must be ordinary precompiles and not special-cased to one call opcode.
  All seven addresses must remain **plain empty accounts at every earlier
  fork**.
- **EIP-2935, the block-hash history contract.** At the start of processing any
  Prague block, *before any transaction runs*, the tool makes a system call to
  `HISTORY_STORAGE_ADDRESS` = `0x0000F90827F1C53a10cb7A02335B175320002935` with
  the caller set to `SYSTEM_ADDRESS` = `0xfffffffffffffffffffffffffffffffffffffffe`,
  a 32-byte calldata of the parent block hash, a gas limit of 30,000,000 and
  zero value. That invokes the contract's `set()` branch, which writes
  storage slot `(block.number - 1) % HISTORY_SERVE_WINDOW` — with
  `HISTORY_SERVE_WINDOW` = 8191 — to the parent hash. The consequences that
  matter:
  - **The parent hash comes from `--input.env`, from `blockHashes` keyed by
    `currentNumber - 1`.** There is no `parentHash` env field; the in-scope
    `--input.env` documents carry a `blockHashes` object and the entry for the
    parent's number is the value the system call must pass as calldata. An env
    with no usable parent entry must not make the run fail.
  - **The slot is a modulus, not an index.** Block 64 writes slot 63; block
    8192 writes slot 0, over whatever was there.
  - The system call is a **system operation**, so it follows the EIP-4788
    convention the text spells out in four bullets: the call must execute to
    completion, it does not count against the block's gas limit, it transfers
    no value, and — stated explicitly — **if no code exists at
    `HISTORY_STORAGE_ADDRESS` the call must fail silently**. A block whose
    pre-state has no history contract is a valid block that writes nothing, not
    an error.
  - It does not warm the account or its slots for EIP-2929 purposes, so the
    first ordinary transaction that touches the contract pays cold prices.
  - A later transaction calling the same contract with a block number in range
    takes the `get()` branch and is served the hash that was stored; outside
    `[block.number - HISTORY_SERVE_WINDOW, block.number - 1]` it **reverts**.
  - **`BLOCKHASH` itself does not change.** Its 256-block window and its cost
    are exactly what they were; this EIP adds a contract, it does not re-point
    the opcode.
  - None of this happens at Cancun or earlier: the ring-buffer slot stays zero.
- **EIP-7702, set-code transactions.** A new transaction type `0x04` whose
  payload adds an `authorizationList` to the type-3 field set, each entry being
  `[chain_id, address, nonce, y_parity, r, s]`. The tool must decode it from an
  RLP `--input.txs` payload and from the JSON form. Before execution, each
  authorization is applied in list order: recover the authority from
  `keccak(MAGIC || rlp([chain_id, address, nonce]))` with `MAGIC` = `0x05`,
  require the chain id to be zero or to match, require `s` to be at most
  `secp256k1n/2`, require the authority to have no code other than an existing
  delegation, require the nonce to match, add the authority to
  `accessed_addresses`, increment the authority's nonce, and set the
  authority's code to the delegation indicator `0xef0100 || address`. An
  authorization that fails any of those checks is **skipped** — the transaction
  remains valid and the remaining entries are still processed — and when the
  same authority appears more than once the **last valid** occurrence wins. A
  delegation to the zero address writes no indicator and clears the account's
  code instead. Gas: the intrinsic cost adds `PER_EMPTY_ACCOUNT_COST` = 25000
  **per tuple in the list, regardless of validity or duplication**, and each
  tuple whose authority **is not empty** puts
  `PER_EMPTY_ACCOUNT_COST - PER_AUTH_BASE_COST` = 12500 back on the global
  refund counter, so `PER_AUTH_BASE_COST` = 12500 is what a successful
  authorization over an existing account costs net. Resolving a delegation
  during execution costs a further EIP-2929 `COLD_ACCOUNT_READ_COST` (2600) or
  `WARM_STORAGE_READ_COST` (100) for the delegation target. A set-code
  transaction with an **empty** `authorizationList` is invalid. EIP-3607 is
  relaxed just far enough that an EOA carrying a delegation indicator may still
  originate a transaction, and no further. A delegation that was written is
  **not rolled back** when the transaction itself then fails or reverts. Code so delegated is
  loaded on every subsequent call to the authority, including within the same
  transaction, while `EXTCODESIZE`/`EXTCODECOPY`/`EXTCODEHASH` observed from
  outside see the 23-byte indicator rather than the target's code. Delegations
  are persistent: they survive the transaction and show up in the post-state
  `alloc` and in `stateRoot`. Type `0x04` must be **rejected at every earlier
  fork**.
- **EIP-7623, the calldata cost floor.** A transaction's intrinsic gas becomes
  the **maximum** of the ordinary intrinsic cost and a floor,
  `21000 + TOTAL_COST_FLOOR_PER_TOKEN * tokens_in_calldata`, where
  `tokens_in_calldata = zero_bytes + 4 * nonzero_bytes`,
  `STANDARD_TOKEN_COST` = 4 and `TOTAL_COST_FLOOR_PER_TOKEN` = 10. This is two
  distinct changes and both are in scope:
  - **`gasUsed` rises** for a transaction whose execution was cheap relative to
    its calldata, because the floor is charged even when execution used less.
    In the EIP's formula the floor is the second arm of an outer `max`, not a
    term inside the execution arm, so anything that reduces the execution arm —
    the refund included — cannot bring `gasUsed` below
    `21000 + TOTAL_COST_FLOOR_PER_TOKEN * tokens_in_calldata`.
  - **Validity tightens.** A transaction whose `gasLimit` is below the floor is
    **invalid**, even though it was valid at Cancun with the same fields. That
    is an entry in `rejected[]` with a zero exit status, not a tool-level
    error.
- **EIP-7691, the blob throughput increase.** Prague raises the blob schedule
  the tool reads out of Cancun's `eip-4844.md`, and the parameter that matters
  is its maximum. `MAX_BLOB_GAS_PER_BLOCK` becomes **1,179,648** — **nine**
  blobs — where Cancun's is 786,432; `TARGET_BLOB_GAS_PER_BLOCK` becomes
  **786,432** — six — and `BLOB_BASE_FEE_UPDATE_FRACTION` becomes **5,007,716**,
  while `GAS_PER_BLOB` (131,072) and `MIN_BASE_FEE_PER_BLOB_GAS` are unchanged.
  The consequence is the rule step 5 built: the per-transaction exclusion
  against the block's running total admits up to **nine** blobs at Prague where
  Cancun admits six, so a blob transaction carrying nine versioned hashes is
  **included** at `--state.fork=Prague` rather than written into `rejected[]`.
  EIP-7691 has no file under `docs/fork/prague/` — it is the Cancun EIP-4844
  parameter set that Prague restates — which is why the three numbers are given
  here and in `docs/fork/prague/README.md`. The next step re-runs the Prague
  fixture family as a gate group, on a further sample of its own, and the cases
  in that sample include exactly this shape: one type-3 transaction carrying
  nine versioned hashes, 1,179,648 blob gas, which the result document must show
  as **included**. A tool that keeps Cancun's six-blob cap at Prague excludes
  them and means that step is not satisfied.
- **EIP-6110, EIP-7002 and EIP-7251, behind EIP-7685's `requestsHash`.** Prague
  adds a new `--output.result` field, `requestsHash`, **present from Prague and
  absent at every earlier fork**. It commits to the block's request list, built
  from three sources during the same block:
  - **EIP-6110 deposits**, `request_type` `0x00`, scraped from the receipts of
    the block: every log emitted by `DEPOSIT_CONTRACT_ADDRESS`
    (`0x00000000219ab540356cbb839cbe05303d7705fa`) whose topic is the deposit
    event signature is decoded, in the order the logs appear, into a
    fixed-layout request. A log from any other address, or a failed
    transaction's log, contributes nothing.
  - **EIP-7002 withdrawal requests**, `request_type` `0x01`, read at the end of
    the block by a system call to `WITHDRAWAL_REQUEST_PREDEPLOY_ADDRESS`
    (`0x00000961Ef480Eb55e80D19ad83579A64c007002`) as `SYSTEM_ADDRESS`, which
    dequeues what the block's transactions enqueued.
  - **EIP-7251 consolidation requests**, `request_type` `0x02`, the same
    mechanism at `CONSOLIDATION_REQUEST_PREDEPLOY_ADDRESS`
    (`0x0000BBdDc7CE488642fb579F8B00f3a590007251`).

  Both system calls are **fail-soft**. The vendored EIP-7002 and EIP-7251 texts
  say a call that fails, or a predeploy with no code at it, "MUST" invalidate
  the block; the required behaviour is the execution-specs behaviour those
  sentences were written ahead of, and it is what this step requires: a system
  call that runs out of gas, reverts, or finds no code at the predeploy
  contributes **no request** and does **not** abort the transition. The block
  still transitions and still produces a result document, and the requests the
  other sources collected are committed normally. There is no invalidation
  channel in `--output.result`, and no case in scope expects one.

  The commitment itself is EIP-7685's `compute_requests_hash`: the intermediate
  list holds one `request_type ++ request_data` element per **non-empty**
  source, ordered by `request_type` ascending, and the hash is
  `sha256` over the concatenated `sha256` digest of each element. Two details
  matter most. An element whose `request_data` is empty — the
  element is then just its one type byte — is **excluded from the
  commitment**, so a block that triggered no requests of a given type is not
  the same as a block that triggered an empty one. And a block with no requests
  at all hashes the empty string: `requestsHash` is `sha256(b"")` =
  `0xe3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`, emitted
  as a real value rather than omitted or zeroed.
- **EIP-4788's beacon-roots system call becomes observable at this step.** It
  is a *Cancun* EIP, so the workspace you received already has to perform it and
  this is not new work — but step 5's in-scope corpus carried no code at
  `BEACON_ROOTS_ADDRESS` = `0x000F3df6D732807Ef1319fB7B8bB8522d0Beac02`, and
  this step's does. Every case in the two blockchain-derived groups (the
  EIP-2935 history-contract group and the EIP-7685 requests group) ships that
  contract **with its runtime code** and a non-zero `currentTimestamp`, so the
  start-of-block system call from `SYSTEM_ADDRESS` writes
  `storage[currentTimestamp % 8191] = currentTimestamp` and
  `storage[currentTimestamp % 8191 + 8191] = parentBeaconBlockRoot` into that
  account, and both writes are inside the `stateRoot` those cases assert. A tool
  that accepts `parentBeaconBlockRoot` as an env field without performing the
  call matches at Cancun and mismatches here. The specification is
  `/app/docs/fork/cancun/eip-4788.md`; the call obeys the same system-call
  convention as EIP-2935's, including failing silently when the address holds no
  code.
- **Nothing below Prague moves.** Cancun and Shanghai are re-run as inherited
  groups, through the RLP input form, asserting `stateRoot`, `logsHash` and
  `rejected`. A Prague change that shifts an earlier fork's result — a BLS
  precompile answering at Cancun, the history contract's slot being written at
  Cancun, the calldata floor being charged at Shanghai, `requestsHash`
  appearing at a fork that does not define it — is a regression and fails a
  gate group, which means the step is not satisfied. `t9n` and `b11r` are
  re-checked inside those same two groups: breaking either subcommand against an
  inherited fork fails the gate. Berlin, London and Merge are not re-checked
  here — the steps that introduced them check them.

  One case in `inherited-shanghai-t8n` is not an earlier step's new capability
  coming back around; it is an inherited behaviour that no step's documents
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

**Normative semantics.** `/app/docs/fork/prague/README.md` first, for what the
fork changes at the `t8n` boundary, for the EIP-7691 blob-schedule numbers that
have no file of their own, and for what is explicitly out of scope, then the
eight EIP files beside it. (Those README files are task-written summaries;
wherever one differs from the EIP text beside it, the EIP file — an unmodified
upstream copy — is authoritative. For EIP-2537 in particular, `eip-2537.md`'s
parameter table lists seven precompiles at `0x0b`..`0x11`.)

- `/app/docs/fork/prague/eip-2537.md` — **Constants** for the precompile
  address table; **Fine points and encoding of base elements** for the
  field-element encoding, the zero-padding rule, the point-at-infinity encoding
  and **Behavior on empty inputs:**; the seven **ABI for ...** sections for
  each operation's input layout, slice size and return value; **Gas schedule**
  for the flat costs, **G1/G2 MSM** for the two discount tables and the
  `(k * multiplication_cost * discount) // 1000` formula, **Pairing check
  operation** for `32600*k + 37700`, and **Gas schedule clarifications for the
  variable-length input** for how `k` is taken as a floor division of the input
  length — an input that is not a whole number of slices is still priced and
  then still errors; **Gas burning on error** for what a failing call costs;
  and **Subgroup checks** for the requirement on MSMs and pairings.
- `/app/docs/fork/prague/eip-2935.md` — the parameter table
  (`HISTORY_SERVE_WINDOW`, `SYSTEM_ADDRESS`, `HISTORY_STORAGE_ADDRESS`,
  and `BLOCKHASH_SERVE_WINDOW`, which is what stays at 256); **Block
  processing** for the system call, its 30,000,000 gas limit and the
  "fail silently" clause; **`set`** for the
  `block.number-1 % HISTORY_SERVE_WINDOW` slot rule and **`get`** for the
  in-range and revert branches; **Bytecode** for the contract's runtime code
  and **Deployment** for the same code behind its constructor prefix;
  **Gas costs** for the no-warming rule; **EVM Changes** for the fact that
  `BLOCKHASH` is untouched.
- `/app/docs/fork/prague/eip-7702.md` — **Parameters** for `SET_CODE_TX_TYPE`,
  `MAGIC`, `PER_AUTH_BASE_COST` and `PER_EMPTY_ACCOUNT_COST`; **Set code
  transaction** for the RLP field order and the signature; **Behavior** for the
  nine numbered steps that apply one authorization, the skip rule, the
  last-valid-occurrence rule and the note that delegations are not rolled back;
  **Delegation indicator** for `0xef0100 || address` and for what the code-
  reading opcodes see (`CODESIZE`/`CODECOPY` act on the delegated code while
  `EXTCODESIZE` returns 23); **Gas Costs** for the per-tuple intrinsic charge
  and the delegation-resolution cost; **Transaction origination** for the
  EIP-3607 relaxation; and **Non-empty authorization list required**, in the
  rationale, for why an empty list is invalid.
- `/app/docs/fork/prague/eip-7623.md` — **Specification**: the two constants,
  the `tokens_in_calldata` definition, the two-branch `gasUsed` formula and the
  final paragraph, which is the validity rule.
- `/app/docs/fork/prague/eip-7685.md` — **Requests** for the
  `request_type ++ request_data` shape, **Block Header** for the ordering rule,
  the exclusion of empty elements and `compute_requests_hash`.
- `/app/docs/fork/prague/eip-6110.md` — **Constants** for
  `DEPOSIT_REQUEST_TYPE` and `DEPOSIT_CONTRACT_ADDRESS`, and the deposit
  request layout and the log-parsing procedure below it.
- `/app/docs/fork/prague/eip-7002.md` — **Constants** for
  `WITHDRAWAL_REQUEST_TYPE` and `WITHDRAWAL_REQUEST_PREDEPLOY_ADDRESS`, the
  request layout, and the system-call-at-end-of-block procedure.
- `/app/docs/fork/prague/eip-7251.md` — the same three things for
  `CONSOLIDATION_REQUEST_TYPE` and
  `CONSOLIDATION_REQUEST_PREDEPLOY_ADDRESS`.

- `/app/docs/fork/cancun/eip-4788.md` — an inherited EIP that this step is the
  first to check. Its **Specification** constants table for
  `BEACON_ROOTS_ADDRESS`, `HISTORY_BUFFER_LENGTH` (8191) and `SYSTEM_ADDRESS`;
  **Beacon roots contract** and its `set` branch for the two slots the call
  writes (`timestamp % 8191` and `timestamp % 8191 + 8191`); and **Block
  processing** for when in the block the call happens and for the four
  system-call bullets, the last of which is the "fail silently with no code"
  rule that EIP-2935 then reuses.

`/app/docs/fork/cancun/` and `/app/docs/fork/shanghai/` define the behaviour the
two inherited groups assert and that you must not disturb;
`/app/docs/fork/london/`, `/app/docs/fork/berlin/` and `/app/docs/fork/merge/`
are the rest of what you arrived with.

**The interface.** `/app/TEST_INTERFACE.md` is the command surface and the
result schema. It states where `requestsHash` sits in the `--output.result`
field set ("present from the fork that defines them": Prague), the
`--state.reward=-1` convention every case in scope uses, the fork vocabulary —
`Prague` spelled exactly that way — and the exit-status rule that separates a
rejected transaction (zero exit, an entry in `rejected[]`) from a malformed
input document (non-zero exit, no partial output). The in-scope `--input.env`
documents at this fork carry `blockHashes`, `withdrawals`, the
`parentBeaconBlockRoot` and blob-gas fields you already accept, and
`parentBaseFee`, `parentGasLimit` and `parentGasUsed`; an env document may
carry a field the fork does not use and must still parse.

**Additional files.** `/app/WORKSPACE_HANDOFF.md` — what this workspace is,
what the next step receives, how the step's requirement groups are organised,
and why the
`github.com/fjl/memsize` replace directive in `go.mod` has to stay for as long
as `go.mod` still requires that module. `/app/README.md` indexes the
documentation tree.

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

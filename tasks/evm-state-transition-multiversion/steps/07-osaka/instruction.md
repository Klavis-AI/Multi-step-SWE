# Step 7 — Osaka

## 1. Implement this milestone

`/app` is the workspace you delivered at step 6: go-ethereum with a
state-transition tool that is correct through **Prague**, carrying the `t9n` and
`b11r` subcommands beside `t8n`.

Carry it forward to **Osaka**. `--state.fork=Osaka` must be accepted and must
mean the EIPs below. Be warned that accepting the name is not the milestone on
its own: a tool that takes `Osaka` and then executes Prague
rules passes none of the new-capability cases in scope, because every requirement
group here asserts Osaka *behaviour* — post-state `alloc`, `stateRoot`, `logsHash`,
`gasUsed` and `rejected`, case by case.

- **EIP-7825, the transaction gas limit cap.** A protocol-level maximum of
  **16,777,216 gas (2^24)** for any single transaction. A transaction whose
  declared `gasLimit` exceeds that value is **invalid**, and this is the
  signature property of the fork — it is the first rule in the ladder that
  makes a transaction every earlier fork accepted stop being includable. The
  details that matter:
  - **The cap is on the declared `gasLimit`, not on gas consumed.** It is
    checked before execution, so nothing runs: no gas is charged, the sender's
    nonce is not spent, and no storage the transaction would have written
    changes.
  - **It is independent of the block gas limit.** The cases in scope set
    `currentGasLimit` far above the cap — 100,000,000 in the probe's
    `--input.env` — so a rejection must come from the new per-transaction rule
    and not from the ordinary block-gas-limit check. Equally, a tool that only
    rejects when the transaction also exceeds the block limit fails.
  - **16,777,216 is the cap, not one below it.** A `gasLimit` of exactly
    `0x1000000` is **accepted** and executes normally; `0x1000001` is rejected.
    Both boundaries are in scope.
  - **It applies to every transaction type** — legacy, EIP-2930, EIP-1559,
    EIP-4844, EIP-7702 — and to a **contract creation** as much as to a call,
    because the rule reads a field every type has.
  - **A rejection removes one transaction, not the block.** The remaining
    transactions still execute, and the block's result is the result of what
    ran. Because the rejected transaction never spent its nonce, a later
    transaction from the same sender with that nonce is still valid.
  - **A rejection is reported as data, not as an error**: an entry in
    `result.rejected` with a **zero** exit status. Turning it into a non-zero
    exit is not accepted.
  - **None of this happens at Prague or earlier.** The same over-cap
    transaction at `--state.fork=Prague` must still execute and still be
    included. A tool that enforces the cap at every fork has not implemented
    EIP-7825, it has broken Prague, and it fails in both the
    new group and the inherited one.
- **EIP-7883, the ModExp gas cost increase.** The precompile at
  `0x0000000000000000000000000000000000000005` keeps its semantics and is
  repriced. The new cost is
  `max(500, floor(multiplication_complexity * iteration_count))`, and all four
  changes from the EIP-2565 formula you arrived with are checked together
  through `gasUsed` and the post-state:
  - the **minimum rises from 200 to 500**;
  - the **division by 3 is removed**, tripling the general cost;
  - `multiplication_complexity` is **16** when `max(base_length,
    modulus_length)` is at most 32, and **`2 * words**2`** — twice the old
    value — when it is larger, with `words = ceil(max_length / 8)`;
  - for an exponent longer than 32 bytes the per-byte multiplier **rises from
    8 to 16**, so `iteration_count = 16 * (exponent_length - 32) + msb`, and
    `iteration_count` is then floored at 1.

  **How `msb` is read is required, and `eip-7883.md`'s pseudocode does not say
  it the way this step requires it.** The vendored text writes the term as
  `((exponent & (2**256 - 1)).bit_length() - 1)`; read literally that takes
  the **low 256 bits** of the exponent and can come out **-1**. What is required
  is the reading the inherited EIP-2565 path in your tree already uses, in two
  clauses, and both are in scope:
  - `msb` is read from the **first 32 bytes of the exponent field** — the
    head, most significant byte first — not from the low 256 bits. The
    vendored **Test Cases** table settles this half on its own: it prices
    `modexp_guido_2_even` at 51152, which is what the head reading gives and
    the low-256 reading does not (51184).
  - It is **clamped at 0**: a head whose 32 bytes are all zero contributes
    `0`, never `-1`. The table does not settle this half — every vector of its
    that this step's corpus carries has a non-zero head — but the in-scope corpus
    does: the cases in this step include that shape, and the literal
    formula charges them one `multiplication_complexity` less than the fixture
    does.

  Longer exponents, where the head and low bits differ and the two readings
  diverge further, are in scope as well. Only the head reading with the clamp
  reproduces the whole corpus, and it changes nothing about the rest of
  EIP-7883: the multiplier, the complexity, the minimum and the removed
  divisor are as stated above. Leave the inherited EIP-2565 half of that
  function alone — the same `msb` rule prices ModExp at Berlin through Prague,
  the inherited corpora carry zero-head exponents of their own, and a change
  to it moves their `gasUsed` and their post-state and fails those gate
  groups.

  The consequence a caller sees is that a `CALL` that carried enough gas at
  Prague may now run out: the 500-gas minimum means a call forwarding 499 gas
  fails where 500 succeeds.
- **EIP-7823, upper bounds for ModExp inputs.** The same precompile at
  `0x0000000000000000000000000000000000000005` also gains an input bound at
  Osaka. Each of the three length fields at the head of the input —
  `length_of_BASE`, `length_of_EXPONENT` and `length_of_MODULUS` — **MUST be
  less than or equal to 8192 bits, that is 1024 bytes**. This is a bound on the
  *declared lengths*, not on the input's total size: a call whose input runs to
  a few kilobytes is fine as long as none of the three fields exceeds 1024
  bytes. When one of them does, **the precompile execution stops, returns an
  error, and consumes all gas** — the call **fails**; it does not revert and it
  does not truncate the oversized field and carry on with the answer a smaller
  modulus or exponent would have given. A caller that forwarded gas sees the
  `CALL` return failure with empty return data, so a contract that stores the
  call's success flag and its `RETURNDATASIZE` observes `0` and `0`. The gas
  consumed is **everything the call was given** — the bound is checked after
  `RequiredGas`, so the caller is charged the whole forwarded amount rather
  than EIP-7883's price, which is what "consumes all gas" in `eip-7823.md`
  means for a caller that forwarded more than the price. The bound is part of
  the same repricing and not a separate validity rule that rejects the
  transaction. **Prague and Cancun
  apply no such bound**: an input with a length
  field above 1024 bytes is legal there and is priced by EIP-2565, so a tool
  that enforces the bound at every fork breaks the inherited groups.
- **EIP-7939, the `CLZ` opcode.** A new instruction **`0x1e`** that pops one
  256-bit word and pushes the number of **leading zero bits** in it — the
  zeros preceding the most significant set bit. `CLZ(0)` is **256**, not 0 or
  an error; that special case is the one most implementations get wrong.
  `CLZ(2**255)` is 0 and `CLZ(1)` is 255. It costs **5 gas** (the `MUL` tier,
  raised from the 3 a shift-like opcode would suggest), and the cost is checked
  directly, not just the result. `0x1e` must remain an **undefined opcode at
  every earlier fork**, where executing it aborts the frame and consumes the
  remaining gas as any invalid instruction does; contract code containing
  `0x1e` becomes executable at Osaka and not before.
- **EIP-7951, the `P256VERIFY` precompile.** A new precompile at address
  **`0x0000000000000000000000000000000000000100`** verifying an ECDSA signature
  over **secp256r1 (P-256)**, at a flat **6900 gas**. Input is **exactly 160
  bytes**: `h || r || s || qx || qy`, 32 bytes each. Output is
  `0x0000...0001` — a full **32 bytes** — for a valid signature and the
  **empty string** for anything else. The validation rules are required as
  strictly as the happy path; each of these must produce failure rather than a wrong
  answer or an error:
  - an input whose length is not exactly 160 bytes;
  - `r` or `s` outside `0 < value < n`, the subgroup order;
  - `qx` or `qy` at or above the base field modulus `p`;
  - a public key that does not satisfy the curve equation
    `y^2 = x^3 + ax + b (mod p)`;
  - the point at infinity, which for this curve is the 64 zero bytes `(0, 0)`.

  **The precompile must never revert.** A failed verification and an invalid
  input both return the empty string and **consume the full 6900 gas**, exactly
  as a success does, so the caller observes a `CALL` that returned nothing
  rather than one that burned only part of its allowance. Address `0x0100` must
  remain a **plain empty account at every earlier fork**.
- **EIP-7918, the blob base-fee floor, is part of this fork but carries no
  requirement group.** It is vendored under `/app/docs/fork/osaka/eip-7918.md` and
  you should implement it for the fork to be right, but no case in scope asserts
  it and none can: `t8n` builds the parent header for the excess-blob-gas
  calculation from `--input.env`'s `parentExcessBlobGas` and
  `parentBlobGasUsed` only, with no parent base fee, and the EIP-7918 branch
  needs a parent base fee to compute its floor. There is no input to the in-scope
  interface that reaches the new rule. **EIP-7918 is out of scope, in
  either direction.** This is stated so you can budget your time honestly.
- **Nothing below Osaka moves.** Prague and Cancun are re-run as inherited
  groups, through the RLP `--input.txs` form, asserting **`stateRoot`,
  `logsHash`, `logsBloom` and `rejected`** at `--state.fork=Prague` and
  `--state.fork=Cancun`, and comparing the **full post-state `alloc`** account by
  account, not only those result fields — balances, nonces, code and storage
  slots all have to come back exactly as they did at step 6. **The EIP-4788
  beacon-roots contract is not part of this step's surface.** It is a Cancun
  requirement and it *is* observable at step 6, whose blockchain-derived corpora
  ship code at `BEACON_ROOTS_ADDRESS` =
  `0x000F3df6D732807Ef1319fB7B8bB8522d0Beac02` and a non-zero
  `currentTimestamp`; step 6's instruction names it and it stays a regression
  obligation there. This step's corpora do not exercise it: every `env.json`
  file here carries `parentBeaconBlockRoot: 0x000…0` and no `alloc.json` places
  code at that address, so the start-of-block system call is a no-op in every
  case and no `stateRoot` above depends on it. Nothing here asks you to
  implement EIP-4788 and nothing here will notice whether you did. An Osaka
  change
  that shifts an earlier fork's result is a regression and fails a gate group, which means the step is not satisfied,
  and these corpora are sensitive enough that it is not a theoretical risk.
  **The inherited cases carry step-6 expectations, not Osaka ones.** The
  Cancun cases are step 6's `inherited-cancun-t8n` corpus file for file;
  the Prague cases are a further sample of the same upstream `fork_Prague`
  fixture family those groups were drawn from, not files step 6 shipped. Every
  `case.json` names the fork its case runs at, and no expectation in either
  corpus was recomputed for Osaka. Applying **any** Osaka rule before
  `--state.fork=Osaka` therefore moves a result that is already correct below
  this fork, and it is measured under the checks those
  cases are actually checked by — the three they declare (`stateRoot`,
  `logsHash`, `rejected`), plus `gasUsed` and `logsBloom` wherever the fixture
  supplies them, plus the post-state `alloc` — not under a byte comparison of
  the result file, which would also count the Cancun
  `requests` field and treat every Cancun case as changed — it changes the
  answer for many cases in both corpora.
  The leaks they actually
  catch are worth naming:
  - **the gas cap charged before Osaka.** This is most of the difference.
    Conformance fixtures routinely give a transaction a `gasLimit` equal to the
    block gas limit — 120,000,000 in many of these cases — which is far above
    2^24, so a tool that applies EIP-7825 unconditionally rejects dozens of
    transactions that Prague and Cancun include, and fails both groups.
  - **the ModExp repricing charged before Osaka.** Both corpora carry ModExp
    cases whose `gasUsed` is EIP-2565's at Prague and Cancun; charging
    EIP-7883's there changes `gasUsed` and `stateRoot`.
  - **the EIP-7823 ModExp input bound applied before Osaka**, which fails a
    call that Prague and Cancun price by EIP-2565 and let through — the same
    shape of leak as the repricing above, reached by the length fields rather
    than by the gas formula.

  **Osaka adds no blob-count rule of its own, but the Prague corpus these
  groups re-run does bound the count**, through the schedule you implemented at
  step 6.
  The in-scope form carries a type-3 transaction's versioned-hash list, and some
  of the Prague cases carry **nine** versioned hashes — 1,179,648 blob gas —
  with a fixture that expects the transaction **included** at
  `--state.fork=Prague`: `rejected: []`, and the sender's balance and the
  post-state `alloc` of a block that ran it. Prague's
  `MAX_BLOB_GAS_PER_BLOCK` is 1,179,648 (EIP-7691, nine blobs, stated in step
  6's instruction and in `/app/docs/fork/prague/README.md`), so nothing here
  asks for a new number — but a tool that still applies Cancun's six-blob cap
  at Prague excludes those transactions, mismatches `rejected`, the
  `alloc` and the `stateRoot`, and fails the gate. EIP-7594 (PeerDAS) is out of
  scope at this fork (see `/app/docs/fork/osaka/README.md`), and the only blob
  change *in* the fork's scope is EIP-7918's fee floor, which carries no requirement
  group either.

  Those two groups are gate groups: they must pass completely or the step is not
  satisfied, and they are a precondition rather than the step's new capability -- a submission that
  changes nothing keeps the gate open without covering Osaka. The
  fork-gating of the gas cap is additionally asserted head-on: the *same*
  over-cap transaction, run at `--state.fork=Prague`, is required
  to execute and be included.

## 2. Read these local references

**Normative semantics.** `/app/docs/fork/osaka/README.md` first, for what the
fork changes at the `t8n` boundary and for what is explicitly out of scope
(EIP-7594/PeerDAS is a consensus-layer and networking change and is **not**
part of this milestone), then the EIP files beside it.

- `/app/docs/fork/osaka/eip-7825.md` — **Specification → Gas Cap** for the
  16,777,216 (2^24) value, for "This cap applies regardless of the block gas
  limit set by miners or validators", and for the rejection requirement;
  **Changes to EVM Behavior** for the rule being stated on "the `gasLimit`
  specified by the sender", which is what makes it a pre-execution validity
  check rather than a consumption limit, and for block validation; **Protocol
  Adjustment** for the cap being independent of the block gas limit;
  **Backwards Compatibility** for the fact that transactions valid at earlier
  forks become invalid here — the fork-gating that must hold in both
  directions.
- `/app/docs/fork/osaka/eip-7883.md` — **Specification** for the three
  functions in full: `calculate_multiplication_complexity`,
  `calculate_iteration_count` and `calculate_gas_cost`. Then the three numbered
  subsections, each of which gives the old expression and the new one side by
  side: **1. Increased minimal and general price** for `max(200, ... / 3)`
  becoming `max(500, ...)`; **2. Increase cost for exponents larger than 32
  bytes** for the `8 *` to `16 *` change; **3. Assume the minimal base /
  modulus length to be 32 ...** for the minimum complexity of 16 and the
  doubling above 32 bytes. **Test Cases** carries a table of the reused
  `modexp_nagydani_*` vectors with their EIP-2565 price beside their EIP-7883
  price, which is the quickest way to check an implementation end to end —
  and it is the arbiter of the one place its own pseudocode is misleading:
  `calculate_iteration_count`'s `(exponent & (2**256 - 1))` is the low-256-bits
  reading, and the table's `modexp_guido_2_even` row prices the head reading
  (51152, against 51184 for the low bits). §1 states both that and the clamp
  at 0, which the table does not reach and the cases in scope do.
- `/app/docs/fork/osaka/eip-7823.md` — **Specification** for the bound itself:
  the three length fields `length_of_BASE`, `length_of_EXPONENT` and
  `length_of_MODULUS` each "MUST be less than or equal to 8192 bits (1024
  bytes)", and the sentence that fixes the failure mode, "If any of these
  inputs are larger than the limit, the precompile execution stops, returns an
  error, and consumes all gas." **Rationale → Limit** for which uses fit under
  the bound (RSA keys up to 8192 bits, elliptic-curve uses below 384 bits), and
  **Rationale → Analysis** for the historical length distribution that shows
  every real-world call was already inside it. The EIP states no fork gating of
  its own: it is an Osaka EIP, so by the same rule as the rest of §1 it applies
  at `--state.fork=Osaka` and not below.
- `/app/docs/fork/osaka/eip-7939.md` — **Specification** for the opcode number
  `0x1e`, the stack effect, the reference implementations in Solidity, Python
  and C++ (the Python one is the clearest statement of the rule), and the final
  line, "The cost of the opcode is 5, matching MUL". **Rationale → The special
  0 case** for why `CLZ(0)` is 256. **Test Cases** for the six published
  vectors, which the `osaka-spec-vectors` group covers. **Backwards Compatibility** for `0x1e` having been undefined before.
- `/app/docs/fork/osaka/eip-7951.md` — **Specification → Precompile** for the
  address `0x100` and the 6900 gas price; **Curve Parameters** for `p`, `a`,
  `b`, the base point and the subgroup order `n`; **Points and Encoding** and
  its four subsections for the 32-byte big-endian field-element encoding, the
  64-byte point encoding, the all-zero point-at-infinity convention and the
  rule that scalars are **not** required to be below `n`; **Behavior on
  Invalid Inputs** for returning the empty string; **ABI for `P256VERIFY`
  Operation → Input** for the 160-byte `h || r || s || qx || qy` layout,
  **→ Output** for the 32-byte success value and the empty failure value, and
  **→ Input Validation** for the five numbered checks; **Signature
  Verification Algorithm** for the verification itself; **Error Cases** for
  the enumeration; and **Gas Schedule → Gas Burning on Error** for "The
  precompile MUST NOT revert under any circumstances ... MUST return `` and
  consume the same amount of gas had verification succeeded".
- `/app/docs/fork/osaka/eip-7918.md` — the blob base-fee floor. Read it to
  keep the fork coherent; as stated above, no case in scope asserts it.

`/app/docs/fork/prague/` and `/app/docs/fork/cancun/` define the behaviour the
two inherited groups assert and that you must not disturb;
`/app/docs/fork/shanghai/`, `/app/docs/fork/merge/`, `/app/docs/fork/london/`
and `/app/docs/fork/berlin/` are the rest of what you arrived with.

**The interface.** `/app/TEST_INTERFACE.md` is the command surface and the
result schema. It states the fork vocabulary — `Osaka` spelled exactly that
way, and every earlier name still accepted, because "a step is checked at its
own fork **and at every earlier fork**" — the `--state.reward=-1` convention
every case in scope uses, the `--input.txs` RLP form the inherited groups are fed
through, and the exit-status rule that separates a rejected transaction (zero
exit, an entry in `rejected[]`) from a malformed input document (non-zero exit,
**no partial `--output.result` file left behind**). Osaka adds **no new
`--output.result` field**: the schema you produced at Prague, `requestsHash`
included, is the schema Osaka emits. An `--input.env` document may carry a
field the fork does not use and must still parse.

**Additional files.** `/app/WORKSPACE_HANDOFF.md` — what this workspace is, how
the step's requirement groups are organised, and why the `github.com/fjl/memsize` replace directive in
`go.mod` has to stay for as long as `go.mod` still requires that module.
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

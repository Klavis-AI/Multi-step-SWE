# Step 4 — Shanghai

## 1. Implement this milestone

`/app` is the workspace you delivered at step 3: go-ethereum with a
state-transition tool that is correct through **the Merge**, carrying the
`t9n` and `b11r` subcommands beside `t8n`.

Carry it forward to **Shanghai**. `--state.fork=Shanghai` must be accepted and
must mean all four of the EIPs below. Shanghai is the first fork to activate by
timestamp rather than by block number; that is a chain-configuration detail and
nothing the in-scope interface asks about — every case in scope names its fork
through `--state.fork`.

- **EIP-3651, warm COINBASE.** At the start of transaction execution the
  address `--input.env`'s `currentCoinbase` names is already in the accessed
  address set. The first `COINBASE`, `BALANCE(coinbase)`, `EXTCODESIZE`,
  `EXTCODEHASH`, `EXTCODECOPY` or `CALL`-family access to it is charged the
  warm price, not the cold one, so `gasUsed` and the post-state fall.
- **EIP-3855, `PUSH0`.** A new instruction at `0x5f`: no immediate data, pops
  nothing, pushes a single zero word, 2 gas (`base`). Bytecode that used to be
  invalid becomes valid and executes, and a contract built out of `PUSH0` costs
  less than the `PUSH1 0x00` it replaces. `PUSH0` must remain **undefined at
  every earlier fork**, where `0x5f` is still an invalid instruction that aborts
  the frame.
- **EIP-3860, metering initcode.** Contract creation is charged
  `2 * ceil(len(initcode) / 32)` extra gas: as part of the *intrinsic* cost of
  a creation transaction, and as an extra charge on `CREATE` and `CREATE2`
  deducted before the new address is derived and before the initcode runs. A
  creation transaction whose `gasLimit` no longer covers its intrinsic cost is
  rejected, exactly as an under-funded transaction already is.
- **EIP-3860, limiting initcode.** `MAX_INITCODE_SIZE` is `2 * 24576 = 49152`
  bytes. A creation *transaction* whose data exceeds it is **invalid** — it is
  recorded in `rejected[]`, it is not a tool-level failure. A `CREATE` or
  `CREATE2` whose initcode exceeds it **exceptionally aborts**, as if it ran out
  of gas, and the enclosing frame sees that as a failed creation rather than as
  a failed transaction. The two clauses are checked separately, and the limit is
  exercised from both sides — at the maximum and at one byte over —
  for a creation transaction and for `CREATE` and `CREATE2` alike, alongside the
  32/33- and 49120/49121-byte word-boundary neighbours and the empty and
  single-byte cases. An off-by-one, a limit counted in words rather than in
  bytes, or a limit applied to the deployed code instead of to the initcode is
  visible there.
- **EIP-4895, beacon-chain push withdrawals.** This is a **block-level** change,
  and it is the only part of this step that widens the tool's input and output
  documents.
  - `--input.env` gains a `withdrawals` array. Each element is an object with
    the keys `index`, `validatorIndex`, `address` and `amount`, spelled in
    camel case as every other `--input.env` field is, and quantity-valued
    fields carry the usual `0x`-prefixed hex.
  - Each withdrawal credits the balance of `address` by `amount`. `amount` is
    **in Gwei**: the balance increase is `amount * 10^9` wei. The credit is
    unconditional and cannot fail, it costs no gas, and it never appears in
    `gasUsed`, in `receipts` or in `rejected`. A withdrawal of amount zero is
    still a withdrawal: it occupies its position in the trie.
  - The credits happen **after** every transaction in the block. A transaction
    that reads `BALANCE` of a recipient in the same block must see the
    *pre*-withdrawal balance.
  - `--output.result` gains **`withdrawalsRoot`**, present from Shanghai
    onward and absent at every earlier fork. It is the root of a
    Merkle-Patricia trie built exactly as the transactions trie is: key =
    `RLP(i)` for the withdrawal's **position `i` in the list**, value = the RLP
    of `[index, validator_index, address, amount]`. The position, not the
    `index` field, is the key — the two need not agree, and the field must not
    be used to sort the list. An empty `withdrawals` list still gets a
    `withdrawalsRoot`: the empty-trie root
    `0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421`, not
    an absent field and not a zero hash.
  - A malformed `withdrawals` record — an `address` that is not 20 bytes, for
    instance — is a malformed input document: non-zero exit, and no partial
    `--output.result` left behind.
- **Nothing below Shanghai moves.** This step covers four forks — **Berlin,
  London, Merge and Shanghai** — through three subcommands — **`t8n`, `t9n` and
  `b11r`**. The two inherited groups carry that load. `inherited-berlin-state`
  is checked at Berlin and also carries `b11r` invocations, which are
  fork-agnostic; `inherited-london-state` is checked at London and also carries
  cases checked at `--state.fork=Merge` (`currentRandom`, `0x44`, and PoS block
  production with `--state.reward=-1`) and `t9n` invocations. Most of their
  cases use the RLP input form and assert `stateRoot`, `logsHash` and
  `rejected`; the rest declare their own check vocabulary.

  Each of the four regressions named above is guarded explicitly, by a
  synthetic case in the inherited tier that stores a *measurement* rather than
  asserting a bare success, so it cannot pass by accident:

  - `0x5f` must still abort the frame at Berlin, London and the Merge. The
    guard calls one contract whose `SSTORE` key comes from `PUSH0` and a second,
    identical but for `PUSH1 0x00`, and records both outcomes; the second is a
    positive precondition.
  - The coinbase must still be **cold** at Berlin and the Merge (London is
    covered by a fixture case). The guard stores the gas a `BALANCE` of the
    coinbase costs: 2607 cold, 107 warm.
  - Initcode must **not** be word-metered before Shanghai. The guard submits one
    creation transaction priced at exactly the pre-Shanghai intrinsic cost and a
    second one gas short of it, and asserts that exactly the second is rejected.
  - `withdrawalsRoot` must be **absent** from the result at every one of Berlin,
    London and the Merge. Every guard above asserts that too.

## 2. Read these local references

**Normative semantics.** `/app/docs/fork/shanghai/README.md` first, for what
the fork changes at the `t8n` boundary, then the four EIP files beside it:

- `/app/docs/fork/shanghai/eip-3651.md` — the one-line specification of the
  warm-COINBASE rule, in full.
- `/app/docs/fork/shanghai/eip-3855.md` — `PUSH0`: the opcode byte, its gas
  cost and its stack effect.
- `/app/docs/fork/shanghai/eip-3860.md` — `INITCODE_WORD_COST`,
  `MAX_INITCODE_SIZE` and the four rules. Rules 2 and 4 are the metering
  clause; rules 1 and 3 are the limit clause, and each is checked on its own.
- `/app/docs/fork/shanghai/eip-4895.md` — the `Withdrawal` object and its RLP
  schema, `compute_trie_root_from_indexed_data` (which is what
  `withdrawalsRoot` is), the "processed **after** any user-level transactions"
  rule and the "`amount` is given in units of Gwei" conversion. The schema, the
  trie construction and the ordering rule are read out of this file; two of its
  sentences are **not** normative for this task and §1 above overrides both —
  its "a nonzero `amount`" (`:48`), where a zero-amount withdrawal is still a
  withdrawal that occupies its trie position, and its framing of `index` as
  monotonic (`:45-48`), where the in-scope corpus supplies records out of index
  order and the trie is keyed by list position regardless.

`/app/docs/fork/london/` and `/app/docs/fork/berlin/` define the behaviour the
two inherited groups assert and that you must not disturb;
`/app/docs/fork/merge/` is the fork you arrived with.

**The interface.** `/app/TEST_INTERFACE.md` is the command surface and the
result schema. It states where `withdrawalsRoot` sits in the `--output.result`
field set ("present from the fork that defines them": Shanghai), the
`--state.reward=-1` convention every case in scope uses, the fork vocabulary —
`Shanghai` spelled exactly that way — and the exit-status rule that separates a
rejected transaction (zero exit, an entry in `rejected[]`) from a malformed
input document (non-zero exit, no partial output).

**What the in-scope invocations actually look like.** `TEST_INTERFACE.md` fixes
the command surface; these are the concrete shapes this step's cases use, so
that nothing about the harness is a surprise.

- Every in-scope `t8n` invocation passes `--input.alloc`, `--input.env`,
  `--input.txs`, `--state.fork`, `--state.chainid`, `--state.reward`,
  `--output.basedir`, `--output.result` and `--output.alloc`. `--state.chainid`
  is **1** in every case in scope at this step, and `--state.reward` is always
  `-1`.
- `--input.env` field sets, by group. Berlin cases carry `currentCoinbase`,
  `currentDifficulty`, `currentGasLimit`, `currentNumber` and
  `currentTimestamp`. London cases add `currentBaseFee`. Merge cases add
  `currentRandom` and carry a zero-or-absent `currentDifficulty`; three of them
  also carry `ommers`. Shanghai cases carry `currentBaseFee`,
  `currentCoinbase`, `currentDifficulty`, `currentGasLimit`, `currentNumber`,
  `currentRandom`, `currentTimestamp` and `withdrawals`; the
  `shanghai-withdrawals` group additionally supplies `blockHashes`,
  `parentBaseFee`, `parentGasLimit` and `parentGasUsed`. This list is
  *descriptive* — it is what these fixtures happen to carry, not a contract the
  field set is bounded by. A field a fork does not use is simply absent and the
  tool must not require it; a field belonging to a *different* fork is ignored
  rather than refused. `/app/TEST_INTERFACE.md` states both rules.
- `--input.txs` is a signed RLP list (`txs.rlp`) in most cases and a JSON array
  (`txs.json`) in the synthetic ones. A JSON transaction object carries
  `chainId`, `gas`, `gasPrice`, `input`, `nonce`, `to`, `value`, `v`, `r`, `s`
  and **`secretKey`**: when `secretKey` is present the tool signs the
  transaction itself and the `v`/`r`/`s` placeholders are ignored. `"to": null`
  is a contract creation.
- `--output.alloc` is the document most of the groups compare
  account-by-account. It is a JSON object keyed by lower-case `0x`-prefixed
  20-byte address, each value an object with `balance` and, **only when
  non-default**, `nonce`, `code` and `storage`; storage keys and values are
  32-byte zero-padded `0x` hex. An account that was deleted, or that EIP-158
  reaped for being empty, is absent rather than present-and-empty. The
  comparison normalises all of that — a missing `nonce` and `"nonce": "0x0"`
  compare equal, and so do `"0x0a"` and `"0xA"` — so only real differences are
  reported.

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
- How your tool is run, so that none of it is a surprise:
  - First, `/app/build/t8n t8n --help` is run and must **exit 0**. If that
    fails, the whole step is not satisfied regardless of what else is complete, so keep the
    subcommand dispatch working even while a fork is half-finished.
  - Every invocation runs **unprivileged** as uid/gid 65534 with `HOME=/tmp`.
    The tool must not need to write anywhere except the `--output.basedir` it
    is given, and must not need a writable home or a cache directory.
  - Groups run four cases at a time. Each case gets 30 seconds, and each corpus
    group gets `90 + ceil(n/4) * 30` seconds of wall clock overall, where `n` is
    the number of cases in it. The probe group is bounded by its own fixed cap
    (600 s for the group, 120 s per invocation), which is this formula's value
    for its case count rounded generously upward. A build whose startup cost is seconds per invocation
    will exceed its time budget even when it is correct.
  - The step has five gate groups -- proof of
    life, regression against Berlin and London, the malformed-input refusal,
    and the predecessor-fork guard that keeps `withdrawalsRoot` absent at
    London. Every case in a gate group must pass, or the step is not satisfied.
    The remaining requirement groups cover this step's new capability, and
    every case in scope is required. A group that does not finish inside its
    wall-clock budget is not satisfied; for a gate group that means the whole
    step is not satisfied.

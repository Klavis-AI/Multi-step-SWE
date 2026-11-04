# Step 1 — London, and RLP transaction input

## 1. Implement this milestone

`/app` is go-ethereum at **v1.10.0**. Its state-transition tool is correct at
**Berlin** and reads `--input.txs` as a JSON array of transaction objects and
nothing else.

Carry it forward to **London**, and give `--input.txs` a second accepted form.
`--state.fork=London` must be accepted, spelled exactly that way, and must mean
all four of the EIPs below.

- **EIP-1559, the fee market.** `--input.env` carries `currentBaseFee`, and a
  new transaction type joins the two you already read.
  - The type-2 envelope is `0x02 ‖ rlp([chainId, nonce, maxPriorityFeePerGas,
    maxFeePerGas, gasLimit, to, value, data, accessList, yParity, r, s])`. It
    must decode and execute at London through both input forms.
  - The **effective gas price** is `min(maxFeePerGas, baseFee +
    maxPriorityFeePerGas)`, and the sender pays `effective × gasUsed`.
  - `baseFee × gasUsed` is **burned**: taken from the sender and credited to
    nobody. The `currentCoinbase` account must not receive it.
  - The remainder, `(effective − baseFee) × gasUsed`, is the priority fee and
    is credited to `currentCoinbase`. When the base fee squeezes the tip to
    zero the coinbase gets nothing. (`--output.alloc` is compared after
    canonicalisation, so whether that leaves the coinbase as an empty account
    or as no account at all is not a difference; the balance is.) Both arms of
    the `min` are exercised, by a sweep over the base
    fee at a fixed cap and tip: base fees at which `baseFee + tip` stays well
    below the cap, one at which it lands exactly on the cap, one at which it
    exceeds the cap so the tip is squeezed, and one at which the base fee alone
    equals the cap so the tip is zero. The whole post-state `alloc` is compared
    on every one, so a burn and a credit are told apart by the coinbase
    balance.
  - The **up-front debit** is `gasLimit × maxFeePerGas + value`, not
    `gasLimit × effective + value`. A sender who can afford the effective price
    but not the cap is rejected for insufficient funds.
  - A transaction with `maxFeePerGas < baseFee` is **rejected**: one
    `rejected[]` entry carrying its input index, `gasUsed` of `0x0`, the
    sender's balance and nonce untouched, and a **zero** exit. It is not a
    tool-level error.
  - Legacy and type-1 transactions keep working at London. Each is normalised
    as if `maxFeePerGas = maxPriorityFeePerGas = gasPrice`, so the base fee is
    burned out of what they already pay and only the remainder reaches the
    miner. Their balances move at London even though nothing about them
    changed.
  - `currentBaseFee` is an `--input.env` field only. The tool reads it and
    never echoes it into `--output.result`.
- **EIP-3198, `BASEFEE`.** A new instruction at `0x48`: no immediate data, pops
  nothing, pushes `env.currentBaseFee` as a single word, 2 gas (`G_base`).
  Contracts that store it, do arithmetic on it, or combine it with `GASPRICE`
  and `SELFBALANCE` change their post-state, which is what is checked. `0x48`
  must remain **undefined at every earlier fork**, where it is still an invalid
  instruction that aborts the frame.
- **EIP-3529, reduced refunds.** Three clauses, and each is checked on its own:
  - `MAX_REFUND_QUOTIENT` becomes **5**. The refund a transaction may collect
    is capped at `gasUsed // 5`, where it was `gasUsed // 2`.
  - `SSTORE_CLEARS_SCHEDULE` drops from 15,000 to `SSTORE_RESET_GAS +
    ACCESS_LIST_STORAGE_KEY_COST` = **4,800** gas. Clearing a storage slot is
    worth less than it was, and clearing four slots is worth four times less.
  - `SELFDESTRUCT` grants **no refund at all**, where before London it
    granted one.
  - The cases in scope clear one slot, clear four, clear then reset, self-destruct
    alone, self-destruct after a clear, and one in which the `gasUsed // 5` cap
    rather than the schedule is the binding constraint. All compare the
    sender's final balance and the `stateRoot`, so a wrong quotient and a wrong
    schedule are separately visible.
- **EIP-3541, code beginning `0xEF`.** A contract creation — a creation
  transaction, `CREATE`, or `CREATE2` — whose **deployed** code would begin with
  the byte `0xEF` fails with an **exceptional abort**. No account is created,
  and every unit of gas given to that creation is consumed.
  - It is the *returned* code that is tested, not the initcode, and its length
    does not matter: a one-byte `0xEF` and an `0xEF00…` prefix both abort.
  - This is an execution failure, not a validity failure, so it puts **nothing
    in `rejected[]`**. Every case in scope here expects `rejected` to be empty.
    `rejected[]` carries only transactions the tool declines to execute at all —
    a signature that does not recover, a chain id the transaction was not signed
    for, a wrong nonce, intrinsic gas above the gas limit, a balance below the
    up-front cost, a fee cap below the base fee.
  - An abort inside `CREATE`/`CREATE2` leaves the **calling frame running**,
    with zero pushed in place of the new address; the transaction is included
    and has a receipt. A second creation after an aborted one still succeeds,
    and storage written after the abort is still written — so "the creation
    failed" and "the transaction failed" are distinguished.
  - Code that does **not** begin `0xEF` must still deploy at London. That
    positive control is checked beside the rejections, so refusing every creation
    is no better than accepting every one.
- **RLP transaction input.** `--input.txs` gains a second accepted form. There
  is no field naming the format and no flag selecting it: the tool decides from
  the filename's `.rlp` suffix.
  - The file is **still a JSON document**. Its whole content is one JSON *string
    literal* whose value is the `0x`-prefixed hex of an RLP list. Parse it as
    JSON, then hex-decode what that yields; hex-decoding the file's own bytes
    fails, because the first byte is `"`. `/app/TEST_INTERFACE.md` gives the
    exact encoding, including the empty list. Read it before you write the
    reader — the obvious guess is wrong.
  - The list's elements are *signed, encoded* transactions, and the input never
    says which type each is. A legacy transaction is a bare RLP list; a type-1
    (EIP-2930) is `0x01 ‖ payload` and a type-2 (EIP-1559) is `0x02 ‖ payload`,
    each an opaque byte string inside the outer list. All three are in scope.
  - Nothing in the file names a sender. Each sender is **recovered from the
    transaction's own signature**. A signature that does not recover to an
    address makes that transaction `rejected[]` at its input index with a zero
    exit — it is not a malformed input document and not a tool error. The in-scope
    cases carry out-of-range `v`/`r`/`s` for each transaction type.
  - The **JSON array form keeps working**, at London as well as at Berlin, and
    is in scope at both.
- **The result document.** `--output.result` has a fixed field set from this
  step onward, and it is not the one v1.10.0 emits. `/app/TEST_INTERFACE.md`
  states it exactly; what moves at this step is:
  - `receiptRoot` is renamed **`receiptsRoot`**. The old spelling is gone.
  - `stateRoot`, `txRoot`, `receiptsRoot`, `logsHash`, `logsBloom`, `gasUsed`
    and `receipts` are **always** present, on every run that exits 0.
  - `txRoot` and `receiptsRoot` are compared **by value** against the block
    header a conformance fixture pins, not merely format-checked.
  - `logsBloom` is the 256-byte block bloom and is compared by value. With no
    logs it is 256 zero bytes — not `0x0`, not absent.
  - `gasUsed` is the block total, and it must equal the **last** `receipts[]`
    entry's `cumulativeGasUsed`, which is the only receipt field that is compared.
  - `receipts` and `rejected` together account for **every** transaction in
    `--input.txs`, one entry each and no transaction in both:
    `len(receipts) + len(rejected)` must equal the number of transactions in
    the input document, and that identity is checked directly.
  - `rejected` is present only when non-empty; its entries are
    `{"index": N, "error": "..."}`. Only the **index** is compared — the message
    is yours to word.
  - With an empty transaction list, or one in which every transaction was
    rejected, the fields take fixed values rather than going absent:
    empty-trie roots, the empty-log-list `logsHash`, a zero bloom, `gasUsed`
    of `0x0`, and `receipts` of `[]`. `/app/TEST_INTERFACE.md` gives the two
    constants.
- **Exit status.** Exit **0** whenever the transition ran — including a run in
  which *every* transaction was rejected. The in-scope cases include runs of exactly
  that shape. Exit **non-zero** only when an *input document* was malformed or
  could not be read: unparseable JSON, a bad RLP payload, a fork name you do not
  know, or an `--input.alloc` that is missing, unreadable, or not a well-formed
  alloc. On a non-zero exit leave **no partial `--output.result`** behind. Both
  directions are checked: turning a rejected transaction into a tool error, and
  turning a malformed input into a zero exit with an empty result, are both
  wrong.
- **Berlin does not move.** EIP-155, EIP-2565, EIP-2718, EIP-2929 and EIP-2930
  must behave at `--state.fork=Berlin` exactly as they do today. Berlin is
  re-run as two inherited groups — accepted transitions and rejections —
  through the **JSON** input form, asserting `stateRoot`, `logsHash`,
  `rejected` and the post-state `alloc`. A London change that leaks backwards —
  a refund quotient of 5 at Berlin, `0x48` becoming valid there, the `0xEF`
  rule applied before London, a base fee charged at a fork that has none — is a
  regression, and it fails a gate group, which means the step is not satisfied. `Berlin` must also keep being *accepted*
  as a `--state.fork` value: the vocabulary is fixed for all seven steps and a
  fork name that stops being recognised is itself a regression.
- **One inherited behaviour is not right yet, and later steps charge it.** The
  tree at v1.10.0 hands a precompile the caller's argument slice and writes
  what it returns back into memory without copying first, so the **identity
  precompile at `0x04`** — which must return a **copy of its input** — corrupts
  its own result when a call's return region overlaps its argument region. With
  memory `[1,2,3,4]`, a call to `0x04` with `args_offset=0, args_size=4,
  ret_offset=1, ret_size=4` must leave memory `[1,2,3,4,4]`: the four bytes
  written back are the copied input, not the argument region being overwritten
  as it is read. The tree you are handed leaves `[1,1,2,3,4]` instead, and this
  is a divergence in the workspace rather than a London rule: it is out of scope
  at this step, but the conformance fixtures that arrive with the Merge at
  step 3 carry the case, and the same test sits inside a gate group at steps 5
  and 6, where one failing case means the step is not satisfied. It is stated here, where the
  workspace is handed to you, so that it can be fixed once and inherited
  correct. Fixing it costs a copy of the bytes.

## 2. Read these local references

**Normative semantics.** `/app/docs/fork/london/README.md` first, for what the
fork changes at the `t8n` boundary, then the four EIP files beside it:

- `/app/docs/fork/london/eip-1559.md` — the type-2 transaction's field order,
  the `effective_gas_price` formula, the reference implementation showing the
  `gasLimit × maxFeePerGas` up-front debit and the refund of the unused part,
  the "miner only receives the priority fee; the base fee is not given to
  anyone" comment on the settlement step, the normalisation that gives a legacy
  transaction `max_fee_per_gas = max_priority_fee_per_gas = gas_price`, and the
  rule that `GASPRICE` returns the effective price.
- `/app/docs/fork/london/eip-3198.md` — `BASEFEE`: the opcode byte `0x48`, its
  `G_base` cost and its stack effect, in a three-row table.
- `/app/docs/fork/london/eip-3529.md` — `MAX_REFUND_QUOTIENT = 5` and the three
  numbered changes: the removed `SELFDESTRUCT` refund, the
  `SSTORE_CLEARS_SCHEDULE` replacement and its 4,800-gas value, and the
  `gas_used // MAX_REFUND_QUOTIENT` cap. Its "With reduced refunds" table gives
  the exact refund each `SSTORE` sequence earns once `SSTORE_CLEARS_SCHEDULE`
  is 4,800, beside the table for the old value.
- `/app/docs/fork/london/eip-3541.md` — the one-sentence rule, and the
  "Remarks" that make it actionable: it is the *code*, not the *initcode*, that
  is tested, and the abort consumes all gas given to the `CREATE*` or creation
  transaction exactly as any other exceptional abort would.

`/app/docs/fork/berlin/README.md` and the five EIP files beside it define the
behaviour the two inherited groups assert and that you must not disturb.
`/app/docs/fork/berlin/eip-2718.md` is also where the typed-envelope encoding
the new RLP input form carries is specified — `TransactionType ‖
TransactionPayload`, with `TransactionType` a single byte from `0` to `0x7f`
and the payload an opaque byte array whose reading depends on that byte.

**The interface.** `/app/TEST_INTERFACE.md` is the command surface and the
result schema, and it is the **only** source for several things in scope here:
the exact byte encoding of an `.rlp` `--input.txs` file including the empty
list, and the JSON key list for each transaction type in the JSON array form —
including `maxFeePerGas`/`maxPriorityFeePerGas` for type 2 and the split between
the JSON spelling `v` and the `yParity` the EIP text uses for a raw envelope;
the `--output.result` field set, the `receiptsRoot` spelling and the one
`receipts[]` field that is compared (`cumulativeGasUsed`); the values every field
takes when no transaction is included, with the empty-trie root and the
empty-log-list hash written out; `--state.reward=-1`, which every case in scope
passes; the fork vocabulary — `Berlin`, `London`, `Merge`, `Shanghai`,
`Cancun`, `Prague`, `Osaka` — and the rule that a step is checked at its own
fork *and at every earlier one*; the exit-status rule that separates a rejected
transaction from a malformed input document; and the build contract, that
`/app/build.sh` must produce `/app/build/t8n`, which the first attempt does
with `GOPROXY=off` against the prewarmed module cache. `t9n` and `b11r` appear
in that file but are introduced at step 02; only `t8n` is in scope here.

**Additional files.** `/app/WORKSPACE_HANDOFF.md` — what this workspace is,
what the next step receives, how the step is organised (the gate
groups are proof of life and regression and must hold for the step to be
satisfied, while the remaining groups cover this step's new capability), and why the
`github.com/fjl/memsize` replace directive in `go.mod` has to stay.
`/app/README.md` indexes the documentation tree and lists what is out of scope
at every step; `/app/docs/PROVENANCE.md` and `/app/docs/LICENSES/` record where
the EIP files came from.

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

# Step 3 — The Merge

## 1. Implement this milestone

`/app` is the workspace you delivered at step 2: go-ethereum with a
state-transition tool that is correct through **London**, reads `--input.txs`
as either a JSON array or an RLP list, and carries the `t9n` and `b11r`
subcommands.

This step adds one fork. `--state.fork=Merge` must be a fork the tool knows,
spelled exactly that way, and at it the block stops being produced by
proof-of-work.

- **`Merge` joins the fork vocabulary.** The in-scope names are `Berlin`,
  `London` and now `Merge`. Every earlier name must keep working: a step is
  checked at its own fork *and* at every earlier one.
- **The block has no difficulty.** EIP-3675 replaces the header's `difficulty`
  with the constant `0`. So a post-merge `--input.env` may state
  `currentDifficulty` as zero **or omit it entirely**, and both are
  well-formed; the tool must not require it the way a pre-merge env does. An
  env that states a *non-zero* `currentDifficulty` at this fork is a malformed
  input document — there is no proof-of-work seal that could have produced one
  — and EIP-3675 lists `difficulty != 0` among the conditions that make a
  post-merge block invalid.
- **`currentRandom` is required at this fork.** EIP-4399 puts the beacon
  chain's RANDAO mix in the header field numbered 13, the one previously called
  `mixHash`, and `--input.env` spells it `currentRandom`. A post-merge env that
  omits it is a malformed input document. An all-zero `currentRandom` is a
  legal value and **not** an omission: test for the field's presence, not for
  its truthiness.
- **`DIFFICULTY (0x44)` becomes `PREVRANDAO (0x44)`.** At this fork the opcode
  returns `currentRandom` instead of `currentDifficulty`. It returns the whole
  32-byte field — EIP-4399 relies on values above `2**64` being the signal that
  a transaction ran in a proof-of-stake block, so a path that narrows the value
  to a 64-bit quantity is wrong. The opcode's **gas cost does not change**. It
  reads state it cannot modify, so it stays legal inside `STATICCALL`, and
  because it is a block-level field a `CALL`ed frame reads the same value as
  the top-level one.
- **The block reward and the ommer reward are gone.** EIP-3675 removes the
  credit to the beneficiary for producing the block, the per-ommer inclusion
  credit to the beneficiary, and the credit to each ommer's own beneficiary.
  The coinbase receives the EIP-1559 priority fee and nothing else. Ommers
  listed in `--input.env` are paid nothing. (Every case in scope passes
  `--state.reward=-1`, which already means "no block reward accounting"; what
  this requirement protects is that nothing else creeps into the coinbase's
  balance at this fork.)
- **`--output.result` carries `currentDifficulty` at every fork.** The Merge
  removes the quantity, not the key: `TEST_INTERFACE.md` lists it among the
  fields that are *always* present, so it stays in the document at `Berlin`
  and `London` too, where it reports the difficulty `--input.env` supplied.
  From the Merge onward there is no difficulty to report and the key carries
  `null`. The fields `TEST_INTERFACE.md` lists as always present stay present,
  including `receiptsRoot` under exactly that spelling.
- **The exit-status rule is unchanged and applies at the new fork.** A
  transaction the fork rejects is a *result*: exit 0, with an entry in
  `rejected[]`. A malformed input document is a non-zero exit that leaves no
  partial `--output.result` file behind.
- **Berlin and London do not move.** The post-merge env requirements belong to
  the Merge, not to the tool: a London env with a non-zero `currentDifficulty`
  and no `currentRandom` is exactly what a pre-merge block looks like and must
  keep working. A regression at either earlier fork fails a gate group and
  means the step is not satisfied.
- **One inherited behaviour is in scope here that no earlier step's documents
  named.** The **identity precompile at `0x04` returns a copy of its input**,
  so a call whose return region overlaps its argument region is safe: with
  memory `[1,2,3,4]`, a call to `0x04` with `args_offset=0, args_size=4,
  ret_offset=1, ret_size=4` must leave memory `[1,2,3,4,4]` — the bytes written
  back are the copied input — where a tree whose precompile hands back the
  argument slice itself and writes it into memory without copying first leaves
  `[1,1,2,3,4]`. This is not a Merge rule; it is EVM behaviour that holds at
  every fork, one case of it sits in the `merge-blockchain-tests` corpus
  carried forward from the conformance fixtures, and later steps re-run the
  same test inside a gate group, where one failing case means the step is not
  satisfied. It is checked here through the block's `stateRoot`, so a tool that
  has inherited the second behaviour from upstream fails the case until it
  copies.

## 2. Read these local references

**Normative semantics.** `/app/docs/fork/merge/README.md` summarises what
changes at this fork and what is out of scope. The two EIP files behind it are
the primary references:

- `/app/docs/fork/merge/eip-3675.md` — Upgrade consensus to Proof-of-Stake.
  Its "Block structure" table describes the post-merge header: `difficulty` is
  `0`, `ommersHash` is `Keccak256(RLP([]))`, `nonce` is `0x0000000000000000`,
  and the ommer list is empty. **Only the first of those four is enforced at
  the `t8n` boundary.** The other three are properties of a *sealed block*, and
  `t8n` is handed header parts rather than a sealed block: a post-merge
  `--input.env` may list ommers, and one that does is a well-formed input the
  tool must accept and run — EIP-3675 pays no ommer reward, which is the
  requirement above, not a reason to refuse the document. Its "Block and ommer
  rewards" section is the whole of the reward requirement above, in three
  bullets. Its "Replacing `difficulty` with `0`" rationale, and the
  block-validity conditions that include `difficulty != 0`, are why a
  non-zero `currentDifficulty` is malformed at this fork.
- `/app/docs/fork/merge/eip-4399.md` — Supplant DIFFICULTY opcode with
  PREVRANDAO. Its "Block structure" clause is the `mixHash`/`prevRandao` field;
  its "EVM" clause is the one sentence that makes `0x44` return it, and the
  note directly beneath it is the unchanged gas cost. Its Motivation section
  explains the `2**64` threshold that makes the full 32-byte width load-bearing,
  and its "Test Cases" section is the shape of the check that must pass.

**The interface.** `/app/TEST_INTERFACE.md` states the command surface, the
`--output.result` field set and the fork at which each field appears, the fork
vocabulary, the `--state.reward=-1` semantics and the exit-status rule. It is
the authority for the *spelling* of every name at the interface —
`--state.fork=Merge`, `currentRandom`, `currentDifficulty`, `receiptsRoot`.

**Inherited definitions.** `/app/docs/fork/london/` and
`/app/docs/fork/berlin/` remain the definitions your tool must keep satisfying;
`/app/docs/fork/london/eip-1559.md` in particular still governs the fee split
that survives the merge, which is the only thing the coinbase is still paid.

**Additional files.** `/app/WORKSPACE_HANDOFF.md` — what this workspace is,
what the next step receives, and how the step's requirement groups are
organised. `/app/README.md`
indexes the documentation tree.

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

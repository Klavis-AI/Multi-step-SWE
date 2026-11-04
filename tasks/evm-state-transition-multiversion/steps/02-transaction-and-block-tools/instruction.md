# Step 2 — The transaction tool and the block builder

## 1. Implement this milestone

`/app` is the workspace you delivered at step 1: go-ethereum with a
state-transition tool that is correct through **London** and reads
`--input.txs` as either a JSON array or an RLP list.

This step adds no fork. It adds two subcommands beside `t8n`, and a dispatcher
that tells the three apart.

- **`t9n`, the transaction tool.** It decodes a batch of transactions and says
  what it found, without running a state transition. It takes the flags
  `--input.txs`, `--state.fork` and `--state.chainid`; it has **no output
  flags** and its result document is its standard output. The
  document is a JSON array with one entry per input transaction, in input
  order, and an entry has one of exactly four shapes: accepted, under-funded,
  unrecoverable, or undecodable. `/app/TEST_INTERFACE.md` gives the four key
  sets. Unlike `t8n`, `t9n` reads the **RLP form only**: its `--input.txs` is a
  `.rlp` file holding a hex string, and any other path is refused. `t9n` is
  required at **Berlin** and **London**.
- **Intrinsic gas.** An accepted or under-funded entry reports
  `intrinsicGas`: the gas the fork charges before execution begins. It is the
  21000 base, plus the creation surcharge when the transaction has no `to`,
  plus the per-calldata-byte cost, plus the per-access-list-address and
  per-storage-key costs. A transaction whose `gasLimit` is below that number is
  under-funded: it still reports its `address`, `hash` and `intrinsicGas`, and
  also an `error`. It is not a tool failure.
- **An element is not the document.** The framing rule runs around *what
  decodes*: a usable payload decodes as exactly one RLP list, and the input
  document is the **first** JSON value in the file, so bytes after the closing
  quote are not part of it. A payload that will not decode at all — a value
  that is not a list, or a list that is truncated before its end — is a
  malformed input document and is refused by every subcommand, `t9n`
  included; a well-formed but empty list is not refusable, it is zero
  transactions, reported as either an empty array or `null`. An element
  *inside* a well-formed list is a different matter.
  `t9n` walks the list element by element; an element that does not decode
  becomes an entry carrying `error` alone, with no `hash` to report, in that
  element's position, and the run still exits 0. `t8n` and `b11r` decode their
  list as a whole, so for them an element that does not decode is a refused
  input.
- **Chain id is per transaction.** `--state.chainid` is applied to each
  transaction separately. A replay-protected transaction signed for a different
  chain becomes one unrecoverable entry beside its accepted neighbours; it
  never turns the whole run into a tool error.
- **`b11r`, the block builder.** It assembles a block from a header, a
  transaction list and an ommer list, and writes `{"rlp": ..., "hash": ...}`.
  It takes `--input.header`, `--input.txs`, `--input.ommers`,
  `--output.basedir` and `--output.block`. **Sealing is out of scope**: no
  case in scope passes a `--seal.*` flag and no block is mined, so whether
  the tool offers sealing is out of scope here. Like `t9n`, `b11r` reads the RLP form
  of `--input.txs` only. It takes **no `--state.fork` and no
  `--state.chainid`** — assembling a block is not a consensus question. No
  case in scope supplies `sha3Uncles` in the header, so the uncle hash is derived
  from `--input.ommers`; each element of that array is the RLP of a whole
  *block*, not of a bare header.
- **The dispatcher and the error surface.** `t8n`, `t9n` and `b11r` are three
  named subcommands and an unknown one is refused. A malformed *input
  document* — unparseable JSON, an RLP payload whose framing is broken, a fork
  name the tool does not know, a missing input file, a `b11r` header missing
  one of the four fields `/app/TEST_INTERFACE.md` marks required — exits
  non-zero and leaves no partial output behind. A transaction the fork refuses
  is not that: it is a recorded entry and a zero exit. What is required is that
  the tool refuses the input and writes no document; the exit code it chooses
  and the sentence it prints are not part of the requirement.
- **`t8n` does not move.** Berlin and London must behave through `t8n` exactly
  as they do today, through **both** of its input forms — the cases in scope
  feed it the RLP list and the JSON array alike. A regression in either fails a
  gate group and means the step is not satisfied.

## 2. Read these local references

**The interface.** `/app/TEST_INTERFACE.md` is the primary reference for this
step. It states the full command surface for all three subcommands, which of
them accepts which `--input.txs` form, the four `t9n` entry shapes, the `b11r`
output document, the `--output.result` field set, the fork vocabulary and the
exit-status rule. Read it before you write anything.

**Normative semantics.** There is no new fork here, so there is no new EIP to
read. The costs `t9n` must report are the ones already in force.
`/app/docs/fork/london/eip-1559.md` states the intrinsic-gas formula in one
line; `/app/docs/fork/berlin/eip-2930.md` gives the two access-list constants
it names. The creation surcharge is the one term neither file restates, and
`/app/TEST_INTERFACE.md` supplies it. `/app/docs/fork/berlin/eip-2718.md`
defines the typed envelope `t9n` must decode — the same bytes the transaction
`hash` is taken over — and `/app/docs/fork/berlin/eip-155.md` defines what
chain id does to a signature, which is what makes a wrong-chain transaction
unrecoverable. `/app/docs/fork/london/` remains the London definition your
`t8n` must keep.

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

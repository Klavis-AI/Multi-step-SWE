# Step 8 — Chain value pools (ZIP-209), the FlyClient history tree, and ZIP-244 auth digests

## Implement this milestone

`/app` already builds and passes its own tests. Steps 1–7 gave you the V1–V5
transaction codec, Orchard bundles, nullifiers and note commitment trees in the
state, the ZIP-211 MMR primitive under `primitives::zcash_history`, and a
`ValueBalance` type that can describe one transaction's effect on the four value
pools. What is missing is everything that makes those pieces *chain* state: a
block does not yet know its own effect on the pools, there is no history tree
type to carry across blocks, and no transaction can produce the authorizing-data
commitment that NU5 blocks commit to. Your milestone:

1. **Make the four chain value pools a first-class, checked quantity
   (ZIP-209).** `ValueBalance` gains a serialized form, a constraint-changing
   conversion, and the ability to absorb a whole block. The key semantic change
   is `remaining_transaction_value`: this release re-bases the sign convention so
   that a transaction's remaining value is the *sum* of its four pool balances,
   constrained to be non-negative, rather than the transparent balance minus the
   shielded ones. Applying a block's change to a running pool total must fail —
   not wrap, not saturate — when any one of the Sprout, Sapling, Orchard or
   transparent pools would go negative, and the failure must name the pool.
   `/app/specs/zips/zip-0209.rst` is the rule. Its §"Specification" is three
   short paragraphs, and it names *six* pools — Sprout, Sapling, Orchard,
   Ironwood, transparent and the deferred development fund. This milestone
   models four of them: **Ironwood and the deferred development fund are out of
   scope here**, so where the spec says "any of" six, `ValueBalance<C>` has
   four slots and every one of them is checked. The negative-balance rule is
   the one an in-scope group asserts directly. The total-supply rule in the
   second paragraph is not exercised by this milestone's tests — no group
   asserts it. That is a statement about the harness and not about the rule:
   the per-pool `MAX_MONEY` bounds of `TEST_INTERFACE.md` §1 do not by
   themselves close the supply envelope, since four pools each inside that
   range sum to four times it. The third paragraph is permissive, not a rule
   to implement.
2. **Let a block compute its own chain value pool change.** Given the UTXOs its
   transparent inputs spend, a `Block` must report the signed change it makes to
   each of the four pools, which is the negation of the total transaction value
   balance. This needs to walk each transaction's Sprout `vpub_old` and
   `vpub_new` values, its Sapling and Orchard value balances, and counts of
   how many of a block's transactions touch each shielded pool. The two Sprout
   iterators are new public interface and `TEST_INTERFACE.md` §3.2 fixes their
   signatures. The per-pool value-balance accessors already exist from the
   previous milestone and stay private: "reachable" here means reachable
   *in-crate*, which is all the block-level computation needs. Leaving them
   `pub(crate)` conforms, and promoting them to `pub` also conforms — no test
   names them (`TEST_INTERFACE.md` §9).
3. **Split `HistoryTree` into an empty and a non-empty case (ZIP-221).** The
   MMR primitive from step 5 cannot represent "no tree yet", but the chain state
   before Heartwood activation is exactly that. Introduce a non-empty tree type
   carrying the network, the tip height, the peak nodes and the tree size, and
   make `HistoryTree` an optional wrapper around it that is empty before
   activation and created in place at the activation block. Pushing a block that
   is not the immediate successor of the tree's tip is a programming error, not a
   recoverable one. The tree's root is what the *next* block commits to in its
   `hashChainHistoryRoot`; the activation block itself commits to all zero bytes,
   which `/app/specs/zips/zip-0221.rst` says MUST NOT be interpreted as a root.
4. **Compute ZIP-244 authorizing-data commitments.** A transaction must be able
   to produce its `auth_digest`, and a block its `auth_data_root` — the root of a
   binary Merkle tree over those digests, padded to a power of two with
   `[0u8; 32]` leaves, with interior nodes BLAKE2b-256 personalized
   `ZcashAuthDatHash`. Transactions before v5 contribute the 32-byte `0xFF`
   placeholder. `/app/specs/zips/zip-0244.rst` defines both, and the
   `hashAuthDataRoot` construction is in its "Block Header Changes" section.
5. **Sharpen the `Amount` error surface.** The arithmetic that backs all of the
   above must report *why* it failed in a form a caller can match on: an
   out-of-range value with the range it violated, a conversion that does not fit
   in an `i64`, an overflowing multiplication with its wide result, a division by
   zero, and a summation that reports how far it got and how much was left.
   Amounts also gain a fixed 8-byte little-endian serialization, and the
   arithmetic operators must be generic over the constraint rather than fixed to
   one marker type.
6. **Version and bump the on-disk state format.** Chain value pool balances, the
   history tree, and cached note commitment tree roots now live in the finalized
   state. A database written by the previous release cannot be read by this one,
   so `zebra-state`'s format version must be bumped, and the history tree's
   failures must be representable as a contextual validation error. The
   transparent coinbase maturity constant moves down into `zebra-chain` and is
   re-exported, so consensus code can reach it without depending on the state.

## Read these local references

- `/app/specs/zips/zip-0209.rst` — the chain value pool balance rule.
- `/app/specs/zips/zip-0221.rst` — FlyClient; MMR node format, hashing, and the
  all-zero activation-block commitment.
- `/app/specs/zips/zip-0244.rst` — `auth_digest` and the `hashAuthDataRoot` tree.
- `/app/specs/zips/zip-0213.rst` — shielded coinbase; a coinbase output may pay
  into the Sapling pool, which the pool arithmetic has to account for.
- `/app/specs/nu5.txt` §3.4 "Transactions and Treestates" (the transaction value
  pool, and the consensus rule that its remaining value MUST be nonnegative),
  §4.17 "Chain Value Pool Balances", §7.6 "Block Header Encoding and Consensus".
- `/app/TEST_INTERFACE.md` — the contract the tests are written against
  for this step. Every type, module path, method signature, trait impl and error
  variant they touch is named there. Read it before you change `ValueBalance`,
  `Amount::Error` or `HistoryTree`, because all three change shape here.
- `/app/WORKSPACE_HANDOFF.md` — what carries forward from steps 1–7 and what the
  next milestone expects to find.
- `zebra-chain/src/value_balance.rs`, `zebra-chain/src/amount.rs`,
  `zebra-chain/src/history_tree.rs`, `zebra-chain/src/primitives/zcash_history.rs`
  — the retained code this milestone extends.

## Explicit out-of-scope decisions (this step)

- **Outdated-peer rejection.** This release stops handshaking with peers whose
  advertised protocol version is too old for the current chain height. The whole
  change lives inside `zebra-network`'s private `protocol` and `peer` modules,
  and no public symbol or constant changes, so it is out of scope. Implement it or
  not; nothing reads it.
- **Cached note commitment tree roots.** Caching the Sapling and Orchard tree
  roots is an internal memoisation; the roots it returns must not change. No
  in-scope group can observe the cache, only that the roots stay correct.
- **Contextual validation using the new data.** Wiring the pool checks and the
  history tree into the non-finalized state's block-commit path is groundwork
  here: the state service's checks are behind private modules. Only the value
  pool arithmetic, the tree type, and the error conversions are in scope.
- **Halo2 and RedPallas verification** — still pinned external primitives.
- **The `hashBlockCommitments` header field itself.** Deriving the NU5
  three-element commitment from the history root and the auth data root is a
  later milestone; this step produces the two inputs, not the combination.

Two capabilities inherited from earlier milestones are also, deliberately, left
**unchecked** at this step — not because they may be broken, but because no
test this step can run would notice:

- **ZIP-239 inventory relay** (`InventoryHash`, the `wtxid` inventory type and
  the messages that carry it). It lives in `zebra-network`'s `mod protocol`,
  which is private, and inside that in `external::inv`, which is private again;
  the crate re-exports only `protocol::internal::{Request, Response}` and
  `protocol::types::PeerServices`. An integration test sees public API only, so
  there is nothing for it to call. `cargo check --workspace --all-targets
  --all-features` is the only check that reaches it, and compiling is not
  behaviour. Do not break it; nothing will tell you if you do.
- **Wiring the note commitment trees into contextual validation.** The trees
  themselves *are* checked — see the `inherited-note-commitment-trees` gate
  group — but the non-finalized state's block-commit path that consumes them is
  behind private modules and an async service with a real database, for the same
  reason the pool checks above are.

## Meta requirements

- **Budget.** `bash /app/timer.sh` shows elapsed and remaining time.
- **Cumulative workspace.** `/app` carries forward in full (minus `.git/` and
  `target/`) to step 9. Leave it building.
- **Network.** This environment has no network access, and everything the
  build needs is already installed. The upstream project's source, its
  releases, mirrors and package registries are unreachable, so a copy cannot
  be fetched by any route. Searching for, downloading, copying, wrapping or
  invoking an implementation of this milestone — or any task-specific
  answer — is prohibited.
- **Workspace scope.** Tests are added to `zebra-chain/`'s and
  `zebra-state/`'s `tests/` directories when the milestone is checked,
  compiled against the
  manifests as you leave them. `--offline` must keep working; do not add a new
  third-party dependency.
- **Local checks.** `cargo build --offline --workspace` and
  `cargo test --offline -p zebra-chain -p zebra-state` are the checks that matter
  most. `cargo check --offline --workspace --all-targets --all-features` must
  also pass — the benches are built under `--all-targets`.
- **Handoff.** Do not delete `/app/specs/`, `/app/TEST_INTERFACE.md`,
  `/app/WORKSPACE_HANDOFF.md` or `/app/timer.sh`.

# Step 4 — Orchard shielded data, nullifiers in state, and the scriptCode fix

## Implement this milestone

`/app` already builds and passes its own tests: steps 1–3 gave you the V1–V5
transaction codec, the Sapling V5 layout (ZIP-225), and the first Orchard
*structures* (`orchard::Action`, `orchard::Nullifier`, and the point/byte
wrapper types around them). The Orchard *bundle* types this milestone is
built on — `orchard::ShieldedData`, `orchard::Flags`, `orchard::AuthorizedAction`
and `orchard::shielded_data`'s constants — are new here, added by this
milestone along with the codec that makes them reachable. The V5 parser you
inherit only
accepts an Orchard section that is a single zero byte — `nActionsOrchard`
must be `0`, or parsing fails. A real Orchard-shielded V5 transaction, the
kind this milestone is named for, cannot be parsed yet. Your milestone:

1. **Parse and serialize a populated Orchard bundle.** `Transaction::V5`'s
   `orchard_shielded_data` field must round-trip a `ShieldedData` carrying one
   or more real `Action`s — not just the empty case — byte-for-byte, per
   ZIP-225 §7.5's Action layout and §7.1's bundle layout (flags, value
   balance, shared anchor, aggregated proof, one spend-auth signature per
   action, binding signature).
2. **Give `Transaction` and `ShieldedData` working accessors** over that data:
   iterating actions, reading nullifiers out of them, and getting from an
   `AuthorizedAction` to its parts and back. The tests call these
   accessors directly, and `TEST_INTERFACE.md` §3 names every one of them
   with its return type; your job is to make them exist as specified.
3. **Thread Orchard nullifiers into `zebra-state`.** The finalized state
   already commits whole blocks and indexes UTXOs; extend it so that a
   committed block's Orchard-shielded transactions, and the nullifiers their
   actions reveal, are retrievable after commit through the same read paths
   used for any other transaction (by transaction hash, by block hash, by
   height). You are not asked to add a dedicated "is this nullifier spent"
   query in this milestone — see the out-of-scope note below.
4. **Fix the `scriptCode` double-prefix bug.** The sighash algorithm (ZIP-143
   §T.4c / ZIP-243) writes the spent output's `scriptCode` into the digest
   with exactly one compactSize length prefix. The transparent `Script` type
   you inherit stores its bytes as a bare, public field, which forces every
   caller to decide for itself whether a length prefix belongs in that field
   — and `zcash_serialize` for `Script` unconditionally adds one, so a caller
   who (reasonably) pre-baked a prefix in ends up with two. Change `Script` so
   its bytes are private and are never prefixed by the caller, and expose the
   accessors named in `TEST_INTERFACE.md` §5 instead, so exactly one prefix
   is ever written, by the crate.

There is no interpreter change and no proof-system change here: Orchard proof
and signature *validity* is still out of scope (see below); this milestone is
about the bundle's wire encoding, its accessors, and where the data that
encoding carries ends up once a block is committed.

## Read these local references

- `/app/specs/zips/zip-0225.rst` — the V5 transaction format, including the
  Orchard `Action` and bundle byte layout this milestone must parse.
- `/app/specs/zips/zip-0143.rst`, `/app/specs/zips/zip-0243.rst` — the sighash
  algorithms; both write `scriptCode` with a single compactSize prefix.
- `/app/specs/nu5.txt` §3.6 (Orchard notes and nullifiers), §7.1
  (transaction encoding and consensus), §7.5 (Action encoding).
- `/app/TEST_INTERFACE.md` — the contract the tests are written
  against for this step; every type, method, field and byte count they touch
  is named there. Read it before changing `Script`, `ShieldedData` or the
  state's commit path.
- `/app/WORKSPACE_HANDOFF.md` — what carries forward from steps 1–3 and what
  the next milestone expects to find.
- `zebra-chain/src/orchard*`, `zebra-chain/src/transparent/*`,
  `zebra-state/src/service/finalized_state.rs` — the retained code this
  milestone extends. `rg -n 'TODO: Parse orchard into structs' /app` finds
  the exact spot the V5 deserializer currently refuses real Orchard data.

## Explicit out-of-scope decisions (this step)

- Orchard proof and signature verification (Halo2, RedPallas) — still a
  pinned external primitive; only the bundle's *encoding* and *accessors* are
  in scope. (Verification arrives at step 6/12 per `REQUIREMENTS.md`.)
- A dedicated "has this nullifier been spent" state query, and duplicate- or
  double-spend detection — nullifiers become retrievable data at this step,
  not yet a validated set. (`REQUIREMENTS.md` step 6/7.)
- Contextual and semantic validation of the new `CoinbaseHasEnableSpendsOrchard`
  consensus rule — only its `zebra_consensus::error::TransactionError` variant
  is in scope this step; the check that raises it is not part of this
  milestone's public surface (see `TEST_INTERFACE.md` §6).
- Out-of-order, duplicate, or wrong-parent block commits to the finalized
  state — unchanged, still asserted against, not a recoverable error path.

## Meta requirements

- **Budget.** `bash /app/timer.sh` shows elapsed and remaining time.
- **Cumulative workspace.** `/app` carries forward in full (minus `.git/` and
  `target/`) to step 5. Leave it building.
- **Network.** This environment has no network access, and everything the
  build needs is already installed. The upstream project's source, its
  releases, mirrors and package registries are unreachable, so a copy cannot
  be fetched by any route. Searching for, downloading, copying, wrapping or
  invoking an implementation of this milestone — or any task-specific
  answer — is prohibited.
- **Workspace scope.** Tests are added to `zebra-chain/`,
  `zebra-state/` and `zebra-consensus/`'s `tests/` directories when the
  milestone is checked, compiled against the manifests as you leave them.
  `--offline` must
  keep working; do not add a new third-party dependency.
- **Local checks.** `cargo build --offline --workspace` and
  `cargo test --offline -p zebra-chain -p zebra-state -p zebra-consensus` are
  the checks that matter most.
- **Handoff.** Do not delete `/app/specs/`, `/app/TEST_INTERFACE.md`,
  `/app/WORKSPACE_HANDOFF.md` or `/app/timer.sh`.

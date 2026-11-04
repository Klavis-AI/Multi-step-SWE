# Step 1 — the Zcash wire format core

## Implement this milestone

`/app` holds a Zcash consensus node workspace (the `zebra*` crates) that is
seeded but **incomplete**. Four modules of the `zebra-chain` crate — the ones
that define how consensus data is encoded on the wire and on disk — have been
withheld:

- `zebra_chain::serialization` — the encode/decode framework
- `zebra_chain::block` — block, block header, block hash, height, merkle root
- `zebra_chain::transaction` — transaction versions 1–4, lock times, memos,
  JoinSplit and Sapling shielded data containers, signature hashing
- `zebra_chain::transparent` — outpoints, inputs, outputs, scripts, addresses

`zebra-chain/src/lib.rs` still declares all four modules, and 33 retained
files across the workspace already `use` 15 distinct paths from them (13 named
items plus the `block` and `transparent` modules themselves). The
workspace therefore does not compile as delivered. Your milestone is to supply
those four modules so that:

1. `cargo build --offline -p zebra-chain --tests` succeeds, and the whole
   workspace type-checks (`cargo check --offline --workspace --all-targets`)
   against the module surface the retained code already imports — including
   the retained crates' own `#[cfg(test)]` code, which is part of "the whole
   workspace" here;
2. every consensus structure named above round-trips through the Zcash wire
   format byte-for-byte — a real mainnet or testnet block parses, re-serializes
   to the identical bytes, and hashes to its published block hash;
3. malformed and oversized input is **rejected as an error**, never by
   panicking, and deserializing a vector does not pre-allocate from the
   attacker-supplied element count;
4. transaction signature hashes match ZIP-143 (Overwinter, version 3) and
   ZIP-243 (Sapling, version 4).

   (Versions 1 and 2 predate both ZIPs and use a different, Bitcoin-inherited
   signature hash; getting versions 3 and 4 right per the ZIPs above is this
   milestone's goal 4, and is what is checked.)

There is no earlier step; there is no earlier behavior to preserve. Everything
retained in `/app` is yours to keep working — do not delete or rewrite retained
crates to make your own code fit.

## Read these local references

Normative specifications (copied into the workspace; treat them as the source
of truth over anything you remember):

- `/app/specs/nu5.txt` — the Zcash Protocol Specification, NU5 edition, as
  searchable text. Start at **§3.3 (blocks)**, **§3.4–3.6 (transactions,
  transparent transfers, JoinSplits)**, **§5.6.1 (transparent addresses)**,
  **§7.1 (transaction encoding and consensus, including §7.1.2's BIP-34
  coinbase height encoding)**, **§7.6 (block header encoding)**, **§7.12
  (coinbase transactions, including the genesis exception to §7.1.2)**.
  `/app/specs/nu5.pdf` is the same document with figures.
- `/app/specs/zips/zip-0143.rst` — Overwinter signature hash (transaction v3).
- `/app/specs/zips/zip-0243.rst` — Sapling signature hash (transaction v4).
- `/app/specs/zips/zip-0200.rst` — network upgrade mechanism and version
  group IDs.
- `/app/specs/zips/zip-0221.rst` — the chain-history MMR root that the block
  header's reserved 32-byte field carries from Heartwood onwards, and why the
  Heartwood **activation block's** field is all-zero reserved rather than a
  root. That one-block carve-out is in scope; nu5 §7.6's field table cites this
  ZIP for it but does not state it.
- `/app/specs/PROVENANCE.md` — exactly which revision of each document this is.

Workspace references:

- `/app/TEST_INTERFACE.md` — **the contract the tests are written
  against.** Every module path, type name, trait, method signature, constant
  and build command that must keep working is listed there. Read it before
  you write code; it is a specification of the public surface, not a hint.
- `/app/WORKSPACE_HANDOFF.md` — how this workspace is laid out, what carries
  forward to later milestones, and how to restore it in a fresh session.
- `/app/zebra-chain/src/lib.rs` and the 33 retained files that import from the
  withheld modules — `rg -l 'use (crate|zebra_chain)::(block|transaction|transparent|serialization)' /app`
  enumerates the call sites your surface has to satisfy. Both prefixes matter:
  14 of those files are inside `zebra-chain` and reach the modules as `crate::`,
  the other 19 are in sibling crates and reach them as `zebra_chain::` (two of
  the matches — under `zebra-consensus/src/block/subsidy/` — import that
  crate's *own* `block` module, which the pattern cannot tell apart).
- `/app/book/` and `/app/design/` — the project's own design notes.

## Meta requirements

- **Budget.** Run `bash /app/timer.sh` at any time to see elapsed and remaining
  time. The configured timeout is authoritative; the timer is advisory.
- **Cumulative workspace.** `/app` is carried forward in full to the next
  milestone, minus `.git/` and `target/`. Leave it building and leave your work
  in place; a later milestone starts from exactly this directory.
- **Network.** This environment has no network access, and everything the build needs is already installed. The upstream project's source, its releases, mirrors and package registries are unreachable, so a copy cannot be fetched by any route. Searching for, downloading, copying, wrapping
  or invoking an online implementation of this milestone — or any
  task-specific answer — is prohibited. So is inspecting `/tests`, later
  milestones, or any solution material.
- **Workspace scope.** The tests are not in the workspace and you
  cannot read them. They are added to `zebra-chain/tests/` when the milestone
  is checked and
  compiled against the manifests as you leave them, so the workspace must build
  with `--offline` and no manifest edits are needed for the tests to work.
- **Local checks.** Two of the twelve groups run whole-workspace commands
  rather than an injected test file. Those two are *gate* groups: if either
  one fails, the step is not satisfied. The other ten are the requirement
  groups (`/app/TEST_INTERFACE.md` §1 states the same rule). If the build
  command fails, every
  one of those ten is **recorded as failed — not skipped — with the reason
  `workspace did not build`**. So run both locally, exactly as shown — both
  must succeed on the workspace as you leave it, preferably in the
  `--offline --locked` form, with the plain one as the fallback if your
  lockfile no longer resolves:
  `cargo build --offline -p zebra-chain --tests` and
  `cargo check --offline --workspace --all-targets`. Also run
  `cargo test --offline -p zebra-chain` — it will not catch everything the
  workspace-wide `check` does (see `/app/TEST_INTERFACE.md` §2), but it is the
  fastest way to exercise your own code as you write it. You may add your own
  tests anywhere; they are out of scope and not read.
- **Handoff.** Do not delete `/app/specs/`, `/app/TEST_INTERFACE.md`,
  `/app/WORKSPACE_HANDOFF.md` or `/app/timer.sh`.

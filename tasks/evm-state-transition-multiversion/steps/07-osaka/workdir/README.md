# Documentation for Step 07: 07-osaka

This file is `/app/README.md`, the index to the documentation this workspace ships. The EIP specification documents for **Osaka** and every earlier fork are in `/app/docs/fork/`, one directory per fork.

## What is new at this step

The following fork directories contain **specifications for capabilities introduced at this step**:

- `docs/fork/osaka/` — **Osaka**

You must implement these capabilities. Read every EIP file in the new directories and each directory's `README.md` to understand what observable state-transition changes each EIP requires.

## What you inherit

The workspace you received already implements the capabilities documented in these earlier fork directories:

- `docs/fork/berlin/` — **Berlin**
- `docs/fork/london/` — **London**
- `docs/fork/merge/` — **Merge**
- `docs/fork/shanghai/` — **Shanghai**
- `docs/fork/cancun/` — **Cancun**
- `docs/fork/prague/` — **Prague**

You must preserve compatibility with all earlier forks. Every step's graded corpora exercise the tool at **Osaka and at earlier forks**, so a regression in an earlier fork will cost you points. Which forks this step grades, and through which subcommands, is stated in its instruction.

## Tool interface contract

The graded interface is fixed across all seven steps and is documented in **`/app/TEST_INTERFACE.md`**. That file specifies:

- the exact command-line surface for `t8n`, and — from step 02, where they are introduced — for `t9n` and `b11r`;
- the `--output.result` schema, which is fixed from step 01 onward;
- the fork vocabulary (`Berlin`, `London`, `Merge`, `Shanghai`, `Cancun`, `Prague`, `Osaka`);
- the `--state.reward=-1` semantics;
- exit-code rules (0 on success, non-zero on malformed input documents).

**Read `/app/TEST_INTERFACE.md` before you begin.**

## Checkpoint and handoff rules

The **`/app/WORKSPACE_HANDOFF.md`** file documents:

- what the previous step's checkpoint contains (the source tree; `build/` and `.git/` are not carried forward);
- what you must deliver for the next step's checkpoint;
- how the verifier scores your work (one score per step, computed as `gate x score`: a binary gate over the proof-of-life and regression groups, times a weighted pass fraction over this step's scored groups);
- the corpus filtering strategy that ensures a no-op continuation scores exactly 0: its gate passes, because it breaks nothing it inherited, and its score is zero, because it implements none of the new capability.

**Read `/app/WORKSPACE_HANDOFF.md` to understand what you inherit and what you must produce.**

## EIP licence and provenance

Every `fork/<fork>/eip-<n>.md` file is an unmodified copy from the upstream `ethereum/EIPs` repository. See **`/app/docs/PROVENANCE.md`** for the commit hash, clone date, and per-file SHA-256 hashes, and **`/app/docs/LICENSES/EIPS-LICENSE.md`** for the CC0-1.0 licence text.

## How to use these documents

1. **Start with the new fork's `README.md`** to get an overview of what changed at this fork.
2. **Read each EIP file** in the new fork directory to understand the detailed specification.
3. **Read `/app/TEST_INTERFACE.md`** for the exact shape of every document your tool reads and writes; the EIP files say what the rules are, that file says how the rules are observed.
4. **Implement the changes** in your working tree, preserving compatibility with all earlier forks.
5. **Build with `/app/build.sh`** — the same entry point the verifier uses — and exercise the result yourself by running `/app/build/t8n` over inputs you write. This workspace ships no fixture corpus and no test runner; the graded corpus is not visible from `/app`.

## Out of scope

The following are explicitly **out of scope** for this task at every step:

- Consensus-layer state (beacon chain, validators, finality, attestations, slashing).
- Peer-to-peer networking (devp2p, transaction and block propagation).
- Engine API methods and communication with a consensus client.
- JSON-RPC interfaces beyond the `t8n`/`t9n`/`b11r` CLI surface.
- Block-difficulty-bomb and ice-age delay EIPs.
- **EIP-7594** (PeerDAS) — Osaka only; a consensus/networking change with no observable effect at the `t8n` boundary.

One EIP that looks like it belongs on that list does not. **EIP-4788** (beacon block root in the EVM) is **not scored as a new capability at any step** — no step introduces it and no scored group is named for it. It is not unobservable, though: **at step 6** the blockchain-derived corpora ship the beacon-roots contract *with runtime code* at `0x000F3df6D732807Ef1319fB7B8bB8522d0Beac02` (70 files across `prague-requests-7685` and `prague-history-contract`), so the expected post-state there embeds that contract's `storage[timestamp % 8191] = timestamp` write, and a tool that accepts `parentBeaconBlockRoot` without performing the system call will not match it. Treat it as an inherited Cancun requirement, not as excluded work.

**This step's own corpora do not carry that contract.** All 585 cases here were measured: none mentions the address, none stores code at it, and every one of the 300 inherited `env.json` files has `parentBeaconBlockRoot` all-zero, so the system call is a no-op at this step and no group here grades EIP-4788 behaviour. That is not a gap to be closed by adding a case: the corpora at this step are byte-identical inherited fixtures, and editing one to carry the contract would edit an upstream-derived fixture to grade a fork that is not this step's. The requirement is real and is carried by the step that introduces it.

One rule in the London material looks graded and is not. `docs/fork/london/eip-1559.md`
states that `GASPRICE` returns the effective gas price, and step 01's reading
list points at it — but **no scored case distinguishes it**. Every case in step
01's `BASEFEE` group is a legacy transaction, and for a legacy transaction
`gasPrice`, `maxFeePerGas` and the effective price are all the same number, so a
tool that returns `maxFeePerGas` from `GASPRICE` scores exactly the same. The
rule is in that EIP because the fork has it; no group is named for it and none
measures it at step 01.

The verifier scores only what is observable through the `t8n`/`t9n`/`b11r` interface as exercised by the filtered conformance fixtures.

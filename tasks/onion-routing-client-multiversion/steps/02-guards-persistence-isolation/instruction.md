# Step 2 — guards persistence isolation

## 1. Implement this milestone

The guard algorithm with lightweight path bias; persistent shared on-disk state; a circuit-isolation API; circuit-build-timeout inference; `EnforceDistinctSubnets`; refusal of internal addresses; SOCKS `RESOLVE` and `RESOLVE_PTR`; and configurable logging.

**Preserve** the bare client from step 1 - cells, directories and the SOCKS proxy. It is re-checked here against the cumulative workspace, so it has to keep working, not merely have worked. The re-check reaches the cell and relay-cell framing, the microdescriptor and policy parsers, the SOCKS CONNECT path and the RESOLVE query encoding; the parts of step 1 that no group here drives are the ones this step's own contract says are out of scope, and a compile error anywhere in the workspace still means every group fails.


**Migration.** The client facade crate is renamed from `tor-client` to `arti-client`, logging moves from `log` to `tracing`, and the binary grows a subcommand CLI.

Three further renames at this checkpoint are **required** — nothing later in the ladder is laid out any other way — and none of them is derivable from the previous step's workspace:

- **Every crate moves down one level**, from `/app/<crate>` to `/app/crates/<crate>`. Update `members` and every `path =` in every manifest to match. `/app/WORKSPACE_HANDOFF.md` has the detail. The relocation is not itself checked — the checker resolves its own path dependencies against whichever of the two layouts your workspace uses — so leaving the tree flat satisfies that much; the point is that every remaining paragraph of this step's contract, and every later step's, names crates at their new paths.
- **`NetParameters` grows from 7 recognised parameters to 34**, and two existing fields are respelled: the Rust fields `send_me_accept_min_version` and `send_me_emit_min_version` become `sendme_accept_min_version` and `sendme_emit_min_version`. Only the Rust field names change — the consensus keys were already `sendme_accept_min_version` / `sendme_emit_min_version` and are unchanged. `/app/TEST_INTERFACE.md` §4.1 lists all 34 fields with their types, defaults and consensus keys; where it and step 01's contract disagree on a name, §4.1 wins.
- **`pick_path` changes shape incompatibly**: both builders gain a `guards` parameter, `ExitPathBuilder::pick_path` also gains a `&PathConfig`, and both now return a triple rather than a bare `TorPath`. Step 01's two-argument signature will not compile against this step's driver. See `/app/TEST_INTERFACE.md` §3.2.

**The synthetic test network is required relay by relay.** Seven of this step's
groups build every network they use from `tor_netdir::testnet`, so the module's
contents — forty relays, their identities, flags, OR addresses, families, exit
policies and the one consensus parameter the document sets — are required as
directly as any signature. `/app/TEST_INTERFACE.md` §4.3 specifies all of it,
and §0.1 specifies the runtime the driver wraps around it. Neither is described
in step 01's contract, and the network's OR address scheme is **not** what the
previous release used, so this is the other thing at this checkpoint you cannot
derive from the workspace you were handed.


## 2. Read these local references

**Normative semantics:** `/app/SPEC_NOTES.md` states the protocol facts this
step is specified against, each cited to a numbered section of the Tor
specifications with a line-anchored URL. The specification documents themselves
are not shipped with this workspace, and this environment cannot reach them:
the citation identifies the section a fact comes from, and `SPEC_NOTES.md`
states the fact itself, so nothing has to be fetched to satisfy it. Most of
this step is *not* protocol — the persistence layer, the isolation API and the
configuration objects are design decisions rather than specified behaviour —
and `SPEC_NOTES.md` says so explicitly in its closing section rather than
citing an anchor that would not support the claim. For those parts,
`/app/TEST_INTERFACE.md` is the normative document.

**Interface contract:** `/app/TEST_INTERFACE.md` names every public item this
step is checked through — crate, module path, exact signature, and the observable
behaviour each one must produce. Everything in scope is named there. If a behaviour
is not described in `/app/SPEC_NOTES.md` or `/app/TEST_INTERFACE.md`, it is not part
of the requirement, and you should not infer requirements from anywhere else.

**Handoff:** `/app/WORKSPACE_HANDOFF.md` records how the workspace is laid out,
how to build and check it, and what the previous step left behind.

**Earlier steps:** `/app/docs/step-NN/` keeps every earlier step's interface
contract and specification notes. Behaviour introduced in an earlier step is
still in scope here, so those documents remain binding.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied source,
  manifests, documents, and handoff files. Do not create a prose answer or
  alternate entry point unless Section 1 explicitly requires one.
- The pinned toolchain is `rustc 1.98.1`. Build with `cargo build --workspace`
  and keep `Cargo.lock` committed in the workspace: the checker builds from your
  manifests, in the same offline crate set, against the graph that lockfile
  records -- so a build that is green here is green there. This step is
  nevertheless specified against **this release's** dependency versions, and
  they are in `/app/Cargo.lock.release`: the `Cargo.lock` you were handed is a
  snapshot of an earlier rung, and cargo will not move it on its own. Adopt
  that file as `Cargo.lock` and re-check before you finish;
  `WORKSPACE_HANDOFF.md` gives the one-line procedure and says why.
- This environment has no network access, and everything the build needs is already installed. The upstream project's source, its releases, mirrors and package registries are unreachable, so a copy cannot be fetched by any route. Do not search for, download, copy, wrap, or invoke an
  online implementation or task-specific answer; do not inspect `/tests`,
  later-step directories, or solution material.
- Do not edit the supplied documents (`TEST_INTERFACE.md`, `SPEC_NOTES.md`, `Cargo.lock.release`, `docs/`) or weaken, filter, rename, or disable tests.
  Run the local checks named by the references and leave a reproducible
  handoff for the next fresh session.

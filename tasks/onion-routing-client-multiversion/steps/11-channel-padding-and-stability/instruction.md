# Step 11 — channel padding and stability

## 1. Implement this milestone

Channel padding against netflow analysis; consistent zeroization; directory parameters available before the directory is complete; a relay-identity model generalised to sets of possibly-unknown identities; `Send` on runtime TCP streams; a configuration directory; experimental-API gating; and a congestion module.

Three of those clauses are **context for the milestone rather than in-scope surface**, and `/app/TEST_INTERFACE.md` says so where each lives: the congestion module (`tor-congestion` is a workspace member at this release, and the *consumption* half — the `nf_ito_*` parameters the padding timings read — is what this step covers), experimental-API gating (in scope only where it changes an item's reachability, not as a feature-name assertion), and the migration clause below. Build them if you want the milestone to hang together; nothing checks them.

**Preserve** the pluggable cryptographic backends and context-carrying errors from step 10. The error taxonomy is re-checked here against the cumulative workspace, so it has to keep working, not merely have worked; the crypto backends are preserved in the sense that `tor-llcrypto`'s public-key API must keep compiling and behaving, which every group here depends on — the backend-*selection* surface is feature-gated off in the checked build and no group reaches it.


**Migration.** The client is declared stable, and the binary API moves behind a feature flag.


## 2. Read these local references

**Normative semantics:** `/app/SPEC_NOTES.md` states, in full, the protocol
facts this step is specified against — field widths, command numbers, field order,
digest and key-derivation construction, and document grammar — each one cited to
a numbered section of the Tor specifications with a line-anchored URL. The
specification documents themselves are not shipped with this workspace; fetch
any you want to read in full from the cited URLs.

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

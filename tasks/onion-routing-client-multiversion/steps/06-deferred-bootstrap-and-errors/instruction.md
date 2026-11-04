# Step 6 — deferred bootstrap and errors

## 1. Implement this milestone

Configuration-file watching; expiry of unused channels; an unbootstrapped `TorClient` with deferred and lazy bootstrap; a structured error taxonomy; an HTTP client over Arti; and a diagnostic for HTTP sent to the SOCKS port.

**Preserve** bootstrap status reporting and the selectable TLS backends from step 5. It is re-checked here against the cumulative workspace, so it has to keep working, not merely have worked.


**Migration.** The client API is declared supported, and errors gain stable kinds.


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
still in scope here, so those documents remain binding. Where an earlier contract
and `/app/TEST_INTERFACE.md` describe the same item differently, **this step's
contract wins**: it is the current one, and where a shape actually changed it
says so at the point of declaration. The changes it calls out are:

- §5 — `tor_chanmgr::Error`, whose variant shapes changed at this step.
- §4 — `LoggingConfig::console_filter`, which replaced step 03's `trace_filter`.
- §7.1 — the inherited error taxonomies, whose declaration blocks supersede
  step 01 §2 and step 04 §11 for `tor_bytes::Error`, step 04 §10.0 for
  `tor_cell::Error`, step 02 §8.2 for `tor_circmgr::Error`, and step 02 §1.6
  for `tor_persist::Error` (including a `From` impl that no longer exists).
- §8 — the `tor-rtcompat` runtime constructors and the `TorClient` bootstrap
  shortcuts, superseding step 05.

That is the list of changes this document *calls out*; it is not a guarantee
that nothing else moved between releases. The rule above is the one that always
holds: where an earlier contract and this one describe the same item
differently, follow this one.

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

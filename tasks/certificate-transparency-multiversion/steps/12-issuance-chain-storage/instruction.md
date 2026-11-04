# Step 12 — issuance chain storage (certificate-transparency-go v1.2.0)

## 1. Implement this milestone

`/app` already holds the stack you delivered through step 11 at certificate-transparency-go v1.1.4: the TLS presentation-language codec and the forked X.509 and ASN.1 parsers; the RFC 6962 structures; `ctutil`, with the Cobra `ctclient` and the fork-free scanners beside it; the witness service and the log-list reader that replaced the two older generations of it; `ctpolicy`, `submission`, `schedule` and `migrillian`; and the front end, `trillian/ctfe`, carrying quota accounting, an overridable handler prefix, a frozen STH, read-only mode and the admission rules that were inverted two steps ago. Continue that implementation in place: this step extends the tree rather than replacing any part of it, so everything just listed must keep working exactly as it does now, and is re-checked.

This milestone — issuance chain storage, at v1.2.0 — is about not writing the same bytes twice. The extra data of a queued leaf has always held the entire issuance chain, the intermediates and the root, once per submission, even when a thousand submissions in a row share the same one. The release adds a second arrangement: the leaf holds only a hash of the chain, and the chain itself is kept in a store the front end owns, looked up when the leaf is read back out. Whatever a client asks for, it gets the same bytes it would have got before, so the change is invisible on the wire and visible only in what the backend holds.

The property that decides whether you have built this correctly is that the new arrangement is *optional and per log*. There is a new field on each log's configuration with a nested enum; its zero value is the behaviour you already have, and every configuration written in the previous steps leaves it at zero. A log on the default keeps the whole chain in the leaf, byte for byte, even if a cache is configured and a store exists alongside it. A log switched to the front end puts a hash there instead. Both layouts are live at once, in the same binary, and the two paths must agree on the Merkle leaf, the leaf hash and the identity hash of a submission while differing in exactly that one field — anything else would silently invalidate entries an existing log has already accepted.

The storage side is deliberately separable and worth doing first, because none of it needs a server to be running. There is an interface with a lookup by key and an add; a factory that turns the configured backend and connection string into an implementation, where a nil implementation is the signal that a log is not using front-end storage rather than an error to be reported; a cache package with a selector type, an LRU implementation and a no-op one, each constructed directly as well as through the factory; and the validation that refuses a cache configuration it cannot honour before anything is bound to a port. The key a chain is filed under is derived from the chain itself, so identical chains submitted twice are stored once — that is the whole point of the exercise, and the reason the release claims a substantially lower storage cost for a log that opts in. The benchmark never opens a database server, and nothing here needs one: what is examined is the interface, the cache's behaviour as observable values, the configuration and flag surface, and the leaf-shape contract.

Four smaller things arrive with the same release. Keys configured for a log are cross-checked at start-up, so a public key that is not the public half of the configured private key stops the process, and so does one private key shared by two logs. Backend status codes are remapped in two places, so a backend that has run out of quota or timed out is reported to the client differently than it was before — both are required exactly. The older of the two remaining log-list generations is deleted, leaving one, so a probe that imports the removed path must now fail to build while the survivor must not — and the survivor gains a single field. And on the client side of the same product, the upload transport stops reading a redirect that converted a POST into a GET as a successful submission.

**Hard prerequisite for step 13:** the storage interface, its factory and the two cache implementations must be in place with the leaf-shape contract intact — a nil store meaning "keep the chain in the leaf", and a stored leaf rehydrating to byte-identical extra data. Step 13 adds a second driver behind the same interface, splits the rehydration path in two and changes what one of the two writes, moves its transport to TLS, and deletes the witness programs. None of that can be built on a storage layer that is half-wired or that has changed what an existing log's leaf looks like.

This milestone is checked by a set of **independent groups**, each run in its own compilation unit or as its own operating system process with a fresh tree state; `TEST_INTERFACE.md` §7 lists them. Each group is checked on its own. Groups check behaviour across the boundary — process exit statuses, HTTP response codes and bodies, byte-comparison of serialised structures, the values a cache returns, and the absence of a package — and never the text of an error message except where that text is part of an HTTP response body, and never the name or shape of anything unexported. Some of the behaviour they check is inherited from earlier steps and is checked again here, and those are the **gate** groups: if the front end no longer starts, or something deleted earlier has come back, the step is not satisfied, regardless of the new work.


---

## 2. Read these local references

Everything you need is already in `/app`. Nothing here requires the network.

Two contract files first, in this order:

1. **`/app/TEST_INTERFACE.md`** — the interface contract: the module path, the
   build command, the import paths and symbols drivers link against, the
   programs they run, the formats crossing the boundary, and what a group
   checks. Read sections 1 to 3 before you write anything, section 7 for the
   group list, and section 10 last.
2. **`/app/WORKSPACE_HANDOFF.md`** — what `/app` is, what is overlay-owned,
   what carries into the next step, and the checkpoint to run before you stop.

Then the corpus in `/app/ct-spec/`, which is the only description of the
behaviour you are implementing. `/app/ct-spec/README.md` indexes all of it; the
pages and sections that matter at this milestone are:

| Read | For |
| --- | --- |
| `ct-spec/RELEASE-MAP.md` §12 | the milestone on one screen, including the sentence that both leaf layouts are live at once and existing logs are unaffected |
| `ct-spec/RELEASE-MAP.md` §14 | the structural signal that tells you which generation of the log-list packages you should have |
| `ct-spec/CLIENT-API.md` §8.4 | the storage and cache surface: both interfaces, both factories, the selector type and its constants |
| `ct-spec/CLIENT-API.md` §8.1, §8.2 | the instance options the cache selection joins, the shape of instance set-up, and the one accessor the instance gains here |
| `ct-spec/CLIENT-API.md` §8.7 | leaf construction in both shapes: the pair that takes a chain, and the pair this release adds that takes the chain's hash |
| `ct-spec/CLIENT-API.md` §4.4 | which log-list generation is deleted here, and which one survives |
| `ct-spec/CLIENT-API.md` §4.3 | the one field the surviving log-list package gains at this release, and its json tag |
| `ct-spec/CLIENT-API.md` §1.1 | the upload transport: the client constructor, the POST entry point and its three return values, and the redirect guard this release adds to that entry point |
| `ct-spec/CONFIG.md` §1, §1.1 | the two new fields and their numbers, and why the field set itself is observable |
| `ct-spec/CONFIG.md` §2.6 | what the backend selection means, the validation it carries, and the promise that the wire format does not change |
| `ct-spec/CONFIG.md` §2.3, §4.3 | the key-consistency rules, and every per-log validation message including the three that arrive here |
| `ct-spec/CLI.md` §1 | the program set: what must still exist, and which directory each program lives in |
| `ct-spec/CLI.md` §2.1 | the three cache flags, their defaults, and the flag total for this release |
| `ct-spec/CLI.md` §2.2 | the order in which start-up fails, and which failures must happen before a socket is bound |
| `ct-spec/CLI.md` §4.8 | the flag this release withdraws from the load generator |
| `ct-spec/HTTP-API.md` §6.1 | the two cells of the backend-error map that move here, and what they were before |
| `ct-spec/HTTP-API.md` §2, §2.1 | the request wrapper and the error body both remapped statuses still use |
| `ct-spec/TLS-CODEC.md` §5.4 | the chain wrappers the two hashed structures sit beside, and the tags they inherit |

`/app/ct-spec/PROFILE.md` is written for this step specifically: what this
milestone adds over the previous one, in the product's own vocabulary, with a
reading route through the corpus. Read it before the pages above.

Finally, `/app/WORKSPACE_HANDOFF.md` tells you what state to leave `/app` in
and gives you a copy-pasteable checkpoint; run it before your time runs out.

---

## 3. Meta requirements

- **Budget**: 7,200 seconds. `bash /app/timer.sh` shows the remaining time;
  the configured timeout is authoritative and resets each step. The corpus in
  `/app/ct-spec/` is 12 pages and about 0.8 MB — read
  the two contract files and the sections named above, not the whole set.
- **Cumulative workspace**: work in `/app`. Everything you leave there is what
  step 13 starts from, so keep it building at every point you might
  be interrupted — the snapshot is taken when the clock stops, not when you
  decide you are finished. Do not delete or move `ct-spec/`,
  `TEST_INTERFACE.md`, `WORKSPACE_HANDOFF.md`, `timer.sh`, `go.mod` or
  `go.sum`: those six are overlay-owned and are refreshed every step.
  Everything else under `/app` is yours and is preserved. Compiled binaries are
  not carried forward; the sources are.
- **Network**: This environment has no network access, and everything the build needs is already installed. The upstream project's source, its releases, mirrors and package registries are unreachable, so a copy cannot be fetched by any route. Do **not** search for, download, copy, wrap or
  invoke an online implementation or a task-specific answer. Checking itself is
  offline and the dependency set is already pinned in `/app/go.mod`, so there
  is nothing you need to fetch in order to build.
- **This applies with full force to certificate-transparency-go itself.**
  `github.com/google/certificate-transparency-go` is Apache-2.0 licensed and is
  one `git clone` away, as are its forks, its vendored copies inside other
  projects, and the Go module proxy's zip of every version of it. Vendoring,
  transcribing or reconstructing upstream source — in whole or in part, by any
  route, **including from memory** — is prohibited and defeats the point of the
  task. The shipped documentation in `/app/ct-spec/` describes the *behaviour*
  you must reproduce, down to the wire formats and the exact bytes where they
  matter; the implementation must be yours.
- **Off-limits**: do not inspect `/tests`, later step directories, fixtures
  outside the workspace or solution material, and do not weaken or disable
  any check.
- **Handoff**: leave `/app` in a reproducible state — `go build ./...` succeeds
  offline from the sources in the workspace, and the `WORKSPACE_HANDOFF.md`
  checkpoint passes.

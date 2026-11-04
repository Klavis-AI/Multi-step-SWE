# Step 11 — log-list v1 removal (certificate-transparency-go v1.1.4)

## 1. Implement this milestone

`/app` already holds the stack you delivered through step 10 at
certificate-transparency-go v1.1.3 — the TLS codec, the forked
`asn1` and `x509`, the RFC 6962 structures, the front end with its admission
rules and read-only mode, quota, `migrillian`, the SCT-submission proxy and
its policies, `ctutil`, the `ctclient` subcommand tree, the witness tree, and
three log-list packages living side by side. Continue that implementation in
place.

This milestone, certificate-transparency-go v1.1.4, is a withdrawal. It
is the smallest release in the series by new behaviour and one of the easiest
to get subtly wrong, because the question it asks is not what to delete but
where the survivors go.

Delete the v1 log-list package. That is the package rooted at the import path
`github.com/google/certificate-transparency-go/loglist`: the flat schema with
a `Logs` array and an `Operators` array joined by integer ids, with a log's
life expressed by the optional `final_sth` and `disqualified_at` fields, plus
its filtering helpers, its bundled list-signing public key, and the
`findlog` command underneath it, which existed only to search that schema.
After this step that import path must not resolve for anything that imports it
and must not be a build target; a program that names it must fail to build
with the toolchain saying so.

Move its dependants to **v3**, not to v2. The v2 package is right there, it
still compiles, and it is the wrong answer. Retype `ctutil`'s `NewLogInfo` and
`LogInfoByKeyHash` onto the v3 log and log list, reading the merge delay from
the v3 field; follow them through `sctcheck`, whose log-info factory is typed
on them, and through `sctscan`; move the `ctclient` root command's persistent
`--log_list` default and the hammer's log-list handling across. Both of those
resolve a log *by name* against the list they have loaded, and that resolution
has no observable except its own refusal: a `--log_name` or `--src_log_name`
matching no log, or matching more than one, must be refused with a diagnostic
naming every log **description** it matched, in the order the list gives them,
and naming the list it actually read. In the same release move the remaining v2
consumers over as well — `ctpolicy`, whose `LogsByGroup`, and `submission`,
whose `NewDistributor` and `LogClientBuilder`, end up declared over the v3
types so that a v3 list is what flows through the proxy, and the two witness
tools, `feeder` and `config`, which swap the v2 parser for the v3 one behind a
`--log_list_url` whose default already names a v3 document, so nothing about
their flags moves. Point the flag defaults that do name a v1 or v2 log-list URL
at the v3 constants, the submission server's `--loglist_path` among them.
That server is exercised here over a loopback socket under its `--dry_run`
flag, and what that flag does is stated in full in `TEST_INTERFACE.md` §4: it
is not new at this release, but it is how the proxy is reached without a
network, so if what you built for it earlier behaves differently, this is
where that shows.
Leave the v2 package itself in the tree, compiling, with no consumers.

Do the rest of the release around that: replace the logging library with the
one this release adopts, which keeps every exit status and changes the flag
names each program carries — and leaves a program that never installs the
logger's flag set carrying no logging flags at all; move the
dependency-tracking file into a `tools/` directory of its own; move the YAML
library the witness configuration tool marshals with up to the major version
this release adopts, which changes the indentation of the block sequences it
writes — the file it produces is compared byte for byte, so that is a
behavioural change and not a cosmetic one; and stop `migrillian` asking for a consistency
proof when the source root is size zero, since an empty tree is consistent with
any root and the proof is empty.

**Hard prerequisite for step 12:** step 12 deletes the
*second* log-list package the same way you delete the first here, and it can
only do that cleanly if no consumer is left on it. If you migrate v1 to v2
instead of to v3, or leave the v1 package on disk while retyping its callers,
the next step inherits two removals and a half-finished one instead of its own
milestone, which is issuance-chain storage and the cache in front of it. The
policy and proxy interfaces must already be declared over v3 when that step
starts.

A set of **gate** groups re-runs what earlier steps already signed off,
together with proof that your tree builds and the artifact starts. If any of
them fails, the step is not satisfied, however much of this milestone you
implemented. Beyond the gate, this milestone's new behaviour is required in
independent groups; `TEST_INTERFACE.md` §7 lists them. A change that breaks
earlier work means the step is not satisfied, and new work must actually be
present. The removal and each
retyped consumer are separate groups, so a migration that is complete in one
package and missing in another still leaves the second unsatisfied — and a
tree that does not compile satisfies nothing, which is why the retyping comes
before the deletion.

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
| `ct-spec/CLIENT-API.md` §4.4 | the removal and where every consumer lands — the one section to read twice |
| `ct-spec/CLIENT-API.md` §4.1 | the v1 schema you are deleting |
| `ct-spec/CLIENT-API.md` §4.2 | the v2 schema, which survives this release and is not the destination |
| `ct-spec/CLIENT-API.md` §4.3 | the v3 schema, its types and its selection helpers |
| `ct-spec/CLIENT-API.md` §2.1 | `ctutil`'s `LogInfo` entry points and the argument type that changes under them |
| `ct-spec/CLIENT-API.md` §7.1, §7.2 | `ctpolicy` and `submission` declared over the v3 list |
| `ct-spec/CLIENT-API.md` §6.3 | the witness tree, whose configuration tool reads a log list |
| `ct-spec/CLI.md` §1, §4.5 | the program inventory, and the command that leaves with the package |
| `ct-spec/CLI.md` §3.2, §4.4, §4.9 | the `--log_list` and `--loglist_path` defaults that move to the v3 constants |
| `ct-spec/CLI.md` §4.10, §5.1 | the witness programs, which read a log list too, and the exact YAML the configuration tool writes |
| `ct-spec/CLI.md` §2.1 | `ct_server`'s flag set, unchanged at this release |
| `ct-spec/CLI.md` flag-grammar and exit-status preamble | the logging swap, and the exit statuses that do not move with it |
| `ct-spec/MERKLE.md` §3 | the empty-tree consistency case `migrillian` starts relying on |
| `ct-spec/RELEASE-MAP.md` §11, §14 | the milestone on one screen, and which directories must and must not exist |

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
  step 12 starts from, so keep it building at every point you might
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

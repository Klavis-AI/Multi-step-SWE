# Step 9 — flag lifecycle and surface withdrawals (certificate-transparency-go v1.1.2)

## 1. Implement this milestone

`/app` already holds the stack you delivered through step 8 at
certificate-transparency-go v1.1.0 — the TLS codec, the forked
`asn1` and `x509` with the `lax` tag and its three relaxations, the RFC 6962
structures, `ctutil`, the log-list packages, the client, the utilities, and a
front end whose admission rules were inverted at that milestone, with
`reject_extensions`, frozen STHs, `/healthz` and masked internal errors.
Continue that implementation in place.

This milestone, certificate-transparency-go v1.1.2, is mostly
subtraction, and the one flag it is famous for both arrives and leaves inside
it.

Finish `by_range`. It chose which backend RPC served `get-entries`; by the end
of this step it must not exist, and the range-based path must be the only one.
In its place add `--align_getentries`, defaulting to **true**, which trims an
over-wide or unaligned range down to the next multiple of the maximum so that
long sequential fetches converge on shared boundaries — a coercion, not a
refusal — and report what it did on a counter labelled by log id and by
whether trimming was needed. Both happen in the handler's own range parsing,
before any backend call. Then make the four endpoints whose answers never
change send an exact `Cache-Control` header, and leave the two whose answers
grow without one.

Make EKUs an admission rule. A non-empty configured usage list must be tested
against the leaf's own extension before path building, refusing a leaf that
shares no entry with it — including a leaf that carries no EKU extension at
all — and an explicit `Any` anywhere in the list must clear the list rather
than join it, so that an empty validated list means "do not filter".

Teach the configuration parser a second encoding: a file that does not parse
as text proto is retried as binary protobuf, and only a file that is neither
fails, with a message naming both attempts. Split the whole-set checks that a
single-backend configuration needs into a validator of their own, whose
duplicate-tree-id check is global — one tree id per set, because a bare
`LogConfigSet` has no backend names to key on. The older multi-backend
validator is untouched: its key stays per backend, so the same tree id on two
different backends is still legal.

Withdraw CT-over-DNS entirely — the package, the log-info constructors built
on it, and the flags in all three programs that offered it — along with the
client's JSON-submission method and everything in the gossip tree except the
extension type that carries an STH. Add a verification entry point that takes
a caller-supplied signature verifier and rejects a nil one, and a PKCS#11 key
handler gated behind a new flag. Finally, absorb two Go releases into the
forked parsers: Ed25519 end to end with algorithm parameters required absent,
DSA digests truncated to the subgroup order, a nil-safe certificate
comparison, and `BMPString`.

**Hard prerequisite for step 10:** step 10 rebuilds
`ctclient` as a subcommand tree and moves a shared certificate-pool type
between packages. Both are refactors of things this step must leave clean: a
`ctclient` still carrying withdrawn DNS flags, or a tree that still imports
the deleted DNS package anywhere, turns that rebuild into a repair job.

One removal is easy to miss because nothing in this milestone's feature list
points at it, and it is the one that costs the most if you skip it: the
deprecated `google.golang.org/grpc/naming` package is **not in the pinned
dependency set at this milestone**. `trillian/util/resolver.go` and
`resolver_test.go` — `FixedBackendResolver` and the `naming.Watcher` behind it
— import it, so they must be **deleted**, and `ct_server` must stop announcing
its endpoints through `github.com/coreos/etcd/clientv3/naming` and use etcd's
own v3 API instead: `go.etcd.io/etcd/client/v3` and
`go.etcd.io/etcd/client/v3/naming/endpoints`. That also relocates the etcd
import path off `github.com/coreos/etcd`. Leaving `resolver.go` in place is
not a localized problem: `trillian/util` then fails to build, and so does
everything importing `trillian/ctfe`, which is most of the surface this
milestone covers.
`trillian/util` keeps only `log_leaf.go` and `timesource.go`.
ARCHITECTURE.md §2.1 and RELEASE-MAP.md §9 describe the change.

A set of **gate** groups re-runs what earlier steps already signed off,
together with proof that your tree builds and the artifact starts. If any of
them fails, the step is not satisfied, however much of this milestone you
implemented. Beyond the gate, this milestone's new behaviour is required in
independent groups; `TEST_INTERFACE.md` §7 lists them. A change that breaks
earlier work means the step is not satisfied, and new work must actually be
present. The removals are checked by
compilation and by command-line behaviour, not by looking for files: a
deleted package that something still imports is a build failure, and a build
failure undermines every requirement that depends on that compilation unit.

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
| `ct-spec/CLI.md` §2.1 | the 19 flags — which one leaves, which two arrive |
| `ct-spec/HTTP-API.md` §4.3 | range coercion, alignment, and the counter |
| `ct-spec/HTTP-API.md` §4.7 | which endpoints become cacheable, and which do not |
| `ct-spec/HTTP-API.md` §5.4 | the leaf-only EKU filter, and that a refusal never reaches the backend |
| `ct-spec/CONFIG.md` §2.4 | what an EKU list means after validation, including `Any` |
| `ct-spec/CONFIG.md` §4.1, §4.2 | the new whole-set validator; text proto then binary |
| `ct-spec/CONFIG.md` §4.4 | the two duplicate-tree-id rules: global in the new validator, still per backend in the multi-backend one |
| `ct-spec/CLIENT-API.md` §2 | `VerifySCTWithVerifier` |
| `ct-spec/CLIENT-API.md` §6.1, §6.2 | the DNS client being withdrawn; what survives of gossip |
| `ct-spec/CLI.md` §3.1, §4.4 | the DNS flags leaving `ctclient`, `sctcheck` and `sctscan` |
| `ct-spec/X509-FORK.md` §2.4, §5.5 | `BMPString`, Ed25519, DSA truncation, nil-safe `Equal` |
| `ct-spec/CONFIG.md` §2.3 | the key-handler registry and the PKCS#11 entry |
| `ct-spec/ARCHITECTURE.md` §2.1 | the `grpc/naming` resolver being withdrawn: what `ct_server` dials with instead, and which two files go |
| `ct-spec/RELEASE-MAP.md` §9 | the milestone on one screen |

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
  step 10 starts from, so keep it building at every point you might
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

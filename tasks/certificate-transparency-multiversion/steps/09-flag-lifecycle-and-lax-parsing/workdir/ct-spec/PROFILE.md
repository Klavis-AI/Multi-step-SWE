# PROFILE — step 09 of 13: flag lifecycle and surface withdrawals (certificate-transparency-go v1.1.2)

## What this milestone adds over v1.1.0

**A flag's whole life in one step.** `by_range`, which chose the backend RPC
for `get-entries`, is defaulted true and then deleted; the range-based RPC
becomes the only path. Grading is on the end state: the flag is gone and a
process given it exits 2.

**Coercion with a receipt.** `--align_getentries`, default true, trims an
over-wide or unaligned `get-entries` range down to the next multiple of the
maximum so that big sequential fetches share cache boundaries, and increments
`aligned_get_entries{logid,aligned}` to say whether it had to. Both happen in
range parsing, before any backend call.

**Cacheability.** The four endpoints whose answers never change —
`get-sth-consistency`, `get-proof-by-hash`, `get-entries`,
`get-entry-and-proof` — start sending `Cache-Control: public, max-age=86400`.
`get-sth` and `get-roots` do not.

**EKUs become an admission filter.** A non-empty configured list is now tested
against the leaf's own EKU extension before path building, refusing a leaf
that shares nothing with it, and an explicit `Any` clears the list instead of
joining it.

**Configuration gets a second encoding.** A file that does not parse as text
proto is retried as binary protobuf, and only a file that is neither fails —
naming the message type it could not build and the file it read. `ValidateLogConfigs` splits the whole-set
checks for a bare `LogConfigSet` out into a second validator; it is handed no
backend names, so its duplicate-tree-id check is **global** — one tree id per
set. `ValidateLogMultiConfig` is not touched: it has keyed tree ids
`"<backend name>-<tree id>"` since v1.0.10 and still does, so the same tree id
on two *different* backends stays legal (CONFIG.md §4.4).

**Withdrawals.** `dnsclient/` and every DNS flag and `LogInfo` constructor
that depended on it; `client.AddJSON`; the whole `gossip/` tree except
`minimal/x509ext`. Three programs disappear and the count falls to 15.

**Do this one first, because nothing else builds until it is done.** The
deprecated `google.golang.org/grpc/naming` is not in the pinned dependency set
at this milestone, so `trillian/util/resolver.go` and `resolver_test.go` —
`FixedBackendResolver` and the watcher behind it — are deleted, and `ct_server`
announces its endpoints through `go.etcd.io/etcd/client/v3` and
`…/client/v3/naming/endpoints` instead of `github.com/coreos/etcd`. The gossip
files above are in the same position: the modules they import leave the
manifest too. Until all of that goes the tree does not compile, so no binary
exists and nothing else you implement can be observed (ARCHITECTURE.md §2.1 and
RELEASE-MAP.md §9 describe the change; TEST_INTERFACE.md §3.5 says what it
costs).

**Additions elsewhere:** `ctutil.VerifySCTWithVerifier`, which verifies
against a caller-supplied verifier and rejects a nil one; a PKCS#11 key
handler gated on `--pkcs11_module_path`; the v2 log-list URLs corrected from
`v2beta` to `v2`; and the Go 1.13/1.14 merge into the forked parsers —
Ed25519, DSA digest truncation, a nil-safe `Certificate.Equal`, `BMPString`.

## Reading route through ct-spec/

1. **ARCHITECTURE.md** §2.1 — the `grpc/naming` withdrawal and what `ct_server`
   dials with instead. First, because it is the one change the build stops for.
2. **CLI.md** §2.1 — the 19 flags: what arrives, what leaves.
3. **HTTP-API.md** §4.3 — coercion, alignment and the counter.
4. **HTTP-API.md** §4.7 — which endpoints are cacheable.
5. **HTTP-API.md** §5.4 — the EKU filter, and **CONFIG.md** §2.4 for what the
   list means after validation.
6. **CONFIG.md** §4.1, §4.2, §4.4 — `ValidateLogConfigs`, the binary fallback,
   and which of the two validators keys duplicate tree ids per backend and
   which rejects them globally.
7. **CLIENT-API.md** §2 — `VerifySCTWithVerifier`.
8. **CLIENT-API.md** §6.1, §6.2 — what `dnsclient` was, and what survives of
   gossip.
9. **X509-FORK.md** §2.4, §5.5 — `BMPString` and the Go merge.
10. **RELEASE-MAP.md** §9 — the milestone on one screen.

## How you will know you are done

The dead flag is unknown to every binary and the range-based path is
unconditional; an over-wide range comes back trimmed to a boundary and the
counter says so; the four cacheable endpoints carry the exact header and the
other two carry none; a leaf with the wrong EKU is refused before the backend
is called; a binary-encoded config loads; nothing in the tree imports
`dnsclient`; an Ed25519 certificate round-trips; fifteen binaries build.

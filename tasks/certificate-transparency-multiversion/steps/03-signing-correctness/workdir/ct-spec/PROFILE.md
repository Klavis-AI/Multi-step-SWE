# PROFILE — step 03 of 13: signing correctness (certificate-transparency-go v1.0.10)

## What this milestone adds over v1.0.5

Correctness in the front end's signing path, in response to the v1.0.7
advisory.

A CT front end signs two kinds of thing: Signed Certificate Timestamps, whose
input is a `TimestampedEntry`-shaped structure, and tree heads, whose input is
a `TreeHeadSignature`. Both are relatively expensive and both recur, so both
invite caching. The fault the advisory describes is a cache whose key does not
capture the whole signing input, so that a signature computed over one input is
served as the signature over a different one.

The rule this milestone establishes: **no two distinct signing inputs may share
a signature**, and the cache must not change the *shape* of any response — no
new field, no new header, no new status.

v1.0.10 acts on that rule on the tree-head side by **adding** a per-log
cache of the last STH signature, so the rule and its mechanism arrive
together. The mechanism is observable in the value it protects, and that is
how this step is graded: the log key is ECDSA P-256, Go's ECDSA is randomised,
so two `get-sth` calls over an unchanged tree return the **byte-identical**
`tree_head_signature` where recomputation would return two different ones. The
cache is per log and is invalidated by any change to the tree head. Nothing
equivalent exists on the SCT path at this release; there, distinct inputs must
simply produce distinct signatures.

ARCHITECTURE.md §2 item 3 describes this same per-log signature cache and
dates it to v1.0.10; TEST_INTERFACE.md §10 names the group that grades
it. The three pages agree.

The front end also gains an exported **request-logging interface**,
`ctfe.RequestLog`, wired through `InstanceOptions.RequestLog` and driven by
`AppHandler.ServeHTTP` around every request, with a shipped
`DefaultRequestLog`. The `x509` fork starts storing the embedded SCT list on
the parsed certificate (`RawSCT`, `SCTList`), and `x509util` renders the two
RFC 6962 extensions in `CertificateToString` output.

The configuration validator also gains an explicit duplicate-prefix check, a
duplicate-backend-spec check, an empty-prefix check, a per-backend
duplicate-tree-id check and the three `not_after` range checks; at
v1.0.5 a duplicate-prefix file was rejected, but later and with a
different diagnostic.

The binary count does not change at this milestone: **10** `package main`
directories, exactly as at v1.0.5, and the same ten must come out of
`go build ./...` (CLI.md §1).

## Reading route through ct-spec/

1. **TLS-CODEC.md** §5.3 and §5.5 — exactly which bytes are in each signature
   input. This is the whole milestone: anything in these bytes that is missing
   from a cache key is the bug.
2. **ARCHITECTURE.md** §5 — where signing sits in the add-chain lifecycle.
3. **CONFIG.md** §4.4 — the whole-file validation table, with the release
   column saying which rows are new at v1.0.10.
4. **CLIENT-API.md** §8 — `InstanceOptions`, and §8.5 for the ten `RequestLog`
   methods and the `prefix{logID}` label.
5. **X509-FORK.md** §4.5 — `RawSCT` and `SCTList` on a parsed certificate.
6. **CLIENT-API.md** §5.2 — what `x509util.CertificateToString` prints for
   those two extensions.
7. **RELEASE-MAP.md** §3.

## How you will know you are done

Two submissions that differ in any byte of their signature input produce
signatures that verify only over their own input; an unchanged tree head,
asked for twice, comes back with the byte-identical `tree_head_signature`; and
no response carries anything it did not carry at v1.0.5.

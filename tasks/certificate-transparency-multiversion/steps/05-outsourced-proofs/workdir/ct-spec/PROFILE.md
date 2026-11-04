# PROFILE — step 05 of 13: outsourced proofs (certificate-transparency-go v1.0.17)

## What this milestone adds over v1.0.13

Mostly, it takes something away.

The product has carried its own Merkle tree verifier since v1.0 — the release
of step 1, where you first wrote it, and every release from there to
v1.0.13 (MERKLE.md §4): `merkletree`, with a tree hasher, an
inclusion verifier, a consistency verifier and a root-mismatch error. At
v1.0.17 that package is **deleted** and proof verification is obtained
from the backend project's Merkle library instead. The hashing rules do not change — they are RFC 6962's —
but the code that implements them leaves the product, and every consumer inside
it is rewritten.

Two additions ride along: `LeafHashForLeaf` in the root package, which computes
a leaf hash from an already-built `MerkleTreeLeaf`; and `scanner/fetcher.go`,
which extracts the batch-walking loop from the scanner so that other tools can
consume a log's entries without reimplementing the pagination.

RELEASE-MAP.md §5 lists a third subtraction, the SQLite backend. That one
happens in the **backend project**, not in this tree: here nothing about
SQLite changes at v1.0.17, and the `gossip_server` built on it is still
one of the thirteen commands that must build. Leave it where it is —
TEST_INTERFACE.md section 9 puts it out of scope.

**The deletion is the headline, not the whole release.** v1.0.17 also
changes the forked parser and the front end in ways that have nothing to do
with proofs, and they are as graded as the deletion is: the `x509` SAN syntax
check leaves the parser, the `VerifyOptions` switch set is completed and leaf
EKU matching tightens, `ct_server` goes behind a permissive CORS wrapper and
narrows its catch-all, `certcheck --validate` learns to read a file as a
chain, and a few flags and symbols come and go. Section 10 of TEST_INTERFACE.md
lists them; the route below is where each one is written down.

## Reading route through ct-spec/

*Proofs — the milestone's headline:*

1. **MERKLE.md** §4 — the whole withdrawal, including §4.2's table of which
   import path is in force at which release. Read §4.1 to know what you are
   removing.
2. **MERKLE.md** §1–§3 — the rules that do not change. Your answers must still
   match these vectors afterwards.
3. **CLIENT-API.md** §3 — `LeafHashForLeaf`.
4. **CLIENT-API.md** §5.1 — the fetcher surface, its field names, its defaults
   and the clamping `Prepare` performs.
5. **CLIENT-API.md** §2.1 — `LogInfo`, whose signatures do not change even
   though what is underneath them does.
6. **RELEASE-MAP.md** §5 and §14.

*Everything else v1.0.17 changes — each of these is scored too, and none
of it is reachable from the six pages above:*

7. **X509-FORK.md** §3.1, the sub-heading "The SAN relaxation at v1.0.17"
   — a malformed `dnsName` or `rfc822Name` stops being a fatal parse error and
   is stored verbatim in `DNSNames` / `EmailAddresses`.
8. **X509-FORK.md** §5.6 — the six `VerifyOptions` disable switches, what each
   suppresses when false, and the "Leaf EKU matching" rule that changes in the
   same release.
9. **HTTP-API.md** §7 — the permissive CORS wrapper in front of the whole
   `ct_server` mux (headers, the `OPTIONS` pre-flight, the three `Vary`
   values) and the narrowing of the catch-all to the exact path `/`.
10. **HTTP-API.md** §5.1 — where the CTFE's own chain validation stops
    hand-editing certificates and sets the §5.6 switches instead.
11. **CLI.md** §4.3 — what `certcheck --validate` validates, and the condition
    under which a multi-certificate file becomes a chain. **CLI.md** §2.1 for
    `ct_server`'s three new tracing flags, **CLI.md** §4.1 for `scanlog
    --end_index`, **CLI.md** §3.1 for `ctclient consistency`.
12. **CLIENT-API.md** §8.3 for the withdrawal of `ctfe.LogEntry` and
    `ctfe.NewLogEntry`; **CLIENT-API.md** §1.1, §1.3 and §6.1 for `BaseURI()`
    on `jsonclient.JSONClient`, `client.LogClient` and `dnsclient.DNSClient`,
    and for `CheckLogClient` growing it as its **first** method.

## How you will know you are done

No package named `merkletree` exists; the inclusion and consistency vectors of
MERKLE.md still verify; `LeafHashForLeaf` is exported from the root package;
a certificate with a malformed SAN parses instead of failing; `ct_server`
answers a cross-origin request with `Access-Control-Allow-Origin: *`;
thirteen binaries build.

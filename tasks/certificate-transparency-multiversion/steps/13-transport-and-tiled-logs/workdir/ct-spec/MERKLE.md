# MERKLE — hashing, proofs, and where proof verification lives

RFC 6962 §2 defines the tree. `rfc/rfc6962.txt` is the normative text; §2.1
gives the two proof algorithms and §2.1.1/§2.1.2 the verification procedures.
This page states the parts the product fixes and the release at which the
product's *own* verifier is withdrawn in favour of a library's.

---

## 1. The hashing rules

Three rules, one hash function (SHA-256), and two one-byte domain separators.
Everything downstream depends on these exact bytes.

| quantity | definition |
| --- | --- |
| empty tree hash | `SHA-256()` of the empty string |
| leaf hash | `SHA-256(0x00 ‖ leaf_data)` |
| interior node | `SHA-256(0x01 ‖ left ‖ right)` |

`leaf_data` for a CT log is the **TLS encoding of the `MerkleTreeLeaf`**
(TLS-CODEC.md §5.2) — not the certificate, not the DER, not the JSON. This is
what `ctutil.LeafHash` computes and what a `get-proof-by-hash` `hash` parameter
must be the base64 of.

The `0x00` / `0x01` prefixes are what make a leaf hash and an interior hash
belong to disjoint domains, and they are not negotiable: an implementation that
omits them produces a tree that is internally consistent and externally wrong.

The tree is **not** padded to a power of two. For a tree of *n* leaves, the
split point is the largest power of two strictly less than *n* (`k` in RFC 6962
§2.1); the left subtree gets `k` leaves and the right gets `n − k`. A subtree
with exactly one leaf is that leaf's hash — no extra interior node is
interposed. Getting this wrong only shows up for sizes that are not powers of
two, so test at sizes 1, 2, 3, 5, 7 and 8.

---

## 2. Inclusion proofs

For leaf index *m* in a tree of size *n*, the proof is the ordered list of
sibling hashes from the leaf up to the root, bottom first. Verification
recomputes the root:

    node := leaf_hash
    index := m
    size  := n
    for each p in proof:
        if size == 1: fail                      # proof too long
        if index is odd or index+1 == size:     # we are a right child,
            node := H(0x01 ‖ p ‖ node)          #   or the last node at this level
            while index is even and index != 0: index >>= 1 ; size >>= 1
        else:
            node := H(0x01 ‖ node ‖ p)
        index >>= 1
        size  >>= 1
    if size != 1: fail                          # proof too short
    compare node with the expected root

The two failure modes that the verifier exercises are "proof too short" and
"proof too long"; both must be errors, not a silent mismatch. A computed root
that differs from the expected root is a distinct error carrying **both**
roots. **Who owns that type depends on the release** (§4): from v1.0 to
v1.0.13 it is the product's own

    // in the product's merkletree/ package, v1.0 .. v1.0.13 only
    type RootMismatchError struct {
        ExpectedRoot   []byte
        CalculatedRoot []byte
    }

whose `Error()` renders both in hex. From **v1.0.17** the product has no such
type: the error comes from whichever external library §4.2 names, with its own
name, its own fields and its own text. What survives the change, and what is
required at every release, is the *behaviour*: a mismatch is an error and not a
silent success, and it is distinguishable from a malformed proof.

The expected proof length is the number of 1-bits and the path length implied
by *n*; for a full tree of size 2^h it is exactly *h*. An empty proof is valid
only for `n == 1, m == 0`.

## 3. Consistency proofs

Between snapshot sizes *n₁ ≤ n₂*, with roots *r₁* and *r₂*. Special cases
first, because they are where implementations diverge:

* `n₁ == n₂`: the proof must be **empty** and `r₁` must equal `r₂`.
* `n₁ == 0`: the proof must be empty; any `r₂` is consistent with the empty
  tree. (Upstream's own `migrillian` relies on this from v1.1.4 — it skips the
  consistency check entirely when the root size is zero. Note that no command
  in the product can be asked for this case: `ctclient consistency` requires
  `--prev_size` to be **positive**, CLI.md §3.1.)
* `n₁ > n₂`: an error.

Otherwise the algorithm walks the same path structure as §2, seeded from the
first proof element when *n₁* is not a power of two, and computes *both* roots
from the proof; both must match. Two distinct errors are possible and must stay
distinct: a mismatch of the **old** root and a mismatch of the **new** root.

---

## 4. Where the verifier lives, per release

This is a structural requirement, not a matter of taste, and it changes twice.

| releases | steps | the product's own proof code |
| --- | --- | --- |
| v1.0 .. v1.0.13 | 1–4 | package `merkletree/` — the product implements verification itself |
| v1.0.17 .. v1.3.3 | 5–13 | **`merkletree/` does not exist**; verification is obtained from an external Merkle library |

### 4.1 `merkletree/` at v1.0..v1.0.13

Import path `github.com/google/certificate-transparency-go/merkletree`. The
surface consumers use:

    // v1.0 .. v1.0.10 only — untyped integer constants, not bytes
    const LeafPrefix = 0
    const NodePrefix = 1

    type HasherFunc func([]byte) []byte
    type TreeHasher struct{ … }
    func NewTreeHasher(h HasherFunc) *TreeHasher
    func (h TreeHasher) HashEmpty() []byte
    func (h TreeHasher) HashLeaf(leaf []byte) []byte
    func (h TreeHasher) HashChildren(left, right []byte) []byte

    type MerkleVerifier struct{ … }
    func NewMerkleVerifier(h HasherFunc) MerkleVerifier
    func (m MerkleVerifier) VerifyInclusionProof(leafIndex, treeSize int64, proof [][]byte, root []byte, leaf []byte) error
    func (m MerkleVerifier) RootFromInclusionProof(leafIndex, treeSize int64, proof [][]byte, leaf []byte) ([]byte, error)
    func (m MerkleVerifier) VerifyConsistencyProof(snapshot1, snapshot2 int64, root1, root2 []byte, proof [][]byte) error

    // from v1.0.5 — take the leaf hash rather than the leaf data
    func (m MerkleVerifier) VerifyInclusionProofByHash(leafIndex, treeSize int64, proof [][]byte, root []byte, leafHash []byte) error
    func (m MerkleVerifier) RootFromInclusionProofAndHash(leafIndex, treeSize int64, proof [][]byte, leafHash []byte) ([]byte, error)

    type RootMismatchError struct{ ExpectedRoot, CalculatedRoot []byte }

plus the `MerkleTreeInterface` / `FullMerkleTreeInterface` interface
declarations. Note that `VerifyInclusionProof` takes the **leaf data**, not the
leaf hash, and hashes it itself.

`LeafPrefix` and `NodePrefix` are this package's exported names for the
`0x00` / `0x01` domain-separation bytes of §1, and `TreeHasher` prepends them
in `HashLeaf` and `HashChildren`. They are declared **in `merkletree/` at v1.0,
v1.0.5 and v1.0.10 only**: at **v1.0.13** they move to the root package as
`ct.TreeLeafPrefix` / `ct.TreeNodePrefix` (CLIENT-API.md §3), which is where
`TreeHasher` then reads them from, and from v1.0.17 the package is gone
altogether (§4.2). They are untyped integer constants here and typed `byte`
constants there, so both spellings print as `0` and `1`.

Its consumers inside the product at v1.0.13 are `client/ctclient`,
`ctutil/loginfo.go` and `trillian/integration`.

### 4.2 The withdrawal at v1.0.17 (step 5)

At v1.0.17 the package is **deleted outright** and those consumers are
rewritten to call the log verifier of the backend project's Merkle library.
This is a subtractive milestone: the capability does not move to another path
inside the product, it leaves the product. A build at step 5 that still
contains a `merkletree/` package has not performed the milestone, and the
verifier observes this as a driver that imports the path failing to build.

The library and the symbol names it is called through change again later; the
import path in force at each anchor is:

| releases | import path(s) | entry point |
| --- | --- | --- |
| v1.0.17 .. v1.1.0 | `github.com/google/trillian/merkle` + `…/merkle/rfc6962` | `merkle.NewLogVerifier(rfc6962.DefaultHasher)` |
| v1.1.2 | `github.com/google/trillian/merkle/logverifier` + `…/merkle/rfc6962` | `logverifier.New(rfc6962.DefaultHasher)` |
| v1.1.3 .. v1.3.3 | `github.com/transparency-dev/merkle/proof` + `…/merkle/rfc6962` | the `proof` package's `VerifyInclusion` / `VerifyConsistency` functions |

Whichever library is in force, the hashing rules of §1 are unchanged — that is
the point of RFC 6962 being a wire specification. What changes is who owns the
code.

The pinned module set available to you is fixed per step (TEST_INTERFACE.md
§2); the correct library for the step is already in it, and no other is.

---

## 5. What the log serves

Three of the eight endpoints are proof endpoints (HTTP-API.md §4):

* `get-sth-consistency?first=&second=` → `{"consistency": [b64, …]}`
* `get-proof-by-hash?hash=&tree_size=` → `{"leaf_index": n, "audit_path": [b64, …]}`
* `get-entry-and-proof?leaf_index=&tree_size=` → leaf, extra data and audit path

The CTFE does not compute any of these: where a proof is needed it asks the
backend and reshapes the answer. What it *must* get right is the parameter
parsing, the error status for an impossible range, and the base64 shaping. An
empty proof is serialised as an empty JSON array, never as `null`.

There is one range for which no proof is needed and none is asked for. A
consistency proof from tree size **0** is empty by definition, so
`get-sth-consistency` with `first=0` is answered **200** with
`{"consistency": []}` **without any call to the backend at all** — the
short-circuit is taken before the proof RPC, on the parsed value of `first`
alone, and it holds at every release from v1.0 to v1.3.3. The `second`
parameter is still required and still parsed, and the range checks of
HTTP-API.md §4.2 still apply, so `first=0` with a missing or malformed
`second` is the same 400 it would otherwise be. The other two proof endpoints
have no such case: they always call the backend.

---

## 6. Reading route

1. `rfc/rfc6962.txt` §2, §2.1, §2.1.1, §2.1.2.
2. §1 of this page — commit the three hashing rules to memory.
3. §4 for the release you are on: are you implementing the verifier, or
   deleting it?
4. CLIENT-API.md §2 for `ctutil.LeafHash` and the inclusion helpers built on
   top.

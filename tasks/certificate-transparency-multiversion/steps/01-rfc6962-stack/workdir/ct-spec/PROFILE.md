# PROFILE — step 01 of 13: the RFC 6962 stack (certificate-transparency-go v1.0)

## What this milestone is

The first tagged Go implementation of RFC 6962: a complete Certificate
Transparency stack with no previous release behind it. Two halves, and you need
both.

The **library** half is a TLS presentation-language codec, a fork of Go's
`encoding/asn1` and `crypto/x509` that can parse certificates the standard
library refuses, the RFC 6962 data structures and their signature inputs, a
Merkle tree verifier, an HTTP client for the eight endpoints, and a handful of
utilities built on those.

The **personality** half is `ct_server`: an HTTP front end that speaks RFC 6962
to the world, validates submitted chains against configured roots, issues
Signed Certificate Timestamps, and asks a Merkle-tree backend over gRPC for
everything that involves the tree itself. The backend is not yours and does not
run here (TEST_INTERFACE.md §3.4).

## Reading route through ct-spec/

1. **ARCHITECTURE.md** — all of it. It is short and it is the map.
2. **TLS-CODEC.md** §1–§4 for the codec, then §5 for every structure you must
   encode. §5 gives field order, tag parameters and widths; that is what fixes
   the bytes, and it is what the verifier's vectors were generated from.
3. **rfc/rfc6962.txt** §1–§3 for the data structures and §4 for the endpoints;
   **rfc/rfc5246.txt** §4 for the presentation language TLS-CODEC.md §2
   summarises.
4. **X509-FORK.md** §1–§4 for the fork, `NonFatalErrors` and precertificates.
5. **MERKLE.md** §1–§4.1 for the hashing rules and the verifier this release
   owns.
6. **HTTP-API.md** §1–§6 for the endpoints, admission control, and how backend
   errors become HTTP statuses.
7. **CONFIG.md** §1–§3 and **CLI.md** preamble and §1–§2 for the config
   message, the key handlers it names, and the `ct_server` flag set — including
   `--log_rpc_server`, which is the only place the backend address comes from at
   v1.0.
8. **CLIENT-API.md** §1, §3, §5, §8 for the client and the front-end entry
   points.

## How you will know you are done

`go build ./...` produces eight binaries. A `MerkleTreeLeaf` you encode and
then decode round-trips, and its encoding is the one TLS-CODEC.md §5.2's field
order and tags describe, byte for byte — that is what the verifier compares
against, from vectors you cannot read. `SHA-256(0x00 ‖ TLS(MerkleTreeLeaf))` is
the leaf hash of MERKLE.md §1. `ct_server` loads a valid text-proto config,
refuses the JSON rendering of the same config and refuses one with an unknown
field, serves `get-roots` from the configured pool, and on SIGTERM exits with
status **1** (CLI.md §2.3).

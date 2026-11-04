// Group sth-response-to-signed-tree-head.
//
// v1.0.19 adds (*ct.GetSTHResponse).ToSignedTreeHead, and rewrites
// client.GetSTH to call it. Before this release the conversion -- copy the
// root hash into a fixed-size array, TLS-decode the tree head signature --
// was open-coded inside the client and unavailable to anything else; the new
// migrillian and the gossip code need it, so it moves onto the wire type.
//
// It is a pure function over a JSON response body, so the whole group is one
// driver with no server: the input is a pinned get-sth response body, exactly
// as RFC 6962 section 4.3 defines it (base64 in JSON), and every field of the
// result is asserted. The four rejection cases matter as much as the happy
// path, because the point of hoisting this conversion is that a malformed
// response must not silently become a zero-valued STH: a root hash of the
// wrong length is a length the fixed-size array would otherwise truncate or
// zero-pad, and a signature that does not TLS-decode cleanly, or that has
// trailing bytes, is a response the log did not really send.
//
// Only the returned value and the presence of an error are graded; no error
// string and no error type is asserted.
//
// SCOPE-NET / SCOPE-TIME: nothing listens, nothing dials, no clock is read.
//
// GetSTHResponse has no such method at v1.0.17, so this driver does not
// compile against the previous tree.
package main

import (
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"fmt"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"

	"ctverify/vhelp"
)

// A pinned get-sth response body: tree size and timestamp as integers, root
// hash and tree head signature base64-encoded, as the wire format requires.
const pinnedSTHBody = `{` +
	`"tree_size":100000,` +
	`"timestamp":1500000000000,` +
	`"sha256_root_hash":"8HkFQvkB1SGf0EjbNzZJsY1eqyaUJSngYMrFbwXi+cU=",` +
	`"tree_head_signature":"BAMARgABAgMEBQYHCAkKCwwNDg8QERITFBUWFxgZGhscHR4fICEiIyQlJicoKSorLC0uLzAxMjM0NTY3ODk6Ozw9Pj9AQUJDREU="` +
	`}`

const (
	pinnedRootHex = "f0790542f901d5219fd048db373649b18d5eab26942529e060cac56f05e2f9c5"
	// The 70 signature bytes carried inside the DigitallySigned above.
	pinnedSigHex = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f" +
		"202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f" +
		"404142434445"
)

// decodeBody parses the pinned body into the wire struct the log returns.
func decodeBody(t *vhelp.T) ct.GetSTHResponse {
	var r ct.GetSTHResponse
	if err := json.Unmarshal([]byte(pinnedSTHBody), &r); err != nil {
		t.Fatalf("decode pinned get-sth body: %v", err)
	}
	return r
}

// rejects renders the outcome of a conversion that must not succeed, without
// naming an error type or an error string.
func rejects(sth *ct.SignedTreeHead, err error) string {
	return fmt.Sprintf("sth=%v err=%v", sth == nil, err != nil)
}

func main() {
	t := vhelp.New()

	good := decodeBody(t)
	sth, err := good.ToSignedTreeHead()
	if err != nil || sth == nil {
		t.Fatalf("ToSignedTreeHead on the pinned body: sth=%v err=%v", sth, err)
	}

	// --- 1. the tree the response describes --------------------------------
	t.Equal("pinned-get-sth-response-converts-to-its-tree-head",
		fmt.Sprintf("size=%d ts=%d root=%x", sth.TreeSize, sth.Timestamp, sth.SHA256RootHash[:]),
		fmt.Sprintf("size=%d ts=%d root=%s", 100000, 1500000000000, pinnedRootHex))

	// --- 2. the signature, TLS-decoded into its algorithm and bytes ---------
	t.Equal("tree-head-signature-is-tls-decoded",
		fmt.Sprintf("hash=%v sig=%v bytes=%s",
			sth.TreeHeadSignature.Algorithm.Hash, sth.TreeHeadSignature.Algorithm.Signature,
			hex.EncodeToString(sth.TreeHeadSignature.Signature)),
		fmt.Sprintf("hash=%v sig=%v bytes=%s", tls.SHA256, tls.ECDSA, pinnedSigHex))

	// --- 3. and it survives a round trip unchanged --------------------------
	// The re-encoding must be the very bytes the log sent, or a proof built on
	// the converted STH would be verified against a different signature.
	reEncoded, merr := tls.Marshal(tls.DigitallySigned(sth.TreeHeadSignature))
	t.Equal("signature-round-trips-back-to-the-response-bytes",
		fmt.Sprintf("err=%v bytes=%s", merr, hex.EncodeToString(reEncoded)),
		fmt.Sprintf("err=%v bytes=%s", error(nil),
			hex.EncodeToString(mustB64(t, "BAMARgABAgMEBQYHCAkKCwwNDg8QERITFBUWFxgZGhscHR4fICEiIyQlJicoKSorLC0uLzAxMjM0NTY3ODk6Ozw9Pj9AQUJDREU="))))

	// --- 4. a root hash that is not 32 bytes is refused ---------------------
	// Both directions: too short would be zero-padded and too long truncated
	// by a plain copy into the fixed-size array.
	short := good
	short.SHA256RootHash = good.SHA256RootHash[:31]
	shortSTH, shortErr := short.ToSignedTreeHead()
	long := good
	long.SHA256RootHash = append(append([]byte{}, good.SHA256RootHash...), 0x00)
	longSTH, longErr := long.ToSignedTreeHead()
	t.Equal("wrong-length-root-hash-is-refused",
		fmt.Sprintf("short:%s long:%s", rejects(shortSTH, shortErr), rejects(longSTH, longErr)),
		"short:sth=true err=true long:sth=true err=true")

	// --- 5. trailing bytes after the DigitallySigned are refused ------------
	trailing := good
	trailing.TreeHeadSignature = append(append([]byte{}, good.TreeHeadSignature...), 0xff)
	trailSTH, trailErr := trailing.ToSignedTreeHead()
	t.Equal("trailing-data-after-the-signature-is-refused",
		rejects(trailSTH, trailErr), "sth=true err=true")

	// --- 6. as is a signature that does not decode at all -------------------
	truncated := good
	truncated.TreeHeadSignature = good.TreeHeadSignature[:4]
	truncSTH, truncErr := truncated.ToSignedTreeHead()
	empty := good
	empty.TreeHeadSignature = nil
	emptySTH, emptyErr := empty.ToSignedTreeHead()
	t.Equal("undecodable-signature-is-refused",
		fmt.Sprintf("truncated:%s empty:%s", rejects(truncSTH, truncErr), rejects(emptySTH, emptyErr)),
		"truncated:sth=true err=true empty:sth=true err=true")

	t.Done()
}

func mustB64(t *vhelp.T, s string) []byte {
	b, err := base64.StdEncoding.DecodeString(s)
	if err != nil {
		t.Fatalf("decode pinned base64: %v", err)
	}
	return b
}

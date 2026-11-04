// Group inherited-rfc6962-leaf-hash.
//
// Leaf hashing and signature validation remain unchanged from v1.0.10.
// Pinned from the upstream test corpus: an x509 certificate leaf built from
// the test chain, serialized to TLS format, and hashed with the 0x00 prefix.
package main

import (
	"crypto/sha256"
	"encoding/hex"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"

	"ctverify/vhelp"
)

const (
	// The serialized x509 leaf as certified at v1.0.10 and still produced at
	// v1.0.13: version 0, leaf_type 0, the pinned timestamp, entry_type 0, and
	// the full DER of the test certificate (735 bytes of TLS framing in all).
	wantX509LeafTLSSHA256 = "109723e13eb2c24fe23303112c81a933ae9d05ceeb9f90b50b92b162360b39b7"
	wantX509LeafTLSLen    = 735
	pinnedTimestamp       = uint64(1396877548274)
)

func main() {
	t := vhelp.New()
	certPEM := vhelp.ReadFixture("rfc6962", "test-cert.pem")
	caPEM := vhelp.ReadFixture("rfc6962", "ca-cert.pem")
	chain := pemCerts(t, concat(certPEM, caPEM))

	leaf, err := ct.MerkleTreeLeafFromChain(chain, ct.X509LogEntryType, pinnedTimestamp)
	t.Case("an-x509-leaf-is-built-from-the-test-chain", err)
	if err != nil {
		t.Fatalf("cannot continue without the leaf: %v", err)
	}

	leafData, err := tls.Marshal(*leaf)
	t.Case("the-leaf-marshals-to-tls-format", err)
	if err != nil {
		t.Fatalf("cannot continue without serialized leaf: %v", err)
	}

	sum := sha256.Sum256(leafData)
	gotHex := hex.EncodeToString(sum[:])
	t.Equal("the-serialized-leaf-length-is-the-pinned-tls-length", len(leafData), wantX509LeafTLSLen)
	t.Equal("the-serialized-leaf-sha256-is-unchanged-from-v1.0.10", gotHex, wantX509LeafTLSSHA256)
	t.Equal("the-leaf-begins-with-the-fixed-version-and-leaf-type-bytes",
		hex.EncodeToString(leafData[:2]), "0000")

	t.Check("the-merkletreeleaf-version-is-v1",
		leaf.Version == ct.V1, "got %v", leaf.Version)

	t.Check("the-merkletreeleaf-leaftype-is-timestamped-entry",
		leaf.LeafType == ct.TimestampedEntryLeafType, "got %v", leaf.LeafType)

	t.Equal("the-timestamp-is-the-pinned-sct-timestamp", leaf.TimestampedEntry.Timestamp, pinnedTimestamp)

	t.Check("the-entry-type-is-x509",
		leaf.TimestampedEntry.EntryType == ct.X509LogEntryType,
		"got %v", leaf.TimestampedEntry.EntryType)

	certOut, err := leaf.X509Certificate()
	t.Case("the-leaf-can-yield-the-certificate-back", err)
	if err == nil {
		t.Check("the-certificate-matches-the-original",
			certOut.Equal(chain[0]), "round-trip certificate does not match")
	}

	t.Done()
}

func concat(parts ...[]byte) []byte {
	var out []byte
	for _, p := range parts {
		out = append(out, p...)
	}
	return out
}

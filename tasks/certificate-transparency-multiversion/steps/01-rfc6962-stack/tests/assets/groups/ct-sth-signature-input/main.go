// Group ct-sth-signature-input.
//
// RFC 6962 s3.5: a signed tree head is signed over a TLS-encoded
// TreeHeadSignature: version, signature_type == tree_hash (1), timestamp,
// tree_size, sha256_root_hash. This group grades
// ct.SerializeSTHSignatureInput against the published serialisation in the
// fixture corpus and against a layout this driver builds itself, checks that
// the encoding is sensitive to the tree size, and grades the documented
// refusal of an unknown STH version. It then reads the same bytes back through
// tls.Unmarshal into ct.TreeHeadSignature.
//
// Boundary: exported root-package serialisation. No wall clock: the timestamp
// is the pinned corpus value.
package main

import (
	"encoding/hex"
	"errors"
	"strings"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"

	"ctverify/vhelp"
)

// From the pinned corpus: a September 2012 timestamp, tree size 42.
const (
	sthTimestamp = uint64(1348589667204)
	sthTreeSize  = uint64(42)
)

func katMap() map[string]string {
	m := map[string]string{}
	for _, ln := range strings.Split(string(vhelp.ReadFixture("rfc6962", "kat.txt")), "\n") {
		if f := strings.Fields(ln); len(f) == 2 {
			m[f[0]] = f[1]
		}
	}
	return m
}

func be64(v uint64) []byte {
	b := make([]byte, 8)
	for i := 7; i >= 0; i-- {
		b[i] = byte(v)
		v >>= 8
	}
	return b
}

func main() {
	t := vhelp.New()
	kat := katMap()

	rootHash, err := hex.DecodeString(kat["sigTestDefaultRootHash"])
	if err != nil || len(rootHash) != 32 {
		t.Fatalf("corpus root hash: %v", err)
	}
	wantSerialized, err := hex.DecodeString(kat["sigTestDefaultSTHSerialized"])
	if err != nil {
		t.Fatalf("corpus STH serialisation: %v", err)
	}

	var rh ct.SHA256Hash
	copy(rh[:], rootHash)
	sth := ct.SignedTreeHead{
		Version:        ct.V1,
		TreeSize:       sthTreeSize,
		Timestamp:      sthTimestamp,
		SHA256RootHash: rh,
	}

	got, err := ct.SerializeSTHSignatureInput(sth)
	if err != nil {
		t.Fatalf("SerializeSTHSignatureInput: %v", err)
	}
	t.EqualBytes("sthinput-exact-known-answer", got, wantSerialized)

	// version(0x00) signature_type(tree_hash == 0x01) timestamp tree_size root
	rebuilt := []byte{0x00, 0x01}
	rebuilt = append(rebuilt, be64(sthTimestamp)...)
	rebuilt = append(rebuilt, be64(sthTreeSize)...)
	rebuilt = append(rebuilt, rootHash...)
	t.EqualBytes("sthinput-layout-rebuilt", got, rebuilt)
	t.Equal("sthinput-length", len(got), 1+1+8+8+32)

	bigger := sth
	bigger.TreeSize = sthTreeSize + 1
	other, err := ct.SerializeSTHSignatureInput(bigger)
	if err != nil {
		t.Fatalf("SerializeSTHSignatureInput(43): %v", err)
	}
	diff := 0
	for i := range other {
		if other[i] != got[i] {
			diff++
		}
	}
	t.Check("sthinput-treesize-sensitive", len(other) == len(got) && diff == 1,
		"changing tree_size 42->43 altered %d byte(s) of %d", diff, len(other))

	bad := sth
	bad.Version = ct.Version(7)
	_, err = ct.SerializeSTHSignatureInput(bad)
	t.Case("sthinput-unknown-version-rejected", func() error {
		if err == nil {
			return errors.New("an STH with version 7 was serialised")
		}
		return nil
	}())

	// --- and the same bytes decode back into the published structure -------
	var ths ct.TreeHeadSignature
	rest, err := tls.Unmarshal(wantSerialized, &ths)
	if err != nil || len(rest) != 0 {
		t.Fatalf("tls.Unmarshal(TreeHeadSignature): err=%v trailing=%d", err, len(rest))
	}
	t.Check("thsig-decode-version-and-type",
		ths.Version == ct.V1 && ths.SignatureType == ct.TreeHashSignatureType,
		"version=%v signature_type=%v", ths.Version, ths.SignatureType)
	t.Check("thsig-decode-timestamp-and-size",
		ths.Timestamp == sthTimestamp && ths.TreeSize == sthTreeSize,
		"timestamp=%d tree_size=%d", ths.Timestamp, ths.TreeSize)
	t.EqualBytes("thsig-decode-root-hash", ths.SHA256RootHash[:], rootHash)

	t.Done()
}

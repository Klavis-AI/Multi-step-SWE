// Group inherited-rfc6962-leaf-hash.
//
// The oldest obligation in the product, carried forward untouched by this
// milestone: the RFC 6962 Merkle tree leaf and the two signature inputs
// computed over it. v1.2.0 adds a second place to *keep* a log entry's
// issuance chain (out of line, behind a hash), and the whole claim of the
// milestone is that this changes only the extra data -- the leaf a log is
// indexed by, and everything signed over it, must be byte-identical to what
// every earlier milestone produced.
//
// So this group is deliberately written from the wire format outwards. Where
// an answer is pinned upstream it comes from the shared known-answer table;
// where it is not, the expected bytes are assembled here out of the RFC's own
// field widths (uint8 version, uint8 leaf type, uint64 timestamp, uint16
// entry type, uint24-prefixed certificate, uint16-prefixed extensions) rather
// than read back from the product. Nothing here is expected to move at this
// release, which is exactly why it is an inherited group.
package main

import (
	"bytes"
	"crypto/sha256"
	"encoding/base64"
	"encoding/binary"
	"encoding/hex"
	"encoding/pem"
	"strings"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/ctutil"
	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

// pinnedTimestamp is fixed; SCOPE-TIME forbids reading the clock.
const pinnedTimestamp = uint64(1519741473000)

func kat(t *vhelp.T) map[string]string {
	out := map[string]string{}
	for _, line := range strings.Split(string(vhelp.ReadFixture("rfc6962", "kat.txt")), "\n") {
		if i := strings.IndexByte(line, ' '); i > 0 {
			out[line[:i]] = strings.TrimSpace(line[i+1:])
		}
	}
	if len(out) == 0 {
		t.Fatalf("known-answer table is empty")
	}
	return out
}

func mustHex(t *vhelp.T, s string) []byte {
	b, err := hex.DecodeString(s)
	if err != nil {
		t.Fatalf("bad pinned hex: %v", err)
	}
	return b
}

func certFrom(t *vhelp.T, dir, name string) *x509.Certificate {
	blk, _ := pem.Decode(vhelp.ReadFixture(dir, name))
	if blk == nil {
		t.Fatalf("fixture %s/%s is not PEM", dir, name)
	}
	c, err := x509.ParseCertificate(blk.Bytes)
	if err != nil && x509.IsFatal(err) {
		t.Fatalf("ParseCertificate(%s/%s): %v", dir, name, err)
	}
	return c
}

// sctFromProof decodes one of the pinned wire-format SCTs.
func sctFromProof(t *vhelp.T, hexed string) *ct.SignedCertificateTimestamp {
	var sct ct.SignedCertificateTimestamp
	rest, err := tls.Unmarshal(mustHex(t, hexed), &sct)
	if err != nil {
		t.Fatalf("tls.Unmarshal(pinned proof): %v", err)
	}
	if len(rest) != 0 {
		t.Fatalf("pinned proof has %d trailing bytes", len(rest))
	}
	return &sct
}

// u24 renders a three-byte big-endian length, the width RFC 6962 gives an
// ASN.1Cert and a TBSCertificate.
func u24(n int) []byte { return []byte{byte(n >> 16), byte(n >> 8), byte(n)} }

func u16(n int) []byte { return []byte{byte(n >> 8), byte(n)} }

func u64(n uint64) []byte {
	var b [8]byte
	binary.BigEndian.PutUint64(b[:], n)
	return b[:]
}

func main() {
	t := vhelp.New()
	k := kat(t)

	leafCert := certFrom(t, "minted", "leaf-valid.pem")
	interCert := certFrom(t, "minted", "bench-intermediate.pem")
	precert := certFrom(t, "minted", "precert-poisoned.pem")

	// ---- 1. The leaf header. -------------------------------------------
	leaf, err := ct.MerkleTreeLeafFromChain([]*x509.Certificate{leafCert, interCert}, ct.X509LogEntryType, pinnedTimestamp)
	if err != nil {
		t.Fatalf("MerkleTreeLeafFromChain: %v", err)
	}
	encLeaf, err := tls.Marshal(*leaf)
	if err != nil {
		t.Fatalf("tls.Marshal(MerkleTreeLeaf): %v", err)
	}
	t.Check("merkle-leaf-header-is-v1-timestamped-entry",
		len(encLeaf) >= 2 && encLeaf[0] == 0x00 && encLeaf[1] == 0x00,
		"leaf starts %x, want version 0x00 then leaf type 0x00", encLeaf[:min(2, len(encLeaf))])

	// ---- 2. The whole timestamped entry, byte for byte. -----------------
	var want bytes.Buffer
	want.Write([]byte{0x00, 0x00})     // Version=v1, LeafType=timestamped_entry
	want.Write(u64(pinnedTimestamp))   // TimestampedEntry.Timestamp
	want.Write(u16(0))                 // EntryType = x509_entry
	want.Write(u24(len(leafCert.Raw))) // ASN.1Cert length
	want.Write(leafCert.Raw)           //          ... and body
	want.Write(u16(0))                 // no extensions
	t.EqualBytes("x509-timestamped-entry-exact-encoding", encLeaf, want.Bytes())

	// ---- 3. The leaf hash is SHA-256 over 0x00 || leaf. -----------------
	gotHash, err := ct.LeafHashForLeaf(leaf)
	if err != nil {
		t.Check("leaf-hash-is-sha256-of-0x00-prefixed-leaf", false, "LeafHashForLeaf: %v", err)
	} else {
		expect := sha256.Sum256(append([]byte{0x00}, encLeaf...))
		t.EqualBytes("leaf-hash-is-sha256-of-0x00-prefixed-leaf", gotHash[:], expect[:])
	}

	// ---- 4-6. The pinned upstream leaf hashes. --------------------------
	upLeaf := certFrom(t, "rfc6962", "test-cert.pem")
	upPre := certFrom(t, "rfc6962", "test-pre-cert.pem")
	upCA := certFrom(t, "rfc6962", "ca-cert.pem")
	upEmbedded := certFrom(t, "rfc6962", "test-embedded-cert.pem")
	certSCT := sctFromProof(t, k["TestCertProof"])
	preSCT := sctFromProof(t, k["TestPreCertProof"])

	wantCertHash, err := base64.StdEncoding.DecodeString(k["TestCertB64LeafHash"])
	if err != nil {
		t.Fatalf("bad pinned leaf hash: %v", err)
	}
	rawHash, err := ctutil.LeafHash([]*x509.Certificate{upLeaf}, certSCT, false)
	if err != nil {
		t.Check("x509-entry-leaf-hash-known-answer", false, "LeafHash: %v", err)
	} else {
		t.EqualBytes("x509-entry-leaf-hash-known-answer", rawHash[:], wantCertHash)
	}

	gotPre, err := ctutil.LeafHashB64([]*x509.Certificate{upPre, upCA}, preSCT, false)
	if err != nil {
		t.Check("precert-entry-leaf-hash-known-answer", false, "LeafHashB64: %v", err)
	} else {
		t.Equal("precert-entry-leaf-hash-known-answer", gotPre, k["TestPreCertB64LeafHash"])
	}

	// A certificate carrying an embedded SCT hashes to the precertificate
	// entry it was issued from, once the SCT list is taken back out.
	gotEmb, err := ctutil.LeafHashB64([]*x509.Certificate{upEmbedded, upCA}, preSCT, true)
	if err != nil {
		t.Check("embedded-sct-leaf-hash-known-answer", false, "LeafHashB64(embedded): %v", err)
	} else {
		t.Equal("embedded-sct-leaf-hash-known-answer", gotEmb, k["TestPreCertB64LeafHash"])
	}

	// ---- 7. The SCT signature input over an X.509 entry. ----------------
	sct := ct.SignedCertificateTimestamp{
		SCTVersion: ct.V1,
		Timestamp:  pinnedTimestamp,
		Extensions: ct.CTExtensions{0xde, 0xad},
	}
	gotIn, err := ct.SerializeSCTSignatureInput(sct, ct.LogEntry{Leaf: *leaf})
	if err != nil {
		t.Check("sct-signature-input-x509-exact-bytes", false, "SerializeSCTSignatureInput: %v", err)
	} else {
		var w bytes.Buffer
		w.Write([]byte{0x00, 0x00}) // version v1, signature type certificate_timestamp
		w.Write(u64(pinnedTimestamp))
		w.Write(u16(0)) // x509_entry
		w.Write(u24(len(leafCert.Raw)))
		w.Write(leafCert.Raw)
		w.Write(u16(2))
		w.Write([]byte{0xde, 0xad})
		t.EqualBytes("sct-signature-input-x509-exact-bytes", gotIn, w.Bytes())
	}

	// ---- 8. The SCT signature input over a precertificate entry. --------
	preLeaf, err := ct.MerkleTreeLeafFromChain([]*x509.Certificate{precert, interCert}, ct.PrecertLogEntryType, pinnedTimestamp)
	if err != nil {
		t.Fatalf("MerkleTreeLeafFromChain(precert): %v", err)
	}
	preIn, err := ct.SerializeSCTSignatureInput(
		ct.SignedCertificateTimestamp{SCTVersion: ct.V1, Timestamp: pinnedTimestamp},
		ct.LogEntry{Leaf: *preLeaf})
	if err != nil {
		t.Check("sct-signature-input-precert-exact-bytes", false, "SerializeSCTSignatureInput(precert): %v", err)
	} else {
		// The issuer key hash is computed here, from the issuer's own DER
		// SubjectPublicKeyInfo, not taken from the leaf the product built.
		ikh := sha256.Sum256(interCert.RawSubjectPublicKeyInfo)
		tbs := preLeaf.TimestampedEntry.PrecertEntry.TBSCertificate
		var w bytes.Buffer
		w.Write([]byte{0x00, 0x00})
		w.Write(u64(pinnedTimestamp))
		w.Write(u16(1)) // precert_entry
		w.Write(ikh[:]) // fixed-width, no length prefix
		w.Write(u24(len(tbs)))
		w.Write(tbs)
		w.Write(u16(0))
		t.EqualBytes("sct-signature-input-precert-exact-bytes", preIn, w.Bytes())
	}

	// ---- 9. The STH signature input, against the pinned serialization. --
	pinnedSTH := mustHex(t, k["sigTestDefaultSTHSerialized"])
	if len(pinnedSTH) != 2+8+8+sha256.Size {
		t.Fatalf("pinned STH serialization is %d bytes", len(pinnedSTH))
	}
	var root ct.SHA256Hash
	copy(root[:], pinnedSTH[18:])
	sth := ct.SignedTreeHead{
		Version:        ct.V1,
		Timestamp:      binary.BigEndian.Uint64(pinnedSTH[2:10]),
		TreeSize:       binary.BigEndian.Uint64(pinnedSTH[10:18]),
		SHA256RootHash: root,
	}
	gotSTH, err := ct.SerializeSTHSignatureInput(sth)
	if err != nil {
		t.Check("sth-signature-input-known-answer", false, "SerializeSTHSignatureInput: %v", err)
	} else {
		t.EqualBytes("sth-signature-input-known-answer", gotSTH, pinnedSTH)
	}

	// ---- 10. A precertificate leaf cannot be built without its issuer. --
	_, err = ct.MerkleTreeLeafFromRawChain(
		[]ct.ASN1Cert{{Data: precert.Raw}}, ct.PrecertLogEntryType, pinnedTimestamp)
	t.Check("precert-leaf-needs-an-issuer", err != nil,
		"MerkleTreeLeafFromRawChain built a precert leaf from a one-element chain")

	// ---- 11. The leaf survives a decode/encode round trip. --------------
	var back ct.MerkleTreeLeaf
	rest, err := tls.Unmarshal(encLeaf, &back)
	if err != nil || len(rest) != 0 {
		t.Check("leaf-roundtrips-through-tls-unmarshal", false,
			"tls.Unmarshal(leaf): err=%v trailing=%d", err, len(rest))
	} else {
		re, merr := tls.Marshal(back)
		ok := merr == nil && bytes.Equal(re, encLeaf) &&
			back.Version == ct.V1 && back.LeafType == ct.TimestampedEntryLeafType &&
			back.TimestampedEntry != nil &&
			back.TimestampedEntry.Timestamp == pinnedTimestamp &&
			back.TimestampedEntry.EntryType == ct.X509LogEntryType &&
			back.TimestampedEntry.X509Entry != nil &&
			bytes.Equal(back.TimestampedEntry.X509Entry.Data, leafCert.Raw)
		t.Check("leaf-roundtrips-through-tls-unmarshal", ok,
			"re-marshal err=%v, decoded leaf did not match the original", merr)
	}

	t.Done()
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

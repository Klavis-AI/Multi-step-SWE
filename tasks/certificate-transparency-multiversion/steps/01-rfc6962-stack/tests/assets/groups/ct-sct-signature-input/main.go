// Group ct-sct-signature-input.
//
// RFC 6962 s3.2: the signature in an SCT is computed over a TLS-encoded
// CertificateTimestamp structure. This group grades
// ct.SerializeSCTSignatureInput byte for byte against an encoding this driver
// builds itself from the RFC's presentation-language definition, for all three
// entry types the v1.0 tree knows (x509, precert, xjson), and grades the two
// documented refusals.
//
// Boundary: the root package's exported serialisation function. Error strings
// are not graded, only whether an error is returned.
package main

import (
	"crypto/sha256"
	"encoding/pem"
	"errors"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

const pinnedTimestamp = uint64(1365181456089)

func be64(v uint64) []byte {
	b := make([]byte, 8)
	for i := 7; i >= 0; i-- {
		b[i] = byte(v)
		v >>= 8
	}
	return b
}

func u24(n int) []byte { return []byte{byte(n >> 16), byte(n >> 8), byte(n)} }
func u16(n int) []byte { return []byte{byte(n >> 8), byte(n)} }

// header is version(1) || signature_type(1) || timestamp(8) || entry_type(2).
func header(entryType int) []byte {
	out := []byte{0x00, 0x00}
	out = append(out, be64(pinnedTimestamp)...)
	return append(out, u16(entryType)...)
}

func loadCert(t *vhelp.T, name string) *x509.Certificate {
	blk, _ := pem.Decode(vhelp.ReadFixture("rfc6962", name))
	if blk == nil {
		t.Fatalf("no PEM block in %s", name)
	}
	c, err := x509.ParseCertificate(blk.Bytes)
	if c == nil {
		t.Fatalf("%s did not parse: %v", name, err)
	}
	return c
}

func entryFor(te *ct.TimestampedEntry) ct.LogEntry {
	return ct.LogEntry{Leaf: ct.MerkleTreeLeaf{
		Version:          ct.V1,
		LeafType:         ct.TimestampedEntryLeafType,
		TimestampedEntry: te,
	}}
}

func main() {
	t := vhelp.New()

	cert := loadCert(t, "test-cert.pem")
	ca := loadCert(t, "ca-cert.pem")

	// --- x509 entry --------------------------------------------------------
	x509Entry := entryFor(&ct.TimestampedEntry{
		Timestamp: pinnedTimestamp,
		EntryType: ct.X509LogEntryType,
		X509Entry: &ct.ASN1Cert{Data: cert.Raw},
	})
	sct := ct.SignedCertificateTimestamp{SCTVersion: ct.V1, Timestamp: pinnedTimestamp}
	got, err := ct.SerializeSCTSignatureInput(sct, x509Entry)
	if err != nil {
		t.Fatalf("SerializeSCTSignatureInput(x509): %v", err)
	}
	want := append(header(0), u24(len(cert.Raw))...)
	want = append(want, cert.Raw...)
	want = append(want, 0x00, 0x00)
	t.EqualBytes("sctinput-x509-exact", got, want)
	t.Equal("sctinput-x509-length", len(got), 1+1+8+2+3+len(cert.Raw)+2)

	// --- precert entry -----------------------------------------------------
	keyHash := sha256.Sum256(ca.RawSubjectPublicKeyInfo)
	tbs := loadCert(t, "test-pre-cert.pem").RawTBSCertificate
	defanged, err := x509.BuildPrecertTBS(tbs, nil)
	if err != nil {
		t.Fatalf("BuildPrecertTBS: %v", err)
	}
	preEntry := entryFor(&ct.TimestampedEntry{
		Timestamp:    pinnedTimestamp,
		EntryType:    ct.PrecertLogEntryType,
		PrecertEntry: &ct.PreCert{IssuerKeyHash: keyHash, TBSCertificate: defanged},
	})
	gotPre, err := ct.SerializeSCTSignatureInput(sct, preEntry)
	if err != nil {
		t.Fatalf("SerializeSCTSignatureInput(precert): %v", err)
	}
	wantPre := append(header(1), keyHash[:]...)
	wantPre = append(wantPre, u24(len(defanged))...)
	wantPre = append(wantPre, defanged...)
	wantPre = append(wantPre, 0x00, 0x00)
	t.EqualBytes("sctinput-precert-exact", gotPre, wantPre)

	// --- xjson entry -------------------------------------------------------
	const payload = "{ \"key\": \"value\" }"
	jsonEntry := entryFor(&ct.TimestampedEntry{
		Timestamp: pinnedTimestamp,
		EntryType: ct.XJSONLogEntryType,
		JSONEntry: &ct.JSONDataEntry{Data: []byte(payload)},
	})
	gotJSON, err := ct.SerializeSCTSignatureInput(sct, jsonEntry)
	if err != nil {
		t.Fatalf("SerializeSCTSignatureInput(xjson): %v", err)
	}
	wantJSON := append(header(0x8000), u24(len(payload))...)
	wantJSON = append(wantJSON, payload...)
	wantJSON = append(wantJSON, 0x00, 0x00)
	t.EqualBytes("sctinput-xjson-exact", gotJSON, wantJSON)

	// --- the SCT's own extensions are part of the signed input -------------
	sctExt := ct.SignedCertificateTimestamp{
		SCTVersion: ct.V1,
		Timestamp:  pinnedTimestamp,
		Extensions: ct.CTExtensions{0xde, 0xad, 0xbe, 0xef},
	}
	gotExt, err := ct.SerializeSCTSignatureInput(sctExt, x509Entry)
	if err != nil {
		t.Fatalf("SerializeSCTSignatureInput(extensions): %v", err)
	}
	wantExt := append(append([]byte{}, want[:len(want)-2]...), 0x00, 0x04, 0xde, 0xad, 0xbe, 0xef)
	t.EqualBytes("sctinput-extensions-carried", gotExt, wantExt)

	// --- refusals ----------------------------------------------------------
	_, err = ct.SerializeSCTSignatureInput(ct.SignedCertificateTimestamp{SCTVersion: ct.Version(9)}, x509Entry)
	t.Case("sctinput-unknown-version-rejected", nonNil(err, "an SCT with version 9 was serialised"))

	bogus := entryFor(&ct.TimestampedEntry{Timestamp: pinnedTimestamp, EntryType: ct.LogEntryType(99)})
	_, err = ct.SerializeSCTSignatureInput(sct, bogus)
	t.Case("sctinput-unknown-entry-type-rejected", nonNil(err, "entry type 99 was serialised"))

	t.Done()
}

func nonNil(err error, why string) error {
	if err == nil {
		return errors.New(why)
	}
	return nil
}

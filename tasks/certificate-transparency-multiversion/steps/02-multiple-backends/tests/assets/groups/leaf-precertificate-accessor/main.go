// Group leaf-precertificate-accessor.
//
// Before v1.0.5 a MerkleTreeLeaf had exactly one accessor, X509Certificate,
// which parsed TimestampedEntry.X509Entry.Data whatever the entry type was --
// on a precert entry that field is nil, so the caller got a parse failure
// rather than an answer, and there was no way at all to reach the logged
// TBSCertificate of a precert entry.
//
// v1.0.5 makes the pair symmetric and type-checked: X509Certificate refuses a
// leaf that is not an X509 entry, the new Precertificate accessor returns the
// parsed TBSCertificate of a precert entry, and it refuses anything else. The
// reshaped ct.Precertificate that carries the result keeps the originally
// submitted DER alongside the parsed form.
package main

import (
	"crypto/sha256"
	"encoding/hex"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/asn1"
	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

const leafTimestamp = uint64(1500000000000)

var poisonOID = asn1.ObjectIdentifier{1, 3, 6, 1, 4, 1, 11129, 2, 4, 3}

func leafOf(te *ct.TimestampedEntry) *ct.MerkleTreeLeaf {
	return &ct.MerkleTreeLeaf{
		Version:          ct.V1,
		LeafType:         ct.TimestampedEntryLeafType,
		TimestampedEntry: te,
	}
}

func parse(t *vhelp.T, what string, der []byte) *x509.Certificate {
	c, err := x509.ParseCertificate(der)
	if err != nil {
		if _, ok := err.(x509.NonFatalErrors); !ok {
			t.Fatalf("parse %s: %v", what, err)
		}
	}
	return c
}

func main() {
	t := vhelp.New()

	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	precertDER := vhelp.ReadFixture("minted", "precert-poisoned.der")
	intDER := vhelp.ReadFixture("minted", "bench-intermediate.der")

	precert := parse(t, "precert-poisoned.der", precertDER)
	issuerKeyHash := sha256.Sum256(parse(t, "bench-intermediate.der", intDER).RawSubjectPublicKeyInfo)

	x509Leaf := leafOf(&ct.TimestampedEntry{
		Timestamp: leafTimestamp,
		EntryType: ct.X509LogEntryType,
		X509Entry: &ct.ASN1Cert{Data: leafDER},
	})
	precertLeaf := leafOf(&ct.TimestampedEntry{
		Timestamp: leafTimestamp,
		EntryType: ct.PrecertLogEntryType,
		PrecertEntry: &ct.PreCert{
			IssuerKeyHash:  issuerKeyHash,
			TBSCertificate: precert.RawTBSCertificate,
		},
	})

	// --- the X509 accessor -------------------------------------------------
	cert, err := x509Leaf.X509Certificate()
	if err != nil {
		if _, ok := err.(x509.NonFatalErrors); !ok {
			t.Fatalf("X509Certificate on an X509 entry: %v", err)
		}
	}
	t.Equal("x509-accessor-returns-the-logged-certificate",
		cert.SerialNumber.String()+"/"+cert.Subject.CommonName, "4097/valid.bench.invalid")

	_, err = precertLeaf.X509Certificate()
	t.Check("x509-accessor-refuses-a-precert-entry", err != nil,
		"X509Certificate answered for a precert entry")

	// --- the precertificate accessor ---------------------------------------
	tbs, err := precertLeaf.Precertificate()
	if err != nil {
		if _, ok := err.(x509.NonFatalErrors); !ok {
			t.Fatalf("Precertificate on a precert entry: %v", err)
		}
	}
	t.Equal("precertificate-accessor-returns-the-logged-tbs",
		tbs.SerialNumber.String()+"/"+tbs.Subject.CommonName, "8193/precert.bench.invalid")
	t.Equal("precertificate-accessor-keeps-issuer-and-validity",
		tbs.Issuer.CommonName+"/"+tbs.NotBefore.UTC().Format("2006-01-02T15:04:05Z")+
			"/"+tbs.NotAfter.UTC().Format("2006-01-02T15:04:05Z"),
		"Harbor CT Bench Intermediate/2020-01-01T00:00:00Z/2060-01-01T00:00:00Z")

	// The accessor parses the bytes the log stored, so the poison extension
	// that was in them is still there, still critical.
	var poison string
	for _, e := range tbs.Extensions {
		if e.Id.Equal(poisonOID) {
			poison = hex.EncodeToString(e.Value)
			if e.Critical {
				poison = "critical:" + poison
			}
		}
	}
	t.Equal("precertificate-accessor-does-not-rewrite-the-stored-bytes", poison, "critical:0500")

	_, err = x509Leaf.Precertificate()
	t.Check("precertificate-accessor-refuses-an-x509-entry", err != nil,
		"Precertificate answered for an X509 entry")

	// Unparseable stored bytes are an error, not a panic.
	bad := leafOf(&ct.TimestampedEntry{
		Timestamp:    leafTimestamp,
		EntryType:    ct.PrecertLogEntryType,
		PrecertEntry: &ct.PreCert{IssuerKeyHash: issuerKeyHash, TBSCertificate: []byte{0x30, 0x01, 0x00}},
	})
	_, err = bad.Precertificate()
	t.Check("unparseable-stored-tbs-is-an-error", err != nil, "garbage TBSCertificate was accepted")

	// --- the reshaped Precertificate ----------------------------------------
	p := ct.Precertificate{
		Submitted:      ct.ASN1Cert{Data: precertDER},
		IssuerKeyHash:  issuerKeyHash,
		TBSCertificate: tbs,
	}
	t.Equal("precertificate-keeps-submitted-der-beside-the-parsed-tbs",
		hex.EncodeToString(p.Submitted.Data)+"/"+p.TBSCertificate.SerialNumber.String(),
		hex.EncodeToString(precertDER)+"/8193")
	t.Equal("precertificate-carries-the-issuer-key-hash",
		hex.EncodeToString(p.IssuerKeyHash[:]), hex.EncodeToString(issuerKeyHash[:]))

	t.Done()
}

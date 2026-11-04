// Group serialization-logentry-from-leaf.
//
// v1.0.5 adds ct.LogEntryFromLeaf: the single conversion from the wire form a
// log serves at get-entries (a LeafEntry of opaque leaf_input / extra_data
// bytes) to the parsed ct.LogEntry every consumer in this repository works
// with. It must TLS-decode the MerkleTreeLeaf, reject trailing bytes, decode
// the matching extra-data structure for the entry type -- CertificateChain for
// an X509 entry, PrecertChainEntry for a precert entry -- parse the
// certificate, populate Chain with the issuers, and refuse an entry type it
// does not model.
//
// Every input here is built from pinned corpus DER with a pinned timestamp.
package main

import (
	"crypto/sha256"
	"encoding/hex"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

// Pinned: never the wall clock.
const leafTimestamp = uint64(1500000000000)

func marshal(t *vhelp.T, what string, v interface{}) []byte {
	b, err := tls.Marshal(v)
	if err != nil {
		t.Fatalf("tls.Marshal %s: %v", what, err)
	}
	return b
}

func leafOf(te *ct.TimestampedEntry) ct.MerkleTreeLeaf {
	return ct.MerkleTreeLeaf{
		Version:          ct.V1,
		LeafType:         ct.TimestampedEntryLeafType,
		TimestampedEntry: te,
	}
}

func main() {
	t := vhelp.New()

	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	intDER := vhelp.ReadFixture("minted", "bench-intermediate.der")
	rootDER := vhelp.ReadFixture("minted", "bench-root.der")
	precertDER := vhelp.ReadFixture("minted", "precert-poisoned.der")

	issuer, err := x509.ParseCertificate(intDER)
	if err != nil {
		if _, ok := err.(x509.NonFatalErrors); !ok {
			t.Fatalf("parse bench-intermediate.der: %v", err)
		}
	}
	issuerKeyHash := sha256.Sum256(issuer.RawSubjectPublicKeyInfo)

	precert, err := x509.ParseCertificate(precertDER)
	if err != nil {
		if _, ok := err.(x509.NonFatalErrors); !ok {
			t.Fatalf("parse precert-poisoned.der: %v", err)
		}
	}

	// ---- an X509 entry --------------------------------------------------
	x509Leaf := marshal(t, "x509 leaf", leafOf(&ct.TimestampedEntry{
		Timestamp: leafTimestamp,
		EntryType: ct.X509LogEntryType,
		X509Entry: &ct.ASN1Cert{Data: leafDER},
	}))
	x509Extra := marshal(t, "certificate chain", ct.CertificateChain{
		Entries: []ct.ASN1Cert{{Data: intDER}, {Data: rootDER}},
	})

	entry, err := ct.LogEntryFromLeaf(7, &ct.LeafEntry{LeafInput: x509Leaf, ExtraData: x509Extra})
	if err != nil {
		if _, ok := err.(x509.NonFatalErrors); !ok {
			t.Fatalf("LogEntryFromLeaf on an X509 entry: %v", err)
		}
	}
	t.Equal("x509-entry-keeps-the-index-and-the-leaf",
		[]interface{}{entry.Index, entry.Leaf.Version, entry.Leaf.LeafType, entry.Leaf.TimestampedEntry.Timestamp},
		[]interface{}{int64(7), ct.V1, ct.TimestampedEntryLeafType, leafTimestamp})
	t.Equal("x509-entry-parses-the-submitted-certificate",
		entry.X509Cert.SerialNumber.String()+"/"+entry.X509Cert.Subject.CommonName,
		"4097/valid.bench.invalid")
	if len(entry.Chain) != 2 {
		t.Check("x509-entry-chain-is-the-issuers-in-order", false, "got %d chain entries, want 2", len(entry.Chain))
	} else {
		t.Equal("x509-entry-chain-is-the-issuers-in-order",
			hex.EncodeToString(entry.Chain[0].Data)+"/"+hex.EncodeToString(entry.Chain[1].Data),
			hex.EncodeToString(intDER)+"/"+hex.EncodeToString(rootDER))
	}
	t.Check("x509-entry-has-no-precertificate", entry.Precert == nil, "Precert was populated for an X509 entry")

	// ---- a precert entry ------------------------------------------------
	precertLeaf := marshal(t, "precert leaf", leafOf(&ct.TimestampedEntry{
		Timestamp: leafTimestamp,
		EntryType: ct.PrecertLogEntryType,
		PrecertEntry: &ct.PreCert{
			IssuerKeyHash:  issuerKeyHash,
			TBSCertificate: precert.RawTBSCertificate,
		},
	}))
	precertExtra := marshal(t, "precert chain entry", ct.PrecertChainEntry{
		PreCertificate:   ct.ASN1Cert{Data: precertDER},
		CertificateChain: []ct.ASN1Cert{{Data: intDER}},
	})

	pEntry, err := ct.LogEntryFromLeaf(9, &ct.LeafEntry{LeafInput: precertLeaf, ExtraData: precertExtra})
	if err != nil {
		if _, ok := err.(x509.NonFatalErrors); !ok {
			t.Fatalf("LogEntryFromLeaf on a precert entry: %v", err)
		}
	}
	if pEntry.Precert == nil {
		t.Check("precert-entry-keeps-the-submitted-precertificate", false, "Precert is nil")
		t.Check("precert-entry-carries-the-issuer-key-hash", false, "Precert is nil")
		t.Check("precert-entry-parses-the-tbs-certificate", false, "Precert is nil")
	} else {
		t.Equal("precert-entry-keeps-the-submitted-precertificate",
			hex.EncodeToString(pEntry.Precert.Submitted.Data), hex.EncodeToString(precertDER))
		t.Equal("precert-entry-carries-the-issuer-key-hash",
			hex.EncodeToString(pEntry.Precert.IssuerKeyHash[:]), hex.EncodeToString(issuerKeyHash[:]))
		t.Equal("precert-entry-parses-the-tbs-certificate",
			pEntry.Precert.TBSCertificate.SerialNumber.String()+"/"+pEntry.Precert.TBSCertificate.Subject.CommonName,
			"8193/precert.bench.invalid")
	}
	t.Check("precert-entry-has-no-x509-certificate", pEntry.X509Cert == nil, "X509Cert was populated for a precert entry")

	// ---- refusals --------------------------------------------------------
	xjsonLeaf := marshal(t, "xjson leaf", leafOf(&ct.TimestampedEntry{
		Timestamp: leafTimestamp,
		EntryType: ct.XJSONLogEntryType,
		JSONEntry: &ct.JSONDataEntry{Data: []byte("{}")},
	}))
	_, err = ct.LogEntryFromLeaf(1, &ct.LeafEntry{LeafInput: xjsonLeaf, ExtraData: nil})
	t.Check("unmodelled-entry-type-is-an-error", err != nil, "an entry type with no chain shape was accepted")

	_, err = ct.LogEntryFromLeaf(2, &ct.LeafEntry{LeafInput: append(append([]byte{}, x509Leaf...), 0x00), ExtraData: x509Extra})
	t.Check("trailing-bytes-after-the-leaf-are-an-error", err != nil, "a leaf with a trailing byte was accepted")

	_, err = ct.LogEntryFromLeaf(3, &ct.LeafEntry{LeafInput: x509Leaf, ExtraData: append(append([]byte{}, x509Extra...), 0x00)})
	t.Check("trailing-bytes-after-the-extra-data-are-an-error", err != nil, "extra data with a trailing byte was accepted")

	_, err = ct.LogEntryFromLeaf(4, &ct.LeafEntry{LeafInput: []byte{}, ExtraData: x509Extra})
	t.Check("empty-leaf-input-is-an-error", err != nil, "an empty leaf_input was accepted")

	t.Done()
}

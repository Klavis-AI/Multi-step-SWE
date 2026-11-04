// Group x509util-files-and-oids.
//
// v1.0.5 adds x509util/files.go -- the loader every new command-line tool in
// this release uses to accept a certificate source that may be DER or PEM, a
// bundle or a single object -- and adds the renderers for the GeneralNames
// type the x509 fork grew in the same release. The OID helpers are respelled
// to the Go-initialism form (OIDInExtensions, OIDForStandardExtension), which
// is the spelling every later release keeps.
package main

import (
	"encoding/hex"
	"net"
	"os"
	"path/filepath"

	"github.com/google/certificate-transparency-go/asn1"
	"github.com/google/certificate-transparency-go/x509"
	"github.com/google/certificate-transparency-go/x509/pkix"
	"github.com/google/certificate-transparency-go/x509util"

	"ctverify/vhelp"
)

var (
	poisonOID   = asn1.ObjectIdentifier{1, 3, 6, 1, 4, 1, 11129, 2, 4, 3}
	basicOID    = asn1.ObjectIdentifier{2, 5, 29, 19}
	inventedOID = asn1.ObjectIdentifier{1, 2, 3, 4, 5}
)

func hexAll(bs [][]byte) []string {
	out := make([]string, 0, len(bs))
	for _, b := range bs {
		out = append(out, hex.EncodeToString(b))
	}
	return out
}

func main() {
	t := vhelp.New()

	rootDER := vhelp.ReadFixture("minted", "bench-root.der")
	intDER := vhelp.ReadFixture("minted", "bench-intermediate.der")
	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")

	// A PEM bundle is split into its blocks, in file order.
	blocks, err := x509util.ReadPossiblePEMFile(vhelp.Fixture("minted", "roots-bench-plus-int.pem"), "CERTIFICATE")
	if err != nil {
		t.Check("pem-bundle-is-split-into-its-blocks", false, "ReadPossiblePEMFile: %v", err)
	} else {
		t.Equal("pem-bundle-is-split-into-its-blocks", hexAll(blocks),
			[]string{hex.EncodeToString(rootDER), hex.EncodeToString(intDER)})
	}

	// A DER file has no blocks, so it comes back whole, as one element.
	blocks, err = x509util.ReadPossiblePEMFile(vhelp.Fixture("minted", "leaf-valid.der"), "CERTIFICATE")
	if err != nil {
		t.Check("der-file-is-returned-whole", false, "ReadPossiblePEMFile: %v", err)
	} else {
		t.Equal("der-file-is-returned-whole", hexAll(blocks), []string{hex.EncodeToString(leafDER)})
	}

	// Blocks of other types in the same file are skipped.
	mixed := filepath.Join(os.Getenv("CT_SCRATCH"), "mixed.pem")
	body := append([]byte{}, vhelp.ReadFixture("minted", "keys", "leaf-key.pem")...)
	body = append(body, vhelp.ReadFixture("minted", "bench-root.pem")...)
	body = append(body, vhelp.ReadFixture("minted", "keys", "root-key.pem")...)
	if err := os.WriteFile(mixed, body, 0600); err != nil {
		t.Fatalf("write %s: %v", mixed, err)
	}
	blocks, err = x509util.ReadPossiblePEMFile(mixed, "CERTIFICATE")
	if err != nil {
		t.Check("other-block-types-in-the-same-file-are-skipped", false, "ReadPossiblePEMFile: %v", err)
	} else {
		t.Equal("other-block-types-in-the-same-file-are-skipped", hexAll(blocks),
			[]string{hex.EncodeToString(rootDER)})
	}

	_, err = x509util.ReadPossiblePEMFile(filepath.Join(os.Getenv("CT_SCRATCH"), "absent.pem"), "CERTIFICATE")
	t.Check("a-missing-file-is-an-error", err != nil, "a missing file was accepted")

	// A target with no http(s) scheme is a filename, and is read as one.
	blocks, err = x509util.ReadPossiblePEMURL(vhelp.Fixture("minted", "roots-bench-plus-int.pem"), "CERTIFICATE")
	if err != nil {
		t.Check("a-target-without-a-scheme-is-read-as-a-file", false, "ReadPossiblePEMURL: %v", err)
	} else {
		t.Equal("a-target-without-a-scheme-is-read-as-a-file", hexAll(blocks),
			[]string{hex.EncodeToString(rootDER), hex.EncodeToString(intDER)})
	}

	// --- OID helpers --------------------------------------------------------
	precert, err := x509.ParseCertificate(vhelp.ReadFixture("minted", "precert-poisoned.der"))
	if err != nil {
		if _, ok := err.(x509.NonFatalErrors); !ok {
			t.Fatalf("parse precert-poisoned.der: %v", err)
		}
	}
	// OIDInExtensions reports how many extensions carry the OID and whether
	// any of them is critical -- not an index, which is what makes it usable
	// for the duplicate-extension checks the new tools perform.
	count, critical := x509util.OIDInExtensions(poisonOID, precert.Extensions)
	t.Equal("oid-in-extensions-counts-the-critical-poison-extension",
		[]interface{}{count, critical}, []interface{}{1, true})

	count, critical = x509util.OIDInExtensions(inventedOID, precert.Extensions)
	t.Equal("oid-in-extensions-reports-an-absent-oid",
		[]interface{}{count, critical}, []interface{}{0, false})

	t.Equal("oid-for-standard-extension-classifies-each-oid",
		[]bool{
			x509util.OIDForStandardExtension(basicOID),
			x509util.OIDForStandardExtension(poisonOID),
			x509util.OIDForStandardExtension(inventedOID),
		},
		[]bool{true, true, false})

	// --- GeneralNames renderers ---------------------------------------------
	// CLIENT-API.md section 5.2 fixes the field order, the per-entry prefixes,
	// the ", " join and the lower-case hex completely, so both renderings are
	// compared as whole strings (TEST_INTERFACE.md section 6.1, item 2). The
	// inputs below are pinned so that every one of the seven fields and both
	// IPNet spellings are exercised in one call.
	_, ipNet, err := net.ParseCIDR("10.1.0.0/16")
	if err != nil {
		t.Fatalf("ParseCIDR: %v", err)
	}
	gn := x509.GeneralNames{
		DNSNames:       []string{"valid.bench.invalid", "other.bench.invalid"},
		EmailAddresses: []string{"ct@bench.invalid"},
		DirectoryNames: []pkix.Name{{CommonName: "Harbor CT Bench Root"}},
		URIs:           []string{"http://bench.invalid/ct"},
		IPNets:         []net.IPNet{*ipNet},
		RegisteredIDs:  []asn1.ObjectIdentifier{basicOID},
		OtherNames:     []x509.OtherName{{TypeID: poisonOID, Value: asn1.RawValue{Bytes: []byte{0xde, 0xad}}}},
	}
	t.Equal("general-names-are-rendered-in-a-fixed-order",
		x509util.GeneralNamesToString(&gn),
		"DNS:valid.bench.invalid, DNS:other.bench.invalid, email:ct@bench.invalid, "+
			"DirName:CN=Harbor CT Bench Root, URI:http://bench.invalid/ct, "+
			"IP Address:10.1.0.0/ffff0000, Registered ID:2.5.29.19, "+
			"othername:1.3.6.1.4.1.11129.2.4.3=dead")

	t.Equal("other-name-is-rendered-as-oid-equals-hex",
		x509util.OtherNameToString(x509.OtherName{TypeID: inventedOID, Value: asn1.RawValue{Bytes: []byte{0x01, 0x02}}}),
		"1.2.3.4.5=0102")

	t.Done()
}

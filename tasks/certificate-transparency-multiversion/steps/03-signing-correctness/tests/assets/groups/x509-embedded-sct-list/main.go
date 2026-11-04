// Group x509-embedded-sct-list.
//
// v1.0.10 teaches the forked x509 parser about RFC 6962's SCT-list extension
// (OID 1.3.6.1.4.1.11129.2.4.2): Certificate gains RawSCT, holding the raw
// extension value, and SCTList, the decoded TLS list of SerializedSCTs. The
// expected values below are the bytes of the pinned upstream fixtures.
//
// The two fields are read as well as written: CreateCertificate synthesises
// the extension from whichever of RawSCT and SCTList a template supplies, so
// the last three cases take the list out of a pinned certificate, put it into
// a template, sign, re-parse, and require the same bytes back. The signing key
// is the pinned corpus key and the validity window is fixed: SCOPE-TIME, no
// wall clock is read.
//
// v1.0.5's x509 has neither field and no SerializedSCT /
// SignedCertificateTimestampList type, so it cannot express the observation at
// all.
package main

import (
	"crypto/ecdsa"
	"crypto/rand"
	stdx509 "crypto/x509"
	"encoding/binary"
	"encoding/hex"
	"encoding/pem"
	"fmt"
	"math/big"
	"time"

	"github.com/google/certificate-transparency-go/x509"
	"github.com/google/certificate-transparency-go/x509/pkix"

	"ctverify/vhelp"
)

const (
	embeddedRawSCT = "0078007600df1c2ec11500945247a96168325ddc5c7959e8f7c6d388fc002e0bbd3f74d764" +
		"0000013ddb27df9300000403004730450220482f6751af35dba65436be1fd6640f3dbf9a4142949592453" +
		"0288fa3e5e23e06022100e4edc0db3ac572b1e2f5e8ab6a680653987dcf41027dfeffa105519d89edbf08"
	embeddedSCT0 = "00df1c2ec11500945247a96168325ddc5c7959e8f7c6d388fc002e0bbd3f74d764" +
		"0000013ddb27df9300000403004730450220482f6751af35dba65436be1fd6640f3dbf9a4142949592453" +
		"0288fa3e5e23e06022100e4edc0db3ac572b1e2f5e8ab6a680653987dcf41027dfeffa105519d89edbf08"
	invalidSCT0 = "00df1c2ec11500945247a96168325ddc5c7959e8f7c6d388fc002e0bbd3f74d764" +
		"0000013ddb27e2a40000040300473045022100a6d34517f3392d9ec5d257adf1c597dc45bd4cd3b73856c" +
		"616a9fb99e5ae75a802205e26c8d1c7e222fe8cda29baeb04a834ee97d34fd81718f1aae0cd66f4b8a93f"
	poisonOID = "1.3.6.1.4.1.11129.2.4.3"
)

func parse(t *vhelp.T, name string) *x509.Certificate {
	blk, _ := pem.Decode(vhelp.ReadFixture("rfc6962", name))
	if blk == nil {
		t.Fatalf("rfc6962/%s is not PEM", name)
	}
	c, err := x509.ParseCertificate(blk.Bytes)
	if c == nil {
		t.Fatalf("rfc6962/%s: %v", name, err)
	}
	return c
}

// relist rebuilds the TLS framing of the SCT list from the decoded entries:
// uint16 total length, then uint16 length plus value for each SerializedSCT.
func relist(c *x509.Certificate) []byte {
	total := 0
	for _, s := range c.SCTList.SCTList {
		total += 2 + len(s.Val)
	}
	out := make([]byte, 2, 2+total)
	binary.BigEndian.PutUint16(out, uint16(total))
	for _, s := range c.SCTList.SCTList {
		var l [2]byte
		binary.BigEndian.PutUint16(l[:], uint16(len(s.Val)))
		out = append(out, l[:]...)
		out = append(out, s.Val...)
	}
	return out
}

func signingKey(t *vhelp.T) *ecdsa.PrivateKey {
	blk, _ := pem.Decode(vhelp.ReadFixture("minted", "keys", "log-key.pem"))
	if blk == nil {
		t.Fatalf("minted/keys/log-key.pem is not PEM")
	}
	k, err := stdx509.ParsePKCS8PrivateKey(blk.Bytes)
	if err != nil {
		t.Fatalf("minted/keys/log-key.pem: %v", err)
	}
	ek, ok := k.(*ecdsa.PrivateKey)
	if !ok {
		t.Fatalf("minted/keys/log-key.pem is not an EC key")
	}
	return ek
}

// issue self-signs tmpl with the pinned key and parses the result back. Only
// the SCT-carrying fields of tmpl are ever set by the callers below.
func issue(t *vhelp.T, tmpl *x509.Certificate) (*x509.Certificate, error) {
	key := signingKey(t)
	tmpl.SerialNumber = big.NewInt(0x43544631)
	tmpl.Subject = pkix.Name{CommonName: "harbor-ct-bench"}
	tmpl.NotBefore = time.Unix(1500000000, 0).UTC()
	tmpl.NotAfter = time.Unix(1800000000, 0).UTC()
	tmpl.BasicConstraintsValid = true
	der, err := x509.CreateCertificate(rand.Reader, tmpl, tmpl, &key.PublicKey, key)
	if err != nil {
		return nil, fmt.Errorf("CreateCertificate: %v", err)
	}
	c, err := x509.ParseCertificate(der)
	if c == nil {
		return nil, fmt.Errorf("ParseCertificate of the issued certificate: %v", err)
	}
	return c, nil
}

func main() {
	t := vhelp.New()

	emb := parse(t, "test-embedded-cert.pem")
	t.Equal("embedded-cert-raw-sct-exact", hex.EncodeToString(emb.RawSCT), embeddedRawSCT)
	t.Equal("embedded-cert-holds-one-sct", len(emb.SCTList.SCTList), 1)
	if len(emb.SCTList.SCTList) == 1 {
		t.Equal("embedded-cert-sct-value-exact", hex.EncodeToString(emb.SCTList.SCTList[0].Val), embeddedSCT0)
	} else {
		t.Check("embedded-cert-sct-value-exact", false, "no SCT decoded")
	}
	t.Equal("embedded-cert-list-framing-round-trips", hex.EncodeToString(relist(emb)), embeddedRawSCT)

	inv := parse(t, "test-invalid-embedded-cert.pem")
	got := ""
	if len(inv.SCTList.SCTList) == 1 {
		got = hex.EncodeToString(inv.SCTList.SCTList[0].Val)
	}
	t.Equal("second-embedded-cert-sct-value-exact", got, invalidSCT0)

	// A precertificate carries the poison marker and no SCT list.
	pre := parse(t, "test-pre-cert.pem")
	unhandled := ""
	for _, o := range pre.UnhandledCriticalExtensions {
		unhandled += o.String() + ";"
	}
	t.Equal("precert-has-poison-and-no-sct-list",
		fmt.Sprintf("%d/%d/%s", len(pre.RawSCT), len(pre.SCTList.SCTList), unhandled),
		fmt.Sprintf("0/0/%s;", poisonOID))

	// A plain certificate has neither.
	plain := parse(t, "test-cert.pem")
	t.Equal("plain-cert-has-no-sct-list",
		fmt.Sprintf("%d/%d/%d", len(plain.RawSCT), len(plain.SCTList.SCTList), len(plain.UnhandledCriticalExtensions)),
		"0/0/0")

	// A template that supplies the decoded list: the extension is built by
	// TLS-encoding it, so the issued certificate must carry exactly the bytes
	// the pinned certificate carried.
	fromList, err := issue(t, &x509.Certificate{SCTList: emb.SCTList})
	got = ""
	if err == nil {
		got = hex.EncodeToString(fromList.RawSCT)
	}
	t.Check("create-certificate-synthesises-extension-from-sct-list", got == embeddedRawSCT,
		"issued certificate RawSCT is %q (err %v), want the pinned list", got, err)

	// A template that supplies the raw extension content instead: same result,
	// and the parser decodes it back into the list.
	fromRaw, err := issue(t, &x509.Certificate{RawSCT: emb.RawSCT})
	got, gotVal := "", ""
	if err == nil {
		got = hex.EncodeToString(fromRaw.RawSCT)
		if len(fromRaw.SCTList.SCTList) == 1 {
			gotVal = hex.EncodeToString(fromRaw.SCTList.SCTList[0].Val)
		}
	}
	t.Check("create-certificate-synthesises-extension-from-raw-sct",
		got == embeddedRawSCT && gotVal == embeddedSCT0,
		"issued certificate RawSCT/SCT[0] are %q/%q (err %v)", got, gotVal, err)

	// The zero value of the carrier is an ordinary Go value holding no SCTs,
	// and a template that supplies it asks for no extension: nothing is
	// synthesised and nothing fails.
	var zero x509.SignedCertificateTimestampList
	var zeroEntry x509.SerializedSCT
	empty, err := issue(t, &x509.Certificate{SCTList: zero})
	shape := ""
	if err == nil {
		shape = fmt.Sprintf("%d/%d/%d/%d/%d",
			len(zero.SCTList), len(zeroEntry.Val), len(empty.RawSCT), len(empty.SCTList.SCTList),
			len(empty.UnhandledCriticalExtensions))
	}
	t.Check("empty-sct-list-is-constructible-and-adds-no-extension", shape == "0/0/0/0/0",
		"zero-value list / issued certificate shape is %q (err %v), want \"0/0/0/0/0\"", shape, err)

	t.Done()
}

// Group inherited-x509-fork-parse.
//
// The forked x509 and asn1 packages are the substrate every later milestone
// builds on. This group grades ordinary parsing and chain building on the
// pinned corpus: exact field values out of two certificates, the three EKU
// variants, the critical poison extension, signature checking up the minted
// chain, and path building under a pinned clock (never the wall clock).
package main

import (
	"encoding/pem"
	"errors"
	"fmt"
	"strings"
	"time"

	"github.com/google/certificate-transparency-go/asn1"
	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

// Every minted certificate is valid from 2020 to 2060, so this instant is
// inside the good ones and outside the expired and not-yet-valid ones.
var pinnedNow = time.Date(2030, 6, 1, 0, 0, 0, 0, time.UTC)

func load(t *vhelp.T, rel ...string) *x509.Certificate {
	raw := vhelp.ReadFixture(rel...)
	if blk, _ := pem.Decode(raw); blk != nil {
		raw = blk.Bytes
	}
	c, err := x509.ParseCertificate(raw)
	if c == nil {
		t.Fatalf("%v did not parse: %v", rel, err)
	}
	return c
}

func ekus(c *x509.Certificate) string {
	parts := make([]string, 0, len(c.ExtKeyUsage))
	for _, e := range c.ExtKeyUsage {
		parts = append(parts, fmt.Sprintf("%d", e))
	}
	return strings.Join(parts, ",")
}

func nonNil(err error, why string) error {
	if err == nil {
		return errors.New(why)
	}
	return nil
}

func main() {
	t := vhelp.New()

	leaf := load(t, "minted", "leaf-valid.der")
	inter := load(t, "minted", "bench-intermediate.der")
	root := load(t, "minted", "bench-root.der")

	t.Equal("minted-leaf-fields", fmt.Sprintf("%s|%s|%d|%s|%s|%v|%s",
		leaf.Subject.CommonName, leaf.Issuer.CommonName, leaf.SerialNumber,
		leaf.NotBefore.UTC().Format(time.RFC3339), leaf.NotAfter.UTC().Format(time.RFC3339),
		leaf.SignatureAlgorithm, strings.Join(leaf.DNSNames, ",")),
		"valid.bench.invalid|Harbor CT Bench Intermediate|4097|2020-01-01T00:00:00Z|2060-01-01T00:00:00Z|ECDSA-SHA256|valid.bench.invalid")

	ref := load(t, "rfc6962", "test-cert.pem")
	t.Equal("rfc6962-test-cert-fields", fmt.Sprintf("%d|%s|%s|%v|%s|%s",
		ref.SerialNumber,
		ref.NotBefore.UTC().Format(time.RFC3339), ref.NotAfter.UTC().Format(time.RFC3339),
		ref.SignatureAlgorithm,
		strings.Join(ref.Subject.Organization, ","),
		strings.Join(ref.Issuer.Organization, ",")),
		"6|2012-06-01T00:00:00Z|2022-06-01T00:00:00Z|SHA1-RSA|Certificate Transparency|Certificate Transparency CA")

	t.Equal("extended-key-usage-variants", fmt.Sprintf("%s/%s/%s",
		ekus(load(t, "minted", "leaf-eku-serverauth.der")),
		ekus(load(t, "minted", "leaf-eku-clientauth.der")),
		ekus(load(t, "minted", "leaf-eku-none.der"))),
		"1/2/")

	poisoned := load(t, "minted", "precert-poisoned.der")
	var poison *pkixExt
	for _, e := range poisoned.Extensions {
		if e.Id.Equal(asn1.ObjectIdentifier{1, 3, 6, 1, 4, 1, 11129, 2, 4, 3}) {
			poison = &pkixExt{critical: e.Critical, value: e.Value}
		}
	}
	t.Check("poison-extension-is-critical-null",
		poison != nil && poison.critical && len(poison.value) == 2 && poison.value[0] == 0x05 && poison.value[1] == 0x00,
		"poison=%v", poison)

	t.Case("signature-checks-up-the-chain", func() error {
		if err := leaf.CheckSignatureFrom(inter); err != nil {
			return fmt.Errorf("leaf from intermediate: %v", err)
		}
		return inter.CheckSignatureFrom(root)
	}())
	t.Case("signature-from-wrong-issuer-rejected",
		nonNil(leaf.CheckSignatureFrom(root), "the leaf verified directly against the root"))

	roots := x509.NewCertPool()
	roots.AddCert(root)
	inters := x509.NewCertPool()
	inters.AddCert(inter)
	opts := x509.VerifyOptions{
		Roots:         roots,
		Intermediates: inters,
		CurrentTime:   pinnedNow,
		KeyUsages:     []x509.ExtKeyUsage{x509.ExtKeyUsageAny},
	}
	chains, err := leaf.Verify(opts)
	t.Check("chain-builds-under-pinned-clock",
		err == nil && len(chains) == 1 && len(chains[0]) == 3,
		"err=%v chains=%d", err, len(chains))

	_, err = load(t, "minted", "leaf-expired.der").Verify(opts)
	t.Case("expired-leaf-rejected-under-pinned-clock", nonNil(err, "an expired certificate built a chain"))

	_, err = load(t, "minted", "leaf-notyetvalid.der").Verify(opts)
	t.Case("not-yet-valid-leaf-rejected-under-pinned-clock", nonNil(err, "a not-yet-valid certificate built a chain"))

	_, err = load(t, "minted", "leaf-unrooted.der").Verify(opts)
	t.Case("unrooted-leaf-rejected", nonNil(err, "a certificate under an unknown root built a chain"))

	// --- the forked asn1 ------------------------------------------------------
	type sample struct {
		N   int
		OID asn1.ObjectIdentifier
		S   string `asn1:"printable"`
	}
	in := sample{N: 1234, OID: asn1.ObjectIdentifier{1, 3, 6, 1, 4, 1, 11129, 2, 4, 2}, S: "harbor"}
	enc, err := asn1.Marshal(in)
	if err != nil {
		t.Fatalf("asn1.Marshal: %v", err)
	}
	var out sample
	rest, err := asn1.Unmarshal(enc, &out)
	t.Check("asn1-fork-roundtrip",
		err == nil && len(rest) == 0 && out.N == in.N && out.S == in.S && out.OID.Equal(in.OID),
		"err=%v trailing=%d out=%+v", err, len(rest), out)

	var oid asn1.ObjectIdentifier
	_, err = asn1.Unmarshal([]byte{0x06, 0x00}, &oid)
	t.Case("asn1-fork-rejects-zero-length-oid", nonNil(err, "a zero-length OBJECT IDENTIFIER was accepted"))

	t.Done()
}

type pkixExt struct {
	critical bool
	value    []byte
}

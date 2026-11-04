// Observes trillian/util.ExtraDataForChainHash, the out-of-line extra-data
// encoder this release adds beside the RFC 6962 one. Where ExtraDataForChain
// serialises the whole issuance chain into the leaf, this encodes only a
// hash of it, and the certificate or precertificate is treated differently in
// each case. Every expectation below is an exact byte string.
package main

import (
	"bytes"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/certificate-transparency-go/trillian/util"

	"ctverify/vhelp"
)

var pinnedHash = []byte{
	0x9f, 0x86, 0xd0, 0x81, 0x88, 0x4c, 0x7d, 0x65, 0x9a, 0x2f, 0xea, 0xa0,
	0xc5, 0x5a, 0xd0, 0x15, 0xa3, 0xbf, 0x4f, 0x1b, 0x2b, 0x0b, 0x82, 0x2c,
	0xd1, 0x5d, 0x6c, 0x15, 0xb0, 0xf0, 0x0a, 0x08,
}

func u24(n int) []byte { return []byte{byte(n >> 16), byte(n >> 8), byte(n)} }

func main() {
	t := vhelp.New()

	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	intDER := vhelp.ReadFixture("minted", "bench-intermediate.der")
	preDER := vhelp.ReadFixture("minted", "precert-poisoned.der")

	leaf := ct.ASN1Cert{Data: leafDER}
	pre := ct.ASN1Cert{Data: preDER}
	chain := []ct.ASN1Cert{{Data: intDER}}

	// A certificate entry carries only the hash: the certificate itself is
	// already in the Merkle leaf and must not be repeated here.
	got, err := util.ExtraDataForChainHash(leaf, pinnedHash, false)
	if err != nil {
		t.Fatalf("ExtraDataForChainHash(cert): %v", err)
	}
	t.EqualBytes("cert-extra-data-is-just-the-hash", got, append([]byte{0x00, 0x20}, pinnedHash...))

	other, err := util.ExtraDataForChainHash(ct.ASN1Cert{Data: intDER}, pinnedHash, false)
	if err != nil {
		t.Fatalf("ExtraDataForChainHash(other cert): %v", err)
	}
	t.Check("cert-extra-data-ignores-the-certificate", bytes.Equal(got, other),
		"two different certificates gave %x and %x", got, other)

	empty, err := util.ExtraDataForChainHash(leaf, []byte{}, false)
	if err != nil {
		t.Fatalf("ExtraDataForChainHash(empty hash): %v", err)
	}
	t.EqualBytes("cert-extra-data-empty-hash", empty, []byte{0x00, 0x00})

	// A precertificate entry keeps the precertificate and appends the hash.
	wantPre := append(u24(len(preDER)), preDER...)
	wantPre = append(wantPre, 0x00, 0x20)
	wantPre = append(wantPre, pinnedHash...)
	gotPre, err := util.ExtraDataForChainHash(pre, pinnedHash, true)
	if err != nil {
		t.Fatalf("ExtraDataForChainHash(precert): %v", err)
	}
	t.EqualBytes("precert-extra-data-keeps-the-precertificate", gotPre, wantPre)

	// The hash form and the RFC 6962 chain form must not collide: the old
	// encoder is unchanged and still writes the full chain.
	classic, err := util.ExtraDataForChain(leaf, chain, false)
	if err != nil {
		t.Fatalf("ExtraDataForChain: %v", err)
	}
	wantClassic := append(u24(len(intDER)+3), u24(len(intDER))...)
	wantClassic = append(wantClassic, intDER...)
	t.Check("chain-form-unchanged-and-distinct",
		bytes.Equal(classic, wantClassic) && !bytes.Equal(classic, got),
		"ExtraDataForChain gave %d bytes (want %d) and hash form gave %d bytes", len(classic), len(wantClassic), len(got))

	// The encoded output is exactly the new TLS structure, so it decodes.
	var back ct.CertificateChainHash
	rest, err := tls.Unmarshal(got, &back)
	t.Check("cert-extra-data-decodes-as-certificate-chain-hash",
		err == nil && len(rest) == 0 && bytes.Equal(back.IssuanceChainHash, pinnedHash),
		"decode gave (%x, rest %d, %v)", back.IssuanceChainHash, len(rest), err)

	_, err = util.ExtraDataForChainHash(pre, make([]byte, 257), true)
	t.Check("oversize-hash-rejected", err != nil,
		"a 257-byte issuance chain hash was encoded without error")

	t.Done()
}

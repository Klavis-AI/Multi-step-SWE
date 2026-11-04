// Group x509-rfc3779-blocks.
//
// v1.0.21 adds RFC 3779 support to the forked x509 package (CHANGELOG
// v1.0.20: "Support for RFC 3779 extensions"). x509.Certificate gains
// RPKIAddressRanges, RPKIASNumbers and RPKIRoutingDomainIDs, filled from the
// sbgp-ipAddrBlock (1.3.6.1.5.5.7.1.7) and sbgp-autonomousSysNum
// (1.3.6.1.5.5.7.1.8) extensions, along with the types IPAddressPrefix,
// IPAddressRange, IPAddressFamilyBlocks, ASIDRange and ASIdentifiers.
//
// Observed per function from a driver. The two extension bodies are built
// here as raw DER -- the ASN.1 CHOICE tagging by hand and the primitives with
// the *standard library's* encoding/asn1 -- so nothing expected comes from
// the code under test, and they exercise both alternatives of both CHOICEs:
// an address family with prefixes and a range, an inherit-from-issuer family
// with a SAFI byte, AS numbers with single ids and a range, and an inherited
// RDI. Every recovered value is compared exactly, including prefix bit
// lengths. The two address families are named through the package's own
// x509.IPv4AddressFamilyIndicator / x509.IPv6AddressFamilyIndicator rather
// than through the numbers 1 and 2, and those constants' values are pinned
// in a case of their own, so a fork that exported them under the wrong
// values is caught rather than agreed with.
//
// SCOPE-TIME: pinned signing key, constant validity dates.
//
// The last case covers the other half of the release's parsing stance: an
// RFC 3779 extension the parser cannot decode is reported as **non-fatal**
// and leaves its field nil, so the certificate still parses and is still
// usable. That is the opposite of the SIA handler added in the same release,
// where the equivalent damage is a hard failure with a nil certificate
// (x509-subject-info-access).
//
// v1.0.19 has none of these fields, types or OIDs, so this driver does not
// compile against it.
package main

import (
	"crypto"
	"crypto/rand"
	stdx509 "crypto/x509"
	"encoding/asn1"
	"encoding/hex"
	"encoding/pem"
	"fmt"
	"math/big"
	"strings"
	"time"

	"github.com/google/certificate-transparency-go/x509"
	"github.com/google/certificate-transparency-go/x509/pkix"

	"ctverify/vhelp"
)

var (
	notBefore = time.Date(2018, time.January, 1, 0, 0, 0, 0, time.UTC)
	notAfter  = time.Date(2028, time.January, 1, 0, 0, 0, 0, time.UTC)

	oidIPBlocks = asn1.ObjectIdentifier{1, 3, 6, 1, 5, 5, 7, 1, 7}
	oidASBlocks = asn1.ObjectIdentifier{1, 3, 6, 1, 5, 5, 7, 1, 8}

	nullDER = []byte{0x05, 0x00}
)

// tlv assembles one DER element by hand.
func tlv(tag byte, content []byte) []byte {
	out := []byte{tag}
	switch {
	case len(content) < 0x80:
		out = append(out, byte(len(content)))
	case len(content) < 0x100:
		out = append(out, 0x81, byte(len(content)))
	default:
		out = append(out, 0x82, byte(len(content)>>8), byte(len(content)))
	}
	return append(out, content...)
}

func der(t *vhelp.T, v interface{}) []byte {
	b, err := asn1.Marshal(v)
	if err != nil {
		t.Fatalf("encoding/asn1 marshal %T: %v", v, err)
	}
	return b
}

// bits renders an IP address prefix as RFC 3779 does: the leading octets of
// the address in a BIT STRING whose length is the prefix length.
func bits(t *vhelp.T, prefixLen int, octets ...byte) []byte {
	return der(t, asn1.BitString{Bytes: octets, BitLength: prefixLen})
}

// asRange is the RFC 3779 ASRange SEQUENCE.
type asRange struct {
	Min int
	Max int
}

func ipBlocksDER(t *vhelp.T) []byte {
	// IPv4: two prefixes and one range.
	v4Choice := tlv(0x30, concat(
		bits(t, 8, 0x0a),        // 10.0.0.0/8
		bits(t, 16, 0xc0, 0xa8), // 192.168.0.0/16
		tlv(0x30, concat( // 172.16.0.0 - 172.31.0.0
			bits(t, 16, 0xac, 0x10),
			bits(t, 16, 0xac, 0x1f))),
	))
	v4 := tlv(0x30, concat(
		der(t, []byte{0x00, 0x01}), // AFI 1, no SAFI
		v4Choice))
	// IPv6 with a SAFI byte: inherit from issuer.
	v6 := tlv(0x30, concat(
		der(t, []byte{0x00, 0x02, 0x01}), // AFI 2, SAFI 1
		nullDER))
	return tlv(0x30, concat(v4, v6))
}

func asBlocksDER(t *vhelp.T, withRDI bool) []byte {
	inner := tlv(0x30, concat(
		der(t, 64496),
		der(t, 64511),
		der(t, asRange{Min: 64500, Max: 64510})))
	out := tlv(0xA0, inner) // asnum [0] EXPLICIT
	if withRDI {
		out = concat(out, tlv(0xA1, nullDER)) // rdi [1] EXPLICIT, inherit
	}
	return tlv(0x30, out)
}

func concat(bs ...[]byte) []byte {
	var out []byte
	for _, b := range bs {
		out = append(out, b...)
	}
	return out
}

// renderPrefix turns one recovered prefix into an exact, comparable string.
// familyName maps a parsed AFI onto the exported constant that names it, so
// the family assertions below are stated in the package's own vocabulary.
// An AFI the package has no constant for is rendered as its number, which
// would fail the comparison rather than quietly pass it.
func familyName(afi uint16) string {
	switch afi {
	case x509.IPv4AddressFamilyIndicator:
		return "IPv4"
	case x509.IPv6AddressFamilyIndicator:
		return "IPv6"
	default:
		return fmt.Sprintf("afi-%d", afi)
	}
}

func renderPrefix(p x509.IPAddressPrefix) string {
	return fmt.Sprintf("%s/%d", hex.EncodeToString(p.Bytes), p.BitLength)
}

func main() {
	t := vhelp.New()

	priv, pub := signer(t)

	t.Equal("rfc3779-oid-constants",
		x509.OIDExtensionIPPrefixList.String()+" "+x509.OIDExtensionASList.String(),
		"1.3.6.1.5.5.7.1.7 1.3.6.1.5.5.7.1.8")

	tmpl := baseTemplate()
	tmpl.ExtraExtensions = []pkix.Extension{
		{Id: x509.OIDExtensionIPPrefixList, Value: ipBlocksDER(t)},
		{Id: x509.OIDExtensionASList, Value: asBlocksDER(t, true)},
	}
	cert := roundTrip(t, "both-extensions", tmpl, priv, pub)

	// ---- address blocks -------------------------------------------------
	t.Equal("address-family-block-count", len(cert.RPKIAddressRanges), 2)
	if len(cert.RPKIAddressRanges) != 2 {
		t.Done()
		return
	}
	v4, v6 := cert.RPKIAddressRanges[0], cert.RPKIAddressRanges[1]

	t.Equal("ipv4-family-afi-safi-and-inheritance",
		fmt.Sprintf("%s/%d/%t", familyName(v4.AFI), v4.SAFI, v4.InheritFromIssuer),
		"IPv4/0/false")

	var got []string
	for _, p := range v4.AddressPrefixes {
		got = append(got, renderPrefix(p))
	}
	t.Equal("ipv4-address-prefixes-exact", strings.Join(got, ","), "0a/8,c0a8/16")

	got = nil
	for _, r := range v4.AddressRanges {
		got = append(got, renderPrefix(r.Min)+"-"+renderPrefix(r.Max))
	}
	t.Equal("ipv4-address-ranges-exact", strings.Join(got, ","), "ac10/16-ac1f/16")

	t.Equal("ipv6-family-inherits-from-issuer",
		fmt.Sprintf("%s/%d/%t/%d/%d", familyName(v6.AFI), v6.SAFI, v6.InheritFromIssuer,
			len(v6.AddressPrefixes), len(v6.AddressRanges)), "IPv6/1/true/0/0")

	// The constants themselves are IANA address-family numbers, and the two
	// assertions above are only as good as their values, so those are pinned
	// here directly (X509-FORK.md section 5.3).
	t.Equal("address-family-indicator-constants",
		fmt.Sprintf("IPv4=%d IPv6=%d",
			x509.IPv4AddressFamilyIndicator, x509.IPv6AddressFamilyIndicator),
		"IPv4=1 IPv6=2")

	// ---- AS identifiers -------------------------------------------------
	if cert.RPKIASNumbers == nil || cert.RPKIRoutingDomainIDs == nil {
		t.Fatalf("RPKIASNumbers=%v RPKIRoutingDomainIDs=%v, want both set",
			cert.RPKIASNumbers, cert.RPKIRoutingDomainIDs)
	}
	got = nil
	for _, id := range cert.RPKIASNumbers.ASIDs {
		got = append(got, fmt.Sprint(id))
	}
	t.Equal("as-numbers-single-ids-exact", strings.Join(got, ","), "64496,64511")

	got = nil
	for _, r := range cert.RPKIASNumbers.ASIDRanges {
		got = append(got, fmt.Sprintf("%d-%d", r.Min, r.Max))
	}
	t.Equal("as-numbers-ranges-exact", strings.Join(got, ","), "64500-64510")

	t.Check("as-numbers-not-inherited", !cert.RPKIASNumbers.InheritFromIssuer,
		"asnum was reported as inherited from the issuer")

	t.Equal("rdi-inherits-from-issuer",
		fmt.Sprintf("%t/%d/%d", cert.RPKIRoutingDomainIDs.InheritFromIssuer,
			len(cert.RPKIRoutingDomainIDs.ASIDs),
			len(cert.RPKIRoutingDomainIDs.ASIDRanges)), "true/0/0")

	// ---- the OPTIONAL rdi field, omitted --------------------------------
	tmpl = baseTemplate()
	tmpl.ExtraExtensions = []pkix.Extension{
		{Id: x509.OIDExtensionASList, Value: asBlocksDER(t, false)},
	}
	c := roundTrip(t, "asnum-only", tmpl, priv, pub)
	t.Check("omitted-rdi-leaves-nil", c.RPKIASNumbers != nil && c.RPKIRoutingDomainIDs == nil,
		"asnum=%v rdi=%v", c.RPKIASNumbers, c.RPKIRoutingDomainIDs)

	// ---- no extensions at all -------------------------------------------
	c = roundTrip(t, "no-rfc3779", baseTemplate(), priv, pub)
	t.Check("no-rfc3779-fields-without-the-extensions",
		c.RPKIAddressRanges == nil && c.RPKIASNumbers == nil && c.RPKIRoutingDomainIDs == nil,
		"a certificate with neither extension reported RFC 3779 data")

	// ---- and damaged RFC 3779 extensions do not fail the parse ----------
	// Both bodies below are the valid encodings used above with one stray
	// byte appended, so the outer SEQUENCE decodes and there is something
	// left over. Each parser records that into the certificate's
	// NonFatalErrors and returns nil for its own field, so ParseCertificate
	// hands back a certificate *and* an error and IsFatal is false. The SIA
	// handler in this same release returns outright on the same damage.
	// X509-FORK.md sections 3.1 and 5.3.
	tmpl = baseTemplate()
	tmpl.ExtraExtensions = []pkix.Extension{
		{Id: x509.OIDExtensionIPPrefixList, Value: append(ipBlocksDER(t), 0x00)},
		{Id: x509.OIDExtensionASList, Value: append(asBlocksDER(t, true), 0x00)},
	}
	rawBroken, err := x509.CreateCertificate(rand.Reader, tmpl, tmpl, pub, priv)
	if err != nil {
		t.Fatalf("CreateCertificate(broken-rfc3779): %v", err)
	}
	broken, errBroken := x509.ParseCertificate(rawBroken)
	t.Equal("malformed-rfc3779-extensions-are-non-fatal",
		fmt.Sprintf("cert=%t err=%t fatal=%t addrs-nil=%t asnum-nil=%t rdi-nil=%t",
			broken != nil, errBroken != nil, x509.IsFatal(errBroken),
			broken != nil && broken.RPKIAddressRanges == nil,
			broken != nil && broken.RPKIASNumbers == nil,
			broken != nil && broken.RPKIRoutingDomainIDs == nil),
		"cert=true err=true fatal=false addrs-nil=true asnum-nil=true rdi-nil=true")

	t.Done()
}

func signer(t *vhelp.T) (crypto.Signer, crypto.PublicKey) {
	blk, _ := pem.Decode(vhelp.ReadFixture("minted", "keys", "root-key.pem"))
	if blk == nil {
		t.Fatalf("pinned key fixture is not PEM")
	}
	k, err := stdx509.ParsePKCS8PrivateKey(blk.Bytes)
	if err != nil {
		t.Fatalf("parse pinned key: %v", err)
	}
	s, ok := k.(crypto.Signer)
	if !ok {
		t.Fatalf("pinned key does not implement crypto.Signer")
	}
	return s, s.Public()
}

func baseTemplate() *x509.Certificate {
	return &x509.Certificate{
		SerialNumber:          big.NewInt(0x3779),
		Subject:               pkix.Name{CommonName: "harbor-ct-bench rfc3779 subject"},
		NotBefore:             notBefore,
		NotAfter:              notAfter,
		BasicConstraintsValid: true,
		IsCA:                  true,
		KeyUsage:              x509.KeyUsageCertSign,
	}
}

func roundTrip(t *vhelp.T, what string, tmpl *x509.Certificate, priv crypto.Signer, pub crypto.PublicKey) *x509.Certificate {
	raw, err := x509.CreateCertificate(rand.Reader, tmpl, tmpl, pub, priv)
	if err != nil {
		t.Fatalf("CreateCertificate(%s): %v", what, err)
	}
	c, err := x509.ParseCertificate(raw)
	if err != nil {
		t.Fatalf("ParseCertificate(%s): %v", what, err)
	}
	return c
}

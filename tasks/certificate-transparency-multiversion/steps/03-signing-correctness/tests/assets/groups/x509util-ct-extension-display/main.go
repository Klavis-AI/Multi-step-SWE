// Group x509util-ct-extension-display.
//
// v1.0.10 teaches x509util.CertificateToString to render the two RFC 6962
// extensions by name instead of leaving them in the unhandled bucket: a
// "RFC6962 Pre-Certificate Poison" section for OID 1.3.6.1.4.1.11129.2.4.3 and
// a "RFC6962 Certificate Transparency SCT" section that decodes each SCT's
// version, log id, timestamp and signature. Both OIDs are also added to the
// already-printed set so they are not repeated as unhandled extensions.
//
// The same release adds x509util.RevocationReasonToString, the CRL-side
// description helper (CLIENT-API.md §5.2 arrival table). The text it returns
// is descriptive and is SCOPE-DIAGNOSTIC: the case below grades that the
// function is present and answers a reason code, not its wording.
//
// The expected text below is the exact rendering of the pinned upstream
// fixtures. v1.0.5's x509util has neither section.
package main

import (
	"encoding/pem"
	"strings"

	"github.com/google/certificate-transparency-go/x509"
	"github.com/google/certificate-transparency-go/x509util"

	"ctverify/vhelp"
)

const (
	sctHeader   = "            RFC6962 Certificate Transparency SCT:\n"
	poisonBlock = "            RFC6962 Pre-Certificate Poison: critical\n                .....\n"
	sctOID      = "1.3.6.1.4.1.11129.2.4.2"
	poisonOID   = "1.3.6.1.4.1.11129.2.4.3"
)

const sctEntry = "              SCT [0]:\n" +
	"                  Version: 0\n" +
	"                  LogID: 3xwuwRUAlFJHqWFoMl3cXHlZ6PfG04j8AC4LvT9012Q=\n" +
	"                  Timestamp: 1365181456275\n" +
	"                  Signature: {SHA256 ECDSA}\n" +
	"                  Signature:\n"

const sctSigDump = "                    30:45:02:20:48:2f:67:51:af:35:db:a6:54:36:be:1f:\n" +
	"                    d6:64:0f:3d:bf:9a:41:42:94:95:92:45:30:28:8f:a3:\n" +
	"                    e5:e2:3e:06:02:21:00:e4:ed:c0:db:3a:c5:72:b1:e2:\n" +
	"                    f5:e8:ab:6a:68:06:53:98:7d:cf:41:02:7d:fe:ff:a1:\n" +
	"                    05:51:9d:89:ed:bf:08:\n"

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

func show(t *vhelp.T, name string) string {
	return x509util.CertificateToString(parse(t, name))
}

func main() {
	t := vhelp.New()

	emb := show(t, "test-embedded-cert.pem")
	t.Check("embedded-cert-sct-section-header", strings.Contains(emb, sctHeader),
		"CertificateToString did not emit the SCT section header")
	t.Check("embedded-cert-sct-entry-block", strings.Contains(emb, sctHeader+sctEntry),
		"CertificateToString did not emit the decoded SCT fields under the header")
	t.Check("embedded-cert-sct-signature-hexdump", strings.Contains(emb, sctEntry+sctSigDump),
		"CertificateToString did not emit the SCT signature hex dump")
	t.Check("embedded-cert-sct-oid-not-repeated", !strings.Contains(emb, sctOID),
		"the SCT OID is still printed as an unhandled extension")

	pre := show(t, "test-pre-cert.pem")
	t.Check("precert-poison-section", strings.Contains(pre, poisonBlock),
		"CertificateToString did not emit the poison section")
	t.Check("precert-poison-oid-not-repeated", !strings.Contains(pre, poisonOID),
		"the poison OID is still printed as an unhandled extension")

	// CLIENT-API.md 5.2: one "SCT [i]:" block per entry of cert.SCTList.SCTList
	// and, for an entry whose Val will not TLS-decode, a hex dump at 18 spaces
	// in place of the five fields. The undecodable entry is made here rather
	// than shipped as a fixture: the extension that puts the section in the
	// output is read from cert.Extensions, which this leaves untouched, so
	// replacing the decoded list is enough to reach the fallback arm and the
	// group needs no certificate it cannot also explain. Four bytes is shorter
	// than an SCT's fixed 32-byte log id, so no decoder can accept it.
	undec := parse(t, "test-embedded-cert.pem")
	undec.SCTList.SCTList = []x509.SerializedSCT{{Val: []byte{0xde, 0xad, 0xbe, 0xef}}}
	t.Check("undecodable-sct-entry-dumped-as-hex",
		strings.Contains(x509util.CertificateToString(undec),
			sctHeader+"              SCT [0]:\n                  de:ad:be:ef:\n"),
		"CertificateToString did not hex-dump an SCT entry that will not TLS-decode")

	plain := show(t, "test-cert.pem")
	t.Check("plain-cert-shows-neither-section",
		!strings.Contains(plain, sctHeader) && !strings.Contains(plain, "RFC6962 Pre-Certificate Poison"),
		"a certificate with neither extension rendered a CT section")

	// The revocation-reason helper the same release adds. Taking it as a
	// function value is deliberate: it pins the arity and the string return
	// without the driver having to name the reason-code type, and the untyped
	// constants below convert to whatever integer type the parameter has.
	// Distinct codes must describe themselves distinctly; an unassigned code
	// must still describe itself somehow. The wording is not graded.
	reason := x509util.RevocationReasonToString
	t.Check("revocation-reason-to-string-describes-each-code",
		len(reason(0)) > 0 && len(reason(1)) > 0 && reason(0) != reason(1) && len(reason(200)) > 0,
		"RevocationReasonToString gave %q, %q, %q", reason(0), reason(1), reason(200))

	t.Done()
}

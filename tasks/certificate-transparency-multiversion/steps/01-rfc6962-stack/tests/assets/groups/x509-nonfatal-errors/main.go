// Group x509-nonfatal-errors.
//
// The forked x509 package differs from Go's in that some defects are recorded
// as non-fatal: ParseCertificate still returns a usable *Certificate alongside
// a non-nil error, so a log can still see certificates that are technically
// invalid. Structural defects stay fatal and yield no certificate at all.
//
// This group grades that split at the exported boundary, using two pinned
// certificates shipped beside the group (the corpus has none that trip a
// non-fatal path) and three from the corpus that trip fatal ones. The verdict
// is "was a certificate returned", never the error's type or text.
package main

import (
	"encoding/pem"
	"errors"
	"os"
	"strings"

	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

func groupDER(t *vhelp.T, name string) []byte {
	b, err := os.ReadFile(vhelp.GroupFile(name))
	if err != nil {
		t.Fatalf("%s: %v", name, err)
	}
	return b
}

func fixturePEM(t *vhelp.T, rel ...string) []byte {
	blk, _ := pem.Decode(vhelp.ReadFixture(rel...))
	if blk == nil {
		t.Fatalf("no PEM block in %v", rel)
	}
	return blk.Bytes
}

func nonNil(err error, why string) error {
	if err == nil {
		return errors.New(why)
	}
	return nil
}

func main() {
	t := vhelp.New()

	// --- non-fatal: a SAN entry holding an IP address of length 5 ----------
	badIP := groupDER(t, "nfe-bad-ip-san.der")
	cert, err := x509.ParseCertificate(badIP)
	t.Check("nonfatal-bad-ip-returns-certificate-and-error", cert != nil && err != nil,
		"certificate=%v err=%v", cert != nil, err)
	if cert == nil {
		t.Fatalf("no certificate returned for the bad-IP SAN case")
	}
	t.EqualBytes("nonfatal-bad-ip-raw-preserved", cert.Raw, badIP)
	ips := []string{}
	for _, ip := range cert.IPAddresses {
		ips = append(ips, ip.String())
	}
	t.Check("nonfatal-bad-ip-good-san-entries-kept",
		strings.Join(cert.DNSNames, ",") == "good.nfe.invalid" &&
			strings.Join(ips, ",") == "10.0.0.2" &&
			cert.Subject.CommonName == "nfe-bad-ip-san.invalid",
		"dns=%v ips=%v cn=%q", cert.DNSNames, ips, cert.Subject.CommonName)

	// The certificate handed back despite the error is a complete one: its
	// signature still checks against the pinned root that minted it.
	root, rerr := x509.ParseCertificate(vhelp.ReadFixture("minted", "bench-root.der"))
	if root == nil {
		t.Fatalf("bench-root.der did not parse: %v", rerr)
	}
	t.Case("nonfatal-bad-ip-certificate-is-usable",
		root.CheckSignature(cert.SignatureAlgorithm, cert.RawTBSCertificate, cert.Signature))

	// --- non-fatal: critical nameConstraints carrying excluded subtrees ----
	ncDER := groupDER(t, "nfe-critical-name-constraints.der")
	nc, err := x509.ParseCertificate(ncDER)
	t.Check("nonfatal-critical-name-constraints-returns-certificate-and-error", nc != nil && err != nil,
		"certificate=%v err=%v", nc != nil, err)
	if nc == nil {
		t.Fatalf("no certificate returned for the nameConstraints case")
	}
	t.Check("nonfatal-critical-name-constraints-permitted-kept",
		strings.Join(nc.PermittedDNSDomains, ",") == "permitted.nfe.invalid" && nc.IsCA,
		"permitted=%v isCA=%v", nc.PermittedDNSDomains, nc.IsCA)

	// --- a well-formed certificate has no error at all ----------------------
	good, err := x509.ParseCertificate(vhelp.ReadFixture("minted", "leaf-valid.der"))
	t.Check("wellformed-certificate-parses-without-error", good != nil && err == nil,
		"certificate=%v err=%v", good != nil, err)

	// --- fatal defects produce no certificate --------------------------------
	trunc, err := x509.ParseCertificate(badIP[:len(badIP)-20])
	t.Case("fatal-truncated-der-returns-nil-certificate", func() error {
		if trunc != nil || err == nil {
			return errors.New("a truncated encoding still produced a certificate")
		}
		return nil
	}())

	nonMin, err := x509.ParseCertificate(fixturePEM(t, "malformed", "xf-der-invalid-nonminimal-int.pem"))
	t.Case("fatal-nonminimal-integer-returns-nil-certificate", func() error {
		if nonMin != nil {
			return errors.New("a non-minimally-encoded INTEGER produced a certificate")
		}
		return nonNil(err, "a non-minimally-encoded INTEGER produced no error")
	}())

	curve, err := x509.ParseCertificate(fixturePEM(t, "malformed", "xf-pubkey-ecdsa-secp192r1.pem"))
	t.Case("fatal-unsupported-curve-returns-nil-certificate", func() error {
		if curve != nil {
			return errors.New("a secp192r1 key produced a certificate")
		}
		return nonNil(err, "a secp192r1 key produced no error")
	}())

	t.Done()
}

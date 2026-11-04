// Group inherited-precert-and-x509.
//
// Step 1's other boundary, re-run at this milestone: the forked x509's
// precertificate processing (poison detection, RemoveCTPoison,
// BuildPrecertTBS) and the extension fields the CTFE's admission path reads.
// None of it is new at v1.1.0, so a do-nothing continuation is entitled to
// pass this group.
//
// Every expected value is a constant taken from the pinned fixture corpus:
// exact lengths, exact SHA-256 digests of the rebuilt TBSCertificate, exact
// extension OID sequences, exact key-usage bits.
//
// DEV-DROPPED: upstream's BuildPrecertTBS removes the poison extension
// outright, and that is what is graded here.
package main

import (
	"crypto/sha256"
	"encoding/hex"
	"strings"

	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

const (
	// TBSCertificate of minted/precert-poisoned.der with the poison removed.
	wantCleanTBSLen = 411
	wantCleanTBSSHA = "2ad8e7e3746fe291bd8b6e3288093d5a6c9a6894405978800bed884e29e8f8ef"
	// SHA-256 of minted/bench-intermediate.der's SubjectPublicKeyInfo.
	wantIssuerKeyHash = "2f5b59344c8fd42ba31eaeeed9dbb5d93c931fd858e98c86bbae84e7841f5e05"
	wantAKI           = "11e4571ee2c64cd8de601ebd3150656bd0ba27ee"

	poisonOID = "1.3.6.1.4.1.11129.2.4.3"
	stdExts   = "2.5.29.15,2.5.29.37,2.5.29.19,2.5.29.35,2.5.29.17"
)

func cert(t *vhelp.T, name string) *x509.Certificate {
	c, err := x509.ParseCertificate(vhelp.ReadFixture("minted", name))
	if err != nil {
		t.Fatalf("parse minted/%s: %v", name, err)
	}
	return c
}

func sha(b []byte) string { s := sha256.Sum256(b); return hex.EncodeToString(s[:]) }

func main() {
	t := vhelp.New()

	leaf := cert(t, "leaf-valid.der")
	intr := cert(t, "bench-intermediate.der")
	pre := cert(t, "precert-poisoned.der")
	nopo := cert(t, "precert-nopoison.der")

	list := func(c *x509.Certificate) string {
		parts := make([]string, 0, len(c.Extensions))
		for _, e := range c.Extensions {
			parts = append(parts, e.Id.String())
		}
		return strings.Join(parts, ",")
	}

	// --- poison detection ----------------------------------------------------
	t.Check("poisoned-cert-is-precertificate", pre.IsPrecertificate(),
		"minted/precert-poisoned.der was not recognised as a precertificate")
	t.Check("leaf-is-not-precertificate", !leaf.IsPrecertificate(),
		"minted/leaf-valid.der was recognised as a precertificate")
	t.Check("nopoison-cert-is-not-precertificate", !nopo.IsPrecertificate(),
		"minted/precert-nopoison.der has no poison extension but was recognised as a precertificate")

	// The poison is the last extension, critical, and carries ASN.1 NULL.
	poison := "absent"
	for _, e := range pre.Extensions {
		if e.Id.String() == poisonOID {
			poison = e.Id.String() + "/critical=" + btoa(e.Critical) + "/" + hex.EncodeToString(e.Value)
		}
	}
	t.Equal("poison-extension-critical-and-null", poison, poisonOID+"/critical=true/0500")
	t.Equal("poisoned-cert-extension-order", list(pre), stdExts+","+poisonOID)

	// --- removing it ---------------------------------------------------------
	clean, err := x509.RemoveCTPoison(pre.RawTBSCertificate)
	t.Check("removectpoison-exact",
		err == nil && len(clean) == wantCleanTBSLen && sha(clean) == wantCleanTBSSHA,
		"err=%v len=%d sha=%s", err, len(clean), sha(clean))

	_, err = x509.RemoveCTPoison(leaf.RawTBSCertificate)
	t.Check("removectpoison-on-plain-tbs-errors", err != nil,
		"RemoveCTPoison succeeded on a certificate that has no poison extension")

	rebuilt, err := x509.BuildPrecertTBS(pre.RawTBSCertificate, nil)
	t.Check("buildprecerttbs-without-preissuer-matches-removectpoison",
		err == nil && sha(rebuilt) == wantCleanTBSSHA && len(rebuilt) == wantCleanTBSLen,
		"err=%v len=%d sha=%s", err, len(rebuilt), sha(rebuilt))

	// The rebuilt TBSCertificate re-parses, and the poison is gone from it.
	tbs, err := x509.ParseTBSCertificate(rebuilt)
	got := "parse error"
	if err == nil {
		parts := make([]string, 0, len(tbs.Extensions))
		for _, e := range tbs.Extensions {
			parts = append(parts, e.Id.String())
		}
		got = strings.Join(parts, ",")
	}
	t.Equal("rebuilt-tbs-has-no-poison", got, stdExts)

	// A pre-issuer without the CT extended key usage may not be used.
	bad, err := x509.BuildPrecertTBS(pre.RawTBSCertificate, intr)
	t.Check("buildprecerttbs-rejects-preissuer-without-ct-eku", err != nil && len(bad) == 0,
		"BuildPrecertTBS accepted an issuer without the CertificateTransparency EKU (err=%v, %d bytes)", err, len(bad))

	// --- what the CTFE reads off these certificates --------------------------
	ikh := sha256.Sum256(intr.RawSubjectPublicKeyInfo)
	t.Equal("issuer-key-hash-exact", hex.EncodeToString(ikh[:]), wantIssuerKeyHash)

	ekus := make([]string, len(leaf.ExtKeyUsage))
	for i, e := range leaf.ExtKeyUsage {
		ekus[i] = itoa(int(e))
	}
	t.Equal("leaf-keyusage-and-eku-exact",
		itoa(int(leaf.KeyUsage))+"/"+strings.Join(ekus, "+")+"/"+btoa(leaf.IsCA),
		"5/1+2/false")

	t.Equal("leaf-san-and-aki-exact",
		strings.Join(leaf.DNSNames, ",")+"/"+hex.EncodeToString(leaf.AuthorityKeyId)+"/"+leaf.SerialNumber.String(),
		"valid.bench.invalid/"+wantAKI+"/4097")

	t.Equal("intermediate-basic-constraints-exact",
		btoa(intr.IsCA)+"/"+itoa(intr.MaxPathLen)+"/"+btoa(intr.MaxPathLenZero)+"/"+
			hex.EncodeToString(intr.SubjectKeyId),
		"true/0/true/"+wantAKI)

	t.Done()
}

func btoa(b bool) string {
	if b {
		return "true"
	}
	return "false"
}

func itoa(n int) string {
	if n == 0 {
		return "0"
	}
	neg := n < 0
	if neg {
		n = -n
	}
	var b [24]byte
	i := len(b)
	for n > 0 {
		i--
		b[i] = byte('0' + n%10)
		n /= 10
	}
	if neg {
		i--
		b[i] = '-'
	}
	return string(b[i:])
}

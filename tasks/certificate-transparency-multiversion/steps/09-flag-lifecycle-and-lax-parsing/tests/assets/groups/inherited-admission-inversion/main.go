// Group inherited-admission-inversion.
//
// The CTFE admission decision that earlier milestones built up, exercised at
// its documented library boundary: ctfe.NewCertValidationOpts builds the
// options, ctfe.ValidateChain applies them to a DER chain and returns the
// verified path or an error. Both signatures are unchanged across v1.1.0 and
// v1.1.2, and every option this group sets is one the predecessor already
// honoured, so a correct implementation of either release passes.
//
// The EKU filter v1.1.2 adds to the same function only fires when the options
// carry a non-empty ExtKeyUsage list; this group always leaves that list nil,
// which is why it is an inherited group and not a discriminator. The
// discriminator lives in eku-filter-admission and config-eku-list-semantics.
//
// SCOPE-TIME: currentTime is always passed explicitly, so ValidateChain never
// reaches time.Now() and the verdicts do not depend on when the suite runs.
package main

import (
	"strings"
	"time"

	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

// Pinned instants. The minted leaves are valid 2020-2060 (leaf-valid),
// 2001-2002 (leaf-expired) and 2090-2091 (leaf-notyetvalid).
var (
	at2030   = time.Date(2030, 1, 1, 0, 0, 0, 0, time.UTC)
	na2050   = time.Date(2050, 1, 1, 0, 0, 0, 0, time.UTC)
	na2070   = time.Date(2070, 1, 1, 0, 0, 0, 0, time.UTC)
	noEKUs   []x509.ExtKeyUsage
	noReject *time.Time
)

func rootPool(t *vhelp.T, name string) *ctfe.PEMCertPool {
	p := ctfe.NewPEMCertPool()
	if !p.AppendCertsFromPEM(vhelp.ReadFixture("minted", name)) {
		t.Fatalf("root pool %s: no certificate appended", name)
	}
	return p
}

func der(name string) []byte { return vhelp.ReadFixture("minted", name) }

// cns renders a verified path as the exact sequence of common names, so the
// assertion is about the whole path and its order rather than its length.
func cns(chain []*x509.Certificate) string {
	parts := make([]string, 0, len(chain))
	for _, c := range chain {
		parts = append(parts, c.Subject.CommonName)
	}
	return strings.Join(parts, "|")
}

func main() {
	t := vhelp.New()

	// Only the root is trusted, so every accepted path is exactly three certs
	// long and there is only one path to find.
	roots := rootPool(t, "roots-bench.pem")
	leafValid := der("leaf-valid.der")
	leafExpired := der("leaf-expired.der")
	leafUnrooted := der("leaf-unrooted.der")
	intermediate := der("bench-intermediate.der")

	plain := func(rejectExpired, rejectUnexpired, onlyCA bool, naStart, naLimit *time.Time) ctfe.CertValidationOpts {
		return ctfe.NewCertValidationOpts(roots, at2030, rejectExpired, rejectUnexpired, naStart, naLimit, onlyCA, noEKUs)
	}

	// 1. The submitted chain leaf->intermediate verifies to the trusted root,
	// and the returned path is the root-terminated one in submission order.
	chain, err := ctfe.ValidateChain([][]byte{leafValid, intermediate}, plain(false, false, false, nil, nil))
	if err != nil {
		t.Check("valid-chain-verifies-to-root", false, "ValidateChain: %v", err)
	} else {
		t.Equal("valid-chain-verifies-to-root", cns(chain),
			"valid.bench.invalid|Harbor CT Bench Intermediate|Harbor CT Bench Root")
	}

	// 2. A leaf issued by an untrusted root has no path at all.
	_, err = ctfe.ValidateChain([][]byte{leafUnrooted}, plain(false, false, false, nil, nil))
	t.Check("unrooted-chain-has-no-path", err != nil, "ValidateChain accepted an unrooted leaf")

	// 3. RFC 6962 s3.1 requires the submitted order to be the path order. The
	// same two certificates submitted the other way round are refused even
	// though both are known and the intermediate does chain to the root.
	_, err = ctfe.ValidateChain([][]byte{intermediate, leafValid}, plain(false, false, false, nil, nil))
	t.Check("reversed-chain-is-not-rfc-compliant", err != nil, "ValidateChain accepted a reversed chain")

	// 4. Expiry is not a path error: with time checks disabled inside the
	// verifier and rejectExpired off, an expired leaf is still logged.
	chain, err = ctfe.ValidateChain([][]byte{leafExpired, intermediate}, plain(false, false, false, nil, nil))
	if err != nil {
		t.Check("expired-leaf-accepted-when-not-rejecting", false, "ValidateChain: %v", err)
	} else {
		t.Equal("expired-leaf-accepted-when-not-rejecting", cns(chain),
			"expired.bench.invalid|Harbor CT Bench Intermediate|Harbor CT Bench Root")
	}

	// 5. ... and is a rejection once the option is set.
	_, err = ctfe.ValidateChain([][]byte{leafExpired, intermediate}, plain(true, false, false, nil, nil))
	t.Check("expired-leaf-rejected-when-rejecting", err != nil, "ValidateChain accepted an expired leaf")

	// 6. The inversion: an expired-only log refuses the live leaf.
	_, err = ctfe.ValidateChain([][]byte{leafValid, intermediate}, plain(false, true, false, nil, nil))
	t.Check("unexpired-leaf-rejected-when-rejecting-unexpired", err != nil, "ValidateChain accepted an unexpired leaf")

	// 7 and 8. The temporal-shard window is half-open on NotAfter: a leaf
	// whose NotAfter is at or after the limit, or before the start, is out.
	_, err = ctfe.ValidateChain([][]byte{leafValid, intermediate}, plain(false, false, false, nil, &na2050))
	t.Check("notafter-limit-excludes-later-leaf", err != nil, "ValidateChain accepted NotAfter >= limit")

	_, err = ctfe.ValidateChain([][]byte{leafValid, intermediate}, plain(false, false, false, &na2070, nil))
	t.Check("notafter-start-excludes-earlier-leaf", err != nil, "ValidateChain accepted NotAfter < start")

	// 9. A CA-only log refuses an end-entity certificate but keeps taking the
	// intermediate on its own.
	_, leafErr := ctfe.ValidateChain([][]byte{leafValid, intermediate}, plain(false, false, true, nil, nil))
	caChain, caErr := ctfe.ValidateChain([][]byte{intermediate}, plain(false, false, true, nil, nil))
	switch {
	case leafErr == nil:
		t.Check("only-ca-rejects-a-leaf", false, "ValidateChain accepted an end-entity leaf")
	case caErr != nil:
		t.Check("only-ca-rejects-a-leaf", false, "ValidateChain refused a CA: %v", caErr)
	default:
		t.Equal("only-ca-rejects-a-leaf", cns(caChain),
			"Harbor CT Bench Intermediate|Harbor CT Bench Root")
	}

	// 10. The poison extension is what makes a precertificate one, and the
	// classifier is exported alongside the validator.
	precert, err := x509.ParseCertificate(der("precert-poisoned.der"))
	if x509.IsFatal(err) {
		t.Fatalf("parse precert-poisoned.der: %v", err)
	}
	leaf, err := x509.ParseCertificate(leafValid)
	if x509.IsFatal(err) {
		t.Fatalf("parse leaf-valid.der: %v", err)
	}
	isPre, preErr := ctfe.IsPrecertificate(precert)
	isLeafPre, leafPreErr := ctfe.IsPrecertificate(leaf)
	if preErr != nil || leafPreErr != nil {
		t.Check("precertificate-recognised", false, "IsPrecertificate errors: %v / %v", preErr, leafPreErr)
	} else {
		t.Equal("precertificate-recognised", []bool{isPre, isLeafPre}, []bool{true, false})
	}

	_ = noReject
	t.Done()
}

// Group admission-reject-expired.
//
// v1.1.0 makes `reject_expired` mean what its name says: it rejects a
// certificate whose notAfter is in the past and says nothing at all about a
// certificate whose notBefore is in the future. Before v1.1.0 the same field
// was wired straight into x509.VerifyOptions as `DisableTimeChecks:
// !rejectExpired`, so turning it on switched on the *whole* validity-window
// check and a not-yet-valid certificate was rejected too.
//
// The whole group is driven through ctfe.NewCertValidationOpts, which is new
// at v1.1.0 (before it the struct's fields were unexported with no
// constructor), so the clock can be injected and no assertion touches the
// wall clock.
package main

import (
	"crypto/ecdsa"
	"crypto/rand"
	stdx509 "crypto/x509"
	"crypto/x509/pkix"
	"encoding/pem"
	"fmt"
	"math/big"
	"time"

	"ctverify/vhelp"

	"github.com/google/certificate-transparency-go/trillian/ctfe"
)

// Pinned instants. Relative to the minted corpus:
//
//	leaf-valid        notBefore 2020-01-01Z  notAfter 2060-01-01Z
//	leaf-expired      notBefore 2001-01-01Z  notAfter 2002-01-01Z
//	leaf-notyetvalid  notBefore 2090-01-01Z  notAfter 2091-01-01Z
//
// so at `atNow` exactly one of the three is current, one is expired and one is
// not yet valid.
var (
	atNow          = time.Date(2030, 1, 1, 0, 0, 0, 0, time.UTC)
	atExpiredEdge  = time.Date(2002, 1, 1, 0, 0, 0, 0, time.UTC)
	atExpiredEdge1 = time.Date(2002, 1, 1, 0, 0, 1, 0, time.UTC)
	atPrehistory   = time.Date(1990, 1, 1, 0, 0, 0, 0, time.UTC)
	atFarFuture    = time.Date(2095, 1, 1, 0, 0, 0, 0, time.UTC)
)

// Subject common names of the in-memory short-life pair (see shortLifeChain).
const (
	shortIntCN  = "Harbor CT Bench Short-Life Intermediate"
	shortLeafCN = "shortpath.bench.invalid"
)

func chain(t *vhelp.T, name string) [][]byte {
	raw := vhelp.ReadFixture("minted", name)
	var out [][]byte
	for {
		var blk *pem.Block
		blk, raw = pem.Decode(raw)
		if blk == nil {
			break
		}
		if blk.Type == "CERTIFICATE" {
			out = append(out, blk.Bytes)
		}
	}
	if len(out) == 0 {
		t.Fatalf("fixture minted/%s held no certificates", name)
	}
	return out
}

func roots(t *vhelp.T) *ctfe.PEMCertPool {
	p := ctfe.NewPEMCertPool()
	if !p.AppendCertsFromPEM(vhelp.ReadFixture("minted", "bench-root.pem")) {
		t.Fatalf("could not load minted/bench-root.pem into a PEMCertPool")
	}
	return p
}

// fixtureKey reads one of the pinned PKCS#8 EC keys out of minted/keys/.
func fixtureKey(t *vhelp.T, name string) *ecdsa.PrivateKey {
	blk, _ := pem.Decode(vhelp.ReadFixture("minted", "keys", name))
	if blk == nil {
		t.Fatalf("no PEM block in minted/keys/%s", name)
	}
	k, err := stdx509.ParsePKCS8PrivateKey(blk.Bytes)
	if err != nil {
		t.Fatalf("minted/keys/%s does not parse: %v", name, err)
	}
	ek, ok := k.(*ecdsa.PrivateKey)
	if !ok {
		t.Fatalf("minted/keys/%s is not an EC key", name)
	}
	return ek
}

// shortLifeChain builds, in memory, the one chain shape the pinned corpus does
// not have: an issuer that expires BEFORE its subject does.
//
//	Harbor CT Bench Short-Life Intermediate  notBefore 2000-01-01Z  notAfter 2025-01-01Z
//	shortpath.bench.invalid                  notBefore 2020-01-01Z  notAfter 2060-01-01Z
//
// At atNow (2030-01-01Z) the intermediate is five years expired and the leaf
// has thirty years to run. Every minted chain shares bench-intermediate, whose
// 2000..2100 window strictly contains every leaf's, so on those chains a
// whole-chain expiry loop and the v1.1.0 leaf-only test agree on every answer;
// this chain is the only one that tells them apart. HTTP-API.md 5.2: the
// v1.1.0 test is "explicit, leaf-only" and runs before path building, and path
// building itself has time checks disabled, so an expired *intermediate* does
// not reject the chain.
//
// Every key here is a pinned fixture -- minted/keys/{root,intermediate,leaf}-key.pem,
// the same three the corpus itself was minted with. Only the validity windows
// are new, and no assertion below reads a signature, a serial or a byte.
func shortLifeChain(t *vhelp.T) [][]byte {
	rootKey := fixtureKey(t, "root-key.pem")
	intKey := fixtureKey(t, "intermediate-key.pem")
	leafKey := fixtureKey(t, "leaf-key.pem")

	blk, _ := pem.Decode(vhelp.ReadFixture("minted", "bench-root.pem"))
	if blk == nil {
		t.Fatalf("no PEM block in minted/bench-root.pem")
	}
	root, err := stdx509.ParseCertificate(blk.Bytes)
	if err != nil {
		t.Fatalf("minted/bench-root.pem does not parse: %v", err)
	}

	intTmpl := &stdx509.Certificate{
		SerialNumber:          big.NewInt(0x08a1),
		Subject:               pkix.Name{CommonName: shortIntCN, Organization: []string{"Harbor CT Bench"}},
		NotBefore:             time.Date(2000, 1, 1, 0, 0, 0, 0, time.UTC),
		NotAfter:              time.Date(2025, 1, 1, 0, 0, 0, 0, time.UTC),
		KeyUsage:              stdx509.KeyUsageCertSign | stdx509.KeyUsageCRLSign | stdx509.KeyUsageDigitalSignature,
		BasicConstraintsValid: true,
		IsCA:                  true,
		MaxPathLen:            0,
		MaxPathLenZero:        true,
		SignatureAlgorithm:    stdx509.ECDSAWithSHA256,
	}
	intDER, err := stdx509.CreateCertificate(rand.Reader, intTmpl, root, &intKey.PublicKey, rootKey)
	if err != nil {
		t.Fatalf("could not mint the short-life intermediate: %v", err)
	}
	shortInt, err := stdx509.ParseCertificate(intDER)
	if err != nil {
		t.Fatalf("the short-life intermediate does not parse: %v", err)
	}

	leafTmpl := &stdx509.Certificate{
		SerialNumber:          big.NewInt(0x08a2),
		Subject:               pkix.Name{CommonName: shortLeafCN, Organization: []string{"Harbor CT Bench"}},
		NotBefore:             time.Date(2020, 1, 1, 0, 0, 0, 0, time.UTC),
		NotAfter:              time.Date(2060, 1, 1, 0, 0, 0, 0, time.UTC),
		KeyUsage:              stdx509.KeyUsageDigitalSignature | stdx509.KeyUsageKeyEncipherment,
		ExtKeyUsage:           []stdx509.ExtKeyUsage{stdx509.ExtKeyUsageServerAuth, stdx509.ExtKeyUsageClientAuth},
		BasicConstraintsValid: true,
		DNSNames:              []string{shortLeafCN},
		SignatureAlgorithm:    stdx509.ECDSAWithSHA256,
	}
	leafDER, err := stdx509.CreateCertificate(rand.Reader, leafTmpl, shortInt, &leafKey.PublicKey, intKey)
	if err != nil {
		t.Fatalf("could not mint the short-life leaf: %v", err)
	}
	return [][]byte{leafDER, intDER}
}

func main() {
	t := vhelp.New()
	pool := roots(t)

	valid := chain(t, "chain-valid.pem")
	expired := chain(t, "chain-expired.pem")
	future := chain(t, "chain-notyetvalid.pem")

	// opts builds a CertValidationOpts with every dimension except the clock
	// and rejectExpired held at its permissive default.
	opts := func(now time.Time, rejectExpired bool) ctfe.CertValidationOpts {
		return ctfe.NewCertValidationOpts(pool, now, rejectExpired, false, nil, nil, false, nil)
	}

	accepts := func(id string, now time.Time, rejectExpired bool, raw [][]byte) {
		got, err := ctfe.ValidateChain(raw, opts(now, rejectExpired))
		t.Check(id, err == nil && len(got) == 3,
			"ValidateChain returned %d certificates, err=%v; want 3, <nil>", len(got), err)
	}

	// --- reject_expired off: the clock is irrelevant in all three directions.
	accepts("permissive-current-accepted", atNow, false, valid)
	accepts("permissive-expired-accepted", atNow, false, expired)
	accepts("permissive-notyetvalid-accepted", atNow, false, future)

	// --- reject_expired on.
	accepts("rejectexpired-current-accepted", atNow, true, valid)

	// The one rejection reject_expired is allowed to make. Only the decision is
	// graded, never the error text: the paired accepts() above runs the same
	// chain through the same call with the switch off and is admitted, so the
	// refusal is attributable to the switch and to nothing else.
	got, err := ctfe.ValidateChain(expired, opts(atNow, true))
	t.Check("rejectexpired-expired-rejected",
		err != nil && len(got) == 0,
		"ValidateChain returned %d certificates, err=%v; want 0 and a refusal", len(got), err)

	// The inversion itself: at v1.0.21 this chain was rejected, because
	// rejectExpired also switched on the notBefore check.
	accepts("rejectexpired-notyetvalid-accepted", atNow, true, future)

	// The returned chain is the built path, leaf first, root last.
	pathNames := func(now time.Time, rejectExpired bool, raw [][]byte) string {
		built, err := ctfe.ValidateChain(raw, opts(now, rejectExpired))
		if err != nil {
			return fmt.Sprintf("error: %v", err)
		}
		names := ""
		for i, c := range built {
			if i > 0 {
				names += "|"
			}
			names += c.Subject.CommonName
		}
		return names
	}
	t.Equal("rejectexpired-built-path-subjects", pathNames(atNow, true, valid),
		"valid.bench.invalid|Harbor CT Bench Intermediate|Harbor CT Bench Root")

	// notAfter is not yet in the past at exactly notAfter: `now.After(notAfter)`
	// is false, so the certificate is still admitted.
	accepts("rejectexpired-at-notafter-accepted", atExpiredEdge, true, expired)

	got, err = ctfe.ValidateChain(expired, opts(atExpiredEdge1, true))
	t.Check("rejectexpired-one-second-past-notafter-rejected",
		err != nil && len(got) == 0,
		"ValidateChain returned %d certificates, err=%v; want 0 and a refusal", len(got), err)

	// Forty years before every certificate in the chain exists, reject_expired
	// still admits the chain: time checks stay disabled inside x509 verification.
	accepts("rejectexpired-before-whole-chain-accepted", atPrehistory, true, valid)

	// The not-yet-valid chain is not special-cased: once its own notAfter is in
	// the past, reject_expired refuses it like any other expired certificate.
	got, err = ctfe.ValidateChain(future, opts(atFarFuture, true))
	t.Check("rejectexpired-future-chain-once-expired-rejected",
		err != nil && len(got) == 0,
		"ValidateChain returned %d certificates, err=%v; want 0 and a refusal", len(got), err)

	// Guard against an implementation that admits everything: an unrooted chain
	// is still refused with reject_expired on.
	unrooted := chain(t, "chain-unrooted.pem")
	got, err = ctfe.ValidateChain(unrooted, opts(atNow, true))
	t.Check("rejectexpired-unrooted-still-rejected", err != nil && len(got) == 0,
		"ValidateChain returned %d certificates, err=%v; want 0 and an error", len(got), err)

	// --- The property the name "reject_expired" does not say out loud: from
	// v1.1.0 the test is on the SUBMITTED LEAF and on nothing else. Every case
	// above is decided identically by a loop that expires the whole chain,
	// because every chain above is issued by bench-intermediate, which outlives
	// all of them. Here the intermediate died in 2025 and the leaf lives until
	// 2060, so at 2030 the two readings disagree, and only the leaf-only one
	// admits the chain.
	shortLife := shortLifeChain(t)
	accepts("rejectexpired-expired-intermediate-accepted", atNow, true, shortLife)

	// ...and the expired intermediate really is on the path that came back, so
	// the acceptance above is not some other route to the root: path building
	// traversed it with time checks disabled, exactly as HTTP-API.md 5.2 says.
	t.Equal("rejectexpired-expired-intermediate-path-subjects",
		pathNames(atNow, true, shortLife),
		shortLeafCN+"|"+shortIntCN+"|Harbor CT Bench Root")

	t.Done()
}

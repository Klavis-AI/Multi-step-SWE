// Group admission-reject-unexpired.
//
// `reject_unexpired` does not exist at v1.0.21 in any form: neither the
// configuration field nor the checker branch nor a constructor that could set
// it. At v1.1.0 it is the mirror image of reject_expired -- it admits only
// certificates whose notAfter is already in the past. The two switches
// partition the certificates rather than overriding one another: one refuses
// the expired, the other refuses the unexpired, so with both on nothing is
// admitted and there is no precedence to get right.
//
// The clock is injected through ctfe.NewCertValidationOpts, so nothing here
// depends on the wall clock.
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

var (
	atNow          = time.Date(2030, 1, 1, 0, 0, 0, 0, time.UTC)
	atExpiredEdge  = time.Date(2002, 1, 1, 0, 0, 0, 0, time.UTC)
	atExpiredEdge1 = time.Date(2002, 1, 1, 0, 0, 1, 0, time.UTC)
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
// has thirty years to run, so "is this chain expired?" and "is this LEAF
// expired?" give opposite answers. Every minted chain shares
// bench-intermediate, whose 2000..2100 window strictly contains every leaf's,
// so on those chains the two readings agree everywhere. HTTP-API.md 5.2: from
// v1.1.0 the test is explicit and leaf-only and runs before path building.
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

	pool := ctfe.NewPEMCertPool()
	if !pool.AppendCertsFromPEM(vhelp.ReadFixture("minted", "bench-root.pem")) {
		t.Fatalf("could not load minted/bench-root.pem into a PEMCertPool")
	}

	valid := chain(t, "chain-valid.pem")
	expired := chain(t, "chain-expired.pem")
	future := chain(t, "chain-notyetvalid.pem")

	opts := func(now time.Time, rejExpired, rejUnexpired bool) ctfe.CertValidationOpts {
		return ctfe.NewCertValidationOpts(pool, now, rejExpired, rejUnexpired, nil, nil, false, nil)
	}
	accepts := func(id string, now time.Time, rejExpired, rejUnexpired bool, raw [][]byte) {
		got, err := ctfe.ValidateChain(raw, opts(now, rejExpired, rejUnexpired))
		t.Check(id, err == nil && len(got) == 3,
			"ValidateChain returned %d certificates, err=%v; want 3, <nil>", len(got), err)
	}
	// Only the decision is graded, never the error text. Every rejection below
	// is paired with an accepts() of the same chain through the same call with
	// the switch off, so the refusal is attributable to the switch alone.
	rejects := func(id string, now time.Time, rejExpired, rejUnexpired bool, raw [][]byte) {
		got, err := ctfe.ValidateChain(raw, opts(now, rejExpired, rejUnexpired))
		t.Check(id, err != nil && len(got) == 0,
			"ValidateChain returned %d certificates, err=%v; want 0 and a refusal", len(got), err)
	}

	// Control: with both switches off the same three chains are all admitted,
	// so every rejection below is attributable to the switch and not the chain.
	accepts("permissive-current-accepted", atNow, false, false, valid)

	// reject_unexpired on.
	rejects("rejectunexpired-current-rejected", atNow, false, true, valid)
	rejects("rejectunexpired-notyetvalid-rejected", atNow, false, true, future)
	accepts("rejectunexpired-expired-accepted", atNow, false, true, expired)

	// The boundary is the same instant as reject_expired's, read the other way:
	// at exactly notAfter the certificate has not expired yet, so it is refused.
	rejects("rejectunexpired-at-notafter-rejected", atExpiredEdge, false, true, expired)
	accepts("rejectunexpired-one-second-past-notafter-accepted", atExpiredEdge1, false, true, expired)

	// The admitted chain is still the fully built path, leaf first, root last.
	built, err := ctfe.ValidateChain(expired, opts(atNow, false, true))
	names := ""
	if err == nil {
		for i, c := range built {
			if i > 0 {
				names += "|"
			}
			names += c.Subject.CommonName
		}
	} else {
		names = fmt.Sprintf("error: %v", err)
	}
	t.Equal("rejectunexpired-built-path-subjects", names,
		"expired.bench.invalid|Harbor CT Bench Intermediate|Harbor CT Bench Root")

	// Both switches on admits nothing at all: expired chains fall to
	// reject_expired and every other chain to reject_unexpired. Each of the
	// three is admitted by at least one of the single-switch cases above.
	rejects("both-switches-expired-rejected", atNow, true, true, expired)
	rejects("both-switches-current-rejected", atNow, true, true, valid)
	rejects("both-switches-notyetvalid-rejected", atNow, true, true, future)

	// --- The mirror of the leaf-only property. Every chain above is issued by
	// bench-intermediate, which outlives all of them, so a whole-chain reading
	// of "expired" decides every case above exactly as v1.1.0 does. Here the
	// intermediate expired in 2025 and the leaf runs to 2060: a whole-chain
	// reading calls this chain expired and admits it, but from v1.1.0 only the
	// leaf is consulted, the leaf is unexpired, and the chain is refused.
	shortLife := shortLifeChain(t)
	accepts("permissive-expired-intermediate-accepted", atNow, false, false, shortLife)
	rejects("rejectunexpired-expired-intermediate-rejected", atNow, false, true, shortLife)

	t.Done()
}

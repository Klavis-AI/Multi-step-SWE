// Group x509-verify-disable-switches.
//
// v1.0.17 rebuilds x509.VerifyOptions so that each verification check can be
// switched off individually (commit 41939987, "x509: allow control of Verify
// checks"): DisableCriticalExtensionChecks, DisableNameChecks,
// DisableEKUChecks, DisablePathLenChecks and DisableNameConstraintChecks join
// the DisableTimeChecks that already existed. The CTFE is the customer -- it
// stops mutating parsed certificates in place and sets these switches instead
// -- but the capability itself is x509's, and that is where it is graded.
//
// All six switches of X509-FORK.md section 5.6 are graded here, each as a
// matched pair over the same pinned input: the check rejects, the switch
// admits. A one-sided assertion would be satisfied by an implementation that
// never checks anything.
//
// The name-constraint pair is also the compensating half of the SAN relaxation
// that x509-san-parse-relaxed grades: X509-FORK.md section 3.1 says validation
// does not disappear at v1.0.17, it MOVES to the name-constraint walk inside
// Verify, which parses the name it is about to compare and fails the chain if
// it cannot. A tree that relaxed the parser and forgot to move the check fails
// case 7 here. Only the refusal is asserted, never the error's type or text
// (TEST_INTERFACE.md section 9).
//
// The last three pairs need inputs the shared corpus does not hold -- an
// issuer carrying name constraints, a leaf naming an issuer that is not its
// signer, and a chain two intermediates deep below a pathlen:0 CA -- so the
// driver mints them from the pinned fixture keys. Every field of every
// template is a compile-time constant.
//
// Minting one of those inputs is a precondition, not a graded behaviour, so a
// fork ParseCertificate that will not hand back a certificate for a freshly
// minted input fails only the pair that needed it -- never the whole group.
// The reading of X509-FORK.md section 3.1 under which that happens is charged
// where it belongs, in x509-san-parse-relaxed, and the other five pairs here
// grade switches that have nothing to do with it.
//
// SCOPE-TIME: opts.CurrentTime is a pinned constant, never time.Now(); every
// minted validity window is a pinned constant too.
//
// v1.0.13's VerifyOptions has none of these fields, so this driver does not
// compile there.
package main

import (
	"crypto/ecdsa"
	"crypto/rand"
	stdx509 "crypto/x509"
	"crypto/x509/pkix"
	"encoding/asn1"
	"encoding/pem"
	"fmt"
	"math/big"
	"time"

	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

// pinnedNow sits inside every minted leaf's validity window except the
// deliberately expired and deliberately not-yet-valid ones.
var pinnedNow = time.Date(2030, 1, 1, 0, 0, 0, 0, time.UTC)

// Pinned inputs for the certificates this driver mints.
var (
	mintNotBefore = time.Date(2020, 1, 1, 0, 0, 0, 0, time.UTC)
	mintNotAfter  = time.Date(2060, 1, 1, 0, 0, 0, 0, time.UTC)
	oidSAN        = asn1.ObjectIdentifier{2, 5, 29, 17}
)

// badDNSName has a trailing empty label, so it is not a parsable domain: from
// v1.0.17 ParseCertificate stores it verbatim and the name-constraint walk is
// where it is finally rejected.
const badDNSName = "bench.invalid."

func mustParse(t *vhelp.T, rel ...string) *x509.Certificate {
	cert, err := x509.ParseCertificate(vhelp.ReadFixture(rel...))
	if err != nil {
		t.Fatalf("parse %v: %v", rel, err)
	}
	return cert
}

// fixtureKey loads one of the pinned PKCS#8 keys from $CT_FIXTURES.
func fixtureKey(t *vhelp.T, name string) *ecdsa.PrivateKey {
	blk, _ := pem.Decode(vhelp.ReadFixture("minted", "keys", name))
	if blk == nil {
		t.Fatalf("minted/keys/%s is not PEM", name)
	}
	anyKey, err := stdx509.ParsePKCS8PrivateKey(blk.Bytes)
	if err != nil {
		t.Fatalf("parse minted/keys/%s: %v", name, err)
	}
	key, ok := anyKey.(*ecdsa.PrivateKey)
	if !ok {
		t.Fatalf("minted/keys/%s is %T, want *ecdsa.PrivateKey", name, anyKey)
	}
	return key
}

// mint signs tmpl under parent with parentKey and returns the result parsed
// both by the fork (what is graded) and by the standard library (so it can in
// turn be handed to CreateCertificate as a parent, with its RawSubject exact).
//
// A fork parse that returns an error is NOT a group abort. Under the reading
// of X509-FORK.md section 3.1 in which the SAN relaxation demotes a malformed
// name to a NonFatalErrors entry, ParseCertificate returns the certificate AND
// a non-nil error, so the certificate is used and this group is unaffected --
// that reading is graded, and charged, by x509-san-parse-relaxed alone. Only a
// parse that returns no certificate at all leaves a pair without its input,
// and mint reports that as an error the caller turns into a failure of that
// pair's own two cases (see unavailable). Failures of the standard library's
// own CreateCertificate and ParseCertificate do not depend on the candidate at
// all, so those stay aborts.
func mint(t *vhelp.T, tmpl, parent *stdx509.Certificate, pub *ecdsa.PublicKey, parentKey *ecdsa.PrivateKey) (*x509.Certificate, *stdx509.Certificate, error) {
	der, err := stdx509.CreateCertificate(rand.Reader, tmpl, parent, pub, parentKey)
	if err != nil {
		t.Fatalf("mint %q: %v", tmpl.Subject.CommonName, err)
	}
	std, err := stdx509.ParseCertificate(der)
	if err != nil {
		t.Fatalf("std parse of minted %q: %v", tmpl.Subject.CommonName, err)
	}
	forked, ferr := x509.ParseCertificate(der)
	if forked == nil {
		return nil, std, fmt.Errorf("x509.ParseCertificate returned no certificate for freshly minted %q: %v", tmpl.Subject.CommonName, ferr)
	}
	return forked, std, nil
}

// sanExtension hand-encodes GeneralNames{ dNSName } so that the standard
// library's own SAN marshaller never gets a chance to sanitise the value.
func sanExtension(dns string) []byte {
	body := append([]byte{0x82, byte(len(dns))}, dns...)
	return append([]byte{0x30, byte(len(body))}, body...)
}

func caTemplate(serial int64, cn string, keyID []byte) *stdx509.Certificate {
	return &stdx509.Certificate{
		SerialNumber:          big.NewInt(serial),
		Subject:               pkix.Name{CommonName: cn},
		NotBefore:             mintNotBefore,
		NotAfter:              mintNotAfter,
		KeyUsage:              stdx509.KeyUsageCertSign,
		BasicConstraintsValid: true,
		IsCA:                  true,
		SubjectKeyId:          keyID,
		SignatureAlgorithm:    stdx509.ECDSAWithSHA256,
	}
}

func leafTemplate(serial int64, cn string) *stdx509.Certificate {
	return &stdx509.Certificate{
		SerialNumber:          big.NewInt(serial),
		Subject:               pkix.Name{CommonName: cn},
		NotBefore:             mintNotBefore,
		NotAfter:              mintNotAfter,
		KeyUsage:              stdx509.KeyUsageDigitalSignature,
		BasicConstraintsValid: true,
		SignatureAlgorithm:    stdx509.ECDSAWithSHA256,
	}
}

// admits records the positive half of a matched pair: with the switch on, the
// same input verifies and the chain has the expected length.
func admits(t *vhelp.T, id string, chains [][]*x509.Certificate, err error, wantLen int) {
	switch {
	case err != nil:
		t.Check(id, false, "Verify: %v", err)
	case len(chains) != 1:
		t.Check(id, false, "got %d chains, want 1", len(chains))
	default:
		t.Equal(id, len(chains[0]), wantLen)
	}
}

// refuses records the negative half: with the switch off, Verify must return
// an error and no chain. The error's identity and wording are not graded.
func refuses(t *vhelp.T, id string, chains [][]*x509.Certificate, err error, what string) {
	if err == nil || len(chains) != 0 {
		t.Check(id, false, "Verify accepted %s (chains=%d, err=%v)", what, len(chains), err)
		return
	}
	t.Check(id, true, "")
}

// firstErr returns the first non-nil error of its arguments.
func firstErr(errs ...error) error {
	for _, err := range errs {
		if err != nil {
			return err
		}
	}
	return nil
}

// unavailable fails the named cases because the input they grade against could
// not be prepared, naming the real cause. Every case of the group is still
// reported, so the executed count still equals COUNT and the report says what
// happened instead of leaving test.sh to report an arithmetic mismatch.
func unavailable(t *vhelp.T, err error, ids ...string) {
	for _, id := range ids {
		t.Check(id, false, "input could not be prepared: %v", err)
	}
}

func main() {
	t := vhelp.New()

	root := mustParse(t, "minted", "bench-root.der")
	inter := mustParse(t, "minted", "bench-intermediate.der")
	precert := mustParse(t, "minted", "precert-poisoned.der")
	clientOnly := mustParse(t, "minted", "leaf-eku-clientauth.der")
	expired := mustParse(t, "minted", "leaf-expired.der")

	roots := x509.NewCertPool()
	roots.AddCert(root)
	inters := x509.NewCertPool()
	inters.AddCert(inter)

	base := func() x509.VerifyOptions {
		return x509.VerifyOptions{
			Roots:         roots,
			Intermediates: inters,
			CurrentTime:   pinnedNow,
			KeyUsages:     []x509.ExtKeyUsage{x509.ExtKeyUsageAny},
		}
	}

	// --- 1 and 2. the critical poison extension ---------------------------
	// A precertificate carries a critical extension at 1.3.6.1.4.1.11129.2.4.3
	// that this x509 does not handle, so it lands in
	// UnhandledCriticalExtensions and the default verification refuses it.
	opts := base()
	if _, err := precert.Verify(opts); err == nil {
		t.Check("critical-extension-check-refuses-poisoned-precert", false, "Verify accepted a certificate with an unhandled critical extension")
	} else {
		t.Check("critical-extension-check-refuses-poisoned-precert", true, "")
	}

	opts = base()
	opts.DisableCriticalExtensionChecks = true
	chains, err := precert.Verify(opts)
	admits(t, "disable-critical-extension-checks-admits-poisoned-precert", chains, err, 3)

	// --- 3 and 4. extended key usage --------------------------------------
	// leaf-eku-clientauth carries ClientAuth and nothing else; asking for
	// ServerAuth must be refused while the EKU check is on.
	opts = base()
	opts.KeyUsages = []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth}
	if _, err := clientOnly.Verify(opts); err == nil {
		t.Check("eku-check-refuses-clientauth-only-leaf", false, "Verify accepted a ClientAuth-only leaf for ServerAuth")
	} else {
		t.Check("eku-check-refuses-clientauth-only-leaf", true, "")
	}

	opts = base()
	opts.KeyUsages = []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth}
	opts.DisableEKUChecks = true
	chains, err = clientOnly.Verify(opts)
	admits(t, "disable-eku-checks-admits-clientauth-only-leaf", chains, err, 3)

	// --- 5 and 6. validity window, against the pinned clock ----------------
	opts = base()
	if _, err := expired.Verify(opts); err == nil {
		t.Check("time-check-refuses-expired-leaf", false, "Verify accepted a leaf that expired in 2002 at the pinned 2030 clock")
	} else {
		t.Check("time-check-refuses-expired-leaf", true, "")
	}

	opts = base()
	opts.DisableTimeChecks = true
	chains, err = expired.Verify(opts)
	admits(t, "disable-time-checks-admits-expired-leaf", chains, err, 3)

	rootKey := fixtureKey(t, "root-key.pem")
	interKey := fixtureKey(t, "intermediate-key.pem")
	rogueKey := fixtureKey(t, "rogue-root-key.pem")
	leafKey := fixtureKey(t, "leaf-key.pem")

	// --- 7 and 8. name constraints, and the SAN relaxation's other half ----
	// A root that permits only names under bench.invalid, and a leaf whose
	// single dNSName is "bench.invalid." -- inside the permitted subtree if
	// you read it loosely, but not a parsable domain, so the walk of
	// X509-FORK.md section 3.1 cannot compare it and must fail the chain.
	ncRootTmpl := caTemplate(0x5c01, "Harbor CT Bench Name-Constrained Root", []byte{0x5c, 0x01})
	ncRootTmpl.PermittedDNSDomains = []string{"bench.invalid"}
	ncRoot, ncRootStd, ncRootErr := mint(t, ncRootTmpl, ncRootTmpl, &rootKey.PublicKey, rootKey)

	badSANTmpl := leafTemplate(0x5c02, "Harbor CT Bench Malformed SAN Leaf")
	badSANTmpl.ExtraExtensions = []pkix.Extension{{Id: oidSAN, Value: sanExtension(badDNSName)}}
	badSANLeaf, _, badSANErr := mint(t, badSANTmpl, ncRootStd, &leafKey.PublicKey, rootKey)

	if perr := firstErr(ncRootErr, badSANErr); perr != nil {
		unavailable(t, perr,
			"name-constraint-check-refuses-malformed-dnsname",
			"disable-name-constraint-checks-admits-malformed-dnsname")
	} else {
		ncRoots := x509.NewCertPool()
		ncRoots.AddCert(ncRoot)
		ncBase := func() x509.VerifyOptions {
			return x509.VerifyOptions{
				Roots:       ncRoots,
				CurrentTime: pinnedNow,
				KeyUsages:   []x509.ExtKeyUsage{x509.ExtKeyUsageAny},
			}
		}

		opts = ncBase()
		chains, err = badSANLeaf.Verify(opts)
		refuses(t, "name-constraint-check-refuses-malformed-dnsname", chains, err,
			"a leaf whose dNSName is not a parsable domain, against a name-constrained issuer")

		opts = ncBase()
		opts.DisableNameConstraintChecks = true
		chains, err = badSANLeaf.Verify(opts)
		admits(t, "disable-name-constraint-checks-admits-malformed-dnsname", chains, err, 2)
	}

	// --- 9 and 10. issuer/subject chaining ---------------------------------
	// The leaf is signed by nmRoot and points its authorityKeyIdentifier at
	// nmRoot, so chain building finds it -- but its issuer name is a subject
	// no certificate in the pool has. With the check on that is a NameMismatch;
	// with it off the chain is built on the signature alone.
	nmRootTmpl := caTemplate(0x5c11, "Harbor CT Bench Name-Check Root", []byte{0x5c, 0x11})
	nmRoot, _, nmRootErr := mint(t, nmRootTmpl, nmRootTmpl, &rootKey.PublicKey, rootKey)

	decoyTmpl := caTemplate(0x5c12, "Harbor CT Bench Issuer That Does Not Exist", []byte{0x5c, 0x11})
	wrongIssuerLeaf, _, wrongIssuerErr := mint(t, leafTemplate(0x5c13, "Harbor CT Bench Wrong-Issuer Leaf"),
		decoyTmpl, &leafKey.PublicKey, rootKey)

	if perr := firstErr(nmRootErr, wrongIssuerErr); perr != nil {
		unavailable(t, perr,
			"name-check-refuses-issuer-subject-mismatch",
			"disable-name-checks-admits-issuer-subject-mismatch")
	} else {
		nmRoots := x509.NewCertPool()
		nmRoots.AddCert(nmRoot)
		nmBase := func() x509.VerifyOptions {
			return x509.VerifyOptions{
				Roots:       nmRoots,
				CurrentTime: pinnedNow,
				KeyUsages:   []x509.ExtKeyUsage{x509.ExtKeyUsageAny},
			}
		}

		opts = nmBase()
		chains, err = wrongIssuerLeaf.Verify(opts)
		refuses(t, "name-check-refuses-issuer-subject-mismatch", chains, err,
			"a leaf whose issuer name is not its signer's subject name")

		opts = nmBase()
		opts.DisableNameChecks = true
		chains, err = wrongIssuerLeaf.Verify(opts)
		admits(t, "disable-name-checks-admits-issuer-subject-mismatch", chains, err, 2)
	}

	// --- 11 and 12. basicConstraints path length ---------------------------
	// root -> plRootCA (pathlen:0) -> plSubCA -> leaf. One intermediate sits
	// below a CA that permits none, so the chain is one too deep.
	plRootTmpl := caTemplate(0x5c21, "Harbor CT Bench PathLen Root", []byte{0x5c, 0x21})
	plRoot, plRootStd, plRootErr := mint(t, plRootTmpl, plRootTmpl, &rootKey.PublicKey, rootKey)

	plCA0Tmpl := caTemplate(0x5c22, "Harbor CT Bench PathLen-Zero CA", []byte{0x5c, 0x22})
	plCA0Tmpl.MaxPathLen = 0
	plCA0Tmpl.MaxPathLenZero = true
	plCA0, plCA0Std, plCA0Err := mint(t, plCA0Tmpl, plRootStd, &interKey.PublicKey, rootKey)

	plSubTmpl := caTemplate(0x5c23, "Harbor CT Bench PathLen Sub CA", []byte{0x5c, 0x23})
	plSub, plSubStd, plSubErr := mint(t, plSubTmpl, plCA0Std, &rogueKey.PublicKey, interKey)

	plLeaf, _, plLeafErr := mint(t, leafTemplate(0x5c24, "Harbor CT Bench PathLen Leaf"),
		plSubStd, &leafKey.PublicKey, rogueKey)

	if perr := firstErr(plRootErr, plCA0Err, plSubErr, plLeafErr); perr != nil {
		unavailable(t, perr,
			"path-len-check-refuses-chain-below-pathlen-zero-ca",
			"disable-path-len-checks-admits-chain-below-pathlen-zero-ca")
	} else {
		plRoots := x509.NewCertPool()
		plRoots.AddCert(plRoot)
		plInters := x509.NewCertPool()
		plInters.AddCert(plCA0)
		plInters.AddCert(plSub)
		plBase := func() x509.VerifyOptions {
			return x509.VerifyOptions{
				Roots:         plRoots,
				Intermediates: plInters,
				CurrentTime:   pinnedNow,
				KeyUsages:     []x509.ExtKeyUsage{x509.ExtKeyUsageAny},
			}
		}

		opts = plBase()
		chains, err = plLeaf.Verify(opts)
		refuses(t, "path-len-check-refuses-chain-below-pathlen-zero-ca", chains, err,
			"a chain carrying an intermediate below a pathlen:0 CA")

		opts = plBase()
		opts.DisablePathLenChecks = true
		chains, err = plLeaf.Verify(opts)
		admits(t, "disable-path-len-checks-admits-chain-below-pathlen-zero-ca", chains, err, 4)
	}

	t.Done()
}

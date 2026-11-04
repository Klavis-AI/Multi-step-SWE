// Group inherited-x509-chain-verification.
//
// Chain building over the forked x509 is the obligation every CT personality
// rests on: get-roots publishes a pool, add-chain accepts a submission only
// because a chain to that pool can be built, and a precertificate is
// recognised only because its poison extension survives parsing. Step 07
// reaches deep into this package -- three parse conditions are demoted to
// non-fatal, a whole new curve arrives, Subject Information Access and the
// RFC 3779 blocks start being decoded -- so the ordinary path through it is
// exactly what must not move underneath.
//
// It is an inherited-obligation group: it is supposed to pass against the
// previous milestone too, and it does.
//
// Everything here is pinned. The fixtures are the minted corpus; the instant
// verification happens at is the constant below, never the wall clock
// (SCOPE-TIME); no socket is opened (SCOPE-NET). Diagnostic strings are never
// asserted -- only whether a verification succeeded, and for the successful
// one, the exact chain it produced.
package main

import (
	"encoding/pem"
	"fmt"
	"strings"
	"time"

	"ctverify/vhelp"

	"github.com/google/certificate-transparency-go/x509"
)

// The instant every validity decision below is taken at. Inside
// bench-root's 2000..2100 window, inside leaf-valid's 2020..2060 window,
// after leaf-expired's 2001..2002 window and before leaf-notyetvalid's
// 2090..2091 window.
var pinnedInstant = time.Date(2030, 6, 1, 0, 0, 0, 0, time.UTC)

// The CT precertificate poison extension, RFC 6962 section 3.1.
const poisonOID = "1.3.6.1.4.1.11129.2.4.3"

// certsFromPEM parses every certificate in a PEM fixture. A fixture that
// yields none, or that yields one that will not parse, stops the group: a
// driver that finds no input must fail, never skip.
func certsFromPEM(t *vhelp.T, name string) []*x509.Certificate {
	rest := vhelp.ReadFixture("minted", name)
	var out []*x509.Certificate
	for {
		blk, remainder := pem.Decode(rest)
		if blk == nil {
			break
		}
		rest = remainder
		if blk.Type != "CERTIFICATE" {
			continue
		}
		cert, err := x509.ParseCertificate(blk.Bytes)
		if cert == nil {
			t.Fatalf("minted/%s: a block would not parse: %v", name, err)
		}
		out = append(out, cert)
	}
	if len(out) == 0 {
		t.Fatalf("minted/%s: no certificates in fixture", name)
	}
	return out
}

func firstCert(t *vhelp.T, name string) *x509.Certificate {
	return certsFromPEM(t, name)[0]
}

// describe renders the fields of a certificate this group pins.
func describe(c *x509.Certificate) string {
	return fmt.Sprintf("cn=%q issuer=%q serial=%v ca=%v pathLen=%d pathLenZero=%v dns=%v eku=%v keyUsage=%d",
		c.Subject.CommonName, c.Issuer.CommonName, c.SerialNumber, c.IsCA,
		c.MaxPathLen, c.MaxPathLenZero, c.DNSNames, c.ExtKeyUsage, c.KeyUsage)
}

// verify runs one chain build at the pinned instant and reports only whether
// it succeeded; the library's diagnostic text is deliberately not graded.
func verify(leaf *x509.Certificate, roots, intermediates *x509.CertPool) (bool, [][]*x509.Certificate) {
	chains, err := leaf.Verify(x509.VerifyOptions{
		Roots:         roots,
		Intermediates: intermediates,
		CurrentTime:   pinnedInstant,
		KeyUsages:     []x509.ExtKeyUsage{x509.ExtKeyUsageAny},
	})
	return err == nil, chains
}

func poolOf(t *vhelp.T, name string) *x509.CertPool {
	p := x509.NewCertPool()
	if !p.AppendCertsFromPEM(vhelp.ReadFixture("minted", name)) {
		t.Fatalf("minted/%s: no certificate accepted into the pool", name)
	}
	return p
}

func main() {
	t := vhelp.New()

	chain := certsFromPEM(t, "chain-valid.pem")
	if len(chain) != 2 {
		t.Fatalf("minted/chain-valid.pem: got %d certificates, want 2", len(chain))
	}
	leaf, intermediate := chain[0], chain[1]
	root := firstCert(t, "bench-root.pem")

	benchRoots := poolOf(t, "roots-bench.pem")
	interPool := x509.NewCertPool()
	interPool.AddCert(intermediate)
	emptyPool := x509.NewCertPool()

	// --- 1. the chain fixture decodes to the two certificates it names -----
	t.Equal("chain-valid-decodes-to-its-two-pinned-certificates",
		describe(leaf)+"\n"+describe(intermediate),
		`cn="valid.bench.invalid" issuer="Harbor CT Bench Intermediate" serial=4097 ca=false pathLen=-1 pathLenZero=false dns=[valid.bench.invalid] eku=[1 2] keyUsage=5`+"\n"+
			`cn="Harbor CT Bench Intermediate" issuer="Harbor CT Bench Root" serial=2 ca=true pathLen=0 pathLenZero=true dns=[] eku=[] keyUsage=97`)

	// --- 2. the trust anchor is the pinned self-signed CA ------------------
	t.Equal("bench-root-is-the-pinned-self-signed-ca",
		fmt.Sprintf("cn=%q serial=%v ca=%v from=%s to=%s selfSigned=%v",
			root.Subject.CommonName, root.SerialNumber, root.IsCA,
			root.NotBefore.UTC().Format(time.RFC3339), root.NotAfter.UTC().Format(time.RFC3339),
			root.CheckSignatureFrom(root) == nil),
		`cn="Harbor CT Bench Root" serial=1 ca=true from=2000-01-01T00:00:00Z to=2100-01-01T00:00:00Z selfSigned=true`)

	// --- 3. the ordinary path: leaf -> intermediate -> root ----------------
	ok, chains := verify(leaf, benchRoots, interPool)
	var names []string
	if len(chains) == 1 {
		for _, c := range chains[0] {
			names = append(names, c.Subject.CommonName)
		}
	}
	t.Equal("leaf-verifies-through-the-intermediate-to-the-bench-root",
		fmt.Sprintf("ok=%v chains=%d path=%s", ok, len(chains), strings.Join(names, " -> ")),
		"ok=true chains=1 path=valid.bench.invalid -> Harbor CT Bench Intermediate -> Harbor CT Bench Root")

	// --- 4. control: the intermediate is load-bearing -----------------------
	// Without it the same leaf against the same roots must not verify, so
	// case 3 is not simply "Verify always succeeds".
	withoutInter, _ := verify(leaf, benchRoots, emptyPool)
	noRoots, _ := verify(leaf, emptyPool, interPool)
	t.Equal("the-chain-needs-both-the-intermediate-and-the-root",
		fmt.Sprintf("withIntermediate=%v withoutIntermediate=%v withoutRoots=%v", ok, withoutInter, noRoots),
		"withIntermediate=true withoutIntermediate=false withoutRoots=false")

	// --- 5. the validity window is enforced at the pinned instant ----------
	expired, _ := verify(firstCert(t, "leaf-expired.pem"), benchRoots, interPool)
	future, _ := verify(firstCert(t, "leaf-notyetvalid.pem"), benchRoots, interPool)
	t.Equal("validity-window-is-enforced-at-the-pinned-instant",
		fmt.Sprintf("expired=%v notYetValid=%v inWindow=%v", expired, future, ok),
		"expired=false notYetValid=false inWindow=true")

	// --- 6. a leaf under an untrusted root is refused ----------------------
	// The same leaf verifies once its actual issuer is trusted, so the
	// refusal is about the trust anchor and not about the certificate.
	unrooted := firstCert(t, "leaf-unrooted.pem")
	roguePool := x509.NewCertPool()
	roguePool.AddCert(firstCert(t, "rogue-root.pem"))
	againstBench, _ := verify(unrooted, benchRoots, interPool)
	againstRogue, _ := verify(unrooted, roguePool, emptyPool)
	t.Equal("a-leaf-under-an-untrusted-root-is-refused",
		fmt.Sprintf("benchPool=%v roguePool=%v", againstBench, againstRogue),
		"benchPool=false roguePool=true")

	// --- 7. issuer signatures follow the chain -----------------------------
	t.Equal("issuer-signature-checks-follow-the-chain",
		fmt.Sprintf("leafFromIntermediate=%v leafFromRoot=%v intermediateFromRoot=%v",
			leaf.CheckSignatureFrom(intermediate) == nil,
			leaf.CheckSignatureFrom(root) == nil,
			intermediate.CheckSignatureFrom(root) == nil),
		"leafFromIntermediate=true leafFromRoot=false intermediateFromRoot=true")

	// --- 8. the precertificate poison extension survives parsing -----------
	// Critical, ASN.1 NULL, and reported as unhandled -- against a fixture of
	// identical construction and identical serial that simply lacks it.
	poisonOf := func(name string) string {
		c := firstCert(t, name)
		state := "absent"
		for _, ext := range c.Extensions {
			if ext.Id.String() == poisonOID {
				state = fmt.Sprintf("critical=%v value=%x", ext.Critical, ext.Value)
			}
		}
		unhandled := make([]string, 0, len(c.UnhandledCriticalExtensions))
		for _, oid := range c.UnhandledCriticalExtensions {
			unhandled = append(unhandled, oid.String())
		}
		return fmt.Sprintf("%s serial=%v poison=%s unhandled=%v", name, c.SerialNumber, state, unhandled)
	}
	t.Equal("precert-poison-extension-is-critical-null-and-unhandled",
		poisonOf("precert-poisoned.pem")+"\n"+poisonOf("precert-nopoison.pem"),
		"precert-poisoned.pem serial=8193 poison=critical=true value=0500 unhandled=[1.3.6.1.4.1.11129.2.4.3]\n"+
			"precert-nopoison.pem serial=8193 poison=absent unhandled=[]")

	t.Done()
}

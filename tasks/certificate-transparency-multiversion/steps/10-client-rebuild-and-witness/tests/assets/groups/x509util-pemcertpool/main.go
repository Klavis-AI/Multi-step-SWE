// Group x509util-pemcertpool.
//
// v1.1.3 moves PEMCertPool out of trillian/ctfe and into x509util
// (`{trillian/ctfe => x509util}/pem_cert_pool.go`). This group observes the
// type at its new home through its exported surface only: construction,
// de-duplication by SHA-256 fingerprint, strict all-or-nothing PEM loading,
// the raw-certificate accessor get-roots needs, and the wrapped
// x509.CertPool.
//
// At v1.1.2 the identifier x509util.PEMCertPool does not exist, so this
// driver does not compile and the group scores zero.
package main

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"encoding/pem"
	"fmt"
	"os"
	"path/filepath"
	"time"

	"github.com/google/certificate-transparency-go/x509"
	"github.com/google/certificate-transparency-go/x509util"

	"ctverify/vhelp"
)

func derOf(t *vhelp.T, pemBytes []byte) []byte {
	block, _ := pem.Decode(pemBytes)
	if block == nil {
		t.Fatalf("fixture is not PEM")
	}
	return block.Bytes
}

func main() {
	t := vhelp.New()

	rootPEM := vhelp.ReadFixture("minted", "bench-root.pem")
	intPEM := vhelp.ReadFixture("minted", "bench-intermediate.pem")
	roguePEM := vhelp.ReadFixture("minted", "rogue-root.pem")
	bothPEM := vhelp.ReadFixture("minted", "roots-bench-plus-int.pem")

	rootDER := derOf(t, rootPEM)
	intDER := derOf(t, intPEM)
	rogueDER := derOf(t, roguePEM)

	rootCert, err := x509.ParseCertificate(rootDER)
	if x509.IsFatal(err) {
		t.Fatalf("parse bench-root: %v", err)
	}
	intCert, err := x509.ParseCertificate(intDER)
	if x509.IsFatal(err) {
		t.Fatalf("parse bench-intermediate: %v", err)
	}
	rogueCert, err := x509.ParseCertificate(rogueDER)
	if x509.IsFatal(err) {
		t.Fatalf("parse rogue-root: %v", err)
	}

	// 1. A fresh pool holds nothing.
	empty := x509util.NewPEMCertPool()
	t.Check("new-pool-is-empty", len(empty.RawCertificates()) == 0 && len(empty.Subjects()) == 0,
		"raw=%d subjects=%d", len(empty.RawCertificates()), len(empty.Subjects()))

	// 2. AppendCertsFromPEM reports true and loads both certificates of the
	//    two-certificate trust pool, in file order.
	p := x509util.NewPEMCertPool()
	ok := p.AppendCertsFromPEM(bothPEM)
	raw := p.RawCertificates()
	t.Check("append-two-cert-pem", ok && len(raw) == 2, "ok=%v n=%d", ok, len(raw))

	// 3. The raw certificates are the exact DER the fixtures hold, in order.
	gotOrder := ""
	if len(raw) == 2 {
		gotOrder = fmt.Sprintf("%x|%x", sha256.Sum256(raw[0].Raw), sha256.Sum256(raw[1].Raw))
	}
	wantOrder := fmt.Sprintf("%x|%x", sha256.Sum256(rootDER), sha256.Sum256(intDER))
	t.Equal("raw-certificates-are-exact-der", gotOrder, wantOrder)

	// 4. Included is fingerprint-based membership, not subject matching.
	t.Check("included-true-for-member", p.Included(rootCert) && p.Included(intCert),
		"root=%v int=%v", p.Included(rootCert), p.Included(intCert))
	t.Check("included-false-for-nonmember", !p.Included(rogueCert), "rogue-root reported as included")

	// 5. AddCert de-duplicates on the SHA-256 of the raw DER.
	p.AddCert(rootCert)
	p.AddCert(rootCert)
	t.Equal("addcert-deduplicates", len(p.RawCertificates()), 2)

	// 6. Adding a genuinely new certificate does grow the pool.
	p.AddCert(rogueCert)
	t.Equal("addcert-appends-new", len(p.RawCertificates()), 3)

	// 7. Subjects() exposes the DER-encoded subjects of everything in the pool.
	subjects := p.Subjects()
	wantSubjects := map[string]bool{
		hex.EncodeToString(rootCert.RawSubject):  false,
		hex.EncodeToString(intCert.RawSubject):   false,
		hex.EncodeToString(rogueCert.RawSubject): false,
	}
	for _, s := range subjects {
		wantSubjects[hex.EncodeToString(s)] = true
	}
	allSeen := len(subjects) == 3
	for _, seen := range wantSubjects {
		if !seen {
			allSeen = false
		}
	}
	t.Check("subjects-cover-the-pool", allSeen, "got %d subjects", len(subjects))

	// 8. CertPool() returns a usable x509.CertPool: the minted leaf chains to
	//    bench-root through bench-intermediate, and to nothing else.
	leafDER := derOf(t, vhelp.ReadFixture("minted", "leaf-valid.pem"))
	leaf, err := x509.ParseCertificate(leafDER)
	if x509.IsFatal(err) {
		t.Fatalf("parse leaf-valid: %v", err)
	}
	trust := x509util.NewPEMCertPool()
	if !trust.AppendCertsFromPEM(rootPEM) {
		t.Fatalf("failed to load bench-root into a pool")
	}
	inter := x509util.NewPEMCertPool()
	if !inter.AppendCertsFromPEM(intPEM) {
		t.Fatalf("failed to load bench-intermediate into a pool")
	}
	chains, verr := leaf.Verify(x509.VerifyOptions{
		Roots:         trust.CertPool(),
		Intermediates: inter.CertPool(),
		CurrentTime:   pinnedTime(),
		KeyUsages:     []x509.ExtKeyUsage{x509.ExtKeyUsageAny},
	})
	t.Check("certpool-verifies-minted-leaf", verr == nil && len(chains) == 1 && len(chains[0]) == 3,
		"err=%v chains=%d", verr, len(chains))

	// 9. Strictness: a PEM blob whose second block is a corrupt certificate is
	//    rejected outright, unlike crypto/x509's CertPool.
	corrupt := append([]byte{}, rootPEM...)
	corrupt = append(corrupt, mangledPEM(intDER)...)
	strict := x509util.NewPEMCertPool()
	t.Check("append-rejects-corrupt-block", !strict.AppendCertsFromPEM(corrupt),
		"a corrupt certificate block was accepted")

	// 10. A PEM blob with no CERTIFICATE block at all returns false.
	noCerts := x509util.NewPEMCertPool()
	keyPEM := vhelp.ReadFixture("minted", "keys", "root-key.pem")
	t.Check("append-without-certificates-is-false", !noCerts.AppendCertsFromPEM(keyPEM),
		"a key-only PEM was reported as containing certificates")

	// 11. AppendCertsFromPEMFile reads the same bytes off disk.
	fromFile := x509util.NewPEMCertPool()
	if err := fromFile.AppendCertsFromPEMFile(vhelp.Fixture("minted", "roots-bench-plus-int.pem")); err != nil {
		t.Check("append-from-file", false, "AppendCertsFromPEMFile: %v", err)
	} else {
		t.Equal("append-from-file", len(fromFile.RawCertificates()), 2)
	}

	// 12. A missing file is an error, and the pool is untouched.
	missing := x509util.NewPEMCertPool()
	err = missing.AppendCertsFromPEMFile(filepath.Join(os.Getenv("CT_SCRATCH"), "no-such-file.pem"))
	t.Check("append-from-missing-file-errors", err != nil && len(missing.RawCertificates()) == 0,
		"err=%v n=%d", err, len(missing.RawCertificates()))

	t.Done()
}

// pinnedTime is the injected clock for every validity check in this group.
// 2030-01-01T00:00:00Z sits inside the minted leaf's 2020..2060 window and
// inside bench-root's 2000..2100 window. No wall clock is consulted.
func pinnedTime() time.Time {
	return time.Date(2030, 1, 1, 0, 0, 0, 0, time.UTC)
}

// mangledPEM re-encodes DER whose first content byte has been flipped, which
// makes the certificate unparseable rather than merely untrusted.
func mangledPEM(der []byte) []byte {
	bad := append([]byte{}, der...)
	bad[4] ^= 0xff
	var buf bytes.Buffer
	_ = pem.Encode(&buf, &pem.Block{Type: "CERTIFICATE", Bytes: bad})
	return buf.Bytes()
}

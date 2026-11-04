// Group crlcheck-binary.
//
// v1.0.5 adds the crlcheck command, the counterpart of certcheck for
// certificate revocation lists, together with the x509 fork's CRL parser and
// the CertificateList renderer it prints with. It is graded as a process:
// exit status, stdout and stderr.
//
// What is graded: the exit status, the flag set of CLI.md section 4.3 at this
// release, and the content of the -verbose description, which that section
// fixes to stdout. stderr's wording is never read.
//
// The CRL under test is minted by this driver from pinned corpus material --
// the bench-root certificate and its key -- with a pinned thisUpdate,
// nextUpdate, revoked serial and revocation date, using only the Go standard
// library, so nothing about it depends on the candidate's code or on the
// clock. ECDSA signing is randomised, so no assertion here looks at the
// signature bytes; the assertions are over the parsed content and over whether
// the signature verifies against a given CA.
package main

import (
	"bytes"
	"crypto/rand"
	stdx509 "crypto/x509"
	"crypto/x509/pkix"
	"encoding/hex"
	"encoding/pem"
	"math/big"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"

	"ctverify/vhelp"
)

type result struct {
	rc     int
	stdout string
	stderr string
}

func run(t *vhelp.T, args ...string) result {
	cmd := vhelp.Command(vhelp.CandidateBin("crlcheck"), args...)
	var out, errb bytes.Buffer
	cmd.Stdout = &out
	cmd.Stderr = &errb
	rc := 0
	if err := cmd.Run(); err != nil {
		ee, ok := err.(*exec.ExitError)
		if !ok {
			t.Fatalf("crlcheck %v: %v", args, err)
		}
		rc = ee.ExitCode()
	}
	return result{rc: rc, stdout: out.String(), stderr: errb.String()}
}

// mintCRL builds the pinned CRL and writes it in both encodings, returning the
// DER path, the PEM path and the issuer's subject key id (which Go copies into
// the CRL's authority key identifier, so it is what a renderer must show).
func mintCRL(t *vhelp.T) (string, string, []byte) {
	root, err := stdx509.ParseCertificate(vhelp.ReadFixture("minted", "bench-root.der"))
	if err != nil {
		t.Fatalf("parse bench-root.der: %v", err)
	}
	blk, _ := pem.Decode(vhelp.ReadFixture("minted", "keys", "root-key.pem"))
	if blk == nil {
		t.Fatalf("minted/keys/root-key.pem holds no PEM block")
	}
	key, err := stdx509.ParsePKCS8PrivateKey(blk.Bytes)
	if err != nil {
		t.Fatalf("parse root-key.pem: %v", err)
	}
	der, err := root.CreateCRL(rand.Reader, key,
		[]pkix.RevokedCertificate{{
			SerialNumber:   big.NewInt(0x1002), // the serial of minted/leaf-expired
			RevocationTime: time.Date(2021, 6, 1, 0, 0, 0, 0, time.UTC),
		}},
		time.Date(2021, 1, 1, 0, 0, 0, 0, time.UTC),
		time.Date(2031, 1, 1, 0, 0, 0, 0, time.UTC))
	if err != nil {
		t.Fatalf("CreateCRL: %v", err)
	}
	derPath := filepath.Join(os.Getenv("CT_SCRATCH"), "bench-root.crl")
	pemPath := filepath.Join(os.Getenv("CT_SCRATCH"), "bench-root-crl.pem")
	if err := os.WriteFile(derPath, der, 0600); err != nil {
		t.Fatalf("write %s: %v", derPath, err)
	}
	if err := os.WriteFile(pemPath, pem.EncodeToMemory(&pem.Block{Type: "X509 CRL", Bytes: der}), 0600); err != nil {
		t.Fatalf("write %s: %v", pemPath, err)
	}
	return derPath, pemPath, root.SubjectKeyId
}

// anyPresent reports whether at least one alternative rendering of a value
// occurs in s. CLI.md section 4.3 fixes crlcheck's flag set and its exit status
// and nothing about how -verbose lays its description out, so an integer is
// looked for in every form a renderer might reasonably choose and never at a
// fixed label, column or order.
func anyPresent(s string, alts ...string) bool {
	for _, a := range alts {
		if strings.Contains(s, a) {
			return true
		}
	}
	return false
}

// unpunctuated drops the separators a hex rendering may or may not carry, so a
// key id matches whether it is printed bare or colon-separated.
func unpunctuated(s string) string {
	return strings.NewReplacer(":", "", " ", "").Replace(s)
}

func main() {
	t := vhelp.New()

	derPath, pemPath, rootSKID := mintCRL(t)
	rootPEM := vhelp.Fixture("minted", "bench-root.pem")
	roguePEM := vhelp.Fixture("minted", "rogue-root.pem")

	// Without -verbose nothing is described: CLI.md section 4.3 makes -verbose
	// the flag that produces a description, and the exit status the verdict.
	// Nothing here reads stderr's wording, which that section says is not
	// stable across releases.
	r := run(t, derPath)
	t.Check("a-der-crl-is-read-quietly", r.rc == 0 && r.stdout == "",
		"rc=%d stdout=%q stderr=%q", r.rc, r.stdout, r.stderr)

	rPEM := run(t, pemPath)
	t.Check("a-pem-crl-is-read-quietly", rPEM.rc == 0 && rPEM.stdout == "",
		"rc=%d stdout=%q stderr=%q", rPEM.rc, rPEM.stdout, rPEM.stderr)

	// Zero positionals is legal and exits 0 (CLI.md section 4.3, first line).
	rNone := run(t)
	t.Check("no-positionals-is-legal-and-exits-zero", rNone.rc == 0 && rNone.stdout == "",
		"rc=%d stdout=%q stderr=%q", rNone.rc, rNone.stdout, rNone.stderr)

	// The flag set itself. CLI.md section 4.3: "crlcheck v1.0.5: ca (String
	// \"\"), verbose (Bool false). v1.0.10 removes verbose and adds strict and
	// cert." The two flags of the NEXT release must therefore not be declared
	// yet, and an undeclared flag is the flag package's own exit 2 (CLI.md
	// section 2.2, step 1). The positive half of the same statement -- that -ca
	// and -verbose are both accepted -- is what every other case here relies
	// on. The usage text the flag package prints is not graded.
	rStrict := run(t, "-strict", derPath)
	rCert := run(t, "-cert", derPath)
	t.Check("strict-and-cert-do-not-exist-until-the-next-release",
		rStrict.rc == 2 && rCert.rc == 2,
		"-strict rc=%d stderr=%q; -cert rc=%d stderr=%q",
		rStrict.rc, rStrict.stderr, rCert.rc, rCert.stderr)

	vDER := run(t, "-verbose", derPath)
	vPEM := run(t, "-verbose", pemPath)
	t.Equal("both-encodings-describe-the-same-crl", vDER.stdout, vPEM.stdout)

	// -verbose describes the CRL. No corpus page fixes the layout of that
	// description, so what is graded is that the CRL's own values reached the
	// output at all -- not the labels, the indentation, the field order or the
	// date format. The values are the ones this driver signed into the CRL
	// above and the ones FIXTURES.md pins for bench-root; the key id is read
	// back off the parsed certificate rather than written out as a constant,
	// and is matched with its separators removed so that either the bare or
	// the colon-separated hex form passes.
	t.Check("verbose-describes-the-crl-header",
		strings.Contains(vDER.stdout, "Harbor CT Bench Root") &&
			strings.Contains(unpunctuated(vDER.stdout), hex.EncodeToString(rootSKID)),
		"stdout=%q", vDER.stdout)

	t.Check("verbose-lists-the-revoked-certificate",
		anyPresent(vDER.stdout, "4098", "0x1002", "1002"),
		"stdout=%q", vDER.stdout)

	// -ca checks the CRL signature. CLI.md section 4.3 fixes the exit status
	// and nothing else: "the diagnostics themselves go to stderr and their
	// wording is not stable across releases". What is graded is the verdict.
	r = run(t, "-ca", rootPEM, derPath)
	t.Check("the-crl-verifies-against-its-issuer", r.rc == 0,
		"rc=%d stdout=%q stderr=%q", r.rc, r.stdout, r.stderr)

	r = run(t, "-ca", roguePEM, derPath)
	t.Check("the-crl-does-not-verify-against-another-ca", r.rc == 1,
		"rc=%d stdout=%q stderr=%q", r.rc, r.stdout, r.stderr)

	// A file that is not a CRL fails its target, so the run exits 1; and even
	// under -verbose there is nothing to describe, so no description is
	// printed. The diagnostic's own text is not graded.
	r = run(t, "-verbose", vhelp.Fixture("minted", "leaf-valid.der"))
	t.Check("a-file-that-is-not-a-crl-is-reported", r.rc == 1 && r.stdout == "",
		"rc=%d stdout=%q stderr=%q", r.rc, r.stdout, r.stderr)

	// So does a file that is not there. The message wrapping the read failure
	// comes partly from the Go standard library, so only the verdict and the
	// fact that something was said are graded.
	missing := filepath.Join(os.Getenv("CT_SCRATCH"), "no-such.crl")
	r = run(t, "-verbose", missing)
	t.Check("a-missing-file-is-reported",
		r.rc == 1 && r.stdout == "" && strings.TrimSpace(r.stderr) != "",
		"rc=%d stdout=%q stderr=%q", r.rc, r.stdout, r.stderr)

	t.Done()
}

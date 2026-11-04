// Group certcheck-chain-from-file.
//
// v1.0.17 changes what `certcheck --validate <file>` means (commit aec482b4,
// "certcheck: treat multiple certs in a file as a chain"). v1.0.13 verified
// every certificate in the file independently against whatever pools the
// --root/--intermediate flags built, so a self-contained chain file always
// failed: the leaf had no issuer in the pool and the root was not in the pool
// either. v1.0.17, when neither flag is given, reads the file as a chain --
// a trailing self-signed certificate becomes the root, the certificates
// between it and the leaf become intermediates, and only the leaf is
// verified.
//
// Graded on the candidate's own certcheck binary by **exit status alone**.
// certcheck accumulates one failure count over every target and every reason
// and exits 1 if it is non-zero, 0 otherwise (CLI.md section 4.3), so the
// status is a complete signal for "did this file validate" -- and, unlike the
// text it prints, it is stable across releases. No message string is asserted
// here; TEST_INTERFACE.md section 9 puts diagnostic wording out of scope.
//
// Cases 3, 4 and 5 are controls that hold at both anchors, so the group cannot
// be passed by a certcheck that validates nothing: 3 pins the *precondition*
// of the new behaviour (chain adoption happens only when neither --root nor
// --intermediate was given), 4 pins that a chain file with no self-signed tail
// still has no anchor, and 5 pins that the explicit pools still work.
//
// SCOPE-TIME: --timecheck is never passed, so certcheck sets
// DisableTimeChecks and no assertion here depends on a clock.
//
// Inputs are pinned fixtures; the three-certificate file in case 2 is those
// same fixtures concatenated into $CT_SCRATCH, because the shared corpus has
// no leaf+intermediate+root file.
package main

import (
	"os"
	"path/filepath"
	"strings"

	"ctverify/vhelp"
)

// runCertcheck returns certcheck's exit status. Its output is captured and
// discarded: it is never asserted on, only kept out of this process's stdout.
func runCertcheck(t *vhelp.T, args ...string) int {
	cmd := vhelp.Command(vhelp.CandidateBin("certcheck"), args...)
	var b strings.Builder
	cmd.Stdout = &b
	cmd.Stderr = &b
	err := cmd.Run()
	if err != nil && cmd.ProcessState == nil {
		t.Fatalf("run certcheck: %v", err)
	}
	return cmd.ProcessState.ExitCode()
}

func main() {
	t := vhelp.New()

	// leaf-valid + bench-intermediate + bench-root, in that order.
	threeCert := filepath.Join(os.Getenv("CT_SCRATCH"), "leaf-inter-root.pem")
	var joined []byte
	for _, f := range [][]string{
		{"minted", "leaf-valid.pem"},
		{"minted", "bench-intermediate.pem"},
		{"minted", "bench-root.pem"},
	} {
		joined = append(joined, vhelp.ReadFixture(f...)...)
	}
	if err := os.WriteFile(threeCert, joined, 0o644); err != nil {
		t.Fatalf("write composed chain: %v", err)
	}

	// --- 1. a two-certificate self-rooted file now validates ----------------
	// leaf-unrooted is issued by rogue-root, which is self-signed and trusted
	// by nothing; read as a chain, the file supplies its own anchor.
	rc := runCertcheck(t, "--validate", vhelp.Fixture("minted", "chain-unrooted-with-root.pem"))
	t.Equal("self-rooted-chain-file-validates", rc, 0)

	// --- 2. and so does a three-certificate one, through the intermediate ---
	rc = runCertcheck(t, "--validate", threeCert)
	t.Equal("three-cert-chain-file-validates-through-intermediate", rc, 0)

	// --- 3. control: supplying either pool flag suppresses chain adoption ---
	// The same self-rooted file, but with --intermediate given: the file is no
	// longer read as a chain, so its trailing self-signed certificate is not
	// adopted, the root pool stays empty and the leaf must fail. This holds at
	// both anchors and is what makes case 1's success mean chain adoption
	// rather than a certcheck that trusts anything.
	rc = runCertcheck(t, "--validate",
		"--intermediate="+vhelp.Fixture("minted", "bench-intermediate.pem"),
		threeCert)
	t.Equal("explicit-pool-flag-suppresses-chain-adoption", rc, 1)

	// --- 4. control: a chain file that does not reach an anchor is refused --
	// chain-valid.pem is leaf + intermediate with no root, so there is nothing
	// to trust and the leaf must still fail.
	rc = runCertcheck(t, "--validate", vhelp.Fixture("minted", "chain-valid.pem"))
	t.Equal("chain-file-without-root-still-refused", rc, 1)

	// --- 5. control: explicit pools are still what they always were ---------
	rc = runCertcheck(t, "--validate",
		"--root="+vhelp.Fixture("minted", "bench-root.pem"),
		"--intermediate="+vhelp.Fixture("minted", "bench-intermediate.pem"),
		vhelp.Fixture("minted", "chain-valid.pem"))
	t.Equal("explicit-root-and-intermediate-flags-still-honoured", rc, 0)

	t.Done()
}

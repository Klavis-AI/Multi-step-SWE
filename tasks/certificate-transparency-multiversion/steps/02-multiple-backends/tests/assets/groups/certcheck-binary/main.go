// Group certcheck-binary.
//
// v1.0.5 adds the certcheck command: the release's user-facing certificate
// inspector and chain validator. It is graded here the way a user meets it --
// as a process, through its exit status, its stdout and its stderr.
//
// Its interface is specific: positional arguments are read with
// ReadPossiblePEMFile so either PEM or DER works and a bundle is expanded,
// while -root and -intermediate are read as raw DER; -verbose prints an
// openssl-like description; -validate builds a chain against the supplied
// pools; and time checks are OFF unless -timecheck is given, so an expired
// certificate validates without it and fails with it. Any error makes the
// process exit 1.
package main

import (
	"bytes"
	"os/exec"
	"strings"

	"ctverify/vhelp"
)

type result struct {
	rc     int
	stdout string
	stderr string
}

func run(t *vhelp.T, args ...string) result {
	cmd := vhelp.Command(vhelp.CandidateBin("certcheck"), args...)
	var out, errb bytes.Buffer
	cmd.Stdout = &out
	cmd.Stderr = &errb
	rc := 0
	if err := cmd.Run(); err != nil {
		ee, ok := err.(*exec.ExitError)
		if !ok {
			t.Fatalf("certcheck %v: %v", args, err)
		}
		rc = ee.ExitCode()
	}
	return result{rc: rc, stdout: out.String(), stderr: errb.String()}
}

// allPresent reports whether every wanted substring occurs in s.
func allPresent(s string, want ...string) bool {
	for _, w := range want {
		if !strings.Contains(s, w) {
			return false
		}
	}
	return true
}

// anyPresent reports whether at least one alternative rendering of a value
// occurs in s. CLI.md section 4.3 fixes certcheck's flag set and its exit
// status and nothing about how -verbose spells a value, so an integer is
// looked for in every form a renderer might reasonably choose.
func anyPresent(s string, alts ...string) bool {
	for _, a := range alts {
		if strings.Contains(s, a) {
			return true
		}
	}
	return false
}

func main() {
	t := vhelp.New()

	leafPEM := vhelp.Fixture("minted", "leaf-valid.pem")
	leafDER := vhelp.Fixture("minted", "leaf-valid.der")
	expiredPEM := vhelp.Fixture("minted", "leaf-expired.pem")
	unrootedPEM := vhelp.Fixture("minted", "leaf-unrooted.pem")
	bundlePEM := vhelp.Fixture("minted", "roots-bench-plus-int.pem")
	rootDER := vhelp.Fixture("minted", "bench-root.der")
	intDER := vhelp.Fixture("minted", "bench-intermediate.der")

	// Reading a certificate is silent and successful, in either encoding.
	r := run(t, leafPEM)
	t.Equal("reading-a-pem-certificate-is-silent-and-succeeds",
		itoa(int64(r.rc))+"|"+r.stdout+"|"+r.stderr, "0||")

	r = run(t, leafDER)
	t.Equal("a-der-certificate-is-read-the-same-way",
		itoa(int64(r.rc))+"|"+r.stdout+"|"+r.stderr, "0||")

	// -verbose renders the certificate. CLI.md section 4.3 gives certcheck its
	// flags and its exit status and fixes nothing about the layout of that
	// rendering -- no corpus page states the labels, the indentation, the field
	// order or how an integer or a date is spelled -- so what is graded is that
	// the certificate's own pinned identifying values reached stdout, and an
	// integer is accepted in any of the forms a renderer might choose. The
	// values are FIXTURES.md's for minted/leaf-valid: serial 0x1001 (4097
	// decimal), SAN valid.bench.invalid, issued by bench-intermediate.
	r = run(t, "-verbose", leafPEM)
	t.Check("verbose-describes-the-certificate",
		r.rc == 0 &&
			anyPresent(r.stdout, "4097", "0x1001", "1001") &&
			allPresent(r.stdout, "valid.bench.invalid", "Harbor CT Bench Intermediate"),
		"rc=%d stdout=%q", r.rc, r.stdout)

	// A PEM bundle is expanded: every block is described, in file order. This
	// is the ReadPossiblePEMFile contract of CLIENT-API.md section 5.2 seen
	// through the binary -- "every block whose type equals blockname is
	// collected in file order" -- so the order is graded and the layout is not.
	r = run(t, "-verbose", bundlePEM)
	rootAt := strings.Index(r.stdout, "Harbor CT Bench Root")
	intAt := strings.Index(r.stdout, "Harbor CT Bench Intermediate")
	t.Check("a-pem-bundle-is-expanded-into-every-certificate",
		r.rc == 0 && rootAt >= 0 && intAt > rootAt,
		"rc=%d root@%d intermediate@%d stdout=%q", r.rc, rootAt, intAt, r.stdout)

	// -validate builds a chain against the DER pools it was given.
	r = run(t, "-validate", "-root", rootDER, "-intermediate", intDER, leafPEM)
	t.Equal("validate-accepts-a-chain-that-reaches-the-supplied-root",
		itoa(int64(r.rc))+"|"+r.stderr, "0|")

	// CLI.md section 4.3: "Exit status is the only thing a caller should key
	// on... the diagnostics themselves go to stderr and their wording is not
	// stable across releases." So a refusal is graded as the non-zero exit plus
	// the fact that something was said about it, never as the sentence.
	r = run(t, "-validate", "-root", rootDER, unrootedPEM)
	t.Check("validate-reports-a-leaf-that-reaches-no-supplied-root",
		r.rc == 1 && strings.TrimSpace(r.stderr) != "",
		"rc=%d stderr=%q", r.rc, r.stderr)

	// Validity dates are not consulted unless -timecheck asks for it.
	r = run(t, "-validate", "-root", rootDER, "-intermediate", intDER, expiredPEM)
	t.Equal("validity-dates-are-ignored-without-timecheck",
		itoa(int64(r.rc))+"|"+r.stderr, "0|")

	r = run(t, "-validate", "-timecheck", "-root", rootDER, "-intermediate", intDER, expiredPEM)
	t.Check("timecheck-refuses-a-certificate-outside-its-validity-window",
		r.rc == 1 && strings.TrimSpace(r.stderr) != "",
		"rc=%d stderr=%q", r.rc, r.stderr)

	// A missing input is an error, and the tool says which file -- the name it
	// was given is the one piece of the diagnostic a caller can rely on, since
	// certcheck accumulates failures over several targets.
	missing := vhelp.GroupFile("no-such-certificate.pem")
	r = run(t, missing)
	t.Check("a-missing-input-file-is-reported-and-exits-nonzero",
		r.rc == 1 && strings.Contains(r.stderr, missing),
		"rc=%d stderr=%q", r.rc, r.stderr)

	// --- the flag set ------------------------------------------------------
	// TEST_INTERFACE.md section 4 says certcheck is graded "on exit status and
	// on the flag set of CLI.md section 4.3", and 4.3 gives v1.0.5 exactly six
	// flags: root and intermediate (String ""), verbose, validate, timecheck
	// and check_revocation (all Bool false). The names, the types and the
	// defaults are the contract; the description wording is stated nowhere and
	// is not asserted. The list is also exhaustive at this release -- the three
	// flags later releases add (ignore_unknown_critical_exts at v1.0.10,
	// system_roots at v1.0.21, and v1.1.0's rename of timecheck to check_time)
	// must not be here yet.
	//
	// -help itself is the flag package's own path, not an error: CLI.md's
	// preamble fixes exit 0, the block on stderr and therefore nothing on
	// stdout, and TEST_INTERFACE.md section 4 repeats all three for this step.
	// The banner line and each flag's declaration line are compared whole
	// (TEST_INTERFACE.md section 6.1, item 5).
	r = run(t, "-help")
	lines := strings.Split(r.stderr, "\n")
	t.Check("help-declares-the-documented-flag-set",
		r.rc == 0 && r.stdout == "" && len(lines) > 0 &&
			lines[0] == "Usage of "+vhelp.CandidateBin("certcheck")+":" &&
			flagDeclared(lines, "  -root string") &&
			flagDeclared(lines, "  -intermediate string") &&
			flagDeclared(lines, "  -verbose") &&
			flagDeclared(lines, "  -validate") &&
			flagDeclared(lines, "  -timecheck") &&
			flagDeclared(lines, "  -check_revocation"),
		"rc=%d stdout=%q stderr=%q", r.rc, r.stdout, r.stderr)

	t.Check("no-flag-carries-a-non-zero-default",
		!strings.Contains(r.stderr, "(default "),
		"stderr=%q", r.stderr)

	t.Check("later-releases-flags-are-absent",
		!strings.Contains(r.stderr, "ignore_unknown_critical_exts") &&
			!strings.Contains(r.stderr, "system_roots") &&
			!strings.Contains(r.stderr, "check_time"),
		"stderr=%q", r.stderr)

	// An unrecognised flag is the flag package's own usage error: the banner
	// and the flag list on stderr, exit 2 (TEST_INTERFACE.md section 4).
	r = run(t, "-no-such-flag")
	t.Check("an-unrecognised-flag-is-a-usage-error",
		r.rc == 2 && r.stdout == "" &&
			strings.Contains(r.stderr, "Usage of "+vhelp.CandidateBin("certcheck")+":"),
		"rc=%d stdout=%q stderr=%q", r.rc, r.stdout, r.stderr)

	// Zero positionals is legal and exits 0 (CLI.md section 4.3).
	r = run(t)
	t.Check("no-positional-arguments-is-legal",
		r.rc == 0 && r.stdout == "", "rc=%d stdout=%q stderr=%q", r.rc, r.stdout, r.stderr)

	t.Done()
}

// flagDeclared reports whether the usage block declares exactly this flag,
// i.e. whether some line is the name-and-type line the flag package writes.
func flagDeclared(lines []string, decl string) bool {
	for _, l := range lines {
		if l == decl {
			return true
		}
	}
	return false
}

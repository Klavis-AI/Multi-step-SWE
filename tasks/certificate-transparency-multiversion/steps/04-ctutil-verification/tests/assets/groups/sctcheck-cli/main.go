// Group sctcheck-cli.
//
// v1.0.13 ships the sctcheck binary: it loads a v1 CT log list, then for each
// certificate file named on the command line pulls the embedded SCTs out of
// the leaf, rebuilds the precertificate Merkle leaf, finds each SCT's log by
// key hash and checks the SCT's signature and its inclusion in that log. Its
// exit status is the verdict: 1 if any SCT it examined could not be
// validated, 0 otherwise, including when there was nothing to examine.
//
// Boundary: the binary run as a subprocess from $CT_BIN -- its exit status
// and its documented flag grammar. What it prints while checking targets is
// deliberately NOT asserted: TEST_INTERFACE.md section 4 says the verdict is
// the exit status and leaves both streams free, so routing per-certificate
// output to stdout instead of stderr must not cost a case. Only the three
// flag-grammar runs require an empty stdout, and there it follows from the
// flag package writing its usage block to stderr (section 4, rule 1).
//
// Every input is a pinned file. Nothing is fetched: the log list comes from
// disk and every log in it is unreachable, so the inclusion step cannot
// succeed. That limit is the point of the two halves of this group -- a
// certificate with no SCTs to check exits 0, and a certificate whose SCTs
// cannot be shown included exits 1.
//
// This driver deliberately imports no product package.
package main

import (
	"bytes"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	"ctverify/vhelp"
)

// run executes sctcheck and returns its exit status, its stdout and its
// stderr.
//
// Only an *exec.ExitError -- the process ran and chose a status -- yields an
// exit status. Every other error from cmd.Run means no verdict was ever
// produced: the commonest is an *exec.Error because $CT_BIN/sctcheck was
// never built, and the rest (permission denied, fork failure) are equally not
// answers from the binary. Inventing a status for those would let this whole
// group grade a tree that ships no sctcheck at all -- rc=1 with empty output
// is exactly what the refusal cases here look for. So it aborts the group the
// way steps/02/groups/certcheck-binary does: t.Fatalf prints an honest
// summary whose executed count is short of COUNT, which test.sh rejects.
func run(t *vhelp.T, arg ...string) (int, string, string) {
	bin := vhelp.CandidateBin("sctcheck")
	cmd := vhelp.Command(bin, arg...)
	cmd.Env = append(cmd.Env, "TZ=UTC")
	var out, errOut bytes.Buffer
	cmd.Stdout = &out
	cmd.Stderr = &errOut
	rc := 0
	if err := cmd.Run(); err != nil {
		ee, ok := err.(*exec.ExitError)
		if !ok {
			t.Fatalf("sctcheck %v: %v", arg, err)
		}
		rc = ee.ExitCode()
	}
	return rc, out.String(), errOut.String()
}

func main() {
	t := vhelp.New()
	scratch := os.Getenv("CT_SCRATCH")
	list := "--log_list=" + vhelp.GroupFile("loglist.json")

	write := func(name string, parts ...[]byte) string {
		p := filepath.Join(scratch, name)
		var b []byte
		for _, part := range parts {
			b = append(b, part...)
		}
		if err := os.WriteFile(p, b, 0o600); err != nil {
			t.Fatalf("writing %s: %v", name, err)
		}
		return p
	}

	plain := write("plain.pem", vhelp.ReadFixture("rfc6962", "test-cert.pem"))
	chain := write("chain.pem",
		vhelp.ReadFixture("rfc6962", "test-embedded-cert.pem"),
		vhelp.ReadFixture("rfc6962", "ca-cert.pem"))
	keyOnly := write("key.pem", vhelp.ReadFixture("keys", "log-public-key.pem"))
	notPEM := write("notpem.txt", []byte("no PEM block here at all\n"))
	absent := filepath.Join(scratch, "no-such-file.pem")

	rc, out, _ := run(t, list, plain)
	t.Check("a-certificate-with-no-embedded-scts-is-clean",
		rc == 0, "rc=%d, stdout=%q", rc, out)

	rc, out, _ = run(t, list, chain)
	t.Check("an-embedded-sct-that-cannot-be-shown-included-is-reported-invalid",
		rc == 1, "rc=%d, stdout=%q", rc, out)

	rc, out, _ = run(t, list)
	t.Check("naming-no-certificate-checks-nothing-and-succeeds",
		rc == 0, "rc=%d, stdout=%q", rc, out)

	rc, out, _ = run(t, list, absent)
	t.Check("a-certificate-file-that-cannot-be-read-is-skipped",
		rc == 0, "rc=%d, stdout=%q", rc, out)

	rc, out, _ = run(t, list, keyOnly)
	t.Check("a-pem-file-that-holds-no-certificate-is-skipped",
		rc == 0, "rc=%d, stdout=%q", rc, out)

	rc, out, _ = run(t, list, notPEM)
	t.Check("a-file-with-no-pem-block-is-skipped",
		rc == 0, "rc=%d, stdout=%q", rc, out)

	// The same clean certificate that exits 0 above exits non-zero when the
	// log list cannot be loaded: the list is read before any argument is.
	// Which non-zero status is not graded -- glog.Exit* gives 1 and
	// glog.Fatal* gives 255 and no document picks between them.
	rc, out, _ = run(t, "--log_list="+filepath.Join(scratch, "absent-list.json"), plain)
	t.Check("the-log-list-is-loaded-before-any-certificate-is-looked-at",
		rc != 0, "rc=%d, stdout=%q", rc, out)

	rc, out, _ = run(t, "--log_list="+vhelp.GroupFile("notalist.json"), plain)
	t.Check("a-log-list-file-that-is-not-a-log-list-is-fatal",
		rc != 0, "rc=%d, stdout=%q", rc, out)

	// Asking for the DNS access points does not change the verdict for a
	// certificate whose SCTs cannot be shown included.
	rc, out, _ = run(t, list, "--dns", chain)
	t.Check("the-dns-access-point-mode-runs-and-still-reports-invalid",
		rc == 1, "rc=%d, stdout=%q", rc, out)

	// --- flag grammar --------------------------------------------------------
	rc, out, _ = run(t, list, "--deadline=bogus", plain)
	t.Check("an-unparsable-deadline-is-a-usage-error",
		rc == 2 && out == "", "rc=%d, stdout=%q", rc, out)

	rc, out, _ = run(t, "--no-such-flag")
	t.Check("an-unknown-flag-is-a-usage-error",
		rc == 2 && out == "", "rc=%d, stdout=%q", rc, out)

	rc, out, errOut := run(t, "--help")
	lines := strings.Split(errOut, "\n")
	wantFirst := "Usage of " + vhelp.CandidateBin("sctcheck") + ":"
	// CLI.md SS4.4 fixes the flag names, their types and their defaults: at
	// v1.0.13 sctcheck has log_list (String), deadline (Duration 30s) and dns
	// (Bool false). The flag package renders the name and type on one line and
	// the description, plus "(default <value>)" for a non-zero default, on the
	// next. The description wording is specified nowhere and is not asserted.
	t.Check("help-prints-the-documented-flags-and-succeeds",
		rc == 0 && out == "" && len(lines) > 0 && lines[0] == wantFirst &&
			flagDeclared(lines, "  -log_list string") &&
			flagDeclared(lines, "  -deadline duration") &&
			flagDeclared(lines, "  -dns") &&
			strings.HasSuffix(descriptionFor(lines, "  -deadline duration"), "(default 30s)") &&
			!strings.Contains(descriptionFor(lines, "  -dns"), "(default "),
		"rc=%d, stdout=%q, first stderr line=%q, deadline line=%q, dns line=%q",
		rc, out, firstLine(lines),
		descriptionFor(lines, "  -deadline duration"), descriptionFor(lines, "  -dns"))

	t.Done()
}

// flagDeclared reports whether the usage block declares exactly this flag,
// i.e. whether some line is the name-and-type line given.
func flagDeclared(lines []string, decl string) bool {
	for _, l := range lines {
		if l == decl {
			return true
		}
	}
	return false
}

// descriptionFor returns the line the flag package writes under a flag's
// name-and-type line, which carries the description and any default.
func descriptionFor(lines []string, decl string) string {
	for i, l := range lines {
		if l == decl && i+1 < len(lines) {
			return lines[i+1]
		}
	}
	return ""
}

func firstLine(lines []string) string {
	if len(lines) == 0 {
		return ""
	}
	return lines[0]
}

// Group findlog-cli.
//
// v1.0.13 ships the findlog binary: it loads a v1 CT log list from a file or
// URL, optionally checks a detached signature over it, and resolves each
// command-line argument to a log with loglist.FuzzyFindLog, printing one line
// per match and a labelled block per match under --verbose.
//
// Boundary: the binary itself, run as a subprocess from $CT_BIN with its
// stdout captured, plus its exit status. The log list, its detached ECDSA
// signature and the signing key are pinned; nothing is fetched.
//
// What stdout is checked for is the contract of TEST_INTERFACE.md section 6:
// one line per log matched, in argument order, each carrying that log's
// description and its URL, and under --verbose an indented block per match
// carrying the key, its hash, the merge delay, the operators, the DNS
// endpoint and the final STH. The exact spacing, labels and date rendering
// are deliberately not asserted -- upstream renders a seconds-valued
// disqualified_at through a milliseconds converter, and pinning that would
// make a defect a requirement.
//
// This driver deliberately imports no product package: what it observes is
// the binary the candidate's tree builds, not a library symbol.
package main

import (
	"bytes"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"os"
	"os/exec"
	"strings"

	"ctverify/vhelp"
)

type logEntry struct {
	Description string `json:"description"`
	Key         []byte `json:"key"`
	URL         string `json:"url"`
}

type logList struct {
	Logs []logEntry `json:"logs"`
}

// The four logs of the pinned list, by description and URL. A matching line
// must name both.
const (
	testLogDesc = "Erw Wen RFC6962 Test Log"
	testLogURL  = "ct.example.com/testlog/"
	aberDesc    = "Aberystwyth Log"
	aberURL     = "ct.example.org/aber/"
	secondDesc  = "Wales Secondary Log"
	secondURL   = "https://ct.example.org/secondary"
	frozenDesc  = "Frozen Retired Log"
	frozenURL   = "ct.example.net/frozen"
)

// The values the pinned list gives the frozen log's final_sth and
// disqualified_at. How they are laid out is not asserted; that they are all
// reported, and correctly, is.
var frozenSTHValues = []string{
	"86400",
	"Erw Wen CT",
	"Wales Logs Ltd",
	"42",
	"1400000000000",
	"18041bd4665083001fba8c5411d2d748e8abbfdcdfd9218cb02b68a78e7d4c23",
	"040300483046022100befd8060563763a5e49ba53e6443c13f7624fd6403178113736e16012aca983e022100f572568dbfe9a86490eb915c4ee16ad5ecd708fed35ed4e5cd1b2c3f087b4130",
	"1451606400",
}

// matched reports whether out is exactly the given sequence of match lines:
// one non-empty line per expected log, in order, each naming that log's
// description and its URL.
func matched(out string, descURL ...string) bool {
	lines := nonEmptyLines(out)
	if len(lines)*2 != len(descURL) {
		return false
	}
	for i, l := range lines {
		if !strings.Contains(l, descURL[2*i]) || !strings.Contains(l, descURL[2*i+1]) {
			return false
		}
	}
	return true
}

func nonEmptyLines(out string) []string {
	var r []string
	for _, l := range strings.Split(out, "\n") {
		if strings.TrimSpace(l) != "" {
			r = append(r, l)
		}
	}
	return r
}

func containsAll(out string, want []string) string {
	for _, w := range want {
		if !strings.Contains(out, w) {
			return w
		}
	}
	return ""
}

var (
	listPath = vhelp.GroupFile("loglist.json")
	sigPath  = vhelp.GroupFile("loglist.sig.ec")
	junkPath = vhelp.GroupFile("notalist.json")
	goodKey  = vhelp.Fixture("rfc6962", "sig-test-ec256-public-key.pem")
	otherKey = vhelp.Fixture("rfc6962", "sig-test-ec256-public-key2.pem")
)

// run executes findlog and returns its exit status and its stdout. stderr is
// captured separately and never mixed into stdout.
//
// Only an *exec.ExitError -- the process ran and chose a status -- yields an
// exit status. Every other error from cmd.Run means no verdict was ever
// produced: the commonest is an *exec.Error because $CT_BIN/findlog was never
// built, and the rest (permission denied, fork failure) are equally not
// answers from the binary. Inventing a status for those would let this whole
// group grade a tree that ships no findlog at all -- rc!=0 with empty stdout
// is exactly what the three error cases here look for. So it aborts the group
// the way steps/02/groups/certcheck-binary does: t.Fatalf prints an honest
// summary whose executed count is short of COUNT, which test.sh rejects.
func run(t *vhelp.T, arg ...string) (int, string) {
	cmd := vhelp.Command(vhelp.CandidateBin("findlog"), arg...)
	cmd.Env = append(cmd.Env, "TZ=UTC")
	var out, errOut bytes.Buffer
	cmd.Stdout = &out
	cmd.Stderr = &errOut
	rc := 0
	if err := cmd.Run(); err != nil {
		ee, ok := err.(*exec.ExitError)
		if !ok {
			t.Fatalf("findlog %v: %v", arg, err)
		}
		rc = ee.ExitCode()
	}
	return rc, out.String()
}

func sha256Of(b []byte) []byte {
	h := sha256.Sum256(b)
	return h[:]
}

func main() {
	t := vhelp.New()

	data, err := os.ReadFile(listPath)
	if err != nil {
		t.Fatalf("reading the pinned log list: %v", err)
	}
	var ll logList
	if err := json.Unmarshal(data, &ll); err != nil || len(ll.Logs) != 4 {
		t.Fatalf("pinned log list did not parse into 4 logs: %v", err)
	}
	testKey := ll.Logs[0].Key
	keyHash := sha256Of(testKey)

	list := "--log_list=" + listPath

	rc, out := run(t, list, "Erw Wen")
	t.Check("finds-a-log-by-a-substring-of-its-description",
		rc == 0 && matched(out, testLogDesc, testLogURL), "rc=%d, stdout=%q", rc, out)

	rc, out = run(t, list, base64.StdEncoding.EncodeToString(keyHash))
	t.Check("finds-a-log-by-the-base64-of-its-key-hash",
		rc == 0 && matched(out, testLogDesc, testLogURL), "rc=%d, stdout=%q", rc, out)

	rc, out = run(t, list, hex.EncodeToString(testKey))
	t.Check("finds-a-log-by-the-hex-of-its-der-key",
		rc == 0 && matched(out, testLogDesc, testLogURL), "rc=%d, stdout=%q", rc, out)

	rc, out = run(t, list, "ct.example.org/aber")
	t.Check("finds-a-log-by-url-without-its-trailing-slash",
		rc == 0 && matched(out, aberDesc, aberURL), "rc=%d, stdout=%q", rc, out)

	rc, out = run(t, list, "Aberystwyth", "Wales Secondary")
	t.Check("resolves-several-arguments-in-the-order-given",
		rc == 0 && matched(out, aberDesc, aberURL, secondDesc, secondURL),
		"rc=%d, stdout=%q", rc, out)

	rc, out = run(t, list, "zzzz-no-such-log-zzzz")
	t.Check("an-argument-that-matches-no-log-prints-nothing-and-succeeds",
		rc == 0 && out == "", "rc=%d, stdout=%q", rc, out)

	wantVerbose := []string{
		hex.EncodeToString(testKey),
		base64.StdEncoding.EncodeToString(testKey),
		hex.EncodeToString(keyHash),
		base64.StdEncoding.EncodeToString(keyHash),
		"86400",
		"Erw Wen CT",
		"testlog.ct.example.com",
	}
	rc, out = run(t, list, "--verbose", "Erw Wen")
	missing := containsAll(out, wantVerbose)
	first := nonEmptyLines(out)
	t.Check("verbose-prints-the-key-its-hash-the-mmd-the-operator-and-the-dns-endpoint",
		rc == 0 && len(first) > 1 &&
			strings.Contains(first[0], testLogDesc) && strings.Contains(first[0], testLogURL) &&
			missing == "",
		"rc=%d, missing=%q, stdout=%q", rc, missing, out)

	rc, out = run(t, list, "--verbose", "Frozen")
	missing = containsAll(out, frozenSTHValues)
	first = nonEmptyLines(out)
	t.Check("verbose-prints-the-final-sth-and-the-disqualification-of-a-frozen-log",
		rc == 0 && len(first) > 1 &&
			strings.Contains(first[0], frozenDesc) && strings.Contains(first[0], frozenURL) &&
			missing == "",
		"rc=%d, missing=%q, stdout=%q", rc, missing, out)

	rc, out = run(t, list)
	t.Check("naming-no-log-at-all-is-an-error",
		rc != 0 && out == "", "rc=%d, stdout=%q", rc, out)

	rc, out = run(t, "--log_list="+vhelp.GroupFile("no-such-log-list.json"))
	t.Check("a-log-list-file-that-does-not-exist-is-an-error",
		rc != 0 && out == "", "rc=%d, stdout=%q", rc, out)

	rc, out = run(t, "--log_list="+junkPath, "Erw Wen")
	t.Check("a-log-list-file-that-is-not-a-log-list-is-an-error",
		rc != 0 && out == "", "rc=%d, stdout=%q", rc, out)

	rcGood, outGood := run(t, list, "--log_list_sig="+sigPath, "--log_list_pubkey="+goodKey, "Erw Wen")
	rcBad, outBad := run(t, list, "--log_list_sig="+sigPath, "--log_list_pubkey="+otherKey, "Erw Wen")
	t.Check("a-detached-signature-is-checked-against-the-supplied-key",
		rcGood == 0 && matched(outGood, testLogDesc, testLogURL) && rcBad != 0 && outBad == "",
		"matching key: rc=%d stdout=%q; other key: rc=%d stdout=%q", rcGood, outGood, rcBad, outBad)

	t.Done()
}

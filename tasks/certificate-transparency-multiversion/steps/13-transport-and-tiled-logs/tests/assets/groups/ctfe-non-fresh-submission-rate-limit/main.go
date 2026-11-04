// ctfe-non-fresh-submission-rate-limit
//
// v1.3.3 lets a log throttle submissions of certificates that are not fresh.
// ct_server grows --non_fresh_submission_limit ("N/duration", empty to
// disable), --non_fresh_submission_age (default 24h) and
// --non_fresh_submission_burst (default 1); a submission whose leaf NotBefore
// is older than the age bound must take a token from the limiter, and a
// submission that cannot is answered 429 Too Many Requests with the new
// "rate-limited submission considered to be non-fresh" body.
//
// Graded over a real loopback socket against the candidate's own ct_server on
// four instances started by this group's own serverkit.go. The pinned
// fixture chains have NotBefore 2020-01-01, decades in the past, so "older
// than one second" and "younger than one hundred years" are both properties of
// the fixture rather than of the clock: no assertion here reads the time.
// SCOPE-BACKEND: the stub backend exists only so accepted submissions have
// somewhere to go. The error string asserted is an HTTP response body.
package main

import (
	"encoding/base64"
	"encoding/json"
	"fmt"
	"strings"

	"github.com/google/certificate-transparency-go/x509util"

	"ctverify/vhelp"
)

// The exact body the CTFE serves when the limiter refuses a submission. The
// 429 error is raised without the log prefix, unlike the parse failures.
const throttled = "429|Too Many Requests\nrate-limited submission considered to be non-fresh\n"

// derList returns the DER of every certificate in a pinned PEM fixture.
func derList(t *vhelp.T, rel ...string) [][]byte {
	certs, err := x509util.CertificatesFromPEM(vhelp.ReadFixture(rel...))
	if err != nil || len(certs) == 0 {
		t.Fatalf("parse %v: %v", rel, err)
	}
	out := make([][]byte, 0, len(certs))
	for _, c := range certs {
		out = append(out, c.Raw)
	}
	return out
}

func chainBody(ders [][]byte) []byte {
	var b strings.Builder
	b.WriteString(`{"chain":[`)
	for i, der := range ders {
		if i > 0 {
			b.WriteString(",")
		}
		b.WriteString(`"` + base64.StdEncoding.EncodeToString(der) + `"`)
	}
	b.WriteString(`]}`)
	return []byte(b.String())
}

type sctReply struct {
	SCTVersion int    `json:"sct_version"`
	ID         string `json:"id"`
	Signature  string `json:"signature"`
}

// issued reports whether a response is a well-formed accepted submission.
func issued(r httpResult) bool {
	var s sctReply
	if r.status != 200 || json.Unmarshal(r.body, &s) != nil {
		return false
	}
	return s.ID != "" && s.Signature != ""
}

func statusAndBody(r httpResult) string {
	return fmt.Sprintf("%d|%s", r.status, string(r.body))
}

// submitN posts the same chain n times and renders every outcome, so a case
// can compare the whole sequence exactly.
func submitN(t *vhelp.T, h *harness, body []byte, n int) string {
	out := make([]string, 0, n)
	for i := 0; i < n; i++ {
		r := h.postJSON(t, h.url("/ct/v1/add-chain"), body)
		if issued(r) {
			out = append(out, "sct")
		} else {
			out = append(out, fmt.Sprintf("%d", r.status))
		}
	}
	return strings.Join(out, ",")
}

func main() {
	t := vhelp.New()

	logPriv := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-key.pem"))
	cert := chainBody(derList(t, "minted", "chain-valid.pem"))
	pre := chainBody(derList(t, "minted", "chain-precert.pem"))
	only := []logSpec{{id: pinnedLogID, prefix: logPrefix, privKey: logPriv}}

	// ---- instance A: one non-fresh submission per hour, default burst -----
	a := startPersonality(t, newStub(), only, []string{
		"--non_fresh_submission_limit=1/1h",
		"--non_fresh_submission_age=1s",
	})
	defer a.stop()

	// 1. Control: the limiter starts full, so the first submission of the
	//    pinned chain is accepted and issued an SCT. Without this every 429
	//    below could be satisfied by a log that refuses everything.
	first := a.postJSON(t, a.url("/ct/v1/add-chain"), cert)
	t.Check("control-first-non-fresh-submission-is-accepted",
		issued(first),
		"first add-chain gave status %d body %s", first.status, lastBytes(first.body, 200))

	// 2. The next one has no token left and is refused with the new status and
	//    the new message.
	second := a.postJSON(t, a.url("/ct/v1/add-chain"), cert)
	t.Equal("second-non-fresh-submission-is-429", statusAndBody(second), throttled)

	// 3. The refill rate is one per hour, so it stays refused.
	third := a.postJSON(t, a.url("/ct/v1/add-chain"), cert)
	t.Equal("third-non-fresh-submission-is-still-429", statusAndBody(third), throttled)

	// 4. add-pre-chain draws on the same limiter: the throttle is a property of
	//    the submission, not of one endpoint.
	prec := a.postJSON(t, a.url("/ct/v1/add-pre-chain"), pre)
	t.Equal("add-pre-chain-is-throttled-by-the-same-limit", statusAndBody(prec), throttled)

	// 5. Reads are untouched while submissions are throttled.
	reads := make([]string, 0, 3)
	for i := 0; i < 3; i++ {
		r := a.get(t, a.baseURL+"/"+logPrefix+"/ct/v1/get-roots")
		reads = append(reads, fmt.Sprintf("%d/%t", r.status, strings.Contains(string(r.body), "certificates")))
	}
	t.Equal("read-endpoints-are-not-rate-limited", strings.Join(reads, ","), "200/true,200/true,200/true")

	// ---- instance B: the same rate, an explicit burst of three ------------
	b := startPersonality(t, newStub(), only, []string{
		"--non_fresh_submission_limit=1/1h",
		"--non_fresh_submission_age=1s",
		"--non_fresh_submission_burst=3",
	})
	defer b.stop()

	// 6-7. The burst is the number of tokens the limiter starts with: three are
	//      accepted and the fourth is refused.
	t.Equal("burst-sets-how-many-non-fresh-submissions-pass", submitN(t, b, cert, 3), "sct,sct,sct")
	fourth := b.postJSON(t, b.url("/ct/v1/add-chain"), cert)
	t.Equal("submission-past-the-burst-is-429", statusAndBody(fourth), throttled)

	// ---- instance C: the same rate, an age bound the fixture is inside ----
	c := startPersonality(t, newStub(), only, []string{
		"--non_fresh_submission_limit=1/1h",
		"--non_fresh_submission_age=876000h",
	})
	defer c.stop()

	// 8. Only submissions older than the age bound are throttled. The same
	//    chain, the same rate, a bound of a hundred years: every submission is
	//    fresh and none of them takes a token.
	t.Equal("submissions-inside-the-age-bound-are-never-throttled",
		submitN(t, c, cert, 4), "sct,sct,sct,sct")

	// ---- instance D: no rate-limiting flags at all ------------------------
	d := startPersonality(t, newStub(), only, nil)
	defer d.stop()

	// 9. The empty default disables the limiter outright.
	t.Equal("rate-limiting-is-off-when-no-limit-is-configured",
		submitN(t, d, cert, 4), "sct,sct,sct,sct")

	t.Done()
}

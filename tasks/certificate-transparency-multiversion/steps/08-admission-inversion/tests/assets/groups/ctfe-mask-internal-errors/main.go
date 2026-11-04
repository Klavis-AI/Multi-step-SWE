// Group ctfe-mask-internal-errors.
//
// --mask_internal_errors / InstanceOptions.MaskInternalErrors is new at this
// milestone. It suppresses the detail text of a 500 response body and nothing
// else: a 503, a 400 and a 405 keep their detail. Error strings are graded
// here only because they are the HTTP response body.
//
// SCOPE-BACKEND: no Trillian server runs. The 500 is produced by a log client
// that answers with a plain, statusless error; the 503 by a client aimed at a
// loopback address nothing listens on.
package main

import (
	"net/http"
	"strings"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/trillian/ctfe/configpb"

	"ctverify/vhelp"
)

func main() {
	t := vhelp.New()

	// A statusless error maps to codes.Unknown and therefore to 500, the only
	// status masking touches.
	unmasked, err := setUp(t, pinnedConfig(t, &configpb.LogConfig{}), false, erroringClient{}, nil)
	if err != nil {
		t.Fatalf("set up unmasked instance: %v", err)
	}
	defer unmasked.stop()
	masked, err := setUp(t, pinnedConfig(t, &configpb.LogConfig{}), true, erroringClient{}, nil)
	if err != nil {
		t.Fatalf("set up masked instance: %v", err)
	}
	defer masked.stop()

	u := unmasked.get(t, ct.GetSTHPath)
	// What is graded is the §2.1 framing and that the backend's own text
	// survives inside it. Whether the handler wraps the failed call in a
	// diagnostic of its own is not stated by the corpus and is not pinned --
	// the same reasoning the add-chain case below already applies.
	t.Check("unmasked-500-carries-detail",
		u.status == http.StatusInternalServerError &&
			strings.HasPrefix(u.body, "Internal Server Error\n") &&
			strings.Contains(u.body, "backend exploded") &&
			strings.HasSuffix(u.body, "\n"),
		"status %d body %q", u.status, u.body)

	// The 500 really did come from a backend call, not from a short-circuit:
	// graded on the client's call counter, never on the error's rendering.
	t.Check("unmasked-500-followed-a-backend-call", unmasked.backendCalls() >= 1,
		"backend calls %d", unmasked.backendCalls())

	m := masked.get(t, ct.GetSTHPath)
	t.Check("masked-500-hides-detail",
		m.status == http.StatusInternalServerError && m.body == "Internal Server Error\n",
		"status %d body %q", m.status, m.body)

	t.Equal("masked-500-content-type", m.ctype, "text/plain; charset=utf-8")

	// The same masking applies to a submission that gets as far as the backend.
	mp := masked.post(t, ct.AddChainPath, chainJSON(t, "chain-valid.pem"))
	t.Check("masked-500-on-add-chain",
		mp.status == http.StatusInternalServerError && mp.body == "Internal Server Error\n",
		"status %d body %q", mp.status, mp.body)
	up := unmasked.post(t, ct.AddChainPath, chainJSON(t, "chain-valid.pem"))
	// What is graded is that the flag is off and the detail therefore
	// survives: the status text, the newline framing of HTTP-API.md section
	// 2.1, and the backend's own error inside it. The handler's internal
	// wrapper around a failed queue call is a diagnostic the corpus does not
	// state, so it is deliberately not pinned here.
	t.Check("unmasked-500-on-add-chain",
		up.status == http.StatusInternalServerError &&
			strings.HasPrefix(up.body, "Internal Server Error\n") &&
			strings.Contains(up.body, "backend exploded") &&
			strings.HasSuffix(up.body, "\n"),
		"status %d body %q", up.status, up.body)

	// 503 comes from a backend that is merely unreachable, and is never masked.
	mu, err := setUpDead(t, pinnedConfig(t, &configpb.LogConfig{}), true)
	if err != nil {
		t.Fatalf("set up unreachable/masked: %v", err)
	}
	defer mu.stop()
	b := mu.get(t, ct.GetSTHPath)
	// SCOPE-DIAGNOSTIC: the detail here is grpc-go's own rendering of an
	// Unavailable status, which the candidate does not write, so only the
	// §2.1 framing and the fact that a detail survives masking are graded.
	t.Check("masked-503-still-carries-detail",
		b.status == http.StatusServiceUnavailable &&
			strings.HasPrefix(b.body, "Service Unavailable\n") &&
			len(b.body) > len("Service Unavailable\n") &&
			strings.HasSuffix(b.body, "\n"),
		"status %d body %q", b.status, b.body)

	// 400 from a malformed submission keeps its detail under masking.
	//
	// SCOPE-DIAGNOSTIC: what is graded is the status, the section 2.1 frame,
	// and the one thing v1.1.0 adds to this body and to no other -- the log's
	// own diagnostic identity "<prefix>{<log_id>}: ", which HTTP-API.md
	// section 3.1 fixes and v1.0.21 does not emit at all. The sentence that
	// identity sits in front of is the product's wording, not contract, so it
	// is required to be non-empty and is not compared.
	c := masked.post(t, ct.AddChainPath, `{"chain":[]}`)
	const badReq, logID = "Bad Request\n", "bench{1001}: "
	t.Check("masked-400-carries-detail",
		c.status == http.StatusBadRequest &&
			strings.HasPrefix(c.body, badReq+logID) &&
			len(c.body) > len(badReq+logID)+1 &&
			strings.HasSuffix(c.body, "\n"),
		"status %d body %q", c.status, c.body)

	// 405 from the wrong method keeps its detail under masking.
	d := masked.post(t, ct.GetSTHPath, "{}")
	t.Check("masked-405-carries-detail",
		d.status == http.StatusMethodNotAllowed &&
			d.body == "Method Not Allowed\nmethod not allowed: POST\n",
		"status %d body %q", d.status, d.body)

	// A successful response, decided with no backend at all, is untouched.
	e := masked.get(t, ct.GetRootsPath)
	var roots ct.GetRootsResponse
	if e.status != http.StatusOK || jsonUnmarshal(e.body, &roots) != nil {
		t.Check("masked-get-roots-unaffected", false, "status %d body %q", e.status, e.body)
	} else {
		t.Check("masked-get-roots-unaffected", len(roots.Certificates) == 2,
			"served %d roots", len(roots.Certificates))
	}

	t.Done()
}

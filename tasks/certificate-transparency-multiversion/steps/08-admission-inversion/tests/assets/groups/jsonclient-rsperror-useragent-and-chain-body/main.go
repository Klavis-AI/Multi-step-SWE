// Group jsonclient-rsperror-useragent-and-chain-body.
//
// Three small pieces of v1.1.0's exported surface that no other group at this
// milestone touches, all of which a do-nothing continuation gets wrong:
//
//	jsonclient.RspError      moves here from client/ at v1.1.0, with
//	                         client.RspError left behind as a type *alias*
//	                         (CLIENT-API.md §1.1). An alias, not a defined
//	                         type: the two names must denote one type, so a
//	                         value produced through client/ type-asserts to
//	                         the jsonclient name and back.
//	jsonclient.Options.UserAgent  new at v1.1.0 (CLIENT-API.md §1.1), and sent
//	                         as the `User-Agent` request header on GET and on
//	                         POST alike.
//	ctfe.ParseBodyAsJSONChain  the read-and-decode half of the write handlers,
//	                         exported for the first time at v1.1.0 and losing
//	                         its per-log first parameter in the process
//	                         (CLIENT-API.md §8.3).
//
// SCOPE-NET: the server here is an httptest.Server on loopback inside this
// process; nothing leaves the machine. SCOPE-DIAGNOSTIC: no error *text* from
// the standard library or from the product is compared -- RspError is graded
// on its structured fields, and ParseBodyAsJSONChain on accept-versus-refuse.
package main

import (
	"context"
	"encoding/base64"
	"errors"
	"net/http"
	"net/http/httptest"
	"reflect"
	"strings"
	"sync"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/client"
	"github.com/google/certificate-transparency-go/jsonclient"
	"github.com/google/certificate-transparency-go/trillian/ctfe"

	"ctverify/vhelp"
)

// The alias, asserted at compile time. If client.RspError were redeclared as
// its own struct type rather than aliased, this file would not build, and the
// group would fail as a whole rather than case by case -- which is the right
// outcome, because every case below depends on it.
var _ client.RspError = jsonclient.RspError{}

const pinnedUA = "harbor-ct-bench/1 (step-08)"

// recorder is the pinned origin: it records the User-Agent of every request it
// sees and answers with a fixed status and body.
type recorder struct {
	mu     sync.Mutex
	agents []string
	status int
	body   string
}

func (rc *recorder) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	rc.mu.Lock()
	rc.agents = append(rc.agents, r.Header.Get("User-Agent"))
	rc.mu.Unlock()
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(rc.status)
	_, _ = w.Write([]byte(rc.body))
}

func (rc *recorder) seen() []string {
	rc.mu.Lock()
	defer rc.mu.Unlock()
	out := make([]string, len(rc.agents))
	copy(out, rc.agents)
	return out
}

// jsonChainRequest builds a POST whose body is what add-chain's body is.
func jsonChainRequest(body string) *http.Request {
	r, err := http.NewRequest(http.MethodPost, "http://localhost/ct/v1/add-chain",
		strings.NewReader(body))
	if err != nil {
		panic(err)
	}
	return r
}

func b64(b []byte) string { return base64.StdEncoding.EncodeToString(b) }

func main() {
	t := vhelp.New()
	ctx := context.Background()

	// --- jsonclient.RspError ------------------------------------------------

	// The alias again, at run time: one reflect.Type, reached by two names.
	t.Check("rsperror-client-name-is-an-alias-of-the-jsonclient-type",
		reflect.TypeOf(client.RspError{}) == reflect.TypeOf(jsonclient.RspError{}) &&
			reflect.TypeOf(jsonclient.RspError{}).PkgPath() ==
				"github.com/google/certificate-transparency-go/jsonclient",
		"client type %v (pkg %q), jsonclient type %v (pkg %q)",
		reflect.TypeOf(client.RspError{}), reflect.TypeOf(client.RspError{}).PkgPath(),
		reflect.TypeOf(jsonclient.RspError{}), reflect.TypeOf(jsonclient.RspError{}).PkgPath())

	// Error() reports the wrapped error and nothing else -- no status, no body
	// (CLIENT-API.md §1.1: the method exists to focus on the inner error).
	inner := errors.New("the-inner-error")
	re := jsonclient.RspError{Err: inner, StatusCode: http.StatusTeapot, Body: []byte("body-bytes")}
	t.Equal("rsperror-error-is-the-wrapped-error-only", re.Error(), "the-inner-error")

	// A real non-200 over a real socket: the failure a caller sees carries the
	// status and the body, not just a string.
	teapot := &recorder{status: http.StatusTeapot, body: `{"why":"short and stout"}`}
	ts := httptest.NewServer(teapot)
	defer ts.Close()

	lc, err := client.New(ts.URL, &http.Client{}, jsonclient.Options{UserAgent: pinnedUA})
	if err != nil {
		t.Fatalf("client.New against the pinned origin: %v", err)
	}
	_, gErr := lc.GetSTH(ctx)
	var got jsonclient.RspError
	asserted := errors.As(gErr, &got)
	t.Check("non-200-read-fails-with-an-rsperror",
		gErr != nil && asserted && got.StatusCode == http.StatusTeapot,
		"err=%v (%T) asserted=%v status=%d", gErr, gErr, asserted, got.StatusCode)
	t.Check("rsperror-body-carries-the-origin-bytes",
		asserted && string(got.Body) == `{"why":"short and stout"}`,
		"body=%q", string(got.Body))
	// And the same value satisfies the client/ spelling, because it is the
	// same type.
	var alias client.RspError
	t.Check("rsperror-also-asserts-through-the-client-alias",
		errors.As(gErr, &alias) && alias.StatusCode == http.StatusTeapot,
		"alias status=%d", alias.StatusCode)

	// --- jsonclient.Options.UserAgent ---------------------------------------

	t.Check("useragent-sent-on-a-get",
		len(teapot.seen()) == 1 && teapot.seen()[0] == pinnedUA,
		"agents seen: %q", teapot.seen())

	// The same header on the POST path, which sets it separately.
	_, _ = lc.AddChain(ctx, []ct.ASN1Cert{{Data: []byte{0x30, 0x00}}})
	seen := teapot.seen()
	t.Check("useragent-sent-on-a-post-too",
		len(seen) >= 2 && seen[len(seen)-1] == pinnedUA,
		"agents seen: %q", seen)

	// The control: with the option left empty the product sets no header of
	// its own, so whatever arrives is the standard library's default and is
	// not the pinned string. (What that default *is* is not graded -- it is
	// net/http's, not the product's.)
	plainOrigin := &recorder{status: http.StatusTeapot, body: "{}"}
	ps := httptest.NewServer(plainOrigin)
	defer ps.Close()
	pc, err := client.New(ps.URL, &http.Client{}, jsonclient.Options{})
	if err != nil {
		t.Fatalf("client.New with no UserAgent: %v", err)
	}
	_, _ = pc.GetSTH(ctx)
	t.Check("useragent-not-invented-when-the-option-is-empty",
		len(plainOrigin.seen()) == 1 && plainOrigin.seen()[0] != pinnedUA,
		"agents seen: %q", plainOrigin.seen())

	// --- ctfe.ParseBodyAsJSONChain ------------------------------------------
	// Exported, and takes the request alone: no per-log context parameter.
	// The signature is pinned by this file compiling at all.
	var _ func(*http.Request) (ct.AddChainRequest, error) = ctfe.ParseBodyAsJSONChain

	leaf := vhelp.ReadFixture("minted", "leaf-valid.der")
	intr := vhelp.ReadFixture("minted", "bench-intermediate.der")

	req, err := ctfe.ParseBodyAsJSONChain(
		jsonChainRequest(`{"chain":["` + b64(leaf) + `","` + b64(intr) + `"]}`))
	t.Check("parse-body-as-json-chain-accepts-a-two-element-chain",
		err == nil && len(req.Chain) == 2 &&
			string(req.Chain[0]) == string(leaf) && string(req.Chain[1]) == string(intr),
		"err=%v chain=%d", err, len(req.Chain))

	// Order is preserved exactly as submitted -- §5's RFC 6962 path rule
	// depends on it, so the decoder may not sort or dedupe.
	rev, err := ctfe.ParseBodyAsJSONChain(
		jsonChainRequest(`{"chain":["` + b64(intr) + `","` + b64(leaf) + `"]}`))
	t.Check("parse-body-as-json-chain-preserves-submission-order",
		err == nil && len(rev.Chain) == 2 && string(rev.Chain[0]) == string(intr),
		"err=%v chain=%d", err, len(rev.Chain))

	// An empty chain is refused even though it is valid JSON: the emptiness
	// test is the function's own, not the JSON decoder's.
	_, err = ctfe.ParseBodyAsJSONChain(jsonChainRequest(`{"chain":[]}`))
	t.Check("parse-body-as-json-chain-refuses-an-empty-chain", err != nil,
		"err=%v; want a refusal", err)

	// A body with no `chain` key at all lands in the same arm.
	_, err = ctfe.ParseBodyAsJSONChain(jsonChainRequest(`{}`))
	t.Check("parse-body-as-json-chain-refuses-a-missing-chain-key", err != nil,
		"err=%v; want a refusal", err)

	// Not JSON at all.
	_, err = ctfe.ParseBodyAsJSONChain(jsonChainRequest(`not json`))
	t.Check("parse-body-as-json-chain-refuses-a-non-json-body", err != nil,
		"err=%v; want a refusal", err)

	// Base64 that is not DER is *not* this function's problem: it decodes the
	// envelope and defers certificate validation to §5. A chain of one
	// two-byte non-certificate is accepted here and refused later.
	junk, err := ctfe.ParseBodyAsJSONChain(
		jsonChainRequest(`{"chain":["` + b64([]byte{0x30, 0x00}) + `"]}`))
	t.Check("parse-body-as-json-chain-does-not-validate-certificates",
		err == nil && len(junk.Chain) == 1 && len(junk.Chain[0]) == 2,
		"err=%v chain=%d", err, len(junk.Chain))

	// Unknown members are ignored rather than refused, so a client that sends
	// extra fields is not broken by the envelope decoder.
	extra, err := ctfe.ParseBodyAsJSONChain(
		jsonChainRequest(`{"chain":["` + b64(leaf) + `"],"unknown_member":42}`))
	t.Check("parse-body-as-json-chain-ignores-unknown-members",
		err == nil && len(extra.Chain) == 1,
		"err=%v chain=%d", err, len(extra.Chain))

	t.Done()
}

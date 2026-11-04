// Group ctfe-requestlog-hooks.
//
// v1.0.10 adds the exported ctfe.RequestLog interface, wires it into
// InstanceOptions and LogContext, and has AppHandler.ServeHTTP drive it around
// every request: Start before dispatch, LogPrefix with the configured log's
// prefix, and Status with the status actually served -- including the 405 the
// entrypoint's method check produces before any handler runs. Individual
// entrypoints add their own hooks; get-sth-consistency reports the range it
// parsed through FirstAndSecond.
//
// The value hooks -- StartAndEnd, LeafHash, TreeSize, LeafIndex,
// AddDERToChain -- report what an entrypoint parsed out of the request, and
// they fire as soon as the value is in hand, before the backend round trip and
// therefore whatever the backend then does. A request whose parameters do not
// parse reports no value hook at all, only the wrapper's frame. The last four
// cases hold the three read entrypoints to that rule on each of the parameter
// failures HTTP-API.md 4.4 and 4.5 document, and to the 400 those failures
// produce; the diagnostic text of each is not compared.
//
// The group observes the hooks through a driver-local implementation of the
// interface, so what is graded is the personality's calling behaviour, not the
// shipped logger's output. None of this exists at v1.0.5: there is no
// RequestLog type, no InstanceOptions.RequestLog field and no hook call.
package main

import (
	"context"
	"encoding/base64"
	"fmt"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"time"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/trillian/util"
	"github.com/google/certificate-transparency-go/x509"
	tcrypto "github.com/google/trillian/crypto"
	"github.com/google/trillian/monitoring"

	"ctverify/vhelp"
)

// recorder is a driver-local ctfe.RequestLog that records the calls it gets.
type recorder struct {
	events []string
}

func (r *recorder) add(format string, a ...interface{}) {
	r.events = append(r.events, fmt.Sprintf(format, a...))
}

func (r *recorder) Start(ctx context.Context) context.Context             { r.add("Start"); return ctx }
func (r *recorder) LogPrefix(_ context.Context, p string)                 { r.add("LogPrefix(%s)", p) }
func (r *recorder) AddDERToChain(_ context.Context, d []byte)             { r.add("AddDERToChain(%d)", len(d)) }
func (r *recorder) AddCertToChain(_ context.Context, c *x509.Certificate) { r.add("AddCertToChain") }
func (r *recorder) FirstAndSecond(_ context.Context, f, s int64) {
	r.add("FirstAndSecond(%d,%d)", f, s)
}
func (r *recorder) StartAndEnd(_ context.Context, s, e int64) { r.add("StartAndEnd(%d,%d)", s, e) }
func (r *recorder) LeafIndex(_ context.Context, li int64)     { r.add("LeafIndex(%d)", li) }
func (r *recorder) TreeSize(_ context.Context, ts int64)      { r.add("TreeSize(%d)", ts) }
func (r *recorder) LeafHash(_ context.Context, lh []byte)     { r.add("LeafHash(%d)", len(lh)) }
func (r *recorder) Status(_ context.Context, s int)           { r.add("Status(%d)", s) }

func build(t *vhelp.T, prefix string, rl ctfe.RequestLog) ctfe.PathHandlers {
	priv := loadPrivateKey(t)
	be := &backend{root: fixedRoot(pinnedNanos, pinnedTreeSize, "requestlog")}
	opts := ctfe.InstanceOptions{
		Deadline:      10 * time.Second,
		MetricFactory: monitoring.InertMetricFactory{},
		RequestLog:    rl,
	}
	lc := ctfe.NewLogContext(pinnedLogID, prefix, ctfe.CertValidationOpts{}, be,
		tcrypto.NewSHA256Signer(priv), opts, util.NewFixedTimeSource(pinnedClock))
	return lc.Handlers(prefix)
}

func serve(hs ctfe.PathHandlers, t *vhelp.T, path, method, query string) int {
	return serveBody(hs, t, path, method, query, nil)
}

func serveBody(hs ctfe.PathHandlers, t *vhelp.T, path, method, query string, body io.Reader) int {
	ap, ok := hs[path]
	if !ok {
		t.Fatalf("no handler registered at %s", path)
	}
	url := path
	if query != "" {
		url += "?" + query
	}
	w := httptest.NewRecorder()
	ap.ServeHTTP(w, httptest.NewRequest(method, url, body))
	return w.Code
}

// frame splits a recording into the wrapper's own frame and the entrypoint's
// value hooks. Every served request reports Start first, the log prefix second
// and exactly one Status last; everything between is what the entrypoint
// reported for the values it managed to parse. Returning the middle as a string
// lets a case name the hooks it expects without also pinning the status an
// unreachable backend happens to produce.
func frame(ev []string, wantPrefix string) (string, error) {
	if len(ev) < 3 {
		return "", fmt.Errorf("only %d hook calls: %v", len(ev), ev)
	}
	if ev[0] != "Start" {
		return "", fmt.Errorf("first hook call is %q, want Start: %v", ev[0], ev)
	}
	if ev[1] != wantPrefix {
		return "", fmt.Errorf("second hook call is %q, want %q: %v", ev[1], wantPrefix, ev)
	}
	if !strings.HasPrefix(ev[len(ev)-1], "Status(") {
		return "", fmt.Errorf("last hook call is %q, want a Status call: %v", ev[len(ev)-1], ev)
	}
	mid := ev[2 : len(ev)-1]
	for _, e := range mid {
		if strings.HasPrefix(e, "Status(") {
			return "", fmt.Errorf("Status reported more than once: %v", ev)
		}
	}
	return strings.Join(mid, " "), nil
}

// hooks runs one request and returns the value hooks it reported.
func hooks(t *vhelp.T, prefix, wantPrefix, path, method, query string, body io.Reader) (string, error) {
	rec := &recorder{}
	hs := build(t, prefix, rec)
	serveBody(hs, t, path, method, query, body)
	return frame(rec.events, wantPrefix)
}

func main() {
	t := vhelp.New()

	const prefix = "reqlog"
	wantPrefix := fmt.Sprintf("LogPrefix(%s{%d})", prefix, pinnedLogID)
	// The whole recording of a request rejected on its parameters: the
	// wrapper's frame and nothing between it, because no value was ever in
	// hand to report.
	frameOnly400 := "400|Start " + wantPrefix + " Status(400)"

	// 1 & 2: a served get-sth.
	rec := &recorder{}
	hs := build(t, prefix, rec)
	code := serve(hs, t, "/"+prefix+ct.GetSTHPath, http.MethodGet, "")
	t.Equal("get-sth-serves-200", code, 200)
	t.Equal("get-sth-hook-sequence", strings.Join(rec.events, " "),
		"Start "+wantPrefix+" Status(200)")

	// 3 & 4: the method check rejects before any handler runs, and still reports.
	rec2 := &recorder{}
	hs2 := build(t, prefix, rec2)
	code = serve(hs2, t, "/"+prefix+ct.GetSTHPath, http.MethodPost, "")
	t.Equal("wrong-method-serves-405", code, 405)
	t.Equal("wrong-method-hook-sequence", strings.Join(rec2.events, " "),
		"Start "+wantPrefix+" Status(405)")

	// 5: an entrypoint-specific hook. first=0 needs no backend.
	rec3 := &recorder{}
	hs3 := build(t, prefix, rec3)
	code = serve(hs3, t, "/"+prefix+ct.GetSTHConsistencyPath, http.MethodGet, "first=0&second=11")
	t.Equal("get-sth-consistency-hook-sequence", fmt.Sprintf("%d|%s", code, strings.Join(rec3.events, " ")),
		"200|Start "+wantPrefix+" FirstAndSecond(0,11) Status(200)")

	// 6: the implementation the release ships is itself usable.
	hs4 := build(t, "shipped", new(ctfe.DefaultRequestLog))
	code = serve(hs4, t, "/shipped"+ct.GetSTHPath, http.MethodGet, "")
	t.Equal("shipped-default-request-log-serves-200", code, 200)

	// 7..9: the value hooks of the three read entrypoints that take parameters.
	// The driver-local client refuses every read RPC, so whatever these requests
	// end up answering, the hooks below were all reported before the refusal:
	// that is the property under test.
	mid, err := hooks(t, prefix, wantPrefix, "/"+prefix+ct.GetEntriesPath, http.MethodGet, "start=3&end=7", nil)
	t.Check("get-entries-reports-parsed-range", err == nil && mid == "StartAndEnd(3,7)",
		"get-entries value hooks are %q (err %v), want \"StartAndEnd(3,7)\"", mid, err)

	leafHash := base64.StdEncoding.EncodeToString(make([]byte, 32))
	mid, err = hooks(t, prefix, wantPrefix, "/"+prefix+ct.GetProofByHashPath, http.MethodGet,
		"hash="+leafHash+"&tree_size=11", nil)
	t.Check("get-proof-by-hash-reports-hash-and-tree-size", err == nil && mid == "LeafHash(32) TreeSize(11)",
		"get-proof-by-hash value hooks are %q (err %v), want \"LeafHash(32) TreeSize(11)\"", mid, err)

	mid, err = hooks(t, prefix, wantPrefix, "/"+prefix+ct.GetEntryAndProofPath, http.MethodGet,
		"leaf_index=5&tree_size=11", nil)
	t.Check("get-entry-and-proof-reports-index-and-tree-size", err == nil && mid == "LeafIndex(5) TreeSize(11)",
		"get-entry-and-proof value hooks are %q (err %v), want \"LeafIndex(5) TreeSize(11)\"", mid, err)

	// 10: a range that does not parse reports no value hook, only the frame.
	rec5 := &recorder{}
	hs5 := build(t, prefix, rec5)
	code = serve(hs5, t, "/"+prefix+ct.GetEntriesPath, http.MethodGet, "start=9&end=4")
	t.Equal("malformed-range-reports-only-its-status",
		fmt.Sprintf("%d|%s", code, strings.Join(rec5.events, " ")),
		frameOnly400)

	// 11: one AddDERToChain per submitted element, in submission order, before
	// the chain is validated. Neither element is a certificate, so validation
	// rejects the request -- which is exactly why the DERs are reported first.
	body := strings.NewReader(`{"chain":["AQID","AQIDBAU="]}`)
	mid, err = hooks(t, prefix, wantPrefix, "/"+prefix+ct.AddChainPath, http.MethodPost, "", body)
	t.Check("add-chain-reports-each-submitted-der", err == nil && mid == "AddDERToChain(3) AddDERToChain(5)",
		"add-chain value hooks are %q (err %v), want \"AddDERToChain(3) AddDERToChain(5)\"", mid, err)

	// 12..15: the documented parameter failures of the two proof entrypoints.
	// HTTP-API.md 4.4 gives get-proof-by-hash three -- hash absent or empty,
	// hash present but not valid base64, tree_size absent, non-numeric or < 1
	// -- and 4.5 gives get-entry-and-proof's, of which tree_size must be > 0 is
	// the one a driver can reach without a backend. All four are decided before
	// the parsed value is in hand, so case 10's rule holds for each: the status
	// is 400 and the wrapper's frame is the whole recording, with no value hook
	// at all. The reasons themselves are diagnostic and are NOT compared; what
	// is graded is the status and the silence of the value hooks. None of these
	// requests reaches the backend, so the driver-local client never answers.
	badParam := func(id, path, query string) {
		rec := &recorder{}
		hs := build(t, prefix, rec)
		code := serve(hs, t, "/"+prefix+path, http.MethodGet, query)
		t.Equal(id, fmt.Sprintf("%d|%s", code, strings.Join(rec.events, " ")), frameOnly400)
	}
	badParam("get-proof-by-hash-missing-hash-is-400",
		ct.GetProofByHashPath, "tree_size=11")
	badParam("get-proof-by-hash-unparsable-hash-is-400",
		ct.GetProofByHashPath, "hash=%21%21%21&tree_size=11")
	badParam("get-proof-by-hash-tree-size-below-one-is-400",
		ct.GetProofByHashPath, "hash="+leafHash+"&tree_size=0")
	badParam("get-entry-and-proof-tree-size-below-one-is-400",
		ct.GetEntryAndProofPath, "leaf_index=0&tree_size=0")

	t.Done()
}

// Group ctfe-requestlog-issue-sct.
//
// v1.0.19 adds one method to the exported ctfe.RequestLog interface:
//
//	// IssueSCT will be called once when the server is about to issue an SCT
//	// to a client. This should not be called if the submission process fails
//	// before an SCT could be presented to a client ... The SCT bytes will be
//	// in TLS serialized format.
//	IssueSCT(context.Context, []byte)
//
// RequestLog is an interface a log operator implements, so it is graded the
// way an operator would exercise it: this driver supplies its own
// implementation to the exported ctfe.SetUpInstance, serves the returned
// handler set over a real loopback socket, and drives it with ordinary HTTP.
// Every clause of the doc comment above is then checked -- called once, with
// TLS-serialised bytes, and NOT called when the submission fails or when the
// request was never a submission.
//
// The "TLS serialized format" clause is graded by exact bytes: the SCT the
// client received is rebuilt from the JSON add-chain response exactly as
// client.LogClient does, re-marshalled with the candidate's own tls package,
// and compared byte for byte with what IssueSCT was handed.
//
// The last case pins the call order for one whole submission. The interface's
// contract is not just "called" but "called when the server is about to issue
// an SCT", i.e. after the chain has been logged and before the response goes
// out; an implementation that called it early, or after Status, would satisfy
// a bare counter but not this.
//
// SCOPE-BACKEND: this group's own pinned TrillianLogServer on a :0 loopback
// port. SCOPE-TIME: the SCT timestamp is compared only against itself, never
// against a clock.
//
// The v1.0.17 RequestLog has no IssueSCT, so nothing ever calls the method
// this driver implements and the counters stay at zero.
package main

import (
	"context"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"net"
	"net/http"
	"strings"
	"sync"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

// recorder is this group's own ctfe.RequestLog. It records the sequence of
// callbacks and every SCT it was handed.
type recorder struct {
	mu   sync.Mutex
	seq  []string
	scts [][]byte
}

func (r *recorder) note(what string) {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.seq = append(r.seq, what)
}

func (r *recorder) snapshot() ([]string, [][]byte) {
	r.mu.Lock()
	defer r.mu.Unlock()
	return append([]string(nil), r.seq...), append([][]byte(nil), r.scts...)
}

func (r *recorder) reset() {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.seq = nil
}

func (r *recorder) Start(ctx context.Context) context.Context { r.note("Start"); return ctx }
func (r *recorder) LogPrefix(context.Context, string)         { r.note("LogPrefix") }
func (r *recorder) AddDERToChain(context.Context, []byte)     { r.note("AddDERToChain") }
func (r *recorder) AddCertToChain(context.Context, *x509.Certificate) {
	r.note("AddCertToChain")
}
func (r *recorder) FirstAndSecond(context.Context, int64, int64) { r.note("FirstAndSecond") }
func (r *recorder) StartAndEnd(context.Context, int64, int64)    { r.note("StartAndEnd") }
func (r *recorder) LeafIndex(context.Context, int64)             { r.note("LeafIndex") }
func (r *recorder) TreeSize(context.Context, int64)              { r.note("TreeSize") }
func (r *recorder) LeafHash(context.Context, []byte)             { r.note("LeafHash") }
func (r *recorder) Status(_ context.Context, s int)              { r.note(fmt.Sprintf("Status(%d)", s)) }

// IssueSCT is the method this release adds.
func (r *recorder) IssueSCT(_ context.Context, sct []byte) {
	r.mu.Lock()
	r.scts = append(r.scts, append([]byte(nil), sct...))
	r.mu.Unlock()
	r.note("IssueSCT")
}

// sctBytesFromResponse rebuilds the SCT from an add-chain response exactly as
// client.LogClient does, and returns its TLS serialisation.
func sctBytesFromResponse(t *vhelp.T, body []byte) []byte {
	var resp ct.AddChainResponse
	if err := json.Unmarshal(body, &resp); err != nil {
		t.Fatalf("decode add-chain response: %v (body %.200s)", err, body)
	}
	var ds ct.DigitallySigned
	if rest, err := tls.Unmarshal(resp.Signature, &ds); err != nil || len(rest) > 0 {
		t.Fatalf("decode DigitallySigned: %v (%d trailing bytes)", err, len(rest))
	}
	exts, err := base64.StdEncoding.DecodeString(resp.Extensions)
	if err != nil {
		t.Fatalf("decode extensions %q: %v", resp.Extensions, err)
	}
	var logID ct.LogID
	copy(logID.KeyID[:], resp.ID)
	b, err := tls.Marshal(ct.SignedCertificateTimestamp{
		SCTVersion: resp.SCTVersion,
		LogID:      logID,
		Timestamp:  resp.Timestamp,
		Extensions: ct.CTExtensions(exts),
		Signature:  ds,
	})
	if err != nil {
		t.Fatalf("re-marshal SCT: %v", err)
	}
	return b
}

func chainJSON(t *vhelp.T, rel ...[]string) []byte {
	chain := make([][]byte, 0, len(rel))
	for _, r := range rel {
		chain = append(chain, vhelp.ReadFixture(r...))
	}
	b, err := json.Marshal(ct.AddChainRequest{Chain: chain})
	if err != nil {
		t.Fatalf("marshal add-chain request: %v", err)
	}
	return b
}

func main() {
	t := vhelp.New()

	_, gs, backendClient := startBackend(t)
	defer gs.Stop()

	rl := &recorder{}
	handlers := setUpPinnedInstance(t, backendClient, ctfe.InstanceOptions{RequestLog: rl})

	mux := http.NewServeMux()
	for path, handler := range *handlers {
		mux.Handle(path, handler)
	}
	lis, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen for the log's HTTP surface: %v", err)
	}
	srv := &http.Server{Handler: mux}
	go func() { _ = srv.Serve(lis) }()
	defer func() { _ = srv.Close() }()
	base := "http://" + lis.Addr().String() + "/" + logPrefix

	post := func(path string, body []byte) (int, []byte) {
		req, err := http.NewRequest(http.MethodPost, base+path, strings.NewReader(string(body)))
		if err != nil {
			t.Fatalf("build POST %s: %v", path, err)
		}
		req.Header.Set("Content-Type", "application/json")
		resp, err := httpClient.Do(req)
		if err != nil {
			t.Fatalf("POST %s: %v", path, err)
		}
		defer func() { _ = resp.Body.Close() }()
		return resp.StatusCode, readAll(resp)
	}

	// --- 1. one submission, one IssueSCT ------------------------------------
	rl.reset()
	status, body := post("/ct/v1/add-chain", chainJSON(t,
		[]string{"minted", "leaf-valid.der"}, []string{"minted", "bench-intermediate.der"}))
	seq, scts := rl.snapshot()
	t.Equal("add-chain-calls-issue-sct-exactly-once",
		fmt.Sprintf("status=%d issued=%d", status, len(scts)),
		fmt.Sprintf("status=%d issued=%d", 200, 1))

	// --- 2. with the TLS serialisation of the SCT the client received -------
	if len(scts) == 1 && status == 200 {
		t.EqualBytes("issued-bytes-are-the-tls-serialised-sct", scts[0], sctBytesFromResponse(t, body))
	} else {
		t.Check("issued-bytes-are-the-tls-serialised-sct", false,
			"no single SCT to compare: status=%d issued=%d", status, len(scts))
	}

	// --- 3. and the ordering the interface documents ------------------------
	// Start, the prefix, the raw DERs, the verified chain, then the SCT, and
	// only then the response status.
	t.Equal("issue-sct-is-called-after-logging-and-before-status",
		strings.Join(seq, ","),
		strings.Join([]string{
			"Start", "LogPrefix",
			"AddDERToChain", "AddDERToChain",
			"AddCertToChain", "AddCertToChain",
			"IssueSCT", "Status(200)",
		}, ","))

	// --- 4. add-pre-chain issues its own -------------------------------------
	status, body = post("/ct/v1/add-pre-chain", chainJSON(t,
		[]string{"minted", "precert-poisoned.der"}, []string{"minted", "bench-intermediate.der"}))
	_, scts = rl.snapshot()
	if status == 200 && len(scts) == 2 {
		t.EqualBytes("add-pre-chain-issues-its-own-sct", scts[1], sctBytesFromResponse(t, body))
	} else {
		t.Check("add-pre-chain-issues-its-own-sct", false,
			"status=%d issued=%d, want 200 and 2", status, len(scts))
	}

	// --- 5. a refused submission issues nothing ------------------------------
	// The same case also proves the recorder is live for that request -- its
	// DER callbacks fire -- so "IssueSCT was not called" is an observation,
	// not a side effect of the handler never running.
	rl.reset()
	status, _ = post("/ct/v1/add-chain", chainJSON(t,
		[]string{"minted", "leaf-unrooted.der"}, []string{"minted", "rogue-root.der"}))
	seq, scts = rl.snapshot()
	ders := 0
	for _, s := range seq {
		if s == "AddDERToChain" {
			ders++
		}
	}
	t.Equal("refused-submission-issues-no-sct",
		fmt.Sprintf("status=%d issued=%d ders=%d", status, len(scts), ders),
		fmt.Sprintf("status=%d issued=%d ders=%d", 400, 2, 2))

	// --- 6. and neither does a read ------------------------------------------
	resp, err := httpClient.Get(base + "/ct/v1/get-sth")
	if err != nil {
		t.Fatalf("GET get-sth: %v", err)
	}
	status = resp.StatusCode
	_ = resp.Body.Close()
	_, scts = rl.snapshot()
	t.Equal("read-endpoint-issues-no-sct",
		fmt.Sprintf("status=%d issued=%d", status, len(scts)),
		fmt.Sprintf("status=%d issued=%d", 200, 2))

	t.Done()
}

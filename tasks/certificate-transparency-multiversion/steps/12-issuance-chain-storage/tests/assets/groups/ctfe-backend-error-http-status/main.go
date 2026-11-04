// ctfe-backend-error-http-status observes how a log translates the gRPC
// status its backend returned into the HTTP status a submitter sees. This
// release re-cuts two entries of that table: a backend that is out of quota
// is now 429 Too Many Requests rather than 403 Forbidden, and a cancelled or
// timed-out backend call is now 504 Gateway Timeout rather than 408 Request
// Timeout. The rest of the table must not move, so the unchanged rows are
// asserted alongside the changed ones.
//
// The log is configured with nothing this release added: same flags, same
// config schema as the previous milestone. The only thing that decides this
// group is the status mapping itself.
package main

import (
	"encoding/base64"
	"encoding/json"
	"net/http"
	"strings"

	"github.com/google/certificate-transparency-go/x509util"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	"ctverify/vhelp"
)

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

func main() {
	t := vhelp.New()

	privDER := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-key.pem"))
	chain := derList(t, "minted", "chain-valid.pem")
	addChainBody, err := json.Marshal(map[string]interface{}{"chain": []string{
		base64.StdEncoding.EncodeToString(chain[0]),
		base64.StdEncoding.EncodeToString(chain[1]),
	}})
	if err != nil {
		t.Fatalf("marshal add-chain body: %v", err)
	}

	be := newStub()
	h := startPersonality(t, be, []logSpec{{id: pinnedLogID, prefix: logPrefix, privKey: privDER}}, nil)
	defer h.stop()

	// addChain: the submitter-visible half of the table.
	queue := func(id string, code codes.Code, want int) {
		be.failWith("QueueLeaf", status.Error(code, "pinned backend failure"))
		r := h.postJSON(t, h.url("/ct/v1/add-chain"), addChainBody)
		t.Check(id, r.status == want, "add-chain with backend %v = HTTP %d, want %d", code, r.status, want)
	}
	// get-sth: the read half, which goes through a different handler.
	sth := func(id string, code codes.Code, want int) {
		be.failWith("GetLatestSignedLogRoot", status.Error(code, "pinned backend failure"))
		r := h.get(t, h.url("/ct/v1/get-sth"))
		t.Check(id, r.status == want, "get-sth with backend %v = HTTP %d, want %d", code, r.status, want)
	}

	// The two rows this release re-cuts.
	queue("add-chain-resource-exhausted-is-429", codes.ResourceExhausted, http.StatusTooManyRequests)
	queue("add-chain-canceled-is-504", codes.Canceled, http.StatusGatewayTimeout)
	queue("add-chain-deadline-exceeded-is-504", codes.DeadlineExceeded, http.StatusGatewayTimeout)
	sth("get-sth-resource-exhausted-is-429", codes.ResourceExhausted, http.StatusTooManyRequests)
	sth("get-sth-deadline-exceeded-is-504", codes.DeadlineExceeded, http.StatusGatewayTimeout)

	// Rows that must not have moved. PermissionDenied shared a row with
	// ResourceExhausted before; it keeps 403 on its own now.
	queue("add-chain-permission-denied-is-still-403", codes.PermissionDenied, http.StatusForbidden)
	sth("get-sth-unavailable-is-still-503", codes.Unavailable, http.StatusServiceUnavailable)
	sth("get-sth-not-found-is-still-404", codes.NotFound, http.StatusNotFound)
	sth("get-sth-aborted-is-still-409", codes.Aborted, http.StatusConflict)
	sth("get-sth-unauthenticated-is-still-401", codes.Unauthenticated, http.StatusUnauthorized)

	// get-entries reaches the same table through a third handler.
	be.failWith("GetLeavesByRange", status.Error(codes.ResourceExhausted, "pinned backend failure"))
	r := h.get(t, h.url("/ct/v1/get-entries?start=0&end=0"))
	t.Check("get-entries-resource-exhausted-is-429", r.status == http.StatusTooManyRequests,
		"get-entries with an exhausted backend = HTTP %d, want 429", r.status)

	// The status is half the contract: both remapped rows must still carry the
	// ordinary error body of HTTP-API.md section 2.1 -- the status text, a
	// newline, the error, a trailing newline -- and not a bare status line or
	// an empty response. Asserted on its own so a personality that moved the
	// number without moving the body with it is caught here.
	okBody := func(res httpResult, want int) bool {
		b := string(res.body)
		return res.status == want &&
			strings.HasPrefix(b, http.StatusText(want)+"\n") &&
			strings.HasSuffix(b, "\n") &&
			len(b) > len(http.StatusText(want))+2
	}
	be.failWith("QueueLeaf", status.Error(codes.ResourceExhausted, "pinned backend failure"))
	r429 := h.postJSON(t, h.url("/ct/v1/add-chain"), addChainBody)
	be.failWith("GetLatestSignedLogRoot", status.Error(codes.DeadlineExceeded, "pinned backend failure"))
	r504 := h.get(t, h.url("/ct/v1/get-sth"))
	t.Check("remapped-statuses-carry-the-standard-error-body",
		okBody(r429, http.StatusTooManyRequests) && okBody(r504, http.StatusGatewayTimeout),
		"429 body %q, 504 body %q, want \"<status text>\\n<error>\\n\"", r429.body, r504.body)

	t.Done()
}

// Group inherited-jsonclient-retry.
//
// The jsonclient retry arm, inherited and not re-graded anywhere else in this
// step. PostAndParseWithRetry's state machine is specified release by release
// in CLIENT-API.md section 1.1, "Retry and backoff"; the 429 row of that table
// is step 10's milestone ("falls to the default arm up to v1.1.2; from v1.1.3,
// identical to 503"), and the default-arm row is step 07's (RspError moves
// into jsonclient at v1.1.0). This release moves log lists, not the
// transport: jsonclient/client.go's retry loop and jsonclient/backoff.go are
// byte-identical at v1.1.3 and v1.1.4, so every case below must pass at both
// anchors and none of them is satisfiable by this step's new capability.
//
// SCOPE-TIME: no case asserts an elapsed time, a duration or an ordering in
// wall-clock terms. What is asserted is how many times the server was asked,
// which is what "retry" and "do not retry" actually mean. Timing is kept out
// of the answers as well as out of the assertions: every backoff this group
// provokes is driven to zero through Retry-After, either as the integer 0 or
// as an RFC 1123 date in the past, so the group cannot be slow and cannot be
// flaky for the same reason. The one jitter the product adds itself is
// bounded by jsonclient's own maxJitter and is never measured.
//
// The server is httptest on loopback, which binds its own listener and serves
// on that same listener, so there is no port to guess and no race to lose.
// Each case gets a fresh server and a fresh JSONClient: backoff state is
// per-client, and "retry immediately" is only immediate on a client that has
// not already backed off.
package main

import (
	"context"
	"errors"
	"fmt"
	"net/http"
	"net/http/httptest"
	"sync/atomic"

	"github.com/google/certificate-transparency-go/jsonclient"

	"ctverify/vhelp"
)

// pastRFC1123 is a fixed date in the past in the layout RFC 7231 section
// 7.1.3 allows for Retry-After. Pinned rather than computed so the answer is
// visible without running anything: time.Until of it is negative at every
// clock this will ever run on, so the honoured backoff is no backoff.
const pastRFC1123 = "Mon, 02 Jan 2006 15:04:05 GMT"

// quiet is the one-method Logger of CLIENT-API.md section 1.1. The retry arm
// logs on every backoff; sending that to a sink rather than to the standard
// library's default keeps the group's stderr clean. The wording of those
// lines is the implementation's and is deliberately not asserted.
type quiet struct{}

func (quiet) Printf(string, ...interface{}) {}

// reply is one scripted server answer.
type reply struct {
	status     int
	retryAfter string // set as the Retry-After header when non-empty
	body       string
}

// scripted serves replies in order, repeating the last one forever, and
// counts every request that actually reached it.
func scripted(replies ...reply) (*httptest.Server, *int32) {
	var n int32
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		i := int(atomic.AddInt32(&n, 1)) - 1
		if i >= len(replies) {
			i = len(replies) - 1
		}
		rep := replies[i]
		if rep.retryAfter != "" {
			w.Header().Set("Retry-After", rep.retryAfter)
		}
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(rep.status)
		fmt.Fprint(w, rep.body)
	}))
	return srv, &n
}

func mustClient(t *vhelp.T, uri string) *jsonclient.JSONClient {
	// A nil *http.Client is the documented default; no public key is supplied,
	// so signatures are not verified.
	c, err := jsonclient.New(uri, nil, jsonclient.Options{Logger: quiet{}})
	if err != nil {
		t.Fatalf("jsonclient.New(%q): %v", uri, err)
	}
	return c
}

type answer struct {
	Ok string `json:"ok"`
}

const okBody = `{"ok":"yes"}`

// post drives one scripted exchange and hands back everything the contract
// says something about.
func post(t *vhelp.T, ctx context.Context, replies ...reply) (*http.Response, []byte, error, *answer, int32) {
	srv, n := scripted(replies...)
	defer srv.Close()
	c := mustClient(t, srv.URL)
	var got answer
	rsp, body, err := c.PostAndParseWithRetry(ctx, "/ct/v1/add-chain", struct{}{}, &got)
	return rsp, body, err, &got, atomic.LoadInt32(n)
}

func main() {
	t := vhelp.New()
	ctx := context.Background()

	// --- 1. the 200 row: return -------------------------------------------
	// The control. Without it every "attempts == 2" below could be satisfied
	// by a client that simply always asks twice.
	rsp, body, err, got, n := post(t, ctx, reply{status: 200, body: okBody})
	t.Check("plain-200-returns-without-retrying",
		err == nil && n == 1 && rsp != nil && rsp.StatusCode == 200 &&
			string(body) == okBody && got.Ok == "yes",
		"err=%v attempts=%d rsp=%v body=%q decoded=%+v", err, n, rsp, body, got)

	// --- 2. the 408 row: retry immediately, no backoff ---------------------
	_, _, err, got, n = post(t, ctx,
		reply{status: http.StatusRequestTimeout, body: `{}`},
		reply{status: 200, body: okBody})
	t.Check("408-request-timeout-is-retried",
		err == nil && n == 2 && got.Ok == "yes",
		"err=%v attempts=%d decoded=%+v", err, n, got)

	// --- 3. the 503 row: back off, honouring Retry-After -------------------
	// Retry-After: 0 is honoured as zero seconds, so the retry is prompt; that
	// the header was honoured at all is what this case shows, not how long it
	// took.
	_, _, err, got, n = post(t, ctx,
		reply{status: http.StatusServiceUnavailable, retryAfter: "0", body: `{}`},
		reply{status: 200, body: okBody})
	t.Check("503-service-unavailable-is-retried",
		err == nil && n == 2 && got.Ok == "yes",
		"err=%v attempts=%d decoded=%+v", err, n, got)

	// --- 4. Retry-After parsed as an RFC 1123 date -------------------------
	// "parsed first as an integer number of seconds and, failing that, as an
	// RFC 1123 date". A date is the second branch; a past one resolves to a
	// backoff already over.
	_, _, err, got, n = post(t, ctx,
		reply{status: http.StatusServiceUnavailable, retryAfter: pastRFC1123, body: `{}`},
		reply{status: 200, body: okBody})
	t.Check("retry-after-is-parsed-as-an-rfc1123-date",
		err == nil && n == 2 && got.Ok == "yes",
		"err=%v attempts=%d decoded=%+v", err, n, got)

	// --- 5. the 429 row, from v1.1.3: identical to 503 ---------------------
	// Step 10's milestone. Up to v1.1.2 a 429 fell to the default arm and came
	// straight back as an error after one attempt, so this case distinguishes
	// v1.1.4 from v1.1.2 as well as pinning it.
	_, _, err, got, n = post(t, ctx,
		reply{status: http.StatusTooManyRequests, retryAfter: "0", body: `{}`},
		reply{status: 200, body: okBody})
	t.Check("429-too-many-requests-is-retried-like-503",
		err == nil && n == 2 && got.Ok == "yes",
		"err=%v attempts=%d decoded=%+v", err, n, got)

	// --- 6. the default row: return an error, and do not retry -------------
	const badBody = `{"error":"nope"}`
	rsp, body, err, got, n = post(t, ctx,
		reply{status: http.StatusBadRequest, body: badBody},
		reply{status: 200, body: okBody})
	t.Check("non-retriable-status-is-not-retried",
		err != nil && n == 1 && got.Ok == "",
		"err=%v attempts=%d decoded=%+v", err, n, got)

	// --- 7. ... as an RspError, with a nil response beside it --------------
	// From v1.1.0 the arm builds the RspError itself and the *http.Response
	// returned beside it is nil again. The error text is quoted by the
	// contract; note the lower-case "status", which is what this arm has said
	// from v1.1.0 onward -- PostAndParse's own non-2xx path still says
	// "Status" with a capital S, and the two are not the same string.
	var rspErr jsonclient.RspError
	t.Check("non-retriable-status-returns-an-rsperror",
		errors.As(err, &rspErr) &&
			rspErr.StatusCode == http.StatusBadRequest &&
			string(rspErr.Body) == badBody &&
			rspErr.Error() == `got HTTP status "400 Bad Request"` &&
			rsp == nil && body == nil,
		"err=%#v rsp=%v body=%q", err, rsp, body)

	// --- 8. context errors are never retried -------------------------------
	// "context.Canceled and context.DeadlineExceeded are never retried."
	// ctxhttp hands the bare sentinel back, which is what the loop's guard
	// compares against, so the call returns rather than backing off. The
	// request never leaves the client, so the server is never asked: that is
	// the observable difference between not retrying and retrying zero times.
	cancelled, cancel := context.WithCancel(context.Background())
	cancel()
	rsp, body, err, _, n = post(t, cancelled, reply{status: 200, body: okBody})
	t.Check("cancelled-context-is-not-retried",
		errors.Is(err, context.Canceled) && n == 0 && rsp == nil && body == nil,
		"err=%v attempts=%d rsp=%v body=%q", err, n, rsp, body)

	t.Done()
}

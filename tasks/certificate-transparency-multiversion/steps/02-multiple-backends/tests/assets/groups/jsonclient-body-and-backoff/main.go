// Group jsonclient-body-and-backoff.
//
// v1.0.5 changes the jsonclient contract in two ways that every caller sees.
//
// First, GetAndParse, PostAndParse and PostAndParseWithRetry now return the
// response body alongside the response: the body is read eagerly so the
// connection can be reused, and it is handed back even when the status is not
// OK or the JSON fails to parse. Before this release the body was consumed by
// a deferred ReadAll and discarded, so a caller could not report what a log
// actually said when it refused a submission.
//
// Second, the retry policy moves out of a local interval variable into a
// shared backoff object: the interval grows with a multiplier, the Retry-After
// header of a 503 is honoured in both of its documented spellings, a status
// that is not 200/408/503 -- 429 among them, until v1.1.3 -- falls straight
// out with the body, and a dead context is never retried
// (CLIENT-API.md section 1.1, "Retry and backoff").
//
// Everything is driven against a stdlib HTTP server on a loopback port
// obtained by binding :0. No assertion reads a clock for a timestamp; the one
// assertion that looks at duration compares the intervals between requests the
// server itself saw, and only as a lower bound and an ordering, never as an
// upper bound, so it cannot fail on a slow or loaded machine.
//
// Three documented parts of the backoff are deliberately not graded here, and
// no group grades them: the 250 ms jitter (randomised, so only a statistical
// assertion could see it), the 128 s ceiling (eight consecutive failures, over
// four minutes of waiting, to reach it) and decreaseMultiplier (unexported,
// and the product never calls it at any release in this package's range -- it
// is exercised only by upstream's own unit test).
package main

import (
	"context"
	"errors"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/google/certificate-transparency-go/jsonclient"

	"ctverify/vhelp"
)

type payload struct {
	Answer int    `json:"answer"`
	Name   string `json:"name"`
}

const okBody = `{"answer":42,"name":"harbor"}`

// serve starts a loopback server whose handler is chosen per request by the
// caller-supplied function, and counts the requests it saw.
func serve(h func(w http.ResponseWriter, r *http.Request, n int32)) (*httptest.Server, *int32, *string) {
	var count int32
	query := ""
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		n := atomic.AddInt32(&count, 1)
		query = r.URL.RawQuery
		h(w, r, n)
	}))
	return ts, &count, &query
}

// serveTimed is serve plus the arrival time of every request, so an assertion
// can compare the intervals the server itself observed. Only lower bounds and
// orderings are ever asserted over these.
func serveTimed(h func(w http.ResponseWriter, r *http.Request, n int32)) (*httptest.Server, func() []time.Time) {
	var mu sync.Mutex
	var seen []time.Time
	var count int32
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		mu.Lock()
		seen = append(seen, time.Now())
		mu.Unlock()
		h(w, r, atomic.AddInt32(&count, 1))
	}))
	return ts, func() []time.Time {
		mu.Lock()
		defer mu.Unlock()
		out := make([]time.Time, len(seen))
		copy(out, seen)
		return out
	}
}

func client(t *vhelp.T, uri string) *jsonclient.JSONClient {
	c, err := jsonclient.New(uri, nil, jsonclient.Options{})
	if err != nil {
		t.Fatalf("jsonclient.New: %v", err)
	}
	return c
}

func main() {
	t := vhelp.New()
	ctx := context.Background()

	// --- GET: body on success, and the parameters actually sent. -----------
	ts, _, query := serve(func(w http.ResponseWriter, r *http.Request, _ int32) {
		w.Header().Set("Content-Type", "application/json")
		w.Write([]byte(okBody))
	})
	var got payload
	rsp, body, err := client(t, ts.URL).GetAndParse(ctx, "/ok", map[string]string{"first": "1", "second": "2"}, &got)
	if err != nil {
		t.Check("get-and-parse-returns-the-body-and-the-parsed-value", false, "GetAndParse: %v", err)
		t.Check("get-and-parse-url-encodes-the-parameters", false, "GetAndParse: %v", err)
	} else {
		t.Equal("get-and-parse-returns-the-body-and-the-parsed-value",
			string(body)+"|"+itoa(int64(rsp.StatusCode))+"|"+itoa(int64(got.Answer))+"|"+got.Name,
			okBody+"|200|42|harbor")
		t.Equal("get-and-parse-url-encodes-the-parameters", *query, "first=1&second=2")
	}
	ts.Close()

	// --- GET: the body is returned with a refusal, not swallowed. ----------
	ts, _, _ = serve(func(w http.ResponseWriter, r *http.Request, _ int32) {
		w.WriteHeader(http.StatusForbidden)
		w.Write([]byte("Forbidden\nthis log does not accept your chain"))
	})
	rsp, body, err = client(t, ts.URL).GetAndParse(ctx, "/deny", nil, &got)
	t.Check("get-and-parse-reports-a-refusal", err != nil && rsp != nil && rsp.StatusCode == http.StatusForbidden,
		"err=%v rsp=%v", err, rsp)
	t.Equal("get-and-parse-returns-the-body-of-a-refusal", string(body),
		"Forbidden\nthis log does not accept your chain")
	ts.Close()

	// --- GET: the body is returned when it is not the expected JSON. -------
	ts, _, _ = serve(func(w http.ResponseWriter, r *http.Request, _ int32) {
		w.Write([]byte("<html>not json at all</html>"))
	})
	_, body, err = client(t, ts.URL).GetAndParse(ctx, "/html", nil, &got)
	t.Check("get-and-parse-reports-unparseable-json", err != nil, "unparseable body was accepted")
	t.Equal("get-and-parse-returns-an-unparseable-body", string(body), "<html>not json at all</html>")
	ts.Close()

	// --- POST: body on success and on refusal. -----------------------------
	ts, _, _ = serve(func(w http.ResponseWriter, r *http.Request, _ int32) {
		w.Write([]byte(okBody))
	})
	got = payload{}
	_, body, err = client(t, ts.URL).PostAndParse(ctx, "/ok", map[string]string{"k": "v"}, &got)
	if err != nil {
		t.Check("post-and-parse-returns-the-body-and-the-parsed-value", false, "PostAndParse: %v", err)
	} else {
		t.Equal("post-and-parse-returns-the-body-and-the-parsed-value",
			string(body)+"|"+itoa(int64(got.Answer)), okBody+"|42")
	}
	ts.Close()

	ts, _, _ = serve(func(w http.ResponseWriter, r *http.Request, _ int32) {
		w.WriteHeader(http.StatusBadRequest)
		w.Write([]byte("Bad Request\nchain is not rooted"))
	})
	rsp, body, _ = client(t, ts.URL).PostAndParse(ctx, "/bad", map[string]string{}, &got)
	t.Equal("post-and-parse-returns-the-body-of-a-rejected-submission",
		itoa(int64(rsp.StatusCode))+"|"+string(body), "400|Bad Request\nchain is not rooted")
	ts.Close()

	// --- retry: 408 is retried immediately. --------------------------------
	ts, count, _ := serve(func(w http.ResponseWriter, r *http.Request, n int32) {
		if n == 1 {
			w.WriteHeader(http.StatusRequestTimeout)
			return
		}
		w.Write([]byte(okBody))
	})
	got = payload{}
	rsp, body, err = client(t, ts.URL).PostAndParseWithRetry(ctx, "/timeout", map[string]string{}, &got)
	if err != nil {
		t.Check("retry-repeats-a-request-timeout", false, "PostAndParseWithRetry: %v", err)
	} else {
		t.Equal("retry-repeats-a-request-timeout",
			itoa(int64(atomic.LoadInt32(count)))+"|"+itoa(int64(rsp.StatusCode))+"|"+string(body),
			"2|200|"+okBody)
	}
	ts.Close()

	// --- retry: a 503 with Retry-After is retried after that interval. -----
	ts, count, _ = serve(func(w http.ResponseWriter, r *http.Request, n int32) {
		if n == 1 {
			w.Header().Set("Retry-After", "1")
			w.WriteHeader(http.StatusServiceUnavailable)
			return
		}
		w.Write([]byte(okBody))
	})
	got = payload{}
	rsp, _, err = client(t, ts.URL).PostAndParseWithRetry(ctx, "/unavailable", map[string]string{}, &got)
	if err != nil {
		t.Check("retry-honours-retry-after-and-then-succeeds", false, "PostAndParseWithRetry: %v", err)
	} else {
		t.Equal("retry-honours-retry-after-and-then-succeeds",
			itoa(int64(atomic.LoadInt32(count)))+"|"+itoa(int64(rsp.StatusCode))+"|"+itoa(int64(got.Answer)),
			"2|200|42")
	}
	ts.Close()

	// --- retry: a status that is not retriable is reported at once. --------
	ts, count, _ = serve(func(w http.ResponseWriter, r *http.Request, _ int32) {
		w.WriteHeader(http.StatusForbidden)
		w.Write([]byte("Forbidden\nno"))
	})
	_, body, err = client(t, ts.URL).PostAndParseWithRetry(ctx, "/forbidden", map[string]string{}, &got)
	t.Equal("retry-gives-up-on-a-non-retriable-status",
		itoa(int64(atomic.LoadInt32(count)))+"|"+boolStr(err != nil)+"|"+string(body),
		"1|true|Forbidden\nno")
	ts.Close()

	// --- retry: Retry-After in its other documented spelling, an RFC 1123
	// date. The date is four seconds ahead, so honouring it waits materially
	// longer than the multiplier's own first step (one second) would. Only a
	// lower bound is asserted.
	ts, times := serveTimed(func(w http.ResponseWriter, r *http.Request, n int32) {
		if n == 1 {
			w.Header().Set("Retry-After", time.Now().UTC().Add(4*time.Second).Format(time.RFC1123))
			w.WriteHeader(http.StatusServiceUnavailable)
			return
		}
		w.Write([]byte(okBody))
	})
	got = payload{}
	rsp, _, err = client(t, ts.URL).PostAndParseWithRetry(ctx, "/date", map[string]string{}, &got)
	seen := times()
	gap := time.Duration(0)
	if len(seen) >= 2 {
		gap = seen[1].Sub(seen[0])
	}
	t.Check("retry-honours-a-retry-after-date",
		err == nil && len(seen) == 2 && rsp != nil && rsp.StatusCode == 200 && gap >= 2*time.Second,
		"err=%v requests=%d gap=%v", err, len(seen), gap)
	ts.Close()

	// --- retry: the interval grows with the multiplier. Two 503s with no
	// Retry-After, so the release's own schedule decides: 2^0 s then 2^1 s.
	// Asserted as two lower bounds and one ordering -- never an upper bound --
	// so a loaded machine cannot fail it.
	ts, times = serveTimed(func(w http.ResponseWriter, r *http.Request, n int32) {
		if n <= 2 {
			w.WriteHeader(http.StatusServiceUnavailable)
			return
		}
		w.Write([]byte(okBody))
	})
	got = payload{}
	_, _, err = client(t, ts.URL).PostAndParseWithRetry(ctx, "/grow", map[string]string{}, &got)
	seen = times()
	first, second := time.Duration(0), time.Duration(0)
	if len(seen) >= 3 {
		first, second = seen[1].Sub(seen[0]), seen[2].Sub(seen[1])
	}
	t.Check("retry-grows-the-interval-on-repeated-failure",
		err == nil && len(seen) == 3 &&
			first >= 900*time.Millisecond && second >= 1900*time.Millisecond && second > first,
		"err=%v requests=%d first=%v second=%v", err, len(seen), first, second)
	ts.Close()

	// --- retry: 429 is NOT retried at this release. CLIENT-API.md section 1.1
	// puts it in the default arm up to v1.1.2 and makes it a second 503 only
	// from v1.1.3, so here one request is made, the body comes back, and the
	// error carries the documented `got HTTP Status %q`.
	ts, count, _ = serve(func(w http.ResponseWriter, r *http.Request, _ int32) {
		w.WriteHeader(http.StatusTooManyRequests)
		w.Write([]byte("rate limited"))
	})
	_, body, err = client(t, ts.URL).PostAndParseWithRetry(ctx, "/429", map[string]string{}, &got)
	t.Check("retry-does-not-back-off-on-too-many-requests",
		atomic.LoadInt32(count) == 1 && err != nil && string(body) == "rate limited" &&
			strings.Contains(err.Error(), `got HTTP Status "429 Too Many Requests"`),
		"requests=%d err=%v body=%q", atomic.LoadInt32(count), err, body)
	ts.Close()

	// --- retry: a dead context ends the loop instead of being retried.
	ts, count, _ = serve(func(w http.ResponseWriter, r *http.Request, _ int32) {
		w.WriteHeader(http.StatusServiceUnavailable)
	})
	deadline, cancel := context.WithTimeout(ctx, 300*time.Millisecond)
	_, _, err = client(t, ts.URL).PostAndParseWithRetry(deadline, "/forever", map[string]string{}, &got)
	cancel()
	t.Check("retry-gives-up-when-the-context-expires",
		errors.Is(err, context.DeadlineExceeded) && atomic.LoadInt32(count) == 1,
		"err=%v (%T) requests=%d", err, err, atomic.LoadInt32(count))
	ts.Close()

	t.Done()
}

func boolStr(b bool) string {
	if b {
		return "true"
	}
	return "false"
}

// Group jsonclient-429-retry.
//
// v1.1.3 adds HTTP 429 (Too Many Requests) to the retriable arm of
// jsonclient.PostAndParseWithRetry's status switch, alongside 503, so a log
// that rate-limits a submission is backed off and retried instead of failing
// the call. The Retry-After header is honoured for 429 exactly as it already
// was for 503. This group drives jsonclient against a benchmark-owned
// loopback server that counts POSTs and scripts a status sequence.
//
// At v1.1.2 a 429 falls through to the default arm and is returned at once as
// an RspError, so the retried cases here see exactly one POST and a non-nil
// error, and cannot pass.
package main

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"net"
	"net/http"
	"os"
	"sync"
	"time"

	"github.com/google/certificate-transparency-go/jsonclient"

	"ctverify/vhelp"
)

// quietLogger keeps the product's retry chatter out of this group's streams.
type quietLogger struct{}

func (quietLogger) Printf(string, ...interface{}) {}

type reqBody struct {
	Ping string `json:"ping"`
}

type rspBody struct {
	Pong string `json:"pong"`
}

// scriptedServer answers each POST with the next status in its script, and
// then 200 for ever after. Every response to a scripted status carries the
// given Retry-After value when it is non-empty.
type scriptedServer struct {
	mu         sync.Mutex
	n          int
	script     []int
	retryAfter string

	ln  net.Listener
	srv *http.Server
	URL string
}

func newScripted(script []int, retryAfter string) *scriptedServer {
	s := &scriptedServer{script: script, retryAfter: retryAfter}
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		fmt.Fprintf(os.Stderr, "listen: %v\n", err)
		os.Exit(3)
	}
	s.ln = ln
	s.URL = "http://" + ln.Addr().String()
	s.srv = &http.Server{Handler: s}
	go func() { _ = s.srv.Serve(ln) }()
	return s
}

func (s *scriptedServer) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	_, _ = new(bytes.Buffer).ReadFrom(r.Body)
	s.mu.Lock()
	i := s.n
	s.n++
	s.mu.Unlock()

	w.Header().Set("Content-Type", "application/json")
	if i < len(s.script) {
		if s.retryAfter != "" {
			w.Header().Set("Retry-After", s.retryAfter)
		}
		w.WriteHeader(s.script[i])
		_, _ = w.Write([]byte(`{"error":"scripted"}`))
		return
	}
	w.WriteHeader(http.StatusOK)
	_, _ = w.Write([]byte(`{"pong":"ok"}`))
}

func (s *scriptedServer) calls() int {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.n
}

func (s *scriptedServer) close() {
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	_ = s.srv.Shutdown(ctx)
	_ = s.ln.Close()
}

// post runs one PostAndParseWithRetry against a scripted server and reports
// how many POSTs it took, what came back, and the error.
func post(t *vhelp.T, script []int, retryAfter string, deadline time.Duration) (int, rspBody, error) {
	s := newScripted(script, retryAfter)
	defer s.close()
	c, err := jsonclient.New(s.URL, &http.Client{Timeout: 10 * time.Second},
		jsonclient.Options{Logger: quietLogger{}, UserAgent: "ct-verify/1"})
	if err != nil {
		t.Fatalf("jsonclient.New: %v", err)
	}
	ctx, cancel := context.WithTimeout(context.Background(), deadline)
	defer cancel()
	var out rspBody
	_, _, err = c.PostAndParseWithRetry(ctx, "/ct/v1/add-chain", &reqBody{Ping: "x"}, &out)
	return s.calls(), out, err
}

func main() {
	t := vhelp.New()

	// 1. A single 429 with Retry-After: 0 is retried, and the retry succeeds.
	n, out, err := post(t, []int{http.StatusTooManyRequests}, "0", 30*time.Second)
	t.Check("429-then-200-retried", err == nil && n == 2 && out.Pong == "ok",
		"posts=%d rsp=%+v err=%v", n, out, err)

	// 2. Three consecutive 429s are each retried; the fourth POST succeeds.
	n, out, err = post(t, []int{
		http.StatusTooManyRequests, http.StatusTooManyRequests, http.StatusTooManyRequests,
	}, "0", 30*time.Second)
	t.Check("429-repeated-then-200", err == nil && n == 4 && out.Pong == "ok",
		"posts=%d rsp=%+v err=%v", n, out, err)

	// 3. 429 shares the arm with 503, which must keep its behaviour.
	n, out, err = post(t, []int{http.StatusServiceUnavailable}, "0", 30*time.Second)
	t.Check("503-then-200-retried", err == nil && n == 2 && out.Pong == "ok",
		"posts=%d rsp=%+v err=%v", n, out, err)

	// 4. A mixed 429/503 sequence is retried throughout.
	n, out, err = post(t, []int{
		http.StatusServiceUnavailable, http.StatusTooManyRequests, http.StatusServiceUnavailable,
	}, "0", 30*time.Second)
	t.Check("mixed-429-503-retried", err == nil && n == 4 && out.Pong == "ok",
		"posts=%d rsp=%+v err=%v", n, out, err)

	// 5. A 429's Retry-After is honoured: with a 60-second hold and a
	//    two-second deadline the call must end in the context error after
	//    exactly one POST, not in a second POST and not in an RspError.
	n, _, err = post(t, []int{http.StatusTooManyRequests}, "60", 2*time.Second)
	t.Check("429-retry-after-honoured",
		n == 1 && errors.Is(err, context.DeadlineExceeded),
		"posts=%d err=%v", n, err)

	// 6. A status outside the retriable set is still returned at once.
	n, _, err = post(t, []int{http.StatusInternalServerError}, "0", 30*time.Second)
	var rspErr jsonclient.RspError
	t.Check("500-not-retried",
		n == 1 && errors.As(err, &rspErr) && rspErr.StatusCode == http.StatusInternalServerError,
		"posts=%d err=%v", n, err)

	// 7. A 403 from the same server is likewise terminal, so the retry arm is
	//    specific and not a blanket "retry every error".
	n, _, err = post(t, []int{http.StatusForbidden}, "0", 30*time.Second)
	t.Check("403-not-retried",
		n == 1 && errors.As(err, &rspErr) && rspErr.StatusCode == http.StatusForbidden,
		"posts=%d err=%v", n, err)

	t.Done()
}

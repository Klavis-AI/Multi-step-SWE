// Package cancellation asks whether the API client still lets a caller call
// off a request that is already on the wire.
//
// The client is pointed at an ordinary httptest server that answers nothing
// until either the request's own context is done or a generous ceiling
// expires. That makes the answer unambiguous: if the caller's context reaches
// the request, the call returns in milliseconds and the handler sees its
// request context close; if it does not, the call sits there until the
// ceiling, and the test times out instead of passing.
//
// Nothing here names an implementation. A client that carries the context on
// the *http.Request passes, and so would one that arranged the same effect
// some other way -- what is being graded is that a cancelled caller stops a
// request, not the spelling of the call that achieves it.
package cancellation

import (
	"context"
	"encoding/json"
	"errors"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	apiclient "github.com/codedellemc/libstorage/api/client"
	apictx "github.com/codedellemc/libstorage/api/context"
	"github.com/codedellemc/libstorage/api/types"
)

// ceiling is how long a handler will sit on a request that nobody cancels. It
// is far longer than any of these tests should take and far shorter than the
// package timeout, so a client that ignores its context produces a failed
// assertion with a readable message rather than a killed test binary.
const ceiling = 20 * time.Second

// settle is the margin allowed between asking for a cancellation and the call
// returning. Generous on purpose: this runs on a loaded seven-CPU box, and the
// difference being measured is milliseconds against twenty seconds.
const settle = 8 * time.Second

// stalling returns a server that holds every request until the request's
// context is done, and a channel that reports what ended each one: true when
// the request context closed, false when the ceiling ran out first.
func stalling() (*httptest.Server, <-chan bool) {
	ended := make(chan bool, 8)
	srv := httptest.NewServer(http.HandlerFunc(
		func(w http.ResponseWriter, r *http.Request) {
			select {
			case <-r.Context().Done():
				ended <- true
			case <-time.After(ceiling):
				ended <- false
				w.Header().Set("Content-Type", "application/json")
				json.NewEncoder(w).Encode([]string{"too", "late"})
			}
		}))
	return srv, ended
}

// clientFor builds the repository's API client for a test server. The client
// formats its URLs as http://<host><path>, so it wants the authority on its
// own rather than the whole URL httptest hands out.
func clientFor(srv *httptest.Server) types.APIClient {
	return apiclient.New(strings.TrimPrefix(srv.URL, "http://"),
		&http.Transport{})
}

// call runs Root on its own goroutine and reports how long it took.
func call(c types.APIClient, ctx types.Context) (<-chan error, <-chan time.Duration) {
	errc, dur := make(chan error, 1), make(chan time.Duration, 1)
	go func() {
		started := time.Now()
		_, err := c.Root(ctx)
		dur <- time.Since(started)
		errc <- err
	}()
	return errc, dur
}

// TestCancelledContextStopsRequest: a caller that cancels gets control back.
func TestCancelledContextStopsRequest(t *testing.T) {
	srv, _ := stalling()
	defer srv.Close()

	parent, cancel := context.WithCancel(context.Background())
	errc, dur := call(clientFor(srv), apictx.New(parent))

	time.AfterFunc(300*time.Millisecond, cancel)
	select {
	case err := <-errc:
		if err == nil {
			t.Fatal("Root returned no error after its context was cancelled; " +
				"the request outlived the caller that asked for it")
		}
		if !errors.Is(err, context.Canceled) {
			t.Fatalf("Root came back with %v after its context was "+
				"cancelled; a call ended by the caller calling it off has to "+
				"report that, and this one reports something else that "+
				"happened to end it at about the same moment", err)
		}
		t.Logf("Root returned %v after %s", err, <-dur)
	case <-time.After(settle):
		t.Fatalf("Root did not return within %s of its context being "+
			"cancelled; the cancellation never reached the request", settle)
	}
}

// TestContextDeadlineStopsRequest: the same, expressed as a deadline.
func TestContextDeadlineStopsRequest(t *testing.T) {
	srv, _ := stalling()
	defer srv.Close()

	parent, cancel := context.WithTimeout(context.Background(),
		400*time.Millisecond)
	defer cancel()
	errc, dur := call(clientFor(srv), apictx.New(parent))

	select {
	case err := <-errc:
		if err == nil {
			t.Fatal("Root returned no error after its context's deadline " +
				"passed; the deadline was not honoured")
		}
		if !errors.Is(err, context.DeadlineExceeded) {
			t.Fatalf("Root came back with %v after its context's deadline "+
				"passed; a call ended by the deadline its caller set has to "+
				"report that, and this one reports something else that "+
				"happened to end it at about the same moment", err)
		}
		t.Logf("Root returned %v after %s", err, <-dur)
	case <-time.After(settle):
		t.Fatalf("Root did not return within %s of its deadline; the "+
			"deadline never reached the request", settle)
	}
}

// TestServerSeesTheCancellation is the one that cannot be satisfied by
// abandoning the call. A client that gives up locally while leaving the
// request running would pass both tests above; here the *handler* has to
// observe its request context close, which only happens when the connection
// carrying that request is actually torn down.
func TestServerSeesTheCancellation(t *testing.T) {
	srv, ended := stalling()
	defer srv.Close()

	parent, cancel := context.WithCancel(context.Background())
	call(clientFor(srv), apictx.New(parent))

	time.AfterFunc(300*time.Millisecond, cancel)
	select {
	case cancelled := <-ended:
		if !cancelled {
			t.Fatal("the handler ran to its ceiling: the server never saw " +
				"the client's cancellation, so the request was still in " +
				"flight after the caller had given up on it")
		}
	case <-time.After(settle):
		t.Fatalf("the handler had not finished %s after the cancellation; "+
			"nothing told the server to stop", settle)
	}
}

// TestLiveContextCompletesRequest is the control. Every assertion above is
// satisfied by a client that fails every request, so one of them has to
// succeed: a context nobody cancels must still produce the server's answer.
func TestLiveContextCompletesRequest(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(
		func(w http.ResponseWriter, r *http.Request) {
			w.Header().Set("Content-Type", "application/json")
			json.NewEncoder(w).Encode([]string{"volumes", "services"})
		}))
	defer srv.Close()

	roots, err := clientFor(srv).Root(apictx.New(context.Background()))
	if err != nil {
		t.Fatalf("Root failed on a context that was never cancelled: %v", err)
	}
	if len(roots) != 2 || roots[0] != "volumes" || roots[1] != "services" {
		t.Fatalf("Root returned %q, want [volumes services]", roots)
	}
}

// TestLiveContextSurvivesASlowAnswer is the second control, and the one that
// separates a client which honours its caller's context from one which bounds
// every request by a short clock of its own. A fixed ceiling low enough to
// look like cancellation in the tests above would end this request too; the
// context here is never cancelled and carries no deadline, so the answer has
// to arrive however long the server takes over it.
func TestLiveContextSurvivesASlowAnswer(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(
		func(w http.ResponseWriter, r *http.Request) {
			time.Sleep(2 * time.Second)
			w.Header().Set("Content-Type", "application/json")
			json.NewEncoder(w).Encode([]string{"volumes", "services"})
		}))
	defer srv.Close()

	roots, err := clientFor(srv).Root(apictx.New(context.Background()))
	if err != nil {
		t.Fatalf("Root failed against a handler that took two seconds to "+
			"answer, on a context that was never cancelled and had no "+
			"deadline: %v", err)
	}
	if len(roots) != 2 || roots[0] != "volumes" || roots[1] != "services" {
		t.Fatalf("Root returned %q after a slow answer, want "+
			"[volumes services]", roots)
	}
}

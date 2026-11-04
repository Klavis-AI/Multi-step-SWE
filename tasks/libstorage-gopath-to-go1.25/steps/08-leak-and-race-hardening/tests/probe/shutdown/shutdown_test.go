// Package shutdown asks what stopping the server actually does to the requests
// and connections it is holding.
//
// Closing a listener and shutting a server down look the same from the
// outside: in both cases the address stops accepting. They differ in what
// happens to work already in progress. A stop that only drops the listener
// returns immediately, leaves a request that is halfway through running
// unattended, and leaves an idle keep-alive connection usable. A stop that
// shuts the server down waits for the request, closes the idle connection --
// and, because waiting forever is its own bug, gives up after a deadline it
// sets itself.
//
// All three are observed through `types.Server`, which is the only thing the
// repository's own callers have. Nothing here names a method on the standard
// library's server or a field on anything.
package shutdown

import (
	"bufio"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"path"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	gofig "github.com/akutz/gofig/types"
	"github.com/akutz/gotil"

	apictx "github.com/codedellemc/libstorage/api/context"
	"github.com/codedellemc/libstorage/api/registry"
	apiserver "github.com/codedellemc/libstorage/api/server"
	"github.com/codedellemc/libstorage/api/server/httputils"
	"github.com/codedellemc/libstorage/api/types"
	"github.com/codedellemc/libstorage/api/utils"
	apicnfg "github.com/codedellemc/libstorage/api/utils/config"

	// the configuration keys, and a driver for the server to serve
	_ "github.com/codedellemc/libstorage/drivers/storage/vfs/storage"
	_ "github.com/codedellemc/libstorage/imports/config"
)

// inFlight is how long the probe route holds a request that is meant to be
// still running when the stop begins. Long enough that a stop which does not
// wait is unmistakable, short enough that one which does is not slow.
const inFlight = 6 * time.Second

// patience is the longest a stop may take on a request that will never finish.
// A stop is allowed to give up; it is not allowed to hang. The step puts the
// far end of the grace period at about a minute, and the ten seconds past that
// are the cost of reading the clock from outside the process that owns the
// deadline on a loaded box -- measurement margin, not ten seconds of extra
// allowance for the deadline itself.
const patience = 70 * time.Second

// grace is the least a stop may take on a request that will never finish.
// What is being asked for is a window rather than a ceiling: a stop that
// returns at once has not waited for work in progress at all, which is the
// behavior this milestone replaces, and it would otherwise satisfy an
// assertion that only bounds the waiting from above.
const grace = 3 * time.Second

const configFormat = `
libstorage:
  host: tcp://%[1]s
  logging:
    level: panic
  server:
    endpoints:
      localhost:
        address: tcp://%[1]s
    services:
      vfs:
        driver: vfs
`

var (
	// holdMillis is how long /verifierprobe/hold sits on a request.
	holdMillis int64
	// holding reports that the holding handler has been entered, so a test
	// never begins a stop before the request it means to stop is running.
	// Waiting for this rather than sleeping is what keeps the assertion a
	// difference in kind: the window being measured starts when the handler
	// starts, however long the request took to get there.
	holding = make(chan struct{}, 16)
	// wedge is closed to release /verifierprobe/wedge. The handler waits on
	// it instead of on a clock, so "a request that never finishes" is exact
	// rather than merely long.
	wedge     = make(chan struct{})
	wedgeOnce sync.Once
	// wedged is closed once the wedging handler has been entered, so a test
	// never begins a stop before the request it means to stop is running. Both
	// channels are closed through a Once because the client may make more than
	// one attempt at a request whose connection goes away under it.
	wedged     = make(chan struct{})
	wedgedOnce sync.Once
)

// release lets the wedged handler finish, whenever and however often it is
// called.
func release() { wedgeOnce.Do(func() { close(wedge) }) }

func init() {
	registry.RegisterRouter(&probeRouter{})
}

// probeRouter adds the two routes these tests control the timing of. It
// belongs to the scoring harness, not to the repository, so what it does
// cannot be adjusted by the tree under test.
type probeRouter struct {
	routes []types.Route
}

func (r *probeRouter) Name() string { return "verifier-probe-router" }

func (r *probeRouter) Init(config gofig.Config) {
	r.routes = []types.Route{
		httputils.NewGetRoute("verifier-probe-hold", "/verifierprobe/hold",
			func(ctx types.Context, w http.ResponseWriter,
				req *http.Request, store types.Store) error {
				select {
				case holding <- struct{}{}:
				default:
				}
				time.Sleep(time.Duration(
					atomic.LoadInt64(&holdMillis)) * time.Millisecond)
				httputils.WriteJSON(w, http.StatusOK,
					map[string]bool{"held": true})
				return nil
			}),
		httputils.NewGetRoute("verifier-probe-wedge", "/verifierprobe/wedge",
			func(ctx types.Context, w http.ResponseWriter,
				req *http.Request, store types.Store) error {
				wedgedOnce.Do(func() { close(wedged) })
				<-wedge
				httputils.WriteJSON(w, http.StatusOK,
					map[string]bool{"released": true})
				return nil
			}),
	}
}

func (r *probeRouter) Routes() []types.Route { return r.routes }

// serve starts the repository's server on a free local port and returns the
// one function that stops it, with its address. The stop is guarded by a
// sync.Once so that a test's own stop and the teardown below are the same
// close: what these tests measure is how long the *first* close takes, and a
// second call into types.Server.Close would be a different question asked of a
// server that has already answered this one.
func serve(t *testing.T) (func(), string) {
	t.Helper()

	apiserver.DisableStartupInfo = true
	os.Setenv("LIBSTORAGE_LOGGING_LEVEL", "panic")

	ctx := apictx.Background()
	pathConfig := utils.NewPathConfig(ctx, t.TempDir(), "", t.TempDir())
	ctx = apictx.WithValue(ctx, apictx.PathConfigKey, pathConfig)
	registry.ProcessRegisteredConfigs(ctx)

	laddr := fmt.Sprintf("127.0.0.1:%d", gotil.RandomTCPPort())
	data := fmt.Sprintf(configFormat, laddr)
	if err := os.WriteFile(
		path.Join(pathConfig.Etc, "config.yml"), []byte(data), 0644); err != nil {
		t.Fatalf("writing the server's configuration: %v", err)
	}

	config, err := apicnfg.NewConfig(ctx)
	if err != nil {
		t.Fatalf("reading the server's configuration: %v", err)
	}

	srv, errs, err := apiserver.Serve(ctx, config)
	if err != nil {
		t.Fatalf("starting the server: %v", err)
	}
	go func() {
		for range errs {
		}
	}()

	var once sync.Once
	stop := func() { once.Do(func() { srv.Close() }) }
	t.Cleanup(func() { closeBounded(t, stop) })
	return stop, laddr
}

// closeBounded stops a server without letting a stop that hangs decide this
// package's result. What stopping the server does is a requirement of its own,
// graded by its own tests; here the stop only has to not take these tests down
// with it -- a test that gives up before it has run its own stop would
// otherwise leave a listening server, and the goroutine draining its errors,
// behind for whichever test runs next in this process.
func closeBounded(t *testing.T, stop func()) {
	done := make(chan struct{})
	go func() { defer close(done); stop() }()
	select {
	case <-done:
	case <-time.After(30 * time.Second):
		t.Log("stopping the server did not return within 30s")
	}
}

// stopped runs the server's stop on its own goroutine and reports how long it
// took, so a test can assert on the duration without risking a hang: a stop
// that never returns shows up as nothing arriving on the channel.
func stopped(stop func()) <-chan time.Duration {
	took := make(chan time.Duration, 1)
	go func() {
		started := time.Now()
		stop()
		took <- time.Since(started)
	}()
	return took
}

// drain empties a signalling channel, so that what a test waits for is the
// request it just made and not one an earlier test left behind.
func drain(c chan struct{}) {
	for {
		select {
		case <-c:
		default:
			return
		}
	}
}

// get issues a request on its own goroutine and reports the outcome.
func get(url string) <-chan error {
	done := make(chan error, 1)
	go func() {
		res, err := http.Get(url)
		if err != nil {
			done <- err
			return
		}
		io.Copy(io.Discard, res.Body)
		res.Body.Close()
		if res.StatusCode != http.StatusOK {
			done <- fmt.Errorf("status %d", res.StatusCode)
			return
		}
		done <- nil
	}()
	return done
}

// TestStopWaitsForRequestsInFlight is the requirement. A request that is
// halfway through when the stop begins has to finish, and the stop has to
// still be running while it does.
func TestStopWaitsForRequestsInFlight(t *testing.T) {
	atomic.StoreInt64(&holdMillis, int64(inFlight/time.Millisecond))
	defer atomic.StoreInt64(&holdMillis, 0)

	stop, laddr := serve(t)
	drain(holding)
	answered := get("http://" + laddr + "/verifierprobe/hold")

	select {
	case <-holding:
	case <-time.After(30 * time.Second):
		t.Fatal("the request that is meant to be in flight never reached " +
			"its handler")
	}
	took := stopped(stop)

	select {
	case d := <-took:
		t.Fatalf("stopping the server returned after %s while a request "+
			"with about %s still to run was in flight; the stop abandoned "+
			"work already in progress instead of waiting for it", d, inFlight)
	case <-time.After(inFlight - 2*time.Second):
	}

	select {
	case d := <-took:
		t.Logf("the stop returned after %s", d)
	case <-time.After(30 * time.Second):
		t.Fatal("stopping the server had not returned 30s after the request " +
			"it was waiting for should have finished")
	}
	if err := <-answered; err != nil {
		t.Fatalf("the in-flight request did not complete: %v; a stop that "+
			"waits must also let the request it waits for finish", err)
	}
}

// TestStopClosesIdleConnections is the half of the same requirement that a
// stop which only waits does not satisfy. A keep-alive connection that is idle
// when the stop begins belongs to the server, and the server is going away.
func TestStopClosesIdleConnections(t *testing.T) {
	atomic.StoreInt64(&holdMillis, 0)
	stop, laddr := serve(t)

	conn, err := net.DialTimeout("tcp", laddr, 10*time.Second)
	if err != nil {
		t.Fatalf("dialling the server at %s: %v", laddr, err)
	}
	defer conn.Close()
	br := bufio.NewReader(conn)

	// one complete exchange, read to the end of its body, which leaves the
	// connection established and idle
	ask := func() (*http.Response, error) {
		if _, err := io.WriteString(conn,
			"GET / HTTP/1.1\r\nHost: probe\r\n\r\n"); err != nil {
			return nil, err
		}
		res, err := http.ReadResponse(br, nil)
		if err != nil {
			return nil, err
		}
		io.Copy(io.Discard, res.Body)
		res.Body.Close()
		return res, nil
	}
	if res, err := ask(); err != nil {
		t.Fatalf("the first request failed before anything was stopped: %v", err)
	} else if res.StatusCode != http.StatusOK {
		t.Fatalf("the first request answered %d, want 200", res.StatusCode)
	}

	select {
	case <-stopped(stop):
	case <-time.After(30 * time.Second):
		t.Fatal("stopping the server with nothing in flight took over 30s")
	}

	conn.SetDeadline(time.Now().Add(15 * time.Second))
	if res, err := ask(); err == nil {
		t.Fatalf("a connection that was idle when the server stopped still "+
			"answered a request with %d afterwards; the stop dropped the "+
			"listener and left the connections behind", res.StatusCode)
	}
}

// TestStopGivesUpOnAWedgedRequest is the window. A stop that waits for a
// request that will never finish is a process that will never exit, which is
// the failure the deadline exists to prevent -- and a stop that returns at
// once has not waited for anything, which is the behavior being replaced.
// Both ends are asserted here because either one alone is satisfiable by a
// server that does nothing this milestone asks for.
func TestStopGivesUpOnAWedgedRequest(t *testing.T) {
	stop, laddr := serve(t)
	get("http://" + laddr + "/verifierprobe/wedge")

	select {
	case <-wedged:
	case <-time.After(30 * time.Second):
		t.Fatal("the request that is meant to wedge never reached its handler")
	}

	took := stopped(stop)
	select {
	case d := <-took:
		if d < grace {
			release()
			t.Fatalf("stopping the server returned after %s with a request "+
				"still running; a stop that gives up that quickly has not "+
				"waited for work in progress at all, and a request that is "+
				"merely slow would be abandoned with it", d)
		}
		t.Logf("the stop gave up after %s", d)
	case <-time.After(patience):
		release()
		t.Fatalf("stopping the server had not returned after %s with one "+
			"request wedged; the stop waits without a deadline, so a single "+
			"stuck request is enough to keep the process alive forever",
			patience)
	}
	release()
}

// TestStopReturnsPromptlyWithNothingInFlight is the control. Every assertion
// above is satisfied by a stop that always takes a long time, so a server with
// no work to drain has to stop at once.
func TestStopReturnsPromptlyWithNothingInFlight(t *testing.T) {
	atomic.StoreInt64(&holdMillis, 0)
	stop, laddr := serve(t)

	if err := <-get("http://" + laddr + "/"); err != nil {
		t.Fatalf("an ordinary request failed: %v", err)
	}
	select {
	case d := <-stopped(stop):
		if d > 20*time.Second {
			t.Fatalf("stopping an idle server took %s", d)
		}
	case <-time.After(30 * time.Second):
		t.Fatal("stopping an idle server had not returned after 30s")
	}
}

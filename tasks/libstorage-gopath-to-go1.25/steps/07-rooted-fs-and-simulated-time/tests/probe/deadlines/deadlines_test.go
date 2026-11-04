// Package deadlines asks whether the server's configured HTTP timeouts do
// anything, and whether what they do follows the value they were given.
//
// `libstorage.http.readTimeout` and `libstorage.http.writeTimeout` are whole
// seconds and have had a 300 default for as long as they have existed. Each
// key is driven at two different values inside one graded test, and the
// connection the server ends has to land in a window around whichever value
// that server was started with. The two windows do not overlap, so no single
// duration compiled into the server sits in both; the same test then asks that
// the two closes arrive as far apart as the two configurations that produced
// them, which is free for a server that reads the keys and impossible for one
// that holds a constant. A third run on the documented default leaves the same
// stalled request alone for longer than the far end of the longest window, and
// a fourth asks that an ordinary request is still answered normally. Every
// margin here is seconds wide against differences of seconds, so nothing is a
// race against a loaded box.
package deadlines

import (
	"bufio"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"path"
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

// short and long are the two values each key is driven at, in the same graded
// test. Two are needed rather than one because a single configured value is
// matched by a single duration compiled into the server; two are not, and the
// further apart they sit the less room there is for one constant to be near
// both. Three seconds is the shorter for the reason it always was -- the
// control test needs an ordinary request answered comfortably inside this same
// value -- and twelve is the longer because nine seconds of separation is more
// than any scheduling delay this package will see.
const (
	short = 3
	long  = 12
)

// early and late are how far before and after the configured value a close may
// land. For a value of v seconds the window is [v-early, v+late], which for
// three and twelve is [2s, 8s] and [11s, 17s]: disjoint, with three seconds
// between them. The window is lopsided because a server under load is late and
// never early, and it is seconds wide because the alternative, a window tight
// enough to name a millisecond, would be a race against the box rather than a
// statement about the tree.
const (
	early = 1 * time.Second
	late  = 5 * time.Second
)

// separation is the least the two closes of one graded test may differ by.
// Nine seconds separate the two configured values, so a server that reads them
// clears this without trying, and a server holding any single duration ends
// both connections at the same moment and cannot. Three seconds rather than
// something nearer nine because the two windows above already permit a
// separation of three -- [2s, 8s] and [11s, 17s] -- and an assertion that can
// contradict the windows it sits beside would fail a tree the windows accept,
// on the clock rather than on the tree. What this assertion is for is the
// single-duration server, and three seconds is as impossible for one as nine.
const separation = 3 * time.Second

// configFormat is the smallest configuration that produces a serving endpoint,
// with the two timeout keys spelled the way a user would spell them.
const configFormat = `
libstorage:
  host: tcp://%[1]s
  logging:
    level: panic
  http:
    readTimeout: %[2]d
    writeTimeout: %[3]d
  server:
    endpoints:
      localhost:
        address: tcp://%[1]s
    services:
      vfs:
        driver: vfs
`

// holdMillis is how long the probe route spends answering one request. Set by
// whichever test is running; the package's tests are sequential.
var holdMillis int64

// chunk is one instalment of a slow answer. Eight kibibytes rather than a few
// bytes because it has to reach the socket on its own: the standard library's
// response writer passes a write larger than its chunking buffer straight
// through, so the instalments arrive whether or not the writer this handler is
// given can be flushed.
var chunk = make([]byte, 8<<10)

func init() {
	registry.RegisterRouter(&probeRouter{})
}

// probeRouter adds one route the tests control the timing of. It belongs to
// the scoring harness rather than to the repository, so what it does cannot be
// adjusted by the tree under test.
type probeRouter struct {
	routes []types.Route
}

func (r *probeRouter) Name() string { return "verifier-probe-router" }

func (r *probeRouter) Init(config gofig.Config) {
	r.routes = []types.Route{
		httputils.NewGetRoute("verifier-probe-hold", "/verifierprobe/hold",
			func(ctx types.Context, w http.ResponseWriter,
				req *http.Request, store types.Store) error {
				hold := time.Duration(
					atomic.LoadInt64(&holdMillis)) * time.Millisecond
				if hold == 0 {
					httputils.WriteJSON(w, http.StatusOK,
						map[string]bool{"held": true})
					return nil
				}
				// A slow answer, produced the way a slow answer is produced:
				// the handler starts replying at once and keeps replying for
				// `hold`. This is what makes the moment a write deadline
				// fires observable from the other end at all -- a deadline is
				// set when the request arrives and is not reset by these
				// writes, so a configured one fires partway through and takes
				// the connection with it. A handler that says nothing until it
				// has finished would instead be cut at the moment it finishes,
				// whatever deadline was in force, and the far end could not
				// tell one deadline from another.
				w.Header().Set("Content-Type", "text/plain")
				w.WriteHeader(http.StatusOK)
				flusher, _ := w.(http.Flusher)
				until := time.Now().Add(hold)
				for time.Now().Before(until) {
					if _, err := w.Write(chunk); err != nil {
						return nil
					}
					if flusher != nil {
						flusher.Flush()
					}
					time.Sleep(200 * time.Millisecond)
				}
				return nil
			}),
	}
}

func (r *probeRouter) Routes() []types.Route { return r.routes }

// serve starts the repository's server on a free local port with the two
// timeouts set to the given whole seconds, and returns its address.
func serve(t *testing.T, readTimeout, writeTimeout int) (types.Server, string) {
	t.Helper()

	apiserver.DisableStartupInfo = true
	os.Setenv("LIBSTORAGE_LOGGING_LEVEL", "panic")

	ctx := apictx.Background()
	pathConfig := utils.NewPathConfig(ctx, t.TempDir(), "", t.TempDir())
	ctx = apictx.WithValue(ctx, apictx.PathConfigKey, pathConfig)
	registry.ProcessRegisteredConfigs(ctx)

	laddr := fmt.Sprintf("127.0.0.1:%d", gotil.RandomTCPPort())
	data := fmt.Sprintf(configFormat, laddr, readTimeout, writeTimeout)
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
	t.Cleanup(func() { closeBounded(t, srv) })
	return srv, laddr
}

// closeBounded stops a server without letting a stop that hangs decide this
// package's result. What stopping the server does is a requirement of its own,
// graded by its own tests; here the stop only has to not take these tests down
// with it.
func closeBounded(t *testing.T, srv types.Server) {
	done := make(chan struct{})
	go func() { defer close(done); srv.Close() }()
	select {
	case <-done:
	case <-time.After(30 * time.Second):
		t.Log("stopping the server did not return within 30s")
	}
}

// stall opens a connection and sends the beginning of a request -- a request
// line and one header, and then nothing. The request is never finished, so a
// server that bounds how long it will wait for one has to close this
// connection and a server that does not will hold it open forever.
func stall(t *testing.T, laddr string) net.Conn {
	t.Helper()
	conn, err := net.DialTimeout("tcp", laddr, 10*time.Second)
	if err != nil {
		t.Fatalf("dialling the server at %s: %v", laddr, err)
	}
	t.Cleanup(func() { conn.Close() })
	if _, err := io.WriteString(conn,
		"GET / HTTP/1.1\r\nHost: probe\r\n"); err != nil {
		t.Fatalf("sending the start of a request: %v", err)
	}
	return conn
}

// closedWithin reports whether the peer ended the connection inside d, and how
// long that took. A read that returns anything at all -- bytes, EOF or a reset
// -- means the server acted; only our own deadline expiring means it did not.
func closedWithin(conn net.Conn, d time.Duration) (bool, time.Duration) {
	started := time.Now()
	conn.SetReadDeadline(time.Now().Add(d))
	buf := make([]byte, 512)
	_, err := conn.Read(buf)
	took := time.Since(started)
	if ne, ok := err.(net.Error); ok && ne.Timeout() {
		return false, took
	}
	return true, took
}

// separated fails the test unless two closes are as far apart as the two
// configurations that produced them. This is the assertion no compiled-in
// duration can satisfy: it ends both connections at the same moment whatever
// the configuration said.
func separated(t *testing.T, key string, quick, patient time.Duration) {
	t.Helper()
	if patient-quick >= separation {
		return
	}
	t.Fatalf("with %s at %ds the server ended the connection after %s, and "+
		"with the same key at %ds it ended after %s -- %s apart; two "+
		"configurations %ds apart have to produce closes that move with "+
		"them, and one duration compiled into the server cannot",
		key, short, quick, long, patient, patient-quick, long-short)
}

// readCloseFollows starts a server with the given whole-second read timeout,
// stalls one request at it and returns how long the server took to end that
// request, failing the test if the close lands outside the window that value
// asks for. The read deadline is set past the far end of the window rather
// than at it, so a server that never closes reports as an assertion with a
// duration in it rather than as a package that hangs.
func readCloseFollows(t *testing.T, seconds int) time.Duration {
	t.Helper()
	_, laddr := serve(t, seconds, 300)
	conn := stall(t, laddr)

	want := time.Duration(seconds) * time.Second
	closed, took := closedWithin(conn, want+late+2*time.Second)
	switch {
	case !closed:
		t.Fatalf("the server held a half-sent request open for %s with "+
			"libstorage.http.readTimeout set to %ds; the configured read "+
			"timeout has no effect on the server that serves requests",
			took, seconds)
	case took < want-early:
		t.Fatalf("the server ended a half-sent request after %s with "+
			"libstorage.http.readTimeout set to %ds; whatever timeout is in "+
			"force is shorter than the configured one", took, seconds)
	case took > want+late:
		t.Fatalf("the server ended a half-sent request after %s with "+
			"libstorage.http.readTimeout set to %ds; whatever timeout is in "+
			"force is longer than the configured one", took, seconds)
	}
	t.Logf("with libstorage.http.readTimeout at %ds the connection ended "+
		"after %s", seconds, took)
	return took
}

// writeCloseFollows is the same measurement for the other key. The request
// arrives complete and promptly; what takes too long is the answer, so the
// handler holds it for four seconds longer than the timeout being measured --
// any less and the answer would arrive before the deadline it is supposed to
// miss, and any more would only add to what the package waits for.
func writeCloseFollows(t *testing.T, seconds int) time.Duration {
	t.Helper()
	atomic.StoreInt64(&holdMillis, int64(seconds+4)*1000)
	defer atomic.StoreInt64(&holdMillis, 0)

	_, laddr := serve(t, 300, seconds)
	conn, err := net.DialTimeout("tcp", laddr, 10*time.Second)
	if err != nil {
		t.Fatalf("dialling the server at %s: %v", laddr, err)
	}
	defer conn.Close()
	if _, err := io.WriteString(conn,
		"GET /verifierprobe/hold HTTP/1.1\r\nHost: probe\r\n\r\n"); err != nil {
		t.Fatalf("sending the request: %v", err)
	}

	want := time.Duration(seconds) * time.Second
	conn.SetReadDeadline(time.Now().Add(want + late + 2*time.Second))
	started := time.Now()
	res, err := http.ReadResponse(bufio.NewReader(conn), nil)
	if err != nil {
		t.Fatalf("the server sent no answer at all to a request it takes "+
			"%ds to produce, %s in: %v", seconds+4, time.Since(started), err)
	}
	_, err = io.Copy(io.Discard, res.Body)
	res.Body.Close()
	took := time.Since(started)
	switch {
	case err == nil:
		t.Fatalf("the server finished an answer it spent %ds producing, "+
			"after %s, with libstorage.http.writeTimeout set to %ds; the "+
			"configured write timeout has no effect", seconds+4, took, seconds)
	case isTimeout(err):
		t.Fatalf("the answer was still arriving %s after a request that "+
			"outran the %ds configured write timeout", took, seconds)
	case took < want-early:
		t.Fatalf("the server cut a slow response off after %s with "+
			"libstorage.http.writeTimeout set to %ds (%v); whatever timeout "+
			"is in force is shorter than the configured one",
			took, seconds, err)
	case took > want+late:
		t.Fatalf("the server cut a slow response off after %s with "+
			"libstorage.http.writeTimeout set to %ds (%v); whatever timeout "+
			"is in force is longer than the configured one",
			took, seconds, err)
	}
	t.Logf("with libstorage.http.writeTimeout at %ds the connection ended "+
		"after %s: %v", seconds, took, err)
	return took
}

// isTimeout reports whether an error is this side's own read deadline rather
// than something the server did, which is the difference between "the server
// never acted" and "the server acted at the wrong moment".
func isTimeout(err error) bool {
	ne, ok := err.(net.Error)
	return ok && ne.Timeout()
}

// TestReadTimeoutFollowsTheConfiguredValue is the requirement: the read
// timeout the server enforces has to be the one it was configured with, and
// not merely some timeout. The same stalled request is driven at two servers
// whose only difference is the configured value, each close has to land in
// that value's own window, and the two closes have to be as far apart as the
// two values.
func TestReadTimeoutFollowsTheConfiguredValue(t *testing.T) {
	quick := readCloseFollows(t, short)
	patient := readCloseFollows(t, long)
	separated(t, "libstorage.http.readTimeout", quick, patient)
}

// TestWriteTimeoutFollowsTheConfiguredValue is the same requirement for the
// other key, asked the same way: two servers, two configured values, two
// windows that do not overlap, and an ordering between them.
func TestWriteTimeoutFollowsTheConfiguredValue(t *testing.T) {
	quick := writeCloseFollows(t, short)
	patient := writeCloseFollows(t, long)
	separated(t, "libstorage.http.writeTimeout", quick, patient)
}

// TestDefaultReadTimeoutLeavesItAlone is the other half of the same input, and
// the reason the tests above cannot be passed by closing every slow
// connection. With the default 300 s the same stalled request must still be
// waiting -- and waiting past the far end of the longest window above, so that
// this control rules out a compiled-in duration on its own as well.
func TestDefaultReadTimeoutLeavesItAlone(t *testing.T) {
	_, laddr := serve(t, 300, 300)
	conn := stall(t, laddr)

	closed, took := closedWithin(conn, 20*time.Second)
	if closed {
		t.Fatalf("the server ended a half-sent request after %s even though "+
			"libstorage.http.readTimeout is at its 300s default; the timeout "+
			"in force is not the configured one", took)
	}
}

// TestConfiguredTimeoutsStillServeAnOrdinaryRequest is the control. Every
// assertion above is satisfied by a server that closes connections at random,
// so a request that finishes inside both timeouts has to be answered normally.
func TestConfiguredTimeoutsStillServeAnOrdinaryRequest(t *testing.T) {
	atomic.StoreInt64(&holdMillis, 0)
	_, laddr := serve(t, short, short)

	res, err := http.Get("http://" + laddr + "/")
	if err != nil {
		t.Fatalf("an ordinary request failed against a server configured "+
			"with %ds timeouts: %v", short, err)
	}
	defer res.Body.Close()
	if res.StatusCode != http.StatusOK {
		t.Fatalf("GET / answered %d, want 200", res.StatusCode)
	}
}

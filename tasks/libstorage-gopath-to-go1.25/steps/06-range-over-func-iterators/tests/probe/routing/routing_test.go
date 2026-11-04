// Package routing holds the bound on this milestone's router swap: the paths
// the service already answers, answered the same way.
//
// Routing is the one part of a web service whose behaviour is easy to change
// by accident, because the happy path is identical under any router. The
// inputs below are the ones where two routers that agree on `GET /volumes/vfs`
// do not agree with each other: a trailing slash, a doubled separator, a
// method the path does not serve, a query that discriminates between two
// handlers on the same path, and a percent-encoded byte that is or is not a
// path separator. Each one has exactly one answer on the tree this milestone
// starts from, and that answer is what is written here.
//
// Everything in this package passes before the migration. That is the point:
// it is the part that must not move.
package routing

import (
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"path"
	"strings"
	"testing"
	"time"

	"github.com/akutz/gotil"

	apictx "github.com/codedellemc/libstorage/api/context"
	"github.com/codedellemc/libstorage/api/registry"
	apiserver "github.com/codedellemc/libstorage/api/server"
	"github.com/codedellemc/libstorage/api/types"
	"github.com/codedellemc/libstorage/api/utils"
	apicnfg "github.com/codedellemc/libstorage/api/utils/config"

	// the configuration keys, and a driver for the server to serve
	_ "github.com/codedellemc/libstorage/drivers/storage/vfs/storage"
	_ "github.com/codedellemc/libstorage/imports/config"
)

// configFormat is the configuration the recorded routing stratum runs under:
// one endpoint, one vfs service, logging quiet enough that the server's own
// output is not what these tests are reading.
//
// tasks.logTimeout keeps every finished task in the server's task map for the
// life of the package. The 2017 task service takes a new id
// from len(s.tasks) and, at the default of 0s, drops each task the moment it
// finishes; once one drops ahead of another, the next request reuses a live
// id and its waiter is handed someone else's result -- a body of "null", a
// failed cast, or no answer at all. That race predates the migration and no
// step asks for it to change, so this package does not let it decide a
// routing question: with nothing removed, sequential requests get distinct
// ids. Nothing graded here reads the task list.
const configFormat = `
libstorage:
  host: tcp://%[1]s
  logging:
    level: error
  server:
    tasks:
      logTimeout: 1h
    endpoints:
      localhost:
        address: tcp://%[1]s
    services:
      vfs:
        driver: vfs
vfs:
  root: %[2]s
`

// instanceID is the header the volume routes require; without it the service
// answers 400 before routing has anything to say.
const instanceID = "vfs=i1"

// One server serves the whole package.
//
// Every test here drives the same table against the same configuration, so
// eight servers would buy isolation this package has no use for and would cost
// eight times every start-up, readiness and teardown deadline in the budget a
// failing tree has to fit inside. The one volume the paths are written against
// is seeded once, and nothing below deletes it; the two tests that attach it
// assert on the presence of a field rather than on its contents, so the order
// the testing package runs them in does not matter.
var (
	sharedBase string
	sharedErr  error
	stopShared func()
)

func TestMain(m *testing.M) {
	sharedBase, stopShared, sharedErr = startServer()
	code := m.Run()
	if stopShared != nil {
		stopShared()
	}
	os.Exit(code)
}

// serverURL returns the shared server's base URL, failing the calling test if
// the server never came up. A tree whose routing table the standard router
// rejects panics on its way through registration and never reaches here.
func serverURL(t *testing.T) string {
	t.Helper()
	if sharedErr != nil {
		t.Fatalf("the server this package tests never started: %v", sharedErr)
	}
	return sharedBase
}

// The deadlines below are liveness bounds, not measurements: the whole package
// runs in about two seconds, and each of these is more than an order of
// magnitude above what that costs. They are here so that a tree which starts a
// listener and then never answers fails inside the gate's budget rather than
// at the end of it.
const (
	startDeadline = 20 * time.Second
	readyDeadline = 10 * time.Second
	stopDeadline  = 10 * time.Second
	replyDeadline = 15 * time.Second
)

func startServer() (string, func(), error) {
	apiserver.DisableStartupInfo = true
	os.Setenv("LIBSTORAGE_LOGGING_LEVEL", "error")

	scratch, err := os.MkdirTemp("", "verifierprobe-routing-")
	if err != nil {
		return "", nil, fmt.Errorf("scratch directory: %v", err)
	}
	cleanup := func() { os.RemoveAll(scratch) }

	ctx := apictx.Background()
	pathConfig := utils.NewPathConfig(ctx, scratch, "", scratch)
	ctx = apictx.WithValue(ctx, apictx.PathConfigKey, pathConfig)
	registry.ProcessRegisteredConfigs(ctx)

	laddr := fmt.Sprintf("127.0.0.1:%d", gotil.RandomTCPPort())
	data := fmt.Sprintf(configFormat, laddr, path.Join(scratch, "vfs"))
	if err := os.WriteFile(
		path.Join(pathConfig.Etc, "config.yml"), []byte(data), 0644); err != nil {
		cleanup()
		return "", nil, fmt.Errorf("writing the configuration: %v", err)
	}

	config, err := apicnfg.NewConfig(ctx)
	if err != nil {
		cleanup()
		return "", nil, fmt.Errorf("reading the configuration: %v", err)
	}

	type started struct {
		srv types.Server
		err error
	}
	done := make(chan started, 1)
	go func() {
		srv, errs, err := apiserver.Serve(ctx, config)
		go func() {
			for range errs {
			}
		}()
		done <- started{srv, err}
	}()

	var s started
	select {
	case s = <-done:
	case <-time.After(startDeadline):
		cleanup()
		return "", nil, fmt.Errorf("the server did not start within %s",
			startDeadline)
	}
	if s.err != nil {
		cleanup()
		return "", nil, fmt.Errorf("starting the server: %v", s.err)
	}

	stop := func() {
		stopped := make(chan struct{})
		go func() { defer close(stopped); s.srv.Close() }()
		select {
		case <-stopped:
		case <-time.After(stopDeadline):
		}
		cleanup()
	}

	base := "http://" + laddr
	if err := waitReady(base); err != nil {
		stop()
		return "", nil, err
	}
	if err := seed(base); err != nil {
		stop()
		return "", nil, err
	}
	return base, stop, nil
}

// waitReady blocks until the listener answers, so that a slow start is not
// read as a routing answer.
func waitReady(base string) error {
	deadline := time.Now().Add(readyDeadline)
	for {
		res, err := request("GET", base+"/", "", nil, false)
		if err == nil && res.status != 0 {
			return nil
		}
		if time.Now().After(deadline) {
			return fmt.Errorf(
				"the server never answered its root route within %s: %v",
				readyDeadline, err)
		}
		time.Sleep(100 * time.Millisecond)
	}
}

// seed creates the one volume every path below is written against.
func seed(base string) error {
	r, err := request("POST", base+"/volumes/vfs", "",
		strings.NewReader(`{"name": "r1", "size": 1}`), true)
	if err != nil {
		return fmt.Errorf("seeding a volume: %v", err)
	}
	if r.status != http.StatusCreated {
		return fmt.Errorf("seeding a volume answered %d %q, want 201",
			r.status, r.body)
	}
	if !strings.Contains(r.body, `"id": "vfs-000"`) {
		return fmt.Errorf("seeding a volume produced %q, which is not the "+
			"volume the paths below are written against", r.body)
	}
	return nil
}

// reply is what one exchange produced. The `Allow` header and the `Location`
// header are in here beside the status because both of them are answers this
// package grades: one router advertises the methods a path serves and the
// other does not, and a cleaned path is either redirected or answered where
// it was asked.
type reply struct {
	status int
	body   string
	allow  string
	loc    string
}

// do issues one request with the path left exactly as written -- no cleaning,
// no re-encoding -- because what a router does with `%2F` and with `//` is
// most of what these tests are about. Redirects are not followed, so the
// status is the one the service gave this request rather than the one it gave
// some other path.
//
// The `Allow` header is read back into the reply but not judged here. It is an
// answer to one question -- what a request for a method a path does not serve
// gets -- and grading it on every exchange would price that one answer once
// per input in this package and again in the others that share the tree.
func do(t *testing.T, method, rawURL, rawQuery string,
	body io.Reader, withID bool) (reply, error) {

	t.Helper()
	return request(method, rawURL, rawQuery, body, withID)
}

// request is the same exchange with no test to report to, for the start-up
// path that runs before any test does.
func request(method, rawURL, rawQuery string,
	body io.Reader, withID bool) (reply, error) {

	u, err := url.Parse(rawURL)
	if err != nil {
		return reply{}, err
	}
	u.RawQuery = rawQuery
	req, err := http.NewRequest(method, u.String(), body)
	if err != nil {
		return reply{}, err
	}
	// url.Parse keeps the encoded form in RawPath only when it differs from
	// the decoded one; setting it back explicitly keeps `%2F` on the wire.
	req.URL.Opaque = ""
	if body != nil {
		req.Header.Set("Content-Type", "application/json")
	}
	if withID {
		req.Header.Set("Libstorage-Instanceid", instanceID)
	}
	// http.ErrUseLastResponse stops the client at the redirect instead of
	// walking it. A client that follows one reports the status of somewhere
	// else, and two of the inputs below are redirects on this service.
	client := &http.Client{
		Timeout: replyDeadline,
		CheckRedirect: func(*http.Request, []*http.Request) error {
			return http.ErrUseLastResponse
		},
	}
	res, err := client.Do(req)
	if err != nil {
		return reply{}, err
	}
	defer res.Body.Close()
	out := reply{
		status: res.StatusCode,
		allow:  res.Header.Get("Allow"),
		loc:    res.Header.Get("Location"),
	}
	buf, err := io.ReadAll(res.Body)
	if err != nil {
		return out, err
	}
	out.body = string(buf)
	return out, nil
}

// want is one edge input and the answer it already has.
type want struct {
	name    string
	method  string
	path    string
	query   string
	status  int
	body    string // a substring the answer must contain, "" for no check
	loc     string // the exact Location header, "" for no check
	noAllow bool   // the answer must carry no Allow header
	withID  bool
	send    string // request body, "" for none
}

func run(t *testing.T, base string, cases []want) {
	t.Helper()
	for _, c := range cases {
		var body io.Reader
		if c.send != "" {
			body = strings.NewReader(c.send)
		}
		r, err := do(t, c.method, base+c.path, c.query, body, c.withID)
		if err != nil {
			t.Errorf("%s: %s %s%s: %v", c.name, c.method, c.path, qs(c.query), err)
			continue
		}
		if r.status != c.status {
			t.Errorf("%s: %s %s%s answered %d, want %d (body %q, location %q)",
				c.name, c.method, c.path, qs(c.query), r.status, c.status,
				truncate(r.body), r.loc)
			continue
		}
		if c.body != "" && !strings.Contains(r.body, c.body) {
			t.Errorf("%s: %s %s%s answered %d but the body %q does not "+
				"contain %q, so the request reached a different handler",
				c.name, c.method, c.path, qs(c.query), r.status,
				truncate(r.body), c.body)
		}
		if c.loc != "" && r.loc != c.loc {
			t.Errorf("%s: %s %s%s answered %d pointing at %q, want %q; where "+
				"a redirect sends a client is the answer here, not the fact "+
				"that there was one",
				c.name, c.method, c.path, qs(c.query), r.status, r.loc, c.loc)
		}
		if c.noAllow && r.allow != "" {
			t.Errorf("%s: %s %s%s answered with an Allow header (%q); this "+
				"service does not advertise the methods a path serves, and a "+
				"router that starts doing so has changed what a client sees",
				c.name, c.method, c.path, qs(c.query), r.allow)
		}
	}
}

func qs(q string) string {
	if q == "" {
		return ""
	}
	return "?" + q
}

func truncate(s string) string {
	if len(s) <= 160 {
		return s
	}
	return s[:160] + "..."
}

// TestTheOrdinaryRoutesStillAnswer is the control. Every test above is about
// an input at the edge of the table; this one is the table itself, and it is
// here so that a tree which answers 404 to everything cannot pass the edges
// by agreeing with them.
func TestTheOrdinaryRoutesStillAnswer(t *testing.T) {
	base := serverURL(t)
	run(t, base, []want{
		{name: "root", method: "GET", path: "/", status: 200,
			body: `/services`},
		{name: "services", method: "GET", path: "/services", status: 200,
			body: `"vfs"`},
		{name: "service", method: "GET", path: "/services/vfs", status: 200,
			body: `"name": "vfs"`},
		{name: "volumes", method: "GET", path: "/volumes", status: 200,
			body: `"vfs"`, withID: true},
		{name: "service-volumes", method: "GET", path: "/volumes/vfs",
			status: 200, body: `"vfs-000": {`, withID: true},
		{name: "one-volume", method: "GET", path: "/volumes/vfs/vfs-000",
			status: 200, body: `"id": "vfs-000"`, withID: true},
		{name: "snapshots", method: "GET", path: "/snapshots", status: 200,
			withID: true},
	})
}

// TestTrailingSlashIsNotTheSameRoute pins the first thing two routers
// disagree about. `/volumes/` is not `/volumes` here: it is not a route at
// all, and it is answered with the standard library's own 404 text rather
// than redirected to the route one character shorter.
func TestTrailingSlashIsNotTheSameRoute(t *testing.T) {
	base := serverURL(t)
	run(t, base, []want{
		{name: "trailing-slash-volumes", method: "GET", path: "/volumes/",
			status: 404, body: "404 page not found", withID: true},
		{name: "trailing-slash-service", method: "GET", path: "/volumes/vfs/",
			status: 404, body: "404 page not found", withID: true},
		{name: "trailing-slash-volume", method: "GET",
			path:   "/volumes/vfs/vfs-000/",
			status: 404, body: "404 page not found", withID: true},
	})
}

// TestUnmatchedPathsAreNotFound is the plain case, and the one a route table
// with a catch-all entry gets wrong: a path with nothing registered under it
// is 404, whether it is one segment past a real route or nowhere near one.
func TestUnmatchedPathsAreNotFound(t *testing.T) {
	base := serverURL(t)
	run(t, base, []want{
		{name: "deep-path", method: "GET", path: "/volumes/vfs/vfs-000/extra",
			status: 404, body: "404 page not found", withID: true},
		{name: "upper-volumes", method: "GET", path: "/VOLUMES/vfs",
			status: 404, body: "404 page not found", withID: true},
		{name: "unknown-root", method: "GET", path: "/nope",
			status: 404, body: "404 page not found"},
		{name: "unknown-deep", method: "GET", path: "/a/b/c/d/e",
			status: 404, body: "404 page not found"},
	})
}

// TestAMethodThePathDoesNotServeIsNotFound is the second disagreement, and it
// carries both halves of it: the answer is 404, not 405, and it carries no
// Allow header. A path that exists for one method does not exist for another
// as far as a client of this service can tell. This is the only test in the
// package that reads the header, because this is the only question it answers.
func TestAMethodThePathDoesNotServeIsNotFound(t *testing.T) {
	base := serverURL(t)
	run(t, base, []want{
		{name: "volume-put", method: "PUT", path: "/volumes/vfs/vfs-000",
			status: 404, noAllow: true, withID: true},
		{name: "volume-patch", method: "PATCH", path: "/volumes/vfs/vfs-000",
			status: 404, noAllow: true, withID: true},
		{name: "volume-options", method: "OPTIONS",
			path: "/volumes/vfs/vfs-000", status: 404, noAllow: true,
			withID: true},
		{name: "volume-head", method: "HEAD", path: "/volumes/vfs/vfs-000",
			status: 404, noAllow: true, withID: true},
		{name: "services-delete", method: "DELETE", path: "/services/vfs",
			status: 404, noAllow: true},
		{name: "tasks-delete", method: "DELETE", path: "/tasks/0",
			status: 404, noAllow: true},
	})
}

// TestQueriesDiscriminateBetweenHandlersOnOnePath is the third, and the one
// no path pattern can express: four handlers share `POST
// /volumes/{service}/{volumeID}` and the query string is what tells them
// apart. A request with none of those queries matches none of them -- there
// is no default -- and a request with two matches the one registered first.
func TestQueriesDiscriminateBetweenHandlersOnOnePath(t *testing.T) {
	base := serverURL(t)
	run(t, base, []want{
		{name: "post-volume-no-query", method: "POST",
			path: "/volumes/vfs/vfs-000", status: 404,
			body: "404 page not found", withID: true, send: "{}"},
		{name: "post-volume-unknown-query", method: "POST",
			path: "/volumes/vfs/vfs-000", query: "frobnicate", status: 404,
			body: "404 page not found", withID: true, send: "{}"},
		{name: "post-volume-both-queries", method: "POST",
			path: "/volumes/vfs/vfs-000", query: "attach&detach", status: 200,
			body: `"attachments"`, withID: true, send: `{"force": false}`},
		{name: "post-volume-query-value", method: "POST",
			path: "/volumes/vfs/vfs-000", query: "attach=1", status: 200,
			body: `"attachments"`, withID: true, send: `{"force": false}`},
		{name: "post-snapshot-on-snapshots", method: "POST",
			path: "/snapshots/vfs", query: "snapshot", status: 404,
			body: "404 page not found", withID: true, send: "{}"},
	})
}

// TestPercentEncodingDecidesWhereARequestLands is the fourth, and the one
// with a security answer attached. `%2F` is a separator here, decoded before
// the path is matched: a request for `/volumes/vfs%2Fvfs-000` reaches the
// single-volume route rather than the service-wide one, and a volume ID
// holding `..%2F..%2Fetc%2Fpasswd` is decoded, cleaned, and answered with a
// 301 to `/etc/passwd` -- it reaches no handler in this service at all, and
// where it is sent instead is part of the answer. `%23` does reach one: it is
// a `#` inside an identifier, and the service's own error names it.
func TestPercentEncodingDecidesWhereARequestLands(t *testing.T) {
	base := serverURL(t)
	run(t, base, []want{
		{name: "encoded-slash-id", method: "GET",
			path: "/volumes/vfs/vfs%2D000", status: 200,
			body: `"id": "vfs-000"`, withID: true},
		{name: "encoded-path-sep", method: "GET",
			path: "/volumes/vfs%2Fvfs-000", status: 200,
			body: `"id": "vfs-000"`, withID: true},
		{name: "traversal-id", method: "GET",
			path: "/volumes/vfs/..%2F..%2Fetc%2Fpasswd", status: 301,
			loc: "/etc/passwd", withID: true},
		{name: "fragment-like", method: "GET", path: "/volumes/vfs%23frag",
			status: 404, body: `"resourceID": "vfs#frag"`, withID: true},
		{name: "null-byte-id", method: "GET",
			path: "/volumes/vfs/vfs%00-000", status: 500, withID: true},
		{name: "long-id", method: "GET",
			path: "/volumes/vfs/" + strings.Repeat("z", 1024), status: 500,
			withID: true},
	})
}

// TestTheSingleVolumeRouteIsNotTheServiceRoute is the body half of the
// encoding case, written separately because it is the one input where the
// status is the same either way and only the payload says which handler ran.
func TestTheSingleVolumeRouteIsNotTheServiceRoute(t *testing.T) {
	base := serverURL(t)
	r, err := do(t, "GET", base+"/volumes/vfs%2Fvfs-000", "", nil, true)
	if err != nil {
		t.Fatalf("GET /volumes/vfs%%2Fvfs-000: %v", err)
	}
	if r.status != 200 {
		t.Fatalf("GET /volumes/vfs%%2Fvfs-000 answered %d, want 200 "+
			"(location %q)", r.status, r.loc)
	}
	if strings.Contains(r.body, `"vfs-000": {`) {
		t.Errorf("GET /volumes/vfs%%2Fvfs-000 answered the map of every "+
			"volume in the service (%q); %%2F is a separator on this service "+
			"and the request names one volume", truncate(r.body))
	}
	if !strings.HasPrefix(strings.TrimSpace(r.body), `{`) ||
		!strings.Contains(r.body, `"name": "r1"`) {
		t.Errorf("GET /volumes/vfs%%2Fvfs-000 answered %q, want the one "+
			"volume it names", truncate(r.body))
	}
}

// TestACleanedPathIsRedirectedRatherThanAnswered is the last disagreement,
// and the two halves of it go opposite ways. A doubled separator is cleaned
// out of the path and the request is redirected to the path without it --
// 301, not an answer -- so a router that answers `/volumes//vfs` directly has
// changed what a client sees even though the eventual body is the same. A
// query string that does not parse is survivable and changes nothing: the
// request still reaches the route the path names.
func TestACleanedPathIsRedirectedRatherThanAnswered(t *testing.T) {
	base := serverURL(t)
	run(t, base, []want{
		{name: "double-slash", method: "GET", path: "/volumes//vfs",
			status: 301, loc: "/volumes/vfs", withID: true},
		{name: "query-junk", method: "GET", path: "/volumes/vfs",
			query: "%ZZ=1&=2&3", status: 200, body: `"vfs-000": {`,
			withID: true},
	})
}

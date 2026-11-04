// Package errstatus asks what the server does with an error it did not
// construct itself.
//
// Every error a route returns passes through one global filter on its way out,
// and that filter decides the HTTP status. Today it decides by looking at the
// value in its hand: the error it was given either is a not-found or it is
// not. That works exactly as long as nothing between the route and the filter
// has anything to add -- and the moment a caller supplies the context that
// made the failure legible ("attaching the volume: ..."), the not-found
// becomes a 500 and the client loses the only thing it could have acted on.
//
// The routes below are the probe's own, so the question is asked of error
// values whose identity is known: the same error is served plain and served
// with a layer of context around it, and the two are required to come back
// with the same status. Nothing here names a helper, a type switch or a
// spelling -- only what the client sees.
//
// The controls are what the change is not allowed to cost: the statuses the
// plain errors already have, the exact body of an error response, and the rule
// that an error which already answers for the outcome of an operation keeps
// its own answer when something wraps it. A partly-completed batch is a server
// error whatever stopped it, and a migration that let the cause decide instead
// would quietly re-label it.
package errstatus

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"path"
	"reflect"
	"sync"
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

// failure is one route: a path, and the error the route returns. The plain and
// the wrapped form of each error are separate routes returning the same value
// with and without a layer of context, so that the only difference between the
// two responses is the wrapping.
type failure struct {
	path string
	err  func() error
}

var failures = []failure{
	{"/verifierprobe/err/plain/notfound", func() error {
		return utils.NewNotFoundError("probe")
	}},
	{"/verifierprobe/err/wrapped/notfound", func() error {
		return fmt.Errorf("inspecting the volume: %w",
			utils.NewNotFoundError("probe"))
	}},
	{"/verifierprobe/err/twice/notfound", func() error {
		return fmt.Errorf("serving the request: %w",
			fmt.Errorf("inspecting the volume: %w",
				utils.NewNotFoundError("probe")))
	}},
	{"/verifierprobe/err/plain/instanceid", func() error {
		return utils.NewMissingInstanceIDError("vfs")
	}},
	{"/verifierprobe/err/wrapped/instanceid", func() error {
		return fmt.Errorf("attaching the volume: %w",
			utils.NewMissingInstanceIDError("vfs"))
	}},
	{"/verifierprobe/err/plain/localdevices", func() error {
		return utils.NewMissingLocalDevicesError("vfs")
	}},
	{"/verifierprobe/err/wrapped/localdevices", func() error {
		return fmt.Errorf("attaching the volume: %w",
			utils.NewMissingLocalDevicesError("vfs"))
	}},
	{"/verifierprobe/err/plain/admintoken", func() error {
		return utils.NewBadAdminTokenError("probe")
	}},
	{"/verifierprobe/err/wrapped/admintoken", func() error {
		return fmt.Errorf("authorizing the request: %w",
			utils.NewBadAdminTokenError("probe"))
	}},
	{"/verifierprobe/err/generic", func() error {
		return errors.New("probe generic")
	}},
	{"/verifierprobe/err/batch-over-instanceid", func() error {
		return utils.NewBatchProcessErr(
			map[string]int{}, utils.NewMissingInstanceIDError("vfs"))
	}},
	{"/verifierprobe/err/batch-over-notfound", func() error {
		return utils.NewBatchProcessErr(
			map[string]int{}, utils.NewNotFoundError("probe"))
	}},
}

func init() {
	registry.RegisterRouter(&probeRouter{})
}

// probeRouter serves one route per failure above. It belongs to the scoring
// harness, not to the repository, so the error each route returns cannot be
// adjusted by the tree under test.
type probeRouter struct {
	routes []types.Route
}

func (r *probeRouter) Name() string { return "verifier-probe-errstatus" }

func (r *probeRouter) Init(config gofig.Config) {
	r.routes = nil
	for _, f := range failures {
		build := f.err
		r.routes = append(r.routes, httputils.NewGetRoute(
			"verifier-probe"+f.path, f.path,
			func(ctx types.Context, w http.ResponseWriter,
				req *http.Request, store types.Store) error {
				return build()
			}))
	}
}

func (r *probeRouter) Routes() []types.Route { return r.routes }

var (
	serveOnce sync.Once
	serveAddr string
	serveErr  error
	serveStop func()
)

// serve starts the repository's server once for the whole package and returns
// its address. One server answers every question here, because what is being
// measured is what the server does with an error and not what it does at
// startup.
func serve(t *testing.T) string {
	t.Helper()
	serveOnce.Do(func() { serveAddr, serveStop, serveErr = startBounded() })
	if serveErr != nil {
		t.Fatalf("starting the server: %v", serveErr)
	}
	return serveAddr
}

// startBounded gives the server a fixed time to come up. A tree that never
// finishes starting one fails every assertion in this package, and bounding the
// start is what makes that a reported failure rather than a killed binary with
// no answers in it at all.
func startBounded() (string, func(), error) {
	type started struct {
		addr string
		stop func()
		err  error
	}
	ch := make(chan started, 1)
	go func() {
		addr, stop, err := startServer()
		ch <- started{addr, stop, err}
	}()
	select {
	case s := <-ch:
		return s.addr, s.stop, s.err
	case <-time.After(60 * time.Second):
		return "", nil, errors.New("the server did not finish starting")
	}
}

func startServer() (string, func(), error) {
	apiserver.DisableStartupInfo = true
	os.Setenv("LIBSTORAGE_LOGGING_LEVEL", "panic")

	ctx := apictx.Background()
	etc, err := os.MkdirTemp("", "errstatus-etc")
	if err != nil {
		return "", nil, err
	}
	lib, err := os.MkdirTemp("", "errstatus-lib")
	if err != nil {
		return "", nil, err
	}
	pathConfig := utils.NewPathConfig(ctx, etc, "", lib)
	ctx = apictx.WithValue(ctx, apictx.PathConfigKey, pathConfig)
	registry.ProcessRegisteredConfigs(ctx)

	laddr := fmt.Sprintf("127.0.0.1:%d", gotil.RandomTCPPort())
	data := fmt.Sprintf(configFormat, laddr)
	if err := os.WriteFile(
		path.Join(pathConfig.Etc, "config.yml"), []byte(data), 0644); err != nil {
		return "", nil, err
	}

	config, err := apicnfg.NewConfig(ctx)
	if err != nil {
		return "", nil, err
	}

	srv, errs, err := apiserver.Serve(ctx, config)
	if err != nil {
		return "", nil, err
	}
	go func() {
		for range errs {
		}
	}()

	var once sync.Once
	return laddr, func() { once.Do(func() { closeBounded(srv) }) }, nil
}

// closeBounded stops the server without letting a stop that hangs decide this
// package's result. What stopping the server does is a requirement of its own,
// graded elsewhere; here the stop only has to not take these tests down with
// it.
func closeBounded(srv types.Server) {
	done := make(chan struct{})
	go func() { defer close(done); srv.Close() }()
	select {
	case <-done:
	case <-time.After(30 * time.Second):
	}
}

func TestMain(m *testing.M) {
	code := m.Run()
	if serveStop != nil {
		serveStop()
	}
	os.Exit(code)
}

// askClient bounds every request this package makes, for the same reason the
// start and the stop are bounded: a server that never answers is the answer to
// the question being asked, and waiting on it forever would cost the package's
// other answers as well.
var askClient = &http.Client{Timeout: 15 * time.Second}

// ask issues one GET and returns the status and the decoded body.
func ask(t *testing.T, addr, route string) (int, interface{}) {
	t.Helper()
	res, err := askClient.Get("http://" + addr + route)
	if err != nil {
		t.Fatalf("GET %s: %v", route, err)
	}
	defer res.Body.Close()
	buf, err := io.ReadAll(res.Body)
	if err != nil {
		t.Fatalf("reading the response to %s: %v", route, err)
	}
	var body interface{}
	if err := json.Unmarshal(buf, &body); err != nil {
		t.Fatalf("the response to %s is not JSON (%d): %s",
			route, res.StatusCode, buf)
	}
	return res.StatusCode, body
}

// samePair is the assertion every graded test below makes: the same error
// served plain and served with a layer of context around it has to come back
// with the same status, and that status has to be the one it already has.
func samePair(t *testing.T, name, plainRoute, wrappedRoute string, want int) {
	t.Helper()
	addr := serve(t)

	plain, _ := ask(t, addr, plainRoute)
	if plain != want {
		t.Errorf("%s served plain: status %d, want %d (the control for "+
			"this pair)", name, plain, want)
	}
	wrapped, body := ask(t, addr, wrappedRoute)
	if wrapped != want {
		t.Errorf("%s served with a layer of context around it: status %d, "+
			"want %d -- the context the caller added is deciding what the "+
			"error is\n  body: %v", name, wrapped, want, body)
	}
}

// TestAWrappedNotFoundIsStillANotFound is the requirement, on the status a
// client has the most to do with: 404 is how it learns that the volume it
// asked about is gone rather than that the server broke. It is asked through
// one layer of context and through two, because a chain that survives one link
// is not a chain.
func TestAWrappedNotFoundIsStillANotFound(t *testing.T) {
	samePair(t, "not found",
		"/verifierprobe/err/plain/notfound",
		"/verifierprobe/err/wrapped/notfound", http.StatusNotFound)
	samePair(t, "not found, twice over",
		"/verifierprobe/err/plain/notfound",
		"/verifierprobe/err/twice/notfound", http.StatusNotFound)
}

// TestAWrappedBadRequestIsStillABadRequest is the same requirement on the
// status that separates the client's fault from the server's. Both of the
// errors that carry it are raised deep in a driver, which is exactly where a
// caller has something to add on the way out.
func TestAWrappedBadRequestIsStillABadRequest(t *testing.T) {
	samePair(t, "missing instance ID",
		"/verifierprobe/err/plain/instanceid",
		"/verifierprobe/err/wrapped/instanceid", http.StatusBadRequest)
	samePair(t, "missing local devices",
		"/verifierprobe/err/plain/localdevices",
		"/verifierprobe/err/wrapped/localdevices", http.StatusBadRequest)
}

// TestAWrappedUnauthorizedIsStillUnauthorized is the same requirement on the
// status it is least acceptable to lose: a 500 in place of a 401 tells a
// client to retry something that will never succeed.
func TestAWrappedUnauthorizedIsStillUnauthorized(t *testing.T) {
	samePair(t, "bad admin token",
		"/verifierprobe/err/plain/admintoken",
		"/verifierprobe/err/wrapped/admintoken", http.StatusUnauthorized)
}

// TestPlainErrorsKeepTheirStatus is a control: the statuses the server already
// produces for the errors it already has.
func TestPlainErrorsKeepTheirStatus(t *testing.T) {
	addr := serve(t)

	cases := []struct {
		route string
		want  int
	}{
		{"/verifierprobe/err/plain/notfound", http.StatusNotFound},
		{"/verifierprobe/err/plain/instanceid", http.StatusBadRequest},
		{"/verifierprobe/err/plain/localdevices", http.StatusBadRequest},
		{"/verifierprobe/err/plain/admintoken", http.StatusUnauthorized},
		{"/verifierprobe/err/generic", http.StatusInternalServerError},
	}

	for _, tc := range cases {
		if got, body := ask(t, addr, tc.route); got != tc.want {
			t.Errorf("GET %s: status %d, want %d\n  body: %v",
				tc.route, got, tc.want, body)
		}
	}
}

// TestAnErrorThatAnswersForTheOutcomeKeepsIt is a control, and the one a
// migration is most likely to lose by doing everything else right. A batch
// that partly completed reports its own outcome: it is a server error whatever
// stopped it. The status belongs to the error that reports the outcome, and a
// cause that would have answered differently on its own does not get to answer
// for it.
//
// It is asked over two causes, because they fail through different branches of
// any filter that reads a status off the chain: the missing instance ID that
// stops the first batch is a 400 in its own right and the not-found that stops
// the second is a 404. A walk that lets the innermost opinion win therefore
// returns a different wrong answer to each, and neither 500 can be arrived at
// by a filter that simply never produces the cause's status.
func TestAnErrorThatAnswersForTheOutcomeKeepsIt(t *testing.T) {
	addr := serve(t)

	routes := []string{
		"/verifierprobe/err/batch-over-instanceid",
		"/verifierprobe/err/batch-over-notfound",
	}

	for _, route := range routes {
		got, body := ask(t, addr, route)
		if got != http.StatusInternalServerError {
			t.Errorf("GET %s: status %d, want %d -- the cause is deciding the "+
				"outcome of an operation that reports its own\n  body: %v",
				route, got, http.StatusInternalServerError, body)
		}
	}
}

// TestErrorResponseBodiesAreUnchanged is a control on the wire format itself.
// The two bodies below are the two shapes every error response in this API
// takes: an error that carries structured detail renders that detail, and one
// that carries none renders its message.
//
// The expectations are pins, not choices. Each one was read off the tree this
// step starts from by asking that tree the same route this test asks, so what
// they record is what the server already does rather than what anyone decided
// it should do; the shape of the first three is the same one the recorded
// exchange for a not-found carries on the wire. What makes them trustworthy is
// that the whole package passes against that tree -- a gate in the regression
// family has to score 1.0 on the pre-step tree for this step to calibrate at
// all, so a value typed wrong here does not sit unnoticed, it fails the
// calibration. They cannot be generated at run time: this package is compiled
// inside the candidate's tree, which has none of the verifier's data.
func TestErrorResponseBodiesAreUnchanged(t *testing.T) {
	addr := serve(t)

	cases := []struct {
		route string
		want  string
	}{
		{"/verifierprobe/err/plain/notfound", `{
			"message": "resource not found",
			"status": 404,
			"error": {"resourceID": "probe"}
		}`},
		{"/verifierprobe/err/plain/instanceid", `{
			"message": "missing instance ID",
			"status": 400,
			"error": {"service": "vfs"}
		}`},
		{"/verifierprobe/err/plain/admintoken", `{
			"message": "invalid admin token",
			"status": 401,
			"error": {"token": "probe"}
		}`},
		{"/verifierprobe/err/generic", `{
			"message": "probe generic",
			"status": 500,
			"error": "probe generic"
		}`},
	}

	for _, tc := range cases {
		_, got := ask(t, addr, tc.route)
		var want interface{}
		if err := json.Unmarshal([]byte(tc.want), &want); err != nil {
			t.Fatalf("the expectation is not JSON: %v", err)
		}
		if !reflect.DeepEqual(got, want) {
			gotBuf, _ := json.Marshal(got)
			wantBuf, _ := json.Marshal(want)
			t.Errorf("GET %s:\n   got %s\n  want %s",
				tc.route, gotBuf, wantBuf)
		}
	}
}

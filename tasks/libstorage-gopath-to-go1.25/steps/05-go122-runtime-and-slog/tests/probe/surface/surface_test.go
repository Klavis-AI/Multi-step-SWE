// Package surface holds the part of this milestone that is a bound rather than
// a leap: the exported shape the repository already has.
//
// Replacing a context type touches every signature that mentions one, and
// merging two build-constrained copies of a function into one is an easy place
// to lose the function. The declarations below are compile-time assertions --
// each one names an exported entry point and the exact type it must still
// have, so a parameter that is dropped, reordered, renamed to a different
// type, or returned as something else stops this package from building. The
// two tests then exercise the one entry point nothing in the repository calls,
// because a signature that still compiles is not the same as a function that
// still works.
//
// Everything here passes on the tree this milestone starts from. That is the
// point: it is what must not break.
package surface

import (
	"context"
	"fmt"
	"os"
	"path"
	"testing"
	"time"

	gofig "github.com/akutz/gofig/types"
	"github.com/akutz/gotil"

	libstorage "github.com/codedellemc/libstorage"
	apictx "github.com/codedellemc/libstorage/api/context"
	"github.com/codedellemc/libstorage/api/registry"
	apiserver "github.com/codedellemc/libstorage/api/server"
	"github.com/codedellemc/libstorage/api/types"
	"github.com/codedellemc/libstorage/api/utils"
	apicnfg "github.com/codedellemc/libstorage/api/utils/config"
	"github.com/codedellemc/libstorage/client"

	// the configuration keys, and a driver for the server to serve
	_ "github.com/codedellemc/libstorage/drivers/storage/vfs/storage"
	_ "github.com/codedellemc/libstorage/imports/config"
)

// types.Context is the repository's own context type, and the whole point of
// it is that it can be used wherever the standard one can.
var _ context.Context = types.Context(nil)

// probeKeyType keeps this package's context key to itself.
type probeKeyType int

const probeKey probeKeyType = 0

// surfaceStillCompiles is never called. It is the exported surface that deals
// in contexts, written out as ordinary code so that the compiler has to agree
// with it: each entry point is handed a standard library context and each
// result is assigned to the exact type it has today. A context parameter that
// is dropped, reordered or narrowed to something a standard context no longer
// satisfies, or a result that comes back as something else, stops this package
// building -- and a package that does not build reports no passing tests.
func surfaceStillCompiles(goCtx context.Context, config gofig.Config) {
	var (
		fromParent  types.Context = apictx.New(goCtx)
		fromNothing types.Context = apictx.Background()
		withValue   types.Context = apictx.WithValue(goCtx, probeKey, "v")
	)

	var (
		srv  types.Server
		errs <-chan error
		err  error
	)
	srv, errs, err = apiserver.Serve(goCtx, config)

	var cl types.Client
	cl, err = client.New(goCtx, config)

	var embedded types.Server
	cl, embedded, errs, err = libstorage.New(goCtx, config)

	_, _, _ = fromParent, fromNothing, withValue
	_, _, _, _, _ = srv, errs, err, cl, embedded
}

// configFormat leaves `libstorage.host` unset on purpose: New documents that it
// connects the client it returns to the server it started, which means filling
// that in is its job and not the caller's.
const configFormat = `
libstorage:
  logging:
    level: panic
  client:
    type: controller
  server:
    endpoints:
      localhost:
        address: tcp://%[1]s
    services:
      vfs:
        driver: vfs
`

// newEmbedded runs libstorage.New against a private configuration and returns
// what it produced. Everything it needs -- the paths, the endpoint, the
// service -- is supplied here, so a failure is the function's and not the
// container's.
func newEmbedded(t *testing.T) (types.Client, types.Server, gofig.Config) {
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
		t.Fatalf("writing the configuration: %v", err)
	}

	config, err := apicnfg.NewConfig(ctx)
	if err != nil {
		t.Fatalf("reading the configuration: %v", err)
	}

	done := make(chan struct{})
	var (
		c    types.Client
		s    types.Server
		errs <-chan error
		nerr error
	)
	go func() {
		defer close(done)
		c, s, errs, nerr = libstorage.New(ctx, config)
	}()
	select {
	case <-done:
	case <-time.After(60 * time.Second):
		t.Fatal("libstorage.New did not return within 60s")
	}

	if nerr != nil {
		t.Fatalf("libstorage.New: %v", nerr)
	}
	if c == nil || s == nil {
		t.Fatalf("libstorage.New returned client=%v server=%v, want both", c, s)
	}
	go func() {
		for range errs {
		}
	}()
	t.Cleanup(func() { closeBounded(t, s) })
	return c, s, config
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

// TestEmbeddedNewReturnsAConnectedClient is the root package's own entry
// point, which nothing in the repository calls and which therefore has nothing
// else holding it up. It must still start a server and hand back a client
// already pointed at it.
func TestEmbeddedNewReturnsAConnectedClient(t *testing.T) {
	c, s, config := newEmbedded(t)

	if len(s.Addrs()) == 0 {
		t.Fatal("the server libstorage.New returned is listening nowhere")
	}
	if h := config.GetString(types.ConfigHost); h == "" {
		t.Fatal("libstorage.New left libstorage.host unset, so the client it " +
			"returned was never pointed at the server it started")
	}
	if c.API() == nil {
		t.Fatal("the client libstorage.New returned has no API client")
	}

	roots, err := c.API().Root(apictx.Background())
	if err != nil {
		t.Fatalf("the client libstorage.New returned could not reach the "+
			"server it started: %v", err)
	}
	if len(roots) == 0 {
		t.Fatal("the server answered its root route with nothing")
	}
}

// TestEmbeddedNewServesTheConfiguredService is the other half: the pair is
// wired to the configuration it was given, not to some default.
func TestEmbeddedNewServesTheConfiguredService(t *testing.T) {
	c, _, _ := newEmbedded(t)

	services, err := c.API().Services(apictx.Background())
	if err != nil {
		t.Fatalf("listing the services of the server libstorage.New "+
			"started: %v", err)
	}
	if _, ok := services["vfs"]; !ok {
		t.Fatalf("the embedded server reported services %v, which does not "+
			"include the vfs service it was configured with", keys(services))
	}
}

func keys(m map[string]*types.ServiceInfo) []string {
	out := make([]string, 0, len(m))
	for k := range m {
		out = append(out, k)
	}
	return out
}

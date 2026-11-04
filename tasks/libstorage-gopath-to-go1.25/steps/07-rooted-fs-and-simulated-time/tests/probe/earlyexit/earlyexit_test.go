// Package earlyexit holds the behavioural half of this milestone: what happens
// when a caller stops reading half way through.
//
// Every producer in this repository is written the same way -- make a channel,
// start a goroutine that takes a read lock and sends every element into it,
// hand the channel back -- and every one of them has the same defect. A caller
// that leaves the loop early, and four route handlers in this tree do exactly
// that on the error path, abandons that goroutine blocked on a send nobody
// will ever receive. The goroutine never ends, and because the lock it took is
// released by a deferred call in a function that never returns, it holds that
// lock forever: the next registration of a driver, a router or a context key
// waits behind it for as long as the process lives.
//
// The two symptoms are graded separately. The first is the goroutine, counted
// before and after a walk that stops at the first element. The second is the
// lock, observed through the package's own registration functions with a
// deadline on them, which is the symptom a caller of this repository would
// actually meet.
//
// Test order in this file is load-bearing. A registration left waiting on a
// held read lock is a writer queued on that lock, and a queued writer blocks
// every later reader of it too, so the cheap evidence is gathered first and
// the context keys -- whose lock is taken by every context this package builds
// afterwards -- are asked last.
package earlyexit

import (
	"fmt"
	"os"
	"path"
	"runtime"
	"testing"
	"time"

	apictx "github.com/codedellemc/libstorage/api/context"
	"github.com/codedellemc/libstorage/api/registry"
	"github.com/codedellemc/libstorage/api/server/services"
	"github.com/codedellemc/libstorage/api/types"
	"github.com/codedellemc/libstorage/api/utils"
	apicnfg "github.com/codedellemc/libstorage/api/utils/config"

	gofig "github.com/akutz/gofig/types"

	// a driver for the services to be built from, and the configuration keys
	_ "github.com/codedellemc/libstorage/drivers/storage/vfs/storage"
	_ "github.com/codedellemc/libstorage/imports/config"
)

// settle is how long a walk that has ended is given to have ended: a producer
// that runs on its own goroutine needs a moment to notice, and a producer that
// runs on the caller's is already finished.
const settle = 3 * time.Second

// deadline is how long a registration is given to complete. Nothing in this
// repository's registration path does any work worth measuring -- it takes a
// lock, writes one map entry and returns -- so the only thing that can spend
// this much of it is a lock somebody else is holding.
const deadline = 5 * time.Second

// probeRouter is the smallest thing the registry will accept as a router.
type probeRouter struct{ name string }

func (r *probeRouter) Name() string             { return r.name }
func (r *probeRouter) Routes() []types.Route    { return nil }
func (r *probeRouter) Init(config gofig.Config) {}

// TestMain registers three of everything before any test runs, which is the
// only time it can be done safely: a registration is a write to the same lock
// the walks below abandon a reader on, so a registration attempted after the
// first walk has stopped early is itself a casualty rather than a fixture.
// Three, because stopping a walk at its first element has to leave something
// behind -- a producer with one element to hand out has finished by the time
// the caller has it, and would answer the question without ever having been
// asked it.
func TestMain(m *testing.M) {
	for i := 0; i < 3; i++ {
		n := fmt.Sprintf("early-exit-%d", i)
		registry.RegisterStorageExecutor(
			n, func() types.StorageExecutor { return nil })
		registry.RegisterStorageDriver(
			n, func() types.StorageDriver { return nil })
		registry.RegisterOSDriver(
			n, func() types.OSDriver { return nil })
		registry.RegisterIntegrationDriver(
			n, func() types.IntegrationDriver { return nil })
		registry.RegisterRouter(&probeRouter{name: n})
		if err := apictx.RegisterCustomKey(
			n, apictx.CustomHeaderKey|apictx.CustomLoggerKey); err != nil {
			fmt.Fprintf(os.Stderr, "registering a custom key: %v\n", err)
			os.Exit(1)
		}
	}
	os.Exit(m.Run())
}

// TestLeavingARegistryWalkEarlyEndsTheWalk is the goroutine symptom, asked of
// each of the registry's five producers in turn.
func TestLeavingARegistryWalkEarlyEndsTheWalk(t *testing.T) {
	for _, c := range []struct {
		what string
		walk func()
	}{
		{"storage executors", func() {
			for range registry.StorageExecutors() {
				break
			}
		}},
		{"storage drivers", func() {
			for range registry.StorageDrivers() {
				break
			}
		}},
		{"OS drivers", func() {
			for range registry.OSDrivers() {
				break
			}
		}},
		{"integration drivers", func() {
			for range registry.IntegrationDrivers() {
				break
			}
		}},
		{"routers", func() {
			for range registry.Routers() {
				break
			}
		}},
	} {
		if leaked := walkAndCount(c.walk); leaked > 0 {
			t.Errorf("stopping a walk of the %s at its first element left "+
				"%d goroutine(s) running", c.what, leaked)
		}
	}
}

// TestLeavingAServiceWalkEarlyEndsTheWalk asks the same of the two producers
// that read a server's own state: the storage services it was configured with
// and the tasks it is tracking.
func TestLeavingAServiceWalkEarlyEndsTheWalk(t *testing.T) {
	ctx := newServer(t)
	for i := 0; i < 3; i++ {
		services.TaskTrack(ctx)
	}

	if leaked := walkAndCount(func() {
		for range services.StorageServices(ctx) {
			break
		}
	}); leaked > 0 {
		t.Errorf("stopping a walk of the storage services at its first "+
			"element left %d goroutine(s) running", leaked)
	}

	if leaked := walkAndCount(func() {
		for range services.Tasks(ctx) {
			break
		}
	}); leaked > 0 {
		t.Errorf("stopping a walk of the tasks at its first element left "+
			"%d goroutine(s) running", leaked)
	}
}

// TestLeavingARegistryWalkEarlyLeavesItUsable is the lock symptom, and it is
// the one a caller of this repository meets: after a handler has returned an
// error out of the middle of a walk, can anything still be registered? Each of
// the five producers reads a registry of its own with a lock of its own, so
// each is asked separately and a failure of one says nothing about the others.
func TestLeavingARegistryWalkEarlyLeavesItUsable(t *testing.T) {
	for _, c := range []struct {
		what     string
		walk     func()
		register func(string)
	}{
		{"storage executors", func() {
			for range registry.StorageExecutors() {
				break
			}
		}, func(n string) {
			registry.RegisterStorageExecutor(
				n, func() types.StorageExecutor { return nil })
		}},
		{"storage drivers", func() {
			for range registry.StorageDrivers() {
				break
			}
		}, func(n string) {
			registry.RegisterStorageDriver(
				n, func() types.StorageDriver { return nil })
		}},
		{"OS drivers", func() {
			for range registry.OSDrivers() {
				break
			}
		}, func(n string) {
			registry.RegisterOSDriver(
				n, func() types.OSDriver { return nil })
		}},
		{"integration drivers", func() {
			for range registry.IntegrationDrivers() {
				break
			}
		}, func(n string) {
			registry.RegisterIntegrationDriver(
				n, func() types.IntegrationDriver { return nil })
		}},
		{"routers", func() {
			for range registry.Routers() {
				break
			}
		}, func(n string) {
			registry.RegisterRouter(&probeRouter{name: n})
		}},
	} {
		c.walk()
		name := fmt.Sprintf("after-early-exit-%s", c.what)
		if !within(deadline, func() { c.register(name) }) {
			t.Errorf("after a walk of the %s stopped at its first element, "+
				"registering another one had not returned %s later",
				c.what, deadline)
		}
	}
}

// TestLeavingACustomKeyWalkEarlyLeavesThemUsable asks the same of the context
// package's two producers, and is deliberately the last test in this file:
// the lock it puts under load is the one every context in this package reads.
func TestLeavingACustomKeyWalkEarlyLeavesThemUsable(t *testing.T) {
	for range apictx.CustomHeaderKeys() {
		break
	}
	if !within(deadline, func() {
		apictx.RegisterCustomKey(
			"after-early-exit-header", apictx.CustomHeaderKey)
	}) {
		t.Fatalf("after a walk of the custom header keys stopped at its "+
			"first element, registering another key had not returned %s "+
			"later", deadline)
	}

	for range apictx.CustomLoggerKeys() {
		break
	}
	if !within(deadline, func() {
		apictx.RegisterCustomKey(
			"after-early-exit-logger", apictx.CustomLoggerKey)
	}) {
		t.Fatalf("after a walk of the custom logger keys stopped at its "+
			"first element, registering another key had not returned %s "+
			"later", deadline)
	}
}

// walkAndCount runs a walk that stops early and reports how many goroutines
// outlived it. The count is polled rather than read once: a producer that runs
// on its own goroutine is still unwinding when the caller gets control back,
// and the question here is whether it ever finishes, not how fast. The first
// sample is taken after a collection so that the runtime's own background
// workers exist before it rather than starting between it and the walk, where
// one that stayed for the whole settle window would read as a producer's
// goroutine.
func walkAndCount(walk func()) int {
	runtime.GC()
	before := runtime.NumGoroutine()
	walk()

	after := runtime.NumGoroutine()
	for end := time.Now().Add(settle); time.Now().Before(end); {
		if after <= before {
			return 0
		}
		time.Sleep(50 * time.Millisecond)
		after = runtime.NumGoroutine()
	}
	if after <= before {
		return 0
	}
	return after - before
}

// within runs f on a goroutine of its own and reports whether it returned in
// time. A registration that never returns cannot be cancelled -- a lock wait
// is not interruptible -- so the goroutine is abandoned, which is why the
// order of the tests above matters.
func within(d time.Duration, f func()) bool {
	done := make(chan struct{})
	go func() { defer close(done); f() }()
	select {
	case <-done:
		return true
	case <-time.After(d):
		return false
	}
}

const configFormat = `
libstorage:
  logging:
    level: panic
  server:
    services:
      probe-one:
        driver: vfs
      probe-two:
        driver: vfs
      probe-three:
        driver: vfs
`

// newServer builds the server-side state the service producers read: a
// configuration with three services in it, and a context naming the server
// they belong to. No listener is started.
func newServer(t *testing.T) types.Context {
	t.Helper()
	os.Setenv("LIBSTORAGE_LOGGING_LEVEL", "panic")

	ctx := apictx.Background()
	pathConfig := utils.NewPathConfig(ctx, t.TempDir(), "", t.TempDir())
	ctx = apictx.WithValue(ctx, apictx.PathConfigKey, pathConfig)
	registry.ProcessRegisteredConfigs(ctx)

	if err := os.WriteFile(
		path.Join(pathConfig.Etc, "config.yml"),
		[]byte(configFormat), 0644); err != nil {
		t.Fatalf("writing the configuration: %v", err)
	}
	config, err := apicnfg.NewConfig(ctx)
	if err != nil {
		t.Fatalf("reading the configuration: %v", err)
	}

	ctx = apictx.WithValue(ctx, apictx.ServerKey, t.Name())
	if err := services.Init(ctx, config); err != nil {
		t.Fatalf("initializing the server's services: %v", err)
	}
	return ctx
}

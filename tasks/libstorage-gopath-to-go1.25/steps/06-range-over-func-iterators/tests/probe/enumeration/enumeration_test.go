// Package enumeration is the part of this milestone that is a bound rather
// than a leap: whatever the repository's enumeration entry points are made of,
// they still have to enumerate.
//
// Nothing in here can tell a channel from a sequence, on purpose. Every walk
// below is an ordinary `for v := range f()` loop, which is how both are
// written, and every lookup is the package's own exported one -- so this
// package compiles and passes against the tree this milestone starts from and
// has to keep doing both afterwards. A rewrite that drops the last element, or
// yields nothing at all, or stops answering a lookup whose spelling differs
// only in case, fails here and says so in terms of what was registered rather
// than in terms of how it was stored.
package enumeration

import (
	"encoding/json"
	"fmt"
	"os"
	"path"
	"slices"
	"testing"
	"time"

	gofig "github.com/akutz/gofig/types"

	apictx "github.com/codedellemc/libstorage/api/context"
	"github.com/codedellemc/libstorage/api/registry"
	"github.com/codedellemc/libstorage/api/server/services"
	"github.com/codedellemc/libstorage/api/types"
	"github.com/codedellemc/libstorage/api/utils"
	apicnfg "github.com/codedellemc/libstorage/api/utils/config"

	// a driver for the services to be built from, and the configuration keys
	_ "github.com/codedellemc/libstorage/drivers/storage/vfs/storage"
	_ "github.com/codedellemc/libstorage/imports/config"
)

// probeRouter is the smallest thing the registry will accept as a router.
type probeRouter struct{ name string }

func (r *probeRouter) Name() string             { return r.name }
func (r *probeRouter) Routes() []types.Route    { return nil }
func (r *probeRouter) Init(config gofig.Config) {}

// Three of this repository's channels are not enumeration and are not this
// milestone's business: two of them report that work has finished and the
// third is the same statement made by the interface that declares it. They are
// declared here, in the package that compiles against both the tree this
// milestone starts from and the tree it finishes at, because a rewrite that
// reads "channel" as "sequence" everywhere reaches them too -- and a
// completion signal that has become a sequence of nothing is a change no walk
// in this file would notice.
var (
	_ func(types.Context, int) <-chan int    = services.TaskWaitC
	_ func(types.Context, ...int) <-chan int = services.TaskWaitAllC

	_ func(types.TaskTrackingService, int) <-chan int    = types.TaskTrackingService.TaskWaitC
	_ func(types.TaskTrackingService, ...int) <-chan int = types.TaskTrackingService.TaskWaitAllC
)

// TestEveryRegisteredValueIsStillWalked counts each of the registry's six
// producers before and after three registrations of its own kind. Counting the
// difference rather than the total is deliberate: what else has registered
// itself by init time is not this package's business, and a producer that
// reports everything except the newest entry is exactly what a rewrite that
// mishandles the end of its walk would produce.
//
// The configuration registrations are the sixth, and the one that has to be
// walked with a context: two of the registrations this tree makes at init
// time read the path configuration out of the one they are handed, so a
// context without one panics rather than enumerating.
func TestEveryRegisteredValueIsStillWalked(t *testing.T) {
	ctx := apictx.Background()
	ctx = apictx.WithValue(ctx, apictx.PathConfigKey,
		utils.NewPathConfig(ctx, t.TempDir(), "", t.TempDir()))

	before := counts(ctx)

	const n = 3
	for i := 0; i < n; i++ {
		s := fmt.Sprintf("enumeration-%d", i)
		registry.RegisterStorageExecutor(
			s, func() types.StorageExecutor { return nil })
		registry.RegisterStorageDriver(
			s, func() types.StorageDriver { return nil })
		registry.RegisterOSDriver(
			s, func() types.OSDriver { return nil })
		registry.RegisterIntegrationDriver(
			s, func() types.IntegrationDriver { return nil })
		registry.RegisterRouter(&probeRouter{name: s})
		registry.RegisterConfigReg(
			s, func(types.Context, gofig.ConfigRegistration) {})
	}

	after := counts(ctx)
	for what, got := range after {
		if want := before[what] + n; got != want {
			t.Errorf("walking the %s gave %d of them, want %d",
				what, got, want)
		}
	}

	// A configuration registration carries the name it was registered under,
	// which the other five producers' elements do not, so this walk is asked
	// what came out of it as well as how much.
	registered := []string{}
	for r := range registry.ConfigRegs(ctx) {
		registered = append(registered, r.Name())
	}
	for i := 0; i < n; i++ {
		s := fmt.Sprintf("enumeration-%d", i)
		if !slices.Contains(registered, s) {
			t.Errorf("the configuration registration %q did not come back "+
				"from a walk of them; the walk gave %v", s, registered)
		}
	}
}

// TestTheCustomKeysAreStillWalkedAndStillFiltered covers the context
// package's two producers together, because the thing they have that the
// others do not is a filter: a key registered for the header side only belongs
// to one of these two walks and not to the other.
func TestTheCustomKeysAreStillWalkedAndStillFiltered(t *testing.T) {
	var (
		beforeHeader = keyCount(headerKeys)
		beforeLogger = keyCount(loggerKeys)
	)

	if err := apictx.RegisterCustomKey(
		"enumeration-header-only", apictx.CustomHeaderKey); err != nil {
		t.Fatalf("registering a custom header key: %v", err)
	}
	if err := apictx.RegisterCustomKey(
		"enumeration-logger-only", apictx.CustomLoggerKey); err != nil {
		t.Fatalf("registering a custom logger key: %v", err)
	}
	if err := apictx.RegisterCustomKey(
		"enumeration-both",
		apictx.CustomHeaderKey|apictx.CustomLoggerKey); err != nil {
		t.Fatalf("registering a custom key for both: %v", err)
	}

	if got, want := keyCount(headerKeys), beforeHeader+2; got != want {
		t.Errorf("walking the custom header keys gave %d of them, want %d",
			got, want)
	}
	if got, want := keyCount(loggerKeys), beforeLogger+2; got != want {
		t.Errorf("walking the custom logger keys gave %d of them, want %d",
			got, want)
	}
	if has(headerKeys, "enumeration-logger-only") {
		t.Error("a key registered for the logger only came back from a walk " +
			"of the custom header keys")
	}
	if has(loggerKeys, "enumeration-header-only") {
		t.Error("a key registered for the header only came back from a walk " +
			"of the custom logger keys")
	}
	for _, k := range []string{"enumeration-header-only", "enumeration-both"} {
		if !has(headerKeys, k) {
			t.Errorf("the custom header key %q did not come back from a "+
				"walk of them", k)
		}
	}
	for _, k := range []string{"enumeration-logger-only", "enumeration-both"} {
		if !has(loggerKeys, k) {
			t.Errorf("the custom logger key %q did not come back from a "+
				"walk of them", k)
		}
	}
}

// TestEveryConfiguredServiceIsStillWalked covers the two producers that read a
// server's own state. The services come from the configuration, so the walk
// has to add up to exactly what was configured rather than to a difference;
// the tasks start at none and are counted as they are tracked.
func TestEveryConfiguredServiceIsStillWalked(t *testing.T) {
	ctx := newServer(t)

	got := []string{}
	for s := range services.StorageServices(ctx) {
		got = append(got, s.Name())
	}
	slices.Sort(got)
	if !slices.Equal(got, configuredServices) {
		t.Errorf("walking the storage services gave %v, want %v",
			got, configuredServices)
	}

	n := 0
	for range services.Tasks(ctx) {
		n++
	}
	if n != 0 {
		t.Fatalf("a server that has run nothing is tracking %d tasks", n)
	}
	const tracked = 3
	for i := 0; i < tracked; i++ {
		services.TaskTrack(ctx)
	}
	n = 0
	for range services.Tasks(ctx) {
		n++
	}
	if n != tracked {
		t.Errorf("after tracking %d tasks, walking them gave %d", tracked, n)
	}
}

// TestTheRegistryStillFindsADriverWhateverTheCaseOfItsName is the lookup side
// of the registry rather than the walking side. Every name this registry
// stores is folded to lower case on the way in and every name it is asked for
// is folded the same way on the way out, so all four spellings below are the
// same driver and a name nobody registered is still an error.
func TestTheRegistryStillFindsADriverWhateverTheCaseOfItsName(t *testing.T) {
	registry.RegisterStorageDriver(
		"EnumerationMixedCase", func() types.StorageDriver { return nil })

	for _, name := range []string{
		"EnumerationMixedCase", "enumerationmixedcase",
		"ENUMERATIONMIXEDCASE", "eNuMeRaTiOnMiXeDcAsE",
	} {
		if _, err := registry.NewStorageDriver(name); err != nil {
			t.Errorf("asking the registry for the storage driver %q: %v",
				name, err)
		}
	}
	for _, name := range []string{"vfs", "VFS", "Vfs"} {
		d, err := registry.NewStorageDriver(name)
		if err != nil {
			t.Errorf("asking the registry for the storage driver %q: %v",
				name, err)
			continue
		}
		if d == nil {
			t.Errorf("the registry answered %q with nothing", name)
		}
	}
	if _, err := registry.NewStorageDriver(
		"no-such-driver-was-ever-registered"); err == nil {
		t.Error("the registry answered a name nobody registered without an " +
			"error")
	}
}

// TestTheCompletionSignalsStillSignalCompletion drives the two channels the
// declarations above pin, through the task service that owns them: a task is
// enqueued, and the channel for that one task and the channel for both tasks
// together each have to be readable once the work behind them is done. A
// signal that never arrives and a signal that arrives before the work is
// finished are both failures, so each wait is bounded and the second one asks
// for a result the first task had to have produced.
func TestTheCompletionSignalsStillSignalCompletion(t *testing.T) {
	ctx := newServer(t)

	ran := make(chan string, 2)
	enqueue := func(name string) *types.Task {
		return services.TaskEnqueue(ctx, func(types.Context) (interface{}, error) {
			ran <- name
			return name, nil
		}, nil)
	}

	one := enqueue("one")
	select {
	case <-services.TaskWaitC(ctx, one.ID):
	case <-time.After(deadline):
		t.Fatalf("waiting for task %d to complete gave nothing after %s",
			one.ID, deadline)
	}
	select {
	case name := <-ran:
		if name != "one" {
			t.Errorf("the completed task reported %q, want %q", name, "one")
		}
	default:
		t.Error("the wait for a task finished before the task itself did")
	}

	two, three := enqueue("two"), enqueue("three")
	select {
	case <-services.TaskWaitAllC(ctx, two.ID, three.ID):
	case <-time.After(deadline):
		t.Fatalf("waiting for tasks %d and %d to complete gave nothing "+
			"after %s", two.ID, three.ID, deadline)
	}
	if n := len(ran); n != 2 {
		t.Errorf("the wait for two tasks finished with %d of them run", n)
	}
}

// TestAnEmptyCollectionIsStillAnEmptyCollection is the bound on the other way
// a collection can change when the loop that built it is replaced by a call.
// The store below has nothing in it, and the list of its keys has always been
// an empty list rather than no list at all -- which is a difference of one byte
// where it is written out, `[]` against `null`, and a difference the compiler
// never mentions.
func TestAnEmptyCollectionIsStillAnEmptyCollection(t *testing.T) {
	keys := utils.NewStore().Keys()
	if keys == nil {
		t.Error("the keys of an empty store are no list at all")
	}
	if n := len(keys); n != 0 {
		t.Errorf("the keys of an empty store number %d", n)
	}
	b, err := json.Marshal(keys)
	if err != nil {
		t.Fatalf("writing the keys of an empty store out: %v", err)
	}
	if string(b) != "[]" {
		t.Errorf("the keys of an empty store are written out as %s, want []", b)
	}
}

// deadline bounds each wait above. Every task these tests enqueue returns
// immediately, so this is the distance between "has not happened yet" and "is
// never going to happen" rather than a budget the work is expected to need.
const deadline = 10 * time.Second

// counts walks all six of the registry's producers and reports how long each
// walk was, keyed by what it walked. The configuration registrations are the
// one that takes a context where the other five take nothing, and the context
// it is given has to carry a path configuration.
func counts(ctx types.Context) map[string]int {
	out := map[string]int{
		"storage executors":           0,
		"storage drivers":             0,
		"OS drivers":                  0,
		"integration drivers":         0,
		"routers":                     0,
		"configuration registrations": 0,
	}
	for range registry.ConfigRegs(ctx) {
		out["configuration registrations"]++
	}
	for range registry.StorageExecutors() {
		out["storage executors"]++
	}
	for range registry.StorageDrivers() {
		out["storage drivers"]++
	}
	for range registry.OSDrivers() {
		out["OS drivers"]++
	}
	for range registry.IntegrationDrivers() {
		out["integration drivers"]++
	}
	for range registry.Routers() {
		out["routers"]++
	}
	return out
}

// headerKeys and loggerKeys walk the two context-package producers. They are
// written as functions taking a callback rather than returning a slice so that
// the walk itself -- the thing under test -- is the only shape this file needs
// in two places.
func headerKeys(visit func(interface{})) {
	for k := range apictx.CustomHeaderKeys() {
		visit(k)
	}
}

func loggerKeys(visit func(interface{})) {
	for k := range apictx.CustomLoggerKeys() {
		visit(k)
	}
}

func keyCount(walk func(func(interface{}))) int {
	n := 0
	walk(func(interface{}) { n++ })
	return n
}

func has(walk func(func(interface{})), want string) bool {
	found := false
	walk(func(k interface{}) {
		if s, ok := k.(string); ok && s == want {
			found = true
		}
	})
	return found
}

// configuredServices is what the configuration below asks for, sorted, which
// is what a walk of the storage services has to add up to.
var configuredServices = []string{"probe-one", "probe-three", "probe-two"}

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

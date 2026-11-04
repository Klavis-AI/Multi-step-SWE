// Package sequences holds the typed half of this milestone: what the
// repository's enumeration entry points hand back.
//
// A function that returns a channel and a function that returns a sequence are
// both written `for v := range f()` at the call site, so the call sites cannot
// tell which one a producer is. The declarations below can: each one names a
// producer and the exact type it must have, so a producer still returning a
// channel -- or returning a sequence of the wrong element type, or one wrapped
// in a type of the candidate's own -- stops this package from building, and a
// package that does not build reports no passing tests.
//
// The tests then ask for the two things the type alone does not promise. A
// sequence is a function, so walking it twice runs it twice and must produce
// the same elements both times; a channel handed out through a sequence-shaped
// wrapper is drained by the first walk and empty on the second. That is asked
// of every producer here and not only of the one the test below names: each
// test calls its producer once, keeps the value it was handed, and walks that
// same value twice, because a wrapper that builds a fresh channel on each call
// is re-walkable only for as long as nobody walks one value again. And the one
// entry point that takes a sequence rather than returning one must still read
// all of what it is given. And nothing may be running beside the
// walk: a sequence yields its values from the calling goroutine, so the
// number of goroutines in the process is the same in the middle of a walk
// as it was before the walk started.
package sequences

import (
	"fmt"
	"iter"
	"os"
	"path"
	"runtime"
	"slices"
	"testing"

	gofig "github.com/akutz/gofig/types"

	apictx "github.com/codedellemc/libstorage/api/context"
	"github.com/codedellemc/libstorage/api/registry"
	"github.com/codedellemc/libstorage/api/server/handlers"
	"github.com/codedellemc/libstorage/api/server/services"
	"github.com/codedellemc/libstorage/api/types"
	"github.com/codedellemc/libstorage/api/utils"
	apicnfg "github.com/codedellemc/libstorage/api/utils/config"

	// a driver for the services to be built from, and the configuration keys
	_ "github.com/codedellemc/libstorage/drivers/storage/vfs/storage"
	_ "github.com/codedellemc/libstorage/imports/config"
)

// The producers, written out as the types they must have. Each line is an
// assignment the compiler has to agree with: the function on the right is
// assigned to the type on the left, so a result that is still a channel, or a
// sequence over a different element type, or a named type whose underlying
// type happens to match, is a compile error rather than a silent pass.
var (
	_ func() iter.Seq[interface{}] = apictx.CustomHeaderKeys
	_ func() iter.Seq[interface{}] = apictx.CustomLoggerKeys

	_ func(types.Context) iter.Seq[gofig.ConfigRegistration] = registry.ConfigRegs
	_ func() iter.Seq[types.StorageExecutor]                 = registry.StorageExecutors
	_ func() iter.Seq[types.StorageDriver]                   = registry.StorageDrivers
	_ func() iter.Seq[types.OSDriver]                        = registry.OSDrivers
	_ func() iter.Seq[types.IntegrationDriver]               = registry.IntegrationDrivers
	_ func() iter.Seq[types.Router]                          = registry.Routers

	_ func(types.Context) iter.Seq[types.StorageService] = services.StorageServices
	_ func(types.Context) iter.Seq[*types.Task]          = services.Tasks

	// the same method through the interface that declares it, which is what
	// the implementation behind the two package functions above has to satisfy
	_ func(types.TaskTrackingService) iter.Seq[*types.Task] = types.TaskTrackingService.Tasks

	// and the one entry point that takes a sequence instead of returning one
	_ func(iter.Seq[types.StorageService]) types.Middleware = handlers.NewInstanceIDHandler
)

// probeRouter is the smallest thing the registry will accept as a router: the
// registry stores what it is given and hands it back, so nothing here is ever
// asked to serve anything.
type probeRouter struct{ name string }

func (r *probeRouter) Name() string             { return r.name }
func (r *probeRouter) Routes() []types.Route    { return nil }
func (r *probeRouter) Init(config gofig.Config) {}

// TestASequenceCanBeWalkedTwice is the test a channel behind a
// sequence-shaped wrapper fails. A sequence is a function: calling it starts
// the walk, so the same value walked twice yields the same elements twice. A
// wrapper that makes its channel once, outside the returned function, yields
// everything on the first walk and nothing on the second.
func TestASequenceCanBeWalkedTwice(t *testing.T) {
	names := []string{"twice-a", "twice-b", "twice-c", "twice-d"}
	for _, n := range names {
		registry.RegisterRouter(&probeRouter{name: n})
	}

	seq := registry.Routers()
	first := routerNames(slices.Collect(seq))
	second := routerNames(slices.Collect(seq))

	for _, n := range names {
		if !slices.Contains(first, n) {
			t.Fatalf("walking the routers gave %v, which is missing the "+
				"registered router %q", first, n)
		}
	}
	if !slices.Equal(first, second) {
		t.Fatalf("the same sequence walked twice gave %v and then %v; a "+
			"sequence is a function and walking it again walks it again",
			first, second)
	}
}

// TestTheWalkRunsOnTheCallersGoroutine is the test a channel behind a
// sequence-shaped wrapper fails even when the wrapper is careful. A wrapper can
// be made re-walkable, by building a fresh channel on each call, and it can be
// made to shut its producer down when the consumer stops; what it cannot do is
// deliver values without something running to send them. A sequence has
// nothing to run: the producer's body and the loop body are frames on the same
// goroutine, and the process holds exactly as many goroutines in the middle of
// the walk as it held before it.
//
// The count is taken after a collection, so that the runtime's own background
// workers exist before the first sample rather than appearing between two of
// them, and the walk is long enough that a producer would have to be scheduled
// during it.
//
// The whole measurement is attempted three times and the first clean attempt
// is the answer, because the runtime is free to start a worker of its own
// between the two samples and one sample cannot tell that apart from a
// producer's goroutine. An attempt is clean only if it walked the whole
// registry as well, so a walk that came up short is attempted again rather
// than taken for a quiet census. The retry does not weaken what is asserted: a
// channel-backed producer starts a goroutine on every walk, so it reads one
// more on all three attempts and only a coincidence that repeated itself three
// times over could carry it through. The numbers reported below are the last
// attempt's, and so is the length of the walk.
func TestTheWalkRunsOnTheCallersGoroutine(t *testing.T) {
	for i := 0; i < 8; i++ {
		registry.RegisterRouter(
			&probeRouter{name: fmt.Sprintf("callers-goroutine-%d", i)})
	}

	var before, peak, after, n int
	for attempt := 0; attempt < 3; attempt++ {
		runtime.GC()
		before = runtime.NumGoroutine()

		peak, n = before, 0
		for range registry.Routers() {
			n++
			if g := runtime.NumGoroutine(); g > peak {
				peak = g
			}
		}
		after = runtime.NumGoroutine()

		if n >= 8 && peak <= before && after <= before {
			break
		}
	}

	if n < 8 {
		t.Fatalf("walking the routers gave %d of them, want at least 8", n)
	}
	if peak > before {
		t.Errorf("%d goroutine(s) were running before the walk of the routers "+
			"and %d during it; a sequence yields from the goroutine that is "+
			"walking it", before, peak)
	}
	if after > before {
		t.Errorf("%d goroutine(s) were running before the walk of the routers "+
			"and %d after it", before, after)
	}
}

// TestEveryRegistrySequenceYieldsWhatWasRegistered walks each of the registry's
// five producers through the standard collector, which is a second statement
// about the type as well as about the contents: `slices.Collect` takes a
// sequence and nothing else.
//
// Each producer is called once and the value it returns is walked twice, so
// what the test above asks of the routers is asked of the other four here. The
// elements are constructors returning nil and routers the registry stored, so
// the two walks are compared on how many came out of them.
func TestEveryRegistrySequenceYieldsWhatWasRegistered(t *testing.T) {
	baseExec := len(slices.Collect(registry.StorageExecutors()))
	baseStor := len(slices.Collect(registry.StorageDrivers()))
	baseOS := len(slices.Collect(registry.OSDrivers()))
	baseInt := len(slices.Collect(registry.IntegrationDrivers()))
	baseRtr := len(slices.Collect(registry.Routers()))

	const n = 3
	for i := 0; i < n; i++ {
		s := fmt.Sprintf("collect-%d", i)
		registry.RegisterStorageExecutor(s, func() types.StorageExecutor { return nil })
		registry.RegisterStorageDriver(s, func() types.StorageDriver { return nil })
		registry.RegisterOSDriver(s, func() types.OSDriver { return nil })
		registry.RegisterIntegrationDriver(s, func() types.IntegrationDriver { return nil })
		registry.RegisterRouter(&probeRouter{name: s})
	}

	seqExec := registry.StorageExecutors()
	seqStor := registry.StorageDrivers()
	seqOS := registry.OSDrivers()
	seqInt := registry.IntegrationDrivers()
	seqRtr := registry.Routers()

	for _, c := range []struct {
		what  string
		got   int
		again int
		want  int
	}{
		{"storage executors", len(slices.Collect(seqExec)), len(slices.Collect(seqExec)), baseExec + n},
		{"storage drivers", len(slices.Collect(seqStor)), len(slices.Collect(seqStor)), baseStor + n},
		{"OS drivers", len(slices.Collect(seqOS)), len(slices.Collect(seqOS)), baseOS + n},
		{"integration drivers", len(slices.Collect(seqInt)), len(slices.Collect(seqInt)), baseInt + n},
		{"routers", len(slices.Collect(seqRtr)), len(slices.Collect(seqRtr)), baseRtr + n},
	} {
		if c.got != c.want {
			t.Errorf("walking the %s gave %d of them, want %d",
				c.what, c.got, c.want)
		}
		if c.again != c.got {
			t.Errorf("the same sequence of %s walked twice gave %d of them "+
				"and then %d; a sequence is a function and walking it again "+
				"walks it again", c.what, c.got, c.again)
		}
	}
}

// TestTheContextKeySequencesYieldTheirKeys covers the two producers in the
// context package, which are filtered rather than plain: a key is registered
// for the header side, the logger side or both, and each sequence carries only
// the keys whose registration asked for it.
//
// Each of the two is called once and walked twice, and these elements have
// names, so the second walk has to give back the keys the first one did rather
// than merely as many of them. The registrations behind them live in a map, so
// the keys are put in order before the two walks are compared.
func TestTheContextKeySequencesYieldTheirKeys(t *testing.T) {
	baseHeader := len(slices.Collect(apictx.CustomHeaderKeys()))
	baseLogger := len(slices.Collect(apictx.CustomLoggerKeys()))

	if err := apictx.RegisterCustomKey(
		"probe-header-key", apictx.CustomHeaderKey); err != nil {
		t.Fatalf("registering a custom header key: %v", err)
	}
	if err := apictx.RegisterCustomKey(
		"probe-logger-key", apictx.CustomLoggerKey); err != nil {
		t.Fatalf("registering a custom logger key: %v", err)
	}

	headerSeq := apictx.CustomHeaderKeys()
	loggerSeq := apictx.CustomLoggerKeys()
	header := slices.Collect(headerSeq)
	logger := slices.Collect(loggerSeq)

	if len(header) != baseHeader+1 {
		t.Errorf("walking the custom header keys gave %d of them, want %d",
			len(header), baseHeader+1)
	}
	if len(logger) != baseLogger+1 {
		t.Errorf("walking the custom logger keys gave %d of them, want %d",
			len(logger), baseLogger+1)
	}
	if !slices.Contains(header, interface{}("probe-header-key")) {
		t.Errorf("the custom header keys %v do not include the header key "+
			"that was just registered", header)
	}
	if slices.Contains(header, interface{}("probe-logger-key")) {
		t.Errorf("the custom header keys %v include a key registered for "+
			"the logger only", header)
	}
	if !slices.Contains(logger, interface{}("probe-logger-key")) {
		t.Errorf("the custom logger keys %v do not include the logger key "+
			"that was just registered", logger)
	}

	firstHeader, againHeader := keyNames(header), keyNames(slices.Collect(headerSeq))
	if !slices.Equal(firstHeader, againHeader) {
		t.Errorf("the same sequence of custom header keys walked twice gave "+
			"%v and then %v; a sequence is a function and walking it again "+
			"walks it again", firstHeader, againHeader)
	}
	firstLogger, againLogger := keyNames(logger), keyNames(slices.Collect(loggerSeq))
	if !slices.Equal(firstLogger, againLogger) {
		t.Errorf("the same sequence of custom logger keys walked twice gave "+
			"%v and then %v; a sequence is a function and walking it again "+
			"walks it again", firstLogger, againLogger)
	}
}

// TestTheServiceAndTaskSequencesYieldTheirContents covers the two producers
// that read a running server's state rather than a package-level registry, and
// the interface method behind the second of them.
//
// Each of the two is called once and walked twice: the services by the names
// they were configured under, sorted the way the first walk's names already
// are, and the tasks by how many of them came back, since a task's identity is
// the number the server gave it.
func TestTheServiceAndTaskSequencesYieldTheirContents(t *testing.T) {
	ctx := newServer(t)

	svcSeq := services.StorageServices(ctx)
	svcs := slices.Collect(svcSeq)
	if len(svcs) != len(configuredServices) {
		t.Fatalf("walking the storage services gave %d of them, want %d",
			len(svcs), len(configuredServices))
	}
	got := make([]string, 0, len(svcs))
	for _, s := range svcs {
		got = append(got, s.Name())
	}
	slices.Sort(got)
	if !slices.Equal(got, configuredServices) {
		t.Errorf("walking the storage services gave %v, want %v",
			got, configuredServices)
	}
	again := make([]string, 0, len(svcs))
	for s := range svcSeq {
		again = append(again, s.Name())
	}
	slices.Sort(again)
	if !slices.Equal(got, again) {
		t.Errorf("the same sequence of storage services walked twice gave %v "+
			"and then %v; a sequence is a function and walking it again "+
			"walks it again", got, again)
	}

	if tasks := slices.Collect(services.Tasks(ctx)); len(tasks) != 0 {
		t.Fatalf("a server that has run nothing has %d tasks", len(tasks))
	}
	const n = 3
	for i := 0; i < n; i++ {
		services.TaskTrack(ctx)
	}
	taskSeq := services.Tasks(ctx)
	tasks := slices.Collect(taskSeq)
	if len(tasks) != n {
		t.Fatalf("after tracking %d tasks, walking them gave %d",
			n, len(tasks))
	}
	if againTasks := len(slices.Collect(taskSeq)); againTasks != len(tasks) {
		t.Errorf("the same sequence of tasks walked twice gave %d of them and "+
			"then %d; a sequence is a function and walking it again walks it "+
			"again", len(tasks), againTasks)
	}
}

// TestTheInstanceIDHandlerReadsTheWholeSequence is the parameter side. The
// constructor builds a service-to-driver map out of what it is given, so it
// has to walk all of it; the sequence handed to it here counts what it was
// asked for.
func TestTheInstanceIDHandlerReadsTheWholeSequence(t *testing.T) {
	ctx := newServer(t)

	yielded := 0
	counted := func(yield func(types.StorageService) bool) {
		for s := range services.StorageServices(ctx) {
			yielded++
			if !yield(s) {
				return
			}
		}
	}

	h := handlers.NewInstanceIDHandler(counted)
	if h == nil {
		t.Fatal("the instance ID handler constructor returned nothing")
	}
	if yielded != len(configuredServices) {
		t.Errorf("the instance ID handler read %d of the %d services it was "+
			"handed", yielded, len(configuredServices))
	}
}

func routerNames(routers []types.Router) []string {
	out := make([]string, 0, len(routers))
	for _, r := range routers {
		out = append(out, r.Name())
	}
	slices.Sort(out)
	return out
}

// keyNames is the comparable form of a walk of custom keys. A key is an
// `interface{}`, so what two walks are compared on is the rendered form of
// each one; they are sorted because the registrations behind them are a map
// and the order of a walk of a map is not the same twice.
func keyNames(keys []interface{}) []string {
	out := make([]string, 0, len(keys))
	for _, k := range keys {
		out = append(out, fmt.Sprint(k))
	}
	slices.Sort(out)
	return out
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

// newServer builds the server-side state the two producers above read: a
// configuration with three services in it, and a context naming the server
// they belong to. No listener is started -- the producers read the service
// container, not the network.
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

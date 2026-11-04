// Package cleanup asks what a server leaves running after it has been told to
// stop, and whether a server that has not been told to stop still works.
//
// A goroutine that outlives the thing it was started for is not visible from
// the outside: the server stops accepting, the wait returns, and every answer
// is correct. What is left behind is a goroutine parked on a channel nobody
// will ever send to again, holding everything it captured. The only way to
// observe it is to count.
//
// Counting goroutines is inherently approximate -- the runtime starts its own,
// and a goroutine on its way out is still running for a moment after the
// function that waits for it returns. Every assertion here is therefore a
// difference against a baseline taken in the same process, with a settle
// window generous enough that an exiting goroutine has finished exiting, and
// a tolerance wide enough that the runtime's own bookkeeping cannot fail a
// tree that has done the work.
//
// Every one of these tests reaches the server through exported API only, so a
// tree is free to solve the problem any way it likes. The one thing it may not
// do is change those signatures, because then this package stops compiling and
// stops being able to say anything at all.
package cleanup

import (
	"fmt"
	"net"
	"os"
	"path"
	"runtime"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/akutz/gotil"

	apictx "github.com/codedellemc/libstorage/api/context"
	"github.com/codedellemc/libstorage/api/registry"
	apiserver "github.com/codedellemc/libstorage/api/server"
	"github.com/codedellemc/libstorage/api/server/services"
	"github.com/codedellemc/libstorage/api/types"
	"github.com/codedellemc/libstorage/api/utils"
	apicnfg "github.com/codedellemc/libstorage/api/utils/config"

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

// deadline bounds every wait for something that is supposed to happen. It is
// long enough that a loaded machine is not the reason a test fails.
const deadline = 15 * time.Second

// overlap is how long a second unit of work is given to start running while
// the first one is still running, before concluding that it is not going to.
// It only has to be long enough that a service which does run both at once
// has certainly done so; the wait is paid once per hand-over whether the
// tree is right or wrong.
const overlap = 250 * time.Millisecond

// settle waits for goroutines that are on their way out to finish leaving,
// then reports how many are running. It is deliberately generous: the
// question is whether a goroutine is parked forever, not how promptly it
// exits.
//
// It reports the smallest count it saw in the second half of the window
// rather than the last one. A goroutine that is about to exit has already
// been counted once; a momentary spike from the runtime's own work has not
// yet been. Taking the minimum of several late samples answers "how many are
// still here" instead of "how many happened to be here at the instant I
// stopped looking", which is the difference between a measurement and a
// coin toss.
func settle() int {
	const samples = 60
	best := -1
	for i := 0; i < samples; i++ {
		runtime.Gosched()
		time.Sleep(10 * time.Millisecond)
		if n := runtime.NumGoroutine(); i >= samples/2 && (best < 0 || n < best) {
			best = n
		}
	}
	return best
}

// dump returns every goroutine's stack, so a failure says what leaked rather
// than only that something did.
func dump() string {
	buf := make([]byte, 1<<20)
	return string(buf[:runtime.Stack(buf, true)])
}

// server is one running server and everything a test needs to talk to it.
type server struct {
	addr string
	ctx  types.Context
	svc  types.StorageService
	stop func()
}

// serve starts a server and returns a handle to it. The storage service is
// resolved while the server is still up, because a server that cleans up
// after itself is entitled to forget it ever existed once it has stopped.
func serve(t *testing.T) *server {
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

	sctx := apictx.WithValue(ctx, apictx.ServerKey, srv.Name())
	svc := services.GetStorageService(sctx, "vfs")
	if svc == nil {
		t.Fatalf("the running server has no vfs storage service")
	}

	var once sync.Once
	s := &server{
		addr: laddr,
		ctx:  sctx,
		svc:  svc,
		stop: func() { once.Do(func() { srv.Close() }) },
	}
	t.Cleanup(s.stop)
	return s
}

// enqueue submits one unit of work to a storage service and returns a channel
// that is closed when that work has run.
func enqueue(s *server) <-chan struct{} {
	ran := make(chan struct{})
	s.svc.TaskEnqueue(s.ctx, func(types.Context, types.StorageService) (
		interface{}, error) {
		close(ran)
		return nil, nil
	}, nil)
	return ran
}

// runWork hands one unit of work to a running service and waits for it,
// checking on the way that the service is the thing that runs it, and that
// it is a service running it rather than a goroutine per unit.
//
// The work blocks until this function releases it, which makes both of those
// questions answerable. If handing over the work has already come back while
// that work is still sitting there, the service accepted it and something
// else is running it; if the hand-over is still in progress, the caller is
// running it on its own goroutine -- which leaks nothing, and is also not a
// task queue. And while the first unit is held, a second one handed to the
// same service must wait for it: a service runs the work it is given one
// unit at a time, and a tree that answers every question about leaks by
// starting a goroutine per unit of work has not given anything a lifetime,
// it has just made the lifetimes short.
//
// Both of the trees this step starts from answer all of this the same way
// the reference does, so these are properties to preserve, not to acquire.
func runWork(t *testing.T, s *server, what string) {
	t.Helper()

	started := make(chan struct{})
	finished := make(chan struct{})
	returned := make(chan struct{})
	release := make(chan struct{})
	free := sync.OnceFunc(func() { close(release) })
	defer free()

	go func() {
		defer close(returned)
		s.svc.TaskEnqueue(s.ctx, func(types.Context, types.StorageService) (
			interface{}, error) {
			close(started)
			<-release
			close(finished)
			return nil, nil
		}, nil)
	}()

	select {
	case <-started:
	case <-time.After(deadline):
		t.Fatalf("%s: work given to a running server had not begun to run "+
			"after %s\n\n%s", what, deadline, dump())
	}
	select {
	case <-returned:
	case <-time.After(deadline):
		t.Fatalf("%s: handing work to a running server had not come back "+
			"after %s, while that work was still running: the submitter is "+
			"running it itself\n\n%s", what, deadline, dump())
	}

	// The first unit is still held. A second one may be accepted, but it may
	// not run until the first is done with.
	second := enqueue(s)
	select {
	case <-second:
		free()
		t.Fatalf("%s: a second unit of work ran while the first one was "+
			"still running: this service runs everything it is given at "+
			"once\n\n%s", what, dump())
	case <-time.After(overlap):
	}

	free()
	select {
	case <-second:
	case <-time.After(deadline):
		t.Fatalf("%s: a second unit of work never ran once the first had "+
			"finished\n\n%s", what, dump())
	}
	select {
	case <-finished:
	case <-time.After(deadline):
		t.Fatalf("%s: work given to a running server never finished\n\n%s",
			what, dump())
	}
}

// TestAStoppedServerLeavesNoGoroutineBehind is the executor question. Each
// storage service starts a goroutine that waits on a queue for work, and
// stopping the server does not tell it to stop waiting, so it stays parked on
// that receive for the life of the process. One start leaks one goroutine,
// which is too close to the runtime's own noise to assert on; the servers are
// therefore started and stopped enough times that the leak is unmistakable
// and a tolerance wide enough to absorb the noise still catches it.
//
// Each cycle also runs one task, so a tree cannot pass by never starting the
// executor in the first place: the work it is given has to actually run while
// the server is up, and nothing may be left over once it is down.
//
// And each cycle hands the service more work once it has been stopped. That
// is the other edge of the same queue: a hand-over that can no longer be
// accepted must end rather than wait, so late work is a no-op rather than a
// goroutine holding it forever. It does not have to run -- the service is
// stopped -- but it may not still be trying to when this test counts. There
// is more of it than a queue is likely to be able to swallow without a
// reader, because a hand-over that ends because there happened to be room
// for it has not ended for the right reason.
func TestAStoppedServerLeavesNoGoroutineBehind(t *testing.T) {
	const cycles = 6
	const late = 16

	before := settle()

	for i := 0; i < cycles; i++ {
		s := serve(t)
		runWork(t, s, fmt.Sprintf("cycle %d", i))
		s.stop()
		for j := 0; j < late; j++ {
			enqueue(s)
		}
	}

	after := settle()
	if after > before+2 {
		t.Errorf("%d start/stop cycles left %d goroutines running "+
			"(%d before, %d after)\n\n%s",
			cycles, after-before, before, after, dump())
	}
}

// executorFrames returns the goroutine stacks that are sitting inside the
// storage-service package. A goroutine parked there after every server has
// been stopped is the executor loop: it waits on a queue for a send that can
// no longer come.
func executorFrames() []string {
	var out []string
	for _, g := range strings.Split(dump(), "\n\n") {
		if strings.Contains(g, "libstorage/api/server/services") {
			out = append(out, strings.SplitN(g, "\n", 2)[0])
		}
	}
	return out
}

// TestAStoppedServerLeavesNoTaskExecutorRunning says the same thing as the
// count above, but by name rather than by arithmetic: once the server is
// stopped, nothing should still be sitting in the code that runs its tasks.
// A count can be argued with; a stack cannot.
//
// It is a difference against a baseline for the same reason every other test
// here is: whatever an earlier test in this process left behind is not this
// server's doing, and this test's question is only what this server added.
func TestAStoppedServerLeavesNoTaskExecutorRunning(t *testing.T) {
	settle()
	before := executorFrames()

	s := serve(t)
	runWork(t, s, "the one server")
	s.stop()
	settle()

	if after := executorFrames(); len(after) > len(before) {
		t.Errorf("starting and stopping one server left %d more goroutine(s) "+
			"running inside its task-execution code than were running before "+
			"it started (%d before, %d after):\n\t%s",
			len(after)-len(before), len(before), len(after),
			strings.Join(after, "\n\t"))
	}
}

// TestWorkSubmittedToAStoppedServerLeavesNothingRunning is the other half of
// the same lifetime. Stopping the consumer of a queue is only half an answer
// while anything can still be handed to it: work submitted to a service that
// has already stopped must not leave a goroutine waiting to deliver it, and
// must not take the process down either.
func TestWorkSubmittedToAStoppedServerLeavesNothingRunning(t *testing.T) {
	const submissions = 6

	before := settle()

	s := serve(t)
	s.stop()
	for i := 0; i < submissions; i++ {
		enqueue(s)
	}

	after := settle()
	if after > before+2 {
		t.Errorf("%d units of work submitted to a stopped server left %d "+
			"goroutines running (%d before, %d after)\n\n%s",
			submissions, after-before, before, after, dump())
	}
}

// TestServersStoppedConcurrentlyLeaveNothingBehind stops several servers at
// once. Shutdown that is only correct when it happens alone is not shutdown,
// and a queue torn down from two goroutines at once is a panic rather than a
// leak, so this asks for both at the same time.
func TestServersStoppedConcurrentlyLeaveNothingBehind(t *testing.T) {
	const servers = 6

	before := settle()

	handles := make([]*server, servers)
	for i := range handles {
		handles[i] = serve(t)
		runWork(t, handles[i], fmt.Sprintf("server %d", i))
	}
	var wg sync.WaitGroup
	for _, s := range handles {
		wg.Add(1)
		go func() {
			defer wg.Done()
			s.stop()
		}()
	}
	wg.Wait()

	after := settle()
	if after > before+2 {
		t.Errorf("%d servers stopped at once left %d goroutines running "+
			"(%d before, %d after)\n\n%s",
			servers, after-before, before, after, dump())
	}
}

// TestARunningServerIsStillServing is the control. Every assertion above is
// satisfied by a server that never starts anything in the first place, or by
// one that tears itself down so eagerly that it stops answering. This one
// asks the two questions that rule both out: while it is running, is it still
// reachable, and does it still do the work it is given?
func TestARunningServerIsStillServing(t *testing.T) {
	s := serve(t)
	defer s.stop()

	conn, err := net.DialTimeout("tcp", s.addr, deadline)
	if err != nil {
		t.Fatalf("a running server did not accept a connection on %s: %v",
			s.addr, err)
	}
	conn.Close()

	select {
	case <-enqueue(s):
	case <-time.After(deadline):
		t.Errorf("a task given to a running server had not run after %s\n\n%s",
			deadline, dump())
	}
}

#!/usr/bin/env bash
# Reference solution, step 8: a server that has been stopped stops everything
# it started.
#
# The shape of the change
# -----------------------
# One defect with two halves, in three files.
#
#   * The executor. Every storage service opens an unbuffered queue in
#     `Init` and starts a goroutine that ranges over it:
#
#         s.taskExecQueue = make(chan *task)
#         go func() {
#             for t := range s.taskExecQueue {
#                 execTask(t)
#             }
#         }()
#
#     Nothing ever closes that channel. A `range` over a channel ends when
#     the channel is closed and at no other time, so the goroutine parks on a
#     receive for the life of the process -- holding the service, the driver
#     it initialised, and the configuration behind both. Stopping the server
#     does not reach it, because the server's shutdown path walks its
#     endpoints and its log writers and then returns; the services package
#     has no shutdown path at all. Measured, one server start leaks exactly
#     one goroutine per configured service, and the leak is linear in the
#     number of starts: six start/stop cycles leave six goroutines behind.
#
#     The other half is the producer. `TaskEnqueue` does
#     `go func() { s.taskExecQueue <- t }()` -- a send on an unbuffered
#     channel with no receiver is a goroutine that blocks forever, so once
#     the consumer is gone every enqueue leaks a second one.
#
#     The fix is to give the service a lifetime and a shutdown. A
#     `context.Context` cancelled by `Close` is what ends the consumer's
#     loop, and the same context is what releases a send that is still
#     waiting; a `sync.WaitGroup` is what lets `Close` wait for the consumer
#     to be gone rather than assume it. `sync.WaitGroup.Go` is the 1.25
#     spelling of "start this and count it", and it is used here because the
#     pair it replaces -- `Add(1)` beside a `defer Done()` inside the
#     lambda -- is the shape this whole line has been retiring.
#
#     The services package then needs a way to be told. `Close(ctx)` finds
#     the container for the server named by the context and closes every
#     storage service in it. The server's own `close()` calls it.
#
#     The container stays in `servicesByServer` afterwards, and that is
#     deliberate. Taking it out looks tidier -- it would bound the map's
#     growth -- but a `types.StorageService` handed out by
#     `GetStorageService` outlives the close, and every task constructor in
#     the package reaches back through the map to find the task service.
#     Remove the entry and that lookup dereferences a nil container, so a
#     caller who submits work a moment too late gets a segfault instead of a
#     no-op. Unbounded growth was here before this step and no part of this
#     milestone asks for it; a new crash path would be a worse trade.
#
#     What must not change is that a running server still runs. The
#     cancellation is per-service and created in `Init`, so it is scoped to
#     the service that owns it and not to any request context; nothing that
#     serves a request can cancel it.
#
# What this deliberately leaves alone
# -----------------------------------
# The one surviving `time.Tick`, in `drivers/storage/libstorage/
# libstorage_client_xcli.go`. Step 7 retired the other one and said why it
# stopped at one: what is wrong with `time.Tick` is the goroutine, not the
# ticker. The call that is left is read in a `select` on its own caller's
# goroutine, so there is no goroutine to leak, and since Go 1.23 an
# unreferenced ticker is collected -- `go doc time.Tick` says in as many
# words that there is no longer any reason to prefer `NewTicker`. Changing
# it would be a diff with nothing behind it.
#
# What this is not
# ----------------
# It is not a rewrite of the task service. `services_task.go` starts
# goroutines too, and they are bounded: they run a task and return. The
# question this step asks is about goroutines that outlive what started
# them, and those are the two above.
set -euo pipefail

cd /app

python3 - <<'PY'
import io, sys

def edit(path, pairs):
    s = io.open(path, encoding="utf-8").read()
    for old, new in pairs:
        if old not in s:
            sys.exit("solve.sh: pattern not found in %s:\n%s" % (path, old))
        s = s.replace(old, new, 1)
    io.open(path, "w", encoding="utf-8").write(s)


# --- the storage service gets a lifetime ------------------------------------
edit("api/server/services/services_storage.go", [
("""import (
	"fmt"
""",
 """import (
	gocontext "context"
	"fmt"
	"sync"
"""),

("""	authConfig    *types.AuthConfig
	taskExecQueue chan *task
}""",
 """	authConfig    *types.AuthConfig
	taskExecQueue chan *task

	// The executor's lifetime. runCtx is cancelled by Close, which is what
	// ends both the loop that takes tasks off the queue and any send that is
	// still waiting to put one on it; wg holds the loop itself, so Close can
	// wait for it to be gone rather than hope. A send is not on the wait
	// group on purpose: TaskEnqueue may be called after Close has returned,
	// and Add after Wait is the one thing sync.WaitGroup does not allow. A
	// send does not need to be waited for anyway -- cancelling runCtx is
	// what ends it, and it touches nothing afterwards.
	runCtx gocontext.Context
	cancel gocontext.CancelFunc
	wg     sync.WaitGroup
}"""),

("""	s.taskExecQueue = make(chan *task)
	go func() {
		for t := range s.taskExecQueue {
			execTask(t)
		}
	}()""",
 """	s.taskExecQueue = make(chan *task)
	s.runCtx, s.cancel = gocontext.WithCancel(gocontext.Background())
	s.wg.Go(func() {
		for {
			select {
			case t := <-s.taskExecQueue:
				execTask(t)
			case <-s.runCtx.Done():
				return
			}
		}
	})"""),

("""	t := newStorageServiceTask(ctx, run, s, schema)
	go func() { s.taskExecQueue <- t }()
	return &t.Task""",
 """	t := newStorageServiceTask(ctx, run, s, schema)
	go func() {
		select {
		case s.taskExecQueue <- t:
		case <-s.runCtx.Done():
		}
	}()
	return &t.Task"""),

("""func (s *storageService) Name() string {
	return s.name
}""",
 """func (s *storageService) Name() string {
	return s.name
}

// Close stops the service's task executor and waits for it to finish. It is
// safe to call more than once.
func (s *storageService) Close() error {
	if s.cancel == nil {
		return nil
	}
	s.cancel()
	s.wg.Wait()
	return nil
}"""),
])


# --- the services registry gets a shutdown ----------------------------------
edit("api/server/services/services.go", [
("""func (sc *serviceContainer) Init(ctx types.Context, config gofig.Config) error {""",
 """// Close shuts down the services registered for the server named by ctx. A
// server that has been closed leaves nothing of its own running, and work
// submitted to one of its services afterwards is a no-op rather than a
// crash.
func Close(ctx types.Context) error {

	serverName, ok := context.Server(ctx)
	if !ok {
		panic("ctx is missing ServerName")
	}

	servicesByServerRWL.RLock()
	sc, ok := servicesByServer[serverName]
	servicesByServerRWL.RUnlock()

	if !ok {
		return nil
	}

	ctx.Info("shutting down server services")
	return sc.Close()
}

func (sc *serviceContainer) Close() error {
	for _, svc := range sc.storageServices {
		if s, ok := svc.(*storageService); ok {
			if err := s.Close(); err != nil {
				return err
			}
		}
	}
	return nil
}

func (sc *serviceContainer) Init(ctx types.Context, config gofig.Config) error {"""),
])


# --- the server calls it ----------------------------------------------------
edit("api/server/server.go", [
("""	s.ctx.Debug("shutdown server complete")

	return nil
}""",
 """	if err := services.Close(s.ctx); err != nil {
		s.ctx.Error(err)
	}

	s.ctx.Debug("shutdown server complete")

	return nil
}"""),
])

print("solve.sh: edits applied")
PY

unformatted=$(gofmt -l -s $(find . -name '*.go' -not -path './vendor/*'))
if [ -n "$unformatted" ]; then
    echo "not gofmt -s clean:" >&2
    echo "$unformatted" >&2
    exit 1
fi

go build ./...
go vet ./...

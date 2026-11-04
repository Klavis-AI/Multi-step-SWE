#!/usr/bin/env bash
# Reference solution, step 6: the twelve functions that hand a caller a
# channel and a goroutine to fill it become sequences the caller drives, the
# loops that copy a map's keys or values into a slice become the standard
# library's own collectors, and the four constructor registries key on an
# interned name.
#
# The shape of the change
# -----------------------
# One defect, in twelve places, and two spellings that only exist once the
# rung is raised.
#
#   * `go 1.23` is the rung. It is what makes `for x := range f` legal for a
#     function of the three range-over-func shapes, and it is the directive
#     at which `iter`, `unique`, and the sequence adapters in `slices` and
#     `maps` exist at all. Every other edit here stands on it.
#
#   * The producers. Twelve functions in this repository are written the same
#     way: make an unbuffered channel, start a goroutine that takes a read
#     lock and sends every element into it, hand the channel back. Every one
#     of them has the same defect, and it is not a style complaint. A caller
#     that leaves the loop early -- and four route handlers in this tree do
#     exactly that, on the error path -- abandons that goroutine blocked on a
#     send nobody will receive. The goroutine never ends; the read lock it
#     took is released by a deferred call in a function that never returns,
#     so it holds that lock forever, and the next registration of a driver, a
#     router or a context key waits behind it for as long as the process
#     lives.
#
#     A sequence has no such failure mode. The producer is a function the
#     caller calls, on the caller's own goroutine, and a caller that stops
#     reading is a `yield` that returns false: the producer sees the answer,
#     returns, and the deferred unlock runs. The body of each of the twelve
#     is the same loop it always was, with the send replaced by a `yield`
#     whose false answer is a `return`.
#
#     Eleven are rewritten that way. The twelfth -- the global task
#     service's -- already copies what it is going to hand out into a slice
#     under the lock, before the channel is ever made, so its sequence is
#     that slice's values and nothing more.
#
#     One of the twelve is consumed by something other than a `range`:
#     `NewInstanceIDHandler` is handed the storage services to read, so its
#     parameter changes with them. And `TaskTrackingService` declares one of
#     the twelve, so the interface changes with the method.
#
#   * `reverse` is the same defect wearing different clothes: a channel and a
#     goroutine whose whole job is to walk a slice backwards. Two callers
#     want that, and `slices.Backward` is it, so the function goes.
#
#   * The collectors. Eight loops in this repository declare a slice, range
#     over a map, append each key or each value to the slice, and stop.
#     Seven of them are `slices.Collect`, `slices.Sorted` or
#     `slices.AppendSeq` over `maps.Keys` or `maps.Values`, and three of
#     those are followed by an in-place sort that `slices.Sorted` does on the
#     way. The eighth is not convertible and is left alone: its destination
#     is a `[]fmt.Stringer` while the sequence's element type is
#     `*types.LocalDevices`, and no adapter in the standard library converts
#     while it collects.
#
#     Two of the seven have to keep handing back an empty slice rather than a
#     nil one -- `keyValueStore.Keys` is marshalled straight into JSON by
#     callers who expect `[]`, not `null` -- so they collect by appending a
#     sequence onto a slice that already exists rather than by letting
#     `slices.Collect` allocate one.
#
#   * The registries. `api/registry` holds four maps of constructors keyed by
#     a driver's name, case-folded at all eight sites that read or write one.
#     The name is the same handful of strings for the life of the process,
#     compared far more often than it is stored, so the key becomes an
#     interned handle: one `unique.Make` in one helper, and the eight sites
#     call the helper.
#
# Every edit below is anchored on text that exists in the tree step 5 leaves
# behind. `edit` requires its anchor to occur exactly once and `sub` requires
# exactly the stated number of occurrences, so a file that has moved
# underneath this script fails loudly instead of being written back
# unchanged.
set -euo pipefail

cd /app
export GO111MODULE=on GOFLAGS=-mod=mod

# ---------------------------------------------------------------------------
# 1. The registry. Six of the twelve producers are here, and so are the four
#    constructor maps whose key changes, so the file is written out whole
#    rather than assembled from eight anchors that between them touch every
#    third line of it.
#
#    What is unchanged: the six registration functions' names and
#    signatures, the four constructors' lookup-and-wrap behaviour, the
#    per-registry locks, and the fact that a name is case-insensitive. What
#    changes: the key of the four maps, the one place a name is folded, and
#    the six producers' return types and bodies.
# ---------------------------------------------------------------------------

cat > api/registry/registry.go <<'GO'
// Package registry is the central hub for Drivers and other types that
// follow the init-time registration.
package registry

import (
	"iter"
	"strings"
	"sync"
	"unique"

	gofig "github.com/akutz/gofig/types"

	"github.com/codedellemc/libstorage/api/types"
)

var (
	storExecsCtors    = map[unique.Handle[string]]types.NewStorageExecutor{}
	storExecsCtorsRWL = &sync.RWMutex{}

	storDriverCtors    = map[unique.Handle[string]]types.NewStorageDriver{}
	storDriverCtorsRWL = &sync.RWMutex{}

	osDriverCtors    = map[unique.Handle[string]]types.NewOSDriver{}
	osDriverCtorsRWL = &sync.RWMutex{}

	intDriverCtors    = map[unique.Handle[string]]types.NewIntegrationDriver{}
	intDriverCtorsRWL = &sync.RWMutex{}

	cfgRegs    = []*cregW{}
	cfgRegsRWL = &sync.RWMutex{}

	routers    = []types.Router{}
	routersRWL = &sync.RWMutex{}
)

// key is how a driver's name becomes a key of the four constructor maps. The
// name is folded first, which is what these registries have always done to
// it, and then interned: the maps between them hold one copy of each name
// however many times a driver of that name is registered or asked for, and
// two keys are compared by comparing two handles rather than two strings.
func key(name string) unique.Handle[string] {
	return unique.Make(strings.ToLower(name))
}

type cregW struct {
	name string
	creg types.NewConfigReg
}

// RegisterConfigReg registers a new configuration registration request.
func RegisterConfigReg(name string, f types.NewConfigReg) {
	cfgRegsRWL.Lock()
	defer cfgRegsRWL.Unlock()
	cfgRegs = append(cfgRegs, &cregW{name, f})
}

// RegisterRouter registers a Router.
func RegisterRouter(router types.Router) {
	routersRWL.Lock()
	defer routersRWL.Unlock()
	routers = append(routers, router)
}

// RegisterStorageExecutor registers a StorageExecutor.
func RegisterStorageExecutor(name string, ctor types.NewStorageExecutor) {
	storExecsCtorsRWL.Lock()
	defer storExecsCtorsRWL.Unlock()
	storExecsCtors[key(name)] = ctor
}

// RegisterStorageDriver registers a StorageDriver.
func RegisterStorageDriver(
	name string, ctor types.NewStorageDriver) {
	storDriverCtorsRWL.Lock()
	defer storDriverCtorsRWL.Unlock()
	storDriverCtors[key(name)] = ctor
}

// RegisterOSDriver registers a OSDriver.
func RegisterOSDriver(name string, ctor types.NewOSDriver) {
	osDriverCtorsRWL.Lock()
	defer osDriverCtorsRWL.Unlock()
	osDriverCtors[key(name)] = ctor
}

// RegisterIntegrationDriver registers a IntegrationDriver.
func RegisterIntegrationDriver(name string, ctor types.NewIntegrationDriver) {
	intDriverCtorsRWL.Lock()
	defer intDriverCtorsRWL.Unlock()
	intDriverCtors[key(name)] = ctor
}

// NewStorageExecutor returns a new instance of the executor specified by the
// executor name.
func NewStorageExecutor(name string) (types.StorageExecutor, error) {

	var ok bool
	var ctor types.NewStorageExecutor

	func() {
		storExecsCtorsRWL.RLock()
		defer storExecsCtorsRWL.RUnlock()
		ctor, ok = storExecsCtors[key(name)]
	}()

	if !ok {
		return nil, types.WithField("executor", name, "invalid executor name")
	}

	return ctor(), nil
}

// NewStorageDriver returns a new instance of the driver specified by the
// driver name.
func NewStorageDriver(name string) (types.StorageDriver, error) {

	var ok bool
	var ctor types.NewStorageDriver

	func() {
		storDriverCtorsRWL.RLock()
		defer storDriverCtorsRWL.RUnlock()
		ctor, ok = storDriverCtors[key(name)]
	}()

	if !ok {
		return nil, types.WithField("driver", name, "invalid driver name")
	}

	return ctor(), nil
}

// NewOSDriver returns a new instance of the driver specified by the
// driver name.
func NewOSDriver(name string) (types.OSDriver, error) {

	var ok bool
	var ctor types.NewOSDriver

	func() {
		osDriverCtorsRWL.RLock()
		defer osDriverCtorsRWL.RUnlock()
		ctor, ok = osDriverCtors[key(name)]
	}()

	if !ok {
		return nil, types.WithField("driver", name, "invalid driver name")
	}

	return NewOSDriverManager(ctor()), nil
}

// NewIntegrationDriver returns a new instance of the driver specified by the
// driver name.
func NewIntegrationDriver(name string) (types.IntegrationDriver, error) {

	var ok bool
	var ctor types.NewIntegrationDriver

	func() {
		intDriverCtorsRWL.RLock()
		defer intDriverCtorsRWL.RUnlock()
		ctor, ok = intDriverCtors[key(name)]
	}()

	if !ok {
		return nil, types.WithField("driver", name, "invalid driver name")
	}

	return NewIntegrationDriverManager(ctor()), nil
}

// ConfigRegs returns a sequence over all registered configuration
// registrations.
func ConfigRegs(ctx types.Context) iter.Seq[gofig.ConfigRegistration] {
	return func(yield func(gofig.ConfigRegistration) bool) {
		cfgRegsRWL.RLock()
		defer cfgRegsRWL.RUnlock()
		for _, cr := range cfgRegs {
			r := NewConfigReg(cr.name)
			cr.creg(ctx, r)
			if !yield(r) {
				return
			}
		}
	}
}

// StorageExecutors returns a sequence over new instances of all registered
// storage executors.
func StorageExecutors() iter.Seq[types.StorageExecutor] {
	return func(yield func(types.StorageExecutor) bool) {
		storExecsCtorsRWL.RLock()
		defer storExecsCtorsRWL.RUnlock()
		for _, ctor := range storExecsCtors {
			if !yield(ctor()) {
				return
			}
		}
	}
}

// StorageDrivers returns a sequence over new instances of all registered
// remote storage drivers.
func StorageDrivers() iter.Seq[types.StorageDriver] {
	return func(yield func(types.StorageDriver) bool) {
		storDriverCtorsRWL.RLock()
		defer storDriverCtorsRWL.RUnlock()
		for _, ctor := range storDriverCtors {
			if !yield(ctor()) {
				return
			}
		}
	}
}

// OSDrivers returns a sequence over new instances of all registered OS
// drivers.
func OSDrivers() iter.Seq[types.OSDriver] {
	return func(yield func(types.OSDriver) bool) {
		osDriverCtorsRWL.RLock()
		defer osDriverCtorsRWL.RUnlock()
		for _, ctor := range osDriverCtors {
			if !yield(ctor()) {
				return
			}
		}
	}
}

// IntegrationDrivers returns a sequence over new instances of all registered
// integration drivers.
func IntegrationDrivers() iter.Seq[types.IntegrationDriver] {
	return func(yield func(types.IntegrationDriver) bool) {
		intDriverCtorsRWL.RLock()
		defer intDriverCtorsRWL.RUnlock()
		for _, ctor := range intDriverCtors {
			if !yield(ctor()) {
				return
			}
		}
	}
}

// Routers returns a sequence over all registered routers.
func Routers() iter.Seq[types.Router] {
	return func(yield func(types.Router) bool) {
		routersRWL.RLock()
		defer routersRWL.RUnlock()
		for _, r := range routers {
			if !yield(r) {
				return
			}
		}
	}
}
GO

# ---------------------------------------------------------------------------
# 2. Everything else, as anchored edits.
# ---------------------------------------------------------------------------

python3 - <<'PY'
import io
import sys


def read(path):
    return io.open(path).read()


def write(path, text):
    io.open(path, "w").write(text)


def edit(path, *pairs):
    """Apply (old, new) replacements to a file; every `old` must occur once."""
    text = read(path)
    for old, new in pairs:
        n = text.count(old)
        if n != 1:
            sys.exit("%s: anchor occurs %d times, expected 1:\n%s" %
                     (path, n, old))
        text = text.replace(old, new)
    write(path, text)


def sub(path, old, new, count):
    """Replace every `old` in a file; there must be exactly `count` of them."""
    text = read(path)
    n = text.count(old)
    if n != count:
        sys.exit("%s: %r occurs %d times, expected %d" % (path, old, n, count))
    write(path, text.replace(old, new))

# -------------------------------------------------------------------------
# 3. The rung. `go 1.23` is what makes a function of the range-over-func
#    shapes rangeable, and it is the directive at which the `iter`,
#    `unique`, `slices` and `maps` surface every edit below reaches for
#    exists. It is a statement about the code, not a note about the
#    toolchain the code is built with.
# -------------------------------------------------------------------------

edit("go.mod", ("\ngo 1.22\n", "\ngo 1.23\n"))

# -------------------------------------------------------------------------
# 4. The two context-key producers. The loop is the one that was there,
#    including the bitmask test that decides which keys belong to which
#    walk; what changes is that the element goes to the caller instead of
#    into a channel, and that a caller who has stopped asking ends the
#    walk -- which is what releases the read lock these two take on the
#    map every context in this package reads.
# -------------------------------------------------------------------------

edit("api/context/context_keys.go",
     ('import (\n'
      '\t"sync"\n',
      'import (\n'
      '\t"iter"\n'
      '\t"sync"\n'),

     ('// CustomHeaderKeys returns a channel on which can be received all the\n'
      '// registered, custom header keys.\n'
      'func CustomHeaderKeys() <-chan interface{} {\n'
      '\tc := make(chan interface{})\n'
      '\n'
      '\tgo func() {\n'
      '\t\tcustomKeysRWL.RLock()\n'
      '\t\tdefer customKeysRWL.RUnlock()\n'
      '\n'
      '\t\tfor _, v := range customKeys {\n'
      '\t\t\tif (v.keyBitmask & CustomHeaderKey) == CustomHeaderKey {\n'
      '\t\t\t\tc <- v.externalID\n'
      '\t\t\t}\n'
      '\t\t}\n'
      '\n'
      '\t\tclose(c)\n'
      '\t}()\n'
      '\n'
      '\treturn c\n'
      '}\n',
      '// CustomHeaderKeys returns a sequence over all the registered, custom\n'
      '// header keys.\n'
      'func CustomHeaderKeys() iter.Seq[interface{}] {\n'
      '\treturn func(yield func(interface{}) bool) {\n'
      '\t\tcustomKeysRWL.RLock()\n'
      '\t\tdefer customKeysRWL.RUnlock()\n'
      '\n'
      '\t\tfor _, v := range customKeys {\n'
      '\t\t\tif (v.keyBitmask & CustomHeaderKey) == CustomHeaderKey {\n'
      '\t\t\t\tif !yield(v.externalID) {\n'
      '\t\t\t\t\treturn\n'
      '\t\t\t\t}\n'
      '\t\t\t}\n'
      '\t\t}\n'
      '\t}\n'
      '}\n'),

     ('// CustomLoggerKeys returns a channel on which can be received all the\n'
      '// registered, custom logger keys.\n'
      'func CustomLoggerKeys() <-chan interface{} {\n'
      '\n'
      '\tc := make(chan interface{})\n'
      '\n'
      '\tgo func() {\n'
      '\t\tcustomKeysRWL.RLock()\n'
      '\t\tdefer customKeysRWL.RUnlock()\n'
      '\n'
      '\t\tfor _, v := range customKeys {\n'
      '\t\t\tif (v.keyBitmask & CustomLoggerKey) == CustomLoggerKey {\n'
      '\t\t\t\tc <- v.externalID\n'
      '\t\t\t}\n'
      '\t\t}\n'
      '\n'
      '\t\tclose(c)\n'
      '\t}()\n'
      '\n'
      '\treturn c\n'
      '}\n',
      '// CustomLoggerKeys returns a sequence over all the registered, custom\n'
      '// logger keys.\n'
      'func CustomLoggerKeys() iter.Seq[interface{}] {\n'
      '\treturn func(yield func(interface{}) bool) {\n'
      '\t\tcustomKeysRWL.RLock()\n'
      '\t\tdefer customKeysRWL.RUnlock()\n'
      '\n'
      '\t\tfor _, v := range customKeys {\n'
      '\t\t\tif (v.keyBitmask & CustomLoggerKey) == CustomLoggerKey {\n'
      '\t\t\t\tif !yield(v.externalID) {\n'
      '\t\t\t\t\treturn\n'
      '\t\t\t\t}\n'
      '\t\t\t}\n'
      '\t\t}\n'
      '\t}\n'
      '}\n'))

# -------------------------------------------------------------------------
# 5. The two server-side producers. `StorageServices` reads the map the
#    server was configured from; `Tasks` is a pass-through to the task
#    service, so all that changes there is what it passes through.
# -------------------------------------------------------------------------

edit("api/server/services/services.go",
     ('import (\n'
      '\t"fmt"\n'
      '\t"strings"\n',
      'import (\n'
      '\t"fmt"\n'
      '\t"iter"\n'
      '\t"strings"\n'),

     ('// StorageServices returns a channel on which all the storage services are\n'
      '// received.\n'
      'func StorageServices(ctx types.Context) <-chan types.StorageService {\n'
      '\tc := make(chan types.StorageService)\n'
      '\tgo func() {\n'
      '\t\tfor _, v := range getStorageServices(ctx) {\n'
      '\t\t\tc <- v\n'
      '\t\t}\n'
      '\t\tclose(c)\n'
      '\t}()\n'
      '\treturn c\n'
      '}\n',
      '// StorageServices returns a sequence over all the storage services.\n'
      'func StorageServices(ctx types.Context) iter.Seq[types.StorageService] {\n'
      '\treturn func(yield func(types.StorageService) bool) {\n'
      '\t\tfor _, v := range getStorageServices(ctx) {\n'
      '\t\t\tif !yield(v) {\n'
      '\t\t\t\treturn\n'
      '\t\t\t}\n'
      '\t\t}\n'
      '\t}\n'
      '}\n'),

     ('// Tasks returns a channel on which all tasks are received.\n'
      'func Tasks(ctx types.Context) <-chan *types.Task {\n',
      '// Tasks returns a sequence over all the tasks.\n'
      'func Tasks(ctx types.Context) iter.Seq[*types.Task] {\n'))

# -------------------------------------------------------------------------
# 6. The task service's own producer, and the one of the twelve that needs
#    no loop at all: it already copies the tasks it is going to hand out
#    into a slice under the lock, before the channel is made, so the
#    sequence it hands back is that slice's values.
# -------------------------------------------------------------------------

edit("api/server/services/services_task.go",
     ('\t"fmt"\n'
      '\t"sync"\n',
      '\t"fmt"\n'
      '\t"iter"\n'
      '\t"slices"\n'
      '\t"sync"\n'),

     ('// Tasks returns a channel on which all tasks are received.\n'
      'func (s *globalTaskService) Tasks() <-chan *types.Task {\n'
      '\ttasks := []*types.Task{}\n'
      '\ts.RLock()\n'
      '\tfor _, v := range s.tasks {\n'
      '\t\ttasks = append(tasks, &v.Task)\n'
      '\t}\n'
      '\ts.RUnlock()\n'
      '\n'
      '\tc := make(chan *types.Task)\n'
      '\tgo func() {\n'
      '\t\tfor _, t := range tasks {\n'
      '\t\t\tc <- t\n'
      '\t\t}\n'
      '\t\tclose(c)\n'
      '\t}()\n'
      '\treturn c\n'
      '}\n',
      '// Tasks returns a sequence over all the tasks.\n'
      'func (s *globalTaskService) Tasks() iter.Seq[*types.Task] {\n'
      '\ttasks := []*types.Task{}\n'
      '\ts.RLock()\n'
      '\tfor _, v := range s.tasks {\n'
      '\t\ttasks = append(tasks, &v.Task)\n'
      '\t}\n'
      '\ts.RUnlock()\n'
      '\n'
      '\treturn slices.Values(tasks)\n'
      '}\n'))

# -------------------------------------------------------------------------
# 7. The interface that declares one of the twelve. The file has had no
#    imports until now.
# -------------------------------------------------------------------------

edit("api/types/types_services.go",
     ('package types\n'
      '\n'
      '// Service is the base type for services.\n',
      'package types\n'
      '\n'
      'import "iter"\n'
      '\n'
      '// Service is the base type for services.\n'),

     ('\t// Tasks returns a channel on which all tasks tracked via TrackTasks are\n'
      '\t// received.\n'
      '\tTasks() <-chan *Task\n',
      '\t// Tasks returns a sequence over all tasks tracked via TrackTasks.\n'
      '\tTasks() iter.Seq[*Task]\n'))

# -------------------------------------------------------------------------
# 8. The one consumer of the twelve that is not a `range`: this handler is
#    handed the storage services to read the driver name out of each, and
#    it reads all of them, so what changes is only where they come from.
# -------------------------------------------------------------------------

edit("api/server/handlers/handlers_instanceid.go",
     ('import (\n'
      '\t"net/http"\n',
      'import (\n'
      '\t"iter"\n'
      '\t"net/http"\n'),

     ('func NewInstanceIDHandler(svcs <-chan types.StorageService) types.Middleware {\n',
      'func NewInstanceIDHandler(\n'
      '\tsvcs iter.Seq[types.StorageService]) types.Middleware {\n'))

# -------------------------------------------------------------------------
# 9. `reverse`, and its two callers. A channel and a goroutine to walk a
#    slice from the end is what `slices.Backward` is, and unlike the
#    function it replaces it does not leak when a caller stops: both of
#    these loops run to the end today, but neither has to now.
# -------------------------------------------------------------------------

edit("api/server/server_middleware.go",
     ('import (\n'
      '\t"github.com/codedellemc/libstorage/api/server/handlers"\n',
      'import (\n'
      '\t"slices"\n'
      '\n'
      '\t"github.com/codedellemc/libstorage/api/server/handlers"\n'),

     ('\t\tfor h := range reverse(middlewaresForRouteName) {\n',
      '\t\tfor _, h := range slices.Backward(middlewaresForRouteName) {\n'),

     ('\tfor h := range reverse(s.globalHandlers) {\n',
      '\tfor _, h := range slices.Backward(s.globalHandlers) {\n'),

     ('\nfunc reverse(middlewares []types.Middleware) chan types.Middleware {\n'
      '\tret := make(chan types.Middleware)\n'
      '\tgo func() {\n'
      '\t\tfor i := range middlewares {\n'
      '\t\t\tret <- middlewares[len(middlewares)-1-i]\n'
      '\t\t}\n'
      '\t\tclose(ret)\n'
      '\t}()\n'
      '\treturn ret\n'
      '}\n',
      ''))

# -------------------------------------------------------------------------
# 10. The collectors. Seven loops, each of which declares a slice, ranges
#     over a map, appends a key or a value and stops -- three of them
#     followed by a sort that the collector does on the way.
#
#     The two that hand their slice to a caller keep handing back an empty
#     slice rather than a nil one, so they append a sequence onto a slice
#     that already exists instead of letting the collector allocate it.
#
#     `client_http.go`'s second loop is not here. Its destination is a
#     `[]fmt.Stringer` and the sequence's elements are
#     `*types.LocalDevices`: the collectors collect what they are given.
# -------------------------------------------------------------------------

edit("api/client/client_http.go",
     ('\t"io"\n'
      '\t"net/http"\n'
      '\t"sync"\n',
      '\t"io"\n'
      '\t"maps"\n'
      '\t"net/http"\n'
      '\t"slices"\n'
      '\t"sync"\n'),

     ('\t\t\tvar iids []string\n'
      '\t\t\tfor iid := range iidHdrs {\n'
      '\t\t\t\tiids = append(iids, iid)\n'
      '\t\t\t}\n',
      '\t\t\tiids := slices.Collect(maps.Keys(iidHdrs))\n'))

edit("api/types/types_log.go",
     ('\tkeys := make([]string, 0, len(fields))\n'
      '\tfor key := range fields {\n'
      '\t\tkeys = append(keys, key)\n'
      '\t}\n'
      '\tslices.Sort(keys)\n',
      '\tkeys := slices.Sorted(maps.Keys(fields))\n'))

edit("api/types/types_localdevices.go",
     ('\t"fmt"\n'
      '\t"regexp"\n',
      '\t"fmt"\n'
      '\t"maps"\n'
      '\t"regexp"\n'),

     ('\tkeys := []string{}\n'
      '\n'
      '\tfor k := range l.DeviceMap {\n'
      '\t\tkeys = append(keys, k)\n'
      '\t}\n'
      '\n'
      '\tslices.Sort(keys)\n',
      '\tkeys := slices.Sorted(maps.Keys(l.DeviceMap))\n'))

edit("api/server/server_http.go",
     ('\t"log/slog"\n'
      '\t"net"\n',
      '\t"log/slog"\n'
      '\t"maps"\n'
      '\t"net"\n'),

     ('\tunmethoded := make([]string, 0, len(paths))\n'
      '\tfor p := range paths {\n'
      '\t\tunmethoded = append(unmethoded, p)\n'
      '\t}\n'
      '\tslices.Sort(unmethoded)\n',
      '\tunmethoded := slices.Sorted(maps.Keys(paths))\n'))

edit("api/utils/utils_store.go",
     ('import (\n'
      '\t"fmt"\n'
      '\t"reflect"\n'
      '\t"strconv"\n',
      'import (\n'
      '\t"fmt"\n'
      '\t"maps"\n'
      '\t"reflect"\n'
      '\t"slices"\n'
      '\t"strconv"\n'),

     ('\tkeys := []string{}\n'
      '\tfor k := range s.store {\n'
      '\t\tkeys = append(keys, k)\n'
      '\t}\n'
      '\treturn keys\n',
      '\treturn slices.AppendSeq(\n'
      '\t\tmake([]string, 0, len(s.store)), maps.Keys(s.store))\n'))

edit("drivers/storage/libstorage/libstorage_driver_funcs.go",
     ('import (\n'
      '\t"errors"\n'
      '\t"github.com/codedellemc/libstorage/api/context"\n',
      'import (\n'
      '\t"errors"\n'
      '\t"maps"\n'
      '\t"slices"\n'
      '\n'
      '\t"github.com/codedellemc/libstorage/api/context"\n'),

     ('\tobjs := []*types.Volume{}\n'
      '\tfor _, o := range objMap {\n'
      '\t\tobjs = append(objs, o)\n'
      '\t}\n',
      '\tobjs := slices.AppendSeq(\n'
      '\t\tmake([]*types.Volume, 0, len(objMap)), maps.Values(objMap))\n'),

     ('\tobjs := []*types.Snapshot{}\n'
      '\tfor _, o := range objMap {\n'
      '\t\tobjs = append(objs, o)\n'
      '\t}\n',
      '\tobjs := slices.AppendSeq(\n'
      '\t\tmake([]*types.Snapshot, 0, len(objMap)), maps.Values(objMap))\n'))

PY

# ---------------------------------------------------------------------------
# 11. Nothing joined or left the module graph here: every import added above
#     is from the standard library. Then the checks step 1 established, plus
#     the cross-compile step 3 added.
# ---------------------------------------------------------------------------

go mod tidy

unformatted=$(gofmt -l -s $(find . -name '*.go' -not -path './vendor/*'))
if [ -n "$unformatted" ]; then
    echo "not gofmt -s clean:" >&2
    echo "$unformatted" >&2
    exit 1
fi

go build ./...
go vet ./...
GOOS=darwin go build ./...

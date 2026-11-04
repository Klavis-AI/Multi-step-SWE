// Package runtime contains operations that interact with Go's runtime system.
package runtime

// Error is the interface implemented by runtime errors.
type Error interface {
	error
	RuntimeError()
}

type runtimeError string

func (e runtimeError) Error() string { return string(e) }
func (e runtimeError) RuntimeError() {}

// A PanicNilError happens when code calls panic(nil).
type PanicNilError struct{ _ byte }

func (*PanicNilError) Error() string  { return "panic called with nil argument" }
func (*PanicNilError) RuntimeError()  {}

func Gosched()
func NumGoroutine() int
func GOMAXPROCS(n int) int
func NumCPU() int
func GC()
func Goexit()
func KeepAlive(x any)
func SetFinalizer(obj any, finalizer any)
func nanotime() int64
func sleepNanos(d int64)
func panicNilEnabled() bool
func hashValue(v any) uint64
func valueEqual(a, b any) bool
func typeName(v any) string
func isNil(v any) bool
func addCleanup(p any, fn any, arg any) int64

func Version() string { return "go1.25.1" }

const GOOS = "linux"
const GOARCH = "amd64"

// MemStats records statistics about the memory allocator.
type MemStats struct {
	Alloc       uint64
	TotalAlloc  uint64
	Sys         uint64
	Mallocs     uint64
	Frees       uint64
	HeapAlloc   uint64
	HeapObjects uint64
	HeapSys     uint64
	NextGC      uint64
	NumGC       uint32
	NumForcedGC uint32
	PauseTotalNs uint64
}

func readMemStats() (heapAlloc, totalAlloc, mallocs, frees uint64, numGC, numForcedGC uint32, pauseTotal, sys uint64)

func ReadMemStats(m *MemStats) {
	h, t, ma, f, n, nf, p, s := readMemStats()
	m.HeapAlloc = h
	m.Alloc = h
	m.TotalAlloc = t
	m.Mallocs = ma
	m.Frees = f
	m.NumGC = n
	m.NumForcedGC = nf
	m.PauseTotalNs = p
	m.Sys = s
	m.HeapSys = s
	m.HeapObjects = ma - f
	m.NextGC = s
}

// Cleanup is the handle returned by AddCleanup.
type Cleanup struct{ id int64 }

func (c Cleanup) Stop() {}

// AddCleanup attaches a cleanup function to ptr. The cleanup runs once ptr is
// no longer reachable, with arg as its argument. Unlike SetFinalizer it does
// not keep ptr itself alive.
func AddCleanup[T, S any](ptr *T, cleanup func(S), arg S) Cleanup {
	if ptr == nil {
		panic("runtime.AddCleanup: ptr is nil")
	}
	return Cleanup{addCleanup(ptr, cleanup, arg)}
}

func callerInfo(skip int) (pc uintptr, file string, line int, ok bool)

// Caller reports file and line information about the calling goroutine's stack.
func Caller(skip int) (pc uintptr, file string, line int, ok bool) { return callerInfo(skip) }

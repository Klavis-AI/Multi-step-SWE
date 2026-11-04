// Package debug contains facilities for programs to debug themselves.
package debug

import "runtime"

func setGCPercent(n int) int
func setMemoryLimit(n int64) int64
func freeOSMemory()

func SetGCPercent(percent int) int { return setGCPercent(percent) }
func SetMemoryLimit(limit int64) int64 { return setMemoryLimit(limit) }
func FreeOSMemory() { freeOSMemory() }

type GCStats struct {
	NumGC        int64
	PauseTotal   int64
}

func ReadGCStats(stats *GCStats) {
	var m runtime.MemStats
	runtime.ReadMemStats(&m)
	stats.NumGC = int64(m.NumGC)
	stats.PauseTotal = int64(m.PauseTotalNs)
}

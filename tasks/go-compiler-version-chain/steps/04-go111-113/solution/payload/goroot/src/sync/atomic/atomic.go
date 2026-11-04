// Package atomic provides low-level atomic memory primitives.
package atomic

func addInt(addr any, delta int64) int64
func loadInt(addr any) int64
func storeInt(addr any, val int64)
func casInt(addr any, old, new int64) bool
func swapInt(addr any, new int64) int64
func loadAny(addr any) any
func storeAny(addr any, val any)
func casAny(addr any, old, new any) bool

func AddInt32(addr *int32, delta int32) int32     { return int32(addInt(addr, int64(delta))) }
func AddInt64(addr *int64, delta int64) int64     { return addInt(addr, delta) }
func AddUint32(addr *uint32, delta uint32) uint32 { return uint32(addInt(addr, int64(delta))) }
func AddUint64(addr *uint64, delta uint64) uint64 { return uint64(addInt(addr, int64(delta))) }

func LoadInt32(addr *int32) int32     { return int32(loadInt(addr)) }
func LoadInt64(addr *int64) int64     { return loadInt(addr) }
func LoadUint32(addr *uint32) uint32  { return uint32(loadInt(addr)) }
func LoadUint64(addr *uint64) uint64  { return uint64(loadInt(addr)) }

func StoreInt32(addr *int32, val int32)     { storeInt(addr, int64(val)) }
func StoreInt64(addr *int64, val int64)     { storeInt(addr, val) }
func StoreUint32(addr *uint32, val uint32)  { storeInt(addr, int64(val)) }
func StoreUint64(addr *uint64, val uint64)  { storeInt(addr, int64(val)) }

func CompareAndSwapInt32(addr *int32, old, new int32) bool    { return casInt(addr, int64(old), int64(new)) }
func CompareAndSwapInt64(addr *int64, old, new int64) bool    { return casInt(addr, old, new) }
func CompareAndSwapUint32(addr *uint32, old, new uint32) bool { return casInt(addr, int64(old), int64(new)) }
func CompareAndSwapUint64(addr *uint64, old, new uint64) bool { return casInt(addr, int64(old), int64(new)) }

func SwapInt32(addr *int32, new int32) int32    { return int32(swapInt(addr, int64(new))) }
func SwapInt64(addr *int64, new int64) int64    { return swapInt(addr, new) }
func SwapUint32(addr *uint32, new uint32) uint32 { return uint32(swapInt(addr, int64(new))) }
func SwapUint64(addr *uint64, new uint64) uint64 { return uint64(swapInt(addr, int64(new))) }

// A Value provides an atomic load and store of a consistently typed value.
type Value struct {
	v any
}

func (v *Value) Load() any { return loadAny(&v.v) }

func (v *Value) Store(val any) {
	if val == nil {
		panic("sync/atomic: store of nil value into Value")
	}
	storeAny(&v.v, val)
}

func (v *Value) Swap(new any) any {
	old := loadAny(&v.v)
	storeAny(&v.v, new)
	return old
}

func (v *Value) CompareAndSwap(old, new any) bool { return casAny(&v.v, old, new) }

// Int32, Int64, Uint32, Uint64, Bool and Pointer are the atomic types added in
// Go 1.19. Their zero values are ready to use.
type Int32 struct{ v int32 }

func (x *Int32) Load() int32                    { return LoadInt32(&x.v) }
func (x *Int32) Store(val int32)                { StoreInt32(&x.v, val) }
func (x *Int32) Add(delta int32) int32          { return AddInt32(&x.v, delta) }
func (x *Int32) Swap(new int32) int32           { return SwapInt32(&x.v, new) }
func (x *Int32) CompareAndSwap(old, new int32) bool { return CompareAndSwapInt32(&x.v, old, new) }

type Int64 struct{ v int64 }

func (x *Int64) Load() int64                    { return LoadInt64(&x.v) }
func (x *Int64) Store(val int64)                { StoreInt64(&x.v, val) }
func (x *Int64) Add(delta int64) int64          { return AddInt64(&x.v, delta) }
func (x *Int64) Swap(new int64) int64           { return SwapInt64(&x.v, new) }
func (x *Int64) CompareAndSwap(old, new int64) bool { return CompareAndSwapInt64(&x.v, old, new) }

type Uint32 struct{ v uint32 }

func (x *Uint32) Load() uint32                    { return LoadUint32(&x.v) }
func (x *Uint32) Store(val uint32)                { StoreUint32(&x.v, val) }
func (x *Uint32) Add(delta uint32) uint32         { return AddUint32(&x.v, delta) }
func (x *Uint32) Swap(new uint32) uint32          { return SwapUint32(&x.v, new) }
func (x *Uint32) CompareAndSwap(old, new uint32) bool { return CompareAndSwapUint32(&x.v, old, new) }

type Uint64 struct{ v uint64 }

func (x *Uint64) Load() uint64                    { return LoadUint64(&x.v) }
func (x *Uint64) Store(val uint64)                { StoreUint64(&x.v, val) }
func (x *Uint64) Add(delta uint64) uint64         { return AddUint64(&x.v, delta) }
func (x *Uint64) Swap(new uint64) uint64          { return SwapUint64(&x.v, new) }
func (x *Uint64) CompareAndSwap(old, new uint64) bool { return CompareAndSwapUint64(&x.v, old, new) }

type Bool struct{ v int32 }

func (x *Bool) Load() bool     { return LoadInt32(&x.v) != 0 }
func (x *Bool) Store(val bool) { StoreInt32(&x.v, b32(val)) }
func (x *Bool) Swap(new bool) bool {
	return SwapInt32(&x.v, b32(new)) != 0
}
func (x *Bool) CompareAndSwap(old, new bool) bool {
	return CompareAndSwapInt32(&x.v, b32(old), b32(new))
}

func b32(b bool) int32 {
	if b {
		return 1
	}
	return 0
}

type Pointer[T any] struct{ v *T }

func (x *Pointer[T]) Load() *T      { return x.v }
func (x *Pointer[T]) Store(val *T)  { x.v = val }
func (x *Pointer[T]) Swap(new *T) *T {
	old := x.v
	x.v = new
	return old
}
func (x *Pointer[T]) CompareAndSwap(old, new *T) bool {
	if x.v == old {
		x.v = new
		return true
	}
	return false
}

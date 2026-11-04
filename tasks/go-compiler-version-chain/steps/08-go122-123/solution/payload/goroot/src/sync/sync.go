// Package sync provides basic synchronization primitives such as mutual exclusion locks.
package sync

// A Locker represents an object that can be locked and unlocked.
type Locker interface {
	Lock()
	Unlock()
}

// A Mutex is a mutual exclusion lock. The zero value is an unlocked mutex.
type Mutex struct {
	ch chan struct{}
}

func (m *Mutex) Lock() {
	if m.ch == nil {
		m.ch = make(chan struct{}, 1)
	}
	m.ch <- struct{}{}
}

func (m *Mutex) TryLock() bool {
	if m.ch == nil {
		m.ch = make(chan struct{}, 1)
	}
	select {
	case m.ch <- struct{}{}:
		return true
	default:
		return false
	}
}

func (m *Mutex) Unlock() {
	if m.ch == nil {
		panic("sync: unlock of unlocked mutex")
	}
	select {
	case <-m.ch:
	default:
		panic("sync: unlock of unlocked mutex")
	}
}

// A RWMutex is a reader/writer mutual exclusion lock.
type RWMutex struct {
	w       Mutex
	readers int
	rch     chan struct{}
}

func (rw *RWMutex) Lock() {
	rw.w.Lock()
	for rw.readers > 0 {
		if rw.rch == nil {
			rw.rch = make(chan struct{}, 1)
		}
		<-rw.rch
	}
}

func (rw *RWMutex) Unlock() { rw.w.Unlock() }

func (rw *RWMutex) RLock() {
	rw.w.Lock()
	rw.readers++
	rw.w.Unlock()
}

func (rw *RWMutex) RUnlock() {
	rw.readers--
	if rw.readers == 0 && rw.rch != nil {
		select {
		case rw.rch <- struct{}{}:
		default:
		}
	}
}

func (rw *RWMutex) RLocker() Locker { return (*rlocker)(rw) }

type rlocker RWMutex

func (r *rlocker) Lock()   { (*RWMutex)(r).RLock() }
func (r *rlocker) Unlock() { (*RWMutex)(r).RUnlock() }

// A WaitGroup waits for a collection of goroutines to finish.
type WaitGroup struct {
	n       int
	waiters []chan struct{}
}

func (wg *WaitGroup) Add(delta int) {
	wg.n += delta
	if wg.n < 0 {
		panic("sync: negative WaitGroup counter")
	}
	if wg.n == 0 {
		for _, c := range wg.waiters {
			close(c)
		}
		wg.waiters = nil
	}
}

func (wg *WaitGroup) Done() { wg.Add(-1) }

// Go calls f in a new goroutine and counts it in the group. Added in Go 1.25.
func (wg *WaitGroup) Go(f func()) {
	wg.Add(1)
	go func() {
		defer wg.Done()
		f()
	}()
}

func (wg *WaitGroup) Wait() {
	if wg.n == 0 {
		return
	}
	c := make(chan struct{})
	wg.waiters = append(wg.waiters, c)
	<-c
}

// Once is an object that will perform exactly one action.
type Once struct {
	done bool
}

func (o *Once) Do(f func()) {
	if o.done {
		return
	}
	o.done = true
	f()
}

// Map is a concurrent map with amortized-constant-time loads, stores, and
// deletes. The zero Map is empty and ready for use.
type Map struct {
	mu Mutex
	m  map[any]any
}

func (m *Map) init() {
	if m.m == nil {
		m.m = make(map[any]any)
	}
}

func (m *Map) Load(key any) (value any, ok bool) {
	m.mu.Lock()
	m.init()
	v, ok := m.m[key]
	m.mu.Unlock()
	return v, ok
}

func (m *Map) Store(key, value any) {
	m.mu.Lock()
	m.init()
	m.m[key] = value
	m.mu.Unlock()
}

func (m *Map) LoadOrStore(key, value any) (actual any, loaded bool) {
	m.mu.Lock()
	m.init()
	if v, ok := m.m[key]; ok {
		m.mu.Unlock()
		return v, true
	}
	m.m[key] = value
	m.mu.Unlock()
	return value, false
}

func (m *Map) LoadAndDelete(key any) (value any, loaded bool) {
	m.mu.Lock()
	m.init()
	v, ok := m.m[key]
	delete(m.m, key)
	m.mu.Unlock()
	return v, ok
}

func (m *Map) Delete(key any) {
	m.mu.Lock()
	m.init()
	delete(m.m, key)
	m.mu.Unlock()
}

func (m *Map) Range(f func(key, value any) bool) {
	m.mu.Lock()
	m.init()
	keys := make([]any, 0, len(m.m))
	vals := make([]any, 0, len(m.m))
	for k, v := range m.m {
		keys = append(keys, k)
		vals = append(vals, v)
	}
	m.mu.Unlock()
	for i := range keys {
		if !f(keys[i], vals[i]) {
			return
		}
	}
}

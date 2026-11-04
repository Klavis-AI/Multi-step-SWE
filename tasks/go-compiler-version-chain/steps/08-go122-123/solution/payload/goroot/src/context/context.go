// Package context defines the Context type, which carries deadlines,
// cancellation signals, and request-scoped values across API boundaries.
package context

import (
	"errors"
	"sync"
	"time"
)

// A Context carries a deadline, a cancellation signal, and other values.
type Context interface {
	Deadline() (deadline time.Time, ok bool)
	Done() <-chan struct{}
	Err() error
	Value(key any) any
}

var Canceled = errors.New("context canceled")
var DeadlineExceeded error = deadlineExceededError{}

type deadlineExceededError struct{}

func (deadlineExceededError) Error() string   { return "context deadline exceeded" }
func (deadlineExceededError) Timeout() bool   { return true }
func (deadlineExceededError) Temporary() bool { return true }

type emptyCtx struct{ name string }

func (emptyCtx) Deadline() (time.Time, bool) { return time.Time{}, false }
func (emptyCtx) Done() <-chan struct{}       { return nil }
func (emptyCtx) Err() error                  { return nil }
func (emptyCtx) Value(key any) any           { return nil }
func (e emptyCtx) String() string            { return e.name }

var background = &emptyCtx{"context.Background"}
var todo = &emptyCtx{"context.TODO"}

func Background() Context { return background }
func TODO() Context       { return todo }

// A CancelFunc tells an operation to abandon its work.
type CancelFunc func()

type cancelCtx struct {
	parent   Context
	mu       sync.Mutex
	done     chan struct{}
	err      error
	children []*cancelCtx
	deadline time.Time
	hasDL    bool
	cause    error
}

func (c *cancelCtx) Deadline() (time.Time, bool) {
	if c.hasDL {
		return c.deadline, true
	}
	return c.parent.Deadline()
}

func (c *cancelCtx) Done() <-chan struct{} { return c.done }

func (c *cancelCtx) Err() error {
	c.mu.Lock()
	err := c.err
	c.mu.Unlock()
	return err
}

func (c *cancelCtx) Value(key any) any { return c.parent.Value(key) }

func (c *cancelCtx) cancel(err error) {
	c.mu.Lock()
	if c.err != nil {
		c.mu.Unlock()
		return
	}
	c.err = err
	close(c.done)
	children := c.children
	c.children = nil
	c.mu.Unlock()
	for _, child := range children {
		child.cancel(err)
	}
}

func newCancelCtx(parent Context) *cancelCtx {
	c := &cancelCtx{parent: parent, done: make(chan struct{})}
	if p, ok := parent.(*cancelCtx); ok {
		p.mu.Lock()
		if p.err != nil {
			p.mu.Unlock()
			c.cancel(p.err)
			return c
		}
		p.children = append(p.children, c)
		p.mu.Unlock()
	}
	return c
}

// WithCancel returns a copy of parent with a new Done channel.
func WithCancel(parent Context) (Context, CancelFunc) {
	c := newCancelCtx(parent)
	return c, func() { c.cancel(Canceled) }
}

// A CancelCauseFunc cancels a context, recording the given cause.
type CancelCauseFunc func(cause error)

// WithCancelCause behaves like WithCancel but records a cancellation cause.
func WithCancelCause(parent Context) (Context, CancelCauseFunc) {
	c := newCancelCtx(parent)
	return c, func(cause error) {
		c.mu.Lock()
		if c.err == nil && cause != nil {
			c.cause = cause
		}
		c.mu.Unlock()
		c.cancel(Canceled)
	}
}

// Cause returns the cause recorded when c was cancelled, or c.Err().
func Cause(c Context) error {
	for {
		cc, ok := c.(*cancelCtx)
		if ok {
			cc.mu.Lock()
			cause, err := cc.cause, cc.err
			cc.mu.Unlock()
			if cause != nil {
				return cause
			}
			if err != nil {
				return err
			}
			c = cc.parent
			continue
		}
		vc, ok := c.(*valueCtx)
		if ok {
			c = vc.parent
			continue
		}
		return c.Err()
	}
}

// AfterFunc arranges to call f in its own goroutine after ctx is done.
func AfterFunc(ctx Context, f func()) (stop func() bool) {
	stopped := make(chan struct{})
	go func() {
		select {
		case <-ctx.Done():
			f()
		case <-stopped:
		}
	}()
	return func() bool {
		select {
		case stopped <- struct{}{}:
			return true
		default:
			close(stopped)
			return true
		}
	}
}

// WithoutCancel returns a copy of parent that is not cancelled when parent is.
func WithoutCancel(parent Context) Context { return &withoutCancelCtx{parent} }

type withoutCancelCtx struct{ parent Context }

func (c *withoutCancelCtx) Deadline() (time.Time, bool) { return time.Time{}, false }
func (c *withoutCancelCtx) Done() <-chan struct{}       { return nil }
func (c *withoutCancelCtx) Err() error                  { return nil }
func (c *withoutCancelCtx) Value(key any) any           { return c.parent.Value(key) }

// WithDeadline returns a copy of parent with the deadline adjusted to d.
func WithDeadline(parent Context, d time.Time) (Context, CancelFunc) {
	// A child never outlives its parent: if the parent already has an earlier
	// deadline, the child inherits it.
	if pd, ok := parent.Deadline(); ok && pd.Before(d) {
		d = pd
	}
	c := newCancelCtx(parent)
	c.deadline = d
	c.hasDL = true
	dur := time.Until(d)
	if dur <= 0 {
		c.cancel(DeadlineExceeded)
		return c, func() { c.cancel(Canceled) }
	}
	go func() {
		time.Sleep(dur)
		c.cancel(DeadlineExceeded)
	}()
	return c, func() { c.cancel(Canceled) }
}

// WithTimeout is WithDeadline(parent, time.Now().Add(timeout)).
func WithTimeout(parent Context, timeout time.Duration) (Context, CancelFunc) {
	return WithDeadline(parent, time.Now().Add(timeout))
}

type valueCtx struct {
	parent   Context
	key, val any
}

func (c *valueCtx) Deadline() (time.Time, bool) { return c.parent.Deadline() }
func (c *valueCtx) Done() <-chan struct{}       { return c.parent.Done() }
func (c *valueCtx) Err() error                  { return c.parent.Err() }
func (c *valueCtx) Value(key any) any {
	if key == c.key {
		return c.val
	}
	return c.parent.Value(key)
}

// WithValue returns a copy of parent carrying key/val.
func WithValue(parent Context, key, val any) Context {
	if key == nil {
		panic("nil key")
	}
	return &valueCtx{parent, key, val}
}

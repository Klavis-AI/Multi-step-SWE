// Package synctest provides support for testing concurrent code.
package synctest

import "testing"

func runBubble(f func())
func wait()

// Run executes f in a new bubble with a synthetic clock: time advances only
// when every goroutine in the bubble is durably blocked.
func Run(f func()) { runBubble(f) }

// Test executes f as a subtest in a new bubble.
func Test(t *testing.T, f func(*testing.T)) { runBubble(func() { f(t) }) }

// Wait blocks until every goroutine in the current bubble other than the
// caller is durably blocked.
func Wait() { wait() }

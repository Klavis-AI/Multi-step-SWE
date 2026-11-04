// Package simtime asks what the repository's key/value store answers about an
// entry that has a lifetime, and what it answers while a second caller is
// writing to it.
//
// A store built with a lifetime hands out four views of itself: whether a key
// is set, the value behind a key, the list of keys, and the whole thing as a
// map. Two of those four consult the entry's deadline and two of them read the
// backing map directly, so an entry that has run out is absent from one pair
// and present in the other until something sweeps it away. A caller cannot
// tell which of the four it is allowed to trust, and the pair that answers
// late is the pair a caller iterates.
//
// The map view has a second problem that has nothing to do with lifetimes. It
// takes the store's read lock and then calls two exported readers that take it
// again. Go's `sync.RWMutex` will not grant a second read lock while a writer
// is waiting, so a write arriving between the outer and the inner acquisition
// stops all three permanently.
//
// Everything here goes through the exported constructors and the `types.Store`
// interface -- what a caller of this package has. Nothing here names a field,
// a helper or a goroutine.
package simtime

import (
	"fmt"
	"reflect"
	"testing"
	"time"

	"github.com/codedellemc/libstorage/api/utils"
)

// The lifetime, seen from each of the four views at once.

func TestAnExpiredEntryIsGoneFromEveryViewOfTheStore(t *testing.T) {
	s := utils.NewTTLStore(200*time.Millisecond, true)
	s.Set("k", "v")
	time.Sleep(500 * time.Millisecond)

	if s.IsSet("k") {
		t.Error(`IsSet("k") is true after the entry's lifetime ran out`)
	}
	if got := s.Get("k"); got != nil {
		t.Errorf(`Get("k") = %v after the entry's lifetime ran out, want nil`,
			got)
	}
	if got := s.Keys(); len(got) != 0 {
		t.Errorf("Keys() = %v after the entry's lifetime ran out, want none",
			got)
	}
	if got := s.Map(); len(got) != 0 {
		t.Errorf("Map() = %v after the entry's lifetime ran out, want empty",
			got)
	}
}

func TestAnEntryInsideItsLifetimeIsInEveryViewOfTheStore(t *testing.T) {
	s := utils.NewTTLStore(time.Minute, true)
	s.Set("k", "v")

	if !s.IsSet("k") {
		t.Error(`IsSet("k") is false inside the entry's lifetime`)
	}
	if got := s.Get("k"); got != "v" {
		t.Errorf(`Get("k") = %v inside the entry's lifetime, want "v"`, got)
	}
	if got := s.Keys(); !reflect.DeepEqual(got, []string{"k"}) {
		t.Errorf("Keys() = %v inside the entry's lifetime, want [k]", got)
	}
	if got := s.Map(); !reflect.DeepEqual(
		got, map[string]interface{}{"k": "v"}) {
		t.Errorf("Map() = %v inside the entry's lifetime, want map[k:v]", got)
	}
}

func TestAStoreWithoutALifetimeKeepsWhatItIsGiven(t *testing.T) {
	s := utils.NewStore()
	s.Set("k", "v")
	time.Sleep(500 * time.Millisecond)

	if !s.IsSet("k") {
		t.Error(`IsSet("k") is false in a store with no lifetime`)
	}
	if got := s.Get("k"); got != "v" {
		t.Errorf(`Get("k") = %v in a store with no lifetime, want "v"`, got)
	}
	if got := s.Keys(); !reflect.DeepEqual(got, []string{"k"}) {
		t.Errorf("Keys() = %v in a store with no lifetime, want [k]", got)
	}
	if got := s.Map(); !reflect.DeepEqual(
		got, map[string]interface{}{"k": "v"}) {
		t.Errorf("Map() = %v in a store with no lifetime, want map[k:v]", got)
	}
}

// A lifetime that is documented to slide. `NewTTLStore(d, false)` is a
// sliding expiration: reading an entry puts its deadline back. The margins
// here are wide -- five reads a quarter of a second apart against a lifetime
// of eight tenths, then a wait of twice the lifetime -- because the subject
// is which reads move the deadline, not how precisely they move it.

func TestAReadPutsASlidingLifetimeBack(t *testing.T) {
	const ttl = 800 * time.Millisecond
	s := utils.NewTTLStore(ttl, false)
	s.Set("k", "v")

	for i := 0; i < 5; i++ {
		time.Sleep(ttl / 4)
		if !s.IsSet("k") {
			t.Fatalf("IsSet(\"k\") is false after %d reads a quarter of a "+
				"lifetime apart; a sliding lifetime should have been put "+
				"back by each of them", i)
		}
	}
	if got := s.Get("k"); got != "v" {
		t.Errorf(`Get("k") = %v after the reads that put its lifetime `+
			`back, want "v"`, got)
	}

	time.Sleep(2 * ttl)

	if s.IsSet("k") {
		t.Error(`IsSet("k") is true two lifetimes after the last read; a ` +
			`sliding lifetime still has to run out when nobody reads it`)
	}
}

// The map view against a writer. Its own deadline, so a store that stops
// answering reports that within ten seconds instead of hanging until the
// package's clock runs out.
//
// The window is widened on purpose. A map view that takes the store's read
// lock and then calls exported
// readers that take it again stops only if a writer arrives between the
// outer acquisition and one of the inner ones. With one entry in the store
// and a writer that runs out of work, that window is a handful of
// instructions and the outcome is the scheduler's: the same store, built
// from the same source, was measured stopping on one tree and finishing in
// 0.01 s on another.
//
// Two changes make it the code's outcome rather than the scheduler's. The
// store holds a thousand entries, so a view of it takes a thousand inner
// acquisitions and the reader is inside the window for very nearly the whole
// of every call; and the writer keeps writing until the reader says it is
// done, so there is always another writer about to arrive. On a tree whose
// map view holds one lock for its own work, the reader finishes; on a tree
// that reacquires, the first call is the last.
//
// The writer is left where it is if the reader stops: it is blocked on a
// lock the reader will never release, and the binary is failing anyway.

func TestASnapshotOfTheStoreIsNotBlockedByAWriter(t *testing.T) {
	s := utils.NewStore()

	const entries = 1000
	for i := 0; i < entries; i++ {
		s.Set(fmt.Sprintf("k%04d", i), i)
	}

	stop := make(chan struct{})
	defer close(stop)
	go func() {
		for i := 0; ; i++ {
			select {
			case <-stop:
				return
			default:
			}
			s.Set("w", i)
		}
	}()

	const rounds = 200
	done := make(chan struct{})
	go func() {
		defer close(done)
		for i := 0; i < rounds; i++ {
			if got := len(s.Map()); got < entries {
				t.Errorf("Map() has %d of the %d entries that were set",
					got, entries)
				return
			}
		}
	}()

	select {
	case <-done:
	case <-time.After(10 * time.Second):
		t.Errorf("%d reads of a %d-entry store did not finish in 10s while "+
			"another goroutine was writing to it", rounds, entries)
	}
}

// Package sweeper asks one question about the repository's key/value store:
// when a store with a lifetime is built, does anything keep running behind it?
//
// The question is asked in simulated time rather than in wall-clock time, and
// it is asked alone. A store built with a lifetime today starts a goroutine
// that waits on a periodic channel forever, with no way to reach it and no way
// to end it, and a test that runs in simulated time reports exactly that: the
// clock is the test's, so an entry's whole lifetime passes in no time at all,
// and a goroutine still waiting when the test returns is a goroutine that was
// never going to stop. A store that expires an entry when it is asked rather
// than on a timer has nothing left waiting and the same test says so.
//
// It is the only test in this package on purpose. What the store does today
// ends the process rather than the test, so anything sharing the package with
// it would be reported as failing for a reason that has nothing to do with it.
//
// Everything here goes through the exported constructor and the `types.Store`
// interface.
package sweeper

import (
	"testing"
	"testing/synctest"
	"time"

	"github.com/codedellemc/libstorage/api/utils"
)

func TestAStoreWithALifetimeLeavesNothingRunningBehind(t *testing.T) {
	synctest.Test(t, func(t *testing.T) {
		s := utils.NewTTLStore(time.Minute, true)
		s.Set("k", "v")

		time.Sleep(2 * time.Minute)

		if s.IsSet("k") {
			t.Error(`IsSet("k") is true two lifetimes after the entry was set`)
		}
		if got := s.Get("k"); got != nil {
			t.Errorf(`Get("k") = %v two lifetimes after the entry was set, `+
				`want nil`, got)
		}
		if got := s.Keys(); len(got) != 0 {
			t.Errorf("Keys() = %v two lifetimes after the entry was set, "+
				"want none", got)
		}
		if got := s.Map(); len(got) != 0 {
			t.Errorf("Map() = %v two lifetimes after the entry was set, "+
				"want empty", got)
		}
	})
}

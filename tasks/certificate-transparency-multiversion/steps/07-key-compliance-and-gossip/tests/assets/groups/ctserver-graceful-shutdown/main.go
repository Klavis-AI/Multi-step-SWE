// Group ctserver-graceful-shutdown.
//
// v1.0.21 changes how ct_server ends (CLI.md section 2.3). On SIGINT or
// SIGTERM it closes the HTTP server with a grace period for in-flight
// requests, ListenAndServe returns http.ErrServerClosed, and main returns
// normally: exit status 0 with the listener released. Every release up to and
// including v1.0.19 calls os.Exit(1) straight out of the signal handler, so
// the same signal there gives status 1.
//
// This case moved out of gate-endtoend. It is a transition this release
// deliberately makes, so it is not regression: in the gate it vetoed every
// candidate run against the step-06 tree, where exit 1 is the correct
// v1.0.19 behaviour, and the step's own no-op continuation reported a FAILED
// GATE instead of a zero score.
//
// Both handled signals are graded, each against its own personality, because
// a handler wired for one and not the other is a real and likely partial
// implementation. Each case asserts three things at once: that the
// personality was serving immediately before the signal, how the process
// ended, and that the listener really was released rather than the process
// merely happening to exit.
//
// SCOPE-NET: loopback only, every port obtained by binding :0. SCOPE-BACKEND:
// the log behind each personality is this group's own pinned stub.
// SCOPE-TIME: no elapsed time is asserted and no sleep is used as
// synchronisation. The wait for the process to end is bounded and a server
// still serving afterwards reports itself as "still-serving" rather than
// hanging the group, so the verdict is the same however slow the host is.
package main

import (
	"fmt"
	"os"
	"syscall"

	ct "github.com/google/certificate-transparency-go"

	"ctverify/vhelp"
)

const shutdownLogID = int64(1601)

// endsCleanly starts a personality of its own, checks it is serving, sends it
// one signal and reports "serving=<status> <how it ended> listener-closed=<v>".
func endsCleanly(t *vhelp.T, name string, sig os.Signal) string {
	h := startPersonalityNamed(t, name, nil,
		logConfigText(t, shutdownLogID, logPrefix, false, ""))
	defer h.stop()

	before := h.get(t, h.baseURL+"/"+logPrefix+ct.GetSTHPath).status
	ended := h.shutdownBySignal(t, sig)
	return fmt.Sprintf("serving=%d %s listener-closed=%v", before, ended, h.listenerClosed())
}

func main() {
	t := vhelp.New()

	t.Equal("sigterm-shuts-the-listener-down-and-exits-zero",
		endsCleanly(t, "sigterm", syscall.SIGTERM),
		"serving=200 exit=0 listener-closed=true")

	t.Equal("sigint-shuts-the-listener-down-and-exits-zero",
		endsCleanly(t, "sigint", syscall.SIGINT),
		"serving=200 exit=0 listener-closed=true")

	t.Done()
}

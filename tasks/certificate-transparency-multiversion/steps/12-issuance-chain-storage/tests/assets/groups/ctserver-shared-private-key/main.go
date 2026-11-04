// ctserver-shared-private-key observes the guard this release adds to
// ct_server: two logs registered by one process may not be configured with
// the same signing key. A CT log's log ID is the hash of its public key, so
// two logs sharing a key share an identity; the server now refuses to start
// rather than serve two prefixes under one name.
//
// The guard is a command-line capability -- it lives in the startup path of
// the ct_server binary and has no library entry point -- so it is observed by
// starting the binary and reading the status it exits with. Keys are pinned
// fixtures and every port comes from binding :0.
package main

import (
	"net"
	"net/http"
	"os"
	"time"

	"github.com/google/trillian"
	"google.golang.org/grpc"

	"ctverify/vhelp"
)

// backendOn serves the pinned stub on its own loopback socket.
func backendOn(t *vhelp.T, be *stubBackend) (*grpc.Server, string) {
	lis, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen for backend: %v", err)
	}
	gs := grpc.NewServer()
	trillian.RegisterTrillianLogServer(gs, be)
	go func() { _ = gs.Serve(lis) }()
	return gs, lis.Addr().String()
}

// healthProbe and awaitHealthy below are this group's own, deliberately not in
// serverkit.go: that file must stay byte-identical to groups/_kit/serverkit.go
// across every copy, and only this group needs to watch a server that may be
// about to die. Keeping them here is what makes the kit safe to regenerate.
// healthProbe is the outcome of watching ct_server's liveness endpoint while
// the process may still be starting, serving, or already gone.
type healthProbe struct {
	status int
	body   []byte
	detail string
}

// awaitHealthy polls /healthz until the server answers 200 or the process
// exits, so a caller that expects the server to stay up does not have to race
// its start-up and a caller that expects it to die gets a definite answer.
func (h *harness) awaitHealthy(t *vhelp.T, d time.Duration) healthProbe {
	deadline := time.Now().Add(d)
	for {
		resp, err := http.Get(h.baseURL + "/healthz")
		if err == nil {
			b := readAll(resp.Body)
			_ = resp.Body.Close()
			if resp.StatusCode == http.StatusOK {
				return healthProbe{status: resp.StatusCode, body: b, detail: "healthy"}
			}
		} else if h.exited() {
			tail, _ := os.ReadFile(h.stderrTo)
			return healthProbe{detail: "process exited; log tail: " + lastBytes(tail, 600)}
		}
		if time.Now().After(deadline) {
			tail, _ := os.ReadFile(h.stderrTo)
			return healthProbe{detail: "timed out waiting for /healthz; log tail: " + lastBytes(tail, 600)}
		}
		time.Sleep(50 * time.Millisecond)
	}
}

func main() {
	t := vhelp.New()

	logPriv := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-key.pem"))
	intPriv := pemBody(t, vhelp.ReadFixture("minted", "keys", "intermediate-key.pem"))
	rootPriv := pemBody(t, vhelp.ReadFixture("minted", "keys", "root-key.pem"))

	// serve runs one ct_server over the pinned stub with the given logs, and
	// with whatever extra command-line arguments the case needs.
	serve := func(id string, logs []logSpec, wantUp bool, extraArgs ...string) {
		be := newStub()
		gs, addr := backendOn(t, be)
		defer gs.Stop()
		cfg := writeConfig(t, addr, logs)
		h, err := launch(t, be, nil, cfg, extraArgs, false)
		if err != nil {
			t.Check(id, false, "could not start ct_server: %v", err)
			return
		}
		if !wantUp {
			code := h.waitExit(60 * 1000 * 1000 * 1000)
			h.stop()
			t.Check(id, code == 1, "ct_server with a duplicated signing key exited %d, want 1", code)
			return
		}
		// Up means it binds and answers; the group then kills its own child.
		r := h.awaitHealthy(t, 60*1000*1000*1000)
		h.stop()
		t.Check(id, r.status == http.StatusOK && string(r.body) == "ok",
			"ct_server with distinct signing keys never served /healthz: %s", r.detail)
	}

	l1 := logSpec{id: pinnedLogID, prefix: logPrefix, privKey: logPriv}
	l2 := logSpec{id: pinnedLogID2, prefix: logPrefix2, privKey: intPriv}
	l3 := logSpec{id: 1003, prefix: "bench3", privKey: rootPriv}

	// Rejection: the same key twice, whether or not a public key is spelled out.
	serve("two-logs-same-private-key-refuses-to-start", []logSpec{l1, {id: pinnedLogID2, prefix: logPrefix2, privKey: logPriv}}, false)

	pub := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-pubkey.pem"))
	serve("two-logs-same-key-and-public-key-refuses-to-start",
		[]logSpec{
			{id: pinnedLogID, prefix: logPrefix, privKey: logPriv, pubKey: pub},
			{id: pinnedLogID2, prefix: logPrefix2, privKey: logPriv, pubKey: pub},
		}, false)

	// The collision is detected against every log already registered, not
	// only against the immediately preceding one.
	serve("collision-with-a-non-adjacent-log-refuses-to-start", []logSpec{l1, l2, {id: 1003, prefix: "bench3", privKey: logPriv}}, false)

	// Acceptance: distinct keys are still perfectly legal.
	serve("two-logs-distinct-keys-starts", []logSpec{l1, l2}, true)
	serve("three-logs-distinct-keys-starts", []logSpec{l1, l2, l3}, true)
	serve("one-log-starts", []logSpec{l1}, true)

	// The guard is about identity, not about a log being alone: one log on a
	// real LRU issuance-chain cache still starts, so the guard cannot be a
	// blanket refusal and does not interact with the cache selection.
	serve("single-log-with-lru-cache-starts", []logSpec{l1}, true,
		"--cache_type=lru", "--cache_size=16", "--cache_ttl=1h")

	// A duplicated key is refused before the server ever binds its HTTP
	// endpoint, so nothing answers at the prefix. The port's history is
	// checked by the fact that the process is gone.
	be := newStub()
	gs, addr := backendOn(t, be)
	defer gs.Stop()
	h, err := launch(t, be, nil, writeConfig(t, addr, []logSpec{l1, {id: pinnedLogID2, prefix: logPrefix2, privKey: logPriv}}), nil, false)
	if err != nil {
		t.Check("duplicate-key-leaves-no-listener", false, "could not start ct_server: %v", err)
	} else {
		r := h.awaitHealthy(t, 20*1000*1000*1000)
		_, finalDialErr := http.Get(h.baseURL + "/healthz")
		h.stop()
		t.Check("duplicate-key-leaves-no-listener", r.status == 0 && finalDialErr != nil,
			"ct_server answered /healthz with %d (%s); want no listener at all", r.status, r.detail)
	}

	t.Done()
}

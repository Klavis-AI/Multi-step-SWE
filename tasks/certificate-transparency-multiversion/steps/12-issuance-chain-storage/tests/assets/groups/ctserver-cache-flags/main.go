// ctserver-cache-flags exercises the three command-line flags this release
// adds to ct_server for the issuance chain cache: --cache_type, --cache_size
// and --cache_ttl. The capability is a command-line one, so it is observed by
// running the candidate's own ct_server as a subprocess and looking at
// whether it comes up and at the status it exits with.
//
// The defaults matter as much as the accepted values. --cache_size and
// --cache_ttl default to negative sentinels, so asking for the LRU cache
// without sizing it is a start-up failure, not a silently unbounded cache.
package main

import (
	"net"
	"net/http"

	"github.com/google/trillian"
	"google.golang.org/grpc"

	"ctverify/vhelp"
)

const startFailExit = 1

// backendAt serves the pinned stub on its own loopback socket and hands back
// the address, so that one backend can outlive several ct_server attempts.
func backendAt(t *vhelp.T, be *stubBackend) (*grpc.Server, string) {
	lis, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen for backend: %v", err)
	}
	gs := grpc.NewServer()
	trillian.RegisterTrillianLogServer(gs, be)
	go func() { _ = gs.Serve(lis) }()
	return gs, lis.Addr().String()
}

func main() {
	t := vhelp.New()

	privDER := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-key.pem"))
	be := newStub()
	gs, addr := backendAt(t, be)
	defer gs.Stop()
	cfg := writeConfig(t, addr, []logSpec{{id: pinnedLogID, prefix: logPrefix, privKey: privDER}})

	// serves reports whether ct_server came up and answered /healthz.
	serves := func(id string, args ...string) {
		h, err := launch(t, be, nil, cfg, args, true)
		if err != nil {
			t.Check(id, false, "ct_server did not come up with %v: %v", args, err)
			return
		}
		r := h.get(t, h.baseURL+"/healthz")
		h.stop()
		t.Check(id, r.status == http.StatusOK && string(r.body) == "ok",
			"ct_server with %v answered /healthz %d %q", args, r.status, string(r.body))
	}
	// refuses reports whether ct_server declined to start at all.
	refuses := func(id string, args ...string) {
		h, err := launch(t, be, nil, cfg, args, false)
		if err != nil {
			t.Check(id, false, "could not start ct_server with %v: %v", args, err)
			return
		}
		code := h.waitExit(60 * 1000 * 1000 * 1000)
		h.stop()
		t.Check(id, code == startFailExit, "ct_server with %v exited %d, want %d", args, code, startFailExit)
	}

	// Accepted shapes.
	serves("no-cache-flags-still-starts")
	serves("cache-type-noop-starts", "--cache_type=noop")
	serves("cache-type-lru-with-size-and-ttl-starts", "--cache_type=lru", "--cache_size=16", "--cache_ttl=1h")
	serves("cache-type-lru-accepts-zero-size-and-ttl", "--cache_type=lru", "--cache_size=0", "--cache_ttl=0s")
	serves("cache-size-and-ttl-are-ignored-by-noop", "--cache_type=noop", "--cache_size=8", "--cache_ttl=30m")

	// Rejected shapes: the negative defaults are not a usable LRU cache.
	refuses("cache-type-lru-alone-refuses-to-start", "--cache_type=lru")
	refuses("cache-type-lru-without-ttl-refuses-to-start", "--cache_type=lru", "--cache_size=16")
	refuses("cache-type-lru-without-size-refuses-to-start", "--cache_type=lru", "--cache_ttl=1h")
	refuses("negative-cache-size-refuses-to-start", "--cache_type=lru", "--cache_size=-2", "--cache_ttl=1h")
	refuses("unknown-cache-type-refuses-to-start", "--cache_type=redis", "--cache_size=16", "--cache_ttl=1h")

	t.Done()
}

// Group ctfe-mirror-sth-getter.
//
// v1.0.21 replaces the free tree-head fetch with the ctfe.STHGetter interface
// and two implementations behind it: LogSTHGetter, which signs with the log's
// own key, and MirrorSTHGetter, which returns whatever a MirrorSTHStorage
// hands it, capped at the Trillian tree size (CLIENT-API.md section 8.3).
//
// Observed end to end: this group supplies its own MirrorSTHStorage, sets up a
// mirror instance, serves the returned handlers on a loopback socket and reads
// get-sth over HTTP. The expected response body is written out here as exact
// JSON, with the base64 fields built from pinned bytes by encoding/base64 --
// not by the product's own marshaller -- so the comparison is against the
// format HTTP-API.md specifies.
//
// SCOPE-BACKEND: the Trillian client speaks to this group's in-process stub,
// which is graded only in that the mirror must ask the storage for an STH no
// larger than the tree size the stub reports. SCOPE-TIME: every value below
// is a compile-time constant or a hash of one.
//
// v1.0.19 has no STHGetter, no MirrorSTHGetter and no MirrorSTHStorage, so
// this driver does not compile against that tree.
package main

import (
	"context"
	"crypto/sha256"
	"encoding/base64"
	"fmt"
	"net"
	"net/http"
	"sync"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/certificate-transparency-go/trillian/ctfe"

	"ctverify/vhelp"
)

// The STH this group's storage serves. None of it comes from a clock.
const (
	mirrorTreeSize  = uint64(42)
	mirrorTimestamp = uint64(1400000000000)
)

var (
	mirrorRootHash = sha256.Sum256([]byte("harbor-ct-bench-mirror-root"))
	mirrorSigBytes = []byte("harbor-ct-bench-pinned-mirror-signature")
)

// pinnedStorage is this group's MirrorSTHStorage. It records the cap it was
// asked for, so the driver can check the mirror passes the backend tree size.
type pinnedStorage struct {
	mu       sync.Mutex
	maxSeen  int64
	calls    int
	failNext bool
}

func (s *pinnedStorage) GetMirrorSTH(ctx context.Context, maxTreeSize int64) (*ct.SignedTreeHead, error) {
	s.mu.Lock()
	s.maxSeen = maxTreeSize
	s.calls++
	fail := s.failNext
	s.mu.Unlock()
	if fail {
		return nil, fmt.Errorf("this storage has no STH")
	}
	return &ct.SignedTreeHead{
		Version:        ct.V1,
		TreeSize:       mirrorTreeSize,
		Timestamp:      mirrorTimestamp,
		SHA256RootHash: mirrorRootHash,
		TreeHeadSignature: ct.DigitallySigned{
			Algorithm: tls.SignatureAndHashAlgorithm{Hash: tls.SHA256, Signature: tls.ECDSA},
			Signature: mirrorSigBytes,
		},
	}, nil
}

// serve publishes a PathHandlers map on a loopback socket of its own.
func serve(t *vhelp.T, h *ctfe.PathHandlers) (string, func()) {
	mux := http.NewServeMux()
	for p, ah := range *h {
		mux.Handle(p, ah)
	}
	lis, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen for handlers: %v", err)
	}
	srv := &http.Server{Handler: mux}
	go func() { _ = srv.Serve(lis) }()
	return "http://" + lis.Addr().String(), func() { _ = srv.Close() }
}

// wantSTHJSON is the exact body RFC 6962 get-sth must return for the STH
// above, assembled here from the pinned values.
func wantSTHJSON() string {
	// TLS encoding of a DigitallySigned: hash algorithm, signature algorithm,
	// then a uint16-prefixed opaque (TLS-CODEC.md section 2).
	sig := []byte{byte(tls.SHA256), byte(tls.ECDSA),
		byte(len(mirrorSigBytes) >> 8), byte(len(mirrorSigBytes))}
	sig = append(sig, mirrorSigBytes...)
	return fmt.Sprintf(`{"tree_size":%d,"timestamp":%d,"sha256_root_hash":%q,"tree_head_signature":%q}`,
		mirrorTreeSize, mirrorTimestamp,
		base64.StdEncoding.EncodeToString(mirrorRootHash[:]),
		base64.StdEncoding.EncodeToString(sig))
}

func main() {
	t := vhelp.New()

	// --- the interface shape --------------------------------------------------
	var logGetter ctfe.STHGetter = (*ctfe.LogSTHGetter)(nil)
	var mirrorGetter ctfe.STHGetter = (*ctfe.MirrorSTHGetter)(nil)
	var storage ctfe.MirrorSTHStorage = &pinnedStorage{}
	var dflt ctfe.MirrorSTHStorage = ctfe.DefaultMirrorSTHStorage{}
	t.Check("sthgetter-implementations-satisfy-interface",
		logGetter != nil && mirrorGetter != nil && storage != nil && dflt != nil,
		"one of the STHGetter/MirrorSTHStorage implementations is missing")

	ctx := context.Background()
	sth, err := ctfe.DefaultMirrorSTHStorage{}.GetMirrorSTH(ctx, 1)
	t.Check("default-mirror-storage-refuses", sth == nil && err != nil,
		"the default mirror storage returned an STH")

	st, err := ctfe.DefaultMirrorSTHFactory{}.NewStorage([sha256.Size]byte{})
	t.Check("default-mirror-factory-builds-storage", err == nil && st != nil,
		"DefaultMirrorSTHFactory.NewStorage failed: %v", err)

	// --- a mirror instance served over a real socket ---------------------------
	_, gs, client := startBackend(t)
	defer gs.Stop()

	store := &pinnedStorage{}
	h, err := ctfe.SetUpMirrorInstance(ctx, mirrorOpts(t, client), store)
	if err != nil {
		t.Fatalf("SetUpMirrorInstance: %v", err)
	}
	base, stop := serve(t, h)
	defer stop()

	hh := &harness{}
	res := hh.get(t, base+"/"+logPrefix+"/ct/v1/get-sth")
	t.Equal("mirror-get-sth-status", res.status, http.StatusOK)
	t.Equal("mirror-get-sth-body", string(res.body), wantSTHJSON())
	t.Equal("mirror-get-sth-content-type", res.header.Get("Content-Type"), "application/json")

	store.mu.Lock()
	maxSeen, calls := store.maxSeen, store.calls
	store.mu.Unlock()
	t.Equal("mirror-storage-asked-for-backend-tree-size", maxSeen, pinnedTreeSize)
	t.Check("mirror-storage-was-consulted", calls == 1, "storage was called %d times, want 1", calls)

	store.mu.Lock()
	store.failNext = true
	store.mu.Unlock()
	res = hh.get(t, base+"/"+logPrefix+"/ct/v1/get-sth")
	t.Check("mirror-get-sth-storage-error-is-not-200", res.status != http.StatusOK,
		"a storage that has no STH still produced %d", res.status)

	t.Done()
}

// Group inherited-http-log-client.
//
// The log client and the wire protocol it speaks are unchanged from v1.0.10.
// Observed the way a candidate would be observed: a real loopback HTTP server
// stands in for the log, and the driver asserts on the requests that arrive
// and on what the client makes of the answers.
package main

import (
	"context"
	"encoding/base64"
	"net/http"
	"net/http/httptest"
	"net/url"
	"sync"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/client"
	"github.com/google/certificate-transparency-go/jsonclient"

	"ctverify/vhelp"
)

const (
	pinnedTreeSize = uint64(4)
	wantHashB64    = "qheNiv1I4/iMtBld/bKp5IF9lfpX/TREDZP5quXE+Cs="
)

type seen struct {
	mu      sync.Mutex
	method  string
	path    string
	hash    string
	size    string
	headers http.Header
}

func main() {
	t := vhelp.New()
	ctx := context.Background()

	s := &seen{}
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		s.mu.Lock()
		s.method, s.path, s.headers = r.Method, r.URL.Path, r.Header.Clone()
		s.hash, s.size = r.URL.Query().Get("hash"), r.URL.Query().Get("tree_size")
		s.mu.Unlock()

		w.Header().Set("Content-Type", "application/json")
		switch r.URL.Path {
		case ct.GetSTHPath:
			w.Write([]byte(`{"tree_size":4,"timestamp":1400000000000,` +
				`"sha256_root_hash":"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=",` +
				`"tree_head_signature":"BAMASABGMCwCIAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAiAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=="}`))
		case ct.GetProofByHashPath:
			w.Write([]byte(`{"leaf_index":2,"audit_path":["AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8="]}`))
		case ct.GetRootsPath:
			w.Write([]byte(`{"certificates":["AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8="]}`))
		case ct.AddChainPath:
			w.Write([]byte(`{"sct_version":0,"id":"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=",` +
				`"timestamp":1400000000000,"extensions":"","signature":"BAMASABGMCwCIAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAiAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=="}`))
		default:
			w.WriteHeader(http.StatusNotFound)
			w.Write([]byte(`{"error_message":"unknown endpoint"}`))
		}
	}))
	defer srv.Close()

	lc, err := client.New(srv.URL, srv.Client(), jsonclient.Options{})
	t.Case("a-client-can-be-built-against-the-log-uri", err)
	if err != nil {
		t.Fatalf("cannot continue without a client: %v", err)
	}

	// --- get-sth ------------------------------------------------------------
	sth, err := lc.GetSTH(ctx)
	t.Case("get-sth-returns-the-tree-head", err)
	if err == nil {
		s.mu.Lock()
		gotMethod, gotPath := s.method, s.path
		s.mu.Unlock()
		t.Equal("get-sth-uses-get-on-the-documented-path", gotMethod+" "+gotPath,
			"GET "+ct.GetSTHPath)
		t.Equal("get-sth-reports-the-server-tree-size", sth.TreeSize, uint64(4))
		t.Equal("get-sth-reports-the-server-timestamp", sth.Timestamp, uint64(1400000000000))
		t.Equal("get-sth-copies-the-root-hash-into-its-fixed-size-field",
			len(sth.SHA256RootHash), 32)
		t.Check("get-sth-keeps-the-signature-it-was-given",
			len(sth.TreeHeadSignature.Signature) > 0, "no signature retained")
	}

	// --- get-proof-by-hash --------------------------------------------------
	wantB64 := url.QueryEscape(wantHashB64)
	resp, err := lc.GetProofByHash(ctx, mustHash(t, wantHashB64), pinnedTreeSize)
	t.Case("get-proof-by-hash-returns-an-audit-path", err)
	if err == nil {
		s.mu.Lock()
		gotHash, gotSize, gotMethod, gotPath := s.hash, s.size, s.method, s.path
		s.mu.Unlock()
		t.Equal("get-proof-by-hash-sends-the-hash-base64-in-the-query",
			url.QueryEscape(gotHash), wantB64)
		t.Equal("get-proof-by-hash-sends-the-tree-size-as-base-10", gotSize, "4")
		t.Equal("get-proof-by-hash-uses-the-documented-path-and-method",
			gotMethod+" "+gotPath, "GET "+ct.GetProofByHashPath)
		t.Equal("get-proof-by-hash-reports-the-server-leaf-index", resp.LeafIndex, int64(2))
		t.Equal("get-proof-by-hash-returns-every-sibling-it-was-given",
			len(resp.AuditPath), 1)
	}
	// --- get-roots ----------------------------------------------------------
	roots, err := lc.GetAcceptedRoots(ctx)
	t.Case("get-accepted-roots-returns-the-servers-roots", err)
	if err == nil {
		t.Equal("get-accepted-roots-returns-one-accepted-root", len(roots), 1)
	}

	// --- a rejection is not silently treated as an answer -------------------
	_, err = lc.GetSTH(ctx)
	t.Check("a-successful-call-does-not-mistake-itself-for-a-failure",
		err == nil, "unexpected error %v", err)

	bad := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusBadRequest)
		w.Write([]byte(`{"error_message":"pinned rejection"}`))
	}))
	defer bad.Close()
	badClient, err := client.New(bad.URL, bad.Client(), jsonclient.Options{})
	t.Case("a-client-can-be-built-against-a-hostile-log-uri", err)
	if err == nil {
		_, sterr := badClient.GetSTH(ctx)
		t.Check("an-http-error-status-is-reported-as-an-error",
			sterr != nil, "a 400 response was accepted as a tree head")
	}

	t.Done()
}

// mustHash turns the pinned base64 leaf hash into bytes without going through
// the client, so the value the driver asks for is fixed by the corpus.
func mustHash(t *vhelp.T, b64 string) []byte {
	b, err := base64.StdEncoding.DecodeString(b64)
	if err != nil {
		t.Fatalf("corpus base64 %q: %v", b64, err)
	}
	return b
}

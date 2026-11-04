// Group ctfe-cors-headers.
//
// v1.0.17 puts every CTFE handler behind cors.AllowAll() (commit 99ced42e,
// "Allow cross-origin requests"): ct_server builds a corsMux, registers the
// log handlers and the root handler on it, and installs
// cors.AllowAll().Handler(corsMux) at "/". The stated reason is that a CT log
// is public and unauthenticated, so a browser on any origin may read it.
// v1.0.13 registered the handlers straight onto http.DefaultServeMux and
// emitted no CORS headers at all.
//
// Graded over loopback HTTP against the candidate's own ct_server (see
// serverkit.go), on the response headers a browser would act on: the simple
// cross-origin request, the OPTIONS preflight, and the Vary contract that
// makes either cacheable.
//
// SCOPE-NET: 127.0.0.1, ports from serverkit.go's freeLoopbackPort.
// SCOPE-TIME: the tree the
// backend serves is pinned, so case 5 compares against constants.
//
// No new identifier is involved: this driver compiles against v1.0.13, where
// the headers are simply absent and the preflight is refused.
package main

import (
	"encoding/json"
	"fmt"
	"net/http"
	"strings"

	"ctverify/vhelp"
)

// withHeaders issues one request carrying the given headers.
func withHeaders(t *vhelp.T, h *harness, method, url string, hdr map[string]string) httpResult {
	req, err := http.NewRequest(method, url, nil)
	if err != nil {
		t.Fatalf("build %s %s: %v", method, url, err)
	}
	for k, v := range hdr {
		req.Header.Set(k, v)
	}
	return h.do(t, req)
}

const browserOrigin = "https://viewer.bench.invalid"

func main() {
	t := vhelp.New()

	h := startPersonality(t, nil, "")
	defer h.stop()

	// --- 1. a simple cross-origin read is allowed from anywhere ------------
	res := withHeaders(t, h, http.MethodGet, h.logURL("/ct/v1/get-sth"),
		map[string]string{"Origin": browserOrigin})
	t.Equal("get-sth-carries-wildcard-allow-origin",
		fmt.Sprintf("status=%d acao=%q", res.status, res.header.Get("Access-Control-Allow-Origin")),
		`status=200 acao="*"`)

	// --- 2. the preflight for a write is answered, not routed --------------
	pre := withHeaders(t, h, http.MethodOptions, h.logURL("/ct/v1/add-chain"), map[string]string{
		"Origin":                        browserOrigin,
		"Access-Control-Request-Method": "POST",
	})
	t.Equal("preflight-answered-with-allowed-method",
		fmt.Sprintf("status=%d acao=%q acam=%q", pre.status,
			pre.header.Get("Access-Control-Allow-Origin"),
			pre.header.Get("Access-Control-Allow-Methods")),
		`status=200 acao="*" acam="POST"`)

	// --- 3. and declares what the answer depends on ------------------------
	t.Equal("preflight-varies-on-origin-and-request-headers",
		strings.Join(pre.header.Values("Vary"), "|"),
		"Origin|Access-Control-Request-Method|Access-Control-Request-Headers")

	// --- 4. the root handler sits behind the same wrapper ------------------
	root := withHeaders(t, h, http.MethodGet, h.rootURL("/"),
		map[string]string{"Origin": browserOrigin})
	t.Equal("root-path-is-cors-wrapped-too",
		fmt.Sprintf("status=%d acao=%q", root.status, root.header.Get("Access-Control-Allow-Origin")),
		`status=200 acao="*"`)

	// --- 5. and the catch-all it wraps narrows to exactly "/" --------------
	// The same commit that adds the wrapper replaces v1.0.13's unconditional
	// root handler -- which answered 200 for every unmatched path -- with one
	// that serves 200 only for "/" and 404 for anything else. Asserted here
	// rather than left to gate-endtoend because it is a change of *this*
	// release and belongs with the handler it ships alongside.
	// TEST_INTERFACE.md section 6, last bullet; HTTP-API.md section 7.
	other := withHeaders(t, h, http.MethodGet, h.rootURL("/not-a-handler"),
		map[string]string{"Origin": browserOrigin})
	t.Equal("unclaimed-path-is-404-and-still-cors-wrapped",
		fmt.Sprintf("status=%d acao=%q", other.status, other.header.Get("Access-Control-Allow-Origin")),
		`status=404 acao="*"`)

	// --- 6. control: wrapping changed the headers and nothing else ---------
	var sth struct {
		TreeSize       uint64 `json:"tree_size"`
		Timestamp      uint64 `json:"timestamp"`
		SHA256RootHash []byte `json:"sha256_root_hash"`
	}
	if err := json.Unmarshal(res.body, &sth); err != nil {
		t.Check("cors-does-not-change-the-sth-body", false, "decode get-sth: %v", err)
	} else {
		t.Equal("cors-does-not-change-the-sth-body",
			fmt.Sprintf("size=%d ts=%d root=%x", sth.TreeSize, sth.Timestamp, sth.SHA256RootHash),
			fmt.Sprintf("size=%d ts=%d root=%x", pinnedTreeSize, pinnedTimestampMS, pinnedRootHash[:]))
	}

	t.Done()
}

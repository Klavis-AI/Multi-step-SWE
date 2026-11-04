// Group inherited-ctfe-config-acceptance.
//
// The ct_server configuration surface is unchanged at this milestone: a
// well-formed LogConfigSet textproto naming a private key, a roots PEM file
// and a URL prefix is accepted, the server starts, and it serves that log on
// that prefix. Step 06 adds two flags to the same binary and a ChargeTo to
// every backend call, so the configuration path it all hangs off is worth
// re-asserting here.
//
// SCOPE-BACKEND: boots the candidate's own ct_server against an in-process
// Trillian v1.2.0 stub over loopback gRPC (the shared _kit serverkit).
// SCOPE-TIME: the backend serves a pinned tree size, root hash and timestamp;
// nothing here reads a clock.
package main

import (
	"encoding/base64"
	"encoding/json"
	"fmt"

	"ctverify/vhelp"
)

func main() {
	t := vhelp.New()

	h := startPersonality(t, nil, "")
	defer h.stop()

	// --- 1. the configured log answers on its own prefix --------------------
	res := h.get(t, h.logURL("/ct/v1/get-sth"))
	t.Equal("configured-prefix-serves-get-sth", res.status, 200)

	// --- 2. with the tree the configured backend reports --------------------
	var sth struct {
		TreeSize  int64  `json:"tree_size"`
		Timestamp int64  `json:"timestamp"`
		RootHash  []byte `json:"sha256_root_hash"`
	}
	if err := json.Unmarshal(res.body, &sth); err != nil {
		t.Check("get-sth-reports-the-configured-backend", false, "unmarshal: %v", err)
	} else {
		t.Equal("get-sth-reports-the-configured-backend",
			fmt.Sprintf("size=%d ts=%d root=%s", sth.TreeSize, sth.Timestamp,
				base64.StdEncoding.EncodeToString(sth.RootHash)),
			fmt.Sprintf("size=%d ts=%d root=%s", pinnedTreeSize, pinnedTimestampMS,
				base64.StdEncoding.EncodeToString(pinnedRootHash[:])))
	}

	// --- 3. the roots the configuration named are published -----------------
	roots := h.get(t, h.logURL("/ct/v1/get-roots"))
	var rootsBody struct {
		Certificates [][]byte `json:"certificates"`
	}
	err := json.Unmarshal(roots.body, &rootsBody)
	t.Equal("configured-roots-are-published",
		fmt.Sprintf("status=%d err=%v roots=%d", roots.status, err, len(rootsBody.Certificates)),
		fmt.Sprintf("status=%d err=%v roots=%d", 200, error(nil), 2))

	// --- 4. and only on that prefix -----------------------------------------
	// The same endpoint under no prefix is not the configured log.
	unprefixed := h.get(t, h.rootURL("/ct/v1/get-sth"))
	t.Equal("unconfigured-prefix-does-not-serve-the-log", unprefixed.status, 404)

	// --- 5. an endpoint the log does not define is refused ------------------
	bogus := h.get(t, h.logURL("/ct/v1/get-nothing"))
	t.Equal("undefined-endpoint-on-the-configured-prefix-is-refused", bogus.status, 404)

	t.Done()
}

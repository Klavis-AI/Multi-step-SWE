// Group inherited-ctfe-config-acceptance.
//
// The ct_server configuration parser and validator remain functional at this
// milestone. This group confirms that a well-formed LogConfigSet textproto with
// a valid key, root source, and log prefix is accepted, the server starts, and
// it serves the same log id and prefix through get-sth.
//
// SCOPE-TIME: the backend serves a pinned tree size, root hash, and timestamp;
// nothing here reads a clock.
//
// SCOPE-BACKEND: boots the candidate's own ct_server against an in-process
// Trillian v1.2.0 stub over loopback gRPC (the shared _kit serverkit).
package main

import (
	"encoding/json"

	"ctverify/vhelp"
)

func main() {
	t := vhelp.New()

	h := startPersonality(t, nil, "")
	defer h.stop()

	// --- 1. the configured log answers on its own prefix --------------------
	res := h.get(t, h.logURL("/ct/v1/get-sth"))
	t.Equal("configured-prefix-serves-get-sth", res.status, 200)

	var sth struct {
		TreeSize  int64  `json:"tree_size"`
		Timestamp int64  `json:"timestamp"`
		RootHash  string `json:"sha256_root_hash"`
	}
	if err := json.Unmarshal(res.body, &sth); err != nil {
		t.Check("get-sth-body-parses", false, "unmarshal: %v", err)
	} else {
		t.Check("get-sth-body-parses", true, "")
		t.Equal("tree-size-comes-from-the-config-backed-log", sth.TreeSize, pinnedTreeSize)
		t.Equal("timestamp-comes-from-the-config-backed-log", sth.Timestamp, pinnedTimestampMS)
	}

	t.Done()
}

// Group gate-endtoend -- KIND gate (the inherited end-to-end path).
//
// One deterministic end-to-end path through the milestone step 02 inherits.
// The candidate's own ct_server binary is run as a subprocess over real
// loopback sockets, configured the way step 01 configured it: a LogConfigSet
// text proto in --log_config plus a single backend address in
// --log_rpc_server. v1.0.5 converts that shape into a LogMultiConfig on load
// rather than keeping a second code path, so the shape is *preserved*, not
// superseded, and it is legitimate regression.
//
// What is deliberately NOT here (gate rule 3, "behaviour this step changes is
// not regression", and rule 4, "nothing in the gate may be satisfiable by the
// new capability alone"):
//
//   - the second legal configuration shape, a LogMultiConfig with several
//     LogBackends and a log bound to each. That is the step's headline new
//     capability; it is graded and priced by the scored groups
//     config-multi-from-file, config-single-to-multi, backend-routing-add-chain
//     and backend-shared-by-two-logs.
//   - the whole-file validator's four refusals seen through the process. Those
//     are graded by the scored group config-multi-validation. They were also
//     the only assertions in this group bounded by elapsed time -- "the process
//     is still running after sixty seconds, so it accepted the file" -- which
//     is not a shape a gate case may have.
//
// Every one of those cases in the gate would have vetoed the whole step for a
// candidate that regressed nothing, and would have made this step's own no-op
// continuation report a FAILED GATE rather than a zero score.
//
// Determinism: the backend's tree size, root hash and root timestamp are
// compile-time constants; the stub backend serves on the listener a :0 bind
// handed back and the front end's two endpoints are probed for below the
// kernel's ephemeral range (serverkit's freePort), so no port is fixed; no
// assertion reads a clock, an elapsed time, an environment name or an
// unordered iteration -- the get-roots pool in particular is compared as a
// set, because section 4.6 fixes its members and not their order.
package main

import (
	"encoding/base64"
	"encoding/json"
	"net/http"
	"sort"
	"strings"

	"ctverify/vhelp"
)

type sthJSON struct {
	TreeSize          uint64 `json:"tree_size"`
	Timestamp         uint64 `json:"timestamp"`
	SHA256RootHash    []byte `json:"sha256_root_hash"`
	TreeHeadSignature []byte `json:"tree_head_signature"`
}

type rootsJSON struct {
	Certificates []string `json:"certificates"`
}

// rootsOf renders the get-roots pool as a *set*, not as a sequence. HTTP-API.md
// section 4.6 fixes which certificates come back -- "the PEM pool loaded from
// the log's roots_pem_file list, re-encoded as base64 DER" -- and states no
// order among them, and no other page adds one. A conforming front end whose
// trust pool is a map (a root pool is a set, so that is a reasonable choice)
// serves them in Go's randomised map order. Sorting both sides keeps the whole
// of what this gate proves -- the configured pool, and only it, is served --
// and drops an ordering the contract never promised.
func rootsOf(t *vhelp.T, r httpResult) string {
	var rs rootsJSON
	if err := json.Unmarshal(r.body, &rs); err != nil {
		return "unparseable: " + err.Error()
	}
	sorted := append([]string(nil), rs.Certificates...)
	sort.Strings(sorted)
	return strings.Join(sorted, ",")
}

func sthOf(t *vhelp.T, r httpResult) sthJSON {
	var s sthJSON
	if err := json.Unmarshal(r.body, &s); err != nil {
		t.Fatalf("get-sth body is not JSON: %v (%s)", err, string(r.body))
	}
	return s
}

func main() {
	t := vhelp.New()

	// The pinned trust pool this corpus configures: bench-root and
	// bench-intermediate, the two PEM blocks of roots-bench-plus-int.pem,
	// compared as a set -- both sides are sorted, see rootsOf.
	wantRootSet := []string{
		base64.StdEncoding.EncodeToString(vhelp.ReadFixture("minted", "bench-root.der")),
		base64.StdEncoding.EncodeToString(vhelp.ReadFixture("minted", "bench-intermediate.der")),
	}
	sort.Strings(wantRootSet)
	wantRoots := strings.Join(wantRootSet, ",")

	// The step-01 shape: a LogConfigSet on disk, the one backend named by the
	// flag. logConfigBody emits no log_backend_name when it is handed an empty
	// backend name, so the file is byte-identical to one step 01 would accept.
	be := newBackend("legacy", rootNanosLgc, rootHashLgcy[:], treeSizeLgcy)
	addr := be.serve(t)
	cfg := logConfigBody(t, "", legacyID, legacyPfx, "", "")
	h := start(t, writeScratch(t, "legacy.textproto", cfg), []string{"--log_rpc_server=" + addr}, legacyPfx)

	// 1-2. The log is served, and out of the root pool its config names.
	r := httpGet(t, h.url(legacyPfx, "/ct/v1/get-roots"))
	t.Equal("legacy-single-config-shape-still-served", r.status, http.StatusOK)
	t.Equal("legacy-shape-serves-the-same-root-pool", rootsOf(t, r), wantRoots)

	// 3. A prefix that is in no LogConfig has no handler at all: the handler
	// map is built from the configuration and from nothing else.
	t.Equal("unconfigured-prefix-is-not-served",
		httpGet(t, h.baseURL+"/bench-not-configured/ct/v1/get-roots").status, http.StatusNotFound)

	// 4. The tree head is the one the flag-named backend answered with, so the
	// whole path -- config load, instance set-up, RPC client, get-sth handler
	// -- is intact rather than merely the HTTP surface.
	s := sthOf(t, httpGet(t, h.url(legacyPfx, "/ct/v1/get-sth")))
	t.Equal("legacy-shape-reaches-the-flag-named-backend",
		[]interface{}{s.TreeSize, base64.StdEncoding.EncodeToString(s.SHA256RootHash)},
		[]interface{}{uint64(treeSizeLgcy), base64.StdEncoding.EncodeToString(rootHashLgcy[:])})

	// 5. And it is signed by the personality's own configured key, even though
	// the backend signed nothing. Only the presence of a signature is graded:
	// the signature itself covers a timestamp the personality takes from its
	// own clock.
	t.Check("tree-head-is-signed-by-the-personality", len(s.TreeHeadSignature) > 0,
		"tree_head_signature is %d bytes", len(s.TreeHeadSignature))

	// t.Done exits the process, so the child is stopped explicitly rather than
	// from a deferred call that would never run.
	h.stop()
	t.Done()
}

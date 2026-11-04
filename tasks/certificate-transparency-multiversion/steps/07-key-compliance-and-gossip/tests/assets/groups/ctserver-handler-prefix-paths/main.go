// Group ctserver-handler-prefix-paths.
//
// v1.0.21 gives ct_server a --handler_prefix flag (CLI.md section 2.1). Every
// handler path that does not carry its own prefix moves under it, so a log
// with prefix "bench" answers get-sth at "/logs/bench/ct/v1/get-sth" and no
// longer at "/bench/ct/v1/get-sth". Two paths are never prefixed: "/", the
// health check, and "/metrics" (HTTP-API.md section 1.1).
//
// Observed where the documentation puts it -- at the command line and over a
// real socket. The candidate's own ct_server is run as a subprocess twice:
// once with the flag and once without it, so "404 at the bare path" is
// evidence that the flag moved the handler rather than that the handler was
// never there.
//
// SCOPE-NET: loopback only, every port obtained by binding :0. SCOPE-BACKEND:
// the log behind the personality is this group's own stub.
//
// v1.0.19's ct_server has no --handler_prefix, so it exits 2 at start-up with
// "flag provided but not defined" and this group cannot report a single case
// against that tree.
package main

import (
	"encoding/json"
	"net/http"

	ct "github.com/google/certificate-transparency-go"

	"ctverify/vhelp"
)

const handlerPrefix = "/logs"

func chainJSON(t *vhelp.T, rel ...[]string) []byte {
	chain := make([][]byte, 0, len(rel))
	for _, r := range rel {
		chain = append(chain, vhelp.ReadFixture(r...))
	}
	b, err := json.Marshal(ct.AddChainRequest{Chain: chain})
	if err != nil {
		t.Fatalf("marshal add-chain request: %v", err)
	}
	return b
}

func main() {
	t := vhelp.New()

	cfg := logConfigText(t, pinnedLogID, logPrefix, false, "")

	// ---------------------------------------------------------------------
	// A personality started with --handler_prefix=/logs.
	// ---------------------------------------------------------------------
	h := startPersonalityNamed(t, "prefixed", []string{"--handler_prefix=" + handlerPrefix}, cfg)

	res := h.get(t, h.baseURL+handlerPrefix+"/"+logPrefix+"/ct/v1/get-sth")
	t.Equal("prefixed-get-sth-status", res.status, http.StatusOK)

	res = h.get(t, h.baseURL+handlerPrefix+"/"+logPrefix+"/ct/v1/get-roots")
	t.Equal("prefixed-get-roots-status", res.status, http.StatusOK)

	// The handler behind the moved path is the real one, not a stub that
	// answers 200 to everything: it issues an SCT for a valid chain.
	body := chainJSON(t,
		[]string{"minted", "leaf-valid.der"},
		[]string{"minted", "bench-intermediate.der"},
		[]string{"minted", "bench-root.der"})
	res = h.postJSON(t, h.baseURL+handlerPrefix+"/"+logPrefix+"/ct/v1/add-chain", body)
	var sct ct.AddChainResponse
	okSCT := res.status == http.StatusOK && json.Unmarshal(res.body, &sct) == nil &&
		sct.SCTVersion == 0 && len(sct.ID) == 32 && len(sct.Signature) > 0
	t.Check("prefixed-add-chain-issues-sct", okSCT,
		"add-chain under the prefix answered %d: %s", res.status, lastBytes(res.body, 200))

	res = h.get(t, h.baseURL+"/"+logPrefix+"/ct/v1/get-sth")
	t.Equal("unprefixed-get-sth-is-404", res.status, http.StatusNotFound)

	res = h.get(t, h.baseURL+"/")
	t.Equal("root-path-is-not-prefixed", res.status, http.StatusOK)

	res = h.get(t, h.baseURL+handlerPrefix+"/")
	t.Equal("root-path-under-prefix-is-404", res.status, http.StatusNotFound)

	res = h.get(t, h.metricsURL+"/metrics")
	t.Equal("metrics-path-is-not-prefixed", res.status, http.StatusOK)

	res = h.get(t, h.metricsURL+handlerPrefix+"/metrics")
	t.Equal("metrics-path-under-prefix-is-404", res.status, http.StatusNotFound)

	h.stop()

	// ---------------------------------------------------------------------
	// Control: the same configuration with no --handler_prefix at all.
	// ---------------------------------------------------------------------
	h2 := startPersonalityNamed(t, "bare", nil, cfg)
	defer h2.stop()

	res = h2.get(t, h2.baseURL+"/"+logPrefix+"/ct/v1/get-sth")
	t.Equal("control-no-prefix-bare-get-sth-status", res.status, http.StatusOK)

	res = h2.get(t, h2.baseURL+handlerPrefix+"/"+logPrefix+"/ct/v1/get-sth")
	t.Equal("control-no-prefix-prefixed-get-sth-is-404", res.status, http.StatusNotFound)

	t.Done()
}

// Group ctserver-override-handler-prefix.
//
// v1.0.21 adds the per-log config field override_handler_prefix (field 13).
// It replaces --handler_prefix for the log that carries it, and is normalised
// by trimming slashes from both ends and prepending exactly one "/", so
// "custom", "/custom" and "/custom/" all name the same URL space
// (CONFIG.md section 2.1). The field exists for migrations, where one log
// keeps a legacy path while every other log moves under the global prefix.
//
// Observed over a real socket against the candidate's own ct_server, started
// once with --handler_prefix=/logs and a four-log configuration: three logs
// spell the same override three ways and the fourth has none. That single
// process answers the question both ways round -- the overridden logs must be
// absent from the global prefix and the plain log must be absent from the
// override -- so no 200 here can come from a catch-all and no 404 from a log
// that was never configured.
//
// SCOPE-NET: loopback only, ports from binding :0. SCOPE-BACKEND: the log
// behind the personality is this group's own pinned stub.
//
// v1.0.19 has neither the config field nor --handler_prefix: its
// configpb.LogConfig has no field 13, so the text proto below is rejected
// before a single case can run.
package main

import (
	"crypto/sha256"
	"encoding/json"
	"net/http"
	"strconv"

	ct "github.com/google/certificate-transparency-go"

	"ctverify/vhelp"
)

const (
	globalPrefix   = "/logs"
	overridePrefix = "/custom"
)

// The four logs: three spellings of one override, then one log with none.
var logs = []struct {
	id       int64
	prefix   string
	override string
}{
	{1101, "alpha", "custom"},
	{1102, "bravo", "/custom"},
	{1103, "charlie", "/custom/"},
	{1104, "delta", ""},
}

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

	cfg := ""
	for _, l := range logs {
		extra := ""
		if l.override != "" {
			extra = "  override_handler_prefix: \"" + l.override + "\"\n"
		}
		cfg += logConfigText(t, l.id, l.prefix, false, extra)
	}

	h := startPersonalityNamed(t, "override", []string{"--handler_prefix=" + globalPrefix}, cfg)
	defer h.stop()

	// The STH the stub serves. Every overridden log must produce exactly it.
	wantSTH := func(id string, res httpResult) {
		var sth ct.GetSTHResponse
		ok := res.status == http.StatusOK && json.Unmarshal(res.body, &sth) == nil &&
			sth.TreeSize == uint64(pinnedTreeSize) &&
			string(sth.SHA256RootHash) == string(pinnedRootHash[:])
		t.Check(id, ok, "get-sth answered %d: %s", res.status, lastBytes(res.body, 200))
	}

	// ---- the three spellings all land on /custom ------------------------
	wantSTH("override-without-slashes-normalised",
		h.get(t, h.baseURL+overridePrefix+"/alpha"+ct.GetSTHPath))
	wantSTH("override-with-leading-slash-normalised",
		h.get(t, h.baseURL+overridePrefix+"/bravo"+ct.GetSTHPath))
	wantSTH("override-with-both-slashes-normalised",
		h.get(t, h.baseURL+overridePrefix+"/charlie"+ct.GetSTHPath))

	// ---- the override replaces the global prefix, it does not add to it --
	res := h.get(t, h.baseURL+globalPrefix+"/alpha"+ct.GetSTHPath)
	t.Equal("overridden-log-absent-from-global-prefix", res.status, http.StatusNotFound)

	res = h.get(t, h.baseURL+"/alpha"+ct.GetSTHPath)
	t.Equal("overridden-log-absent-from-bare-path", res.status, http.StatusNotFound)

	// ---- a log with no override keeps the global prefix -----------------
	wantSTH("log-without-override-keeps-global-prefix",
		h.get(t, h.baseURL+globalPrefix+"/delta"+ct.GetSTHPath))

	res = h.get(t, h.baseURL+overridePrefix+"/delta"+ct.GetSTHPath)
	t.Equal("log-without-override-absent-from-override", res.status, http.StatusNotFound)

	// ---- the moved handlers are the real ones ---------------------------
	body := chainJSON(t,
		[]string{"minted", "leaf-valid.der"},
		[]string{"minted", "bench-intermediate.der"},
		[]string{"minted", "bench-root.der"})
	res = h.postJSON(t, h.baseURL+overridePrefix+"/alpha"+ct.AddChainPath, body)
	var sct ct.AddChainResponse
	_, pubDER := pinnedKeyDER(t)
	wantID := sha256.Sum256(pubDER)
	okSCT := res.status == http.StatusOK && json.Unmarshal(res.body, &sct) == nil &&
		sct.SCTVersion == 0 && string(sct.ID) == string(wantID[:]) && len(sct.Signature) > 0
	t.Check("override-add-chain-issues-sct", okSCT,
		"add-chain under the override answered %d: %s", res.status, lastBytes(res.body, 200))

	res = h.get(t, h.baseURL+overridePrefix+"/bravo"+ct.GetRootsPath)
	var roots ct.GetRootsResponse
	okRoots := res.status == http.StatusOK && json.Unmarshal(res.body, &roots) == nil &&
		len(roots.Certificates) == 2
	t.Check("override-get-roots-serves-the-pool", okRoots,
		"get-roots under the override answered %d: %s", res.status, lastBytes(res.body, 200))

	// ---- the whole endpoint set moved, not just get-sth -----------------
	// Each endpoint is asked with no parameters at all, so the status is the
	// wrapper's own: 200 where no parameter is needed, 400 where one is, 405
	// for a GET of a POST endpoint. A 404 would mean the path is unregistered.
	want := []struct {
		path   string
		status int
	}{
		{ct.AddChainPath, http.StatusMethodNotAllowed},
		{ct.AddPreChainPath, http.StatusMethodNotAllowed},
		{ct.GetSTHPath, http.StatusOK},
		{ct.GetSTHConsistencyPath, http.StatusBadRequest},
		{ct.GetProofByHashPath, http.StatusBadRequest},
		{ct.GetEntriesPath, http.StatusBadRequest},
		{ct.GetRootsPath, http.StatusOK},
		{ct.GetEntryAndProofPath, http.StatusBadRequest},
	}
	bad := ""
	for _, w := range want {
		r := h.get(t, h.baseURL+overridePrefix+"/alpha"+w.path)
		if r.status != w.status {
			bad += w.path + " gave " + strconv.Itoa(r.status) + " want " + strconv.Itoa(w.status) + "; "
		}
	}
	t.Check("override-moves-every-endpoint", bad == "", "%s", bad)

	t.Done()
}

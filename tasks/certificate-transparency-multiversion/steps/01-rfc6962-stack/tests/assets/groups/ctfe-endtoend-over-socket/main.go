// Group ctfe-endtoend-over-socket.
//
// This step's headline new capability -- the CTFE personality -- walked
// through the candidate's own ct_server binary over a real loopback HTTP
// socket, with a pinned in-process TrillianLogServer (this step's Trillian
// pin) behind it on a real loopback gRPC socket. Both ports come from binding
// :0.
//
// This is a SCORED group, not the gate. Step 1 inherits no milestone, so its
// gate is proof of life only (`build-clean` and `gate-endtoend`); everything
// asserted below -- the published root pool, SCT issuance and its signature,
// admission control, the read endpoints, the backend error map, the envelope
// and the shutdown contract -- is capability this step introduces and is
// therefore priced rather than used as a veto.
//
// The server coming up is a prerequisite here, not a case: it is the gate's
// one job, and counting it again would price the gate.
//
// Nothing here reads a clock: the backend stub rewrites the timestamp inside
// the leaf it returns to a pinned value, and v1.0's addChainInternal builds the
// SCT from the *returned* leaf, so the issued SCT and its signature are fully
// determined. The STH's tree size, root hash and timestamp are pinned in the
// stub the same way.
package main

import (
	"bytes"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"encoding/pem"
	"fmt"
	"io/ioutil"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"syscall"
	"time"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/trillian"
	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"

	"ctverify/vhelp"
)

const (
	pinnedLeafTimestamp      = uint64(1348589665525)
	pinnedSTHTimestampMillis = uint64(1348589667204)
	pinnedTreeSize           = int64(42)
	pinnedLeafIndex          = int64(5)

	rootHashHex = "18041bd4665083001fba8c5411d2d748e8abbfdcdfd9218cb02b68a78e7d4c23"
	proofHash0  = "0000000000000000000000000000000000000000000000000000000000000011"
	proofHash1  = "00000000000000000000000000000000000000000000000000000000000000f2"
)

var storedLeafDER []byte

func pinnedRootHash() []byte { return mustHex(rootHashHex) }
func pinnedProofHashes() [][]byte {
	return [][]byte{mustHex(proofHash0), mustHex(proofHash1)}
}
func pinnedStoredLeaf() []byte      { return x509LeafInput(pinnedLeafTimestamp, storedLeafDER) }
func pinnedStoredExtraData() []byte { return certificateChain(storedLeafDER) }

func mustHex(s string) []byte {
	b, err := hex.DecodeString(s)
	if err != nil {
		panic(err)
	}
	return b
}

type harness struct {
	t      *vhelp.T
	cmd    *exec.Cmd
	stdout *bytes.Buffer
	stderr *bytes.Buffer
	base   string
}

// die tears the server down before reporting a fatal setup failure, so the
// driver never leaves a subprocess behind.
func (h *harness) die(format string, args ...interface{}) {
	h.stop()
	h.t.Fatalf(format, args...)
}

func (h *harness) stop() {
	if h.cmd != nil && h.cmd.Process != nil {
		h.cmd.Process.Signal(syscall.SIGKILL)
		h.cmd.Wait()
		h.cmd = nil
	}
}

func main() {
	t := vhelp.New()
	h := &harness{t: t}
	// Done calls os.Exit, which does not run deferred functions, so a
	// `defer h.stop()` in a driver never fires and the child survives the
	// group. Register the teardown with the harness instead.
	t.Cleanup(h.stop)

	storedLeafDER = mustHex(katValue(t, "sigTestDERCertString"))

	scratch, err := ioutil.TempDir("", "gate")
	if err != nil {
		t.Fatalf("TempDir: %v", err)
	}

	// ---- the backend --------------------------------------------------------
	stub := &stubLog{}
	grpcLn, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen for backend: %v", err)
	}
	gs := grpc.NewServer()
	trillian.RegisterTrillianLogServer(gs, stub)
	go gs.Serve(grpcLn)
	defer gs.Stop()

	// ---- the configuration ---------------------------------------------------
	tmpl, err := ioutil.ReadFile(vhelp.GroupFile("logconfig.textproto.in"))
	if err != nil {
		t.Fatalf("group logconfig.textproto.in: %v", err)
	}
	cfgPath := filepath.Join(scratch, "logs.textproto")
	if err := ioutil.WriteFile(cfgPath, []byte(strings.Replace(string(tmpl), "@FIXTURES@", vhelp.Fixture(), -1)), 0644); err != nil {
		t.Fatalf("write config: %v", err)
	}

	// ---- the candidate's server ----------------------------------------------
	httpAddr := lowBandLoopbackAddr(t)

	h.base = "http://" + httpAddr
	h.stdout = &bytes.Buffer{}
	h.stderr = &bytes.Buffer{}
	h.cmd = vhelp.Command(vhelp.CandidateBin("ct_server"),
		"--log_config="+cfgPath,
		"--log_rpc_server="+grpcLn.Addr().String(),
		"--http_endpoint="+httpAddr,
		"--metrics_endpoint=",
		"--get_sth_interval=0",
		"--rpc_deadline=10s",
		"--log_dir="+scratch,
	)
	h.cmd.Stdout = h.stdout
	h.cmd.Stderr = h.stderr
	if err := h.cmd.Start(); err != nil {
		t.Fatalf("start ct_server: %v", err)
	}

	up := false
	for i := 0; i < 600; i++ {
		rsp, err := http.Get(h.base + "/bench/ct/v1/get-roots")
		if err == nil {
			rsp.Body.Close()
			up = true
			break
		}
		if h.cmd.ProcessState != nil {
			break
		}
		time.Sleep(50 * time.Millisecond)
	}
	if !up {
		h.die("the server never answered on %s; stderr=%s", h.base, tail(h.stderr.String()))
	}

	// ---- get-roots -----------------------------------------------------------
	benchRoot := base64.StdEncoding.EncodeToString(vhelp.ReadFixture("minted", "bench-root.der"))
	benchInter := base64.StdEncoding.EncodeToString(vhelp.ReadFixture("minted", "bench-intermediate.der"))

	st, body := h.get("/bench/ct/v1/get-roots")
	t.Check("get-roots-serves-the-configured-pool",
		st == 200 && jsonBody(body) == `{"certificates":["`+benchRoot+`","`+benchInter+`"]}`,
		"status=%d body=%s", st, tail(body))

	st, body = h.get("/strict/ct/v1/get-roots")
	t.Check("second-log-has-its-own-independent-pool",
		st == 200 && jsonBody(body) == `{"certificates":["`+benchRoot+`"]}`,
		"status=%d body=%s", st, tail(body))

	// ---- get-sth --------------------------------------------------------------
	verifier := logVerifier(t)
	st, body = h.get("/bench/ct/v1/get-sth")
	var sthRsp ct.GetSTHResponse
	if st != 200 || json.Unmarshal([]byte(body), &sthRsp) != nil {
		h.die("get-sth: status=%d body=%s", st, tail(body))
	}
	t.Check("get-sth-reports-the-backend-tree-head",
		sthRsp.TreeSize == uint64(pinnedTreeSize) &&
			sthRsp.Timestamp == pinnedSTHTimestampMillis &&
			bytes.Equal(sthRsp.SHA256RootHash, pinnedRootHash()),
		"size=%d ts=%d root=%x", sthRsp.TreeSize, sthRsp.Timestamp, sthRsp.SHA256RootHash)

	sth := ct.SignedTreeHead{Version: ct.V1, TreeSize: sthRsp.TreeSize, Timestamp: sthRsp.Timestamp}
	copy(sth.SHA256RootHash[:], sthRsp.SHA256RootHash)
	if _, err := tls.Unmarshal(sthRsp.TreeHeadSignature, &sth.TreeHeadSignature); err != nil {
		h.die("tree_head_signature did not decode as DigitallySigned: %v", err)
	}
	t.Case("get-sth-signature-verifies-under-the-configured-log-key",
		verifier.VerifySTHSignature(sth))

	tampered := sth
	tampered.TreeSize = sth.TreeSize + 1
	t.Case("get-sth-signature-does-not-cover-a-different-tree-size",
		wantErr(verifier.VerifySTHSignature(tampered), "the STH signature verified at a different tree size"))

	// ---- add-chain ------------------------------------------------------------
	validChain := chainBody(t, "chain-valid.pem")
	st, body = h.post("/bench/ct/v1/add-chain", validChain)
	var sctRsp ct.AddChainResponse
	if st != 200 || json.Unmarshal([]byte(body), &sctRsp) != nil {
		h.die("add-chain: status=%d body=%s", st, tail(body))
	}
	t.Check("add-chain-issues-an-sct-200", st == 200 && sctRsp.SCTVersion == ct.V1,
		"status=%d version=%d", st, sctRsp.SCTVersion)

	logID := logKeyID(t)
	t.Check("add-chain-sct-log-id-is-sha256-of-the-log-spki",
		bytes.Equal(sctRsp.ID, logID), "id=%x want=%x", sctRsp.ID, logID)

	t.Check("add-chain-sct-timestamp-comes-from-the-leaf-the-backend-returned",
		sctRsp.Timestamp == pinnedLeafTimestamp,
		"timestamp=%d want=%d", sctRsp.Timestamp, pinnedLeafTimestamp)

	sent, returned, calls := stub.snapshot()
	if calls != 1 || len(sent) != 1 || len(returned) != 1 {
		h.die("backend saw %d QueueLeaves calls with %d leaves", calls, len(sent))
	}
	leafDER := derOf(t, "minted", "leaf-valid.der")
	t.Check("add-chain-queues-a-v1-x509-leaf-over-the-submitted-certificate",
		bytes.Equal(sent[0], x509LeafInputAnyTimestamp(sent[0], leafDER)),
		"leaf the backend received did not carry leaf-valid.der as an x509_entry (len=%d)", len(sent[0]))

	var issuedLeaf ct.MerkleTreeLeaf
	if _, err := tls.Unmarshal(returned[0], &issuedLeaf); err != nil {
		h.die("the leaf handed back to the personality did not decode: %v", err)
	}
	sct := ct.SignedCertificateTimestamp{
		SCTVersion: sctRsp.SCTVersion,
		Timestamp:  sctRsp.Timestamp,
		Extensions: ct.CTExtensions(sctRsp.Extensions),
	}
	copy(sct.LogID.KeyID[:], sctRsp.ID)
	if _, err := tls.Unmarshal(sctRsp.Signature, &sct.Signature); err != nil {
		h.die("SCT signature did not decode as DigitallySigned: %v", err)
	}
	t.Case("add-chain-sct-signature-covers-the-returned-leaf",
		verifier.VerifySCTSignature(sct, ct.LogEntry{Leaf: issuedLeaf}))

	otherLeaf := issuedLeaf
	otherTE := *issuedLeaf.TimestampedEntry
	otherTE.X509Entry = &ct.ASN1Cert{Data: derOf(t, "minted", "bench-intermediate.der")}
	otherLeaf.TimestampedEntry = &otherTE
	t.Case("add-chain-sct-signature-does-not-cover-a-different-certificate",
		wantErr(verifier.VerifySCTSignature(sct, ct.LogEntry{Leaf: otherLeaf}),
			"the SCT signature verified over a different certificate"))

	// ---- admission -------------------------------------------------------------
	st, body = h.post("/bench/ct/v1/add-chain", chainBody(t, "chain-unrooted.pem"))
	t.Check("add-chain-refuses-a-chain-with-no-path-to-a-configured-root",
		st == 400 && strings.HasSuffix(body, "because: x509: certificate signed by unknown authority\n"),
		"status=%d body=%s", st, tail(body))

	st, body = h.post("/bench/ct/v1/add-chain", chainBody(t, "chain-unrooted-with-root.pem"))
	t.Check("add-chain-refuses-a-complete-chain-to-an-unconfigured-root",
		st == 400, "status=%d body=%s", st, tail(body))

	// v1.0: reject_expired defaults false, so an expired chain is admitted.
	st, body = h.post("/bench/ct/v1/add-chain", chainBody(t, "chain-expired.pem"))
	t.Check("add-chain-admits-an-expired-chain-when-reject-expired-is-unset",
		st == 200, "status=%d body=%s", st, tail(body))

	// ...and the second log, which sets it, refuses the very same chain.
	st, body = h.post("/strict/ct/v1/add-chain", chainBody(t, "chain-expired.pem"))
	t.Check("add-chain-refuses-an-expired-chain-when-reject-expired-is-set",
		st == 400, "status=%d body=%s", st, tail(body))

	// A precertificate belongs on add-pre-chain, and only there.
	preChain := chainBody(t, "chain-precert.pem")
	st, body = h.post("/bench/ct/v1/add-chain", preChain)
	t.Check("add-chain-refuses-a-precertificate", st == 400, "status=%d body=%s", st, tail(body))

	st, body = h.post("/bench/ct/v1/add-pre-chain", preChain)
	var preRsp ct.AddChainResponse
	if st != 200 || json.Unmarshal([]byte(body), &preRsp) != nil {
		h.die("add-pre-chain: status=%d body=%s", st, tail(body))
	}
	sentAll, _, _ := stub.snapshot()
	preLeaf := sentAll[len(sentAll)-1]
	var parsedPre ct.MerkleTreeLeaf
	if _, err := tls.Unmarshal(preLeaf, &parsedPre); err != nil {
		h.die("precert leaf did not decode: %v", err)
	}
	t.Check("add-pre-chain-queues-a-precert-entry-with-the-issuer-key-hash",
		parsedPre.TimestampedEntry.EntryType == ct.PrecertLogEntryType &&
			parsedPre.TimestampedEntry.PrecertEntry != nil &&
			bytes.Equal(parsedPre.TimestampedEntry.PrecertEntry.IssuerKeyHash[:], issuerKeyHash(t)) &&
			preRsp.Timestamp == pinnedLeafTimestamp,
		"entry=%v ts=%d", parsedPre.TimestampedEntry.EntryType, preRsp.Timestamp)

	// ---- read endpoints backed by the stub --------------------------------------
	st, body = h.get("/bench/ct/v1/get-entries?start=5&end=5")
	wantEntries := `{"entries":[{"leaf_input":"` + base64.StdEncoding.EncodeToString(pinnedStoredLeaf()) +
		`","extra_data":"` + base64.StdEncoding.EncodeToString(pinnedStoredExtraData()) + `"}]}`
	t.Check("get-entries-returns-the-backend-leaf-verbatim", st == 200 && jsonBody(body) == wantEntries,
		"status=%d body=%s", st, tail(body))

	// A range wider than the maximum is coerced, not refused: end becomes
	// start+max-1 with max = MaxGetEntriesAllowed = 1000 (HTTP-API.md s4.3 rule 2).
	// The stub echoes one leaf per index it is asked for, so the count of entries
	// in the answer is the count of indices the handler asked the backend for.
	st, body = h.get("/bench/ct/v1/get-entries?start=5&end=5000")
	var wide struct {
		Entries []json.RawMessage `json:"entries"`
	}
	wideErr := json.Unmarshal([]byte(body), &wide)
	t.Check("get-entries-coerces-an-over-wide-range-to-a-thousand-entries",
		st == 200 && wideErr == nil && len(wide.Entries) == 1000,
		"status=%d entries=%d err=%v body=%s", st, len(wide.Entries), wideErr, tail(body))

	st, body = h.get("/bench/ct/v1/get-proof-by-hash?hash=" + urlB64(pinnedRootHash()) + "&tree_size=42")
	wantProof := `{"leaf_index":5,"audit_path":["` +
		base64.StdEncoding.EncodeToString(mustHex(proofHash0)) + `","` +
		base64.StdEncoding.EncodeToString(mustHex(proofHash1)) + `"]}`
	t.Check("get-proof-by-hash-returns-the-backend-proof", st == 200 && jsonBody(body) == wantProof,
		"status=%d body=%s", st, tail(body))

	st, body = h.get("/bench/ct/v1/get-sth-consistency?first=7&second=42")
	wantCons := `{"consistency":["` +
		base64.StdEncoding.EncodeToString(mustHex(proofHash0)) + `","` +
		base64.StdEncoding.EncodeToString(mustHex(proofHash1)) + `"]}`
	t.Check("get-sth-consistency-returns-the-backend-proof", st == 200 && jsonBody(body) == wantCons,
		"status=%d body=%s", st, tail(body))

	st, body = h.get("/bench/ct/v1/get-entry-and-proof?leaf_index=5&tree_size=42")
	wantEAP := `{"leaf_input":"` + base64.StdEncoding.EncodeToString(pinnedStoredLeaf()) +
		`","extra_data":"` + base64.StdEncoding.EncodeToString(pinnedStoredExtraData()) +
		`","audit_path":["` + base64.StdEncoding.EncodeToString(mustHex(proofHash0)) + `","` +
		base64.StdEncoding.EncodeToString(mustHex(proofHash1)) + `"]}`
	t.Check("get-entry-and-proof-returns-the-backend-entry", st == 200 && jsonBody(body) == wantEAP,
		"status=%d body=%s", st, tail(body))

	// ---- the backend-error map of HTTP-API.md section 6.1 -------------------------
	// Every case above ran against a stub that never errors. From here the stub
	// answers get-proof-by-hash with a chosen gRPC status and nothing else
	// changes, so the status the front end returns is the map's output and only
	// the map's output. The body is the CTFE's own text wrapping the backend's,
	// which no page fixes; only the status is graded (SCOPE-DIAGNOSTIC).
	proofPath := "/bench/ct/v1/get-proof-by-hash?hash=" + urlB64(pinnedRootHash()) + "&tree_size=42"

	stub.failProofsWith(codes.NotFound)
	st, body = h.get(proofPath)
	t.Check("backend-not-found-becomes-404", st == 404, "status=%d body=%s", st, tail(body))

	// The cell that moves at v1.2.0: quota exhaustion is 403 here, not 429.
	stub.failProofsWith(codes.ResourceExhausted)
	st, body = h.get(proofPath)
	t.Check("backend-resource-exhausted-becomes-403", st == 403, "status=%d body=%s", st, tail(body))

	// The other cell that moves at v1.2.0: a backend deadline is 408 here, not 504.
	stub.failProofsWith(codes.DeadlineExceeded)
	st, body = h.get(proofPath)
	t.Check("backend-deadline-exceeded-becomes-408", st == 408, "status=%d body=%s", st, tail(body))

	// Anything without a gRPC status, and anything outside the table, is 500.
	stub.failProofsWith(codes.Unknown)
	st, body = h.get(proofPath)
	t.Check("backend-unknown-code-becomes-500", st == 500, "status=%d body=%s", st, tail(body))

	stub.failProofsWith(codes.OK)

	// ---- envelope and metrics ----------------------------------------------------
	st, body = h.get("/bench/ct/v1/add-chain")
	t.Check("wrong-method-is-405-over-the-socket",
		st == 405 && body == "Method Not Allowed\nmethod not allowed: GET\n",
		"status=%d body=%s", st, tail(body))

	st, body = h.get("/bench/ct/v1/no-such-entrypoint")
	// At v1.0 nothing registers a catch-all, so an unmapped path falls through
	// to net/http's own ServeMux. The status is the product's observable; the
	// body is the standard library's and is not graded (SCOPE-DIAGNOSTIC).
	t.Check("unmapped-path-is-404-over-the-socket",
		st == 404 && body != "", "status=%d body=%s", st, tail(body))

	st, body = h.get("/metrics")
	// --metrics_endpoint is empty, so /metrics must answer on the HTTP endpoint
	// (HTTP-API.md section 7). What is counted there is metrics *content* and
	// TEST_INTERFACE.md section 9 does not score it.
	t.Check("metrics-are-served-on-the-http-endpoint-when-none-is-configured",
		st == 200, "status=%d body=%s", st, tail(body))

	// ---- shutdown ------------------------------------------------------------------
	if err := h.cmd.Process.Signal(syscall.SIGTERM); err != nil {
		h.die("SIGTERM: %v", err)
	}
	waitErr := h.cmd.Wait()
	code := -1
	if ee, ok := waitErr.(*exec.ExitError); ok {
		code = ee.ExitCode()
	} else if waitErr == nil {
		code = 0
	}
	h.cmd = nil
	t.Check("sigterm-terminates-the-server-with-exit-status-1", code == 1, "exit=%d err=%v", code, waitErr)

	_, err = http.Get(h.base + "/bench/ct/v1/get-roots")
	t.Case("listener-is-closed-after-shutdown",
		wantErr(err, "the HTTP endpoint still answered after the server exited"))

	// ---- the other half of the metrics rule ----------------------------------
	//
	// Everything above ran with --metrics_endpoint= (empty), which is the case
	// HTTP-API.md section 7 calls "served on the HTTP endpoint's mux". The rule
	// has a second half: a non-empty --metrics_endpoint that differs from
	// --http_endpoint gets a listener of its own, and /metrics then lives there
	// and NOT on the HTTP endpoint. That needs a second process, because the
	// arrangement is fixed at start-up, so the first server is fully down (the
	// case above proves it) before this one comes up.
	//
	// Two distinct ports are required for the rule to mean anything, and
	// lowBandLoopbackAddr closes its probe listener before returning, so two
	// calls can in principle collide; retry until they differ rather than
	// weakening the audited helper.
	httpAddr2 := lowBandLoopbackAddr(t)
	metricsAddr := ""
	for i := 0; i < 16; i++ {
		if a := lowBandLoopbackAddr(t); a != httpAddr2 {
			metricsAddr = a
			break
		}
	}
	if metricsAddr == "" {
		t.Fatalf("could not obtain two distinct low-band loopback ports")
	}

	h.base = "http://" + httpAddr2
	h.stdout = &bytes.Buffer{}
	h.stderr = &bytes.Buffer{}
	h.cmd = vhelp.Command(vhelp.CandidateBin("ct_server"),
		"--log_config="+cfgPath,
		"--log_rpc_server="+grpcLn.Addr().String(),
		"--http_endpoint="+httpAddr2,
		"--metrics_endpoint="+metricsAddr,
		"--get_sth_interval=0",
		"--rpc_deadline=10s",
		"--log_dir="+scratch,
	)
	h.cmd.Stdout = h.stdout
	h.cmd.Stderr = h.stderr
	if err := h.cmd.Start(); err != nil {
		t.Fatalf("restart ct_server with a separate metrics endpoint: %v", err)
	}

	up = false
	for i := 0; i < 600; i++ {
		rsp, err := http.Get(h.base + "/bench/ct/v1/get-roots")
		if err == nil {
			rsp.Body.Close()
			up = true
			break
		}
		if h.cmd.ProcessState != nil {
			break
		}
		time.Sleep(50 * time.Millisecond)
	}
	if !up {
		h.die("the server never answered on %s with --metrics_endpoint=%s; stderr=%s",
			h.base, metricsAddr, tail(h.stderr.String()))
	}

	// The metrics listener is brought up in its own goroutine before the HTTP
	// endpoint binds, so "the HTTP endpoint answers" does not imply the metrics
	// listener has bound. Poll it -- but a timeout here is NOT a setup failure:
	// a candidate that registered /metrics on the HTTP mux regardless of the
	// flag should lose these two cases, not take the whole group down with it.
	mst, mbody := 0, ""
	for i := 0; i < 200; i++ {
		rsp, err := http.Get("http://" + metricsAddr + "/metrics")
		if err == nil {
			b, _ := ioutil.ReadAll(rsp.Body)
			rsp.Body.Close()
			mst, mbody = rsp.StatusCode, string(b)
			break
		}
		if h.cmd.ProcessState != nil {
			break
		}
		time.Sleep(50 * time.Millisecond)
	}
	t.Check("metrics-are-served-on-their-own-listener-when-one-is-configured",
		mst == 200, "status=%d body=%s", mst, tail(mbody))

	// What is counted on that page is metrics *content*; TEST_INTERFACE.md
	// section 9 does not grade it. Only where the page is -- and where it is
	// not -- is graded here. At v1.0 an unmapped path on the HTTP endpoint
	// falls through to net/http's own ServeMux, so the body is the standard
	// library's and is not compared (SCOPE-DIAGNOSTIC).
	st, body = h.get("/metrics")
	t.Check("the-http-endpoint-does-not-serve-metrics-when-a-separate-listener-is-configured",
		st == 404, "status=%d body=%s", st, tail(body))

	h.stop()

	os.RemoveAll(scratch)
	t.Done()
}

// ---- HTTP helpers ------------------------------------------------------------

func (h *harness) get(path string) (int, string) {
	rsp, err := http.Get(h.base + path)
	if err != nil {
		h.die("GET %s: %v", path, err)
	}
	defer rsp.Body.Close()
	b, _ := ioutil.ReadAll(rsp.Body)
	return rsp.StatusCode, string(b)
}

func (h *harness) post(path, body string) (int, string) {
	rsp, err := http.Post(h.base+path, "application/json", strings.NewReader(body))
	if err != nil {
		h.die("POST %s: %v", path, err)
	}
	defer rsp.Body.Close()
	b, _ := ioutil.ReadAll(rsp.Body)
	return rsp.StatusCode, string(b)
}

// ---- fixture helpers ----------------------------------------------------------

func katValue(t *vhelp.T, key string) string {
	for _, line := range strings.Split(string(vhelp.ReadFixture("rfc6962", "kat.txt")), "\n") {
		f := strings.Fields(line)
		if len(f) == 2 && f[0] == key {
			return f[1]
		}
	}
	t.Fatalf("rfc6962/kat.txt has no %s", key)
	return ""
}

func derOf(t *vhelp.T, rel ...string) []byte {
	raw := vhelp.ReadFixture(rel...)
	if blk, _ := pem.Decode(raw); blk != nil {
		return blk.Bytes
	}
	return raw
}

func chainBody(t *vhelp.T, name string) string {
	rest := vhelp.ReadFixture("minted", name)
	var buf bytes.Buffer
	buf.WriteString(`{"chain":[`)
	n := 0
	for {
		var blk *pem.Block
		blk, rest = pem.Decode(rest)
		if blk == nil {
			break
		}
		if n > 0 {
			buf.WriteByte(',')
		}
		fmt.Fprintf(&buf, "%q", base64.StdEncoding.EncodeToString(blk.Bytes))
		n++
	}
	buf.WriteString("]}")
	if n == 0 {
		t.Fatalf("minted/%s held no PEM blocks", name)
	}
	return buf.String()
}

func logVerifier(t *vhelp.T) *ct.SignatureVerifier {
	pk, _, rest, err := ct.PublicKeyFromPEM(vhelp.ReadFixture("keys", "ct-http-server.pubkey.pem"))
	if err != nil || len(rest) > 0 {
		t.Fatalf("keys/ct-http-server.pubkey.pem: %v", err)
	}
	v, err := ct.NewSignatureVerifier(pk)
	if err != nil {
		t.Fatalf("NewSignatureVerifier: %v", err)
	}
	return v
}

func logKeyID(t *vhelp.T) []byte {
	_, id, _, err := ct.PublicKeyFromPEM(vhelp.ReadFixture("keys", "ct-http-server.pubkey.pem"))
	if err != nil {
		t.Fatalf("keys/ct-http-server.pubkey.pem: %v", err)
	}
	return id[:]
}

func issuerKeyHash(t *vhelp.T) []byte {
	der := derOf(t, "minted", "bench-intermediate.der")
	h := sha256Of(spkiOf(t, der))
	return h
}

func wantErr(err error, why string) error {
	if err == nil {
		return fmt.Errorf("%s", why)
	}
	return nil
}

// jsonBody normalises a JSON *success* body before comparison. Whether a
// handler writes its JSON with json.Marshal+Write or with json.NewEncoder
// decides whether one trailing newline is present, and no page of the corpus
// fixes that choice, so it is not graded. Error bodies are a different matter:
// HTTP-API.md section 2.1 states their "<status text>\n<error>\n" shape, so
// those are still compared as written.
func jsonBody(s string) string { return strings.TrimSuffix(s, "\n") }

func tail(s string) string {
	if len(s) > 300 {
		return "..." + s[len(s)-300:]
	}
	return s
}

func urlB64(b []byte) string {
	return strings.NewReplacer("+", "%2B", "/", "%2F", "=", "%3D").Replace(base64.StdEncoding.EncodeToString(b))
}

// ---- hand-built TLS encodings --------------------------------------------------

func x509LeafInput(ts uint64, der []byte) []byte {
	out := []byte{0x00, 0x00}
	for i := 7; i >= 0; i-- {
		out = append(out, byte(ts>>(uint(i)*8)))
	}
	out = append(out, 0x00, 0x00)
	out = append(out, uint24(len(der))...)
	out = append(out, der...)
	out = append(out, 0x00, 0x00)
	return out
}

// x509LeafInputAnyTimestamp rebuilds the expected leaf using whatever timestamp
// the given encoding carries, so the certificate and the framing are graded
// exactly while the personality's own clock is not.
func x509LeafInputAnyTimestamp(got []byte, der []byte) []byte {
	var ts uint64
	if len(got) >= 10 {
		for i := 2; i < 10; i++ {
			ts = ts<<8 | uint64(got[i])
		}
	}
	return x509LeafInput(ts, der)
}

func certificateChain(ders ...[]byte) []byte {
	var body []byte
	for _, d := range ders {
		body = append(body, uint24(len(d))...)
		body = append(body, d...)
	}
	return append(uint24(len(body)), body...)
}

func uint24(n int) []byte { return []byte{byte(n >> 16), byte(n >> 8), byte(n)} }

// lowBandLoopbackAddr returns a loopback host:port taken from BELOW the
// kernel's ephemeral range (32768+ on the harness host), probed free by an
// actual bind. A `:0` probe hands back a port the kernel may immediately
// reassign to any unrelated process on the machine in the window before it is
// used; a low-band port is never handed out by the ephemeral allocator, so
// only an explicit binder of that exact port can take it. The starting slot is
// dispersed by one throwaway `:0` bind so that sibling probes in the same run
// do not converge on the same slot.
func lowBandLoopbackAddr(t *vhelp.T) string {
	seed, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen 127.0.0.1:0 to disperse the probe: %v", err)
	}
	base := seed.Addr().(*net.TCPAddr).Port % 12000
	if err := seed.Close(); err != nil {
		t.Fatalf("close the dispersal listener: %v", err)
	}
	for i := 0; i < 512; i++ {
		l, err := net.ListenTCP("tcp", &net.TCPAddr{
			IP:   net.IPv4(127, 0, 0, 1),
			Port: 20000 + (base+i)%12000,
		})
		if err != nil {
			continue
		}
		a := l.Addr().String()
		if err := l.Close(); err != nil {
			t.Fatalf("close probe listener: %v", err)
		}
		return a
	}
	t.Fatalf("no free loopback port in 20000-31999 after 512 probes")
	return ""
}

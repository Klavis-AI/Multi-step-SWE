// Group ctclient-cli.
//
// The shipped ctclient command, run as a subprocess against a canned CT log on
// a loopback port obtained by binding :0. Everything the log answers is pinned,
// the child's TZ is pinned, and the child's stdout is captured into a buffer,
// never shared with this process. Verdicts come from the child's exit status
// and its stdout, plus the requests the canned log recorded.
package main

import (
	"bytes"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"encoding/pem"
	"fmt"
	"io/ioutil"
	"net/http"
	"net/http/httptest"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"time"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/x509"
	"github.com/google/certificate-transparency-go/x509util"

	"ctverify/vhelp"
)

const (
	sthTimestamp  = uint64(1348589667204)
	sctTimestamp  = uint64(1348589665525)
	leafTimestamp = uint64(1348589665525)
	preTimestamp  = uint64(1348589666000)
	treeSize      = uint64(42)
)

var (
	mu          sync.Mutex
	recorded    []string
	sthStatus   = 200
	sctTSServed = sctTimestamp
)

func main() {
	// Pin the rendering of every pinned timestamp on both sides.
	os.Setenv("TZ", "UTC")

	t := vhelp.New()
	kat := readKAT(t)

	certDER := mustHex(t, kat["sigTestDERCertString"])
	rootHash := mustHex(t, kat["sigTestDefaultRootHash"])
	sthSig := mustHex(t, kat["sigTestDefaultSTHSignature"])
	sctSig := mustHex(t, kat["sigTestCertSCTSignatureEC"])
	logID := mustHex(t, kat["sigTestKeyIDEC"])

	rootA := vhelp.ReadFixture("minted", "bench-root.der")
	rootB := vhelp.ReadFixture("minted", "bench-intermediate.der")

	// Entry 1 is a precert entry built over a real TBSCertificate so that the
	// text renderer has something to parse and the hex renderer something exact.
	leafCert, err := x509.ParseCertificate(vhelp.ReadFixture("minted", "leaf-valid.der"))
	if leafCert == nil {
		t.Fatalf("minted/leaf-valid.der did not parse: %v", err)
	}
	tbs := leafCert.RawTBSCertificate
	var issuerKeyHash [32]byte
	copy(issuerKeyHash[:], mustHex(t, "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"))

	entries := []map[string]string{
		{
			"leaf_input": b64(x509Leaf(leafTimestamp, certDER)),
			"extra_data": b64(certChain(certDER)),
		},
		{
			"leaf_input": b64(precertLeaf(preTimestamp, issuerKeyHash, tbs)),
			"extra_data": b64(precertChain(certDER)),
		},
	}

	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		body, _ := ioutil.ReadAll(r.Body)
		mu.Lock()
		recorded = append(recorded, fmt.Sprintf("%s %s?%s|%s", r.Method, r.URL.Path, r.URL.RawQuery, string(body)))
		status := sthStatus
		ts := sctTSServed
		mu.Unlock()

		w.Header().Set("Content-Type", "application/json")
		switch r.URL.Path {
		case "/ct/v1/get-sth":
			if status != 200 {
				w.WriteHeader(status)
				w.Write([]byte(`{"error":"no"}`))
				return
			}
			json.NewEncoder(w).Encode(map[string]interface{}{
				"tree_size":           treeSize,
				"timestamp":           sthTimestamp,
				"sha256_root_hash":    base64.StdEncoding.EncodeToString(rootHash),
				"tree_head_signature": base64.StdEncoding.EncodeToString(sthSig),
			})
		case "/ct/v1/get-roots":
			json.NewEncoder(w).Encode(map[string]interface{}{
				"certificates": []string{b64(rootA), b64(rootB)},
			})
		case "/ct/v1/get-entries":
			json.NewEncoder(w).Encode(map[string]interface{}{"entries": entries})
		case "/ct/v1/add-chain", "/ct/v1/add-pre-chain":
			json.NewEncoder(w).Encode(map[string]interface{}{
				"sct_version": 0,
				"id":          base64.StdEncoding.EncodeToString(logID),
				"timestamp":   ts,
				"extensions":  "",
				"signature":   base64.StdEncoding.EncodeToString(sctSig),
			})
		default:
			http.NotFound(w, r)
		}
	}))
	defer srv.Close()

	scratch, err := ioutil.TempDir("", "ctclient")
	if err != nil {
		t.Fatalf("TempDir: %v", err)
	}
	defer os.RemoveAll(scratch)
	certPEM := filepath.Join(scratch, "sigtest.pem")
	writeFile(t, certPEM, pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: certDER}))
	junk := filepath.Join(scratch, "junk.txt")
	writeFile(t, junk, []byte("this file holds no certificates\n"))

	goodKey := vhelp.Fixture("rfc6962", "sig-test-ec256-public-key.pem")
	otherKey := vhelp.Fixture("rfc6962", "sig-test-ec256-public-key2.pem")

	// ---- expected renderings, computed here from the pinned inputs ----------
	sthWhen := time.Unix(int64(sthTimestamp/1000), int64(sthTimestamp%1000)*1e6).UTC()
	sctWhen := time.Unix(int64(sctTimestamp/1000), int64(sctTimestamp%1000)*1e6).UTC()
	leafWhen := time.Unix(int64(leafTimestamp/1000), int64(leafTimestamp%1000)*1e6).UTC()
	preWhen := time.Unix(int64(preTimestamp/1000), int64(preTimestamp%1000)*1e6).UTC()

	wantSTH := fmt.Sprintf("%v: Got STH for %v log (size=%d) at %v, hash %x\n%s\n",
		sthWhen, ct.V1, treeSize, srv.URL, rootHash, sigString(t, sthSig))
	wantUpload := fmt.Sprintf("%v: Uploaded chain of %d certs to %v log at %v\n%s\n",
		sctWhen, 1, ct.V1, srv.URL, sigString(t, sctSig))

	pemOf := func(der []byte) string {
		return string(pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der}))
	}
	wantRootsPEM := pemOf(rootA) + pemOf(rootB)
	wantRootsText := certText(t, rootA) + certText(t, rootB)

	wantEntries := fmt.Sprintf("Index=0 Timestamp=%v X.509 certificate:\n%s", leafWhen, pemOf(certDER)) +
		fmt.Sprintf("Index=1 Timestamp=%v pre-certificate from issuer with keyhash %x:\n%x\n",
			preWhen, issuerKeyHash, tbs)

	run := func(args ...string) (int, string, string) {
		cmd := vhelp.Command(vhelp.CandidateBin("ctclient"), args...)
		cmd.Env = append(cmd.Env, "TZ=UTC")
		var so, se bytes.Buffer
		cmd.Stdout = &so
		cmd.Stderr = &se
		err := cmd.Run()
		code := 0
		if ee, ok := err.(*exec.ExitError); ok {
			code = ee.ExitCode()
		} else if err != nil {
			code = -1
		}
		return code, so.String(), se.String()
	}
	uri := func(extra ...string) string {
		if len(extra) == 0 {
			return "-log_uri=" + srv.URL
		}
		return "-log_uri=" + srv.URL + extra[0]
	}

	// ---- sth -----------------------------------------------------------------
	code, out, _ := run(uri(), "sth")
	t.Check("sth-prints-the-pinned-tree-head", code == 0 && out == wantSTH,
		"exit=%d stdout=%q want=%q", code, out, wantSTH)

	code, out, _ = run(uri(), "-pub_key="+goodKey, "sth")
	t.Check("sth-accepts-a-signature-made-by-the-configured-log-key", code == 0 && out == wantSTH,
		"exit=%d stdout=%q", code, out)

	code, out, _ = run(uri(), "-pub_key="+otherKey, "sth")
	t.Check("sth-refuses-a-signature-made-by-another-key", code == 1 && out == "",
		"exit=%d stdout=%q", code, out)

	code, out, _ = run(uri(), "-pub_key="+filepath.Join(scratch, "absent.pem"), "sth")
	t.Check("an-unreadable-public-key-file-is-fatal-before-any-request", code == 1 && out == "",
		"exit=%d stdout=%q", code, out)

	reset()
	wantSTHSlashed := strings.Replace(wantSTH, "at "+srv.URL+",", "at "+srv.URL+"///,", 1)
	code, out, _ = run(uri("///"), "sth")
	t.Check("trailing-slashes-in-log-uri-are-trimmed",
		code == 0 && out == wantSTHSlashed && last() == "GET /ct/v1/get-sth?|",
		"exit=%d stdout=%q request=%q", code, out, last())

	mu.Lock()
	sthStatus = 400
	mu.Unlock()
	code, out, _ = run(uri(), "sth")
	mu.Lock()
	sthStatus = 200
	mu.Unlock()
	t.Check("a-log-http-error-is-fatal-and-prints-nothing", code == 1 && out == "",
		"exit=%d stdout=%q", code, out)

	// ---- getroots --------------------------------------------------------------
	reset()
	code, out, _ = run(uri(), "-text=false", "getroots")
	t.Check("getroots-pem-output-is-the-logs-root-list-in-order",
		code == 0 && out == wantRootsPEM && last() == "GET /ct/v1/get-roots?|",
		"exit=%d stdout=%q request=%q", code, out, last())

	code, out, _ = run(uri(), "-text=false", "get-roots")
	t.Check("getroots-is-also-spelled-get-roots", code == 0 && out == wantRootsPEM,
		"exit=%d stdout=%q", code, out)

	code, out, _ = run(uri(), "-text=false", "get_roots")
	t.Check("getroots-is-also-spelled-get_roots", code == 0 && out == wantRootsPEM,
		"exit=%d stdout=%q", code, out)

	code, out, _ = run(uri(), "getroots")
	t.Check("getroots-defaults-to-the-x509util-text-rendering", code == 0 && out == wantRootsText,
		"exit=%d stdout=%q", code, out)

	// ---- getentries ---------------------------------------------------------------
	reset()
	code, out, _ = run(uri(), "-text=false", "-first=0", "-last=1", "getentries")
	t.Check("getentries-renders-x509-and-precert-entries-with-their-indices",
		code == 0 && out == wantEntries && last() == "GET /ct/v1/get-entries?end=1&start=0|",
		"exit=%d stdout=%q request=%q", code, out, last())

	code, out, _ = run(uri(), "-text=false", "-first=0", "-last=1", "get-entries")
	t.Check("getentries-is-also-spelled-get-entries", code == 0 && out == wantEntries,
		"exit=%d stdout=%q", code, out)

	reset()
	code, out, _ = run(uri(), "-last=1", "getentries")
	t.Check("getentries-without-first-is-fatal-before-any-request",
		code == 1 && out == "" && len(snapshot()) == 0, "exit=%d stdout=%q reqs=%v", code, out, snapshot())

	reset()
	code, out, _ = run(uri(), "-first=0", "getentries")
	t.Check("getentries-without-last-is-fatal-before-any-request",
		code == 1 && out == "" && len(snapshot()) == 0, "exit=%d stdout=%q reqs=%v", code, out, snapshot())

	code, out, _ = run(uri(), "-first=1", "-last=0", "getentries")
	t.Check("getentries-refuses-an-inverted-range", code == 1 && out == "",
		"exit=%d stdout=%q", code, out)

	// ---- upload ---------------------------------------------------------------------
	reset()
	code, out, _ = run(uri(), "-cert_chain="+certPEM, "upload")
	wantBody := `{"chain":["` + b64(certDER) + `"]}`
	t.Check("upload-posts-the-chain-to-add-chain-and-prints-the-sct",
		code == 0 && out == wantUpload && last() == "POST /ct/v1/add-chain?|"+wantBody,
		"exit=%d stdout=%q request=%q", code, out, last())

	code, out, _ = run(uri(), "-pub_key="+goodKey, "-cert_chain="+certPEM, "upload")
	t.Check("upload-accepts-an-sct-that-verifies-over-the-submitted-chain",
		code == 0 && out == wantUpload, "exit=%d stdout=%q", code, out)

	mu.Lock()
	sctTSServed = sctTimestamp + 1
	mu.Unlock()
	code, out, _ = run(uri(), "-pub_key="+goodKey, "-cert_chain="+certPEM, "upload")
	mu.Lock()
	sctTSServed = sctTimestamp
	mu.Unlock()
	t.Check("upload-refuses-an-sct-whose-signature-does-not-cover-what-was-sent",
		code == 1 && out == "", "exit=%d stdout=%q", code, out)

	reset()
	preChainFile := filepath.Join(scratch, "precert-chain.pem")
	writeFile(t, preChainFile, vhelp.ReadFixture("minted", "chain-precert.pem"))
	code, out, _ = run(uri(), "-cert_chain="+preChainFile, "upload")
	t.Check("upload-routes-a-poisoned-precertificate-to-add-pre-chain",
		code == 0 && strings.HasPrefix(out, "Uploading pre-certificate to log\n") &&
			strings.HasPrefix(last(), "POST /ct/v1/add-pre-chain?|"),
		"exit=%d stdout=%q request=%q", code, out, first(last(), 60))

	reset()
	code, out, _ = run(uri(), "upload")
	t.Check("upload-without-a-chain-file-is-fatal-before-any-request",
		code == 1 && out == "" && len(snapshot()) == 0, "exit=%d stdout=%q reqs=%v", code, out, snapshot())

	reset()
	code, out, _ = run(uri(), "-cert_chain="+junk, "upload")
	t.Check("upload-refuses-a-chain-file-holding-no-certificates",
		code == 1 && out == "" && len(snapshot()) == 0, "exit=%d stdout=%q reqs=%v", code, out, snapshot())

	// ---- command grammar ---------------------------------------------------------------
	reset()
	code, out, errOut := run(uri())
	t.Check("no-command-argument-exits-one-with-the-command-list",
		code == 1 && out == "" && len(snapshot()) == 0 &&
			strings.Contains(errOut, "Usage: ctclient [options] <cmd>\n") &&
			strings.Contains(errOut, "   sth         retrieve signed tree head\n") &&
			strings.Contains(errOut, "   upload      upload cert chain and show SCT (needs -cert_chain)\n") &&
			strings.Contains(errOut, "   getroots    show accepted roots\n") &&
			strings.Contains(errOut, "   getentries  get log entries (needs -first and -last)\n"),
		"exit=%d stdout=%q stderr=%q", code, out, errOut)

	reset()
	code, out, _ = run(uri(), "frobnicate")
	t.Check("an-unknown-command-exits-one-without-contacting-the-log",
		code == 1 && out == "" && len(snapshot()) == 0, "exit=%d stdout=%q reqs=%v", code, out, snapshot())

	reset()
	code, out, _ = run(uri(), "sth", "getroots")
	t.Check("more-than-one-command-argument-exits-one",
		code == 1 && out == "" && len(snapshot()) == 0, "exit=%d stdout=%q reqs=%v", code, out, snapshot())

	// The flag package's own convention, which TEST_INTERFACE.md section 4 states
	// for every program the verifier runs: an unrecognised flag is exit 2 with the
	// usage dump on stderr. Only the "Usage of <prog>:" banner is graded -- the
	// flag list below it is the standard library's rendering of flags this group
	// does not fix, and the diagnostic line above it is the standard library's own
	// wording (SCOPE-DIAGNOSTIC).
	reset()
	code, out, errOut = run(uri(), "--no_such_flag", "sth")
	t.Check("an-unrecognised-flag-exits-two-with-the-usage-dump-on-stderr",
		code == 2 && out == "" && len(snapshot()) == 0 &&
			strings.Contains(errOut, "Usage of ") && errOut != "",
		"exit=%d stdout=%q stderr=%q reqs=%v", code, out, errOut, snapshot())

	t.Done()
}

// ---- recorder ---------------------------------------------------------------

func reset() { mu.Lock(); recorded = nil; mu.Unlock() }
func snapshot() []string {
	mu.Lock()
	defer mu.Unlock()
	return append([]string{}, recorded...)
}
func last() string {
	s := snapshot()
	if len(s) == 0 {
		return ""
	}
	return s[len(s)-1]
}
func first(s string, n int) string {
	if len(s) > n {
		return s[:n]
	}
	return s
}

// ---- helpers ------------------------------------------------------------------

func readKAT(t *vhelp.T) map[string]string {
	m := map[string]string{}
	for _, line := range strings.Split(string(vhelp.ReadFixture("rfc6962", "kat.txt")), "\n") {
		if f := strings.Fields(line); len(f) == 2 {
			m[f[0]] = f[1]
		}
	}
	if len(m) == 0 {
		t.Fatalf("rfc6962/kat.txt held no known answers")
	}
	return m
}

func mustHex(t *vhelp.T, s string) []byte {
	b, err := hex.DecodeString(s)
	if err != nil {
		t.Fatalf("hex: %v", err)
	}
	return b
}

func b64(b []byte) string { return base64.StdEncoding.EncodeToString(b) }

func writeFile(t *vhelp.T, path string, b []byte) {
	if err := ioutil.WriteFile(path, b, 0644); err != nil {
		t.Fatalf("write %s: %v", path, err)
	}
}

// sigString renders a DigitallySigned exactly as ctclient's signatureToString does.
func sigString(t *vhelp.T, encoded []byte) string {
	var ds ct.DigitallySigned
	if _, err := tlsUnmarshalDS(encoded, &ds); err != nil {
		t.Fatalf("pinned DigitallySigned did not decode: %v", err)
	}
	return fmt.Sprintf("Signature: Hash=%v Sign=%v Value=%x", ds.Algorithm.Hash, ds.Algorithm.Signature, ds.Signature)
}

func certText(t *vhelp.T, der []byte) string {
	c, err := x509.ParseCertificate(der)
	if c == nil {
		t.Fatalf("fixture root did not parse: %v", err)
	}
	return x509util.CertificateToString(c) + "\n"
}

// ---- hand-built TLS encodings ---------------------------------------------------

func uint24(n int) []byte { return []byte{byte(n >> 16), byte(n >> 8), byte(n)} }

func be64(v uint64) []byte {
	out := make([]byte, 8)
	for i := 0; i < 8; i++ {
		out[i] = byte(v >> (uint(7-i) * 8))
	}
	return out
}

func x509Leaf(ts uint64, der []byte) []byte {
	out := []byte{0x00, 0x00}
	out = append(out, be64(ts)...)
	out = append(out, 0x00, 0x00)
	out = append(out, uint24(len(der))...)
	out = append(out, der...)
	return append(out, 0x00, 0x00)
}

func precertLeaf(ts uint64, issuerKeyHash [32]byte, tbs []byte) []byte {
	out := []byte{0x00, 0x00}
	out = append(out, be64(ts)...)
	out = append(out, 0x00, 0x01)
	out = append(out, issuerKeyHash[:]...)
	out = append(out, uint24(len(tbs))...)
	out = append(out, tbs...)
	return append(out, 0x00, 0x00)
}

func certChain(ders ...[]byte) []byte {
	var body []byte
	for _, d := range ders {
		body = append(body, uint24(len(d))...)
		body = append(body, d...)
	}
	return append(uint24(len(body)), body...)
}

func precertChain(ders ...[]byte) []byte {
	out := append(uint24(len(ders[0])), ders[0]...)
	return append(out, uint24(0)...)
}

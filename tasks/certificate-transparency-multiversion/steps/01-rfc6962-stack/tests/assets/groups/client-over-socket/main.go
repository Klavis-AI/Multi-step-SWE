// Group client-over-socket.
//
// The RFC 6962 client (client/ + jsonclient/) driven against a pinned,
// benchmark-owned HTTP log on a loopback port obtained by binding :0. The log
// is canned, so every byte the client is asked to interpret is fixed: the
// group grades the request the client makes (path, method, query parameters,
// body) and the values it hands back, including the signature checks it
// performs when a public key is configured.
//
// The STH, its signature, the SCT signature and the log key are the pinned
// RFC 6962 known answers from the corpus; no wall clock is consulted.
package main

import (
	"context"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"encoding/pem"
	"fmt"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/client"
	"github.com/google/certificate-transparency-go/jsonclient"
	"github.com/google/certificate-transparency-go/tls"

	"ctverify/vhelp"
)

const (
	sctTimestamp = uint64(1348589665525)
	sthTimestamp = uint64(1348589667204)
	sthTreeSize  = uint64(42)
)

type quiet struct{}

func (quiet) Printf(string, ...interface{}) {}

// recorder remembers what the client asked for.
type recorder struct {
	mu   sync.Mutex
	seen map[string]string // path -> "METHOD query|body"
}

func (r *recorder) note(req *http.Request, body string) {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.seen[req.URL.Path] = req.Method + " " + req.URL.RawQuery + "|" + body
}

func (r *recorder) get(path string) string {
	r.mu.Lock()
	defer r.mu.Unlock()
	return r.seen[path]
}

func main() {
	t := vhelp.New()
	kat := loadKAT(t)
	ctx := context.Background()

	sigTestDER := unhex(t, kat["sigTestDERCertString"])
	sthSig := unhex(t, kat["sigTestDefaultSTHSignature"])
	sctSig := unhex(t, kat["sigTestCertSCTSignatureEC"])
	rootHash := unhex(t, kat["sigTestDefaultRootHash"])
	keyIDEC := unhex(t, kat["sigTestKeyIDEC"])

	logKeyPEM := string(vhelp.ReadFixture("rfc6962", "sig-test-ec256-public-key.pem"))
	otherKeyPEM := string(vhelp.ReadFixture("rfc6962", "sig-test-ec256-public-key2.pem"))

	// ---- construction ------------------------------------------------------
	if _, err := client.New("http://127.0.0.1:1/x", nil, jsonclient.Options{PublicKey: "-----BEGIN PUBLIC KEY-----\nnope\n-----END PUBLIC KEY-----\n", Logger: quiet{}}); err != nil {
		t.Case("client-new-rejects-malformed-public-key", nil)
	} else {
		t.Case("client-new-rejects-malformed-public-key", fmt.Errorf("a malformed PEM public key was accepted"))
	}
	if _, err := client.New("http://127.0.0.1:1/x", nil, jsonclient.Options{PublicKey: logKeyPEM + "trailing junk\n", Logger: quiet{}}); err != nil {
		t.Case("client-new-rejects-trailing-data-after-pem", nil)
	} else {
		t.Case("client-new-rejects-trailing-data-after-pem", fmt.Errorf("trailing data after the PEM key was accepted"))
	}

	// ---- the canned log ----------------------------------------------------
	rec := &recorder{seen: map[string]string{}}
	var (
		sthOverride  []byte // when non-nil, the get-sth body to serve
		addChainCode = http.StatusOK
		addChain503  bool
	)
	mux := http.NewServeMux()
	serve := func(path string, fn func(*http.Request) (int, string)) {
		mux.HandleFunc(path, func(w http.ResponseWriter, r *http.Request) {
			body := ""
			if r.Method == http.MethodPost {
				buf := make([]byte, 65536)
				n, _ := r.Body.Read(buf)
				body = string(buf[:n])
			}
			rec.note(r, body)
			code, out := fn(r)
			w.Header().Set("Content-Type", "application/json")
			if code == http.StatusServiceUnavailable {
				w.Header().Set("Retry-After", "0")
			}
			w.WriteHeader(code)
			fmt.Fprint(w, out)
		})
	}

	defaultSTH := jsonObj(map[string]interface{}{
		"tree_size":           sthTreeSize,
		"timestamp":           sthTimestamp,
		"sha256_root_hash":    rootHash,
		"tree_head_signature": sthSig,
	})
	serve(ct.GetSTHPath, func(*http.Request) (int, string) {
		if sthOverride != nil {
			return http.StatusOK, string(sthOverride)
		}
		return http.StatusOK, defaultSTH
	})
	serve(ct.GetRootsPath, func(*http.Request) (int, string) {
		return http.StatusOK, jsonObj(map[string]interface{}{"certificates": []string{
			base64.StdEncoding.EncodeToString(vhelp.ReadFixture("minted", "bench-root.der")),
			base64.StdEncoding.EncodeToString(vhelp.ReadFixture("minted", "bench-intermediate.der")),
		}})
	})
	serve(ct.GetSTHConsistencyPath, func(*http.Request) (int, string) {
		return http.StatusOK, jsonObj(map[string]interface{}{"consistency": [][]byte{
			{0x01, 0x02}, {0x03, 0x04, 0x05},
		}})
	})
	serve(ct.GetProofByHashPath, func(*http.Request) (int, string) {
		return http.StatusOK, jsonObj(map[string]interface{}{
			"leaf_index": 17,
			"audit_path": [][]byte{{0xaa}, {0xbb, 0xcc}},
		})
	})
	leafInput := x509LeafInput(sctTimestamp, sigTestDER)
	extraData := certificateChain(sigTestDER)
	serve(ct.GetEntriesPath, func(*http.Request) (int, string) {
		return http.StatusOK, jsonObj(map[string]interface{}{"entries": []map[string]interface{}{
			{"leaf_input": leafInput, "extra_data": extraData},
		}})
	})
	sctBody := jsonObj(map[string]interface{}{
		"sct_version": 0,
		"id":          keyIDEC,
		"timestamp":   sctTimestamp,
		"extensions":  "",
		"signature":   sctSig,
	})
	serve(ct.AddChainPath, func(*http.Request) (int, string) {
		if addChain503 {
			addChain503 = false
			return http.StatusServiceUnavailable, `{}`
		}
		if addChainCode != http.StatusOK {
			return addChainCode, `{}`
		}
		return http.StatusOK, sctBody
	})
	serve(ct.AddPreChainPath, func(*http.Request) (int, string) {
		return http.StatusOK, sctBody
	})

	srv := httptest.NewServer(mux)
	defer srv.Close()

	// A deliberately over-decorated base URI: the client must trim it.
	lc, err := client.New(srv.URL+"///", nil, jsonclient.Options{PublicKey: logKeyPEM, Logger: quiet{}})
	if err != nil {
		t.Fatalf("client.New: %v", err)
	}
	t.Case("client-new-accepts-pinned-ec-log-key", nil)

	// ---- get-sth -----------------------------------------------------------
	sth, err := lc.GetSTH(ctx)
	if err != nil {
		t.Fatalf("GetSTH: %v", err)
	}
	t.Equal("get-sth-uses-rfc6962-path-and-trims-uri", rec.get("/ct/v1/get-sth"), "GET |")
	t.Check("get-sth-fields-exact",
		sth.TreeSize == sthTreeSize && sth.Timestamp == sthTimestamp &&
			string(sth.SHA256RootHash[:]) == string(rootHash) && sth.Version == ct.V1,
		"size=%d ts=%d root=%x version=%d", sth.TreeSize, sth.Timestamp, sth.SHA256RootHash[:], sth.Version)
	t.Check("get-sth-signature-decoded-exact",
		sth.TreeHeadSignature.Algorithm.Hash == tls.SHA256 &&
			sth.TreeHeadSignature.Algorithm.Signature == tls.ECDSA &&
			string(sth.TreeHeadSignature.Signature) == string(sthSig[4:]),
		"alg=%+v siglen=%d", sth.TreeHeadSignature.Algorithm, len(sth.TreeHeadSignature.Signature))
	t.Case("get-sth-verifies-against-configured-log-key", nil)

	// The very same STH must be refused when the client holds a different key.
	wrongKeyClient, err := client.New(srv.URL, nil, jsonclient.Options{PublicKey: otherKeyPEM, Logger: quiet{}})
	if err != nil {
		t.Fatalf("client.New(other key): %v", err)
	}
	_, err = wrongKeyClient.GetSTH(ctx)
	t.Case("get-sth-refused-when-key-does-not-match", nonNil(err, "an STH verified under the wrong log key"))

	// Structural defences, checked before any signature work.
	sthOverride = []byte(jsonObj(map[string]interface{}{
		"tree_size": sthTreeSize, "timestamp": sthTimestamp,
		"sha256_root_hash": rootHash[:31], "tree_head_signature": sthSig,
	}))
	_, err = lc.GetSTH(ctx)
	t.Case("get-sth-rejects-short-root-hash", nonNil(err, "a 31-byte sha256_root_hash was accepted"))

	sthOverride = []byte(jsonObj(map[string]interface{}{
		"tree_size": sthTreeSize, "timestamp": sthTimestamp,
		"sha256_root_hash": rootHash, "tree_head_signature": append(append([]byte{}, sthSig...), 0x00),
	}))
	_, err = lc.GetSTH(ctx)
	t.Case("get-sth-rejects-trailing-data-after-signature", nonNil(err, "a trailing byte after DigitallySigned was accepted"))
	sthOverride = nil

	// ---- the read endpoints ------------------------------------------------
	roots, err := lc.GetAcceptedRoots(ctx)
	if err != nil {
		t.Fatalf("GetAcceptedRoots: %v", err)
	}
	t.Check("get-roots-decodes-base64-der-in-order",
		len(roots) == 2 &&
			string(roots[0].Data) == string(vhelp.ReadFixture("minted", "bench-root.der")) &&
			string(roots[1].Data) == string(vhelp.ReadFixture("minted", "bench-intermediate.der")),
		"n=%d", len(roots))

	cons, err := lc.GetSTHConsistency(ctx, 3, 7)
	if err != nil {
		t.Fatalf("GetSTHConsistency: %v", err)
	}
	t.Check("get-sth-consistency-params-and-result",
		rec.get("/ct/v1/get-sth-consistency") == "GET first=3&second=7|" &&
			len(cons) == 2 && hex.EncodeToString(cons[0]) == "0102" && hex.EncodeToString(cons[1]) == "030405",
		"req=%q n=%d", rec.get("/ct/v1/get-sth-consistency"), len(cons))

	proof, err := lc.GetProofByHash(ctx, []byte{0xde, 0xad, 0xbe, 0xef}, 99)
	if err != nil {
		t.Fatalf("GetProofByHash: %v", err)
	}
	t.Check("get-proof-by-hash-params-and-result",
		rec.get("/ct/v1/get-proof-by-hash") == "GET hash=3q2%2B7w%3D%3D&tree_size=99|" &&
			proof.LeafIndex == 17 && len(proof.AuditPath) == 2 &&
			hex.EncodeToString(proof.AuditPath[0]) == "aa" && hex.EncodeToString(proof.AuditPath[1]) == "bbcc",
		"req=%q index=%d path=%d", rec.get("/ct/v1/get-proof-by-hash"), proof.LeafIndex, len(proof.AuditPath))

	entries, err := lc.GetEntries(ctx, 5, 5)
	if err != nil {
		t.Fatalf("GetEntries: %v", err)
	}
	t.Check("get-entries-parses-leaf-and-chain",
		rec.get("/ct/v1/get-entries") == "GET end=5&start=5|" &&
			len(entries) == 1 && entries[0].Index == 5 &&
			entries[0].Leaf.Version == ct.V1 &&
			entries[0].Leaf.LeafType == ct.TimestampedEntryLeafType &&
			entries[0].Leaf.TimestampedEntry.Timestamp == sctTimestamp &&
			entries[0].Leaf.TimestampedEntry.EntryType == ct.X509LogEntryType &&
			string(entries[0].Leaf.TimestampedEntry.X509Entry.Data) == string(sigTestDER) &&
			len(entries[0].Chain) == 1 && string(entries[0].Chain[0].Data) == string(sigTestDER),
		"req=%q n=%d", rec.get("/ct/v1/get-entries"), len(entries))

	_, err = lc.GetEntries(ctx, 9, 2)
	t.Case("get-entries-rejects-inverted-range-client-side", nonNil(err, "start > end was sent to the log"))

	// ---- the submission endpoints -------------------------------------------
	chain := []ct.ASN1Cert{{Data: sigTestDER}}
	sct, err := lc.AddChain(ctx, chain)
	if err != nil {
		t.Fatalf("AddChain: %v", err)
	}
	wantBody := `{"chain":["` + base64.StdEncoding.EncodeToString(sigTestDER) + `"]}`
	t.Check("add-chain-posts-der-chain-and-verifies-sct",
		rec.get("/ct/v1/add-chain") == "POST |"+wantBody &&
			sct.SCTVersion == ct.V1 && sct.Timestamp == sctTimestamp &&
			string(sct.LogID.KeyID[:]) == string(keyIDEC) &&
			sct.Signature.Algorithm.Hash == tls.SHA256 && sct.Signature.Algorithm.Signature == tls.ECDSA,
		"req=%q sct=%+v", rec.get("/ct/v1/add-chain"), sct)

	noVerify, err := client.New(srv.URL, nil, jsonclient.Options{Logger: quiet{}})
	if err != nil {
		t.Fatalf("client.New(no key): %v", err)
	}
	preSCT, err := noVerify.AddPreChain(ctx, chain)
	t.Check("add-pre-chain-uses-its-own-path",
		err == nil && preSCT != nil && rec.get("/ct/v1/add-pre-chain") == "POST |"+wantBody,
		"err=%v req=%q", err, rec.get("/ct/v1/add-pre-chain"))

	// A 503 is retriable; Retry-After: 0 makes the retry immediate.
	addChain503 = true
	if _, err := lc.AddChain(ctx, chain); err != nil {
		t.Case("add-chain-retries-after-503", fmt.Errorf("a retriable 503 was not retried: %v", err))
	} else {
		t.Case("add-chain-retries-after-503", nil)
	}

	// A 400 is not retriable and must surface.
	addChainCode = http.StatusBadRequest
	_, err = lc.AddChain(ctx, chain)
	t.Case("add-chain-surfaces-non-retriable-400", nonNil(err, "a 400 from the log was swallowed"))
	addChainCode = http.StatusOK

	// The SCT the log returned does not belong to a different chain.
	other := []ct.ASN1Cert{{Data: vhelp.ReadFixture("minted", "leaf-valid.der")}}
	_, err = lc.AddChain(ctx, other)
	t.Case("add-chain-rejects-sct-that-signs-another-chain",
		nonNil(err, "an SCT over a different certificate was accepted"))

	t.Done()
}

// ---- helpers ---------------------------------------------------------------

func nonNil(err error, why string) error {
	if err == nil {
		return fmt.Errorf("%s", why)
	}
	return nil
}

func loadKAT(t *vhelp.T) map[string]string {
	out := map[string]string{}
	for _, line := range strings.Split(string(vhelp.ReadFixture("rfc6962", "kat.txt")), "\n") {
		f := strings.Fields(line)
		if len(f) == 2 {
			out[f[0]] = f[1]
		}
	}
	if len(out) == 0 {
		t.Fatalf("rfc6962/kat.txt held no known answers")
	}
	return out
}

func unhex(t *vhelp.T, s string) []byte {
	b, err := hex.DecodeString(s)
	if err != nil || len(b) == 0 {
		t.Fatalf("bad hex known answer %q: %v", s, err)
	}
	return b
}

func jsonObj(m interface{}) string {
	b, err := json.Marshal(m)
	if err != nil {
		panic(err)
	}
	return string(b)
}

// x509LeafInput hand-builds the RFC 6962 MerkleTreeLeaf encoding for an X.509
// entry, without using the product's own serializer.
func x509LeafInput(ts uint64, der []byte) []byte {
	out := []byte{0x00, 0x00} // version v1, leaf_type timestamped_entry
	for i := 7; i >= 0; i-- {
		out = append(out, byte(ts>>(uint(i)*8)))
	}
	out = append(out, 0x00, 0x00) // entry_type x509_entry
	out = append(out, uint24(len(der))...)
	out = append(out, der...)
	out = append(out, 0x00, 0x00) // empty extensions
	return out
}

// certificateChain hand-builds the TLS encoding of ct.CertificateChain.
func certificateChain(ders ...[]byte) []byte {
	var body []byte
	for _, d := range ders {
		body = append(body, uint24(len(d))...)
		body = append(body, d...)
	}
	return append(uint24(len(body)), body...)
}

func uint24(n int) []byte {
	return []byte{byte(n >> 16), byte(n >> 8), byte(n)}
}

var _ = pem.Decode

// ctserver-tls-transport
//
// v1.3.3 teaches ct_server to serve HTTPS itself. --tls_certificate and
// --tls_key load a key pair with tls.LoadX509KeyPair and put it in the
// http.Server's TLSConfig with MinVersion: tls.VersionTLS12, after which the
// process calls ListenAndServeTLS instead of ListenAndServe; the pair is
// inert unless both flags are set. The same release adds
// --trillian_tls_ca_cert_file, which builds gRPC transport credentials from a
// CA file and terminates the process when that file is not a certificate.
//
// The boundary is a real loopback socket on a port obtained by binding :0:
// the group dials the candidate's own binary and looks at the handshake and
// the responses, never at its source. Certificate, key, log key and backend
// answers are pinned fixtures and pinned constants (SCOPE-TIME); the client
// does no date-based validation of the server certificate, it compares the
// presented DER to the fixture byte for byte.
package main

import (
	"crypto/tls"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"net"
	"net/http"
	"os"
	"strings"
	"time"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/x509util"
	"github.com/google/trillian"
	"google.golang.org/grpc"

	stdx509 "crypto/x509"

	"ctverify/vhelp"
)

func derList(t *vhelp.T, rel ...string) [][]byte {
	certs, err := x509util.CertificatesFromPEM(vhelp.ReadFixture(rel...))
	if err != nil || len(certs) == 0 {
		t.Fatalf("parse %v: %v", rel, err)
	}
	out := make([][]byte, 0, len(certs))
	for _, c := range certs {
		out = append(out, c.Raw)
	}
	return out
}

func b64all(ders [][]byte) []string {
	out := make([]string, 0, len(ders))
	for _, d := range ders {
		out = append(out, base64.StdEncoding.EncodeToString(d))
	}
	return out
}

func sameSet(a, b map[string]bool) bool {
	if len(a) != len(b) {
		return false
	}
	for k := range a {
		if !b[k] {
			return false
		}
	}
	return true
}

// httpsClient is a client that performs the handshake but leaves the verdict
// on the certificate to this group, which compares the presented DER with the
// pinned fixture instead of trusting a chain or a clock.
func httpsClient() *http.Client {
	return &http.Client{
		Timeout: 30 * time.Second,
		Transport: &http.Transport{
			DisableKeepAlives: true,
			TLSClientConfig: &tls.Config{
				InsecureSkipVerify: true,
				MinVersion:         tls.VersionTLS12,
			},
		},
	}
}

func do(t *vhelp.T, c *http.Client, req *http.Request) httpResult {
	resp, err := c.Do(req)
	if err != nil {
		t.Fatalf("%s %s: %v", req.Method, req.URL, err)
	}
	defer func() { _ = resp.Body.Close() }()
	return httpResult{status: resp.StatusCode, body: readAll(resp.Body), header: resp.Header}
}

func getURL(t *vhelp.T, c *http.Client, url string) httpResult {
	req, err := http.NewRequest(http.MethodGet, url, nil)
	if err != nil {
		t.Fatalf("build GET %s: %v", url, err)
	}
	return do(t, c, req)
}

func postURL(t *vhelp.T, c *http.Client, url string, body []byte) httpResult {
	req, err := http.NewRequest(http.MethodPost, url, strings.NewReader(string(body)))
	if err != nil {
		t.Fatalf("build POST %s: %v", url, err)
	}
	req.Header.Set("Content-Type", "application/json")
	return do(t, c, req)
}

// startTLS boots the candidate's ct_server with the TLS flags and waits for
// /healthz over HTTPS. The kit's own wait is plaintext, which a TLS listener
// must refuse, so the readiness loop lives here.
func startTLS(t *vhelp.T, be *stubBackend, logs []logSpec, extraArgs []string) (*harness, string) {
	lis, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen for backend: %v", err)
	}
	gs := grpc.NewServer()
	trillian.RegisterTrillianLogServer(gs, be)
	go func() { _ = gs.Serve(lis) }()

	cfg := writeConfig(t, lis.Addr().String(), logs)
	h, err := launch(t, be, gs, cfg, extraArgs, false)
	if err != nil {
		t.Fatalf("%v", err)
	}
	addr := strings.TrimPrefix(h.baseURL, "http://")

	c := httpsClient()
	deadline := time.Now().Add(90 * time.Second)
	for {
		if time.Now().After(deadline) {
			h.stop()
			t.Fatalf("ct_server never answered /healthz over TLS")
		}
		resp, err := c.Get("https://" + addr + "/healthz")
		if err == nil {
			_ = resp.Body.Close()
			if resp.StatusCode == http.StatusOK {
				return h, addr
			}
		}
		time.Sleep(100 * time.Millisecond)
	}
}

func main() {
	t := vhelp.New()

	privDER := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-key.pem"))
	pubDER := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-pubkey.pem"))

	pub, err := stdx509.ParsePKIXPublicKey(pubDER)
	if err != nil {
		t.Fatalf("parse pinned log public key: %v", err)
	}
	verifier, err := ct.NewSignatureVerifier(pub)
	if err != nil {
		t.Fatalf("new signature verifier: %v", err)
	}

	// The server's own TLS identity: a pinned leaf and the pinned key that
	// signed for it. Both are fixtures; neither is generated here.
	certPath := vhelp.Fixture("minted", "leaf-valid.pem")
	keyPath := vhelp.Fixture("minted", "keys", "leaf-key.pem")
	wantServerDER := vhelp.ReadFixture("minted", "leaf-valid.der")

	chain := derList(t, "minted", "chain-valid.pem")
	roots := derList(t, "minted", "roots-bench-plus-int.pem")

	be := newStub()
	h, addr := startTLS(t, be, []logSpec{{
		id: pinnedLogID, prefix: logPrefix, privKey: privDER, pubKey: pubDER,
	}}, []string{
		"--tls_certificate=" + certPath,
		"--tls_key=" + keyPath,
	})
	defer h.stop()

	c := httpsClient()
	base := "https://" + addr
	logBase := base + "/" + logPrefix

	// 1. Liveness is served, and only over TLS.
	r := getURL(t, c, base+"/healthz")
	t.Check("healthz-is-served-over-https", r.status == http.StatusOK && string(r.body) == "ok",
		"GET https /healthz = %d %q, want 200 \"ok\"", r.status, string(r.body))

	// 2. The certificate presented is exactly the configured one.
	conn, err := tls.Dial("tcp", addr, &tls.Config{
		InsecureSkipVerify: true,
		MinVersion:         tls.VersionTLS12,
		MaxVersion:         tls.VersionTLS12,
	})
	if err != nil {
		t.Fatalf("TLS dial %s: %v", addr, err)
	}
	state := conn.ConnectionState()
	_ = conn.Close()
	presented := []byte(nil)
	if len(state.PeerCertificates) > 0 {
		presented = state.PeerCertificates[0].Raw
	}
	t.Check("the-server-presents-the-configured-certificate",
		len(state.PeerCertificates) == 1 && string(presented) == string(wantServerDER),
		"server presented %d certificate(s), first %d bytes; want exactly the pinned %d-byte leaf",
		len(state.PeerCertificates), len(presented), len(wantServerDER))

	// 3. ... over TLS 1.2 when that is the most the client will do.
	t.Equal("the-handshake-negotiates-tls-1-2", fmt.Sprintf("0x%04x", state.Version), "0x0303")

	// 4. A client that cannot go above TLS 1.1 gets no session: the server's
	//    configured floor is TLS 1.2.
	old, err := tls.Dial("tcp", addr, &tls.Config{
		InsecureSkipVerify: true,
		MinVersion:         tls.VersionTLS10,
		MaxVersion:         tls.VersionTLS11,
	})
	if err == nil {
		_ = old.Close()
	}
	t.Check("a-client-capped-at-tls-1-1-is-refused", err != nil,
		"a TLS 1.1 client completed a handshake, want it refused")

	// 5. A plaintext request on the TLS port is refused rather than served:
	//    400, and not the personality's own answer. The body net/http writes
	//    here belongs to the standard library, not to the candidate, so it is
	//    not compared (SCOPE-DIAGNOSTIC): the graded facts are the status and
	//    that /healthz did not answer "ok" over cleartext.
	plain, err := http.Get("http://" + addr + "/healthz")
	if err != nil {
		t.Fatalf("plaintext GET on the TLS port: %v", err)
	}
	plainBody := readAll(plain.Body)
	_ = plain.Body.Close()
	t.Check("plain-http-to-the-tls-port-is-refused",
		plain.StatusCode == http.StatusBadRequest && string(plainBody) != "ok",
		"plaintext GET = %d %q, want 400 and not the personality's own answer",
		plain.StatusCode, lastBytes(plainBody, 200))

	// 6. The log's own endpoints are served over the TLS listener.
	r = getURL(t, c, logBase+"/ct/v1/get-roots")
	var gotRoots struct {
		Certificates []string `json:"certificates"`
	}
	if err := json.Unmarshal(r.body, &gotRoots); err != nil {
		t.Fatalf("get-roots body %q: %v", lastBytes(r.body, 400), err)
	}
	wantRoots := map[string]bool{}
	for _, d := range roots {
		wantRoots[base64.StdEncoding.EncodeToString(d)] = true
	}
	gotSet := map[string]bool{}
	for _, s := range gotRoots.Certificates {
		gotSet[s] = true
	}
	t.Check("get-roots-is-served-over-https",
		r.status == http.StatusOK && sameSet(gotSet, wantRoots),
		"get-roots over TLS returned %d certificates (status %d), want the %d configured anchors",
		len(gotSet), r.status, len(wantRoots))

	// 7. A submission over TLS still produces an SCT signed by the log key.
	body, err := json.Marshal(map[string]interface{}{"chain": b64all(chain)})
	if err != nil {
		t.Fatalf("marshal add-chain body: %v", err)
	}
	r = postURL(t, c, logBase+"/ct/v1/add-chain", body)
	if r.status != http.StatusOK {
		t.Fatalf("add-chain over TLS = %d %q", r.status, lastBytes(r.body, 400))
	}
	var acr ct.AddChainResponse
	if err := json.Unmarshal(r.body, &acr); err != nil {
		t.Fatalf("add-chain body %q: %v", lastBytes(r.body, 400), err)
	}
	sct, err := acr.ToSignedCertificateTimestamp()
	if err != nil {
		t.Fatalf("decode SCT: %v", err)
	}
	leaf := ct.MerkleTreeLeaf{
		Version:  ct.V1,
		LeafType: ct.TimestampedEntryLeafType,
		TimestampedEntry: &ct.TimestampedEntry{
			Timestamp: sct.Timestamp,
			EntryType: ct.X509LogEntryType,
			X509Entry: &ct.ASN1Cert{Data: chain[0]},
		},
	}
	sigErr := verifier.VerifySCTSignature(*sct, ct.LogEntry{Leaf: leaf})
	t.Check("add-chain-over-https-issues-a-verifiable-sct",
		sct.SCTVersion == ct.V1 && sigErr == nil && len(be.queuedLeaves()) == 1,
		"SCT version %v, signature check %v, %d leaves queued", sct.SCTVersion, sigErr, len(be.queuedLeaves()))

	// 8. ... and the tree head the pinned backend holds comes back unchanged.
	r = getURL(t, c, logBase+"/ct/v1/get-sth")
	var sthResp ct.GetSTHResponse
	if err := json.Unmarshal(r.body, &sthResp); err != nil {
		t.Fatalf("get-sth body %q: %v", lastBytes(r.body, 400), err)
	}
	sth, err := sthResp.ToSignedTreeHead()
	if err != nil {
		t.Fatalf("decode STH: %v", err)
	}
	sthErr := verifier.VerifySTHSignature(*sth)
	t.Check("get-sth-is-served-over-https",
		r.status == http.StatusOK && sth.TreeSize == pinnedTreeSize &&
			sth.Timestamp == pinnedTimestampMS && sth.SHA256RootHash == pinnedRootHash && sthErr == nil,
		"get-sth over TLS = %d, tree size %d (want %d), timestamp %d (want %d), root %x, signature %v",
		r.status, sth.TreeSize, pinnedTreeSize, sth.Timestamp, pinnedTimestampMS, sth.SHA256RootHash[:], sthErr)

	h.stop()

	// 9. The pair is inert unless both halves are set: with only the
	//    certificate the server is the plaintext one again.
	be2 := newStub()
	h2 := startPersonality(t, be2, []logSpec{{
		id: pinnedLogID, prefix: logPrefix, privKey: privDER, pubKey: pubDER,
	}}, []string{"--tls_certificate=" + certPath})
	defer h2.stop()
	addr2 := strings.TrimPrefix(h2.baseURL, "http://")
	r = h2.get(t, h2.baseURL+"/healthz")
	tlsConn, tlsErr := tls.Dial("tcp", addr2, &tls.Config{InsecureSkipVerify: true, MinVersion: tls.VersionTLS12})
	if tlsErr == nil {
		_ = tlsConn.Close()
	}
	t.Check("the-certificate-flag-alone-leaves-the-listener-plaintext",
		r.status == http.StatusOK && string(r.body) == "ok" && tlsErr != nil,
		"with only --tls_certificate: GET /healthz = %d %q and a TLS handshake returned %v; want 200 \"ok\" and a refused handshake",
		r.status, string(r.body), tlsErr)
	h2.stop()

	// 10. A --trillian_tls_ca_cert_file that is not a certificate stops the
	//     process rather than starting a server that cannot reach a backend.
	badCA := scratchPath("not-a-ca.pem")
	if err := os.WriteFile(badCA, []byte("this file is not a certificate\n"), 0600); err != nil {
		t.Fatalf("write bad CA file: %v", err)
	}
	be3 := newStub()
	lis, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen for backend: %v", err)
	}
	gs := grpc.NewServer()
	trillian.RegisterTrillianLogServer(gs, be3)
	go func() { _ = gs.Serve(lis) }()
	cfg3 := writeConfig(t, lis.Addr().String(), []logSpec{{
		id: pinnedLogID, prefix: logPrefix, privKey: privDER, pubKey: pubDER,
	}})
	h3, err := launch(t, be3, gs, cfg3, []string{"--trillian_tls_ca_cert_file=" + badCA}, false)
	if err != nil {
		t.Fatalf("launch with a bad CA file: %v", err)
	}
	code := h3.waitExit(60 * time.Second)
	h3.stop()
	t.Check("an-unreadable-trillian-ca-certificate-stops-startup", code == 1,
		"ct_server with a non-certificate --trillian_tls_ca_cert_file exited %d, want 1", code)

	t.Done()
}

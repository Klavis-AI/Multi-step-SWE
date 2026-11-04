// Group client-get-entry-and-proof.
//
// v1.0.19 adds LogClient.GetEntryAndProof, the last RFC 6962 read endpoint the
// client package was missing (section 4.8). migrillian needs it to pull an
// entry together with its inclusion proof in one round trip.
//
// Graded over a real socket: the driver runs its own minimal log on loopback
// (port from binding :0), points the candidate's own client at it, and grades
// both halves of the call -- the request the client composes, recorded by the
// server, and the response it parses back, compared against the exact bytes
// the server sent. The two error cases fix the other half of the contract: a
// log that answers with an HTTP error, or with a body that is not the JSON
// this endpoint defines, must surface as an error and not as an empty entry.
//
// SCOPE-NET: loopback only, on a port obtained by binding :0.
// SCOPE-TIME: the served leaf carries a pinned timestamp; no clock is read.
//
// client.LogClient has no GetEntryAndProof method at v1.0.17, so this driver
// does not compile against the previous tree.
package main

import (
	"context"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"net"
	"net/http"
	"net/url"
	"sync"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/client"
	"github.com/google/certificate-transparency-go/jsonclient"
	"github.com/google/certificate-transparency-go/tls"

	"ctverify/vhelp"
)

const pinnedLeafTimestamp = uint64(1519737600000)

// fakeLog is a log that answers exactly one endpoint, recording what it was
// asked. Its body is chosen by the caller so the error paths can be driven.
type fakeLog struct {
	mu      sync.Mutex
	method  string
	path    string
	query   string
	status  int
	body    []byte
	ln      net.Listener
	srv     *http.Server
	baseURI string
}

func newFakeLog(t *vhelp.T, status int, body []byte) *fakeLog {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen: %v", err)
	}
	f := &fakeLog{status: status, body: body, ln: ln, baseURI: "http://" + ln.Addr().String()}
	mux := http.NewServeMux()
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		f.mu.Lock()
		f.method, f.path, f.query = r.Method, r.URL.Path, r.URL.RawQuery
		f.mu.Unlock()
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(f.status)
		w.Write(f.body)
	})
	f.srv = &http.Server{Handler: mux}
	go f.srv.Serve(ln)
	return f
}

func (f *fakeLog) seen() (string, string, string) {
	f.mu.Lock()
	defer f.mu.Unlock()
	return f.method, f.path, f.query
}

func (f *fakeLog) stop() {
	f.srv.Close()
	f.ln.Close()
}

func clientFor(t *vhelp.T, f *fakeLog) *client.LogClient {
	lc, err := client.New(f.baseURI, nil, jsonclient.Options{})
	if err != nil {
		t.Fatalf("client.New: %v", err)
	}
	return lc
}

// canonQuery parses a raw query string and re-renders it with its parameters
// in sorted order. Query parameters are unordered (RFC 3986) and HTTP-API.md
// 4.5 fixes only their names, so an assertion built on this pins which
// parameters are sent and with what values, and not the order they appear in.
// An unparseable query is reported verbatim so the assertion still fails
// against the bytes that were actually sent.
func canonQuery(raw string) string {
	v, err := url.ParseQuery(raw)
	if err != nil {
		return fmt.Sprintf("unparseable query %q: %v", raw, err)
	}
	return v.Encode()
}

// hexes renders a proof so a whole audit path is one exact comparison.
func hexes(bs [][]byte) string {
	out := make([]string, 0, len(bs))
	for _, b := range bs {
		out = append(out, hex.EncodeToString(b))
	}
	return fmt.Sprintf("%v", out)
}

func main() {
	t := vhelp.New()
	ctx := context.Background()

	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	interDER := vhelp.ReadFixture("minted", "bench-intermediate.der")

	// The entry the fake log will serve: a real RFC 6962 leaf over a fixture
	// certificate, at a pinned timestamp, with a real chain as extra data.
	leafInput, err := tls.Marshal(ct.MerkleTreeLeaf{
		Version:  ct.V1,
		LeafType: ct.TimestampedEntryLeafType,
		TimestampedEntry: &ct.TimestampedEntry{
			Timestamp: pinnedLeafTimestamp,
			EntryType: ct.X509LogEntryType,
			X509Entry: &ct.ASN1Cert{Data: leafDER},
		},
	})
	if err != nil {
		t.Fatalf("marshal pinned leaf: %v", err)
	}
	extraData, err := tls.Marshal(ct.CertificateChain{Entries: []ct.ASN1Cert{{Data: interDER}}})
	if err != nil {
		t.Fatalf("marshal pinned extra data: %v", err)
	}
	auditPath := [][]byte{
		{0x01, 0x02, 0x03, 0x04},
		{0xaa, 0xbb},
		{0xff},
	}
	body, err := json.Marshal(ct.GetEntryAndProofResponse{
		LeafInput: leafInput,
		ExtraData: extraData,
		AuditPath: auditPath,
	})
	if err != nil {
		t.Fatalf("marshal pinned response: %v", err)
	}

	good := newFakeLog(t, http.StatusOK, body)
	defer good.stop()

	rsp, err := clientFor(t, good).GetEntryAndProof(ctx, 7, 100000)
	if err != nil || rsp == nil {
		t.Fatalf("GetEntryAndProof against the fake log: rsp=%v err=%v", rsp, err)
	}
	method, path, query := good.seen()

	// --- 1. the request goes to the RFC 6962 path, by GET -------------------
	t.Equal("get-entry-and-proof-uses-the-rfc6962-path",
		fmt.Sprintf("%s %s", method, path),
		fmt.Sprintf("GET %s", ct.GetEntryAndProofPath))

	// --- 2. carrying leaf_index and tree_size, base 10 ----------------------
	// The whole parameter set, so an extra, missing or renamed parameter
	// fails -- but parsed and re-rendered in sorted order, so the order the
	// client chose to emit them in is not graded.
	t.Equal("get-entry-and-proof-sends-leaf-index-and-tree-size",
		canonQuery(query), "leaf_index=7&tree_size=100000")

	// --- 3. the entry comes back byte for byte ------------------------------
	t.Equal("get-entry-and-proof-returns-the-served-entry",
		fmt.Sprintf("leaf=%x extra=%x", rsp.LeafInput, rsp.ExtraData),
		fmt.Sprintf("leaf=%x extra=%x", leafInput, extraData))

	// --- 4. and the entry really is the leaf that was logged ----------------
	var got ct.MerkleTreeLeaf
	rest, uerr := tls.Unmarshal(rsp.LeafInput, &got)
	entry := ""
	if uerr == nil && got.TimestampedEntry != nil && got.TimestampedEntry.X509Entry != nil {
		entry = hex.EncodeToString(got.TimestampedEntry.X509Entry.Data)
	}
	t.Equal("returned-entry-decodes-to-the-logged-merkle-leaf",
		fmt.Sprintf("err=%v trailing=%d ts=%d cert=%s", uerr, len(rest),
			func() uint64 {
				if got.TimestampedEntry == nil {
					return 0
				}
				return got.TimestampedEntry.Timestamp
			}(), entry),
		fmt.Sprintf("err=%v trailing=%d ts=%d cert=%s", error(nil), 0,
			pinnedLeafTimestamp, hex.EncodeToString(leafDER)))

	// --- 5. as does the proof, in order -------------------------------------
	t.Equal("get-entry-and-proof-returns-the-audit-path-in-order",
		hexes(rsp.AuditPath), hexes(auditPath))

	// --- 6. an unhappy log is reported, not swallowed -----------------------
	// An HTTP error and a body that is not this endpoint's JSON must both
	// produce no response and an error; neither may become an empty entry.
	bad := newFakeLog(t, http.StatusNotFound, []byte(`{"error":"no such entry"}`))
	defer bad.stop()
	badRsp, badErr := clientFor(t, bad).GetEntryAndProof(ctx, 7, 100000)

	junk := newFakeLog(t, http.StatusOK, []byte(`not json at all`))
	defer junk.stop()
	junkRsp, junkErr := clientFor(t, junk).GetEntryAndProof(ctx, 7, 100000)

	t.Equal("log-errors-are-reported-rather-than-returned-as-an-empty-entry",
		fmt.Sprintf("http:rsp=%v err=%v json:rsp=%v err=%v",
			badRsp == nil, badErr != nil, junkRsp == nil, junkErr != nil),
		"http:rsp=true err=true json:rsp=true err=true")

	t.Done()
}

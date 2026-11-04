// Group inherited-logclient-signature-checked.
//
// The client surface every earlier milestone built and this one leaves alone:
// client.LogClient over jsonclient, talking RFC 6962 s4 to a log. The
// interesting part is not that the JSON decodes, it is that a client
// configured with a log's public key refuses anything that key did not sign --
// which is the only reason an SCT is worth anything.
//
// So the log here is a real loopback HTTP server owned by this driver, on a
// port obtained by binding :0, and every signature it serves is produced with
// the standard library over a preimage this driver assembles from RFC 6962's
// own field widths. The product never gets to both make and check the same
// signature. Timestamps, tree size and root hash are pinned constants
// (SCOPE-TIME); nothing reaches the network (SCOPE-NET).
package main

import (
	"bytes"
	"context"
	"crypto/ecdsa"
	"crypto/rand"
	"crypto/sha256"
	stdx509 "crypto/x509"
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"encoding/pem"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/client"
	"github.com/google/certificate-transparency-go/jsonclient"

	"ctverify/vhelp"
)

const (
	pinnedTimestamp = uint64(1519741473000)
	pinnedTreeSize  = uint64(42)
	pinnedIndex     = int64(7)
)

// ---- byte-level helpers: RFC 6962 field widths, written out by hand. ----

func u16(n int) []byte { return []byte{byte(n >> 8), byte(n)} }

func u24(n int) []byte { return []byte{byte(n >> 16), byte(n >> 8), byte(n)} }

func u64(n uint64) []byte {
	var b [8]byte
	binary.BigEndian.PutUint64(b[:], n)
	return b[:]
}

// merkleLeaf is the MerkleTreeLeaf of an X.509 entry: v1, timestamped_entry,
// timestamp, x509_entry, uint24-prefixed certificate, no extensions.
func merkleLeaf(der []byte, ts uint64) []byte {
	var b bytes.Buffer
	b.Write([]byte{0x00, 0x00})
	b.Write(u64(ts))
	b.Write(u16(0))
	b.Write(u24(len(der)))
	b.Write(der)
	b.Write(u16(0))
	return b.Bytes()
}

// sctInput is the CertificateTimestamp an SCT signature is computed over.
func sctInput(der []byte, ts uint64) []byte {
	var b bytes.Buffer
	b.Write([]byte{0x00, 0x00}) // v1, certificate_timestamp
	b.Write(u64(ts))
	b.Write(u16(0)) // x509_entry
	b.Write(u24(len(der)))
	b.Write(der)
	b.Write(u16(0))
	return b.Bytes()
}

// sthInput is the TreeHeadSignature an STH signature is computed over.
func sthInput(ts, size uint64, root []byte) []byte {
	var b bytes.Buffer
	b.Write([]byte{0x00, 0x01}) // v1, tree_hash
	b.Write(u64(ts))
	b.Write(u64(size))
	b.Write(root)
	return b.Bytes()
}

// digitallySigned wraps a raw ECDSA signature in the RFC 5246 structure:
// sha256(4), ecdsa(3), uint16-prefixed signature.
func digitallySigned(sig []byte) []byte {
	return append(append([]byte{0x04, 0x03}, u16(len(sig))...), sig...)
}

// ---- the loopback log ---------------------------------------------------

type fakeLog struct {
	mu sync.Mutex

	key       *ecdsa.PrivateKey
	logID     []byte
	leafDER   []byte
	interDER  []byte
	rootDER   []byte
	rootHash  []byte
	sctOver   []byte // the preimage the add-chain SCT is signed over
	sthSize   uint64 // the tree size the STH signature is computed over
	lastPath  string
	lastQuery map[string]string
	lastBody  []byte
	lastVerb  string
}

func (f *fakeLog) sign(data []byte) []byte {
	h := sha256.Sum256(data)
	sig, err := ecdsa.SignASN1(rand.Reader, f.key, h[:])
	if err != nil {
		panic(err)
	}
	return digitallySigned(sig)
}

func (f *fakeLog) record(r *http.Request) {
	f.lastVerb = r.Method
	f.lastPath = r.URL.Path
	f.lastQuery = map[string]string{}
	for k, v := range r.URL.Query() {
		if len(v) > 0 {
			f.lastQuery[k] = v[0]
		}
	}
	if r.Body != nil {
		buf := new(bytes.Buffer)
		buf.ReadFrom(r.Body)
		f.lastBody = buf.Bytes()
	}
}

func (f *fakeLog) handler(w http.ResponseWriter, r *http.Request) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.record(r)

	w.Header().Set("Content-Type", "application/json")
	enc := json.NewEncoder(w)
	switch r.URL.Path {
	case ct.GetSTHPath:
		enc.Encode(map[string]interface{}{
			"tree_size":           pinnedTreeSize,
			"timestamp":           pinnedTimestamp,
			"sha256_root_hash":    f.rootHash,
			"tree_head_signature": f.sign(sthInput(pinnedTimestamp, f.sthSize, f.rootHash)),
		})
	case ct.AddChainPath:
		enc.Encode(map[string]interface{}{
			"sct_version": 0,
			"id":          f.logID,
			"timestamp":   pinnedTimestamp,
			"extensions":  "",
			"signature":   f.sign(sctInput(f.sctOver, pinnedTimestamp)),
		})
	case ct.GetRootsPath:
		enc.Encode(map[string]interface{}{
			"certificates": []string{
				base64.StdEncoding.EncodeToString(f.rootDER),
				base64.StdEncoding.EncodeToString(f.interDER),
			},
		})
	case ct.GetProofByHashPath:
		enc.Encode(map[string]interface{}{
			"leaf_index": pinnedIndex,
			"audit_path": [][]byte{f.rootHash},
		})
	case ct.GetSTHConsistencyPath:
		enc.Encode(map[string]interface{}{"consistency": [][]byte{f.rootHash}})
	case ct.GetEntriesPath:
		var extra bytes.Buffer
		extra.Write(u24(len(f.interDER) + 3))
		extra.Write(u24(len(f.interDER)))
		extra.Write(f.interDER)
		enc.Encode(map[string]interface{}{
			"entries": []map[string]interface{}{{
				"leaf_input": merkleLeaf(f.leafDER, pinnedTimestamp),
				"extra_data": extra.Bytes(),
			}},
		})
	default:
		w.WriteHeader(http.StatusNotFound)
		w.Write([]byte(`{"error_message":"no such endpoint"}`))
	}
}

func privateKey(t *vhelp.T) *ecdsa.PrivateKey {
	blk, _ := pem.Decode(vhelp.ReadFixture("minted", "keys", "log-key.pem"))
	if blk == nil {
		t.Fatalf("log-key.pem is not PEM")
	}
	// Parsed with the standard library, not the product's fork.
	any, err := stdx509.ParsePKCS8PrivateKey(blk.Bytes)
	if err != nil {
		t.Fatalf("parsing log-key.pem: %v", err)
	}
	k, ok := any.(*ecdsa.PrivateKey)
	if !ok {
		t.Fatalf("log-key.pem is %T, want *ecdsa.PrivateKey", any)
	}
	return k
}

func main() {
	t := vhelp.New()
	ctx := context.Background()

	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	interDER := vhelp.ReadFixture("minted", "bench-intermediate.der")
	rootDER := vhelp.ReadFixture("minted", "bench-root.der")
	pubPEM := string(vhelp.ReadFixture("minted", "keys", "log-pubkey.pem"))

	logIDLines := strings.Split(strings.TrimSpace(string(vhelp.ReadFixture("minted", "keys", "log-id.txt"))), "\n")
	if len(logIDLines) < 2 {
		t.Fatalf("log-id.txt has %d lines", len(logIDLines))
	}
	logID, err := base64.StdEncoding.DecodeString(strings.TrimSpace(logIDLines[1]))
	if err != nil || len(logID) != sha256.Size {
		t.Fatalf("log-id.txt line 2 is not a 32-byte base64 digest: %v", err)
	}

	rootHash := make([]byte, sha256.Size)
	for i := range rootHash {
		rootHash[i] = byte(0xa0 + i%16)
	}

	f := &fakeLog{
		key:      privateKey(t),
		logID:    logID,
		leafDER:  leafDER,
		interDER: interDER,
		rootDER:  rootDER,
		rootHash: rootHash,
		sctOver:  leafDER,
		sthSize:  pinnedTreeSize,
	}
	srv := httptest.NewServer(http.HandlerFunc(f.handler))
	defer srv.Close()

	// ---- 1. A client is built from the log's PEM public key. -----------
	lc, err := client.New(srv.URL, srv.Client(), jsonclient.Options{PublicKey: pubPEM})
	t.Case("client-is-built-from-the-log-public-key", err)
	if err != nil {
		t.Fatalf("cannot continue without a client: %v", err)
	}

	// ---- 2. ... and refused when the key is not a key. -----------------
	_, badErr := client.New(srv.URL, srv.Client(), jsonclient.Options{PublicKey: "-----BEGIN PUBLIC KEY-----\nAAAA\n-----END PUBLIC KEY-----\n"})
	t.Check("malformed-public-key-is-refused", badErr != nil,
		"client.New accepted a public key that does not parse")

	// ---- 3. get-sth: the STH comes back only because it verifies. ------
	sth, err := lc.GetSTH(ctx)
	ok := err == nil && sth != nil
	if ok {
		ok = sth.TreeSize == pinnedTreeSize && sth.Timestamp == pinnedTimestamp &&
			bytes.Equal(sth.SHA256RootHash[:], rootHash) &&
			sth.Version == ct.V1
	}
	t.Check("get-sth-verifies-the-log-signature", ok, "GetSTH: err=%v sth=%v", err, sth)

	// ---- 4. A signature over a different tree is refused. --------------
	f.mu.Lock()
	f.sthSize = pinnedTreeSize + 1
	f.mu.Unlock()
	badSTH, err := lc.GetSTH(ctx)
	t.Check("get-sth-rejects-a-signature-over-a-different-tree",
		err != nil && badSTH == nil,
		"GetSTH accepted an STH signed over tree size %d: err=%v", pinnedTreeSize+1, err)
	f.mu.Lock()
	f.sthSize = pinnedTreeSize
	f.mu.Unlock()

	// ---- 5. add-chain: the SCT is checked against the submitted chain. -
	chain := []ct.ASN1Cert{{Data: leafDER}, {Data: interDER}}
	sct, err := lc.AddChain(ctx, chain)
	ok = err == nil && sct != nil
	if ok {
		ok = sct.SCTVersion == ct.V1 && sct.Timestamp == pinnedTimestamp &&
			bytes.Equal(sct.LogID.KeyID[:], logID) && len(sct.Extensions) == 0
	}
	t.Check("add-chain-returns-a-signature-checked-sct", ok, "AddChain: err=%v sct=%v", err, sct)

	// ---- 6. ... having posted the chain as base64 DER, in order. -------
	f.mu.Lock()
	verb, path, body := f.lastVerb, f.lastPath, append([]byte{}, f.lastBody...)
	f.mu.Unlock()
	var req struct {
		Chain []string `json:"chain"`
	}
	jerr := json.Unmarshal(body, &req)
	t.Check("add-chain-posts-the-chain-as-base64-der",
		jerr == nil && verb == http.MethodPost && path == ct.AddChainPath &&
			len(req.Chain) == 2 &&
			req.Chain[0] == base64.StdEncoding.EncodeToString(leafDER) &&
			req.Chain[1] == base64.StdEncoding.EncodeToString(interDER),
		"%s %s body=%q (json err %v)", verb, path, string(body), jerr)

	// ---- 7. An SCT signed over another certificate is refused. ---------
	f.mu.Lock()
	f.sctOver = interDER
	f.mu.Unlock()
	badSCT, err := lc.AddChain(ctx, chain)
	t.Check("add-chain-rejects-an-sct-signed-over-another-certificate",
		err != nil && badSCT == nil,
		"AddChain accepted an SCT signed over a different certificate: err=%v", err)
	f.mu.Lock()
	f.sctOver = leafDER
	f.mu.Unlock()

	// ---- 8. get-sth-consistency sends first and second in decimal. -----
	consistency, err := lc.GetSTHConsistency(ctx, 3, 42)
	f.mu.Lock()
	q := f.lastQuery
	f.mu.Unlock()
	t.Check("get-sth-consistency-passes-first-and-second",
		err == nil && len(consistency) == 1 && bytes.Equal(consistency[0], rootHash) &&
			q["first"] == "3" && q["second"] == "42",
		"err=%v n=%d query=%v", err, len(consistency), q)

	// ---- 9. get-proof-by-hash sends the hash base64-encoded. -----------
	proof, err := lc.GetProofByHash(ctx, rootHash, pinnedTreeSize)
	f.mu.Lock()
	q = f.lastQuery
	f.mu.Unlock()
	t.Check("get-proof-by-hash-passes-hash-and-tree-size",
		err == nil && proof != nil && proof.LeafIndex == pinnedIndex &&
			len(proof.AuditPath) == 1 && bytes.Equal(proof.AuditPath[0], rootHash) &&
			q["hash"] == base64.StdEncoding.EncodeToString(rootHash) &&
			q["tree_size"] == "42",
		"err=%v proof=%v query=%v", err, proof, q)

	// ---- 10. get-roots decodes the anchor list, in order. --------------
	roots, err := lc.GetAcceptedRoots(ctx)
	t.Check("get-roots-decodes-the-anchor-list",
		err == nil && len(roots) == 2 &&
			bytes.Equal(roots[0].Data, rootDER) && bytes.Equal(roots[1].Data, interDER),
		"err=%v n=%d", err, len(roots))

	// ---- 11. get-entries parses the leaf and its issuance chain. -------
	entries, err := lc.GetEntries(ctx, pinnedIndex, pinnedIndex)
	ok = err == nil && len(entries) == 1
	if ok {
		e := entries[0]
		ok = e.Index == pinnedIndex && e.X509Cert != nil &&
			e.X509Cert.SerialNumber != nil && e.X509Cert.SerialNumber.String() == "4097" &&
			e.Leaf.TimestampedEntry != nil &&
			e.Leaf.TimestampedEntry.Timestamp == pinnedTimestamp &&
			len(e.Chain) == 1 && bytes.Equal(e.Chain[0].Data, interDER)
	}
	t.Check("get-entries-parses-the-stored-entry", ok, "err=%v n=%d", err, len(entries))

	// ---- 12. A non-200 answer is an error, not an empty STH. -----------
	bad := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusBadRequest)
		w.Write([]byte(`{"error_message":"rejected"}`))
	}))
	defer bad.Close()
	badClient, err := client.New(bad.URL, bad.Client(), jsonclient.Options{PublicKey: pubPEM})
	if err != nil {
		t.Check("non-200-response-is-an-error", false, "client.New: %v", err)
	} else {
		gotSTH, gotErr := badClient.GetSTH(ctx)
		t.Check("non-200-response-is-an-error", gotErr != nil && gotSTH == nil,
			"GetSTH against a 400 returned sth=%v err=%v", gotSTH, gotErr)
	}

	t.Done()
}

// Group backend-routing-add-chain.
//
// LogMultiConfig exists so that different logs can be served by different
// Trillian backends from one personality process. Whether a candidate has
// really implemented that, rather than parsing the new proto and then talking
// to a single connection, is only observable from the backend side: the
// submission for log "bench-a" must arrive at backend-a's socket and nowhere
// else, carrying backend-a's log id, and likewise for "bench-b".
//
// This group therefore stands up two independent in-process
// trillian.TrillianLogServer instances on two loopback sockets, configures one
// log against each, submits one pinned chain to each log, and reads back what
// each socket actually received.
package main

import (
	"encoding/base64"
	"encoding/json"
	"encoding/pem"
	"net/http"
	"strings"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"

	"ctverify/vhelp"
)

type sctJSON struct {
	SCTVersion uint8  `json:"sct_version"`
	ID         []byte `json:"id"`
	Timestamp  uint64 `json:"timestamp"`
	Extensions string `json:"extensions"`
	Signature  []byte `json:"signature"`
}

// chainBody renders a concatenated-PEM fixture as an RFC 6962 add-chain body.
func chainBody(t *vhelp.T, name string) ([]byte, [][]byte) {
	rest := vhelp.ReadFixture("minted", name)
	var ders [][]byte
	for {
		var blk *pem.Block
		blk, rest = pem.Decode(rest)
		if blk == nil {
			break
		}
		if blk.Type == "CERTIFICATE" {
			ders = append(ders, blk.Bytes)
		}
	}
	if len(ders) == 0 {
		t.Fatalf("fixture minted/%s holds no certificates", name)
	}
	b64 := make([]string, 0, len(ders))
	for _, d := range ders {
		b64 = append(b64, "\""+base64.StdEncoding.EncodeToString(d)+"\"")
	}
	return []byte("{\"chain\":[" + strings.Join(b64, ",") + "]}"), ders
}

func render(ids []int64) string {
	parts := make([]string, 0, len(ids))
	for _, id := range ids {
		parts = append(parts, itoa(id))
	}
	return strings.Join(parts, ",")
}

// leafCertOf returns the DER of the certificate inside a queued MerkleTreeLeaf.
func leafCertOf(t *vhelp.T, raw []byte) []byte {
	if len(raw) == 0 {
		return nil
	}
	var leaf ct.MerkleTreeLeaf
	if _, err := tls.Unmarshal(raw, &leaf); err != nil {
		t.Fatalf("queued leaf is not a MerkleTreeLeaf: %v", err)
	}
	if leaf.TimestampedEntry == nil || leaf.TimestampedEntry.X509Entry == nil {
		return nil
	}
	return leaf.TimestampedEntry.X509Entry.Data
}

func main() {
	t := vhelp.New()

	body, ders := chainBody(t, "chain-valid.pem")
	leafDER := ders[0]

	// The log id both logs sign with: SHA-256 of the pinned log key's DER
	// SubjectPublicKeyInfo, shipped in the corpus in base64.
	idLines := strings.Fields(string(vhelp.ReadFixture("minted", "keys", "log-id.txt")))
	if len(idLines) < 2 {
		t.Fatalf("minted/keys/log-id.txt does not hold the two pinned encodings")
	}
	wantLogIDB64 := idLines[1]

	beA := newBackend(backendA, rootNanosA, rootHashA[:], treeSizeA)
	beB := newBackend(backendB, rootNanosB, rootHashB[:], treeSizeB)
	addrA := beA.serve(t)
	addrB := beB.serve(t)

	multi := "backends {\n" +
		"  backend { name: \"" + backendA + "\" backend_spec: \"" + addrA + "\" }\n" +
		"  backend { name: \"" + backendB + "\" backend_spec: \"" + addrB + "\" }\n}\n" +
		"log_configs {\n" +
		logConfigBody(t, "  ", logIDA, prefixA, backendA, "") +
		logConfigBody(t, "  ", logIDB, prefixB, backendB, "") +
		"}\n"
	h := start(t, writeScratch(t, "multi.textproto", multi), []string{"--log_rpc_server="}, prefixA)
	defer h.stop()

	rA := httpPostJSON(t, h.url(prefixA, "/ct/v1/add-chain"), body)
	rB := httpPostJSON(t, h.url(prefixB, "/ct/v1/add-chain"), body)

	t.Equal("add-chain-to-first-log-is-accepted", rA.status, http.StatusOK)
	t.Equal("add-chain-to-second-log-is-accepted", rB.status, http.StatusOK)

	// Exactly one submission reached each socket: neither backend saw the
	// other log's traffic, and neither saw it twice.
	t.Equal("first-backend-received-exactly-one-submission", beA.queueCount(), 1)
	t.Equal("second-backend-received-exactly-one-submission", beB.queueCount(), 1)

	// And it carried the log id of the log bound to that backend.
	t.Equal("first-backend-saw-only-its-own-log-id", render(beA.queuedLogIDs()), itoa(logIDA))
	t.Equal("second-backend-saw-only-its-own-log-id", render(beB.queuedLogIDs()), itoa(logIDB))

	// The leaf that reached the backend is the certificate that was submitted.
	t.EqualBytes("queued-leaf-holds-the-submitted-certificate", leafCertOf(t, beA.lastLeafValue()), leafDER)

	// The SCT is signed by the personality's own configured key, so its LogID
	// is the pinned hash of that key, on both logs.
	var sA, sB sctJSON
	if err := json.Unmarshal(rA.body, &sA); err != nil {
		t.Check("both-logs-issue-an-sct-under-the-pinned-log-id", false, "first response is not an SCT: %v", err)
	} else if err := json.Unmarshal(rB.body, &sB); err != nil {
		t.Check("both-logs-issue-an-sct-under-the-pinned-log-id", false, "second response is not an SCT: %v", err)
	} else {
		t.Equal("both-logs-issue-an-sct-under-the-pinned-log-id",
			base64.StdEncoding.EncodeToString(sA.ID)+"|"+base64.StdEncoding.EncodeToString(sB.ID),
			wantLogIDB64+"|"+wantLogIDB64)
	}

	t.Done()
}

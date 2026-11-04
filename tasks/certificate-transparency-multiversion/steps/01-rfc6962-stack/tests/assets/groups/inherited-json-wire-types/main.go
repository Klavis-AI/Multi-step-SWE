// Group inherited-json-wire-types.
//
// RFC 6962 s4 defines the JSON bodies of the client API. This group grades the
// root package's request/response structures by marshalling them and reading
// the result back as a generic JSON object: the member names and their values
// are graded, member order is not. It also grades the two custom JSON
// marshallers (DigitallySigned and SHA256Hash both render as base64 strings).
package main

import (
	"encoding/base64"
	"encoding/json"
	"fmt"
	"sort"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"

	"ctverify/vhelp"
)

func obj(t *vhelp.T, v interface{}) map[string]interface{} {
	b, err := json.Marshal(v)
	if err != nil {
		t.Fatalf("json.Marshal(%T): %v", v, err)
	}
	m := map[string]interface{}{}
	if err := json.Unmarshal(b, &m); err != nil {
		t.Fatalf("json.Unmarshal(%T): %v", v, err)
	}
	return m
}

func names(m map[string]interface{}) string {
	k := make([]string, 0, len(m))
	for n := range m {
		k = append(k, n)
	}
	sort.Strings(k)
	out := ""
	for i, n := range k {
		if i > 0 {
			out += ","
		}
		out += n
	}
	return out
}

func main() {
	t := vhelp.New()

	der := []byte{0x30, 0x03, 0x02, 0x01, 0x07}
	derB64 := base64.StdEncoding.EncodeToString(der)

	req := obj(t, ct.AddChainRequest{Chain: [][]byte{der}})
	t.Check("add-chain-request-shape",
		names(req) == "chain" && fmt.Sprint(req["chain"]) == fmt.Sprint([]interface{}{derB64}),
		"names=%q chain=%v", names(req), req["chain"])

	resp := obj(t, ct.AddChainResponse{
		SCTVersion: ct.V1, ID: der, Timestamp: 1365181456089, Extensions: "", Signature: der,
	})
	t.Check("add-chain-response-shape",
		names(resp) == "extensions,id,sct_version,signature,timestamp" &&
			resp["sct_version"] == float64(0) && resp["id"] == derB64 &&
			resp["timestamp"] == float64(1365181456089) &&
			resp["extensions"] == "" && resp["signature"] == derB64,
		"names=%q body=%v", names(resp), resp)

	sth := obj(t, ct.GetSTHResponse{
		TreeSize: 42, Timestamp: 1348589667204, SHA256RootHash: der, TreeHeadSignature: der,
	})
	t.Check("get-sth-response-shape",
		names(sth) == "sha256_root_hash,timestamp,tree_head_signature,tree_size" &&
			sth["tree_size"] == float64(42) && sth["timestamp"] == float64(1348589667204) &&
			sth["sha256_root_hash"] == derB64 && sth["tree_head_signature"] == derB64,
		"names=%q body=%v", names(sth), sth)

	cons := obj(t, ct.GetSTHConsistencyResponse{Consistency: [][]byte{der}})
	t.Equal("get-sth-consistency-response-shape", names(cons), "consistency")

	proof := obj(t, ct.GetProofByHashResponse{LeafIndex: 7, AuditPath: [][]byte{der}})
	t.Check("get-proof-by-hash-response-shape",
		names(proof) == "audit_path,leaf_index" && proof["leaf_index"] == float64(7),
		"names=%q body=%v", names(proof), proof)

	entries := obj(t, ct.GetEntriesResponse{Entries: []ct.LeafEntry{{LeafInput: der, ExtraData: der}}})
	first, _ := json.Marshal(entries["entries"])
	t.Check("get-entries-response-shape",
		names(entries) == "entries" &&
			string(first) == `[{"extra_data":"`+derB64+`","leaf_input":"`+derB64+`"}]`,
		"names=%q entries=%s", names(entries), first)

	roots := obj(t, ct.GetRootsResponse{Certificates: []string{derB64}})
	t.Check("get-roots-response-shape",
		names(roots) == "certificates" && fmt.Sprint(roots["certificates"]) == fmt.Sprint([]interface{}{derB64}),
		"names=%q body=%v", names(roots), roots)

	eap := obj(t, ct.GetEntryAndProofResponse{LeafInput: der, ExtraData: der, AuditPath: [][]byte{der}})
	t.Equal("get-entry-and-proof-response-shape", names(eap), "audit_path,extra_data,leaf_input")

	addJSON := obj(t, ct.AddJSONRequest{Data: map[string]string{"k": "v"}})
	t.Equal("add-json-request-shape", names(addJSON), "data")

	// --- the two custom marshallers -----------------------------------------
	ds := ct.DigitallySigned{
		Algorithm: tls.SignatureAndHashAlgorithm{Hash: tls.SHA256, Signature: tls.ECDSA},
		Signature: []byte{0xca, 0xfe},
	}
	dsJSON, err := json.Marshal(ds)
	if err != nil {
		t.Fatalf("json.Marshal(DigitallySigned): %v", err)
	}
	t.Equal("digitally-signed-marshals-as-base64-string", string(dsJSON), `"`+base64.StdEncoding.EncodeToString([]byte{0x04, 0x03, 0x00, 0x02, 0xca, 0xfe})+`"`)

	var hash ct.SHA256Hash
	for i := range hash {
		hash[i] = byte(i)
	}
	hashJSON, err := json.Marshal(hash)
	if err != nil {
		t.Fatalf("json.Marshal(SHA256Hash): %v", err)
	}
	t.Equal("sha256hash-marshals-as-base64-string", string(hashJSON), `"`+hash.Base64String()+`"`)

	t.Done()
}

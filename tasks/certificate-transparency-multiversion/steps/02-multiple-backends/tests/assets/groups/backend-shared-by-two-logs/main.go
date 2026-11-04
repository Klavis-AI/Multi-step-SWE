// Group backend-shared-by-two-logs.
//
// The other half of the LogBackendSet contract: the binding is many-to-one.
// Several LogConfigs may name the same backend, in which case they share one
// RPC client -- the connection map v1.0.5 builds is keyed by *backend name*,
// not by log -- while still being separate logs with their own prefix and
// their own log id on the wire.
//
// This group binds two logs to one backend and a third log to a second
// backend, then reads each log's tree head and submits to each of the shared
// pair. The pinned answers of the two backends disagree on every field, so
// which socket served a given request is decidable from the response alone,
// and the backends' own request records decide the rest.
package main

import (
	"encoding/base64"
	"encoding/json"
	"encoding/pem"
	"net/http"
	"strings"

	"ctverify/vhelp"
)

// The third log, on the second backend. Pinned like everything else.
const (
	logIDC  = 1003
	prefixC = "bench-c"
)

type sthJSON struct {
	TreeSize          uint64 `json:"tree_size"`
	Timestamp         uint64 `json:"timestamp"`
	SHA256RootHash    []byte `json:"sha256_root_hash"`
	TreeHeadSignature []byte `json:"tree_head_signature"`
}

func sthOf(t *vhelp.T, r httpResult) string {
	if r.status != http.StatusOK {
		return "status " + itoa(int64(r.status))
	}
	var s sthJSON
	if err := json.Unmarshal(r.body, &s); err != nil {
		return "unparseable: " + err.Error()
	}
	return itoa(int64(s.TreeSize)) + "/" + itoa(int64(s.Timestamp)) + "/" +
		base64.StdEncoding.EncodeToString(s.SHA256RootHash)
}

func pinned(size int64, millis uint64, root []byte) string {
	return itoa(size) + "/" + itoa(int64(millis)) + "/" + base64.StdEncoding.EncodeToString(root)
}

func chainBody(t *vhelp.T, name string) []byte {
	rest := vhelp.ReadFixture("minted", name)
	var b64 []string
	for {
		var blk *pem.Block
		blk, rest = pem.Decode(rest)
		if blk == nil {
			break
		}
		if blk.Type == "CERTIFICATE" {
			b64 = append(b64, "\""+base64.StdEncoding.EncodeToString(blk.Bytes)+"\"")
		}
	}
	if len(b64) == 0 {
		t.Fatalf("fixture minted/%s holds no certificates", name)
	}
	return []byte("{\"chain\":[" + strings.Join(b64, ",") + "]}")
}

func main() {
	t := vhelp.New()

	shared := newBackend(backendA, rootNanosA, rootHashA[:], treeSizeA)
	other := newBackend(backendB, rootNanosB, rootHashB[:], treeSizeB)
	addrShared := shared.serve(t)
	addrOther := other.serve(t)

	cfg := "backends {\n" +
		"  backend { name: \"" + backendA + "\" backend_spec: \"" + addrShared + "\" }\n" +
		"  backend { name: \"" + backendB + "\" backend_spec: \"" + addrOther + "\" }\n}\n" +
		"log_configs {\n" +
		logConfigBody(t, "  ", logIDA, prefixA, backendA, "") +
		logConfigBody(t, "  ", logIDB, prefixB, backendA, "") +
		logConfigBody(t, "  ", logIDC, prefixC, backendB, "") +
		"}\n"
	h := start(t, writeScratch(t, "shared.textproto", cfg), []string{"--log_rpc_server="}, prefixA)
	defer h.stop()

	rA := httpGet(t, h.url(prefixA, "/ct/v1/get-sth"))
	rB := httpGet(t, h.url(prefixB, "/ct/v1/get-sth"))
	rC := httpGet(t, h.url(prefixC, "/ct/v1/get-sth"))

	t.Equal("all-three-logs-are-served",
		itoa(int64(rA.status))+"/"+itoa(int64(rB.status))+"/"+itoa(int64(rC.status)),
		"200/200/200")

	// The two logs that name the same backend answer with that backend's
	// pinned root; they are not accidentally reading the other one.
	want := pinned(treeSizeA, rootMillisA, rootHashA[:])
	t.Equal("both-shared-logs-report-the-shared-backends-tree-head",
		sthOf(t, rA)+"|"+sthOf(t, rB), want+"|"+want)

	// The third log names the other backend and gets the other answer.
	t.Equal("third-log-reports-the-other-backends-tree-head",
		sthOf(t, rC), pinned(treeSizeB, rootMillisB, rootHashB[:]))

	// Seen from the sockets: the shared backend was addressed under two
	// distinct log ids, in request order, and never under the third.
	t.Equal("shared-backend-saw-exactly-its-two-log-ids",
		render(shared.rootLogIDs()), itoa(logIDA)+","+itoa(logIDB))
	t.Equal("other-backend-saw-exactly-its-one-log-id",
		render(other.rootLogIDs()), itoa(logIDC))

	// Writes follow the same binding: both submissions land on the one shared
	// connection, carrying the submitting log's own id.
	body := chainBody(t, "chain-valid.pem")
	pA := httpPostJSON(t, h.url(prefixA, "/ct/v1/add-chain"), body)
	pB := httpPostJSON(t, h.url(prefixB, "/ct/v1/add-chain"), body)
	t.Equal("both-shared-logs-accept-a-submission",
		itoa(int64(pA.status))+"/"+itoa(int64(pB.status)), "200/200")
	t.Equal("shared-backend-received-both-submissions",
		render(shared.queuedLogIDs()), itoa(logIDA)+","+itoa(logIDB))
	t.Equal("unaddressed-backend-received-no-submission", other.queueCount(), 0)

	t.Done()
}

// ctfe-logv3-json-endpoint
//
// v1.3.3 adds a ninth entrypoint to the CTFE, served at <prefix>/log.v3.json
// rather than under ct/v1. It exists to satisfy the Chrome CT log policy, and
// it is driven entirely by the new logv3_url config field: set it and the
// endpoint serves a JSON object describing the log; leave it unset and the
// endpoint answers 404 with a message naming the missing field.
//
// Graded over a real loopback socket against the candidate's own ct_server.
// The key and log id in the payload are compared against the pinned fixture
// key the config points at, so nothing here depends on a clock or on the
// backend.
package main

import (
	"crypto"
	"crypto/sha256"
	stdx509 "crypto/x509"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"strings"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/trillian/ctfe"

	"ctverify/vhelp"
)

const (
	v3URL     = "https://logv3.bench.invalid/harbor"
	v3URL3    = "https://logv3-three.bench.invalid/harbor"
	v3URL4    = "https://logv3-four.bench.invalid/harbor"
	v3URL5    = "https://logv3-five.bench.invalid/harbor"
	mmdSec    = 86400
	notAfterS = 1577836800 // 2020-01-01T00:00:00Z
	notAfterL = 2840140800 // 2060-01-01T00:00:00Z
)

// spkiOf derives the DER SubjectPublicKeyInfo of a pinned PKCS#8 private key,
// so a log configured with only a private key can still be compared against
// the exact key bytes the endpoint has to serve. Pure computation over a
// fixture: no clock, no socket.
func spkiOf(t *vhelp.T, pkcs8 []byte) []byte {
	k, err := stdx509.ParsePKCS8PrivateKey(pkcs8)
	if err != nil {
		t.Fatalf("parse pinned private key: %v", err)
	}
	signer, ok := k.(crypto.Signer)
	if !ok {
		t.Fatalf("pinned private key of type %T is not a crypto.Signer", k)
	}
	der, err := stdx509.MarshalPKIXPublicKey(signer.Public())
	if err != nil {
		t.Fatalf("marshal pinned public key: %v", err)
	}
	return der
}

func main() {
	t := vhelp.New()

	logPriv := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-key.pem"))
	intPriv := pemBody(t, vhelp.ReadFixture("minted", "keys", "intermediate-key.pem"))
	rootPriv := pemBody(t, vhelp.ReadFixture("minted", "keys", "root-key.pem"))
	leafPriv := pemBody(t, vhelp.ReadFixture("minted", "keys", "leaf-key.pem"))
	roguePriv := pemBody(t, vhelp.ReadFixture("minted", "keys", "rogue-root-key.pem"))
	logSPKI := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-pubkey.pem"))

	wantKey := base64.StdEncoding.EncodeToString(logSPKI)
	spkiHash := sha256.Sum256(logSPKI)
	wantLogID := base64.StdEncoding.EncodeToString(spkiHash[:])

	be := newStub()
	h := startPersonality(t, be, []logSpec{
		{
			id: pinnedLogID, prefix: logPrefix, privKey: logPriv,
			extraText: fmt.Sprintf("    logv3_url: %q\n    max_merge_delay_sec: %d\n"+
				"    not_after_start: { seconds: %d }\n    not_after_limit: { seconds: %d }\n",
				v3URL, mmdSec, notAfterS, notAfterL),
		},
		// The same release, the same server, a log with no logv3_url.
		{id: pinnedLogID2, prefix: logPrefix2, privKey: intPriv},
		// A log with the URL but no temporal interval at all.
		{
			id: 1003, prefix: "bench3", privKey: rootPriv,
			extraText: fmt.Sprintf("    logv3_url: %q\n    max_merge_delay_sec: %d\n", v3URL3, mmdSec),
		},
		// The two single-bound logs HTTP-API.md 7.1 describes: the handler's
		// condition is `||`, not `&&`, so each of these must still carry the
		// object -- with both keys, the unconfigured one empty.
		{
			id: 1004, prefix: "bench4", privKey: leafPriv,
			extraText: fmt.Sprintf("    logv3_url: %q\n    max_merge_delay_sec: %d\n"+
				"    not_after_start: { seconds: %d }\n", v3URL4, mmdSec, notAfterS),
		},
		{
			id: 1005, prefix: "bench5", privKey: roguePriv,
			extraText: fmt.Sprintf("    logv3_url: %q\n    max_merge_delay_sec: %d\n"+
				"    not_after_limit: { seconds: %d }\n", v3URL5, mmdSec, notAfterL),
		},
	}, nil)
	defer h.stop()

	// 1. Control: the server this group started really is serving this log.
	//    get-roots is backend-free and answers from the configured pool.
	roots := h.get(t, h.baseURL+"/"+logPrefix+"/ct/v1/get-roots")
	var rootsRsp struct {
		Certificates []string `json:"certificates"`
	}
	t.Check("control-server-is-up-and-serving-this-log",
		roots.status == 200 && json.Unmarshal(roots.body, &rootsRsp) == nil && len(rootsRsp.Certificates) == 2,
		"get-roots gave status %d body %s", roots.status, lastBytes(roots.body, 200))

	// 2. The new endpoint is served, as JSON, at <prefix>/log.v3.json.
	v3 := h.get(t, h.baseURL+"/"+logPrefix+"/log.v3.json")
	t.Equal("logv3-json-status-and-content-type",
		fmt.Sprintf("%d %s", v3.status, v3.header.Get("Content-Type")),
		"200 application/json")

	// 3. The payload, byte for byte. The key is the DER SubjectPublicKeyInfo of
	//    the configured signing key in standard base64, the log id is the
	//    base64 SHA-256 of those same bytes, mmd comes from
	//    max_merge_delay_sec and the interval from not_after_start/limit.
	want := `{"key":"` + wantKey + `","log_id":"` + wantLogID + `","mmd":86400,` +
		`"temporal_interval":{"start_inclusive":"2020-01-01T00:00:00Z","end_exclusive":"2060-01-01T00:00:00Z"},` +
		`"url":"` + v3URL + `"}`
	t.Equal("logv3-json-exact-payload", string(v3.body), want)

	// 4. The log id is the full 32-byte SHA-256 of the key, not a truncation.
	var got struct {
		Key   string `json:"key"`
		LogID string `json:"log_id"`
	}
	if err := json.Unmarshal(v3.body, &got); err != nil {
		t.Fatalf("log.v3.json is not JSON: %v", err)
	}
	keyDER, _ := base64.StdEncoding.DecodeString(got.Key)
	idBytes, _ := base64.StdEncoding.DecodeString(got.LogID)
	sum := sha256.Sum256(keyDER)
	t.Check("logv3-json-log-id-is-the-full-sha256-of-the-key",
		len(idBytes) == 32 && string(idBytes) == string(sum[:]),
		"log_id is %d bytes and %s the SHA-256 of key", len(idBytes),
		map[bool]string{true: "is", false: "is not"}[string(idBytes) == string(sum[:])])

	// 5. A log configured with no temporal window omits the member entirely.
	v33 := h.get(t, h.baseURL+"/bench3/log.v3.json")
	t.Check("logv3-json-omits-temporal-interval-when-unconfigured",
		v33.status == 200 &&
			!strings.Contains(string(v33.body), "temporal_interval") &&
			strings.Contains(string(v33.body), `"mmd":86400`) &&
			strings.HasSuffix(string(v33.body), `"url":"`+v3URL3+`"}`),
		"bench3 log.v3.json gave status %d body %s", v33.status, lastBytes(v33.body, 300))

	// 6. A log with no logv3_url does not serve it, and says why.
	v32 := h.get(t, h.baseURL+"/"+logPrefix2+"/log.v3.json")
	t.Equal("logv3-json-404-when-url-is-unset",
		fmt.Sprintf("%d|%s", v32.status, string(v32.body)),
		"404|Not Found\nlogV3JSON: logv3_url is not set in config\n")

	// 7. The entrypoint is GET-only.
	post := h.postJSON(t, h.baseURL+"/"+logPrefix+"/log.v3.json", []byte(`{}`))
	t.Equal("logv3-json-rejects-post",
		fmt.Sprintf("%d|%s", post.status, string(post.body)),
		"405|Method Not Allowed\nmethod not allowed: POST\n")

	// 8. It lives beside ct/v1, not inside it.
	inV1 := h.get(t, h.baseURL+"/"+logPrefix+"/ct/v1/log.v3.json")
	t.Check("logv3-json-is-not-served-under-ct-v1",
		inV1.status == 404,
		"GET %s/ct/v1/log.v3.json gave status %d", logPrefix, inV1.status)

	// 9-10. One bound configured, either one, still emits the object and both
	//       of its keys, with the bound that was not configured sent as the
	//       empty string (HTTP-API.md 7.1). A handler that wrote `&&` where
	//       the release writes `||` omits the member here and passes every
	//       case above.
	v34 := h.get(t, h.baseURL+"/bench4/log.v3.json")
	t.Equal("logv3-json-start-bound-alone-keeps-both-interval-keys",
		fmt.Sprintf("%d|%s", v34.status, intervalOf(t, v34.body)),
		`200|{"start_inclusive":"2020-01-01T00:00:00Z","end_exclusive":""}`)

	v35 := h.get(t, h.baseURL+"/bench5/log.v3.json")
	t.Equal("logv3-json-limit-bound-alone-keeps-both-interval-keys",
		fmt.Sprintf("%d|%s", v35.status, intervalOf(t, v35.body)),
		`200|{"start_inclusive":"","end_exclusive":"2060-01-01T00:00:00Z"}`)

	// 11. Rehomed from gate-endtoend, which used to assert exactly this
	//     payload inside its end-to-end path. Case 5 above only says the
	//     temporal member is absent; this says the whole body of a log with no
	//     temporal window is those four members and nothing else, in the order
	//     the response struct declares them. The key is derived from the
	//     pinned private key bench3 is configured with, so the comparison is
	//     against fixture bytes rather than against the product's own output.
	spki3 := spkiOf(t, rootPriv)
	hash3 := sha256.Sum256(spki3)
	want3 := fmt.Sprintf(`{"key":%q,"log_id":%q,"mmd":%d,"url":%q}`,
		base64.StdEncoding.EncodeToString(spki3),
		base64.StdEncoding.EncodeToString(hash3[:]), mmdSec, v3URL3)
	t.Equal("logv3-json-exact-payload-without-temporal-interval", string(v33.body), want3)

	// 12. Rehomed from build-clean, whose v1.3.3 consumer probe named these
	//     two constants and nothing else did. The path is ct.LogV3JSONPath and
	//     the entrypoint the CTFE registers there is ctfe.LogV3JSONName;
	//     neither identifier exists at v1.2.0. Fetching by the constant rather
	//     than by a literal ties the two together: a product that spells the
	//     constant differently from the route it registers fails here.
	atConst := h.get(t, h.baseURL+"/"+logPrefix+ct.LogV3JSONPath)
	t.Equal("logv3-json-path-and-entrypoint-constants",
		fmt.Sprintf("%s|%s|%d|%t", ct.LogV3JSONPath, ctfe.LogV3JSONName,
			atConst.status, string(atConst.body) == string(v3.body)),
		"/log.v3.json|LogV3JSON|200|true")

	t.Done()
}

// intervalOf lifts the temporal_interval object out of a log.v3.json body
// exactly as it was written, so that a missing member and an empty one are
// told apart, and so is the order of the two keys inside it.
func intervalOf(t *vhelp.T, body []byte) string {
	const key = `"temporal_interval":`
	i := strings.Index(string(body), key)
	if i < 0 {
		return "<no temporal_interval in " + string(lastBytes(body, 300)) + ">"
	}
	rest := string(body)[i+len(key):]
	j := strings.Index(rest, "}")
	if j < 0 {
		return "<unterminated temporal_interval in " + string(lastBytes(body, 300)) + ">"
	}
	return rest[:j+1]
}

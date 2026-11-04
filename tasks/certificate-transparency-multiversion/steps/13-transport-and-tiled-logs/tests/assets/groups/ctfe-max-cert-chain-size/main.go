// ctfe-max-cert-chain-size
//
// v1.3.3 caps the size of an add-chain / add-pre-chain request body. ct_server
// wraps exactly those two handlers in http.MaxBytesHandler with the new
// --max_cert_chain_size flag (default 512000), ParseBodyAsJSONChain recognises
// the resulting *http.MaxBytesError and reports "certificate chain exceeds
// N-byte limit", and addChainInternal turns it into 413 Request Entity Too
// Large instead of the 400 every other body problem gets.
//
// Graded over a real loopback socket against the candidate's own ct_server,
// on two instances: one with an explicit small limit, one left at the
// documented default. The error strings asserted here are HTTP response
// bodies, which is where this corpus allows them. SCOPE-BACKEND: the stub
// backend exists only so the accepted control submission has somewhere to go.
package main

import (
	"encoding/base64"
	"encoding/json"
	"fmt"
	"strings"

	"github.com/google/certificate-transparency-go/x509util"

	"ctverify/vhelp"
)

const smallLimit = 4096

// jsonChainOf returns an add-chain request body whose single chain element is
// n bytes of a fixed filler, so the body size is a pinned function of n.
func jsonChainOf(n int) []byte {
	raw := make([]byte, n)
	for i := range raw {
		raw[i] = byte('A' + i%26)
	}
	return []byte(`{"chain":["` + base64.StdEncoding.EncodeToString(raw) + `"]}`)
}

// tooLarge is the exact body the CTFE serves when the cap fires.
func tooLarge(prefix string, limit int) string {
	return fmt.Sprintf("Request Entity Too Large\n%s: certificate chain exceeds %d-byte limit: http: request body too large\n", prefix, limit)
}

// derList returns the DER of every certificate in a pinned PEM fixture.
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

func jsonInto(b []byte, v interface{}) bool { return json.Unmarshal(b, v) == nil }

func main() {
	t := vhelp.New()

	logPriv := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-key.pem"))
	chain := derList(t, "minted", "chain-valid.pem")

	// ---- instance A: an explicit small cap -------------------------------
	beA := newStub()
	a := startPersonality(t, beA, []logSpec{{id: pinnedLogID, prefix: logPrefix, privKey: logPriv}},
		[]string{fmt.Sprintf("--max_cert_chain_size=%d", smallLimit)})
	defer a.stop()

	// 1. Control: a real chain, comfortably under the cap, is accepted and
	//    issued an SCT. Without this the 413 cases could be satisfied by a log
	//    that rejects everything.
	var body strings.Builder
	body.WriteString(`{"chain":[`)
	for i, der := range chain {
		if i > 0 {
			body.WriteString(",")
		}
		body.WriteString(`"` + base64.StdEncoding.EncodeToString(der) + `"`)
	}
	body.WriteString(`]}`)
	ok := a.postJSON(t, a.url("/ct/v1/add-chain"), []byte(body.String()))
	var sct struct {
		SCTVersion int    `json:"sct_version"`
		ID         string `json:"id"`
		Signature  string `json:"signature"`
	}
	t.Check("control-chain-under-the-cap-is-accepted",
		ok.status == 200 && jsonInto(ok.body, &sct) && sct.ID != "" && sct.Signature != "",
		"add-chain under the cap gave status %d body %s", ok.status, lastBytes(ok.body, 200))

	// 2. A body over the cap is refused with 413 and the exact new message.
	big := a.postJSON(t, a.url("/ct/v1/add-chain"), jsonChainOf(6000))
	t.Equal("add-chain-over-the-cap-is-413",
		fmt.Sprintf("%d|%s", big.status, string(big.body)),
		fmt.Sprintf("%d|%s", 413, tooLarge("bench{1001}", smallLimit)))

	// 3. add-pre-chain is capped by the same flag and reports the same way.
	bigPre := a.postJSON(t, a.url("/ct/v1/add-pre-chain"), jsonChainOf(6000))
	t.Equal("add-pre-chain-over-the-cap-is-413",
		fmt.Sprintf("%d|%s", bigPre.status, string(bigPre.body)),
		fmt.Sprintf("%d|%s", 413, tooLarge("bench{1001}", smallLimit)))

	// 4. A junk body *under* the cap still takes the ordinary 400 path, so the
	//    413 above is the cap firing and not a blanket rejection.
	small := a.postJSON(t, a.url("/ct/v1/add-chain"), jsonChainOf(2000))
	t.Check("junk-body-under-the-cap-is-still-400",
		small.status == 400 && !strings.Contains(string(small.body), "exceeds"),
		"a 2000-byte chain gave status %d body %s", small.status, lastBytes(small.body, 200))

	// 5. Only the two submission endpoints are wrapped: the rest of the log is
	//    untouched and still serving.
	roots := a.get(t, a.baseURL+"/"+logPrefix+"/ct/v1/get-roots")
	t.Check("other-endpoints-are-not-capped",
		roots.status == 200 && strings.Contains(string(roots.body), "certificates"),
		"get-roots on the capped instance gave status %d", roots.status)

	// 6. Rehomed from gate-endtoend, which used to assert this as part of its
	//    end-to-end path. The cap is a property of the request body and not of
	//    what is in it: the very chain case 1 accepted, padded past the cap
	//    with one more element, is refused with the same 413 rather than
	//    parsed and issued an SCT.
	padded := strings.TrimSuffix(body.String(), `]}`) + `,"` + strings.Repeat("QQ==", smallLimit) + `"]}`
	if len(padded) <= smallLimit {
		t.Fatalf("padded body is only %d bytes, cap is %d", len(padded), smallLimit)
	}
	bigValid := a.postJSON(t, a.url("/ct/v1/add-chain"), []byte(padded))
	t.Equal("valid-chain-padded-past-the-cap-is-413",
		fmt.Sprintf("%d|%s", bigValid.status, string(bigValid.body)),
		fmt.Sprintf("%d|%s", 413, tooLarge("bench{1001}", smallLimit)))

	// ---- instance B: the documented default cap --------------------------
	beB := newStub()
	b := startPersonality(t, beB, []logSpec{{id: pinnedLogID, prefix: logPrefix, privKey: logPriv}}, nil)
	defer b.stop()

	// 7. With no flag the cap is 512000 bytes, and the message says so.
	bigB := b.postJSON(t, b.url("/ct/v1/add-chain"), jsonChainOf(600000))
	t.Equal("default-cap-is-512000-bytes",
		fmt.Sprintf("%d|%s", bigB.status, string(bigB.body)),
		fmt.Sprintf("%d|%s", 413, tooLarge("bench{1001}", 512000)))

	// 8. ... and a 300000-byte body is below it.
	smallB := b.postJSON(t, b.url("/ct/v1/add-chain"), jsonChainOf(300000))
	t.Check("body-below-the-default-cap-is-not-413",
		smallB.status == 400 && !strings.Contains(string(smallB.body), "exceeds"),
		"a 300000-byte chain gave status %d body %s", smallB.status, lastBytes(smallB.body, 200))

	t.Done()
}

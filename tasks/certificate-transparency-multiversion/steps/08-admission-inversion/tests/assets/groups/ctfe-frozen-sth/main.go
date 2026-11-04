// Group ctfe-frozen-sth.
//
// frozen_sth is a new LogConfig field at this milestone. A log configured with
// one serves that exact signed tree head from get-sth for ever, without asking
// the backend at all, while still accepting submissions. The STH below is
// pinned: its tree size, timestamp, root hash and signature are constants, and
// the signature verifies under the pinned log key, so the served body is
// byte-exact and contains no clock reading.
//
// SCOPE-BACKEND: no Trillian server runs, and none is needed: the whole point
// of a frozen STH is that get-sth is answered without one. The control case
// shows the opposite - an unfrozen log of the same shape reaches for the
// backend and reports the 503 that its absence produces.
package main

import (
	"encoding/hex"
	"encoding/json"
	"net/http"
	"strings"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/trillian/ctfe/configpb"

	"ctverify/vhelp"
)

const (
	frozenTreeSize    = uint64(4096)
	frozenTimestamp   = uint64(1573689600000)
	frozenRootHashHex = "0617c7c846ca8a3b3f75148b399d0968b1e6201f487667ab53a06e7aeb8bc967"
	frozenSigHex      = "0403004630440220321b76d7186c379feba36102096cfa120c0af8f538099aa554100567d080f2450220390e4dbed6567372b4571b68b1d7531e8f12892e4ba9da2c891abd6b7d0e9646"
	frozenBody        = `{"tree_size":4096,"timestamp":1573689600000,` +
		`"sha256_root_hash":"BhfHyEbKijs/dRSLOZ0JaLHmIB9IdmerU6BueuuLyWc=",` +
		`"tree_head_signature":"BAMARjBEAiAyG3bXGGw3n+ujYQIJbPoSDAr49TgJmqVUEAVn0IDyRQIgOQ5NvtZWc3K0VxtosddTHo8SiS5LqdosiRq9a30OlkY="}`
)

func mustHex(t *vhelp.T, s string) []byte {
	b, err := hex.DecodeString(s)
	if err != nil {
		t.Fatalf("bad pinned hex: %v", err)
	}
	return b
}

func main() {
	t := vhelp.New()

	pub, _, _, err := ct.PublicKeyFromPEM(vhelp.ReadFixture("minted", "keys", "log-pubkey.pem"))
	if err != nil {
		t.Fatalf("parse pinned log public key: %v", err)
	}
	verifier, err := ct.NewSignatureVerifier(pub)
	if err != nil {
		t.Fatalf("build signature verifier: %v", err)
	}

	frozen, err := setUpDead(t, pinnedConfig(t, &configpb.LogConfig{FrozenSth: &configpb.SignedTreeHead{
		TreeSize:          int64(frozenTreeSize),
		Timestamp:         int64(frozenTimestamp),
		Sha256RootHash:    mustHex(t, frozenRootHashHex),
		TreeHeadSignature: mustHex(t, frozenSigHex),
	}}), false)
	if err != nil {
		t.Fatalf("set up frozen instance: %v", err)
	}
	defer frozen.stop()

	r := frozen.get(t, ct.GetSTHPath)
	t.Check("frozen-get-sth-body-exact", r.status == http.StatusOK && r.body == frozenBody,
		"status %d body %q", r.status, r.body)

	// The frozen STH is served without asking the backend anything. This is
	// graded on the log client's own call counter, which is exact: it is zero
	// only if the handler never reached for the backend at all.
	t.Check("frozen-backend-never-asked",
		r.status == http.StatusOK && frozen.backendCalls() == 0,
		"status %d after %d backend calls", r.status, frozen.backendCalls())

	// It is constant: a second request returns the same bytes.
	r2 := frozen.get(t, ct.GetSTHPath)
	t.Equal("frozen-get-sth-repeatable", r2.body, r.body)

	// The served signature verifies under the pinned log key over the served
	// tree size, timestamp and root hash.
	var rsp ct.GetSTHResponse
	if json.Unmarshal([]byte(r.body), &rsp) != nil {
		t.Check("frozen-sth-signature-verifies", false, "body is not a GetSTHResponse: %q", r.body)
	} else if head, err := rsp.ToSignedTreeHead(); err != nil {
		t.Check("frozen-sth-signature-verifies", false, "convert: %v", err)
	} else {
		t.Case("frozen-sth-signature-verifies", verifier.VerifySTHSignature(*head))
	}

	// A frozen log still admits submissions; only its STH is fixed. The chain
	// passes admission and then reaches the absent backend.
	a := frozen.post(t, ct.AddChainPath, chainJSON(t, "chain-valid.pem"))
	t.Check("frozen-log-still-admits-chain", a.status == http.StatusServiceUnavailable,
		"status %d body %q", a.status, a.body)

	// Control: without frozen_sth the same configuration goes to the backend,
	// which is exactly what the frozen case must not do.
	live, err := setUpDead(t, pinnedConfig(t, &configpb.LogConfig{}), false)
	if err != nil {
		t.Fatalf("set up unfrozen instance: %v", err)
	}
	defer live.stop()
	l := live.get(t, ct.GetSTHPath)
	// SCOPE-DIAGNOSTIC: that the backend *was* consulted is graded on the call
	// counter, and the response on the §6.1 mapping of Unavailable to 503 plus
	// the §2.1 framing. The detail text inside that body is grpc-go's own
	// rendering of its status type, which the candidate does not write, so it
	// is not compared.
	t.Check("unfrozen-get-sth-reads-backend",
		l.status == http.StatusServiceUnavailable &&
			live.backendCalls() >= 1 &&
			strings.HasPrefix(l.body, "Service Unavailable\n") &&
			len(l.body) > len("Service Unavailable\n"),
		"status %d backend calls %d body %q", l.status, live.backendCalls(), l.body)

	t.Done()
}

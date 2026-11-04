// Group eku-filter-admission.
//
// v1.1.2 turns a log's ext_key_usages list into an admission filter: a leaf
// whose own EKU extension shares no entry with the configured list is refused
// by add-chain and never reaches the backend. Filtering is leaf-only.
//
// The boundary is the add-chain endpoint of the candidate's own ct_server and
// the RPCs the pinned backend observes. The log below is configured with
// ClientAuth alone, so the three pinned leaves -- one ClientAuth, one
// ServerAuth, one with no EKU extension at all -- split one/two.
//
// v1.1.0 parses the same ext_key_usages list and then ignores it, so all
// three submissions are accepted there.
//
// Cases 5 and 6 are inherited rather than new: they were the gate's
// `add-chain-sct-verifies` and `add-chain-log-id-exact` and moved here at the
// 2026-09-21 re-tier, because a gate group must compile against v1.1.0 too and
// the pinned trillian module -- and so _kit/serverkit.go -- does not survive
// that anchor. They ride on the accepted submission of case 1, so they cost no
// extra request, and they cannot make this group satisfiable by inherited work
// alone: cases 1 to 4 are the v1.1.2 filter.
package main

import (
	"bytes"
	"encoding/hex"
	"encoding/json"
	"net/http"
	"strings"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/ctutil"
	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

func main() {
	t := vhelp.New()

	intermediate := vhelp.ReadFixture("minted", "bench-intermediate.der")

	h := startPersonality(t, nil, "    ext_key_usages: \"ClientAuth\"\n")
	defer h.stop()

	submit := func(leafFile string) (int, []byte) {
		leaf := vhelp.ReadFixture("minted", leafFile)
		body, err := json.Marshal(ct.AddChainRequest{Chain: [][]byte{leaf, intermediate}})
		if err != nil {
			t.Fatalf("marshal add-chain request: %v", err)
		}
		res := h.postJSON(t, h.url(ct.AddChainPath), body)
		return res.status, res.body
	}

	// 1. The leaf whose EKU is in the configured list is logged.
	stClientAuth, acceptedBody := submit("leaf-eku-clientauth.der")
	t.Check("clientauth-leaf-accepted", stClientAuth == http.StatusOK,
		"add-chain returned %d, want 200", stClientAuth)

	// 2. A leaf with a different EKU is refused.
	st, _ := submit("leaf-eku-serverauth.der")
	t.Check("serverauth-leaf-rejected", st == http.StatusBadRequest, "add-chain returned %d, want 400", st)

	// 3. A leaf with no extended key usage extension at all is refused: an
	// absent list is not treated as "anything goes" at the leaf.
	st, _ = submit("leaf-eku-none.der")
	t.Check("no-eku-leaf-rejected", st == http.StatusBadRequest, "add-chain returned %d, want 400", st)

	// 3a. The test is leaf-only: HTTP-API.md §5.4 says an intermediate's EKUs
	// are not consulted. The pinned intermediate rides in all three chains
	// above and carries no extended-key-usage extension at all -- the very
	// shape that gets leaf-eku-none.der refused in case 3 -- yet case 1's
	// chain was admitted. Both halves are checked rather than assumed: the
	// intermediate's DER is scanned for id-ce-extKeyUsage (2.5.29.37), and
	// the accepted status is the one case 1 observed. Nothing about the
	// refusal's text is graded (TEST_INTERFACE.md §3.3); the status and the
	// untouched backend of case 4 are the observation.
	oidExtKeyUsage := []byte{0x06, 0x03, 0x55, 0x1d, 0x25}
	interHasEKU := bytes.Contains(intermediate, oidExtKeyUsage)
	t.Check("eku-filter-is-leaf-only",
		!interHasEKU && stClientAuth == http.StatusOK,
		"intermediate carries an EKU extension=%v, accepted-chain status=%d; want false and 200",
		interHasEKU, stClientAuth)

	// 4. Exactly one leaf was queued, and it is the accepted one. A rejected
	// submission must cost the backend nothing.
	queued, leafValue := h.backend.queueSnapshot()
	if queued != 1 {
		t.Check("rejected-leaves-never-queued", false, "backend saw %d QueueLeaf calls, want 1", queued)
	} else {
		want := vhelp.ReadFixture("minted", "leaf-eku-clientauth.der")
		var mtl ct.MerkleTreeLeaf
		if _, err := tlsUnmarshalLeaf(leafValue, &mtl); err != nil {
			t.Check("rejected-leaves-never-queued", false, "queued leaf undecodable: %v", err)
		} else if mtl.TimestampedEntry == nil || mtl.TimestampedEntry.X509Entry == nil {
			t.Check("rejected-leaves-never-queued", false, "queued leaf carries no X509 entry")
		} else {
			t.EqualBytes("rejected-leaves-never-queued", mtl.TimestampedEntry.X509Entry.Data, want)
		}
	}

	// 5 and 6. What the admitted submission of case 1 came back with: the SCT
	// must verify under the log's own configured key over the chain that was
	// submitted, and must carry the exact pinned log id. Both are inherited
	// obligations (see the header); the key and the log id are fixtures, so
	// nothing here reads a clock or the network.
	logPubPEM := vhelp.ReadFixture("minted", "keys", "log-pubkey.pem")
	wantLogID := strings.Fields(string(vhelp.ReadFixture("minted", "keys", "log-id.txt")))[0]
	pubKey, _, _, err := ct.PublicKeyFromPEM(logPubPEM)
	if err != nil {
		t.Fatalf("parse pinned log public key: %v", err)
	}
	submitted := parseChain(t, vhelp.ReadFixture("minted", "leaf-eku-clientauth.der"), intermediate)

	var acr ct.AddChainResponse
	switch {
	case stClientAuth != http.StatusOK:
		t.Check("add-chain-sct-verifies", false, "add-chain returned %d", stClientAuth)
		t.Check("add-chain-log-id-exact", false, "add-chain returned %d", stClientAuth)
	case json.Unmarshal(acceptedBody, &acr) != nil:
		t.Check("add-chain-sct-verifies", false, "response is not an AddChainResponse")
		t.Check("add-chain-log-id-exact", false, "response is not an AddChainResponse")
	default:
		sct, err := acr.ToSignedCertificateTimestamp()
		if err != nil {
			t.Check("add-chain-sct-verifies", false, "ToSignedCertificateTimestamp: %v", err)
			t.Check("add-chain-log-id-exact", false, "ToSignedCertificateTimestamp: %v", err)
		} else {
			t.Case("add-chain-sct-verifies", ctutil.VerifySCT(pubKey, submitted, sct, false))
			t.Equal("add-chain-log-id-exact", hex.EncodeToString(sct.LogID.KeyID[:]), wantLogID)
		}
	}

	t.Done()
}

// parseChain parses the DER certificates of a submitted chain, tolerating the
// non-fatal parse errors the forked x509 reports for certificates a log must
// still be able to handle.
func parseChain(t *vhelp.T, ders ...[]byte) []*x509.Certificate {
	out := make([]*x509.Certificate, 0, len(ders))
	for i, der := range ders {
		c, err := x509.ParseCertificate(der)
		if x509.IsFatal(err) {
			t.Fatalf("parse submitted certificate %d: %v", i, err)
		}
		out = append(out, c)
	}
	return out
}

func tlsUnmarshalLeaf(b []byte, out *ct.MerkleTreeLeaf) ([]byte, error) {
	return tls.Unmarshal(b, out)
}

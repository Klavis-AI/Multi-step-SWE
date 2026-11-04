// Group inherited-reads-and-quota. KIND=gate: it earns nothing and only vetoes.
//
// The inherited half of the front end, graded at this milestone because
// v1.1.0 rewrites the admission path that sits next to it and nothing here is
// allowed to move. Three earlier milestones are represented:
//
//	step 3 (v1.0.10) the per-log STH signature cache: an unchanged tree head
//	                 is re-served with the byte-identical signature
//	                 (RELEASE-MAP.md §3). ECDSA is randomised, so two equal
//	                 signatures cannot happen by accident.
//	step 5 (v1.0.17) get-proof-by-hash, get-sth-consistency and get-entries
//	                 answered out of the backend (HTTP-API.md §4.2-§4.5).
//	step 6 (v1.0.19) ChargeTo: who each backend call is charged to, in the
//	                 order ARCHITECTURE.md §2.2 fixes -- remote user first,
//	                 then one element per certificate after the leaf.
//
// is_mirror used to be graded here as a fourth, inherited item. It is not
// inherited: v1.0.21 has the config field and the library support, but its
// ct_server refuses the log outright -- setupAndRegister returns "mirrors are
// not supported" and the process exits before it listens -- so a mirror that
// serves reads and 404s both write paths is something v1.1.0 does first. Those
// three cases are graded by the scored group ctserver-mirror-serving.
//
// Everything is observed over a real loopback socket against the candidate's
// own ct_server binary and the pinned in-process backend in serverkit.go
// (SCOPE-BACKEND, SCOPE-NET, SCOPE-TIME as documented there).
package main

import (
	"encoding/base64"
	"encoding/json"
	"net/http"
	"strings"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

func nextPEM(b []byte) ([]byte, []byte) {
	const begin = "-----BEGIN CERTIFICATE-----"
	const end = "-----END CERTIFICATE-----"
	s := string(b)
	i := strings.Index(s, begin)
	if i < 0 {
		return nil, nil
	}
	j := strings.Index(s[i:], end)
	if j < 0 {
		return nil, nil
	}
	der, err := base64.StdEncoding.DecodeString(strings.Join(strings.Fields(s[i+len(begin):i+j]), ""))
	if err != nil {
		return nil, nil
	}
	return der, []byte(s[i+j+len(end):])
}

func chainJSON(t *vhelp.T, name string) string {
	raw := vhelp.ReadFixture("minted", name)
	var ders []string
	rest := raw
	for {
		der, r := nextPEM(rest)
		if der == nil {
			break
		}
		rest = r
		ders = append(ders, base64.StdEncoding.EncodeToString(der))
	}
	if len(ders) == 0 {
		t.Fatalf("fixture %s held no certificate", name)
	}
	b, err := json.Marshal(map[string][]string{"chain": ders})
	if err != nil {
		t.Fatalf("marshal chain: %v", err)
	}
	return string(b)
}

// The identity ARCHITECTURE.md §2.2 and CLIENT-API.md §8.6 require for the one
// certificate that follows the leaf in fixtures/minted/chain-valid.pem. Pinned
// rather than computed, so that the group states the answer instead of asking
// the candidate for it.
const wantIntermediateQuotaUser = "@intermediate CN=Harbor CT Bench Intermediate,O=Harbor CT Bench 2f5b59344c"

func main() {
	t := vhelp.New()

	// --- step 6, the pure function half -----------------------------------
	// QuotaUserForCert is exported and total, so it can be checked directly
	// against the documented three-field format before any server is started.
	intDER := vhelp.ReadFixture("minted", "bench-intermediate.der")
	intCert, err := x509.ParseCertificate(intDER)
	if err != nil {
		t.Fatalf("parse pinned intermediate: %v", err)
	}
	t.Equal("quota-user-for-cert-format", ctfe.QuotaUserForCert(intCert), wantIntermediateQuotaUser)
	t.Equal("certificate-quota-user-prefix", ctfe.CertificateQuotaUserPrefix, "@intermediate")

	// --- the ordinary log --------------------------------------------------
	// No quota flags are passed: ARCHITECTURE.md §2.2 says both derivations
	// default on, and that default is exactly what is being graded.
	h := startPersonality(t, "inh", nil, "")
	defer h.stop()

	// --- step 6, add-chain: remote first, then chain[1:] --------------------
	ac := h.postJSON(t, h.url(ct.AddChainPath), chainJSON(t, "chain-valid.pem"))
	charge, present := h.backend.chargeFor("QueueLeaves")
	t.Check("add-chain-charged-remote-then-chain-tail",
		ac.status == http.StatusOK && present && len(charge) == 2 &&
			charge[0] == "127.0.0.1" && charge[1] == wantIntermediateQuotaUser,
		"status %d chargeTo present=%v %q", ac.status, present, charge)

	// --- step 3, the STH signature cache ------------------------------------
	// The backend's tree head is a constant, so the serialised input to the
	// signer is identical across the two calls and the cached signature must
	// come back unchanged. Nothing in the response says whether a cache was
	// used; only the bytes do.
	sig1, ok1 := sthSignature(t, h)
	sig2, ok2 := sthSignature(t, h)
	t.Check("sth-signature-stable-for-unchanged-tree",
		ok1 && ok2 && len(sig1) > 0 && sig1 == sig2,
		"first %q second %q", sig1, sig2)

	// --- step 6, reads are charged too, to the remote user only -------------
	rc, rpresent := h.backend.chargeFor("GetLatestSignedLogRoot")
	t.Check("read-charged-to-remote-user-only",
		rpresent && len(rc) == 1 && rc[0] == "127.0.0.1",
		"chargeTo present=%v %q", rpresent, rc)

	// A proxied request is charged to the client in front of the proxy, and
	// private-range hops in the list are skipped.
	fw := h.getWithHeaders(t, h.url(ct.GetSTHPath), map[string]string{
		"X-Forwarded-For": "203.0.113.9, 10.0.0.1",
	})
	fc, fpresent := h.backend.chargeFor("GetLatestSignedLogRoot")
	t.Check("forwarded-for-charges-the-client-not-the-proxy",
		fw.status == http.StatusOK && fpresent && len(fc) == 1 && fc[0] == "203.0.113.9",
		"status %d chargeTo present=%v %q", fw.status, fpresent, fc)

	// get-roots touches no backend at all, so it charges nobody.
	beforeQ, beforeS := h.backend.counts()
	gr := h.get(t, h.url(ct.GetRootsPath))
	afterQ, afterS := h.backend.counts()
	t.Check("get-roots-touches-no-backend",
		gr.status == http.StatusOK && afterQ == beforeQ && afterS == beforeS,
		"status %d queue %d->%d sth %d->%d", gr.status, beforeQ, afterQ, beforeS, afterS)

	// --- step 5, the three read endpoints -----------------------------------
	leafHash := base64.StdEncoding.EncodeToString(make([]byte, 32))
	pb := h.get(t, h.url(ct.GetProofByHashPath)+"?hash="+
		strings.ReplaceAll(strings.ReplaceAll(leafHash, "+", "%2B"), "=", "%3D")+"&tree_size=100")
	var pbRsp ct.GetProofByHashResponse
	if pb.status != http.StatusOK || json.Unmarshal(pb.body, &pbRsp) != nil {
		t.Check("get-proof-by-hash-shape", false, "status %d body %q", pb.status, string(pb.body))
	} else {
		t.Check("get-proof-by-hash-shape",
			pbRsp.LeafIndex == 7 && len(pbRsp.AuditPath) == 1 &&
				base64.StdEncoding.EncodeToString(pbRsp.AuditPath[0]) ==
					base64.StdEncoding.EncodeToString(pinnedRootHash[:]),
			"leaf_index %d audit_path %v", pbRsp.LeafIndex, pbRsp.AuditPath)
	}

	cs := h.get(t, h.url(ct.GetSTHConsistencyPath)+"?first=1&second=2")
	var csRsp ct.GetSTHConsistencyResponse
	if cs.status != http.StatusOK || json.Unmarshal(cs.body, &csRsp) != nil {
		t.Check("get-sth-consistency-shape", false, "status %d body %q", cs.status, string(cs.body))
	} else {
		t.Check("get-sth-consistency-shape",
			len(csRsp.Consistency) == 1 &&
				base64.StdEncoding.EncodeToString(csRsp.Consistency[0]) ==
					base64.StdEncoding.EncodeToString(pinnedRootHash[:]),
			"consistency %v", csRsp.Consistency)
	}

	ge := h.get(t, h.url(ct.GetEntriesPath)+"?start=0&end=0")
	var geRsp ct.GetEntriesResponse
	if ge.status != http.StatusOK || json.Unmarshal(ge.body, &geRsp) != nil {
		t.Check("get-entries-shape", false, "status %d body %q", ge.status, string(ge.body))
	} else {
		t.Check("get-entries-shape",
			len(geRsp.Entries) == 1 &&
				base64.StdEncoding.EncodeToString(geRsp.Entries[0].LeafInput) ==
					base64.StdEncoding.EncodeToString(h.backend.lastLeafValue()) &&
				string(geRsp.Entries[0].ExtraData) == "pinned-extra-data",
			"entries %d", len(geRsp.Entries))
	}

	// A range the front end can reject on its own never reaches the backend.
	beforeLeaves := h.backend.leafCallCount()
	bad := h.get(t, h.url(ct.GetEntriesPath)+"?start=9&end=4")
	t.Check("get-entries-bad-range-refused-locally",
		bad.status == http.StatusBadRequest && h.backend.leafCallCount() == beforeLeaves,
		"status %d leaf calls %d->%d", bad.status, beforeLeaves, h.backend.leafCallCount())

	t.Done()
}

// sthSignature fetches get-sth and returns the base64 tree_head_signature.
func sthSignature(t *vhelp.T, h *harness) (string, bool) {
	r := h.get(t, h.url(ct.GetSTHPath))
	if r.status != http.StatusOK {
		return "", false
	}
	var rsp ct.GetSTHResponse
	if err := json.Unmarshal(r.body, &rsp); err != nil {
		return "", false
	}
	return base64.StdEncoding.EncodeToString(rsp.TreeHeadSignature), true
}

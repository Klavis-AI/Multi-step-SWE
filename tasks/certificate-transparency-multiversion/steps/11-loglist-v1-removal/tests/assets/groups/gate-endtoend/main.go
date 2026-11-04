// Group gate-endtoend.
//
// The inherited end-to-end path of this milestone, plus proof that the
// artifact starts: the candidate's own ct_server binary, started as a
// subprocess, talking RFC 6962 over a real loopback socket to a pinned
// in-process Trillian backend (SCOPE-BACKEND).
//
// It is a GATE group. It earns nothing and only vetoes, so every case below
// is behaviour v1.1.3 already had and v1.1.4 keeps, and the group is required
// to pass against both trees. Nothing here touches this milestone's own
// capability: no log list of any version is read, no v3 type is named, and --
// see below -- no klog-only flag appears on the command line (gate rule 3).
//
// SCOPE-BACKEND: the pinned trillian module does move between the two anchors
// (v1.4.1 -> v1.5.1-0.20220819043421-0a389c4bb8d9), which elsewhere in this
// corpus is what forces a gate driver off the shared stub. It does not here:
// the kit's stubBackend satisfies trillian.TrillianLogServer at both pins, so
// the driver compiles and the whole submit/read path below is observable at
// both anchors. That was measured, not assumed.
//
// SCOPE-FLAGS: an earlier revision started the personality with --one_output,
// a k8s.io/klog/v2 flag, on the reasoning that v1.1.4 is the release that
// swaps glog for klog. That is precisely this step's new capability, and it
// made the whole group fail to get off the ground at v1.1.3 -- a rule-3
// migration sitting in the gate, which would have made the Oracle fail its
// own step. The flag is gone from here; the klog grammar is graded where it
// belongs, in the scored group glog-to-klog-flag-migration (which asserts
// ct_server's exact flag set alongside klog's) and in
// submission-proxy-server-v3.
//
// SCOPE-TIME: every timestamp compared below is a pinned constant -- the
// stub's root timestamp and the leaf timestamps it derives from it. No
// elapsed time, deadline or wall clock is asserted; the readiness poll in the
// kit is a fatal prerequisite, not a case.
//
// Everything asserted here is exact: an exact body, an exact set of root
// DERs, an exact leaf encoding, an exact tree size and root hash, and
// signatures that verify against the pinned log key.
package main

import (
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"net/http"
	"sort"
	"strings"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/ctutil"
	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

func parsePEMChain(t *vhelp.T, pemBytes []byte) []*x509.Certificate {
	var out []*x509.Certificate
	rest := pemBytes
	for {
		var der []byte
		der, rest = nextPEMBlock(rest)
		if der == nil {
			break
		}
		c, err := x509.ParseCertificate(der)
		if err != nil && !x509.IsFatal(err) {
			err = nil
		}
		if err != nil {
			t.Fatalf("parse certificate: %v", err)
		}
		out = append(out, c)
	}
	return out
}

// nextPEMBlock is a tiny PEM reader; encoding/pem is stdlib but this keeps the
// DER bytes byte-identical to the fixture without a dependency on block types.
func nextPEMBlock(b []byte) ([]byte, []byte) {
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
	body := s[i+len(begin) : i+j]
	der, err := base64Decode(body)
	if err != nil {
		return nil, nil
	}
	return der, []byte(s[i+j+len(end):])
}

func main() {
	t := vhelp.New()

	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	rootDER := vhelp.ReadFixture("minted", "bench-root.der")
	intDER := vhelp.ReadFixture("minted", "bench-intermediate.der")
	chainPEM := vhelp.ReadFixture("minted", "chain-valid.pem")
	logPubPEM := vhelp.ReadFixture("minted", "keys", "log-pubkey.pem")
	wantLogID := strings.Fields(string(vhelp.ReadFixture("minted", "keys", "log-id.txt")))[0]

	pubKey, _, _, err := ct.PublicKeyFromPEM(logPubPEM)
	if err != nil {
		t.Fatalf("parse pinned log public key: %v", err)
	}

	// --max_get_entries pins the alignment boundary to a small, exactly
	// predictable value. It is the only extra flag: it is spelled the same way
	// under glog and under klog, so it says nothing about which of the two the
	// personality was built on.
	h := startPersonality(t, []string{"--max_get_entries=10"}, "")
	defer h.stop()

	// 1. /healthz answers exactly "ok" with 200.
	hz := h.get(t, h.baseURL+"/healthz")
	t.Check("healthz-exact", hz.status == http.StatusOK && string(hz.body) == "ok",
		"status %d body %q", hz.status, string(hz.body))

	// 2. get-roots returns exactly the two configured roots, byte for byte.
	gr := h.get(t, h.url(ct.GetRootsPath))
	var roots ct.GetRootsResponse
	if gr.status != http.StatusOK {
		t.Check("get-roots-exact", false, "status %d body %s", gr.status, trunc(gr.body))
	} else if err := json.Unmarshal(gr.body, &roots); err != nil {
		t.Check("get-roots-exact", false, "unmarshal: %v", err)
	} else {
		got := make([]string, 0, len(roots.Certificates))
		for _, c := range roots.Certificates {
			der, err := base64Decode(c)
			if err != nil {
				t.Fatalf("root not base64: %v", err)
			}
			got = append(got, hex.EncodeToString(sha256Sum(der)))
		}
		want := []string{hex.EncodeToString(sha256Sum(rootDER)), hex.EncodeToString(sha256Sum(intDER))}
		sort.Strings(got)
		sort.Strings(want)
		t.Equal("get-roots-exact", strings.Join(got, ","), strings.Join(want, ","))
	}

	// 3. add-chain issues an SCT that verifies against the pinned log key.
	chain := parsePEMChain(t, chainPEM)
	if len(chain) != 2 {
		t.Fatalf("pinned chain-valid.pem holds %d certificates, want 2", len(chain))
	}
	req := ct.AddChainRequest{Chain: [][]byte{chain[0].Raw, chain[1].Raw}}
	body, err := json.Marshal(req)
	if err != nil {
		t.Fatalf("marshal add-chain: %v", err)
	}
	ac := h.postJSON(t, h.url(ct.AddChainPath), body)
	var acr ct.AddChainResponse
	if ac.status != http.StatusOK {
		t.Check("add-chain-sct-verifies", false, "status %d body %s", ac.status, trunc(ac.body))
		t.Check("add-chain-log-id-exact", false, "add-chain returned %d", ac.status)
	} else if err := json.Unmarshal(ac.body, &acr); err != nil {
		t.Check("add-chain-sct-verifies", false, "unmarshal: %v", err)
		t.Check("add-chain-log-id-exact", false, "unmarshal: %v", err)
	} else {
		sct, err := acr.ToSignedCertificateTimestamp()
		if err != nil {
			t.Check("add-chain-sct-verifies", false, "ToSignedCertificateTimestamp: %v", err)
			t.Check("add-chain-log-id-exact", false, "ToSignedCertificateTimestamp: %v", err)
		} else {
			t.Case("add-chain-sct-verifies", ctutil.VerifySCT(pubKey, chain, sct, false))
			t.Equal("add-chain-log-id-exact", hex.EncodeToString(sct.LogID.KeyID[:]), wantLogID)
		}
	}

	// 4. The backend saw exactly one QueueLeaf carrying the submitted leaf.
	qc, queued := h.backend.queueSnapshot()
	if qc != 1 {
		t.Check("queue-leaf-observed", false, "backend saw %d QueueLeaf calls, want 1", qc)
	} else {
		var mtl ct.MerkleTreeLeaf
		if _, err := tls.Unmarshal(queued, &mtl); err != nil {
			t.Check("queue-leaf-observed", false, "queued leaf is not a MerkleTreeLeaf: %v", err)
		} else if mtl.TimestampedEntry == nil || mtl.TimestampedEntry.X509Entry == nil {
			t.Check("queue-leaf-observed", false, "queued leaf carries no X509 entry")
		} else {
			t.EqualBytes("queue-leaf-observed", mtl.TimestampedEntry.X509Entry.Data, leafDER)
		}
	}

	// 5. get-sth reports the pinned root and signs it.
	gs := h.get(t, h.url(ct.GetSTHPath))
	var sthRsp ct.GetSTHResponse
	if gs.status != http.StatusOK {
		t.Check("get-sth-matches-pinned-root", false, "status %d body %s", gs.status, trunc(gs.body))
	} else if err := json.Unmarshal(gs.body, &sthRsp); err != nil {
		t.Check("get-sth-matches-pinned-root", false, "unmarshal: %v", err)
	} else {
		sth, err := sthRsp.ToSignedTreeHead()
		if err != nil {
			t.Check("get-sth-matches-pinned-root", false, "ToSignedTreeHead: %v", err)
		} else {
			sv, err := ct.NewSignatureVerifier(pubKey)
			if err != nil {
				t.Fatalf("NewSignatureVerifier: %v", err)
			}
			verr := sv.VerifySTHSignature(*sth)
			ok := sth.TreeSize == pinnedTreeSize &&
				sth.Timestamp == pinnedTimestampMS &&
				hex.EncodeToString(sth.SHA256RootHash[:]) == hex.EncodeToString(pinnedRootHash[:]) &&
				verr == nil
			t.Check("get-sth-matches-pinned-root", ok,
				"size=%d ts=%d root=%x sigerr=%v", sth.TreeSize, sth.Timestamp, sth.SHA256RootHash[:], verr)
		}
	}

	// 6. get-entries returns the pinned leaves byte for byte.
	ge := h.get(t, h.url(ct.GetEntriesPath)+"?start=3&end=5")
	var entries ct.GetEntriesResponse
	if ge.status != http.StatusOK {
		t.Check("get-entries-exact-leaves", false, "status %d body %s", ge.status, trunc(ge.body))
	} else if err := json.Unmarshal(ge.body, &entries); err != nil {
		t.Check("get-entries-exact-leaves", false, "unmarshal: %v", err)
	} else if len(entries.Entries) != 3 {
		t.Check("get-entries-exact-leaves", false, "got %d entries, want 3", len(entries.Entries))
	} else {
		bad := ""
		for i, e := range entries.Entries {
			want := pinnedLeafValue(leafDER, int64(3+i))
			if hex.EncodeToString(e.LeafInput) != hex.EncodeToString(want) {
				bad += fmt.Sprintf("entry %d leaf_input mismatch; ", i)
			}
			if hex.EncodeToString(e.ExtraData) != "000000" {
				bad += fmt.Sprintf("entry %d extra_data=%x; ", i, e.ExtraData)
			}
		}
		t.Check("get-entries-exact-leaves", bad == "", "%s", bad)
	}

	// 7. A maximally sized get-entries request is coerced down to the next
	// multiple of the maximum batch size, so every caller converges on the same
	// cachable ranges. start=5, end=1004, max=10: the range is first clipped to
	// [5,14] and then truncated to [5,9].
	al := h.get(t, h.url(ct.GetEntriesPath)+"?start=5&end=1004")
	var aligned ct.GetEntriesResponse
	if al.status != http.StatusOK {
		t.Check("get-entries-aligned-to-boundary", false, "status %d body %s", al.status, trunc(al.body))
	} else if err := json.Unmarshal(al.body, &aligned); err != nil {
		t.Check("get-entries-aligned-to-boundary", false, "unmarshal: %v", err)
	} else {
		_, gotStart, gotCount := h.backend.snapshot()
		t.Check("get-entries-aligned-to-boundary",
			len(aligned.Entries) == 5 && gotStart == 5 && gotCount == 5,
			"entries=%d backend asked for start=%d count=%d, want 5 entries and [5,+5)",
			len(aligned.Entries), gotStart, gotCount)
	}

	t.Done()
}

func sha256Sum(b []byte) []byte {
	s := sha256.Sum256(b)
	return s[:]
}

func trunc(b []byte) string {
	s := strings.ReplaceAll(string(b), "\n", " ")
	if len(s) > 200 {
		s = s[:200]
	}
	return s
}

// base64Decode accepts the standard base64 the RFC 6962 JSON uses, ignoring
// the line breaks PEM introduces.
func base64Decode(s string) ([]byte, error) {
	clean := strings.Map(func(r rune) rune {
		if r == '\n' || r == '\r' || r == ' ' || r == '\t' {
			return -1
		}
		return r
	}, s)
	return base64.StdEncoding.DecodeString(clean)
}

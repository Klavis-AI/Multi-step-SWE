// Group gate-endtoend.
//
// The inherited end-to-end path of this milestone, plus proof that the
// artifact starts: the candidate's own ct_server binary, started as a
// subprocess, serving a pinned CT log over a real loopback socket in front of
// a pinned Trillian stub (SCOPE-BACKEND). It is a GATE group. It earns nothing
// and only vetoes, so everything below is behaviour v1.1.2 already had and
// v1.1.3 keeps, and it must pass against both anchors.
//
// Nothing here touches this milestone's own capability (gate rule 3). Until
// the 2026-09-21 tier audit this driver also brought up the candidate's
// `witness` and `feeder` and graded seven cases over them. Neither binary
// exists at v1.1.2, so those cases made a no-op continuation for step 10 fail
// the gate and measure nothing. They now live, verbatim, in the scored group
// `witness-feeder-endtoend`. Nothing was deleted.
//
// SCOPE-NET: loopback only. The benchmark's own listeners bind :0 and serve on
// the listener the kernel hands back; ct_server's port has to be named on its
// command line before the process exists, so that one is probed for below the
// kernel's ephemeral range instead.
// SCOPE-TIME: no elapsed time, deadline or timestamp is asserted -- the SCT's
// own timestamp is only ever fed back into the leaf comparison, never read as
// a value -- and the readiness poll is a fatal prerequisite, not a case.
package main

import (
	"crypto/ecdsa"
	"crypto/sha256"
	"crypto/x509"
	"encoding/hex"
	"encoding/json"
	"encoding/pem"
	"fmt"
	"net"
	"net/http"
	"strings"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/ctutil"
	"github.com/google/certificate-transparency-go/tls"
	ctx509 "github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

// verifyLogSTH checks an RFC 6962 tree-head signature with the standard
// library: SHA-256 over the TLS-encoded TreeHeadSignature, ECDSA under the
// pinned log key. Only the serialisation is the product's.
func verifyLogSTH(t *vhelp.T, pub *ecdsa.PublicKey, sth *ct.SignedTreeHead) error {
	input, err := tls.Marshal(ct.TreeHeadSignature{
		Version:        sth.Version,
		SignatureType:  ct.TreeHashSignatureType,
		Timestamp:      sth.Timestamp,
		TreeSize:       sth.TreeSize,
		SHA256RootHash: sth.SHA256RootHash,
	})
	if err != nil {
		return fmt.Errorf("marshal TreeHeadSignature: %v", err)
	}
	digest := sha256.Sum256(input)
	if !ecdsa.VerifyASN1(pub, digest[:], sth.TreeHeadSignature.Signature) {
		return fmt.Errorf("tree-head signature does not verify under the pinned log key")
	}
	return nil
}

func parsePEMChain(t *vhelp.T, b []byte) []*ctx509.Certificate {
	var out []*ctx509.Certificate
	rest := b
	for {
		var blk *pem.Block
		blk, rest = pem.Decode(rest)
		if blk == nil {
			return out
		}
		c, err := ctx509.ParseCertificate(blk.Bytes)
		if err != nil && ctx509.IsFatal(err) {
			t.Fatalf("parse pinned certificate: %v", err)
		}
		out = append(out, c)
	}
}

func main() {
	t := vhelp.New()

	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	logPubPEM := vhelp.ReadFixture("minted", "keys", "log-pubkey.pem")
	logPubDER := pemBody(t, logPubPEM)
	wantLogID := strings.Fields(string(vhelp.ReadFixture("minted", "keys", "log-id.txt")))[0]

	stdPub, err := x509.ParsePKIXPublicKey(logPubDER)
	if err != nil {
		t.Fatalf("parse pinned log public key: %v", err)
	}
	logPub, ok := stdPub.(*ecdsa.PublicKey)
	if !ok {
		t.Fatalf("pinned log key is %T, want *ecdsa.PublicKey", stdPub)
	}
	ctPub, _, _, err := ct.PublicKeyFromPEM(logPubPEM)
	if err != nil {
		t.Fatalf("parse pinned log public key for SCT check: %v", err)
	}

	// --- the log -----------------------------------------------------------
	p := startPersonality(t)
	defer p.stop()

	// 1. /healthz answers exactly "ok" with 200.
	hz := httpGet(t, p.baseURL+"/healthz")
	t.Check("personality-healthz-exact", hz.status == http.StatusOK && string(hz.body) == "ok",
		"status %d body %q", hz.status, string(hz.body))

	// 2. get-sth reports exactly the pinned tree head.
	gs := httpGet(t, p.url(ct.GetSTHPath))
	if gs.status != http.StatusOK {
		t.Fatalf("get-sth returned %d: %s", gs.status, trunc(gs.body))
	}
	var sthRsp ct.GetSTHResponse
	if err := json.Unmarshal(gs.body, &sthRsp); err != nil {
		t.Fatalf("get-sth is not a GetSTHResponse: %v", err)
	}
	logSTH, err := sthRsp.ToSignedTreeHead()
	if err != nil {
		t.Fatalf("get-sth does not parse as an STH: %v", err)
	}
	t.Equal("personality-get-sth-exact-tree-head",
		fmt.Sprintf("%d %d %x", logSTH.TreeSize, logSTH.Timestamp, logSTH.SHA256RootHash[:]),
		fmt.Sprintf("%d %d %x", pinnedTreeSize, pinnedTimestampMS, pinnedRootHash[:]))

	// 3. and signs it with the pinned log key.
	t.Case("personality-get-sth-signature-verifies", verifyLogSTH(t, logPub, logSTH))

	// 4. add-chain issues an SCT that verifies against the same key, under the
	//    log ID the corpus pinned.
	chain := parsePEMChain(t, vhelp.ReadFixture("minted", "chain-valid.pem"))
	if len(chain) != 2 {
		t.Fatalf("pinned chain-valid.pem holds %d certificates, want 2", len(chain))
	}
	reqBody, err := json.Marshal(ct.AddChainRequest{Chain: [][]byte{chain[0].Raw, chain[1].Raw}})
	if err != nil {
		t.Fatalf("marshal add-chain request: %v", err)
	}
	ac := httpPostJSON(t, p.url(ct.AddChainPath), reqBody)
	if ac.status != http.StatusOK {
		t.Fatalf("add-chain returned %d: %s", ac.status, trunc(ac.body))
	}
	var acr ct.AddChainResponse
	if err := json.Unmarshal(ac.body, &acr); err != nil {
		t.Fatalf("add-chain is not an AddChainResponse: %v", err)
	}
	sct, err := acr.ToSignedCertificateTimestamp()
	if err != nil {
		t.Fatalf("add-chain response does not parse as an SCT: %v", err)
	}
	t.Case("personality-add-chain-sct-verifies", ctutil.VerifySCT(ctPub, chain, sct, false))
	t.Equal("personality-add-chain-log-id-exact", hex.EncodeToString(sct.LogID.KeyID[:]), wantLogID)

	// 5. The backend saw exactly one QueueLeaf, carrying the submitted leaf.
	if n := p.backend.count("QueueLeaf"); n != 1 {
		t.Check("personality-queued-exactly-the-submitted-leaf", false,
			"backend saw %d QueueLeaf calls, want 1", n)
	} else {
		var mtl ct.MerkleTreeLeaf
		if _, err := tls.Unmarshal(p.backend.queuedLeaf(), &mtl); err != nil {
			t.Check("personality-queued-exactly-the-submitted-leaf", false,
				"queued leaf is not a MerkleTreeLeaf: %v", err)
		} else if mtl.TimestampedEntry == nil || mtl.TimestampedEntry.X509Entry == nil {
			t.Check("personality-queued-exactly-the-submitted-leaf", false,
				"queued leaf carries no X509 entry")
		} else {
			t.EqualBytes("personality-queued-exactly-the-submitted-leaf",
				mtl.TimestampedEntry.X509Entry.Data, leafDER)
		}
	}

	// 6. get-entries returns the pinned leaves byte for byte.
	ge := httpGet(t, p.url(ct.GetEntriesPath)+"?start=3&end=5")
	var entries ct.GetEntriesResponse
	if ge.status != http.StatusOK {
		t.Check("personality-get-entries-exact-leaves", false, "status %d body %s", ge.status, trunc(ge.body))
	} else if err := json.Unmarshal(ge.body, &entries); err != nil {
		t.Check("personality-get-entries-exact-leaves", false, "unmarshal: %v", err)
	} else if len(entries.Entries) != 3 {
		t.Check("personality-get-entries-exact-leaves", false, "got %d entries, want 3", len(entries.Entries))
	} else {
		bad := ""
		for i, e := range entries.Entries {
			if hex.EncodeToString(e.LeafInput) != hex.EncodeToString(pinnedLeafValue(leafDER, int64(3+i))) {
				bad += fmt.Sprintf("entry %d leaf_input mismatch; ", i)
			}
			if hex.EncodeToString(e.ExtraData) != hex.EncodeToString(pinnedExtraData) {
				bad += fmt.Sprintf("entry %d extra_data=%x; ", i, e.ExtraData)
			}
		}
		t.Check("personality-get-entries-exact-leaves", bad == "", "%s", bad)
	}

	t.Done()
}

// --- helpers the personality bring-up needs ------------------------------
// These three lived in witnesskit.go, which this group no longer carries.

func pemBody(t *vhelp.T, b []byte) []byte {
	blk, _ := pem.Decode(b)
	if blk == nil {
		t.Fatalf("fixture is not PEM")
	}
	return blk.Bytes
}

func freeLoopbackPort(t *vhelp.T) int {
	// Ports are taken from BELOW the kernel's ephemeral range (32768+ here) so
	// that the kernel cannot hand this port to an unrelated process in the
	// window between the probe closing and the child binding it. That window
	// is the one way a loopback harness can fail under concurrent load, and a
	// gate group that fails under load scores the whole step zero for a
	// correct solution. The starting slot is dispersed by a single throwaway
	// ephemeral bind so sibling probes in one run do not converge; a slot a
	// sibling has already claimed is held by its live listener and so is
	// visibly busy here.
	seed, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen 127.0.0.1:0: %v", err)
	}
	base := seed.Addr().(*net.TCPAddr).Port % 12000
	if err := seed.Close(); err != nil {
		t.Fatalf("close probe listener: %v", err)
	}
	for i := 0; i < 512; i++ {
		p := 20000 + (base+i)%12000
		l, err := net.Listen("tcp", fmt.Sprintf("127.0.0.1:%d", p))
		if err != nil {
			continue
		}
		if err := l.Close(); err != nil {
			t.Fatalf("close probe listener: %v", err)
		}
		return p
	}
	t.Fatalf("no free loopback port in 20000-31999 after 512 probes")
	return 0
}

func lastBytes(b []byte, n int) string {
	if len(b) > n {
		b = b[len(b)-n:]
	}
	return strings.ReplaceAll(string(b), "\n", " | ")
}

func trunc(b []byte) string {
	s := strings.ReplaceAll(string(b), "\n", " ")
	if len(s) > 200 {
		s = s[:200]
	}
	return s
}

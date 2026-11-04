// Group gate-endtoend -- KIND gate (the inherited end-to-end path).
//
// One deterministic end-to-end path through the milestone step 03 inherits.
// The candidate's own ct_server is driven over a real loopback socket against
// this group's own pinned trillian stub, and the whole inherited lifecycle is
// walked in one process: the configured root pool is served, a valid chain is
// admitted and comes back as an SCT that verifies under the pinned log key
// over the RFC 6962 signature input, an unrooted chain is refused before the
// backend is asked anything, and the signed tree head is served and verifies
// -- once by hand and once through step 01's client library.
//
// Every case below was already true of the step-02 tree, which is the only
// thing a gate group may assert: it earns nothing and only vetoes, so a case
// this milestone introduces would zero every candidate that merely has not
// finished the milestone, and would make this step's own no-op continuation
// report a failed gate instead of a zero score.
//
// What is deliberately NOT here (gate rule 4, "nothing in the gate may be
// satisfiable by the new capability alone"): the signature cache itself --
// two get-sth calls over an unchanged backend root returning byte-identical
// tree_head_signature bytes. That is this release's headline new capability,
// it is exactly what the v1.0.7 advisory is about, and it is graded and
// priced by the scored groups sth-signature-cache-over-http (whose
// repeated-get-sth-identical-r0 makes the same observation through the same
// running ct_server), sth-signature-cache-stable,
// sth-signature-cache-distinct-inputs, sth-signature-cache-not-shared-
// between-logs and sth-signature-cache-invisible-in-response. It was also the
// one assertion here that a correct step-02 tree fails, because ECDSA draws a
// fresh nonce per signature.
//
// Determinism: the stub's tree size, root hash and root timestamp are
// compile-time constants; no port is fixed, and the two loopback ports are
// obtained differently on purpose -- the stub backend serves on the listener
// a :0 bind handed back and never releases it, while ct_server's --http_endpoint
// has to be named on a command line before the process that binds it exists,
// so serverkit's freeLoopbackPort probes for a free one below the kernel's
// ephemeral range instead of binding and releasing :0 (TEST_INTERFACE.md §8
// states both mechanisms; the banner above freeLoopbackPort has the reason);
// the root set is compared sorted; the SCT and STH signature checks are made over
// the timestamp the response itself carries, so no assertion reads a clock,
// an elapsed time, an environment name or an unordered iteration. Start-up is
// a fatal prerequisite rather than a counted case.
package main

import (
	"context"
	"encoding/hex"
	"encoding/json"
	"net/http"
	"sort"
	"strings"

	"github.com/google/certificate-transparency-go/client"
	"github.com/google/certificate-transparency-go/jsonclient"

	"ctverify/vhelp"
)

func main() {
	t := vhelp.New()
	pub := loadPublicKey(t)

	// The pinned log id is the SHA-256 of the DER SubjectPublicKeyInfo, and
	// the corpus ships it so this driver does not recompute the product's own
	// answer with the product's own method.
	wantLogID := strings.TrimSpace(strings.SplitN(string(vhelp.ReadFixture("minted", "keys", "log-id.txt")), "\n", 2)[0])

	h := startPersonality(t)
	defer h.stop()

	// --- get-roots: served entirely from the configured root pool ----------
	roots := h.get(t, "/ct/v1/get-roots")
	t.Equal("get-roots-status", roots.status, http.StatusOK)

	var rootsRsp struct {
		Certificates [][]byte `json:"certificates"`
	}
	gotRoots := []string{}
	if err := json.Unmarshal(roots.body, &rootsRsp); err == nil {
		for _, d := range rootsRsp.Certificates {
			gotRoots = append(gotRoots, hex.EncodeToString(d))
		}
	}
	wantRoots := []string{}
	for _, d := range pemDERs(t, vhelp.ReadFixture("minted", "roots-bench-plus-int.pem")) {
		wantRoots = append(wantRoots, hex.EncodeToString(d))
	}
	sort.Strings(gotRoots)
	sort.Strings(wantRoots)
	t.Equal("get-roots-exact-der-set", strings.Join(gotRoots, ","), strings.Join(wantRoots, ","))

	// --- add-chain: the admission path all the way to an issued SCT --------
	chainPEM := vhelp.ReadFixture("minted", "chain-valid.pem")
	leafDER := pemDERs(t, chainPEM)[0]
	add := h.postChain(t, "/ct/v1/add-chain", chainPEM)
	t.Equal("add-chain-status", add.status, http.StatusOK)

	var sct sctJSON
	sctErr := json.Unmarshal(add.body, &sct)
	t.Check("add-chain-body-shape", sctErr == nil && sct.SCTVersion == 0 && sct.Extensions == "",
		"add-chain body %q: err=%v version=%d extensions=%q", string(add.body), sctErr, sct.SCTVersion, sct.Extensions)
	t.Equal("add-chain-log-id", hex.EncodeToString(sct.ID), wantLogID)

	// The SCT timestamp is the personality's own wall clock at this anchor --
	// ct_server has no clock flag -- so it is read from the response rather
	// than pinned. The assertion is the signature over the exact RFC 6962
	// input built from that timestamp, whose truth does not depend on it.
	if hash, sig, raw, err := split(sct.Signature); err != nil {
		t.Check("add-chain-signature-framing", false, "%v", err)
		t.Check("add-chain-signature-verifies", false, "no signature to verify")
	} else {
		t.Check("add-chain-signature-framing", hash == 4 && sig == 3,
			"digitally-signed says hash=%d sig=%d, want 4 (sha256) and 3 (ecdsa)", hash, sig)
		t.Case("add-chain-signature-verifies",
			verifyECDSA(pub, sctSignatureInput(sct.Timestamp, leafDER, nil), raw))
	}

	calls, logIDs, lastLeaf := h.be.queueSnapshot()
	t.Check("add-chain-queued-one-leaf", calls == 1 && len(logIDs) == 1 && logIDs[0] == kitLogID && len(lastLeaf) > 0,
		"backend saw %d QueueLeaves calls for log ids %v, leaf %d bytes; want exactly one for %d with a non-empty leaf",
		calls, logIDs, len(lastLeaf), kitLogID)

	// A chain with no path to a configured root is refused before the backend
	// is asked anything.
	bad := h.postChain(t, "/ct/v1/add-chain", vhelp.ReadFixture("minted", "chain-unrooted.pem"))
	callsAfter, _, _ := h.be.queueSnapshot()
	t.Check("add-chain-unrooted-refused", bad.status == http.StatusBadRequest && callsAfter == calls,
		"unrooted chain got HTTP %d and left the backend on %d calls (was %d); want 400 and no new call",
		bad.status, callsAfter, calls)

	// --- get-sth: the pinned root, signed ----------------------------------
	first := h.get(t, "/ct/v1/get-sth")
	t.Equal("get-sth-status", first.status, http.StatusOK)
	s1, err := parseSTH(first.body)
	if err != nil {
		t.Check("get-sth-fields", false, "%v", err)
		t.Check("get-sth-signature-verifies", false, "no STH to verify")
		t.Check("client-get-sth-agrees", false, "no STH to compare")
		t.Done()
	}
	t.Check("get-sth-fields",
		s1.TreeSize == uint64(kitTreeSize) && s1.Timestamp == kitRootMilli && sameBytes(s1.SHA256RootHash, kitRootHash("root-a")),
		"get-sth served tree_size=%d timestamp=%d root=%x; want %d, %d, %x",
		s1.TreeSize, s1.Timestamp, s1.SHA256RootHash, kitTreeSize, kitRootMilli, kitRootHash("root-a"))

	if hash, sig, raw, err := split(s1.TreeHeadSignature); err != nil {
		t.Check("get-sth-signature-verifies", false, "%v", err)
	} else if hash != 4 || sig != 3 {
		t.Check("get-sth-signature-verifies", false, "digitally-signed says hash=%d sig=%d, want 4 and 3", hash, sig)
	} else {
		t.Case("get-sth-signature-verifies",
			verifyECDSA(pub, sthSignatureInput(s1.Timestamp, s1.TreeSize, s1.SHA256RootHash), raw))
	}

	// --- step 01's client library, still able to read this log ------------
	// Harbor never re-runs an earlier step's verifier, so the HTTP read
	// methods step 01 established have no other scored home. client.New
	// parses the PEM public key and GetSTH verifies the signature it
	// receives before returning, so this is an independent second opinion on
	// the STH the raw-HTTP cases above checked by hand -- and it fails if the
	// candidate breaks the client while working on the front end.
	lc, errClient := client.New(h.url(""), http.DefaultClient,
		jsonclient.Options{PublicKey: string(vhelp.ReadFixture("minted", "keys", "log-pubkey.pem"))})
	if errClient != nil {
		t.Check("client-get-sth-agrees", false, "client.New: %v", errClient)
	} else if sth, err := lc.GetSTH(context.Background()); err != nil {
		t.Check("client-get-sth-agrees", false, "client GetSTH: %v", err)
	} else {
		t.Check("client-get-sth-agrees",
			uint64(sth.TreeSize) == s1.TreeSize && uint64(sth.Timestamp) == s1.Timestamp &&
				sameBytes(sth.SHA256RootHash[:], s1.SHA256RootHash),
			"client read tree_size=%d timestamp=%d root=%x; raw HTTP read %d, %d, %x",
			sth.TreeSize, sth.Timestamp, sth.SHA256RootHash[:], s1.TreeSize, s1.Timestamp, s1.SHA256RootHash)
	}

	t.Done()
}

// split is a thin name for the digitally-signed decomposition so the call
// sites above stay readable.
func split(b []byte) (byte, byte, []byte, error) {
	return splitDigitallySigned(b)
}

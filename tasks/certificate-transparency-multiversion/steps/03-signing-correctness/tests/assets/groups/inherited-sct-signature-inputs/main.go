// Group inherited-sct-signature-inputs.
//
// The v1.0.7 advisory has two halves, and the tree-head half is what v1.0.10
// changes. This group covers the other half: an SCT signature must be bound
// to the whole SCT signature input -- the timestamp, the entry type and the
// entry -- so that no two distinct submissions can ever share one.
//
// It is the inverse of the tree-head groups. There the property is that an
// unchanged input is re-served byte-identically; here it is that what comes
// back is the signature over exactly the input that was served, and over no
// other. An implementation that reuses a signature under a key which does not
// capture the whole input commits the advisory's bug on the SCT path and
// fails the cross-checks below.
//
// Named inherited- because it must hold at v1.0.5 as well: that release has
// no cache at all and satisfies it by recomputing. v1.0.10 must not lose it
// while gaining the tree-head cache, which is the whole point of scoring it
// here. The continuation gate therefore requires this group to PASS on the
// previous release's tree.
//
// Everything is observed through the candidate's own ct_server over loopback.
//
// SCOPE-TIME: this driver reads no clock, sleeps for nothing and asserts on
// no wall-clock value, so TEST_INTERFACE.md section 8's "no wall clock" holds
// here too. The two distinct signature inputs come from two *different*
// pinned certificates rather than from waiting for a tick. ct_server has no
// clock flag, so the SCT timestamp is the personality's own -- it is only
// ever read back out of a response and fed to the signature input rebuilt
// from that same response, never compared with a time this driver obtained.
// Nothing here resubmits an identical chain either: HTTP-API.md section 3.2
// reserves that case for the backend's deduplication and says a front end
// that mints a second SCT for it is wrong, so the group does not ask for one.
package main

import (
	"encoding/hex"
	"encoding/json"
	"net/http"

	"ctverify/vhelp"
)

// submission is one add-chain or add-pre-chain round trip, reduced to what
// the signature rule is about.
type submission struct {
	status    int
	timestamp uint64
	sig       []byte // the raw ECDSA signature, digitally-signed framing removed
	framingOK bool
}

func submit(t *vhelp.T, h *harness, path string, chainPEM []byte) submission {
	r := h.postChain(t, path, chainPEM)
	s := submission{status: r.status}
	if r.status != http.StatusOK {
		return s
	}
	var sct sctJSON
	if err := json.Unmarshal(r.body, &sct); err != nil {
		return s
	}
	s.timestamp = sct.Timestamp
	hash, alg, raw, err := splitDigitallySigned(sct.Signature)
	if err != nil {
		return s
	}
	s.framingOK = hash == 4 && alg == 3
	s.sig = raw
	return s
}

// concatPEM joins pinned PEM fixtures into one submission chain, leaf first.
func concatPEM(parts ...[]byte) []byte {
	var out []byte
	for _, p := range parts {
		out = append(out, p...)
		if n := len(p); n > 0 && p[n-1] != '\n' {
			out = append(out, '\n')
		}
	}
	return out
}

func main() {
	t := vhelp.New()
	pub := loadPublicKey(t)

	h := startPersonality(t)
	defer h.stop()

	// Two different leaves under the same pinned intermediate. Everything
	// about the two submissions is fixed except which certificate is in it.
	chainA := vhelp.ReadFixture("minted", "chain-valid.pem")
	leafA := pemDERs(t, chainA)[0]
	chainB := concatPEM(
		vhelp.ReadFixture("minted", "leaf-eku-serverauth.pem"),
		vhelp.ReadFixture("minted", "bench-intermediate.pem"),
	)
	leafB := pemDERs(t, chainB)[0]

	// --- 1 & 2: both chains are accepted and framed as sha256/ecdsa --------
	first := submit(t, h, "/ct/v1/add-chain", chainA)
	t.Check("add-chain-first-leaf-status", first.status == http.StatusOK && first.framingOK,
		"add-chain of the first leaf: HTTP %d, digitally-signed framing ok=%v; want 200 and sha256/ecdsa",
		first.status, first.framingOK)

	second := submit(t, h, "/ct/v1/add-chain", chainB)
	t.Check("add-chain-second-leaf-status", second.status == http.StatusOK && second.framingOK,
		"add-chain of the second leaf: HTTP %d, digitally-signed framing ok=%v; want 200 and sha256/ecdsa",
		second.status, second.framingOK)

	// --- 3 & 4: each signature verifies over its own input ------------------
	// The input is rebuilt from the timestamp and the leaf that this very
	// response reported, so this is the whole "what you signed is what you
	// served" property for one submission.
	t.Case("first-sct-verifies-over-its-own-input",
		verifyECDSA(pub, sctSignatureInput(first.timestamp, leafA, nil), first.sig))
	t.Case("second-sct-verifies-over-its-own-input",
		verifyECDSA(pub, sctSignatureInput(second.timestamp, leafB, nil), second.sig))

	// --- 5: and over no other entry -----------------------------------------
	// This is the advisory. A signature reused across two entries verifies
	// over at most one of them, so a front end that serves one for the other
	// is caught here whatever its cache is keyed on.
	crossA := verifyECDSA(pub, sctSignatureInput(second.timestamp, leafA, nil), second.sig)
	crossB := verifyECDSA(pub, sctSignatureInput(first.timestamp, leafB, nil), first.sig)
	t.Check("neither-signature-verifies-over-the-other-entry",
		crossA != nil && crossB != nil,
		"a signature issued for one certificate verified over the other: second-over-first-leaf=%v first-over-second-leaf=%v",
		crossA, crossB)

	// --- 6: and not at any other timestamp ----------------------------------
	// The timestamp is inside the signed bytes (TLS-CODEC.md 5.5), so moving
	// it by one millisecond -- the smallest change the field can carry -- must
	// break verification. No clock is read: the value perturbed is the one the
	// response itself reported.
	skewA := verifyECDSA(pub, sctSignatureInput(first.timestamp+1, leafA, nil), first.sig)
	skewB := verifyECDSA(pub, sctSignatureInput(second.timestamp+1, leafB, nil), second.sig)
	t.Check("neither-signature-verifies-at-a-shifted-timestamp",
		skewA != nil && skewB != nil,
		"an SCT signature verified over its own entry at a timestamp 1 ms off the one served: first=%v second=%v",
		skewA, skewB)

	// --- 7: two distinct inputs, two distinct signatures --------------------
	t.Check("distinct-inputs-produce-distinct-signatures",
		!sameBytes(first.sig, second.sig),
		"two different certificates were served the same signature %s",
		hex.EncodeToString(first.sig))

	// --- 8 & 9: the entry type is in the signed bytes too -------------------
	// A precertificate is signed over a precert entry: entry type 1, the
	// issuer key hash and the poison-stripped TBS, not the submitted DER
	// (TLS-CODEC.md 5.5). So the x509-shaped input built from this response's
	// own timestamp and this chain's own leaf DER -- which is what an
	// implementation that ignores the entry type would sign -- must not
	// verify. The precert input itself is deliberately not rebuilt here: that
	// would mean reimplementing the TBS transformation the product performs,
	// and this group is about what may not be shared, not about that.
	preChainPEM := vhelp.ReadFixture("minted", "chain-precert.pem")
	preLeafDER := pemDERs(t, preChainPEM)[0]
	pre := submit(t, h, "/ct/v1/add-pre-chain", preChainPEM)
	t.Check("add-pre-chain-status", pre.status == http.StatusOK && pre.framingOK,
		"add-pre-chain: HTTP %d, digitally-signed framing ok=%v; want 200 and sha256/ecdsa",
		pre.status, pre.framingOK)

	preAsX509 := verifyECDSA(pub, sctSignatureInput(pre.timestamp, preLeafDER, nil), pre.sig)
	t.Check("precert-signature-is-not-over-an-x509-entry",
		preAsX509 != nil && !sameBytes(pre.sig, first.sig) && !sameBytes(pre.sig, second.sig),
		"precert SCT signature verified as an x509 entry (%v) or repeated an add-chain signature",
		preAsX509)

	t.Done()
}

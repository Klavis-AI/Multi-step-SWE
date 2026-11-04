// Group witness-update-and-cosign.
//
// v1.1.3 adds a CT witness: a service that follows a set of logs, refuses to
// countersign an STH it cannot reconcile with the last one it saw, and
// countersigns the ones it can. Its packages live under internal/, so the only
// boundary at which it can be observed is the binary and its HTTP API. This
// group starts the candidate's own witness on a loopback port picked from
// below the kernel's ephemeral range and exercises the whole accept path: which log identifier the
// witness follows a log under, trust on first use, the shape of the cosigned
// STH it returns, the witness signature itself, the log list, the served STH,
// an identical resubmission, and two consistent growth steps whose proofs are
// computed here in plain SHA-256.
//
// At v1.1.2 there is no witness binary, no /ctwitness/v0 API and no cosigned
// STH type at all, so the harness cannot even start and the group aborts.
package main

import (
	"fmt"
	"net/http"

	"ctverify/vhelp"
)

func main() {
	t := vhelp.New()
	h := startWitness(t)
	defer h.stop()

	// 1. Nothing is followed until something has been witnessed: the log list
	//    is the JSON null of an empty result, not an empty array.
	r := h.do(t, http.MethodGet, h.baseURL+witnessAPILogs, nil)
	t.Check("logs-empty-before-tofu", r.status == http.StatusOK && r.body == "null",
		"got %d %q, want 200 %q", r.status, r.body, "null")

	// 2. The identifier a log is followed under is the witness's own
	//    derivation from the public key it was configured with, not whatever
	//    identifier a caller addresses it at.
	//
	//    The log_id in a countersigned body cannot show that. witnesskit.go
	//    derives h.logID here, the driver puts that string in the request
	//    path, and the witness fills an empty sth.LogID straight from the
	//    path -- so comparing the two only reads the benchmark's own URL back,
	//    and case 4 below already pins the filled-in value anyway, because
	//    sthSummary renders logid=. What an echo cannot survive is being
	//    addressed at a *different* well-formed identifier: the same
	//    base64(SHA-256(PKIX DER)), taken over the witness's own pinned key,
	//    which is not the log this witness was configured with. A witness that
	//    keys its logs on the configured public key does not know that log and
	//    refuses the submission even though the STH is correctly signed by the
	//    log it does know; one that took the identifier from the URL would
	//    trust it on first use and start following a second log. Both halves
	//    below are the witness's own answers, and the list is read while the
	//    witness is still empty, so neither is implied by any other case here.
	stray := h.update(t, logIDFor(h.pubDER(t)),
		sthJSON(t, signedSTH(t, h.logKey, 2, witnessTimestampMS, root(2), "")), nil)
	strayLogs := h.do(t, http.MethodGet, h.baseURL+witnessAPILogs, nil)
	t.Equal("tofu-log-id-is-derived-from-the-configured-key",
		fmt.Sprintf("refused=%d follows=%s", stray.status, strayLogs.body),
		fmt.Sprintf("refused=%d follows=null", http.StatusInternalServerError))

	// 3. Trust on first use, at the identifier the witness does follow. The
	//    submitted STH deliberately omits log_id, so the witness has to fill
	//    it in from the request path.
	sth2 := signedSTH(t, h.logKey, 2, witnessTimestampMS, root(2), "")
	raw2 := sthJSON(t, sth2)
	up := h.update(t, h.logID, raw2, nil)
	t.Check("tofu-update-status", up.status == http.StatusOK, "got %d %q", up.status, up.body)
	if up.status != http.StatusOK {
		t.Fatalf("first update was refused: %d %q", up.status, up.body)
	}

	cos := decodeCosigned(t, up.body)

	// 4. The countersigned body is the submitted tree head, unchanged, with
	//    the log's own signature carried through byte for byte, and with the
	//    empty log_id filled in.
	want := sth2
	want.LogID = hashFromB64(t, h.logID)
	t.Equal("tofu-cosigned-sth-matches-submitted", sthSummary(cos.SignedTreeHead), sthSummary(want))

	// 5. The witness signature is a SHA-256/ECDSA signature over the TLS
	//    encoding of the STH, verified here with the standard library.
	t.Case("tofu-witness-signature-verifies", witnessSigVerifies(t, cos, &h.witnessKey.PublicKey))

	// 6. It is the witness's signature, not a copy of the log's: the same
	//    bytes do not verify under the log's key.
	t.Check("tofu-witness-signature-is-not-the-log-signature",
		witnessSigVerifies(t, cos, &h.logKey.PublicKey) != nil,
		"the witness signature also verifies under the log's key")

	// 7. The log is now followed.
	r = h.do(t, http.MethodGet, h.baseURL+witnessAPILogs, nil)
	t.Equal("logs-lists-log-after-tofu", fmt.Sprintf("%d %s", r.status, r.body),
		fmt.Sprintf("200 [%q]", h.logID))

	// 8. Serving it back yields a cosigned STH over the same tree head.
	r = h.do(t, http.MethodGet, h.sthPath(h.logID), nil)
	t.Check("get-sth-status", r.status == http.StatusOK, "got %d %q", r.status, r.body)
	served := decodeCosigned(t, r.body)
	t.Equal("get-sth-returns-the-stored-tree-head", sthSummary(served.SignedTreeHead), sthSummary(want))
	t.Case("get-sth-witness-signature-verifies", witnessSigVerifies(t, served, &h.witnessKey.PublicKey))

	// 9. Resubmitting the identical STH changes nothing, and the witness
	//    answers with the raw bytes it stored -- uncosigned, log_id still the
	//    zero hash the submission carried.
	again := h.update(t, h.logID, raw2, nil)
	t.Check("identical-resubmit-status", again.status == http.StatusOK, "got %d %q", again.status, again.body)
	t.EqualBytes("identical-resubmit-returns-stored-raw-sth", []byte(again.body), raw2)

	// 10. Growth from 2 to 4 with RFC 6962 PROOF(2, D[4]), computed here.
	sth4 := signedSTH(t, h.logKey, 4, witnessTimestampMS+1000, root(4), h.logID)
	grow := h.update(t, h.logID, sthJSON(t, sth4), consistency(2, 4))
	t.Check("growth-2-to-4-status", grow.status == http.StatusOK, "got %d %q", grow.status, grow.body)
	g4 := decodeCosigned(t, grow.body)
	t.Equal("growth-2-to-4-cosigned-sth", sthSummary(g4.SignedTreeHead), sthSummary(sth4))
	t.Case("growth-2-to-4-witness-signature-verifies", witnessSigVerifies(t, g4, &h.witnessKey.PublicKey))

	// 11. The served STH follows the tree forward.
	r = h.do(t, http.MethodGet, h.sthPath(h.logID), nil)
	s4 := decodeCosigned(t, r.body)
	t.Equal("get-sth-after-growth",
		fmt.Sprintf("%d %d %x", r.status, s4.TreeSize, s4.SHA256RootHash[:]),
		fmt.Sprintf("200 4 %x", root(4)))

	// 12. And again from 4 to 8, whose proof has a different shape.
	sth8 := signedSTH(t, h.logKey, 8, witnessTimestampMS+2000, root(8), h.logID)
	grow8 := h.update(t, h.logID, sthJSON(t, sth8), consistency(4, 8))
	t.Check("growth-4-to-8-status", grow8.status == http.StatusOK, "got %d %q", grow8.status, grow8.body)
	g8 := decodeCosigned(t, grow8.body)
	t.Equal("growth-4-to-8-cosigned-root", fmt.Sprintf("%d %x", g8.TreeSize, g8.SHA256RootHash[:]),
		fmt.Sprintf("8 %x", root(8)))

	t.Done()
}

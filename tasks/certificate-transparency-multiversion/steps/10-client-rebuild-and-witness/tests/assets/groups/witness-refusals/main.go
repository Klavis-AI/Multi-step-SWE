// Group witness-refusals.
//
// The point of a witness is what it refuses. v1.1.3's witness distinguishes
// several kinds of bad news and gives each its own answer over HTTP, exactly as
// the status table in CLIENT-API.md section 6.3 lays them out: a GET for a log
// it has nothing stored for is a 404, a PUT naming a log it does not follow is
// a 500, a body it cannot read as an UpdateRequest is a 400, an STH that does
// not parse or is signed by the wrong key is a 500, and a tree head that is
// well formed but irreconcilable with the one already stored is a 409 whose
// body is the stored STH, byte for byte, so the caller can re-aim. This group
// starts the candidate's own witness binary on a loopback port picked from
// below the kernel's ephemeral range and grades each of those answers, then shows that none of the
// refusals disturbed what was stored.
//
// The *status* of each refusal, and the two bodies section 6.3 makes normative
// (the raw stored STH returned with a 409 and with an identical re-submission),
// are graded exactly. The remaining bodies are graded only as "a diagnosis" --
// the word the table itself uses -- because section 6.3 says they "are prose
// assembled from whatever error was raised ... and are not an interface
// anything should depend on". A witness that reports the same conditions with
// its own wording, and one that does not route its storage errors through a
// gRPC status wrapper at all, both pass.
//
// At v1.1.2 none of this exists: there is no witness binary to start, so the
// harness aborts and the group reports no cases at all.
package main

import (
	"encoding/json"
	"fmt"
	"net/http"
	"strings"

	"ctverify/vhelp"
)

// unknownLogID is a syntactically valid, pinned, all-zero logID. The witness is
// configured with exactly one log, and it is not this one.
const unknownLogID = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="

// isDiagnosis reports whether a refusal body is what CLIENT-API.md section 6.3
// calls "a diagnosis": something non-empty explaining the refusal, and in
// particular not the stored tree head that the same table reserves for 409 and
// for an identical re-submission. Section 6.3 declares the prose itself to be
// no interface at all, so nothing here reads its words -- only that the
// witness said something, and that what it said was not an STH.
func isDiagnosis(body string) bool {
	if strings.TrimSpace(body) == "" {
		return false
	}
	var probe struct {
		TreeSize *uint64 `json:"tree_size"`
	}
	if json.Unmarshal([]byte(body), &probe) == nil && probe.TreeSize != nil {
		return false
	}
	return true
}

func main() {
	t := vhelp.New()
	h := startWitness(t)
	defer h.stop()

	// 1. A followed log with nothing stored yet is a 404 carrying a diagnosis
	//    rather than a tree head.
	r := h.do(t, http.MethodGet, h.sthPath(h.logID), nil)
	t.Equal("absent-sth-status", r.status, http.StatusNotFound)
	t.Check("absent-sth-body-is-a-diagnosis", isDiagnosis(r.body), "body=%q", r.body)

	// 2. A log the witness does not follow cannot be updated at all, and the
	//    refusal is reported as an internal error rather than a 404, because
	//    only a failed precondition is given a status of its own.
	sth4 := signedSTH(t, h.logKey, 4, witnessTimestampMS, root(4), h.logID)
	raw4 := sthJSON(t, sth4)
	r = h.update(t, unknownLogID, raw4, nil)
	t.Equal("unknown-log-update-status", r.status, http.StatusInternalServerError)
	t.Check("unknown-log-update-body-is-a-diagnosis", isDiagnosis(r.body), "body=%q", r.body)

	// 3. Reading an unfollowed log is indistinguishable from reading a
	//    followed one with nothing stored: both are 404.
	r = h.do(t, http.MethodGet, h.sthPath(unknownLogID), nil)
	t.Check("unknown-log-get-sth",
		r.status == http.StatusNotFound && isDiagnosis(r.body), "got %d %q", r.status, r.body)

	// 4. A body that is not a JSON object is rejected before the witness is
	//    consulted at all.
	r = h.do(t, http.MethodPut, h.updatePath(h.logID), []byte("{not json"))
	t.Equal("malformed-json-status", r.status, http.StatusBadRequest)
	t.Check("malformed-json-body-is-a-diagnosis", isDiagnosis(r.body), "body=%q", r.body)

	// 5. So is an empty body.
	r = h.do(t, http.MethodPut, h.updatePath(h.logID), nil)
	t.Check("empty-body-rejected",
		r.status == http.StatusBadRequest && isDiagnosis(r.body), "got %d %q", r.status, r.body)

	// 6. A well-formed update request whose STH field is absent gets past the
	//    request parser and fails inside the witness instead.
	r = h.do(t, http.MethodPut, h.updatePath(h.logID), []byte(`{"Proof":[]}`))
	t.Check("absent-sth-field-is-an-update-failure",
		r.status == http.StatusInternalServerError && isDiagnosis(r.body), "got %d %q", r.status, r.body)

	// 7. An STH the configured log did not sign is refused. This is the
	//    load-bearing check of the whole service.
	badSig := signedSTH(t, h.witnessKey, 4, witnessTimestampMS, root(4), h.logID)
	r = h.update(t, h.logID, sthJSON(t, badSig), nil)
	t.Equal("wrong-signer-status", r.status, http.StatusInternalServerError)
	t.Check("wrong-signer-body-is-a-diagnosis", isDiagnosis(r.body), "body=%q", r.body)

	// 8. An STH that names a different log than the URL is refused even
	//    though it is correctly signed by the log in the URL.
	otherID := logIDFor(h.pubDER(t))
	mismatch := signedSTH(t, h.logKey, 4, witnessTimestampMS, root(4), otherID)
	r = h.update(t, h.logID, sthJSON(t, mismatch), nil)
	t.Equal("mismatched-log-id-status", r.status, http.StatusInternalServerError)
	t.Check("mismatched-log-id-body-is-a-diagnosis", isDiagnosis(r.body), "body=%q", r.body)

	// 9. An STH claiming a protocol version the signature scheme is not
	//    defined for is refused.
	var obj map[string]interface{}
	if err := json.Unmarshal(raw4, &obj); err != nil {
		t.Fatalf("decode pinned STH: %v", err)
	}
	obj["sth_version"] = 1
	v1, err := json.Marshal(obj)
	if err != nil {
		t.Fatalf("re-encode pinned STH: %v", err)
	}
	r = h.update(t, h.logID, v1, nil)
	t.Check("unsupported-version-refused",
		r.status == http.StatusInternalServerError && isDiagnosis(r.body), "got %d %q", r.status, r.body)

	// Nothing above was accepted, so this is still trust on first use.
	first := h.update(t, h.logID, raw4, nil)
	t.Check("nothing-refused-so-far-was-stored", first.status == http.StatusOK,
		"first accepted update returned %d %q", first.status, first.body)
	if first.status != http.StatusOK {
		t.Fatalf("cannot establish a stored STH: %d %q", first.status, first.body)
	}

	// 10. A smaller tree cannot be proved consistent with a larger one, and
	//     the refusal hands back exactly the bytes the witness is holding.
	sth2 := signedSTH(t, h.logKey, 2, witnessTimestampMS+1000, root(2), h.logID)
	r = h.update(t, h.logID, sthJSON(t, sth2), nil)
	t.Equal("backwards-status", r.status, http.StatusConflict)
	t.EqualBytes("backwards-returns-stored-raw-sth", []byte(r.body), raw4)

	// 11. The same size with a different root hash is a split view.
	forked := signedSTH(t, h.logKey, 4, witnessTimestampMS+2000, root(3), h.logID)
	r = h.update(t, h.logID, sthJSON(t, forked), nil)
	t.Equal("same-size-different-hash-status", r.status, http.StatusConflict)
	t.EqualBytes("same-size-different-hash-returns-stored-raw-sth", []byte(r.body), raw4)

	// 12. Growth is not taken on trust: an omitted proof is refused.
	sth8 := signedSTH(t, h.logKey, 8, witnessTimestampMS+3000, root(8), h.logID)
	raw8 := sthJSON(t, sth8)
	r = h.update(t, h.logID, raw8, nil)
	t.Equal("growth-without-proof-status", r.status, http.StatusConflict)
	t.EqualBytes("growth-without-proof-returns-stored-raw-sth", []byte(r.body), raw4)

	// 13. Nor is a proof of the right length but the wrong content, nor the
	//     proof for a different pair of sizes.
	r = h.update(t, h.logID, raw8, [][]byte{leafHash(0), leafHash(1)})
	t.Equal("growth-with-forged-proof-status", r.status, http.StatusConflict)
	r = h.update(t, h.logID, raw8, consistency(2, 8))
	t.Equal("growth-with-proof-for-other-sizes-status", r.status, http.StatusConflict)

	// 14. None of the refusals moved the stored STH.
	r = h.do(t, http.MethodGet, h.sthPath(h.logID), nil)
	held := decodeCosigned(t, r.body)
	t.Equal("refusals-left-stored-sth-unchanged",
		fmt.Sprintf("%d %d %x", r.status, held.TreeSize, held.SHA256RootHash[:]),
		fmt.Sprintf("200 4 %x", root(4)))
	t.Case("stored-sth-still-cosigned", witnessSigVerifies(t, held, &h.witnessKey.PublicKey))

	// 15. And the correct proof for the stored size is still accepted
	//     afterwards, so the refusals were refusals and not a wedged witness.
	r = h.update(t, h.logID, raw8, consistency(4, 8))
	t.Check("correct-proof-still-accepted-after-refusals", r.status == http.StatusOK,
		"got %d %q", r.status, r.body)

	// 16. Each endpoint answers only its own method, and an unrouted path is
	//     a plain 404 from the router.
	r = h.do(t, http.MethodPost, h.updatePath(h.logID), []byte("{}"))
	t.Equal("post-to-update-is-405", fmt.Sprintf("%d %q", r.status, r.body), `405 ""`)
	r = h.do(t, http.MethodPut, h.sthPath(h.logID), nil)
	t.Equal("put-to-sth-is-405", fmt.Sprintf("%d %q", r.status, r.body), `405 ""`)
	r = h.do(t, http.MethodPut, h.baseURL+witnessAPILogs, nil)
	t.Equal("put-to-logs-is-405", fmt.Sprintf("%d %q", r.status, r.body), `405 ""`)
	//     The router's own 404 page is the Go standard library's, so only its
	//     status is graded and its wording is not.
	r = h.do(t, http.MethodGet, h.baseURL+"/ctwitness/v0/logs/"+h.logID+"/consistency", nil)
	t.Check("unrouted-path-is-404",
		r.status == http.StatusNotFound && isDiagnosis(r.body),
		"got %d %q", r.status, r.body)

	t.Done()
}

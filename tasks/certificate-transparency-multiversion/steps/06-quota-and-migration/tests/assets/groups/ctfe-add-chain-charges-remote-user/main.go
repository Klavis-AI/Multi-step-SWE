// Group ctfe-add-chain-charges-remote-user.
//
// v1.0.19 makes the CTFE ask its backend to charge quota to whoever sent the
// request. ct_server installs InstanceOptions.RemoteQuotaUser (from
// --quota_remote, default true) as tomasen/realip's FromRequest, and
// addChainInternal seeds the QueueLeavesRequest's ChargeTo with it:
//
//	req := trillian.QueueLeavesRequest{..., ChargeTo: li.chargeUser(r)}
//
// This group grades the submission half of that at the boundary the
// documentation names -- the log's HTTP surface, over a real loopback socket,
// with the candidate's own ct_server binary -- and reads the charge back off
// this group's own pinned backend (SCOPE-BACKEND).
//
// The remote user is always the FIRST entry of ChargeTo.User; the rest are
// the intermediates, which the sibling group ctfe-add-chain-charges-
// intermediates owns. What is graded here is that the entry is present, that
// it is the address the request actually came from, that it is recomputed per
// request rather than latched at startup, that add-pre-chain does it too, and
// that a submission the log refuses charges nobody at all -- chain validation
// runs before the backend call, so a rejected chain must never consume quota.
//
// realip.FromRequest prefers the first non-private address in X-Forwarded-For
// over the socket peer, so a loopback client says who it is with that header;
// the case that omits it pins the fallback, the peer address of the socket.
//
// SCOPE-TIME: no case reads a clock. SCOPE-NET: loopback only, every port
// obtained by binding :0.
//
// At v1.0.17 no product code constructs a ChargeTo, so every charge this
// group reads back is empty there.
package main

import (
	"encoding/json"
	"fmt"

	ct "github.com/google/certificate-transparency-go"

	"ctverify/vhelp"
)

const queueMethod = "QueueLeaves"

func chainJSON(t *vhelp.T, rel ...[]string) []byte {
	chain := make([][]byte, 0, len(rel))
	for _, r := range rel {
		chain = append(chain, vhelp.ReadFixture(r...))
	}
	b, err := json.Marshal(ct.AddChainRequest{Chain: chain})
	if err != nil {
		t.Fatalf("marshal add-chain request: %v", err)
	}
	return b
}

// remoteUserOf reports the first ChargeTo user of the most recent QueueLeaves,
// or a description of why there is not one.
func remoteUserOf(users []string) string {
	if len(users) == 0 {
		return "<no ChargeTo on the request>"
	}
	return users[0]
}

func main() {
	t := vhelp.New()

	h := startPersonality(t, nil, "")
	defer h.stop()

	valid := chainJSON(t, []string{"minted", "leaf-valid.der"}, []string{"minted", "bench-intermediate.der"})
	precert := chainJSON(t, []string{"minted", "precert-poisoned.der"}, []string{"minted", "bench-intermediate.der"})

	// --- 1. the declared client address is charged --------------------------
	res := h.postJSONFrom(t, h.logURL("/ct/v1/add-chain"), "203.0.113.9", valid)
	t.Check("add-chain-charges-the-declared-client-address",
		res.status == 200 && remoteUserOf(h.backend.lastCharge(queueMethod)) == "203.0.113.9",
		"status %d, remote quota user %q, want 200 and %q",
		res.status, remoteUserOf(h.backend.lastCharge(queueMethod)), "203.0.113.9")

	// --- 2. and re-derived for the next request, not latched ----------------
	res = h.postJSONFrom(t, h.logURL("/ct/v1/add-chain"), "198.51.100.44", valid)
	t.Check("remote-quota-user-is-recomputed-per-request",
		res.status == 200 && remoteUserOf(h.backend.lastCharge(queueMethod)) == "198.51.100.44",
		"status %d, remote quota user %q, want 200 and %q",
		res.status, remoteUserOf(h.backend.lastCharge(queueMethod)), "198.51.100.44")

	// --- 3. with no declared address, the socket peer is charged ------------
	res = h.postJSON(t, h.logURL("/ct/v1/add-chain"), valid)
	t.Check("undeclared-client-is-charged-as-the-socket-peer",
		res.status == 200 && remoteUserOf(h.backend.lastCharge(queueMethod)) == "127.0.0.1",
		"status %d, remote quota user %q, want 200 and %q",
		res.status, remoteUserOf(h.backend.lastCharge(queueMethod)), "127.0.0.1")

	// --- 4. add-pre-chain charges the submitter too -------------------------
	res = h.postJSONFrom(t, h.logURL("/ct/v1/add-pre-chain"), "192.0.2.77", precert)
	t.Check("add-pre-chain-charges-the-declared-client-address",
		res.status == 200 && remoteUserOf(h.backend.lastCharge(queueMethod)) == "192.0.2.77",
		"status %d, remote quota user %q, want 200 and %q",
		res.status, remoteUserOf(h.backend.lastCharge(queueMethod)), "192.0.2.77")

	// --- 5. one backend request, and one charge, per accepted submission ----
	t.Equal("one-charge-record-per-accepted-submission", len(h.backend.chargesFor(queueMethod)), 4)

	// --- 6. a refused submission charges nobody -----------------------------
	// Chain validation runs before the backend call, so an untrusted chain
	// must not reach the queue at all; without this case the group could not
	// tell "charges the submitter" from "charges everyone who connects".
	unrooted := chainJSON(t, []string{"minted", "leaf-unrooted.der"}, []string{"minted", "rogue-root.der"})
	res = h.postJSONFrom(t, h.logURL("/ct/v1/add-chain"), "203.0.113.200", unrooted)
	t.Equal("refused-submission-reaches-neither-queue-nor-quota",
		fmt.Sprintf("status=%d charges=%d", res.status, len(h.backend.chargesFor(queueMethod))),
		fmt.Sprintf("status=%d charges=%d", 400, 4))

	t.Done()
}

// Group ctfe-quota-flags-disable-charging.
//
// --quota_remote and --quota_intermediate are not decoration: ct_server
// consults them when it builds InstanceOptions, and a nil callback is what
// makes the CTFE omit that part of the charge.
//
//	if *quotaRemote       { opts.RemoteQuotaUser      = ... realip.FromRequest }
//	if *quotaIntermediate { opts.CertificateQuotaUser = ctfe.QuotaUserForCert }
//
// The sibling group ctserver-quota-flag-grammar grades that the flags exist
// and default to true; this one grades that turning them off actually turns
// the charging off, independently for each of the two halves, observed at the
// backend over a real loopback socket. Three ct_server processes are started
// in turn, each with a different combination, and each is stopped before the
// next starts.
//
// The last case is the control that keeps the other four honest: with both
// flags off the log must still admit the submission and issue an SCT. Without
// it, "no charge was recorded" would also be satisfied by a personality that
// refused everything, or never reached its backend at all.
//
// SCOPE-BACKEND: this group's own pinned TrillianLogServer. SCOPE-TIME: no
// case reads a clock; the SCT timestamp is not asserted. SCOPE-NET: loopback,
// ports from binding :0.
//
// At v1.0.17 neither flag exists (ct_server's parser rejects them, so the
// process never comes up) and no ChargeTo is ever built.
package main

import (
	"encoding/json"
	"fmt"
	"strings"

	ct "github.com/google/certificate-transparency-go"

	"ctverify/vhelp"
)

const (
	queueMethod = "QueueLeaves"
	sthMethod   = "GetLatestSignedLogRoot"

	benchInterUser = "@intermediate CN=Harbor CT Bench Intermediate,O=Harbor CT Bench 2f5b59344c"
	client         = "203.0.113.9"
)

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

func render(status int, users []string) string {
	return fmt.Sprintf("status=%d charge=[%s]", status, strings.Join(users, " | "))
}

func main() {
	t := vhelp.New()

	valid := chainJSON(t, []string{"minted", "leaf-valid.der"}, []string{"minted", "bench-intermediate.der"})

	// --- A. --quota_remote=false --------------------------------------------
	func() {
		h := startPersonality(t, []string{"--quota_remote=false"}, "")
		defer h.stop()

		res := h.postJSONFrom(t, h.logURL("/ct/v1/add-chain"), client, valid)
		t.Equal("quota-remote-false-drops-the-submitter-but-keeps-issuers",
			render(res.status, h.backend.lastCharge(queueMethod)),
			render(200, []string{benchInterUser}))

		// A read endpoint has no issuers to charge, so with the remote user
		// switched off it must carry no ChargeTo at all.
		res = h.getFrom(t, h.logURL("/ct/v1/get-sth"), client)
		t.Equal("quota-remote-false-leaves-read-endpoints-uncharged",
			render(res.status, h.backend.lastCharge(sthMethod)),
			render(200, nil))
	}()

	// --- B. --quota_intermediate=false --------------------------------------
	func() {
		h := startPersonality(t, []string{"--quota_intermediate=false"}, "")
		defer h.stop()

		res := h.postJSONFrom(t, h.logURL("/ct/v1/add-chain"), client, valid)
		t.Equal("quota-intermediate-false-drops-the-issuers-but-keeps-the-submitter",
			render(res.status, h.backend.lastCharge(queueMethod)),
			render(200, []string{client}))
	}()

	// --- C. both off ---------------------------------------------------------
	func() {
		h := startPersonality(t, []string{"--quota_remote=false", "--quota_intermediate=false"}, "")
		defer h.stop()

		res := h.postJSONFrom(t, h.logURL("/ct/v1/add-chain"), client, valid)
		t.Equal("both-flags-false-attaches-no-charge-at-all",
			render(res.status, h.backend.lastCharge(queueMethod)),
			render(200, nil))

		// Control: the log is still a log.
		var sct ct.AddChainResponse
		err := json.Unmarshal(res.body, &sct)
		t.Check("both-flags-false-still-issues-an-sct",
			err == nil && sct.SCTVersion == ct.V1 && len(sct.ID) == 32 && len(sct.Signature) > 0,
			"decode=%v version=%v id=%d bytes signature=%d bytes; body %.200s",
			err, sct.SCTVersion, len(sct.ID), len(sct.Signature), res.body)
	}()

	t.Done()
}

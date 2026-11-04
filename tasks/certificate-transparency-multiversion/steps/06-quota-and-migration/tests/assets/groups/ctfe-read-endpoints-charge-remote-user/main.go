// Group ctfe-read-endpoints-charge-remote-user.
//
// v1.0.19 does not stop at add-chain: every read endpoint that talks to the
// backend now attaches li.chargeUser(r) to its request, so a client that only
// ever reads still consumes its own quota. Five endpoints, five distinct
// backend RPCs:
//
//	get-sth              -> GetLatestSignedLogRoot
//	get-sth-consistency  -> GetConsistencyProof
//	get-proof-by-hash    -> GetInclusionProofByHash
//	get-entries          -> GetLeavesByIndex
//	get-entry-and-proof  -> GetEntryAndProof
//
// Each is driven over the log's real HTTP surface against the candidate's own
// ct_server, each from a different declared client address, and each charge is
// read back off this group's own pinned backend (SCOPE-BACKEND). Using a
// different address per endpoint means a charge cannot be satisfied by some
// other endpoint's leftovers, and each case also pins the HTTP status, so an
// endpoint that errored before reaching its backend cannot pass by silence.
//
// The sixth case pins the exact set of RPCs that were charged over the whole
// run. It is the control: it fails both for an implementation that charges
// nothing and for one that charges on a method no endpoint here touched.
//
// The seventh drives the one read endpoint ARCHITECTURE.md 2.2 exempts:
// get-roots answers out of the log's own trust pool, so it reaches no backend
// and charges nobody. The case takes a per-method tally of every call the
// backend has seen, issues the request from an address of its own, and
// requires the tally to be byte-identical afterwards -- so neither a new
// method nor a repeat of one of the five can hide in it. Without this case
// the "only get-roots is uncharged" clause is satisfied by never asking.
//
// SCOPE-TIME: the backend answers from constants; no case reads a clock.
// SCOPE-NET: loopback only. The kit's ct_server ports come from the shared
// freeLoopbackPort probe (20000-31999, below the kernel ephemeral range);
// the in-process gRPC backend serves on the listener it binds itself.
//
// At v1.0.17 li.chargeUser does not exist and none of these requests carries a
// ChargeTo, so every case but the "want none" direction fails there.
package main

import (
	"encoding/base64"
	"fmt"
	"net/url"
	"strings"

	"ctverify/vhelp"
)

func render(status int, users []string) string {
	return fmt.Sprintf("status=%d charge=[%s]", status, strings.Join(users, " | "))
}

func main() {
	t := vhelp.New()

	h := startPersonality(t, nil, "")
	defer h.stop()

	// A hash of the right length is all get-proof-by-hash needs; the pinned
	// backend answers any hash with a pinned proof.
	hash := base64.StdEncoding.EncodeToString(pinnedRootHash[:])

	for _, tc := range []struct {
		id     string
		path   string
		query  url.Values
		from   string
		method string
	}{
		{
			id:     "get-sth-charges-its-caller",
			path:   "/ct/v1/get-sth",
			from:   "198.51.100.1",
			method: "GetLatestSignedLogRoot",
		},
		{
			id:     "get-sth-consistency-charges-its-caller",
			path:   "/ct/v1/get-sth-consistency",
			query:  url.Values{"first": {"1"}, "second": {"2"}},
			from:   "198.51.100.2",
			method: "GetConsistencyProof",
		},
		{
			id:     "get-proof-by-hash-charges-its-caller",
			path:   "/ct/v1/get-proof-by-hash",
			query:  url.Values{"hash": {hash}, "tree_size": {"10"}},
			from:   "198.51.100.3",
			method: "GetInclusionProofByHash",
		},
		{
			id:     "get-entries-charges-its-caller",
			path:   "/ct/v1/get-entries",
			query:  url.Values{"start": {"0"}, "end": {"1"}},
			from:   "198.51.100.4",
			method: "GetLeavesByIndex",
		},
		{
			id:     "get-entry-and-proof-charges-its-caller",
			path:   "/ct/v1/get-entry-and-proof",
			query:  url.Values{"leaf_index": {"3"}, "tree_size": {"10"}},
			from:   "198.51.100.5",
			method: "GetEntryAndProof",
		},
	} {
		u := h.logURL(tc.path)
		if len(tc.query) > 0 {
			u += "?" + tc.query.Encode()
		}
		res := h.getFrom(t, u, tc.from)
		t.Equal(tc.id,
			render(res.status, h.backend.lastCharge(tc.method)),
			render(200, []string{tc.from}))
	}

	// --- 6. exactly those five RPCs were charged, and no others -------------
	t.Equal("read-path-charges-exactly-those-five-backend-rpcs",
		strings.Join(h.backend.methodsCharged(), ","),
		strings.Join([]string{
			"GetConsistencyProof",
			"GetEntryAndProof",
			"GetInclusionProofByHash",
			"GetLatestSignedLogRoot",
			"GetLeavesByIndex",
		}, ","))

	// --- 7. get-roots is the exception, because it reaches no backend -------
	// tally is every method the backend has been called on, with its call
	// count; any backend call at all made while serving get-roots changes it.
	tally := func() string {
		methods := h.backend.methodsCharged()
		parts := make([]string, 0, len(methods))
		for _, m := range methods {
			parts = append(parts, fmt.Sprintf("%s=%d", m, len(h.backend.chargesFor(m))))
		}
		return strings.Join(parts, ",")
	}
	before := tally()
	res := h.getFrom(t, h.logURL("/ct/v1/get-roots"), "198.51.100.6")
	t.Equal("get-roots-is-served-without-reaching-the-backend",
		fmt.Sprintf("status=%d backend=%s", res.status, tally()),
		fmt.Sprintf("status=200 backend=%s", before))

	t.Done()
}

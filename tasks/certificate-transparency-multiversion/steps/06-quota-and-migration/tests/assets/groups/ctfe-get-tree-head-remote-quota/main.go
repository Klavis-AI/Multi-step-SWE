// Group ctfe-get-tree-head-remote-quota.
//
// v1.0.19 gives ctfe.GetTreeHead a fifth parameter, `remoteQuota *string`, and
// makes it attach that user to the backend's GetLatestSignedLogRootRequest as
// a trillian.ChargeTo. GetTreeHead is an exported library function, so it is
// graded from a driver that calls it; the charge it attached is then read back
// off this group's own pinned backend.
//
// The contract upstream actually writes is three-way, and all three arms are
// graded here:
//
//	remoteQuota == nil       -> the request carries no ChargeTo at all
//	remoteQuota -> "someone"  -> ChargeTo.User == ["someone"], exactly one entry
//	a later call              -> a *fresh* ChargeTo, never an appended one
//
// The third arm is worth pinning on its own: an implementation that hoisted
// the request or the ChargeTo out of the call would accumulate users across
// calls and over-charge every caller after the first, while still passing the
// single-call cases.
//
// SCOPE-BACKEND: the TrillianLogServer here is this group's own stub, served
// on a loopback port obtained by binding :0 and answering from pinned
// constants. Nothing about google/trillian is asserted -- only what the
// candidate's own code chose to send it. SCOPE-TIME: the STH timestamp comes
// from the stub's constant TimestampNanos, never from a clock.
//
// At v1.0.17 GetTreeHead takes four parameters and no product code builds a
// ChargeTo, so this driver does not compile against the previous tree.
package main

import (
	"context"
	"time"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/trillian/ctfe"

	"ctverify/vhelp"
)

const rpcMethod = "GetLatestSignedLogRoot"

func main() {
	t := vhelp.New()

	be, gs, client := startBackend(t)
	defer gs.Stop()

	// GetTreeHead writes ctfe's package-level STH gauges unconditionally, and
	// those are created only when an instance is stood up, so the instance is
	// created first. Nothing about it is asserted here; the handler set it
	// returns is not served.
	setUpPinnedInstance(t, client, ctfe.InstanceOptions{})

	ctx, cancel := context.WithTimeout(context.Background(), 60*time.Second)
	defer cancel()

	// --- call 1: a named remote quota user ---------------------------------
	// The happy path is graded on the call that carries a quota user, so that
	// a quota user is seen not to disturb the STH the function returns.
	userA := "10.11.12.13"
	sth, err := ctfe.GetTreeHead(ctx, client, pinnedLogID, logPrefix, &userA)
	if err != nil {
		t.Fatalf("GetTreeHead with a quota user: %v", err)
	}

	t.Check("sth-fields-from-pinned-root",
		sth.Version == ct.V1 &&
			sth.TreeSize == uint64(pinnedTreeSize) &&
			sth.Timestamp == uint64(pinnedTimestampMS),
		"version=%v tree_size=%d timestamp=%d, want %v/%d/%d",
		sth.Version, sth.TreeSize, sth.Timestamp, ct.V1, pinnedTreeSize, pinnedTimestampMS)

	t.EqualBytes("sth-root-hash-copied-from-backend", sth.SHA256RootHash[:], pinnedRootHash[:])

	t.Equal("charge-to-carries-the-remote-quota-user",
		be.lastCharge(rpcMethod), []string{userA})

	// --- call 2: no quota user at all ---------------------------------------
	if _, err := ctfe.GetTreeHead(ctx, client, pinnedLogID, logPrefix, nil); err != nil {
		t.Fatalf("GetTreeHead with a nil quota user: %v", err)
	}
	t.Check("nil-quota-user-attaches-no-charge",
		len(be.lastCharge(rpcMethod)) == 0,
		"backend saw ChargeTo users %q on a nil-quota call, want none", be.lastCharge(rpcMethod))

	// --- call 3: a different user, on the same client ------------------------
	userB := "198.51.100.7"
	if _, err := ctfe.GetTreeHead(ctx, client, pinnedLogID, logPrefix, &userB); err != nil {
		t.Fatalf("GetTreeHead with a second quota user: %v", err)
	}
	t.Equal("charge-to-is-rebuilt-per-call", be.lastCharge(rpcMethod), []string{userB})

	// --- call 4: an empty user is still a user -------------------------------
	// upstream appends unconditionally once the pointer is non-nil; it does not
	// treat "" as "no quota user".
	userEmpty := ""
	if _, err := ctfe.GetTreeHead(ctx, client, pinnedLogID, logPrefix, &userEmpty); err != nil {
		t.Fatalf("GetTreeHead with an empty quota user: %v", err)
	}
	t.Equal("empty-quota-user-is-still-charged", be.lastCharge(rpcMethod), []string{""})

	// --- the whole sequence, in order ---------------------------------------
	// One charge record per call, none skipped and none doubled.
	got := be.chargesFor(rpcMethod)
	t.Equal("one-charge-record-per-call", len(got), 4)

	t.Done()
}

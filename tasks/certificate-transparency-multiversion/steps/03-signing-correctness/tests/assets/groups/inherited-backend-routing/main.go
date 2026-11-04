// Group inherited-backend-routing.
//
// Step 02's milestone was that one ct_server can front several Trillian
// backends at once and that each configured log reaches the backend its
// config names. Nothing in step 03 removes that obligation -- the signature
// cache is per log, which only means anything if the logs are distinct all the
// way down to their backends -- and it is the one earlier milestone a
// signing-only step could quietly break, because a personality that ignored
// log_backend_name and used the first backend for everything would still sign
// perfectly.
//
// So the harness boots the candidate's own ct_server over two independent
// stub backends on two loopback ports, with a config that binds log "bencha"
// to backend "alpha" and log "benchb" to backend "beta". The two stubs serve
// visibly different tree heads, so an answer can be attributed to exactly one
// of them by its contents; each stub also counts the calls it received, so a
// request that went to the wrong backend is visible from both ends.
//
// The binding is also checked live: moving one backend's root must move that
// log's tree head and leave the other log's exactly where it was.
//
// This group re-checks an earlier milestone, so it says nothing about the
// signature cache: a candidate whose cache is missing or wrong fails the
// sth-signature-cache-* groups, not this one.
//
// SCOPE-NET: loopback only, ports from binding :0.
// SCOPE-BACKEND: the stubs are scaffolding, not a deliverable.
// SCOPE-TIME: both tree heads are pinned constants.
package main

import (
	"crypto/ecdsa"
	"encoding/hex"
	"fmt"
	"net/http"

	"ctverify/vhelp"
)

func main() {
	t := vhelp.New()
	pub := loadPublicKey(t)

	h := startTwoLogPersonality(t)
	defer h.stop()

	fetch := func(prefix, when string) ([]byte, *sthJSON) {
		r := h.get(t, prefix, "/ct/v1/get-sth")
		if r.status != http.StatusOK {
			h.stop()
			t.Fatalf("get-sth on /%s %s returned HTTP %d: %s", prefix, when, r.status, string(r.body))
		}
		s, err := parseSTH(r.body)
		if err != nil {
			h.stop()
			t.Fatalf("get-sth on /%s %s: %v", prefix, when, err)
		}
		return r.body, s
	}

	// One request to the first log only, then read both counters before the
	// second log has been touched at all.
	bodyA1, a1 := fetch(kitPrefixA, "first")
	alphaSeen := h.beA.rootCallCount()
	betaSeen := h.beB.rootCallCount()

	t.Check("first-log-serves-a-tree-head", len(bodyA1) > 0 && a1 != nil,
		"get-sth on /%s produced no tree head", kitPrefixA)
	t.Check("request-to-first-log-never-reached-the-other-backend",
		alphaSeen == 1 && betaSeen == 0,
		"after one get-sth on /%s the backends had seen %d and %d root requests, want 1 and 0",
		kitPrefixA, alphaSeen, betaSeen)

	// The first log's answer must be the first backend's root, field for field.
	t.Equal("first-log-serves-its-own-backends-tree-head",
		fmt.Sprintf("%d/%d/%s", a1.TreeSize, a1.Timestamp, hex.EncodeToString(a1.SHA256RootHash)),
		fmt.Sprintf("%d/%d/%s", kitTreeSize, kitRootMilli, hex.EncodeToString(kitRootHash("routing-alpha"))))

	bodyB1, b1 := fetch(kitPrefixB, "first")
	t.Check("second-log-serves-a-tree-head", len(bodyB1) > 0 && b1 != nil,
		"get-sth on /%s produced no tree head", kitPrefixB)
	t.Equal("second-log-serves-its-own-backends-tree-head",
		fmt.Sprintf("%d/%d/%s", b1.TreeSize, b1.Timestamp, hex.EncodeToString(b1.SHA256RootHash)),
		fmt.Sprintf("%d/%d/%s", kitTreeSizeB, kitRootMilliB, hex.EncodeToString(kitRootHash("routing-beta"))))

	// A second round on each log. Every get-sth fetches the root afresh -- the
	// cache is of the signature, not of the tree head -- so each backend must
	// end up having answered exactly its own log's two requests and no others.
	fetch(kitPrefixA, "second")
	fetch(kitPrefixB, "second")
	t.Equal("each-backend-answered-exactly-its-own-logs-requests",
		fmt.Sprintf("%d/%d", h.beA.rootCallCount(), h.beB.rootCallCount()), "2/2")

	// Each signature must verify over that log's own tree head.
	errA := verifyOne(pub, a1)
	errB := verifyOne(pub, b1)
	t.Check("each-logs-signature-verifies-over-its-own-tree-head", errA == nil && errB == nil,
		"/%s: %v; /%s: %v", kitPrefixA, errA, kitPrefixB, errB)

	// The binding is live, not a one-off lookup: move the first backend's root
	// and only the first log may follow it.
	movedHash := kitRootHash("routing-alpha-moved")
	h.beA.setRoot(1600000000000000000, 123456, movedHash)
	_, a3 := fetch(kitPrefixA, "after the first backend moved")
	_, b3 := fetch(kitPrefixB, "after the first backend moved")
	t.Equal("changing-one-backends-root-moves-only-its-own-log",
		fmt.Sprintf("%d/%d/%s | %d/%d/%s",
			a3.TreeSize, a3.Timestamp, hex.EncodeToString(a3.SHA256RootHash),
			b3.TreeSize, b3.Timestamp, hex.EncodeToString(b3.SHA256RootHash)),
		fmt.Sprintf("%d/%d/%s | %d/%d/%s",
			123456, uint64(1600000000000), hex.EncodeToString(movedHash),
			kitTreeSizeB, kitRootMilliB, hex.EncodeToString(kitRootHash("routing-beta"))))

	t.Done()
}

// verifyOne checks one STH's digitally-signed blob against the RFC 6962
// signature input rebuilt from the tree head the response carried.
func verifyOne(pub *ecdsa.PublicKey, s *sthJSON) error {
	hashAlg, sigAlg, sig, err := splitDigitallySigned(s.TreeHeadSignature)
	if err != nil {
		return err
	}
	if hashAlg != 4 || sigAlg != 3 {
		return fmt.Errorf("tree_head_signature algorithms are (%d,%d), want (4,3) = sha256/ecdsa", hashAlg, sigAlg)
	}
	return verifyECDSA(pub, sthSignatureInput(s.Timestamp, s.TreeSize, s.SHA256RootHash), sig)
}

// Group inherited-consistency-proof-verification.
//
// The other half of this milestone's headline capability: consistency-proof
// verification must still give the answers of MERKLE.md section 3 after the
// product's own merkletree/ package is deleted.
//
// At this release the product exposes consistency verification in exactly one
// place a driver can reach -- `ctclient consistency`, which fetches the proof
// and then checks it with whichever verifier the release owns (CLI.md section
// 3.1). So the group is the log: it serves get-sth-consistency from a tree
// built here out of the three hashing rules of MERKLE.md section 1, runs the
// candidate's own ctclient against it, and reads the exit status. Exit status
// is the whole signal -- no diagnostic text is asserted, because none of it is
// stable across releases (CLI.md section 4.3, TEST_INTERFACE.md section 9).
//
// Nothing here names a library, an error type or an import path. The proofs
// are correct or damaged by construction; the candidate's verifier is what has
// to tell the difference.
//
// This is an inherited group: every answer below is required at v1.0.13 too,
// where the verifier is the product's own merkletree/.
//
// MERKLE.md section 3's `n1 == 0` case is deliberately absent: ctclient
// rejects a non-positive --prev_size before any proof is fetched, so the case
// has no reachable behaviour here (TEST_INTERFACE.md section 9).
//
// SCOPE-NET: 127.0.0.1, port from binding :0. SCOPE-TIME: the leaf timestamps
// are pinned constants and no assertion depends on a clock.
package main

import (
	"encoding/hex"
	"fmt"
	"strings"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"

	"ctverify/vhelp"
)

// pinnedTimestamp is the SCT-style timestamp of leaf 0; leaf i carries
// pinnedTimestamp+i.
const pinnedTimestamp = uint64(1519737600000)

// treeSize is deliberately not a power of two, and neither is the old size
// used in case 2, so the seeded-subproof branch of MERKLE.md section 3 is
// exercised rather than only the aligned one.
const treeSize = 7

// runClient runs the candidate's ctclient against the log and returns its exit
// status.
func runClient(t *vhelp.T, args ...string) int {
	cmd := vhelp.Command(vhelp.CandidateBin("ctclient"), args...)
	var b strings.Builder
	cmd.Stdout = &b
	cmd.Stderr = &b
	err := cmd.Run()
	if err != nil && cmd.ProcessState == nil {
		t.Fatalf("run ctclient: %v", err)
	}
	return cmd.ProcessState.ExitCode()
}

func main() {
	t := vhelp.New()

	// Seven distinct leaves over one pinned certificate.
	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	leaves := make([][]byte, treeSize)
	for i := 0; i < treeSize; i++ {
		l := ct.CreateX509MerkleTreeLeaf(ct.ASN1Cert{Data: leafDER}, pinnedTimestamp+uint64(i))
		b, err := tls.Marshal(*l)
		if err != nil {
			t.Fatalf("marshal leaf %d: %v", i, err)
		}
		leaves[i] = hashLeaf(b)
	}

	// consult runs one consistency query against a log with the given tamper
	// function and returns ctclient's exit status.
	consult := func(first, second int, prevRoot, treeRoot []byte, tamper func([][]byte) [][]byte) int {
		log, err := startConsistencyLog(leaves, tamper)
		if err != nil {
			t.Fatalf("start loopback log: %v", err)
		}
		defer log.Close()
		return runClient(t,
			"--log_uri="+log.URL,
			fmt.Sprintf("--prev_size=%d", first),
			fmt.Sprintf("--size=%d", second),
			"--prev_hash="+hex.EncodeToString(prevRoot),
			"--tree_hash="+hex.EncodeToString(treeRoot),
			"consistency")
	}

	// Roots are computed here, from MERKLE.md section 1, never read from a
	// fixture and never taken from the candidate.
	rootAt := func(n int) []byte { return mth(leaves[:n]) }

	// --- 1. a proof between two aligned snapshots verifies ------------------
	t.Equal("consistency-proof-verifies-between-snapshots",
		consult(4, 7, rootAt(4), rootAt(7), nil), 0)

	// --- 2. ... and so does one whose old size is not a power of two --------
	// n1=3 is where the proof has to be seeded from its first element rather
	// than starting at a subtree boundary.
	t.Equal("consistency-proof-verifies-from-unaligned-old-size",
		consult(3, 7, rootAt(3), rootAt(7), nil), 0)

	// --- 3. equal sizes take an empty proof ---------------------------------
	t.Equal("equal-snapshot-sizes-accept-an-empty-proof",
		consult(7, 7, rootAt(7), rootAt(7), nil), 0)

	// --- 4. a wrong *old* root is refused -----------------------------------
	badOld := append([]byte(nil), rootAt(4)...)
	badOld[0] ^= 0xFF
	t.Equal("wrong-old-root-refused", consult(4, 7, badOld, rootAt(7), nil), 1)

	// --- 5. a wrong *new* root is refused -----------------------------------
	badNew := append([]byte(nil), rootAt(7)...)
	badNew[31] ^= 0xFF
	t.Equal("wrong-new-root-refused", consult(4, 7, rootAt(4), badNew, nil), 1)

	// --- 6. a tampered proof element is refused -----------------------------
	flipFirst := func(p [][]byte) [][]byte {
		out := make([][]byte, len(p))
		for i, e := range p {
			out[i] = append([]byte(nil), e...)
		}
		if len(out) > 0 {
			out[0][0] ^= 0x01
		}
		return out
	}
	t.Equal("tampered-proof-element-refused",
		consult(4, 7, rootAt(4), rootAt(7), flipFirst), 1)

	// --- 7. an old size larger than the new size is refused -----------------
	// The log answers 200 with an empty proof rather than an HTTP error, so
	// the refusal has to come from the verifier, not from the transport.
	t.Equal("old-size-greater-than-new-size-refused",
		consult(7, 4, rootAt(7), rootAt(4), nil), 1)

	t.Done()
}

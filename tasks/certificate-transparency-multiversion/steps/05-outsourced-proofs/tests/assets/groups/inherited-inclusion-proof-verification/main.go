// Group inherited-inclusion-proof-verification.
//
// The headline capability of this milestone is that proof verification keeps
// working after the product's own merkletree/ package is deleted. This group
// grades that directly: it verifies real RFC 6962 inclusion proofs through the
// candidate's ctutil.LogInfo entry points, which are the product's public way
// in to a proof verifier at every release (CLIENT-API.md 2.1, TEST_INTERFACE.md
// section 6).
//
// Nothing here names a library. LogInfo is handed a client.CheckLogClient of
// this driver's own, backed by a tree this driver builds from the three hashing
// rules of MERKLE.md section 1, and every expected value is computed from those
// rules with crypto/sha256 -- so the group states what the answers must be, not
// where the code that produces them lives. Repointing at a different library,
// or writing the verifier by hand, passes this group iff the answers are right.
//
// Tree size is 7: not a power of two, so the odd-sized subtree rule of
// MERKLE.md section 1 is exercised, and proof lengths differ per index.
//
// The three negatives matter as much as the positives. ctutil reports failure
// as index -1 plus an error (CLIENT-API.md 2.1); a verifier that returned 0,
// or that accepted a proof against the wrong root, would pass a group that
// only checked the happy path.
//
// This is an inherited group: the same answers are required at v1.0.13, where
// the verifier is the product's own merkletree/. The driver therefore avoids
// ct.LeafHashForLeaf (which arrives at v1.0.14) and computes leaf hashes from
// the domain-separation bytes itself.
//
// SCOPE-TIME: every timestamp is a pinned constant. SCOPE-NET: nothing leaves
// this process -- the CheckLogClient is an in-memory value, not a socket.
package main

import (
	"bytes"
	"context"
	"crypto/sha256"
	"errors"
	"fmt"
	"sync"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/ctutil"
	"github.com/google/certificate-transparency-go/tls"

	"ctverify/vhelp"
)

// pinnedTimestamp is the SCT-style timestamp of leaf 0; leaf i carries
// pinnedTimestamp+i, which is what makes the seven leaves distinct.
const pinnedTimestamp = uint64(1519737600000)

// treeSize is deliberately not a power of two.
const treeSize = 7

// --- the tree, built from MERKLE.md section 1 and nothing else --------------

// hashLeaf is SHA-256(0x00 || leaf_data).
func hashLeaf(data []byte) []byte {
	h := sha256.Sum256(append([]byte{0x00}, data...))
	return h[:]
}

// hashNode is SHA-256(0x01 || left || right).
func hashNode(l, r []byte) []byte {
	b := make([]byte, 0, 1+len(l)+len(r))
	b = append(b, 0x01)
	b = append(b, l...)
	b = append(b, r...)
	h := sha256.Sum256(b)
	return h[:]
}

// split is RFC 6962's k: the largest power of two strictly less than n.
func split(n int) int {
	k := 1
	for k<<1 < n {
		k <<= 1
	}
	return k
}

// root computes the Merkle tree hash of the given leaf hashes.
func root(leaves [][]byte) []byte {
	switch len(leaves) {
	case 0:
		h := sha256.Sum256(nil)
		return h[:]
	case 1:
		return leaves[0]
	}
	k := split(len(leaves))
	return hashNode(root(leaves[:k]), root(leaves[k:]))
}

// inclusionProof is the ordered sibling list from leaf m up to the root,
// bottom first.
func inclusionProof(leaves [][]byte, m int) [][]byte {
	if len(leaves) <= 1 {
		return nil
	}
	k := split(len(leaves))
	if m < k {
		return append(inclusionProof(leaves[:k], m), root(leaves[k:]))
	}
	return append(inclusionProof(leaves[k:], m-k), root(leaves[:k]))
}

// --- the log this driver is ------------------------------------------------

// stubLog answers the read side of client.CheckLogClient from the tree above.
// It is the whole reason the group can state an expected answer: the proof it
// serves is correct by construction, so a refusal can only come from the
// candidate's verifier.
type stubLog struct {
	leafHashes [][]byte
	rootHash   []byte

	// tamper, when set, rewrites a proof just before it is returned.
	tamper func([][]byte) [][]byte

	mu        sync.Mutex
	sthCalls  int
	askedFor  [][]byte
	lastSize  uint64
	proofCall int
}

func (s *stubLog) BaseURI() string { return "http://stub.bench.invalid/log" }

func (s *stubLog) GetSTH(_ context.Context) (*ct.SignedTreeHead, error) {
	s.mu.Lock()
	s.sthCalls++
	s.mu.Unlock()
	sth := &ct.SignedTreeHead{
		Version:   ct.V1,
		TreeSize:  treeSize,
		Timestamp: pinnedTimestamp,
	}
	copy(sth.SHA256RootHash[:], s.rootHash)
	return sth, nil
}

func (s *stubLog) GetSTHConsistency(_ context.Context, _, _ uint64) ([][]byte, error) {
	return nil, errors.New("stub log serves inclusion proofs only")
}

func (s *stubLog) GetProofByHash(_ context.Context, hash []byte, size uint64) (*ct.GetProofByHashResponse, error) {
	s.mu.Lock()
	s.proofCall++
	s.askedFor = append(s.askedFor, append([]byte(nil), hash...))
	s.lastSize = size
	s.mu.Unlock()
	for i, lh := range s.leafHashes {
		if string(lh) != string(hash) {
			continue
		}
		p := inclusionProof(s.leafHashes, i)
		if s.tamper != nil {
			p = s.tamper(p)
		}
		return &ct.GetProofByHashResponse{LeafIndex: int64(i), AuditPath: p}, nil
	}
	return nil, errors.New("no such leaf hash in the stub log")
}

func (s *stubLog) stats() (int, int, [][]byte) {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.sthCalls, s.proofCall, s.askedFor
}

func main() {
	t := vhelp.New()

	// Seven distinct leaves over one pinned certificate.
	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	leaves := make([]ct.MerkleTreeLeaf, treeSize)
	hashes := make([][]byte, treeSize)
	for i := 0; i < treeSize; i++ {
		l := ct.CreateX509MerkleTreeLeaf(ct.ASN1Cert{Data: leafDER}, pinnedTimestamp+uint64(i))
		b, err := tls.Marshal(*l)
		if err != nil {
			t.Fatalf("marshal leaf %d: %v", i, err)
		}
		leaves[i] = *l
		hashes[i] = hashLeaf(b)
	}
	want := root(hashes)

	newInfo := func(s *stubLog) *ctutil.LogInfo {
		s.leafHashes = hashes
		s.rootHash = want
		return &ctutil.LogInfo{Description: "stub", Client: s}
	}

	ctx := context.Background()

	// --- 1. every index of a non-power-of-two tree verifies -----------------
	// The proof length differs per index here (3, 3, 3, 3, 2, 2, 2), so a
	// verifier that mishandles the odd right-hand subtree fails some index.
	{
		li := newInfo(&stubLog{})
		var got []int64
		ok := true
		for i := 0; i < treeSize; i++ {
			idx, err := li.VerifyInclusionAt(ctx, leaves[i], pinnedTimestamp+uint64(i), treeSize, want)
			if err != nil {
				t.Check("inclusion-proof-verifies-at-every-index", false,
					"index %d: %v", i, err)
				ok = false
				break
			}
			got = append(got, idx)
		}
		if ok {
			t.Equal("inclusion-proof-verifies-at-every-index",
				fmt.Sprintf("%v", got), "[0 1 2 3 4 5 6]")
		}
	}

	// --- 2. the log is asked for the RFC 6962 leaf hash, at the right size --
	{
		s := &stubLog{}
		li := newInfo(s)
		if _, err := li.VerifyInclusionAt(ctx, leaves[3], pinnedTimestamp+3, treeSize, want); err != nil {
			t.Check("proof-requested-by-rfc6962-leaf-hash", false, "VerifyInclusionAt: %v", err)
		} else {
			// What the contract fixes is *which hash* is asked for, not how many
			// requests it takes (TEST_INTERFACE.md section 5): the stub answers
			// every request, so a verifier that fetches the same proof more than
			// once has still asked for the right thing. Every hash asked for must
			// be the RFC 6962 leaf hash of the leaf under test.
			_, calls, asked := s.stats()
			wrong := 0
			for _, a := range asked {
				if !bytes.Equal(a, hashes[3]) {
					wrong++
				}
			}
			switch {
			case calls < 1 || len(asked) < 1:
				t.Check("proof-requested-by-rfc6962-leaf-hash", false,
					"get-proof-by-hash was never called (calls=%d)", calls)
			case wrong > 0:
				t.Check("proof-requested-by-rfc6962-leaf-hash", false,
					"%d of %d hashes asked for were not the leaf's RFC 6962 hash", wrong, len(asked))
			default:
				t.EqualBytes("proof-requested-by-rfc6962-leaf-hash", asked[0], hashes[3])
			}
		}
	}

	// --- 3. a proof against the wrong root is refused, with index -1 --------
	{
		li := newInfo(&stubLog{})
		bad := append([]byte(nil), want...)
		bad[0] ^= 0xFF
		idx, err := li.VerifyInclusionAt(ctx, leaves[2], pinnedTimestamp+2, treeSize, bad)
		t.Equal("wrong-root-hash-refused",
			fmt.Sprintf("index=%d refused=%v", idx, err != nil), "index=-1 refused=true")
	}

	// --- 4. a tampered audit path is refused --------------------------------
	{
		s := &stubLog{tamper: func(p [][]byte) [][]byte {
			out := make([][]byte, len(p))
			for i, e := range p {
				out[i] = append([]byte(nil), e...)
			}
			if len(out) > 0 {
				out[0][0] ^= 0x01
			}
			return out
		}}
		li := newInfo(s)
		idx, err := li.VerifyInclusionAt(ctx, leaves[5], pinnedTimestamp+5, treeSize, want)
		t.Equal("tampered-audit-path-refused",
			fmt.Sprintf("index=%d refused=%v", idx, err != nil), "index=-1 refused=true")
	}

	// --- 5. a short audit path is refused, not silently accepted ------------
	{
		s := &stubLog{tamper: func(p [][]byte) [][]byte {
			if len(p) == 0 {
				return p
			}
			return p[:len(p)-1]
		}}
		li := newInfo(s)
		idx, err := li.VerifyInclusionAt(ctx, leaves[1], pinnedTimestamp+1, treeSize, want)
		t.Equal("truncated-audit-path-refused",
			fmt.Sprintf("index=%d refused=%v", idx, err != nil), "index=-1 refused=true")
	}

	// --- 6. VerifyInclusion takes the tree from the log's current STH -------
	{
		s := &stubLog{}
		li := newInfo(s)
		idx, err := li.VerifyInclusion(ctx, leaves[4], pinnedTimestamp+4)
		if err != nil {
			t.Check("verify-inclusion-fetches-the-current-sth", false, "VerifyInclusion: %v", err)
		} else {
			sthCalls, _, _ := s.stats()
			cached := li.LastSTH()
			cachedSize := uint64(0)
			if cached != nil {
				cachedSize = cached.TreeSize
			}
			t.Equal("verify-inclusion-fetches-the-current-sth",
				fmt.Sprintf("index=%d sthcalls=%d cached=%d", idx, sthCalls, cachedSize),
				fmt.Sprintf("index=4 sthcalls=1 cached=%d", uint64(treeSize)))
		}
	}

	// --- 7. VerifyInclusionLatest reuses a cached STH instead of fetching ---
	{
		s := &stubLog{}
		li := newInfo(s)
		pinned := &ct.SignedTreeHead{Version: ct.V1, TreeSize: treeSize, Timestamp: pinnedTimestamp}
		copy(pinned.SHA256RootHash[:], want)
		li.SetSTH(pinned)
		idx, err := li.VerifyInclusionLatest(ctx, leaves[6], pinnedTimestamp+6)
		if err != nil {
			t.Check("verify-inclusion-latest-uses-the-cached-sth", false, "VerifyInclusionLatest: %v", err)
		} else {
			sthCalls, _, _ := s.stats()
			t.Equal("verify-inclusion-latest-uses-the-cached-sth",
				fmt.Sprintf("index=%d sthcalls=%d", idx, sthCalls), "index=6 sthcalls=0")
		}
	}

	t.Done()
}

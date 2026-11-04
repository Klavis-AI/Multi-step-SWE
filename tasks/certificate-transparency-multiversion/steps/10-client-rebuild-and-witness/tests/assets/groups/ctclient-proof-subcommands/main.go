// Group ctclient-proof-subcommands.
//
// v1.1.3 splits proof retrieval into two Cobra subcommands,
// `get-inclusion-proof` and `get-consistency-proof`, each with its own flag
// set parsed after the command word, and each printing a fixed rendering and
// -- when it has enough material -- a verification line. This group builds a
// four-leaf RFC 6962 Merkle tree with nothing but crypto/sha256, serves the
// matching proofs over a loopback socket, and compares the candidate's whole
// stdout byte for byte.
//
// At v1.1.2 the spellings are the positional words `inclusion` and
// `consistency`, flags precede them, and `--size`/`--prev_size` do not exist
// in this grammar, so no case here can pass.
package main

import (
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"fmt"
	"net/url"
	"strings"

	ct "github.com/google/certificate-transparency-go"

	"ctverify/vhelp"
)

// pinnedLeaves are the four leaf payloads of the pinned tree.
var pinnedLeaves = [][]byte{
	[]byte("bench-leaf-0"),
	[]byte("bench-leaf-1"),
	[]byte("bench-leaf-2"),
	[]byte("bench-leaf-3"),
}

// RFC 6962 s2.1 hashing, written out here so the expectations owe nothing to
// the product.
func leafHash(b []byte) []byte {
	h := sha256.Sum256(append([]byte{0x00}, b...))
	return h[:]
}

func nodeHash(l, r []byte) []byte {
	in := make([]byte, 0, 1+len(l)+len(r))
	in = append(in, 0x01)
	in = append(in, l...)
	in = append(in, r...)
	h := sha256.Sum256(in)
	return h[:]
}

const pinnedTimestamp = int64(1500000000000) // 2017-07-14 02:40:00 +0000 UTC

func b64(b []byte) string { return base64.StdEncoding.EncodeToString(b) }

func main() {
	t := vhelp.New()
	requireClient(t)

	lh := make([][]byte, 4)
	for i, d := range pinnedLeaves {
		lh[i] = leafHash(d)
	}
	n01 := nodeHash(lh[0], lh[1]) // == root at size 2
	n23 := nodeHash(lh[2], lh[3])
	root4 := nodeHash(n01, n23)

	// Inclusion of leaf 0 in a tree of size 4 is [LH(1), n23].
	audit := [][]byte{lh[1], n23}
	// Consistency from size 2 to size 4 is [n23].
	consistency := [][]byte{n23}

	proofJSON := fmt.Sprintf(`{"leaf_index":0,"audit_path":[%q,%q]}`, b64(audit[0]), b64(audit[1]))
	badProofJSON := fmt.Sprintf(`{"leaf_index":0,"audit_path":[%q,%q]}`, b64(audit[0]), b64(lh[0]))
	consistencyJSON := fmt.Sprintf(`{"consistency":[%q]}`, b64(consistency[0]))
	sthJSON := fmt.Sprintf(
		`{"tree_size":4,"timestamp":%d,"sha256_root_hash":%q,"tree_head_signature":"BAMAAjBF"}`,
		pinnedTimestamp, b64(root4))

	wantProofBlock := fmt.Sprintf("Inclusion proof for index 0 in tree of size 4:\n  %x\n  %x\n",
		audit[0], audit[1])
	wantProofQuery := "hash=" + url.QueryEscape(b64(lh[0])) + "&tree_size=4"

	// 1. --size pins the tree size, so no STH is fetched and no verification
	//    line is printed.
	s := newStubLog()
	defer s.close()
	s.json("/ct/v1/get-proof-by-hash", 200, proofJSON)
	r := runClient("get-inclusion-proof", "--log_uri="+s.URL,
		"--leaf_hash="+hex.EncodeToString(lh[0]), "--size=4")
	t.Check("inclusion-fixed-size-exit", r.code == 0, "exit=%d stderr=%s", r.code, oneLine(r.stderr))
	t.Equal("inclusion-fixed-size-stdout", r.stdout, wantProofBlock)
	t.Equal("inclusion-fixed-size-requests", strings.Join(s.requests(), "|"),
		"GET /ct/v1/get-proof-by-hash?"+wantProofQuery)

	// 2. A base64 --leaf_hash is the same hash.
	s2 := newStubLog()
	defer s2.close()
	s2.json("/ct/v1/get-proof-by-hash", 200, proofJSON)
	runClient("get-inclusion-proof", "--log_uri="+s2.URL,
		"--leaf_hash="+b64(lh[0]), "--size=4")
	t.Equal("inclusion-base64-leaf-hash-requests", strings.Join(s2.requests(), "|"),
		"GET /ct/v1/get-proof-by-hash?"+wantProofQuery)

	// 3. Without --size the tool fetches an STH first and then verifies the
	//    proof against it, printing the verification line.
	s3 := newStubLog()
	defer s3.close()
	s3.json("/ct/v1/get-sth", 200, sthJSON)
	s3.json("/ct/v1/get-proof-by-hash", 200, proofJSON)
	r3 := runClient("get-inclusion-proof", "--log_uri="+s3.URL, "--leaf_hash="+hex.EncodeToString(lh[0]))
	t.Check("inclusion-verified-exit", r3.code == 0, "exit=%d stderr=%s", r3.code, oneLine(r3.stderr))
	t.Equal("inclusion-verified-stdout", r3.stdout,
		wantProofBlock+fmt.Sprintf("Verified that hash %x + proof = root hash %x\n", lh[0], root4))
	t.Equal("inclusion-verified-requests", strings.Join(s3.requests(), "|"),
		"GET /ct/v1/get-sth|GET /ct/v1/get-proof-by-hash?"+wantProofQuery)

	// 4. A proof that does not reconstruct the STH root is fatal.
	s4 := newStubLog()
	defer s4.close()
	s4.json("/ct/v1/get-sth", 200, sthJSON)
	s4.json("/ct/v1/get-proof-by-hash", 200, badProofJSON)
	r4 := runClient("get-inclusion-proof", "--log_uri="+s4.URL, "--leaf_hash="+hex.EncodeToString(lh[0]))
	t.Check("inclusion-bad-proof-is-fatal", r4.code != 0, "exit=%d stdout=%q", r4.code, r4.stdout)

	// 5. --cert_chain + --timestamp derives the leaf hash from a certificate.
	chainLeaf := ct.ASN1Cert{Data: vhelp.ReadFixture("minted", "leaf-valid.der")}
	entry := ct.CreateX509MerkleTreeLeaf(chainLeaf, uint64(pinnedTimestamp))
	derived, err := ct.LeafHashForLeaf(entry)
	if err != nil {
		t.Fatalf("LeafHashForLeaf: %v", err)
	}
	s5 := newStubLog()
	defer s5.close()
	s5.json("/ct/v1/get-proof-by-hash", 200, proofJSON)
	r5 := runClient("get-inclusion-proof", "--log_uri="+s5.URL,
		"--cert_chain="+vhelp.Fixture("minted", "leaf-valid.pem"),
		fmt.Sprintf("--timestamp=%d", pinnedTimestamp), "--size=4")
	t.Check("inclusion-cert-chain-exit", r5.code == 0, "exit=%d stderr=%s", r5.code, oneLine(r5.stderr))
	t.Equal("inclusion-cert-chain-requests", strings.Join(s5.requests(), "|"),
		"GET /ct/v1/get-proof-by-hash?hash="+url.QueryEscape(b64(derived[:]))+"&tree_size=4")

	// 6. get-consistency-proof without hashes prints the proof only.
	wantConsBlock := fmt.Sprintf("Consistency proof from size 2 to size 4:\n  %x\n", consistency[0])
	s6 := newStubLog()
	defer s6.close()
	s6.json("/ct/v1/get-sth-consistency", 200, consistencyJSON)
	r6 := runClient("get-consistency-proof", "--log_uri="+s6.URL, "--size=4", "--prev_size=2")
	t.Check("consistency-exit", r6.code == 0, "exit=%d stderr=%s", r6.code, oneLine(r6.stderr))
	t.Equal("consistency-stdout", r6.stdout, wantConsBlock)
	t.Equal("consistency-requests", strings.Join(s6.requests(), "|"),
		"GET /ct/v1/get-sth-consistency?first=2&second=4")

	// 7. With both tree hashes the proof is verified and a second line printed.
	s7 := newStubLog()
	defer s7.close()
	s7.json("/ct/v1/get-sth-consistency", 200, consistencyJSON)
	r7 := runClient("get-consistency-proof", "--log_uri="+s7.URL, "--size=4", "--prev_size=2",
		"--prev_hash="+hex.EncodeToString(n01), "--tree_hash="+hex.EncodeToString(root4))
	t.Equal("consistency-verified-stdout", r7.stdout,
		wantConsBlock+fmt.Sprintf("Verified that hash %x @2 + proof = hash %x @4\n", n01, root4))

	// 8. A mismatching pair of tree hashes is fatal.
	s8 := newStubLog()
	defer s8.close()
	s8.json("/ct/v1/get-sth-consistency", 200, consistencyJSON)
	r8 := runClient("get-consistency-proof", "--log_uri="+s8.URL, "--size=4", "--prev_size=2",
		"--prev_hash="+hex.EncodeToString(n01), "--tree_hash="+hex.EncodeToString(n23))
	t.Check("consistency-mismatch-is-fatal", r8.code != 0, "exit=%d stdout=%q", r8.code, r8.stdout)

	// 9. Only one of the two hashes is refused, and refused before the proof
	//    is fetched rather than after. The diagnosis is required to exist and
	//    not to be compared word for word (TEST_INTERFACE.md §3.3).
	s9 := newStubLog()
	defer s9.close()
	s9.json("/ct/v1/get-sth-consistency", 200, consistencyJSON)
	r9 := runClient("get-consistency-proof", "--log_uri="+s9.URL, "--size=4", "--prev_size=2",
		"--prev_hash="+hex.EncodeToString(n01))
	t.Check("consistency-one-hash-is-fatal",
		r9.code != 0 && r9.stdout == "" && strings.TrimSpace(r9.stderr) != "",
		"exit=%d stdout=%q stderr=%s", r9.code, r9.stdout, oneLine(r9.stderr))

	// 10. A missing --size is refused before any request is made.
	s10 := newStubLog()
	defer s10.close()
	s10.json("/ct/v1/get-sth-consistency", 200, consistencyJSON)
	r10 := runClient("get-consistency-proof", "--log_uri="+s10.URL, "--prev_size=2")
	t.Check("consistency-missing-size-is-fatal",
		r10.code != 0 && r10.stdout == "" && len(s10.requests()) == 0 &&
			strings.TrimSpace(r10.stderr) != "",
		"exit=%d requests=%v stderr=%s", r10.code, s10.requests(), oneLine(r10.stderr))

	// 11. The same for a missing --prev_size: CLI.md §3.1 requires *both*
	//     bounds to be positive, at every release, so the omission is refused
	//     before the log is asked anything rather than defaulted to zero and
	//     fetched.
	s11 := newStubLog()
	defer s11.close()
	s11.json("/ct/v1/get-sth-consistency", 200, consistencyJSON)
	r11 := runClient("get-consistency-proof", "--log_uri="+s11.URL, "--size=4")
	t.Check("consistency-missing-prev-size-is-fatal",
		r11.code != 0 && r11.stdout == "" && len(s11.requests()) == 0 &&
			strings.TrimSpace(r11.stderr) != "",
		"exit=%d requests=%v stderr=%s", r11.code, s11.requests(), oneLine(r11.stderr))

	t.Done()
}

func oneLine(s string) string {
	return strings.ReplaceAll(strings.TrimSpace(s), "\n", " / ")
}

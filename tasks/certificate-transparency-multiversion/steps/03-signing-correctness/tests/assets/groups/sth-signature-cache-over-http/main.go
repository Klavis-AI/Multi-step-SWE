// sth-signature-cache-over-http observes the v1.0.7 advisory where the
// advisory is actually about: what a running log emits on get-sth.
//
// v1.0.10 gives each LogContext a one-slot cache of the last STH signature,
// keyed on the exact RFC 6962 signature input. The two halves of the property
// are checked separately here:
//
//   - stability. Over an unchanged backend root the log must emit the same
//     tree_head_signature bytes every time. ECDSA draws a fresh nonce per
//     signature, so a tree without the cache emits different bytes on every
//     call even though each one is a correct signature.
//   - no reuse across distinct inputs. Each of the three fields that enter the
//     signature input -- root hash, tree size, timestamp -- is varied on its
//     own, and each variation must produce a different signature that verifies
//     over its own input. A cache keyed too loosely would hand the old
//     signature back for the new tree head, which is the bug the advisory is
//     about.
//
// The cache must also be invisible: nothing about it may show up in the
// response beyond the signature bytes themselves.
package main

import (
	"encoding/hex"
	"fmt"
	"net/http"

	"ctverify/vhelp"
)

type sample struct {
	name string
	body []byte
	sth  *sthJSON
}

func main() {
	t := vhelp.New()
	pub := loadPublicKey(t)

	h := startPersonality(t)
	defer h.stop()

	// fetch returns one get-sth observation, aborting the group if the log
	// cannot serve one at all -- a group that silently observed nothing would
	// be a vacuous pass.
	fetch := func(name string) sample {
		r := h.get(t, "/ct/v1/get-sth")
		if r.status != http.StatusOK {
			h.stop()
			t.Fatalf("get-sth at %s returned HTTP %d: %s", name, r.status, string(r.body))
		}
		s, err := parseSTH(r.body)
		if err != nil {
			h.stop()
			t.Fatalf("get-sth at %s: %v", name, err)
		}
		return sample{name: name, body: r.body, sth: s}
	}

	verifies := func(s sample) error {
		hash, sig, raw, err := splitDigitallySigned(s.sth.TreeHeadSignature)
		if err != nil {
			return err
		}
		if hash != 4 || sig != 3 {
			return fmt.Errorf("digitally-signed says hash=%d sig=%d, want 4 (sha256) and 3 (ecdsa)", hash, sig)
		}
		return verifyECDSA(pub, sthSignatureInput(s.sth.Timestamp, s.sth.TreeSize, s.sth.SHA256RootHash), raw)
	}

	// --- R0: the root the stub starts with ---------------------------------
	rootA := kitRootHash("root-a")
	r0 := fetch("r0")
	t.Check("fields-track-root-r0",
		r0.sth.TreeSize == uint64(kitTreeSize) && r0.sth.Timestamp == kitRootMilli && sameBytes(r0.sth.SHA256RootHash, rootA),
		"get-sth served tree_size=%d timestamp=%d root=%x; want %d, %d, %x",
		r0.sth.TreeSize, r0.sth.Timestamp, r0.sth.SHA256RootHash, kitTreeSize, kitRootMilli, rootA)
	t.Case("signature-verifies-r0", verifies(r0))

	stable := true
	for i := 0; i < 3; i++ {
		again := fetch("r0")
		if !sameBytes(again.sth.TreeHeadSignature, r0.sth.TreeHeadSignature) {
			stable = false
			t.Check("repeated-get-sth-identical-r0", false,
				"call %d over an unchanged root returned %x, first call returned %x",
				i+2, again.sth.TreeHeadSignature, r0.sth.TreeHeadSignature)
			break
		}
	}
	if stable {
		t.Check("repeated-get-sth-identical-r0", true, "")
	}

	// --- R1: only the root hash changes ------------------------------------
	rootB := kitRootHash("root-b")
	h.be.setRoot(kitRootNanos, kitTreeSize, rootB)
	r1 := fetch("r1")
	t.Check("fields-track-root-r1",
		r1.sth.TreeSize == uint64(kitTreeSize) && r1.sth.Timestamp == kitRootMilli && sameBytes(r1.sth.SHA256RootHash, rootB),
		"get-sth served tree_size=%d timestamp=%d root=%x; want %d, %d, %x",
		r1.sth.TreeSize, r1.sth.Timestamp, r1.sth.SHA256RootHash, kitTreeSize, kitRootMilli, rootB)
	t.Check("new-signature-after-root-hash-change",
		!sameBytes(r1.sth.TreeHeadSignature, r0.sth.TreeHeadSignature),
		"a tree head with a different sha256_root_hash was served the previous signature %x", r1.sth.TreeHeadSignature)
	t.Case("signature-verifies-r1", verifies(r1))

	// --- R2: only the tree size changes ------------------------------------
	h.be.setRoot(kitRootNanos, kitTreeSize+1, rootB)
	r2 := fetch("r2")
	t.Check("fields-track-root-r2",
		r2.sth.TreeSize == uint64(kitTreeSize+1) && r2.sth.Timestamp == kitRootMilli && sameBytes(r2.sth.SHA256RootHash, rootB),
		"get-sth served tree_size=%d timestamp=%d root=%x; want %d, %d, %x",
		r2.sth.TreeSize, r2.sth.Timestamp, r2.sth.SHA256RootHash, kitTreeSize+1, kitRootMilli, rootB)
	t.Check("new-signature-after-tree-size-change",
		!sameBytes(r2.sth.TreeHeadSignature, r1.sth.TreeHeadSignature),
		"a tree head with a different tree_size was served the previous signature %x", r2.sth.TreeHeadSignature)
	t.Case("signature-verifies-r2", verifies(r2))

	// --- R3: only the timestamp changes, by one millisecond ----------------
	h.be.setRoot(kitRootNanos+1000000, kitTreeSize+1, rootB)
	r3 := fetch("r3")
	t.Check("fields-track-root-r3",
		r3.sth.TreeSize == uint64(kitTreeSize+1) && r3.sth.Timestamp == kitRootMilli+1 && sameBytes(r3.sth.SHA256RootHash, rootB),
		"get-sth served tree_size=%d timestamp=%d root=%x; want %d, %d, %x",
		r3.sth.TreeSize, r3.sth.Timestamp, r3.sth.SHA256RootHash, kitTreeSize+1, kitRootMilli+1, rootB)
	t.Check("new-signature-after-timestamp-change",
		!sameBytes(r3.sth.TreeHeadSignature, r2.sth.TreeHeadSignature),
		"a tree head with a different timestamp was served the previous signature %x", r3.sth.TreeHeadSignature)
	t.Case("signature-verifies-r3", verifies(r3))

	// No two of the four distinct signing inputs may share a signature.
	seen := map[string]string{}
	clash := ""
	for _, s := range []sample{r0, r1, r2, r3} {
		k := hex.EncodeToString(s.sth.TreeHeadSignature)
		if prev, dup := seen[k]; dup {
			clash = fmt.Sprintf("%s and %s share signature %s", prev, s.name, k)
			break
		}
		seen[k] = s.name
	}
	t.Check("four-distinct-inputs-four-distinct-signatures", clash == "", "%s", clash)

	// The whole response body, not just the signature, must repeat exactly:
	// the cache is an implementation detail and must not surface anywhere.
	r3again := fetch("r3")
	t.Check("cache-invisible-response-byte-identical",
		sameBytes(r3.body, r3again.body),
		"two get-sth responses over an unchanged root differ: %q then %q", string(r3.body), string(r3again.body))

	t.Done()
}

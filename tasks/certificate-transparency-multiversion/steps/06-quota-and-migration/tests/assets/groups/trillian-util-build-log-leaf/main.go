// Group trillian-util-build-log-leaf.
//
// The other half of the new trillian/util/log_leaf.go: BuildLogLeaf turns an
// RFC 6962 MerkleTreeLeaf plus the chain it was submitted with into the
// trillian.LogLeaf the personality queues. v1.0.19 exports it so the CTFE and
// the new migrillian build identical leaves; before this release the same code
// was inline and unexported in the CTFE.
//
// Four fields, each with its own contract, each graded separately:
//
//	LeafValue        the TLS encoding of the Merkle tree leaf
//	LeafIdentityHash SHA-256 of the submitted certificate's DER -- the
//	                 deduplication key, NOT the leaf hash
//	LeafIndex        passed through untouched
//	ExtraData        the chain encoding, shape chosen by isPrecert
//
// Every expectation is pinned to the bytes the Oracle produces for fixture
// certificates, with a fixed timestamp in the leaf so nothing depends on a
// clock. The last two cases vary one argument at a time -- isPrecert, then the
// chain -- and assert that only ExtraData moves, which is what makes the
// per-field contracts above separable rather than one lucky digest.
//
// The seventh case closes the loop the export was made for: the CTFE must now
// build the leaf it queues with this very function. It boots the candidate's
// own ct_server against the shared _kit backend stub (serverkit.go), submits
// one chain over the log's HTTP surface, rebuilds the leaf here at the
// timestamp the SCT reports, and compares the bytes the backend actually
// received. It arrived from `gate-endtoend`, which under `reward = gate x
// score` may carry only behaviour the step-05 anchor already has; nothing
// else in the corpus grades "the personality queues what BuildLogLeaf
// produces", so it lands here rather than being dropped.
//
// SCOPE-BACKEND: the group's own in-process stub on a :0 loopback port; see
// serverkit.go. SCOPE-NET: loopback only. SCOPE-TIME: cases 1-6 read no clock
// at all; case 7's only clock-derived value is the SCT timestamp, which is
// never asserted -- it is fed back into BuildLogLeaf so that the comparison is
// over leaf bytes, and upstream reads that clock once per submission for both
// the SCT and the leaf.
//
// trillian/util exists at v1.0.17 but holds only the time source, so this
// driver does not compile against the previous tree.
package main

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"
	tutil "github.com/google/certificate-transparency-go/trillian/util"

	"ctverify/vhelp"
)

// Pinned: the Oracle's output over the fixtures below, at this timestamp.
// `logPrefix` -- only used for BuildLogLeaf's own error messages; it does not
// reach the leaf, and the pinned values below are independent of it -- comes
// from serverkit.go, which configures the personality on the same prefix.
const (
	pinnedLeafTimestamp = uint64(1519737600000)

	leafValueLen = 513
	leafValueSHA = "0b7a4c7df5e92e2b2ae4fe6898974d5460a65316040a67a51834be96905b4288"

	// SHA-256 of minted/leaf-valid.der, recomputed by the driver as well.
	identityHashHex = "bb24a3abf3006f8042c1a81cb0d2d4fcb2553e10b3e86bebbbef870c39e98241"

	certExtraLen    = 902
	certExtraSHA    = "448930ecfb8d39b0814707067eb67a643a2c093a24baf9e53b9e77ee21591054"
	precertExtraLen = 1401
	precertExtraSHA = "f1c66adb9f429221e6c0e337f73098d2af0363707ec0c31f94c3be284171dd5b"
	emptyChainHex   = "000000"
)

func digest(b []byte) string {
	h := sha256.Sum256(b)
	return hex.EncodeToString(h[:])
}

func shape(b []byte) string {
	return fmt.Sprintf("len=%d sha256=%s", len(b), digest(b))
}

func main() {
	t := vhelp.New()

	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	intDER := vhelp.ReadFixture("minted", "bench-intermediate.der")
	rootDER := vhelp.ReadFixture("minted", "bench-root.der")

	cert := ct.ASN1Cert{Data: leafDER}
	chain := []ct.ASN1Cert{{Data: intDER}, {Data: rootDER}}

	// A fixed RFC 6962 leaf: pinned timestamp, so LeafValue is pinned bytes.
	mtl := ct.MerkleTreeLeaf{
		Version:  ct.V1,
		LeafType: ct.TimestampedEntryLeafType,
		TimestampedEntry: &ct.TimestampedEntry{
			Timestamp: pinnedLeafTimestamp,
			EntryType: ct.X509LogEntryType,
			X509Entry: &ct.ASN1Cert{Data: leafDER},
		},
	}

	leaf, err := tutil.BuildLogLeaf(logPrefix, mtl, 7, cert, chain, false)
	if err != nil {
		t.Fatalf("BuildLogLeaf(cert): %v", err)
	}

	// --- 1. LeafValue is the TLS encoding of the leaf it was handed ---------
	// Both pinned and recomputed: a tree that encoded some other structure of
	// the same length cannot satisfy both halves.
	marshalled, merr := tls.Marshal(mtl)
	t.Equal("leaf-value-is-the-tls-encoded-merkle-tree-leaf",
		fmt.Sprintf("%s marshalErr=%v matchesTLSMarshal=%v", shape(leaf.LeafValue), merr,
			string(leaf.LeafValue) == string(marshalled)),
		fmt.Sprintf("len=%d sha256=%s marshalErr=%v matchesTLSMarshal=%v",
			leafValueLen, leafValueSHA, error(nil), true))

	// --- 2. LeafIdentityHash is SHA-256 of the certificate, not of the leaf --
	// The distinction matters: this is the dedup key, so it must not depend on
	// the timestamp that is inside LeafValue.
	certHash := sha256.Sum256(leafDER)
	t.Equal("leaf-identity-hash-is-sha256-of-the-submitted-certificate",
		fmt.Sprintf("hash=%x equalsCertDigest=%v equalsLeafDigest=%v",
			leaf.LeafIdentityHash, hex.EncodeToString(certHash[:]) == hex.EncodeToString(leaf.LeafIdentityHash),
			digest(leaf.LeafValue) == hex.EncodeToString(leaf.LeafIdentityHash)),
		fmt.Sprintf("hash=%s equalsCertDigest=%v equalsLeafDigest=%v", identityHashHex, true, false))

	// --- 3. LeafIndex is carried through, not invented -----------------------
	zeroIdx, err := tutil.BuildLogLeaf(logPrefix, mtl, 0, cert, chain, false)
	if err != nil {
		t.Fatalf("BuildLogLeaf(index 0): %v", err)
	}
	t.Equal("leaf-index-is-carried-through",
		fmt.Sprintf("%d %d", leaf.LeafIndex, zeroIdx.LeafIndex), "7 0")

	// --- 4. ExtraData is the chain encoding ---------------------------------
	var decoded ct.CertificateChain
	rest, derr := tls.Unmarshal(leaf.ExtraData, &decoded)
	entries := ""
	for _, e := range decoded.Entries {
		entries += hex.EncodeToString(e.Data) + " "
	}
	t.Equal("extra-data-is-the-encoded-issuer-chain",
		fmt.Sprintf("%s err=%v trailing=%d entries=%s", shape(leaf.ExtraData), derr, len(rest), entries),
		fmt.Sprintf("len=%d sha256=%s err=%v trailing=%d entries=%s %s ",
			certExtraLen, certExtraSHA, error(nil), 0,
			hex.EncodeToString(intDER), hex.EncodeToString(rootDER)))

	// --- 5. isPrecert changes the extra data and nothing else ---------------
	// BuildLogLeaf treats the Merkle leaf and the certificate opaquely: the
	// flag selects PrecertChainEntry over CertificateChain, and LeafValue and
	// LeafIdentityHash are unaffected.
	pre, err := tutil.BuildLogLeaf(logPrefix, mtl, 7, cert, chain, true)
	if err != nil {
		t.Fatalf("BuildLogLeaf(precert): %v", err)
	}
	t.Equal("precert-flag-selects-the-precert-chain-encoding",
		fmt.Sprintf("extra=%s value=%s id=%x", shape(pre.ExtraData), digest(pre.LeafValue), pre.LeafIdentityHash),
		fmt.Sprintf("extra=len=%d sha256=%s value=%s id=%s",
			precertExtraLen, precertExtraSHA, leafValueSHA, identityHashHex))

	// --- 6. an empty chain still produces a well-formed leaf -----------------
	// ExtraData becomes the empty 24-bit vector, not nil, and the two fields
	// that do not depend on the chain are unchanged.
	bare, err := tutil.BuildLogLeaf(logPrefix, mtl, 7, cert, nil, false)
	if err != nil {
		t.Fatalf("BuildLogLeaf(no chain): %v", err)
	}
	t.Equal("empty-chain-leaf-keeps-its-value-and-identity-hash",
		fmt.Sprintf("extra=%s value=%s id=%x", hex.EncodeToString(bare.ExtraData), digest(bare.LeafValue), bare.LeafIdentityHash),
		fmt.Sprintf("extra=%s value=%s id=%s", emptyChainHex, leafValueSHA, identityHashHex))

	// --- 7. and the personality queues exactly this leaf --------------------
	// The point of exporting the builder is that the CTFE stops open-coding
	// it, so the bytes ct_server hands the backend for a submission must be
	// BuildLogLeaf's output for the same submission. Driven over the log's
	// real HTTP surface with the candidate's own binary, and read back off
	// the pinned stub (SCOPE-BACKEND).
	const queuedLeafCase = "queued-leaf-is-what-the-exported-leaf-builder-produces"

	h := startPersonality(t, nil, "")
	defer h.stop()

	submission, jerr := json.Marshal(ct.AddChainRequest{Chain: [][]byte{leafDER, intDER}})
	if jerr != nil {
		t.Fatalf("marshal add-chain request: %v", jerr)
	}
	res := h.postJSON(t, h.logURL("/ct/v1/add-chain"), submission)
	var acr ct.AddChainResponse
	rspErr := json.Unmarshal(res.body, &acr)
	switch {
	case res.status != 200 || rspErr != nil:
		t.Check(queuedLeafCase, false, "add-chain status=%d decode=%v body=%s",
			res.status, rspErr, lastBytes(res.body, 120))
	default:
		// The leaf the CTFE builds carries only the submitted certificate and
		// the SCT's own timestamp; the index is 0 because the leaf has not
		// been sequenced yet.
		want, berr := tutil.BuildLogLeaf(logPrefix, ct.MerkleTreeLeaf{
			Version:  ct.V1,
			LeafType: ct.TimestampedEntryLeafType,
			TimestampedEntry: &ct.TimestampedEntry{
				Timestamp: acr.Timestamp,
				EntryType: ct.X509LogEntryType,
				X509Entry: &ct.ASN1Cert{Data: leafDER},
			},
		}, 0, ct.ASN1Cert{Data: leafDER}, []ct.ASN1Cert{{Data: intDER}}, false)
		if berr != nil {
			t.Check(queuedLeafCase, false, "BuildLogLeaf: %v", berr)
		} else {
			calls, queued, _ := h.backend.queueSnapshot()
			t.Equal(queuedLeafCase,
				fmt.Sprintf("queued=%d leaf=%x", calls, queued),
				fmt.Sprintf("queued=%d leaf=%x", 1, want.LeafValue))
		}
	}

	t.Done()
}

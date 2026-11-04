// leaf-rehydration-round-trip observes the milestone's own critical property:
// a leaf that carries only a chain hash must rehydrate back to a
// byte-identical extra_data, so that the wire format does not change when a
// log opts in to front-end issuance chain storage.
//
// The equation is TEST_INTERFACE section 6's, over the stored form CONFIG.md
// section 2.6 fixes: the store holds asn1.Marshal of the certificates after
// the leaf, under the key sha256 of those same bytes; decoding a hash-bearing
// extra_data, looking the hash up, ASN.1-decoding the result and re-encoding
// it as CertificateChain (or PrecertChainEntry) must reproduce exactly what
// ExtraDataForChain would have written for the same submission.
//
// SCOPE-DB: no database server runs. The store is the driver's own in-memory
// implementation of the candidate's storage.IssuanceChainStorage, which is the
// only way to reach the CTFE-side data path offline -- the product's own
// store dials MySQL at construction. Everything the round trip passes through
// is candidate code: both extra-data encoders, the TLS codec over all four
// chain structures, the forked ASN.1 codec, and the storage interface.
//
// SCOPE-TIME: every certificate is a pinned fixture and the Merkle leaf
// carries a pinned timestamp, so every byte compared below is fixed.
package main

import (
	"bytes"
	"context"
	"crypto/sha256"
	"errors"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/asn1"
	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/certificate-transparency-go/trillian/ctfe/storage"
	"github.com/google/certificate-transparency-go/trillian/util"
	"github.com/google/trillian"

	"ctverify/vhelp"
)

const (
	pinnedTimestampMS = uint64(1500000000000)
	pinnedLeafIndex   = int64(4242)
	logPrefix         = "bench"
)

// memStore is a driver-owned IssuanceChainStorage. It compiles only if the
// candidate's interface is the documented two-method one.
type memStore struct{ m map[string][]byte }

func (s *memStore) FindByKey(_ context.Context, key []byte) ([]byte, error) {
	v, ok := s.m[string(key)]
	if !ok {
		return nil, errors.New("no such issuance chain")
	}
	return append([]byte(nil), v...), nil
}

func (s *memStore) Add(_ context.Context, key []byte, chain []byte) error {
	s.m[string(key)] = append([]byte(nil), chain...)
	return nil
}

// store takes the documented stored form of an issuance chain -- the DER of
// the certificates after the leaf -- and files it under its SHA-256, returning
// that key. This is the derivation CONFIG.md section 2.6 states.
func store(t *vhelp.T, ctx context.Context, s storage.IssuanceChainStorage, issuers []ct.ASN1Cert) []byte {
	der, err := asn1.Marshal(issuers)
	if err != nil {
		t.Fatalf("asn1.Marshal of the issuance chain: %v", err)
	}
	sum := sha256.Sum256(der)
	if err := s.Add(ctx, sum[:], der); err != nil {
		t.Fatalf("Add the issuance chain: %v", err)
	}
	return sum[:]
}

// rehydrate is the read path, written out of the documents alone: decode the
// hash-bearing extra data, look the hash up, ASN.1-decode what comes back, and
// re-encode the classic structure. An empty hash means there is no chain to
// look up and yields a zero-length chain rather than an error.
func rehydrate(ctx context.Context, s storage.IssuanceChainStorage, extra []byte, isPrecert bool) ([]byte, error) {
	var hash []byte
	var pre ct.ASN1Cert
	if isPrecert {
		var ph ct.PrecertChainEntryHash
		rest, err := tls.Unmarshal(extra, &ph)
		if err != nil {
			return nil, err
		}
		if len(rest) != 0 {
			return nil, errors.New("trailing bytes after PrecertChainEntryHash")
		}
		hash, pre = ph.IssuanceChainHash, ph.PreCertificate
	} else {
		var ch ct.CertificateChainHash
		rest, err := tls.Unmarshal(extra, &ch)
		if err != nil {
			return nil, err
		}
		if len(rest) != 0 {
			return nil, errors.New("trailing bytes after CertificateChainHash")
		}
		hash = ch.IssuanceChainHash
	}

	var chain []ct.ASN1Cert
	if len(hash) > 0 {
		der, err := s.FindByKey(ctx, hash)
		if err != nil {
			return nil, err
		}
		rest, err := asn1.Unmarshal(der, &chain)
		if err != nil {
			return nil, err
		}
		if len(rest) != 0 {
			return nil, errors.New("trailing bytes after the stored issuance chain")
		}
	}

	if isPrecert {
		return tls.Marshal(ct.PrecertChainEntry{PreCertificate: pre, CertificateChain: chain})
	}
	return tls.Marshal(ct.CertificateChain{Entries: chain})
}

func main() {
	t := vhelp.New()
	ctx := context.Background()

	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	intDER := vhelp.ReadFixture("minted", "bench-intermediate.der")
	rootDER := vhelp.ReadFixture("minted", "bench-root.der")
	preDER := vhelp.ReadFixture("minted", "precert-poisoned.der")

	cert := ct.ASN1Cert{Data: leafDER}
	preCert := ct.ASN1Cert{Data: preDER}
	issuers := []ct.ASN1Cert{{Data: intDER}}
	longer := []ct.ASN1Cert{{Data: intDER}, {Data: rootDER}}

	var s storage.IssuanceChainStorage = &memStore{m: map[string][]byte{}}
	key := store(t, ctx, s, issuers)
	longKey := store(t, ctx, s, longer)

	// 1. What the store holds is the DER of the issuers and nothing else: it
	//    survives the forked ASN.1 codec in both directions, in order.
	var back []ct.ASN1Cert
	if der, err := s.FindByKey(ctx, key); err != nil {
		t.Check("stored-chain-is-the-der-of-the-issuers", false, "FindByKey: %v", err)
	} else if rest, err := asn1.Unmarshal(der, &back); err != nil || len(rest) != 0 {
		t.Check("stored-chain-is-the-der-of-the-issuers", false,
			"asn1.Unmarshal of the stored chain = (%d certs, rest %d, %v)", len(back), len(rest), err)
	} else {
		t.Check("stored-chain-is-the-der-of-the-issuers",
			len(back) == 1 && bytes.Equal(back[0].Data, intDER),
			"stored chain decoded to %d certificates, want the one intermediate", len(back))
	}

	// 2. The hash-bearing extra data carries the storage key, exactly, and
	//    nothing else: 2 length bytes plus the 32-byte SHA-256.
	hashedExtra, err := util.ExtraDataForChainHash(cert, key, false)
	if err != nil {
		t.Fatalf("ExtraDataForChainHash(cert): %v", err)
	}
	var decoded ct.CertificateChainHash
	rest, err := tls.Unmarshal(hashedExtra, &decoded)
	t.Check("cert-hash-layout-decodes-to-the-storage-key",
		err == nil && len(rest) == 0 && bytes.Equal(decoded.IssuanceChainHash, key) && len(hashedExtra) == 34,
		"hashed extra data %d bytes decoded to (%x, rest %d, %v), want the 32-byte key %x",
		len(hashedExtra), decoded.IssuanceChainHash, len(rest), err, key)

	// 3. The certificate round trip: rehydrating the hash layout reproduces
	//    the RFC 6962 layout byte for byte.
	classicExtra, err := util.ExtraDataForChain(cert, issuers, false)
	if err != nil {
		t.Fatalf("ExtraDataForChain(cert): %v", err)
	}
	got, err := rehydrate(ctx, s, hashedExtra, false)
	if err != nil {
		t.Check("cert-rehydrates-to-byte-identical-extra-data", false, "rehydrate: %v", err)
	} else {
		t.EqualBytes("cert-rehydrates-to-byte-identical-extra-data", got, classicExtra)
	}

	// 4. The precertificate round trip, which additionally has to carry the
	//    precertificate through from the hash layout.
	preHashed, err := util.ExtraDataForChainHash(preCert, key, true)
	if err != nil {
		t.Fatalf("ExtraDataForChainHash(precert): %v", err)
	}
	preClassic, err := util.ExtraDataForChain(preCert, issuers, true)
	if err != nil {
		t.Fatalf("ExtraDataForChain(precert): %v", err)
	}
	gotPre, err := rehydrate(ctx, s, preHashed, true)
	if err != nil {
		t.Check("precert-rehydrates-to-byte-identical-extra-data", false, "rehydrate(precert): %v", err)
	} else {
		t.EqualBytes("precert-rehydrates-to-byte-identical-extra-data", gotPre, preClassic)
	}

	// 5. An empty hash is well formed and means "no chain to look up": it
	//    rehydrates to the extra data of a submission with no issuers, and
	//    never reaches the store.
	emptyHashed, err := util.ExtraDataForChainHash(cert, []byte{}, false)
	if err != nil {
		t.Fatalf("ExtraDataForChainHash(empty hash): %v", err)
	}
	emptyClassic, err := util.ExtraDataForChain(cert, nil, false)
	if err != nil {
		t.Fatalf("ExtraDataForChain(nil chain): %v", err)
	}
	gotEmpty, err := rehydrate(ctx, s, emptyHashed, false)
	if err != nil {
		t.Check("empty-hash-rehydrates-to-an-empty-chain", false, "rehydrate(empty hash): %v", err)
	} else {
		t.Check("empty-hash-rehydrates-to-an-empty-chain",
			bytes.Equal(gotEmpty, emptyClassic) && bytes.Equal(gotEmpty, []byte{0x00, 0x00, 0x00}),
			"empty hash rehydrated to %x, want %x", gotEmpty, emptyClassic)
	}

	// 6. Two different chains are two different keys and two different
	//    rehydrations: the hash really does stand for the chain it was taken
	//    of, not for the entry it was found in.
	longHashed, err := util.ExtraDataForChainHash(cert, longKey, false)
	if err != nil {
		t.Fatalf("ExtraDataForChainHash(longer chain): %v", err)
	}
	longClassic, err := util.ExtraDataForChain(cert, longer, false)
	if err != nil {
		t.Fatalf("ExtraDataForChain(longer chain): %v", err)
	}
	gotLong, err := rehydrate(ctx, s, longHashed, false)
	if err != nil {
		t.Check("a-second-chain-rehydrates-to-its-own-extra-data", false, "rehydrate(longer chain): %v", err)
	} else {
		t.Check("a-second-chain-rehydrates-to-its-own-extra-data",
			!bytes.Equal(key, longKey) && bytes.Equal(gotLong, longClassic) && !bytes.Equal(gotLong, classicExtra),
			"two-certificate chain rehydrated to %d bytes, want the %d bytes of its own chain layout (keys %x / %x)",
			len(gotLong), len(longClassic), key, longKey)
	}

	// 7. The whole queued leaf, not just the extra data: a leaf built in the
	//    hash layout and then rehydrated is the leaf the classic builder would
	//    have queued, field for field.
	mtl := ct.MerkleTreeLeaf{
		Version:  ct.V1,
		LeafType: ct.TimestampedEntryLeafType,
		TimestampedEntry: &ct.TimestampedEntry{
			Timestamp: pinnedTimestampMS,
			EntryType: ct.X509LogEntryType,
			X509Entry: &ct.ASN1Cert{Data: leafDER},
		},
	}
	classicLeaf, err := util.BuildLogLeaf(logPrefix, mtl, pinnedLeafIndex, cert, issuers, false)
	if err != nil {
		t.Fatalf("BuildLogLeaf: %v", err)
	}
	hashedLeaf, err := util.BuildLogLeafWithChainHash(logPrefix, mtl, pinnedLeafIndex, cert, key, false)
	if err != nil {
		t.Fatalf("BuildLogLeafWithChainHash: %v", err)
	}
	fixed := trillian.LogLeaf{
		LeafValue:        hashedLeaf.LeafValue,
		ExtraData:        hashedLeaf.ExtraData,
		LeafIndex:        hashedLeaf.LeafIndex,
		LeafIdentityHash: hashedLeaf.LeafIdentityHash,
	}
	if fixed.ExtraData, err = rehydrate(ctx, s, fixed.ExtraData, false); err != nil {
		t.Check("rehydrated-leaf-equals-the-classic-leaf", false, "rehydrate(queued leaf): %v", err)
	} else {
		t.Check("rehydrated-leaf-equals-the-classic-leaf",
			bytes.Equal(fixed.ExtraData, classicLeaf.ExtraData) &&
				bytes.Equal(fixed.LeafValue, classicLeaf.LeafValue) &&
				bytes.Equal(fixed.LeafIdentityHash, classicLeaf.LeafIdentityHash) &&
				fixed.LeafIndex == classicLeaf.LeafIndex,
			"rehydrated leaf: extra %d bytes (want %d), value %d/%d, identity %x/%x, index %d/%d",
			len(fixed.ExtraData), len(classicLeaf.ExtraData), len(fixed.LeafValue), len(classicLeaf.LeafValue),
			fixed.LeafIdentityHash, classicLeaf.LeafIdentityHash, fixed.LeafIndex, classicLeaf.LeafIndex)
	}

	t.Done()
}

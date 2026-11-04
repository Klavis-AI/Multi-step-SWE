// Group ct-key-and-time-helpers.
//
// v1.0.13 lifts four small pieces of shared vocabulary into the exported API
// of the root package and of tls/, so that ctutil and the new command-line
// tools can talk about log keys, log timestamps and hash domain separators
// without each re-deriving them:
//
//	ct.PublicKeyFromB64          a log key as it appears in a log list
//	ct.TimestampToTime           CT's milliseconds-since-epoch to a time.Time
//	ct.TreeLeafPrefix/NodePrefix the RFC 6962 s2.1 domain separators
//	tls.SignatureAlgorithmFromPubKey  key type to TLS SignatureAlgorithm
//
// Boundary: each exported function called directly, against pinned inputs.
package main

import (
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"time"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

const (
	// The pinned SCT timestamp, and the instant it denotes.
	pinnedTimestamp = uint64(1365181456275)
	wantTimeRFC3339 = "2013-04-05T17:04:16.275Z"
	// SHA-256 of the log's DER SubjectPublicKeyInfo == the LogID in every
	// pinned SCT.
	wantLogKeyHash = "df1c2ec11500945247a96168325ddc5c7959e8f7c6d388fc002e0bbd3f74d764"
)

func main() {
	t := vhelp.New()
	keyKAT := katFile("keys", "kat.txt")
	certKAT := katFile("rfc6962", "kat.txt")
	b64Key := kat(t, keyKAT, "LogPublicKeyB64")

	wantDER, err := base64.StdEncoding.DecodeString(b64Key)
	if err != nil {
		t.Fatalf("pinned LogPublicKeyB64 is not base64: %v", err)
	}

	// --- ct.PublicKeyFromB64 -------------------------------------------------
	k, err := ct.PublicKeyFromB64(b64Key)
	if err != nil {
		t.Fatalf("PublicKeyFromB64 of the pinned log key: %v", err)
	}
	gotDER, err := x509.MarshalPKIXPublicKey(k)
	if err != nil {
		t.Fatalf("re-marshalling the parsed log key: %v", err)
	}
	t.EqualBytes("public-key-from-b64-round-trips-the-log-key", gotDER, wantDER)

	_, err = ct.PublicKeyFromB64("this is not base64 !!")
	t.Check("public-key-from-b64-rejects-non-base64", err != nil,
		"accepted a string that is not base64")

	_, err = ct.PublicKeyFromB64(base64.StdEncoding.EncodeToString([]byte("hello, world")))
	t.Check("public-key-from-b64-rejects-base64-that-is-not-a-key", err != nil,
		"accepted base64 whose contents are not a SubjectPublicKeyInfo")

	// --- ct.PublicKeyFromPEM agrees, and reports the key hash ----------------
	pemKey, keyHash, rest, err := ct.PublicKeyFromPEM(vhelp.ReadFixture("keys", "log-public-key.pem"))
	if err != nil {
		t.Fatalf("PublicKeyFromPEM of the pinned log key: %v", err)
	}
	pemDER, err := x509.MarshalPKIXPublicKey(pemKey)
	if err != nil {
		t.Fatalf("re-marshalling the PEM log key: %v", err)
	}
	t.Check("public-key-from-pem-parses-the-same-key-as-from-b64",
		string(pemDER) == string(wantDER) && len(rest) == 0,
		"PEM key DER differs from the base64 key DER, or %d trailing bytes remain", len(rest))

	// The reported hash is SHA-256 over the DER SPKI, which is exactly the
	// LogID carried inside the pinned SCTs.
	sct := parseSCT(t, kat(t, certKAT, "TestPreCertProof"))
	sctX509 := parseSCT(t, kat(t, certKAT, "TestCertProof"))
	t.Check("public-key-from-pem-key-hash-is-the-sct-log-id",
		hex.EncodeToString(keyHash[:]) == wantLogKeyHash &&
			hex.EncodeToString(sct.LogID.KeyID[:]) == wantLogKeyHash,
		"key hash %x, SCT LogID %x, want %s", keyHash[:], sct.LogID.KeyID[:], wantLogKeyHash)

	// --- ct.TimestampToTime --------------------------------------------------
	got := ct.TimestampToTime(pinnedTimestamp).UTC()
	t.Check("timestamp-to-time-splits-seconds-and-milliseconds",
		got.Format("2006-01-02T15:04:05.000Z") == wantTimeRFC3339 &&
			got.UnixNano() == int64(pinnedTimestamp)*1000000,
		"got %s (%d ns), want %s", got.Format(time.RFC3339Nano), got.UnixNano(), wantTimeRFC3339)

	zero := ct.TimestampToTime(0).UTC()
	sub := ct.TimestampToTime(999).UTC()
	t.Check("timestamp-to-time-at-the-epoch",
		zero.UnixNano() == 0 && sub.UnixNano() == 999000000 &&
			zero.Format("2006-01-02T15:04:05.000Z") == "1970-01-01T00:00:00.000Z",
		"zero=%s sub-second=%d ns", zero.Format(time.RFC3339Nano), sub.UnixNano())

	// --- the hash domain separators -----------------------------------------
	// Pinned by value, and by the leaf hash they produce for a known entry.
	leaf, err := ct.MerkleTreeLeafFromChain(
		pemCerts(t, vhelp.ReadFixture("rfc6962", "test-cert.pem")), ct.X509LogEntryType, sctX509.Timestamp)
	if err != nil {
		t.Fatalf("building the pinned x509 leaf: %v", err)
	}
	leafData, err := tls.Marshal(*leaf)
	if err != nil {
		t.Fatalf("tls.Marshal of the pinned x509 leaf: %v", err)
	}
	lh := sha256.Sum256(append([]byte{ct.TreeLeafPrefix}, leafData...))
	t.Check("tree-leaf-prefix-is-zero-and-produces-the-published-leaf-hash",
		ct.TreeLeafPrefix == 0x00 &&
			base64.StdEncoding.EncodeToString(lh[:]) == kat(t, certKAT, "TestCertB64LeafHash"),
		"prefix=%#x leaf hash=%s", ct.TreeLeafPrefix, base64.StdEncoding.EncodeToString(lh[:]))

	// SHA-256 over 0x01 || SHA256("L") || SHA256("R"), computed here.
	l := sha256.Sum256([]byte("L"))
	r := sha256.Sum256([]byte("R"))
	nodeIn := append([]byte{ct.TreeNodePrefix}, l[:]...)
	nodeIn = append(nodeIn, r[:]...)
	nh := sha256.Sum256(nodeIn)
	t.Check("tree-node-prefix-is-one-and-differs-from-the-leaf-prefix",
		ct.TreeNodePrefix == 0x01 && ct.TreeNodePrefix != ct.TreeLeafPrefix && len(nh) == 32,
		"node prefix=%#x leaf prefix=%#x", ct.TreeNodePrefix, ct.TreeLeafPrefix)

	// --- tls.SignatureAlgorithmFromPubKey ------------------------------------
	for _, c := range []struct {
		id      string
		fixture string
		want    tls.SignatureAlgorithm
		wantStr string
	}{
		{"signature-algorithm-of-an-ecdsa-key", "ecdsa-public-key.pem", tls.ECDSA, "ECDSA"},
		{"signature-algorithm-of-an-rsa-key", "rsa-public-key.pem", tls.RSA, "RSA"},
		{"signature-algorithm-of-a-dsa-key", "dsa-public-key.pem", tls.DSA, "DSA"},
	} {
		pk, _, _, err := ct.PublicKeyFromPEM(vhelp.ReadFixture("keys", c.fixture))
		if err != nil {
			t.Check(c.id, false, "parsing %s: %v", c.fixture, err)
			continue
		}
		alg := tls.SignatureAlgorithmFromPubKey(pk)
		t.Check(c.id, alg == c.want && alg.String() == c.wantStr,
			"got %v (%s), want %v (%s)", uint8(alg), alg.String(), uint8(c.want), c.wantStr)
	}

	// Anything that is not a supported key is Anonymous, not an error and not
	// a panic; and the log key's algorithm is the one its SCTs are signed with.
	anon := tls.SignatureAlgorithmFromPubKey("not a key at all")
	t.Check("signature-algorithm-of-the-log-key-matches-its-own-sct",
		tls.SignatureAlgorithmFromPubKey(k) == sct.Signature.Algorithm.Signature &&
			sct.Signature.Algorithm.Signature == tls.ECDSA &&
			sct.Signature.Algorithm.Hash == tls.SHA256 &&
			anon == tls.Anonymous && anon.String() == "Anonymous",
		"key alg=%v sct alg=%v/%v non-key alg=%v",
		tls.SignatureAlgorithmFromPubKey(k), sct.Signature.Algorithm.Hash,
		sct.Signature.Algorithm.Signature, anon)

	t.Done()
}

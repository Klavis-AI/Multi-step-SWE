// Group ctutil-loginfo-and-dns-clients.
//
// v1.0.13 adds ctutil.LogInfo: everything needed to check one log's SCTs,
// built straight from a v1 log-list entry. NewLogInfo builds the HTTPS
// variant, NewLogInfoOverDNS builds one backed by the new dnsclient package,
// and LogInfoByKeyHash / LogInfoByKeyHashOverDNS index a whole log list by
// SHA-256 of each log's DER key -- which is exactly the LogID an SCT carries.
// LogInfo then owns the log's last-known STH and verifies SCT signatures,
// re-stamping the leaf with the SCT's own timestamp first.
//
// Boundary: the exported ctutil and dnsclient constructors and methods. No
// socket is opened: constructing a client does not contact the log, and every
// assertion here is about the objects built and the pinned SCT verified.
package main

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/pem"
	"net/http"
	"os"
	"time"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/ctutil"
	"github.com/google/certificate-transparency-go/dnsclient"
	"github.com/google/certificate-transparency-go/jsonclient"
	"github.com/google/certificate-transparency-go/loglist"

	"ctverify/vhelp"
)

const testLogKeyHashHex = "df1c2ec11500945247a96168325ddc5c7959e8f7c6d388fc002e0bbd3f74d764"

func main() {
	t := vhelp.New()
	m := katFile("rfc6962", "kat.txt")

	data, err := os.ReadFile(vhelp.GroupFile("loglist.json"))
	if err != nil {
		t.Fatalf("reading the pinned log list: %v", err)
	}
	ll, err := loglist.NewFromJSON(data)
	if err != nil || len(ll.Logs) != 4 {
		t.Fatalf("pinned log list did not parse into 4 logs: %v", err)
	}
	hc := &http.Client{}

	// --- NewLogInfo ----------------------------------------------------------
	li, err := ctutil.NewLogInfo(&ll.Logs[0], hc)
	if err != nil {
		t.Fatalf("NewLogInfo for the test log: %v", err)
	}
	t.Check("new-log-info-carries-the-log-list-entry",
		li.Description == "Erw Wen RFC6962 Test Log" &&
			li.MMD == 86400*time.Second &&
			string(li.PublicKey) == string(ll.Logs[0].Key) &&
			li.Verifier != nil && li.Client != nil,
		"description=%q mmd=%v key=%d bytes verifier=%t client=%t",
		li.Description, li.MMD, len(li.PublicKey), li.Verifier != nil, li.Client != nil)

	li2, err := ctutil.NewLogInfo(&ll.Logs[1], hc)
	t.Check("new-log-info-accepts-a-url-that-already-carries-its-scheme",
		err == nil && li2 != nil && li2.MMD == 172800*time.Second,
		"err=%v", err)

	_, err = ctutil.NewLogInfo(&loglist.Log{Description: "junk key", Key: []byte("not a key"), URL: "ct.example.com/junk"}, hc)
	t.Check("new-log-info-rejects-a-log-whose-key-is-not-a-public-key", err != nil,
		"a log with an unparsable key was accepted")

	dsaDER := derKey(t, "keys", "dsa-public-key.pem")
	_, err = ctutil.NewLogInfo(&loglist.Log{Description: "dsa log", Key: dsaDER, URL: "ct.example.com/dsa"}, hc)
	t.Check("new-log-info-rejects-a-key-no-ct-verifier-will-take", err != nil,
		"a DSA-keyed log was accepted")

	// --- LogInfoByKeyHash ----------------------------------------------------
	byHash, err := ctutil.LogInfoByKeyHash(ll, hc)
	hashBytes, _ := hex.DecodeString(testLogKeyHashHex)
	var keyHash [sha256.Size]byte
	copy(keyHash[:], hashBytes)
	t.Check("log-info-by-key-hash-indexes-every-log-by-its-key-hash",
		err == nil && len(byHash) == 4 && byHash[keyHash] != nil &&
			byHash[keyHash].Description == "Erw Wen RFC6962 Test Log",
		"err=%v, %d entries", err, len(byHash))

	broken := &loglist.LogList{Logs: []loglist.Log{ll.Logs[0], {Description: "bad", Key: []byte("nope"), URL: "u"}}}
	badMap, err := ctutil.LogInfoByKeyHash(broken, hc)
	t.Check("log-info-by-key-hash-fails-if-any-log-is-unusable",
		err != nil && badMap == nil, "err=%v, map=%v", err, badMap != nil)

	// --- the DNS variants ----------------------------------------------------
	dnsLI, err := ctutil.NewLogInfoOverDNS(&ll.Logs[0])
	_, isDNS := interface{}(dnsLI).(*ctutil.LogInfo)
	var clientIsDNS bool
	if err == nil && dnsLI != nil {
		_, clientIsDNS = dnsLI.Client.(*dnsclient.DNSClient)
	}
	t.Check("new-log-info-over-dns-uses-a-dns-client-for-a-log-with-an-endpoint",
		err == nil && isDNS && clientIsDNS && dnsLI.Verifier != nil,
		"err=%v, client is a DNSClient: %t", err, clientIsDNS)

	_, err = ctutil.NewLogInfoOverDNS(&ll.Logs[1])
	t.Check("new-log-info-over-dns-refuses-a-log-with-no-dns-endpoint", err != nil,
		"a log with no dns_api_endpoint was accepted")

	// NewLogInfoOverDNSWrapper is NewLogInfoOverDNS with NewLogInfo's shape,
	// so that one loop can build either map: it takes an *http.Client and
	// ignores it, including when that client is nil (CLIENT-API.md SS2.2).
	wrapped, wrapErr := ctutil.NewLogInfoOverDNSWrapper(&ll.Logs[0], nil)
	var wrappedIsDNS bool
	if wrapErr == nil && wrapped != nil {
		_, wrappedIsDNS = wrapped.Client.(*dnsclient.DNSClient)
	}
	_, wrapNoEndpointErr := ctutil.NewLogInfoOverDNSWrapper(&ll.Logs[1], hc)
	t.Check("the-over-dns-wrapper-ignores-its-http-client-and-is-otherwise-the-one-argument-form",
		wrapErr == nil && wrappedIsDNS &&
			wrapped.Description == "Erw Wen RFC6962 Test Log" &&
			wrapped.Verifier != nil && wrapNoEndpointErr != nil,
		"with a nil client: err=%v, DNS client=%t; a log with no dns_api_endpoint: err=%v",
		wrapErr, wrappedIsDNS, wrapNoEndpointErr)

	_, wholeListErr := ctutil.LogInfoByKeyHashOverDNS(ll, hc)
	oneLog := &loglist.LogList{Logs: []loglist.Log{ll.Logs[0]}}
	oneMap, oneErr := ctutil.LogInfoByKeyHashOverDNS(oneLog, hc)
	t.Check("log-info-by-key-hash-over-dns-needs-every-log-to-have-an-endpoint",
		wholeListErr != nil && oneErr == nil && len(oneMap) == 1 && oneMap[keyHash] != nil,
		"whole list err=%v; single-log err=%v with %d entries", wholeListErr, oneErr, len(oneMap))

	// --- dnsclient directly --------------------------------------------------
	withKey, err1 := dnsclient.New("testlog.ct.example.com", jsonclient.Options{PublicKeyDER: ll.Logs[0].Key})
	withoutKey, err2 := dnsclient.New("testlog.ct.example.com", jsonclient.Options{})
	t.Check("dns-client-verifies-signatures-only-when-given-a-key",
		err1 == nil && withKey != nil && withKey.Verifier != nil &&
			err2 == nil && withoutKey != nil && withoutKey.Verifier == nil,
		"with key: err=%v verifier=%t; without key: err=%v verifier=%t",
		err1, withKey != nil && withKey.Verifier != nil, err2, withoutKey != nil && withoutKey.Verifier != nil)

	_, badKeyErr := dnsclient.New("testlog.ct.example.com", jsonclient.Options{PublicKeyDER: []byte("not a key")})
	_, dsaErr := dnsclient.New("testlog.ct.example.com", jsonclient.Options{PublicKeyDER: dsaDER})
	t.Check("dns-client-rejects-a-key-it-cannot-verify-with",
		badKeyErr != nil && dsaErr != nil,
		"unparsable key -> %v; DSA key -> %v", badKeyErr, dsaErr)

	// --- the remembered STH --------------------------------------------------
	before := li.LastSTH()
	sth := &ct.SignedTreeHead{Version: ct.V1, TreeSize: 42, Timestamp: 1400000000000}
	li.SetSTH(sth)
	after := li.LastSTH()
	t.Check("set-sth-and-last-sth-remember-the-logs-last-known-head",
		before == nil && after != nil && after.TreeSize == 42 && after.Timestamp == 1400000000000,
		"before=%v after=%+v", before, after)

	// --- VerifySCTSignature --------------------------------------------------
	sct := parseSCT(t, kat(t, m, "TestPreCertProof"))
	chain := pemCerts(t, concat(
		vhelp.ReadFixture("rfc6962", "test-embedded-cert.pem"),
		vhelp.ReadFixture("rfc6962", "ca-cert.pem")))
	leaf, err := ct.MerkleTreeLeafForEmbeddedSCT(chain, sct.Timestamp)
	if err != nil {
		t.Fatalf("rebuilding the embedded-SCT leaf: %v", err)
	}

	good := li.VerifySCTSignature(*sct, *leaf)
	wrongLog := li2.VerifySCTSignature(*sct, *leaf)
	t.Check("verify-sct-signature-accepts-the-logs-own-sct-and-no-others",
		good == nil && wrongLog != nil,
		"own log -> %v; another log -> %v", good, wrongLog)

	// The leaf's timestamp is overwritten with the SCT's before verification,
	// so a leaf built with the wrong timestamp still verifies -- and comes
	// back re-stamped.
	skewed, err := ct.MerkleTreeLeafForEmbeddedSCT(chain, sct.Timestamp+1)
	if err != nil {
		t.Fatalf("rebuilding the skewed leaf: %v", err)
	}
	skewErr := li.VerifySCTSignature(*sct, *skewed)
	t.Check("verify-sct-signature-restamps-the-leaf-with-the-sct-timestamp",
		skewErr == nil && skewed.TimestampedEntry.Timestamp == sct.Timestamp,
		"err=%v, leaf timestamp is now %d, SCT says %d",
		skewErr, skewed.TimestampedEntry.Timestamp, sct.Timestamp)

	t.Done()
}

func derKey(t *vhelp.T, dir, name string) []byte {
	_, _, _, err := ct.PublicKeyFromPEM(vhelp.ReadFixture(dir, name))
	if err != nil {
		t.Fatalf("parsing %s/%s: %v", dir, name, err)
	}
	return pemBlockBytes(t, vhelp.ReadFixture(dir, name))
}

func pemBlockBytes(t *vhelp.T, b []byte) []byte {
	block, _ := pem.Decode(b)
	if block == nil {
		t.Fatalf("no PEM block in key fixture")
	}
	return block.Bytes
}

func concat(parts ...[]byte) []byte {
	var out []byte
	for _, p := range parts {
		out = append(out, p...)
	}
	return out
}

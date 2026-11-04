// Group loglist3-schema.
//
// v1.1.3 introduces the loglist3 package: a hand-mapping of Chrome's v3 log
// list schema, with a per-list version and publication timestamp, per-operator
// contact details, per-log previous-operator history, a six-way state union
// with a final tree head for read-only logs, and a detached-signature
// constructor. This group parses a pinned v3 document and asserts every
// decoded field exactly, then round-trips a real ECDSA detached signature over
// it using the benchmark's own log key.
//
// The package does not exist at v1.1.2 under any name -- loglist and loglist2
// are the only log-list packages there, and neither carries a version string,
// a list timestamp, previous operators, a rejected state or a final tree head
// -- so this group cannot be satisfied by the previous milestone.
package main

import (
	"crypto/ecdsa"
	"crypto/rand"
	"crypto/sha256"
	"crypto/x509"
	"encoding/hex"
	"encoding/pem"
	"fmt"
	"os"
	"strings"
	"time"

	"github.com/google/certificate-transparency-go/loglist3"

	"ctverify/vhelp"
)

func at(s string) time.Time {
	t, err := time.Parse(time.RFC3339, s)
	if err != nil {
		panic(err)
	}
	return t
}

func main() {
	t := vhelp.New()

	blob, err := os.ReadFile(vhelp.GroupFile("loglist-v3.json"))
	if err != nil {
		t.Fatalf("read pinned v3 log list: %v", err)
	}
	if len(blob) == 0 {
		t.Fatalf("pinned v3 log list is empty")
	}

	ll, err := loglist3.NewFromJSON(blob)
	if err != nil {
		t.Fatalf("NewFromJSON: %v", err)
	}

	// 1. List-level fields.
	t.Equal("list-version", ll.Version, "bench-3")
	t.Check("list-timestamp", ll.LogListTimestamp.Equal(at("2022-05-13T12:00:00Z")),
		"got %v", ll.LogListTimestamp.UTC())

	// 2. Operator grouping, in document order, with contact addresses.
	var ops []string
	for _, op := range ll.Operators {
		ops = append(ops, op.Name+"["+strings.Join(op.Email, ";")+"]="+fmt.Sprint(len(op.Logs)))
	}
	t.Equal("operators", strings.Join(ops, "|"),
		"Bench Operator[ops@bench.example]=2|"+
			"Google Bench[google-ct-logs@googlegroups.com]=2|"+
			"Third Operator[third@bench.example;noc@bench.example]=3")

	// 3. The Google-operated predicate keys off the well-known address.
	t.Check("google-operated",
		!ll.Operators[0].GoogleOperated() && ll.Operators[1].GoogleOperated() &&
			!ll.Operators[2].GoogleOperated(),
		"got %v %v %v", ll.Operators[0].GoogleOperated(),
		ll.Operators[1].GoogleOperated(), ll.Operators[2].GoogleOperated())

	// 4. Scalar fields of one fully-populated log.
	alpha := ll.FindLogByURL("https://alpha.bench.example")
	if alpha == nil {
		t.Fatalf("log bench-alpha not found by URL")
	}
	t.Equal("alpha-scalars",
		fmt.Sprintf("%s|%s|%s|%d|%s", alpha.Description, alpha.URL, alpha.DNS, alpha.MMD, alpha.Type),
		"bench-alpha|https://alpha.bench.example/|alpha.bench.example|86400|prod")

	// 5. log_id is the SHA-256 of the DER key, and both survive base64 decoding
	//    into raw bytes.
	sum := sha256.Sum256(alpha.Key)
	t.EqualBytes("alpha-log-id-is-key-hash", alpha.LogID, sum[:])
	pubDER := derOfPEM(t, vhelp.ReadFixture("minted", "keys", "log-pubkey.pem"))
	t.EqualBytes("alpha-key-is-log-public-key", alpha.Key, pubDER)

	// 6. The temporal interval decodes as a half-open time range.
	if alpha.TemporalInterval == nil {
		t.Fatalf("bench-alpha has no temporal interval")
	}
	t.Check("alpha-temporal-interval",
		alpha.TemporalInterval.StartInclusive.Equal(at("2050-01-01T00:00:00Z")) &&
			alpha.TemporalInterval.EndExclusive.Equal(at("2070-01-01T00:00:00Z")),
		"got %v..%v", alpha.TemporalInterval.StartInclusive.UTC(),
		alpha.TemporalInterval.EndExclusive.UTC())

	// 7. Every state of the six-way union maps to its enum value and name,
	//    and a log with no state at all is Undefined.
	want := []string{
		"bench-alpha=UsableLogStatus/3",
		"bench-beta=ReadOnlyLogStatus/4",
		"bench-gamma=RetiredLogStatus/5",
		"bench-delta=PendingLogStatus/1",
		"bench-epsilon=QualifiedLogStatus/2",
		"bench-zeta=RejectedLogStatus/6",
		"bench-eta=UndefinedLogStatus/0",
	}
	var got []string
	for _, op := range ll.Operators {
		for _, l := range op.Logs {
			got = append(got, fmt.Sprintf("%s=%s/%d", l.Description, l.State.String(), l.State.LogStatus()))
		}
	}
	t.Equal("log-states", strings.Join(got, "|"), strings.Join(want, "|"))

	// 8. Active() returns the read-only state through its own return slot.
	beta := ll.FindLogByURL("https://beta.bench.example/")
	if beta == nil {
		t.Fatalf("log bench-beta not found by URL")
	}
	plain, ro := beta.State.Active()
	t.Check("readonly-active-slot", plain == nil && ro != nil, "plain=%v ro=%v", plain, ro)

	// 9. The read-only final tree head is decoded exactly.
	if ro == nil {
		t.Fatalf("bench-beta has no read-only state")
	}
	t.Equal("readonly-final-tree-head",
		fmt.Sprintf("%d/%s/%s", ro.FinalTreeHead.TreeSize,
			hex.EncodeToString(ro.FinalTreeHead.SHA256RootHash), ro.Timestamp.UTC().Format(time.RFC3339)),
		"99/000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f/2021-09-01T00:00:00Z")

	// 10. Previous operators carry a name and an end time.
	gamma := ll.FindLogByURL("https://gamma.bench.example")
	if gamma == nil {
		t.Fatalf("log bench-gamma not found by URL")
	}
	var prev []string
	for _, p := range gamma.PreviousOperators {
		prev = append(prev, p.Name+"@"+p.EndTime.UTC().Format(time.RFC3339))
	}
	t.Equal("previous-operators", strings.Join(prev, "|"), "Former Operator@2019-06-01T00:00:00Z")

	// 11. Malformed JSON is an error and no list, not an empty list. The
	//     wording of the error is not graded: no document fixes it, and
	//     TEST_INTERFACE.md section 3.3 rules the text of an error out.
	badList, err := loglist3.NewFromJSON([]byte(`{"operators":`))
	t.Check("malformed-json-rejected", err != nil && badList == nil,
		"list=%v err=%v", badList, err)

	// 12/13. A detached ECDSA signature over the exact bytes is verified, and
	//        a single flipped byte in the body is not.
	priv := ecPrivate(t, vhelp.ReadFixture("minted", "keys", "log-key.pem"))
	digest := sha256.Sum256(blob)
	sig, err := ecdsa.SignASN1(rand.Reader, priv, digest[:])
	if err != nil {
		t.Fatalf("sign log list: %v", err)
	}
	signed, err := loglist3.NewFromSignedJSON(blob, sig, priv.Public())
	t.Check("signed-json-accepted", err == nil && signed != nil && signed.Version == "bench-3",
		"err=%v", err)

	tampered := append([]byte{}, blob...)
	tampered[len(tampered)/2] ^= 0x01
	tamperedList, err := loglist3.NewFromSignedJSON(tampered, sig, priv.Public())
	t.Check("signed-json-rejects-tampering", err != nil && tamperedList == nil,
		"list=%v err=%v", tamperedList, err)

	t.Done()
}

func derOfPEM(t *vhelp.T, raw []byte) []byte {
	blk, _ := pem.Decode(raw)
	if blk == nil {
		t.Fatalf("no PEM block in key fixture")
	}
	return blk.Bytes
}

func ecPrivate(t *vhelp.T, raw []byte) *ecdsa.PrivateKey {
	blk, _ := pem.Decode(raw)
	if blk == nil {
		t.Fatalf("no PEM block in private key fixture")
	}
	if k, err := x509.ParseECPrivateKey(blk.Bytes); err == nil {
		return k
	}
	any, err := x509.ParsePKCS8PrivateKey(blk.Bytes)
	if err != nil {
		t.Fatalf("parse private key fixture: %v", err)
	}
	k, ok := any.(*ecdsa.PrivateKey)
	if !ok {
		t.Fatalf("private key fixture is %T, want *ecdsa.PrivateKey", any)
	}
	return k
}

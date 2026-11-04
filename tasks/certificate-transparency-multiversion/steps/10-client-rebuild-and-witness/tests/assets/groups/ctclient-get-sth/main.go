// Group ctclient-get-sth.
//
// v1.1.3 replaces client/ctclient/ctclient.go with a Cobra command tree under
// client/ctclient/cmd/. The STH subcommand is now spelled `get-sth` (with
// `sth` kept as an alias) and its flags are parsed AFTER the subcommand word.
// This group drives the candidate's own ctclient binary against a pinned CT
// log served over a real loopback socket and compares its whole stdout, byte
// for byte, against a string derived from the pinned STH.
//
// At v1.1.2 the tool takes its single command word as a trailing positional
// argument parsed by the stdlib flag package, there is no `get-sth` command at
// all, and a flag placed after the command word is not a flag but a second
// positional, so every case here fails.
package main

import (
	"encoding/base64"
	"encoding/hex"
	"fmt"
	"strings"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"

	"ctverify/vhelp"
)

// The pinned tree head. Nothing here is derived from the clock.
const (
	pinnedTreeSize  = 4321
	pinnedTimestamp = 1500000000000 // 2017-07-14T02:40:00Z
)

// pinnedRootHash is an arbitrary but fixed 32-byte root hash.
var pinnedRootHash = mustHex("5f1c2a8b0d4e6f30112233445566778899aabbccddeeff00102030405060708a")

// pinnedSigValue is an arbitrary but fixed DigitallySigned.signature body; no
// verifier is configured, so the client must accept it and render it verbatim.
var pinnedSigValue = mustHex("30450220112233445566778899aabbccddeeff00112233445566778899aabbccddeeff00022100")

func mustHex(s string) []byte {
	b, err := hex.DecodeString(s)
	if err != nil {
		panic(err)
	}
	return b
}

func main() {
	t := vhelp.New()
	requireClient(t)

	// The tree-head signature is a TLS DigitallySigned: hash(1) sig(1)
	// uint16-length-prefixed opaque. Marshalled through the product's own
	// presentation-language codec, which step 1 already pins.
	ds := ct.DigitallySigned{
		Algorithm: tls.SignatureAndHashAlgorithm{Hash: tls.SHA256, Signature: tls.ECDSA},
		Signature: pinnedSigValue,
	}
	dsBytes, err := tls.Marshal(ds)
	if err != nil {
		t.Fatalf("tls.Marshal(DigitallySigned): %v", err)
	}

	sthJSON := fmt.Sprintf(
		`{"tree_size":%d,"timestamp":%d,"sha256_root_hash":%q,"tree_head_signature":%q}`,
		pinnedTreeSize, pinnedTimestamp,
		base64.StdEncoding.EncodeToString(pinnedRootHash),
		base64.StdEncoding.EncodeToString(dsBytes))

	s := newStubLog()
	defer s.close()
	s.json("/ct/v1/get-sth", 200, sthJSON)

	// 1. `get-sth`, the new primary spelling, with its flag after the word.
	r := runClient("get-sth", "--log_uri="+s.URL)
	wantLine1 := fmt.Sprintf(
		"2017-07-14 02:40:00 +0000 UTC (timestamp %d): Got STH for V1 log (size=%d) at %s, hash %x",
		pinnedTimestamp, pinnedTreeSize, s.URL, pinnedRootHash)
	wantLine2 := fmt.Sprintf("Signature: Hash=SHA256 Sign=ECDSA Value=%x", pinnedSigValue)
	want := wantLine1 + "\n" + wantLine2 + "\n"
	t.Check("get-sth-exit-status", r.code == 0, "exit=%d stderr=%s", r.code, oneLine(r.stderr))
	t.Equal("get-sth-stdout", r.stdout, want)

	// 2. It reached exactly the RFC 6962 get-sth entrypoint, once, by GET.
	t.Equal("get-sth-requests", strings.Join(s.requests(), "|"), "GET /ct/v1/get-sth")

	// 3. `sth` is retained as an alias of the same subcommand and produces
	//    identical output.
	s2 := newStubLog()
	defer s2.close()
	s2.json("/ct/v1/get-sth", 200, sthJSON)
	r2 := runClient("sth", "--log_uri="+s2.URL)
	want2 := strings.Replace(wantLine1, s.URL, s2.URL, 1) + "\n" + wantLine2 + "\n"
	t.Check("sth-alias-exit-status", r2.code == 0, "exit=%d stderr=%s", r2.code, oneLine(r2.stderr))
	t.Equal("sth-alias-stdout", r2.stdout, want2)

	// 4. The base URI is taken from the flag, not from the compiled-in
	//    default: a log whose path has a per-log prefix is honoured.
	s3 := newStubLog()
	defer s3.close()
	s3.json("/bench/ct/v1/get-sth", 200, sthJSON)
	r3 := runClient("get-sth", "--log_uri="+s3.URL+"/bench")
	t.Check("prefixed-log-uri-exit-status", r3.code == 0, "exit=%d stderr=%s", r3.code, oneLine(r3.stderr))
	t.Equal("prefixed-log-uri-requests", strings.Join(s3.requests(), "|"), "GET /bench/ct/v1/get-sth")

	// 5. A log that answers get-sth with an HTTP error is a non-zero exit and
	//    no stdout, not a silent success.
	s4 := newStubLog()
	defer s4.close()
	s4.json("/ct/v1/get-sth", 451, `{"error":"unavailable for legal reasons"}`)
	r4 := runClient("get-sth", "--log_uri="+s4.URL)
	t.Check("server-error-is-fatal", r4.code != 0 && r4.stdout == "",
		"exit=%d stdout=%q", r4.code, r4.stdout)

	t.Done()
}

func oneLine(s string) string {
	return strings.ReplaceAll(strings.TrimSpace(s), "\n", " / ")
}

// Group ctclient-upload.
//
// v1.1.3's `upload` subcommand (alias `add-chain`) reads a PEM chain named by
// --cert_chain given after the command word, decides between add-chain and
// add-pre-chain by looking for the CT poison extension in the leaf, and prints
// a fixed four-line SCT report plus the leaf hash it derives. --log_mmd, new
// in this grammar, governs whether it goes on to fetch an inclusion proof.
// This group posts against a loopback socket and checks both the exact request
// body the candidate sends and the exact stdout it prints.
//
// At v1.1.2 the word is a trailing positional and -cert_chain must precede it,
// there is no --log_mmd flag and no `add-chain` alias, so nothing here passes.
package main

import (
	"encoding/base64"
	"encoding/hex"
	"encoding/pem"
	"fmt"
	"strings"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"

	"ctverify/vhelp"
)

const (
	pinnedTimestamp = uint64(1500000000000) // 2017-07-14 02:40:00 +0000 UTC
	pinnedWhen      = "2017-07-14 02:40:00 +0000 UTC"
	// mmdNever is long enough that the SCT is never treated as merged, so the
	// upload path never depends on the wall clock.
	mmdNever = "--log_mmd=1000000h"
)

var pinnedSigValue = mustHex("3046022100a1b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8f90")

func mustHex(s string) []byte {
	b, err := hex.DecodeString(s)
	if err != nil {
		panic(err)
	}
	return b
}

func b64(b []byte) string { return base64.StdEncoding.EncodeToString(b) }

// pemChain returns the DER bodies of every CERTIFICATE block in a fixture.
func pemChain(raw []byte) [][]byte {
	var out [][]byte
	rest := raw
	for {
		var blk *pem.Block
		blk, rest = pem.Decode(rest)
		if blk == nil {
			break
		}
		if blk.Type == "CERTIFICATE" {
			out = append(out, blk.Bytes)
		}
	}
	return out
}

func main() {
	t := vhelp.New()
	requireClient(t)

	logID := mustHex(strings.TrimSpace(strings.Split(
		string(vhelp.ReadFixture("minted", "keys", "log-id.txt")), "\n")[0]))
	if len(logID) != 32 {
		t.Fatalf("log-id fixture is %d bytes, want 32", len(logID))
	}

	ds := ct.DigitallySigned{
		Algorithm: tls.SignatureAndHashAlgorithm{Hash: tls.SHA256, Signature: tls.ECDSA},
		Signature: pinnedSigValue,
	}
	dsBytes, err := tls.Marshal(ds)
	if err != nil {
		t.Fatalf("tls.Marshal(DigitallySigned): %v", err)
	}
	sctJSON := fmt.Sprintf(`{"sct_version":0,"id":%q,"timestamp":%d,"extensions":"","signature":%q}`,
		b64(logID), pinnedTimestamp, b64(dsBytes))

	validChain := pemChain(vhelp.ReadFixture("minted", "chain-valid.pem"))
	precertChain := pemChain(vhelp.ReadFixture("minted", "chain-precert.pem"))
	if len(validChain) != 2 || len(precertChain) != 2 {
		t.Fatalf("chain fixtures: got %d and %d certs, want 2 and 2", len(validChain), len(precertChain))
	}

	wantBody := func(chain [][]byte) string {
		parts := make([]string, len(chain))
		for i, c := range chain {
			parts[i] = `"` + b64(c) + `"`
		}
		return `{"chain":[` + strings.Join(parts, ",") + `]}`
	}

	// The tool derives the leaf hash from the *X.509* leaf shape in both the
	// certificate and the precertificate case.
	report := func(uri string, chain [][]byte) string {
		lh, err := ct.LeafHashForLeaf(ct.CreateX509MerkleTreeLeaf(ct.ASN1Cert{Data: chain[0]}, pinnedTimestamp))
		if err != nil {
			t.Fatalf("LeafHashForLeaf: %v", err)
		}
		return fmt.Sprintf("Uploaded chain of %d certs to V1 log at %s, timestamp: %d (%s)\n",
			len(chain), uri, pinnedTimestamp, pinnedWhen) +
			fmt.Sprintf("LogID: %x\n", logID) +
			fmt.Sprintf("LeafHash: %x\n", lh) +
			// The tool prints its own "Signature: " prefix in front of the
			// string signatureToString already prefixes, so the line is
			// doubled; that doubling is part of the fixed rendering.
			fmt.Sprintf("Signature: Signature: Hash=SHA256 Sign=ECDSA Value=%x\n", pinnedSigValue)
	}

	// 1. A plain certificate chain goes to add-chain.
	s := newStubLog()
	defer s.close()
	s.json("/ct/v1/add-chain", 200, sctJSON)
	r := runClient("upload", "--log_uri="+s.URL,
		"--cert_chain="+vhelp.Fixture("minted", "chain-valid.pem"), mmdNever)
	t.Check("upload-exit-status", r.code == 0, "exit=%d stderr=%s", r.code, oneLine(r.stderr))
	t.Equal("upload-requests", strings.Join(s.requests(), "|"), "POST /ct/v1/add-chain")
	t.Equal("upload-request-body", s.bodyFor("/ct/v1/add-chain"), wantBody(validChain))
	t.Equal("upload-stdout", r.stdout, report(s.URL, validChain))

	// 2. A poisoned leaf is recognised as a precertificate and goes to
	//    add-pre-chain, with an extra first line.
	s2 := newStubLog()
	defer s2.close()
	s2.json("/ct/v1/add-pre-chain", 200, sctJSON)
	r2 := runClient("upload", "--log_uri="+s2.URL,
		"--cert_chain="+vhelp.Fixture("minted", "chain-precert.pem"), mmdNever)
	t.Check("precert-exit-status", r2.code == 0, "exit=%d stderr=%s", r2.code, oneLine(r2.stderr))
	t.Equal("precert-requests", strings.Join(s2.requests(), "|"), "POST /ct/v1/add-pre-chain")
	t.Equal("precert-request-body", s2.bodyFor("/ct/v1/add-pre-chain"), wantBody(precertChain))
	t.Equal("precert-stdout", r2.stdout, "Uploading pre-certificate to log\n"+report(s2.URL, precertChain))

	// 3. The `add-chain` alias is the same command.
	s3 := newStubLog()
	defer s3.close()
	s3.json("/ct/v1/add-chain", 200, sctJSON)
	r3 := runClient("add-chain", "--log_uri="+s3.URL,
		"--cert_chain="+vhelp.Fixture("minted", "chain-valid.pem"), mmdNever)
	t.Equal("alias-add-chain-stdout", r3.stdout, report(s3.URL, validChain))

	// 4. No --cert_chain is a diagnosed fatal error with no request sent.
	s4 := newStubLog()
	defer s4.close()
	s4.json("/ct/v1/add-chain", 200, sctJSON)
	r4 := runClient("upload", "--log_uri="+s4.URL, mmdNever)
	t.Check("missing-cert-chain-is-fatal",
		r4.code != 0 && r4.stdout == "" && len(s4.requests()) == 0 &&
			strings.TrimSpace(r4.stderr) != "",
		"exit=%d requests=%v stderr=%s", r4.code, s4.requests(), oneLine(r4.stderr))

	// 5. A file with no certificates in it is refused.
	s5 := newStubLog()
	defer s5.close()
	s5.json("/ct/v1/add-chain", 200, sctJSON)
	r5 := runClient("upload", "--log_uri="+s5.URL,
		"--cert_chain="+vhelp.Fixture("minted", "keys", "log-pubkey.pem"), mmdNever)
	t.Check("no-certificates-in-file-is-fatal",
		r5.code != 0 && r5.stdout == "" && len(s5.requests()) == 0,
		"exit=%d requests=%v stderr=%s", r5.code, s5.requests(), oneLine(r5.stderr))

	// 6. A log that rejects the submission is a non-zero exit with no report.
	s6 := newStubLog()
	defer s6.close()
	s6.json("/ct/v1/add-chain", 403, `{"error":"chain not accepted"}`)
	r6 := runClient("upload", "--log_uri="+s6.URL,
		"--cert_chain="+vhelp.Fixture("minted", "chain-valid.pem"), mmdNever)
	t.Check("rejected-submission-is-fatal", r6.code != 0 && r6.stdout == "",
		"exit=%d stdout=%q", r6.code, r6.stdout)

	t.Done()
}

func oneLine(s string) string {
	return strings.ReplaceAll(strings.TrimSpace(s), "\n", " / ")
}

// Group ctclient-get-entries.
//
// v1.1.3's `get-entries` subcommand takes --first/--last/--chain/--text after
// the command word, defaults --last to --first, fetches the RFC 6962
// get-entries range and renders each raw entry itself. This group serves a
// pinned two-entry range -- one X.509 entry with a validation chain and one
// precertificate entry -- over a loopback socket and compares the candidate's
// whole stdout, byte for byte, with the rendering the milestone fixes.
//
// Two points about the precertificate entry. First, from v1.0.2 onward the
// renderer is handed the *submitted* certificate out of extra_data, not the
// TBSCertificate out of the leaf, so under --text=false it is a PEM
// CERTIFICATE block like any other -- the `%x`-of-the-TBS rendering that
// CLI.md section 3.1 describes belongs to v1.0 alone, and TEST_INTERFACE.md
// section 6 records the correction for this step. Second, the header line
// still carries the issuer key hash out of the leaf, so both halves are
// exercised.
//
// At v1.1.2 the subcommand is spelled `getentries`, it is a trailing
// positional, and its flags must precede it, so `get-entries --first=0` is an
// unknown command and every case here fails.
package main

import (
	"crypto/sha256"
	"encoding/base64"
	"encoding/pem"
	"fmt"
	"strings"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/certificate-transparency-go/x509"
	"github.com/google/certificate-transparency-go/x509util"

	"ctverify/vhelp"
)

// Pinned entry timestamps. Nothing is read from the clock.
const (
	tsX509    = uint64(1500000000000) // 2017-07-14 02:40:00 +0000 UTC
	tsPrecert = uint64(1600000000000) // 2020-09-13 12:26:40 +0000 UTC
)

const (
	whenX509    = "2017-07-14 02:40:00 +0000 UTC"
	whenPrecert = "2020-09-13 12:26:40 +0000 UTC"
)

func mustMarshal(t *vhelp.T, what string, v interface{}) []byte {
	b, err := tls.Marshal(v)
	if err != nil {
		t.Fatalf("tls.Marshal(%s): %v", what, err)
	}
	return b
}

func b64(b []byte) string { return base64.StdEncoding.EncodeToString(b) }

func pemOf(der []byte) string {
	return string(pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der}))
}

func main() {
	t := vhelp.New()
	requireClient(t)

	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	rootDER := vhelp.ReadFixture("minted", "bench-root.der")
	precertDER := vhelp.ReadFixture("minted", "precert-poisoned.der")

	// Entry 0: an X.509 entry whose extra_data is a one-certificate chain.
	leaf0 := ct.MerkleTreeLeaf{
		Version:  ct.V1,
		LeafType: ct.TimestampedEntryLeafType,
		TimestampedEntry: &ct.TimestampedEntry{
			Timestamp: tsX509,
			EntryType: ct.X509LogEntryType,
			X509Entry: &ct.ASN1Cert{Data: leafDER},
		},
	}
	extra0 := ct.CertificateChain{Entries: []ct.ASN1Cert{{Data: rootDER}}}

	// Entry 1: a precertificate entry. The issuer key hash is pinned as the
	// SHA-256 of the root's whole DER so the value in the expectation is
	// derived, not copied.
	ikh := sha256.Sum256(rootDER)
	precertTBS := precertDER[:64] // opaque to the renderer under --text=false
	leaf1 := ct.MerkleTreeLeaf{
		Version:  ct.V1,
		LeafType: ct.TimestampedEntryLeafType,
		TimestampedEntry: &ct.TimestampedEntry{
			Timestamp: tsPrecert,
			EntryType: ct.PrecertLogEntryType,
			PrecertEntry: &ct.PreCert{
				IssuerKeyHash:  ikh,
				TBSCertificate: precertTBS,
			},
		},
	}
	extra1 := ct.PrecertChainEntry{
		PreCertificate:   ct.ASN1Cert{Data: precertDER},
		CertificateChain: []ct.ASN1Cert{{Data: rootDER}},
	}

	l0 := b64(mustMarshal(t, "leaf0", leaf0))
	e0 := b64(mustMarshal(t, "extra0", extra0))
	l1 := b64(mustMarshal(t, "leaf1", leaf1))
	e1 := b64(mustMarshal(t, "extra1", extra1))

	twoEntries := fmt.Sprintf(
		`{"entries":[{"leaf_input":%q,"extra_data":%q},{"leaf_input":%q,"extra_data":%q}]}`,
		l0, e0, l1, e1)
	oneEntry := fmt.Sprintf(`{"entries":[{"leaf_input":%q,"extra_data":%q}]}`, l0, e0)

	hdr0 := fmt.Sprintf("Index=0 Timestamp=%d (%s) X.509 certificate:\n", tsX509, whenX509)
	hdr1 := fmt.Sprintf("Index=1 Timestamp=%d (%s) pre-certificate from issuer with keyhash %x:\n",
		tsPrecert, whenPrecert, ikh)

	// 1. The two-entry range, rendered as PEM.
	s := newStubLog()
	defer s.close()
	s.json("/ct/v1/get-entries", 200, twoEntries)
	r := runClient("get-entries", "--log_uri="+s.URL, "--first=0", "--last=1", "--text=false")
	t.Check("range-exit-status", r.code == 0, "exit=%d stderr=%s", r.code, oneLine(r.stderr))
	t.Equal("range-requests", strings.Join(s.requests(), "|"),
		"GET /ct/v1/get-entries?end=1&start=0")
	t.Equal("range-pem-stdout", r.stdout,
		hdr0+pemOf(leafDER)+hdr1+pemOf(precertDER))

	// 2. --last defaults to --first: a single-entry request, single entry out.
	s2 := newStubLog()
	defer s2.close()
	s2.json("/ct/v1/get-entries", 200, oneEntry)
	r2 := runClient("get-entries", "--log_uri="+s2.URL, "--first=0", "--text=false")
	t.Equal("last-defaults-to-first-requests", strings.Join(s2.requests(), "|"),
		"GET /ct/v1/get-entries?end=0&start=0")
	t.Equal("last-defaults-to-first-stdout", r2.stdout, hdr0+pemOf(leafDER))

	// 3. --chain appends the validation chain from extra_data.
	s3 := newStubLog()
	defer s3.close()
	s3.json("/ct/v1/get-entries", 200, oneEntry)
	r3 := runClient("get-entries", "--log_uri="+s3.URL, "--first=0", "--text=false", "--chain")
	t.Equal("chain-flag-stdout", r3.stdout, hdr0+pemOf(leafDER)+pemOf(rootDER))

	// 4. The default --text rendering goes through x509util.CertificateToString.
	parsed, err := x509.ParseCertificate(leafDER)
	if err != nil {
		t.Fatalf("parse leaf fixture: %v", err)
	}
	s4 := newStubLog()
	defer s4.close()
	s4.json("/ct/v1/get-entries", 200, oneEntry)
	r4 := runClient("get-entries", "--log_uri="+s4.URL, "--first=0")
	t.Equal("text-stdout", r4.stdout, hdr0+x509util.CertificateToString(parsed)+"\n")

	// 5. The `entries` alias is the same command with the same rendering.
	s5 := newStubLog()
	defer s5.close()
	s5.json("/ct/v1/get-entries", 200, oneEntry)
	r5 := runClient("entries", "--log_uri="+s5.URL, "--first=0", "--text=false")
	t.Equal("alias-entries-stdout", r5.stdout, hdr0+pemOf(leafDER))

	// 6. Omitting --first is fatal before any request is made, prints nothing
	//    on stdout, and says something about it on stderr. The wording of
	//    that something is not graded (TEST_INTERFACE.md §3.3).
	s6 := newStubLog()
	defer s6.close()
	s6.json("/ct/v1/get-entries", 200, oneEntry)
	r6 := runClient("get-entries", "--log_uri="+s6.URL)
	t.Check("missing-first-is-fatal",
		r6.code != 0 && r6.stdout == "" && len(s6.requests()) == 0 &&
			strings.TrimSpace(r6.stderr) != "",
		"exit=%d stdout=%q requests=%v stderr=%s", r6.code, r6.stdout, s6.requests(), oneLine(r6.stderr))

	// 7. A certificate that will not parse is logged and skipped rather than
	//    being fatal: the entry still gets its header line, nothing is
	//    rendered for it, the next entry is rendered normally and the exit
	//    status is success.
	badLeaf := ct.MerkleTreeLeaf{
		Version:  ct.V1,
		LeafType: ct.TimestampedEntryLeafType,
		TimestampedEntry: &ct.TimestampedEntry{
			Timestamp: tsX509,
			EntryType: ct.X509LogEntryType,
			X509Entry: &ct.ASN1Cert{Data: []byte{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}},
		},
	}
	badThenGood := fmt.Sprintf(
		`{"entries":[{"leaf_input":%q,"extra_data":%q},{"leaf_input":%q,"extra_data":%q}]}`,
		b64(mustMarshal(t, "badLeaf", badLeaf)), e0, l0, e0)
	hdrGoodAt1 := fmt.Sprintf("Index=1 Timestamp=%d (%s) X.509 certificate:\n", tsX509, whenX509)
	s8 := newStubLog()
	defer s8.close()
	s8.json("/ct/v1/get-entries", 200, badThenGood)
	r8 := runClient("get-entries", "--log_uri="+s8.URL, "--first=0", "--last=1")
	t.Check("unparseable-cert-logged-and-skipped",
		r8.code == 0 && r8.stdout == hdr0+hdrGoodAt1+x509util.CertificateToString(parsed)+"\n",
		"exit=%d stdout=%q", r8.code, r8.stdout)

	// 8. A log that refuses the range is a non-zero exit with no stdout.
	s7 := newStubLog()
	defer s7.close()
	s7.json("/ct/v1/get-entries", 400, `{"error":"bad range"}`)
	r7 := runClient("get-entries", "--log_uri="+s7.URL, "--first=0", "--last=1")
	t.Check("server-error-is-fatal", r7.code != 0 && r7.stdout == "",
		"exit=%d stdout=%q", r7.code, r7.stdout)

	t.Done()
}

func oneLine(s string) string {
	return strings.ReplaceAll(strings.TrimSpace(s), "\n", " / ")
}

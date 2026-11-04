// Group trillian-util-extra-data-for-chain.
//
// trillian/util/log_leaf.go is another of the eight files v1.0.19 adds. It
// lifts the leaf-building the CTFE had inline into an exported package so the
// new migrillian can share it. ExtraDataForChain is the half that produces
// RFC 6962 section 4.6's "extra data" -- pure, deterministic, byte-exact, and
// therefore graded from a driver that calls it, with no server and no clock.
//
// Two shapes, both pinned:
//
//	certificate     ASN.1Cert certificate_chain<0..2^24-1>  -- the chain AFTER
//	                the leaf; the leaf itself is not in it
//	pre-certificate a TLS-encoded PrecertChainEntry: the pre-certificate,
//	                then that same chain
//
// Each is graded three ways over the same pinned fixtures: the exact length
// and SHA-256 of the encoding, the structure it decodes to (so a coincidental
// digest match cannot come from garbage), and, for the certificate form, the
// 24-bit vector header computed from the DER lengths of the fixtures
// themselves. The empty chain is pinned as literal bytes, 00 00 00.
//
// SCOPE-NET / SCOPE-TIME: nothing listens, nothing dials, no clock is read.
//
// trillian/util exists at v1.0.17 but has no ExtraDataForChain, so this driver
// does not compile against the previous tree.
package main

import (
	"crypto/sha256"
	"encoding/hex"
	"fmt"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"
	tutil "github.com/google/certificate-transparency-go/trillian/util"

	"ctverify/vhelp"
)

// The Oracle's own output for these fixtures, pinned as constants.
const (
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

// entriesHex renders a chain as hex so two chains can be compared exactly in
// a single case with a readable failure message.
func entriesHex(entries []ct.ASN1Cert) string {
	out := ""
	for _, e := range entries {
		if out != "" {
			out += " "
		}
		out += hex.EncodeToString(e.Data)
	}
	return out
}

func main() {
	t := vhelp.New()

	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	intDER := vhelp.ReadFixture("minted", "bench-intermediate.der")
	rootDER := vhelp.ReadFixture("minted", "bench-root.der")

	leaf := ct.ASN1Cert{Data: leafDER}
	chain := []ct.ASN1Cert{{Data: intDER}, {Data: rootDER}}

	// --- 1. the certificate form, byte for byte -----------------------------
	got, err := tutil.ExtraDataForChain(leaf, chain, false)
	if err != nil {
		t.Fatalf("ExtraDataForChain(cert): %v", err)
	}
	t.Equal("certificate-extra-data-is-the-pinned-encoding",
		fmt.Sprintf("len=%d sha256=%s", len(got), digest(got)),
		fmt.Sprintf("len=%d sha256=%s", certExtraLen, certExtraSHA))

	// --- 2. and it really is the chain after the leaf ------------------------
	var decoded ct.CertificateChain
	rest, err := tls.Unmarshal(got, &decoded)
	t.Equal("certificate-extra-data-decodes-to-the-chain-after-the-leaf",
		fmt.Sprintf("err=%v trailing=%d entries=%s", err, len(rest), entriesHex(decoded.Entries)),
		fmt.Sprintf("err=%v trailing=%d entries=%s", error(nil), 0, entriesHex(chain)))

	// --- 3. with RFC 6962's 24-bit vector header -----------------------------
	// Computed from the fixtures' own DER lengths: each entry is itself a
	// 24-bit-prefixed opaque, so the outer length is sum(3 + len(der)).
	wantBody := (3 + len(intDER)) + (3 + len(rootDER))
	header := -1
	if len(got) >= 3 {
		header = int(got[0])<<16 | int(got[1])<<8 | int(got[2])
	}
	t.Equal("certificate-extra-data-carries-the-24-bit-vector-header",
		fmt.Sprintf("header=%d body=%d", header, len(got)-3),
		fmt.Sprintf("header=%d body=%d", wantBody, wantBody))

	// --- 4. an empty chain is an empty vector, not an empty slice ------------
	empty, err := tutil.ExtraDataForChain(leaf, nil, false)
	t.Equal("empty-chain-extra-data-is-an-empty-vector",
		fmt.Sprintf("err=%v hex=%s", err, hex.EncodeToString(empty)),
		fmt.Sprintf("err=%v hex=%s", error(nil), emptyChainHex))

	// --- 5. the pre-certificate form, byte for byte --------------------------
	gotPre, err := tutil.ExtraDataForChain(leaf, chain, true)
	if err != nil {
		t.Fatalf("ExtraDataForChain(precert): %v", err)
	}
	t.Equal("precertificate-extra-data-is-the-pinned-encoding",
		fmt.Sprintf("len=%d sha256=%s", len(gotPre), digest(gotPre)),
		fmt.Sprintf("len=%d sha256=%s", precertExtraLen, precertExtraSHA))

	// --- 6. and decodes to a PrecertChainEntry, leaf included ----------------
	// The difference from the certificate form is exactly that the submitted
	// (pre-)certificate is carried in the extra data as well as the chain.
	var pre ct.PrecertChainEntry
	rest, err = tls.Unmarshal(gotPre, &pre)
	t.Equal("precertificate-extra-data-decodes-to-a-precert-chain-entry",
		fmt.Sprintf("err=%v trailing=%d pre=%s chain=%s",
			err, len(rest), hex.EncodeToString(pre.PreCertificate.Data), entriesHex(pre.CertificateChain)),
		fmt.Sprintf("err=%v trailing=%d pre=%s chain=%s",
			error(nil), 0, hex.EncodeToString(leafDER), entriesHex(chain)))

	t.Done()
}

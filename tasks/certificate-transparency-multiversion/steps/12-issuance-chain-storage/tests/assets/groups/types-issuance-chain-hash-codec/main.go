// Observes the two TLS presentation-language structures this release adds to
// the top-level package: CertificateChainHash and PrecertChainEntryHash.
// They are the on-the-wire shape of a log entry's extra data when the
// issuance chain is held out of line, so their encodings are pinned exactly,
// including the width of every length prefix and the declared bounds.
package main

import (
	"bytes"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"

	"ctverify/vhelp"
)

// A pinned 32-byte SHA-256-shaped issuance chain hash.
var pinnedHash = []byte{
	0x9f, 0x86, 0xd0, 0x81, 0x88, 0x4c, 0x7d, 0x65, 0x9a, 0x2f, 0xea, 0xa0,
	0xc5, 0x5a, 0xd0, 0x15, 0xa3, 0xbf, 0x4f, 0x1b, 0x2b, 0x0b, 0x82, 0x2c,
	0xd1, 0x5d, 0x6c, 0x15, 0xb0, 0xf0, 0x0a, 0x08,
}

func main() {
	t := vhelp.New()

	// uint16 length prefix, because the field is declared maxlen:256.
	wantCert := append([]byte{0x00, 0x20}, pinnedHash...)
	got, err := tls.Marshal(ct.CertificateChainHash{IssuanceChainHash: pinnedHash})
	if err != nil {
		t.Fatalf("marshal CertificateChainHash: %v", err)
	}
	t.EqualBytes("certificate-chain-hash-encoding", got, wantCert)

	got, err = tls.Marshal(ct.CertificateChainHash{IssuanceChainHash: []byte{}})
	if err != nil {
		t.Fatalf("marshal empty CertificateChainHash: %v", err)
	}
	t.EqualBytes("certificate-chain-hash-empty-is-two-zero-bytes", got, []byte{0x00, 0x00})

	var back ct.CertificateChainHash
	rest, err := tls.Unmarshal(wantCert, &back)
	t.Check("certificate-chain-hash-unmarshal",
		err == nil && len(rest) == 0 && bytes.Equal(back.IssuanceChainHash, pinnedHash),
		"unmarshal gave (%x, rest %d, %v)", back.IssuanceChainHash, len(rest), err)

	// A hash longer than the declared maximum must not encode.
	_, err = tls.Marshal(ct.CertificateChainHash{IssuanceChainHash: make([]byte, 257)})
	t.Check("certificate-chain-hash-maxlen-256-enforced", err != nil,
		"a 257-byte issuance chain hash encoded without error")

	// PrecertChainEntryHash: a 24-bit-prefixed precertificate followed by the
	// same 16-bit-prefixed hash.
	der := vhelp.ReadFixture("minted", "precert-poisoned.der")
	n := len(der)
	wantPre := []byte{byte(n >> 16), byte(n >> 8), byte(n)}
	wantPre = append(wantPre, der...)
	wantPre = append(wantPre, 0x00, 0x20)
	wantPre = append(wantPre, pinnedHash...)

	got, err = tls.Marshal(ct.PrecertChainEntryHash{
		PreCertificate:    ct.ASN1Cert{Data: der},
		IssuanceChainHash: pinnedHash,
	})
	if err != nil {
		t.Fatalf("marshal PrecertChainEntryHash: %v", err)
	}
	t.EqualBytes("precert-chain-entry-hash-encoding", got, wantPre)

	var backPre ct.PrecertChainEntryHash
	rest, err = tls.Unmarshal(wantPre, &backPre)
	t.Check("precert-chain-entry-hash-unmarshal",
		err == nil && len(rest) == 0 &&
			bytes.Equal(backPre.PreCertificate.Data, der) &&
			bytes.Equal(backPre.IssuanceChainHash, pinnedHash),
		"unmarshal gave (%d precert bytes, %x, rest %d, %v)", len(backPre.PreCertificate.Data), backPre.IssuanceChainHash, len(rest), err)

	// The precertificate itself is declared minlen:1, so an entry with no
	// precertificate is not representable.
	_, err = tls.Marshal(ct.PrecertChainEntryHash{
		PreCertificate:    ct.ASN1Cert{Data: []byte{}},
		IssuanceChainHash: pinnedHash,
	})
	t.Check("precert-chain-entry-hash-requires-a-precertificate", err != nil,
		"an empty precertificate encoded without error")

	// The two hashed structures arrive *beside* the chain-bearing pair, which
	// this release does not touch. That is what makes an existing log's bytes
	// unchanged, so it is asserted here rather than assumed: CertificateChain
	// keeps a 24-bit outer prefix over 24-bit-prefixed entries, and
	// PrecertChainEntry keeps its 24-bit precertificate prefix followed by the
	// same chain (TLS-CODEC.md section 5.4).
	u24 := func(n int) []byte { return []byte{byte(n >> 16), byte(n >> 8), byte(n)} }
	wantChain := append(u24(n+3), u24(n)...)
	wantChain = append(wantChain, der...)
	gotChain, err := tls.Marshal(ct.CertificateChain{Entries: []ct.ASN1Cert{{Data: der}}})
	if err != nil {
		t.Fatalf("marshal CertificateChain: %v", err)
	}
	t.EqualBytes("certificate-chain-encoding-is-unchanged", gotChain, wantChain)

	wantPreEntry := append(u24(n), der...)
	wantPreEntry = append(wantPreEntry, wantChain...)
	gotPreEntry, err := tls.Marshal(ct.PrecertChainEntry{
		PreCertificate:   ct.ASN1Cert{Data: der},
		CertificateChain: []ct.ASN1Cert{{Data: der}},
	})
	if err != nil {
		t.Fatalf("marshal PrecertChainEntry: %v", err)
	}
	t.EqualBytes("precert-chain-entry-encoding-is-unchanged", gotPreEntry, wantPreEntry)

	t.Done()
}

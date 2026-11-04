// Group inherited-ctfe-pool-and-logid.
//
// The parts of the CTFE personality that need no Trillian backend at all:
// ctfe.PEMCertPool (how the log's accepted roots are loaded and reported),
// ctfe.GetCTLogID (RFC 6962 s3.2, the log id is SHA-256 over the DER
// SubjectPublicKeyInfo) and ctfe.IsPrecertificate (the critical ASN.1 NULL
// poison extension).
//
// Every certificate and key here is a pinned corpus fixture, and path building
// runs under a pinned clock.
package main

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/pem"
	"strings"
	"time"

	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

var pinnedNow = time.Date(2030, 6, 1, 0, 0, 0, 0, time.UTC)

func loadDER(t *vhelp.T, rel ...string) *x509.Certificate {
	raw := vhelp.ReadFixture(rel...)
	if blk, _ := pem.Decode(raw); blk != nil {
		raw = blk.Bytes
	}
	c, err := x509.ParseCertificate(raw)
	if c == nil {
		t.Fatalf("%v did not parse: %v", rel, err)
	}
	return c
}

func main() {
	t := vhelp.New()

	root := loadDER(t, "minted", "bench-root.der")
	inter := loadDER(t, "minted", "bench-intermediate.der")

	pool := ctfe.NewPEMCertPool()
	t.Check("pool-starts-empty",
		len(pool.RawCertificates()) == 0 && len(pool.Subjects()) == 0,
		"raw=%d subjects=%d", len(pool.RawCertificates()), len(pool.Subjects()))

	ok := pool.AppendCertsFromPEM(vhelp.ReadFixture("minted", "roots-bench-plus-int.pem"))
	t.Check("pool-appends-both-pinned-roots", ok && len(pool.RawCertificates()) == 2,
		"ok=%v count=%d", ok, len(pool.RawCertificates()))

	raws := pool.RawCertificates()
	if len(raws) != 2 {
		t.Fatalf("pool holds %d certificates", len(raws))
	}
	t.Check("pool-raw-certificates-are-the-pinned-der",
		string(raws[0].Raw) == string(root.Raw) && string(raws[1].Raw) == string(inter.Raw),
		"first=%q second=%q", raws[0].Subject.CommonName, raws[1].Subject.CommonName)

	subs := pool.Subjects()
	want := map[string]bool{string(root.RawSubject): true, string(inter.RawSubject): true}
	for _, s := range subs {
		delete(want, string(s))
	}
	t.Check("pool-subjects-are-the-der-subjects", len(subs) == 2 && len(want) == 0,
		"subjects=%d unmatched=%d", len(subs), len(want))

	fromFile := ctfe.NewPEMCertPool()
	err := fromFile.AppendCertsFromPEMFile(vhelp.Fixture("minted", "roots-bench.pem"))
	t.Check("pool-loads-from-file", err == nil && len(fromFile.RawCertificates()) == 1,
		"err=%v count=%d", err, len(fromFile.RawCertificates()))

	junk := ctfe.NewPEMCertPool()
	t.Check("pool-rejects-input-with-no-certificates",
		!junk.AppendCertsFromPEM([]byte("this is not a PEM block\n")) && len(junk.RawCertificates()) == 0,
		"count=%d", len(junk.RawCertificates()))

	// The pool's CertPool is the one chain building actually uses.
	inters := x509.NewCertPool()
	inters.AddCert(inter)
	rootsOnly := ctfe.NewPEMCertPool()
	if err := rootsOnly.AppendCertsFromPEMFile(vhelp.Fixture("minted", "roots-bench.pem")); err != nil {
		t.Fatalf("roots-bench.pem: %v", err)
	}
	chains, err := loadDER(t, "minted", "leaf-valid.der").Verify(x509.VerifyOptions{
		Roots:         rootsOnly.CertPool(),
		Intermediates: inters,
		CurrentTime:   pinnedNow,
		KeyUsages:     []x509.ExtKeyUsage{x509.ExtKeyUsageAny},
	})
	t.Check("pool-certpool-anchors-chain-building", err == nil && len(chains) == 1 && len(chains[0]) == 3,
		"err=%v chains=%d", err, len(chains))

	// --- the log id ------------------------------------------------------------
	pubPEM := vhelp.ReadFixture("minted", "keys", "log-pubkey.pem")
	blk, _ := pem.Decode(pubPEM)
	if blk == nil {
		t.Fatalf("no PEM block in minted/keys/log-pubkey.pem")
	}
	pub, err := x509.ParsePKIXPublicKey(blk.Bytes)
	if err != nil {
		t.Fatalf("ParsePKIXPublicKey: %v", err)
	}
	id, err := ctfe.GetCTLogID(pub)
	if err != nil {
		t.Fatalf("GetCTLogID: %v", err)
	}
	pinnedID := strings.Fields(string(vhelp.ReadFixture("minted", "keys", "log-id.txt")))
	if len(pinnedID) == 0 {
		t.Fatalf("minted/keys/log-id.txt is empty")
	}
	t.Equal("logid-matches-pinned-fixture", hex.EncodeToString(id[:]), pinnedID[0])
	spki := sha256.Sum256(blk.Bytes)
	t.EqualBytes("logid-is-sha256-of-der-spki", id[:], spki[:])

	// --- precertificate detection ------------------------------------------------
	isPre, err := ctfe.IsPrecertificate(loadDER(t, "minted", "precert-poisoned.der"))
	t.Check("isprecertificate-true-for-poisoned-cert", isPre && err == nil, "isPre=%v err=%v", isPre, err)

	notPre, err := ctfe.IsPrecertificate(loadDER(t, "minted", "leaf-valid.der"))
	t.Check("isprecertificate-false-for-plain-leaf", !notPre && err == nil, "isPre=%v err=%v", notPre, err)

	t.Done()
}

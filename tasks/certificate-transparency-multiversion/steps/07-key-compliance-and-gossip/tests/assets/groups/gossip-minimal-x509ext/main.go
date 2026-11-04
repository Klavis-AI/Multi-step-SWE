// Group gossip-minimal-x509ext.
//
// v1.0.20 adds the minimal-gossip implementation (CHANGELOG v1.0.20: "An
// implementation of Gossip has been added. See the gossip/minimal package").
// Its durable, library-level part is gossip/minimal/x509ext: an STH carried
// inside an X.509 certificate extension, TLS-encoded per RFC 5246's
// presentation language, plus the two private-arc OIDs that name the
// extension and the matching extended key usage.
//
// Observed per function from a driver, as a library capability. The wire
// form is pinned as an exact byte string assembled here field by field from
// the presentation language -- a one-byte length for the opaque<0..255> log
// URL, one byte of Version, eight of TreeSize, eight of Timestamp,
// thirty-two of root hash and the four-byte DigitallySigned header -- so
// nothing expected comes from the code under test. The extension is then
// embedded in a real certificate and recovered through the three exported
// entrypoints, and the three ways the recovery must fail (no extension,
// trailing data, truncation) are pinned alongside so "it errored" is not the
// answer to everything.
//
// SCOPE-TIME: every value in the STH is a compile-time constant and the
// certificate's validity dates are fixed; nothing reads a clock.
//
// v1.0.19 has no gossip/minimal directory at all, so the import of
// .../gossip/minimal/x509ext does not resolve and this driver does not
// compile against it.
package main

import (
	"crypto"
	"crypto/rand"
	stdx509 "crypto/x509"
	"encoding/hex"
	"encoding/pem"
	"fmt"
	"math/big"
	"strings"
	"time"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/asn1"
	"github.com/google/certificate-transparency-go/gossip/minimal/x509ext"
	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/certificate-transparency-go/x509"
	"github.com/google/certificate-transparency-go/x509/pkix"

	"ctverify/vhelp"
)

// Pinned STH. Nothing below reads a clock.
const (
	pinnedLogURL    = "https://ct.example.invalid/bench"
	pinnedTreeSize  = uint64(100000)
	pinnedTimestamp = uint64(1519737600000)
)

// pinnedRoot is 00 01 02 .. 1f, chosen so the expected encoding below can be
// read off by eye.
var pinnedRoot = func() ct.SHA256Hash {
	var h ct.SHA256Hash
	for i := range h {
		h[i] = byte(i)
	}
	return h
}()

// pinnedSig is DigitallySigned{SHA256, ECDSA, 0xdeadbeef}: hash algorithm 4,
// signature algorithm 3, a uint16 length and four bytes of signature.
var pinnedSig = ct.DigitallySigned{
	Algorithm: tls.SignatureAndHashAlgorithm{Hash: tls.SHA256, Signature: tls.ECDSA},
	Signature: []byte{0xde, 0xad, 0xbe, 0xef},
}

// wantEncoding is the TLS presentation-language encoding of the LogSTHInfo
// built from the constants above, assembled by hand:
//
//	20                                 opaque LogURL<0..255>, 32 bytes
//	68 74 74 70 ... 63 68              "https://ct.example.invalid/bench"
//	00                                 Version = v1
//	00 00 00 00 00 01 86 a0            TreeSize = 100000
//	00 00 01 61 d7 6b 78 00            Timestamp = 1519737600000
//	00 01 02 ... 1e 1f                 SHA256RootHash
//	04 03                              DigitallySigned: SHA256, ECDSA
//	00 04 de ad be ef                  opaque signature<0..2^16-1>
const wantEncoding = "20" +
	"68747470733a2f2f63742e6578616d706c652e696e76616c69642f62656e6368" +
	"00" +
	"00000000000186a0" +
	"00000161d76b7800" +
	"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f" +
	"0403" +
	"0004deadbeef"

// Pinned, clock-free certificate skeleton.
var (
	notBefore = time.Date(2018, time.January, 1, 0, 0, 0, 0, time.UTC)
	notAfter  = time.Date(2028, time.January, 1, 0, 0, 0, 0, time.UTC)
)

func sthInfo() x509ext.LogSTHInfo {
	return x509ext.LogSTHInfo{
		LogURL:            []byte(pinnedLogURL),
		Version:           tls.Enum(ct.V1),
		TreeSize:          pinnedTreeSize,
		Timestamp:         pinnedTimestamp,
		SHA256RootHash:    pinnedRoot,
		TreeHeadSignature: pinnedSig,
	}
}

// signer parses the pinned fixture key. No signature this group produces is
// ever graded; CreateCertificate simply needs a real key pair.
func signer(t *vhelp.T) (crypto.Signer, crypto.PublicKey) {
	blk, _ := pem.Decode(vhelp.ReadFixture("minted", "keys", "root-key.pem"))
	if blk == nil {
		t.Fatalf("pinned key fixture is not PEM")
	}
	k, err := stdx509.ParsePKCS8PrivateKey(blk.Bytes)
	if err != nil {
		t.Fatalf("parse pinned key: %v", err)
	}
	s, ok := k.(crypto.Signer)
	if !ok {
		t.Fatalf("pinned key does not implement crypto.Signer")
	}
	return s, s.Public()
}

func baseTemplate() *x509.Certificate {
	return &x509.Certificate{
		SerialNumber:          big.NewInt(0x5714),
		Subject:               pkix.Name{CommonName: "harbor-ct-bench gossip subject"},
		NotBefore:             notBefore,
		NotAfter:              notAfter,
		BasicConstraintsValid: true,
		IsCA:                  true,
		KeyUsage:              x509.KeyUsageCertSign,
	}
}

// certWith mints a certificate carrying the given extra extensions and parses
// it back with the fork.
func certWith(t *vhelp.T, what string, priv crypto.Signer, pub crypto.PublicKey, exts ...pkix.Extension) *x509.Certificate {
	tmpl := baseTemplate()
	tmpl.ExtraExtensions = exts
	der, err := x509.CreateCertificate(rand.Reader, tmpl, tmpl, pub, priv)
	if err != nil {
		t.Fatalf("CreateCertificate(%s): %v", what, err)
	}
	cert, err := x509.ParseCertificate(der)
	if x509.IsFatal(err) {
		t.Fatalf("ParseCertificate(%s): %v", what, err)
	}
	return cert
}

func sthExtension(value []byte) pkix.Extension {
	return pkix.Extension{Id: x509ext.OIDExtensionCTSTH, Critical: false, Value: value}
}

func main() {
	t := vhelp.New()

	priv, pub := signer(t)

	// --- 1. the two OIDs the gossip profile defines -----------------------
	t.Equal("gossip-oid-constants",
		strings.Join([]string{
			x509ext.OIDExtensionCTSTH.String(),
			x509ext.OIDExtKeyUsageCTMinimalGossip.String(),
		}, " "),
		"1.3.6.1.4.1.11129.2.4.5 1.3.6.1.4.1.11129.2.4.6")

	// --- 2. the structure's wire form is exactly the pinned bytes ---------
	encoded, err := tls.Marshal(sthInfo())
	if err != nil {
		t.Fatalf("tls.Marshal(LogSTHInfo): %v", err)
	}
	want, err := hex.DecodeString(wantEncoding)
	if err != nil {
		t.Fatalf("bad pinned constant: %v", err)
	}
	t.EqualBytes("log-sth-info-tls-encoding", encoded, want)

	// --- 3. and it round-trips field for field ----------------------------
	var back x509ext.LogSTHInfo
	rest, err := tls.Unmarshal(want, &back)
	if err != nil {
		t.Check("log-sth-info-tls-round-trip", false, "Unmarshal: %v", err)
	} else {
		t.Equal("log-sth-info-tls-round-trip",
			fmt.Sprintf("rest=%d url=%s ver=%d size=%d ts=%d root=%x sig=%x hash=%d alg=%d",
				len(rest), back.LogURL, back.Version, back.TreeSize, back.Timestamp,
				back.SHA256RootHash[:], back.TreeHeadSignature.Signature,
				back.TreeHeadSignature.Algorithm.Hash, back.TreeHeadSignature.Algorithm.Signature),
			fmt.Sprintf("rest=%d url=%s ver=%d size=%d ts=%d root=%x sig=%x hash=%d alg=%d",
				0, pinnedLogURL, ct.V1, pinnedTreeSize, pinnedTimestamp,
				pinnedRoot[:], pinnedSig.Signature, tls.SHA256, tls.ECDSA))
	}

	// --- 4. a certificate carrying it is recognised -----------------------
	withSTH := certWith(t, "with-sth", priv, pub, sthExtension(want))
	without := certWith(t, "without-sth", priv, pub)
	t.Equal("has-sth-info-distinguishes-the-two-certificates",
		fmt.Sprintf("with=%v without=%v", x509ext.HasSTHInfo(withSTH), x509ext.HasSTHInfo(without)),
		"with=true without=false")

	// --- 5. and the extension it carries is the one that was asked for ----
	// The DER is read back off the parsed certificate, so this also pins
	// that CreateCertificate emitted the value unchanged and non-critical.
	var got []byte
	critical := "no-such-extension"
	for _, ext := range withSTH.Extensions {
		if ext.Id.Equal(x509ext.OIDExtensionCTSTH) {
			got = ext.Value
			critical = fmt.Sprintf("%v", ext.Critical)
		}
	}
	t.Equal("sth-extension-is-non-critical-and-byte-identical",
		fmt.Sprintf("critical=%s value=%x", critical, got),
		fmt.Sprintf("critical=false value=%s", wantEncoding))

	// --- 6. LogSTHInfoFromCert recovers the structure ---------------------
	info, err := x509ext.LogSTHInfoFromCert(withSTH)
	if err != nil {
		t.Check("log-sth-info-from-cert", false, "%v", err)
	} else {
		t.Equal("log-sth-info-from-cert",
			fmt.Sprintf("url=%s size=%d ts=%d root=%x", info.LogURL, info.TreeSize, info.Timestamp, info.SHA256RootHash[:]),
			fmt.Sprintf("url=%s size=%d ts=%d root=%x", pinnedLogURL, pinnedTreeSize, pinnedTimestamp, pinnedRoot[:]))
	}

	// --- 7. STHFromCert lifts it into a ct.SignedTreeHead -----------------
	// Only the five fields LogSTHInfo actually carries (CLIENT-API.md §6.2)
	// are graded. The SignedTreeHead has a LogID field that the extension has
	// no counterpart for, and no document states what becomes of it, so this
	// case says nothing about it either way.
	sth, err := x509ext.STHFromCert(withSTH)
	if err != nil {
		t.Check("sth-from-cert-lifts-the-extension-into-an-sth", false, "%v", err)
	} else {
		t.Equal("sth-from-cert-lifts-the-extension-into-an-sth",
			fmt.Sprintf("ver=%d size=%d ts=%d root=%x sig=%x",
				sth.Version, sth.TreeSize, sth.Timestamp, sth.SHA256RootHash[:],
				sth.TreeHeadSignature.Signature),
			fmt.Sprintf("ver=%d size=%d ts=%d root=%x sig=%x",
				ct.V1, pinnedTreeSize, pinnedTimestamp, pinnedRoot[:],
				pinnedSig.Signature))
	}

	// --- 8. a certificate without the extension yields an error -----------
	_, errNoExt := x509ext.LogSTHInfoFromCert(without)
	_, errNoExtSTH := x509ext.STHFromCert(without)
	t.Equal("absent-extension-is-an-error",
		fmt.Sprintf("info=%v sth=%v", errNoExt != nil, errNoExtSTH != nil),
		"info=true sth=true")

	// --- 9. trailing data after the STH is rejected -----------------------
	trailing := certWith(t, "trailing", priv, pub, sthExtension(append(append([]byte(nil), want...), 0x00)))
	_, errTrailing := x509ext.LogSTHInfoFromCert(trailing)
	t.Check("trailing-data-after-the-sth-is-rejected", errTrailing != nil,
		"an extension one byte too long was accepted")

	// --- 10. and so is a truncated one ------------------------------------
	truncated := certWith(t, "truncated", priv, pub, sthExtension(want[:len(want)-1]))
	_, errTruncated := x509ext.LogSTHInfoFromCert(truncated)
	t.Check("truncated-sth-is-rejected", errTruncated != nil,
		"an extension one byte too short was accepted")

	// --- 11. the gossip EKU survives a round trip through a certificate ---
	// It is not one of the EKUs the fork knows by name, so it must come back
	// in UnknownExtKeyUsage, spelled exactly as the constant.
	tmpl := baseTemplate()
	tmpl.UnknownExtKeyUsage = []asn1.ObjectIdentifier{x509ext.OIDExtKeyUsageCTMinimalGossip}
	der, err := x509.CreateCertificate(rand.Reader, tmpl, tmpl, pub, priv)
	if err != nil {
		t.Fatalf("CreateCertificate(eku): %v", err)
	}
	ekuCert, err := x509.ParseCertificate(der)
	if x509.IsFatal(err) {
		t.Fatalf("ParseCertificate(eku): %v", err)
	}
	unknown := make([]string, 0, len(ekuCert.UnknownExtKeyUsage))
	for _, oid := range ekuCert.UnknownExtKeyUsage {
		unknown = append(unknown, oid.String())
	}
	t.Equal("gossip-eku-round-trips-as-an-unknown-eku",
		fmt.Sprintf("named=%d unknown=%v", len(ekuCert.ExtKeyUsage), unknown),
		"named=0 unknown=[1.3.6.1.4.1.11129.2.4.6]")

	t.Done()
}

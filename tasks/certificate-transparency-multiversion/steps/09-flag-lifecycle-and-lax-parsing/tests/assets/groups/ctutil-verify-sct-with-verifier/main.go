// Group ctutil-verify-sct-with-verifier.
//
// v1.1.2 splits ctutil.VerifySCT into two: the public-key form keeps its
// signature, and a new exported entry point VerifySCTWithVerifier takes an
// already-constructed *ct.SignatureVerifier so a caller checking many SCTs
// against one log need not re-derive the verifier. Its documented contract
// adds one rule of its own: a nil verifier is an error, not a panic.
//
// Observed per function against the pinned RFC 6962 known-answer vectors, so
// the verdicts are fixed by the fixtures and not by anything the driver signs.
package main

import (
	"fmt"
	"strings"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/ctutil"
	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

// The timestamp of the pinned SCT vector, in milliseconds.
const sigTestSCTTimestamp = uint64(1348589665525)

func unhex(t *vhelp.T, s string) []byte {
	out := make([]byte, 0, len(s)/2)
	for i := 0; i+1 < len(s); i += 2 {
		hi, lo := hexVal(t, s[i]), hexVal(t, s[i+1])
		out = append(out, byte(hi<<4|lo))
	}
	return out
}

func hexVal(t *vhelp.T, c byte) int {
	switch {
	case c >= '0' && c <= '9':
		return int(c - '0')
	case c >= 'a' && c <= 'f':
		return int(c-'a') + 10
	case c >= 'A' && c <= 'F':
		return int(c-'A') + 10
	}
	t.Fatalf("bad hex byte %q in fixture", c)
	return 0
}

// kat loads the pinned known-answer table shipped with the fixture corpus.
func kat(t *vhelp.T) map[string]string {
	out := map[string]string{}
	for _, line := range strings.Split(string(vhelp.ReadFixture("rfc6962/kat.txt")), "\n") {
		if i := strings.IndexByte(line, ' '); i > 0 {
			out[line[:i]] = strings.TrimSpace(line[i+1:])
		}
	}
	return out
}

func pubKey(t *vhelp.T, name string) *ct.SignatureVerifier {
	pk, _, _, err := ct.PublicKeyFromPEM(vhelp.ReadFixture("rfc6962/" + name))
	if err != nil {
		t.Fatalf("PublicKeyFromPEM(%s): %v", name, err)
	}
	sv, err := ct.NewSignatureVerifier(pk)
	if err != nil {
		t.Fatalf("NewSignatureVerifier(%s): %v", name, err)
	}
	return sv
}

func sct(t *vhelp.T, sigHex string, ts uint64) *ct.SignedCertificateTimestamp {
	var ds tls.DigitallySigned
	if _, err := tls.Unmarshal(unhex(t, sigHex), &ds); err != nil {
		t.Fatalf("unmarshal pinned signature: %v", err)
	}
	return &ct.SignedCertificateTimestamp{
		SCTVersion: ct.V1,
		Timestamp:  ts,
		Signature:  ct.DigitallySigned(ds),
	}
}

func verdict(err error) string {
	if err == nil {
		return "nil"
	}
	return "err"
}

func main() {
	t := vhelp.New()
	k := kat(t)

	certDER := unhex(t, k["sigTestDERCertString"])
	leaf, err := x509.ParseCertificate(certDER)
	if err != nil && x509.IsFatal(err) {
		t.Fatalf("ParseCertificate(pinned vector): %v", err)
	}
	if leaf == nil {
		t.Fatalf("ParseCertificate(pinned vector) returned no certificate")
	}
	chain := []*x509.Certificate{leaf}

	ecSV := pubKey(t, "sig-test-ec256-public-key.pem")
	ec2SV := pubKey(t, "sig-test-ec256-public-key2.pem")
	rsaSV := pubKey(t, "sig-test-rsa-public-key.pem")

	ecSCT := sct(t, k["sigTestCertSCTSignatureEC"], sigTestSCTTimestamp)
	rsaSCT := sct(t, k["sigTestCertSCTSignatureRSA"], sigTestSCTTimestamp)

	// 1. The pinned ECDSA SCT verifies through the new entry point.
	t.Check("ec-sct-verifies",
		ctutil.VerifySCTWithVerifier(ecSV, chain, ecSCT, false) == nil,
		"VerifySCTWithVerifier rejected the pinned EC vector: %v",
		ctutil.VerifySCTWithVerifier(ecSV, chain, ecSCT, false))

	// 2. So does the pinned RSA SCT, under the RSA verifier.
	t.Check("rsa-sct-verifies",
		ctutil.VerifySCTWithVerifier(rsaSV, chain, rsaSCT, false) == nil,
		"VerifySCTWithVerifier rejected the pinned RSA vector: %v",
		ctutil.VerifySCTWithVerifier(rsaSV, chain, rsaSCT, false))

	// 3. A verifier built from a different log key rejects the same SCT.
	t.Check("wrong-key-rejected",
		ctutil.VerifySCTWithVerifier(ec2SV, chain, ecSCT, false) != nil,
		"a verifier for a different log key accepted the SCT")

	// 4. The documented nil-verifier rule: an error, not a panic, even when the
	//    chain and the SCT are the otherwise-valid pinned pair.
	nilErr := func() (e error) {
		defer func() {
			if r := recover(); r != nil {
				e = fmt.Errorf("panicked: %v", r)
				t.Check("nil-verifier-rejected", false, "VerifySCTWithVerifier(nil, ...) panicked: %v", r)
			}
		}()
		return ctutil.VerifySCTWithVerifier(nil, chain, ecSCT, false)
	}()
	if nilErr == nil || !strings.HasPrefix(nilErr.Error(), "panicked:") {
		t.Check("nil-verifier-rejected", nilErr != nil,
			"VerifySCTWithVerifier(nil, ...) returned no error")
	}

	// 5. An empty chain has no leaf to hash, so it is rejected even with a good
	//    verifier.
	t.Check("empty-chain-rejected",
		ctutil.VerifySCTWithVerifier(ecSV, nil, ecSCT, false) != nil,
		"VerifySCTWithVerifier accepted an empty chain")

	// 6. The public-key form is now a wrapper: for the same inputs it must
	//    reach exactly the same verdicts, including on a tampered timestamp.
	pk, _, _, err := ct.PublicKeyFromPEM(vhelp.ReadFixture("rfc6962/sig-test-ec256-public-key.pem"))
	if err != nil {
		t.Fatalf("PublicKeyFromPEM: %v", err)
	}
	bad := sct(t, k["sigTestCertSCTSignatureEC"], sigTestSCTTimestamp+1)
	t.Equal("wrapper-agrees-with-verifier-form",
		fmt.Sprintf("%s/%s|%s/%s",
			verdict(ctutil.VerifySCT(pk, chain, ecSCT, false)),
			verdict(ctutil.VerifySCT(pk, chain, bad, false)),
			verdict(ctutil.VerifySCTWithVerifier(ecSV, chain, ecSCT, false)),
			verdict(ctutil.VerifySCTWithVerifier(ecSV, chain, bad, false))),
		"nil/err|nil/err")

	t.Done()
}

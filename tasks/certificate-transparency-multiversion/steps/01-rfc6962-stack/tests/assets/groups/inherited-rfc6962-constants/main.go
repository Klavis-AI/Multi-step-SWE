// Group inherited-rfc6962-constants.
//
// The vocabulary the whole ladder rests on: the numeric values RFC 6962 gives
// its enums, the nine client API URI paths of s4, and the two base64 helpers
// the JSON wire format is built from. Everything here is a plain exported
// constant or an exported method on an exported type.
package main

import (
	"encoding/base64"
	"encoding/hex"
	"errors"
	"fmt"
	"strings"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"

	"ctverify/vhelp"
)

func katMap() map[string]string {
	m := map[string]string{}
	for _, ln := range strings.Split(string(vhelp.ReadFixture("rfc6962", "kat.txt")), "\n") {
		if f := strings.Fields(ln); len(f) == 2 {
			m[f[0]] = f[1]
		}
	}
	return m
}

func main() {
	t := vhelp.New()
	kat := katMap()

	t.Equal("logentrytype-values",
		fmt.Sprintf("%d/%d/%d", ct.X509LogEntryType, ct.PrecertLogEntryType, ct.XJSONLogEntryType),
		"0/1/32768")
	t.Equal("merkleleaftype-value", fmt.Sprintf("%d", ct.TimestampedEntryLeafType), "0")
	t.Equal("version-value", fmt.Sprintf("%d", ct.V1), "0")
	t.Equal("signaturetype-values",
		fmt.Sprintf("%d/%d", ct.CertificateTimestampSignatureType, ct.TreeHashSignatureType),
		"0/1")

	t.Equal("entrypoint-uri-paths", strings.Join([]string{
		ct.AddChainPath, ct.AddPreChainPath, ct.GetSTHPath, ct.GetEntriesPath,
		ct.GetProofByHashPath, ct.GetSTHConsistencyPath, ct.GetRootsPath,
		ct.GetEntryAndProofPath, ct.AddJSONPath,
	}, " "),
		"/ct/v1/add-chain /ct/v1/add-pre-chain /ct/v1/get-sth /ct/v1/get-entries "+
			"/ct/v1/get-proof-by-hash /ct/v1/get-sth-consistency /ct/v1/get-roots "+
			"/ct/v1/get-entry-and-proof /ct/v1/add-json")

	t.Equal("enum-renderings", strings.Join([]string{
		ct.X509LogEntryType.String(), ct.PrecertLogEntryType.String(), ct.XJSONLogEntryType.String(),
		ct.TimestampedEntryLeafType.String(), ct.V1.String(),
		ct.CertificateTimestampSignatureType.String(), ct.TreeHashSignatureType.String(),
	}, " "),
		"X509LogEntryType PrecertLogEntryType XJSONLogEntryType TimestampedEntryLeafType V1 "+
			"CertificateTimestamp TreeHash")

	// --- SHA256Hash base64 ---------------------------------------------------
	rootHashHex := kat["sigTestDefaultRootHash"]
	raw, err := hex.DecodeString(rootHashHex)
	if err != nil {
		t.Fatalf("corpus root hash: %v", err)
	}
	var h ct.SHA256Hash
	if err := h.FromBase64String(base64.StdEncoding.EncodeToString(raw)); err != nil {
		t.Fatalf("SHA256Hash.FromBase64String: %v", err)
	}
	t.Equal("sha256hash-base64-roundtrip", h.Base64String(), base64.StdEncoding.EncodeToString(raw))

	var tooShort ct.SHA256Hash
	err = tooShort.FromBase64String(base64.StdEncoding.EncodeToString(raw[:31]))
	t.Case("sha256hash-wrong-length-rejected", func() error {
		if err == nil {
			return errors.New("a 31-byte base64 value was accepted as a SHA256Hash")
		}
		return nil
	}())

	// --- DigitallySigned base64 ----------------------------------------------
	sigBytes, err := hex.DecodeString(kat["sigTestDefaultSTHSignature"])
	if err != nil {
		t.Fatalf("corpus STH signature: %v", err)
	}
	b64 := base64.StdEncoding.EncodeToString(sigBytes)
	var ds ct.DigitallySigned
	if err := ds.FromBase64String(b64); err != nil {
		t.Fatalf("DigitallySigned.FromBase64String: %v", err)
	}
	t.Check("digitallysigned-base64-decodes-algorithms",
		ds.Algorithm.Hash == tls.SHA256 && ds.Algorithm.Signature == tls.ECDSA && len(ds.Signature) == 0x48,
		"hash=%v sig=%v len=%d", ds.Algorithm.Hash, ds.Algorithm.Signature, len(ds.Signature))

	round, err := ds.Base64String()
	if err != nil {
		t.Fatalf("DigitallySigned.Base64String: %v", err)
	}
	t.Equal("digitallysigned-base64-roundtrip", round, b64)

	t.Done()
}

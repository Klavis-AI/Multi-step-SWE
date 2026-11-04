// Group loglist-signed-json.
//
// v1.0.13's loglist package can take the log list together with the detached
// signature the log-list publisher serves alongside it:
//
//	loglist.NewFromSignedJSON(llData, rawSig, pubKey)
//
// The signature value is the RAW one -- a bare DER ECDSA-Sig-Value, or a
// bare PKCS#1 v1.5 block -- and NOT a TLS DigitallySigned structure, which is
// the exception CLIENT-API.md section 4 calls out: this project TLS-wraps the
// signatures it defines, and the log list is somebody else's format. The hash
// is always SHA-256 and the algorithm comes from the key type, over the
// log-list bytes exactly as given; only then is the JSON parsed. RSA and
// ECDSA keys are accepted and anything else is refused.
//
// Boundary: loglist.NewFromSignedJSON. The log list and the two detached
// signatures over it are pinned fixtures shipped beside this group, generated
// once at authoring time and checked in; nothing here signs anything at run
// time.
package main

import (
	"os"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/loglist"

	"ctverify/vhelp"
)

func main() {
	t := vhelp.New()

	data := groupBytes(t, "loglist.json")
	sigEC := groupBytes(t, "loglist.sig.ec")
	sigRSA := groupBytes(t, "loglist.sig.rsa")
	notAList := groupBytes(t, "notalist.json")
	notAListSig := groupBytes(t, "notalist.sig.ec")

	ecKey := pubKey(t, "rfc6962", "sig-test-ec256-public-key.pem")
	otherECKey := pubKey(t, "rfc6962", "sig-test-ec256-public-key2.pem")
	rsaKey := pubKey(t, "rfc6962", "sig-test-rsa-public-key.pem")
	dsaKey := pubKey(t, "keys", "dsa-public-key.pem")

	// --- accepted ------------------------------------------------------------
	ll, err := loglist.NewFromSignedJSON(data, sigEC, ecKey)
	t.Check("accepts-an-ecdsa-signed-log-list",
		err == nil && ll != nil && len(ll.Logs) == 4 && len(ll.Operators) == 2,
		"err=%v, logs=%d", err, logCount(ll))

	llRSA, err := loglist.NewFromSignedJSON(data, sigRSA, rsaKey)
	t.Check("accepts-an-rsa-signed-log-list",
		err == nil && llRSA != nil && len(llRSA.Logs) == 4,
		"err=%v, logs=%d", err, logCount(llRSA))

	plain, plainErr := loglist.NewFromJSON(data)
	same := err == nil && plainErr == nil && ll != nil && plain != nil &&
		len(ll.Logs) == len(plain.Logs)
	if same {
		for i := range ll.Logs {
			if ll.Logs[i].Description != plain.Logs[i].Description ||
				string(ll.Logs[i].Key) != string(plain.Logs[i].Key) {
				same = false
				break
			}
		}
	}
	t.Check("signed-parse-yields-the-same-log-list-as-the-unsigned-parse", same,
		"signed and unsigned parses differ")

	// --- refused -------------------------------------------------------------
	_, err = loglist.NewFromSignedJSON(data, sigEC, otherECKey)
	t.Check("rejects-a-signature-checked-with-a-different-ecdsa-key", err != nil,
		"a signature verified under the wrong key")

	_, err = loglist.NewFromSignedJSON(data, sigRSA, ecKey)
	t.Check("rejects-an-rsa-signature-checked-with-an-ecdsa-key", err != nil,
		"an RSA signature verified as ECDSA")

	tampered := append([]byte{}, data...)
	tampered[len(tampered)/2] ^= 0x01
	_, err = loglist.NewFromSignedJSON(tampered, sigEC, ecKey)
	t.Check("rejects-a-log-list-with-one-flipped-bit", err != nil,
		"a modified log list verified")

	_, err = loglist.NewFromSignedJSON(append(append([]byte{}, data...), '\n'), sigEC, ecKey)
	t.Check("rejects-one-extra-trailing-byte", err != nil,
		"the signature covered more bytes than it was made over")

	_, err = loglist.NewFromSignedJSON(data, sigEC[:len(sigEC)-4], ecKey)
	t.Check("rejects-a-truncated-signature", err != nil, "a truncated signature verified")

	_, err = loglist.NewFromSignedJSON(data, sigEC, dsaKey)
	t.Check("rejects-a-key-type-the-log-list-format-does-not-allow", err != nil,
		"a DSA key was accepted for log-list signing")

	// A correct signature over the wrong payload is still not a log list.
	_, err = loglist.NewFromSignedJSON(notAList, notAListSig, ecKey)
	t.Check("a-valid-signature-over-something-that-is-not-a-log-list-is-an-error", err != nil,
		"a correctly signed non-log-list was accepted")

	t.Done()
}

func groupBytes(t *vhelp.T, name string) []byte {
	b, err := os.ReadFile(vhelp.GroupFile(name))
	if err != nil {
		t.Fatalf("reading %s: %v", name, err)
	}
	return b
}

func pubKey(t *vhelp.T, dir, name string) interface{} {
	k, _, _, err := ct.PublicKeyFromPEM(vhelp.ReadFixture(dir, name))
	if err != nil {
		t.Fatalf("parsing %s/%s: %v", dir, name, err)
	}
	return k
}

func logCount(ll *loglist.LogList) int {
	if ll == nil {
		return -1
	}
	return len(ll.Logs)
}

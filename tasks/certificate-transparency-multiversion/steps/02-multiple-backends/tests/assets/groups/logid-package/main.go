// Group logid-package.
//
// v1.0.5 adds the `logid/` package: a fixed-width 32-byte LogID type with
// factory functions that derive it from a log's DER SubjectPublicKeyInfo, as
// RFC 6962 s3.2 specifies, plus base64/raw-byte constructors and a rendering.
//
// Everything below is a known answer taken from a pinned fixture: the SHA-256
// of the minted log's DER SPKI is shipped in minted/keys/log-id.txt in both
// hex and standard base64, and keys/kat.txt carries upstream's own
// LogPublicKeyB64 constant. No clock, no network.
package main

import (
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/pem"
	"strings"

	"github.com/google/certificate-transparency-go/logid"

	"ctverify/vhelp"
)

func main() {
	t := vhelp.New()

	// The pinned identity of the minted log key.
	idLines := strings.Fields(string(vhelp.ReadFixture("minted", "keys", "log-id.txt")))
	if len(idLines) < 2 {
		t.Fatalf("minted/keys/log-id.txt does not hold the two pinned encodings")
	}
	wantHex, wantB64 := idLines[0], idLines[1]
	wantRaw, err := hex.DecodeString(wantHex)
	if err != nil || len(wantRaw) != sha256.Size {
		t.Fatalf("pinned log id is not 32 hex bytes: %v", err)
	}

	blk, _ := pem.Decode(vhelp.ReadFixture("minted", "keys", "log-pubkey.pem"))
	if blk == nil {
		t.Fatalf("minted/keys/log-pubkey.pem is not PEM")
	}
	spkiB64 := base64.StdEncoding.EncodeToString(blk.Bytes)

	// 1. FromPubKeyB64 hashes the base64-decoded DER SPKI, per RFC 6962 s3.2.
	got, err := logid.FromPubKeyB64(spkiB64)
	if err != nil {
		t.Check("from-pubkey-b64-minted", false, "FromPubKeyB64: %v", err)
	} else {
		t.EqualBytes("from-pubkey-b64-minted", got.Bytes(), wantRaw)
	}

	// 2. The same, over upstream's own pinned LogPublicKeyB64 constant.
	katB64 := katValue(t, "LogPublicKeyB64")
	katDER, err := base64.StdEncoding.DecodeString(katB64)
	if err != nil {
		t.Fatalf("pinned LogPublicKeyB64 is not base64: %v", err)
	}
	katWant := sha256.Sum256(katDER)
	got2, err := logid.FromPubKeyB64(katB64)
	if err != nil {
		t.Check("from-pubkey-b64-upstream-kat", false, "FromPubKeyB64: %v", err)
	} else {
		t.EqualBytes("from-pubkey-b64-upstream-kat", got2.Bytes(), katWant[:])
	}

	// 3. FromB64 parses the pinned base64 rendering of the same id.
	got3, err := logid.FromB64(wantB64)
	if err != nil {
		t.Check("from-b64-exact", false, "FromB64: %v", err)
	} else {
		t.EqualBytes("from-b64-exact", got3.Bytes(), wantRaw)
	}

	// 4. FromBytes copies exactly 32 bytes.
	got4, err := logid.FromBytes(wantRaw)
	if err != nil {
		t.Check("from-bytes-exact", false, "FromBytes: %v", err)
	} else {
		t.EqualBytes("from-bytes-exact", got4.Bytes(), wantRaw)
	}

	// 5. A short slice is refused rather than silently zero-padded.
	if _, err := logid.FromBytes(wantRaw[:31]); err == nil {
		t.Check("from-bytes-rejects-short", false, "FromBytes accepted 31 bytes")
	} else {
		t.Check("from-bytes-rejects-short", true, "")
	}

	// 6. Garbage base64 is refused.
	if _, err := logid.FromB64("not!valid!base64"); err == nil {
		t.Check("from-b64-rejects-garbage", false, "FromB64 accepted non-base64 input")
	} else {
		t.Check("from-b64-rejects-garbage", true, "")
	}

	// 7. String() is the exact documented rendering, on a value receiver:
	// CLIENT-API.md section 5.5 prints it complete, so the whole returned
	// string is compared (TEST_INTERFACE.md section 6.1, item 3).
	var id logid.LogID
	copy(id[:], wantRaw)
	t.Equal("string-rendering-exact", id.String(), "logid:["+wantB64+"]")

	// 8. Bytes() hands back exactly the 32 stored bytes.
	t.EqualBytes("bytes-accessor-exact", id.Bytes(), wantRaw)

	// 9. The two OrDie forms (CLIENT-API.md s5.5) are the same two conversions
	// with the error turned into a fatal log, so on well-formed input they
	// must return exactly what the checked forms returned. Only valid input is
	// driven here: the failing arm of an OrDie kills the calling process, and
	// a group that killed itself would report nothing at all.
	t.EqualBytes("from-b64-or-die-matches-from-b64", logid.FromB64OrDie(wantB64).Bytes(), wantRaw)
	t.EqualBytes("from-pubkey-b64-or-die-matches-from-pubkey-b64",
		logid.FromPubKeyB64OrDie(spkiB64).Bytes(), wantRaw)

	t.Done()
}

// katValue returns the value of one "<name> <value>" line of the pinned
// known-answer file. A missing name is a corpus fault, not a candidate fault.
func katValue(t *vhelp.T, name string) string {
	for _, line := range strings.Split(string(vhelp.ReadFixture("keys", "kat.txt")), "\n") {
		f := strings.Fields(line)
		if len(f) == 2 && f[0] == name {
			return f[1]
		}
	}
	t.Fatalf("keys/kat.txt has no %s entry", name)
	return ""
}

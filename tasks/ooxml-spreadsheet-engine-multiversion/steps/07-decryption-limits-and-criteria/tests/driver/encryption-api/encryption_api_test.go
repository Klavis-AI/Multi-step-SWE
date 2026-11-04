// Scored group: encryption-api.
//
// The workbook-encryption surface this milestone exposes: the Options value
// that carries a password, the Decrypt entry point, the Encrypt entry point
// and the limit it currently declares, and the way OpenReader and OpenFile
// decide whether a password is relevant at all. The container format for an
// encrypted workbook is the OLE compound file described in MS-OFFCRYPTO; the
// agile and standard encryption mechanisms are MS-OFFCRYPTO sections 2.3.4.10
// and 2.3.4.5.
//
// Encryption is deliberately one-way at this checkpoint: Decrypt is
// implemented, Encrypt is not, and it says so rather than writing a package a
// host application could not open. Because the library cannot encrypt, the
// decryption path is graded against a frozen fixture rather than a round trip
// of its own making -- a round trip would cancel any error in the key
// derivation and grade nothing.
package encryptionapi

import (
	"archive/zip"
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// fixtureDigests pins every asset this group reads, so the claims the fixture
// comment makes about its contents are checked by machine and not merely
// asserted in prose. The asset is baked into the verifier image and cannot
// drift between runs; this catches a regenerated fixture at authoring time.
var fixtureDigests = map[string]string{
	"standard-encrypted.xlsx": "fde330703371b16fc99fa774f986ca6cc645475bb8f48f402ba81358bc13dcdc",
}

// plain returns the bytes of a small unencrypted workbook.
func plain(t *testing.T) []byte {
	t.Helper()
	f := seedcalc.NewFile()
	if err := f.SetCellValue("Sheet1", "A1", "secret"); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	// Copy: the returned slice must not alias a buffer a later call may reuse.
	return append([]byte(nil), buf.Bytes()...)
}

func TestOptionsCarriesAPassword(t *testing.T) {
	opt := seedcalc.Options{Password: "pw123"}
	if opt.Password != "pw123" {
		t.Fatalf("Options.Password = %q, want %q", opt.Password, "pw123")
	}
	var zero seedcalc.Options
	if zero.Password != "" {
		t.Fatalf("the zero Options carries password %q, want the empty string", zero.Password)
	}
}

func TestEncryptReportsThatItIsNotImplemented(t *testing.T) {
	// Encrypt is declared but does not yet produce a package. It must say so
	// rather than return something that is not an encrypted workbook.
	out, err := seedcalc.Encrypt(plain(t), &seedcalc.Options{Password: "pw123"})
	if err == nil {
		t.Fatal("Encrypt returned no error; encryption is not supported at this checkpoint")
	}
	if len(out) != 0 {
		t.Fatalf("Encrypt returned %d bytes alongside an error; a refused encryption produces nothing", len(out))
	}
	if err.Error() != "not support encryption currently" {
		t.Fatalf("Encrypt error = %q, want the documented %q", err, "not support encryption currently")
	}
}

func TestDecryptRejectsInputThatIsNotACompoundFile(t *testing.T) {
	// An unencrypted workbook is a ZIP, not an OLE compound file, so there is
	// no EncryptionInfo stream to read.
	out, err := seedcalc.Decrypt(plain(t), &seedcalc.Options{Password: "pw123"})
	if err == nil {
		t.Fatal("Decrypt accepted an unencrypted ZIP workbook")
	}
	if len(out) != 0 {
		t.Fatalf("Decrypt returned %d bytes alongside an error", len(out))
	}
	for _, raw := range [][]byte{nil, []byte("not a compound file")} {
		out, err := seedcalc.Decrypt(raw, &seedcalc.Options{Password: "pw123"})
		if err == nil {
			t.Fatalf("Decrypt accepted %d bytes of input that is not a compound file", len(raw))
		}
		if len(out) != 0 {
			t.Fatalf("Decrypt returned %d bytes alongside an error", len(out))
		}
	}
}

func TestOpenReaderIgnoresAPasswordOnAnUnencryptedWorkbook(t *testing.T) {
	raw := plain(t)
	// The decryption path is entered only when the stream carries the OLE
	// identifier. A plain ZIP workbook opens whether or not a password is
	// supplied, and the password is simply not consulted.
	for _, opts := range [][]seedcalc.Options{
		nil,
		{seedcalc.Options{}},
		{seedcalc.Options{Password: "pw123"}},
		{seedcalc.Options{Password: "the wrong password entirely"}},
	} {
		f, err := seedcalc.OpenReader(bytes.NewReader(raw), opts...)
		if err != nil {
			t.Fatalf("OpenReader with %d options: %v", len(opts), err)
		}
		got, err := f.GetCellValue("Sheet1", "A1")
		if err != nil {
			t.Fatalf("GetCellValue: %v", err)
		}
		if got != "secret" {
			t.Fatalf("GetCellValue = %q, want %q", got, "secret")
		}
	}
}

func TestOpenFileAcceptsOptions(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellValue("Sheet1", "A1", "on disk"); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	path := filepath.Join(t.TempDir(), "book.xlsx")
	if err := f.SaveAs(path); err != nil {
		t.Fatalf("SaveAs: %v", err)
	}
	g, err := seedcalc.OpenFile(path, seedcalc.Options{Password: "pw123"})
	if err != nil {
		t.Fatalf("OpenFile with a password on an unencrypted workbook: %v", err)
	}
	got, err := g.GetCellValue("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetCellValue: %v", err)
	}
	if got != "on disk" {
		t.Fatalf("GetCellValue = %q, want %q", got, "on disk")
	}
}

func TestOpenReaderStillRejectsAStreamThatIsNeitherZipNorCompoundFile(t *testing.T) {
	for _, raw := range []string{"", "this is plain text", "PK-but-not-really"} {
		if _, err := seedcalc.OpenReader(strings.NewReader(raw), seedcalc.Options{Password: "pw123"}); err == nil {
			t.Fatalf("OpenReader accepted %q", raw)
		}
	}
}

// fixture returns the bytes of the frozen asset named by n.
//
// standard-encrypted.xlsx is an OLE compound file holding a workbook
// protected with the ECMA-376 *standard* mechanism (MS-OFFCRYPTO 2.3.4.5):
// AES-128 in ECB, SHA-1, 50,000 spin iterations, password "seedcalc". Its
// plain payload is a one-sheet workbook with A1 = "locked", B2 = 42 and the
// formula SUM(B2,8) in C3. The bytes are frozen, so the salt and therefore
// the whole ciphertext are fixed and decryption is deterministic.
func fixture(t *testing.T, n string) []byte {
	t.Helper()
	dir := os.Getenv("SEEDCALC_ASSETS")
	if dir == "" {
		t.Fatal("SEEDCALC_ASSETS is not set")
	}
	raw, err := os.ReadFile(filepath.Join(dir, n))
	if err != nil {
		t.Fatalf("reading the %s fixture: %v", n, err)
	}
	want, ok := fixtureDigests[n]
	if !ok {
		t.Fatalf("no pinned digest for the %s fixture", n)
	}
	sum := sha256.Sum256(raw)
	if got := hex.EncodeToString(sum[:]); got != want {
		t.Fatalf("the %s fixture has digest %s, want %s: the frozen asset has changed", n, got, want)
	}
	return raw
}

func TestAnEncryptedWorkbookOpensWithTheRightPassword(t *testing.T) {
	// The whole decryption path end to end: the compound-file container is
	// parsed, EncryptionInfo names the standard mechanism, the password is
	// spun into a key, EncryptedPackage is deciphered, and what comes out is
	// the ZIP package the rest of the library already knows how to read.
	f, err := seedcalc.OpenReader(bytes.NewReader(fixture(t, "standard-encrypted.xlsx")),
		seedcalc.Options{Password: "seedcalc"})
	if err != nil {
		t.Fatalf("OpenReader with the correct password: %v", err)
	}
	if got := f.GetSheetList(); len(got) != 1 || got[0] != "Sheet1" {
		t.Fatalf("GetSheetList = %v, want [Sheet1]", got)
	}
	for _, c := range []struct{ axis, want string }{{"A1", "locked"}, {"B2", "42"}} {
		got, err := f.GetCellValue("Sheet1", c.axis)
		if err != nil {
			t.Fatalf("GetCellValue(%s): %v", c.axis, err)
		}
		if got != c.want {
			t.Fatalf("GetCellValue(%s) = %q, want %q", c.axis, got, c.want)
		}
	}
	// The formula survives decryption as text, which it would not if the
	// deciphered bytes were merely a plausible-looking ZIP.
	formula, err := f.GetCellFormula("Sheet1", "C3")
	if err != nil {
		t.Fatalf("GetCellFormula(C3): %v", err)
	}
	if formula != "SUM(B2,8)" {
		t.Fatalf("GetCellFormula(C3) = %q, want %q", formula, "SUM(B2,8)")
	}
}

func TestDecryptReturnsThePlainPackage(t *testing.T) {
	// Decrypt is the documented entry point and hands back a ZIP, not a File.
	out, err := seedcalc.Decrypt(fixture(t, "standard-encrypted.xlsx"),
		&seedcalc.Options{Password: "seedcalc"})
	if err != nil {
		t.Fatalf("Decrypt: %v", err)
	}
	if len(out) < 4 || !bytes.HasPrefix(out, []byte("PK\x03\x04")) {
		t.Fatalf("Decrypt returned %d bytes not beginning with the ZIP local-header signature", len(out))
	}
	// The returned bytes open with no password at all, because they are no
	// longer encrypted.
	g, err := seedcalc.OpenReader(bytes.NewReader(out))
	if err != nil {
		t.Fatalf("OpenReader on the decrypted package: %v", err)
	}
	got, err := g.GetCellValue("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetCellValue: %v", err)
	}
	if got != "locked" {
		t.Fatalf("GetCellValue(A1) = %q, want %q", got, "locked")
	}
}

func TestTheWrongPasswordIsNotRejectedAsAPassword(t *testing.T) {
	// The standard mechanism carries a verifier hash, but this checkpoint does
	// not consult it: a wrong password derives a wrong key, the package is
	// deciphered into rubbish, and the complaint comes from the ZIP reader
	// afterwards rather than from the decryptor. So the error is NOT the
	// "decrypted file failed" that a failing Decrypt produces.
	_, err := seedcalc.OpenReader(bytes.NewReader(fixture(t, "standard-encrypted.xlsx")),
		seedcalc.Options{Password: "not the password"})
	if err == nil {
		t.Fatal("OpenReader accepted the wrong password")
	}
	if !errors.Is(err, zip.ErrFormat) {
		t.Fatalf("OpenReader error = %v, want the ZIP reader's own zip.ErrFormat over the rubbish a wrong key produces", err)
	}
	if strings.Contains(err.Error(), "decrypted file failed") {
		t.Fatalf("OpenReader error = %q; that error belongs to a Decrypt that failed, and Decrypt did not fail here", err)
	}
}

func TestADecryptionThatFailsIsReportedAsDecryptedFileFailed(t *testing.T) {
	// The documented failure mode of the decryption path. When the stream
	// carries the OLE identifier and a password is supplied, OpenReader
	// commits to decrypting it; if Decrypt then fails -- a malformed compound
	// file, or one whose EncryptionInfo cannot be read -- the caller is told
	// so, rather than being handed the ZIP reader's complaint about bytes
	// that were never deciphered.
	full := fixture(t, "standard-encrypted.xlsx")
	oleIdentifier := []byte{0xd0, 0xcf, 0x11, 0xe0, 0xa1, 0xb1, 0x1a, 0xe1}
	for _, c := range []struct {
		name string
		raw  []byte
	}{
		// The compound-file identifier followed by bytes that are not a
		// compound file at all: the container cannot be parsed.
		{"the identifier alone", append(append([]byte(nil), oleIdentifier...), []byte("not a compound file")...)},
		// The fixture's header only: the identifier is there, the directory
		// is not.
		{"the header alone", append([]byte(nil), full[:512]...)},
		// A truncated fixture: the container parses, but EncryptionInfo is
		// no longer readable from it.
		{"a truncated fixture", append([]byte(nil), full[:len(full)/2]...)},
	} {
		if !bytes.Contains(c.raw, oleIdentifier) {
			t.Fatalf("test setup: %s does not carry the OLE identifier, so the decryption path is not entered", c.name)
		}
		_, err := seedcalc.OpenReader(bytes.NewReader(c.raw), seedcalc.Options{Password: "seedcalc"})
		if err == nil {
			t.Fatalf("OpenReader accepted %s", c.name)
		}
		if !strings.Contains(err.Error(), "decrypted file failed") {
			t.Fatalf("OpenReader on %s = %q, want the documented %q", c.name, err, "decrypted file failed")
		}
	}
}

func TestAnEncryptedWorkbookWithNoOptionsIsNeverDecrypted(t *testing.T) {
	// The decryption path is entered only when at least one Options is passed.
	// With none, the compound file is handed to the ZIP reader unchanged.
	_, err := seedcalc.OpenReader(bytes.NewReader(fixture(t, "standard-encrypted.xlsx")))
	if err == nil {
		t.Fatal("OpenReader read an encrypted workbook with no options at all")
	}
	if !errors.Is(err, zip.ErrFormat) {
		t.Fatalf("OpenReader error = %v, want the ZIP reader's own zip.ErrFormat over an undeciphered compound file", err)
	}
}

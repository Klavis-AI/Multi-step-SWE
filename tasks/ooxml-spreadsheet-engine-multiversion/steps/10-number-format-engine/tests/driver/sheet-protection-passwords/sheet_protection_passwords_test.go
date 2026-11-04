// Scored group: sheet-protection-passwords.
//
// Worksheet protection gains a real password hash. Without an algorithm name
// the legacy sixteen-bit hash is written, as before; with one, the password
// is stretched over a hundred thousand iterations of the named digest against
// a random salt, and the algorithm, salt, hash and iteration count are all
// recorded in the element. Unprotecting may now be asked to check a password
// before it clears anything.
//
// The element and its attributes are ECMA-376 Part 1 section 18.3.1.85
// (sheetProtection); the iterated-hash scheme is section 18.3.1.85's
// algorithmName / hashValue / saltValue / spinCount group.
//
// The salt is drawn at random on each call, so the salt and the hash it
// produces are not asserted here -- only the attributes that do not vary and
// the round trip through UnprotectSheet, which is what the hash is for.
package sheetprotectionpasswords

import (
	"archive/zip"
	"bytes"
	"io"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// protection returns the sheetProtection element of the first worksheet as
// written, or the empty string when the sheet carries no protection.
func protection(t *testing.T, f *seedcalc.File) string {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	zr, err := zip.NewReader(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	if err != nil {
		t.Fatalf("zip.NewReader: %v", err)
	}
	for _, zf := range zr.File {
		if zf.Name != "xl/worksheets/sheet1.xml" {
			continue
		}
		rc, err := zf.Open()
		if err != nil {
			t.Fatalf("open sheet1.xml: %v", err)
		}
		b, err := io.ReadAll(rc)
		rc.Close()
		if err != nil {
			t.Fatalf("read sheet1.xml: %v", err)
		}
		for _, chunk := range strings.Split(strings.ReplaceAll(string(b), ">", ">\n"), "\n") {
			if strings.HasPrefix(chunk, "<sheetProtection") {
				return chunk
			}
		}
		return ""
	}
	t.Fatal("xl/worksheets/sheet1.xml is missing from the package")
	return ""
}

func mustContain(t *testing.T, element, want string) {
	t.Helper()
	if !strings.Contains(element, want) {
		t.Fatalf("sheetProtection element %q does not contain %q", element, want)
	}
}

func TestTheLegacyHashIsStillWrittenWithoutAnAlgorithmName(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.ProtectSheet("Sheet1", &seedcalc.FormatSheetProtection{
		Password:      "password",
		EditScenarios: true,
	}); err != nil {
		t.Fatalf("ProtectSheet: %v", err)
	}
	el := protection(t, f)
	// The sixteen-bit hash is deterministic and unchanged.
	mustContain(t, el, `password="83AF"`)
	mustContain(t, el, `sheet="true"`)
	mustContain(t, el, `scenarios="true"`)
	// None of the iterated-hash attributes appear.
	for _, attr := range []string{"algorithmName", "hashValue", "saltValue", "spinCount"} {
		if strings.Contains(el, attr) {
			t.Fatalf("legacy protection wrote %s: %q", attr, el)
		}
	}
}

func TestAnAlgorithmNameSelectsTheIteratedHash(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.ProtectSheet("Sheet1", &seedcalc.FormatSheetProtection{
		AlgorithmName:     "SHA-512",
		Password:          "password",
		SelectLockedCells: true,
	}); err != nil {
		t.Fatalf("ProtectSheet: %v", err)
	}
	el := protection(t, f)
	mustContain(t, el, `algorithmName="SHA-512"`)
	mustContain(t, el, `spinCount="100000"`)
	mustContain(t, el, `selectLockedCells="true"`)
	mustContain(t, el, `hashValue="`)
	mustContain(t, el, `saltValue="`)
	// The legacy attribute is not written alongside it.
	if strings.Contains(el, `password="`) {
		t.Fatalf("iterated protection also wrote the legacy password: %q", el)
	}
}

func TestEveryNamedDigestIsAccepted(t *testing.T) {
	for _, name := range []string{"MD4", "MD5", "SHA-1", "SHA-256", "SHA-384", "SHA-512"} {
		f := seedcalc.NewFile()
		if err := f.ProtectSheet("Sheet1", &seedcalc.FormatSheetProtection{
			AlgorithmName: name,
			Password:      "password",
		}); err != nil {
			t.Fatalf("ProtectSheet(%s): %v", name, err)
		}
		mustContain(t, protection(t, f), `algorithmName="`+name+`"`)
	}
}

func TestTheSaltIsDrawnFreshForEachCall(t *testing.T) {
	// Two protections of the same sheet with the same password must not
	// produce the same stored hash, which is what a per-call salt buys.
	var seen []string
	for i := 0; i < 2; i++ {
		f := seedcalc.NewFile()
		if err := f.ProtectSheet("Sheet1", &seedcalc.FormatSheetProtection{
			AlgorithmName: "SHA-512",
			Password:      "password",
		}); err != nil {
			t.Fatalf("ProtectSheet: %v", err)
		}
		seen = append(seen, protection(t, f))
	}
	if seen[0] == seen[1] {
		t.Fatalf("two protections produced an identical element: %q", seen[0])
	}
	// What does not vary is the algorithm and the iteration count.
	for _, el := range seen {
		mustContain(t, el, `algorithmName="SHA-512"`)
		mustContain(t, el, `spinCount="100000"`)
	}
}

func TestAnUnknownDigestAndAnOverlongPasswordAreRefused(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.ProtectSheet("Sheet1", &seedcalc.FormatSheetProtection{
		AlgorithmName: "MD2",
		Password:      "password",
	}); err == nil || err.Error() != "unsupported hash algorithm" {
		t.Fatalf("ProtectSheet(MD2) = %v, want unsupported hash algorithm", err)
	}
	if err := f.ProtectSheet("Sheet1", &seedcalc.FormatSheetProtection{
		AlgorithmName: "SHA-512",
		Password:      strings.Repeat("a", 256),
	}); err == nil || err.Error() != "password length invalid" {
		t.Fatalf("ProtectSheet(overlong) = %v, want password length invalid", err)
	}
	// The element itself is installed before the password is hashed, so a
	// refusal leaves the sheet protected but with no password material at
	// all -- neither the legacy attribute nor the iterated group.
	el := protection(t, f)
	mustContain(t, el, `sheet="true"`)
	for _, attr := range []string{"password=", "algorithmName", "hashValue", "saltValue", "spinCount"} {
		if strings.Contains(el, attr) {
			t.Fatalf("a refused ProtectSheet wrote %s: %q", attr, el)
		}
	}
}

func TestUnprotectSheetChecksAPasswordWhenGivenOne(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.ProtectSheet("Sheet1", &seedcalc.FormatSheetProtection{
		AlgorithmName: "SHA-512",
		Password:      "password",
	}); err != nil {
		t.Fatalf("ProtectSheet: %v", err)
	}
	if err := f.UnprotectSheet("Sheet1", "wrong"); err == nil || err.Error() != "worksheet protect password not match" {
		t.Fatalf("UnprotectSheet(wrong) = %v, want worksheet protect password not match", err)
	}
	// A failed attempt leaves the protection in place.
	mustContain(t, protection(t, f), `algorithmName="SHA-512"`)
	if err := f.UnprotectSheet("Sheet1", "password"); err != nil {
		t.Fatalf("UnprotectSheet(right): %v", err)
	}
	if el := protection(t, f); el != "" {
		t.Fatalf("after unprotecting the element is still %q", el)
	}
}

func TestTheLegacyHashAlsoVerifies(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.ProtectSheet("Sheet1", &seedcalc.FormatSheetProtection{Password: "password"}); err != nil {
		t.Fatalf("ProtectSheet: %v", err)
	}
	if err := f.UnprotectSheet("Sheet1", "wrong"); err == nil || err.Error() != "worksheet protect password not match" {
		t.Fatalf("UnprotectSheet(wrong) = %v, want worksheet protect password not match", err)
	}
	if err := f.UnprotectSheet("Sheet1", "password"); err != nil {
		t.Fatalf("UnprotectSheet(right): %v", err)
	}
	if el := protection(t, f); el != "" {
		t.Fatalf("after unprotecting the element is still %q", el)
	}
}

func TestUnprotectSheetWithoutAPasswordStillClearsUnconditionally(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.ProtectSheet("Sheet1", &seedcalc.FormatSheetProtection{
		AlgorithmName: "SHA-256",
		Password:      "password",
	}); err != nil {
		t.Fatalf("ProtectSheet: %v", err)
	}
	// No password argument means no check.
	if err := f.UnprotectSheet("Sheet1"); err != nil {
		t.Fatalf("UnprotectSheet: %v", err)
	}
	if el := protection(t, f); el != "" {
		t.Fatalf("after unprotecting the element is still %q", el)
	}
	// Clearing an unprotected sheet is idempotent without a password.
	if err := f.UnprotectSheet("Sheet1"); err != nil {
		t.Fatalf("second UnprotectSheet: %v", err)
	}
}

func TestUnprotectSheetReportsWhatItCannotDo(t *testing.T) {
	f := seedcalc.NewFile()
	// Checking a password against a sheet that has none.
	if err := f.UnprotectSheet("Sheet1", "password"); err == nil || err.Error() != "worksheet has set no protect" {
		t.Fatalf("UnprotectSheet(unprotected) = %v, want worksheet has set no protect", err)
	}
	if err := f.UnprotectSheet("Nope", "password"); err == nil || err.Error() != "sheet Nope is not exist" {
		t.Fatalf("UnprotectSheet(missing sheet) = %v, want sheet Nope is not exist", err)
	}
	if err := f.ProtectSheet("Nope", &seedcalc.FormatSheetProtection{Password: "password"}); err == nil || err.Error() != "sheet Nope is not exist" {
		t.Fatalf("ProtectSheet(missing sheet) = %v, want sheet Nope is not exist", err)
	}
}

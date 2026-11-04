// Scored group: save-as-path.
//
// SaveAs and the file-name ceiling it enforces before it touches the file
// system: a name of at most MaxFileNameLength characters is written, a longer
// one is refused without creating anything, and the variadic Options parameter
// added in this milestone is accepted in the same call position.
package saveaspath

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func TestSaveAsWritesAWorkbookAtAnOrdinaryPath(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellValue("Sheet1", "A1", "saved"); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	path := filepath.Join(t.TempDir(), "book.xlsx")
	if err := f.SaveAs(path); err != nil {
		t.Fatalf("SaveAs: %v", err)
	}
	info, err := os.Stat(path)
	if err != nil {
		t.Fatalf("stat the saved workbook: %v", err)
	}
	if info.Size() == 0 {
		t.Fatal("SaveAs created an empty file")
	}
	g, err := seedcalc.OpenFile(path)
	if err != nil {
		t.Fatalf("OpenFile: %v", err)
	}
	got, err := g.GetCellValue("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetCellValue: %v", err)
	}
	if got != "saved" {
		t.Fatalf("GetCellValue after SaveAs = %q, want %q", got, "saved")
	}
}

func TestSaveAsRefusesANameLongerThanTheLimit(t *testing.T) {
	f := seedcalc.NewFile()
	name := strings.Repeat("a", seedcalc.MaxFileNameLength+1)
	if err := f.SaveAs(name); err == nil {
		t.Fatalf("SaveAs accepted a name of %d characters; the limit is %d", len(name), seedcalc.MaxFileNameLength)
	} else if err.Error() != "file name length exceeds maximum limit" {
		t.Fatalf("SaveAs on an over-long name = %q, want the documented %q", err, "file name length exceeds maximum limit")
	}
	// The name is rejected before any file is opened, so nothing is created.
	if _, err := os.Stat(name); err == nil {
		t.Fatal("SaveAs created a file despite rejecting the name")
	}
}

func TestTheLengthCheckCountsTheWholeNameNotTheBaseName(t *testing.T) {
	f := seedcalc.NewFile()
	dir := t.TempDir()
	// A short base name under a directory long enough to push the full path
	// past the limit is still refused: the check is on the whole string.
	long := filepath.Join(dir, strings.Repeat("b", seedcalc.MaxFileNameLength), "x.xlsx")
	if len(long) <= seedcalc.MaxFileNameLength {
		t.Fatalf("test setup: path is only %d characters, expected more than %d", len(long), seedcalc.MaxFileNameLength)
	}
	if err := f.SaveAs(long); err == nil {
		t.Fatal("SaveAs accepted a path longer than MaxFileNameLength")
	} else if err.Error() != "file name length exceeds maximum limit" {
		// The length message, not the file system's -- the directory in the
		// middle of that path does not exist either, and the length check
		// runs first.
		t.Fatalf("SaveAs on an over-long path = %q, want the documented %q", err, "file name length exceeds maximum limit")
	}
}

func TestSaveAsReportsAnUnusablePath(t *testing.T) {
	f := seedcalc.NewFile()
	dir := t.TempDir()
	// Two paths that are within the length limit and still cannot be opened,
	// so the refusal comes from the file system rather than from the length
	// check. Both are deterministic: neither depends on how long the
	// temporary directory happens to be.
	blocker := filepath.Join(dir, "blocker")
	if err := os.WriteFile(blocker, []byte("a regular file, not a directory"), 0644); err != nil {
		t.Fatalf("writing the blocking file: %v", err)
	}
	for _, unusable := range []string{
		filepath.Join(dir, "no-such-dir", "book.xlsx"), // parent does not exist
		filepath.Join(blocker, "book.xlsx"),            // parent is a regular file
	} {
		if len(unusable) > seedcalc.MaxFileNameLength {
			t.Fatalf("test setup: %q is %d characters, past the %d-character ceiling, "+
				"so the refusal would come from the length check instead of the file system",
				unusable, len(unusable), seedcalc.MaxFileNameLength)
		}
		if err := f.SaveAs(unusable); err == nil {
			t.Fatalf("SaveAs accepted the unusable path %q", unusable)
		}
	}
}

func TestSaveAsAcceptsOptions(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellValue("Sheet1", "A1", "with options"); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	// SaveAs takes Options variadically; passing none and passing an empty
	// value are both legal call shapes.
	plain := filepath.Join(t.TempDir(), "plain.xlsx")
	if err := f.SaveAs(plain); err != nil {
		t.Fatalf("SaveAs with no options: %v", err)
	}
	opted := filepath.Join(t.TempDir(), "opted.xlsx")
	if err := f.SaveAs(opted, seedcalc.Options{}); err != nil {
		t.Fatalf("SaveAs with an empty Options value: %v", err)
	}
	g, err := seedcalc.OpenFile(opted)
	if err != nil {
		t.Fatalf("OpenFile on the workbook saved with Options: %v", err)
	}
	got, err := g.GetCellValue("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetCellValue: %v", err)
	}
	if got != "with options" {
		t.Fatalf("GetCellValue = %q, want %q", got, "with options")
	}
}

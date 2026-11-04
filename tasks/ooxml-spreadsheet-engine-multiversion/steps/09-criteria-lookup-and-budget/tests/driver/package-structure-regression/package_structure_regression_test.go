// Gate group: package-structure-regression.
//
// Inherited behaviour: whatever shared-formula and options machinery this
// milestone adds, a workbook written by Write or SaveAs must still
// be a well-formed OPC package -- a ZIP whose content-type stream covers every
// part and whose relationship parts resolve -- and it must still be readable
// back from disk and from memory. The package model is ECMA-376 Part 2 clause 6
// "Physical Package" and clause 7 "ZIP physical mapping"; the content-types
// stream is Part 2 clause 10; the SpreadsheetML part names are ECMA-376 Part 1
// section 12.3.
package packagestructureregression

import (
	"archive/zip"
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"encoding/xml"
	"io"
	"os"
	"path/filepath"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// fixtureDigests pins every asset this group reads, so a regenerated fixture
// is caught here rather than showing up as an unexplained decryption failure.
var fixtureDigests = map[string]string{
	"standard-encrypted.xlsx": "fde330703371b16fc99fa774f986ca6cc645475bb8f48f402ba81358bc13dcdc",
}

// fixture returns the bytes of a frozen asset, checked against its digest.
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

// contentTypes mirrors the [Content_Types].xml stream.
type contentTypes struct {
	Defaults []struct {
		Extension   string `xml:"Extension,attr"`
		ContentType string `xml:"ContentType,attr"`
	} `xml:"Default"`
	Overrides []struct {
		PartName    string `xml:"PartName,attr"`
		ContentType string `xml:"ContentType,attr"`
	} `xml:"Override"`
}

// relationships mirrors a .rels part.
type relationships struct {
	Relationship []struct {
		ID     string `xml:"Id,attr"`
		Type   string `xml:"Type,attr"`
		Target string `xml:"Target,attr"`
	} `xml:"Relationship"`
}

// pack writes f and returns a reader over the package.
func pack(t *testing.T, f *seedcalc.File) *zip.Reader {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	if buf.Len() == 0 {
		t.Fatal("Write produced an empty package")
	}
	zr, err := zip.NewReader(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	if err != nil {
		t.Fatalf("written package is not a readable ZIP container: %v", err)
	}
	return zr
}

// readPart returns the bytes of a named part.
func readPart(t *testing.T, zr *zip.Reader, name string) []byte {
	t.Helper()
	for _, zf := range zr.File {
		if zf.Name != name {
			continue
		}
		rc, err := zf.Open()
		if err != nil {
			t.Fatalf("open %s: %v", name, err)
		}
		defer rc.Close()
		b, err := io.ReadAll(rc)
		if err != nil {
			t.Fatalf("read %s: %v", name, err)
		}
		return b
	}
	t.Fatalf("written package has no part %s", name)
	return nil
}

// populated returns a workbook with a second sheet, some values and a formula.
func populated(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	f.NewSheet("Second")
	if err := f.SetCellValue("Sheet1", "A1", 1); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	if err := f.SetCellValue("Sheet1", "A2", "text"); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	if err := f.SetCellFormula("Sheet1", "A3", "=SUM(A1,1)"); err != nil {
		t.Fatalf("SetCellFormula: %v", err)
	}
	return f
}

func TestTheRequiredPartsArePresent(t *testing.T) {
	zr := pack(t, populated(t))
	if len(zr.File) == 0 {
		t.Fatal("written package holds no parts")
	}
	present := map[string]bool{}
	for _, zf := range zr.File {
		if strings.HasPrefix(zf.Name, "/") {
			t.Fatalf("part name %q begins with a slash; ZIP item names are relative", zf.Name)
		}
		present[zf.Name] = true
	}
	for _, want := range []string{
		"[Content_Types].xml",
		"_rels/.rels",
		"xl/workbook.xml",
		"xl/_rels/workbook.xml.rels",
		"xl/worksheets/sheet1.xml",
		"xl/worksheets/sheet2.xml",
		"docProps/app.xml",
		"docProps/core.xml",
	} {
		if !present[want] {
			t.Fatalf("written package has no part %s", want)
		}
	}
}

func TestEveryPartIsCoveredByTheContentTypeStream(t *testing.T) {
	zr := pack(t, populated(t))
	var ct contentTypes
	if err := xml.Unmarshal(readPart(t, zr, "[Content_Types].xml"), &ct); err != nil {
		t.Fatalf("[Content_Types].xml is not well-formed: %v", err)
	}
	defaults := map[string]string{}
	for _, d := range ct.Defaults {
		defaults[strings.ToLower(d.Extension)] = d.ContentType
	}
	overrides := map[string]string{}
	for _, o := range ct.Overrides {
		overrides[o.PartName] = o.ContentType
	}
	for _, zf := range zr.File {
		if zf.Name == "[Content_Types].xml" {
			continue
		}
		if _, ok := overrides["/"+zf.Name]; ok {
			continue
		}
		ext := strings.ToLower(strings.TrimPrefix(filepath.Ext(zf.Name), "."))
		if _, ok := defaults[ext]; !ok {
			t.Fatalf("part %s is covered by neither an Override nor a Default for extension %q", zf.Name, ext)
		}
	}
	if got := overrides["/xl/workbook.xml"]; got == "" {
		t.Fatal("[Content_Types].xml declares no content type for /xl/workbook.xml")
	}
	if got := overrides["/xl/worksheets/sheet1.xml"]; got != seedcalc.ContentTypeSpreadSheetMLWorksheet {
		t.Fatalf("content type of /xl/worksheets/sheet1.xml = %q, want %q", got, seedcalc.ContentTypeSpreadSheetMLWorksheet)
	}
}

func TestTheRelationshipGraphResolves(t *testing.T) {
	zr := pack(t, populated(t))
	present := map[string]bool{}
	for _, zf := range zr.File {
		present[zf.Name] = true
	}
	var root relationships
	if err := xml.Unmarshal(readPart(t, zr, "_rels/.rels"), &root); err != nil {
		t.Fatalf("_rels/.rels is not well-formed: %v", err)
	}
	if len(root.Relationship) == 0 {
		t.Fatal("the package declares no root relationships")
	}
	for _, r := range root.Relationship {
		target := strings.TrimPrefix(r.Target, "/")
		if !present[target] {
			t.Fatalf("package relationship %s points at %q, which is not a part of the package", r.ID, r.Target)
		}
	}
	var wb relationships
	if err := xml.Unmarshal(readPart(t, zr, "xl/_rels/workbook.xml.rels"), &wb); err != nil {
		t.Fatalf("xl/_rels/workbook.xml.rels is not well-formed: %v", err)
	}
	if len(wb.Relationship) == 0 {
		t.Fatal("the workbook declares no relationships")
	}
	for _, r := range wb.Relationship {
		if strings.HasPrefix(r.Target, "http://") || strings.HasPrefix(r.Target, "https://") {
			continue
		}
		// A workbook relationship target is either absolute within the package
		// ("/xl/worksheets/sheet2.xml") or relative to the workbook part's own
		// directory ("worksheets/sheet1.xml"). Both forms appear in a package
		// this library writes, so resolve each against its own base.
		target := "xl/" + r.Target
		if strings.HasPrefix(r.Target, "/") {
			target = strings.TrimPrefix(r.Target, "/")
		}
		if !present[target] {
			t.Fatalf("workbook relationship %s points at %q, which resolves to %q and is not a part of the package", r.ID, r.Target, target)
		}
	}
}

func TestOpenReaderRejectsAStreamThatIsNotAPackage(t *testing.T) {
	if _, err := seedcalc.OpenReader(strings.NewReader("this is not a zip archive")); err == nil {
		t.Fatal("OpenReader accepted a stream that is not a ZIP container")
	}
	if _, err := seedcalc.OpenReader(bytes.NewReader(nil)); err == nil {
		t.Fatal("OpenReader accepted an empty stream")
	}
	if _, err := seedcalc.OpenFile(filepath.Join(t.TempDir(), "absent.xlsx")); err == nil {
		t.Fatal("OpenFile accepted a path that does not exist")
	}
}

func TestSaveAsAndOpenFileRoundTripTheWorkbook(t *testing.T) {
	f := populated(t)
	path := filepath.Join(t.TempDir(), "book.xlsx")
	if err := f.SaveAs(path); err != nil {
		t.Fatalf("SaveAs: %v", err)
	}
	g, err := seedcalc.OpenFile(path)
	if err != nil {
		t.Fatalf("OpenFile: %v", err)
	}
	got, err := g.GetCellValue("Sheet1", "A2")
	if err != nil {
		t.Fatalf("GetCellValue: %v", err)
	}
	if got != "text" {
		t.Fatalf("GetCellValue after SaveAs/OpenFile = %q, want %q", got, "text")
	}
	formula, err := g.GetCellFormula("Sheet1", "A3")
	if err != nil {
		t.Fatalf("GetCellFormula: %v", err)
	}
	if formula != "=SUM(A1,1)" {
		t.Fatalf("GetCellFormula after SaveAs/OpenFile = %q, want %q", formula, "=SUM(A1,1)")
	}
	if names := g.GetSheetList(); len(names) != 2 {
		t.Fatalf("sheet list after SaveAs/OpenFile = %v, want two sheets", names)
	}
}

func TestSaveWritesBackToTheOpenedPath(t *testing.T) {
	f := populated(t)
	path := filepath.Join(t.TempDir(), "book.xlsx")
	if err := f.SaveAs(path); err != nil {
		t.Fatalf("SaveAs: %v", err)
	}
	g, err := seedcalc.OpenFile(path)
	if err != nil {
		t.Fatalf("OpenFile: %v", err)
	}
	if err := g.SetCellValue("Sheet1", "A2", "changed"); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	if err := g.Save(); err != nil {
		t.Fatalf("Save: %v", err)
	}
	h, err := seedcalc.OpenFile(path)
	if err != nil {
		t.Fatalf("OpenFile after Save: %v", err)
	}
	got, err := h.GetCellValue("Sheet1", "A2")
	if err != nil {
		t.Fatalf("GetCellValue after Save: %v", err)
	}
	if got != "changed" {
		t.Fatalf("GetCellValue after Save = %q, want %q", got, "changed")
	}
	// A workbook built in memory has no origin path, so Save has nowhere to go.
	if err := seedcalc.NewFile().Save(); err == nil {
		t.Fatal("Save on an in-memory workbook with no path returned no error")
	}
}

func TestAnEncryptedPackageStillDecryptsToAReadableContainer(t *testing.T) {
	// The decryption path established at milestone 07 is inherited unchanged.
	// It is asserted here because it is the one way into the package reader
	// that does not begin with a plain ZIP, so a change to the container
	// plumbing can break it while every other case in this group still passes.
	// Only the outcome is graded: that the right password yields the workbook,
	// that Decrypt hands back a plain package, and that a wrong password does
	// not silently succeed. No diagnostic text is pinned.
	f, err := seedcalc.OpenReader(bytes.NewReader(fixture(t, "standard-encrypted.xlsx")),
		seedcalc.Options{Password: "seedcalc"})
	if err != nil {
		t.Fatalf("OpenReader with the correct password: %v", err)
	}
	if got := f.GetSheetList(); len(got) != 1 || got[0] != "Sheet1" {
		t.Fatalf("GetSheetList = %v, want [Sheet1]", got)
	}
	got, err := f.GetCellValue("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetCellValue(A1): %v", err)
	}
	if got != "locked" {
		t.Fatalf("GetCellValue(A1) = %q, want %q", got, "locked")
	}

	// Decrypt is the standalone entry point and returns the plain ZIP, which
	// then opens with no password at all.
	out, err := seedcalc.Decrypt(fixture(t, "standard-encrypted.xlsx"),
		&seedcalc.Options{Password: "seedcalc"})
	if err != nil {
		t.Fatalf("Decrypt: %v", err)
	}
	if !bytes.HasPrefix(out, []byte("PK\x03\x04")) {
		t.Fatalf("Decrypt returned %d bytes not beginning with the ZIP local-header signature", len(out))
	}
	if _, err := seedcalc.OpenReader(bytes.NewReader(out)); err != nil {
		t.Fatalf("OpenReader on the decrypted package: %v", err)
	}

	// A wrong password must fail rather than hand back plausible rubbish.
	if _, err := seedcalc.OpenReader(bytes.NewReader(fixture(t, "standard-encrypted.xlsx")),
		seedcalc.Options{Password: "wrong"}); err == nil {
		t.Fatal("OpenReader with an incorrect password returned no error")
	}
}

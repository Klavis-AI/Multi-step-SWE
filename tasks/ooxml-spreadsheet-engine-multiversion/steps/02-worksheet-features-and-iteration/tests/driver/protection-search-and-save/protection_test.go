// Scored group: protection-search-and-save.
//
// Worksheet protection and its removal, searching a worksheet for a value,
// enumerating merged ranges, and the in-memory save paths. Graded against
// ECMA-376 Part 1 clause 18.3.1.85 (sheetProtection, and the individual
// permission attributes it carries, including the hashed password attribute),
// clause 18.3.1.55 (mergeCells) and clause 18.3.1.54 (mergeCell/@ref, an
// A1-style range reference). Behaviour cases were checked against the upstream
// release this checkpoint tracks (tag v1.4.1).
package protectionsearchandsave

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"io"
	"sort"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

type xmlSheetProtection struct {
	Password            string `xml:"password,attr"`
	Sheet               bool   `xml:"sheet,attr"`
	Objects             bool   `xml:"objects,attr"`
	Scenarios           bool   `xml:"scenarios,attr"`
	SelectLockedCells   bool   `xml:"selectLockedCells,attr"`
	SelectUnlockedCells bool   `xml:"selectUnlockedCells,attr"`
	FormatCells         bool   `xml:"formatCells,attr"`
	FormatColumns       bool   `xml:"formatColumns,attr"`
	FormatRows          bool   `xml:"formatRows,attr"`
	InsertRows          bool   `xml:"insertRows,attr"`
	InsertColumns       bool   `xml:"insertColumns,attr"`
	InsertHyperlinks    bool   `xml:"insertHyperlinks,attr"`
	DeleteRows          bool   `xml:"deleteRows,attr"`
	DeleteColumns       bool   `xml:"deleteColumns,attr"`
	AutoFilter          bool   `xml:"autoFilter,attr"`
	PivotTables         bool   `xml:"pivotTables,attr"`
	Sort                bool   `xml:"sort,attr"`
}

type xmlMergeCell struct {
	Ref string `xml:"ref,attr"`
}

type worksheet struct {
	SheetProtection *xmlSheetProtection `xml:"sheetProtection"`
	MergeCells      []xmlMergeCell      `xml:"mergeCells>mergeCell"`
}

func readPart(t *testing.T, pkg []byte, name string) []byte {
	t.Helper()
	zr, err := zip.NewReader(bytes.NewReader(pkg), int64(len(pkg)))
	if err != nil {
		t.Fatalf("package is not a ZIP container: %v", err)
	}
	for _, fh := range zr.File {
		if fh.Name != name {
			continue
		}
		rc, err := fh.Open()
		if err != nil {
			t.Fatalf("open %s: %v", name, err)
		}
		defer rc.Close()
		var out bytes.Buffer
		if _, err := io.Copy(&out, rc); err != nil {
			t.Fatalf("read %s: %v", name, err)
		}
		return out.Bytes()
	}
	t.Fatalf("package has no part %s", name)
	return nil
}

func decodeSheet(t *testing.T, f *seedcalc.File) worksheet {
	t.Helper()
	buf, err := f.WriteToBuffer()
	if err != nil {
		t.Fatalf("WriteToBuffer: %v", err)
	}
	var ws worksheet
	if err := xml.Unmarshal(readPart(t, buf.Bytes(), "xl/worksheets/sheet1.xml"), &ws); err != nil {
		t.Fatalf("worksheet part is not well-formed XML: %v", err)
	}
	return ws
}

func TestProtectSheetRecordsTheRequestedRestrictions(t *testing.T) {
	f := seedcalc.NewFile()
	f.ProtectSheet("Sheet1", &seedcalc.FormatSheetProtection{
		SelectLockedCells: true,
		FormatCells:       true,
		InsertRows:        true,
		Sort:              true,
	})

	ws := decodeSheet(t, f)
	if ws.SheetProtection == nil {
		t.Fatal("written worksheet carries no sheetProtection element")
	}
	if !ws.SheetProtection.Sheet {
		t.Error("sheetProtection/@sheet = false, want true -- protecting a worksheet always locks the sheet itself")
	}
	for _, tc := range []struct {
		name string
		got  bool
	}{
		{"selectLockedCells", ws.SheetProtection.SelectLockedCells},
		{"formatCells", ws.SheetProtection.FormatCells},
		{"insertRows", ws.SheetProtection.InsertRows},
		{"sort", ws.SheetProtection.Sort},
	} {
		if !tc.got {
			t.Errorf("sheetProtection/@%s = false, want true", tc.name)
		}
	}
	for _, tc := range []struct {
		name string
		got  bool
	}{
		{"deleteRows", ws.SheetProtection.DeleteRows},
		{"selectUnlockedCells", ws.SheetProtection.SelectUnlockedCells},
		{"objects", ws.SheetProtection.Objects},
	} {
		if tc.got {
			t.Errorf("sheetProtection/@%s = true, want false -- it was not requested", tc.name)
		}
	}
}

func TestProtectSheetRecordsEveryPermissionMember(t *testing.T) {
	// Every one of the fifteen boolean FormatSheetProtection members reaches
	// its own sheetProtection attribute; EditObjects and EditScenarios map to
	// the objects and scenarios attributes respectively.
	f := seedcalc.NewFile()
	f.ProtectSheet("Sheet1", &seedcalc.FormatSheetProtection{
		AutoFilter:          true,
		DeleteColumns:       true,
		DeleteRows:          true,
		EditObjects:         true,
		EditScenarios:       true,
		FormatCells:         true,
		FormatColumns:       true,
		FormatRows:          true,
		InsertColumns:       true,
		InsertHyperlinks:    true,
		InsertRows:          true,
		PivotTables:         true,
		SelectLockedCells:   true,
		SelectUnlockedCells: true,
		Sort:                true,
	})

	ws := decodeSheet(t, f)
	if ws.SheetProtection == nil {
		t.Fatal("written worksheet carries no sheetProtection element")
	}
	for _, tc := range []struct {
		name string
		got  bool
	}{
		{"autoFilter", ws.SheetProtection.AutoFilter},
		{"deleteColumns", ws.SheetProtection.DeleteColumns},
		{"deleteRows", ws.SheetProtection.DeleteRows},
		{"objects", ws.SheetProtection.Objects},
		{"scenarios", ws.SheetProtection.Scenarios},
		{"formatCells", ws.SheetProtection.FormatCells},
		{"formatColumns", ws.SheetProtection.FormatColumns},
		{"formatRows", ws.SheetProtection.FormatRows},
		{"insertColumns", ws.SheetProtection.InsertColumns},
		{"insertHyperlinks", ws.SheetProtection.InsertHyperlinks},
		{"insertRows", ws.SheetProtection.InsertRows},
		{"pivotTables", ws.SheetProtection.PivotTables},
		{"selectLockedCells", ws.SheetProtection.SelectLockedCells},
		{"selectUnlockedCells", ws.SheetProtection.SelectUnlockedCells},
		{"sort", ws.SheetProtection.Sort},
	} {
		if !tc.got {
			t.Errorf("sheetProtection/@%s = false after requesting it, want true", tc.name)
		}
	}
}

func TestProtectSheetStoresAHashedPassword(t *testing.T) {
	const password = "s3cret"
	f := seedcalc.NewFile()
	f.ProtectSheet("Sheet1", &seedcalc.FormatSheetProtection{Password: password})

	ws := decodeSheet(t, f)
	if ws.SheetProtection == nil {
		t.Fatal("written worksheet carries no sheetProtection element")
	}
	if ws.SheetProtection.Password == "" {
		t.Fatal("sheetProtection carries no password attribute")
	}
	if ws.SheetProtection.Password == password {
		t.Error("the password was stored verbatim; it must be stored as a hash")
	}
	if strings.Contains(ws.SheetProtection.Password, password) {
		t.Error("the stored password contains the plaintext")
	}

	g := seedcalc.NewFile()
	g.ProtectSheet("Sheet1", &seedcalc.FormatSheetProtection{Password: password})
	other := decodeSheet(t, g)
	if other.SheetProtection == nil || other.SheetProtection.Password != ws.SheetProtection.Password {
		t.Error("the same password produced two different stored values")
	}

	h := seedcalc.NewFile()
	h.ProtectSheet("Sheet1", &seedcalc.FormatSheetProtection{})
	none := decodeSheet(t, h)
	if none.SheetProtection == nil {
		t.Fatal("written worksheet carries no sheetProtection element")
	}
	if none.SheetProtection.Password != "" {
		t.Errorf("a protection request without a password stored %q", none.SheetProtection.Password)
	}
}

func TestProtectSheetWithoutSettingsUsesTheDefaultRestrictions(t *testing.T) {
	f := seedcalc.NewFile()
	f.ProtectSheet("Sheet1", nil)

	ws := decodeSheet(t, f)
	if ws.SheetProtection == nil {
		t.Fatal("written worksheet carries no sheetProtection element")
	}
	if !ws.SheetProtection.Sheet {
		t.Error("sheetProtection/@sheet = false, want true")
	}
	if !ws.SheetProtection.Objects {
		t.Error("sheetProtection/@objects = false, want true by default")
	}
	if !ws.SheetProtection.Scenarios {
		t.Error("sheetProtection/@scenarios = false, want true by default")
	}
	if !ws.SheetProtection.SelectLockedCells {
		t.Error("sheetProtection/@selectLockedCells = false, want true by default")
	}
	if ws.SheetProtection.DeleteRows {
		t.Error("sheetProtection/@deleteRows = true, want false by default")
	}
}

func TestUnprotectSheetRemovesTheProtection(t *testing.T) {
	f := seedcalc.NewFile()
	f.ProtectSheet("Sheet1", &seedcalc.FormatSheetProtection{Password: "pw", SelectLockedCells: true})
	if decodeSheet(t, f).SheetProtection == nil {
		t.Fatal("ProtectSheet did not produce a sheetProtection element")
	}

	f.UnprotectSheet("Sheet1")
	if got := decodeSheet(t, f).SheetProtection; got != nil {
		t.Errorf("sheetProtection is still present after UnprotectSheet: %+v", got)
	}

	// Unprotecting a worksheet that was never protected is not an error.
	g := seedcalc.NewFile()
	g.UnprotectSheet("Sheet1")
	if got := decodeSheet(t, g).SheetProtection; got != nil {
		t.Errorf("an unprotected worksheet gained a sheetProtection element: %+v", got)
	}
}

func TestSearchSheetReturnsTheCoordinatesOfEveryExactMatch(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", "apple")
	f.SetCellValue("Sheet1", "B2", "banana")
	f.SetCellValue("Sheet1", "C3", "apple")

	got := f.SearchSheet("Sheet1", "apple")
	sort.Strings(got)
	want := []string{"A1", "C3"}
	if len(got) != len(want) {
		t.Fatalf("SearchSheet returned %v, want %v", got, want)
	}
	for i := range want {
		if got[i] != want[i] {
			t.Fatalf("SearchSheet returned %v, want %v", got, want)
		}
	}
	if hits := f.SearchSheet("Sheet1", "appl"); len(hits) != 0 {
		t.Errorf("SearchSheet matched a prefix: %v -- the default search is an exact match", hits)
	}
	if hits := f.SearchSheet("Sheet1", "missing"); len(hits) != 0 {
		t.Errorf("SearchSheet returned %v for a value that is not present", hits)
	}
}

func TestSearchSheetMatchesARegularExpressionWhenAsked(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", "apple")
	f.SetCellValue("Sheet1", "B2", "banana")
	f.SetCellValue("Sheet1", "C3", "apricot")

	got := f.SearchSheet("Sheet1", "^ap", true)
	sort.Strings(got)
	want := []string{"A1", "C3"}
	if len(got) != len(want) {
		t.Fatalf("regular-expression search returned %v, want %v", got, want)
	}
	for i := range want {
		if got[i] != want[i] {
			t.Fatalf("regular-expression search returned %v, want %v", got, want)
		}
	}
	if hits := f.SearchSheet("Sheet1", "^ap"); len(hits) != 0 {
		t.Errorf("the pattern matched %v without the regular-expression flag", hits)
	}
	if hits := f.SearchSheet("Sheet1", "an+a", true); len(hits) != 1 || hits[0] != "B2" {
		t.Errorf("regular-expression search for an+a returned %v, want [B2]", hits)
	}
}

func TestGetMergeCellsEnumeratesEveryMergedRange(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", "top left")
	f.MergeCell("Sheet1", "A1", "B2")
	f.SetCellValue("Sheet1", "D4", "second")
	f.MergeCell("Sheet1", "D4", "E5")

	merged := f.GetMergeCells("Sheet1")
	if len(merged) != 2 {
		t.Fatalf("GetMergeCells returned %d ranges, want 2: %v", len(merged), merged)
	}
	byStart := map[string]seedcalc.MergeCell{}
	for _, m := range merged {
		byStart[m.GetStartAxis()] = m
	}
	first, ok := byStart["A1"]
	if !ok {
		t.Fatalf("no merged range starts at A1: %v", merged)
	}
	if got := first.GetEndAxis(); got != "B2" {
		t.Errorf("GetEndAxis = %q, want %q", got, "B2")
	}
	if got := first.GetCellValue(); got != "top left" {
		t.Errorf("GetCellValue = %q, want the value of the upper-left cell %q", got, "top left")
	}
	second, ok := byStart["D4"]
	if !ok {
		t.Fatalf("no merged range starts at D4: %v", merged)
	}
	if got := second.GetEndAxis(); got != "E5" {
		t.Errorf("GetEndAxis = %q, want %q", got, "E5")
	}

	ws := decodeSheet(t, f)
	if len(ws.MergeCells) != 2 {
		t.Errorf("written worksheet carries %d mergeCell elements, want 2", len(ws.MergeCells))
	}
	if got := f.GetMergeCells("NoSuchSheet"); len(got) != 0 {
		t.Errorf("GetMergeCells on an unknown worksheet returned %v, want nothing", got)
	}
}

func TestWriteToAndWriteToBufferProduceAReadablePackage(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", "saved")

	var sink bytes.Buffer
	n, err := f.WriteTo(&sink)
	if err != nil {
		t.Fatalf("WriteTo: %v", err)
	}
	if n != int64(sink.Len()) {
		t.Errorf("WriteTo reported %d bytes but wrote %d", n, sink.Len())
	}
	if n == 0 {
		t.Fatal("WriteTo reported that it wrote nothing")
	}

	buf, err := f.WriteToBuffer()
	if err != nil {
		t.Fatalf("WriteToBuffer: %v", err)
	}
	if buf.Len() != sink.Len() {
		t.Errorf("WriteToBuffer produced %d bytes, WriteTo produced %d", buf.Len(), sink.Len())
	}

	for name, b := range map[string][]byte{"WriteTo": sink.Bytes(), "WriteToBuffer": buf.Bytes()} {
		g, err := seedcalc.OpenReader(bytes.NewReader(b))
		if err != nil {
			t.Fatalf("OpenReader on the package produced by %s: %v", name, err)
		}
		if got := g.GetCellValue("Sheet1", "A1"); got != "saved" {
			t.Errorf("%s: A1 = %q, want %q", name, got, "saved")
		}
	}
}

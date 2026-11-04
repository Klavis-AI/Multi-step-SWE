// Scored group: file-close-and-zip-reader.
//
// Closing a workbook, reading a ZIP archive into one, and the field the parts
// of an open package now live in. These belong together: a worksheet part
// larger than the XML budget is not held in memory but extracted to a file in
// the system temporary directory, so a workbook now owns operating-system
// resources and needs a way to release them; and the parts it does hold in
// memory have moved out of the plain map milestone 02 exposed into a
// concurrent one. The archive itself is the OPC physical package of ECMA-376
// Part 2 section 7.3 "Mapping to a ZIP file", whose item names this reader
// maps onto the logical part names the rest of the library uses.
package filecloseandzipreader

import (
	"archive/zip"
	"bytes"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// packed returns a two-sheet workbook serialised to bytes.
func packed(t *testing.T) []byte {
	t.Helper()
	f := seedcalc.NewFile()
	f.NewSheet("Second")
	for i := 1; i <= 40; i++ {
		axis, err := seedcalc.CoordinatesToCellName(1, i)
		if err != nil {
			t.Fatalf("CoordinatesToCellName: %v", err)
		}
		if err := f.SetCellValue("Sheet1", axis, i); err != nil {
			t.Fatalf("SetCellValue(%s): %v", axis, err)
		}
	}
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	return buf.Bytes()
}

// bulky returns a workbook large enough that its parts, uncompressed, come to
// well over twenty thousand bytes.
func bulky(t *testing.T) []byte {
	t.Helper()
	f := seedcalc.NewFile()
	f.NewSheet("Second")
	for i := 1; i <= 400; i++ {
		axis, err := seedcalc.CoordinatesToCellName(1, i)
		if err != nil {
			t.Fatalf("CoordinatesToCellName: %v", err)
		}
		if err := f.SetCellValue("Sheet1", axis, i); err != nil {
			t.Fatalf("SetCellValue(%s): %v", axis, err)
		}
	}
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	return buf.Bytes()
}

func TestClosingAWorkbookThatHoldsNothingSucceeds(t *testing.T) {
	// A workbook built in memory never spools a part, so there is nothing to
	// release and Close is a no-op that still reports success.
	if err := seedcalc.NewFile().Close(); err != nil {
		t.Fatalf("Close on a fresh workbook: %v", err)
	}
	g, err := seedcalc.OpenReader(bytes.NewReader(packed(t)))
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	if err := g.Close(); err != nil {
		t.Fatalf("Close on a workbook opened within the default budget: %v", err)
	}
}

func TestASpooledWorksheetIsStillReadableAndIsReleasedByClose(t *testing.T) {
	// An XML budget of one byte forces every part over it out to the system
	// temporary directory rather than into memory.
	g, err := seedcalc.OpenReader(bytes.NewReader(packed(t)), seedcalc.Options{
		UnzipSizeLimit:    1 << 20,
		UnzipXMLSizeLimit: 1,
	})
	if err != nil {
		t.Fatalf("OpenReader with a one byte XML budget: %v", err)
	}
	// Spooling is invisible to the reader: the values come back the same way.
	got, err := g.GetCellValue("Sheet1", "A7")
	if err != nil {
		t.Fatalf("GetCellValue on a spooled worksheet: %v", err)
	}
	if got != "7" {
		t.Fatalf("GetCellValue(A7) = %q, want %q", got, "7")
	}
	rows, err := g.GetRows("Sheet1")
	if err != nil {
		t.Fatalf("GetRows on a spooled worksheet: %v", err)
	}
	if len(rows) != 40 {
		t.Fatalf("GetRows returned %d rows, want 40", len(rows))
	}
	if err := g.Close(); err != nil {
		t.Fatalf("Close on a workbook with spooled parts: %v", err)
	}
}

func TestReadZipReaderIsAMethodOnTheWorkbook(t *testing.T) {
	b := packed(t)
	zr, err := zip.NewReader(bytes.NewReader(b), int64(len(b)))
	if err != nil {
		t.Fatalf("zip.NewReader: %v", err)
	}
	// The reader is a method because it consults the receiver's decompression
	// budgets as it goes; it cannot be a free function.
	f := seedcalc.NewFile()
	parts, worksheets, err := f.ReadZipReader(zr)
	if err != nil {
		t.Fatalf("ReadZipReader: %v", err)
	}
	if worksheets != 2 {
		t.Fatalf("ReadZipReader counted %d worksheets, want 2", worksheets)
	}
	for _, want := range []string{
		"[Content_Types].xml",
		"_rels/.rels",
		"docProps/app.xml",
		"xl/workbook.xml",
		"xl/worksheets/sheet1.xml",
	} {
		if _, ok := parts[want]; !ok {
			t.Fatalf("ReadZipReader did not return the part %s", want)
		}
	}
	if !bytes.Contains(parts["xl/worksheets/sheet1.xml"], []byte(`r="7"`)) {
		t.Fatal("the returned worksheet part does not contain the rows that were written")
	}
}

func TestReadZipReaderHonoursTheReceiversUnzipBudget(t *testing.T) {
	// The receiver is opened under a budget that comfortably admits a bare
	// workbook but not the larger one it is then asked to read.
	const budget = 20000
	bare := func() []byte {
		var buf bytes.Buffer
		if err := seedcalc.NewFile().Write(&buf); err != nil {
			t.Fatalf("Write: %v", err)
		}
		return buf.Bytes()
	}()
	f, err := seedcalc.OpenReader(bytes.NewReader(bare), seedcalc.Options{
		UnzipSizeLimit:    budget,
		UnzipXMLSizeLimit: budget,
	})
	if err != nil {
		t.Fatalf("OpenReader of a bare workbook under a %d byte budget: %v", budget, err)
	}
	b := bulky(t)
	zr, err := zip.NewReader(bytes.NewReader(b), int64(len(b)))
	if err != nil {
		t.Fatalf("zip.NewReader: %v", err)
	}
	// The budget travels with the receiver rather than being a property of the
	// archive, so the same reader that succeeded on a fresh workbook above is
	// refused here.
	if _, _, err := f.ReadZipReader(zr); err == nil {
		t.Fatalf("ReadZipReader ignored the receiver's %d byte unzip budget", budget)
	} else if !bytes.Contains([]byte(err.Error()), []byte("20000")) {
		t.Fatalf("ReadZipReader failed with %v, which does not name the budget that was exceeded", err)
	}
}

// TestCloseIsNotIdempotent records a sharp edge: the workbook removes the files
// it spooled but does not forget that it spooled them, so closing a second time
// tries to remove them again and reports the failure.
func TestCloseIsNotIdempotent(t *testing.T) {
	g, err := seedcalc.OpenReader(bytes.NewReader(packed(t)), seedcalc.Options{
		UnzipSizeLimit:    1 << 20,
		UnzipXMLSizeLimit: 1,
	})
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	if err := g.Close(); err != nil {
		t.Fatalf("the first Close: %v", err)
	}
	if err := g.Close(); err == nil {
		t.Fatal("the second Close reported success; it must fail on the file the first one already removed")
	}
}

func TestThePackagePartsLiveInASyncMap(t *testing.T) {
	// The parts of an open package are reachable through Pkg, which is a
	// sync.Map rather than the plain map of milestone 02: the lookup is Load
	// and the traversal is Range, and both return the part content as bytes.
	f := seedcalc.NewFile()
	names := map[string]int{}
	f.Pkg.Range(func(k, v interface{}) bool {
		name, ok := k.(string)
		if !ok {
			t.Errorf("Pkg holds a key of type %T, want a part name string", k)
			return false
		}
		b, ok := v.([]byte)
		if !ok {
			t.Errorf("Pkg[%s] holds a value of type %T, want the part content as bytes", name, v)
			return false
		}
		names[name] = len(b)
		return true
	})
	for _, want := range []string{
		"[Content_Types].xml",
		"_rels/.rels",
		"docProps/app.xml",
		"docProps/core.xml",
		"xl/_rels/workbook.xml.rels",
		"xl/styles.xml",
		"xl/theme/theme1.xml",
		"xl/workbook.xml",
		"xl/worksheets/sheet1.xml",
	} {
		if _, ok := names[want]; !ok {
			t.Fatalf("a fresh workbook's Pkg does not hold the template part %s; it holds %v", want, names)
		}
	}

	v, ok := f.Pkg.Load("xl/workbook.xml")
	if !ok {
		t.Fatal("Pkg.Load of a part that Range just reported is absent")
	}
	if !bytes.Contains(v.([]byte), []byte("<workbook ")) {
		t.Fatalf("Pkg[xl/workbook.xml] is not the workbook part: %s", v.([]byte))
	}
	if _, ok := f.Pkg.Load("xl/worksheets/sheet99.xml"); ok {
		t.Fatal("Pkg.Load of an absent part reported success")
	}
}

func TestThePartsMapIsWhatWasReadNotWhatIsHeld(t *testing.T) {
	// Pkg is the package as it was read or templated, not a live view of the
	// workbook: an edit to a cell does not reach it.
	f := seedcalc.NewFile()
	before, ok := f.Pkg.Load("xl/worksheets/sheet1.xml")
	if !ok {
		t.Fatal("a fresh workbook's Pkg does not hold xl/worksheets/sheet1.xml")
	}
	if err := f.SetCellValue("Sheet1", "A1", 42); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	after, ok := f.Pkg.Load("xl/worksheets/sheet1.xml")
	if !ok {
		t.Fatal("Pkg lost xl/worksheets/sheet1.xml after a cell was written")
	}
	if !bytes.Equal(before.([]byte), after.([]byte)) {
		t.Fatal("writing a cell changed the bytes in Pkg; that field is the package as read, not a live serialisation")
	}

	// The parsed worksheets are tracked separately, in Sheet, which is the
	// other map that became a sync.Map here. A workbook read from an archive
	// fills it lazily: Pkg holds every part from the moment it is opened, but
	// nothing has been unmarshalled until a worksheet is asked for. The keys
	// there are part paths, not sheet names.
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	g, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	reread, ok := g.Pkg.Load("xl/worksheets/sheet1.xml")
	if !ok {
		t.Fatal("a reopened workbook's Pkg does not hold xl/worksheets/sheet1.xml")
	}
	if !bytes.Contains(reread.([]byte), []byte("<v>42</v>")) {
		t.Fatalf("the reopened worksheet part does not carry the value that was written: %s", reread.([]byte))
	}
	parsed := func() []string {
		var keys []string
		g.Sheet.Range(func(k, v interface{}) bool {
			keys = append(keys, k.(string))
			return true
		})
		return keys
	}
	if keys := parsed(); len(keys) != 0 {
		t.Fatalf("a workbook that has only been opened has already parsed %v; Sheet is filled on first use", keys)
	}
	if _, err := g.GetCellValue("Sheet1", "A1"); err != nil {
		t.Fatalf("GetCellValue: %v", err)
	}
	keys := parsed()
	if len(keys) != 1 || keys[0] != "xl/worksheets/sheet1.xml" {
		t.Fatalf("after one worksheet was read Sheet holds %v, want [xl/worksheets/sheet1.xml]", keys)
	}
}

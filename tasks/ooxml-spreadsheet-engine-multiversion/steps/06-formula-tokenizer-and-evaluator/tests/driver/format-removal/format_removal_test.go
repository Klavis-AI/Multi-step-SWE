// Scored group: format-removal.
//
// Two deletion paths that arrive in this milestone: removing a data validation
// by sqref and removing a conditional-format rule by area. Both are exercised
// through the public interface only, and each removal is confirmed by reading
// the worksheet part back out of the written package -- a call that returns nil
// without deleting anything would otherwise pass. DeleteDataValidation is
// defined in datavalidation.go; UnsetConditionalFormat is defined in styles.go
// at this checkpoint. Behaviour verified against qax-os/excelize tag v2.2.0.
// The stored forms are ECMA-376 Part 1 section 18.3.1.33 "dataValidations"
// over section 18.3.1.32 "dataValidation", and section 18.3.1.18
// "conditionalFormatting" over section 18.3.1.10 "cfRule".
package formatremoval

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"io"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// sheetXML writes f and returns xl/worksheets/sheet1.xml as a string.
func sheetXML(t *testing.T, f *seedcalc.File) string {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	zr, err := zip.NewReader(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	if err != nil {
		t.Fatalf("written package is not a readable ZIP container: %v", err)
	}
	for _, zf := range zr.File {
		if zf.Name != "xl/worksheets/sheet1.xml" {
			continue
		}
		rc, err := zf.Open()
		if err != nil {
			t.Fatalf("open xl/worksheets/sheet1.xml: %v", err)
		}
		raw, err := io.ReadAll(rc)
		rc.Close()
		if err != nil {
			t.Fatalf("read xl/worksheets/sheet1.xml: %v", err)
		}
		return string(raw)
	}
	t.Fatal("written package has no xl/worksheets/sheet1.xml")
	return ""
}

// mustContain and mustNotContain assert on the serialized worksheet.
func mustContain(t *testing.T, ws, needle, what string) {
	t.Helper()
	if !strings.Contains(ws, needle) {
		t.Fatalf("worksheet part does not contain %s (%q)", what, needle)
	}
}

// elementAttrs decodes part and returns the attributes of its first element
// named local, keyed by attribute local name. Attribute order is not
// significant in XML and no document fixes the order this library happens to
// emit, so each attribute is asserted on its own rather than against a
// serialized start tag.
func elementAttrs(t *testing.T, part, local string) map[string]string {
	t.Helper()
	dec := xml.NewDecoder(strings.NewReader(part))
	for {
		tok, err := dec.Token()
		if err == io.EOF {
			break
		}
		if err != nil {
			t.Fatalf("worksheet part is not well-formed XML: %v", err)
		}
		se, ok := tok.(xml.StartElement)
		if !ok || se.Name.Local != local {
			continue
		}
		attrs := map[string]string{}
		for _, a := range se.Attr {
			attrs[a.Name.Local] = a.Value
		}
		return attrs
	}
	t.Fatalf("worksheet part has no <%s> element", local)
	return nil
}

func mustNotContain(t *testing.T, ws, needle, what string) {
	t.Helper()
	if strings.Contains(ws, needle) {
		t.Fatalf("worksheet part still contains %s (%q) after it was removed", what, needle)
	}
}

// addValidation attaches a simple presence-required validation to sheet/sqref.
func addValidation(t *testing.T, f *seedcalc.File, sheet, sqref string) {
	t.Helper()
	dv := seedcalc.NewDataValidation(true)
	dv.Sqref = sqref
	if err := f.AddDataValidation(sheet, dv); err != nil {
		t.Fatalf("AddDataValidation(%s, %s): %v", sheet, sqref, err)
	}
}

func TestDeleteDataValidationRemovesAnExistingEntry(t *testing.T) {
	f := seedcalc.NewFile()
	addValidation(t, f, "Sheet1", "A1:A5")
	mustContain(t, sheetXML(t, f), `sqref="A1:A5"`, "the validation it was asked to delete")
	if err := f.DeleteDataValidation("Sheet1", "A1:A5"); err != nil {
		t.Fatalf("DeleteDataValidation(present): %v", err)
	}
	// The entry is gone from the part, and the empty parent goes with it.
	ws := sheetXML(t, f)
	mustNotContain(t, ws, "dataValidation", "the data validation")
}

func TestDeleteDataValidationOnAnAbsentSqrefIsANoOp(t *testing.T) {
	f := seedcalc.NewFile()
	addValidation(t, f, "Sheet1", "A1:A5")
	// Deleting a range that was never registered must succeed silently, and
	// must not take the registered one with it.
	if err := f.DeleteDataValidation("Sheet1", "Z1:Z5"); err != nil {
		t.Fatalf("DeleteDataValidation(absent sqref): %v", err)
	}
	ws := sheetXML(t, f)
	mustContain(t, ws, "<dataValidations", "the dataValidations collection")
	mustContain(t, ws, `sqref="A1:A5"`, "the untouched validation")
}

func TestDeleteDataValidationRejectsAnAbsentSheet(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.DeleteDataValidation("NoSuchSheet", "A1:A5"); err == nil {
		t.Fatal("DeleteDataValidation on an absent sheet returned no error")
	}
}

func TestUnsetConditionalFormatRemovesAnExistingRule(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetConditionalFormat("Sheet1", "A1:A5",
		`[{"type":"cell","criteria":">","format":1,"value":"10"}]`); err != nil {
		t.Fatalf("SetConditionalFormat: %v", err)
	}
	mustContain(t, sheetXML(t, f), "<conditionalFormatting", "the rule it was asked to remove")
	if err := f.UnsetConditionalFormat("Sheet1", "A1:A5"); err != nil {
		t.Fatalf("UnsetConditionalFormat(present): %v", err)
	}
	// Both the collection element and the rule inside it are gone.
	ws := sheetXML(t, f)
	mustNotContain(t, ws, "conditionalFormatting", "the conditional format")
	mustNotContain(t, ws, "cfRule", "the conditional-format rule")
}

func TestUnsetConditionalFormatOnAnAbsentAreaIsANoOp(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetConditionalFormat("Sheet1", "A1:A5",
		`[{"type":"cell","criteria":">","format":1,"value":"10"}]`); err != nil {
		t.Fatalf("SetConditionalFormat: %v", err)
	}
	// Unsetting a range that has no conditional format must succeed silently,
	// and must leave the rule that does exist in place.
	if err := f.UnsetConditionalFormat("Sheet1", "B1:B5"); err != nil {
		t.Fatalf("UnsetConditionalFormat(absent area): %v", err)
	}
	ws := sheetXML(t, f)
	if got := elementAttrs(t, ws, "conditionalFormatting")["sqref"]; got != "A1:A5" {
		t.Fatalf("the untouched rule covers sqref %q, want %q", got, "A1:A5")
	}
	mustContain(t, ws, "<cfRule", "the rule body")
}

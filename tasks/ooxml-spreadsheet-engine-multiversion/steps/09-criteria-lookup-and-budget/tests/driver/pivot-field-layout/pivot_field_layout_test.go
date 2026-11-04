// Scored group: pivot-field-layout.
//
// PivotTableField gains two boolean members, Compact and Outline, which
// control how a pivot field is laid out in the rendered report. They are
// written straight through to the pivotField element as its @compact and
// @outline attributes, and only for the fields placed on an axis: a data
// field is serialised without either. The element and its two attributes are
// ECMA-376 Part 1 section 18.10.1.69 "pivotField (PivotTable Field)". The
// rest of the struct is unchanged from milestone 06, and is checked here
// alongside so that the two new members can be read as an addition rather
// than a replacement.
package pivotfieldlayout

import (
	"archive/zip"
	"bytes"
	"io"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// pivotBook builds a workbook holding a small three-column source block, laid
// out so that Region and Type are candidate axis fields and Sales is the
// candidate data field.
func pivotBook(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	if err := f.SetSheetRow("Sheet1", "A1", &[]interface{}{"Region", "Type", "Sales"}); err != nil {
		t.Fatalf("SetSheetRow: %v", err)
	}
	for i, row := range [][]interface{}{
		{"North", "Apple", 10},
		{"South", "Apple", 20},
		{"North", "Pear", 30},
		{"South", "Pear", 40},
	} {
		axis, err := seedcalc.CoordinatesToCellName(1, i+2)
		if err != nil {
			t.Fatalf("CoordinatesToCellName: %v", err)
		}
		if err := f.SetSheetRow("Sheet1", axis, &row); err != nil {
			t.Fatalf("SetSheetRow(%s): %v", axis, err)
		}
	}
	return f
}

// pivotPart adds a pivot table over the block pivotBook built and returns the
// xl/pivotTables/pivotTable1.xml part of the written package.
func pivotPart(t *testing.T, rows, cols, data []seedcalc.PivotTableField) string {
	t.Helper()
	f := pivotBook(t)
	if err := f.AddPivotTable(&seedcalc.PivotTableOption{
		DataRange:       "Sheet1!$A$1:$C$5",
		PivotTableRange: "Sheet1!$F$2:$K$10",
		Rows:            rows,
		Columns:         cols,
		Data:            data,
	}); err != nil {
		t.Fatalf("AddPivotTable: %v", err)
	}
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	zr, err := zip.NewReader(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	if err != nil {
		t.Fatalf("written package is not a ZIP container: %v", err)
	}
	const name = "xl/pivotTables/pivotTable1.xml"
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
		return string(b)
	}
	t.Fatalf("written package has no part %s", name)
	return ""
}

// pivotFields returns the <pivotFields> element of the pivot table part.
func pivotFields(t *testing.T, part string) string {
	t.Helper()
	i := strings.Index(part, "<pivotFields")
	j := strings.Index(part, "</pivotFields>")
	if i < 0 || j < i {
		t.Fatalf("pivot table part carries no pivotFields element:\n%s", part)
	}
	return part[i : j+len("</pivotFields>")]
}

func TestTheLayoutFlagsAreWrittenThroughToEachAxisField(t *testing.T) {
	// A zero-valued PivotTableField is the false/false case: both attributes
	// are still emitted, spelt out rather than omitted.
	got := pivotFields(t, pivotPart(t,
		[]seedcalc.PivotTableField{{Data: "Region"}},
		[]seedcalc.PivotTableField{{Data: "Type"}},
		[]seedcalc.PivotTableField{{Data: "Sales"}}))
	for _, want := range []string{
		`<pivotField axis="axisRow" compact="false" outline="false"`,
		`<pivotField axis="axisCol" compact="false" outline="false"`,
	} {
		if !strings.Contains(got, want) {
			t.Fatalf("pivotFields for two zero-valued axis fields = %s, which does not contain %s", got, want)
		}
	}

	// Setting both members flips both attributes, independently of each other.
	got = pivotFields(t, pivotPart(t,
		[]seedcalc.PivotTableField{{Data: "Region", Compact: true, Outline: true}},
		[]seedcalc.PivotTableField{{Data: "Type", Compact: true}},
		[]seedcalc.PivotTableField{{Data: "Sales"}}))
	for _, want := range []string{
		`<pivotField axis="axisRow" compact="true" outline="true"`,
		`<pivotField axis="axisCol" compact="true" outline="false"`,
	} {
		if !strings.Contains(got, want) {
			t.Fatalf("pivotFields for a compact/outline pair = %s, which does not contain %s", got, want)
		}
	}

	// Outline alone is the remaining combination.
	got = pivotFields(t, pivotPart(t,
		[]seedcalc.PivotTableField{{Data: "Region", Outline: true}},
		nil,
		[]seedcalc.PivotTableField{{Data: "Sales"}}))
	if want := `<pivotField axis="axisRow" compact="false" outline="true"`; !strings.Contains(got, want) {
		t.Fatalf("pivotFields for an outline-only row field = %s, which does not contain %s", got, want)
	}
}

func TestADataFieldIsSerialisedWithoutEitherFlag(t *testing.T) {
	// The two members exist on every PivotTableField, but a field placed in
	// Data is written as a bare dataField marker: setting them there changes
	// nothing.
	plain := pivotFields(t, pivotPart(t,
		[]seedcalc.PivotTableField{{Data: "Region"}},
		nil,
		[]seedcalc.PivotTableField{{Data: "Sales"}}))
	flagged := pivotFields(t, pivotPart(t,
		[]seedcalc.PivotTableField{{Data: "Region"}},
		nil,
		[]seedcalc.PivotTableField{{Data: "Sales", Compact: true, Outline: true}}))
	if plain != flagged {
		t.Fatalf("the layout flags changed the serialised data field:\n plain = %s\nflagged = %s", plain, flagged)
	}
	if want := `<pivotField dataField="true" showAll="false">`; !strings.Contains(plain, want) {
		t.Fatalf("pivotFields = %s, which does not contain the bare data field %s", plain, want)
	}
}

func TestTheRestOfThePivotFieldStructIsUnchanged(t *testing.T) {
	part := pivotPart(t,
		[]seedcalc.PivotTableField{{Data: "Region", Name: "Where", DefaultSubtotal: true, Compact: true}},
		nil,
		[]seedcalc.PivotTableField{{Data: "Sales", Name: "Total", Subtotal: "Sum"}})
	fields := pivotFields(t, part)
	// Name renames the field in place; DefaultSubtotal is carried on the same
	// element as the two new members, and the axis field named by Data keeps
	// its items list.
	if want := `<pivotField name="Where" axis="axisRow" compact="true" outline="false" showAll="false" defaultSubtotal="true">`; !strings.Contains(fields, want) {
		t.Fatalf("pivotFields = %s, which does not contain %s", fields, want)
	}
	if !strings.Contains(fields, `<items count="1"><item t="default"></item></items>`) {
		t.Fatalf("a row field with DefaultSubtotal set lost its items list: %s", fields)
	}
	// Subtotal and Name on a data field still reach the dataFields element,
	// with the subtotal name lower-cased.
	if want := `<dataField name="Total" fld="2" subtotal="sum">`; !strings.Contains(part, want) {
		t.Fatalf("pivot table part does not contain the data field %s", want)
	}
}

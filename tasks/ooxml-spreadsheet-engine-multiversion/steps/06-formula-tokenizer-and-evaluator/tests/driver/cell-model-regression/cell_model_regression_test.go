// Gate group: cell-model-regression.
//
// Inherited behaviour that the arrival of an evaluator is most likely to
// disturb: the coordinate helpers, the typed cell setters and the string form
// every cell value is read back as. None of this is new in this milestone; it
// must keep working unchanged now that cells can also carry a computed result.
// The A1 reference grammar is ECMA-376 Part 1 section 18.17.2.3.1 "Cell
// References"; the cell itself is section 18.3.1.4 "c (Cell)" and its type
// attribute is section 18.18.11 "ST_CellType".
package cellmodelregression

import (
	"bytes"
	"testing"
	"time"

	seedcalc "__SEEDCALC_MODULE__"
)

func TestCellNameAndCoordinatesRoundTrip(t *testing.T) {
	for _, tc := range []struct {
		name string
		col  int
		row  int
	}{
		{"A1", 1, 1},
		{"B3", 2, 3},
		{"Z1", 26, 1},
		{"AA1", 27, 1},
		{"XFD1048576", 16384, 1048576},
	} {
		col, row, err := seedcalc.CellNameToCoordinates(tc.name)
		if err != nil {
			t.Fatalf("CellNameToCoordinates(%q): %v", tc.name, err)
		}
		if col != tc.col || row != tc.row {
			t.Fatalf("CellNameToCoordinates(%q) = (%d, %d), want (%d, %d)", tc.name, col, row, tc.col, tc.row)
		}
		back, err := seedcalc.CoordinatesToCellName(tc.col, tc.row)
		if err != nil {
			t.Fatalf("CoordinatesToCellName(%d, %d): %v", tc.col, tc.row, err)
		}
		if back != tc.name {
			t.Fatalf("CoordinatesToCellName(%d, %d) = %q, want %q", tc.col, tc.row, back, tc.name)
		}
	}
	for _, bad := range []string{"", "A", "1", "A0", "1A", "$A$1"} {
		if _, _, err := seedcalc.CellNameToCoordinates(bad); err == nil {
			t.Fatalf("CellNameToCoordinates(%q) returned no error", bad)
		}
	}
	for _, bad := range [][2]int{{0, 1}, {1, 0}, {-1, 1}, {1, -1}} {
		if _, err := seedcalc.CoordinatesToCellName(bad[0], bad[1]); err == nil {
			t.Fatalf("CoordinatesToCellName(%d, %d) returned no error", bad[0], bad[1])
		}
	}
}

func TestColumnNameAndNumberRoundTrip(t *testing.T) {
	for _, tc := range []struct {
		name string
		num  int
	}{
		{"A", 1}, {"Z", 26}, {"AA", 27}, {"AZ", 52}, {"BA", 53}, {"XFD", 16384},
	} {
		n, err := seedcalc.ColumnNameToNumber(tc.name)
		if err != nil {
			t.Fatalf("ColumnNameToNumber(%q): %v", tc.name, err)
		}
		if n != tc.num {
			t.Fatalf("ColumnNameToNumber(%q) = %d, want %d", tc.name, n, tc.num)
		}
		back, err := seedcalc.ColumnNumberToName(tc.num)
		if err != nil {
			t.Fatalf("ColumnNumberToName(%d): %v", tc.num, err)
		}
		if back != tc.name {
			t.Fatalf("ColumnNumberToName(%d) = %q, want %q", tc.num, back, tc.name)
		}
	}
	if _, err := seedcalc.ColumnNameToNumber(""); err == nil {
		t.Fatal("ColumnNameToNumber(\"\") returned no error")
	}
	if _, err := seedcalc.ColumnNumberToName(0); err == nil {
		t.Fatal("ColumnNumberToName(0) returned no error")
	}
}

func TestSplitAndJoinCellName(t *testing.T) {
	col, row, err := seedcalc.SplitCellName("AB12")
	if err != nil {
		t.Fatalf("SplitCellName: %v", err)
	}
	if col != "AB" || row != 12 {
		t.Fatalf("SplitCellName(AB12) = (%q, %d), want (AB, 12)", col, row)
	}
	joined, err := seedcalc.JoinCellName("AB", 12)
	if err != nil {
		t.Fatalf("JoinCellName: %v", err)
	}
	if joined != "AB12" {
		t.Fatalf("JoinCellName(AB, 12) = %q, want %q", joined, "AB12")
	}
	if _, _, err := seedcalc.SplitCellName("12AB"); err == nil {
		t.Fatal("SplitCellName(12AB) returned no error")
	}
	if _, err := seedcalc.JoinCellName("AB", 0); err == nil {
		t.Fatal("JoinCellName(AB, 0) returned no error")
	}
}

func TestTypedCellSettersReadBackAsStrings(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellInt("Sheet1", "A1", -7); err != nil {
		t.Fatalf("SetCellInt: %v", err)
	}
	if err := f.SetCellFloat("Sheet1", "A2", 3.14159, 2, 64); err != nil {
		t.Fatalf("SetCellFloat: %v", err)
	}
	if err := f.SetCellBool("Sheet1", "A3", true); err != nil {
		t.Fatalf("SetCellBool: %v", err)
	}
	if err := f.SetCellStr("Sheet1", "A4", "hello"); err != nil {
		t.Fatalf("SetCellStr: %v", err)
	}
	if err := f.SetCellDefault("Sheet1", "A5", "123"); err != nil {
		t.Fatalf("SetCellDefault: %v", err)
	}
	for axis, want := range map[string]string{
		// SetCellBool stores type "b" with values "1"/"0"; GetCellValue returns the raw stored literal.
		"A1": "-7", "A2": "3.14", "A3": "1", "A4": "hello", "A5": "123",
	} {
		got, err := f.GetCellValue("Sheet1", axis)
		if err != nil {
			t.Fatalf("GetCellValue(%s): %v", axis, err)
		}
		if got != want {
			t.Fatalf("GetCellValue(%s) = %q, want %q", axis, got, want)
		}
	}
}

func TestSetCellValueAcceptsTheGoTypesItDocuments(t *testing.T) {
	f := seedcalc.NewFile()
	when := time.Date(2020, time.May, 11, 0, 0, 0, 0, time.UTC)
	for _, c := range []struct {
		axis string
		v    interface{}
	}{
		{"A1", 42},
		{"A2", int64(-42)},
		{"A3", 1.5},
		{"A4", "text"},
		{"A5", false},
		{"A6", when},
		{"A7", nil},
	} {
		if err := f.SetCellValue("Sheet1", c.axis, c.v); err != nil {
			t.Fatalf("SetCellValue(%s, %v): %v", c.axis, c.v, err)
		}
	}
	for axis, want := range map[string]string{
		// SetCellValue(bool) stores the raw "1"/"0" literal, same as SetCellBool.
		"A1": "42", "A2": "-42", "A3": "1.5", "A4": "text", "A5": "0", "A7": "",
	} {
		got, err := f.GetCellValue("Sheet1", axis)
		if err != nil {
			t.Fatalf("GetCellValue(%s): %v", axis, err)
		}
		if got != want {
			t.Fatalf("GetCellValue(%s) = %q, want %q", axis, got, want)
		}
	}
	got, err := f.GetCellValue("Sheet1", "A6")
	if err != nil {
		t.Fatalf("GetCellValue(A6): %v", err)
	}
	if got == "" {
		t.Fatal("GetCellValue(A6) is empty; a time value must be stored as a formatted date")
	}
}

func TestReadingAnUnsetOrUnaddressableCell(t *testing.T) {
	f := seedcalc.NewFile()
	got, err := f.GetCellValue("Sheet1", "Q17")
	if err != nil {
		t.Fatalf("GetCellValue on an unset cell: %v", err)
	}
	if got != "" {
		t.Fatalf("GetCellValue on an unset cell = %q, want the empty string", got)
	}
	if _, err := f.GetCellValue("Sheet1", "ZZZ"); err == nil {
		t.Fatal("GetCellValue at a malformed reference returned no error")
	}
	if _, err := f.GetCellValue("NoSuchSheet", "A1"); err == nil {
		t.Fatal("GetCellValue on an absent sheet returned no error")
	}
	if err := f.SetCellValue("NoSuchSheet", "A1", 1); err == nil {
		t.Fatal("SetCellValue on an absent sheet returned no error")
	}
}

func TestTheGridIsReadBackRowByRow(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetSheetRow("Sheet1", "A1", &[]interface{}{"a", "b", "c"}); err != nil {
		t.Fatalf("SetSheetRow: %v", err)
	}
	if err := f.SetSheetRow("Sheet1", "A2", &[]interface{}{1, 2, 3}); err != nil {
		t.Fatalf("SetSheetRow: %v", err)
	}
	rows, err := f.GetRows("Sheet1")
	if err != nil {
		t.Fatalf("GetRows: %v", err)
	}
	if len(rows) != 2 {
		t.Fatalf("GetRows returned %d rows, want 2", len(rows))
	}
	if len(rows[0]) != 3 || rows[0][0] != "a" || rows[0][2] != "c" {
		t.Fatalf("first row = %v, want [a b c]", rows[0])
	}
	if len(rows[1]) != 3 || rows[1][0] != "1" || rows[1][2] != "3" {
		t.Fatalf("second row = %v, want [1 2 3]", rows[1])
	}
	// The same grid survives a write and reopen.
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	g, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	again, err := g.GetRows("Sheet1")
	if err != nil {
		t.Fatalf("GetRows after round trip: %v", err)
	}
	if len(again) != 2 || again[0][1] != "b" || again[1][1] != "2" {
		t.Fatalf("grid after round trip = %v, want [[a b c] [1 2 3]]", again)
	}
}

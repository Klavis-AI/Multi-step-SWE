// Gate group: cell-model-regression.
//
// Inherited cell-model behaviour: coordinate helpers, typed setters, GetRows,
// and the write/re-open round trip. This behaviour was first established at
// milestone 01 and must survive the new API surface of this checkpoint. The
// one contract that does move -- what the coordinate helpers accept as a cell
// name -- is scored in inherited-contract-migrations, not asserted here.
package cellmodelregression

import (
	"bytes"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func TestCellNameAndCoordinatesRoundTrip(t *testing.T) {
	for _, tc := range []struct {
		name string
		col  int
		row  int
	}{
		{"A1", 1, 1},
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
}

func TestTypedCellSettersAndReadBack(t *testing.T) {
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
	for axis, want := range map[string]string{
		"A1": "-7", "A2": "3.14", "A3": "1", "A4": "hello",
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

func TestGetRowsReadBackRowByRow(t *testing.T) {
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
	if rows[0][0] != "a" || rows[1][2] != "3" {
		t.Fatalf("unexpected rows: %v", rows)
	}
}

func TestGridSurvivesRoundTrip(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetSheetRow("Sheet1", "A1", &[]interface{}{"x", "y"}); err != nil {
		t.Fatalf("SetSheetRow: %v", err)
	}
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	g, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	rows, err := g.GetRows("Sheet1")
	if err != nil {
		t.Fatalf("GetRows after round trip: %v", err)
	}
	if len(rows) < 1 || len(rows[0]) < 2 || rows[0][0] != "x" || rows[0][1] != "y" {
		t.Fatalf("round trip grid = %v, want [[x y]]", rows)
	}
}

func TestGetCellValueOnAbsentSheetErrors(t *testing.T) {
	f := seedcalc.NewFile()
	if _, err := f.GetCellValue("NoSuch", "A1"); err == nil {
		t.Fatal("GetCellValue on absent sheet returned no error")
	}
}

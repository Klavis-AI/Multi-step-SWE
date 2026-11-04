// Scored group: cell-name-conversion (milestone 03, "Workbook metadata and
// the error contract").
//
// Normative sources:
//   * TEST_INTERFACE.md section 4.03 -- "Cell and column name conversion".
//   * ECMA-376 Part 1, 18.17.2.4 "Cell References" (A1 reference style:
//     one or more letters naming the column, followed by a 1-based row
//     number).
//   * Cases cross-checked against the upstream test corpus at tag v2.0.1
//     (lib_test.go: TestColumnNameToNumber, TestColumnNumberToName,
//     TestSplitCellName, TestJoinCellName, TestCellNameToCoordinates,
//     TestCoordinatesToCellName).
package cellnameconversion

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func TestSplitCellName(t *testing.T) {
	valid := map[string]struct {
		col string
		row int
	}{
		"A1":      {"A", 1},
		"Z9":      {"Z", 9},
		"AB42":    {"AB", 42},
		"ZZZ1048": {"ZZZ", 1048},
		"a1":      {"a", 1},
	}
	for cell, want := range valid {
		col, row, err := seedcalc.SplitCellName(cell)
		if err != nil {
			t.Errorf("SplitCellName(%q) returned %v, want nil", cell, err)
			continue
		}
		if col != want.col || row != want.row {
			t.Errorf("SplitCellName(%q) = (%q, %d), want (%q, %d)", cell, col, row, want.col, want.row)
		}
	}

	for _, cell := range []string{"", "A", "1", "1A", "A0", "A-1", "-A1", " A1", "$A$1", "A1.5"} {
		col, row, err := seedcalc.SplitCellName(cell)
		if err == nil {
			t.Errorf("SplitCellName(%q) returned nil error, want a non-nil error", cell)
		}
		if col != "" || row != -1 {
			t.Errorf("SplitCellName(%q) = (%q, %d) on failure, want (\"\", -1)", cell, col, row)
		}
	}
}

func TestJoinCellName(t *testing.T) {
	cases := []struct {
		col  string
		row  int
		want string
	}{
		{"A", 1, "A1"},
		{"a", 1, "A1"},
		{"AB", 42, "AB42"},
		{"zz", 7, "ZZ7"},
	}
	for _, c := range cases {
		got, err := seedcalc.JoinCellName(c.col, c.row)
		if err != nil {
			t.Errorf("JoinCellName(%q, %d) returned %v, want nil", c.col, c.row, err)
			continue
		}
		if got != c.want {
			t.Errorf("JoinCellName(%q, %d) = %q, want %q", c.col, c.row, got, c.want)
		}
	}

	bad := []struct {
		col string
		row int
	}{
		{"", 1}, {"A1", 1}, {"1", 1}, {" A", 1}, {"A ", 1},
		{"A", 0}, {"A", -1},
	}
	for _, c := range bad {
		got, err := seedcalc.JoinCellName(c.col, c.row)
		if err == nil {
			t.Errorf("JoinCellName(%q, %d) returned nil error, want a non-nil error", c.col, c.row)
		}
		if got != "" {
			t.Errorf("JoinCellName(%q, %d) = %q on failure, want \"\"", c.col, c.row, got)
		}
	}
}

func TestColumnNameToNumber(t *testing.T) {
	valid := map[string]int{
		"A": 1, "Z": 26, "AA": 27, "AB": 28, "AD": 30, "AK": 37, "AZ": 52,
		"BA": 53, "z": 26, "aa": 27, "Column": 42724228,
	}
	for name, want := range valid {
		got, err := seedcalc.ColumnNameToNumber(name)
		if err != nil {
			t.Errorf("ColumnNameToNumber(%q) returned %v, want nil", name, err)
			continue
		}
		if got != want {
			t.Errorf("ColumnNameToNumber(%q) = %d, want %d", name, got, want)
		}
	}

	for _, name := range []string{"", "3", "A1", "AZ90", " A", "A ", "A1A", "-"} {
		got, err := seedcalc.ColumnNameToNumber(name)
		if err == nil {
			t.Errorf("ColumnNameToNumber(%q) returned nil error, want a non-nil error", name)
		}
		if got != -1 {
			t.Errorf("ColumnNameToNumber(%q) = %d on failure, want -1", name, got)
		}
	}
}

func TestColumnNumberToName(t *testing.T) {
	valid := map[int]string{1: "A", 26: "Z", 27: "AA", 28: "AB", 37: "AK", 52: "AZ", 53: "BA", 16384: "XFD"}
	for num, want := range valid {
		got, err := seedcalc.ColumnNumberToName(num)
		if err != nil {
			t.Errorf("ColumnNumberToName(%d) returned %v, want nil", num, err)
			continue
		}
		if got != want {
			t.Errorf("ColumnNumberToName(%d) = %q, want %q", num, got, want)
		}
	}

	for _, num := range []int{0, -1, -26} {
		got, err := seedcalc.ColumnNumberToName(num)
		if err == nil {
			t.Errorf("ColumnNumberToName(%d) returned nil error, want a non-nil error", num)
		}
		if got != "" {
			t.Errorf("ColumnNumberToName(%d) = %q on failure, want \"\"", num, got)
		}
	}

	// Round trip through both directions.
	for num := 1; num <= 200; num++ {
		name, err := seedcalc.ColumnNumberToName(num)
		if err != nil {
			t.Fatalf("ColumnNumberToName(%d): %v", num, err)
		}
		back, err := seedcalc.ColumnNameToNumber(name)
		if err != nil {
			t.Fatalf("ColumnNameToNumber(%q): %v", name, err)
		}
		if back != num {
			t.Fatalf("round trip %d -> %q -> %d", num, name, back)
		}
	}
}

func TestCellNameToCoordinates(t *testing.T) {
	valid := map[string][2]int{
		"A1":   {1, 1},
		"Z3":   {26, 3},
		"AA10": {27, 10},
		"XFD1": {16384, 1},
	}
	for cell, want := range valid {
		col, row, err := seedcalc.CellNameToCoordinates(cell)
		if err != nil {
			t.Errorf("CellNameToCoordinates(%q) returned %v, want nil", cell, err)
			continue
		}
		if col != want[0] || row != want[1] {
			t.Errorf("CellNameToCoordinates(%q) = (%d, %d), want (%d, %d)", cell, col, row, want[0], want[1])
		}
	}

	for _, cell := range []string{"", "A", "1", "1A", "A0", "A-1", "*", "A1:B2", "$A$1"} {
		col, row, err := seedcalc.CellNameToCoordinates(cell)
		if err == nil {
			t.Errorf("CellNameToCoordinates(%q) returned nil error, want a non-nil error", cell)
		}
		if col != -1 || row != -1 {
			t.Errorf("CellNameToCoordinates(%q) = (%d, %d) on failure, want (-1, -1)", cell, col, row)
		}
	}
}

func TestCoordinatesToCellName(t *testing.T) {
	cases := []struct {
		col, row int
		want     string
	}{
		{1, 1, "A1"},
		{26, 3, "Z3"},
		{27, 10, "AA10"},
		{16384, 1048576, "XFD1048576"},
	}
	for _, c := range cases {
		got, err := seedcalc.CoordinatesToCellName(c.col, c.row)
		if err != nil {
			t.Errorf("CoordinatesToCellName(%d, %d) returned %v, want nil", c.col, c.row, err)
			continue
		}
		if got != c.want {
			t.Errorf("CoordinatesToCellName(%d, %d) = %q, want %q", c.col, c.row, got, c.want)
		}
	}

	for _, c := range [][2]int{{0, 0}, {0, 1}, {1, 0}, {-1, -1}, {-1, 1}, {1, -1}} {
		got, err := seedcalc.CoordinatesToCellName(c[0], c[1])
		if err == nil {
			t.Errorf("CoordinatesToCellName(%d, %d) returned nil error, want a non-nil error", c[0], c[1])
		}
		if got != "" {
			t.Errorf("CoordinatesToCellName(%d, %d) = %q on failure, want \"\"", c[0], c[1], got)
		}
	}

	// Round trip against CellNameToCoordinates.
	for _, cell := range []string{"A1", "B2", "AA100", "XFD1048576"} {
		col, row, err := seedcalc.CellNameToCoordinates(cell)
		if err != nil {
			t.Fatalf("CellNameToCoordinates(%q): %v", cell, err)
		}
		back, err := seedcalc.CoordinatesToCellName(col, row)
		if err != nil {
			t.Fatalf("CoordinatesToCellName(%d, %d): %v", col, row, err)
		}
		if back != cell {
			t.Fatalf("round trip %q -> (%d, %d) -> %q", cell, col, row, back)
		}
	}
}

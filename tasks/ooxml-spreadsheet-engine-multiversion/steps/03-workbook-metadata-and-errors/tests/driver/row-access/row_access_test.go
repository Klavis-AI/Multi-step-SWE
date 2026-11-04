// Scored group: row-access (milestone 03, the error-return migration).
//
// The streaming row iterator and the bulk row reader established by earlier
// milestones must keep working now that Rows.Columns returns an error and the
// row-addressed mutators are 1-based. Normative sources: TEST_INTERFACE.md
// sections 4.02 and 4.03; ECMA-376 Part 1, 18.3.1.80 sheetData.
package rowaccess

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func seedGrid(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	grid := [][]interface{}{
		{"a1", "b1", "c1"},
		{"a2", nil, "c2"},
		{"a3", "b3", "c3"},
	}
	for i := range grid {
		cell, err := seedcalc.CoordinatesToCellName(1, i+1)
		if err != nil {
			t.Fatalf("CoordinatesToCellName: %v", err)
		}
		if err := f.SetSheetRow("Sheet1", cell, &grid[i]); err != nil {
			t.Fatalf("SetSheetRow: %v", err)
		}
	}
	return f
}

func TestRowsIteratorYieldsEachRow(t *testing.T) {
	f := seedGrid(t)
	rows, err := f.Rows("Sheet1")
	if err != nil {
		t.Fatalf("Rows: %v", err)
	}
	var seen [][]string
	for rows.Next() {
		cols, err := rows.Columns()
		if err != nil {
			t.Fatalf("Columns: %v", err)
		}
		seen = append(seen, cols)
	}
	if err := rows.Error(); err != nil {
		t.Fatalf("Rows.Error: %v", err)
	}
	if len(seen) != 3 {
		t.Fatalf("the iterator produced %d rows, want 3", len(seen))
	}
	want := [][]string{{"a1", "b1", "c1"}, {"a2", "", "c2"}, {"a3", "b3", "c3"}}
	for i := range want {
		if len(seen[i]) != len(want[i]) {
			t.Fatalf("row %d = %q, want %q", i+1, seen[i], want[i])
		}
		for j := range want[i] {
			if seen[i][j] != want[i][j] {
				t.Fatalf("row %d = %q, want %q", i+1, seen[i], want[i])
			}
		}
	}
}

func TestRowsColumnsMatchesGetRows(t *testing.T) {
	f := seedGrid(t)
	bulk, err := f.GetRows("Sheet1")
	if err != nil {
		t.Fatalf("GetRows: %v", err)
	}
	rows, err := f.Rows("Sheet1")
	if err != nil {
		t.Fatalf("Rows: %v", err)
	}
	i := 0
	for rows.Next() {
		cols, err := rows.Columns()
		if err != nil {
			t.Fatalf("Columns: %v", err)
		}
		if i >= len(bulk) {
			t.Fatalf("the iterator produced more rows than GetRows (%d)", len(bulk))
		}
		if len(cols) != len(bulk[i]) {
			t.Fatalf("row %d: iterator gave %q, GetRows gave %q", i+1, cols, bulk[i])
		}
		for j := range cols {
			if cols[j] != bulk[i][j] {
				t.Fatalf("row %d: iterator gave %q, GetRows gave %q", i+1, cols, bulk[i])
			}
		}
		i++
	}
	if i != len(bulk) {
		t.Errorf("the iterator produced %d rows, GetRows produced %d", i, len(bulk))
	}
}

func TestRowsIteratorOnUnknownWorksheet(t *testing.T) {
	f := seedGrid(t)
	rows, err := f.Rows("NoSuchSheet")
	if err == nil {
		t.Fatal("Rows on an unknown worksheet returned a nil error")
	}
	if rows != nil {
		t.Error("Rows on an unknown worksheet returned a non-nil iterator")
	}
}

func TestGetRowsOnAnEmptyWorksheet(t *testing.T) {
	f := seedcalc.NewFile()
	rows, err := f.GetRows("Sheet1")
	if err != nil {
		t.Fatalf("GetRows: %v", err)
	}
	if len(rows) != 0 {
		t.Errorf("GetRows on an empty worksheet = %v, want no rows", rows)
	}
	// An unknown worksheet yields no rows.
	if rows, err := f.GetRows("NoSuchSheet"); err != nil || len(rows) != 0 {
		t.Errorf("GetRows on an unknown worksheet = (%v, %v), want (no rows, nil)", rows, err)
	}
	// The iterator over an empty worksheet simply produces nothing.
	it, err := f.Rows("Sheet1")
	if err != nil {
		t.Fatalf("Rows: %v", err)
	}
	n := 0
	for it.Next() {
		n++
	}
	if n != 0 {
		t.Errorf("the iterator produced %d rows on an empty worksheet, want 0", n)
	}
}

func TestGetRowsAfterInsertAndRemove(t *testing.T) {
	f := seedGrid(t)
	if err := f.InsertRow("Sheet1", 2); err != nil {
		t.Fatalf("InsertRow(2): %v", err)
	}
	rows, err := f.GetRows("Sheet1")
	if err != nil {
		t.Fatalf("GetRows: %v", err)
	}
	if len(rows) != 4 {
		t.Fatalf("after InsertRow(2) there are %d rows, want 4", len(rows))
	}
	if rows[0][0] != "a1" || rows[1][0] != "" || rows[2][0] != "a2" {
		t.Errorf("after InsertRow(2) column A = %q / %q / %q, want a1 / (empty) / a2",
			rows[0][0], rows[1][0], rows[2][0])
	}

	if err := f.RemoveRow("Sheet1", 2); err != nil {
		t.Fatalf("RemoveRow(2): %v", err)
	}
	rows, err = f.GetRows("Sheet1")
	if err != nil {
		t.Fatalf("GetRows: %v", err)
	}
	if len(rows) != 3 {
		t.Fatalf("after RemoveRow(2) there are %d rows, want 3", len(rows))
	}
	if rows[0][0] != "a1" || rows[1][0] != "a2" || rows[2][0] != "a3" {
		t.Errorf("after RemoveRow(2) column A = %q / %q / %q, want a1 / a2 / a3",
			rows[0][0], rows[1][0], rows[2][0])
	}
}

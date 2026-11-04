// Scored group: iterator-accessors.
//
// The position and extent accessors the row and column iterators now carry,
// and the row iterator's Close. These exist because the iterators may be
// reading a worksheet part that was too large to hold in memory and was
// spooled to the system temporary directory instead; the caller needs to know
// how far through it is, how much there is, and when the handle can be
// released. The worksheet part is ECMA-376 Part 1 section 18.3.1.99 and its
// rows are section 18.3.1.73 "row".
package iteratoraccessors

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// grid builds a three-row, four-column block starting at A1.
func grid(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	for r := 1; r <= 3; r++ {
		row := make([]interface{}, 4)
		for c := range row {
			row[c] = r*10 + c + 1
		}
		axis, err := seedcalc.CoordinatesToCellName(1, r)
		if err != nil {
			t.Fatalf("CoordinatesToCellName: %v", err)
		}
		if err := f.SetSheetRow("Sheet1", axis, &row); err != nil {
			t.Fatalf("SetSheetRow(%s): %v", axis, err)
		}
	}
	return f
}

func TestTheRowIteratorReportsItsExtentBeforeItStarts(t *testing.T) {
	rows, err := grid(t).Rows("Sheet1")
	if err != nil {
		t.Fatalf("Rows: %v", err)
	}
	defer rows.Close()
	if got := rows.TotalRows(); got != 3 {
		t.Fatalf("TotalRows = %d, want 3", got)
	}
	// The extent is known up front, but no row has been read yet.
	if got := rows.CurrentRow(); got != 0 {
		t.Fatalf("CurrentRow before the first Next = %d, want 0", got)
	}
}

func TestTheRowIteratorAdvancesItsPositionOnEachNext(t *testing.T) {
	rows, err := grid(t).Rows("Sheet1")
	if err != nil {
		t.Fatalf("Rows: %v", err)
	}
	defer rows.Close()
	for want := 1; want <= 3; want++ {
		if !rows.Next() {
			t.Fatalf("Next returned false before row %d", want)
		}
		if got := rows.CurrentRow(); got != want {
			t.Fatalf("CurrentRow = %d, want %d", got, want)
		}
		if got := rows.TotalRows(); got != 3 {
			t.Fatalf("TotalRows changed mid-iteration to %d", got)
		}
	}
	if rows.Next() {
		t.Fatalf("Next returned true past the last row; CurrentRow is now %d", rows.CurrentRow())
	}
	if err := rows.Error(); err != nil {
		t.Fatalf("Error after a clean iteration: %v", err)
	}
}

func TestTheRowIteratorIsClosedWhetherOrNotItSpooled(t *testing.T) {
	rows, err := grid(t).Rows("Sheet1")
	if err != nil {
		t.Fatalf("Rows: %v", err)
	}
	// This worksheet is small enough to stay in memory, so there is no
	// temporary file behind it; Close is still the way the iteration ends and
	// still reports success.
	if err := rows.Close(); err != nil {
		t.Fatalf("Close: %v", err)
	}
}

func TestTheColumnIteratorReportsItsExtentAndPosition(t *testing.T) {
	cols, err := grid(t).Cols("Sheet1")
	if err != nil {
		t.Fatalf("Cols: %v", err)
	}
	if got := cols.TotalCols(); got != 4 {
		t.Fatalf("TotalCols = %d, want 4", got)
	}
	if got := cols.CurrentCol(); got != 0 {
		t.Fatalf("CurrentCol before the first Next = %d, want 0", got)
	}
	for want := 1; want <= 4; want++ {
		if !cols.Next() {
			t.Fatalf("Next returned false before column %d", want)
		}
		if got := cols.CurrentCol(); got != want {
			t.Fatalf("CurrentCol = %d, want %d", got, want)
		}
	}
	if cols.Next() {
		t.Fatalf("Next returned true past the last column; CurrentCol is now %d", cols.CurrentCol())
	}
	if err := cols.Error(); err != nil {
		t.Fatalf("Error after a clean iteration: %v", err)
	}
}

func TestThePositionMatchesTheRowThatIsRead(t *testing.T) {
	rows, err := grid(t).Rows("Sheet1")
	if err != nil {
		t.Fatalf("Rows: %v", err)
	}
	defer rows.Close()
	for rows.Next() {
		cells, err := rows.Columns()
		if err != nil {
			t.Fatalf("Columns: %v", err)
		}
		if len(cells) != 4 {
			t.Fatalf("row %d has %d cells, want 4", rows.CurrentRow(), len(cells))
		}
		// The fixture stores r*10+c in row r, so the first cell of the row the
		// iterator says it is on identifies that row.
		want := map[int]string{1: "11", 2: "21", 3: "31"}[rows.CurrentRow()]
		if cells[0] != want {
			t.Fatalf("CurrentRow says %d but the row reads %v", rows.CurrentRow(), cells)
		}
	}
}

func TestBothIteratorsRejectAnAbsentWorksheet(t *testing.T) {
	f := grid(t)
	if _, err := f.Rows("NoSuchSheet"); err == nil {
		t.Fatal("Rows on an absent sheet returned no error")
	}
	if _, err := f.Cols("NoSuchSheet"); err == nil {
		t.Fatal("Cols on an absent sheet returned no error")
	}
}

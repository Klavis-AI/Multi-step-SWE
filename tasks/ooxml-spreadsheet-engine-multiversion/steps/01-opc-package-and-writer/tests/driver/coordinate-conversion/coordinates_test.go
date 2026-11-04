// Scored group: coordinate-conversion.
//
// A1-style addressing: the bijection between a zero-based column ordinal and
// the base-26 bicameral column title ECMA-376 Part 1 clause 18.3.1.4 fixes for
// the c/@r attribute, and the use of that bijection when a worksheet is read
// back as a rectangle of rows.
package coordinateconversion

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func TestToAlphaStringNamesTheColumnAtAnOrdinal(t *testing.T) {
	cases := []struct {
		ordinal int
		title   string
	}{
		{0, "A"},
		{1, "B"},
		{25, "Z"},
		{26, "AA"},
		{27, "AB"},
		{51, "AZ"},
		{52, "BA"},
		{701, "ZZ"},
		{702, "AAA"},
		{16383, "XFD"},
	}
	for _, c := range cases {
		if got := seedcalc.ToAlphaString(c.ordinal); got != c.title {
			t.Errorf("ToAlphaString(%d) = %q, want %q", c.ordinal, got, c.title)
		}
	}
}

func TestTitleToNumberOrdersTheColumnATitleNames(t *testing.T) {
	cases := []struct {
		title   string
		ordinal int
	}{
		{"A", 0},
		{"B", 1},
		{"Z", 25},
		{"AA", 26},
		{"AB", 27},
		{"AZ", 51},
		{"BA", 52},
		{"ZZ", 701},
		{"AAA", 702},
		{"XFD", 16383},
	}
	for _, c := range cases {
		if got := seedcalc.TitleToNumber(c.title); got != c.ordinal {
			t.Errorf("TitleToNumber(%q) = %d, want %d", c.title, got, c.ordinal)
		}
	}
}

func TestTheTwoConversionsAreInverses(t *testing.T) {
	for i := 0; i <= 2000; i++ {
		title := seedcalc.ToAlphaString(i)
		if title == "" {
			t.Fatalf("ToAlphaString(%d) returned the empty title", i)
		}
		if got := seedcalc.TitleToNumber(title); got != i {
			t.Fatalf("TitleToNumber(ToAlphaString(%d)) = %d via %q, want %d", i, got, title, i)
		}
	}
}

func TestDegenerateCoordinatesConvertToDegenerateResults(t *testing.T) {
	if got := seedcalc.ToAlphaString(-1); got != "" {
		t.Errorf("ToAlphaString(-1) = %q, want the empty title", got)
	}
	if got := seedcalc.TitleToNumber(""); got != -1 {
		t.Errorf("TitleToNumber(\"\") = %d, want -1", got)
	}
}

func TestCellReferencesAddressTheCellTheyName(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", "a1")
	f.SetCellValue("Sheet1", "B2", "b2")
	f.SetCellValue("Sheet1", "AA5", "aa5")

	for axis, want := range map[string]string{
		"A1":  "a1",
		"B2":  "b2",
		"AA5": "aa5",
		"A2":  "",
		"B1":  "",
		"Z5":  "",
	} {
		if got := f.GetCellValue("Sheet1", axis); got != want {
			t.Errorf("GetCellValue(%q) = %q, want %q", axis, got, want)
		}
	}
}

func TestRowsAreReturnedWithCellsAtTheirColumnOrdinal(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "C1", "third")
	f.SetCellValue("Sheet1", "A1", "first")

	rows := f.GetRows("Sheet1")
	if len(rows) < 1 {
		t.Fatalf("GetRows returned %d rows, want at least 1", len(rows))
	}
	row := rows[0]
	if len(row) < 3 {
		t.Fatalf("first row has %d cells, want at least 3: %q", len(row), row)
	}
	if row[seedcalc.TitleToNumber("A")] != "first" {
		t.Errorf("column A of the first row = %q, want %q", row[0], "first")
	}
	if row[seedcalc.TitleToNumber("B")] != "" {
		t.Errorf("column B of the first row = %q, want the empty string", row[1])
	}
	if row[seedcalc.TitleToNumber("C")] != "third" {
		t.Errorf("column C of the first row = %q, want %q", row[2], "third")
	}
}

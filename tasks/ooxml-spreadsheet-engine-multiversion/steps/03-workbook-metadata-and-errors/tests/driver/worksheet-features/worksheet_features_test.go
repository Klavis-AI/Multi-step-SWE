// Scored group: worksheet-features (milestone 03, the error-return migration).
//
// Milestone 02's worksheet surface -- sheets addressed by name, outline
// levels, sheet properties, data validation, protection and search -- must
// keep working once every mutator returns error. Normative sources:
// TEST_INTERFACE.md sections 4.02 and 4.03; ECMA-376 Part 1, 18.3.1.88
// sheetPr, 18.3.1.33 dataValidation, 18.3.1.85 sheetProtection.
package worksheetfeatures

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func TestSheetLifecycleByName(t *testing.T) {
	f := seedcalc.NewFile()
	if got := f.GetSheetName(1); got != "Sheet1" {
		t.Errorf("GetSheetName(1) = %q, want %q", got, "Sheet1")
	}
	if got := f.GetSheetIndex("Sheet1"); got != 1 {
		t.Errorf("GetSheetIndex(\"Sheet1\") = %d, want 1", got)
	}
	if got := f.NewSheet("Data"); got != 2 {
		t.Errorf("NewSheet(\"Data\") = %d, want 2", got)
	}
	f.SetSheetName("Data", "Renamed")
	if got := f.GetSheetIndex("Renamed"); got != 2 {
		t.Errorf("GetSheetIndex(\"Renamed\") = %d, want 2", got)
	}
	if got := f.GetSheetIndex("Data"); got != 0 {
		t.Errorf("GetSheetIndex of the old name = %d, want 0", got)
	}
	names := f.GetSheetMap()
	if len(names) != 2 || names[1] != "Sheet1" || names[2] != "Renamed" {
		t.Errorf("GetSheetMap = %v, want {1: Sheet1, 2: Renamed}", names)
	}
	f.DeleteSheet("Renamed")
	if got := f.GetSheetIndex("Renamed"); got != 0 {
		t.Errorf("GetSheetIndex after DeleteSheet = %d, want 0", got)
	}
	f.SetActiveSheet(1)
	if got := f.GetActiveSheetIndex(); got != 1 {
		t.Errorf("GetActiveSheetIndex = %d, want 1", got)
	}
}

func TestSheetVisibility(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet("Hidden")
	if !f.GetSheetVisible("Hidden") {
		t.Error("a new worksheet is not visible")
	}
	if err := f.SetSheetVisible("Hidden", false); err != nil {
		t.Fatalf("SetSheetVisible: %v", err)
	}
	if f.GetSheetVisible("Hidden") {
		t.Error("GetSheetVisible reports a hidden worksheet as visible")
	}
	if err := f.SetSheetVisible("Hidden", true); err != nil {
		t.Fatalf("SetSheetVisible: %v", err)
	}
	if !f.GetSheetVisible("Hidden") {
		t.Error("GetSheetVisible reports a re-shown worksheet as hidden")
	}
	// SetSheetVisible is the other documented exception to the
	// unknown-worksheet-is-an-error rule (4.03.1): it reports nil for a name
	// that matches no worksheet, in either direction.
	if err := f.SetSheetVisible("NoSuchSheet", false); err != nil {
		t.Errorf("SetSheetVisible(unknown, false) = %v, want nil", err)
	}
	if err := f.SetSheetVisible("NoSuchSheet", true); err != nil {
		t.Errorf("SetSheetVisible(unknown, true) = %v, want nil", err)
	}
}

func TestColumnGeometryAndOutlineLevels(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetColWidth("Sheet1", "A", "C", 17.5); err != nil {
		t.Fatalf("SetColWidth: %v", err)
	}
	if w, err := f.GetColWidth("Sheet1", "B"); err != nil || w != 17.5 {
		t.Errorf("GetColWidth(B) = (%v, %v), want (17.5, nil)", w, err)
	}
	if err := f.SetColVisible("Sheet1", "D", false); err != nil {
		t.Fatalf("SetColVisible: %v", err)
	}
	if v, err := f.GetColVisible("Sheet1", "D"); err != nil || v {
		t.Errorf("GetColVisible(D) = (%v, %v), want (false, nil)", v, err)
	}
	if err := f.SetColOutlineLevel("Sheet1", "E", 3); err != nil {
		t.Fatalf("SetColOutlineLevel: %v", err)
	}
	if lvl, err := f.GetColOutlineLevel("Sheet1", "E"); err != nil || lvl != 3 {
		t.Errorf("GetColOutlineLevel(E) = (%d, %v), want (3, nil)", lvl, err)
	}
	// Column names are validated.
	if err := f.SetColWidth("Sheet1", "*", "C", 10); err == nil {
		t.Error("SetColWidth with a malformed column name returned nil, want a non-nil error")
	}
	if _, err := f.GetColOutlineLevel("Sheet1", "*"); err == nil {
		t.Error("GetColOutlineLevel with a malformed column name returned nil, want a non-nil error")
	}
}

func TestSheetPropertyOptions(t *testing.T) {
	f := seedcalc.NewFile()
	var fit seedcalc.FitToPage
	var summaryBelow seedcalc.OutlineSummaryBelow
	var codeName seedcalc.CodeName

	// Defaults established by milestone 02.
	if err := f.GetSheetPrOptions("Sheet1", &fit, &summaryBelow); err != nil {
		t.Fatalf("GetSheetPrOptions: %v", err)
	}
	if bool(fit) {
		t.Error("FitToPage defaults to true, want false")
	}
	if !bool(summaryBelow) {
		t.Error("OutlineSummaryBelow defaults to false, want true")
	}

	if err := f.SetSheetPrOptions("Sheet1",
		seedcalc.FitToPage(true),
		seedcalc.OutlineSummaryBelow(false),
		seedcalc.CodeName("codename"),
	); err != nil {
		t.Fatalf("SetSheetPrOptions: %v", err)
	}
	if err := f.GetSheetPrOptions("Sheet1", &fit, &summaryBelow, &codeName); err != nil {
		t.Fatalf("GetSheetPrOptions: %v", err)
	}
	if !bool(fit) {
		t.Error("FitToPage was not set")
	}
	if bool(summaryBelow) {
		t.Error("OutlineSummaryBelow was not cleared")
	}
	if string(codeName) != "codename" {
		t.Errorf("CodeName = %q, want %q", codeName, "codename")
	}
	if err := f.SetSheetPrOptions("NoSuchSheet", seedcalc.FitToPage(true)); err == nil {
		t.Error("SetSheetPrOptions on an unknown worksheet returned nil, want a non-nil error")
	}
}

func TestDataValidationRules(t *testing.T) {
	f := seedcalc.NewFile()

	whole := seedcalc.NewDataValidation(true)
	whole.Sqref = "A1:B2"
	if err := whole.SetRange(10, 20, seedcalc.DataValidationTypeWhole, seedcalc.DataValidationOperatorBetween); err != nil {
		t.Fatalf("SetRange: %v", err)
	}
	whole.SetError(seedcalc.DataValidationErrorStyleStop, "error title", "error body")
	whole.SetInput("input title", "input body")
	if err := f.AddDataValidation("Sheet1", whole); err != nil {
		t.Fatalf("AddDataValidation: %v", err)
	}

	list := seedcalc.NewDataValidation(true)
	list.SetSqref("C1:C5")
	if err := list.SetDropList([]string{"1", "2", "3"}); err != nil {
		t.Fatalf("SetDropList: %v", err)
	}
	if err := f.AddDataValidation("Sheet1", list); err != nil {
		t.Fatalf("AddDataValidation: %v", err)
	}

	// A drop list longer than the 255-character limit is rejected.
	long := make([]string, 0, 64)
	for i := 0; i < 64; i++ {
		long = append(long, "0123456789")
	}
	if err := seedcalc.NewDataValidation(true).SetDropList(long); err == nil {
		t.Error("SetDropList with an overlong list returned nil, want a non-nil error")
	}
	if err := f.AddDataValidation("NoSuchSheet", list); err == nil {
		t.Error("AddDataValidation on an unknown worksheet returned nil, want a non-nil error")
	}
}

func TestProtectionAndSearch(t *testing.T) {
	f := seedcalc.NewFile()
	for cell, value := range map[string]string{"A1": "needle", "B2": "haystack", "C3": "needle"} {
		if err := f.SetCellValue("Sheet1", cell, value); err != nil {
			t.Fatalf("SetCellValue(%s): %v", cell, err)
		}
	}
	hits, err := f.SearchSheet("Sheet1", "needle")
	if err != nil {
		t.Fatalf("SearchSheet: %v", err)
	}
	found := map[string]bool{}
	for _, h := range hits {
		found[h] = true
	}
	if !found["A1"] || !found["C3"] || len(hits) != 2 {
		t.Errorf("SearchSheet = %v, want exactly A1 and C3", hits)
	}
	if hits, err := f.SearchSheet("Sheet1", "needle|haystack", true); err != nil || len(hits) != 3 {
		t.Errorf("SearchSheet with a regular expression = (%v, %v), want three hits", hits, err)
	}

	if err := f.ProtectSheet("Sheet1", &seedcalc.FormatSheetProtection{
		Password:      "password",
		EditScenarios: false,
	}); err != nil {
		t.Fatalf("ProtectSheet: %v", err)
	}
	if err := f.UnprotectSheet("Sheet1"); err != nil {
		t.Fatalf("UnprotectSheet: %v", err)
	}
	// Protection with default settings is accepted.
	if err := f.ProtectSheet("Sheet1", nil); err != nil {
		t.Errorf("ProtectSheet(nil) = %v, want nil", err)
	}
}

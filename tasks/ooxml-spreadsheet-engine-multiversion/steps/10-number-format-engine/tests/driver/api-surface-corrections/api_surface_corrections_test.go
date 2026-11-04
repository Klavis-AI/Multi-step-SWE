// Scored group: api-surface-corrections.
//
// Four exported identifiers milestone 09 shipped misspelled are corrected
// here, four content-type constants are published, and the cross-sheet drop
// list is withdrawn. None of the corrected names exists under the previous
// spelling any more, so this is a rename and not an alias.
package apisurfacecorrections

import (
	"errors"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func TestTheCorrectedSpellingsCarryTheirOldValues(t *testing.T) {
	// A spelling correction must not become a value change.
	if got := seedcalc.NameSpaceDublinCoreMetadataInitiative; got != "http://purl.org/dc/dcmitype/" {
		t.Fatalf("NameSpaceDublinCoreMetadataInitiative = %q", got)
	}
	if got := int(seedcalc.DataValidationTypeTextLength); got != 6 {
		t.Fatalf("DataValidationTypeTextLength = %d, want 6", got)
	}
	if got := seedcalc.ErrDataValidationFormulaLength.Error(); got != "data validation must be 0-255 characters" {
		t.Fatalf("ErrDataValidationFormulaLength = %q", got)
	}
	if got := seedcalc.ErrDefinedNameDuplicate.Error(); got != "the same name already exists on the scope" {
		t.Fatalf("ErrDefinedNameDuplicate = %q", got)
	}
}

func TestTheRenamedSentinelsAreStillTheOnesReturned(t *testing.T) {
	// The rename reaches the paths that raise them, so errors.Is still
	// answers for a caller that branches on them.
	f := seedcalc.NewFile()
	name := seedcalc.DefinedName{Name: "Amount", RefersTo: "Sheet1!$A$1", Scope: "Sheet1"}
	if err := f.SetDefinedName(&name); err != nil {
		t.Fatalf("SetDefinedName: %v", err)
	}
	if err := f.SetDefinedName(&name); !errors.Is(err, seedcalc.ErrDefinedNameDuplicate) {
		t.Fatalf("second SetDefinedName = %v, want ErrDefinedNameDuplicate", err)
	}

	dv := seedcalc.NewDataValidation(true)
	dv.Sqref = "A1:A10"
	if err := dv.SetDropList([]string{strings.Repeat("x", 256)}); !errors.Is(err, seedcalc.ErrDataValidationFormulaLength) {
		t.Fatalf("SetDropList(256 chars) = %v, want ErrDataValidationFormulaLength", err)
	}
	// One character under the limit is accepted.
	if err := dv.SetDropList([]string{strings.Repeat("x", 253)}); err != nil {
		t.Fatalf("SetDropList(253 chars): %v", err)
	}
}

func TestTheTextLengthValidationTypeSpellsItselfOut(t *testing.T) {
	dv := seedcalc.NewDataValidation(true)
	dv.Sqref = "B1:B3"
	if err := dv.SetRange(1, 10, seedcalc.DataValidationTypeTextLength,
		seedcalc.DataValidationOperatorBetween); err != nil {
		t.Fatalf("SetRange: %v", err)
	}
	if dv.Type != "textLength" {
		t.Fatalf("Type = %q, want textLength", dv.Type)
	}
	if dv.Operator != "between" {
		t.Fatalf("Operator = %q, want between", dv.Operator)
	}
	if dv.Formula1 != "<formula1>1</formula1>" || dv.Formula2 != "<formula2>10</formula2>" {
		t.Fatalf("formulae = %q / %q", dv.Formula1, dv.Formula2)
	}
}

func TestTheDropListSqrefFormTakesOnlyTheReference(t *testing.T) {
	// The cross-sheet flag is withdrawn, and with it the failure it was
	// the only cause of, so the method no longer returns an error.
	dv := seedcalc.NewDataValidation(true)
	dv.SetSqrefDropList("E1:E3")
	if dv.Type != "list" {
		t.Fatalf("Type = %q, want list", dv.Type)
	}
	if dv.Formula1 != "<formula1>E1:E3</formula1>" {
		t.Fatalf("Formula1 = %q", dv.Formula1)
	}
	// It sets the source, not the target: the validated range is still
	// whatever the caller assigns to Sqref.
	if dv.Sqref != "" {
		t.Fatalf("Sqref = %q, want it left alone", dv.Sqref)
	}
	dv.Sqref = "A1:A5"
	if dv.Formula1 != "<formula1>E1:E3</formula1>" {
		t.Fatalf("Formula1 changed to %q", dv.Formula1)
	}
}

func TestTheWorkbookContentTypesArePublished(t *testing.T) {
	for name, pair := range map[string][2]string{
		"ContentTypeSheetML": {seedcalc.ContentTypeSheetML,
			"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"},
		"ContentTypeTemplate": {seedcalc.ContentTypeTemplate,
			"application/vnd.openxmlformats-officedocument.spreadsheetml.template.main+xml"},
		"ContentTypeAddinMacro": {seedcalc.ContentTypeAddinMacro,
			"application/vnd.ms-excel.addin.macroEnabled.main+xml"},
		"ContentTypeTemplateMacro": {seedcalc.ContentTypeTemplateMacro,
			"application/vnd.ms-excel.template.macroEnabled.main+xml"},
	} {
		if pair[0] != pair[1] {
			t.Fatalf("%s = %q, want %q", name, pair[0], pair[1])
		}
	}
}

func TestTheMacroContentTypeIsStillTheOneItWas(t *testing.T) {
	// The four new constants join the macro-enabled workbook type
	// milestone 04 published; that one keeps its value and its spelling.
	if got := seedcalc.ContentTypeMacro; got != "application/vnd.ms-excel.sheet.macroEnabled.main+xml" {
		t.Fatalf("ContentTypeMacro = %q", got)
	}
	// All five are distinct, which is what makes the extension map that
	// selects between them meaningful.
	seen := map[string]bool{}
	for _, ct := range []string{
		seedcalc.ContentTypeSheetML, seedcalc.ContentTypeTemplate,
		seedcalc.ContentTypeMacro, seedcalc.ContentTypeAddinMacro,
		seedcalc.ContentTypeTemplateMacro,
	} {
		if seen[ct] {
			t.Fatalf("content type %q is duplicated", ct)
		}
		seen[ct] = true
	}
}

func TestTheSparklineStyleMessageIsSpelledCorrectly(t *testing.T) {
	// The sentinel keeps its name; only the word "betweent" in its text is
	// repaired, so a caller matching on the string sees the change.
	if got := seedcalc.ErrSparklineStyle.Error(); got != "parameter 'Style' must between 0-35" {
		t.Fatalf("ErrSparklineStyle = %q", got)
	}
	f := seedcalc.NewFile()
	sparkline := func(style int) error {
		return f.AddSparkline("Sheet1", &seedcalc.SparklineOption{
			Location: []string{"A1"},
			Range:    []string{"Sheet1!B1:D1"},
			Type:     "line",
			Style:    style,
		})
	}
	for _, style := range []int{-1, 36} {
		if err := sparkline(style); !errors.Is(err, seedcalc.ErrSparklineStyle) {
			t.Fatalf("Style=%d gave %v, want ErrSparklineStyle", style, err)
		}
	}
	// Both ends of the range are inclusive.
	for _, style := range []int{0, 35} {
		if err := sparkline(style); err != nil {
			t.Fatalf("Style=%d: %v", style, err)
		}
	}
	// Its neighbours in the same family are untouched.
	if got := seedcalc.ErrSparklineType.Error(); got != "parameter 'Type' must be 'line', 'column' or 'win_loss'" {
		t.Fatalf("ErrSparklineType = %q", got)
	}
}

func TestTheEncryptionMechanismSentinelIsRenamedAndRephrased(t *testing.T) {
	// Milestone 09 spelled both the identifier and its message without the
	// "-ed"; both are corrected, and the sentinel for a mechanism that is
	// not merely unsupported but unrecognised stands beside it.
	if got := seedcalc.ErrUnsupportedEncryptMechanism.Error(); got != "unsupported encryption mechanism" {
		t.Fatalf("ErrUnsupportedEncryptMechanism = %q", got)
	}
	if got := seedcalc.ErrUnknownEncryptMechanism.Error(); got != "unknown encryption mechanism" {
		t.Fatalf("ErrUnknownEncryptMechanism = %q", got)
	}
	if errors.Is(seedcalc.ErrUnsupportedEncryptMechanism, seedcalc.ErrUnknownEncryptMechanism) {
		t.Fatalf("the two mechanism sentinels are the same value")
	}
	// The sentinel milestone 07 introduced for a workbook that cannot be
	// encrypted at all is unchanged.
	if got := seedcalc.ErrEncrypt.Error(); got != "not support encryption currently" {
		t.Fatalf("ErrEncrypt = %q", got)
	}
}

// Scored group: data-validation.
//
// Data validation rules: in-cell drop lists, numeric ranges with a comparison
// operator, input prompts, error alerts and the sqref sequence the rule
// applies to. Graded against ECMA-376 Part 1 clause 18.3.1.32
// (dataValidation, and its type, operator, errorStyle, allowBlank,
// showInputMessage, showErrorMessage and sqref attributes) and clause
// 18.3.1.33 (dataValidations and its count attribute). Behaviour cases were
// checked against the upstream release this checkpoint tracks (tag v1.4.1).
package datavalidation

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"io"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

type xmlValidation struct {
	AllowBlank       bool   `xml:"allowBlank,attr"`
	Type             string `xml:"type,attr"`
	Operator         string `xml:"operator,attr"`
	Sqref            string `xml:"sqref,attr"`
	ErrorStyle       string `xml:"errorStyle,attr"`
	ErrorTitle       string `xml:"errorTitle,attr"`
	Error            string `xml:"error,attr"`
	Prompt           string `xml:"prompt,attr"`
	ShowErrorMessage bool   `xml:"showErrorMessage,attr"`
	ShowInputMessage bool   `xml:"showInputMessage,attr"`
	Formula1         string `xml:"formula1"`
	Formula2         string `xml:"formula2"`
}

type xmlValidations struct {
	Count int             `xml:"count,attr"`
	Rules []xmlValidation `xml:"dataValidation"`
}

type worksheet struct {
	Validations *xmlValidations `xml:"dataValidations"`
}

func decodeSheet(t *testing.T, f *seedcalc.File) worksheet {
	t.Helper()
	buf, err := f.WriteToBuffer()
	if err != nil {
		t.Fatalf("WriteToBuffer: %v", err)
	}
	b := buf.Bytes()
	zr, err := zip.NewReader(bytes.NewReader(b), int64(len(b)))
	if err != nil {
		t.Fatalf("written package is not a ZIP container: %v", err)
	}
	const name = "xl/worksheets/sheet1.xml"
	for _, fh := range zr.File {
		if fh.Name != name {
			continue
		}
		rc, err := fh.Open()
		if err != nil {
			t.Fatalf("open %s: %v", name, err)
		}
		defer rc.Close()
		var raw bytes.Buffer
		if _, err := io.Copy(&raw, rc); err != nil {
			t.Fatalf("read %s: %v", name, err)
		}
		var ws worksheet
		if err := xml.Unmarshal(raw.Bytes(), &ws); err != nil {
			t.Fatalf("%s is not well-formed XML: %v", name, err)
		}
		return ws
	}
	t.Fatalf("written package has no part %s", name)
	return worksheet{}
}

func deref(t *testing.T, label string, p *string) string {
	t.Helper()
	if p == nil {
		t.Fatalf("%s is nil", label)
	}
	return *p
}

func TestNewDataValidationStartsWithMessagesOff(t *testing.T) {
	dv := seedcalc.NewDataValidation(true)
	if dv == nil {
		t.Fatal("NewDataValidation returned nil")
	}
	if !dv.AllowBlank {
		t.Error("NewDataValidation(true).AllowBlank = false, want true")
	}
	if dv.ShowErrorMessage {
		t.Error("a fresh rule already shows an error alert")
	}
	if dv.ShowInputMessage {
		t.Error("a fresh rule already shows an input message")
	}
	if seedcalc.NewDataValidation(false).AllowBlank {
		t.Error("NewDataValidation(false).AllowBlank = true, want false")
	}
}

func TestDropListBecomesAQuotedInlineList(t *testing.T) {
	dv := seedcalc.NewDataValidation(true)
	if err := dv.SetDropList([]string{"red", "green", "blue"}); err != nil {
		t.Fatalf("SetDropList: %v", err)
	}
	if dv.Type != "list" {
		t.Errorf("Type = %q after SetDropList, want %q", dv.Type, "list")
	}
	if want := `"red,green,blue"`; dv.Formula1 != want {
		t.Errorf("Formula1 = %q, want %q -- the members are comma separated inside one quoted literal", dv.Formula1, want)
	}
}

func TestRangeRecordsBoundsTypeAndOperator(t *testing.T) {
	dv := seedcalc.NewDataValidation(true)
	if err := dv.SetRange(10, 20, seedcalc.DataValidationTypeWhole, seedcalc.DataValidationOperatorBetween); err != nil {
		t.Fatalf("SetRange: %v", err)
	}
	if dv.Formula1 != "10" || dv.Formula2 != "20" {
		t.Errorf("Formula1/Formula2 = %q/%q, want \"10\"/\"20\"", dv.Formula1, dv.Formula2)
	}
	if dv.Type != "whole" {
		t.Errorf("Type = %q, want %q", dv.Type, "whole")
	}
	if dv.Operator != "between" {
		t.Errorf("Operator = %q, want %q", dv.Operator, "between")
	}

	other := seedcalc.NewDataValidation(true)
	if err := other.SetRange(1, 5, seedcalc.DataValidationTypeDecimal, seedcalc.DataValidationOperatorGreaterThan); err != nil {
		t.Fatalf("SetRange: %v", err)
	}
	if other.Type != "decimal" || other.Operator != "greaterThan" {
		t.Errorf("Type/Operator = %q/%q, want \"decimal\"/\"greaterThan\"", other.Type, other.Operator)
	}

	// A formula already longer than the 255-character payload limit (plus the
	// two enclosing quotes) must be refused rather than silently truncated.
	over := seedcalc.NewDataValidation(true)
	if err := over.SetDropList([]string{strings.Repeat("x", 300)}); err != nil {
		t.Fatalf("SetDropList: %v", err)
	}
	if err := over.SetRange(1, 2, seedcalc.DataValidationTypeWhole, seedcalc.DataValidationOperatorBetween); err == nil {
		t.Error("SetRange accepted a rule whose existing formula exceeds the length limit")
	}

	// Every declared type constant reaches its documented markup spelling.
	for _, tc := range []struct {
		typ  seedcalc.DataValidationType
		want string
	}{
		{seedcalc.DataValidationTypeCustom, "custom"},
		{seedcalc.DataValidationTypeDate, "date"},
		{seedcalc.DataValidationTypeDecimal, "decimal"},
		{seedcalc.DataValidationTypeTextLeng, "textLength"},
		{seedcalc.DataValidationTypeTime, "time"},
		{seedcalc.DataValidationTypeWhole, "whole"},
	} {
		d := seedcalc.NewDataValidation(true)
		if err := d.SetRange(1, 2, tc.typ, seedcalc.DataValidationOperatorBetween); err != nil {
			t.Fatalf("SetRange with type constant %q: %v", tc.want, err)
		}
		if d.Type != tc.want {
			t.Errorf("Type = %q for the constant meaning %q, want %q", d.Type, tc.want, tc.want)
		}
	}

	// Every declared operator constant reaches its documented markup spelling.
	for _, tc := range []struct {
		op   seedcalc.DataValidationOperator
		want string
	}{
		{seedcalc.DataValidationOperatorBetween, "between"},
		{seedcalc.DataValidationOperatorEqual, "equal"},
		{seedcalc.DataValidationOperatorGreaterThan, "greaterThan"},
		{seedcalc.DataValidationOperatorGreaterThanOrEqual, "greaterThanOrEqual"},
		{seedcalc.DataValidationOperatorLessThan, "lessThan"},
		{seedcalc.DataValidationOperatorLessThanOrEqual, "lessThanOrEqual"},
		{seedcalc.DataValidationOperatorNotBetween, "notBetween"},
		{seedcalc.DataValidationOperatorNotEqual, "notEqual"},
	} {
		d := seedcalc.NewDataValidation(true)
		if err := d.SetRange(1, 2, seedcalc.DataValidationTypeWhole, tc.op); err != nil {
			t.Fatalf("SetRange with operator constant %q: %v", tc.want, err)
		}
		if d.Operator != tc.want {
			t.Errorf("Operator = %q for the constant meaning %q, want %q", d.Operator, tc.want, tc.want)
		}
	}
}

func TestErrorAlertRecordsStyleTitleAndMessage(t *testing.T) {
	for _, tc := range []struct {
		style seedcalc.DataValidationErrorStyle
		want  string
	}{
		{seedcalc.DataValidationErrorStyleStop, "stop"},
		{seedcalc.DataValidationErrorStyleWarning, "warning"},
		{seedcalc.DataValidationErrorStyleInformation, "information"},
	} {
		dv := seedcalc.NewDataValidation(true)
		dv.SetError(tc.style, "title", "message")
		if !dv.ShowErrorMessage {
			t.Errorf("style %v: ShowErrorMessage = false after SetError, want true", tc.want)
		}
		if got := deref(t, "ErrorStyle", dv.ErrorStyle); got != tc.want {
			t.Errorf("ErrorStyle = %q, want %q", got, tc.want)
		}
		if got := deref(t, "ErrorTitle", dv.ErrorTitle); got != "title" {
			t.Errorf("ErrorTitle = %q, want %q", got, "title")
		}
		if got := deref(t, "Error", dv.Error); got != "message" {
			t.Errorf("Error = %q, want %q", got, "message")
		}
	}
}

func TestInputMessageRecordsTitleAndPrompt(t *testing.T) {
	dv := seedcalc.NewDataValidation(true)
	if dv.ShowInputMessage {
		t.Fatal("ShowInputMessage is set before SetInput")
	}
	dv.SetInput("heading", "type a number")
	if !dv.ShowInputMessage {
		t.Error("ShowInputMessage = false after SetInput, want true")
	}
	if got := deref(t, "PromptTitle", dv.PromptTitle); got != "heading" {
		t.Errorf("PromptTitle = %q, want %q", got, "heading")
	}
	if got := deref(t, "Prompt", dv.Prompt); got != "type a number" {
		t.Errorf("Prompt = %q, want %q", got, "type a number")
	}
}

func TestSqrefAccumulatesASpaceSeparatedSequence(t *testing.T) {
	dv := seedcalc.NewDataValidation(true)
	if dv.Sqref != "" {
		t.Fatalf("a fresh rule already has Sqref %q", dv.Sqref)
	}
	dv.SetSqref("A1:B2")
	if dv.Sqref != "A1:B2" {
		t.Errorf("Sqref = %q after the first SetSqref, want %q", dv.Sqref, "A1:B2")
	}
	dv.SetSqref("D4")
	if want := "A1:B2 D4"; dv.Sqref != want {
		t.Errorf("Sqref = %q after a second SetSqref, want %q", dv.Sqref, want)
	}
	dv.SetSqref("F6:F9")
	if want := "A1:B2 D4 F6:F9"; dv.Sqref != want {
		t.Errorf("Sqref = %q after a third SetSqref, want %q", dv.Sqref, want)
	}
}

func TestSqrefDropListRejectsACrossSheetSource(t *testing.T) {
	same := seedcalc.NewDataValidation(true)
	if err := same.SetSqrefDropList("E1:E3", true); err != nil {
		t.Fatalf("SetSqrefDropList for a source on the same worksheet: %v", err)
	}
	if same.Type != "list" {
		t.Errorf("Type = %q, want %q", same.Type, "list")
	}
	if same.Formula1 != "E1:E3" {
		t.Errorf("Formula1 = %q, want the source reference %q", same.Formula1, "E1:E3")
	}

	cross := seedcalc.NewDataValidation(true)
	if err := cross.SetSqrefDropList("Sheet2!E1:E3", false); err == nil {
		t.Error("SetSqrefDropList accepted a source on another worksheet")
	}
	if cross.Formula1 != "" {
		t.Errorf("Formula1 = %q after a rejected cross-sheet source, want it left unset", cross.Formula1)
	}
}

func TestAddDataValidationWritesTheRuleToTheWorksheet(t *testing.T) {
	f := seedcalc.NewFile()
	dv := seedcalc.NewDataValidation(true)
	dv.Sqref = "A1:B2"
	if err := dv.SetDropList([]string{"1", "2", "3"}); err != nil {
		t.Fatalf("SetDropList: %v", err)
	}
	dv.SetError(seedcalc.DataValidationErrorStyleWarning, "bad", "pick one")
	dv.SetInput("pick", "one of three")
	f.AddDataValidation("Sheet1", dv)

	second := seedcalc.NewDataValidation(false)
	second.SetSqref("D1")
	if err := second.SetRange(1, 9, seedcalc.DataValidationTypeWhole, seedcalc.DataValidationOperatorBetween); err != nil {
		t.Fatalf("SetRange: %v", err)
	}
	f.AddDataValidation("Sheet1", second)

	ws := decodeSheet(t, f)
	if ws.Validations == nil {
		t.Fatal("written worksheet carries no dataValidations element")
	}
	if len(ws.Validations.Rules) != 2 {
		t.Fatalf("worksheet carries %d rules, want 2", len(ws.Validations.Rules))
	}
	if ws.Validations.Count != 2 {
		t.Errorf("dataValidations/@count = %d, want 2", ws.Validations.Count)
	}

	got := ws.Validations.Rules[0]
	if got.Type != "list" {
		t.Errorf("rule 1 type = %q, want %q", got.Type, "list")
	}
	if got.Sqref != "A1:B2" {
		t.Errorf("rule 1 sqref = %q, want %q", got.Sqref, "A1:B2")
	}
	if !got.AllowBlank {
		t.Error("rule 1 allowBlank = false, want true")
	}
	if got.Formula1 != `"1,2,3"` {
		t.Errorf("rule 1 formula1 = %q, want %q", got.Formula1, `"1,2,3"`)
	}
	if !got.ShowErrorMessage || got.ErrorStyle != "warning" || got.ErrorTitle != "bad" || got.Error != "pick one" {
		t.Errorf("rule 1 error alert = %+v, want a warning alert titled \"bad\"", got)
	}
	if !got.ShowInputMessage || got.Prompt != "one of three" {
		t.Errorf("rule 1 input message = %+v, want the prompt to be carried", got)
	}

	got = ws.Validations.Rules[1]
	if got.Type != "whole" || got.Operator != "between" {
		t.Errorf("rule 2 type/operator = %q/%q, want \"whole\"/\"between\"", got.Type, got.Operator)
	}
	if got.Formula1 != "1" || got.Formula2 != "9" {
		t.Errorf("rule 2 bounds = %q..%q, want 1..9", got.Formula1, got.Formula2)
	}
	if got.Sqref != "D1" {
		t.Errorf("rule 2 sqref = %q, want %q", got.Sqref, "D1")
	}
	if got.AllowBlank {
		t.Error("rule 2 allowBlank = true, want false")
	}
}

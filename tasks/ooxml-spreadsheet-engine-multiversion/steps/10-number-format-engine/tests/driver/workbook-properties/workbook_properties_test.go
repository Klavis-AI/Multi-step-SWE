// Scored group: workbook-properties.
//
// The workbookPr element gains an option interface of its own, matching the
// one the sheet views already use: a setter and a getter that take a varying
// list of typed options. Two options are defined -- the privacy flag that
// tells a consumer not to save personal information with the file, and the
// VBA code name, which was already settable by another route and now also
// implements this interface.
//
// The element is ECMA-376 Part 1 section 18.2.28 (workbookPr); filterPrivacy
// and codeName are attributes of it.
package workbookproperties

import (
	"archive/zip"
	"bytes"
	"io"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// element returns the first occurrence of the named start tag in a part of
// the written package, or the empty string when the tag is absent.
func element(t *testing.T, f *seedcalc.File, part, tag string) string {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	zr, err := zip.NewReader(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	if err != nil {
		t.Fatalf("zip.NewReader: %v", err)
	}
	for _, zf := range zr.File {
		if zf.Name != part {
			continue
		}
		rc, err := zf.Open()
		if err != nil {
			t.Fatalf("open %s: %v", part, err)
		}
		b, err := io.ReadAll(rc)
		rc.Close()
		if err != nil {
			t.Fatalf("read %s: %v", part, err)
		}
		for _, chunk := range strings.Split(strings.ReplaceAll(string(b), ">", ">\n"), "\n") {
			if strings.HasPrefix(chunk, tag) {
				return chunk
			}
		}
		return ""
	}
	t.Fatalf("%s is missing from the package", part)
	return ""
}

func workbookPr(t *testing.T, f *seedcalc.File) string {
	t.Helper()
	return element(t, f, "xl/workbook.xml", "<workbookPr")
}

func TestThePrivacyFlagRoundTrips(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetWorkbookPrOptions(seedcalc.FilterPrivacy(true)); err != nil {
		t.Fatalf("SetWorkbookPrOptions: %v", err)
	}
	var privacy seedcalc.FilterPrivacy
	if err := f.GetWorkbookPrOptions(&privacy); err != nil {
		t.Fatalf("GetWorkbookPrOptions: %v", err)
	}
	if !bool(privacy) {
		t.Fatalf("FilterPrivacy = %v, want true", bool(privacy))
	}
	if el := workbookPr(t, f); !strings.Contains(el, `filterPrivacy="true"`) {
		t.Fatalf("workbookPr = %q, want filterPrivacy=\"true\"", el)
	}
}

func TestTheFalsePrivacyFlagOmitsTheAttribute(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetWorkbookPrOptions(seedcalc.FilterPrivacy(false)); err != nil {
		t.Fatalf("SetWorkbookPrOptions: %v", err)
	}
	// The attribute defaults to false in the schema, so the writer leaves it
	// out rather than spelling it.
	if el := workbookPr(t, f); strings.Contains(el, "filterPrivacy") {
		t.Fatalf("workbookPr = %q, want no filterPrivacy attribute", el)
	}
	var privacy seedcalc.FilterPrivacy
	if err := f.GetWorkbookPrOptions(&privacy); err != nil {
		t.Fatalf("GetWorkbookPrOptions: %v", err)
	}
	if bool(privacy) {
		t.Fatalf("FilterPrivacy = %v, want false", bool(privacy))
	}
}

func TestTheCodeNameRoundTripsThroughTheSameInterface(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetWorkbookPrOptions(seedcalc.CodeName("Project")); err != nil {
		t.Fatalf("SetWorkbookPrOptions: %v", err)
	}
	var name seedcalc.CodeName
	if err := f.GetWorkbookPrOptions(&name); err != nil {
		t.Fatalf("GetWorkbookPrOptions: %v", err)
	}
	if string(name) != "Project" {
		t.Fatalf("CodeName = %q, want %q", string(name), "Project")
	}
	if el := workbookPr(t, f); !strings.Contains(el, `codeName="Project"`) {
		t.Fatalf("workbookPr = %q, want codeName=\"Project\"", el)
	}
}

func TestSeveralOptionsAreAppliedInOneCall(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetWorkbookPrOptions(seedcalc.FilterPrivacy(true), seedcalc.CodeName("Both")); err != nil {
		t.Fatalf("SetWorkbookPrOptions: %v", err)
	}
	var privacy seedcalc.FilterPrivacy
	var name seedcalc.CodeName
	if err := f.GetWorkbookPrOptions(&privacy, &name); err != nil {
		t.Fatalf("GetWorkbookPrOptions: %v", err)
	}
	if !bool(privacy) || string(name) != "Both" {
		t.Fatalf("got privacy=%v name=%q, want true and %q", bool(privacy), string(name), "Both")
	}
	el := workbookPr(t, f)
	for _, want := range []string{`filterPrivacy="true"`, `codeName="Both"`} {
		if !strings.Contains(el, want) {
			t.Fatalf("workbookPr = %q, want %s", el, want)
		}
	}
}

func TestAnOptionSetTwiceKeepsTheSecondValue(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetWorkbookPrOptions(seedcalc.CodeName("First")); err != nil {
		t.Fatalf("SetWorkbookPrOptions: %v", err)
	}
	if err := f.SetWorkbookPrOptions(seedcalc.CodeName("Second")); err != nil {
		t.Fatalf("SetWorkbookPrOptions: %v", err)
	}
	var name seedcalc.CodeName
	if err := f.GetWorkbookPrOptions(&name); err != nil {
		t.Fatalf("GetWorkbookPrOptions: %v", err)
	}
	if string(name) != "Second" {
		t.Fatalf("CodeName = %q, want %q", string(name), "Second")
	}
	el := workbookPr(t, f)
	if strings.Contains(el, "First") {
		t.Fatalf("workbookPr still carries the first value: %q", el)
	}
}

func TestAnEmptyOptionListIsAccepted(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetWorkbookPrOptions(); err != nil {
		t.Fatalf("SetWorkbookPrOptions with no options: %v", err)
	}
	if err := f.GetWorkbookPrOptions(); err != nil {
		t.Fatalf("GetWorkbookPrOptions with no options: %v", err)
	}
	// The element is still written, and still has no code name.
	el := workbookPr(t, f)
	if !strings.HasPrefix(el, "<workbookPr") {
		t.Fatalf("the workbook part carries no workbookPr element: %q", el)
	}
	if strings.Contains(el, "codeName") {
		t.Fatalf("workbookPr = %q, want no codeName attribute", el)
	}
}

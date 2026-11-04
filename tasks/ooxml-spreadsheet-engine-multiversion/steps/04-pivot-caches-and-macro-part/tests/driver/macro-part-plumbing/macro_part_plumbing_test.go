// Scored group: macro-part-plumbing (milestone 04, peripheral).
//
// AddVBAProject is milestone 04's headline macro feature, and it is easy to
// special-case: write the given file to some part name and call it done. The
// cases here exercise the plumbing that a special case would skip -- the
// fixed part name and relationship target the implementation actually uses
// (not the caller's basename), what happens when the source path exists but
// cannot be read as a file, the order in which the existence and extension
// checks run, and what a second call does to previously-stored bytes.
//
// Behaviour stated by TEST_INTERFACE.md 4.04.10 (measured, for the
// reference, against the release this checkpoint tracks):
//
//   - the stored part is always keyed "xl/vbaProject.bin" and the workbook
//     relationship's Target is always the literal string "vbaProject.bin",
//     regardless of the basename of the path passed in;
//   - the existence check (os.Stat + os.IsNotExist) runs before the
//     extension check, so a missing path with a bad extension fails as a
//     "does not exist" error, never reaching the extension error;
//   - the file is read with `file, _ := ioutil.ReadFile(bin)`, discarding
//     the read error, and the content-type/relationship wiring happens
//     before that read -- so a path that exists but cannot be read as a
//     plain file (a directory) still wires the content type override, the
//     default media type and the workbook relationship, and still returns a
//     nil error, but stores a zero-length part;
//   - a second call overwrites the stored bytes while the relationship and
//     default content-type entries stay singular (deduplicated by Target/
//     Extension, not by input path).
//
// This surface does not exist before milestone 04: AddVBAProject,
// SourceRelationshipVBAProject and the macro-enabled content types are new
// symbols with no milestone-03 equivalent, so every case here fails to even
// compile against the milestone-03 predecessor.
package macropartplumbing

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"io/ioutil"
	"os"
	"path/filepath"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func savedParts(t *testing.T, f *seedcalc.File) map[string]string {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	zr, err := zip.NewReader(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	if err != nil {
		t.Fatalf("the saved workbook is not a readable package: %v", err)
	}
	out := make(map[string]string, len(zr.File))
	for _, zf := range zr.File {
		rc, err := zf.Open()
		if err != nil {
			t.Fatalf("open %s: %v", zf.Name, err)
		}
		b, err := ioutil.ReadAll(rc)
		rc.Close()
		if err != nil {
			t.Fatalf("read %s: %v", zf.Name, err)
		}
		out[zf.Name] = string(b)
	}
	return out
}

type contentTypes struct {
	Default []struct {
		Extension   string `xml:"Extension,attr"`
		ContentType string `xml:"ContentType,attr"`
	} `xml:"Default"`
	Override []struct {
		PartName    string `xml:"PartName,attr"`
		ContentType string `xml:"ContentType,attr"`
	} `xml:"Override"`
}

func parseContentTypes(t *testing.T, parts map[string]string) contentTypes {
	t.Helper()
	var ct contentTypes
	if err := xml.Unmarshal([]byte(parts["[Content_Types].xml"]), &ct); err != nil {
		t.Fatalf("[Content_Types].xml is not well-formed: %v", err)
	}
	return ct
}

type relationships struct {
	Rel []struct {
		ID     string `xml:"Id,attr"`
		Type   string `xml:"Type,attr"`
		Target string `xml:"Target,attr"`
	} `xml:"Relationship"`
}

func parseRels(t *testing.T, parts map[string]string, name string) relationships {
	t.Helper()
	var r relationships
	raw, ok := parts[name]
	if !ok {
		t.Fatalf("the package has no %s part", name)
	}
	if err := xml.Unmarshal([]byte(raw), &r); err != nil {
		t.Fatalf("%s is not well-formed relationship markup: %v", name, err)
	}
	return r
}

func writeMacro(t *testing.T, name string, content []byte) string {
	t.Helper()
	p := filepath.Join(t.TempDir(), name)
	if err := ioutil.WriteFile(p, content, 0o600); err != nil {
		t.Fatalf("writing the fixture: %v", err)
	}
	return p
}

func macroRelCount(rels relationships) int {
	n := 0
	for _, r := range rels.Rel {
		if r.Type == seedcalc.SourceRelationshipVBAProject {
			n++
		}
	}
	return n
}

func binDefaultCount(ct contentTypes) int {
	n := 0
	for _, d := range ct.Default {
		if d.Extension == "bin" {
			n++
		}
	}
	return n
}

// TestAddVBAProjectUsesAFixedPartNameRegardlessOfBasename checks that the
// part name inside the package and the relationship target it is wired to
// are both the fixed literal "vbaProject.bin" (well, "xl/vbaProject.bin" for
// the part), not derived from the basename of the path the caller passed in.
// A special case that copies the input file to a part named after its own
// basename would fail this.
func TestAddVBAProjectUsesAFixedPartNameRegardlessOfBasename(t *testing.T) {
	f := seedcalc.NewFile()
	content := []byte{0xD0, 0xCF, 0x11, 0xE0, 0x01, 0x02, 0x03, 0x04}
	p := writeMacro(t, "quite-a-different-name.bin", content)
	if err := f.AddVBAProject(p); err != nil {
		t.Fatalf("AddVBAProject: %v", err)
	}
	parts := savedParts(t, f)
	got, ok := parts["xl/vbaProject.bin"]
	if !ok {
		t.Fatal("the package has no xl/vbaProject.bin part, even though the input had a different basename")
	}
	if !bytes.Equal([]byte(got), content) {
		t.Errorf("the stored macro part is %d bytes and differs from the input", len(got))
	}
	if _, ok := parts["xl/quite-a-different-name.bin"]; ok {
		t.Error("the package stored the macro part under the input's own basename instead of the fixed name")
	}
	rels := parseRels(t, parts, "xl/_rels/workbook.xml.rels")
	var found bool
	for _, r := range rels.Rel {
		if r.Type != seedcalc.SourceRelationshipVBAProject {
			continue
		}
		found = true
		if r.Target != "vbaProject.bin" {
			t.Errorf("the macro relationship targets %q, want the fixed literal vbaProject.bin", r.Target)
		}
	}
	if !found {
		t.Error("the workbook declares no macro relationship")
	}
}

// TestAddVBAProjectOnADirectoryPathSucceedsWithAnEmptyPart documents a real
// gap in the Oracle's plumbing: the existence check only rejects a path that
// does not exist at all (os.IsNotExist). A path that exists but cannot be
// read as a plain file -- here, a directory -- passes that check, passes the
// extension check (the directory is named with a .bin suffix), gets the
// content type and relationship wired exactly as a real macro would, and the
// subsequent `ioutil.ReadFile` failure is discarded (`file, _ :=
// ioutil.ReadFile(bin)`), so AddVBAProject reports success while storing a
// zero-length part.
func TestAddVBAProjectOnADirectoryPathSucceedsWithAnEmptyPart(t *testing.T) {
	dir := filepath.Join(t.TempDir(), "vbaProject.bin")
	if err := os.Mkdir(dir, 0o755); err != nil {
		t.Fatalf("Mkdir: %v", err)
	}
	f := seedcalc.NewFile()
	if err := f.AddVBAProject(dir); err != nil {
		t.Fatalf("AddVBAProject on a directory path returned an error (want nil, matching the Oracle's discarded-read-error behaviour): %v", err)
	}
	parts := savedParts(t, f)
	got, ok := parts["xl/vbaProject.bin"]
	if !ok {
		t.Fatal("the package has no xl/vbaProject.bin part")
	}
	if len(got) != 0 {
		t.Errorf("the stored macro part is %d bytes, want 0 for an unreadable directory source", len(got))
	}
	ct := parseContentTypes(t, parts)
	if binDefaultCount(ct) != 1 {
		t.Errorf("bin default content type entries = %d, want 1 even though the payload could not be read", binDefaultCount(ct))
	}
	var workbookType string
	for _, o := range ct.Override {
		if o.PartName == "/xl/workbook.xml" {
			workbookType = o.ContentType
		}
	}
	if workbookType != "application/vnd.ms-excel.sheet.macroEnabled.main+xml" {
		t.Errorf("workbook part content type = %q, want the macro-enabled type even though the payload could not be read", workbookType)
	}
	if macroRelCount(parseRels(t, parts, "xl/_rels/workbook.xml.rels")) != 1 {
		t.Error("the workbook declares no macro relationship even though the plumbing otherwise wired the macro part")
	}
}

// TestMissingPathWithBadExtensionFailsOnExistenceNotExtension checks the
// order the two validations run in: AddVBAProject calls os.Stat before it
// looks at path.Ext, so a path that both does not exist and has the wrong
// extension is rejected as "does not exist", never reaching the "unsupported
// VBA project extension" branch. Nothing gets wired in this case.
func TestMissingPathWithBadExtensionFailsOnExistenceNotExtension(t *testing.T) {
	f := seedcalc.NewFile()
	missing := filepath.Join(t.TempDir(), "nope.xml")
	err := f.AddVBAProject(missing)
	if err == nil {
		t.Fatal("AddVBAProject accepted a path that does not exist")
	}
	if !os.IsNotExist(err) {
		t.Errorf("AddVBAProject error = %v, want an os.IsNotExist error from the existence check", err)
	}
	if err.Error() == "unsupported VBA project extension" {
		t.Error("AddVBAProject reached the extension check on a path that does not exist")
	}
	parts := savedParts(t, f)
	if _, ok := parts["xl/vbaProject.bin"]; ok {
		t.Error("a rejected macro project produced a part anyway")
	}
	ct := parseContentTypes(t, parts)
	if binDefaultCount(ct) != 0 {
		t.Error("a rejected macro project still added a bin default content type")
	}
	for _, o := range ct.Override {
		if o.PartName == "/xl/workbook.xml" && o.ContentType == "application/vnd.ms-excel.sheet.macroEnabled.main+xml" {
			t.Error("a rejected macro project still made the workbook part macro-enabled")
		}
	}
	if macroRelCount(parseRels(t, parts, "xl/_rels/workbook.xml.rels")) != 0 {
		t.Error("a rejected macro project still added a macro relationship")
	}
}

// TestSecondAddVBAProjectCallOverwritesStoredBytes checks what a second call
// does to previously-stored bytes: the relationship is deduplicated by
// Target/Type and the default content type by Extension, both independent of
// the input path, but the stored part itself is unconditionally overwritten
// with whatever the second call reads.
func TestSecondAddVBAProjectCallOverwritesStoredBytes(t *testing.T) {
	f := seedcalc.NewFile()
	first := writeMacro(t, "first.bin", []byte{0x01, 0x02, 0x03})
	second := writeMacro(t, "second.bin", []byte{0xAA, 0xBB, 0xCC, 0xDD, 0xEE})
	if err := f.AddVBAProject(first); err != nil {
		t.Fatalf("first AddVBAProject: %v", err)
	}
	if err := f.AddVBAProject(second); err != nil {
		t.Fatalf("second AddVBAProject: %v", err)
	}
	parts := savedParts(t, f)
	got, ok := parts["xl/vbaProject.bin"]
	if !ok {
		t.Fatal("the package has no xl/vbaProject.bin part")
	}
	if !bytes.Equal([]byte(got), []byte{0xAA, 0xBB, 0xCC, 0xDD, 0xEE}) {
		t.Errorf("after two calls the stored part is %v, want the second call's bytes", []byte(got))
	}
	if macroRelCount(parseRels(t, parts, "xl/_rels/workbook.xml.rels")) != 1 {
		t.Error("after two calls the workbook declares other than 1 macro relationship")
	}
	if binDefaultCount(parseContentTypes(t, parts)) != 1 {
		t.Error("after two calls the content type stream declares other than 1 default for bin")
	}
}

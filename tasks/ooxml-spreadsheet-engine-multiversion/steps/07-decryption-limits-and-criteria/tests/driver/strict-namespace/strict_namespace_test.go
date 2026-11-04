// Scored group: strict-namespace.
//
// The two officeDocument relationship-type constants this milestone exports:
// the transitional one a package written here actually uses, and the Strict
// one a package produced by a Strict-conformant producer would carry instead.
// The relationship type is ECMA-376 Part 1 section 12.3.23 "Workbook Part",
// whose "Source Relationship" row carries the Strict form and whose "Root
// Namespace" row carries the Strict namespace root, and Part 2 section 6.5
// "Relationships". The Strict conformance class is ECMA-376 Part 1 clause 2
// "Conformance" (section 2.1, Document Conformance); the shipped edition of
// Part 1 specifies the Strict form only, so the Transitional URI is given
// verbatim in TEST_INTERFACE.md section 4.07.8 rather than cited.
package strictnamespace

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"io"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func TestTheOfficeDocumentRelationshipTypeConstants(t *testing.T) {
	if seedcalc.SourceRelationshipOfficeDocument != "http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" {
		t.Fatalf("SourceRelationshipOfficeDocument = %q", seedcalc.SourceRelationshipOfficeDocument)
	}
	if seedcalc.StrictSourceRelationshipOfficeDocument != "http://purl.oclc.org/ooxml/officeDocument/relationships/officeDocument" {
		t.Fatalf("StrictSourceRelationshipOfficeDocument = %q", seedcalc.StrictSourceRelationshipOfficeDocument)
	}
}

func TestTheTwoConformanceClassesUseDifferentNamespaceRoots(t *testing.T) {
	transitional := seedcalc.SourceRelationshipOfficeDocument
	strict := seedcalc.StrictSourceRelationshipOfficeDocument
	if transitional == strict {
		t.Fatal("the Transitional and Strict officeDocument relationship types are the same string")
	}
	if !strings.HasPrefix(transitional, "http://schemas.openxmlformats.org/") {
		t.Fatalf("the Transitional relationship type %q is not rooted at the openxmlformats.org namespace", transitional)
	}
	if !strings.HasPrefix(strict, "http://purl.oclc.org/ooxml/") {
		t.Fatalf("the Strict relationship type %q is not rooted at the purl.oclc.org/ooxml namespace", strict)
	}
	// Both name the same relationship, so both end in the same segment.
	const suffix = "/officeDocument"
	if !strings.HasSuffix(transitional, suffix) || !strings.HasSuffix(strict, suffix) {
		t.Fatalf("the two relationship types do not both end in %q: %q and %q", suffix, transitional, strict)
	}
}

func TestAWrittenPackageUsesTheTransitionalRelationshipType(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellValue("Sheet1", "A1", 1); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	zr, err := zip.NewReader(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	if err != nil {
		t.Fatalf("written package is not a ZIP container: %v", err)
	}
	var raw []byte
	for _, zf := range zr.File {
		if zf.Name != "_rels/.rels" {
			continue
		}
		rc, err := zf.Open()
		if err != nil {
			t.Fatalf("open _rels/.rels: %v", err)
		}
		raw, err = io.ReadAll(rc)
		rc.Close()
		if err != nil {
			t.Fatalf("read _rels/.rels: %v", err)
		}
	}
	if raw == nil {
		t.Fatal("written package has no _rels/.rels part")
	}
	var rels struct {
		Relationship []struct {
			Type   string `xml:"Type,attr"`
			Target string `xml:"Target,attr"`
		} `xml:"Relationship"`
	}
	if err := xml.Unmarshal(raw, &rels); err != nil {
		t.Fatalf("_rels/.rels is not well-formed: %v", err)
	}
	var found bool
	for _, r := range rels.Relationship {
		if r.Type == seedcalc.StrictSourceRelationshipOfficeDocument {
			t.Fatal("the written package declares the Strict officeDocument relationship type; this library writes Transitional packages")
		}
		if r.Type == seedcalc.SourceRelationshipOfficeDocument {
			found = true
		}
	}
	if !found {
		t.Fatalf("the written package declares no relationship of type %q", seedcalc.SourceRelationshipOfficeDocument)
	}
}

func TestAStrictPackageIsTranslatedOnRead(t *testing.T) {
	// The other half of the conformance-class contract: a package written by a
	// Strict-conformant producer carries the purl.oclc.org relationship type,
	// and the reader must recognise it and translate it to the Transitional
	// namespace rather than failing to find the workbook part.
	f := seedcalc.NewFile()
	if err := f.SetCellValue("Sheet1", "A1", "hello"); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	zr, err := zip.NewReader(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	if err != nil {
		t.Fatalf("written package is not a ZIP container: %v", err)
	}

	// Rewrite the root relationship part into its Strict form, leaving every
	// other part of the package byte-identical.
	var out bytes.Buffer
	zw := zip.NewWriter(&out)
	var rewrote bool
	for _, zf := range zr.File {
		rc, err := zf.Open()
		if err != nil {
			t.Fatalf("open %s: %v", zf.Name, err)
		}
		content, err := io.ReadAll(rc)
		rc.Close()
		if err != nil {
			t.Fatalf("read %s: %v", zf.Name, err)
		}
		if zf.Name == "_rels/.rels" {
			swapped := bytes.ReplaceAll(content,
				[]byte(seedcalc.SourceRelationshipOfficeDocument),
				[]byte(seedcalc.StrictSourceRelationshipOfficeDocument))
			if !bytes.Equal(swapped, content) {
				rewrote = true
			}
			content = swapped
		}
		w, err := zw.Create(zf.Name)
		if err != nil {
			t.Fatalf("create %s: %v", zf.Name, err)
		}
		if _, err := w.Write(content); err != nil {
			t.Fatalf("write %s: %v", zf.Name, err)
		}
	}
	if err := zw.Close(); err != nil {
		t.Fatalf("close rewritten package: %v", err)
	}
	if !rewrote {
		t.Fatal("the Transitional officeDocument relationship type was not present in _rels/.rels, so nothing was converted to Strict")
	}

	g, err := seedcalc.OpenReader(bytes.NewReader(out.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader on a Strict-namespaced package: %v", err)
	}
	got, err := g.GetCellValue("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetCellValue after reading a Strict package: %v", err)
	}
	if got != "hello" {
		t.Fatalf("GetCellValue after reading a Strict package = %q, want %q", got, "hello")
	}
	if sheets := g.GetSheetList(); len(sheets) != 1 || sheets[0] != "Sheet1" {
		t.Fatalf("sheet list after reading a Strict package = %v, want [Sheet1]", sheets)
	}
}

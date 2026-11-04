// Scored group: comments-and-vml.
//
// Cell comments are stored in two cooperating parts: the SpreadsheetML comments
// part of ECMA-376 Part 1 clause 18.7 (authors + commentList) and the legacy
// VML drawing that anchors the comment box, referenced from the worksheet by
// the legacyDrawing element of clause 18.3.1.36.
package commentsandvml

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

const relNS = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"

func packageParts(t *testing.T, f *seedcalc.File) map[string][]byte {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	zr, err := zip.NewReader(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	if err != nil {
		t.Fatalf("written package is not a ZIP container: %v", err)
	}
	out := map[string][]byte{}
	for _, e := range zr.File {
		rc, err := e.Open()
		if err != nil {
			t.Fatalf("open part %s: %v", e.Name, err)
		}
		var b bytes.Buffer
		if _, err := b.ReadFrom(rc); err != nil {
			t.Fatalf("read part %s: %v", e.Name, err)
		}
		rc.Close()
		out[e.Name] = b.Bytes()
	}
	return out
}

func part(t *testing.T, parts map[string][]byte, name string) []byte {
	t.Helper()
	b, ok := parts[name]
	if !ok {
		names := make([]string, 0, len(parts))
		for n := range parts {
			names = append(names, n)
		}
		t.Fatalf("package has no part %s; it has %v", name, names)
	}
	return b
}

type commentsPart struct {
	Authors struct {
		Author []string `xml:"author"`
	} `xml:"authors"`
	CommentList struct {
		Comment []struct {
			Ref      string `xml:"ref,attr"`
			AuthorID string `xml:"authorId,attr"`
			Text     struct {
				R []struct {
					T string `xml:"t"`
				} `xml:"r"`
			} `xml:"text"`
		} `xml:"comment"`
	} `xml:"commentList"`
}

type relationships struct {
	Rel []struct {
		ID     string `xml:"Id,attr"`
		Type   string `xml:"Type,attr"`
		Target string `xml:"Target,attr"`
	} `xml:"Relationship"`
}

func TestACommentIsStoredWithItsAuthorAndText(t *testing.T) {
	f := seedcalc.NewFile()
	f.AddComment("Sheet1", "B4", `{"author":"Reviewer: ","text":"needs a second look"}`)

	var c commentsPart
	if err := xml.Unmarshal(part(t, packageParts(t, f), "xl/comments1.xml"), &c); err != nil {
		t.Fatalf("xl/comments1.xml is not well-formed XML: %v", err)
	}
	if len(c.Authors.Author) == 0 {
		t.Fatal("the comments part declares no authors")
	}
	if !strings.Contains(strings.Join(c.Authors.Author, "\x00"), "Reviewer") {
		t.Errorf("authors = %q, want one naming the author given to AddComment", c.Authors.Author)
	}
	if len(c.CommentList.Comment) != 1 {
		t.Fatalf("the comment list holds %d comments, want 1", len(c.CommentList.Comment))
	}
	got := c.CommentList.Comment[0]
	if got.Ref != "B4" {
		t.Errorf("comment ref = %q, want %q", got.Ref, "B4")
	}
	if got.AuthorID == "" {
		t.Error("comment has no authorId")
	}
	var text string
	for _, r := range got.Text.R {
		text += r.T
	}
	if !strings.Contains(text, "needs a second look") {
		t.Errorf("comment text = %q, want it to carry the text given to AddComment", text)
	}
	if !strings.Contains(text, "Reviewer") {
		t.Errorf("comment text = %q, want it to carry the author as well", text)
	}
}

func TestACommentBringsItsLegacyVmlDrawing(t *testing.T) {
	f := seedcalc.NewFile()
	f.AddComment("Sheet1", "B4", `{"author":"Reviewer: ","text":"needs a second look"}`)
	parts := packageParts(t, f)

	vml := part(t, parts, "xl/drawings/vmlDrawing1.vml")
	if len(vml) == 0 {
		t.Fatal("the legacy VML drawing part is empty")
	}
	if !strings.Contains(string(vml), `ObjectType="Note"`) {
		t.Error("the legacy VML drawing declares no Note client object for the comment")
	}

	var ct struct {
		Defaults []struct {
			Extension   string `xml:"Extension,attr"`
			ContentType string `xml:"ContentType,attr"`
		} `xml:"Default"`
		Overrides []struct {
			PartName    string `xml:"PartName,attr"`
			ContentType string `xml:"ContentType,attr"`
		} `xml:"Override"`
	}
	if err := xml.Unmarshal(part(t, parts, "[Content_Types].xml"), &ct); err != nil {
		t.Fatalf("[Content_Types].xml is not well-formed XML: %v", err)
	}
	seenVML := false
	for _, d := range ct.Defaults {
		if strings.EqualFold(d.Extension, "vml") {
			seenVML = true
			if d.ContentType != "application/vnd.openxmlformats-officedocument.vmlDrawing" {
				t.Errorf("Default for vml has content type %q", d.ContentType)
			}
		}
	}
	if !seenVML {
		t.Error("[Content_Types].xml has no Default for the vml extension")
	}
	seenComments := false
	for _, o := range ct.Overrides {
		if o.PartName == "/xl/comments1.xml" {
			seenComments = true
			if o.ContentType != "application/vnd.openxmlformats-officedocument.spreadsheetml.comments+xml" {
				t.Errorf("Override for the comments part has content type %q", o.ContentType)
			}
		}
	}
	if !seenComments {
		t.Error("[Content_Types].xml has no Override for /xl/comments1.xml")
	}
}

func TestTheWorksheetPointsAtTheLegacyDrawing(t *testing.T) {
	f := seedcalc.NewFile()
	f.AddComment("Sheet1", "B4", `{"author":"Reviewer: ","text":"needs a second look"}`)
	parts := packageParts(t, f)

	var ws struct {
		LegacyDrawing struct {
			ID string `xml:"http://schemas.openxmlformats.org/officeDocument/2006/relationships id,attr"`
		} `xml:"legacyDrawing"`
	}
	if err := xml.Unmarshal(part(t, parts, "xl/worksheets/sheet1.xml"), &ws); err != nil {
		t.Fatalf("the worksheet part is not well-formed XML: %v", err)
	}
	if ws.LegacyDrawing.ID == "" {
		t.Fatal("the worksheet declares no legacyDrawing relationship")
	}

	var rels relationships
	if err := xml.Unmarshal(part(t, parts, "xl/worksheets/_rels/sheet1.xml.rels"), &rels); err != nil {
		t.Fatalf("the worksheet relationship part is not well-formed XML: %v", err)
	}
	var target, commentsTarget string
	for _, r := range rels.Rel {
		switch r.Type {
		case relNS + "/vmlDrawing":
			if r.ID == ws.LegacyDrawing.ID {
				target = r.Target
			}
		case relNS + "/comments":
			commentsTarget = r.Target
		}
	}
	if target == "" {
		t.Fatalf("the legacyDrawing id %q resolves to no vmlDrawing relationship (%d relationships present)", ws.LegacyDrawing.ID, len(rels.Rel))
	}
	if !strings.HasSuffix(target, "vmlDrawing1.vml") {
		t.Errorf("the legacyDrawing relationship targets %q, want the vmlDrawing1.vml part", target)
	}
	if !strings.HasSuffix(commentsTarget, "comments1.xml") {
		t.Errorf("the worksheet relates to comments target %q, want the comments1.xml part", commentsTarget)
	}
}

func TestEachWorksheetGetsItsOwnCommentParts(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet(2, "Second")
	f.AddComment("Sheet1", "A1", `{"author":"A: ","text":"first"}`)
	f.AddComment("Sheet2", "A1", `{"author":"B: ","text":"second"}`)
	parts := packageParts(t, f)

	for _, name := range []string{
		"xl/comments1.xml",
		"xl/comments2.xml",
		"xl/drawings/vmlDrawing1.vml",
		"xl/drawings/vmlDrawing2.vml",
	} {
		if _, ok := parts[name]; !ok {
			t.Errorf("package has no part %s", name)
		}
	}

	var second commentsPart
	if err := xml.Unmarshal(part(t, parts, "xl/comments2.xml"), &second); err != nil {
		t.Fatalf("xl/comments2.xml is not well-formed XML: %v", err)
	}
	if len(second.CommentList.Comment) != 1 {
		t.Fatalf("the second worksheet's comment list holds %d comments, want 1", len(second.CommentList.Comment))
	}
	var text string
	for _, r := range second.CommentList.Comment[0].Text.R {
		text += r.T
	}
	if !strings.Contains(text, "second") {
		t.Errorf("the second worksheet's comment reads %q, want the text added to that worksheet", text)
	}
}

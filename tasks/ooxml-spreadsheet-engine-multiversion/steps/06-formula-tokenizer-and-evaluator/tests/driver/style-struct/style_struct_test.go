// Scored group: style-struct.
//
// The cell-format description becomes a first-class exported value: NewStyle
// now takes interface{} and accepts either the historical JSON string or a
// *Style built from the exported Alignment, Border, Fill, Font and Protection
// structures. Behaviour transcribed from the upstream checkpoint this milestone
// tracks, qax-os/excelize tag v2.2.0 (styles_test.go), restricted to the public
// interface. The underlying format record is ECMA-376 Part 1 section 18.8.45
// "xf (Format)" inside section 18.8.39 "styleSheet"; the horizontal and
// vertical alignment vocabularies are sections 18.18.40 "ST_HorizontalAlignment"
// and 18.18.88 "ST_VerticalAlignment".
package stylestruct

import (
	"bytes"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// fullStyle is a style that exercises every member of the Style structure.
func fullStyle() *seedcalc.Style {
	return &seedcalc.Style{
		Font: &seedcalc.Font{
			Bold:      true,
			Italic:    true,
			Underline: "single",
			Family:    "Times New Roman",
			Size:      14,
			Strike:    true,
			Color:     "FF0000",
		},
		Alignment: &seedcalc.Alignment{
			Horizontal:   "center",
			Vertical:     "center",
			WrapText:     true,
			TextRotation: 45,
			Indent:       1,
			ShrinkToFit:  false,
			ReadingOrder: 0,
		},
		Fill: seedcalc.Fill{
			Type:    "pattern",
			Pattern: 1,
			Color:   []string{"00FF00"},
		},
		Border: []seedcalc.Border{
			{Type: "left", Color: "0000FF", Style: 1},
			{Type: "top", Color: "0000FF", Style: 2},
		},
		Protection: &seedcalc.Protection{Hidden: true, Locked: true},
		NumFmt:     2,
	}
}

func TestNewStyleAcceptsAStyleStructure(t *testing.T) {
	f := seedcalc.NewFile()
	id, err := f.NewStyle(fullStyle())
	if err != nil {
		t.Fatalf("NewStyle(*Style): %v", err)
	}
	if id <= 0 {
		t.Fatalf("NewStyle(*Style) = %d, want a positive style identifier", id)
	}
}

func TestNewStyleStillAcceptsAJSONDescription(t *testing.T) {
	f := seedcalc.NewFile()
	id, err := f.NewStyle(`{"font":{"bold":true,"size":12,"color":"#777777"}}`)
	if err != nil {
		t.Fatalf("NewStyle(string): %v", err)
	}
	if id <= 0 {
		t.Fatalf("NewStyle(string) = %d, want a positive style identifier", id)
	}
	if _, err := f.NewStyle("not json"); err == nil {
		t.Fatal("NewStyle with a malformed JSON description returned no error")
	}
}

func TestNewStyleRejectsAnUnsupportedParameterType(t *testing.T) {
	f := seedcalc.NewFile()
	for _, bad := range []interface{}{42, 1.5, true, []string{"a"}, nil} {
		id, err := f.NewStyle(bad)
		if err == nil {
			t.Fatalf("NewStyle(%T) = %d, want an error: only a JSON string and a *Style are accepted", bad, id)
		}
		if id != 0 {
			t.Fatalf("NewStyle(%T) returned style identifier %d alongside an error, want 0", bad, id)
		}
	}
}

func TestAnEmptyStyleStructureIsAccepted(t *testing.T) {
	f := seedcalc.NewFile()
	id, err := f.NewStyle(&seedcalc.Style{})
	if err != nil {
		t.Fatalf("NewStyle(&Style{}): %v", err)
	}
	if id <= 0 {
		t.Fatalf("NewStyle(&Style{}) = %d, want a positive style identifier", id)
	}
}

func TestEveryRegisteredStyleGetsItsOwnIdentifier(t *testing.T) {
	f := seedcalc.NewFile()
	seen := map[int]string{}
	for _, s := range []*seedcalc.Style{
		{Font: &seedcalc.Font{Bold: true}},
		{Font: &seedcalc.Font{Italic: true}},
		{Fill: seedcalc.Fill{Type: "pattern", Pattern: 1, Color: []string{"FFFF00"}}},
		{NumFmt: 3},
		{Alignment: &seedcalc.Alignment{Horizontal: "right"}},
	} {
		id, err := f.NewStyle(s)
		if err != nil {
			t.Fatalf("NewStyle: %v", err)
		}
		if id <= 0 {
			t.Fatalf("NewStyle = %d, want a positive style identifier", id)
		}
		if _, dup := seen[id]; dup {
			t.Fatalf("NewStyle reused style identifier %d for a different style", id)
		}
		seen[id] = ""
	}
	if len(seen) != 5 {
		t.Fatalf("registered %d distinct style identifiers, want 5", len(seen))
	}
}

func TestACustomNumberFormatIsAccepted(t *testing.T) {
	f := seedcalc.NewFile()
	format := "[$-380A]dddd\\,\\ dd\" de \"mmmm\" de \"yyyy;@"
	id, err := f.NewStyle(&seedcalc.Style{
		CustomNumFmt:  &format,
		DecimalPlaces: 3,
		Lang:          "en-US",
		NegRed:        true,
	})
	if err != nil {
		t.Fatalf("NewStyle with a custom number format: %v", err)
	}
	if id <= 0 {
		t.Fatalf("NewStyle with a custom number format = %d, want a positive style identifier", id)
	}
}

func TestAStyleAppliedToACellIsReadBack(t *testing.T) {
	f := seedcalc.NewFile()
	id, err := f.NewStyle(fullStyle())
	if err != nil {
		t.Fatalf("NewStyle: %v", err)
	}
	if err := f.SetCellStyle("Sheet1", "B2", "C3", id); err != nil {
		t.Fatalf("SetCellStyle: %v", err)
	}
	for _, axis := range []string{"B2", "B3", "C2", "C3"} {
		got, err := f.GetCellStyle("Sheet1", axis)
		if err != nil {
			t.Fatalf("GetCellStyle(%s): %v", axis, err)
		}
		if got != id {
			t.Fatalf("GetCellStyle(%s) = %d, want %d", axis, got, id)
		}
	}
	// A cell outside the styled rectangle keeps the default format.
	got, err := f.GetCellStyle("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetCellStyle(A1): %v", err)
	}
	if got == id {
		t.Fatalf("GetCellStyle(A1) = %d, want the default style, not the one applied to B2:C3", got)
	}
}

func TestAStyleSurvivesTheWriteReadRoundTrip(t *testing.T) {
	f := seedcalc.NewFile()
	id, err := f.NewStyle(fullStyle())
	if err != nil {
		t.Fatalf("NewStyle: %v", err)
	}
	if err := f.SetCellStyle("Sheet1", "A1", "A1", id); err != nil {
		t.Fatalf("SetCellStyle: %v", err)
	}
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	g, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	got, err := g.GetCellStyle("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetCellStyle after round trip: %v", err)
	}
	if got != id {
		t.Fatalf("GetCellStyle after round trip = %d, want %d", got, id)
	}
}

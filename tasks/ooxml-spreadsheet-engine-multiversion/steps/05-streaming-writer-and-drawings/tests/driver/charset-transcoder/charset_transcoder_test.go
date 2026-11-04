// Scored group: charset-transcoder (milestone 05, "The streaming writer and
// the drawing registry").
//
// Normative sources:
//   * TEST_INTERFACE.md section 4.05 -- "Charset transcoder".
//   * excelize.go CharsetTranscoder: method sets f.CharsetReader and returns
//     *File for chaining. The default CharsetReader is charset.NewReaderLabel
//     (golang.org/x/net/html/charset). A custom function is called when
//     reading XML with a non-UTF-8 charset declaration; the fixture
//     assets/windows-1252.xlsx carries such a declaration on its worksheet
//     part, so a transcoder attached before that part is read must be called.
package charsettranscoder

import (
	"bytes"
	"io"
	"os"
	"path/filepath"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// nullTranscoder is a no-op that converts any charset as if it were UTF-8.
func nullTranscoder(charset string, input io.Reader) (io.Reader, error) {
	return input, nil
}

func TestCharsetTranscoderReturnsFile(t *testing.T) {
	f := seedcalc.NewFile()
	got := f.CharsetTranscoder(nullTranscoder)
	if got != f {
		t.Error("CharsetTranscoder must return the receiver *File for chaining")
	}
}

func TestCharsetTranscoderIsCalledOnNonUTF8Content(t *testing.T) {
	dir := os.Getenv("SEEDCALC_ASSETS")
	if dir == "" {
		t.Fatal("SEEDCALC_ASSETS is not set")
	}
	called := false
	label := ""
	spy := func(charset string, input io.Reader) (io.Reader, error) {
		if !called {
			label = charset
		}
		called = true
		return decodeWindows1252(input), nil
	}

	// The fixture's worksheet part declares encoding="windows-1252", so
	// reading that part is the path that calls CharsetReader. OpenFile
	// returns before the worksheet is decoded, which is what lets a caller
	// attach a transcoder to the returned *File and have the read use it.
	f, err := seedcalc.OpenFile(filepath.Join(dir, "windows-1252.xlsx"))
	if err != nil {
		t.Fatalf("OpenFile on the windows-1252 fixture: %v", err)
	}
	f.CharsetTranscoder(spy)
	rows, err := f.GetRows("Sheet1")
	if err != nil {
		t.Fatalf("GetRows through the transcoder: %v", err)
	}
	if !called {
		t.Fatal("the transcoder was not called for a part that declares a non-UTF-8 charset")
	}
	if !strings.EqualFold(label, "windows-1252") {
		t.Errorf("the transcoder was called with charset %q, want the declared %q", label, "windows-1252")
	}
	if len(rows) == 0 || len(rows[0]) == 0 || rows[0][0] != "café" {
		t.Errorf("the transcoder's output did not reach the reader: GetRows = %q, want the first cell to read %q", rows, "café")
	}
}

// decodeWindows1252 turns the fixture's bytes into UTF-8. Its only non-ASCII
// byte is 0xE9 ("é"), which is the same code point in windows-1252 and in
// Latin-1, so a byte-to-rune copy is enough for this fixture.
func decodeWindows1252(input io.Reader) io.Reader {
	b, err := io.ReadAll(input)
	if err != nil {
		return input
	}
	var out bytes.Buffer
	for _, c := range b {
		out.WriteRune(rune(c))
	}
	return &out
}

func TestCharsetTranscoderChaining(t *testing.T) {
	f := seedcalc.NewFile()
	// Chain two transcoders; the second must win and the object must survive.
	f2 := f.CharsetTranscoder(nullTranscoder).CharsetTranscoder(nullTranscoder)
	if f2 == nil {
		t.Fatal("chained CharsetTranscoder returned nil")
	}
	if f2 != f {
		t.Error("chained CharsetTranscoder must return the same *File")
	}
}

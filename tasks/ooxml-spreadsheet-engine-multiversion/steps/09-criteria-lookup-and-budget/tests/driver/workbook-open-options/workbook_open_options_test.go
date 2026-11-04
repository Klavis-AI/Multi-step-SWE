// Scored group: workbook-open-options.
//
// The Options structure the reading entry points now accept, and the two
// decompression budgets it carries: the overall unzip limit and the per-part
// XML limit that decides whether a worksheet is held in memory or spooled to
// the system temporary directory. The budgets exist because an OPC package is
// a ZIP archive whose declared sizes are not trustworthy: ECMA-376 Part 2
// section 7.3 "Mapping to a ZIP file" maps the package onto the container, and
// its Annex B.2 notes that the sizes describing each item are duplicated
// between the local file header and the central directory. Nothing there
// bounds what a part expands to.
package workbookopenoptions

import (
	"bytes"
	"errors"
	"os"
	"path/filepath"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// packed returns a small workbook serialised to bytes.
func packed(t *testing.T) []byte {
	t.Helper()
	f := seedcalc.NewFile()
	for i := 1; i <= 20; i++ {
		axis, err := seedcalc.CoordinatesToCellName(1, i)
		if err != nil {
			t.Fatalf("CoordinatesToCellName: %v", err)
		}
		if err := f.SetCellValue("Sheet1", axis, i); err != nil {
			t.Fatalf("SetCellValue(%s): %v", axis, err)
		}
	}
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	return buf.Bytes()
}

func TestOpeningWithoutOptionsStillWorks(t *testing.T) {
	b := packed(t)
	g, err := seedcalc.OpenReader(bytes.NewReader(b))
	if err != nil {
		t.Fatalf("OpenReader with no options: %v", err)
	}
	got, err := g.GetCellValue("Sheet1", "A7")
	if err != nil {
		t.Fatalf("GetCellValue: %v", err)
	}
	if got != "7" {
		t.Fatalf("GetCellValue(A7) = %q, want %q", got, "7")
	}
	// An empty Options value is the same as passing none: the zero fields are
	// replaced by the defaults rather than taken literally.
	if _, err := seedcalc.OpenReader(bytes.NewReader(b), seedcalc.Options{}); err != nil {
		t.Fatalf("OpenReader with a zero Options: %v", err)
	}
}

func TestTheDefaultBudgetsAreTheExportedConstants(t *testing.T) {
	if seedcalc.UnzipSizeLimit != 1000<<24 {
		t.Fatalf("UnzipSizeLimit = %d, want %d", seedcalc.UnzipSizeLimit, 1000<<24)
	}
	if seedcalc.StreamChunkSize != 1<<24 {
		t.Fatalf("StreamChunkSize = %d, want %d", seedcalc.StreamChunkSize, 1<<24)
	}
	// The per-part limit defaults to the chunk size, which is well under the
	// overall limit, so the two defaults are consistent with each other.
	if int64(seedcalc.StreamChunkSize) > int64(seedcalc.UnzipSizeLimit) {
		t.Fatal("the default XML size limit exceeds the default unzip size limit")
	}
}

func TestAnOversizePackageIsRefusedByTheUnzipLimit(t *testing.T) {
	b := packed(t)
	// A budget of a few hundred bytes cannot hold even the relationship parts.
	_, err := seedcalc.OpenReader(bytes.NewReader(b), seedcalc.Options{UnzipSizeLimit: 256, UnzipXMLSizeLimit: 128})
	if err == nil {
		t.Fatal("OpenReader with a 256 byte unzip budget returned no error")
	}
	// The budget that was exceeded is named in the message.
	if want := "256"; !bytes.Contains([]byte(err.Error()), []byte(want)) {
		t.Fatalf("error %q does not name the budget %s that was exceeded", err, want)
	}
}

func TestTheXMLBudgetMayNotExceedTheOverallBudget(t *testing.T) {
	b := packed(t)
	_, err := seedcalc.OpenReader(bytes.NewReader(b), seedcalc.Options{
		UnzipSizeLimit:    1 << 10,
		UnzipXMLSizeLimit: 1 << 20,
	})
	if !errors.Is(err, seedcalc.ErrOptionsUnzipSizeLimit) {
		t.Fatalf("OpenReader with an XML budget above the overall budget returned %v, want %v", err, seedcalc.ErrOptionsUnzipSizeLimit)
	}
}

func TestOneBudgetGivenAloneWidensTheOtherAndOnlyTheLastOptionsValueCounts(t *testing.T) {
	b := packed(t)
	// Only the XML budget is named, and it is larger than the default overall
	// budget would be if that were left at zero; the overall budget is raised
	// to match rather than the pair being rejected.
	if _, err := seedcalc.OpenReader(bytes.NewReader(b), seedcalc.Options{UnzipXMLSizeLimit: 1 << 20}); err != nil {
		t.Fatalf("OpenReader with only an XML budget: %v", err)
	}
	// Only the overall budget is named, and it is smaller than the default XML
	// budget; the XML budget is narrowed to match.
	if _, err := seedcalc.OpenReader(bytes.NewReader(b), seedcalc.Options{UnzipSizeLimit: 1 << 20}); err != nil {
		t.Fatalf("OpenReader with only an overall budget: %v", err)
	}

	// The tail is variadic, and several Options values may be passed. They are
	// not merged: the last one wins outright and every earlier one is discarded
	// whole. An inconsistent pair followed by a consistent one therefore opens,
	// because only the second pair is ever looked at.
	if _, err := seedcalc.OpenReader(bytes.NewReader(b),
		seedcalc.Options{UnzipSizeLimit: 1 << 10, UnzipXMLSizeLimit: 1 << 20},
		seedcalc.Options{UnzipSizeLimit: 1 << 24, UnzipXMLSizeLimit: 1 << 20}); err != nil {
		t.Fatalf("OpenReader with a rejected pair followed by an accepted one: %v, want the second pair to win", err)
	}
	// And the same two values in the other order are refused, which they could
	// not be if the pair were being merged or if the first value were kept.
	if _, err := seedcalc.OpenReader(bytes.NewReader(b),
		seedcalc.Options{UnzipSizeLimit: 1 << 24, UnzipXMLSizeLimit: 1 << 20},
		seedcalc.Options{UnzipSizeLimit: 1 << 10, UnzipXMLSizeLimit: 1 << 20}); !errors.Is(err, seedcalc.ErrOptionsUnzipSizeLimit) {
		t.Fatalf("OpenReader with an accepted pair followed by a rejected one returned %v, want %v", err, seedcalc.ErrOptionsUnzipSizeLimit)
	}
}

func TestOpenFileTakesTheSameOptions(t *testing.T) {
	path := filepath.Join(t.TempDir(), "book.xlsx")
	if err := os.WriteFile(path, packed(t), 0o600); err != nil {
		t.Fatalf("WriteFile: %v", err)
	}
	g, err := seedcalc.OpenFile(path, seedcalc.Options{UnzipSizeLimit: 1 << 24, UnzipXMLSizeLimit: 1 << 20})
	if err != nil {
		t.Fatalf("OpenFile with options: %v", err)
	}
	got, err := g.GetCellValue("Sheet1", "A3")
	if err != nil {
		t.Fatalf("GetCellValue: %v", err)
	}
	if got != "3" {
		t.Fatalf("GetCellValue(A3) = %q, want %q", got, "3")
	}
	if _, err := seedcalc.OpenFile(path, seedcalc.Options{UnzipSizeLimit: 1 << 10, UnzipXMLSizeLimit: 1 << 20}); !errors.Is(err, seedcalc.ErrOptionsUnzipSizeLimit) {
		t.Fatalf("OpenFile with an inconsistent pair returned %v, want %v", err, seedcalc.ErrOptionsUnzipSizeLimit)
	}
}

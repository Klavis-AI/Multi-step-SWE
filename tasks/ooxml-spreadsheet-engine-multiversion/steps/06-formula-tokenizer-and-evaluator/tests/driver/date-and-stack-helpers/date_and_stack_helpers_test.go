// Scored group: date-and-stack-helpers.
//
// Two utility surfaces that arrive with the formula engine in this milestone:
// ExcelDateToTime converts a serial date float to a Go time.Time, and the
// Stack data structure (NewStack / Push / Pop / Peek / Len / Empty) is the
// in-process evaluator stack made public. Both are tested through the public
// interface only. Behaviour verified against qax-os/excelize tag v2.2.0
// (date.go, lib.go).
package dateandstackhelpers

import (
	"testing"
	"time"

	seedcalc "__SEEDCALC_MODULE__"
)

func TestExcelDateToTimeConvertsAKnownSerialDate(t *testing.T) {
	// Serial 44000 in the 1900 date system is 2020-06-18.
	got, err := seedcalc.ExcelDateToTime(44000, false)
	if err != nil {
		t.Fatalf("ExcelDateToTime(44000, false): %v", err)
	}
	want := time.Date(2020, time.June, 18, 0, 0, 0, 0, time.UTC)
	if !got.Equal(want) {
		t.Fatalf("ExcelDateToTime(44000, false) = %v, want %v", got, want)
	}
	// use1904Format selects the 1904 date system, whose epoch is 1904-01-01,
	// so the same serial lands on a different day.
	got1904, err := seedcalc.ExcelDateToTime(44000, true)
	if err != nil {
		t.Fatalf("ExcelDateToTime(44000, true): %v", err)
	}
	want1904 := time.Date(2024, time.June, 19, 0, 0, 0, 0, time.UTC)
	if !got1904.Equal(want1904) {
		t.Fatalf("ExcelDateToTime(44000, true) = %v, want %v", got1904, want1904)
	}
	zero1904, err := seedcalc.ExcelDateToTime(0, true)
	if err != nil {
		t.Fatalf("ExcelDateToTime(0, true): %v", err)
	}
	if wantEpoch := time.Date(1904, time.January, 1, 0, 0, 0, 0, time.UTC); !zero1904.Equal(wantEpoch) {
		t.Fatalf("ExcelDateToTime(0, true) = %v, want the 1904 epoch %v", zero1904, wantEpoch)
	}
}

func TestExcelDateToTimeRejectsANegativeValue(t *testing.T) {
	if _, err := seedcalc.ExcelDateToTime(-1, false); err == nil {
		t.Fatal("ExcelDateToTime(-1, false) returned no error; negative serial dates are invalid")
	}
}

func TestExcelDateToTimeAcceptsZeroAndOne(t *testing.T) {
	// Zero and one are valid serial dates in the 1900 system.
	if _, err := seedcalc.ExcelDateToTime(0, false); err != nil {
		t.Fatalf("ExcelDateToTime(0, false): %v", err)
	}
	if _, err := seedcalc.ExcelDateToTime(1, false); err != nil {
		t.Fatalf("ExcelDateToTime(1, false): %v", err)
	}
}

func TestStackStartsEmptyAndReportsCorrectLen(t *testing.T) {
	s := seedcalc.NewStack()
	if !s.Empty() {
		t.Fatal("NewStack().Empty() = false, want true")
	}
	if s.Len() != 0 {
		t.Fatalf("NewStack().Len() = %d, want 0", s.Len())
	}
}

func TestStackPushAndPopAreLastInFirstOut(t *testing.T) {
	s := seedcalc.NewStack()
	s.Push("first")
	s.Push("second")
	if s.Len() != 2 {
		t.Fatalf("Len after two pushes = %d, want 2", s.Len())
	}
	if got := s.Pop(); got != "second" {
		t.Fatalf("first Pop = %v, want %q (LIFO)", got, "second")
	}
	if got := s.Pop(); got != "first" {
		t.Fatalf("second Pop = %v, want %q", got, "first")
	}
	if !s.Empty() {
		t.Fatal("stack not empty after popping all elements")
	}
}

func TestStackPeekDoesNotRemoveTheTop(t *testing.T) {
	s := seedcalc.NewStack()
	s.Push(42)
	top := s.Peek()
	if top != 42 {
		t.Fatalf("Peek() = %v, want 42", top)
	}
	if s.Len() != 1 {
		t.Fatalf("Len after Peek = %d, want 1 (Peek must not remove the element)", s.Len())
	}
}

func TestStackPopAndPeekOnEmptyReturnNil(t *testing.T) {
	s := seedcalc.NewStack()
	if v := s.Pop(); v != nil {
		t.Fatalf("Pop on empty stack = %v, want nil", v)
	}
	if v := s.Peek(); v != nil {
		t.Fatalf("Peek on empty stack = %v, want nil", v)
	}
}

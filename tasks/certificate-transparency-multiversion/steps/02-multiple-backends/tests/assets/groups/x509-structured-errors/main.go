// Group x509-structured-errors.
//
// ct-spec/X509-FORK.md §5.2 and ct-spec/RELEASE-MAP.md §2: at v1.0.5
// `x509/errors.go` adds an `ErrorID` enumeration and
// `func NewError(id ErrorID, args ...interface{}) Error`, giving each
// certificate defect a stable identity rather than only a string.
// TEST_INTERFACE.md §5 states the rest of the surface this group uses --
// `Error{ID, Summary, Fatal}`, `Errors{Errs}` and its six methods, and the two
// defect identities named there -- and nothing outside that statement is
// graded here.
//
// Deliberately NOT graded, with one exception: how an Error or an Errors
// renders. The severity rune, the zero-padded id, the per-defect summary
// wording, `VerboseError`'s parenthesised tail, `ErrCategory.String()` and the
// `Errors:` join are one implementation's presentation choices, not a
// documented boundary. The cases below assert only that a rendering *carries*
// the Summary it was built from -- except case 13, the one-member set, which
// TEST_INTERFACE.md section 5 does pin: a set holding a single member is that
// member's own rendering, so the `Errors:` join must not appear for it.
//
// Everything below is a pure library call: no fixture is parsed, no clock is
// read, no socket is opened. The defect identities are the package's own
// exported constants, so nothing here pins a private table.
package main

import (
	"strings"

	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

func main() {
	t := vhelp.New()

	// 1-3. NewError returns a template Error for a known id: the defect's own
	// identity, the Summary with its arguments formatted in, and the template's
	// fatal flag. TEST_INTERFACE.md §5 names ErrInvalidCertList as fatal.
	fatal := x509.NewError(x509.ErrInvalidCertList, "bad tag")
	t.Equal("new-error-carries-the-id", fatal.ID, x509.ErrInvalidCertList)
	t.Check("new-error-formats-its-arguments-into-the-summary",
		strings.Contains(fatal.Summary, "bad tag"), "Summary=%q", fatal.Summary)
	t.Check("a-fatal-defect-is-marked-fatal", fatal.Fatal,
		"Fatal=false for ErrInvalidCertList")

	// 4. And a non-fatal one is not. TEST_INTERFACE.md §5 names
	// ErrUnexpectedlyCriticalCertListExtension as the non-fatal example.
	warn := x509.NewError(x509.ErrUnexpectedlyCriticalCertListExtension, "sct")
	t.Check("a-non-fatal-defect-is-not-marked-fatal", !warn.Fatal,
		"Fatal=true for ErrUnexpectedlyCriticalCertListExtension")

	// 5. Error() is the error-interface rendering, and whatever else it
	// decorates the message with, the Summary reaches the caller.
	t.Check("error-renders-the-summary",
		strings.Contains(fatal.Error(), fatal.Summary),
		"Error()=%q does not carry Summary=%q", fatal.Error(), fatal.Summary)

	// 6-7. An id past the end of the table is still given an identity and
	// refused as fatal, rather than panicking or returning a zero Error.
	unknown := x509.NewError(x509.ErrMaxID+7, "x")
	t.Equal("an-unknown-id-keeps-its-id", unknown.ID, x509.ErrMaxID+7)
	t.Check("an-unknown-id-is-fatal", unknown.Fatal,
		"Fatal=false for an id past ErrMaxID")

	// 8-11. Errors is the collection: Empty() is true only before anything is
	// added, and AddID() appends exactly the NewError template for the id.
	// The non-fatal member is added first so the fatal one is not at index 0.
	var errs x509.Errors
	t.Check("a-fresh-error-set-is-empty", errs.Empty(), "Empty()=false on a zero Errors")
	errs.AddID(x509.ErrUnexpectedlyCriticalCertListExtension, "sct")
	errs.AddID(x509.ErrInvalidCertList, "bad tag")
	t.Equal("add-id-appends-one-error-per-call", len(errs.Errs), 2)
	t.Check("an-error-set-reports-itself-not-empty", !errs.Empty(),
		"Empty()=true after two AddID calls")
	t.Check("add-id-uses-the-new-error-template",
		len(errs.Errs) == 2 &&
			errs.Errs[0].ID == x509.ErrUnexpectedlyCriticalCertListExtension && !errs.Errs[0].Fatal &&
			errs.Errs[1].ID == x509.ErrInvalidCertList && errs.Errs[1].Fatal,
		"Errs=%v", errs.Errs)

	// 12-13. A set renders every member it holds; a one-member set renders that
	// member and nothing more. How the members are separated is not graded.
	if len(errs.Errs) == 2 {
		set := errs.Error()
		t.Check("a-set-renders-every-member",
			strings.Contains(set, errs.Errs[0].Summary) && strings.Contains(set, errs.Errs[1].Summary),
			"Error()=%q", set)
	} else {
		t.Check("a-set-renders-every-member", false, "wrong member count %d", len(errs.Errs))
	}
	one := x509.Errors{Errs: []x509.Error{warn}}
	t.Equal("a-one-member-set-renders-the-member-alone", one.Error(), warn.Error())

	// 14-16. Fatal() and FirstFatal() agree, and FirstFatal() returns the first
	// fatal member rather than merely a bool. It is deliberately untyped
	// (`error`), so the identity is read back through an Error value.
	ff := errs.FirstFatal()
	t.Check("fatal-is-true-when-a-fatal-member-is-present", errs.Fatal(),
		"Fatal()=false on a set holding ErrInvalidCertList")
	t.Check("fatal-agrees-with-first-fatal", errs.Fatal() == (ff != nil),
		"Fatal()=%v FirstFatal()=%v", errs.Fatal(), ff)
	var ffErr x509.Error
	if ff != nil {
		if err, ok := ff.(x509.Error); ok {
			ffErr = err
		}
	}
	t.Check("first-fatal-returns-the-fatal-member",
		ffErr.ID == x509.ErrInvalidCertList && ffErr.Fatal,
		"FirstFatal() id=%v fatal=%v", ffErr.ID, ffErr.Fatal)

	// 17-18. Filter() drops the named ids and keeps the rest, in order. The
	// filtered id is the last member, so this observes the documented result
	// without depending on how the scan is written.
	kept := errs.Filter([]x509.ErrorID{x509.ErrInvalidCertList})
	t.Equal("filter-drops-the-named-id", len(kept.Errs), 1)
	t.Check("filter-keeps-the-others-in-order",
		len(kept.Errs) == 1 && kept.Errs[0].ID == x509.ErrUnexpectedlyCriticalCertListExtension,
		"kept=%v", kept.Errs)

	t.Done()
}

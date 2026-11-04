// Package errors implements functions to manipulate errors.
package errors

type errorString struct{ s string }

func (e *errorString) Error() string { return e.s }

// New returns an error that formats as the given text.
func New(text string) error { return &errorString{text} }

type wrapError struct {
	msg string
	err error
}

func (e *wrapError) Error() string { return e.msg }
func (e *wrapError) Unwrap() error { return e.err }

type joinError struct{ errs []error }

func (e *joinError) Error() string {
	s := ""
	for i, err := range e.errs {
		if i > 0 {
			s += "\n"
		}
		s += err.Error()
	}
	return s
}
func (e *joinError) Unwrap() []error { return e.errs }

func newWrap(msg string, err error) error { return &wrapError{msg, err} }

// Unwrap returns the result of calling the Unwrap method on err.
func Unwrap(err error) error {
	u, ok := err.(interface{ Unwrap() error })
	if !ok {
		return nil
	}
	return u.Unwrap()
}

// Is reports whether any error in err's tree matches target.
func Is(err, target error) bool {
	if target == nil {
		return err == target
	}
	for {
		if err == target {
			return true
		}
		if x, ok := err.(interface{ Is(error) bool }); ok && x.Is(target) {
			return true
		}
		if m, ok := err.(interface{ Unwrap() []error }); ok {
			for _, e := range m.Unwrap() {
				if Is(e, target) {
					return true
				}
			}
			return false
		}
		err = Unwrap(err)
		if err == nil {
			return false
		}
	}
}

func asAssign(err any, target any) bool

// As finds the first error in err's tree that matches target.
func As(err error, target any) bool {
	if target == nil {
		panic("errors: target cannot be nil")
	}
	for {
		if asAssign(err, target) {
			return true
		}
		if m, ok := err.(interface{ Unwrap() []error }); ok {
			for _, e := range m.Unwrap() {
				if As(e, target) {
					return true
				}
			}
			return false
		}
		err = Unwrap(err)
		if err == nil {
			return false
		}
	}
}

// Join returns an error that wraps the given errors.
func Join(errs ...error) error {
	n := 0
	for _, err := range errs {
		if err != nil {
			n++
		}
	}
	if n == 0 {
		return nil
	}
	e := &joinError{errs: make([]error, 0, n)}
	for _, err := range errs {
		if err != nil {
			e.errs = append(e.errs, err)
		}
	}
	return e
}
